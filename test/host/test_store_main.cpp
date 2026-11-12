// Host tests for the flash storage: storage/Store.cpp and storage/Backup.cpp, run on the emulator's
// Arduino shims (emu/shims) with an in-memory NVS and a LittleFS that is a directory under
// .pio/host/storefs. Built and run by run.sh as its own program (bbw_store_tests): the shims and the
// portable core tests cannot share one translation unit.
//
// What is pinned here: the NVS layout (a build must keep reading what an earlier one wrote, and what a
// later one wrote), a rolled-back board keeping its settings, a set-aside shot history coming back,
// the contexts file following the history, and "no secrets" backups holding no secret.
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>

#include <stdarg.h>
#include <stddef.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/BackupFormat.h"
#include "core/PowerControl.h"
#include "core/ShotContext.h"
#include "platform/FsUtil.h"
#include "storage/Backup.h"
#include "storage/Store.h"

namespace fs = std::filesystem;
using namespace bbw;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    ++g_checks;                                                            \
    if (!(cond)) {                                                         \
      ++g_failures;                                                        \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
    }                                                                      \
  } while (0)

// ---------------------------------------------------------------- the shims --

static std::map<std::string, std::vector<uint8_t>> g_nvs;
static std::string g_fsRoot = ".pio/host/storefs";
static std::string g_lastWarning;

extern "C" uint32_t millis(void) { return 0; }
extern "C" void delay(uint32_t) {}
extern "C" size_t strlcpy(char *dst, const char *src, size_t size) {
  const size_t n = strlen(src);
  if (size) {
    const size_t c = n < size - 1 ? n : size - 1;
    memcpy(dst, src, c);
    dst[c] = '\0';
  }
  return n;
}
void esp_task_wdt_reset() {}

namespace bbw {
void logLine(char level, const char *format, ...) {
  char text[200];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (level == 'W' || level == 'E') g_lastWarning = text;
}
}  // namespace bbw

// The NVS refuses a buffer shorter than the record (Arduino-ESP32 Preferences::getBytes), and so does this.
bool Preferences::begin(const char *name, bool) {
  dir_ = name;
  return true;
}
bool Preferences::isKey(const char *key) { return g_nvs.count(key) != 0; }
size_t Preferences::getBytesLength(const char *key) { return g_nvs.count(key) ? g_nvs[key].size() : 0; }
size_t Preferences::getBytes(const char *key, void *buffer, size_t maxLength) {
  if (!g_nvs.count(key) || g_nvs[key].size() > maxLength) return 0;
  memcpy(buffer, g_nvs[key].data(), g_nvs[key].size());
  return g_nvs[key].size();
}
size_t Preferences::putBytes(const char *key, const void *value, size_t length) {
  g_nvs[key].assign(static_cast<const uint8_t *>(value), static_cast<const uint8_t *>(value) + length);
  return length;
}

LittleFSClass LittleFS;
bool LittleFSClass::begin(bool, const char *, uint8_t, const char *) {
  std::error_code e;
  fs::create_directories(g_fsRoot, e);
  return !e;
}
File LittleFSClass::open(const char *path, const char *mode) {
  std::string m = mode;
  m += "b";
  return File(fopen((g_fsRoot + path).c_str(), m.c_str()));
}
bool LittleFSClass::exists(const char *path) {
  std::error_code e;
  return fs::exists(g_fsRoot + path, e);
}
bool LittleFSClass::remove(const char *path) {
  std::error_code e;
  return fs::remove(g_fsRoot + path, e);
}
bool LittleFSClass::mkdir(const char *path) {   // restore of add-on module files (Backup.cpp)
  std::error_code e;
  fs::create_directories(g_fsRoot + path, e);
  return !e;
}
bool LittleFSClass::rename(const char *from, const char *to) {
  std::error_code e;
  fs::rename(g_fsRoot + from, g_fsRoot + to, e);
  return !e;
}
size_t LittleFSClass::usedBytes() {
  size_t n = 0;
  std::error_code e;
  for (const auto &entry : fs::directory_iterator(g_fsRoot, e)) {
    if (entry.is_regular_file(e)) n += static_cast<size_t>(entry.file_size(e));
  }
  return n;
}
void LittleFSClass::list(void (*visit)(const char *, size_t, void *), void *context) {
  std::error_code e;
  std::vector<std::pair<std::string, size_t>> names;
  for (const auto &entry : fs::directory_iterator(g_fsRoot, e)) {
    if (entry.is_regular_file(e)) names.push_back({"/" + entry.path().filename().string(), static_cast<size_t>(entry.file_size(e))});
  }
  for (const auto &n : names) visit(n.first.c_str(), n.second, context);
}

// --------------------------------------------------------------- test tools --

static void freshWorld() {
  g_nvs.clear();
  std::error_code e;
  fs::remove_all(g_fsRoot, e);
  fs::create_directories(g_fsRoot, e);
  g_lastWarning.clear();
}

static bool haveFile(const char *name) { return fs::exists(g_fsRoot + name); }

static std::vector<uint8_t> readFile(const char *name) {
  std::vector<uint8_t> out;
  FILE *f = fopen((g_fsRoot + name).c_str(), "rb");
  if (!f) return out;
  uint8_t buffer[4096];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) out.insert(out.end(), buffer, buffer + n);
  fclose(f);
  return out;
}

static void writeFile(const char *name, const void *data, size_t length) {
  FILE *f = fopen((g_fsRoot + name).c_str(), "wb");
  if (f) {
    fwrite(data, 1, length, f);
    fclose(f);
  }
}

static std::vector<uint8_t> nvsGet(const char *key) { return g_nvs.count(key) ? g_nvs[key] : std::vector<uint8_t>(); }

static ShotRecord shotWithId(uint32_t id) {
  ShotRecord r;
  r.id = id;
  r.epochS = 1790000000u + id;
  r.targetWeightG = 36.0f;
  return r;
}

// What a context file made by this build looks like on flash.
struct CtxHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t recordSize;
  uint32_t capacity;
};
constexpr uint32_t CTX_MAGIC_BY_ID = 0x58574242;  // "BBWX": the layout until 10 Nov 2026
constexpr uint32_t CTX_MAGIC_BY_SLOT = 0x59574242;  // "BBWY"

static ShotContext contextFor(uint32_t id) {
  ShotContext c;
  c.shotId = id;
  c.doseActualDg = static_cast<uint16_t>(100 + id % 90);
  return c;
}

// ------------------------------------------------------------------- tests --

// The shot list's one pass, the curve cache and the history's revision (11 Nov 2026).
static void testShotListPass() {
  printf("shot list: one pass with contexts, curves cached, revision on every change\n");
  freshWorld();
  Store store;
  CHECK(store.begin());
  CurvePoint points[4] = {};
  uint32_t rev = store.historyRevision();
  for (uint32_t id = 1; id <= 3; ++id) {
    CHECK(store.appendShot(shotWithId(id)));
    CHECK(store.historyRevision() > rev);
    rev = store.historyRevision();
  }
  CHECK(store.saveCurve(1, points, 4));
  CHECK(store.historyRevision() > rev);
  rev = store.historyRevision();
  CHECK(store.saveCurve(3, points, 4));
  CHECK(store.saveContext(contextFor(2)));
  CHECK(store.saveContext(contextFor(3)));
  CHECK(store.historyRevision() > rev);
  rev = store.historyRevision();
  CHECK(store.setShotRating(2, 4, 1));
  CHECK(store.historyRevision() > rev);
  rev = store.historyRevision();
  CHECK(!store.setShotRating(77, 4, 1));          // no such shot: nothing changed
  CHECK(store.historyRevision() == rev);
  CHECK(store.hasCurve(1) && !store.hasCurve(2) && store.hasCurve(3));
  CHECK(!store.hasCurve(2));                       // asked twice: the cached answer is the same
  CHECK(store.historyRevision() == rev);           // asking changes nothing

  struct Seen {
    uint32_t ids[8] = {};
    bool context[8] = {};
    bool data[8] = {};
    uint16_t dose[8] = {};
    uint8_t rating[8] = {};
    size_t n = 0;
  } seen;
  store.scanShotsWithContext(500, [](const ShotRecord &r, const ShotContext *c, bool hasData, void *x) {
    Seen &s = *static_cast<Seen *>(x);
    s.ids[s.n] = r.id;
    s.context[s.n] = c != nullptr;
    s.dose[s.n] = c ? c->doseActualDg : 0;
    s.data[s.n] = hasData;
    s.rating[s.n] = r.rating;
    ++s.n;
    return true;
  }, &seen);
  CHECK(seen.n == 3);
  CHECK(seen.ids[0] == 3 && seen.ids[1] == 2 && seen.ids[2] == 1);          // newest first
  CHECK(seen.context[0] && seen.context[1] && !seen.context[2]);
  CHECK(seen.dose[1] == contextFor(2).doseActualDg);
  CHECK(seen.data[0] && !seen.data[1] && seen.data[2]);
  CHECK(seen.rating[1] == 4);
  // Stopping early, and a limit.
  seen = Seen();
  store.scanShotsWithContext(2, [](const ShotRecord &r, const ShotContext *, bool, void *x) {
    static_cast<Seen *>(x)->ids[static_cast<Seen *>(x)->n++] = r.id;
    return true;
  }, &seen);
  CHECK(seen.n == 2 && seen.ids[1] == 2);

  // A restart: no cache yet, the same answers from flash.
  {
    Store again;
    CHECK(again.begin());
    CHECK(again.hasCurve(1) && !again.hasCurve(2) && again.hasCurve(3));
  }

  // The ring goes round: the curve of the shot that falls out goes, the ones in the history stay.
  for (uint32_t id = 4; id <= 520; ++id) {
    CHECK(store.appendShot(shotWithId(id)));
    CHECK(store.saveCurve(id, points, 4));
  }
  CHECK(store.shotCount() == 500);
  CHECK(!store.hasCurve(1) && !store.hasCurve(3) && !store.hasCurve(20));   // fell out of the history
  CHECK(!haveFile("/c20.bin") && haveFile("/c21.bin"));
  CHECK(store.hasCurve(21) && store.hasCurve(520) && !store.hasCurve(521));
  {
    Store again;
    CHECK(again.begin());
    CHECK(!again.hasCurve(20) && again.hasCurve(21) && again.hasCurve(520));
  }
  // Clearing the history: no curves, a new revision.
  rev = store.historyRevision();
  store.clearShots();
  CHECK(store.historyRevision() > rev);
  CHECK(store.shotCount() == 0 && !store.hasCurve(520) && !store.hasCurve(21));
  CHECK(!haveFile("/c520.bin"));
}

// Layout of everything the firmware keeps on flash in binary form. A later build may add fields at the
// END of a record and nothing else; these numbers say what the builds so far wrote, so a change to any
// of them is a decision and not an accident. (Every member is 1, 2 or 4 bytes wide, so the host and the
// board lay the structs out alike.)
static void testNvsLayoutGolden() {
  printf("NVS layout: sizes and offsets of the stored structs\n");
  CHECK(sizeof(Settings) == 76);
  CHECK(offsetof(Settings, learningEnabled) == 0 && offsetof(Settings, learningFactor) == 4);
  CHECK(offsetof(Settings, scaleTimeoutMs) == 16 && offsetof(Settings, grindToleranceS) == 44);
  CHECK(offsetof(Settings, requireCup) == 55 && offsetof(Settings, shareEnabled) == 56);
  CHECK(offsetof(Settings, retiredLearnGrinder) == 62 && offsetof(Settings, keepPowerBotConnected) == 64);
  CHECK(offsetof(Settings, scaleRestMin) == 66 && offsetof(Settings, uiTheme) == 67);
  CHECK(offsetof(Settings, diagnosticsOff) == 70 && offsetof(Settings, webPanelsChosen) == 71);
  CHECK(offsetof(Settings, webPanels) == 72 && offsetof(Settings, grindConfidenceOff) == 74);
  CHECK(offsetof(Settings, grindRoomMark) == 75);

  CHECK(sizeof(NetConfig) == 1244);
  CHECK(offsetof(NetConfig, apPassword) == 98 && offsetof(NetConfig, webStartEnabled) == 131);
  CHECK(offsetof(NetConfig, botPassword) == 132 && offsetof(NetConfig, libraryRepo) == 156);
  CHECK(offsetof(NetConfig, libraryToken) == 220 && offsetof(NetConfig, diagRelay) == 348);
  CHECK(offsetof(NetConfig, haUrl) == 444 && offsetof(NetConfig, haToken) == 540);
  CHECK(offsetof(NetConfig, haRhEntity) == 796 && offsetof(NetConfig, haTempEntity) == 860);
  CHECK(offsetof(NetConfig, haWebhookId) == 924 && offsetof(NetConfig, mqttUrl) == 988);
  CHECK(offsetof(NetConfig, mqttUser) == 1084 && offsetof(NetConfig, mqttPassword) == 1132);
  CHECK(offsetof(NetConfig, tzRule) == 1196);

  CHECK(sizeof(UpdateConfig) == 193 && offsetof(UpdateConfig, token) == 64 &&
        offsetof(UpdateConfig, autoCheck) == 192);
  CHECK(sizeof(PusherSecrets) == 99 && offsetof(PusherSecrets, tuyaUuid) == 24 &&
        offsetof(PusherSecrets, tuyaLocalKey) == 64 && offsetof(PusherSecrets, tuyaVariant) == 81 &&
        offsetof(PusherSecrets, microbotToken) == 82 && offsetof(PusherSecrets, microbotTokenSet) == 98);
  CHECK(sizeof(PusherConfig) == 258 && offsetof(PusherConfig, brew) == 19 && offsetof(PusherConfig, power) == 118 &&
        offsetof(PusherConfig, schedule) == 218 && offsetof(PusherConfig, utcOffsetMin) == 254 &&
        offsetof(PusherConfig, belief) == 256);
  CHECK(sizeof(StopBotConfig) == 118 && offsetof(StopBotConfig, secrets) == 19);
  CHECK(sizeof(Pairing) == 38 && sizeof(CupMemory) == 12 && sizeof(Maintenance) == 36);
  CHECK(sizeof(MachineProfile) == 200 && sizeof(Drinkers) == 136);
  // Fixed-size records in files.
  CHECK(sizeof(ShotRecord) == 232 && offsetof(ShotRecord, id) == 0);
  CHECK(sizeof(ShotContext) == 76 && offsetof(ShotContext, shotId) == 0 && offsetof(ShotContext, ambientRhX10) == 64 &&
        offsetof(ShotContext, roomEffX10) == 72);
  CHECK(sizeof(CoffeeRating) == 32 && sizeof(PowerEvent) == 8);
}

// The board was rolled back to an older build. What the newer one stored is longer than the struct the
// older one knows; it still reads the front (the fields only ever grow at the end), and writing the
// front back leaves the tail for the next update.
static void testLongBlobRollback() {
  printf("rollback: a record longer than this build's struct is read, and its tail kept\n");
  freshWorld();
  Store store;
  CHECK(store.begin());
  Settings s = defaultSettings();
  s.dripObserveMs = 4200;
  s.grindToleranceS = 3.5f;
  s.shareEnabled = true;
  store.saveSettings(s);
  NetConfig net;
  strncpy(net.staSsid, "kitchen", sizeof(net.staSsid) - 1);
  strncpy(net.haUrl, "http://ha.local:8123", sizeof(net.haUrl) - 1);
  store.saveNet(net);
  const size_t cfgLength = nvsGet("cfg").size();
  const size_t netLength = nvsGet("net").size();
  CHECK(cfgLength >= sizeof(Settings) && netLength >= sizeof(NetConfig));

  // The newer build's records: the same front, 24 and 40 bytes more at the end.
  for (const auto &entry : {std::make_pair("cfg", 24), std::make_pair("net", 40)}) {
    std::vector<uint8_t> blob = nvsGet(entry.first);
    for (int i = 0; i < entry.second; ++i) blob.push_back(static_cast<uint8_t>(0xA0 + i));
    g_nvs[entry.first] = blob;
  }
  {  // The shim refuses a short buffer, as the board's NVS does: reading the front alone would fail.
    Preferences prefs;
    prefs.begin("bbw", true);
    uint8_t one[8];
    CHECK(prefs.getBytes("cfg", one, sizeof(one)) == 0);
  }

  Store older;
  CHECK(older.begin());
  g_lastWarning.clear();
  const Settings read = older.loadSettings();
  CHECK(read.dripObserveMs == 4200 && read.grindToleranceS == 3.5f && read.shareEnabled);  // not the defaults
  CHECK(g_lastWarning.find("longer") != std::string::npos);
  const NetConfig readNet = older.loadNet();
  CHECK(strcmp(readNet.staSsid, "kitchen") == 0 && strcmp(readNet.haUrl, "http://ha.local:8123") == 0);

  // An edit on the older build changes the front and leaves the tail.
  Settings edited = read;
  edited.dripObserveMs = 3600;
  older.saveSettings(edited);
  const std::vector<uint8_t> after = nvsGet("cfg");
  CHECK(after.size() == cfgLength + 24);
  bool tail = true;
  for (int i = 0; i < 24; ++i) tail &= after[cfgLength + i] == static_cast<uint8_t>(0xA0 + i);
  CHECK(tail);
  CHECK(older.loadSettings().dripObserveMs == 3600);
  older.saveNet(readNet);
  CHECK(nvsGet("net").size() == netLength + 40);

  // A record of another version is not read, long or not.
  std::vector<uint8_t> other = nvsGet("cfg");
  other[0] ^= 0x7f;
  g_nvs["cfg"] = other;
  CHECK(older.loadSettings().dripObserveMs == defaultSettings().dripObserveMs);
}

// A history an older build could not read is set aside as /shots.unread. It comes back when a build that
// reads it starts and the history in use is empty; shots made since are never replaced.
static void testUnreadHistoryComesBack() {
  printf("history: /shots.unread comes back into an empty history, never over newer shots\n");
  freshWorld();
  {
    Store store;
    CHECK(store.begin());
    for (uint32_t id = 1; id <= 40; ++id) CHECK(store.appendShot(shotWithId(id)));
    CHECK(store.shotCount() == 40 && store.nextShotId() == 41);
  }
  const std::vector<uint8_t> original = readFile("/shots.bin");
  CHECK(!original.empty());
  // What an older build does to it: sets it aside (and makes an empty history of its own).
  fs::rename(g_fsRoot + "/shots.bin", g_fsRoot + "/shots.unread");
  {
    Store store;
    CHECK(store.begin());   // no history in use: it comes back
    CHECK(store.shotCount() == 40 && store.nextShotId() == 41);
    ShotRecord r;
    CHECK(store.readShot(0, r) && r.id == 40);
    CHECK(!haveFile("/shots.unread"));
  }
  // An empty history in use (the one the older build started): replaced by the set-aside one.
  {
    freshWorld();
    Store older;
    CHECK(older.begin());    // empty history file made
    CHECK(older.shotCount() == 0 && haveFile("/shots.bin"));
    writeFile("/shots.unread", original.data(), original.size());
    Store store;
    CHECK(store.begin());
    CHECK(store.shotCount() == 40 && !haveFile("/shots.unread"));
  }
  // Shots made since: the set-aside history stays where it is, and nothing is lost.
  {
    freshWorld();
    {
      Store store;
      CHECK(store.begin());
      CHECK(store.appendShot(shotWithId(1)));
    }
    writeFile("/shots.unread", original.data(), original.size());
    Store store;
    CHECK(store.begin());
    CHECK(store.shotCount() == 1 && haveFile("/shots.unread"));
  }
  // A version this build does not read stays set aside.
  {
    freshWorld();
    std::vector<uint8_t> newer = original;
    newer[4] = 11;   // the version field
    writeFile("/shots.unread", newer.data(), newer.size());
    Store store;
    CHECK(store.begin());
    CHECK(store.shotCount() == 0 && haveFile("/shots.unread"));
  }
}

static void testContextsFollowTheHistory() {
  printf("contexts: a shot's context sits in its history slot, ids that skip do not collide\n");
  freshWorld();
  Store store;
  CHECK(store.begin());
  // Ids 1..499 saved, 500 failed to save (the controller still used the id), then 501..: with the
  // slot id % 500, shots 3 and 503 would share a slot.
  for (uint32_t id = 1; id <= 499; ++id) {
    CHECK(store.appendShot(shotWithId(id)));
    CHECK(store.saveContext(contextFor(id)));
  }
  CHECK(!store.saveContext(contextFor(500)));            // not in the history: no context, no stray slot
  for (uint32_t id = 501; id <= 503; ++id) {
    CHECK(store.appendShot(shotWithId(id)));
    CHECK(store.saveContext(contextFor(id)));
  }
  CHECK(store.shotCount() == 500);                        // 502 saved, the 2 oldest (ids 1, 2) fell out
  ShotContext c;
  CHECK(!store.loadContext(1, c) && !store.loadContext(2, c));   // left the history
  CHECK(store.loadContext(3, c) && c.shotId == 3 && c.doseActualDg == contextFor(3).doseActualDg);
  CHECK(store.loadContext(499, c) && c.doseActualDg == contextFor(499).doseActualDg);
  CHECK(store.loadContext(501, c) && c.doseActualDg == contextFor(501).doseActualDg);
  CHECK(store.loadContext(503, c) && c.shotId == 503);
  CHECK(!store.loadContext(500, c));

  // One pass for the grind advice finds the same.
  ShotContext withRoom = contextFor(10);
  withRoom.roomEffX10 = 215;
  CHECK(store.saveContext(withRoom));
  withRoom = contextFor(503);
  withRoom.roomEffX10 = 190;
  CHECK(store.saveContext(withRoom));
  const uint32_t ids[4] = {10, 503, 500, 1};
  float room[4];
  store.loadRooms(ids, 4, room);
  CHECK(fabsf(room[0] - 21.5f) < 0.01f && fabsf(room[1] - 19.0f) < 0.01f && std::isnan(room[2]) && std::isnan(room[3]));
  // The owner's entries for an older shot land in that shot's slot.
  CHECK(store.setShotPrep(10, 180, true, false));
  CHECK(store.loadContext(10, c) && c.shotId == 10 && c.roomEffX10 == 215);
  CHECK(!store.setShotPrep(2, 180, true, false));         // left the history

  // A restart reads the same.
  Store again;
  CHECK(again.begin());
  CHECK(again.loadContext(503, c) && c.roomEffX10 == 190 && again.loadContext(3, c));
}

// The contexts file as builds until 10 Nov 2026 made it (slot id % 500) becomes the layout by history
// slot at start-up; every context whose shot is still in the history comes with it.
static void testContextsConversion() {
  printf("contexts: the old layout by id is converted, version 2 files too\n");
  for (int variant = 0; variant < 2; ++variant) {
    freshWorld();
    {
      Store store;
      CHECK(store.begin());
      for (uint32_t id = 1; id <= 700; ++id) CHECK(store.appendShot(shotWithId(id)));   // 201..700 stay
      CHECK(store.shotCount() == 500 && store.nextShotId() == 701);
    }
    // The old file: header, then slot id % 500. It also holds a shot that has left (id 5).
    const size_t recordSize = variant == 0 ? sizeof(ShotContext) : SHOT_CONTEXT_V2_SIZE;
    const CtxHeader header = {CTX_MAGIC_BY_ID,
                              static_cast<uint16_t>(variant == 0 ? SHOT_CONTEXT_VERSION : SHOT_CONTEXT_VERSION_V2),
                              static_cast<uint16_t>(recordSize), 500};
    std::vector<uint8_t> file(sizeof(header) + 500 * recordSize, 0);
    memcpy(file.data(), &header, sizeof(header));
    auto put = [&](uint32_t id) {
      const ShotContext c = contextFor(id);
      memcpy(file.data() + sizeof(header) + (id % 500) * recordSize, &c, recordSize);
    };
    for (uint32_t id = 201; id <= 700; id += 3) put(id);     // live
    put(5);                                                  // left the history
    writeFile("/contexts.bin", file.data(), file.size());

    Store store;
    CHECK(store.begin());
    const std::vector<uint8_t> converted = readFile("/contexts.bin");
    CtxHeader now = {};
    memcpy(&now, converted.data(), sizeof(now));
    CHECK(now.magic == CTX_MAGIC_BY_SLOT && now.version == SHOT_CONTEXT_VERSION && now.recordSize == sizeof(ShotContext));
    CHECK(converted.size() == sizeof(now) + 500 * sizeof(ShotContext));
    int found = 0, wrong = 0;
    for (uint32_t id = 201; id <= 700; ++id) {
      ShotContext c;
      const bool have = store.loadContext(id, c);
      const bool expect = (id - 201) % 3 == 0;
      if (have) ++found;
      if (have != expect || (have && c.doseActualDg != contextFor(id).doseActualDg)) ++wrong;
    }
    CHECK(found == 167 && wrong == 0);
    ShotContext c;
    CHECK(!store.loadContext(5, c));
    CHECK(!haveFile("/contexts.tmp") && !haveFile("/contexts.unread"));
    // And a new context is written to the new layout.
    CHECK(store.saveContext(contextFor(700)) && store.loadContext(700, c));
  }
}

static void testContextsSetAside() {
  printf("contexts: an unrecognised file is set aside, never deleted\n");
  freshWorld();
  Store store;
  CHECK(store.begin());
  CHECK(store.appendShot(shotWithId(1)));
  const uint8_t junk1[100] = {1, 2, 3, 4, 5};
  writeFile("/contexts.bin", junk1, sizeof(junk1));
  CHECK(store.saveContext(contextFor(1)));            // prepares the file: the junk is set aside first
  CHECK(haveFile("/contexts.unread") && readFile("/contexts.unread").size() == sizeof(junk1));
  ShotContext c;
  CHECK(store.loadContext(1, c) && c.shotId == 1);
  // Another one: the first stays.
  uint8_t junk2[50] = {9, 9};
  writeFile("/contexts.bin", junk2, sizeof(junk2));
  Store again;
  CHECK(again.begin());                               // start-up prepares an existing file too
  CHECK(readFile("/contexts.unread").size() == sizeof(junk1));
  CHECK(haveFile("/contexts.unread1") && readFile("/contexts.unread1").size() == sizeof(junk2));
  // A file of the right size with a magic nobody knows is set aside as well.
  std::vector<uint8_t> big(sizeof(CtxHeader) + 500 * sizeof(ShotContext), 0x5A);
  writeFile("/contexts.bin", big.data(), big.size());
  Store third;
  CHECK(third.begin());
  CHECK(haveFile("/contexts.unread2") && readFile("/contexts.unread2").size() == big.size());
}

static void testHistoryResetDropsStaleData() {
  printf("history reset: no trusted next id clears the contexts and curves, a trusted one keeps them\n");
  auto fill = [&]() {
    Store store;
    CHECK(store.begin());
    for (uint32_t id = 1; id <= 8; ++id) {
      CHECK(store.appendShot(shotWithId(id)));
      CHECK(store.saveContext(contextFor(id)));
      const CurvePoint points[2] = {};
      CHECK(store.saveCurve(id, points, 2));
    }
    CHECK(haveFile("/c3.bin") && haveFile("/contexts.bin"));
  };
  // A header that is not ours at all: the ids start again at 1.
  freshWorld();
  fill();
  std::vector<uint8_t> history = readFile("/shots.bin");
  history[0] ^= 0xff;   // the magic
  writeFile("/shots.bin", history.data(), history.size());
  {
    Store store;
    CHECK(store.begin());
    CHECK(store.shotCount() == 0 && store.nextShotId() == 1);
    CHECK(haveFile("/shots.unread"));                // the history itself is kept
    CHECK(!haveFile("/contexts.bin") && !haveFile("/c3.bin") && !haveFile("/c8.bin"));
    CHECK(store.appendShot(shotWithId(1)));
    ShotContext c;
    CHECK(!store.loadContext(1, c));                 // no stale context for the new shot 1
    CHECK(!store.hasCurve(1));
  }
  // A header of ours that this build cannot read (a newer version): its next id is believed, and what
  // belongs to those ids stays.
  freshWorld();
  fill();
  history = readFile("/shots.bin");
  history[4] = 11;
  writeFile("/shots.bin", history.data(), history.size());
  {
    Store store;
    CHECK(store.begin());
    CHECK(store.shotCount() == 0 && store.nextShotId() == 9);
    CHECK(haveFile("/shots.unread") && haveFile("/c3.bin") && haveFile("/contexts.bin"));
  }
}

// ----------------------------------------------------------------- backups --

struct Collected {
  std::vector<uint8_t> bytes;
};
static std::vector<uint8_t> makeBackup(bool secrets) {
  Collected c;
  BackupOptions options;
  options.history = false;
  options.secrets = secrets;
  options.epochS = 1790000000u;
  writeBackup(options, [](const uint8_t *data, size_t length, void *context) {
    Collected &out = *static_cast<Collected *>(context);
    out.bytes.insert(out.bytes.end(), data, data + length);
  }, &c);
  return c.bytes;
}

static bool contains(const std::vector<uint8_t> &haystack, const char *needle) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(needle);   // unsigned: 0xC0 must equal 0xC0
  return std::search(haystack.begin(), haystack.end(), bytes, bytes + strlen(needle)) != haystack.end();
}

static void fillSecrets(PusherSecrets &s, const char *tag) {
  snprintf(s.password, sizeof(s.password), "pw-%s", tag);
  snprintf(s.tuyaUuid, sizeof(s.tuyaUuid), "uuid-%s", tag);
  snprintf(s.tuyaDeviceId, sizeof(s.tuyaDeviceId), "dev-%s", tag);
  snprintf(s.tuyaLocalKey, sizeof(s.tuyaLocalKey), "key-%s", tag);
  s.tuyaVariant = 1;
  for (int i = 0; i < 16; ++i) s.microbotToken[i] = static_cast<uint8_t>(0xC0 + i);
  s.microbotTokenSet = true;
}

static void testBackupSecrets() {
  printf("backup without secrets: every password and token blanked, a restore keeps the controller's own\n");
  freshWorld();
  Store store;
  CHECK(store.begin());
  UpdateConfig ota;
  strncpy(ota.repo, "someone/their-fork", sizeof(ota.repo) - 1);
  strncpy(ota.token, "ghp_OTA_TOKEN_VALUE", sizeof(ota.token) - 1);
  ota.autoCheck = false;
  store.saveUpdateConfig(ota);
  PusherConfig pushers;
  strncpy(pushers.powerAddress, "AA:BB:CC:DD:EE:01", sizeof(pushers.powerAddress) - 1);
  fillSecrets(pushers.brew, "brew");
  fillSecrets(pushers.power, "power");
  pushers.utcOffsetMin = 120;
  pushers.schedule[0].minuteOfDay = 405;
  store.savePushers(pushers);
  StopBotConfig stop;
  strncpy(stop.address, "AA:BB:CC:DD:EE:02", sizeof(stop.address) - 1);
  fillSecrets(stop.secrets, "stop");
  store.saveStopBot(stop);
  NetConfig net;
  strncpy(net.staSsid, "kitchen", sizeof(net.staSsid) - 1);
  strncpy(net.staPassword, "wifi-PASSWORD", sizeof(net.staPassword) - 1);
  strncpy(net.haToken, "HA_LONG_LIVED_TOKEN", sizeof(net.haToken) - 1);
  strncpy(net.libraryToken, "LIB_TOKEN_VALUE", sizeof(net.libraryToken) - 1);
  store.saveNet(net);

  const char *secretsText[] = {"ghp_OTA_TOKEN_VALUE", "pw-brew", "pw-power", "pw-stop", "key-brew", "key-power",
                               "key-stop", "wifi-PASSWORD", "HA_LONG_LIVED_TOKEN", "LIB_TOKEN_VALUE"};
  const char microbot[] = "\xC0\xC1\xC2\xC3\xC4\xC5";
  const std::vector<uint8_t> safe = makeBackup(false);
  CHECK(!safe.empty());
  for (const char *text : secretsText) CHECK(!contains(safe, text));
  CHECK(!contains(safe, microbot));
  // What is not a secret is still there.
  CHECK(contains(safe, "someone/their-fork") && contains(safe, "kitchen") && contains(safe, "AA:BB:CC:DD:EE:01") &&
        contains(safe, "AA:BB:CC:DD:EE:02") && contains(safe, "dev-brew") && contains(safe, "dev-stop"));
  const std::vector<uint8_t> full = makeBackup(true);
  for (const char *text : secretsText) CHECK(contains(full, text));
  CHECK(contains(full, microbot));

  // Restoring the safe backup onto a controller with other secrets keeps them: the same devices keep
  // their passwords and keys, and a different bot's are not carried over.
  PusherConfig mine = pushers;
  fillSecrets(mine.brew, "mine");
  fillSecrets(mine.power, "mine2");
  store.savePushers(mine);
  StopBotConfig otherStop = stop;
  strncpy(otherStop.address, "AA:BB:CC:DD:EE:99", sizeof(otherStop.address) - 1);   // another bot
  fillSecrets(otherStop.secrets, "other");
  store.saveStopBot(otherStop);
  UpdateConfig myOta = ota;
  strncpy(myOta.token, "ghp_MY_OWN_TOKEN", sizeof(myOta.token) - 1);
  strncpy(myOta.repo, "me/mine", sizeof(myOta.repo) - 1);
  store.saveUpdateConfig(myOta);
  NetConfig myNet = net;
  strncpy(myNet.staPassword, "my-wifi", sizeof(myNet.staPassword) - 1);
  strncpy(myNet.haToken, "MY_HA_TOKEN", sizeof(myNet.haToken) - 1);
  store.saveNet(myNet);

  writeFile(RESTORE_PATH, safe.data(), safe.size());
  CHECK(applyRestore() == nullptr);
  const UpdateConfig gotOta = store.loadUpdateConfig();
  CHECK(strcmp(gotOta.repo, "someone/their-fork") == 0 && !gotOta.autoCheck);   // the backup's settings
  CHECK(strcmp(gotOta.token, "ghp_MY_OWN_TOKEN") == 0);                          // the controller's token
  const PusherConfig gotPushers = store.loadPushers();
  CHECK(strcmp(gotPushers.brew.password, "pw-mine") == 0 && strcmp(gotPushers.power.password, "pw-mine2") == 0);
  CHECK(strcmp(gotPushers.brew.tuyaLocalKey, "") == 0);   // another Tuya device id in the backup: no key carried over
  CHECK(strcmp(gotPushers.power.tuyaDeviceId, "dev-power") == 0 && gotPushers.utcOffsetMin == 120 &&
        gotPushers.schedule[0].minuteOfDay == 405);
  CHECK(gotPushers.power.microbotTokenSet && gotPushers.power.microbotToken[0] == 0xC0);
  const StopBotConfig gotStop = store.loadStopBot();
  CHECK(strcmp(gotStop.address, "AA:BB:CC:DD:EE:02") == 0);
  CHECK(strcmp(gotStop.secrets.password, "") == 0 && !gotStop.secrets.microbotTokenSet);   // a different bot
  const NetConfig gotNet = store.loadNet();
  CHECK(strcmp(gotNet.staSsid, "kitchen") == 0 && strcmp(gotNet.staPassword, "my-wifi") == 0 &&
        strcmp(gotNet.haToken, "MY_HA_TOKEN") == 0);
}

int main() {
  testNvsLayoutGolden();
  testLongBlobRollback();
  testUnreadHistoryComesBack();
  testContextsFollowTheHistory();
  testShotListPass();
  testContextsConversion();
  testContextsSetAside();
  testHistoryResetDropsStaleData();
  testBackupSecrets();
  freshWorld();
  printf("\n%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
