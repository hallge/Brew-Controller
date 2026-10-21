#pragma once
// Defaults and range enforcement for Settings. Every write path (NVS load,
// web form, touch UI) goes through sanitize().

#include <cmath>
#include <string.h>

#include "ScaleRest.h"
#include "Types.h"

namespace bbw {

template <typename T>
inline T clampTo(T value, T lo, T hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

inline float finiteOr(float value, float fallback) {
  return std::isfinite(value) ? value : fallback;
}

// YYYYMMDD with a plausible year, month and day.
inline bool validRoastDate(uint32_t date) {
  const uint32_t year = date / 10000, month = (date / 100) % 100, day = date % 100;
  return year >= 2000 && year <= 2099 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

inline void sanitizeRecipe(Recipe &r) {
  r.name[RECIPE_NAME_LEN - 1] = '\0';
  if (r.name[0] == '\0') strncpy(r.name, "Recipe", RECIPE_NAME_LEN - 1);
  r.doseG = clampTo(finiteOr(r.doseG, 18.0f), MIN_DOSE_G, MAX_DOSE_G);
  r.ratio = clampTo(finiteOr(r.ratio, 2.0f), MIN_RATIO, MAX_RATIO);
  if (r.useRatio) r.targetWeightG = roundf(r.doseG * r.ratio * 10.0f) / 10.0f;
  r.targetWeightG = clampTo(finiteOr(r.targetWeightG, 36.0f), MIN_TARGET_G, MAX_TARGET_G);
  if (r.learnedAlpha > 100) r.learnedAlpha = 0;
  r.targetTimeS = clampTo(finiteOr(r.targetTimeS, 28.0f), 5.0f, 60.0f);
  if (r.grindUm > 2000) r.grindUm = 0;
  r.coffee[COFFEE_NAME_LEN - 1] = '\0';
  r.region[ORIGIN_LEN - 1] = '\0';
  r.varietal[ORIGIN_LEN - 1] = '\0';
  if (r.roastLevel > static_cast<uint8_t>(RoastLevel::DARK)) r.roastLevel = 0;
  if (!validRoastDate(r.roastDate)) r.roastDate = 0;
  if (r.preinfusion != Preinfusion::HOLD) r.preinfusion = Preinfusion::MACHINE;
  r.preinfusionHoldMs = clampTo<uint16_t>(r.preinfusionHoldMs, 300, MAX_PREINFUSION_HOLD_MS);
  if (r.bloomPauseMs != 0) {
    r.bloomPauseMs = clampTo<uint16_t>(r.bloomPauseMs, MIN_BLOOM_PAUSE_MS, MAX_BLOOM_PAUSE_MS);
    r.preinfusion = Preinfusion::HOLD;  // a bloom cannot follow the machine's own pre-infusion
  }
  r.baselineOffsetG = clampTo(finiteOr(r.baselineOffsetG, 1.5f), 0.0f, MAX_OFFSET_G);
  r.stopOffsetG = clampTo(finiteOr(r.stopOffsetG, r.baselineOffsetG), 0.0f, MAX_OFFSET_G);
  r.maxShotTimeS = clampTo<uint16_t>(r.maxShotTimeS, 5, HARD_MAX_SHOT_MS / 1000);
  if (r.timeLimit != TimeLimit::CONTROLLER) r.timeLimit = TimeLimit::MACHINE;
}

inline void sanitize(Settings &s) {
  s.learningFactor = clampTo(finiteOr(s.learningFactor, 0.30f), 0.01f, 1.0f);
  s.minValidWeightG = clampTo(finiteOr(s.minValidWeightG, -5.0f), -50.0f, -0.5f);
  s.scaleTimeoutMs = clampTo<uint16_t>(s.scaleTimeoutMs, 500, 5000);
  s.dripObserveMs = clampTo<uint16_t>(s.dripObserveMs, 500, 10000);
  s.minShotTimeMs = clampTo<uint16_t>(s.minShotTimeMs, 0, 15000);
  s.noFlowTimeoutS = clampTo<uint16_t>(s.noFlowTimeoutS, 5, 60);
  if (s.cupMode != CupMode::CUP_AFTER_TARE) s.cupMode = CupMode::CUP_BEFORE_START;
  s.cupMinWeightG = clampTo(finiteOr(s.cupMinWeightG, 10.0f), 1.0f, 500.0f);
  if (s.actuationMode != ActuationMode::DOWN_UP) s.actuationMode = ActuationMode::ATOMIC_PRESS;
  s.pressDurationMs = clampTo<uint16_t>(s.pressDurationMs, 100, 3000);
  s.releaseSettleMs = clampTo<uint16_t>(s.releaseSettleMs, 300, 5000);
  s.botHoldSeconds = clampTo<uint8_t>(s.botHoldSeconds, 0, 5);
  s.grindToleranceS = clampTo(finiteOr(s.grindToleranceS, 2.0f), 0.5f, 10.0f);
  s.grindUmPerS = clampTo(finiteOr(s.grindUmPerS, 3.0f), 0.5f, 20.0f);
  s.grindWindow = clampTo<uint8_t>(s.grindWindow, 1, 10);
  if (s.grindTimeBasis != GrindTimeBasis::POUR) s.grindTimeBasis = GrindTimeBasis::TOTAL;
  s.grindMaxStepUm = clampTo<uint8_t>(s.grindMaxStepUm, 1, 100);
  s.scaleRestMin = sanitizeScaleRestMin(s.scaleRestMin);
  if (s.uiTheme >= UI_THEME_COUNT) s.uiTheme = 0;
}

inline Settings defaultSettings() {
  Settings s;
  sanitize(s);
  return s;
}

}  // namespace bbw
