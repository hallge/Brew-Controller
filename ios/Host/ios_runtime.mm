// iOS host hooks for the in-process controller. See ios_runtime.h.
#import <Foundation/Foundation.h>

#include <dlfcn.h>
#include <mach-o/getsect.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "BrewHost.h"
#include "ble_corebluetooth.h"
#include "emu.h"
#include "ios_runtime.h"

namespace {

std::once_flag g_once;
std::string g_dataDir;
uint64_t g_clockBaseNs = 0;

// ---- clock ------------------------------------------------------------------------------------

uint32_t clockMs() {
  return static_cast<uint32_t>((clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) - g_clockBaseNs) / 1000000ull);
}

// ---- RTC memory -----------------------------------------------------------------------------
// The firmware's RTC_NOINIT_ATTR variables live in the __DATA,__bbw_rtc section (emu/shims/Arduino.h).
// The image is found in the image that holds this code (dladdr), which is the app executable, or its
// debug dylib in an Xcode debug build.

void rtcRegion(char *&begin, size_t &size) {
  begin = nullptr;
  size = 0;
  Dl_info info;
  if (!dladdr(reinterpret_cast<const void *>(&bbw_ios_prepare), &info) || !info.dli_fbase) return;
  unsigned long length = 0;
  uint8_t *data = getsectiondata(static_cast<const struct mach_header_64 *>(info.dli_fbase), "__DATA",
                                 "__bbw_rtc", &length);
  if (!data || !length) return;
  begin = reinterpret_cast<char *>(data);
  size = length;
}

std::string rtcPath() { return g_dataDir + "/rtc.bin"; }

void rtcWrite(const char *data, size_t size) {
  const std::string tmp = rtcPath() + ".tmp";
  FILE *f = fopen(tmp.c_str(), "wb");
  if (!f) return;
  const bool ok = fwrite(data, 1, size, f) == size;
  fclose(f);
  if (ok) rename(tmp.c_str(), rtcPath().c_str());
  else unlink(tmp.c_str());
}

long rtcRead(char *data, size_t size) {
  FILE *f = fopen(rtcPath().c_str(), "rb");
  if (!f) return -1;
  const size_t got = fread(data, 1, size, f);
  fclose(f);
  return static_cast<long>(got);
}

// At start: put the saved image back, if there is one of exactly the right size. (The firmware's own
// rtcRestore(POWER_ON) discards nothing here: rtcClear below keeps the file.)
// main.cpp SHOT_IN_PROGRESS_MAGIC: g_shotInProgress holds it while the machine may be brewing.
constexpr uint32_t kShotInProgressMagic = 0xB7E55107;
emu::Reset g_bootReason = emu::Reset::POWER_ON;

// At start: if the saved image (of exactly the right size) shows a shot in progress, the app was
// killed mid-shot: put the image back and start as a software restart, so the firmware's restart
// recovery stops the machine and keeps the shot. Any other launch is a power on with a zeroed image,
// as on the board: a cup believed on the scale hours ago is not believed again.
void rtcRestoreAtStart() {
  char *begin;
  size_t size;
  rtcRegion(begin, size);
  if (!begin) return;
  std::vector<char> image(size);
  bool midShot = false;
  if (rtcRead(image.data(), size) == static_cast<long>(size)) {
    for (size_t at = 0; at + sizeof(uint32_t) <= size; at += sizeof(uint32_t)) {
      uint32_t word;
      memcpy(&word, image.data() + at, sizeof(word));
      if (word == kShotInProgressMagic) midShot = true;
    }
  }
  if (midShot) {
    memcpy(begin, image.data(), size);
    g_bootReason = emu::Reset::SOFTWARE;
  } else {
    memset(begin, 0, size);
  }
}

emu::Reset bootReason() { return g_bootReason; }

// Saves the image to rtc.bin whenever it differs from the last one saved, checked every 100 ms.
void rtcWatcher() {
  char *begin;
  size_t size;
  rtcRegion(begin, size);
  if (!begin) return;
  std::vector<char> last(begin, begin + size);
  std::vector<char> now(size);
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    memcpy(now.data(), begin, size);  // a torn read is harmless: the next pass saves it again
    if (memcmp(now.data(), last.data(), size) != 0) {
      rtcWrite(now.data(), size);
      last = now;
    }
  }
}

// ---- restart, threads ---------------------------------------------------------------------------

// The controller asked to restart: the host shows "tap to restart"; nothing runs the firmware again in
// this process. Never returns, never exits.
void onRestart(emu::Reset) {
  for (;;) std::this_thread::sleep_for(std::chrono::seconds(3600));
}

// FreeRTOS priority to a scheduling class: control (5) user-interactive, BLE (2) user-initiated, the
// rest default. The loop thread is created by BrewHost.cpp and stays at the default.
void taskStarted(const char *, int priority) {
  qos_class_t qos = QOS_CLASS_DEFAULT;
  if (priority >= 5) qos = QOS_CLASS_USER_INTERACTIVE;
  else if (priority >= 2) qos = QOS_CLASS_USER_INITIATED;
  pthread_set_qos_class_self_np(qos, 0);
}

// ---- data folder ------------------------------------------------------------------------------

std::string makeDataDir(const char *override, int demo) {
  NSFileManager *fm = [NSFileManager defaultManager];
  NSString *path;
  if (override && *override) {
    path = [NSString stringWithUTF8String:override];
  } else {
    NSURL *support = [[fm URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask] firstObject];
    path = [[support path] stringByAppendingPathComponent:demo ? @"Controller-demo" : @"Controller"];
  }
  NSDictionary *attrs = @{NSFileProtectionKey : NSFileProtectionCompleteUntilFirstUserAuthentication};
  [fm createDirectoryAtPath:path withIntermediateDirectories:YES attributes:attrs error:nil];
  [fm setAttributes:attrs ofItemAtPath:path error:nil];  // also when it existed already
  return std::string([path fileSystemRepresentation]);
}

void prepareOnce(const char *override, int demo) {
  g_clockBaseNs = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
  g_dataDir = makeDataDir(override, demo);

  emu::g_hooks.clockMs = clockMs;
  emu::g_hooks.rtcRegion = rtcRegion;
  emu::g_hooks.rtcSave = rtcWrite;
  emu::g_hooks.rtcLoad = rtcRead;
  emu::g_hooks.rtcClear = [] {};  // keep rtc.bin: it is the only record of a shot cut short by a kill
  emu::g_hooks.onRestart = onRestart;
  emu::g_hooks.taskStarted = taskStarted;
  emu::g_hooks.bootReason = bootReason;
  emu::g_hooks.webListen = nullptr;  // in process only

  rtcRestoreAtStart();
  std::thread(rtcWatcher).detach();

  bbw_host_configure(g_dataDir.c_str(), demo);
  if (!demo) {
    bbw_host_set_ble_backend(bbw_corebluetooth_backend_create(g_dataDir.c_str(), "com.hallge.brewcontroller.central"));
  }
}

}  // namespace

extern "C" {

void bbw_ios_prepare_at(const char *dataDir, int demo) {
  std::call_once(g_once, [&] { prepareOnce(dataDir, demo); });
}

void bbw_ios_prepare(int demo) { bbw_ios_prepare_at(nullptr, demo); }

const char *bbw_ios_data_dir(void) { return g_dataDir.c_str(); }

}  // extern "C"
