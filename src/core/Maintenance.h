#pragma once
// Upkeep counters: how many shots since the machine was last backflushed or descaled and the grinder
// last cleaned, and a reminder when the interval the owner chose has passed.
//
// Nothing is inferred. The controller cannot see scale in the machine or dull burrs from the cup (see
// docs/FEATURES.md 14 and 15), so it counts what it can count and lets the owner decide the intervals;
// none is preset. OBBW keeps a similar history of activations (shot, rinse, backflush, ...).
//
// Portable and host-tested; no flash or UI access in here.

#include <stddef.h>
#include <stdint.h>

#include "Types.h"

namespace bbw {

enum class Upkeep : uint8_t { BACKFLUSH = 0, DESCALE = 1, GRINDER_CLEAN = 2 };
constexpr size_t UPKEEP_COUNT = 3;
constexpr uint16_t UPKEEP_MAX_EVERY = 5000;

struct Maintenance {
  uint32_t shotsTotal = 0;                    // shots counted since this was first used
  uint32_t doneAtShots[UPKEEP_COUNT] = {};    // shotsTotal when each job was last done
  uint32_t doneAtEpochS[UPKEEP_COUNT] = {};   // and when, 0 = not known
  uint16_t everyShots[UPKEEP_COUNT] = {};     // shots between reminders; 0 = no reminder
};

inline const char *upkeepName(Upkeep job) {
  switch (job) {
    case Upkeep::BACKFLUSH: return "backflush";
    case Upkeep::DESCALE: return "descale";
    case Upkeep::GRINDER_CLEAN: return "grinder_clean";
  }
  return "";
}

// false for a name that is not a job.
inline bool upkeepFromName(const char *name, Upkeep &out) {
  for (size_t i = 0; i < UPKEEP_COUNT; ++i) {
    const char *n = upkeepName(static_cast<Upkeep>(i));
    size_t k = 0;
    while (n[k] && name && name[k] == n[k]) ++k;
    if (name && n[k] == '\0' && name[k] == '\0') {
      out = static_cast<Upkeep>(i);
      return true;
    }
  }
  return false;
}

// A shot worth counting: not a rinse or a flush (the same line OBBW draws for its history).
inline bool upkeepCountsShot(const ShotRecord &s) { return s.shotTimeS >= 12.0f && s.finalWeightG >= 2.0f; }

inline void sanitizeMaintenance(Maintenance &m) {
  for (size_t i = 0; i < UPKEEP_COUNT; ++i) {
    if (m.doneAtShots[i] > m.shotsTotal) m.doneAtShots[i] = m.shotsTotal;
    if (m.everyShots[i] > UPKEEP_MAX_EVERY) m.everyShots[i] = UPKEEP_MAX_EVERY;
  }
}

inline void countUpkeepShot(Maintenance &m) {
  if (m.shotsTotal < 0xFFFFFFF0u) ++m.shotsTotal;
}

inline uint32_t shotsSince(const Maintenance &m, Upkeep job) {
  const size_t i = static_cast<size_t>(job);
  return m.shotsTotal >= m.doneAtShots[i] ? m.shotsTotal - m.doneAtShots[i] : 0;
}

inline bool upkeepDue(const Maintenance &m, Upkeep job) {
  const uint16_t every = m.everyShots[static_cast<size_t>(job)];
  return every != 0 && shotsSince(m, job) >= every;
}

inline uint8_t upkeepDueCount(const Maintenance &m) {
  uint8_t n = 0;
  for (size_t i = 0; i < UPKEEP_COUNT; ++i) n += upkeepDue(m, static_cast<Upkeep>(i)) ? 1 : 0;
  return n;
}

inline void markUpkeepDone(Maintenance &m, Upkeep job, uint32_t epochS) {
  const size_t i = static_cast<size_t>(job);
  m.doneAtShots[i] = m.shotsTotal;
  m.doneAtEpochS[i] = epochS;
}

inline void setUpkeepEvery(Maintenance &m, Upkeep job, uint32_t everyShots) {
  m.everyShots[static_cast<size_t>(job)] =
      static_cast<uint16_t>(everyShots > UPKEEP_MAX_EVERY ? UPKEEP_MAX_EVERY : everyShots);
}

}  // namespace bbw
