#pragma once
// Grind recommendations in the manner of Mahlkoenig Grind-by-Sync, for a
// grinder whose burr distance is set by hand in microns (E64 WS).
//
// What Mahlkoenig documents (Software Manual Grind-by-Sync v1.36, Sync Scale
// user manual, Sync System technical FAQ) and what is done here:
//   * A recipe has a target brew time. The grinder compares the moving
//     average of actual brew times with it.            -> averageTimeS vs target
//   * "GbS Brew Time Tolerance": inside it the cycle counts as correct and
//     nothing changes.                                 -> toleranceS
//   * Too fast means too coarse: go finer. Too slow: go coarser.
//   * A brew event only counts if it lasted more than 15 s and less than
//     target + 15 s, and was not interfered with.      -> grindShotValid()
//
// What Mahlkoenig does not publish: how many shots the average covers and how
// many microns it moves per second of error. Here the window is a setting,
// and the microns-per-second figure is measured from the user's own history
// (how brew time moved when the grind setting changed), with a configurable
// starting guess until there is enough history.
//
// Portable and host-tested; no flash or UI access in here.

#include <cmath>
#include <stddef.h>
#include <stdint.h>

#include "Types.h"

namespace bbw {

constexpr float GRIND_MIN_VALID_TIME_S = 15.0f;       // Sync Scale manual
constexpr float GRIND_MAX_OVER_TARGET_S = 15.0f;      // Sync Scale manual
constexpr float GRIND_MIN_UM_PER_S = 0.5f;            // plausibility bounds for the
constexpr float GRIND_MAX_UM_PER_S = 20.0f;           // learned sensitivity
constexpr float GRIND_MIN_LEARN_SPAN_UM = 4.0f;
constexpr size_t GRIND_MIN_LEARN_SHOTS = 3;

struct GrindConfig {
  float toleranceS = 2.0f;      // +- around the target brew time
  float defaultUmPerS = 3.0f;   // starting guess: microns per second of error
  uint8_t window = 3;           // shots in the moving average
  uint8_t maxStepUm = 20;       // largest single recommendation
};

// A shot this far from the others at the same setting is treated as a one-off
// (uneven puck, channel, mis-dose) and left out of the average: twice the
// tolerance, and never less than 4 s.
inline float grindOutlierS(const GrindConfig &config) {
  const float twice = 2.0f * config.toleranceS;
  return twice > 4.0f ? twice : 4.0f;
}
constexpr size_t GRIND_OUTLIER_MIN_SHOTS = 3;   // fewer than this: nothing to compare with
constexpr size_t GRIND_OUTLIER_LOOKBACK = 8;    // shots at the setting that form the reference

struct GrindShot {
  float timeS = 0.0f;
  uint16_t grindUm = 0;
  bool forced = false;  // the user said "count this one": never set aside as a one-off
};

enum class GrindAction : uint8_t {
  NO_GRIND_SET,   // the current grinder setting has not been entered
  NO_DATA,        // no valid shot at the current setting yet
  HOLD,           // inside tolerance
  FINER,
  COARSER,
  // Shots at this setting ran both clearly fast and clearly slow. Their
  // average says nothing about the grind, so no advice is given.
  INCONSISTENT,
};

inline const char *grindActionName(GrindAction a) {
  switch (a) {
    case GrindAction::NO_GRIND_SET: return "no_grind_set";
    case GrindAction::NO_DATA: return "no_data";
    case GrindAction::HOLD: return "hold";
    case GrindAction::FINER: return "finer";
    case GrindAction::COARSER: return "coarser";
    case GrindAction::INCONSISTENT: return "inconsistent";
  }
  return "unknown";
}

struct GrindAdvice {
  GrindAction action = GrindAction::NO_GRIND_SET;
  int16_t deltaUm = 0;         // negative = finer
  uint16_t currentUm = 0;
  uint16_t suggestedUm = 0;
  float targetTimeS = 0.0f;
  float averageTimeS = 0.0f;   // over shotsUsed shots at the current setting
  float errorS = 0.0f;         // average - target; positive = running slow
  uint8_t shotsUsed = 0;
  float umPerS = 0.0f;         // sensitivity applied
  bool learned = false;        // true: measured from history, false: starting guess
  uint8_t learnShots = 0;      // shots behind the measured sensitivity
  bool stepLimited = false;    // the full correction exceeded maxStepUm
  uint8_t outliersIgnored = 0; // one-off shots left out of the average
  bool newestIgnored = false;  // ...and the most recent shot was one of them
  float newestTimeS = 0.0f;    // brew time of the most recent shot at this setting
  float minTimeS = 0.0f;       // fastest and slowest of the shots used
  float maxTimeS = 0.0f;
};

// The duration of a stored shot on the chosen basis.
inline float grindShotTime(const ShotRecord &s, GrindTimeBasis basis) {
  return basis == GrindTimeBasis::POUR ? s.pourTimeS : s.shotTimeS;
}

// Why a stored shot is, or is not, used for grind advice.
enum class GrindUse : uint8_t {
  COUNTED,          // used
  IGNORED_BY_USER,  // the user ruled it out
  NO_GRIND,         // no grind setting recorded with it
  NO_TIME,          // no usable duration (pour time needs first drops)
  OTHER_BASIS,      // its target time was set on the other time basis
  NOT_WEIGHT_STOP,  // stopped by hand, by a fault, or it ran out of time
  NOT_SETTLED,      // the final weight never settled (cup lifted)
  STOP_RETRIED,     // the stop had to be pressed more than once
  TOO_SHORT,        // under 15 s: a flush or a gusher
  TOO_LONG,         // more than 15 s over the target
};

// Mahlkoenig's "valid brew event", applied to a stored shot. A shot the
// controller had to stop for any reason other than reaching the weight, or
// whose final weight never settled, is an interfered event. The 15 s floor
// separates shots from flushes and applies to the whole run; the ceiling is
// relative to the target and uses the same basis as the target.
inline GrindUse grindShotUse(const ShotRecord &s, GrindTimeBasis basis = GrindTimeBasis::TOTAL) {
  const float time = grindShotTime(s, basis);
  // The user's ruling comes first, either way. A counted shot still needs a
  // grind setting and a time to be of any use.
  if (s.grindVote == static_cast<uint8_t>(GrindVote::IGNORE)) return GrindUse::IGNORED_BY_USER;
  if (s.grindUm == 0) return GrindUse::NO_GRIND;
  if (!(time > 0.0f)) return GrindUse::NO_TIME;
  if (s.grindVote == static_cast<uint8_t>(GrindVote::COUNT)) return GrindUse::COUNTED;
  const bool weightCut = s.endReason == static_cast<uint8_t>(EndReason::WEIGHT) ||
                         s.endReason == static_cast<uint8_t>(EndReason::WEIGHT_PREDICTED);
  if (!weightCut) return GrindUse::NOT_WEIGHT_STOP;
  if ((s.flags & SHOT_FLAG_FINAL_VALID) == 0) return GrindUse::NOT_SETTLED;
  if ((s.flags & SHOT_FLAG_STOP_RETRIED) != 0) return GrindUse::STOP_RETRIED;
  if (!(s.targetTimeS > 0.0f) || s.timeBasis != static_cast<uint8_t>(basis)) {
    return GrindUse::OTHER_BASIS;  // target was set on the other basis
  }
  if (!(s.shotTimeS > GRIND_MIN_VALID_TIME_S)) return GrindUse::TOO_SHORT;
  if (!(time < s.targetTimeS + GRIND_MAX_OVER_TARGET_S)) return GrindUse::TOO_LONG;
  return GrindUse::COUNTED;
}

inline bool grindShotValid(const ShotRecord &s, GrindTimeBasis basis = GrindTimeBasis::TOTAL) {
  return grindShotUse(s, basis) == GrindUse::COUNTED;
}

// How many microns one second of brew time is worth on this grinder, coffee
// and recipe: least squares of brew time against grind setting. Coarser must
// mean faster; anything else is noise and is rejected.
inline bool learnGrindSensitivity(const GrindShot *shots, size_t count, float &umPerS) {
  if (!shots || count < GRIND_MIN_LEARN_SHOTS) return false;
  double meanX = 0, meanY = 0;
  uint16_t lo = 0xFFFF, hi = 0;
  for (size_t i = 0; i < count; ++i) {
    meanX += shots[i].grindUm;
    meanY += shots[i].timeS;
    if (shots[i].grindUm < lo) lo = shots[i].grindUm;
    if (shots[i].grindUm > hi) hi = shots[i].grindUm;
  }
  if (hi - lo < GRIND_MIN_LEARN_SPAN_UM) return false;
  meanX /= count;
  meanY /= count;
  double cov = 0, var = 0;
  for (size_t i = 0; i < count; ++i) {
    const double dx = shots[i].grindUm - meanX;
    cov += dx * (shots[i].timeS - meanY);
    var += dx * dx;
  }
  if (var <= 0.0) return false;
  const double secondsPerUm = cov / var;
  if (!(secondsPerUm < 0.0)) return false;
  const double result = -1.0 / secondsPerUm;
  if (!std::isfinite(result) || result < GRIND_MIN_UM_PER_S || result > GRIND_MAX_UM_PER_S) {
    return false;
  }
  umPerS = static_cast<float>(result);
  return true;
}

// shots: valid shots of one recipe, newest first.
inline GrindAdvice adviseGrind(const GrindShot *shots, size_t count, uint16_t currentUm,
                               float targetTimeS, const GrindConfig &config) {
  GrindAdvice advice;
  advice.currentUm = advice.suggestedUm = currentUm;
  advice.targetTimeS = targetTimeS;
  advice.umPerS = config.defaultUmPerS;
  if (currentUm == 0) return advice;

  float learned = 0.0f;
  if (learnGrindSensitivity(shots, count, learned)) {
    advice.umPerS = learned;
    advice.learned = true;
    advice.learnShots = count > 255 ? 255 : static_cast<uint8_t>(count);
  }

  // The newest shots made at the current setting. A shot at another setting
  // ends the run: it says nothing about this one.
  float run[GRIND_OUTLIER_LOOKBACK];
  bool forced[GRIND_OUTLIER_LOOKBACK];
  size_t runCount = 0;
  for (size_t i = 0; i < count && runCount < GRIND_OUTLIER_LOOKBACK; ++i) {
    if (shots[i].grindUm != currentUm) break;
    forced[runCount] = shots[i].forced;
    run[runCount++] = shots[i].timeS;
  }

  // Do not blame the grind for a one-off. With three or more shots at this
  // setting, one that sits far from their median is left out. Two shots that
  // agree with each other move the median, so a real, lasting change still
  // gets through after one more shot.
  float median = 0.0f;
  bool screen = runCount >= GRIND_OUTLIER_MIN_SHOTS;
  const float outlierS = grindOutlierS(config);
  if (screen) {
    float sorted[GRIND_OUTLIER_LOOKBACK];
    for (size_t i = 0; i < runCount; ++i) sorted[i] = run[i];
    for (size_t i = 1; i < runCount; ++i) {
      const float v = sorted[i];
      size_t j = i;
      while (j > 0 && sorted[j - 1] > v) { sorted[j] = sorted[j - 1]; --j; }
      sorted[j] = v;
    }
    median = (runCount % 2) ? sorted[runCount / 2]
                            : 0.5f * (sorted[runCount / 2 - 1] + sorted[runCount / 2]);
    // "One-off" only means something when most shots agree. If they are
    // split or scattered, the median describes none of them: use them all.
    size_t agreeing = 0;
    for (size_t i = 0; i < runCount; ++i) agreeing += fabsf(run[i] - median) <= outlierS;
    screen = agreeing * 2 > runCount;
  }

  if (runCount > 0) advice.newestTimeS = run[0];

  // Moving average over what is left.
  const uint8_t window = config.window < 1 ? 1 : config.window;
  float sum = 0.0f;
  for (size_t i = 0; i < runCount && advice.shotsUsed < window; ++i) {
    if (screen && !forced[i] && fabsf(run[i] - median) > outlierS) {
      ++advice.outliersIgnored;
      if (i == 0) advice.newestIgnored = true;
      continue;
    }
    sum += run[i];
    if (advice.shotsUsed == 0 || run[i] < advice.minTimeS) advice.minTimeS = run[i];
    if (advice.shotsUsed == 0 || run[i] > advice.maxTimeS) advice.maxTimeS = run[i];
    ++advice.shotsUsed;
  }
  if (advice.shotsUsed == 0) {
    advice.action = GrindAction::NO_DATA;
    return advice;
  }
  advice.averageTimeS = sum / advice.shotsUsed;
  advice.errorS = advice.averageTimeS - targetTimeS;
  // One shot outside the tolerance on the fast side and another on the slow
  // side, at the same setting: averaging them would report "on target" (or a
  // small correction) for shots none of which were. Something other than the
  // grind is varying, or the grinder was changed without the new setting
  // being entered.
  if (advice.minTimeS < targetTimeS - config.toleranceS &&
      advice.maxTimeS > targetTimeS + config.toleranceS) {
    advice.action = GrindAction::INCONSISTENT;
    return advice;
  }
  if (fabsf(advice.errorS) <= config.toleranceS) {
    advice.action = GrindAction::HOLD;
    return advice;
  }

  // Slow shot (positive error): open the burrs. Fast shot: close them.
  float delta = advice.errorS * advice.umPerS;
  const float limit = config.maxStepUm < 1 ? 1.0f : config.maxStepUm;
  if (fabsf(delta) > limit) {
    delta = delta > 0 ? limit : -limit;
    advice.stepLimited = true;
  }
  int rounded = static_cast<int>(delta > 0 ? delta + 0.5f : delta - 0.5f);
  if (rounded == 0) rounded = delta > 0 ? 1 : -1;  // outside tolerance: move at least 1 um
  int suggested = static_cast<int>(currentUm) + rounded;
  if (suggested < 1) suggested = 1;
  advice.deltaUm = static_cast<int16_t>(suggested - static_cast<int>(currentUm));
  advice.suggestedUm = static_cast<uint16_t>(suggested);
  advice.action = advice.deltaUm < 0 ? GrindAction::FINER : GrindAction::COARSER;
  return advice;
}

}  // namespace bbw
