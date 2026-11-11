#pragma once
// Shared internals of the PC emulator. Nothing in here is firmware code.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "emu_http.h"

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
extern std::atomic<bool> g_firmwareReady;  // setup() has finished
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
extern Reset g_resetReason;  // why this controller instance started
// Exit codes the Windows supervisor understands as "restart the controller" (100 + Reset).
constexpr int EXIT_RESTART_BASE = 100;

// What a host other than the Windows emulator supplies. Every hook is optional: a null one keeps
// the default (std::chrono clock, files under g_options.dataDir, no RTC memory, no socket
// listener). Set them before the firmware's setup() runs. The data directory itself is
// g_options.dataDir, which Preferences and LittleFS read on every call.
struct HostHooks {
  // Monotonic milliseconds since the host started; backs millis(). Default: steady_clock.
  uint32_t (*clockMs)() = nullptr;
  // The RTC_NOINIT_ATTR variables' memory. Without it nothing survives a restart.
  void (*rtcRegion)(char *&begin, size_t &size) = nullptr;
  // RTC persistence across a restart. Defaults: <dataDir>/rtc.bin.
  void (*rtcSave)(const char *data, size_t size) = nullptr;
  long (*rtcLoad)(char *data, size_t size) = nullptr;  // bytes read, -1 when nothing was saved
  void (*rtcClear)() = nullptr;                         // forget the saved image
  // Called by restart() after the world and RTC are saved. Return to get the default
  // (fflush + _exit(EXIT_RESTART_BASE + reason)).
  void (*onRestart)(Reset reason) = nullptr;
  // WebServer::begin(): start whatever accepts requests for the device web server and feed them
  // to webDispatch(). Null means in-process only (webEnqueue).
  bool (*webListen)(int port) = nullptr;
};
extern HostHooks g_hooks;
// Reload the RTC variables after a restart (or discard the saved image after a power-on).
void rtcRestore(Reset reason);

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

// The device web server's request queue (host_web.cpp). A request is handled by
// WebServer::handleClient() on the loop task, whichever way it arrived.
struct WebResponse {
  int status = 200;
  std::string contentType = "text/plain";
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};
// In-process request, no socket. `uri` may carry a query string. A body with contentType
// application/x-www-form-urlencoded also supplies arguments; `body` is always available to the
// handler as the "plain" argument. Blocks until the loop task answered, or answers 503 after
// timeoutMs (the request may still run later, as with the socket server).
WebResponse webEnqueue(const std::string &method, const std::string &uri, const std::string &body,
                       const std::string &contentType, int timeoutMs = 10000);
// The same for a parsed request (what the socket server holds).
HttpResponse webDispatch(const HttpRequest &request, int timeoutMs = 10000);
// Windows only (emu_http.cpp): accept loopback connections and feed webDispatch().
bool webServe(int port);

}  // namespace emu
