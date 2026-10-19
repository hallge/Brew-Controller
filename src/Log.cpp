#include "Log.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <atomic>
#include <stdarg.h>

#include "platform/BigMem.h"
#include "platform/FsUtil.h"

namespace bbw {

namespace {

constexpr uint32_t FLUSH_PERIOD_MS = 10000;  // at the latest, while idle
constexpr size_t FLUSH_AT_BYTES = 4096;      // or as soon as this much is waiting

char *g_ring = nullptr;
uint32_t g_written = 0;   // bytes ever put in the ring; guarded by g_mutex
uint32_t g_flushed = 0;   // ...of which this many are on flash; loop task only
SemaphoreHandle_t g_mutex = nullptr;
std::atomic<uint8_t> g_level{static_cast<uint8_t>(LogLevel::INFO)};
std::atomic<bool> g_keep{true};
bool g_fsOk = false;
uint32_t g_fileBytes = 0;  // size of LOG_FILE; loop task only
uint32_t g_lastFlushMs = 0;

uint8_t levelOf(char letter) {
  switch (letter) {
    case 'E': return static_cast<uint8_t>(LogLevel::ERROR);
    case 'W': return static_cast<uint8_t>(LogLevel::WARN);
    case 'D': return static_cast<uint8_t>(LogLevel::DEBUG);
    default: return static_cast<uint8_t>(LogLevel::INFO);
  }
}

uint32_t fileSize(const char *path) {
  if (!g_fsOk || !bbw::fileExists(path)) return 0;
  File file = LittleFS.open(path, "r");
  if (!file) return 0;
  const uint32_t size = static_cast<uint32_t>(file.size());
  file.close();
  return size;
}

// Copies ring bytes [from, to) into out. Caller holds the mutex.
void copyOut(uint32_t from, uint32_t to, char *out) {
  for (uint32_t at = from; at != to; ++at) *out++ = g_ring[at % LOG_RING_BYTES];
}

}  // namespace

void logBegin(bool filesystemOk) {
  g_fsOk = filesystemOk;
  g_ring = bigArray<char>(LOG_RING_BYTES);
  g_mutex = xSemaphoreCreateMutex();
  LogConfig config;
  Preferences prefs;
  if (prefs.begin("bbw", true)) {
    uint8_t stored[2];
    if (prefs.isKey("log") && prefs.getBytesLength("log") == sizeof(stored) &&
        prefs.getBytes("log", stored, sizeof(stored)) == sizeof(stored)) {
      config.level = stored[0] > static_cast<uint8_t>(LogLevel::DEBUG)
                         ? static_cast<uint8_t>(LogLevel::INFO) : stored[0];
      config.keepOnFlash = stored[1] != 0;
    }
    prefs.end();
  }
  g_level = config.level;
  g_keep = config.keepOnFlash;
  g_fileBytes = fileSize(LOG_FILE);
}

LogConfig logConfig() {
  LogConfig config;
  config.level = g_level;
  config.keepOnFlash = g_keep;
  return config;
}

void logSetConfig(const LogConfig &config) {
  g_level = config.level > static_cast<uint8_t>(LogLevel::DEBUG)
                ? static_cast<uint8_t>(LogLevel::DEBUG) : config.level;
  g_keep = config.keepOnFlash;
  Preferences prefs;
  if (prefs.begin("bbw", false)) {
    const uint8_t stored[2] = {g_level.load(), static_cast<uint8_t>(g_keep.load() ? 1 : 0)};
    prefs.putBytes("log", stored, sizeof(stored));
    prefs.end();
  }
}

void logLine(char level, const char *format, ...) {
  if (levelOf(level) > g_level.load()) return;
  char line[224];
  int used = snprintf(line, sizeof(line), "[%8.3f] %c ", millis() / 1000.0, level);
  va_list args;
  va_start(args, format);
  const int body = vsnprintf(line + used, sizeof(line) - used - 1, format, args);
  va_end(args);
  if (body > 0) used += body;
  if (used > static_cast<int>(sizeof(line)) - 2) used = sizeof(line) - 2;  // cut long lines
  line[used++] = '\n';
  line[used] = '\0';
  Serial.print(line);
  if (!g_ring || !g_mutex) return;  // before logBegin(): serial only
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  for (int i = 0; i < used; ++i) g_ring[(g_written + i) % LOG_RING_BYTES] = line[i];
  g_written += static_cast<uint32_t>(used);
  xSemaphoreGive(g_mutex);
}

uint32_t logPosition() {
  if (!g_mutex) return 0;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  const uint32_t position = g_written;
  xSemaphoreGive(g_mutex);
  return position;
}

size_t logRead(uint32_t &from, char *out, size_t max) {
  if (!g_ring || !g_mutex || max < 2) {
    if (max) out[0] = '\0';
    return 0;
  }
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  const uint32_t oldest = g_written > LOG_RING_BYTES ? g_written - LOG_RING_BYTES : 0;
  // Behind the ring, or ahead of it (the controller restarted): start over.
  if (from < oldest || from > g_written) from = oldest;
  uint32_t to = g_written;
  if (to - from > max - 1) to = from + static_cast<uint32_t>(max - 1);
  copyOut(from, to, out);
  xSemaphoreGive(g_mutex);
  const size_t copied = to - from;
  out[copied] = '\0';
  from = to;
  return copied;
}

void logFlush() {
  if (!g_fsOk || !g_ring || !g_mutex) return;
  static char chunk[1024];
  for (;;) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const uint32_t oldest = g_written > LOG_RING_BYTES ? g_written - LOG_RING_BYTES : 0;
    if (g_flushed < oldest) g_flushed = oldest;  // overwritten before it could be saved
    uint32_t to = g_written;
    if (to - g_flushed > sizeof(chunk)) to = g_flushed + sizeof(chunk);
    const size_t count = to - g_flushed;
    copyOut(g_flushed, to, chunk);
    xSemaphoreGive(g_mutex);
    if (count == 0) break;
    if (g_fileBytes + count > LOG_FILE_BYTES) {
      // The current file becomes the previous one; the one before that goes.
      if (bbw::fileExists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
      LittleFS.rename(LOG_FILE, LOG_FILE_OLD);
      g_fileBytes = 0;
    }
    File file = LittleFS.open(LOG_FILE, "a");
    if (!file) break;
    const size_t stored = file.write(reinterpret_cast<const uint8_t *>(chunk), count);
    file.close();
    if (stored != count) break;  // flash full: try again later, nothing is lost from RAM yet
    g_fileBytes += static_cast<uint32_t>(count);
    g_flushed = to;
  }
  g_lastFlushMs = millis();
}

void logService(bool idle) {
  if (!idle || !g_keep.load() || !g_fsOk) return;
  const uint32_t waiting = logPosition() - g_flushed;
  if (waiting == 0) return;
  if (waiting >= FLUSH_AT_BYTES || millis() - g_lastFlushMs >= FLUSH_PERIOD_MS) logFlush();
}

void logClear() {
  if (g_fsOk) {
    if (bbw::fileExists(LOG_FILE)) LittleFS.remove(LOG_FILE);
    if (bbw::fileExists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
  }
  g_fileBytes = 0;
  g_flushed = logPosition();
}

uint32_t logFlashBytes() { return g_fileBytes + fileSize(LOG_FILE_OLD); }

}  // namespace bbw
