#pragma once
#include <Arduino.h>

// The log: "[  12.345] I scale: ..." - one line per event.
//
// Every line goes three places:
//   - the serial port, as before;
//   - a ring in RAM (PSRAM), which the Logs page reads live;
//   - optionally a file on flash, so that what led up to a fault or a restart
//     can still be read afterwards with no cable attached.
//
// Lines are written to flash by the loop task only, in batches, and never
// while a shot is running: a flash write stalls both cores for milliseconds.
// What a shot logged is written once it is over.

namespace bbw {

enum class LogLevel : uint8_t { ERROR = 0, WARN = 1, INFO = 2, DEBUG = 3 };

struct LogConfig {
  uint8_t level = static_cast<uint8_t>(LogLevel::INFO);  // lines above this are dropped
  bool keepOnFlash = true;
};

constexpr size_t LOG_RING_BYTES = 48 * 1024;      // live view: the last few hundred lines
constexpr size_t LOG_FILE_BYTES = 256 * 1024;     // per file; two are kept (current and previous)
constexpr const char *LOG_FILE = "/log.txt";
constexpr const char *LOG_FILE_OLD = "/log.old";

// Call once from setup(), after the filesystem is mounted. Lines logged
// before it reach the serial port only.
void logBegin(bool filesystemOk);
LogConfig logConfig();
void logSetConfig(const LogConfig &config);  // saved; takes effect at once

void logLine(char level, const char *format, ...) __attribute__((format(printf, 2, 3)));

// Live view. `from` is a position in the stream of everything ever logged;
// 0 starts at the oldest line still held. Copies whole bytes up to `max - 1`,
// terminates, and moves `from` on. Returns the number of bytes copied.
size_t logRead(uint32_t &from, char *out, size_t max);
uint32_t logPosition();

// Loop task. `idle`: no shot is running or being set up.
void logService(bool idle);
// Loop task: write everything still in RAM now (before a restart).
void logFlush();
void logClear();
uint32_t logFlashBytes();  // both files together

}  // namespace bbw

#define LOGE(fmt, ...) bbw::logLine('E', fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) bbw::logLine('W', fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) bbw::logLine('I', fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) bbw::logLine('D', fmt, ##__VA_ARGS__)
