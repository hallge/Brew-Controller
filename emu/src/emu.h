#pragma once
// Shared internals of the PC emulator. Nothing in here is firmware code.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace emu {

struct Options {
  std::string dataDir = "emu_data";  // emulated flash (NVS + LittleFS)
  std::string uiDir = "emu/ui";      // emulator page
  int webPort = 8080;                // the firmware's own web interface
  int emuPort = 8090;                // emulator page and control API
  std::string exePath;
};
extern Options g_options;

// Guards the whole emulated world (scale, Bot, machine). Firmware callbacks
// are never invoked while it is held.
extern std::recursive_mutex g_world;

// Emulator log, shown on the page. Categories: serial, ble, scale, bot,
// machine, emu.
void log(const char *category, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
std::string hex(const uint8_t *data, size_t length);
struct LogLine {
  uint64_t seq;
  uint32_t ms;
  std::string category, text;
};
std::vector<LogLine> logSince(uint64_t seq, size_t max);
extern std::atomic<bool> g_logPeriodic;  // also log heartbeats and weight packets

// The board itself.
struct Board {
  std::atomic<int> brightness{0};
  std::atomic<int> cpuMhz{240};
  std::atomic<int> rotation{0};
  std::atomic<bool> bootButtonDown{false};
  std::atomic<int> touchX{0}, touchY{0};
  std::atomic<bool> touchDown{false};
  // A quick click must still be seen by the firmware's polling: presses are
  // held for a minimum time (see pressed()).
  std::atomic<uint32_t> touchDownAtMs{0}, bootDownAtMs{0};
  std::atomic<uint32_t> watchdogFedMs{0};
  std::atomic<bool> watchdogArmed{false};
  std::string apSsid, apPassword, staSsid;
  const char *resetReason = "power on";
};
extern Board g_board;
bool pressed(const std::atomic<bool> &down, const std::atomic<uint32_t> &downAtMs);

enum class Reset { POWER_ON, SOFTWARE, PANIC };
[[noreturn]] void restart(Reset reason);
void eraseFlash();
void serialInject(const std::string &line);
// The touch key on the glass beside the USB-C connector.
void touchKey(bool down);

// Display: copies the current frame as RGBA when it is newer than `since`.
bool frameCopy(uint64_t since, std::vector<uint8_t> &rgba, uint64_t &counter, int &width,
               int &height);

// World (emu_devices.cpp)
void worldBegin(bool restoreSnapshot);
void worldSaveSnapshot();
std::string worldStateJson();
bool worldSet(const std::string &device, const std::string &key, const std::string &value);

// Control API + page (emu_api.cpp)
void apiBegin();

}  // namespace emu
