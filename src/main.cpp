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
#include <atomic>
#include <esp_task_wdt.h>
#include <sys/time.h>

#include "App.h"
#include "Log.h"
#include "ble/Ble.h"
#include "core/BrewController.h"
#include "core/SettingsUtil.h"
#include "net/ShotFormat.h"
#include "net/Web.h"
#include "platform/BigMem.h"
#include "platform/DeviceKey.h"
#include "storage/Store.h"
#include "ui/Ui.h"

using bbw::State;

#ifdef BBW_EMULATOR
// Where the emulator "sends" records: a folder beside its data (emu/src).
bool emuPublish(const char *path, const char *text);
// What the "library" says back: published/status/<key id>.json, if someone put it there.
bool emuFetchStatus(const char *kid, char *out, size_t size);
// Any file of the "library": published/<path>.
bool emuFetchFile(const char *path, char *out, size_t size);
#endif

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
bbw::PusherConfig g_pushers;
bbw::PowerControl g_powerControl;
std::atomic<bool> g_pushersDirty{false};
std::atomic<uint32_t> g_powerHoldUntilMs{0};  // keep the power bot's link up until then
bbw::BleManager g_ble(g_scale, g_bot, g_power);
bbw::BrewController g_controller(g_scale, g_bot, g_settings, g_library);

SemaphoreHandle_t g_lock;  // guards g_controller, g_settings and g_library
bool g_settingsDirty = false;
bool g_libraryDirty = false;
bool g_grindAdviceStale = true;  // loop task only
bbw::GrindAdvice g_grindAdvice;
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

#ifdef BBW_EMULATOR
constexpr bool CAN_PUBLISH = true;
constexpr const char *PUBLISHES_TO = "a folder beside the emulator's data (published/)";
bool publishRecord(const char *path, const char *text) { return emuPublish(path, text); }
#else
// No repository yet: records wait in the outbox.
constexpr bool CAN_PUBLISH = false;
constexpr const char *PUBLISHES_TO = "nowhere yet: records wait on the controller";
bool publishRecord(const char *, const char *) { return false; }
#endif

constexpr uint32_t SHARE_SEND_PERIOD_MS = 1000;
constexpr size_t LIBRARY_TEXT_SIZE = 16384;
char *g_shareBody;      // SHARE_BODY_MAX
char *g_shareEnvelope;  // SHARE_ENVELOPE_MAX
// Whatever was last read from the library, or from its copy on flash. Enough
// for a list of about a hundred coffees; a bigger library will need searching
// on its side, not a whole list here.
char *g_libraryText;    // LIBRARY_TEXT_SIZE
bbw::CurvePoint *g_curveOut;  // MAX_CURVE_POINTS: a shot's readings on their way to flash

void allocateBuffers() {
  g_libraryBefore = bbw::bigNew<bbw::Library>();
  g_librarySaving = bbw::bigNew<bbw::Library>();
  g_shareBody = bbw::bigArray<char>(bbw::SHARE_BODY_MAX);
  g_shareEnvelope = bbw::bigArray<char>(bbw::SHARE_ENVELOPE_MAX);
  g_libraryText = bbw::bigArray<char>(LIBRARY_TEXT_SIZE);
  g_curveOut = bbw::bigArray<bbw::CurvePoint>(bbw::MAX_CURVE_POINTS);
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
  if (g_shareCoffee.shareState != 0 && !bbw::coffeeChangedSinceShared(g_shareCoffee)) return false;
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
#ifdef BBW_EMULATOR
bool fetchStatus(const char *kid, char *out, size_t size) { return emuFetchStatus(kid, out, size); }
#else
bool fetchStatus(const char *, char *, size_t) { return false; }  // no repository yet
#endif

constexpr uint32_t SHARE_STATUS_PERIOD_MS = 15000;

void shareReadStatus(bool idle) {
  static uint32_t lastMs = 0;
  if (!idle || !bbw::deviceSigner().ready() || millis() - lastMs < SHARE_STATUS_PERIOD_MS) return;
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
                                    : bbw::ShareState::LOCAL;
      any |= bbw::applyCoffeeReview(lib, entry["local_id"] | 0, entry["record"] | 0u, state,
                                    entry["note"] | "", entry["library_id"] | "");
    }
    for (JsonObjectConst entry : doc["grinders"].as<JsonArrayConst>()) {
      const char *name = entry["state"] | "";
      const bbw::ShareState state = !strcmp(name, "review") ? bbw::ShareState::IN_REVIEW
                                    : !strcmp(name, "published") ? bbw::ShareState::PUBLISHED
                                    : !strcmp(name, "declined") ? bbw::ShareState::DECLINED
                                    : bbw::ShareState::LOCAL;
      any |= bbw::applyGrinderReview(lib, entry["local_id"] | 0, entry["record"] | 0u, state,
                                     entry["note"] | "", entry["library_id"] | "");
    }
    return any;  // nothing new: leave the library unsaved
  }, nullptr);
  doc.clear();
  if (changed) {
    LOGI("share: the library's verdicts on this controller's coffees have changed");
    g_libraryReadSoon = true;  // a coffee just published has figures to fetch
  }
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
    if (bbw::buildShotRecord(shot, haveCoffee ? &g_shareCoffee : nullptr, grinder, s.shareRatings,
                             g_shareBody, bbw::SHARE_BODY_MAX) != 0) {
      snprintf(g_sharePath, sizeof(g_sharePath), "shots/%s/%s-%u.json", month, kid,
               static_cast<unsigned>(shot.id));
      if (!shareQueue(g_sharePath)) LOGW("share: shot %u not queued", static_cast<unsigned>(shot.id));
    }
  }

  if (!s.shareDialIns || !haveCoffee) return;
  const bbw::GrindAdvice advice = app::grindAdvice();
  const bbw::Recipe preset = app::activeRecipe();
  if (advice.action != bbw::GrindAction::HOLD || advice.shotsUsed < s.grindWindow ||
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
  if (!CAN_PUBLISH || !idle || g_store.outboxWaiting() == 0) return;
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
constexpr bool CAN_FETCH = true;
constexpr uint32_t LIBRARY_REFRESH_MS = 60000;
bool fetchLibraryFile(const char *path, char *out, size_t size) { return emuFetchFile(path, out, size); }
#else
constexpr bool CAN_FETCH = false;  // no repository yet
constexpr uint32_t LIBRARY_REFRESH_MS = 3600000;
bool fetchLibraryFile(const char *, char *, size_t) { return false; }
#endif

constexpr uint32_t LIBRARY_STEP_MS = 2000;
constexpr const char *FIT_CACHE = "/lfit.json";
constexpr const char *COFFEES_CACHE = "/lcoffees.json";
constexpr const char *GRINDERS_CACHE = "/lgrinders.json";
constexpr uint8_t LIBRARY_FIXED_STEPS = 3;  // fit, coffees, grinders; then a dial-in per coffee
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
  if (!CAN_FETCH || !idle) return;
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
      static bool scaleWasConnected = false;
      const bool scaleConnected = g_scale.connected();
      if (scaleConnected && !scaleWasConnected) g_botHoldUntilMs = now + BOT_PREWAKE_MS;
      scaleWasConnected = scaleConnected;
      bbw::WeightSample sample;
      while (g_scale.nextSample(sample)) g_controller.onSample(sample);
      g_controller.tick(now);
      // Bot battery: connected from Start until the shot is confirmed over,
      // asleep the rest of the time.
      if (g_controller.actuatorNeeded()) g_botHoldUntilMs = now + BOT_LINGER_MS;
      g_ble.setBotWanted(static_cast<int32_t>(now - g_botHoldUntilMs.load()) < 0);
      // The power button is only pressed while no shot is running or being set up.
      const bbw::State state = g_controller.state();
      const bool idle = !g_controller.machineMayBeRunning() &&
                        (state == State::READY || state == State::CONNECTING_SCALE);
      g_powerControl.tick(now, static_cast<uint32_t>(time(nullptr)), idle, g_power);
      if (g_powerControl.takeDirty()) g_pushersDirty = true;
      g_ble.setPowerWanted(g_powerControl.wantsLink() ||
                           static_cast<int32_t>(now - g_powerHoldUntilMs.load()) < 0);
      g_shotInProgress = g_controller.machineMayBeRunning() ? SHOT_IN_PROGRESS_MAGIC : 0;
    }
    esp_task_wdt_reset();
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
  }
}

void bleTask(void *) { g_ble.run(); }

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
    } else {
      Serial.println("commands: status start stop tare ack csv json");
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

void start() { Lock lock; g_controller.requestStart(); }
void stop() { Lock lock; g_controller.requestStop(); }
void tare() { Lock lock; g_controller.requestTare(); }
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

void selectRecipe(uint8_t index) {
  Lock lock;
  if (!editable()) return;
  if (index < g_library.presetCount) {
    g_library.activePreset = index;
    markLibraryDirty();
  }
}

void nextRecipe() {
  uint8_t next;
  {
    Lock lock;
    next = (g_library.activePreset + 1) % g_library.presetCount;
  }
  selectRecipe(next);
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

bbw::GrindAdvice grindAdvice() {
  if (!g_grindAdviceStale) return g_grindAdvice;
  constexpr uint32_t SCAN_SHOTS = 150;  // how far back to look
  struct Scan {
    bbw::Recipe recipe;
    bbw::GrindTimeBasis basis = bbw::GrindTimeBasis::TOTAL;
    uint16_t calibration = 0;
    bbw::GrindShot shots[40];
    size_t count = 0;
  };
  static Scan scan;
  const bbw::Settings s = settings();
  scan.recipe = activeRecipe();
  scan.basis = s.grindTimeBasis;
  scan.calibration = grinder().calibration;
  scan.count = 0;
  g_store.scanShots(SCAN_SHOTS, [](const bbw::ShotRecord &record, void *context) {
    Scan &sc = *static_cast<Scan *>(context);
    // Same recipe, same coffee (and bag: roast date), same yield and dose:
    // otherwise brew times do not compare. A new bag starts a clean history.
    // By id, so renaming either does not.
    // And the grinder as it is now calibrated: a setting from before it was
    // zeroed again is another setting.
    if (record.recipeId == sc.recipe.recipeId && record.coffeeId == sc.recipe.coffeeId &&
        record.grinderId == sc.calibration &&
        record.roastDate == sc.recipe.roastDate &&
        fabsf(record.targetWeightG - sc.recipe.targetWeightG) <= 0.05f &&
        fabsf(record.doseG - sc.recipe.doseG) <= 0.05f &&
        bbw::grindShotValid(record, sc.basis)) {
      sc.shots[sc.count].timeS = bbw::grindShotTime(record, sc.basis);
      sc.shots[sc.count].grindUm = record.grindUm;
      sc.shots[sc.count].forced =
          record.grindVote == static_cast<uint8_t>(bbw::GrindVote::COUNT);
      ++sc.count;
    }
    return sc.count < sizeof(sc.shots) / sizeof(sc.shots[0]);
  }, &scan);
  const bbw::Recipe &recipe = scan.recipe;
  const bbw::GrindShot *shots = scan.shots;
  const size_t count = scan.count;
  bbw::GrindConfig config;
  config.toleranceS = s.grindToleranceS;
  config.defaultUmPerS = s.grindUmPerS;
  config.window = s.grindWindow;
  config.maxStepUm = s.grindMaxStepUm;
  g_grindAdvice = bbw::adviseGrind(shots, count, recipe.grindUm, recipe.targetTimeS, config);
  g_grindAdviceStale = false;
  return g_grindAdvice;
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

bool setShotRating(uint32_t shotId, uint8_t rating, uint8_t taste) {
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
  wantCoffee = coffeeId;
  wantRecipe = recipeId;
  have = false;
  readLibrary([](const bbw::Library &lib, void *) {
    const bbw::RecipeDef *d = bbw::findRecipe(lib, wantRecipe);
    const bbw::Coffee *c = bbw::findCoffee(lib, wantCoffee);
    grinder = lib.grinder;
    if (d && c && c->libraryId[0]) {
      def = *d;
      have = true;
    }
  }, nullptr);
  if (!have) return view;
  static JsonDocument doc;
  char cache[20];
  dialinCache(coffeeId, cache, sizeof(cache));
  if (g_store.readText(cache, g_libraryText, LIBRARY_TEXT_SIZE) != 0 &&
      !deserializeJson(doc, g_libraryText) && doc["v"] == 1) {
    for (JsonObjectConst r : doc["recipes"].as<JsonArrayConst>()) {
      if (!bbw::sameRecipeClass(def.doseG, def.targetWeightG, r["dose_g"] | 0.0f,
                                r["yield_g"] | 0.0f)) {
        continue;
      }
      view.dialin.found = true;
      view.dialin.contributors = r["contributors"] | 0;
      view.dialin.dialins = r["dialins"] | 0;
      view.dialin.timeS = r["time_s"] | 0.0f;
      view.dialin.finenessUm = r["fineness_um"] | 0.0f;
      view.dialin.spreadUm = r["spread_um"] | 0.0f;
      for (JsonObjectConst m : r["models"].as<JsonArrayConst>()) {
        // The same model: by the firmware's own number for it, or by its
        // entry in the library's list.
        const bool byNumber = grinder.modelId != 0 && (m["model_id"] | 0) == grinder.modelId;
        const bool byEntry = grinder.libraryId[0] != '\0' &&
                             strcmp(m["library_id"] | "", grinder.libraryId) == 0;
        if ((byNumber || byEntry) && strcmp(m["unit"] | "", grinder.unit) == 0) {
          view.dialin.modelMedian = m["median"] | 0.0f;
          view.dialin.modelCount = m["n"] | 0;
        }
      }
      break;
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
      break;
    }
  }
  doc.clear();
  view.start = bbw::communityStart(view.dialin, view.fit, grinder);
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

// Good until the next call that reads the library's cache.
const char *communityCoffees() {
  g_store.readText(COFFEES_CACHE, g_libraryText, LIBRARY_TEXT_SIZE);
  return g_libraryText;
}

const char *communityGrinders() {
  g_store.readText(GRINDERS_CACHE, g_libraryText, LIBRARY_TEXT_SIZE);
  return g_libraryText;
}

bool libraryReachable() { return CAN_FETCH; }
void readLibrarySoon() { g_libraryReadSoon = true; }

ShareStatus shareStatus() {
  ShareStatus st;
  bbw::ISigner &signer = bbw::deviceSigner();
  st.keyReady = signer.ready();
  if (st.keyReady) {
    bbw::keyId(signer.publicKey(), st.keyId);
    strlcpy(st.algorithm, signer.algorithm(), sizeof(st.algorithm));
  }
  st.canSend = CAN_PUBLISH;
  st.sendsTo = PUBLISHES_TO;
  st.waiting = g_store.outboxWaiting();
  st.sent = g_store.outboxSent();
  return st;
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

void pairPower(const char *address, uint8_t type) {
  {
    Lock lock;
    strlcpy(g_pushers.powerAddress, address ? address : "", sizeof(g_pushers.powerAddress));
    g_pushers.powerAddressType = type;
  }
  g_pushersDirty = true;
  g_ble.setPower(address, type);
  // Connect once now so the owner sees it work and the battery level is read.
  if (address && address[0]) g_powerHoldUntilMs = millis() + BOT_PAIR_CHECK_MS;
}

bbw::PusherSecrets pusherSecrets(bool power) {
  Lock lock;
  return power ? g_pushers.power : g_pushers.brew;
}

void applyPusherSecrets(bool power, const bbw::PusherSecrets &secrets) {
  {
    Lock lock;
    bbw::PusherSecrets &stored = power ? g_pushers.power : g_pushers.brew;
    stored = secrets;
    bbw::sanitizePusherConfig(g_pushers);
    bbw::PusherSecrets use = stored;
    // The brew Bot's SwitchBot password stays where it always was, in the network record.
    if (!power) strlcpy(use.password, g_net.botPassword, sizeof(use.password));
    (power ? g_power : g_bot).setSecrets(use);
  }
  g_pushersDirty = true;
  // Try the new key at once.
  (power ? g_powerHoldUntilMs : g_botHoldUntilMs) = millis() + BOT_PAIR_CHECK_MS;
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

bool powerRequest(bool on) {
  Lock lock;
  return g_powerControl.request(on, millis());
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

}  // namespace app

// ============================================================ setup / loop ==

void setup() {
  Serial.begin(115200);
  // USB CDC with no host listening must never stall a task that logs.
  Serial.setTxTimeoutMs(0);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  const bool resumedMidShot = esp_reset_reason() != ESP_RST_POWERON &&
                              g_shotInProgress == SHOT_IN_PROGRESS_MAGIC;
  g_shotInProgress = 0;

  g_lock = xSemaphoreCreateMutex();
  allocateBuffers();  // before anything that uses them; PSRAM is up by now
  g_store.begin();
  g_settings = g_store.loadSettings();
  g_store.loadLibrary(g_library);
  bbw::beginDeviceKey();
  g_net = g_store.loadNet();
  const bbw::Pairing pairing = g_store.loadPairing();

  ui::begin();

  g_controller.setNextShotId(g_store.nextShotId() ? g_store.nextShotId() : 1);
  g_controller.epochNow = app::epochNow;
  g_scale.begin();
  g_pushers = g_store.loadPushers();
  g_powerControl.begin(&g_pushers);
  bbw::PusherSecrets brewSecrets = g_pushers.brew;
  strlcpy(brewSecrets.password, g_net.botPassword, sizeof(brewSecrets.password));
  g_bot.begin("bot", &g_settings, brewSecrets);
  g_power.begin("power", &g_settings, g_pushers.power);
  if (!g_ble.begin(pairing, g_pushers.powerAddress, g_pushers.powerAddressType)) {
    LOGW("ble: init failed");
    g_controller.raiseBleError();
  } else {
    xTaskCreatePinnedToCore(bleTask, "ble", 8192, nullptr, 2, nullptr, 0);
  }
  if (resumedMidShot) {
    LOGW("restarted while a shot was running: will stop it if the scale still shows flow");
    g_controller.armRestartRecovery(millis());
  }

  esp_task_wdt_init(5, true);  // a stuck control task resets the controller
  xTaskCreatePinnedToCore(controlTask, "control", 8192, nullptr, 5, nullptr, 1);

  web::begin();
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

  // Persistence happens here, never in the control task.
  bbw::ShotRecord shot;
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
    if (!g_store.appendShot(shot)) LOGW("store: shot %u not saved", static_cast<unsigned>(shot.id));
    if (g_store.saveCurve(shot.id, g_curveOut, curvePoints)) {
      LOGI("store: shot %u data, %u readings", static_cast<unsigned>(shot.id),
           static_cast<unsigned>(curvePoints));
    } else {
      LOGW("store: shot %u data not saved", static_cast<unsigned>(shot.id));
    }
    g_grindAdviceStale = true;
    shareAfterShot(shot);
  }
  shareSendOne(idle);
  shareReadStatus(idle);
  libraryRefresh(idle);
  if (saveSettings) g_store.saveSettings(snapshot);
  if (saveLibrary && !g_store.saveLibrary((*g_librarySaving))) LOGW("store: library not saved");
  if (g_ble.takePairingDirty()) g_store.savePairing(g_ble.pairing());
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
