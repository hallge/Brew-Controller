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

#include "GrinderRegistry.h"
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
  // Everything below is in the unit the settings are stored in (the grinder's smallest step). The
  // defaults are the E64's microns; grindConfigForGrinder converts them for other grinders. The
  // controller always builds its config with grindConfigForGrinder; a default-constructed config is
  // the original rule, kept for the tests and for comparison.
  float minUmPerS = GRIND_MIN_UM_PER_S;        // a learned sensitivity outside these is noise
  float maxUmPerS = GRIND_MAX_UM_PER_S;
  float minLearnSpan = GRIND_MIN_LEARN_SPAN_UM;
  // Learning the grinder (see the section of that name); set by grindConfigForGrinder.
  float priorCv = 0.0f;         // > 0: a measured sensitivity is weighed against the starting guess
  float noiseS = 1.5f;          //   ...assuming this shot-to-shot noise
  bool bracket = false;         // never step past a setting already seen on the other side
  uint8_t guessSource = 0;      // SensitivitySource of defaultUmPerS
};

// Where the sensitivity applied came from.
enum class SensitivitySource : uint8_t {
  GUESS = 0,      // the starting guess in the settings
  GRINDER = 1,    // learned from this grinder's other coffees
  MEASURED = 2,   // measured from this coffee's own shots
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
  // What the owner said of it afterwards (ShotRecord::rating, ::taste); 0 = nothing.
  uint8_t rating = 0;   // 1..5 stars
  uint8_t taste = 0;    // Taste: 1 sour, 2 balanced, 3 bitter
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

struct GrindAdvice;
inline void keepAdviceInRange(GrindAdvice &advice, const StepRange &range);

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

// ------------------------------------------- refining by the owner's ratings --
// The advice above is Mahlkoenig Grind-by-Sync's idea: steer the grind by how far the moving
// average of measured brew times is from the recipe's target time, inside a tolerance. That
// stays the base and the default, and it is always worked out first, from the shots only.
//
// Ratings add to it, never replace it. Once the owner has rated shots of this coffee, the
// ones they LIKED say where inside the tolerance the good shots fall. When the base advice
// says "keep", and enough liked shots agree, the advice may move the grind a little to bring
// the average onto the liked time: a tighter target range inside the base one. It cannot
// widen the range or override the base: a liked time outside the tolerance is ignored, and a
// base verdict of finer or coarser stands as it is.
//
// What research could and could not confirm (research/assumptions-validation.md):
//   * Mahlkoenig publishes neither its tolerance, the length of its average nor its step, so
//     none of those is borrowed from it; they are this project's settings.
//   * No published study gives a liking threshold or a test-retest figure for coffee ratings.
//     Related sensory work finds individual hedonic ratings noisy (replicate correlations
//     0.32-0.75 for the same juice), which supports using several shots and a median, not any
//     particular number. RATED_LIKED_MIN (4 of 5 stars) is a convention; RATED_MIN_SHOTS (3)
//     is the smallest sample whose median ignores one odd shot.
//   * So the width of the tighter range is not a constant but follows the owner's own data:
//     the standard error of the median of the liked times (floored at RATED_BAND_FLOOR_S, below
//     which shot-to-shot noise makes a difference meaningless) and never wider than the base
//     tolerance.
constexpr uint8_t RATED_LIKED_MIN = 4;
constexpr size_t RATED_MIN_SHOTS = 3;
constexpr float RATED_BAND_FLOOR_S = 0.5f;

enum class RatedState : uint8_t {
  OFF = 0,        // not asked for
  TOO_FEW,        // fewer than RATED_MIN_SHOTS liked shots
  OUTSIDE_BAND,   // their time is outside the base tolerance: ignored, the base advice stands
  BASE_MOVING,    // the base advice is saying finer or coarser; ratings wait
  REFINING,       // keep per the base, but the average is off the liked time: a small move
  ON_LIKED,       // keep per the base, and the average is on the liked time
  BETWEEN_STEPS,  // the base kept the closer of two neighbouring steps: no finer aim is reachable
};

inline const char *ratedStateName(RatedState s) {
  switch (s) {
    case RatedState::TOO_FEW: return "too_few";
    case RatedState::OUTSIDE_BAND: return "outside_band";
    case RatedState::BASE_MOVING: return "base_moving";
    case RatedState::REFINING: return "refining";
    case RatedState::ON_LIKED: return "on_liked";
    case RatedState::BETWEEN_STEPS: return "between_steps";
    default: return "off";
  }
}

struct LikedTime {
  uint8_t shots = 0;       // liked shots found
  float medianS = 0.0f;    // their median brew time (0 when fewer than RATED_MIN_SHOTS)
  float spreadS = 0.0f;    // fastest to slowest
  float seS = 0.0f;        // standard error of that median (1.2533 sd / sqrt n)
};

inline LikedTime likedTime(const GrindShot *shots, size_t count) {
  LikedTime r;
  float liked[40];
  size_t n = 0;
  for (size_t i = 0; i < count && n < sizeof(liked) / sizeof(liked[0]); ++i) {
    if (shots[i].rating >= RATED_LIKED_MIN && shots[i].timeS > 0.0f) liked[n++] = shots[i].timeS;
  }
  r.shots = static_cast<uint8_t>(n);
  if (n < RATED_MIN_SHOTS) return r;
  for (size_t i = 1; i < n; ++i) {  // insertion sort
    const float v = liked[i];
    size_t j = i;
    while (j > 0 && liked[j - 1] > v) { liked[j] = liked[j - 1]; --j; }
    liked[j] = v;
  }
  r.medianS = (n % 2) ? liked[n / 2] : 0.5f * (liked[n / 2 - 1] + liked[n / 2]);
  r.spreadS = liked[n - 1] - liked[0];
  float mean = 0.0f;
  for (size_t i = 0; i < n; ++i) mean += liked[i];
  mean /= static_cast<float>(n);
  float var = 0.0f;
  for (size_t i = 0; i < n; ++i) var += (liked[i] - mean) * (liked[i] - mean);
  var /= static_cast<float>(n - 1);
  r.seS = 1.2533f * sqrtf(var) / sqrtf(static_cast<float>(n));
  return r;
}

// Sour or bitter in at least TASTE_MIN_SHOTS of the newest shots at a setting, and nothing
// that contradicts it, is a verdict worth showing. Time stays the rule: this only adds a note.
// -1: sour (finer), +1: bitter (coarser), 0: no clear verdict. `used`: shots that had a verdict.
constexpr size_t TASTE_LOOKBACK = 3;
constexpr size_t TASTE_MIN_SHOTS = 2;
inline int8_t grindTasteHint(const uint8_t *tastes, size_t count, uint8_t &used) {
  used = 0;
  int sour = 0, bitter = 0, balanced = 0;
  for (size_t i = 0; i < count && i < TASTE_LOOKBACK; ++i) {
    if (tastes[i] == 1) ++sour;
    else if (tastes[i] == 3) ++bitter;
    else if (tastes[i] == 2) ++balanced;
  }
  used = static_cast<uint8_t>(sour + bitter + balanced);
  if (sour >= static_cast<int>(TASTE_MIN_SHOTS) && bitter == 0 && balanced == 0) return -1;
  if (bitter >= static_cast<int>(TASTE_MIN_SHOTS) && sour == 0 && balanced == 0) return 1;
  return 0;
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
  bool stepLimited = false;    // the full correction exceeded maxStepUm (or the grinder's range)
  bool atLimit = false;        // it wants to go further, but the grinder is at the end of its range
  uint8_t outliersIgnored = 0; // one-off shots left out of the average
  bool newestIgnored = false;  // ...and the most recent shot was one of them
  float newestTimeS = 0.0f;    // brew time of the most recent shot at this setting
  float minTimeS = 0.0f;       // fastest and slowest of the shots used
  float maxTimeS = 0.0f;
  // The Grind-by-Sync-style verdict, before any refinement by ratings. `action` is this unless
  // REFINING; the controller's "settled" test (and the shared dial-in) goes by this one.
  GrindAction baseAction = GrindAction::NO_GRIND_SET;
  // Refinement by ratings (see "refining by the owner's ratings"). targetTimeS stays the
  // recipe's; aimTimeS is the liked time the average is being brought to, 0 when not refining.
  RatedState rated = RatedState::OFF;
  uint8_t ratedShots = 0;      // liked shots behind it
  float ratedTimeS = 0.0f;     // their median time, 0 if fewer than RATED_MIN_SHOTS
  float ratedBandS = 0.0f;     // the tighter range, plus or minus, around it
  float aimTimeS = 0.0f;
  bool refined = false;        // `action` is a refinement, not the base verdict
  int8_t tasteHint = 0;        // see grindTasteHint
  uint8_t tasteShots = 0;
  // Learning the grinder (grindConfigForGrinder); zero / false with a plain config.
  SensitivitySource source = SensitivitySource::GUESS;  // where umPerS came from
  float dataWeight = 0.0f;     // share of the sensitivity that rests on this coffee's shots (0..1)
  bool bracketed = false;      // the step was kept short of a setting seen on the other side
  bool betweenSteps = false;   // no setting between the two: the closer one is kept
  bool outOfRange = false;     // from a shot too long or too short to count (adviseOutOfRange)
  // Confidence in the advice (applyAdviceConfidence): how close past advice on this grinder landed.
  // confidence -1: not worked out. windowUm: plus or minus around the suggestion, grinder units.
  float confidence = -1.0f;
  uint8_t confidenceFollowed = 0;  // past advice behind it that was followed
  float confidenceRmsS = 0.0f;     // how far the next shot landed from its target, typically
  float windowUm = 0.0f;
  bool confidenceScaled = false;   // the step was made smaller or allowed larger by it
};

// The grinder cannot go past the ends of its range: a suggestion beyond one is the end itself, and
// at the end already there is nothing to turn (atLimit), not "finer by 0".
inline void keepAdviceInRange(GrindAdvice &advice, const StepRange &range) {
  if (advice.action != GrindAction::FINER && advice.action != GrindAction::COARSER) return;
  if (!advice.currentUm) return;
  const uint16_t kept = clampToRange(advice.suggestedUm, range);
  if (kept != advice.suggestedUm) {
    advice.suggestedUm = kept;
    advice.deltaUm = static_cast<int16_t>(static_cast<int>(kept) - static_cast<int>(advice.currentUm));
    advice.stepLimited = true;
  }
  if (advice.deltaUm == 0 || (advice.action == GrindAction::FINER) != (advice.deltaUm < 0)) {
    advice.deltaUm = 0;
    advice.suggestedUm = advice.currentUm;
    advice.atLimit = true;
    advice.action = GrindAction::HOLD;
  }
}

// Whether the advice can name a setting rather than only a direction: its sensitivity rests on
// shots (this coffee's, or this grinder's other coffees), not on the guess; or it is the largest step
// after a shot too long or short to count.
inline bool adviceHasNumber(const GrindAdvice &a) {
  return a.source != SensitivitySource::GUESS || a.outOfRange;
}

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

// A shot that ran far too long (more than 15 s over the target) or far too
// short (under 15 s) is rightly kept out of the average and the learning: it may not be a fair shot.
// But when it is the newest shot at the current setting and nothing valid has been pulled there,
// the plain advisor has nothing to say (NO_DATA) at exactly the moment a big move is needed, and the
// owner is left to guess. The direction is not in doubt, so this names the largest single step in
// that direction; the next shot, nearer the target, gets the usual advice.
inline void adviseOutOfRange(GrindAdvice &advice, GrindUse use, uint16_t currentUm, const GrindConfig &config) {
  if (advice.action != GrindAction::NO_DATA || currentUm == 0) return;
  if (use != GrindUse::TOO_LONG && use != GrindUse::TOO_SHORT) return;
  const int step = config.maxStepUm < 1 ? 1 : config.maxStepUm;
  int suggested = static_cast<int>(currentUm) + (use == GrindUse::TOO_LONG ? step : -step);
  if (suggested < 1) suggested = 1;
  advice.deltaUm = static_cast<int16_t>(suggested - static_cast<int>(currentUm));
  advice.suggestedUm = static_cast<uint16_t>(suggested);
  advice.action = advice.deltaUm < 0 ? GrindAction::FINER : GrindAction::COARSER;
  advice.baseAction = advice.action;
  advice.stepLimited = true;
  advice.outOfRange = true;
}

// How many microns one second of brew time is worth on this grinder, coffee
// and recipe: least squares of brew time against grind setting. Coarser must
// mean faster; anything else is noise and is rejected.
inline bool learnGrindSensitivity(const GrindShot *shots, size_t count, const GrindConfig &config,
                                  float &umPerS) {
  if (!shots || count < GRIND_MIN_LEARN_SHOTS) return false;
  double meanX = 0, meanY = 0;
  uint16_t lo = 0xFFFF, hi = 0;
  for (size_t i = 0; i < count; ++i) {
    meanX += shots[i].grindUm;
    meanY += shots[i].timeS;
    if (shots[i].grindUm < lo) lo = shots[i].grindUm;
    if (shots[i].grindUm > hi) hi = shots[i].grindUm;
  }
  if (static_cast<float>(hi - lo) < config.minLearnSpan) return false;
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
  if (!std::isfinite(result) || result < config.minUmPerS || result > config.maxUmPerS) {
    return false;
  }
  umPerS = static_cast<float>(result);
  return true;
}

inline bool learnGrindSensitivity(const GrindShot *shots, size_t count, float &umPerS) {
  return learnGrindSensitivity(shots, count, GrindConfig(), umPerS);
}

// ------------------------------------------------------------- learn the grinder --
// Four things the plain Grind-by-Sync rule above does not do. Each is a GrindConfig field and
// grindConfigForGrinder turns them all on: that is how the controller always runs the advice (part of
// the core logic since 17 Oct 2026, research/grind-logic-review.md).
//
// 1. Units. The settings, the steps and the limits are in the grinder's own smallest step. The
//    starting guess, the largest step and the plausible range of a learned sensitivity are
//    written in microns (they were chosen for the E64, one micron a step). On another grinder
//    they are converted with its microns per step; where that is not known they are taken
//    relative to the setting, which needs no unit at all. The largest step is never less than a
//    tenth of the setting (it was a fixed 20 um, which made coarse settings take extra shots).
// 2. Weighing what was measured. A sensitivity fitted to three noisy shots a few microns apart can
//    be wildly wrong, and the old rule took it as soon as it fell inside the plausible range. Here
//    the fitted slope is combined with the starting guess in proportion to how much each is worth
//    (the precision of each, a standard Bayesian update of one number): little data, little
//    weight; the more spread and shots, the more the measurement counts. It is "measured" once
//    it carries at least half the weight.
// 3. Learning the grinder. The sensitivity was learned per bag and forgotten with every new bag.
//    How strongly shot time responds to a change of setting is a property of the grinder (and
//    the recipe), so it is pooled over all the owner's coffees on the grinder, as the slope of
//    ln(time) against ln(setting) within each coffee (an elasticity: a percentage per percentage,
//    so coffees ground at different settings can be pooled). A new coffee then starts from the
//    grinder's own figure instead of the guess, and its first correction can show a number.
// 4. Not overshooting what is known. If an earlier setting of this run of shots ran clearly on
//    the other side of the target, the target lies between it and this one. A step that would
//    reach or pass it is replaced by the interpolated setting between the two (the regula falsi
//    step of root finding). If no setting lies between them (a grinder whose single step moves
//    the time by more than the tolerance), the closer of the two is kept instead of bouncing
//    between them for ever.
constexpr float GRIND_PRIOR_CV = 0.5f;          // how far the starting guess may be off, relative
constexpr float GRIND_PRIOR_CV_MIN = 0.25f;     // a grinder figure is never trusted more than this
constexpr float GRIND_ELASTICITY_GUESS = 0.9f;  // only for a grinder with no microns and no history
constexpr float GRIND_ELASTICITY_MIN = 0.2f;    // plausible range of a pooled elasticity
constexpr float GRIND_ELASTICITY_MAX = 5.0f;
constexpr size_t GRIND_BRACKET_LOOKBACK = 8;    // shots before this run searched for the other side

// The measured slope weighed against the starting guess. false: nothing could be measured (fewer
// than two shots, or all at one setting); the guess stands. dataWeight 0..1.
inline bool weighGrindSensitivity(const GrindShot *shots, size_t count, const GrindConfig &config,
                                  float &umPerS, float &dataWeight) {
  dataWeight = 0.0f;
  if (!shots || count < 2 || !(config.defaultUmPerS > 0.0f) || !(config.priorCv > 0.0f)) return false;
  double meanX = 0, meanY = 0;
  for (size_t i = 0; i < count; ++i) {
    meanX += shots[i].grindUm;
    meanY += shots[i].timeS;
  }
  meanX /= count;
  meanY /= count;
  double sxy = 0, sxx = 0;
  for (size_t i = 0; i < count; ++i) {
    const double dx = shots[i].grindUm - meanX;
    sxy += dx * (shots[i].timeS - meanY);
    sxx += dx * dx;
  }
  if (sxx <= 0.0) return false;
  // In seconds per step, where the noise is: the guess, its spread, and the fit's precision.
  const double priorSlope = -1.0 / config.defaultUmPerS;
  const double priorSd = config.priorCv * fabs(priorSlope);
  const double noise = config.noiseS > 0.1f ? config.noiseS : 0.1;
  const double priorPrec = 1.0 / (priorSd * priorSd);
  const double dataPrec = sxx / (noise * noise);
  const double slope = (priorSlope * priorPrec + (sxy / sxx) * dataPrec) / (priorPrec + dataPrec);
  dataWeight = static_cast<float>(dataPrec / (priorPrec + dataPrec));
  if (!(slope < 0.0)) {   // the data overwhelm the guess with an impossible sign: noise; keep the guess
    dataWeight = 0.0f;
    return false;
  }
  double result = -1.0 / slope;
  if (result < config.minUmPerS) result = config.minUmPerS;
  if (result > config.maxUmPerS) result = config.maxUmPerS;
  umPerS = static_cast<float>(result);
  return true;
}

// One valid shot on the grinder, of any coffee, for pooledGrindElasticity. `group` keeps the shots
// of one coffee, bag and recipe together (any number that differs between them).
struct GrinderShot {
  float timeS = 0.0f;
  uint16_t setting = 0;
  uint16_t group = 0;
};

struct GrinderElasticity {
  bool known = false;
  float eps = 0.0f;     // percent change of time per percent change of setting, as a positive number
  float se = 0.0f;      // its standard error
  uint16_t shots = 0;   // shots that carried information
  uint8_t groups = 0;   // coffees (bags, recipes) they came from
};

constexpr size_t GRIND_POOL_MAX_GROUPS = 32;
constexpr float GRIND_POOL_MIN_SPREAD = 0.03f;  // ln-setting spread (about 3 %) before a slope means anything

inline GrinderElasticity pooledGrindElasticity(const GrinderShot *shots, size_t count) {
  GrinderElasticity out;
  if (!shots || count < 3) return out;
  uint16_t ids[GRIND_POOL_MAX_GROUPS];
  double sumX[GRIND_POOL_MAX_GROUPS] = {}, sumY[GRIND_POOL_MAX_GROUPS] = {};
  uint16_t n[GRIND_POOL_MAX_GROUPS] = {};
  size_t groups = 0;
  auto find = [&](uint16_t id) -> int {
    for (size_t g = 0; g < groups; ++g) {
      if (ids[g] == id) return static_cast<int>(g);
    }
    if (groups == GRIND_POOL_MAX_GROUPS) return -1;
    ids[groups] = id;
    return static_cast<int>(groups++);
  };
  for (size_t i = 0; i < count; ++i) {
    if (shots[i].setting == 0 || !(shots[i].timeS > 0.0f)) continue;
    const int g = find(shots[i].group);
    if (g < 0) continue;
    sumX[g] += logf(static_cast<float>(shots[i].setting));
    sumY[g] += logf(shots[i].timeS);
    ++n[g];
  }
  double sxy = 0, sxx = 0, syy = 0;
  size_t used = 0, usedGroups = 0;
  for (size_t g = 0; g < groups; ++g) {
    if (n[g] < 2) continue;
    const double mx = sumX[g] / n[g], my = sumY[g] / n[g];
    double gxx = 0;
    for (size_t i = 0; i < count; ++i) {
      if (shots[i].group != ids[g] || shots[i].setting == 0 || !(shots[i].timeS > 0.0f)) continue;
      const double dx = logf(static_cast<float>(shots[i].setting)) - mx;
      const double dy = logf(shots[i].timeS) - my;
      sxy += dx * dy;
      sxx += dx * dx;
      syy += dy * dy;
      gxx += dx * dx;
    }
    if (gxx > 0.0) {
      used += n[g];
      ++usedGroups;
    }
  }
  if (used < 3 || sxx <= 0.0) return out;
  // Enough spread of settings within coffees, counted as if from one coffee's worth of shots.
  if (sxx < GRIND_POOL_MIN_SPREAD * GRIND_POOL_MIN_SPREAD * 2.0) return out;
  const double slope = sxy / sxx;   // ln time per ln setting: negative
  const double eps = -slope;
  if (!(eps >= GRIND_ELASTICITY_MIN && eps <= GRIND_ELASTICITY_MAX)) return out;
  const long dof = static_cast<long>(used) - static_cast<long>(usedGroups) - 1;
  if (dof < 1) return out;
  double resid = (syy - slope * sxy) / static_cast<double>(dof);
  if (resid < 0.0) resid = 0.0;
  out.known = true;
  out.eps = static_cast<float>(eps);
  out.se = static_cast<float>(sqrt(resid / sxx));
  out.shots = static_cast<uint16_t>(used > 0xFFFF ? 0xFFFF : used);
  out.groups = static_cast<uint8_t>(usedGroups > 255 ? 255 : usedGroups);
  return out;
}

// The advisor's settings for this grinder, at this setting and target: the owner's configured
// values (in microns) put into the grinder's own unit, plus everything learned about the grinder.
// umPerStep: microns per stored step, 0 = not known.
inline GrindConfig grindConfigForGrinder(const GrindConfig &base, float umPerStep, uint16_t setting,
                                         float targetS, const GrinderElasticity &pool) {
  GrindConfig c = base;
  c.priorCv = GRIND_PRIOR_CV;
  c.bracket = true;
  c.guessSource = static_cast<uint8_t>(SensitivitySource::GUESS);
  auto clampStep = [](float v) -> uint8_t {
    if (v < 1.0f) v = 1.0f;
    if (v > 255.0f) v = 255.0f;
    return static_cast<uint8_t>(v + 0.5f);
  };
  if (umPerStep > 0.0f) {
    c.defaultUmPerS = base.defaultUmPerS / umPerStep;
    c.minUmPerS = base.minUmPerS / umPerStep;
    c.maxUmPerS = base.maxUmPerS / umPerStep;
    c.minLearnSpan = base.minLearnSpan / umPerStep < 1.0f ? 1.0f : base.minLearnSpan / umPerStep;
    // The largest single change: the configured one, or a tenth of the setting where that is more
    // (a coarse setting needs bigger moves for the same change of time).
    const float configured = base.maxStepUm / umPerStep;
    const float tenth = 0.10f * static_cast<float>(setting);
    c.maxStepUm = clampStep(tenth > configured ? tenth : configured);
  } else if (setting > 0 && targetS > 0.0f) {
    // No microns: everything relative to the setting, which is unit-free.
    const float x = static_cast<float>(setting);
    c.defaultUmPerS = x / (GRIND_ELASTICITY_GUESS * targetS);
    c.minUmPerS = x / (GRIND_ELASTICITY_MAX * targetS);
    c.maxUmPerS = x / (GRIND_ELASTICITY_MIN * targetS);
    c.minLearnSpan = 0.03f * x < 1.0f ? 1.0f : 0.03f * x;
    c.maxStepUm = clampStep(0.10f * x);
  }
  if (pool.known && setting > 0 && targetS > 0.0f && pool.eps > 0.0f) {
    float sens = static_cast<float>(setting) / (pool.eps * targetS);
    if (sens < c.minUmPerS) sens = c.minUmPerS;
    if (sens > c.maxUmPerS) sens = c.maxUmPerS;
    c.defaultUmPerS = sens;
    const float cv = pool.se / pool.eps;
    c.priorCv = cv > GRIND_PRIOR_CV_MIN ? (cv < 1.0f ? cv : 1.0f) : GRIND_PRIOR_CV_MIN;
    c.guessSource = static_cast<uint8_t>(SensitivitySource::GRINDER);
  }
  return c;
}

// shots: valid shots of one recipe, newest first.
inline GrindAdvice adviseGrind(const GrindShot *shots, size_t count, uint16_t currentUm,
                               float targetTimeS, const GrindConfig &config) {
  GrindAdvice advice;
  advice.currentUm = advice.suggestedUm = currentUm;
  advice.targetTimeS = targetTimeS;
  advice.umPerS = config.defaultUmPerS;
  if (currentUm == 0) return advice;

  advice.source = static_cast<SensitivitySource>(config.guessSource);
  float learned = 0.0f;
  if (config.priorCv > 0.0f) {
    float weight = 0.0f;
    if (weighGrindSensitivity(shots, count, config, learned, weight)) {
      advice.umPerS = learned;
      advice.dataWeight = weight;
      if (weight >= 0.5f) {
        advice.learned = true;
        advice.source = SensitivitySource::MEASURED;
        advice.learnShots = count > 255 ? 255 : static_cast<uint8_t>(count);
      }
    }
  } else if (learnGrindSensitivity(shots, count, config, learned)) {
    advice.umPerS = learned;
    advice.learned = true;
    advice.source = SensitivitySource::MEASURED;
    advice.learnShots = count > 255 ? 255 : static_cast<uint8_t>(count);
  }

  // The newest shots made at the current setting. A shot at another setting
  // ends the run: it says nothing about this one.
  float run[GRIND_OUTLIER_LOOKBACK];
  bool forced[GRIND_OUTLIER_LOOKBACK];
  uint8_t tastes[GRIND_OUTLIER_LOOKBACK];
  size_t runCount = 0;
  for (size_t i = 0; i < count && runCount < GRIND_OUTLIER_LOOKBACK; ++i) {
    if (shots[i].grindUm != currentUm) break;
    forced[runCount] = shots[i].forced;
    tastes[runCount] = shots[i].taste;
    run[runCount++] = shots[i].timeS;
  }
  advice.tasteHint = grindTasteHint(tastes, runCount, advice.tasteShots);

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
  if (config.bracket) {
    // The earlier settings of this run, newest first: the nearest one in the direction of the move
    // that ran clearly on the other side of the target.
    const int dir = delta > 0 ? 1 : -1;
    size_t k = 0;
    while (k < count && shots[k].grindUm == currentUm) ++k;
    size_t seen = 0;
    int otherX = 0;
    float otherErr = 0.0f;
    while (k < count && seen < GRIND_BRACKET_LOOKBACK) {
      const uint16_t x = shots[k].grindUm;
      float sum = 0.0f;
      size_t n = 0;
      while (k < count && shots[k].grindUm == x && seen < GRIND_BRACKET_LOOKBACK) {
        sum += shots[k].timeS;
        ++n;
        ++k;
        ++seen;
      }
      const float e = sum / static_cast<float>(n) - targetTimeS;
      const int side = static_cast<int>(x) - static_cast<int>(currentUm);
      if (side * dir > 0 && e * advice.errorS < 0.0f && fabsf(e) > config.toleranceS &&
          (otherX == 0 || abs(side) < abs(otherX - static_cast<int>(currentUm)))) {
        otherX = x;
        otherErr = e;
      }
    }
    if (otherX != 0 && (suggested - otherX) * dir >= 0) {
      advice.bracketed = true;
      const float frac = advice.errorS / (advice.errorS - otherErr);  // 0..1: where the target lies
      int between = static_cast<int>(currentUm) +
                    static_cast<int>(lroundf(frac * static_cast<float>(otherX - static_cast<int>(currentUm))));
      if (between == static_cast<int>(currentUm) || between == otherX) {
        // Nothing between the two: the closer of them.
        advice.betweenSteps = true;
        if (fabsf(advice.errorS) <= fabsf(otherErr)) {
          advice.action = GrindAction::HOLD;
          return advice;
        }
        between = otherX;
      }
      suggested = between;
    }
  }
  advice.deltaUm = static_cast<int16_t>(suggested - static_cast<int>(currentUm));
  advice.suggestedUm = static_cast<uint16_t>(suggested);
  advice.action = advice.deltaUm < 0 ? GrindAction::FINER : GrindAction::COARSER;
  return advice;
}

// adviseGrind (the base) first; then, if asked and the base says "keep", a refinement by the
// owner's liked shots. useRatings false: exactly adviseGrind.
inline GrindAdvice adviseGrindRated(const GrindShot *shots, size_t count, uint16_t currentUm,
                                    float recipeTargetS, const GrindConfig &config,
                                    bool useRatings) {
  GrindAdvice advice = adviseGrind(shots, count, currentUm, recipeTargetS, config);
  advice.baseAction = advice.action;
  if (!useRatings || currentUm == 0) return advice;

  const LikedTime liked = likedTime(shots, count);
  advice.ratedShots = liked.shots;
  advice.ratedTimeS = liked.medianS;
  advice.rated = RatedState::TOO_FEW;
  if (liked.shots < RATED_MIN_SHOTS) return advice;
  // Only the base's own range is open to refinement: a liked time outside it is not used.
  if (fabsf(liked.medianS - recipeTargetS) > config.toleranceS) {
    advice.rated = RatedState::OUTSIDE_BAND;
    return advice;
  }
  // The base kept the closer of two neighbouring steps: nothing between them to aim at.
  if (advice.betweenSteps) {
    advice.rated = RatedState::BETWEEN_STEPS;
    return advice;
  }
  // The base is already asking for a move, or has nothing to judge: ratings wait.
  if (advice.baseAction != GrindAction::HOLD) {
    advice.rated = RatedState::BASE_MOVING;
    return advice;
  }
  // The tighter range: as wide as the liked times are uncertain, within the base's.
  float band = liked.seS;
  if (band < RATED_BAND_FLOOR_S) band = RATED_BAND_FLOOR_S;
  if (band > config.toleranceS) band = config.toleranceS;
  advice.ratedBandS = band;
  advice.aimTimeS = liked.medianS;
  const float error = advice.averageTimeS - liked.medianS;  // positive: running slower than liked
  if (fabsf(error) <= band) {
    advice.rated = RatedState::ON_LIKED;
    return advice;
  }
  // Same sensitivity as the base, same largest step; the move stops at the liked time.
  float delta = error * advice.umPerS;
  const float limit = config.maxStepUm < 1 ? 1.0f : config.maxStepUm;
  if (fabsf(delta) > limit) delta = delta > 0 ? limit : -limit;
  // Learning the grinder: a move smaller than half a step would land further from the liked time
  // than staying; the current step is the nearest one to it.
  if (config.bracket && fabsf(delta) < 0.5f) {
    advice.rated = RatedState::ON_LIKED;
    return advice;
  }
  int rounded = static_cast<int>(delta > 0 ? delta + 0.5f : delta - 0.5f);
  if (rounded == 0) rounded = delta > 0 ? 1 : -1;
  int suggested = static_cast<int>(currentUm) + rounded;
  if (suggested < 1) suggested = 1;
  advice.deltaUm = static_cast<int16_t>(suggested - static_cast<int>(currentUm));
  advice.suggestedUm = static_cast<uint16_t>(suggested);
  advice.action = advice.deltaUm < 0 ? GrindAction::FINER : GrindAction::COARSER;
  advice.stepLimited = fabsf(error * advice.umPerS) > limit;
  advice.rated = RatedState::REFINING;
  advice.refined = true;
  return advice;
}

// ---- Confidence in the advice ---------------------------------------------------------------------
// Advice is a prediction: "move to X and the next shot lands on the target". How close past
// predictions landed on this grinder says how far to trust the next one. Low confidence: a smaller
// step and a wider window around it, so a noisy run is not chased. High confidence: the full
// correction, past the usual largest step if need be, and a tighter window.

// One past advice to move, and the next shot of the same coffee, bag and recipe.
struct AdviceOutcome {
  uint32_t epochS = 0;        // of the next shot
  uint16_t coffeeId = 0;
  uint16_t fromSetting = 0;   // where the advised shot was pulled
  uint16_t advised = 0;       // the setting advised after it
  uint16_t nextSetting = 0;   // where the next shot was pulled
  float nextTimeS = 0.0f;
  float targetS = 0.0f;
  bool followed = false;      // the next shot was pulled at the advised setting
  uint8_t confidencePct = 0;  // what the advice was given with, percent + 1; 0: not recorded
};

constexpr float ADVICE_CONFIDENCE_START = 0.3f;  // until two followed advices have landed
constexpr size_t ADVICE_CONFIDENCE_SHOTS = 30;   // the newest followed advices looked at

struct AdviceConfidence {
  float score = ADVICE_CONFIDENCE_START;  // 0..1
  uint8_t followed = 0;
  float rmsS = 0.0f;
};

// outcomes newest first. A miss of three tolerances or more scores 0; a handful of followed advices
// cannot score high however well they landed (n / (n + 3)).
inline AdviceConfidence adviceConfidence(const AdviceOutcome *outcomes, size_t n, float toleranceS) {
  AdviceConfidence c;
  float sum = 0.0f;
  uint8_t k = 0;
  for (size_t i = 0; i < n && k < ADVICE_CONFIDENCE_SHOTS; ++i) {
    if (!outcomes[i].followed || outcomes[i].targetS <= 0.0f) continue;
    const float e = outcomes[i].nextTimeS - outcomes[i].targetS;
    sum += e * e;
    ++k;
  }
  c.followed = k;
  if (k) c.rmsS = sqrtf(sum / k);
  if (k < 2) return c;
  const float tol = toleranceS > 0.1f ? toleranceS : 0.1f;
  float raw = 1.0f - c.rmsS / (3.0f * tol);
  if (raw < 0.0f) raw = 0.0f;
  if (raw > 1.0f) raw = 1.0f;
  c.score = raw * static_cast<float>(k) / (static_cast<float>(k) + 3.0f);
  return c;
}

// Records the confidence and the window on the advice; with scaleStep, also resizes a move:
// the step is the correction times min(m, 1) and the largest step is maxStepUm times m, where
// m = 0.5 + score. A step kept short of a setting seen on the other side (bracketed) only shrinks.
// Called before keepAdviceInRange.
inline void applyAdviceConfidence(GrindAdvice &a, const AdviceConfidence &c, float toleranceS,
                                  uint8_t maxStepUm, bool scaleStep) {
  a.confidence = c.score;
  a.confidenceFollowed = c.followed;
  a.confidenceRmsS = c.rmsS;
  const float spreadS = c.followed >= 2 ? (c.rmsS > 0.5f * toleranceS ? c.rmsS : 0.5f * toleranceS)
                                        : 1.5f * toleranceS;
  a.windowUm = spreadS * a.umPerS;
  if (!scaleStep) return;
  if (a.action != GrindAction::FINER && a.action != GrindAction::COARSER) return;
  if (a.outOfRange || a.betweenSteps || !a.currentUm || a.umPerS <= 0.0f) return;
  const float m = 0.5f + c.score;
  const float err = (a.refined && a.aimTimeS > 0.0f) ? a.averageTimeS - a.aimTimeS : a.errorS;
  float delta = err * a.umPerS * (m < 1.0f ? m : 1.0f);
  const float limit = (maxStepUm < 1 ? 1.0f : static_cast<float>(maxStepUm)) * m;
  bool limited = false;
  if (fabsf(delta) > limit) {
    delta = delta > 0 ? limit : -limit;
    limited = true;
  }
  if (a.bracketed && fabsf(delta) > fabsf(static_cast<float>(a.deltaUm))) {
    delta = static_cast<float>(a.deltaUm);
    limited = a.stepLimited;
  }
  int rounded = static_cast<int>(delta > 0 ? delta + 0.5f : delta - 0.5f);
  const int dir = a.action == GrindAction::COARSER ? 1 : -1;
  if (rounded * dir <= 0) rounded = dir;  // outside tolerance: at least one step, the same way
  int suggested = static_cast<int>(a.currentUm) + rounded;
  if (suggested < 1) suggested = 1;
  if (suggested > 65535) suggested = 65535;
  a.confidenceScaled = rounded != a.deltaUm;
  a.deltaUm = static_cast<int16_t>(suggested - static_cast<int>(a.currentUm));
  a.suggestedUm = static_cast<uint16_t>(suggested);
  a.stepLimited = limited;
}

}  // namespace bbw
