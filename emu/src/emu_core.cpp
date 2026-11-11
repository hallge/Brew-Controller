// The Windows emulator's main(): the restart supervisor (_spawnv) and the PE section that holds
// RTC memory. The platform-neutral runtime is in host_runtime.cpp, the device web queue in
// host_web.cpp.

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>

#include <process.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "emu.h"
#include "emu_radio.h"

namespace fs = std::filesystem;

namespace emu {

namespace {

// RTC_NOINIT_ATTR variables land between these two markers (PE sections
// merge in '$' suffix order).
__attribute__((section(".rtcni$a"), used)) uint32_t g_rtcStart = 0;
__attribute__((section(".rtcni$z"), used)) uint32_t g_rtcEnd = 0;

void windowsRtcRegion(char *&begin, size_t &size) {
  begin = reinterpret_cast<char *>(&g_rtcStart + 1);
  char *end = reinterpret_cast<char *>(&g_rtcEnd);
  size = end > begin && end - begin < 4096 ? static_cast<size_t>(end - begin) : 0;
}

}  // namespace

}  // namespace emu

// ==================================================================== main ==

namespace {

int runController(emu::Reset reason) {
  // The clock is the default (steady_clock since process start); the RTC lives in a PE section
  // and the device web server listens on a loopback socket.
  emu::g_hooks.rtcRegion = emu::windowsRtcRegion;
  emu::g_hooks.webListen = emu::webServe;
  emu::g_resetReason = reason;
  emu::g_board.resetReason = reason == emu::Reset::POWER_ON  ? "power on"
                             : reason == emu::Reset::SOFTWARE ? "software reset"
                                                              : "crash (panic)";
  std::error_code ignored;
  fs::create_directories(emu::g_options.dataDir, ignored);

  emu::rtcRestore(reason);

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
  emu::g_firmwareReady = true;
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
