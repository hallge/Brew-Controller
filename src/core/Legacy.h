#pragma once
// Layouts written to flash by earlier builds, kept only so that what they
// stored can be read once and converted (core/Library.h, storage/Store.cpp).
// Never change anything in here.

#include <stddef.h>
#include <stdint.h>

#include "Types.h"

namespace bbw {
namespace legacy {

// Settings version 7: coffee, recipe and everything learned in one record.
struct RecipeV7 {
  char name[RECIPE_NAME_LEN];
  float targetWeightG;
  float baselineOffsetG;
  float stopOffsetG;
  TimeLimit timeLimit;
  uint16_t maxShotTimeS;
  bool useRatio;
  float doseG;
  float ratio;
  uint8_t learnedAlpha;
  float targetTimeS;
  uint16_t grindUm;
  char coffee[COFFEE_NAME_LEN];
  char region[ORIGIN_LEN];
  char varietal[ORIGIN_LEN];
  uint8_t roastLevel;
  uint32_t roastDate;
  Preinfusion preinfusion;
  uint16_t preinfusionHoldMs;
};

constexpr size_t MAX_RECIPES_V7 = 16;

struct SettingsV7 {
  bool learningEnabled;
  float learningFactor;
  bool adaptiveLearning;
  float minValidWeightG;
  uint16_t scaleTimeoutMs;
  uint16_t dripObserveMs;
  uint16_t minShotTimeMs;
  uint16_t noFlowTimeoutS;
  CupMode cupMode;
  float cupMinWeightG;
  bool autoStartAfterTare;
  bool scaleTimer;
  ActuationMode actuationMode;
  uint16_t pressDurationMs;
  uint16_t releaseSettleMs;
  uint8_t botHoldSeconds;
  float grindToleranceS;
  float grindUmPerS;
  uint8_t grindWindow;
  uint8_t grindMaxStepUm;
  GrindTimeBasis grindTimeBasis;
  RecipeV7 recipes[MAX_RECIPES_V7];
  uint8_t recipeCount;
  uint8_t activeRecipe;
  bool requireCup;  // added late: absent from the oldest version 7 blobs
};

// Shot history version 8.
struct ShotRecordV8 {
  uint32_t id;
  uint32_t epochS;
  char recipe[RECIPE_NAME_LEN];
  float targetWeightG;
  float stopOffsetG;
  float stopWeightG;
  float finalWeightG;
  float dripWeightG;
  float shotTimeS;
  float firstDropS;
  float averageFlowGS;
  float peakFlowGS;
  float nextStopOffsetG;
  uint16_t stopLatencyMs;
  uint8_t endReason;
  uint8_t flags;
  uint8_t alphaUsed;
  uint8_t alphaNext;
  uint16_t grindUm;
  float doseG;
  float targetTimeS;
  float pourTimeS;
  float pourFlowGS;
  uint8_t timeBasis;
  char coffee[COFFEE_NAME_LEN];
  char region[ORIGIN_LEN];
  char varietal[ORIGIN_LEN];
  uint32_t roastDate;
  uint8_t roastLevel;
  uint16_t preinfusionHoldMs;
  float flow25GS;
  float flow50GS;
  float flow75GS;
  float flowAcceleration;
  float flowRoughnessGS;
  uint8_t rating;
  uint8_t taste;
  uint8_t grindVote;
};

// The sizes those builds wrote (history header's recordSize; settings blob
// length minus its 4-byte version prefix). If either fails, the compiler lays
// these out differently and nothing stored could be read back correctly.
static_assert(sizeof(ShotRecordV8) == 220, "history v8 record layout");
static_assert(sizeof(SettingsV7) == 2620, "settings v7 layout");
static_assert(offsetof(SettingsV7, requireCup) == 2618, "settings v7 layout");

}  // namespace legacy
}  // namespace bbw
