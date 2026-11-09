#pragma once
// Which job was running when chip RAM reached a new low (9 Nov 2026).
//
// The board's chip RAM low fell over a week from 23 kB to 10.5 kB, each new
// low around a shot or at night with nothing logged. The free figure itself
// stayed level, so it is short peaks, and the health report only saw them ten
// minutes later. Each job marks itself while it runs (RamJobScope); the loop
// samples the low often (ramWatchSample), and a new low is put down to every
// job seen running since the sample before. Any task may mark a job.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <atomic>

namespace bbw {

enum RamJob : uint16_t {
  RAM_UI = 1u << 0,
  RAM_WEB = 1u << 1,
  RAM_SHOT_SAVE = 1u << 2,
  RAM_ADVICE = 1u << 3,
  RAM_UPDATE = 1u << 4,     // firmware update check or install
  RAM_SHARE = 1u << 5,      // sharing, review status, the library
  RAM_DIAG = 1u << 6,       // diagnostics reports
  RAM_SUBS = 1u << 7,       // subscriptions
  RAM_HA = 1u << 8,         // Home Assistant: pull, webhook, MQTT
  RAM_BLE_SCALE = 1u << 9,  // connecting the scale
  RAM_BLE_BOT = 1u << 10,   // connecting a bot (brew, power or stop)
  RAM_BREWING = 1u << 11,   // a shot is running
  RAM_LOG = 1u << 12,       // log to flash
  RAM_ADDON = 1u << 13,     // an add-on (docs/ADDONS.md): its tick, routes and network jobs
};

constexpr const char *RAM_JOB_NAMES[] = {"ui",      "web",    "shot save", "advice", "update",
                                         "sharing", "diag",   "subs",      "ha",     "ble scale",
                                         "ble bot", "brewing", "log",   "addon"};

inline std::atomic<uint16_t> &ramJobsActive() {
  static std::atomic<uint16_t> active{0};
  return active;
}
inline std::atomic<uint16_t> &ramJobsSeen() {
  static std::atomic<uint16_t> seen{0};
  return seen;
}

inline void ramJobBegin(uint16_t job) {
  ramJobsActive().fetch_or(job);
  ramJobsSeen().fetch_or(job);
}
inline void ramJobEnd(uint16_t job) { ramJobsActive().fetch_and(static_cast<uint16_t>(~job)); }

struct RamJobScope {
  explicit RamJobScope(uint16_t job) : job_(job) { ramJobBegin(job); }
  ~RamJobScope() { ramJobEnd(job_); }
  RamJobScope(const RamJobScope &) = delete;
  RamJobScope &operator=(const RamJobScope &) = delete;

 private:
  uint16_t job_;
};

// "web+ble bot"; "none" when no job was marked.
inline void ramJobNames(uint16_t jobs, char *out, size_t size) {
  if (!out || size == 0) return;
  out[0] = '\0';
  size_t used = 0;
  for (size_t i = 0; i < sizeof(RAM_JOB_NAMES) / sizeof(RAM_JOB_NAMES[0]) && used < size; ++i) {
    if ((jobs & (1u << i)) == 0) continue;
    const int n = snprintf(out + used, size - used, "%s%s", used ? "+" : "", RAM_JOB_NAMES[i]);
    if (n < 0) break;
    used += static_cast<size_t>(n);
  }
  if (!out[0]) snprintf(out, size, "none");
}

struct RamLow {
  uint32_t lowest = 0;    // bytes; 0 until the first sample
  uint32_t drop = 0;      // how far the last new low went below the one before
  uint32_t uptimeS = 0;   // when it was seen
  uint16_t jobs = 0;      // the jobs seen running since the sample before
};

// lowestNow: the chip's low since start-up, as it reads now. True when it is a new low (after the
// first sample): `low` then holds it and the jobs behind it. The jobs seen are reset to those still
// running, either way.
inline bool ramWatchSample(RamLow &low, uint32_t lowestNow, uint32_t uptimeS) {
  const uint16_t seen = ramJobsSeen().exchange(ramJobsActive().load());
  if (low.lowest == 0) {
    low.lowest = lowestNow;
    return false;
  }
  if (lowestNow >= low.lowest) return false;
  low.drop = low.lowest - lowestNow;
  low.lowest = lowestNow;
  low.uptimeS = uptimeS;
  low.jobs = seen;
  return true;
}

}  // namespace bbw
