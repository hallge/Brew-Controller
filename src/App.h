#pragma once
// Thread-safe facade between the control task and the UI / web / serial
// front ends. Everything here may be called from any task.

#include <ArduinoJson.h>

#include "ble/Ble.h"
#include "core/BrewController.h"
#include "core/GrindAdvisor.h"
#include "core/Maintenance.h"
#include "core/SimilarCoffee.h"
#include "core/Library.h"
#include "core/Community.h"
#include "core/Share.h"
#include "core/Subscriptions.h"
#include "platform/LibraryLink.h"
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
  // The stop bot, on the stop button of a machine that has one.
  bool stopConnected = false;
  char stopAddress[bbw::ADDR_LEN] = {};
  bbw::PusherInfo stop;
  char stopProtocol[24] = {};
  char stopLabel[24] = {};
  const char *stopProblem = nullptr;
};

// Which bot something is about.
enum class BotRole : uint8_t { BREW = 0, POWER = 1, STOP = 2 };

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
// The idle scale (core/ScaleRest.h): the owner did something; whether the scale has been let go so
// that it can switch itself off; a start asked for while it rested, waiting for it to come back
// (start() asks for one then).
void noteActivity();
bool scaleResting();
bool scaleStartPending();
bool scaleNearby();  // not connected, but resting or heard in the last 30 s: Start waits for it
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
// A preset whose coffee's bag is finished is hidden (bbw::presetVisible): not listed, not selected,
// skipped by nextRecipe. Presets with no coffee are always shown.
bool presetVisible(uint8_t index);
void selectRecipe(uint8_t index);  // a hidden preset is not selected
void nextRecipe();                 // the next shown preset
// "Bag finished" for the active preset's coffee (bbw::finishCoffee, dated today by the clock); the
// next preset by roast age is selected. false: no coffee on it, or a shot is running.
bool finishActiveCoffee();
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
// What the grinder's other coffees showed (computed with grindAdvice; empty until they show something).
bbw::GrinderElasticity grinderElasticity();
void setGrind(uint16_t grindUm);                     // grinder setting of the active preset
bool setShotRating(uint32_t shotId, uint8_t rating, uint8_t taste);
bool setShotTds(uint32_t shotId, uint16_t tdsX100);

// Upkeep counters (core/Maintenance.h): shots since backflush, descale, grinder clean.
bbw::Maintenance maintenance();
void upkeepDone(bbw::Upkeep job);                       // the owner did it just now
void upkeepEvery(bbw::Upkeep job, uint32_t everyShots);  // remind after this many shots; 0 = never  // 0 clears; hundredths of a percent, up to 25.00 %
bool setShotGrindVote(uint32_t shotId, uint8_t vote);  // bbw::GrindVote
bool setShotGrind(uint32_t shotId, uint16_t grindUm);  // correct a stored shot

// Sharing with the community library. Loop task only.
struct ShareStatus {
  bool keyReady = false;
  char keyId[bbw::KEY_ID_LEN] = {};
  char algorithm[24] = {};
  bool canSend = false;      // the library can be reached right now
  bool tokenSet = false;     // a token is stored (it is never handed back)
  bbw::LibraryLinkStatus link;
  uint32_t waiting = 0;
  uint32_t sent = 0;
};
ShareStatus shareStatus();
// The library's repository ("owner/name") and the token for it. An empty
// token keeps the one already stored; an empty repository forgets both.
void setLibraryLink(const char *repo, const char *token);
// Queues a coffee's description if sharing is on and the library does not
// already have this version. asked: the owner pressed the button, so the
// "coffees I add or change" setting does not have to be on.
bool shareCoffee(uint16_t coffeeId, bool asked = false);
uint8_t shareAllCoffees();            // queue every coffee on the shelf; how many
// Forgets what was sent and queues every coffee and grinder the owner has
// described, the key first. For when earlier sending went nowhere.
uint8_t shareEverythingAgain();
// Queues a grinder's description (the one in use or the other, by its id), if
// sharing is on and it is not a model already known.
bool shareGrinder(uint16_t grinderId);

// What the shared library says about a coffee and recipe on this shelf, from
// the copy kept on the controller, and the grind setting to start from on
// this grinder (core/Community.h). Loop task only.
struct CommunityView {
  bbw::CommunityDialin dialin;
  bbw::CommunityFit fit;
  bbw::CommunityModel model;   // this grinder's model, from all its owners (index/models.json)
  bbw::CommunityStart start;
  bbw::AgeModel age;
  int myAgeDays = -1;          // this controller's bag
};

// The age of the coffee being brewed, and what that means for the grind.
struct BeanAge {
  int ageDays = -1;            // days off roast today, less days frozen; -1: no roast date, or no clock
  int daysSinceBrewed = -1;    // since the last shot of this coffee and recipe that counted; -1: none
  bool rateKnown = false;      // the library has measured how fast grind follows age
  int shift = 0;               // expected change of setting since that shot, grinder's unit; negative = finer
  bool frozenNow = false;      // the bag in use is in the freezer today (it is not ageing)
  int frozenDays = 0;          // days it has spent there, not counted in ageDays
};
BeanAge beanAge();  // for the active preset; loop task only
CommunityView community(uint16_t coffeeId, uint16_t recipeId);
// For the active preset, remembered for a few seconds: cheap to ask often.
bbw::CommunityStart communityStartForActive();
// A place to start a coffee that has no setting yet, from the owner's most similar coffee
// (core/SimilarCoffee.h). Loop task only.
bbw::SimilarStart similarStart(uint16_t coffeeId, uint16_t recipeId);
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
void pairStop(const char *address, uint8_t type);
// The keys a pusher needs (Tuya, MicroBot), per bot.
bbw::PusherSecrets pusherSecrets(BotRole role);
void applyPusherSecrets(BotRole role, const bbw::PusherSecrets &secrets);

// The espresso machine: how its shot is started and stopped (core/MachineProfile.h).
bbw::MachineProfile machineProfile();
// false: refused (a shot is running). Stored, and used from the next shot.
bool applyMachine(const bbw::MachineProfile &machine);
// Where the machine profile stands with the shared library (bbw::ShareState).
bbw::ShareState machineShareState();
// Queues the owner's own machine profile for the shared library, if sharing
// is on and it is new or changed. false: nothing to send.
bool shareMachine();
// The library's list of machine profiles as last read (JSON text); "" if never read.
const char *communityMachines();

// Machine power
PowerView powerView();
bool powerRequest(bool on);       // false: refused, powerView().lastOutcome says why
// Coffee taste (core/CoffeeTaste.h): a coffee with this id is in the library; a rating was saved
// (it is shared from the main loop when sharing is on).
bool coffeeExists(uint16_t id);
void ratingSaved(uint32_t id);
// A rating was deleted; wasShared: it had gone (or was going) to the library, which is told to drop it.
void ratingDeleted(uint32_t id, bool wasShared);
const char *communityTaste();   // index/taste.json as cached; good until the next library read
bool powerPressAnyway();
// One press of the brew bot, to see which pusher it is. False if a shot is in
// hand or no brew bot is paired. Progress: "none", "waiting", "pressed", "failed".
bool testPressBrew();
// Connects a bot without pressing, so it reports battery and firmware.
bool wakeBot(BotRole role);
// The radio test switch: zero the heard counts, and report them.
void resetHeard();
void radioTestReport(JsonObject out);
// GET /api/diagnostics: firmware, memory, stacks, Wi-Fi, Bluetooth links and
// their connection statistics, storage, update state.
void diagnostics(JsonObject out);
// Diagnostics reports (docs/TELEMETRY.md). The next daily report's body, as it would be sent now.
void diagPreview(String &out);
// Builds a report now and sends it as soon as it can; false when diagnostics are off.
bool diagSendNow();
// A problem the owner describes goes with a diagnostics report, sent at once. False: diagnostics are off.
bool diagReportBug(const char *kind, const char *text, const char *contact);
// On or off, the relay, how many wait, when the last went and how.
void diagStatus(JsonObject out);
// The last crash's report body (JSON); 0 when there has been none.
size_t lastCrash(char *out, size_t size);
// Empty: the built-in relay.
void setDiagRelay(const char *url);
// The scale has been seen taring itself (an auto-tare mode).
bool scaleZeroesItself();
const char *testPressState();
// Saves the log and restarts the controller. Loop task.
void restartSoon();
void powerBelieve(bbw::PowerBelief belief);
void applyPowerSchedule(const bbw::PowerScheduleEntry *entries, int16_t utcOffsetMin);

// Subscriptions (core/Subscriptions.h), kept in /subs.json.
// Runs `visit` on the list with the lock held: keep it short.
void readSubscriptions(void (*visit)(const bbw::Subscription *subs, size_t count, void *), void *context);
// Adds one (id 0) or replaces it. Returns its id; 0: the list is full, or no such one.
uint16_t saveSubscription(const bbw::Subscription &s);
bool deleteSubscription(uint16_t id);
// A delivery arrived (supplies: added to the stock), or one was skipped: the next date moves on.
bool subscriptionArrived(uint16_t id);
bool subscriptionSkipped(uint16_t id);
// Shots a day over the last 14 days, for how long supplies last. Loop task (reads flash; cached).
float shotsPerDay();
// One subscription as JSON (the stored fields), and read back from what the page sent.
void subscriptionToJson(const bbw::Subscription &s, JsonObject o);
void subscriptionFromJson(JsonObjectConst o, bbw::Subscription &s);
// A shop's page for a card: asked for by the web page, read by the loop task while idle.
enum class PreviewState : uint8_t { NONE, WAITING, DONE, FAILED };
// The owner's say for recommendations: their country (two letters, "" = not said) and the sourcing
// values that matter to them (a SERVICE_SOURCING mask).
struct SubscriptionPrefs {
  char country[3] = {};
  uint16_t matters = 0;
};
SubscriptionPrefs subscriptionPrefs();
void setSubscriptionPrefs(const SubscriptionPrefs &p);
// Sends a service's description to the shared library for review. false: sharing is off, it cannot
// be shared (no name or https link), or nothing has changed since it was sent.
bool shareService(uint16_t id);
uint32_t outboxSent();
// Reviews of services (core/Subscriptions.h ServiceReview), kept in /svcreviews.json. One per drinker
// per subscription: saving again replaces it. Shared, when ratings are, for a service in the library.
void readServiceReviews(void (*visit)(const bbw::ServiceReview *reviews, size_t count, void *), void *context);
uint32_t saveServiceReview(const bbw::ServiceReview &r);
bool deleteServiceReview(uint32_t id);
// The library's list of services (index/services.json) as cached. Good until the next library read.
const char *communityServices();
void requestPagePreview(const char *url);
PreviewState pagePreview(char *url, size_t urlSize, bbw::PageMeta &meta, char *problem, size_t problemSize);

// Network / history
bbw::NetConfig netConfig();
void applyNetConfig(const bbw::NetConfig &net);  // takes effect after restart
bbw::Store &store();
uint32_t epochNow();
void setEpoch(uint32_t epochS);

}  // namespace app
