// Arduino / FreeRTOS / flash runtime for the PC emulator, and its main().

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>

#include <process.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

#include "emu.h"
#include "emu_radio.h"

namespace fs = std::filesystem;

namespace emu {

Options g_options;
Board g_board;
std::recursive_mutex g_world;
std::atomic<bool> g_logPeriodic{false};

namespace {

const auto g_bootTime = std::chrono::steady_clock::now();
Reset g_resetReason = Reset::POWER_ON;

std::mutex g_logMutex;
std::deque<LogLine> g_log;
uint64_t g_logSeq = 0;

std::mutex g_serialMutex;
std::deque<char> g_serialRx;
std::string g_serialLine;  // partial output line

// Exit codes the supervisor understands as "restart the controller".
constexpr int EXIT_RESTART_BASE = 100;

// RTC_NOINIT_ATTR variables land between these two markers (PE sections
// merge in '$' suffix order).
__attribute__((section(".rtcni$a"), used)) uint32_t g_rtcStart = 0;
__attribute__((section(".rtcni$z"), used)) uint32_t g_rtcEnd = 0;

std::string rtcPath() { return g_options.dataDir + "/rtc.bin"; }

void rtcRegion(char *&begin, size_t &size) {
  begin = reinterpret_cast<char *>(&g_rtcStart + 1);
  char *end = reinterpret_cast<char *>(&g_rtcEnd);
  size = end > begin && end - begin < 4096 ? static_cast<size_t>(end - begin) : 0;
}

}  // namespace

void log(const char *category, const char *fmt, ...) {
  char text[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  const uint32_t ms = millis();
  if (strcmp(category, "serial") != 0) {
    printf("[%8.3f] . %s: %s\n", ms / 1000.0, category, text);
    fflush(stdout);
  }
  std::lock_guard<std::mutex> lock(g_logMutex);
  g_log.push_back({++g_logSeq, ms, category, text});
  if (g_log.size() > 3000) g_log.pop_front();
}

std::vector<LogLine> logSince(uint64_t seq, size_t max) {
  std::lock_guard<std::mutex> lock(g_logMutex);
  std::vector<LogLine> out;
  for (const LogLine &line : g_log) {
    if (line.seq > seq) out.push_back(line);
  }
  if (out.size() > max) out.erase(out.begin(), out.end() - static_cast<long>(max));
  return out;
}

std::string hex(const uint8_t *data, size_t length) {
  std::string out;
  char byte[4];
  for (size_t i = 0; i < length; ++i) {
    snprintf(byte, sizeof(byte), i ? " %02x" : "%02x", data[i]);
    out += byte;
  }
  return out;
}

bool pressed(const std::atomic<bool> &down, const std::atomic<uint32_t> &downAtMs) {
  constexpr uint32_t MIN_PRESS_MS = 90;
  return down || millis() - downAtMs.load() < MIN_PRESS_MS;
}

void serialInject(const std::string &line) {
  std::lock_guard<std::mutex> lock(g_serialMutex);
  for (char c : line) g_serialRx.push_back(c);
  g_serialRx.push_back('\n');
}

void eraseFlash() {
  std::error_code ignored;
  fs::remove_all(g_options.dataDir + "/nvs", ignored);
  fs::remove_all(g_options.dataDir + "/fs", ignored);
}

void restart(Reset reason) {
  // The world (machine, scale, Bot) carries on across a controller restart.
  worldSaveSnapshot();
  if (reason != Reset::POWER_ON) {
    char *begin;
    size_t size;
    rtcRegion(begin, size);
    if (FILE *f = fopen(rtcPath().c_str(), "wb")) {
      fwrite(begin, 1, size, f);
      fclose(f);
    }
  }
  fflush(stdout);
  _exit(EXIT_RESTART_BASE + static_cast<int>(reason));
}

}  // namespace emu

// ================================================================= Arduino ==

extern "C" uint32_t millis(void) {
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - emu::g_bootTime)
                                   .count());
}

extern "C" void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

extern "C" size_t strlcpy(char *dst, const char *src, size_t size) {
  const size_t length = strlen(src);
  if (size) {
    const size_t n = length < size - 1 ? length : size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return length;
}

void pinMode(int, int) {}
int digitalRead(int pin) { const emu::Board &b = emu::g_board;
  return pin == 0 && emu::pressed(b.bootButtonDown, b.bootDownAtMs) ? LOW : HIGH; }
bool setCpuFrequencyMhz(uint32_t mhz) {
  emu::g_board.cpuMhz = static_cast<int>(mhz);
  return true;
}
void configTime(long, int, const char *) {}
int settimeofday(const struct timeval *, const void *) { return 0; }  // the PC clock is already set

esp_reset_reason_t esp_reset_reason() {
  switch (emu::g_resetReason) {
    case emu::Reset::SOFTWARE: return ESP_RST_SW;
    case emu::Reset::PANIC: return ESP_RST_PANIC;
    default: return ESP_RST_POWERON;
  }
}

EspClass ESP;
void EspClass::restart() {
  emu::log("emu", "ESP.restart()");
  emu::restart(emu::Reset::SOFTWARE);
}

size_t Print::printf(const char *fmt, ...) {
  char text[768];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  return n > 0 ? print(text) : 0;
}

HardwareSerial Serial;

int HardwareSerial::available() {
  std::lock_guard<std::mutex> lock(emu::g_serialMutex);
  return static_cast<int>(emu::g_serialRx.size());
}

int HardwareSerial::read() {
  std::lock_guard<std::mutex> lock(emu::g_serialMutex);
  if (emu::g_serialRx.empty()) return -1;
  const char c = emu::g_serialRx.front();
  emu::g_serialRx.pop_front();
  return static_cast<uint8_t>(c);
}

size_t HardwareSerial::write(uint8_t c) { return write(&c, 1); }

size_t HardwareSerial::write(const uint8_t *data, size_t length) {
  fwrite(data, 1, length, stdout);
  std::vector<std::string> lines;
  {
    std::lock_guard<std::mutex> lock(emu::g_serialMutex);
    for (size_t i = 0; i < length; ++i) {
      if (data[i] == '\n') {
        lines.push_back(emu::g_serialLine);
        emu::g_serialLine.clear();
      } else if (data[i] != '\r') {
        emu::g_serialLine += static_cast<char>(data[i]);
      }
    }
  }
  if (!lines.empty()) fflush(stdout);
  for (const std::string &line : lines) {
    // LIVE lines arrive four times a second during a shot.
    if (line.rfind("LIVE,", 0) == 0 && !emu::g_logPeriodic) continue;
    emu::log("serial", "%s", line.c_str());
  }
  return length;
}

// ================================================================ FreeRTOS ==

struct EmuSemaphore {
  std::timed_mutex mutex;
};

struct EmuQueue {
  std::mutex mutex;
  std::deque<std::vector<uint8_t>> items;
  size_t capacity = 0, itemSize = 0;
};

SemaphoreHandle_t xSemaphoreCreateMutex() { return new EmuSemaphore; }

int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
  if (ticks == portMAX_DELAY) {
    semaphore->mutex.lock();
    return pdTRUE;
  }
  if (ticks == 0) return semaphore->mutex.try_lock() ? pdTRUE : pdFALSE;
  return semaphore->mutex.try_lock_for(std::chrono::milliseconds(ticks)) ? pdTRUE : pdFALSE;
}

int xSemaphoreGive(SemaphoreHandle_t semaphore) {
  semaphore->mutex.unlock();
  return pdTRUE;
}

QueueHandle_t xQueueCreate(size_t length, size_t itemSize) {
  EmuQueue *queue = new EmuQueue;
  queue->capacity = length;
  queue->itemSize = itemSize;
  return queue;
}

int xQueueSend(QueueHandle_t queue, const void *item, TickType_t) {
  std::lock_guard<std::mutex> lock(queue->mutex);
  if (queue->items.size() >= queue->capacity) return pdFALSE;
  const uint8_t *bytes = static_cast<const uint8_t *>(item);
  queue->items.emplace_back(bytes, bytes + queue->itemSize);
  return pdTRUE;
}

int xQueueReceive(QueueHandle_t queue, void *item, TickType_t) {
  std::lock_guard<std::mutex> lock(queue->mutex);
  if (queue->items.empty()) return pdFALSE;
  memcpy(item, queue->items.front().data(), queue->itemSize);
  queue->items.pop_front();
  return pdTRUE;
}

int xQueueReset(QueueHandle_t queue) {
  std::lock_guard<std::mutex> lock(queue->mutex);
  queue->items.clear();
  return pdTRUE;
}

TickType_t xTaskGetTickCount() { return millis(); }
void vTaskDelay(TickType_t ticks) { delay(ticks); }

void vTaskDelayUntil(TickType_t *previousWake, TickType_t increment) {
  const TickType_t target = *previousWake + increment;
  const int32_t wait = static_cast<int32_t>(target - millis());
  if (wait > 0) delay(static_cast<uint32_t>(wait));
  // If the PC stalled, do not try to catch up tick by tick.
  *previousWake = wait < -200 ? millis() : target;
}

int xTaskCreatePinnedToCore(void (*task)(void *), const char *, uint32_t, void *arg, int, void *,
                            int) {
  std::thread(task, arg).detach();
  return pdTRUE;
}

void esp_task_wdt_init(int, bool) {}
void esp_task_wdt_add(void *) {
  emu::g_board.watchdogFedMs = millis();
  emu::g_board.watchdogArmed = true;
}
void esp_task_wdt_reset() { emu::g_board.watchdogFedMs = millis(); }

// ============================================================ flash, Wi-Fi ==

bool Preferences::begin(const char *name, bool readOnly) {
  dir_ = emu::g_options.dataDir + "/nvs/" + name;
  std::error_code error;
  if (!readOnly) fs::create_directories(dir_, error);
  return readOnly ? fs::exists(dir_, error) : !error;
}

size_t Preferences::getBytesLength(const char *key) {
  std::error_code error;
  const auto size = fs::file_size(dir_ + "/" + key + ".bin", error);
  return error ? 0 : static_cast<size_t>(size);
}

size_t Preferences::getBytes(const char *key, void *buffer, size_t maxLength) {
  FILE *f = fopen((dir_ + "/" + key + ".bin").c_str(), "rb");
  if (!f) return 0;
  const size_t n = fread(buffer, 1, maxLength, f);
  fclose(f);
  return n;
}

size_t Preferences::putBytes(const char *key, const void *value, size_t length) {
  FILE *f = fopen((dir_ + "/" + key + ".bin").c_str(), "wb");
  if (!f) return 0;
  const size_t n = fwrite(value, 1, length, f);
  fclose(f);
  return n;
}

LittleFSClass LittleFS;

bool LittleFSClass::begin(bool) {
  std::error_code error;
  fs::create_directories(emu::g_options.dataDir + "/fs", error);
  return !error;
}

File LittleFSClass::open(const char *path, const char *mode) {
  // Arduino modes "r", "w", "r+" on binary data.
  const std::string binary = std::string(mode) == "r+" ? "r+b" : std::string(mode) + "b";
  return File(fopen((emu::g_options.dataDir + "/fs" + path).c_str(), binary.c_str()));
}

bool LittleFSClass::exists(const char *path) {
  std::error_code error;
  return fs::exists(emu::g_options.dataDir + "/fs" + path, error);
}

bool LittleFSClass::remove(const char *path) {
  std::error_code error;
  return fs::remove(emu::g_options.dataDir + "/fs" + path, error);
}

bool LittleFSClass::rename(const char *from, const char *to) {
  std::error_code error;
  fs::rename(emu::g_options.dataDir + "/fs" + from, emu::g_options.dataDir + "/fs" + to, error);
  return !error;
}

size_t LittleFSClass::usedBytes() {
  std::error_code error;
  size_t used = 0;
  for (const auto &entry : fs::directory_iterator(emu::g_options.dataDir + "/fs", error)) {
    // A file takes whole 4 kB blocks on the real partition.
    if (entry.is_regular_file(error)) used += (entry.file_size(error) + 4095) / 4096 * 4096;
  }
  return used;
}

WiFiClass WiFi;
MDNSClass MDNS;

void WiFiClass::macAddress(uint8_t *mac) {
  static const uint8_t address[6] = {0x34, 0x85, 0x18, 0xB0, 0xE3, 0x5C};
  memcpy(mac, address, 6);
}

bool WiFiClass::softAP(const char *ssid, const char *password) {
  emu::g_board.apSsid = ssid;
  emu::g_board.apPassword = password;
  emu::log("emu", "Wi-Fi access point '%s' is not emulated; the device web page is at "
                  "http://localhost:%d",
           ssid, emu::g_options.webPort);
  return true;
}

void WiFiClass::begin(const char *ssid, const char *) { emu::g_board.staSsid = ssid; }

// ==================================================================== main ==

namespace {

int runController(emu::Reset reason) {
  emu::g_resetReason = reason;
  emu::g_board.resetReason = reason == emu::Reset::POWER_ON  ? "power on"
                             : reason == emu::Reset::SOFTWARE ? "software reset"
                                                              : "crash (panic)";
  std::error_code ignored;
  fs::create_directories(emu::g_options.dataDir, ignored);

  // RTC memory: kept across a reset, garbage (here: zero) after power-on.
  char *rtc;
  size_t rtcSize;
  emu::rtcRegion(rtc, rtcSize);
  if (reason != emu::Reset::POWER_ON) {
    if (FILE *f = fopen(emu::rtcPath().c_str(), "rb")) {
      if (fread(rtc, 1, rtcSize, f) != rtcSize) memset(rtc, 0, rtcSize);
      fclose(f);
    }
  }
  fs::remove(emu::rtcPath(), ignored);

  emu::log("emu", "controller boot, reset reason: %s", emu::g_board.resetReason);
  emu::worldBegin(true);
  emu::radioBegin();
  emu::apiBegin();

  // Lines typed into the console go to the firmware's serial port.
  std::thread([]() {
    char line[128];
    while (fgets(line, sizeof(line), stdin)) {
      line[strcspn(line, "\r\n")] = '\0';
      emu::serialInject(line);
    }
  }).detach();

  // Task watchdog: report, do not reset (a paused PC is not a hung task).
  std::thread([]() {
    bool reported = false;
    for (;;) {
      delay(500);
      const bool late = emu::g_board.watchdogArmed &&
                        millis() - emu::g_board.watchdogFedMs.load() > 5000;
      if (late && !reported) emu::log("emu", "task watchdog: control task not fed for 5 s");
      reported = late;
    }
  }).detach();

  setup();
  for (;;) loop();
}

}  // namespace

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IOLBF, 4096);
  emu::Options &o = emu::g_options;
  o.exePath = argv[0];
  int child = -1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto value = [&](const char *name) -> const char * {
      const std::string prefix = std::string(name) + "=";
      return arg.rfind(prefix, 0) == 0 ? arg.c_str() + prefix.size() : nullptr;
    };
    if (const char *v = value("--data")) o.dataDir = v;
    else if (const char *v = value("--ui")) o.uiDir = v;
    else if (const char *v = value("--web-port")) o.webPort = atoi(v);
    else if (const char *v = value("--emu-port")) o.emuPort = atoi(v);
    else if (const char *v = value("--controller")) child = atoi(v);
    else {
      printf("usage: bbw_emu [--data=DIR] [--ui=DIR] [--web-port=8080] [--emu-port=8090]\n");
      return 2;
    }
  }
  if (child >= 0) return runController(static_cast<emu::Reset>(child));

  // Supervisor: the controller runs as a child process so that a reset, a
  // crash or a power cycle really starts the firmware from scratch.
  printf("Brew by Weight emulator\n  emulator page  http://localhost:%d\n"
         "  device web UI  http://localhost:%d\n  emulated flash %s\n\n",
         o.emuPort, o.webPort, o.dataDir.c_str());
  int reason = static_cast<int>(emu::Reset::POWER_ON);
  for (;;) {
    std::vector<std::string> args = {std::string("\"") + argv[0] + "\"",
                                     "\"--data=" + o.dataDir + "\"", "\"--ui=" + o.uiDir + "\"",
                                     "--web-port=" + std::to_string(o.webPort),
                                     "--emu-port=" + std::to_string(o.emuPort),
                                     "--controller=" + std::to_string(reason)};
    std::vector<const char *> raw;
    for (const std::string &a : args) raw.push_back(a.c_str());
    raw.push_back(nullptr);
    const intptr_t code = _spawnv(_P_WAIT, argv[0], raw.data());
    if (code < emu::EXIT_RESTART_BASE || code > emu::EXIT_RESTART_BASE + 2) {
      return static_cast<int>(code);
    }
    reason = static_cast<int>(code) - emu::EXIT_RESTART_BASE;
  }
}
