#include "Store.h"

#include <LittleFS.h>
#include <Preferences.h>

#include "Log.h"
#include "platform/FsUtil.h"
#include "core/SettingsUtil.h"
#include "platform/BigMem.h"

namespace bbw {

namespace {

constexpr const char *NVS_NAMESPACE = "bbw";
constexpr const char *HISTORY_PATH = "/shots.bin";
constexpr const char *HISTORY_TMP_PATH = "/shots.new";
constexpr const char *HISTORY_V8_BACKUP_PATH = "/shots.v8.bak";
constexpr const char *HISTORY_V9_BACKUP_PATH = "/shots.v9.bak";
constexpr const char *LIBRARY_PATH = "/library.bin";
constexpr const char *LIBRARY_TMP_PATH = "/library.new";
constexpr const char *OUTBOX_PATH = "/outbox.bin";
constexpr uint32_t OUTBOX_MAGIC = 0x4F574242;   // "BBWO"
constexpr uint32_t HISTORY_MAGIC = 0x53574242;  // "BBWS"
constexpr uint16_t HISTORY_VERSION = 10;        // 10: the bag and the measured TDS added
constexpr uint16_t HISTORY_VERSION_V9 = 9;      // 9: coffee, recipe and grinder ids added
constexpr uint16_t HISTORY_VERSION_V8 = 8;
constexpr uint32_t CURVE_MAGIC = 0x43574242;    // "BBWC"
constexpr uint16_t CURVE_VERSION = 1;
constexpr uint32_t CONTEXT_MAGIC = 0x58574242;   // "BBWX"
// Flash kept free so the history file and settings can always be written.
constexpr size_t CURVE_RESERVE_BYTES = 96 * 1024;

struct CurveHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t shotId;
};

// Shot contexts live in one ring file beside the history, one fixed slot per shot (id modulo the
// history's capacity): one file instead of one per shot keeps the directory small and every
// read or write a single seek. A slot is only believed when the shot id in it matches.
constexpr const char *CONTEXT_PATH = "/contexts.bin";
struct ContextHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t recordSize;  // sizeof(ShotContext) when the file was made
  uint32_t capacity;
};

// The power presses: a header saying how many were ever logged, then a fixed ring of slots.
constexpr const char *POWER_PATH = "/power.bin";
constexpr uint32_t POWER_MAGIC = 0x31455750;  // "PWE1"
struct PowerHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t recordSize;  // sizeof(PowerEvent) when the file was made
  uint32_t capacity;
  uint32_t logged;      // events ever written; the next goes into slot logged % capacity
};

// Coffee ratings: a header, then a fixed ring of 32-byte slots. Slots with id 0 are empty.
constexpr const char *RATINGS_PATH = "/ratings.bin";
constexpr uint32_t RATINGS_MAGIC = 0x31544152;  // "RAT1"
struct RatingsHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t recordSize;
  uint32_t capacity;
  uint32_t logged;   // ratings ever appended; the next goes into slot logged % capacity
  uint32_t nextId;
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
  const size_t length = prefs.isKey(SETTINGS_KEY_V7) ? prefs.getBytesLength(SETTINGS_KEY_V7) : 0;
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
  const size_t length = prefs.isKey(key) ? prefs.getBytesLength(key) : 0;
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
  if (bbw::fileExists(LIBRARY_PATH)) return;
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
  if (bbw::fileExists(HISTORY_V8_BACKUP_PATH)) LittleFS.remove(HISTORY_V8_BACKUP_PATH);
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

// Rewrites a version 9 history as version 10 (two fields added at the end of each shot), shot for shot, and keeps the
// version 8 file as a backup. The new file is complete before the old one is
// moved, so a reset part-way leaves the old history in place to try again.
// false: nothing was changed.
bool Store::convertHistoryV9() {
  struct HeaderV9 {
    uint32_t magic;
    uint16_t version;
    uint16_t recordSize;
    uint32_t head;
    uint32_t count;
    uint32_t nextId;
  };
  File in = LittleFS.open(HISTORY_PATH, "r");
  HeaderV9 old = {};
  if (!in || in.read(reinterpret_cast<uint8_t *>(&old), sizeof(old)) != sizeof(old) ||
      old.magic != HISTORY_MAGIC || old.version != HISTORY_VERSION_V9 ||
      old.recordSize != sizeof(legacy::ShotRecordV9) || old.head >= HISTORY_CAPACITY ||
      old.count > HISTORY_CAPACITY) {
    if (in) in.close();
    return false;
  }
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
  static legacy::ShotRecordV9 before;
  static ShotRecord after;
  for (uint32_t slot = 0; ok && slot < old.count; ++slot) {
    ok = in.seek(sizeof(old) + static_cast<size_t>(slot) * sizeof(before)) &&
         in.read(reinterpret_cast<uint8_t *>(&before), sizeof(before)) == sizeof(before);
    if (!ok) break;
    before.recipe[RECIPE_NAME_LEN - 1] = '\0';
    before.coffee[COFFEE_NAME_LEN - 1] = '\0';
    before.region[ORIGIN_LEN - 1] = before.varietal[ORIGIN_LEN - 1] = '\0';
    after = shotFromV9(before);
    ok = out.write(reinterpret_cast<const uint8_t *>(&after), sizeof(after)) == sizeof(after);
  }
  in.close();
  out.close();
  if (!ok) {
    LittleFS.remove(HISTORY_TMP_PATH);
    LOGW("store: shot history could not be converted");
    return false;
  }
  if (bbw::fileExists(HISTORY_V9_BACKUP_PATH)) LittleFS.remove(HISTORY_V9_BACKUP_PATH);
  if (!LittleFS.rename(HISTORY_PATH, HISTORY_V9_BACKUP_PATH)) {
    LittleFS.remove(HISTORY_TMP_PATH);
    return false;
  }
  if (!LittleFS.rename(HISTORY_TMP_PATH, HISTORY_PATH)) {
    LittleFS.rename(HISTORY_V9_BACKUP_PATH, HISTORY_PATH);  // put it back
    return false;
  }
  LOGI("store: %u shots converted to the new history format",
       static_cast<unsigned>(old.count));
  return true;
}

bool Store::begin() {
  allocateStoreBuffers();
  // By label: partitions.csv calls it "littlefs", and LittleFS.begin() would
  // otherwise look for one called "spiffs" and find nothing.
  fsOk_ = LittleFS.begin(true, "/littlefs", 10, "littlefs");
  if (!fsOk_) {
    LOGW("store: LittleFS mount failed, shot history disabled");
    return false;
  }
  prepareLibrary();
  {
    File state = fileExists(OUTBOX_PATH) ? LittleFS.open(OUTBOX_PATH, "r") : File();
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
  if (!bbw::fileExists(HISTORY_PATH) && bbw::fileExists(HISTORY_V9_BACKUP_PATH)) {
    LittleFS.rename(HISTORY_V9_BACKUP_PATH, HISTORY_PATH);
  }
  if (!bbw::fileExists(HISTORY_PATH) && bbw::fileExists(HISTORY_V8_BACKUP_PATH)) {
    LittleFS.rename(HISTORY_V8_BACKUP_PATH, HISTORY_PATH);
  }
  if (bbw::fileExists(HISTORY_TMP_PATH)) LittleFS.remove(HISTORY_TMP_PATH);
  convertHistoryV8();   // a version 8 history becomes version 10 in one step
  convertHistoryV9();
  File file = LittleFS.open(HISTORY_PATH, "r");
  const bool valid = file && file.read(reinterpret_cast<uint8_t *>(&header_), sizeof(header_)) ==
                                 sizeof(header_) &&
                     header_.magic == HISTORY_MAGIC && header_.version == HISTORY_VERSION &&
                     header_.recordSize == sizeof(ShotRecord) &&
                     header_.head < HISTORY_CAPACITY && header_.count <= HISTORY_CAPACITY;
  if (file) file.close();
  if (!valid) {
    // Never throw away a history this build cannot read: set it aside.
    if (bbw::fileExists(HISTORY_PATH)) {
      if (bbw::fileExists("/shots.unread")) LittleFS.remove("/shots.unread");
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
  if (bbw::fileExists(path)) LittleFS.remove(path);
}

bool Store::prepareContexts() {
  if (!fsOk_) return false;
  if (bbw::fileExists(CONTEXT_PATH)) {
    File file = LittleFS.open(CONTEXT_PATH, "r");
    ContextHeader header = {};
    const bool ok = file && file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
                    header.magic == CONTEXT_MAGIC && header.version == SHOT_CONTEXT_VERSION &&
                    header.recordSize == sizeof(ShotContext) && header.capacity == HISTORY_CAPACITY &&
                    file.size() == sizeof(header) + HISTORY_CAPACITY * sizeof(ShotContext);
    if (file) file.close();
    if (ok) return true;
    LittleFS.remove(CONTEXT_PATH);  // another layout: start again (only recorded data is lost)
  }
  File file = LittleFS.open(CONTEXT_PATH, "w");
  if (!file) return false;
  const ContextHeader header = {CONTEXT_MAGIC, SHOT_CONTEXT_VERSION,
                                static_cast<uint16_t>(sizeof(ShotContext)), HISTORY_CAPACITY};
  bool ok = file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header);
  const ShotContext blank;  // shot id 0: an empty slot
  for (uint32_t i = 0; ok && i < HISTORY_CAPACITY; ++i) {
    ok = file.write(reinterpret_cast<const uint8_t *>(&blank), sizeof(blank)) == sizeof(blank);
  }
  file.close();
  if (!ok) LittleFS.remove(CONTEXT_PATH);
  return ok;
}

bool Store::preparePowerLog() {
  if (!fsOk_) return false;
  if (bbw::fileExists(POWER_PATH)) {
    File file = LittleFS.open(POWER_PATH, "r");
    PowerHeader header = {};
    const bool ok = file && file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
                    header.magic == POWER_MAGIC && header.version == 1 &&
                    header.recordSize == sizeof(PowerEvent) && header.capacity == POWER_EVENT_CAPACITY &&
                    file.size() == sizeof(header) + POWER_EVENT_CAPACITY * sizeof(PowerEvent);
    if (file) file.close();
    if (ok) return true;
    LittleFS.remove(POWER_PATH);  // another layout: start again
  }
  File file = LittleFS.open(POWER_PATH, "w");
  if (!file) return false;
  const PowerHeader header = {POWER_MAGIC, 1, static_cast<uint16_t>(sizeof(PowerEvent)), POWER_EVENT_CAPACITY, 0};
  bool ok = file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header);
  const PowerEvent blank;
  for (uint32_t i = 0; ok && i < POWER_EVENT_CAPACITY; ++i) {
    ok = file.write(reinterpret_cast<const uint8_t *>(&blank), sizeof(blank)) == sizeof(blank);
  }
  file.close();
  if (!ok) LittleFS.remove(POWER_PATH);
  return ok;
}

bool Store::appendPowerEvent(const PowerEvent &event) {
  if (!preparePowerLog()) return false;
  File file = LittleFS.open(POWER_PATH, "r+");
  if (!file) return false;
  PowerHeader header = {};
  bool ok = file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header);
  if (ok) {
    const size_t offset = sizeof(PowerHeader) + (header.logged % POWER_EVENT_CAPACITY) * sizeof(PowerEvent);
    ok = file.seek(offset) && file.write(reinterpret_cast<const uint8_t *>(&event), sizeof(event)) == sizeof(event);
  }
  if (ok) {
    ++header.logged;
    ok = file.seek(0) && file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header);
  }
  file.close();
  return ok;
}

void Store::scanPowerEvents(bool (*visit)(const PowerEvent &, void *), void *context) {
  if (!fsOk_ || !bbw::fileExists(POWER_PATH)) return;
  File file = LittleFS.open(POWER_PATH, "r");
  if (!file) return;
  PowerHeader header = {};
  if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
      header.magic == POWER_MAGIC && header.recordSize == sizeof(PowerEvent) &&
      header.capacity == POWER_EVENT_CAPACITY) {
    const uint32_t kept = header.logged < POWER_EVENT_CAPACITY ? header.logged : POWER_EVENT_CAPACITY;
    for (uint32_t i = 0; i < kept; ++i) {
      const uint32_t slot = (header.logged - 1 - i) % POWER_EVENT_CAPACITY;
      PowerEvent event;
      if (!file.seek(sizeof(PowerHeader) + slot * sizeof(PowerEvent)) ||
          file.read(reinterpret_cast<uint8_t *>(&event), sizeof(event)) != sizeof(event) ||
          !visit(event, context)) {
        break;
      }
    }
  }
  file.close();
}

bool Store::prepareRatings() {
  if (!fsOk_) return false;
  if (bbw::fileExists(RATINGS_PATH)) {
    File file = LittleFS.open(RATINGS_PATH, "r");
    RatingsHeader header = {};
    const bool ok = file && file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
                    header.magic == RATINGS_MAGIC && header.version == 1 &&
                    header.recordSize == sizeof(CoffeeRating) && header.capacity == RATING_CAPACITY &&
                    file.size() == sizeof(header) + RATING_CAPACITY * sizeof(CoffeeRating);
    if (file) file.close();
    if (ok) return true;
    LittleFS.remove(RATINGS_PATH);  // another layout: start again
  }
  File file = LittleFS.open(RATINGS_PATH, "w");
  if (!file) return false;
  const RatingsHeader header = {RATINGS_MAGIC, 1, static_cast<uint16_t>(sizeof(CoffeeRating)), RATING_CAPACITY, 0, 1};
  bool ok = file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header);
  const CoffeeRating blank;
  for (uint32_t i = 0; ok && i < RATING_CAPACITY; ++i) {
    ok = file.write(reinterpret_cast<const uint8_t *>(&blank), sizeof(blank)) == sizeof(blank);
  }
  file.close();
  if (!ok) LittleFS.remove(RATINGS_PATH);
  return ok;
}

bool Store::saveRating(CoffeeRating &r) {
  if (!sanitizeRating(r) || !prepareRatings()) return false;
  File file = LittleFS.open(RATINGS_PATH, "r+");
  if (!file) return false;
  RatingsHeader header = {};
  bool ok = file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header);
  // The slot of the rating it replaces: the same id, or the drinker's rating of the same bag.
  int32_t slot = -1;
  for (uint32_t i = 0; ok && i < RATING_CAPACITY && slot < 0; ++i) {
    CoffeeRating old;
    if (file.read(reinterpret_cast<uint8_t *>(&old), sizeof(old)) != sizeof(old)) {
      ok = false;
      break;
    }
    if (old.id != 0 && (r.id != 0 ? old.id == r.id : sameRated(old, r))) {
      slot = static_cast<int32_t>(i);
      r.id = old.id;
    }
  }
  if (ok && slot < 0) {
    slot = static_cast<int32_t>(header.logged % RATING_CAPACITY);
    r.id = header.nextId++;
    ++header.logged;
    ok = file.seek(0) && file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header);
  }
  if (ok) {
    ok = file.seek(sizeof(RatingsHeader) + static_cast<uint32_t>(slot) * sizeof(CoffeeRating)) &&
         file.write(reinterpret_cast<const uint8_t *>(&r), sizeof(r)) == sizeof(r);
  }
  file.close();
  return ok;
}

// The slot holding rating `id`, overwritten with *with (or emptied when with is null).
bool Store::editRatingSlot(uint32_t id, const CoffeeRating *with) {
  if (id == 0 || !fsOk_ || !bbw::fileExists(RATINGS_PATH)) return false;
  File file = LittleFS.open(RATINGS_PATH, "r+");
  if (!file) return false;
  bool found = false;
  if (file.seek(sizeof(RatingsHeader))) {
    for (uint32_t i = 0; i < RATING_CAPACITY; ++i) {
      CoffeeRating old;
      if (file.read(reinterpret_cast<uint8_t *>(&old), sizeof(old)) != sizeof(old)) break;
      if (old.id != id) continue;
      const CoffeeRating blank;
      const CoffeeRating &value = with ? *with : blank;
      found = file.seek(sizeof(RatingsHeader) + i * sizeof(CoffeeRating)) &&
              file.write(reinterpret_cast<const uint8_t *>(&value), sizeof(value)) == sizeof(value);
      break;
    }
  }
  file.close();
  return found;
}

bool Store::deleteRating(uint32_t id) { return editRatingSlot(id, nullptr); }

bool Store::setRatingShare(uint32_t id, uint8_t shareState, uint32_t shareSeq) {
  struct Find {
    uint32_t id;
    CoffeeRating found;
    bool have;
  } find = {id, CoffeeRating(), false};
  scanRatings([](const CoffeeRating &r, void *ctx) {
    Find &f = *static_cast<Find *>(ctx);
    if (r.id != f.id) return true;
    f.found = r;
    f.have = true;
    return false;
  }, &find);
  if (!find.have) return false;
  find.found.shareState = shareState;
  find.found.shareSeq = shareSeq;
  return editRatingSlot(id, &find.found);
}

void Store::scanRatings(bool (*visit)(const CoffeeRating &, void *), void *context) {
  if (!fsOk_ || !bbw::fileExists(RATINGS_PATH)) return;
  File file = LittleFS.open(RATINGS_PATH, "r");
  if (!file) return;
  RatingsHeader header = {};
  if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
      header.magic == RATINGS_MAGIC && header.recordSize == sizeof(CoffeeRating) &&
      header.capacity == RATING_CAPACITY) {
    const uint32_t kept = header.logged < RATING_CAPACITY ? header.logged : RATING_CAPACITY;
    for (uint32_t i = 0; i < kept; ++i) {
      const uint32_t slot = (header.logged - 1 - i) % RATING_CAPACITY;
      CoffeeRating r;
      if (!file.seek(sizeof(RatingsHeader) + slot * sizeof(CoffeeRating)) ||
          file.read(reinterpret_cast<uint8_t *>(&r), sizeof(r)) != sizeof(r)) {
        break;
      }
      if (r.id != 0 && !visit(r, context)) break;
    }
  }
  file.close();
}

Drinkers Store::loadDrinkers() {
  Drinkers d;
  loadBlob("drinkers", 1, d);
  sanitizeDrinkers(d);
  return d;
}

void Store::saveDrinkers(const Drinkers &drinkers) { saveBlob("drinkers", 1, drinkers); }

bool Store::saveContext(const ShotContext &context) {
  if (context.shotId == 0 || !prepareContexts()) return false;
  File file = LittleFS.open(CONTEXT_PATH, "r+");
  if (!file) return false;
  const size_t offset = sizeof(ContextHeader) + (context.shotId % HISTORY_CAPACITY) * sizeof(ShotContext);
  const bool ok = file.seek(offset) &&
                  file.write(reinterpret_cast<const uint8_t *>(&context), sizeof(context)) == sizeof(context);
  file.close();
  return ok;
}

bool Store::setShotPrep(uint32_t id, uint16_t doseDg, bool purged, bool uneven) {
  if (!fsOk_ || id == 0 || id >= header_.nextId) return false;
  if (header_.nextId > HISTORY_CAPACITY && id < header_.nextId - HISTORY_CAPACITY) return false;  // left the history
  ShotContext context;
  if (!loadContext(id, context)) {
    context = ShotContext();
    context.shotId = id;
    context.engineVersion = 0;   // made afterwards: nothing but the owner's entries is known
  }
  if (!applyShotPrep(context, doseDg, purged, uneven)) return false;
  return saveContext(context);
}

bool Store::loadContext(uint32_t id, ShotContext &out) {
  if (!fsOk_ || id == 0 || !bbw::fileExists(CONTEXT_PATH)) return false;
  File file = LittleFS.open(CONTEXT_PATH, "r");
  if (!file) return false;
  ContextHeader header = {};
  bool ok = file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
            header.magic == CONTEXT_MAGIC && header.version == SHOT_CONTEXT_VERSION &&
            header.recordSize == sizeof(ShotContext) && header.capacity == HISTORY_CAPACITY;
  const size_t offset = sizeof(ContextHeader) + (id % HISTORY_CAPACITY) * sizeof(ShotContext);
  if (ok) ok = file.seek(offset) && file.read(reinterpret_cast<uint8_t *>(&out), sizeof(out)) == sizeof(out);
  file.close();
  return ok && out.shotId == id;   // a slot still holding an older shot is not this one
}

bool Store::hasCurve(uint32_t id) {
  if (!fsOk_) return false;
  char path[20];
  curvePath(id, path, sizeof(path));
  return bbw::fileExists(path);
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

bool Store::setShotTds(uint32_t id, uint16_t tdsX100) {
  return editShot(id, [](ShotRecord &r, void *c) { r.tdsX100 = *static_cast<uint16_t *>(c); }, &tdsX100);
}

void Store::clearShots() {
  if (!fsOk_) return;
  const uint32_t first = header_.nextId > HISTORY_CAPACITY + 1 ? header_.nextId - HISTORY_CAPACITY - 1 : 1;
  for (uint32_t id = first; id < header_.nextId; ++id) removeCurve(id);
  if (bbw::fileExists(CONTEXT_PATH)) LittleFS.remove(CONTEXT_PATH);
  resetHistory();
}

size_t Store::readText(const char *path, char *out, size_t size) {
  if (size == 0) return 0;
  out[0] = '\0';
  if (!fsOk_ || !bbw::fileExists(path)) return 0;
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
  return writeText(path, text) == 1;
}

bool Store::saveText(const char *path, const char *text) {
  return writeText(path, text) >= 0;
}

// 1: written; 0: the file held this text already; -1: it could not be written.
int Store::writeText(const char *path, const char *text) {
  if (!fsOk_) return -1;
  const size_t length = strlen(text);
  bool same = false;
  if (bbw::fileExists(path)) {
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
  if (same) return 0;
  File file = LittleFS.open(path, "w");
  if (!file) return -1;
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(text), length) == length;
  file.close();
  return ok ? 1 : -1;
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

void Store::outboxForgetKey() {
  outbox_.keyQueued = 0;
  saveOutboxState();
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

Maintenance Store::loadMaintenance() {
  Maintenance m;
  loadBlob("maint", 1, m);
  sanitizeMaintenance(m);
  return m;
}

void Store::saveMaintenance(const Maintenance &m) { saveBlob("maint", 1, m); }

Pairing Store::loadPairing() {
  Pairing pairing;
  loadBlob("pairing", 1, pairing);
  pairing.scaleAddress[ADDR_LEN - 1] = '\0';
  pairing.botAddress[ADDR_LEN - 1] = '\0';
  return pairing;
}

void Store::savePairing(const Pairing &pairing) { saveBlob("pairing", 1, pairing); }

UpdateConfig Store::loadUpdateConfig() {
  UpdateConfig config;
  if (!loadBlob("otacfg", 1, config)) config = UpdateConfig();
  config.repo[sizeof(config.repo) - 1] = '\0';
  config.token[sizeof(config.token) - 1] = '\0';
  if (config.repo[0] == '\0') strlcpy(config.repo, DEFAULT_RELEASE_REPO, sizeof(config.repo));
  return config;
}

void Store::saveUpdateConfig(const UpdateConfig &config) { saveBlob("otacfg", 1, config); }

CupMemory Store::loadCupMemory() {
  CupMemory memory;
  if (!loadBlob("cupmem", 1, memory)) memory = CupMemory();
  return memory;
}

void Store::saveCupMemory(const CupMemory &memory) { saveBlob("cupmem", 1, memory); }

NetConfig Store::loadNet() {
  NetConfig net;
  loadBlob("net", 1, net);
  net.staSsid[sizeof(net.staSsid) - 1] = '\0';
  net.staPassword[sizeof(net.staPassword) - 1] = '\0';
  net.apPassword[sizeof(net.apPassword) - 1] = '\0';
  net.botPassword[sizeof(net.botPassword) - 1] = '\0';
  net.libraryRepo[sizeof(net.libraryRepo) - 1] = '\0';
  net.libraryToken[sizeof(net.libraryToken) - 1] = '\0';
  net.diagRelay[sizeof(net.diagRelay) - 1] = '\0';
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

StopBotConfig Store::loadStopBot() {
  StopBotConfig config;
  loadBlob("stopbot", 1, config);
  sanitizeStopBotConfig(config);
  return config;
}

void Store::saveStopBot(const StopBotConfig &config) { saveBlob("stopbot", 1, config); }

MachineProfile Store::loadMachine() {
  MachineProfile machine;
  if (!loadBlob("machine", 1, machine)) machine = MachineProfile();
  sanitizeMachine(machine);
  return machine;
}

void Store::saveMachine(const MachineProfile &machine) { saveBlob("machine", 1, machine); }

}  // namespace bbw
