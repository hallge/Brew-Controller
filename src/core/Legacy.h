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

// History version 9: coffee, recipe and grinder ids added. Version 10 is this plus the bag and TDS at the end.
struct ShotRecordV9 {
  uint32_t id = 0;
  uint32_t epochS = 0;             // 0 when wall-clock time was unknown
  char recipe[RECIPE_NAME_LEN] = {};
  float targetWeightG = 0.0f;
  float stopOffsetG = 0.0f;        // offset used for this shot
  float stopWeightG = 0.0f;        // weight when the stop command was issued
  float finalWeightG = 0.0f;       // settled weight after the drip period
  float dripWeightG = 0.0f;        // final - stop
  float shotTimeS = 0.0f;          // start command accepted -> stop command issued
  float firstDropS = 0.0f;         // 0 when not detected
  float averageFlowGS = 0.0f;      // final weight / shot time
  float peakFlowGS = 0.0f;
  float nextStopOffsetG = 0.0f;    // offset after learning (== stopOffsetG if unchanged)
  uint16_t stopLatencyMs = 0;      // stop command -> flow below FLOW_OFF
  uint8_t endReason = 0;           // EndReason
  uint8_t flags = 0;               // SHOT_FLAG_*
  uint8_t alphaUsed = 0;           // learning factor for this shot, hundredths
  uint8_t alphaNext = 0;           // learning factor after this shot
  uint16_t grindUm = 0;            // grinder setting for this shot, 0 = not recorded
  float doseG = 0.0f;              // dry coffee dose of the recipe
  float targetTimeS = 0.0f;        // recipe's target brew time when the shot was made
  // Pour time: first drops in the cup -> stop command. Excludes pre-infusion,
  // which is what a flow-detecting scale timer measures. 0 if no first drop.
  float pourTimeS = 0.0f;
  float pourFlowGS = 0.0f;         // final weight / pour time
  uint8_t timeBasis = 0;           // GrindTimeBasis the target time referred to
  // The coffee, copied from the recipe so history stays true if it is edited.
  char coffee[COFFEE_NAME_LEN] = {};
  char region[ORIGIN_LEN] = {};
  char varietal[ORIGIN_LEN] = {};
  uint32_t roastDate = 0;          // YYYYMMDD
  uint8_t roastLevel = 0;          // RoastLevel
  uint16_t preinfusionHoldMs = 0;  // 0 = machine's own pre-infusion
  // Shape of the pour (core/FlowFeatures.h). All zero when not computable.
  float flow25GS = 0.0f;           // flow with 25 % of the yield in the cup
  float flow50GS = 0.0f;
  float flow75GS = 0.0f;
  float flowAcceleration = 0.0f;   // flow75 / flow25
  float flowRoughnessGS = 0.0f;    // RMS departure from a straight-line flow
  // Entered afterwards by the person who drank it.
  uint8_t rating = 0;              // 1..5, 0 = not rated
  uint8_t taste = 0;               // Taste
  // The user's ruling on an unusual shot (result screen or history table).
  uint8_t grindVote = 0;           // GrindVote
  // Which library entries the shot was made with. The names above are what
  // history shows; these survive a rename. 0 = none, or no longer known.
  uint16_t coffeeId = 0;
  uint16_t recipeId = 0;
  uint16_t grinderId = 0;          // Grinder::calibration the shot was made under
  uint16_t bloomPauseMs = 0;       // bloom pause of the recipe, 0 = none
};

}  // namespace legacy
}  // namespace bbw
