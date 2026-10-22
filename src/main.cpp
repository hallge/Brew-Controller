// Brew-by-weight controller for the Breville Dual Boiler.
//   scale    : Acaia Lunar over BLE
//   actuator : a button pusher over BLE (SwitchBot Bot, or another make with a
//              driver in core/PusherLink.h), pressing the machine's brew button
//   power    : optionally a second one on the machine's power button
//   board    : LILYGO T-Display S3 AMOLED Plus
//
// Tasks
//   control (core 1, prio 5) : 10 ms tick. Drains scale samples, runs the brew
//                              state machine and the Bot press sequencer.
//   ble     (core 0, prio 2) : scanning and (re)connecting, the blocking part.
//   loop    (core 1, prio 1) : display, web server, serial, flash writes.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <atomic>
#include <esp_task_wdt.h>
#include <sys/time.h>
#ifndef BBW_EMULATOR
#include <WiFi.h>
#include <esp_ota_ops.h>
#endif

#include "App.h"
#include "Log.h"
#include "Version.h"
#include "ble/Ble.h"
#include "core/BrewController.h"
#include "core/SettingsUtil.h"
#include "net/ShotFormat.h"
#include "net/Web.h"
#include "platform/BigMem.h"
#include "platform/DeviceKey.h"
#include "platform/FirmwareUpdate.h"
#include "platform/LibraryLink.h"
#include "platform/PagePreview.h"
#include "storage/Store.h"
#include "ui/Ui.h"

using bbw::State;



namespace {

constexpr int BOOT_BUTTON_PIN = 0;
constexpr uint32_t CONTROL_PERIOD_MS = 10;
constexpr uint32_t SHOT_IN_PROGRESS_MAGIC = 0xB7E55107;
constexpr uint32_t SETTINGS_SAVE_DELAY_MS = 1500;
// The Bot link is dropped this long after the shot no longer needs it.
constexpr uint32_t BOT_LINGER_MS = 10000;
constexpr uint32_t BOT_PAIR_CHECK_MS = 20000;
// Switching the Lunar on is the cue that a shot is coming: the Bot is woken
// then, so Start does not have to wait for it. Released again if unused.
constexpr uint32_t BOT_PREWAKE_MS = 120000;
constexpr uint32_t LONG_PRESS_MS = 1200;

// Survives a panic / watchdog / software reset, not a power cycle.
RTC_NOINIT_ATTR uint32_t g_shotInProgress;

bbw::Settings g_settings;
bbw::Library g_library;
// The library the controller brews from stays inside the chip, with the
// controller. Its two working copies are only ever touched by the loop task
// and live in PSRAM (platform/BigMem.h), as do the other large buffers below:
// allocateBuffers() in setup() makes them.
bbw::Library *g_libraryBefore;  // the library as an edit found it
bbw::Library *g_librarySaving;  // copy taken under the lock, written outside it
bbw::NetConfig g_net;
bbw::Store g_store;
bbw::BleScale g_scale;
bbw::BlePusher g_bot;    // brew button
bbw::BlePusher g_power;  // power button
bbw::BlePusher g_stop;   // stop button, on a machine with separate start and stop buttons
bbw::StopBotConfig g_stopBot;
std::atomic<bool> g_stopBotDirty{false};
bbw::PusherConfig g_pushers;
bbw::MachineProfile g_machine;  // guarded by g_lock, like the settings
std::atomic<bool> g_machineDirty{false};
bbw::PowerControl g_powerControl;
bbw::ScaleRest g_scaleRest;  // control task, under the lock
std::atomic<bool> g_pushersDirty{false};
std::atomic<uint32_t> g_ratingToShare{0};  // a coffee rating just saved, for the main loop to share
// Power presses that ended, waiting to be logged by the main loop (control task, under the lock).
bbw::PowerEvent g_powerEventQueue[4];
uint8_t g_powerEventsQueued = 0;
std::atomic<uint32_t> g_powerHoldUntilMs{0};  // keep the power bot's link up until then
std::atomic<uint32_t> g_stopHoldUntilMs{0};   // and the stop bot's (pairing check, new key)
bbw::BleManager g_ble(g_scale, g_bot, g_power, g_stop);
bbw::BrewController g_controller(g_scale, g_bot, g_settings, g_library);

SemaphoreHandle_t g_lock;  // guards g_controller, g_settings and g_library
uint32_t g_bootCount = 0;  // starts of this controller, kept in NVS

// ---- test press (Setup page): which pusher is on which button -------------
// Asked for by the page, done by the control task once the brew bot's link is
// up, and only while nothing is brewing or being set up.
enum : uint8_t { TEST_NONE, TEST_WAITING, TEST_PRESSED, TEST_FAILED };
constexpr uint32_t TEST_PRESS_PATIENCE_MS = 40000;  // a sleeping SwitchBot advertises only every 10-15 s
std::atomic<uint8_t> g_testPress{TEST_NONE};
std::atomic<uint32_t> g_testPressUntilMs{0};
bool g_testPressTidy = false;  // control task: clear the press result once the arm is back

#ifndef BBW_EMULATOR
TaskHandle_t g_controlTask = nullptr, g_bleTask = nullptr;
#endif
bool g_settingsDirty = false;
bool g_libraryDirty = false;
bbw::Maintenance g_maintenance;     // guarded by Lock
bool g_maintenanceDirty = false;
bool g_grindAdviceStale = true;  // loop task only
bbw::GrinderElasticity g_grinderElasticity;  // with grindAdvice(): what the grinder's other coffees showed
bbw::GrindAdvice g_grindAdvice;
uint32_t g_lastCountedEpochS = 0;  // newest shot behind g_grindAdvice; loop task only
std::atomic<uint32_t> g_botHoldUntilMs{0};  // keep the Bot link up until then
uint32_t g_settingsDirtyAtMs = 0;

struct Lock {
  Lock() { xSemaphoreTake(g_lock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_lock); }
};

void markSettingsDirty() {
  g_grindAdviceStale = true;
  g_settingsDirty = true;
  g_settingsDirtyAtMs = millis();
}

// Both are written by the same delayed save in loop().
void markLibraryDirty() {
  g_grindAdviceStale = true;
  g_libraryDirty = true;
  g_settingsDirtyAtMs = millis();
}

bool editable() {  // call with the lock held
  return g_controller.state() == State::READY || g_controller.state() == State::CONNECTING_SCALE;
}

// ---------------------------------------------------------------- sharing --
// Records for the community library are signed and put in the outbox as they
// arise, and sent from there one at a time while the controller is idle.
// All of it runs on the loop task.

// Where records go and the index comes from: platform/LibraryLink.h. On the
// board, the library's repository over HTTPS; in the emulator, a folder.
bool publishRecord(const char *path, const char *text) { return bbw::libraryPut(path, text); }

constexpr uint32_t SHARE_SEND_PERIOD_MS = 1000;
constexpr size_t LIBRARY_TEXT_SIZE = 16384;
char *g_shareBody;      // SHARE_BODY_MAX
char *g_shareEnvelope;  // SHARE_ENVELOPE_MAX
// Whatever was last read from the library, or from its copy on flash. Enough
// for a list of about a hundred coffees; a bigger library will need searching
// on its side, not a whole list here.
char *g_libraryText;    // LIBRARY_TEXT_SIZE
bbw::CurvePoint *g_curveOut;  // MAX_CURVE_POINTS: a shot's readings on their way to flash

// Subscriptions (core/Subscriptions.h): the list, and /subs.json written from it when it changes.
constexpr const char *SUBS_FILE = "/subs.json";
constexpr size_t SUBS_TEXT_SIZE = 24576;
bbw::Subscription *g_subs;    // MAX_SUBSCRIPTIONS
size_t g_subCount = 0;
uint16_t g_nextSubId = 1;
bool g_subsDirty = false;
char *g_subsText;             // SUBS_TEXT_SIZE
// The page fetch for a card: one at a time.
struct PreviewJob {
  char url[256];
  app::PreviewState state;
  bbw::PageMeta meta;
  char problem[80];
};
PreviewJob *g_preview;
// Reviews of services, and the owner's say for recommendations.
constexpr const char *SERVICE_REVIEWS_FILE = "/svcreviews.json";
bbw::ServiceReview *g_reviews;   // MAX_SERVICE_REVIEWS
size_t g_reviewCount = 0;
uint32_t g_nextReviewId = 1;
bool g_reviewsDirty = false;
app::SubscriptionPrefs g_subPrefs;

void allocateBuffers() {
  g_libraryBefore = bbw::bigNew<bbw::Library>();
  g_librarySaving = bbw::bigNew<bbw::Library>();
  g_shareBody = bbw::bigArray<char>(bbw::SHARE_BODY_MAX);
  g_shareEnvelope = bbw::bigArray<char>(bbw::SHARE_ENVELOPE_MAX);
  g_libraryText = bbw::bigArray<char>(LIBRARY_TEXT_SIZE);
  g_curveOut = bbw::bigArray<bbw::CurvePoint>(bbw::MAX_CURVE_POINTS);
  g_subs = bbw::bigArray<bbw::Subscription>(bbw::MAX_SUBSCRIPTIONS);
  for (size_t i = 0; i < bbw::MAX_SUBSCRIPTIONS; ++i) new (&g_subs[i]) bbw::Subscription();
  g_subsText = bbw::bigArray<char>(SUBS_TEXT_SIZE);
  g_preview = bbw::bigNew<PreviewJob>();
  g_reviews = bbw::bigArray<bbw::ServiceReview>(bbw::MAX_SERVICE_REVIEWS);
  for (size_t i = 0; i < bbw::MAX_SERVICE_REVIEWS; ++i) new (&g_reviews[i]) bbw::ServiceReview();
  g_preview->state = app::PreviewState::NONE;
}
char g_sharePath[96];
// Reading the library's index (further down): do not wait out the period,
// something new is wanted.
bool g_libraryReadSoon = true;
bbw::Coffee g_shareCoffee;

uint32_t wallClock() {
  const time_t now = time(nullptr);
  return now > 1700000000 ? static_cast<uint32_t>(now) : 0;
}

// "2026/10" for the folders records are filed under; "undated" with no clock.
void monthFolder(uint32_t epochS, char out[12]) {
  char at[21];
  bbw::timeText(epochS, at);
  if (!at[0]) {
    strlcpy(out, "undated", 12);
    return;
  }
  snprintf(out, 12, "%.4s/%.2s", at, at + 5);
}

// The key goes into the outbox first, once: without it nothing after can be
// checked. Uses g_shareBody.
bool ensureKeyQueued() {
  bbw::ISigner &signer = bbw::deviceSigner();
  if (!signer.ready()) return false;
  if (g_store.outboxKeyQueued()) return true;
  char kid[bbw::KEY_ID_LEN], keyPath[48];
  bbw::keyId(signer.publicKey(), kid);
  snprintf(keyPath, sizeof(keyPath), "keys/%s.json", kid);
  return bbw::buildKeyRecord(signer, wallClock(), g_shareBody, bbw::SHARE_BODY_MAX) != 0 &&
         bbw::sealRecord(signer, g_shareBody, g_shareEnvelope, bbw::SHARE_ENVELOPE_MAX) != 0 &&
         g_store.outboxAdd(keyPath, g_shareEnvelope);
}

// Signs the record in g_shareBody and queues it under `path`. The key must
// be in already (ensureKeyQueued, before the record is built).
bool shareQueue(const char *path) {
  bbw::ISigner &signer = bbw::deviceSigner();
  if (!signer.ready() || !g_store.outboxKeyQueued()) return false;
  if (bbw::sealRecord(signer, g_shareBody, g_shareEnvelope, bbw::SHARE_ENVELOPE_MAX) == 0) {
    return false;
  }
  return g_store.outboxAdd(path, g_shareEnvelope);
}

bool copyCoffee(uint16_t id) {  // into g_shareCoffee
  Lock lock;
  const bbw::Coffee *coffee = bbw::findCoffee(g_library, id);
  if (coffee) g_shareCoffee = *coffee;
  return coffee != nullptr;
}

// Queues a coffee's description, unless the library already has this very
// version of it.
bool queueCoffee(uint16_t id) {
  if (!copyCoffee(id)) return false;
  // Sent before and unchanged: nothing new to send, unless the library refused it (then it may go again).
  if (g_shareCoffee.shareState != 0 &&
      g_shareCoffee.shareState != static_cast<uint8_t>(bbw::ShareState::REFUSED) &&
      !bbw::coffeeChangedSinceShared(g_shareCoffee)) {
    return false;
  }
  if (!ensureKeyQueued()) return false;
  char kid[bbw::KEY_ID_LEN];
  bbw::keyId(bbw::deviceSigner().publicKey(), kid);
  static uint32_t seq;
  static uint16_t coffeeId;
  seq = g_store.outboxNext();
  coffeeId = id;
  if (bbw::buildCoffeeRecord(g_shareCoffee, seq, wallClock(), g_shareBody, bbw::SHARE_BODY_MAX) == 0) {
    return false;
  }
  // The record's number is in its name: the library's verdict refers to it.
  snprintf(g_sharePath, sizeof(g_sharePath), "coffees/%s/c%u-%u.json", kid,
           static_cast<unsigned>(id), static_cast<unsigned>(seq));
  if (!shareQueue(g_sharePath)) return false;
  app::editLibrary([](bbw::Library &lib, void *) {
    bbw::Coffee *coffee = bbw::findCoffee(lib, coffeeId);
    if (!coffee) return false;
    bbw::markCoffeeQueued(*coffee, seq);
    return true;
  }, nullptr);
  return true;
}

// Queues a grinder's description, for a grinder that is not a known model.
bool queueGrinder(uint16_t id) {
  static bbw::Grinder grinder;
  static uint16_t grinderId;
  static uint32_t seq;
  static bool found;
  grinderId = id;
  found = false;
  app::readLibrary([](const bbw::Library &lib, void *) {
    if (lib.grinder.id == grinderId) {
      grinder = lib.grinder;
      found = true;
    } else if (lib.hasOtherGrinder && lib.otherGrinder.id == grinderId) {
      grinder = lib.otherGrinder;
      found = true;
    }
  }, nullptr);
  if (!found || !bbw::grinderCanBeShared(grinder) || !ensureKeyQueued()) return false;
  char kid[bbw::KEY_ID_LEN];
  bbw::keyId(bbw::deviceSigner().publicKey(), kid);
  seq = g_store.outboxNext();
  if (bbw::buildGrinderRecord(grinder, seq, wallClock(), g_shareBody, bbw::SHARE_BODY_MAX) == 0) {
    return false;
  }
  snprintf(g_sharePath, sizeof(g_sharePath), "grinders/%s/g%u-%u.json", kid,
           static_cast<unsigned>(id), static_cast<unsigned>(seq));
  if (!shareQueue(g_sharePath)) return false;
  app::editLibrary([](bbw::Library &lib, void *) {
    bbw::Grinder *mine = bbw::findGrinder(lib, grinderId);
    if (!mine) return false;
    bbw::markGrinderQueued(*mine, seq);
    return true;
  }, nullptr);
  return true;
}

// What the library says about this controller's coffees: in review,
// published, declined. Asked for now and then while idle.
bool fetchStatus(const char *kid, char *out, size_t size) {
  char path[48];
  snprintf(path, sizeof(path), "status/%s.json", kid);
  return bbw::libraryGet(path, out, size);
}

// Asking is a request to GitHub on the board: often enough to notice a
// reviewer's decision the same day, not so often as to keep the radio busy.
#ifdef BBW_EMULATOR
constexpr uint32_t SHARE_STATUS_PERIOD_MS = 15000;
#else
constexpr uint32_t SHARE_STATUS_PERIOD_MS = 600000;
#endif

void shareReadStatus(bool idle) {
  static uint32_t lastMs = 0;
  if (!idle || !bbw::deviceSigner().ready() || !bbw::libraryLinkReady()) return;
  if (lastMs != 0 && millis() - lastMs < SHARE_STATUS_PERIOD_MS) return;
  lastMs = millis();
  if (!app::settings().shareEnabled) return;
  char kid[bbw::KEY_ID_LEN];
  bbw::keyId(bbw::deviceSigner().publicKey(), kid);
  if (!fetchStatus(kid, g_libraryText, LIBRARY_TEXT_SIZE)) return;
  static JsonDocument doc;
  if (deserializeJson(doc, g_libraryText) || doc["v"] != 1) {
    LOGW("share: status from the library not understood");
    return;
  }
  const bool changed = app::editLibrary([](bbw::Library &lib, void *) {
    bool any = false;
    for (JsonObjectConst entry : doc["coffees"].as<JsonArrayConst>()) {
      const char *name = entry["state"] | "";
      const bbw::ShareState state = !strcmp(name, "review") ? bbw::ShareState::IN_REVIEW
                                    : !strcmp(name, "published") ? bbw::ShareState::PUBLISHED
                                    : !strcmp(name, "declined") ? bbw::ShareState::DECLINED
                                    : !strcmp(name, "refused") ? bbw::ShareState::REFUSED
                                    : bbw::ShareState::LOCAL;
      any |= bbw::applyCoffeeReview(lib, entry["local_id"] | 0, entry["record"] | 0u, state,
                                    entry["note"] | "", entry["library_id"] | "");
    }
    for (JsonObjectConst entry : doc["grinders"].as<JsonArrayConst>()) {
      const char *name = entry["state"] | "";
      const bbw::ShareState state = !strcmp(name, "review") ? bbw::ShareState::IN_REVIEW
                                    : !strcmp(name, "published") ? bbw::ShareState::PUBLISHED
                                    : !strcmp(name, "declined") ? bbw::ShareState::DECLINED
                                    : !strcmp(name, "refused") ? bbw::ShareState::REFUSED
                                    : bbw::ShareState::LOCAL;
      any |= bbw::applyGrinderReview(lib, entry["local_id"] | 0, entry["record"] | 0u, state,
                                     entry["note"] | "", entry["library_id"] | "");
    }
    return any;  // nothing new: leave the library unsaved
  }, nullptr);
  // And about the machine profile, which is not part of the library record.
  for (JsonObjectConst entry : doc["machines"].as<JsonArrayConst>()) {
    const char *name = entry["state"] | "";
    const bbw::ShareState state = !strcmp(name, "review") ? bbw::ShareState::IN_REVIEW
                                  : !strcmp(name, "published") ? bbw::ShareState::PUBLISHED
                                  : !strcmp(name, "declined") ? bbw::ShareState::DECLINED
                                  : !strcmp(name, "refused") ? bbw::ShareState::REFUSED
                                  : bbw::ShareState::LOCAL;
    if ((entry["local_id"] | 0) != 1) continue;
    bool news = false;
    {
      Lock lock;
      news = bbw::applyReviewTo(&g_machine, entry["record"] | 0u, state, entry["note"] | "",
                                entry["library_id"] | "");
    }
    if (news) {
      g_machineDirty = true;
      LOGI("share: the library on the machine profile: %s", name);
    }
  }
  // And about the subscription services this controller described.
  for (JsonObjectConst entry : doc["services"].as<JsonArrayConst>()) {
    const char *name = entry["state"] | "";
    const bbw::ShareState state = !strcmp(name, "review") ? bbw::ShareState::IN_REVIEW
                                  : !strcmp(name, "published") ? bbw::ShareState::PUBLISHED
                                  : !strcmp(name, "declined") ? bbw::ShareState::DECLINED
                                  : !strcmp(name, "refused") ? bbw::ShareState::REFUSED
                                  : bbw::ShareState::LOCAL;
    const uint16_t localId = entry["local_id"] | 0;
    Lock lock;
    for (size_t i = 0; i < g_subCount; ++i) {
      if (g_subs[i].id != localId) continue;
      if (bbw::applyReviewTo(&g_subs[i], entry["record"] | 0u, state, entry["note"] | "",
                             entry["library_id"] | "")) {
        g_subsDirty = true;
        LOGI("share: the library on the service %s: %s", g_subs[i].name, name);
      }
    }
  }
  doc.clear();
  if (changed) {
    LOGI("share: the library's verdicts on this controller's coffees have changed");
    g_libraryReadSoon = true;  // a coffee just published has figures to fetch
  }
}
// A coffee rating just saved, sent to the library when ratings are shared. A changed rating goes again
// under the same name, so the library keeps the latest.
void shareRating(uint32_t id) {
  const bbw::Settings s = app::settings();
  if (!s.shareEnabled || !s.shareRatings || !bbw::deviceSigner().ready()) return;
  struct Find {
    uint32_t id;
    bbw::CoffeeRating r;
    bool have;
  } find = {id, bbw::CoffeeRating(), false};
  g_store.scanRatings([](const bbw::CoffeeRating &r, void *c) {
    Find &f = *static_cast<Find *>(c);
    if (r.id != f.id) return true;
    f.r = r;
    f.have = true;
    return false;
  }, &find);
  if (!find.have || !copyCoffee(find.r.coffeeId)) return;
  uint32_t roastDate = g_shareCoffee.roastDate;
  {
    Lock lock;
    for (uint8_t i = 0; i < g_library.bagCount; ++i) {
      if (g_library.bags[i].id == find.r.bagId) roastDate = g_library.bags[i].roastDate;
    }
  }
  // The service the bag came with, if that service is in the library's list.
  char service[40] = {};
  {
    Lock lock;
    uint16_t subId = 0;
    for (uint8_t i = 0; i < g_library.bagCount; ++i) {
      if (g_library.bags[i].id == find.r.bagId) subId = g_library.bags[i].subscriptionId;
    }
    for (size_t i = 0; subId && i < g_subCount; ++i) {
      if (g_subs[i].id == subId && g_subs[i].shareState == static_cast<uint8_t>(bbw::ShareState::PUBLISHED)) {
        strlcpy(service, g_subs[i].libraryId, sizeof(service));
      }
    }
  }
  if (!ensureKeyQueued()) return;
  if (bbw::buildTasteRecord(find.r, g_shareCoffee, roastDate, g_shareBody, bbw::SHARE_BODY_MAX, service) == 0) return;
  char kid[bbw::KEY_ID_LEN];
  bbw::keyId(bbw::deviceSigner().publicKey(), kid);
  snprintf(g_sharePath, sizeof(g_sharePath), "tastes/%s-r%u.json", kid, static_cast<unsigned>(id));
  const uint32_t seq = g_store.outboxNext();
  if (!shareQueue(g_sharePath)) {
    LOGW("share: coffee rating %u not queued", static_cast<unsigned>(id));
    return;
  }
  g_store.setRatingShare(id, static_cast<uint8_t>(bbw::ShareState::QUEUED), seq);
  LOGI("share: coffee rating %u queued", static_cast<unsigned>(id));
}

// After a shot is stored: the shot itself if shots are shared, and a dial-in
// if this one settled the preset at a grind setting not sent before.
void shareAfterShot(const bbw::ShotRecord &shot) {
  const bbw::Settings s = app::settings();
  if (!s.shareEnabled || !bbw::deviceSigner().ready()) return;
  char kid[bbw::KEY_ID_LEN], month[12];
  bbw::keyId(bbw::deviceSigner().publicKey(), kid);
  monthFolder(shot.epochS ? shot.epochS : wallClock(), month);
  const bbw::Grinder grinder = app::grinder();
  const bool haveCoffee = shot.coffeeId != 0 && copyCoffee(shot.coffeeId);

  if (!ensureKeyQueued()) return;
  if (s.shareShots && bbw::grindShotValid(shot, s.grindTimeBasis)) {
    const bbw::MachineProfile machine = app::machineProfile();
    if (bbw::buildShotRecord(shot, haveCoffee ? &g_shareCoffee : nullptr, grinder, s.shareRatings,
                             g_shareBody, bbw::SHARE_BODY_MAX, &machine) != 0) {
      snprintf(g_sharePath, sizeof(g_sharePath), "shots/%s/%s-%u.json", month, kid,
               static_cast<unsigned>(shot.id));
      if (!shareQueue(g_sharePath)) LOGW("share: shot %u not queued", static_cast<unsigned>(shot.id));
    }
  }

  if (!s.shareDialIns || !haveCoffee) return;
  const bbw::GrindAdvice advice = app::grindAdvice();
  const bbw::Recipe preset = app::activeRecipe();
  // Settled means the base verdict (inside the tolerance): a refinement by ratings is personal taste
  // and does not hold a dial-in back from the library.
  if (advice.baseAction != bbw::GrindAction::HOLD || advice.shotsUsed < s.grindWindow ||
      preset.recipeId != shot.recipeId || preset.coffeeId != shot.coffeeId) {
    return;
  }
  static uint16_t settledAt;
  settledAt = advice.currentUm;
  static uint16_t recipeId, coffeeId;
  recipeId = preset.recipeId;
  coffeeId = preset.coffeeId;
  {
    Lock lock;
    const int index = bbw::findPreset(g_library, recipeId, coffeeId);
    if (index < 0 || g_library.presets[index].sharedGrindUm == settledAt) return;  // sent already
  }
  if (bbw::buildDialinRecord(preset, g_shareCoffee, grinder, advice, s.grindTimeBasis,
                             wallClock(), g_shareBody, bbw::SHARE_BODY_MAX) == 0) {
    return;
  }
  snprintf(g_sharePath, sizeof(g_sharePath), "dialins/%s/%s-%u.json", month, kid,
           static_cast<unsigned>(g_store.outboxNext()));
  if (!shareQueue(g_sharePath)) return;
  app::editLibrary([](bbw::Library &lib, void *) {
    const int index = bbw::findPreset(lib, recipeId, coffeeId);
    if (index < 0) return false;
    lib.presets[index].sharedGrindUm = settledAt;
    return true;
  }, nullptr);
  LOGI("share: dial-in queued, %u at %.1f s", static_cast<unsigned>(settledAt),
       static_cast<double>(advice.averageTimeS));
}

// One record a second, and never while a shot needs the radio.
void shareSendOne(bool idle) {
  static uint32_t lastMs = 0;
  if (!idle || g_store.outboxWaiting() == 0 || !bbw::libraryLinkReady()) return;
  if (millis() - lastMs < SHARE_SEND_PERIOD_MS) return;
  lastMs = millis();
  if (!g_store.outboxPeek(g_sharePath, sizeof(g_sharePath), g_shareEnvelope,
                          bbw::SHARE_ENVELOPE_MAX)) {
    return;
  }
  if (publishRecord(g_sharePath, g_shareEnvelope)) {
    g_store.outboxPop();
    LOGI("share: sent %s", g_sharePath);
  }
}

// ------------------------------------------------------ reading the library --
// The library's index is read a file at a time while idle and kept on flash,
// so what it said is still there with no network. Loop task only.

#ifdef BBW_EMULATOR
constexpr uint32_t LIBRARY_REFRESH_MS = 60000;
#else
constexpr uint32_t LIBRARY_REFRESH_MS = 3600000;
#endif
bool fetchLibraryFile(const char *path, char *out, size_t size) {
  return bbw::libraryGet(path, out, size);
}

constexpr uint32_t LIBRARY_STEP_MS = 2000;
constexpr const char *FIT_CACHE = "/lfit.json";
constexpr const char *COFFEES_CACHE = "/lcoffees.json";
constexpr const char *GRINDERS_CACHE = "/lgrinders.json";
constexpr const char *MACHINES_CACHE = "/lmachines.json";
constexpr const char *AGE_CACHE = "/lage.json";
constexpr const char *SERVICES_CACHE = "/lservices.json";  // index/services.json: subscription services
constexpr const char *TASTE_CACHE = "/ltaste.json";   // index/taste.json: coffee ratings from everyone
constexpr uint8_t LIBRARY_FIXED_STEPS = 7;  // fit, coffees, grinders, ageing rate, machines, taste, services; then a dial-in per coffee
uint32_t g_communityStamp = 1;  // goes up when anything cached changes

void dialinCache(uint16_t coffeeId, char *out, size_t size) {
  snprintf(out, size, "/ld%u.json", static_cast<unsigned>(coffeeId));
}

void libraryRefresh(bool idle) {
  static uint32_t lastMs = 0, passStartedMs = 0;
  static uint8_t step = 0;
  static uint16_t ids[bbw::MAX_COFFEES];
  static char libraryIds[bbw::MAX_COFFEES][bbw::LIBRARY_ID_LEN];
  static uint8_t count = 0;
  if (!idle || !bbw::libraryLinkReady()) return;
  const uint32_t now = millis();
  if (step == 0 && passStartedMs != 0 && !g_libraryReadSoon &&
      now - passStartedMs < LIBRARY_REFRESH_MS) {
    return;
  }
  if (now - lastMs < LIBRARY_STEP_MS) return;
  lastMs = now;
  char path[96];
  if (step == 0) {
    passStartedMs = now ? now : 1;
    g_libraryReadSoon = false;
    count = 0;
    app::readLibrary([](const bbw::Library &lib, void *) {
      for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
        if (!lib.coffees[i].libraryId[0]) continue;
        ids[count] = lib.coffees[i].id;
        strlcpy(libraryIds[count], lib.coffees[i].libraryId, bbw::LIBRARY_ID_LEN);
        ++count;
      }
    }, nullptr);
    if (bbw::deviceSigner().ready()) {
      char kid[bbw::KEY_ID_LEN];
      bbw::keyId(bbw::deviceSigner().publicKey(), kid);
      snprintf(path, sizeof(path), "index/fit/%s.json", kid);
      if (fetchLibraryFile(path, g_libraryText, LIBRARY_TEXT_SIZE) &&
          g_store.writeTextIfChanged(FIT_CACHE, g_libraryText)) {
        ++g_communityStamp;
      }
    }
  } else if (step == 1) {
    if (fetchLibraryFile("index/coffees.json", g_libraryText, LIBRARY_TEXT_SIZE)) {
      g_store.writeTextIfChanged(COFFEES_CACHE, g_libraryText);
    }
  } else if (step == 2) {
    if (fetchLibraryFile("index/grinders.json", g_libraryText, LIBRARY_TEXT_SIZE)) {
      g_store.writeTextIfChanged(GRINDERS_CACHE, g_libraryText);
    }
  } else if (step == 3) {
    if (fetchLibraryFile("index/age.json", g_libraryText, LIBRARY_TEXT_SIZE) &&
        g_store.writeTextIfChanged(AGE_CACHE, g_libraryText)) {
      ++g_communityStamp;
    }
  } else if (step == 4) {
    if (fetchLibraryFile("index/machines.json", g_libraryText, LIBRARY_TEXT_SIZE)) {
      g_store.writeTextIfChanged(MACHINES_CACHE, g_libraryText);
    }
  } else if (step == 5) {
    if (fetchLibraryFile("index/taste.json", g_libraryText, LIBRARY_TEXT_SIZE)) {
      g_store.writeTextIfChanged(TASTE_CACHE, g_libraryText);
    }
  } else if (step == 6) {
    if (fetchLibraryFile("index/services.json", g_libraryText, LIBRARY_TEXT_SIZE)) {
      g_store.writeTextIfChanged(SERVICES_CACHE, g_libraryText);
    }
  } else if (step - LIBRARY_FIXED_STEPS < count) {
    char file[bbw::LIBRARY_ID_LEN * 2], cache[20];
    if (bbw::libraryIdToFile(libraryIds[step - LIBRARY_FIXED_STEPS], file, sizeof(file))) {
      snprintf(path, sizeof(path), "index/dialin/%s.json", file);
      dialinCache(ids[step - LIBRARY_FIXED_STEPS], cache, sizeof(cache));
      if (fetchLibraryFile(path, g_libraryText, LIBRARY_TEXT_SIZE) &&
          g_store.writeTextIfChanged(cache, g_libraryText)) {
        ++g_communityStamp;
      }
    }
  }
  step = static_cast<uint8_t>(step + 1 >= count + LIBRARY_FIXED_STEPS ? 0 : step + 1);
}

void controlTask(void *) {
  esp_task_wdt_add(nullptr);
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    {
      Lock lock;
      const uint32_t now = millis();
      g_scale.setShotRunning(g_controller.machineMayBeRunning());
      g_scale.service(now);
      g_bot.tick(now);
      g_power.tick(now);
      g_stop.tick(now);
      static bool scaleWasConnected = false;
      const bool scaleConnected = g_scale.connected();
      if (scaleConnected && !scaleWasConnected) g_botHoldUntilMs = now + BOT_PREWAKE_MS;
      // The brew bot stays connected for as long as the scale is on (the
      // owner's choice): a sleeping SwitchBot advertises only every 20-30 s,
      // so waking it at Start could keep a shot waiting that long.
      if (scaleConnected) {
        const uint32_t until = now + BOT_LINGER_MS;
        if (static_cast<int32_t>(until - g_botHoldUntilMs.load()) > 0) g_botHoldUntilMs = until;
      }
      scaleWasConnected = scaleConnected;
      // A cup going on the scale means a shot is coming: wake the brew bot
      // now, it takes a sleeping SwitchBot several seconds to answer.
      static bool cupWasPresent = false;
      const bool cupPresent = g_controller.cupPresent();
      if (const char *why = g_controller.takeTareReason()) {
        LOGI("scale: tared (%s; reading %.1f g)", why, static_cast<double>(g_controller.status(now).weightG));
      }
      if (g_controller.takeStartRefusedNoCup()) {
        LOGI("start refused: no cup seen on the scale (reading %.1f g)",
             static_cast<double>(g_controller.status(now).weightG));
      }
      if (cupPresent && !cupWasPresent) {
        const uint32_t until = now + BOT_PREWAKE_MS;
        if (static_cast<int32_t>(until - g_botHoldUntilMs.load()) > 0) g_botHoldUntilMs = until;
      }
      if (cupPresent != cupWasPresent) {
        const bbw::State cupState = g_controller.state();
        const bool inUse = !cupPresent && cupState != State::READY && cupState != State::CONNECTING_SCALE &&
                           cupState != State::SHOT_COMPLETE;
        LOGI("cup: %s (reading %.1f g)", cupPresent ? "on the scale" : inUse ? "in use for the shot" : "taken off",
             static_cast<double>(g_controller.status(now).weightG));
      }
      cupWasPresent = cupPresent;
      static bool zeroWarned = false;
      if (!zeroWarned && g_controller.scaleZeroesItself()) {
        zeroWarned = true;
        LOGW("scale: it tares itself (auto-tare mode). A cup taken off can then go unseen: "
             "set the scale to a mode without auto-tare");
      }
      bbw::WeightSample sample;
      while (g_scale.nextSample(sample)) g_controller.onSample(sample);
      // The controller's time is read after the readings are taken in, not
      // before: a reading that arrived while the scale and the Bots were
      // being serviced above (a heartbeat write takes milliseconds) is
      // stamped later than `now`.
      g_controller.tick(millis());
      // Bot battery: connected from Start until the shot is confirmed over,
      // asleep the rest of the time.
      if (g_controller.actuatorNeeded()) g_botHoldUntilMs = now + BOT_LINGER_MS;
      const bool botWanted = static_cast<int32_t>(now - g_botHoldUntilMs.load()) < 0;
      g_ble.setBotWanted(botWanted);
      // Three Bluetooth links at most with Wi-Fi running: the scale, the brew
      // bot, and one more. On a machine with its own stop button that one is
      // the stop bot from Start until the shot is known to be over (nothing
      // is stopped that was not started), and the power bot the rest of the
      // time (the machine is not switched off under a shot). The stop bot is
      // taken up at Start, during the tare, so that it is there before the
      // brew button is pressed.
      const bool shotPhase = g_controller.stopActuatorNeeded();
      g_ble.setStopWanted(shotPhase || static_cast<int32_t>(now - g_stopHoldUntilMs.load()) < 0);
      // The power button is only pressed while no shot is running or being set up.
      const bbw::State state = g_controller.state();
      const bool idle = !g_controller.machineMayBeRunning() &&
                        (state == State::READY || state == State::CONNECTING_SCALE);
      g_powerControl.tick(now, static_cast<uint32_t>(time(nullptr)), idle, g_power);
      if (g_powerControl.takeDirty()) g_pushersDirty = true;
      bbw::PowerEvent powerEvent;
      if (g_powerControl.takeEvent(powerEvent) && g_powerEventsQueued < 4) {
        g_powerEventQueue[g_powerEventsQueued++] = powerEvent;
      }
      g_ble.setPowerKept(g_settings.keepPowerBotConnected);
      g_ble.setPowerWanted(!shotPhase &&
                           (g_powerControl.wantsLink() ||
                            static_cast<int32_t>(now - g_powerHoldUntilMs.load()) < 0));
      g_shotInProgress = g_controller.machineMayBeRunning() ? SHOT_IN_PROGRESS_MAGIC : 0;
      if (g_testPress.load() == TEST_WAITING) {
        if (!idle || static_cast<int32_t>(now - g_testPressUntilMs.load()) >= 0) {
          g_testPress = TEST_FAILED;
          LOGW("bot: test press not made (%s)", idle ? "the bot did not connect" : "a shot is in hand");
        } else if (g_bot.connected() && !g_bot.busy() && g_bot.beginPress()) {
          g_testPress = TEST_PRESSED;
          g_testPressTidy = true;
          LOGI("bot: test press sent");
        }
      }
      if (g_testPressTidy && idle && !g_bot.busy()) {
        g_bot.clearResult();  // the brew logic must not find a result it did not ask for
        g_testPressTidy = false;
      }
      // An idle scale is let go so that it can switch itself off; the bots follow it (core/ScaleRest.h).
      {
        const bbw::LiveStatus st = g_controller.status(now);
        const uint32_t heardAt = g_ble.scaleHeardAtMs();
        const uint32_t quietMs = scaleConnected ? 0 : (heardAt == 0 ? UINT32_MAX : now - heardAt);
        const bool wasResting = g_scaleRest.resting();
        const bool resting = g_scaleRest.update(now, g_settings.scaleRestMin, state == State::READY,
                                                scaleConnected, st.weightG, quietMs);
        if (resting != wasResting) {
          LOGI("scale: %s", resting ? "resting (idle), let go so it can switch itself off"
                                    : "taken back");
        }
        g_ble.setScaleResting(resting);
        // The scale switched off: the machine too, if the owner asked for that.
        static bbw::ScaleOffWatch scaleOff;
        if (scaleOff.update(now, scaleConnected, resting, quietMs, bbw::ScaleRest::QUIET_MS) &&
            g_settings.powerOffWithScale) {
          if (g_powerControl.request(false, now, bbw::PowerBy::SCALE_OFF)) {
            LOGI("power: the scale switched off; switching the machine off");
          } else {
            LOGI("power: the scale switched off; machine not switched off: %s",
                 bbw::PowerControl::outcomeText(g_powerControl.lastOutcome()));
          }
        }
        if (g_scaleRest.takeStart(now, state == State::READY, st.weightFresh,
                                  !st.cupRequired || st.cupPresent)) {
          if (bbw::updateBusy()) {
            LOGW("start refused: a firmware update is in progress");
          } else {
            LOGI("scale: back, starting the shot asked for while it rested");
            g_controller.requestStart();
          }
        }
      }
    }
    esp_task_wdt_reset();
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
  }
}

void bleTask(void *) { g_ble.run(); }

// ------------------------------------------------------------ diagnostics --

const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_SW: return "restart requested";
    case ESP_RST_PANIC: return "CRASH (exception)";
    case ESP_RST_INT_WDT: return "WATCHDOG (interrupt)";
    case ESP_RST_TASK_WDT: return "WATCHDOG (a task stopped responding)";
#ifndef BBW_EMULATOR
    case ESP_RST_WDT: return "WATCHDOG (other)";
    case ESP_RST_BROWNOUT: return "BROWNOUT (supply voltage dropped)";
    case ESP_RST_DEEPSLEEP: return "wake from deep sleep";
#endif
    default: {
      // e.g. a reset from the USB port, which this core has no name for
      static char text[24];
      snprintf(text, sizeof(text), "other (%d)", static_cast<int>(esp_reset_reason()));
      return text;
    }
  }
}

// A new firmware is on trial until it has run for a while: if it crashes or
// hangs before then, the bootloader goes back to the one before it.
void confirmFirmware() {
#ifndef BBW_EMULATOR
  constexpr uint32_t TRIAL_MS = 45000;
  static bool done = false;
  if (done || millis() < TRIAL_MS) return;
  done = true;
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    LOGI("update: new firmware %s has run for %u s, kept", BBW_VERSION,
         static_cast<unsigned>(TRIAL_MS / 1000));
  }
#endif
}

// Every ten minutes, and whenever the least free memory seen gets lower: what
// the controller has left. On the real board this is the line that shows a
// leak or a stack running out before it becomes a crash.
void logHealth() {
#ifndef BBW_EMULATOR
  constexpr uint32_t PERIOD_MS = 10UL * 60UL * 1000UL;
  static uint32_t lastMs = 0, lastLowest = 0xFFFFFFFF;
  const bbw::MemoryReport memory = bbw::memoryReport();
  const bool lower = memory.internalLowest + 4096 < lastLowest;
  if (lastMs != 0 && !lower && millis() - lastMs < PERIOD_MS) return;
  lastMs = millis() ? millis() : 1;
  lastLowest = memory.internalLowest;
  const bbw::ScaleDiag scale = g_scale.diag();
  LOGI("health: chip RAM %u free, %u lowest, %u largest; PSRAM %u free; stack left: loop %u, "
       "control %u, ble %u; wifi %d dBm; scale frames %u, bad %u",
       static_cast<unsigned>(memory.internalFree), static_cast<unsigned>(memory.internalLowest),
       static_cast<unsigned>(memory.internalLargest), static_cast<unsigned>(memory.psramFree),
       static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
       static_cast<unsigned>(g_controlTask ? uxTaskGetStackHighWaterMark(g_controlTask) : 0),
       static_cast<unsigned>(g_bleTask ? uxTaskGetStackHighWaterMark(g_bleTask) : 0),
       WiFi.status() == WL_CONNECTED ? static_cast<int>(WiFi.RSSI()) : 0,
       static_cast<unsigned>(scale.frames), static_cast<unsigned>(scale.badFrames));
  if (memory.internalLowest < 30000) LOGW("health: chip RAM has been down to %u bytes",
                                          static_cast<unsigned>(memory.internalLowest));
#endif
}

// ------------------------------------------------------------ serial log --

void printShot(const bbw::ShotRecord &shot) {
  JsonDocument doc;
  bbw::shotToJson(shot, doc.to<JsonObject>());
  Serial.print("SHOT_JSON ");
  serializeJson(doc, Serial);
  Serial.println();
  char line[480];
  bbw::shotCsvLine(shot, line, sizeof(line));
  Serial.print("SHOT_CSV ");
  Serial.print(line);
}

void dumpHistory(bool json) {
  if (!json) Serial.print(bbw::shotCsvHeader());
  bbw::ShotRecord shot;
  char line[480];
  for (int32_t i = static_cast<int32_t>(g_store.shotCount()) - 1; i >= 0; --i) {
    if (!g_store.readShot(static_cast<uint32_t>(i), shot)) continue;
    if (json) {
      JsonDocument doc;
      bbw::shotToJson(shot, doc.to<JsonObject>());
      serializeJson(doc, Serial);
      Serial.println();
    } else {
      bbw::shotCsvLine(shot, line, sizeof(line));
      Serial.print(line);
    }
  }
}

void handleSerial() {
  static char buffer[32];
  static size_t length = 0;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c != '\n' && c != '\r') {
      if (length + 1 < sizeof(buffer)) buffer[length++] = c;
      continue;
    }
    buffer[length] = '\0';
    length = 0;
    if (buffer[0] == '\0') continue;
    if (!strcmp(buffer, "start")) app::start();
    else if (!strcmp(buffer, "stop")) app::stop();
    else if (!strcmp(buffer, "tare")) app::tare();
    else if (!strcmp(buffer, "ack")) app::acknowledge();
    else if (!strcmp(buffer, "csv")) dumpHistory(false);
    else if (!strcmp(buffer, "json")) dumpHistory(true);
    else if (!strcmp(buffer, "status")) {
      const bbw::LiveStatus st = app::status();
      const app::Devices dev = app::devices();
      Serial.printf("state=%s scale=%d(%s %s) bot=%d weight=%.2f flow=%.2f period=%ums "
                    "target=%.1f offset=%.2f recipe=%s\n",
                    bbw::stateName(st.state), st.scaleConnected, dev.scaleName,
                    dev.scaleProtocol, st.botConnected, st.weightG, st.flowGS,
                    st.samplePeriodMs, st.targetWeightG, st.stopOffsetG, st.recipe);
#ifndef BBW_EMULATOR
    } else if (!strcmp(buffer, "wifi off")) {
      // Diagnostics: does Wi-Fi crowd out Bluetooth on the shared radio?
      WiFi.mode(WIFI_OFF);
      Serial.println("wifi: off (until 'wifi on' or a restart)");
    } else if (!strcmp(buffer, "wifi on")) {
      Serial.println("wifi: restarting the controller to bring the network back");
      app::restartSoon();
#endif
    } else if (!strcmp(buffer, "update") || !strcmp(buffer, "update check")) {
      if (!strcmp(buffer, "update check")) bbw::updateCheck();
      bbw::SlotInfo slots[4];
      const size_t n = bbw::updateSlots(slots, 4);
      for (size_t i = 0; i < n; ++i) {
        Serial.printf("slot %-8s %-10s %-9s %u kB%s%s\n", slots[i].label,
                      slots[i].version[0] ? slots[i].version : "?", slots[i].state,
                      static_cast<unsigned>(slots[i].bytes / 1024), slots[i].running ? "  running" : "",
                      slots[i].bootNext && !slots[i].running ? "  starts next" : "");
      }
      const bbw::UpdateStatus u = bbw::updateStatus();
      Serial.printf("update: %s %u%% %s\n", bbw::updatePhaseName(u.phase),
                    static_cast<unsigned>(u.percent), u.message);
    } else if (!strcmp(buffer, "devices")) {
      // Field check: what is paired, and what the radio has heard lately.
      const bbw::Pairing pairing = g_ble.pairing();
      char power[bbw::ADDR_LEN], stop[bbw::ADDR_LEN];
      g_ble.powerAddress(power);
      g_ble.stopAddress(stop);
      Serial.printf("paired: scale %s, brew bot %s, power bot %s, stop bot %s\n",
                    pairing.scaleAddress[0] ? pairing.scaleAddress : "-",
                    pairing.botAddress[0] ? pairing.botAddress : "-", power[0] ? power : "-",
                    stop[0] ? stop : "-");
      // A pusher that is connected does not advertise, so it is not "heard".
      Serial.printf("links: scale %s, brew bot %s, power bot %s, stop bot %s\n",
                    g_scale.connected() ? "up" : "down", g_bot.linkUp() ? "up" : "down",
                    g_power.linkUp() ? "up" : "down", g_stop.linkUp() ? "up" : "down");
      bbw::SeenDevice seen[bbw::BleManager::MAX_SEEN];
      const size_t n = g_ble.seen(seen, bbw::BleManager::MAX_SEEN);
      const uint32_t now = millis();
      for (size_t i = 0; i < n; ++i) {
        Serial.printf("heard: %-5s %s  %-20s %4d dBm  %u times, last %lu s ago\n",
                      seen[i].kind == bbw::DeviceKind::BOT ? "bot" : "scale", seen[i].address,
                      seen[i].name, seen[i].rssi, static_cast<unsigned>(seen[i].heard),
                      static_cast<unsigned long>((now - seen[i].seenAtMs) / 1000));
      }
      if (n == 0) Serial.println("heard: nothing");
    } else {
      Serial.println("commands: status devices update [check] start stop tare ack csv json");
    }
  }
}

void logLoop() {
  static State lastState = State::BOOT;
  static uint32_t lastLiveMs = 0;
  const bbw::LiveStatus st = app::status();
  const uint32_t now = millis();
  if (st.state != lastState) {
    LOGI("state %s -> %s", bbw::stateName(lastState), bbw::stateName(st.state));
    lastState = st.state;
  }
  const bool live = st.state == State::BREWING || st.state == State::BLOOMING ||
                    st.state == State::STOPPING_SHOT ||
                    st.state == State::POST_SHOT_DRIP;
  if (live && now - lastLiveMs >= 250) {
    lastLiveMs = now;
    Serial.printf("LIVE,%.2f,%.2f,%.2f,%s\n", st.shotTimeS, st.weightG, st.flowGS,
                  bbw::stateName(st.state));
  }
  // Debug level: the shot as it runs, once a second, in the kept log too.
  static uint32_t lastDebugMs = 0;
  if (live && now - lastDebugMs >= 1000) {
    lastDebugMs = now;
    LOGD("shot: %.1f s  %.1f g  %.2f g/s  %s%s", static_cast<double>(st.shotTimeS),
         static_cast<double>(st.weightG), static_cast<double>(st.flowGS),
         bbw::stateName(st.state), st.weightFresh ? "" : "  (no fresh reading)");
  }
}

// ---------------------------------------------------------------- button --

void handleButton() {
  static bool wasDown = false;
  static uint32_t downAtMs = 0;
  static bool longHandled = false;
  const bool down = digitalRead(BOOT_BUTTON_PIN) == LOW;
  const uint32_t now = millis();
  if (down && !wasDown) {
    downAtMs = now;
    // A press on a dark screen only wakes it.
    longHandled = ui::isOff();
    ui::wake();
  }
  const State state = app::status().state;
  if (down && !longHandled && now - downAtMs >= LONG_PRESS_MS) {
    longHandled = true;
    if (state == State::READY || state == State::CONNECTING_SCALE) app::nextRecipe();
  }
  if (!down && wasDown && !longHandled && now - downAtMs >= 30) {
    ui::bootShortPress();
  }
  wasDown = down;
}

}  // namespace

// =================================================================== app:: ==

// ------------------------------------------------------------ subscriptions --
void loadSubscriptions() {
  g_subCount = 0;
  const size_t length = g_store.readText(SUBS_FILE, g_subsText, SUBS_TEXT_SIZE);
  if (length == 0) return;
  JsonDocument doc;
  if (deserializeJson(doc, g_subsText, length)) {
    LOGW("subscriptions: %s could not be read", SUBS_FILE);
    return;
  }
  g_nextSubId = doc["next_id"] | 1;
  strlcpy(g_subPrefs.country, doc["country"] | "", sizeof(g_subPrefs.country));
  g_subPrefs.matters = doc["matters"] | 0;
  for (JsonObjectConst o : doc["subscriptions"].as<JsonArrayConst>()) {
    if (g_subCount >= bbw::MAX_SUBSCRIPTIONS) break;
    bbw::Subscription &s = g_subs[g_subCount];
    app::subscriptionFromJson(o, s);
    bbw::sanitizeSubscription(s);
    if (s.id == 0) continue;
    if (s.id >= g_nextSubId) g_nextSubId = s.id + 1;
    ++g_subCount;
  }
  LOGI("subscriptions: %u loaded", static_cast<unsigned>(g_subCount));
}

void saveSubscriptions() {
  JsonDocument doc;
  {
    Lock lock;
    doc["next_id"] = g_nextSubId;
    doc["country"] = g_subPrefs.country;
    doc["matters"] = g_subPrefs.matters;
    JsonArray list = doc["subscriptions"].to<JsonArray>();
    for (size_t i = 0; i < g_subCount; ++i) app::subscriptionToJson(g_subs[i], list.add<JsonObject>());
    g_subsDirty = false;
  }
  const size_t n = serializeJson(doc, g_subsText, SUBS_TEXT_SIZE);
  if (n == 0 || n >= SUBS_TEXT_SIZE - 1) {
    LOGW("subscriptions: too big to save");
    return;
  }
  if (!g_store.saveText(SUBS_FILE, g_subsText)) LOGW("subscriptions: not saved");
}

// Queues a service's description for review, unless the library has this very version already.
bool queueService(uint16_t id) {
  static bbw::Subscription s;
  bool found = false;
  {
    Lock lock;
    for (size_t i = 0; i < g_subCount; ++i) {
      if (g_subs[i].id == id) {
        s = g_subs[i];
        found = true;
      }
    }
  }
  if (!found || !bbw::serviceCanBeShared(s)) return false;
  if (s.shareState != 0 && s.shareState != static_cast<uint8_t>(bbw::ShareState::REFUSED) &&
      s.shareState != static_cast<uint8_t>(bbw::ShareState::DECLINED) && !bbw::serviceChangedSinceShared(s)) {
    return false;
  }
  if (!ensureKeyQueued()) return false;
  char kid[bbw::KEY_ID_LEN];
  bbw::keyId(bbw::deviceSigner().publicKey(), kid);
  const uint32_t seq = g_store.outboxNext();
  if (bbw::buildServiceRecord(s, seq, wallClock(), g_shareBody, bbw::SHARE_BODY_MAX) == 0) return false;
  snprintf(g_sharePath, sizeof(g_sharePath), "services/%s/s%u-%u.json", kid, static_cast<unsigned>(id),
           static_cast<unsigned>(seq));
  if (!shareQueue(g_sharePath)) return false;
  Lock lock;
  for (size_t i = 0; i < g_subCount; ++i) {
    if (g_subs[i].id == id) bbw::markServiceQueued(g_subs[i], seq);
  }
  g_subsDirty = true;
  LOGI("share: service %s queued", s.name);
  return true;
}

// A review just saved, sent when ratings are shared and its service is in the library's list.
void shareServiceReview(uint32_t id) {
  const bbw::Settings st = app::settings();
  if (!st.shareEnabled || !st.shareRatings || !bbw::deviceSigner().ready()) return;
  bbw::ServiceReview r;
  char service[40] = {};
  {
    Lock lock;
    size_t i = 0;
    while (i < g_reviewCount && g_reviews[i].id != id) ++i;
    if (i == g_reviewCount) return;
    r = g_reviews[i];
    for (size_t k = 0; k < g_subCount; ++k) {
      if (g_subs[k].id == r.subscriptionId &&
          g_subs[k].shareState == static_cast<uint8_t>(bbw::ShareState::PUBLISHED)) {
        strlcpy(service, g_subs[k].libraryId, sizeof(service));
      }
    }
  }
  if (!service[0] || !ensureKeyQueued()) return;
  if (bbw::buildServiceReviewRecord(r, service, g_shareBody, bbw::SHARE_BODY_MAX) == 0) return;
  char kid[bbw::KEY_ID_LEN];
  bbw::keyId(bbw::deviceSigner().publicKey(), kid);
  snprintf(g_sharePath, sizeof(g_sharePath), "svcreviews/%s-v%u.json", kid, static_cast<unsigned>(id));
  const uint32_t seq = g_store.outboxNext();
  if (!shareQueue(g_sharePath)) return;
  Lock lock;
  for (size_t i = 0; i < g_reviewCount; ++i) {
    if (g_reviews[i].id == id) {
      g_reviews[i].shareState = static_cast<uint8_t>(bbw::ShareState::QUEUED);
      g_reviews[i].shareSeq = seq;
      g_reviewsDirty = true;
    }
  }
  LOGI("share: review of %s queued", service);
}

void loadServiceReviews() {
  g_reviewCount = 0;
  const size_t length = g_store.readText(SERVICE_REVIEWS_FILE, g_subsText, SUBS_TEXT_SIZE);
  if (length == 0) return;
  JsonDocument doc;
  if (deserializeJson(doc, g_subsText, length)) return;
  g_nextReviewId = doc["next_id"] | 1;
  for (JsonObjectConst o : doc["reviews"].as<JsonArrayConst>()) {
    if (g_reviewCount >= bbw::MAX_SERVICE_REVIEWS) break;
    bbw::ServiceReview &r = g_reviews[g_reviewCount];
    r = bbw::ServiceReview();
    r.id = o["id"] | 0u;
    r.epochS = o["at"] | 0u;
    r.subscriptionId = o["sub"] | 0;
    r.drinkerId = o["drinker"] | 0;
    r.stars = o["stars"] | 0;
    for (uint8_t a = 0; a < bbw::SERVICE_ASPECTS; ++a) r.aspect[a] = o["aspects"][a] | 0;
    r.shareState = o["share"] | 0;
    r.shareSeq = o["seq"] | 0u;
    strlcpy(r.note, o["note"] | "", sizeof(r.note));
    bbw::sanitizeServiceReview(r);
    if (r.id == 0) continue;
    if (r.id >= g_nextReviewId) g_nextReviewId = r.id + 1;
    ++g_reviewCount;
  }
}

void saveServiceReviews() {
  JsonDocument doc;
  {
    Lock lock;
    doc["next_id"] = g_nextReviewId;
    JsonArray list = doc["reviews"].to<JsonArray>();
    for (size_t i = 0; i < g_reviewCount; ++i) {
      const bbw::ServiceReview &r = g_reviews[i];
      JsonObject o = list.add<JsonObject>();
      o["id"] = r.id;
      o["at"] = r.epochS;
      o["sub"] = r.subscriptionId;
      o["drinker"] = r.drinkerId;
      o["stars"] = r.stars;
      JsonArray a = o["aspects"].to<JsonArray>();
      for (uint8_t k = 0; k < bbw::SERVICE_ASPECTS; ++k) a.add(r.aspect[k]);
      o["share"] = r.shareState;
      o["seq"] = r.shareSeq;
      o["note"] = r.note;
    }
    g_reviewsDirty = false;
  }
  const size_t n = serializeJson(doc, g_subsText, SUBS_TEXT_SIZE);
  if (n == 0 || n >= SUBS_TEXT_SIZE - 1 || !g_store.saveText(SERVICE_REVIEWS_FILE, g_subsText)) {
    LOGW("subscriptions: reviews not saved");
  }
}

// A shot was saved: supplies that each shot uses (a paper filter) go down.
void subscriptionsShotTaken() {
  Lock lock;
  for (size_t i = 0; i < g_subCount; ++i) {
    if (!bbw::subscriptionShotTaken(g_subs[i])) continue;
    g_subsDirty = true;
    LOGI("subscriptions: %s, %d left", g_subs[i].item[0] ? g_subs[i].item : g_subs[i].name,
         static_cast<int>(g_subs[i].stock));
  }
}

// Loop task: a waiting page fetch is made while idle, and the list saved when it has changed.
void subscriptionsService(bool idle) {
  if (!idle) return;
  char url[256];
  bool fetch = false;
  {
    Lock lock;
    if (g_preview->state == app::PreviewState::WAITING) {
      strlcpy(url, g_preview->url, sizeof(url));
      fetch = true;
    }
  }
  if (fetch) {
    static bbw::PageMeta meta;
    char problem[80];
    const int code = bbw::fetchPagePreview(url, meta, problem, sizeof(problem));
    Lock lock;
    if (strcmp(g_preview->url, url) == 0) {  // not asked again for another page meanwhile
      g_preview->meta = meta;
      strlcpy(g_preview->problem, problem, sizeof(g_preview->problem));
      g_preview->state = code == 200 ? app::PreviewState::DONE : app::PreviewState::FAILED;
    }
  }
  bool save;
  {
    Lock lock;
    save = g_subsDirty;
  }
  if (save) saveSubscriptions();
  {
    Lock lock;
    save = g_reviewsDirty;
  }
  if (save) saveServiceReviews();
}

namespace app {

bbw::LiveStatus status() {
  bbw::LiveStatus st;
  {
    Lock lock;
    st = g_controller.status(millis());
  }
  st.botPaired = g_ble.pairing().botAddress[0] != '\0';
  uint32_t timerMs = 0;
  // The shot time on the screen is the scale's own timer only where that
  // timer comes with every reading; a coarser one (the Varia's whole seconds)
  // would make the display step, so the controller's own time is shown.
  if ((g_scale.caps() & bbw::SCALE_CAP_TIMER_WITH_READINGS) && g_scale.timer(millis(), timerMs)) {
    st.scaleTimerValid = true;
    st.scaleTimerS = timerMs / 1000.0f;
  }
  if (st.scaleConnected) st.scaleBatteryPercent = static_cast<int8_t>(g_scale.diag().batteryPercent);
  // The Bot sleeps between shots; its battery is the value from its last connection.
  const bbw::PusherInfo bot = g_bot.info();
  if (st.botPaired && bot.valid) st.botBatteryPercent = static_cast<int8_t>(bot.batteryPercent);
  return st;
}

bool lastShot(bbw::ShotRecord &out) {
  Lock lock;
  if (!g_controller.hasLastShot()) return false;
  out = g_controller.lastShot();
  return true;
}

size_t lastCurve(float *timeS, float *weightG, size_t max) {
  Lock lock;
  const bbw::ShotCurve &curve = g_controller.curve();
  const size_t n = curve.count < max ? curve.count : max;
  memcpy(timeS, curve.timeS, n * sizeof(float));
  memcpy(weightG, curve.weightG, n * sizeof(float));
  return n;
}

// The scale is not connected but is near: let go to rest, or heard advertising in the last 30 s while
// the controller waits for it (switched on, or just taken back). Under the lock.
bool scaleNearbyLocked() {
  if (g_scaleRest.resting()) return true;
  if (g_controller.state() != State::CONNECTING_SCALE) return false;
  const uint32_t heardAt = g_ble.scaleHeardAtMs();
  return heardAt != 0 && millis() - heardAt < bbw::ScaleRest::QUIET_MS;
}

void start() {
  if (bbw::updateBusy()) {  // a firmware is being downloaded or installed
    LOGW("start refused: a firmware update is in progress");
    return;
  }
  Lock lock;
  // The scale was let go to rest, or is on its way back: take it back, and start once it is there
  // and ready.
  if (scaleNearbyLocked()) {
    LOGI("scale: start asked for while it rested; taking it back");
    g_scaleRest.startWhenBack(millis());
    return;
  }
  g_scaleRest.activity(millis());
  g_controller.requestStart();
}
// The owner did something (screen, touch key, web page): an idle scale is not let go, and one let go
// is taken back.
void noteActivity() { Lock lock; g_scaleRest.activity(millis()); }
bool scaleResting() { Lock lock; return g_scaleRest.resting(); }
bool scaleStartPending() { Lock lock; return g_scaleRest.startPending(millis()); }
bool scaleNearby() { Lock lock; return scaleNearbyLocked(); }
void stop() { Lock lock; g_scaleRest.cancelStart(); g_controller.requestStop(); }
void tare() { Lock lock; g_scaleRest.activity(millis()); g_controller.requestTare(); }
void acknowledge() { Lock lock; g_controller.acknowledge(); }
void manualPress() { Lock lock; g_controller.requestManualPress(); }

bbw::Settings settings() {
  Lock lock;
  return g_settings;
}

void applySettings(const bbw::Settings &settings) {
  bbw::Settings clean = settings;
  bbw::sanitize(clean);
  Lock lock;
  if (g_controller.machineMayBeRunning()) return;  // never change a running shot
  g_settings = clean;
  markSettingsDirty();
}

uint8_t presetCount() {
  Lock lock;
  return g_library.presetCount;
}

uint8_t activePreset() {
  Lock lock;
  return g_library.activePreset < g_library.presetCount ? g_library.activePreset : 0;
}

bbw::Recipe preset(uint8_t index) {
  Lock lock;
  return bbw::resolvePreset(g_library, index);
}

bbw::Recipe activeRecipe() {
  Lock lock;
  return bbw::resolvePreset(g_library, g_library.activePreset);
}

bool presetVisible(uint8_t index) {
  Lock lock;
  return bbw::presetVisible(g_library, index);
}

void selectRecipe(uint8_t index) {
  Lock lock;
  if (!editable()) return;
  if (bbw::presetVisible(g_library, index)) {
    g_library.activePreset = index;
    // A coffee waiting on an unopened bag: choosing it puts that bag in use.
    if (bbw::openActiveCoffeeBag(g_library, bbw::dateFromEpoch(epochNow()))) {
      LOGI("bag: opened (the coffee was chosen)");
    }
    markLibraryDirty();
  }
}

void nextRecipe() {
  uint8_t next;
  {
    Lock lock;
    if (g_library.presetCount == 0) return;
    next = bbw::nextShownPreset(g_library, g_library.activePreset);
  }
  selectRecipe(next);
}

bool finishActiveCoffee() {
  Lock lock;
  if (g_controller.machineMayBeRunning()) return false;
  if (g_library.activePreset >= g_library.presetCount) return false;
  const uint16_t coffeeId = g_library.presets[g_library.activePreset].coffeeId;
  if (!bbw::finishCoffee(g_library, coffeeId, bbw::dateFromEpoch(epochNow()))) return false;
  markLibraryDirty();
  return true;
}

bool applyPresetEdit(uint8_t index, const bbw::Recipe &edited) {
  Lock lock;
  if (g_controller.machineMayBeRunning()) return false;  // never change a running shot
  (*g_libraryBefore) = g_library;
  if (!bbw::editPreset(g_library, (*g_libraryBefore), index, edited)) {
    g_library = (*g_libraryBefore);
    return false;
  }
  markLibraryDirty();
  return true;
}

bool applyPresetList(const bbw::Recipe *flat, const uint8_t *slots, uint8_t count,
                     uint8_t active) {
  Lock lock;
  if (g_controller.machineMayBeRunning()) return false;
  (*g_libraryBefore) = g_library;
  if (!bbw::applyPresetList(g_library, (*g_libraryBefore), flat, slots, count, active)) return false;
  bbw::keepActivePresetShown(g_library);
  bbw::openActiveCoffeeBag(g_library, bbw::dateFromEpoch(epochNow()));
  markLibraryDirty();
  return true;
}

bool editLibrary(bool (*edit)(bbw::Library &, void *), void *context) {
  Lock lock;
  if (g_controller.machineMayBeRunning()) return false;
  (*g_libraryBefore) = g_library;
  if (!edit(g_library, context)) {
    g_library = (*g_libraryBefore);
    return false;
  }
  bbw::sanitizeLibrary(g_library);
  bbw::openActiveCoffeeBag(g_library, bbw::dateFromEpoch(epochNow()));
  markLibraryDirty();
  return true;
}

void readLibrary(void (*visit)(const bbw::Library &, void *), void *context) {
  Lock lock;
  visit(g_library, context);
}

void adjustTarget(float deltaG) {
  uint8_t index;
  bbw::Recipe recipe;
  {
    Lock lock;
    if (!editable()) return;
    index = g_library.activePreset;
    recipe = bbw::resolvePreset(g_library, index);
  }
  if (recipe.useRatio) {
    // Ratio mode: the buttons change the dose, the target follows.
    recipe.doseG = recipe.doseG + deltaG;
  } else {
    recipe.targetWeightG = recipe.targetWeightG + deltaG;
  }
  applyPresetEdit(index, recipe);
}

void resetLearnedOffset(uint8_t presetIndex) {
  Lock lock;
  if (presetIndex >= g_library.presetCount || g_controller.machineMayBeRunning()) return;
  bbw::Preset &preset = g_library.presets[presetIndex];
  preset.stopOffsetG = bbw::resolvePreset(g_library, presetIndex).baselineOffsetG;
  preset.learnedAlpha = 0;  // back to the baseline learning factor
  preset.learnedShots = 0;  // and a fresh warm-up
  markLibraryDirty();
}

void resetLearnedFactors() {
  Lock lock;
  if (g_controller.machineMayBeRunning()) return;
  for (bbw::Preset &preset : g_library.presets) preset.learnedAlpha = 0;
  markLibraryDirty();
}

bbw::Grinder grinder() {
  Lock lock;
  return g_library.grinder;
}

bbw::GrinderElasticity grinderElasticity() {
  grindAdvice();
  return g_grinderElasticity;
}

bbw::GrindAdvice grindAdvice() {
  if (!g_grindAdviceStale) return g_grindAdvice;
  constexpr uint32_t SCAN_SHOTS = 150;  // how far back to look
  struct Scan {
    bbw::Recipe recipe;
    bbw::GrindTimeBasis basis = bbw::GrindTimeBasis::TOTAL;
    uint16_t calibration = 0;
    uint32_t newestEpochS = 0;  // of the shots below
    bbw::GrindShot shots[40];
    size_t count = 0;
    // Every valid shot on this grinder as now calibrated, of any other coffee, bag or recipe, for
    // learning the grinder (bbw::pooledGrindElasticity).
    bbw::GrinderShot others[150];
    size_t otherCount = 0;
    // The newest shot of this coffee, bag and recipe, counted or not (adviseOutOfRange).
    bool newestSeen = false;
    bbw::GrindUse newestUse = bbw::GrindUse::COUNTED;
    uint16_t newestSetting = 0;
  };
  static Scan scan;
  const bbw::Settings s = settings();
  scan.recipe = activeRecipe();
  scan.basis = s.grindTimeBasis;
  scan.calibration = grinder().calibration;
  scan.count = 0;
  scan.otherCount = 0;
  scan.newestSeen = false;
  scan.newestEpochS = 0;
  g_store.scanShots(SCAN_SHOTS, [](const bbw::ShotRecord &record, void *context) {
    Scan &sc = *static_cast<Scan *>(context);
    // Same recipe, same coffee (and bag: roast date), same yield and dose:
    // otherwise brew times do not compare. A new bag starts a clean history.
    // By id, so renaming either does not.
    // And the grinder as it is now calibrated: a setting from before it was
    // zeroed again is another setting.
    const bool valid = record.grinderId == sc.calibration && bbw::grindShotValid(record, sc.basis);
    const bool same = record.recipeId == sc.recipe.recipeId && record.coffeeId == sc.recipe.coffeeId &&
                      bbw::sameBag(record.bagId, record.roastDate, sc.recipe.bagId, sc.recipe.roastDate) &&
                      fabsf(record.targetWeightG - sc.recipe.targetWeightG) <= 0.05f &&
                      fabsf(record.doseG - sc.recipe.doseG) <= 0.05f;
    if (same && record.grinderId == sc.calibration && !sc.newestSeen) {
      sc.newestSeen = true;
      sc.newestUse = bbw::grindShotUse(record, sc.basis);
      sc.newestSetting = record.grindUm;
    }
    if (valid && !same && sc.otherCount < sizeof(sc.others) / sizeof(sc.others[0])) {
      // A group per coffee, bag and recipe; a collision only merges two groups, which weakens the
      // pooled figure a little and never biases it.
      const uint32_t bag = record.bagId ? record.bagId : record.roastDate;
      sc.others[sc.otherCount].timeS = bbw::grindShotTime(record, sc.basis);
      sc.others[sc.otherCount].setting = record.grindUm;
      sc.others[sc.otherCount].group =
          static_cast<uint16_t>((record.recipeId * 977u + record.coffeeId * 131u + bag * 7u) & 0xFFFF);
      ++sc.otherCount;
    }
    if (valid && same && sc.count < sizeof(sc.shots) / sizeof(sc.shots[0])) {
      if (sc.count == 0) sc.newestEpochS = record.epochS;  // newest first
      sc.shots[sc.count].timeS = bbw::grindShotTime(record, sc.basis);
      sc.shots[sc.count].grindUm = record.grindUm;
      sc.shots[sc.count].forced =
          record.grindVote == static_cast<uint8_t>(bbw::GrindVote::COUNT);
      sc.shots[sc.count].rating = record.rating;
      sc.shots[sc.count].taste = record.taste;
      ++sc.count;
    }
    return true;   // SCAN_SHOTS bounds the pass
  }, &scan);
  const bbw::Recipe &recipe = scan.recipe;
  const bbw::GrindShot *shots = scan.shots;
  const size_t count = scan.count;
  bbw::GrindConfig config;
  config.toleranceS = s.grindToleranceS;
  config.defaultUmPerS = s.grindUmPerS;
  config.window = s.grindWindow;
  config.maxStepUm = s.grindMaxStepUm;
  // Limits in this grinder's own unit, and the sensitivity it has shown on its other coffees.
  const bbw::Grinder grinderNow = grinder();
  g_grinderElasticity = bbw::pooledGrindElasticity(scan.others, scan.otherCount);
  config = bbw::grindConfigForGrinder(config, grinderNow.umPerUnit, recipe.grindUm, recipe.targetTimeS,
                                      g_grinderElasticity);
  g_grindAdvice = bbw::adviseGrindRated(shots, count, recipe.grindUm, recipe.targetTimeS, config,
                                        s.grindRatedTarget);
  if (scan.newestSeen && scan.newestSetting == recipe.grindUm) {
    bbw::adviseOutOfRange(g_grindAdvice, scan.newestUse, recipe.grindUm, config);
  }
  g_lastCountedEpochS = scan.newestEpochS;
  g_grindAdviceStale = false;
  return g_grindAdvice;
}

// The library's measured ageing rate, from its copy on flash.
bbw::AgeModel ageModel() {
  bbw::AgeModel model;
  static JsonDocument doc;
  if (g_store.readText(AGE_CACHE, g_libraryText, LIBRARY_TEXT_SIZE) != 0 &&
      !deserializeJson(doc, g_libraryText) && doc["v"] == 1 && doc["um_per_day"].is<float>()) {
    const float rate = doc["um_per_day"];
    // Outside this it is not a rate anyone has measured on espresso.
    if (isfinite(rate) && rate >= -5.0f && rate <= 5.0f) {
      model.known = true;
      model.umPerDay = rate;
    }
  }
  doc.clear();
  return model;
}

// When the bag in use of a coffee was roasted, and its time in the freezer.
struct FreezerDates {
  uint32_t roast = 0, frozen = 0, thaw = 0;
};

FreezerDates freezerDates(uint16_t coffeeId) {
  static FreezerDates out;
  static uint16_t want;
  want = coffeeId;
  out = FreezerDates{};
  if (coffeeId == 0) return out;
  readLibrary([](const bbw::Library &lib, void *) {
    if (const bbw::Coffee *c = bbw::findCoffee(lib, want)) {
      out.roast = c->roastDate;
      out.frozen = c->frozenDate;
      out.thaw = c->thawDate;
    }
  }, nullptr);
  return out;
}

BeanAge beanAge() {
  BeanAge result;
  grindAdvice();  // brings g_lastCountedEpochS up to date
  const bbw::Recipe active = activeRecipe();
  const uint32_t now = epochNow();
  // Days in the freezer are not days of ageing (bbw::effectiveAgeDays).
  const FreezerDates fz = freezerDates(active.coffeeId);
  const uint32_t roast = active.coffeeId != 0 ? fz.roast : active.roastDate;
  result.ageDays = bbw::effectiveAgeDays(roast, fz.frozen, fz.thaw, now);
  result.frozenNow = bbw::frozenAt(fz.frozen, fz.thaw, now);
  result.frozenDays = bbw::frozenDaysAt(fz.frozen, fz.thaw, now);
  if (now != 0 && g_lastCountedEpochS != 0 && now >= g_lastCountedEpochS) {
    result.daysSinceBrewed = static_cast<int>((now - g_lastCountedEpochS) / 86400);
  }
  const bbw::AgeModel model = ageModel();
  result.rateKnown = model.known;
  if (model.known && result.ageDays >= 0 && result.daysSinceBrewed > 0 && active.coffeeId != 0) {
    const CommunityView view = community(active.coffeeId, active.recipeId);
    // Age then and now, both without the freezer, so a bag put away and taken out again has
    // aged only by the days it spent out.
    const int ageThen = bbw::effectiveAgeDays(roast, fz.frozen, fz.thaw, g_lastCountedEpochS);
    const float units = bbw::grinderUnitsPerUm(view.fit, grinder()) *
                        bbw::ageShiftUm(model, ageThen >= 0 ? ageThen : result.ageDays - result.daysSinceBrewed,
                                        result.ageDays);
    result.shift = static_cast<int>(units < 0 ? units - 0.5f : units + 0.5f);
  }
  return result;
}

void setGrind(uint16_t grindUm) {
  Lock lock;
  if (g_controller.machineMayBeRunning() || g_library.presetCount == 0) return;
  const uint8_t index = g_library.activePreset < g_library.presetCount ? g_library.activePreset : 0;
  g_library.presets[index].grindUm = grindUm > 2000 ? 0 : grindUm;
  markLibraryDirty();
}

bool setShotGrindVote(uint32_t shotId, uint8_t vote) {
  g_grindAdviceStale = true;
  if (!g_store.setShotGrindVote(shotId, vote > 2 ? 0 : vote)) return false;
  Lock lock;
  g_controller.setLastShotGrindVote(shotId, vote > 2 ? 0 : vote);  // keep the result screen in step
  return true;
}

bbw::Maintenance maintenance() {
  Lock lock;
  return g_maintenance;
}

void upkeepDone(bbw::Upkeep job) {
  {
    Lock lock;
    bbw::markUpkeepDone(g_maintenance, job, epochNow());
    g_maintenanceDirty = true;
  }
}

void upkeepEvery(bbw::Upkeep job, uint32_t everyShots) {
  Lock lock;
  bbw::setUpkeepEvery(g_maintenance, job, everyShots);
  g_maintenanceDirty = true;
}

bool setShotTds(uint32_t shotId, uint16_t tdsX100) {
  return g_store.setShotTds(shotId, tdsX100 > 2500 ? 0 : tdsX100);
}

bool setShotRating(uint32_t shotId, uint8_t rating, uint8_t taste) {
  g_grindAdviceStale = true;  // ratings steer the advice (bbw::ratedTarget)
  return g_store.setShotRating(shotId, rating > 5 ? 0 : rating, taste > 3 ? 0 : taste);
}


bool setShotGrind(uint32_t shotId, uint16_t grindUm) {
  g_grindAdviceStale = true;
  return g_store.setShotGrind(shotId, grindUm > 2000 ? 0 : grindUm);
}

CommunityView community(uint16_t coffeeId, uint16_t recipeId) {
  CommunityView view;
  static bbw::RecipeDef def;
  static bbw::Grinder grinder;
  static uint16_t wantCoffee, wantRecipe;
  static bool have;
  static uint32_t roastDate, frozenDate, thawDate;
  wantCoffee = coffeeId;
  wantRecipe = recipeId;
  have = false;
  readLibrary([](const bbw::Library &lib, void *) {
    const bbw::RecipeDef *d = bbw::findRecipe(lib, wantRecipe);
    const bbw::Coffee *c = bbw::findCoffee(lib, wantCoffee);
    grinder = lib.grinder;
    if (d && c && c->libraryId[0]) {
      def = *d;
      roastDate = c->roastDate;
      frozenDate = c->frozenDate;
      thawDate = c->thawDate;
      have = true;
    }
  }, nullptr);
  if (!have) return view;
  static JsonDocument doc;
  char cache[20];
  dialinCache(coffeeId, cache, sizeof(cache));
  if (g_store.readText(cache, g_libraryText, LIBRARY_TEXT_SIZE) != 0 &&
      !deserializeJson(doc, g_libraryText) && doc["v"] == 1) {
    // The same recipe class if the library has it, else the nearest within reach
    // (bbw::recipeDistance); of two equally near, the one more people settled.
    JsonObjectConst r;
    float bestDistance = -1.0f;
    for (JsonObjectConst c : doc["recipes"].as<JsonArrayConst>()) {
      const float d = bbw::recipeDistance(def.doseG, def.targetWeightG, c["dose_g"] | 0.0f,
                                          c["yield_g"] | 0.0f);
      if (d < 0.0f) continue;
      if (bestDistance < 0.0f || d < bestDistance ||
          (d == bestDistance && (c["contributors"] | 0) > (r["contributors"] | 0))) {
        r = c;
        bestDistance = d;
      }
    }
    if (bestDistance >= 0.0f) {
      view.dialin.recipeDistance = bestDistance;
      view.dialin.theirDoseG = r["dose_g"] | 0.0f;
      view.dialin.theirYieldG = r["yield_g"] | 0.0f;
      view.dialin.found = true;
      view.dialin.contributors = r["contributors"] | 0;
      view.dialin.dialins = r["dialins"] | 0;
      view.dialin.timeS = r["time_s"] | 0.0f;
      view.dialin.finenessUm = r["fineness_um"] | 0.0f;
      view.dialin.spreadUm = r["spread_um"] | 0.0f;
      view.dialin.ageDays = r["age_days"] | -1;
      view.dialin.axis = r["axis"] | 0UL;
      view.dialin.flowGs = r["flow_gs"] | 0.0f;
      view.dialin.kappa = r["kappa"] | 0.0f;
      view.dialin.flowBasis = strcmp(r["flow_basis"] | "", "pour") == 0 ? 1
                              : strcmp(r["flow_basis"] | "", "total") == 0 ? 0 : 255;
      for (JsonObjectConst m : r["models"].as<JsonArrayConst>()) {
        // The same model: by the firmware's own number for it, or by its
        // entry in the library's list.
        const bool byNumber = grinder.modelId != 0 && (m["model_id"] | 0) == grinder.modelId;
        const bool byEntry = grinder.libraryId[0] != '\0' &&
                             strcmp(m["library_id"] | "", grinder.libraryId) == 0;
        char unitKey[24];
        bbw::grinderUnitKey(grinder, unitKey, sizeof(unitKey));   // "dial/d10": how it is read, too
        if ((byNumber || byEntry) && strcmp(m["unit"] | "", unitKey) == 0) {
          view.dialin.modelMedian = m["median"] | 0.0f;
          view.dialin.modelCount = m["n"] | 0;
          view.dialin.modelSpread = m["spread"] | 0.0f;
        }
      }
    }
  }
  if (g_store.readText(FIT_CACHE, g_libraryText, LIBRARY_TEXT_SIZE) != 0 &&
      !deserializeJson(doc, g_libraryText) && doc["v"] == 1) {
    for (JsonObjectConst g : doc["grinders"].as<JsonArrayConst>()) {
      if ((g["calibration"] | -1) != grinder.calibration) continue;
      view.fit.known = true;
      view.fit.scale = g["scale"] | 0.0f;
      view.fit.offset = g["offset"] | 0.0f;
      view.fit.coffeesInCommon = g["coffees_in_common"] | 0;
      view.fit.axis = g["axis"] | 0UL;
      break;
    }
  }
  doc.clear();
  view.age = ageModel();
  view.myAgeDays = bbw::effectiveAgeDays(roastDate, frozenDate, thawDate, epochNow());
  // The flow this recipe aims for, on the time basis it is steered by.
  const float myFlow = def.targetTimeS > 0.0f ? def.targetWeightG / def.targetTimeS : 0.0f;
  const uint8_t myBasis = settings().grindTimeBasis == bbw::GrindTimeBasis::POUR ? 1 : 0;
  view.start = bbw::communityStart(view.dialin, view.fit, grinder, view.age, view.myAgeDays, myFlow, myBasis);
  return view;
}

bbw::CommunityStart communityStartForActive() {
  static bbw::CommunityStart memo;
  static uint32_t memoStamp = 0, memoAtMs = 0;
  static uint16_t memoCoffee = 0xFFFF, memoRecipe = 0xFFFF, memoCalibration = 0xFFFF;
  const bbw::Recipe active = activeRecipe();
  const uint16_t calibration = grinder().calibration;
  if (memoStamp == g_communityStamp && memoCoffee == active.coffeeId &&
      memoRecipe == active.recipeId && memoCalibration == calibration &&
      millis() - memoAtMs < 30000) {
    return memo;
  }
  memo = active.coffeeId ? community(active.coffeeId, active.recipeId).start : bbw::CommunityStart{};
  memoStamp = g_communityStamp;
  memoCoffee = active.coffeeId;
  memoRecipe = active.recipeId;
  memoCalibration = calibration;
  memoAtMs = millis();
  return memo;
}

// Where to start a coffee with no setting of its own, from the owner's most similar coffee
// (bbw::similarStart). Reads only the shelf, so it needs neither the shared library nor a clock.
bbw::SimilarStart similarStart(uint16_t coffeeId, uint16_t recipeId) {
  static bbw::SimilarStart out;
  static uint16_t wantCoffee, wantRecipe;
  wantCoffee = coffeeId;
  wantRecipe = recipeId;
  out = bbw::SimilarStart{};
  readLibrary([](const bbw::Library &lib, void *) {
    const bbw::Coffee *target = bbw::findCoffee(lib, wantCoffee);
    const bbw::RecipeDef *def = bbw::findRecipe(lib, wantRecipe);
    if (!target || !def) return;
    bbw::SimilarCandidate candidates[bbw::MAX_PRESETS];
    size_t n = 0;
    for (uint8_t i = 0; i < lib.presetCount && n < bbw::MAX_PRESETS; ++i) {
      const bbw::Preset &p = lib.presets[i];
      const bbw::Coffee *c = p.coffeeId != 0 ? bbw::findCoffee(lib, p.coffeeId) : nullptr;
      const bbw::RecipeDef *r = bbw::findRecipe(lib, p.recipeId);
      if (!c || !r || c->id == target->id || p.grindUm == 0) continue;
      candidates[n].coffee = c;
      candidates[n].grindUm = p.grindUm;
      candidates[n].doseG = r->doseG;
      candidates[n].yieldG = r->targetWeightG;
      candidates[n].settled = p.sharedGrindUm != 0 && p.sharedGrindUm == p.grindUm;
      ++n;
    }
    out = bbw::similarStart(*target, def->doseG, def->targetWeightG, candidates, n, lib.grinder);
  }, nullptr);
  return out;
}

// Good until the next call that reads the library's cache.
const char *communityCoffees() {
  g_store.readText(COFFEES_CACHE, g_libraryText, LIBRARY_TEXT_SIZE);
  return g_libraryText;
}

// The library's coffee ratings (index/taste.json), cached. Good until the next call that reads the
// library's cache.
const char *communityTaste() {
  g_store.readText(TASTE_CACHE, g_libraryText, LIBRARY_TEXT_SIZE);
  return g_libraryText;
}

const char *communityMachines() {
  g_store.readText(MACHINES_CACHE, g_libraryText, LIBRARY_TEXT_SIZE);
  return g_libraryText;
}

bbw::MachineProfile machineProfile() {
  Lock lock;
  return g_machine;
}

bool applyMachine(const bbw::MachineProfile &machine) {
  {
    Lock lock;
    if (!editable()) return false;
    g_machine = machine;
    bbw::sanitizeMachine(g_machine);
  }
  g_machineDirty = true;
  return true;
}

bool shareMachine() {
  if (!settings().shareEnabled) return false;
  bbw::MachineProfile machine = machineProfile();
  if (!bbw::machineCanBeShared(machine) || !ensureKeyQueued()) return false;
  char kid[bbw::KEY_ID_LEN];
  bbw::keyId(bbw::deviceSigner().publicKey(), kid);
  const uint32_t seq = g_store.outboxNext();
  if (bbw::buildMachineRecord(machine, seq, wallClock(), g_shareBody, bbw::SHARE_BODY_MAX) == 0) {
    return false;
  }
  snprintf(g_sharePath, sizeof(g_sharePath), "machines/%s/m1-%u.json", kid, static_cast<unsigned>(seq));
  if (!shareQueue(g_sharePath)) return false;
  {
    Lock lock;
    bbw::markMachineQueued(g_machine, seq);
  }
  g_machineDirty = true;
  return true;
}

bbw::ShareState machineShareState() {
  Lock lock;
  return bbw::machineShareState(g_machine, g_store.outboxSent());
}

const char *communityGrinders() {
  g_store.readText(GRINDERS_CACHE, g_libraryText, LIBRARY_TEXT_SIZE);
  return g_libraryText;
}

bool libraryReachable() { return bbw::libraryLinkReady(); }
void readLibrarySoon() { g_libraryReadSoon = true; }

ShareStatus shareStatus() {
  ShareStatus st;
  bbw::ISigner &signer = bbw::deviceSigner();
  st.keyReady = signer.ready();
  if (st.keyReady) {
    bbw::keyId(signer.publicKey(), st.keyId);
    strlcpy(st.algorithm, signer.algorithm(), sizeof(st.algorithm));
  }
  st.link = bbw::libraryLinkStatus();
  st.canSend = st.link.ready;
  st.tokenSet = g_net.libraryToken[0] != '\0';
  st.waiting = g_store.outboxWaiting();
  st.sent = g_store.outboxSent();
  return st;
}

void setLibraryLink(const char *repo, const char *token) {
  strlcpy(g_net.libraryRepo, repo ? repo : "", sizeof(g_net.libraryRepo));
  if (token && token[0]) strlcpy(g_net.libraryToken, token, sizeof(g_net.libraryToken));
  if (!g_net.libraryRepo[0]) g_net.libraryToken[0] = '\0';  // no repository: nothing to keep a token for
  g_store.saveNet(g_net);
  // Never the token itself: whether one came, and whether one is kept.
  LOGI("library: connection saved: %s, %s", g_net.libraryRepo[0] ? g_net.libraryRepo : "no repository",
       token && token[0] ? "new token stored" : g_net.libraryToken[0] ? "token kept" : "no token");
  bbw::libraryLinkConfigure(g_net.libraryRepo, g_net.libraryToken);
  g_libraryReadSoon = true;
}

// Everything the owner has described is put up again from scratch.
uint8_t shareEverythingAgain() {
  if (!settings().shareEnabled) return 0;
  if (!editLibrary([](bbw::Library &lib, void *) {
        bbw::forgetSharing(lib);
        return true;
      }, nullptr)) {
    return 0;
  }
  g_store.outboxForgetKey();
  uint8_t queued = shareAllCoffees();
  const bbw::Grinder inUse = grinder();
  if (queueGrinder(inUse.id)) ++queued;
  static uint16_t otherId;
  otherId = 0;
  readLibrary([](const bbw::Library &lib, void *) {
    if (lib.hasOtherGrinder) otherId = lib.otherGrinder.id;
  }, nullptr);
  if (otherId && queueGrinder(otherId)) ++queued;
  return queued;
}

bool shareCoffee(uint16_t coffeeId, bool asked) {
  const bbw::Settings s = settings();
  if (!s.shareEnabled || (!asked && !s.shareCoffees)) return false;
  return queueCoffee(coffeeId);
}

bool shareGrinder(uint16_t grinderId) {
  if (!settings().shareEnabled) return false;
  return queueGrinder(grinderId);
}

uint8_t shareAllCoffees() {
  static uint16_t ids[bbw::MAX_COFFEES];
  static uint8_t count;
  count = 0;
  readLibrary([](const bbw::Library &lib, void *) {
    for (uint8_t i = 0; i < lib.coffeeCount; ++i) ids[count++] = lib.coffees[i].id;
  }, nullptr);
  uint8_t queued = 0;
  for (uint8_t i = 0; i < count; ++i) queued += queueCoffee(ids[i]) ? 1 : 0;
  return queued;
}

Devices devices() {
  Devices d;
  d.scaleConnected = g_scale.connected();
  d.botConnected = g_bot.connected();
  strlcpy(d.scaleName, g_scale.name(), sizeof(d.scaleName));
  strlcpy(d.scaleProtocol, d.scaleConnected ? g_scale.protocolId() : "", sizeof(d.scaleProtocol));
  d.bot = g_bot.info();  // last values read; the Bot sleeps between shots
  d.botLastStatus = g_bot.lastStatus();
  strlcpy(d.botProtocol, g_bot.protocolId(), sizeof(d.botProtocol));
  strlcpy(d.botLabel, g_bot.protocolLabel(), sizeof(d.botLabel));
  d.botCaps = g_bot.caps();
  d.botProblem = g_bot.problem();
  d.powerConnected = g_power.connected();
  g_ble.powerAddress(d.powerAddress);
  d.power = g_power.info();
  d.powerLastStatus = g_power.lastStatus();
  strlcpy(d.powerProtocol, g_power.protocolId(), sizeof(d.powerProtocol));
  strlcpy(d.powerLabel, g_power.protocolLabel(), sizeof(d.powerLabel));
  d.powerCaps = g_power.caps();
  d.powerProblem = g_power.problem();
  d.stopConnected = g_stop.connected();
  g_ble.stopAddress(d.stopAddress);
  d.stop = g_stop.info();
  strlcpy(d.stopProtocol, g_stop.protocolId(), sizeof(d.stopProtocol));
  strlcpy(d.stopLabel, g_stop.protocolLabel(), sizeof(d.stopLabel));
  d.stopProblem = g_stop.problem();
  d.pairing = g_ble.pairing();
  d.scale = g_scale.diag();
  return d;
}

size_t seenDevices(bbw::SeenDevice *out, size_t max) {
  g_ble.requestDiscovery();
  return g_ble.seen(out, max);
}
void discover() { g_ble.requestDiscovery(); }
void pairScale(const char *address, uint8_t type) { g_ble.setScale(address, type); }
void pairBot(const char *address, uint8_t type) {
  g_ble.setBot(address, type);
  // Connect once now so the user sees it work and the battery level is read.
  if (address && address[0]) g_botHoldUntilMs = millis() + BOT_PAIR_CHECK_MS;
}
bool pairStrongestBot() {
  const bool found = g_ble.pairStrongestBot();
  if (found) g_botHoldUntilMs = millis() + BOT_PAIR_CHECK_MS;
  return found;
}

// The power bot and the stop bot share one Bluetooth link, so a first
// connection asked for one of them (after pairing, or a new key) waits its
// turn behind one already asked for the other, instead of running out while
// it is kept waiting.
static void holdForCheck(std::atomic<uint32_t> &mine, const std::atomic<uint32_t> &other) {
  const uint32_t now = millis();
  const uint32_t otherUntil = other.load();
  const uint32_t from = static_cast<int32_t>(otherUntil - now) > 0 ? otherUntil : now;
  mine = from + BOT_PAIR_CHECK_MS;
}

void pairPower(const char *address, uint8_t type) {
  {
    Lock lock;
    strlcpy(g_pushers.powerAddress, address ? address : "", sizeof(g_pushers.powerAddress));
    g_pushers.powerAddressType = type;
  }
  g_pushersDirty = true;
  g_ble.setPower(address, type);
  // Connect once now so the owner sees it work and the battery level is read.
  if (address && address[0]) holdForCheck(g_powerHoldUntilMs, g_stopHoldUntilMs);
}

void pairStop(const char *address, uint8_t type) {
  {
    Lock lock;
    strlcpy(g_stopBot.address, address ? address : "", sizeof(g_stopBot.address));
    g_stopBot.addressType = type;
  }
  g_stopBotDirty = true;
  g_ble.setStop(address, type);
  if (address && address[0]) holdForCheck(g_stopHoldUntilMs, g_powerHoldUntilMs);
}

bbw::PusherSecrets pusherSecrets(BotRole role) {
  Lock lock;
  return role == BotRole::POWER ? g_pushers.power : role == BotRole::STOP ? g_stopBot.secrets : g_pushers.brew;
}

void applyPusherSecrets(BotRole role, const bbw::PusherSecrets &secrets) {
  {
    Lock lock;
    bbw::PusherSecrets &stored = role == BotRole::POWER ? g_pushers.power
                                 : role == BotRole::STOP ? g_stopBot.secrets : g_pushers.brew;
    stored = secrets;
    bbw::sanitizePusherConfig(g_pushers);
    bbw::sanitizeStopBotConfig(g_stopBot);
    bbw::PusherSecrets use = stored;
    // The brew Bot's SwitchBot password stays where it always was, in the network record.
    if (role == BotRole::BREW) strlcpy(use.password, g_net.botPassword, sizeof(use.password));
    (role == BotRole::POWER ? g_power : role == BotRole::STOP ? g_stop : g_bot).setSecrets(use);
  }
  (role == BotRole::STOP ? g_stopBotDirty : g_pushersDirty) = true;
  // Try the new key at once.
  if (role == BotRole::POWER) holdForCheck(g_powerHoldUntilMs, g_stopHoldUntilMs);
  else if (role == BotRole::STOP) holdForCheck(g_stopHoldUntilMs, g_powerHoldUntilMs);
  else g_botHoldUntilMs = millis() + BOT_PAIR_CHECK_MS;
}

// Connect a bot without pressing anything, so it reports its battery and
// firmware: up to three minutes, a sleeping SwitchBot advertises rarely.
bool wakeBot(BotRole role) {
  constexpr uint32_t WAKE_MS = 180000;
  const uint32_t until = millis() + WAKE_MS;
  if (role == BotRole::POWER) {
    if (g_pushers.powerAddress[0] == '\0') return false;
    g_powerHoldUntilMs = until;
  } else if (role == BotRole::STOP) {
    if (g_stopBot.address[0] == '\0') return false;
    g_stopHoldUntilMs = until;
  } else {
    if (g_ble.pairing().botAddress[0] == '\0') return false;
    g_botHoldUntilMs = until;
  }
  LOGI("%s bot: woken to read its status", role == BotRole::POWER ? "power" : role == BotRole::STOP ? "stop" : "brew");
  return true;
}

void resetHeard() { g_ble.resetHeard(); }

// What was heard since the counts were reset, per minute, and how the bot
// links are doing: the measure for the radio test switch.
void radioTestReport(JsonObject out) {
  const uint32_t since = g_ble.heardSinceMs();
  const float minutes = (millis() - since) / 60000.0f;
  out["measuring_s"] = (millis() - since) / 1000;
  out["adverts"] = g_ble.advertsSinceReset();
  if (minutes > 0.01f) out["adverts_per_s"] = g_ble.advertsSinceReset() / (minutes * 60.0f);
  bbw::SeenDevice seen[bbw::BleManager::MAX_SEEN];
  const size_t n = g_ble.seen(seen, bbw::BleManager::MAX_SEEN);
  JsonArray hl = out["heard"].to<JsonArray>();
  for (size_t i = 0; i < n; ++i) {
    JsonObject o = hl.add<JsonObject>();
    o["address"] = seen[i].address;
    o["name"] = seen[i].name;
    o["rssi"] = seen[i].rssi;
    o["times"] = seen[i].heard;
    if (minutes > 0.01f) o["per_min"] = seen[i].heard / minutes;
    o["last_s_ago"] = (millis() - seen[i].seenAtMs) / 1000;
  }
  JsonObject links = out["links"].to<JsonObject>();
  const struct { const char *name; const char *which; } list[] = {
      {"scale", "scale"}, {"brew_bot", "bot"}, {"power_bot", "power"}};
  for (const auto &l : list) {
    const bbw::LinkStats s = g_ble.linkStats(l.which);
    JsonObject o = links[l.name].to<JsonObject>();
    o["attempts"] = s.attempts;
    o["failures"] = s.failures;
    o["connects"] = s.connects;
    o["last_wait_ms"] = s.lastWaitMs;
    o["longest_wait_ms"] = s.longestWaitMs;
  }
  out["scale_connected"] = g_scale.connected();
  out["brew_bot_connected"] = g_bot.linkUp();
  out["power_bot_connected"] = g_power.linkUp();
}

PowerView powerView() {
  Lock lock;
  PowerView v;
  v.paired = g_pushers.powerAddress[0] != '\0';
  v.belief = g_powerControl.belief();
  v.working = g_powerControl.wantsLink();
  v.lastOutcome = bbw::PowerControl::outcomeText(g_powerControl.lastOutcome());
  v.lastWasSchedule = g_powerControl.lastWasSchedule();
  memcpy(v.schedule, g_pushers.schedule, sizeof(v.schedule));
  v.utcOffsetMin = g_pushers.utcOffsetMin;
  return v;
}

bool coffeeExists(uint16_t id) {
  Lock lock;
  return id != 0 && bbw::findCoffee(g_library, id) != nullptr;
}
void ratingSaved(uint32_t id) { g_ratingToShare = id; }

bool powerRequest(bool on) {
  Lock lock;
  return g_powerControl.request(on, millis());
}

// Everything about how the controller itself is doing, in one place, for
// remote analysis of a deployed board (GET /api/diagnostics).
void diagnostics(JsonObject out) {
  JsonObject fw = out["firmware"].to<JsonObject>();
  fw["version"] = BBW_VERSION;
  fw["built"] = BBW_BUILT;
  fw["reset_reason"] = resetReasonText();
  fw["boot_count"] = g_bootCount;
  fw["uptime_s"] = millis() / 1000;
  bbw::SlotInfo slots[4];
  const size_t n = bbw::updateSlots(slots, 4);
  for (size_t i = 0; i < n; ++i) {
    if (slots[i].running) fw["running_slot"] = slots[i].label;
  }
  const bbw::UpdateStatus u = bbw::updateStatus();
  fw["update_state"] = bbw::updatePhaseName(u.phase);
  fw["update_message"] = u.message;
  if (u.latest.valid) fw["latest_release"] = u.latest.tag;

  const bbw::MemoryReport memory = bbw::memoryReport();
  JsonObject mem = out["memory"].to<JsonObject>();
  mem["internal_free"] = memory.internalFree;
  mem["internal_lowest"] = memory.internalLowest;
  mem["internal_largest"] = memory.internalLargest;
  mem["psram_total"] = memory.psramTotal;
  mem["psram_free"] = memory.psramFree;
#ifndef BBW_EMULATOR
  JsonObject stacks = out["stack_left"].to<JsonObject>();
  stacks["loop"] = uxTaskGetStackHighWaterMark(nullptr);
  if (g_controlTask) stacks["control"] = uxTaskGetStackHighWaterMark(g_controlTask);
  if (g_bleTask) stacks["ble"] = uxTaskGetStackHighWaterMark(g_bleTask);
#endif

  JsonObject wifi = out["wifi"].to<JsonObject>();
  wifi["station_ssid"] = g_net.staSsid;
#ifndef BBW_EMULATOR
  wifi["station_connected"] = WiFi.status() == WL_CONNECTED;
  if (WiFi.status() == WL_CONNECTED) {
    wifi["rssi_dbm"] = WiFi.RSSI();
    wifi["ip"] = WiFi.localIP().toString();
  }
#endif
  const time_t now = time(nullptr);
  out["clock_set"] = now > 1700000000;
  if (now > 1700000000) out["epoch"] = static_cast<uint32_t>(now);

  JsonObject ble = out["ble"].to<JsonObject>();
  const bbw::Pairing pairing = g_ble.pairing();
  char power[bbw::ADDR_LEN], stop[bbw::ADDR_LEN];
  g_ble.powerAddress(power);
  g_ble.stopAddress(stop);
  struct Link { const char *key; const char *which; const char *address; bool up; };
  const Link links[] = {
      {"scale", "scale", pairing.scaleAddress, g_scale.connected()},
      {"brew_bot", "bot", pairing.botAddress, g_bot.linkUp()},
      {"power_bot", "power", power, g_power.linkUp()},
      {"stop_bot", "stop", stop, g_stop.linkUp()},
  };
  for (const Link &l : links) {
    JsonObject o = ble[l.key].to<JsonObject>();
    o["paired"] = l.address;
    o["connected"] = l.up;
    const bbw::LinkStats s = g_ble.linkStats(l.which);
    o["attempts"] = s.attempts;
    o["failures"] = s.failures;
    o["connects"] = s.connects;
    o["last_wait_ms"] = s.lastWaitMs;
    o["longest_wait_ms"] = s.longestWaitMs;
  }
  const bbw::ScaleDiag scale = g_scale.diag();
  JsonObject sd = ble["scale_data"].to<JsonObject>();
  sd["frames"] = scale.frames;
  sd["weight_frames"] = scale.weightFrames;
  sd["timer_frames"] = scale.timerFrames;
  sd["bad_frames"] = scale.badFrames;
  sd["split_frames"] = scale.splitFrames;
  sd["dropped"] = scale.dropped;
  if (scale.batteryPercent >= 0) sd["battery"] = scale.batteryPercent;
  bbw::SeenDevice seen[bbw::BleManager::MAX_SEEN];
  const size_t heard = g_ble.seen(seen, bbw::BleManager::MAX_SEEN);
  ble["adverts_heard_total"] = g_ble.advertsHeard();
  JsonArray hl = ble["heard"].to<JsonArray>();
  for (size_t i = 0; i < heard; ++i) {
    JsonObject o = hl.add<JsonObject>();
    o["address"] = seen[i].address;
    o["name"] = seen[i].name;
    o["rssi"] = seen[i].rssi;
    o["times"] = seen[i].heard;
    o["last_s_ago"] = (millis() - seen[i].seenAtMs) / 1000;
  }

  JsonObject st = out["storage"].to<JsonObject>();
  st["ok"] = g_store.historyOk();
  st["fs_total"] = LittleFS.totalBytes();
  st["fs_used"] = LittleFS.usedBytes();
  st["shots"] = g_store.shotCount();
  st["next_shot_id"] = g_store.nextShotId();
  st["log_flash_bytes"] = bbw::logFlashBytes();

  const bbw::LiveStatus live = status();
  JsonObject c = out["controller"].to<JsonObject>();
  c["state"] = bbw::stateName(live.state);
  c["cup_present"] = live.cupPresent;
  c["sample_period_ms"] = live.samplePeriodMs;
  {
    Lock lock;
    c["scale_zeroes_itself"] = g_controller.scaleZeroesItself();
  }
}

bool scaleZeroesItself() {
  Lock lock;
  return g_controller.scaleZeroesItself();
}

bool testPressBrew() {
  {
    Lock lock;
    const State state = g_controller.state();
    if (g_controller.machineMayBeRunning() ||
        (state != State::READY && state != State::CONNECTING_SCALE)) {
      return false;
    }
  }
  if (g_ble.pairing().botAddress[0] == '\0') return false;
  const uint32_t now = millis();
  g_botHoldUntilMs = now + TEST_PRESS_PATIENCE_MS + BOT_LINGER_MS;  // wake the bot
  g_testPressUntilMs = now + TEST_PRESS_PATIENCE_MS;
  g_testPress = TEST_WAITING;
  return true;
}

const char *testPressState() {
  switch (g_testPress.load()) {
    case TEST_WAITING: return "waiting";
    case TEST_PRESSED: return "pressed";
    case TEST_FAILED: return "failed";
    default: return "none";
  }
}

void restartSoon() {
  LOGI("restarting");
  bbw::logFlush();
  delay(300);
  ESP.restart();
}

bool powerPressAnyway() {
  Lock lock;
  return g_powerControl.pressAnyway(millis());
}

void powerBelieve(bbw::PowerBelief belief) {
  Lock lock;
  g_powerControl.believe(belief);
  if (g_powerControl.takeDirty()) g_pushersDirty = true;
}

void applyPowerSchedule(const bbw::PowerScheduleEntry *entries, int16_t utcOffsetMin) {
  {
    Lock lock;
    memcpy(g_pushers.schedule, entries, sizeof(g_pushers.schedule));
    g_pushers.utcOffsetMin = utcOffsetMin;
    bbw::sanitizePusherConfig(g_pushers);
  }
  g_pushersDirty = true;
}

bbw::NetConfig netConfig() { return g_net; }

void applyNetConfig(const bbw::NetConfig &net) {
  g_net = net;
  g_store.saveNet(g_net);
}

bbw::Store &store() { return g_store; }

uint32_t epochNow() {
  const time_t now = time(nullptr);
  return now > 1700000000 ? static_cast<uint32_t>(now) : 0;
}

void setEpoch(uint32_t epochS) {
  if (epochNow() != 0) return;  // keep a clock that is already set
  const timeval tv = {static_cast<time_t>(epochS), 0};
  settimeofday(&tv, nullptr);
}

// ------------------------------------------------------------ subscriptions --

void readSubscriptions(void (*visit)(const bbw::Subscription *, size_t, void *), void *context) {
  Lock lock;
  visit(g_subs, g_subCount, context);
}

uint16_t saveSubscription(const bbw::Subscription &in) {
  Lock lock;
  bbw::Subscription s = in;
  bbw::sanitizeSubscription(s);
  // Where it stands with the library is the controller's to know, never the page's: kept as it was,
  // or, for one just added from the library's list, published already (nothing to send).
  s.shareState = 0;
  s.shareSeq = s.shareHash = 0;
  s.reviewNote[0] = '\0';
  if (s.id == 0) {
    if (s.libraryId[0]) s.shareState = static_cast<uint8_t>(bbw::ShareState::PUBLISHED);
  } else {
    for (size_t i = 0; i < g_subCount; ++i) {
      if (g_subs[i].id != s.id) continue;
      memcpy(s.libraryId, g_subs[i].libraryId, sizeof(s.libraryId));
      s.shareState = g_subs[i].shareState;
      s.shareSeq = g_subs[i].shareSeq;
      s.shareHash = g_subs[i].shareHash;
      memcpy(s.reviewNote, g_subs[i].reviewNote, sizeof(s.reviewNote));
    }
  }
  if (s.id == 0) {
    if (g_subCount >= bbw::MAX_SUBSCRIPTIONS) return 0;
    s.id = g_nextSubId++;
    if (g_nextSubId == 0) g_nextSubId = 1;
    g_subs[g_subCount++] = s;
  } else {
    size_t i = 0;
    while (i < g_subCount && g_subs[i].id != s.id) ++i;
    if (i == g_subCount) return 0;
    g_subs[i] = s;
  }
  g_subsDirty = true;
  return s.id;
}

bool deleteSubscription(uint16_t id) {
  Lock lock;
  for (size_t i = 0; i < g_subCount; ++i) {
    if (g_subs[i].id != id) continue;
    for (size_t j = i; j + 1 < g_subCount; ++j) g_subs[j] = g_subs[j + 1];
    --g_subCount;
    g_subsDirty = true;
    size_t keep = 0;
    for (size_t k = 0; k < g_reviewCount; ++k) {
      if (g_reviews[k].subscriptionId != id) g_reviews[keep++] = g_reviews[k];
    }
    if (keep != g_reviewCount) {
      g_reviewCount = keep;
      g_reviewsDirty = true;
    }
    return true;
  }
  return false;
}

namespace {
bbw::Subscription *findSubscription(uint16_t id) {
  for (size_t i = 0; i < g_subCount; ++i) if (g_subs[i].id == id) return &g_subs[i];
  return nullptr;
}
}  // namespace

bool subscriptionArrived(uint16_t id) {
  Lock lock;
  bbw::Subscription *s = findSubscription(id);
  if (!s) return false;
  bbw::subscriptionArrived(*s, bbw::dateFromEpoch(epochNow()));
  g_subsDirty = true;
  LOGI("subscriptions: %s arrived, next %u", s->name, static_cast<unsigned>(s->nextDate));
  return true;
}

bool subscriptionSkipped(uint16_t id) {
  Lock lock;
  bbw::Subscription *s = findSubscription(id);
  if (!s) return false;
  s->nextDate = bbw::nextDateAfter(*s, bbw::dateFromEpoch(epochNow()));
  g_subsDirty = true;
  return true;
}

float shotsPerDay() {
  static float cached = 0.0f;
  static uint32_t cachedAtMs = 0;
  static uint32_t cachedShot = 0xFFFFFFFFu;
  const uint32_t nowS = epochNow();
  if (nowS == 0) return 0.0f;
  bbw::ShotRecord newest;
  const uint32_t newestId = g_store.readShot(0, newest) ? newest.id : 0;
  if (cachedAtMs != 0 && newestId == cachedShot && millis() - cachedAtMs < 3600000u) return cached;
  static uint32_t epochs[200];
  size_t n = 0;
  bool historyEnds = false;   // the kept history began inside the 14 days
  for (uint32_t i = 0; i < 200; ++i) {
    bbw::ShotRecord r;
    if (!g_store.readShot(i, r)) {
      historyEnds = true;
      break;
    }
    if (r.epochS != 0 && nowS - r.epochS > 14u * 86400u && r.epochS < nowS) break;
    epochs[n++] = r.epochS;
  }
  uint32_t keptSince = 0;
  if (historyEnds) {
    for (size_t i = 0; i < n; ++i) {
      if (epochs[i] != 0 && (keptSince == 0 || epochs[i] < keptSince)) keptSince = epochs[i];
    }
  }
  cached = bbw::shotsPerDay(epochs, n, nowS, 14, keptSince);
  cachedAtMs = millis() ? millis() : 1;
  cachedShot = newestId;
  return cached;
}

void subscriptionToJson(const bbw::Subscription &s, JsonObject o) {
  o["id"] = s.id;
  o["kind"] = bbw::subKindName(s.kind);
  o["status"] = bbw::subStatusName(s.status);
  o["name"] = s.name;
  o["url"] = s.url;
  o["manage_url"] = s.manageUrl;
  o["every_days"] = s.everyDays;
  o["next_date"] = s.nextDate;
  o["price_cents"] = s.priceCents;
  o["last_arrived"] = s.lastArrived;
  o["note"] = s.note;
  o["title"] = s.title;
  o["site"] = s.site;
  o["description"] = s.description;
  o["image"] = s.image;
  o["icon"] = s.icon;
  o["color"] = s.color;
  o["platform"] = s.platform;
  o["item"] = s.item;
  o["stock"] = s.stock;
  o["per_shot"] = s.perShot;
  o["per_delivery"] = s.perDelivery;
  o["ships_to"] = s.shipsTo;
  JsonArray cad = o["cadences"].to<JsonArray>();
  for (uint8_t i = 0; i < bbw::SERVICE_CADENCES; ++i) if (s.cadences[i]) cad.add(s.cadences[i]);
  o["price_from_cents"] = s.priceFromCents;
  o["currency"] = s.currency;
  JsonArray style = o["style"].to<JsonArray>();
  for (uint8_t i = 0; i < bbw::SERVICE_STYLE_COUNT; ++i) if ((s.styleMask >> i) & 1u) style.add(bbw::SERVICE_STYLES[i]);
  JsonArray sourcing = o["sourcing"].to<JsonArray>();
  for (uint8_t i = 0; i < bbw::SERVICE_SOURCING_COUNT; ++i) {
    if ((s.sourcingMask >> i) & 1u) sourcing.add(bbw::SERVICE_SOURCING[i]);
  }
  o["library_id"] = s.libraryId;
  o["share_state"] = s.shareState;
  o["share_seq"] = s.shareSeq;
  o["share_hash"] = s.shareHash;
  o["review_note"] = s.reviewNote;
}

void subscriptionFromJson(JsonObjectConst o, bbw::Subscription &s) {
  s = bbw::Subscription();
  s.id = o["id"] | 0;
  s.kind = bbw::subKindFrom(o["kind"] | "coffee");
  s.status = bbw::subStatusFrom(o["status"] | "active");
  bbw::subCopy(s.name, o["name"] | "", sizeof(s.name));
  bbw::subCopy(s.url, o["url"] | "", sizeof(s.url));
  bbw::subCopy(s.manageUrl, o["manage_url"] | "", sizeof(s.manageUrl));
  s.everyDays = o["every_days"] | 0;
  s.nextDate = o["next_date"] | 0;
  s.priceCents = o["price_cents"] | 0;
  s.lastArrived = o["last_arrived"] | 0;
  bbw::subCopy(s.note, o["note"] | "", sizeof(s.note));
  bbw::subCopy(s.title, o["title"] | "", sizeof(s.title));
  bbw::subCopy(s.site, o["site"] | "", sizeof(s.site));
  bbw::subCopy(s.description, o["description"] | "", sizeof(s.description));
  bbw::subCopy(s.image, o["image"] | "", sizeof(s.image));
  bbw::subCopy(s.icon, o["icon"] | "", sizeof(s.icon));
  bbw::subCopy(s.color, o["color"] | "", sizeof(s.color));
  bbw::subCopy(s.platform, o["platform"] | "", sizeof(s.platform));
  bbw::subCopy(s.item, o["item"] | "", sizeof(s.item));
  s.stock = o["stock"] | 0;
  s.perShot = o["per_shot"] | 0;
  s.perDelivery = o["per_delivery"] | 0;
  bbw::subCopy(s.shipsTo, o["ships_to"] | "", sizeof(s.shipsTo));
  uint8_t n = 0;
  for (JsonVariantConst d : o["cadences"].as<JsonArrayConst>()) {
    if (n < bbw::SERVICE_CADENCES) s.cadences[n++] = d | 0;
  }
  s.priceFromCents = o["price_from_cents"] | 0;
  bbw::subCopy(s.currency, o["currency"] | "", sizeof(s.currency));
  for (JsonVariantConst v : o["style"].as<JsonArrayConst>()) {
    const int k = bbw::listIndex(bbw::SERVICE_STYLES, bbw::SERVICE_STYLE_COUNT, v | "");
    if (k >= 0) s.styleMask |= static_cast<uint16_t>(1u << k);
  }
  for (JsonVariantConst v : o["sourcing"].as<JsonArrayConst>()) {
    const int k = bbw::listIndex(bbw::SERVICE_SOURCING, bbw::SERVICE_SOURCING_COUNT, v | "");
    if (k >= 0) s.sourcingMask |= static_cast<uint16_t>(1u << k);
  }
  bbw::subCopy(s.libraryId, o["library_id"] | "", sizeof(s.libraryId));
  s.shareState = o["share_state"] | 0;
  s.shareSeq = o["share_seq"] | 0u;
  s.shareHash = o["share_hash"] | 0u;
  bbw::subCopy(s.reviewNote, o["review_note"] | "", sizeof(s.reviewNote));
}

SubscriptionPrefs subscriptionPrefs() {
  Lock lock;
  return g_subPrefs;
}

void setSubscriptionPrefs(const SubscriptionPrefs &p) {
  Lock lock;
  g_subPrefs = p;
  g_subPrefs.country[2] = '\0';
  for (char &c : g_subPrefs.country) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
  if (!(g_subPrefs.country[0] >= 'A' && g_subPrefs.country[0] <= 'Z' && g_subPrefs.country[1] >= 'A' &&
        g_subPrefs.country[1] <= 'Z')) {
    g_subPrefs.country[0] = '\0';
  }
  g_subPrefs.matters &= static_cast<uint16_t>((1u << bbw::SERVICE_SOURCING_COUNT) - 1);
  g_subsDirty = true;
}

bool shareService(uint16_t id) {
  if (!settings().shareEnabled) return false;
  return queueService(id);
}

uint32_t outboxSent() { return g_store.outboxSent(); }

void readServiceReviews(void (*visit)(const bbw::ServiceReview *, size_t, void *), void *context) {
  Lock lock;
  visit(g_reviews, g_reviewCount, context);
}

uint32_t saveServiceReview(const bbw::ServiceReview &in) {
  uint32_t id = 0;
  {
    Lock lock;
    bbw::ServiceReview r = in;
    bbw::sanitizeServiceReview(r);
    bool known = false;
    for (size_t i = 0; i < g_subCount; ++i) known |= g_subs[i].id == r.subscriptionId;
    if (!known) return 0;
    r.epochS = epochNow();
    r.shareState = 0;
    r.shareSeq = 0;
    // One per drinker per subscription: a new one replaces it, keeping its id (the library keeps the latest).
    size_t i = 0;
    while (i < g_reviewCount && !(g_reviews[i].subscriptionId == r.subscriptionId &&
                                  g_reviews[i].drinkerId == r.drinkerId)) {
      ++i;
    }
    if (i == g_reviewCount) {
      if (g_reviewCount >= bbw::MAX_SERVICE_REVIEWS) return 0;
      r.id = g_nextReviewId++;
      ++g_reviewCount;
    } else {
      r.id = g_reviews[i].id;
    }
    g_reviews[i] = r;
    g_reviewsDirty = true;
    id = r.id;
  }
  shareServiceReview(id);
  return id;
}

bool deleteServiceReview(uint32_t id) {
  Lock lock;
  for (size_t i = 0; i < g_reviewCount; ++i) {
    if (g_reviews[i].id != id) continue;
    for (size_t j = i; j + 1 < g_reviewCount; ++j) g_reviews[j] = g_reviews[j + 1];
    --g_reviewCount;
    g_reviewsDirty = true;
    return true;
  }
  return false;
}

const char *communityServices() {
  if (g_store.readText(SERVICES_CACHE, g_libraryText, LIBRARY_TEXT_SIZE) == 0) g_libraryText[0] = '\0';
  return g_libraryText;
}

void requestPagePreview(const char *url) {
  Lock lock;
  strlcpy(g_preview->url, url ? url : "", sizeof(g_preview->url));
  g_preview->meta = bbw::PageMeta();
  g_preview->problem[0] = '\0';
  g_preview->state = PreviewState::WAITING;
}

PreviewState pagePreview(char *url, size_t urlSize, bbw::PageMeta &meta, char *problem, size_t problemSize) {
  Lock lock;
  strlcpy(url, g_preview->url, urlSize);
  meta = g_preview->meta;
  strlcpy(problem, g_preview->problem, problemSize);
  return g_preview->state;
}

}  // namespace app

// ============================================================ setup / loop ==

void setup() {
  Serial.begin(115200);
  // USB serial with a PC attached but nothing reading the port must never
  // stall a task that logs. Not 0: in this core (HWCDC::write) a timeout of 0
  // makes the retry count wrap around and the write waits ~49 days for the
  // port to be read (seen on the board: start-up stuck for 250 s, the screen
  // dark, until the port was opened). After 10 ms without progress the core
  // marks the port as not listening and drops output until it is read again.
  Serial.setTxTimeoutMs(10);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  const bool resumedMidShot = esp_reset_reason() != ESP_RST_POWERON &&
                              g_shotInProgress == SHOT_IN_PROGRESS_MAGIC;
  g_shotInProgress = 0;

  g_lock = xSemaphoreCreateMutex();
  bbw::tlsUseExternalMemory();  // before any HTTPS connection
  allocateBuffers();  // before anything that uses them; PSRAM is up by now
  const bool filesystemOk = g_store.begin();
  bbw::logBegin(filesystemOk);
  {
    Preferences prefs;
    if (prefs.begin("bbw", false)) {
      uint32_t boots = 0;
      if (prefs.isKey("boots") && prefs.getBytesLength("boots") == sizeof(boots)) {
        prefs.getBytes("boots", &boots, sizeof(boots));
      }
      g_bootCount = boots + 1;
      prefs.putBytes("boots", &g_bootCount, sizeof(g_bootCount));
      prefs.end();
    }
  }
  // The first line kept: what is running, and why it started. After a crash
  // or a watchdog reset this is the line to look for in the Logs page.
  LOGI("boot: firmware %s (built %s), reset reason: %s", BBW_VERSION, BBW_BUILT, resetReasonText());

  g_settings = g_store.loadSettings();
  g_store.loadLibrary(g_library);
  LOGI("start-up: library loaded");
  loadSubscriptions();
  loadServiceReviews();
  if (bbw::beginDeviceKey()) {
    char kid[bbw::KEY_ID_LEN], last[bbw::KEY_ID_LEN + 2];
    bbw::keyId(bbw::deviceSigner().publicKey(), kid);
    const bool had = g_store.readText("/sharekey.txt", last, sizeof(last)) != 0;
    if (!had || strcmp(last, kid) != 0) {
      if (had || g_store.outboxSent() != 0 || g_store.outboxKeyQueued()) {
        // A different key from the one records were signed with before: the
        // library knows nothing under this one. Start the sharing over.
        bbw::forgetSharing(g_library);
        g_store.saveLibrary(g_library);
        g_store.outboxForgetKey();
        LOGW("share: this controller's key has changed; what was sent before is forgotten");
      }
      g_store.writeTextIfChanged("/sharekey.txt", kid);
    }
  }
  LOGI("start-up: device key ready");
  g_net = g_store.loadNet();
  g_maintenance = g_store.loadMaintenance();
  bbw::libraryLinkConfigure(g_net.libraryRepo, g_net.libraryToken);
  const bbw::Pairing pairing = g_store.loadPairing();
  // The cup on the scale before a restart is believed again (core/BrewController.h CupMemory), but
  // not after the power was off: the scale may have been switched off and on meanwhile.
  if (esp_reset_reason() != ESP_RST_POWERON && esp_reset_reason() != ESP_RST_BROWNOUT) {
    g_controller.restoreCupMemory(g_store.loadCupMemory());
  }

  LOGI("start-up: settings read, starting the display");
  ui::begin();
  LOGI("start-up: display ready");

  g_controller.setNextShotId(g_store.nextShotId() ? g_store.nextShotId() : 1);
  g_controller.epochNow = app::epochNow;
  g_scale.begin();
  g_machine = g_store.loadMachine();
  g_controller.setMachine(&g_machine);
  g_pushers = g_store.loadPushers();
  g_powerControl.begin(&g_pushers);
  bbw::PusherSecrets brewSecrets = g_pushers.brew;
  strlcpy(brewSecrets.password, g_net.botPassword, sizeof(brewSecrets.password));
  g_bot.begin("bot", &g_settings, brewSecrets);
  g_power.begin("power", &g_settings, g_pushers.power);
  g_stopBot = g_store.loadStopBot();
  g_stop.begin("stop", &g_settings, g_stopBot.secrets);
  g_controller.setStopActuator(&g_stop);
  LOGI("start-up: starting Bluetooth");
  if (!g_ble.begin(pairing, g_pushers.powerAddress, g_pushers.powerAddressType, g_stopBot.address,
                   g_stopBot.addressType)) {
    LOGW("ble: init failed");
    g_controller.raiseBleError();
  } else {
#ifdef BBW_EMULATOR
    xTaskCreatePinnedToCore(bleTask, "ble", 8192, nullptr, 2, nullptr, 0);
#else
    xTaskCreatePinnedToCore(bleTask, "ble", 8192, nullptr, 2, &g_bleTask, 0);
#endif
  }
  if (resumedMidShot) {
    LOGW("restarted while a shot was running: will stop it if the scale still shows flow");
    g_controller.armRestartRecovery(millis());
  }

  esp_task_wdt_init(5, true);  // a stuck control task resets the controller
#ifdef BBW_EMULATOR
  xTaskCreatePinnedToCore(controlTask, "control", 8192, nullptr, 5, nullptr, 1);
#else
  xTaskCreatePinnedToCore(controlTask, "control", 8192, nullptr, 5, &g_controlTask, 1);
#endif

  LOGI("start-up: starting Wi-Fi and the web server");
  web::begin();
  bbw::updateBegin(g_store.loadUpdateConfig());
  LOGI("start-up: done in %u ms", static_cast<unsigned>(millis()));
  if (g_net.staSsid[0]) configTime(0, 0, "pool.ntp.org");  // optional; the browser also sets the clock
  const bbw::MemoryReport memory = bbw::memoryReport();
  if (memory.known) {
    LOGI("memory: inside the chip %u free (largest piece %u), PSRAM %u of %u free",
         static_cast<unsigned>(memory.internalFree), static_cast<unsigned>(memory.internalLargest),
         static_cast<unsigned>(memory.psramFree), static_cast<unsigned>(memory.psramTotal));
    if (memory.psramTotal == 0) LOGW("memory: no PSRAM found, large buffers are inside the chip");
  }
  const bbw::Recipe active = app::activeRecipe();
  LOGI("ready. recipe '%s' %.1f g", active.name, active.targetWeightG);
}

void loop() {
  ui::loop();
  web::loop();
  handleButton();
  handleSerial();
  logLoop();
  confirmFirmware();
  logHealth();

  // Persistence happens here, never in the control task.
  bbw::ShotRecord shot;
  bbw::CupMemory cupNow;
  size_t curvePoints = 0;
  bool haveShot = false, learned = false;
  bool idle = false, saveSettings = false, saveLibrary = false;
  bbw::Settings snapshot;
  {
    Lock lock;
    haveShot = g_controller.takeCompletedShot(shot);
    if (haveShot) {
      // Every reading of the shot, copied while the controller cannot start
      // the next one; written to flash below, outside the lock.
      const bbw::ShotCurve &curve = g_controller.curve();
      curvePoints = curve.count;
      for (size_t i = 0; i < curvePoints; ++i) {
        g_curveOut[i] = bbw::packPoint(curve.timeS[i], curve.weightG[i], curve.timerDs[i],
                                       curve.flags[i]);
      }
    }
    learned = g_controller.takeSettingsDirty();
    idle = !g_controller.machineMayBeRunning();
    cupNow = g_controller.cupMemory();
    if (learned) markLibraryDirty();  // the learned offset belongs to the preset
    if (idle && (learned || millis() - g_settingsDirtyAtMs >= SETTINGS_SAVE_DELAY_MS)) {
      saveSettings = g_settingsDirty;
      saveLibrary = g_libraryDirty;
      g_settingsDirty = g_libraryDirty = false;
      if (saveSettings) snapshot = g_settings;
      if (saveLibrary) (*g_librarySaving) = g_library;
    }
  }
  if (haveShot) {
    printShot(shot);
    if (bbw::upkeepCountsShot(shot)) {
      Lock lock;
      bbw::countUpkeepShot(g_maintenance);
      g_maintenanceDirty = true;
    }
    // The shot before this one, for the time between them (read before this one is appended).
    uint32_t previousEpochS = 0;
    {
      bbw::ShotRecord previous;
      if (g_store.readShot(0, previous)) previousEpochS = previous.epochS;
    }
    if (!g_store.appendShot(shot)) LOGW("store: shot %u not saved", static_cast<unsigned>(shot.id));
    if (g_store.saveCurve(shot.id, g_curveOut, curvePoints)) {
      LOGI("store: shot %u data, %u readings", static_cast<unsigned>(shot.id),
           static_cast<unsigned>(curvePoints));
    } else {
      LOGW("store: shot %u data not saved", static_cast<unsigned>(shot.id));
    }
    g_grindAdviceStale = true;
    subscriptionsShotTaken();
    // What surrounded the shot, kept for the learning work (core/ShotContext.h). Recorded only: nothing
    // reads it back to steer a shot.
    {
      static bbw::ShotContextInputs in;
      in = bbw::ShotContextInputs();
      in.shot = &shot;
      in.previousEpochS = previousEpochS;
      // The bag as the coffee entry holds it now, which is the bag the shot was just made from.
      static uint16_t wantCoffee;
      static uint32_t roastDate, frozenDate, thawDate;
      wantCoffee = shot.coffeeId;
      roastDate = shot.roastDate;
      frozenDate = thawDate = 0;
      app::readLibrary([](const bbw::Library &lib, void *) {
        const bbw::Coffee *c = bbw::findCoffee(lib, wantCoffee);
        if (c) {
          roastDate = c->roastDate;
          frozenDate = c->frozenDate;
          thawDate = c->thawDate;
        }
      }, nullptr);
      in.roastDate = roastDate;
      in.frozenDate = frozenDate;
      in.thawDate = thawDate;
      if (app::activeRecipe().coffeeId == shot.coffeeId && app::activeRecipe().recipeId == shot.recipeId) {
        in.start = app::communityStartForActive();
      }
      in.similar = app::similarStart(shot.coffeeId, shot.recipeId);
      {
        Lock lock;
        in.grinderCleanShots = bbw::shotsSince(g_maintenance, bbw::Upkeep::GRINDER_CLEAN);
      }
      in.advice = app::grindAdvice();
      const bbw::Settings st = app::settings();
      in.toleranceS = st.grindToleranceS;
      in.window = st.grindWindow;
      in.maxStepUm = st.grindMaxStepUm;
      in.defaultUmPerS = st.grindUmPerS;
      in.ratedTarget = st.grindRatedTarget;
      in.learnGrinder = true;   // always since 17 Oct 2026; records from 16-17 Oct may say otherwise
      in.curve = g_curveOut;
      in.curvePoints = curvePoints;
      if (!g_store.saveContext(bbw::buildShotContext(in))) {
        LOGW("store: shot %u context not saved", static_cast<unsigned>(shot.id));
      }
    }
    shareAfterShot(shot);
  }
  if (idle && g_maintenanceDirty) {
    static bbw::Maintenance snapshot;
    {
      Lock lock;
      snapshot = g_maintenance;
      g_maintenanceDirty = false;
    }
    g_store.saveMaintenance(snapshot);
  }
  bbw::logService(idle);
  bbw::updateService(idle, true);
  if (bbw::updateStatus().phase == bbw::UpdatePhase::INSTALLED) {
    // the update task restarts the controller itself in a moment
  }
  shareSendOne(idle);
  shareReadStatus(idle);
  libraryRefresh(idle);
  subscriptionsService(idle);
  if (saveSettings) g_store.saveSettings(snapshot);
  if (saveLibrary && !g_store.saveLibrary((*g_librarySaving))) LOGW("store: library not saved");
  if (g_ble.takePairingDirty()) g_store.savePairing(g_ble.pairing());
  {
    // The cup on the scale, for after a restart. Written only when it
    // changes (a cup on or off, a tare, the scale's timer), and between shots.
    static bbw::CupMemory saved;
    static bool loaded = false;
    if (!loaded) {
      saved = g_store.loadCupMemory();
      loaded = true;
    }
    if (idle && (cupNow.present != saved.present || cupNow.scaleTimerMs != saved.scaleTimerMs ||
                 fabsf(cupNow.levelG - saved.levelG) > 0.2f)) {
      g_store.saveCupMemory(cupNow);
      saved = cupNow;
    }
  }
  if (g_machineDirty.exchange(false)) g_store.saveMachine(app::machineProfile());
  if (g_stopBotDirty.exchange(false)) {
    bbw::StopBotConfig copy;
    {
      Lock lock;
      copy = g_stopBot;
    }
    g_store.saveStopBot(copy);
  }
  {
    bbw::PowerEvent events[4];
    uint8_t count = 0;
    {
      Lock lock;
      count = g_powerEventsQueued;
      for (uint8_t i = 0; i < count; ++i) events[i] = g_powerEventQueue[i];
      g_powerEventsQueued = 0;
    }
    for (uint8_t i = 0; i < count; ++i) {
      if (!g_store.appendPowerEvent(events[i])) LOGW("power: the press could not be logged");
    }
  }
  // A coffee rating just saved: to the library, when ratings are shared.
  if (const uint32_t ratingId = g_ratingToShare.exchange(0)) shareRating(ratingId);
  if (g_pushersDirty.exchange(false)) {
    bbw::PusherConfig copy;
    {
      Lock lock;
      copy = g_pushers;
    }
    g_store.savePushers(copy);
  }
  // Standby (scale off, screen dark): the loop only has the web server and
  // the touch panel to look after, so it can idle most of the time.
  delay(ui::isOff() ? 25 : 2);
}

#ifndef BBW_EMULATOR
// The loop task serves the web pages, parses the library's JSON and takes
// firmware uploads: twice the Arduino default of 8 kB (measured on the board:
// about 5.4 kB used at the default). This must be at global scope: inside a
// namespace the core does not see it and silently keeps 8 kB.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// Arduino would otherwise mark a new firmware as good the moment it starts.
// confirmFirmware() does that once it has run for a while instead.
extern "C" bool verifyRollbackLater() { return true; }
#endif
