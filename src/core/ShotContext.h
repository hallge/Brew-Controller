#pragma once
// Everything about the circumstances of a shot that a learning engine will need and the shot
// record does not hold. Kept apart from the history record on purpose: the record is a
// fixed layout that is also the on-flash history (changing it means a migration), while this
// is an experiment in what to capture and has to be free to grow. Contexts are kept in one
// versioned ring file beside the history (Store: /contexts.bin, one 64-byte slot per shot, the
// slot reused when the shot leaves the history). Nothing here is read by the grind advice; it
// only records.
//
// Why each group exists (research/dial-in-engine.md section 5):
//   bean state    the shot record keeps the roast date, but a bag can be edited, frozen or thawed
//                 afterwards; what mattered is the age the coffee had AT the shot.
//   routine       time since the previous shot and the machine's warm-up state explain part of the
//                 shot-to-shot noise that caps how often two shots are enough.
//   offered       what the controller suggested before the first shot of the coffee, so that the
//                 start suggestion can be scored against where the coffee actually settled.
//   advised       what the advisor said after this shot, so advice can be scored against what the
//                 owner did next.
//   curve quality sample interval and weight resolution decide whether flow-shape detectors can be
//                 trusted with this scale.
//   config        the advisor's settings in force, without which a stored shot cannot be replayed.
//   owner entered fields for things only the owner knows; zero until an input for them exists.
//
// Portable and host-tested; no flash or network in here.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "Community.h"
#include "GrindAdvisor.h"
#include "ShotData.h"
#include "SimilarCoffee.h"
#include "Types.h"

namespace bbw {

constexpr uint8_t SHOT_CONTEXT_VERSION = 1;
// Bumped whenever the advice or start rules change in a way that would change a recorded result.
constexpr uint8_t SHOT_ENGINE_VERSION = 1;

constexpr uint8_t CTX_BEAN_FROZEN = 0x01;   // the bag was in the freezer at the shot
// Owner-entered preparation flags (prepFlags). PURGED and UNEVEN are set from the shot's page
// (applyShotPrep); the others are reserved so that records made later mean the same thing.
constexpr uint16_t CTX_PREP_WDT = 0x0001;
constexpr uint16_t CTX_PREP_SCREEN = 0x0002;
constexpr uint16_t CTX_PREP_PAPER = 0x0004;
constexpr uint16_t CTX_PREP_PURGED = 0x0008;       // the grinder was purged before this dose
constexpr uint16_t CTX_PREP_UNEVEN = 0x0010;       // the owner saw or suspected uneven flow / channeling
constexpr uint16_t SHOT_DOSE_MIN_DG = 50;          // a weighed dose outside 5..40 g is a typing error
constexpr uint16_t SHOT_DOSE_MAX_DG = 400;

struct ShotContext {
  uint32_t shotId = 0;
  // The bean at the shot.
  int16_t ageDays = -1;          // effective: frozen time weighted by FROZEN_AGING_FRACTION; -1 unknown
  int16_t calendarDays = -1;     // days since roast, frozen time counted in full; -1 unknown
  int16_t frozenDays = 0;        // days spent frozen up to the shot
  uint8_t beanFlags = 0;         // CTX_BEAN_*
  uint8_t engineVersion = SHOT_ENGINE_VERSION;
  // The routine.
  uint32_t sinceLastShotS = 0;   // 0: first shot on record, or a clock was missing
  uint32_t machineOnS = 0;       // 0: not known (reserved; needs the power bot's state)
  uint32_t grinderCleanShots = 0;  // shots since the grinder was cleaned (upkeep counter)
  // Offered before the shot (what the controller would have shown for this coffee and recipe).
  uint16_t startSetting = 0;     // community start, grinder units; 0: none
  uint16_t startRange = 0;
  uint16_t similarSetting = 0;   // own-coffee start; 0: none
  uint16_t similarRange = 0;
  uint8_t startBasis = 0;        // StartBasis
  uint8_t similarFitted = 0;
  uint8_t similarSamples = 0;
  uint8_t pad0 = 0;
  // Advised after the shot.
  uint16_t adviceUm = 0;         // the setting the advice names (= current if it says keep)
  int16_t adviceDeltaUm = 0;
  uint16_t adviceUmPerSx100 = 0; // sensitivity applied, hundredths of um per second
  uint8_t adviceAction = 0;      // GrindAction
  uint8_t adviceBaseAction = 0;  // GrindAction before ratings
  uint8_t adviceShots = 0;       // shots behind the average
  uint8_t adviceLearnShots = 0;  // shots behind a measured sensitivity (0: the starting guess)
  uint8_t adviceFlags = 0;       // bit0 learned, bit1 step limited, bit2 refined by ratings
  uint8_t pad1 = 0;
  // The curve's quality.
  uint16_t sampleIntervalMs = 0; // median time between readings; 0 unknown
  uint16_t curvePoints = 0;
  uint8_t weightStepCg = 0;      // smallest weight step seen, hundredths of a gram; 0 unknown
  // The advisor's settings in force.
  uint8_t toleranceDs = 0;       // tenths of a second
  uint8_t window = 0;
  uint8_t maxStepUm = 0;
  uint16_t defaultUmPerSx100 = 0;
  uint8_t ratedTarget = 0;
  uint8_t learnGrinder = 0;      // "learn the grinder" was on: the limits above were converted (core/GrindAdvisor.h)
  // Entered by the owner afterwards, on the shot's page (applyShotPrep).
  uint16_t doseActualDg = 0;     // dose actually weighed, tenths of a gram; 0 unknown
  uint16_t prepFlags = 0;        // CTX_PREP_*
};

static_assert(sizeof(ShotContext) % 4 == 0, "ShotContext must not need padding at its end");

// Median time between readings, and the smallest weight step, from the curve as stored.
inline void curveQuality(const CurvePoint *points, size_t count, uint16_t &medianIntervalMs,
                         uint8_t &weightStepCg) {
  medianIntervalMs = 0;
  weightStepCg = 0;
  if (!points || count < 3) return;
  // Intervals into 5 ms bins up to 2 s: no sorting and no big array on the caller's stack.
  constexpr size_t BINS = 400;
  uint16_t hist[BINS] = {};
  size_t total = 0;
  int minStepCg = 1 << 20;
  for (size_t i = 1; i < count; ++i) {
    const int dtCs = static_cast<int>(points[i].timeCs) - static_cast<int>(points[i - 1].timeCs);
    if (dtCs > 0 && dtCs < 200) {
      const size_t bin = static_cast<size_t>(dtCs) * 2;   // centiseconds -> 5 ms bins
      ++hist[bin < BINS ? bin : BINS - 1];
      ++total;
    }
    int dw = static_cast<int>(points[i].weightCg) - static_cast<int>(points[i - 1].weightCg);
    if (dw < 0) dw = -dw;
    if (dw >= 1 && dw < minStepCg) minStepCg = dw;
  }
  if (total >= 2) {
    size_t seen = 0;
    for (size_t b = 0; b < BINS; ++b) {
      seen += hist[b];
      if (seen * 2 >= total) {
        medianIntervalMs = static_cast<uint16_t>(b * 5);
        break;
      }
    }
  }
  if (minStepCg < (1 << 19)) weightStepCg = static_cast<uint8_t>(minStepCg > 255 ? 255 : minStepCg);
}

struct ShotContextInputs {
  const ShotRecord *shot = nullptr;
  uint32_t previousEpochS = 0;          // the shot before this one; 0 none
  // The bag in use at the shot.
  uint32_t roastDate = 0, frozenDate = 0, thawDate = 0;
  uint32_t grinderCleanShots = 0;
  CommunityStart start;
  SimilarStart similar;
  GrindAdvice advice;                   // computed after this shot
  float toleranceS = 0.0f;
  uint8_t window = 0, maxStepUm = 0;
  float defaultUmPerS = 0.0f;
  bool ratedTarget = false;
  bool learnGrinder = false;
  const CurvePoint *curve = nullptr;
  size_t curvePoints = 0;
};

// What the owner says about a shot's preparation afterwards: the dose they weighed (0 = not
// weighed), whether the grinder was purged first, and whether the flow looked uneven. false: a
// dose outside 5..40 g; nothing changed. These are records for the learning work; none of them
// steers the advice (an uneven shot that should not count is ruled out with "ignore this shot").
inline bool applyShotPrep(ShotContext &c, uint16_t doseDg, bool purged, bool uneven) {
  if (doseDg != 0 && (doseDg < SHOT_DOSE_MIN_DG || doseDg > SHOT_DOSE_MAX_DG)) return false;
  c.doseActualDg = doseDg;
  c.prepFlags = static_cast<uint16_t>((c.prepFlags & ~(CTX_PREP_PURGED | CTX_PREP_UNEVEN)) |
                                      (purged ? CTX_PREP_PURGED : 0) | (uneven ? CTX_PREP_UNEVEN : 0));
  return true;
}

inline ShotContext buildShotContext(const ShotContextInputs &in) {
  ShotContext c;
  if (!in.shot) return c;
  const ShotRecord &s = *in.shot;
  c.shotId = s.id;
  if (s.epochS != 0 && in.roastDate != 0) {
    const int eff = effectiveAgeDays(in.roastDate, in.frozenDate, in.thawDate, s.epochS);
    const int cal = beanAgeDays(in.roastDate, s.epochS);
    const int frozen = frozenDaysAt(in.frozenDate, in.thawDate, s.epochS);
    c.ageDays = static_cast<int16_t>(eff < -1 ? -1 : (eff > 32000 ? 32000 : eff));
    c.calendarDays = static_cast<int16_t>(cal < -1 ? -1 : (cal > 32000 ? 32000 : cal));
    c.frozenDays = static_cast<int16_t>(frozen < 0 ? 0 : (frozen > 32000 ? 32000 : frozen));
    if (frozenAt(in.frozenDate, in.thawDate, s.epochS)) c.beanFlags |= CTX_BEAN_FROZEN;
  }
  if (s.epochS != 0 && in.previousEpochS != 0 && s.epochS > in.previousEpochS) {
    c.sinceLastShotS = s.epochS - in.previousEpochS;
  }
  c.grinderCleanShots = in.grinderCleanShots;
  c.startSetting = in.start.setting;
  c.startRange = in.start.range;
  c.startBasis = static_cast<uint8_t>(in.start.basis);
  if (in.similar.found) {
    c.similarSetting = in.similar.setting;
    c.similarRange = in.similar.range;
    c.similarFitted = in.similar.fitted ? 1 : 0;
    c.similarSamples = in.similar.samples;
  }
  const GrindAdvice &a = in.advice;
  c.adviceUm = a.suggestedUm;
  c.adviceDeltaUm = a.deltaUm;
  float sens = a.umPerS * 100.0f + 0.5f;
  c.adviceUmPerSx100 = static_cast<uint16_t>(sens < 0.0f ? 0.0f : (sens > 65535.0f ? 65535.0f : sens));
  c.adviceAction = static_cast<uint8_t>(a.action);
  c.adviceBaseAction = static_cast<uint8_t>(a.baseAction);
  c.adviceShots = a.shotsUsed;
  c.adviceLearnShots = a.learned ? a.learnShots : 0;
  c.adviceFlags = static_cast<uint8_t>((a.learned ? 1 : 0) | (a.stepLimited ? 2 : 0) | (a.refined ? 4 : 0));
  curveQuality(in.curve, in.curvePoints, c.sampleIntervalMs, c.weightStepCg);
  c.curvePoints = static_cast<uint16_t>(in.curvePoints > 0xFFFF ? 0xFFFF : in.curvePoints);
  float tol = in.toleranceS * 10.0f + 0.5f;
  c.toleranceDs = static_cast<uint8_t>(tol < 0.0f ? 0.0f : (tol > 255.0f ? 255.0f : tol));
  c.window = in.window;
  c.maxStepUm = in.maxStepUm;
  float d = in.defaultUmPerS * 100.0f + 0.5f;
  c.defaultUmPerSx100 = static_cast<uint16_t>(d < 0.0f ? 0.0f : (d > 65535.0f ? 65535.0f : d));
  c.ratedTarget = in.ratedTarget ? 1 : 0;
  c.learnGrinder = in.learnGrinder ? 1 : 0;
  return c;
}

}  // namespace bbw
