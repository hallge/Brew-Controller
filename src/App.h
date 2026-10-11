#pragma once
// Thread-safe facade between the control task and the UI / web / serial
// front ends. Everything here may be called from any task.

#include "ble/Ble.h"
#include "core/BrewController.h"
#include "core/GrindAdvisor.h"
#include "core/Library.h"
#include "core/Community.h"
#include "core/Share.h"
#include "core/Types.h"
#include "storage/Store.h"

namespace app {

struct Devices {
  bool scaleConnected = false;
  bool botConnected = false;
  char scaleName[24] = {};
  char scaleProtocol[24] = {};
  bbw::PusherInfo bot;
  uint8_t botLastStatus = 0;
  bbw::Pairing pairing;
  bbw::ScaleDiag scale;
  // Which make the brew bot is (known once it has connected), what it can
  // do, and what stops it from working, if anything.
  char botProtocol[24] = {};
  char botLabel[24] = {};
  uint32_t botCaps = 0;
  const char *botProblem = nullptr;
  // The power bot, on the machine's power button.
  bool powerConnected = false;
  char powerAddress[bbw::ADDR_LEN] = {};
  bbw::PusherInfo power;
  uint8_t powerLastStatus = 0;
  char powerProtocol[24] = {};
  char powerLabel[24] = {};
  uint32_t powerCaps = 0;
  const char *powerProblem = nullptr;
};

// The machine's power button (core/PowerControl.h).
struct PowerView {
  bool paired = false;
  bbw::PowerBelief belief = bbw::PowerBelief::UNKNOWN;
  bool working = false;             // a press is waiting for the bot or on its way
  const char *lastOutcome = "";     // of the last request, in words
  bool lastWasSchedule = false;
  bbw::PowerScheduleEntry schedule[bbw::POWER_SCHEDULE_ENTRIES];
  int16_t utcOffsetMin = 0;
};

bbw::LiveStatus status();
bool lastShot(bbw::ShotRecord &out);
// Copies the last shot's weight curve; returns the number of points.
size_t lastCurve(float *timeS, float *weightG, size_t max);

// Intents
void start();
void stop();
void tare();
void acknowledge();
void manualPress();

// Settings. apply() sanitizes, swaps in atomically and schedules a save.
bbw::Settings settings();
void applySettings(const bbw::Settings &settings);

// Presets: what the controller lists, each a recipe with or without a coffee
// (core/Library.h). They are handed out as flat bbw::Recipe copies; the
// library itself is too big to pass around by value.
uint8_t presetCount();
uint8_t activePreset();
bbw::Recipe preset(uint8_t index);
bbw::Recipe activeRecipe();
void selectRecipe(uint8_t index);
void nextRecipe();
void adjustTarget(float deltaG);
void resetLearnedOffset(uint8_t presetIndex);
void resetLearnedFactors();  // every preset back to the baseline learning factor
// One preset edited. Changes to recipe or coffee fields reach every preset
// that shares that recipe or coffee. false: refused (shot running, list full).
bool applyPresetEdit(uint8_t index, const bbw::Recipe &edited);
// The whole list replaced (bbw::applyPresetList). Loop task only.
bool applyPresetList(const bbw::Recipe *flat, const uint8_t *slots, uint8_t count,
                     uint8_t active);
// Changes the library through `edit`, called with the lock held. If it
// returns false, or a shot is running, nothing changes.
bool editLibrary(bool (*edit)(bbw::Library &, void *), void *context);
// Runs `visit` on the library with the lock held: keep it short.
void readLibrary(void (*visit)(const bbw::Library &, void *), void *context);

bbw::Grinder grinder();

// Grinder. Advice is for the active preset and is cached; call from the
// loop task only (it reads shot history from flash when the cache is stale).
bbw::GrindAdvice grindAdvice();
void setGrind(uint16_t grindUm);                     // grinder setting of the active preset
bool setShotRating(uint32_t shotId, uint8_t rating, uint8_t taste);
bool setShotGrindVote(uint32_t shotId, uint8_t vote);  // bbw::GrindVote
bool setShotGrind(uint32_t shotId, uint16_t grindUm);  // correct a stored shot

// Sharing with the community library. Loop task only.
struct ShareStatus {
  bool keyReady = false;
  char keyId[bbw::KEY_ID_LEN] = {};
  char algorithm[24] = {};
  bool canSend = false;      // there is somewhere to send to
  const char *sendsTo = "";  // in words
  uint32_t waiting = 0;
  uint32_t sent = 0;
};
ShareStatus shareStatus();
// Queues a coffee's description if sharing is on and the library does not
// already have this version. asked: the owner pressed the button, so the
// "coffees I add or change" setting does not have to be on.
bool shareCoffee(uint16_t coffeeId, bool asked = false);
uint8_t shareAllCoffees();            // queue every coffee on the shelf; how many
// Queues a grinder's description (the one in use or the other, by its id), if
// sharing is on and it is not a model already known.
bool shareGrinder(uint16_t grinderId);

// What the shared library says about a coffee and recipe on this shelf, from
// the copy kept on the controller, and the grind setting to start from on
// this grinder (core/Community.h). Loop task only.
struct CommunityView {
  bbw::CommunityDialin dialin;
  bbw::CommunityFit fit;
  bbw::CommunityStart start;
};
CommunityView community(uint16_t coffeeId, uint16_t recipeId);
// For the active preset, remembered for a few seconds: cheap to ask often.
bbw::CommunityStart communityStartForActive();
// The library's list of coffees as last read (JSON text); "" if never read.
const char *communityCoffees();
// The library's list of grinder models as last read; "" if never read.
const char *communityGrinders();
bool libraryReachable();
void readLibrarySoon();  // a coffee from the library was just added: fetch its figures now

// Devices / pairing
Devices devices();
size_t seenDevices(bbw::SeenDevice *out, size_t max);
void discover();  // scan for a while so the pairing list fills in
void pairScale(const char *address, uint8_t type);
void pairBot(const char *address, uint8_t type);
bool pairStrongestBot();
void pairPower(const char *address, uint8_t type);
// The keys a pusher needs (Tuya, MicroBot); power: the power bot's, else the brew bot's.
bbw::PusherSecrets pusherSecrets(bool power);
void applyPusherSecrets(bool power, const bbw::PusherSecrets &secrets);

// Machine power
PowerView powerView();
bool powerRequest(bool on);       // false: refused, powerView().lastOutcome says why
bool powerPressAnyway();
void powerBelieve(bbw::PowerBelief belief);
void applyPowerSchedule(const bbw::PowerScheduleEntry *entries, int16_t utcOffsetMin);

// Network / history
bbw::NetConfig netConfig();
void applyNetConfig(const bbw::NetConfig &net);  // takes effect after restart
bbw::Store &store();
uint32_t epochNow();
void setEpoch(uint32_t epochS);

}  // namespace app
