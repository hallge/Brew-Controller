#pragma once
// Portable domain types. No Arduino / BLE / LVGL includes here: everything in
// src/core builds on a host compiler for the tests in test/host.

#include <stddef.h>
#include <stdint.h>

namespace bbw {

constexpr size_t MAX_RECIPES = 16;   // recipe definitions
constexpr size_t MAX_PRESETS = 16;   // lines in the controller's list
constexpr size_t MAX_COFFEES = 24;
constexpr size_t COFFEE_NAME_LEN = 24;
constexpr size_t ORIGIN_LEN = 32;  // region, varietal, producer
constexpr size_t PROCESS_LEN = 16;
constexpr size_t NOTES_LEN = 48;
constexpr size_t LIBRARY_ID_LEN = 40;
constexpr size_t GRINDER_NAME_LEN = 32;
constexpr size_t GRINDER_BURRS_LEN = 24;
constexpr size_t GRINDER_UNIT_LEN = 8;
constexpr uint16_t MAX_PREINFUSION_HOLD_MS = 30000;
// Bloom: pre-infuse, switch the pump off, wait, start again (see Recipe).
constexpr uint16_t MIN_BLOOM_PAUSE_MS = 1000;
constexpr uint16_t MAX_BLOOM_PAUSE_MS = 45000;
// How long the button is held for the start after the pause. On the Breville
// a hold is pre-infusion for as long as it lasts, so a short one all but
// skips the machine's own.
constexpr uint16_t BLOOM_RESTART_HOLD_MS = 1000;
constexpr size_t RECIPE_NAME_LEN = 24;

// Limits carried over from Open Brew by Weight (ShotStopperBrewTypes.h,
// ShotStopperScaleTypes.h, ShotStopperMachineMomentaryOnlyState.h).
constexpr float MAX_OFFSET_G = 10.0f;  // was 5 (Open Brew); the Bot and the Breville take ~1.7 s to stop: ~5 g at 2.5 g/s
constexpr float MIN_TARGET_G = 5.0f;
constexpr float MAX_TARGET_G = 200.0f;
constexpr float MIN_DOSE_G = 5.0f;
constexpr float MAX_DOSE_G = 30.0f;
constexpr float MIN_RATIO = 1.0f;
constexpr float MAX_RATIO = 5.0f;
constexpr uint32_t HARD_MAX_SHOT_MS = 90000;  // absolute ceiling for max shot time
constexpr float MAX_AUTOMATION_WEIGHT_G = 1000.0f;
constexpr float MAX_WEIGHT_SLEW_G_PER_S = 100.0f;
constexpr float WEIGHT_SLEW_ALLOWANCE_G = 20.0f;
constexpr uint8_t DIRECT_STOP_CONFIRMATION_SAMPLES = 2;
constexpr float TARE_SETTLE_BAND_G = 0.5f;
constexpr float FIRST_DROP_THRESHOLD_G = 0.3f;
constexpr float FLOW_ON_G_S = 0.60f;   // espresso-like flow => machine is running
constexpr float FLOW_OFF_G_S = 0.20f;  // below this => machine has stopped
constexpr uint32_t FLOW_HOLD_MS = 500;
constexpr float MAX_SETTLED_WEIGHT_DROP_G = 2.0f;
constexpr float MIN_LEARN_WEIGHT_G = 1.0f;

enum class State : uint8_t {
  BOOT,
  INITIALIZING,
  CONNECTING_SCALE,
  READY,
  TARING,
  WAITING_FOR_CUP,
  READY_TO_BREW,
  STARTING_SHOT,
  BREWING,
  BLOOMING,       // pump switched off between pre-infusion and the brew
  STOPPING_SHOT,
  POST_SHOT_DRIP,
  SHOT_COMPLETE,
  // Fault states. A fault raised while the machine may be running always goes
  // through STOPPING_SHOT first; the fault state is entered afterwards.
  SCALE_DISCONNECTED,
  SWITCHBOT_DISCONNECTED,
  SCALE_DATA_STALE,
  START_COMMAND_FAILED,
  STOP_COMMAND_FAILED,
  TARE_FAILED,
  SHOT_TIMEOUT,
  INVALID_WEIGHT,
  BLE_ERROR,
};

inline bool isFault(State s) { return s >= State::SCALE_DISCONNECTED; }

inline const char *stateName(State s) {
  switch (s) {
    case State::BOOT: return "BOOT";
    case State::INITIALIZING: return "INITIALIZING";
    case State::CONNECTING_SCALE: return "CONNECTING_SCALE";
    case State::READY: return "READY";
    case State::TARING: return "TARING";
    case State::WAITING_FOR_CUP: return "WAITING_FOR_CUP";
    case State::READY_TO_BREW: return "READY_TO_BREW";
    case State::STARTING_SHOT: return "STARTING_SHOT";
    case State::BREWING: return "BREWING";
    case State::BLOOMING: return "BLOOMING";
    case State::STOPPING_SHOT: return "STOPPING_SHOT";
    case State::POST_SHOT_DRIP: return "POST_SHOT_DRIP";
    case State::SHOT_COMPLETE: return "SHOT_COMPLETE";
    case State::SCALE_DISCONNECTED: return "SCALE_DISCONNECTED";
    case State::SWITCHBOT_DISCONNECTED: return "SWITCHBOT_DISCONNECTED";
    case State::SCALE_DATA_STALE: return "SCALE_DATA_STALE";
    case State::START_COMMAND_FAILED: return "START_COMMAND_FAILED";
    case State::STOP_COMMAND_FAILED: return "STOP_COMMAND_FAILED";
    case State::TARE_FAILED: return "TARE_FAILED";
    case State::SHOT_TIMEOUT: return "SHOT_TIMEOUT";
    case State::INVALID_WEIGHT: return "INVALID_WEIGHT";
    case State::BLE_ERROR: return "BLE_ERROR";
  }
  return "UNKNOWN";
}

enum class EndReason : uint8_t {
  NONE,
  WEIGHT,           // direct threshold: two fresh samples >= target - offset
  WEIGHT_PREDICTED, // linear prediction reached the cut point between samples
  USER,
  TIMEOUT,
  SCALE_LOST,       // disconnect or stale data
  INVALID_WEIGHT,   // cup removed / implausible reading
  NO_FLOW,          // start never confirmed by the scale
  MACHINE_STOPPED,  // the pour stopped with nothing pressed here: the machine's own
                    // time-out, or its button pressed by hand
  CUP_REMOVED,      // the cup was lifted off the scale mid-pour; stop was pressed
  RECOVERED,        // stopped after the controller restarted in the middle of a shot
  BLOOM_FAILED,     // a press of the bloom sequence could not be made or confirmed
  BOT_LOST,         // the bot holding the button for the shot stopped answering
};

inline const char *endReasonName(EndReason r) {
  switch (r) {
    case EndReason::NONE: return "none";
    case EndReason::WEIGHT: return "weight";
    case EndReason::WEIGHT_PREDICTED: return "weight_predicted";
    case EndReason::USER: return "user";
    case EndReason::TIMEOUT: return "timeout";
    case EndReason::SCALE_LOST: return "scale_lost";
    case EndReason::INVALID_WEIGHT: return "invalid_weight";
    case EndReason::NO_FLOW: return "no_flow";
    case EndReason::MACHINE_STOPPED: return "machine_stopped";
    case EndReason::CUP_REMOVED: return "cup_removed";
    case EndReason::RECOVERED: return "recovered";
    case EndReason::BLOOM_FAILED: return "bloom_failed";
    case EndReason::BOT_LOST: return "bot_lost";
  }
  return "unknown";
}

// Which duration the grind advisor compares with the recipe's target time.
enum class GrindTimeBasis : uint8_t {
  TOTAL = 0,  // start press -> stop press (includes pre-infusion)
  POUR = 1,   // first drops -> stop press
};

// How the start press is made. On the Breville, a tap on MANUAL starts with
// the machine's own programmed pre-infusion; holding the button keeps it in
// low-pressure pre-infusion for exactly as long as it is held (BES920 manual,
// page 15). So a hold sets the pre-infusion time, and a very short hold all
// but skips it.
enum class Preinfusion : uint8_t {
  MACHINE = 0,  // tap: the machine's programmed pre-infusion
  HOLD = 1,     // hold the button for preinfusionHoldMs, then release
};

// What ends a shot that never reaches its target weight.
enum class TimeLimit : uint8_t {
  MACHINE = 0,     // leave it to the machine's own time-out; the Bot does not press
  CONTROLLER = 1,  // press stop after maxShotTimeS
};

enum class RoastLevel : uint8_t { UNSET = 0, LIGHT, MEDIUM_LIGHT, MEDIUM, MEDIUM_DARK, DARK };

inline const char *roastLevelName(uint8_t level) {
  switch (static_cast<RoastLevel>(level)) {
    case RoastLevel::LIGHT: return "light";
    case RoastLevel::MEDIUM_LIGHT: return "medium-light";
    case RoastLevel::MEDIUM: return "medium";
    case RoastLevel::MEDIUM_DARK: return "medium-dark";
    case RoastLevel::DARK: return "dark";
    default: return "";
  }
}

// What is brewed is stored in three parts (core/Library.h puts them together):
//   Coffee     what it is: name, roaster, origin, roast
//   RecipeDef  how to pull it: dose, yield, time, pre-infusion
//   Preset     one coffee with one recipe on this grinder, and everything
//              learned for the pair: grind setting, stop offset
// The controller lists presets. A preset without a coffee is allowed.
// Fields are only ever added at the end of each (see encodeLibrary).

struct Coffee {
  uint16_t id = 0;                       // 0 = none; never reused
  char name[COFFEE_NAME_LEN] = {};       // e.g. "Idjwi Island"
  char roaster[COFFEE_NAME_LEN] = {};
  char origin[COFFEE_NAME_LEN] = {};     // country
  char region[ORIGIN_LEN] = {};
  char producer[ORIGIN_LEN] = {};
  char varietal[ORIGIN_LEN] = {};
  char process[PROCESS_LEN] = {};        // washed, natural, ...
  char notes[NOTES_LEN] = {};
  uint16_t altitudeM = 0;
  uint8_t roastLevel = 0;                // RoastLevel
  uint32_t roastDate = 0;                // the bag in use, YYYYMMDD, 0 = not entered
  // Where it stands with the shared library (core/Share.h). Kept by the
  // controller, never typed by the owner.
  uint8_t shareState = 0;                // ShareState
  uint32_t shareSeq = 0;                 // outbox number of the record last queued for it
  uint32_t shareHash = 0;                // of the description that record carried
  char reviewNote[NOTES_LEN] = {};       // the reviewer's words, if there were any
  char libraryId[LIBRARY_ID_LEN] = {};   // its name in the library once published
  // The bag in use went into the freezer on frozenDate and came out on
  // thawDate (YYYYMMDD; 0 = not frozen / still frozen). Frozen time does not
  // age the coffee (core/Community.h effectiveAgeDays). Added at the end of
  // the record: libraries saved before it read it as 0.
  uint32_t frozenDate = 0;
  uint32_t thawDate = 0;
  // The Bag in use (Library::bags). roastDate, frozenDate and thawDate above mirror that bag, so
  // everything that only needs the coffee in use reads them from here. 0: no bag yet.
  uint16_t bagId = 0;
  // The owner said the bag was finished on this day (YYYYMMDD; 0 = on the shelf). Its presets are
  // hidden from the controller's list until a new bag is started (core/Library.h finishCoffee).
  // Added at the end of the record: libraries saved before it read it as 0.
  uint32_t finishedDate = 0;
};

struct RecipeDef {
  uint16_t id = 0;
  char name[RECIPE_NAME_LEN] = "Double";
  float targetWeightG = 36.0f;
  bool useRatio = false;
  float doseG = 18.0f;
  float ratio = 2.0f;
  float targetTimeS = 28.0f;
  Preinfusion preinfusion = Preinfusion::MACHINE;
  uint16_t preinfusionHoldMs = 3000;
  TimeLimit timeLimit = TimeLimit::MACHINE;
  uint16_t maxShotTimeS = 50;
  float baselineOffsetG = 1.5f;
  uint16_t bloomPauseMs = 0;   // see Recipe
};

struct Preset {
  uint16_t recipeId = 0;
  uint16_t coffeeId = 0;       // 0 = no coffee attached
  uint16_t grindUm = 0;        // grinder setting for this pair, 0 = not entered
  uint8_t learnedAlpha = 0;    // hundredths; 0 = use Settings::learningFactor
  float stopOffsetG = 1.5f;    // learned; subtracted from the target
  uint16_t sharedGrindUm = 0;  // grind setting of the last dial-in sent to the library, 0 = none
  // The same two for the owner's other grinder, swapped in when it is put
  // in use (swapGrinders).
  uint16_t otherGrindUm = 0;
  uint16_t otherSharedGrindUm = 0;
  // Shots the stop offset has learned from since this pair was made (or its
  // offset reset): the first few learn in bigger steps (warm-up). Appended
  // last: an older stored library reads as 0, so its pairs warm up too.
  uint8_t learnedShots = 0;
};

// A grinder its owner has. A controller keeps the one in use and, at most,
// one other; every preset has a setting for each (Library::grinder is the
// one in use, Preset::grindUm its setting).
struct Grinder {
  uint16_t modelId = 0;                    // core/GrinderRegistry.h, 0 = not in it
  char model[GRINDER_NAME_LEN] = {};
  char burrs[GRINDER_BURRS_LEN] = {};
  char unit[GRINDER_UNIT_LEN] = "um";      // what a setting is read in, as shown beside it
  float umPerUnit = 1.0f;                  // microns of burr gap per unit, 0 = not known
  uint32_t zeroDate = 0;                   // YYYYMMDD it was last zeroed or its burrs changed
  // Goes up when the grinder is zeroed again, gets other burrs or is replaced:
  // a setting from before no longer means the same. Shots carry the number
  // they were made under and are only compared within it.
  uint16_t calibration = 0;
  uint16_t id = 0;                         // which of the owner's grinders; never reused
  // Where its description stands with the shared library, as for a coffee
  // (core/Share.h). Only a grinder that is not already a known model has
  // anything to send.
  uint8_t shareState = 0;                  // ShareState
  uint32_t shareSeq = 0;
  uint32_t shareHash = 0;
  char reviewNote[NOTES_LEN] = {};
  char libraryId[LIBRARY_ID_LEN] = {};     // its entry in the library's list of grinders
  // How a setting is read (core/SettingFormat.h), added at the end of the record. Settings are
  // always stored as whole steps; these say what a step looks like on the dial.
  uint8_t divisor = 1;                     // steps per displayed unit: 1, 2, 4, 5, 10, 20, 25, 50 or 100
  uint8_t subSteps = 0;                    // 0: one scale; 2 to 99: a second, inner ring with this many steps
};

// One bag of a coffee: what was bought, when it was roasted, whether it spent time in the freezer.
// A coffee keeps every bag it has had so that a new bag starts a fresh run of shots for grind advice
// and an earlier one can be put back in use. A shot remembers the bag it was made from.
constexpr size_t MAX_BAGS = 48;
struct Bag {
  uint16_t id = 0;             // never reused
  uint16_t coffeeId = 0;
  uint32_t roastDate = 0;      // YYYYMMDD, 0 = not entered
  uint32_t openedDate = 0;     // when the bag was opened, 0 = not entered
  uint32_t frozenDate = 0;     // went into the freezer; 0 = never
  uint32_t thawDate = 0;       // came out; 0 with a frozenDate = still frozen
  uint16_t weightG = 0;        // what the bag held, 0 = not entered
  // Added at the end of the record (the library stores its length): bags saved before read 0.
  // An unopened bag waits on the shelf (a subscription delivery, a bag bought ahead): nothing is
  // tracked for it until it is put in use (core/Library.h openBag).
  uint8_t unopened = 0;
  uint8_t spare = 0;
  uint16_t subscriptionId = 0; // the subscription it came with, 0 = none (core/Subscriptions.h)
};

struct Library {
  Coffee coffees[MAX_COFFEES];
  RecipeDef recipes[MAX_RECIPES];
  Preset presets[MAX_PRESETS];
  uint8_t coffeeCount = 0;
  uint8_t recipeCount = 0;
  uint8_t presetCount = 0;
  uint8_t activePreset = 0;    // last used; Start always uses this one
  uint16_t nextCoffeeId = 1;
  uint16_t nextRecipeId = 1;
  Grinder grinder;             // the one in use
  Grinder otherGrinder;        // the owner's other one, if hasOtherGrinder
  bool hasOtherGrinder = false;
  uint16_t nextCalibration = 1;
  uint16_t nextGrinderId = 1;
  Bag bags[MAX_BAGS];
  uint8_t bagCount = 0;
  uint16_t nextBagId = 1;
};

// One preset with its recipe and coffee filled in: what the brew controller,
// the display and the grind advisor work with. Made by resolvePreset(), never
// stored.
struct Recipe {
  char name[RECIPE_NAME_LEN] = "Double";
  float targetWeightG = 36.0f;
  float baselineOffsetG = 1.5f;  // seed used by "reset learned offset"
  float stopOffsetG = 1.5f;      // learned; subtracted from the target
  TimeLimit timeLimit = TimeLimit::MACHINE;
  uint16_t maxShotTimeS = 50;    // used when timeLimit is CONTROLLER
  // Ratio mode: target = dose x ratio (18 g at 1:2.0 -> 36 g in the cup).
  bool useRatio = false;
  float doseG = 18.0f;
  float ratio = 2.0f;
  // Adaptive learning factor in hundredths; 0 = not learned yet, use the
  // baseline from Settings::learningFactor.
  uint8_t learnedAlpha = 0;
  // Grind advice: the brew time this recipe should take.
  float targetTimeS = 28.0f;
  // Grinder burr distance for this recipe, microns. 0 = not entered.
  uint16_t grindUm = 0;
  // The coffee this recipe is for.
  char coffee[COFFEE_NAME_LEN] = {};   // roaster or product, e.g. "Portrait"
  char region[ORIGIN_LEN] = {};        // e.g. "Idjwi Island, Kivu"
  char varietal[ORIGIN_LEN] = {};      // e.g. "Bourbon, Caturra"
  uint8_t roastLevel = 0;      // RoastLevel
  uint32_t roastDate = 0;      // YYYYMMDD, 0 = not entered
  Preinfusion preinfusion = Preinfusion::MACHINE;
  uint16_t preinfusionHoldMs = 3000;
  uint16_t recipeId = 0;       // where it came from
  uint16_t coffeeId = 0;
  // Bloom, 0 = none. The button is held for preinfusionHoldMs and released,
  // pressed again as soon as the Bot is ready (about a second) to switch the
  // pump off, left for this long, then held for BLOOM_RESTART_HOLD_MS to
  // start the brew. Needs Preinfusion::HOLD: with the machine's own
  // pre-infusion there is no telling when to switch off.
  uint16_t bloomPauseMs = 0;
  uint16_t bagId = 0;          // the bag of the coffee in use, 0 = none
};

enum class CupMode : uint8_t {
  CUP_BEFORE_START = 0,  // cup is on the scale when Start is pressed; tare zeroes it
  CUP_AFTER_TARE = 1,    // tare the empty pan, wait for a cup, tare again
};

enum class ActuationMode : uint8_t {
  ATOMIC_PRESS = 0,  // Bot action 0x00: the Bot pushes and pulls back by itself
  DOWN_UP = 1,       // Bot action 0x03 then 0x04, hold time controlled here
};

struct Settings {
  // Brew
  bool learningEnabled = true;
  float learningFactor = 0.30f;      // EWMA alpha, 0.01..1.00 (1.00 = full correction)
  bool adaptiveLearning = false;     // Open Brew's selector tunes alpha per recipe
  float minValidWeightG = -5.0f;     // below this while brewing => cup removed
  uint16_t scaleTimeoutMs = 1500;    // no sample for this long => stale
  uint16_t dripObserveMs = 3000;     // post-shot drip observation period
  uint16_t minShotTimeMs = 3000;     // weight stop is ignored before this
  uint16_t noFlowTimeoutS = 20;      // start not confirmed by the scale => fault
  CupMode cupMode = CupMode::CUP_BEFORE_START;
  float cupMinWeightG = 10.0f;
  bool autoStartAfterTare = true;    // READY_TO_BREW continues without a second tap
  bool scaleTimer = true;            // drive the Lunar's own timer
  // SwitchBot
  ActuationMode actuationMode = ActuationMode::ATOMIC_PRESS;
  uint16_t pressDurationMs = 300;    // DOWN_UP hold time
  uint16_t releaseSettleMs = 1200;   // no new actuation until the arm is back
  uint8_t botHoldSeconds = 0;        // Bot-side hold time (cmd 0x0F 0x08), ATOMIC_PRESS
  // Grind advice (the grind setting itself is part of each preset)
  float grindToleranceS = 2.0f;      // brew time inside target +- this needs no change
  float grindUmPerS = 3.0f;          // starting guess until history gives a measured value
  uint8_t grindWindow = 3;           // shots in the moving average
  uint8_t grindMaxStepUm = 20;       // largest single recommendation
  GrindTimeBasis grindTimeBasis = GrindTimeBasis::TOTAL;
  bool requireCup = true;            // no Start until a cup has been seen placed on the scale
  // New settings are added below this line, at the end, so that settings
  // saved by an earlier build still load (see Store.cpp). Coffees, recipes
  // and presets are not settings: they are the Library.
  // Sharing with the community library (docs/COMMUNITY.md). Nothing is
  // queued for sending until shareEnabled is switched on.
  bool shareEnabled = false;
  bool shareCoffees = true;          // coffees added or changed
  bool shareDialIns = true;          // where a coffee and recipe settled
  bool shareShots = false;           // every shot that counts for grind advice
  bool shareRatings = false;         // taste ratings, with those shots
  // Grind advice steers by the time of the owner's best-rated shots of this
  // coffee and recipe, once there are enough (core/GrindAdvisor.h ratedTarget),
  // instead of the recipe's fixed target time.
  bool grindRatedTarget = true;
  // Retired 17 Oct 2026: "learn the grinder" was a switch for two days and is now part of the grind
  // advice itself (core/GrindAdvisor.h grindConfigForGrinder). Nothing reads these two bytes; they stay
  // so that settings saved by those builds are not longer than this record (a longer blob is refused).
  bool retiredLearnGrinder = true;
  uint8_t retiredLearnGrinderFlag = 0;
  // Keep the power bot connected while the scale is off, so the power
  // button presses at once (waking a sleeping SwitchBot from here took from
  // seconds to minutes). It is let go as soon as the scale is heard: measured
  // on the board, with the power bot held the scale's connection broke off
  // during its setup every time, and came up at the first try once the power
  // bot was let go. Costs the bot battery.
  bool keepPowerBotConnected = true;
  // The controller's screen turned half a turn: for a mounting with the
  // USB-C connector (and the touch key) on the right instead of the left.
  bool displayFlipped = false;
  // Minutes the scale may sit idle before the controller lets go of it, so that it can switch itself
  // off (core/ScaleRest.h). 0, as a record saved before this field reads, means the default (5);
  // 255 keeps it connected all the time.
  uint8_t scaleRestMin = 5;
  // The colour scheme of the web page and the controller's screen (UiTheme). 0, as a record saved
  // before this field reads, is Dark.
  uint8_t uiTheme = 0;
  // Switch the espresso machine off with the power bot when the scale switches off (only when the
  // machine is known to be on, never during a shot). false, as a record saved before it reads.
  bool powerOffWithScale = false;
  // Coffee ratings say which drinker gave them (core/CoffeeTaste.h Drinkers); off: all are the
  // household's. false, as a record saved before it reads.
  bool namedDrinkers = false;
};

// The colour schemes, in the order the web page lists them.
enum class UiTheme : uint8_t { DARK = 0, LIGHT, NORD, SOLARIZED_DARK, DRACULA, HIGH_CONTRAST };
constexpr uint8_t UI_THEME_COUNT = 6;

struct WeightSample {
  float weightG = 0.0f;
  uint32_t atMs = 0;       // local receive timestamp of the BLE notification
  uint32_t sequence = 0;   // increments per accepted notification
  int32_t scaleTimerMs = -1;  // the scale's own timer from the same frame; -1 if none
  uint8_t flags = 0;          // the scale's raw weight flag byte
};

// One completed shot. Fixed layout: this is also the on-flash history record.
struct ShotRecord {
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
  // History version 10: the bag it was made from (0 = before bags: matched by roastDate), and the
  // extraction the owner measured, TDS in hundredths of a percent (0 = not measured).
  uint16_t bagId = 0;
  uint16_t tdsX100 = 0;
};

// Extraction yield, percent of the dose that ended up in the cup: TDS x beverage mass / dose. 0 when
// there is no measured TDS or no dose.
inline float shotExtractionPct(const ShotRecord &s) {
  if (s.tdsX100 == 0 || !(s.doseG > 0.0f) || !(s.finalWeightG > 0.0f)) return 0.0f;
  return (s.tdsX100 / 100.0f) * s.finalWeightG / s.doseG;
}

// Two shots (or a shot and the recipe now in use) are of the same bag if their bag ids agree; one
// made before bags existed has none and is matched by the roast date it copied.
inline bool sameBag(uint16_t shotBagId, uint32_t shotRoastDate, uint16_t bagId, uint32_t roastDate) {
  if (shotBagId != 0 && bagId != 0) return shotBagId == bagId;
  return shotRoastDate == roastDate;
}

// Whether a shot is used for grind advice.
enum class GrindVote : uint8_t {
  AUTO = 0,    // the advisor decides: valid shots count, one-offs wait for a repeat
  IGNORE = 1,  // never use this shot
  COUNT = 2,   // use it, even if it looks like a one-off or ran out of time
};

inline const char *grindVoteName(uint8_t vote) {
  switch (static_cast<GrindVote>(vote)) {
    case GrindVote::IGNORE: return "ignore";
    case GrindVote::COUNT: return "count";
    default: return "";
  }
}

enum class Taste : uint8_t { UNSET = 0, SOUR, BALANCED, BITTER };

inline const char *tasteName(uint8_t taste) {
  switch (static_cast<Taste>(taste)) {
    case Taste::SOUR: return "sour";
    case Taste::BALANCED: return "balanced";
    case Taste::BITTER: return "bitter";
    default: return "";
  }
}


constexpr uint8_t SHOT_FLAG_FINAL_VALID = 1 << 0;
constexpr uint8_t SHOT_FLAG_LEARNED = 1 << 1;
constexpr uint8_t SHOT_FLAG_STOP_CONFIRMED = 1 << 2;
constexpr uint8_t SHOT_FLAG_STOP_RETRIED = 1 << 3;
// The stop press went out before any coffee had been seen, and turned out to
// start the pump (the start press had never reached the button).
constexpr uint8_t SHOT_FLAG_STOP_STARTED_PUMP = 1 << 4;

// Live values for the display, the web UI and the serial log.
struct LiveStatus {
  State state = State::BOOT;
  bool scaleConnected = false;
  bool botConnected = false;
  bool weightFresh = false;
  float weightG = 0.0f;
  float flowGS = 0.0f;
  float shotTimeS = 0.0f;
  float targetWeightG = 0.0f;
  float stopOffsetG = 0.0f;
  uint16_t samplePeriodMs = 0;   // measured, not assumed
  bool manualStopNeeded = false; // stop command could not be delivered/confirmed
  bool verifyingStop = false;    // watching that a blind stop press did not start the pump
  bool holdingButton = false;    // start press is being held for pre-infusion
  bool stopStartedPump = false;  // the last stop press turned out to start the pump
  // Machine profile (core/MachineProfile.h).
  uint8_t machineStyle = 0;      // MachineStyle
  bool targetCue = false;        // the shot is at its target and it is for the owner to stop it
  bool awaitingDrip = false;     // armed: the shot's clock starts with the first coffee
  bool cupPresent = false;       // a cup has been seen placed on the scale and is still there
  bool cupRequired = false;      // Start is held back until cupPresent
  uint8_t activeRecipe = 0;
  char recipe[RECIPE_NAME_LEN] = {};
  bool useRatio = false;
  float doseG = 0.0f;
  float ratio = 0.0f;
  bool firstDropSeen = false;    // coffee has reached the cup in this shot
  bool botPaired = false;        // filled in by the application layer
  // The Lunar's own timer, when its firmware reports it (17-byte packets and
  // timer events). Filled in by the application layer.
  bool scaleTimerValid = false;
  float scaleTimerS = 0.0f;
  int8_t scaleBatteryPercent = -1;  // -1: the scale has not reported it
  int8_t botBatteryPercent = -1;    // last value read from the Bot; -1: never read
  float bloomLeftS = 0.0f;       // BLOOMING: seconds of the pause still to go
};

}  // namespace bbw
