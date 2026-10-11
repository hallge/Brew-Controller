#include "Store.h"

#include <LittleFS.h>
#include <Preferences.h>

#include "Log.h"
#include "core/SettingsUtil.h"
#include "platform/BigMem.h"

namespace bbw {

namespace {

constexpr const char *NVS_NAMESPACE = "bbw";
constexpr const char *HISTORY_PATH = "/shots.bin";
constexpr const char *HISTORY_TMP_PATH = "/shots.new";
constexpr const char *HISTORY_V8_BACKUP_PATH = "/shots.v8.bak";
constexpr const char *LIBRARY_PATH = "/library.bin";
constexpr const char *LIBRARY_TMP_PATH = "/library.new";
constexpr const char *OUTBOX_PATH = "/outbox.bin";
constexpr uint32_t OUTBOX_MAGIC = 0x4F574242;   // "BBWO"
constexpr uint32_t HISTORY_MAGIC = 0x53574242;  // "BBWS"
constexpr uint16_t HISTORY_VERSION = 9;         // 9: coffee, recipe and grinder ids added
constexpr uint16_t HISTORY_VERSION_V8 = 8;
constexpr uint32_t CURVE_MAGIC = 0x43574242;    // "BBWC"
constexpr uint16_t CURVE_VERSION = 1;
// Flash kept free so the history file and settings can always be written.
constexpr size_t CURVE_RESERVE_BYTES = 96 * 1024;

struct CurveHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t shotId;
};

void curvePath(uint32_t id, char *out, size_t size) {
  snprintf(out, size, "/c%u.bin", static_cast<unsigned>(id));
}
// Bump when the Settings layout changes; a mismatch falls back to defaults.
// Version 8 lives under its own key: version 7, which also held the recipes,
// is left where it was.
constexpr uint16_t SETTINGS_VERSION = 8;
constexpr const char *SETTINGS_KEY = "cfg";
constexpr uint16_t SETTINGS_VERSION_V7 = 7;
constexpr const char *SETTINGS_KEY_V7 = "settings";

// Too big for a task's stack, and only one of them is ever in use: flash is
// read and written from the loop task alone.
// In PSRAM (platform/BigMem.h), made in Store::begin().
Library *g_scratchLibrary;
uint8_t *g_libraryBytes;  // LIBRARY_MAX_BYTES
struct StoredV7 {
  uint16_t version;
  legacy::SettingsV7 value;
};
StoredV7 *g_storedV7;

void allocateStoreBuffers() {
  if (g_scratchLibrary) return;
  g_scratchLibrary = bigNew<Library>();
  g_libraryBytes = bigArray<uint8_t>(LIBRARY_MAX_BYTES);
  g_storedV7 = static_cast<StoredV7 *>(bigAlloc(sizeof(StoredV7)));
}

// The settings an earlier build left in NVS. 0 if there are none; otherwise
// how many bytes of legacy::SettingsV7 were stored.
size_t loadSettingsV7() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) return 0;
  memset(&(*g_storedV7), 0, sizeof(StoredV7));
  const size_t header = offsetof(StoredV7, value);
  const size_t length = prefs.getBytesLength(SETTINGS_KEY_V7);
  const bool ok = length > header && length <= sizeof(StoredV7) &&
                  prefs.getBytes(SETTINGS_KEY_V7, &(*g_storedV7), length) == length &&
                  (*g_storedV7).version == SETTINGS_VERSION_V7;
  prefs.end();
  return ok ? length - header : 0;
}

template <typename T>
bool loadBlob(const char *key, uint16_t version, T &out) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) return false;
  struct Stored { uint16_t version; T value; };
  // A blob saved by an earlier build of the same version is shorter: fields
  // are only ever added at the end. What was stored fills the front, and
  // anything added since keeps the default already in `out`.
  Stored stored;
  stored.version = 0;
  stored.value = out;
  const size_t length = prefs.getBytesLength(key);
  const bool ok = length > sizeof(uint16_t) && length <= sizeof(Stored) &&
                  prefs.getBytes(key, &stored, length) == length && stored.version == version;
  prefs.end();
  if (ok) out = stored.value;
  return ok;
}

template <typename T>
void saveBlob(const char *key, uint16_t version, const T &value) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    LOGW("store: NVS open failed, %s not saved", key);
    return;
  }
  struct Stored { uint16_t version; T value; };
  Stored stored;
  memset(&stored, 0, sizeof(stored));
  stored.version = version;
  stored.value = value;
  if (prefs.putBytes(key, &stored, sizeof(stored)) != sizeof(stored)) {
    LOGW("store: write of %s failed", key);
  }
  prefs.end();
}

}  // namespace

// Makes sure there is a library on flash before anything asks for it. With
// none there, one is made from the recipes an earlier build kept in its
// settings, or from the defaults.
void Store::prepareLibrary() {
  if (LittleFS.exists(LIBRARY_PATH)) return;
  if (loadSettingsV7() != 0) {
    libraryFromV7((*g_storedV7).value, (*g_scratchLibrary));
    LOGI("store: %u recipes from the earlier settings are now %u presets, %u coffees",
         static_cast<unsigned>((*g_storedV7).value.recipeCount),
         static_cast<unsigned>((*g_scratchLibrary).presetCount),
         static_cast<unsigned>((*g_scratchLibrary).coffeeCount));
  } else {
    setDefaultLibrary((*g_scratchLibrary));
  }
  saveLibrary((*g_scratchLibrary));
}

void Store::loadLibrary(Library &out) {
  bool ok = false;
  if (fsOk_) {
    File file = LittleFS.open(LIBRARY_PATH, "r");
    if (file) {
      const size_t length = file.read(g_libraryBytes, LIBRARY_MAX_BYTES);
      file.close();
      ok = decodeLibrary(g_libraryBytes, length, out);
    }
  }
  if (ok) return;
  // No file system, or a file that is not a library: the earlier settings
  // if they are still there, else the defaults. Nothing is written here.
  const bool earlier = loadSettingsV7() != 0;
  if (earlier) libraryFromV7((*g_storedV7).value, out);
  else setDefaultLibrary(out);
  LOGW("store: no stored library, using %s", earlier ? "the earlier settings" : "defaults");
}

// Written beside the old file and swapped in, so a reset in the middle
// leaves one complete library or the other.
bool Store::saveLibrary(const Library &library) {
  if (!fsOk_) return false;
  const size_t length = encodeLibrary(library, g_libraryBytes, LIBRARY_MAX_BYTES);
  if (length == 0) return false;
  File file = LittleFS.open(LIBRARY_TMP_PATH, "w");
  if (!file) return false;
  const bool ok = file.write(g_libraryBytes, length) == length;
  file.close();
  if (!ok) {
    LittleFS.remove(LIBRARY_TMP_PATH);
    LOGW("store: library not saved");
    return false;
  }
  return LittleFS.rename(LIBRARY_TMP_PATH, LIBRARY_PATH);
}

// Rewrites a version 8 history as version 9, shot for shot, and keeps the
// version 8 file as a backup. The new file is complete before the old one is
// moved, so a reset part-way leaves the old history in place to try again.
// false: nothing was changed.
bool Store::convertHistoryV8() {
  struct HeaderV8 {
    uint32_t magic;
    uint16_t version;
    uint16_t recordSize;
    uint32_t head;
    uint32_t count;
    uint32_t nextId;
  };
  File in = LittleFS.open(HISTORY_PATH, "r");
  HeaderV8 old = {};
  if (!in || in.read(reinterpret_cast<uint8_t *>(&old), sizeof(old)) != sizeof(old) ||
      old.magic != HISTORY_MAGIC || old.version != HISTORY_VERSION_V8 ||
      old.recordSize != sizeof(legacy::ShotRecordV8) || old.head >= HISTORY_CAPACITY ||
      old.count > HISTORY_CAPACITY) {
    if (in) in.close();
    return false;
  }
  loadLibrary((*g_scratchLibrary));  // for the ids
  File out = LittleFS.open(HISTORY_TMP_PATH, "w");
  if (!out) {
    in.close();
    return false;
  }
  Header fresh = {HISTORY_MAGIC, HISTORY_VERSION, static_cast<uint16_t>(sizeof(ShotRecord)),
                  old.head, old.count, old.nextId};
  bool ok = out.write(reinterpret_cast<const uint8_t *>(&fresh), sizeof(fresh)) == sizeof(fresh);
  // A ring that has not wrapped holds its shots in slots 0 .. count-1; one
  // that has is full. Either way the slots in use are the first `count`.
  static legacy::ShotRecordV8 before;
  static ShotRecord after;
  for (uint32_t slot = 0; ok && slot < old.count; ++slot) {
    ok = in.seek(sizeof(old) + static_cast<size_t>(slot) * sizeof(before)) &&
         in.read(reinterpret_cast<uint8_t *>(&before), sizeof(before)) == sizeof(before);
    if (!ok) break;
    before.recipe[RECIPE_NAME_LEN - 1] = '\0';
    before.coffee[COFFEE_NAME_LEN - 1] = '\0';
    before.region[ORIGIN_LEN - 1] = before.varietal[ORIGIN_LEN - 1] = '\0';
    after = shotFromV8(before, (*g_scratchLibrary));
    ok = out.write(reinterpret_cast<const uint8_t *>(&after), sizeof(after)) == sizeof(after);
  }
  in.close();
  out.close();
  if (!ok) {
    LittleFS.remove(HISTORY_TMP_PATH);
    LOGW("store: shot history could not be converted");
    return false;
  }
  if (LittleFS.exists(HISTORY_V8_BACKUP_PATH)) LittleFS.remove(HISTORY_V8_BACKUP_PATH);
  if (!LittleFS.rename(HISTORY_PATH, HISTORY_V8_BACKUP_PATH)) {
    LittleFS.remove(HISTORY_TMP_PATH);
    return false;
  }
  if (!LittleFS.rename(HISTORY_TMP_PATH, HISTORY_PATH)) {
    LittleFS.rename(HISTORY_V8_BACKUP_PATH, HISTORY_PATH);  // put it back
    return false;
  }
  LOGI("store: %u shots converted to the new history format",
       static_cast<unsigned>(old.count));
  return true;
}

bool Store::begin() {
  allocateStoreBuffers();
  fsOk_ = LittleFS.begin(true);
  if (!fsOk_) {
    LOGW("store: LittleFS mount failed, shot history disabled");
    return false;
  }
  prepareLibrary();
  {
    File state = LittleFS.open(OUTBOX_PATH, "r");
    OutboxState read = {};
    if (state && state.read(reinterpret_cast<uint8_t *>(&read), sizeof(read)) == sizeof(read) &&
        read.magic == OUTBOX_MAGIC && read.sent <= read.next) {
      outbox_ = read;
    }
    if (state) state.close();
    outbox_.magic = OUTBOX_MAGIC;
  }
  // A reset between the two renames of a conversion: the backup is the
  // history. Put it back and convert again.
  if (!LittleFS.exists(HISTORY_PATH) && LittleFS.exists(HISTORY_V8_BACKUP_PATH)) {
    LittleFS.rename(HISTORY_V8_BACKUP_PATH, HISTORY_PATH);
  }
  if (LittleFS.exists(HISTORY_TMP_PATH)) LittleFS.remove(HISTORY_TMP_PATH);
  convertHistoryV8();
  File file = LittleFS.open(HISTORY_PATH, "r");
  const bool valid = file && file.read(reinterpret_cast<uint8_t *>(&header_), sizeof(header_)) ==
                                 sizeof(header_) &&
                     header_.magic == HISTORY_MAGIC && header_.version == HISTORY_VERSION &&
                     header_.recordSize == sizeof(ShotRecord) &&
                     header_.head < HISTORY_CAPACITY && header_.count <= HISTORY_CAPACITY;
  if (file) file.close();
  if (!valid) {
    // Never throw away a history this build cannot read: set it aside.
    if (LittleFS.exists(HISTORY_PATH)) {
      if (LittleFS.exists("/shots.unread")) LittleFS.remove("/shots.unread");
      LittleFS.rename(HISTORY_PATH, "/shots.unread");
      LOGW("store: shot history not readable, kept as /shots.unread");
    }
    resetHistory();
  }
  LOGI("store: %u shots in history", static_cast<unsigned>(header_.count));
  return true;
}

void Store::resetHistory() {
  header_.magic = HISTORY_MAGIC;
  header_.version = HISTORY_VERSION;
  header_.recordSize = sizeof(ShotRecord);
  header_.head = 0;
  header_.count = 0;
  if (header_.nextId == 0) header_.nextId = 1;
  File file = LittleFS.open(HISTORY_PATH, "w");
  if (file) {
    file.write(reinterpret_cast<const uint8_t *>(&header_), sizeof(header_));
    file.close();
  }
}

bool Store::writeHeader() {
  File file = LittleFS.open(HISTORY_PATH, "r+");
  if (!file) return false;
  const bool ok = file.seek(0) &&
                  file.write(reinterpret_cast<const uint8_t *>(&header_), sizeof(header_)) ==
                      sizeof(header_);
  file.close();
  return ok;
}

bool Store::appendShot(const ShotRecord &record) {
  if (!fsOk_) return false;
  File file = LittleFS.open(HISTORY_PATH, "r+");
  if (!file) return false;
  const size_t offset = sizeof(Header) + static_cast<size_t>(header_.head) * sizeof(ShotRecord);
  bool ok = file.seek(offset) &&
            file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) ==
                sizeof(record);
  file.close();
  if (!ok) return false;
  header_.head = (header_.head + 1) % HISTORY_CAPACITY;
  if (header_.count < HISTORY_CAPACITY) ++header_.count;
  header_.nextId = record.id + 1;
  // The shot that just fell out of the ring takes its data with it.
  if (record.id > HISTORY_CAPACITY) removeCurve(record.id - HISTORY_CAPACITY);
  return writeHeader();
}

void Store::removeCurve(uint32_t id) {
  char path[20];
  curvePath(id, path, sizeof(path));
  if (LittleFS.exists(path)) LittleFS.remove(path);
}

bool Store::hasCurve(uint32_t id) {
  if (!fsOk_) return false;
  char path[20];
  curvePath(id, path, sizeof(path));
  return LittleFS.exists(path);
}

void Store::makeRoomForCurve(size_t bytes) {
  // Normally there is room for all of them (500 shots are about 2 MB of the
  // 3.9 MB partition). If long shots have filled it, the oldest data goes
  // first; the shot summaries in the history are not touched.
  uint32_t id = header_.nextId > HISTORY_CAPACITY ? header_.nextId - HISTORY_CAPACITY : 1;
  while (id < header_.nextId &&
         LittleFS.totalBytes() - LittleFS.usedBytes() < bytes + CURVE_RESERVE_BYTES) {
    removeCurve(id++);
  }
}

bool Store::saveCurve(uint32_t id, const CurvePoint *points, size_t count) {
  if (!fsOk_ || !points || count == 0 || count > 0xFFFF) return false;
  makeRoomForCurve(sizeof(CurveHeader) + count * sizeof(CurvePoint));
  char path[20];
  curvePath(id, path, sizeof(path));
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  const CurveHeader header = {CURVE_MAGIC, CURVE_VERSION, static_cast<uint16_t>(count), id};
  const size_t bytes = count * sizeof(CurvePoint);
  const bool ok =
      file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
      file.write(reinterpret_cast<const uint8_t *>(points), bytes) == bytes;
  file.close();
  if (!ok) LittleFS.remove(path);  // never leave half a data set behind
  return ok;
}

size_t Store::loadCurve(uint32_t id, CurvePoint *out, size_t max) {
  if (!hasCurve(id) || !out) return 0;
  char path[20];
  curvePath(id, path, sizeof(path));
  File file = LittleFS.open(path, "r");
  if (!file) return 0;
  CurveHeader header = {};
  size_t count = 0;
  if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
      header.magic == CURVE_MAGIC && header.version == CURVE_VERSION && header.shotId == id) {
    count = header.count < max ? header.count : max;
    const size_t bytes = count * sizeof(CurvePoint);
    if (file.read(reinterpret_cast<uint8_t *>(out), bytes) != bytes) count = 0;
  }
  file.close();
  return count;
}

bool Store::readShot(uint32_t newestIndex, ShotRecord &out) {
  if (!fsOk_ || newestIndex >= header_.count) return false;
  const uint32_t slot =
      (header_.head + HISTORY_CAPACITY - 1 - newestIndex) % HISTORY_CAPACITY;
  File file = LittleFS.open(HISTORY_PATH, "r");
  if (!file) return false;
  const bool ok = file.seek(sizeof(Header) + static_cast<size_t>(slot) * sizeof(ShotRecord)) &&
                  file.read(reinterpret_cast<uint8_t *>(&out), sizeof(out)) == sizeof(out);
  file.close();
  if (ok) out.recipe[RECIPE_NAME_LEN - 1] = '\0';
  return ok;
}

void Store::scanShots(uint32_t maxShots, bool (*visit)(const ShotRecord &, void *),
                      void *context) {
  if (!fsOk_ || header_.count == 0) return;
  File file = LittleFS.open(HISTORY_PATH, "r");
  if (!file) return;
  ShotRecord record;
  const uint32_t limit = maxShots < header_.count ? maxShots : header_.count;
  for (uint32_t i = 0; i < limit; ++i) {
    const uint32_t slot = (header_.head + HISTORY_CAPACITY - 1 - i) % HISTORY_CAPACITY;
    if (!file.seek(sizeof(Header) + static_cast<size_t>(slot) * sizeof(ShotRecord)) ||
        file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) != sizeof(record)) {
      break;
    }
    record.recipe[RECIPE_NAME_LEN - 1] = '\0';
    if (!visit(record, context)) break;
  }
  file.close();
}

bool Store::editShot(uint32_t id, void (*edit)(ShotRecord &, void *), void *context) {
  if (!fsOk_) return false;
  ShotRecord record;
  for (uint32_t i = 0; i < header_.count; ++i) {
    if (!readShot(i, record) || record.id != id) continue;
    edit(record, context);
    const uint32_t slot = (header_.head + HISTORY_CAPACITY - 1 - i) % HISTORY_CAPACITY;
    File file = LittleFS.open(HISTORY_PATH, "r+");
    if (!file) return false;
    const bool ok =
        file.seek(sizeof(Header) + static_cast<size_t>(slot) * sizeof(ShotRecord)) &&
        file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
    file.close();
    return ok;
  }
  return false;
}

bool Store::setShotGrind(uint32_t id, uint16_t grindUm) {
  return editShot(id, [](ShotRecord &r, void *c) { r.grindUm = *static_cast<uint16_t *>(c); },
                  &grindUm);
}

bool Store::setShotGrindVote(uint32_t id, uint8_t vote) {
  return editShot(id, [](ShotRecord &r, void *c) { r.grindVote = *static_cast<uint8_t *>(c); },
                  &vote);
}

bool Store::setShotRating(uint32_t id, uint8_t rating, uint8_t taste) {
  uint8_t values[2] = {rating, taste};
  return editShot(id, [](ShotRecord &r, void *c) {
    r.rating = static_cast<uint8_t *>(c)[0];
    r.taste = static_cast<uint8_t *>(c)[1];
  }, values);
}

void Store::clearShots() {
  if (!fsOk_) return;
  const uint32_t first = header_.nextId > HISTORY_CAPACITY + 1 ? header_.nextId - HISTORY_CAPACITY - 1 : 1;
  for (uint32_t id = first; id < header_.nextId; ++id) removeCurve(id);
  resetHistory();
}

size_t Store::readText(const char *path, char *out, size_t size) {
  if (size == 0) return 0;
  out[0] = '\0';
  if (!fsOk_ || !LittleFS.exists(path)) return 0;
  File file = LittleFS.open(path, "r");
  if (!file) return 0;
  const size_t length = file.read(reinterpret_cast<uint8_t *>(out), size - 1);
  file.close();
  out[length] = '\0';
  return length;
}

// Flash wears: a file read again and again from the library is only written
// when what came back is different. true if it was written.
bool Store::writeTextIfChanged(const char *path, const char *text) {
  if (!fsOk_) return false;
  const size_t length = strlen(text);
  bool same = false;
  if (LittleFS.exists(path)) {
    File existing = LittleFS.open(path, "r");
    if (existing) {
      // Compared a piece at a time: no second copy of the file in memory.
      uint8_t piece[128];
      size_t at = 0;
      same = true;
      for (;;) {
        const size_t got = existing.read(piece, sizeof(piece));
        if (got == 0) break;
        if (at + got > length || memcmp(piece, text + at, got) != 0) {
          same = false;
          break;
        }
        at += got;
      }
      same = same && at == length;
      existing.close();
    }
  }
  if (same) return false;
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(text), length) == length;
  file.close();
  return ok;
}
// ---- outbox ---------------------------------------------------------------
// One file per record, "/o<number>.txt": the path it is to be published at,
// a line break, then the record.

namespace {
void outboxFile(uint32_t number, char *out, size_t size) {
  snprintf(out, size, "/o%u.txt", static_cast<unsigned>(number));
}
}  // namespace

void Store::saveOutboxState() {
  File file = LittleFS.open(OUTBOX_PATH, "w");
  if (!file) return;
  file.write(reinterpret_cast<const uint8_t *>(&outbox_), sizeof(outbox_));
  file.close();
}

bool Store::outboxAdd(const char *path, const char *text) {
  if (!fsOk_ || !path || !text || outboxWaiting() >= OUTBOX_CAPACITY) return false;
  if (LittleFS.totalBytes() - LittleFS.usedBytes() < CURVE_RESERVE_BYTES) return false;
  char name[20];
  outboxFile(outbox_.next, name, sizeof(name));
  File file = LittleFS.open(name, "w");
  if (!file) return false;
  const size_t pathLength = strlen(path), textLength = strlen(text);
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(path), pathLength) == pathLength &&
                  file.write(reinterpret_cast<const uint8_t *>("\n"), 1) == 1 &&
                  file.write(reinterpret_cast<const uint8_t *>(text), textLength) == textLength;
  file.close();
  if (!ok) {
    LittleFS.remove(name);
    return false;
  }
  ++outbox_.next;
  if (strncmp(path, "keys/", 5) == 0) outbox_.keyQueued = 1;
  saveOutboxState();
  return true;
}

bool Store::outboxPeek(char *path, size_t pathSize, char *text, size_t textSize) {
  while (fsOk_ && outboxWaiting() > 0) {
    char name[20];
    outboxFile(outbox_.sent, name, sizeof(name));
    File file = LittleFS.open(name, "r");
    size_t length = 0;
    if (file) {
      length = file.read(reinterpret_cast<uint8_t *>(text), textSize - 1);
      file.close();
    }
    text[length] = '\0';
    char *split = strchr(text, '\n');
    if (split && static_cast<size_t>(split - text) < pathSize && length < textSize - 1) {
      memcpy(path, text, split - text);
      path[split - text] = '\0';
      memmove(text, split + 1, strlen(split + 1) + 1);
      return true;
    }
    outboxPop();  // missing or not a record: nothing to send, move on
  }
  return false;
}

void Store::outboxPop() {
  if (outboxWaiting() == 0) return;
  char name[20];
  outboxFile(outbox_.sent, name, sizeof(name));
  LittleFS.remove(name);
  ++outbox_.sent;
  saveOutboxState();
}

Settings Store::loadSettings() {
  Settings settings = defaultSettings();
  if (loadBlob(SETTINGS_KEY, SETTINGS_VERSION, settings)) {
    sanitize(settings);
    return settings;
  }
  const size_t stored = loadSettingsV7();
  if (stored != 0) {
    settings = settingsFromV7((*g_storedV7).value, stored);
    saveSettings(settings);
    LOGI("store: settings carried over from the earlier build");
  } else {
    LOGI("store: no saved settings, using defaults");
  }
  return settings;
}

void Store::saveSettings(const Settings &settings) {
  saveBlob(SETTINGS_KEY, SETTINGS_VERSION, settings);
}

Pairing Store::loadPairing() {
  Pairing pairing;
  loadBlob("pairing", 1, pairing);
  pairing.scaleAddress[ADDR_LEN - 1] = '\0';
  pairing.botAddress[ADDR_LEN - 1] = '\0';
  return pairing;
}

void Store::savePairing(const Pairing &pairing) { saveBlob("pairing", 1, pairing); }

NetConfig Store::loadNet() {
  NetConfig net;
  loadBlob("net", 1, net);
  net.staSsid[sizeof(net.staSsid) - 1] = '\0';
  net.staPassword[sizeof(net.staPassword) - 1] = '\0';
  net.apPassword[sizeof(net.apPassword) - 1] = '\0';
  net.botPassword[sizeof(net.botPassword) - 1] = '\0';
  if (strlen(net.apPassword) < 8) strlcpy(net.apPassword, "brewbyweight", sizeof(net.apPassword));
  return net;
}

void Store::saveNet(const NetConfig &net) { saveBlob("net", 1, net); }

PusherConfig Store::loadPushers() {
  PusherConfig config;
  loadBlob("pushers", 1, config);
  sanitizePusherConfig(config);
  return config;
}

void Store::savePushers(const PusherConfig &config) { saveBlob("pushers", 1, config); }

}  // namespace bbw
