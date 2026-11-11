// The firmware's screen (src/ui/Ui.h) with no LVGL: the app draws the screen itself. What the
// firmware needs from it is only whether the screen is "off" (so the loop idles) and wake-ups.
#include <atomic>
#include <mutex>
#include <string>

#include "BrewHost.h"
#include "HostInternal.h"
#include "ui/Ui.h"

namespace {
std::atomic<bool> g_foreground{true};
std::atomic<uint32_t> g_wakes{0};
std::mutex g_lineMutex;
std::string g_line;
}  // namespace

namespace bbwhost {
void setForeground(bool foreground) { g_foreground = foreground; }
bool foreground() { return g_foreground; }
void noteWake() { ++g_wakes; }
uint32_t wakeCount() { return g_wakes; }
}  // namespace bbwhost

extern "C" void bbw_set_foreground(int foreground) { bbwhost::setForeground(foreground != 0); }

namespace ui {

bool begin() { return true; }
void loop() {}
void wake() { bbwhost::noteWake(); }
// Off while the app is in the background: the control loop then sleeps 25 ms per turn.
bool isOff() { return !bbwhost::foreground(); }
// The board's BOOT button does not exist on a phone.
void bootShortPress() {}
const char *wifiLine() {
  static thread_local std::string copy;
  std::lock_guard<std::mutex> lock(g_lineMutex);
  copy = g_line;
  return copy.c_str();
}
void setWifiLine(const char *text) {
  std::lock_guard<std::mutex> lock(g_lineMutex);
  g_line = text ? text : "";
}

}  // namespace ui
