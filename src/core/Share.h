#pragma once
// What the controller sends to the shared library (docs/COMMUNITY.md,
// sections 5 and 7): the records, and the signed envelope each travels in.
//
// A record is one line of JSON, the "body". The envelope carries the body
// as a string, exactly as it was signed, beside the signature:
//
//   {"v":1,"kid":"<key fingerprint>","alg":"ES256","sig":"<base64>","body":"{...}"}
//
// The signature is over the body's bytes as they stand in that string, so
// whoever checks it never has to rebuild the JSON the way this code wrote it.
//
// Portable and host-tested; no flash, network or key storage in here.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Community.h"
#include "MachineProfile.h"
#include "GrindAdvisor.h"
#include "Library.h"
#include "Sha256.h"
#include "Types.h"

namespace bbw {

constexpr size_t PUBLIC_KEY_LEN = 65;   // P-256 point, uncompressed: 04 | X | Y
constexpr size_t SIGNATURE_LEN = 64;    // r | s, 32 bytes each, big endian
constexpr size_t KEY_ID_LEN = 21;       // 20 hex digits and the terminator
constexpr size_t SHARE_BODY_MAX = 1400;
constexpr size_t SHARE_ENVELOPE_MAX = 2 * SHARE_BODY_MAX + 256;  // every character escaped, at worst

// The controller's key. The private half never leaves the implementation.
class ISigner {
 public:
  virtual ~ISigner() = default;
  virtual bool ready() const = 0;
  virtual const uint8_t *publicKey() const = 0;  // PUBLIC_KEY_LEN bytes
  // "ES256": ECDSA on P-256 over SHA-256. Anything else is not a signature
  // the library accepts (the emulator's stand-in says so here).
  virtual const char *algorithm() const = 0;
  virtual bool sign(const uint8_t hash[32], uint8_t signature[SIGNATURE_LEN]) = 0;
};

// --------------------------------------------------------------- encoding --

inline size_t base64Encode(const uint8_t *in, size_t size, char *out, size_t outSize) {
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const size_t need = (size + 2) / 3 * 4;
  if (!out || outSize < need + 1) return 0;
  size_t o = 0;
  for (size_t i = 0; i < size; i += 3) {
    const uint32_t a = in[i], b = i + 1 < size ? in[i + 1] : 0, c = i + 2 < size ? in[i + 2] : 0;
    const uint32_t v = a << 16 | b << 8 | c;
    out[o++] = table[v >> 18];
    out[o++] = table[(v >> 12) & 63];
    out[o++] = i + 1 < size ? table[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < size ? table[v & 63] : '=';
  }
  out[o] = '\0';
  return o;
}

// Who a record is from: the first 80 bits of the public key's SHA-256, in hex.
inline void keyId(const uint8_t *publicKey, char out[KEY_ID_LEN]) {
  uint8_t hash[32];
  sha256(publicKey, PUBLIC_KEY_LEN, hash);
  for (int i = 0; i < 10; ++i) snprintf(out + 2 * i, 3, "%02x", hash[i]);
}

// YYYYMMDD as "2026-03-03"; empty for 0.
inline void dateText(uint32_t date, char out[11]) {
  if (date == 0) {
    out[0] = '\0';
    return;
  }
  snprintf(out, 11, "%04u-%02u-%02u", static_cast<unsigned>(date / 10000 % 10000),
           static_cast<unsigned>(date / 100 % 100), static_cast<unsigned>(date % 100));
}

// Seconds since 1970 as "2026-10-10T22:51:50Z"; empty for 0 (clock not set).
inline void timeText(uint32_t epochS, char out[21]) {
  if (epochS == 0) {
    out[0] = '\0';
    return;
  }
  // Days to civil date (H. Hinnant's algorithm), valid for the whole range.
  const uint32_t days = epochS / 86400, rest = epochS % 86400;
  const uint32_t z = days + 719468;
  const uint32_t era = z / 146097, doe = z - era * 146097;
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  const uint32_t day = doy - (153 * mp + 2) / 5 + 1;
  const uint32_t month = mp < 10 ? mp + 3 : mp - 9;
  const uint32_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
  snprintf(out, 21, "%04u-%02u-%02uT%02u:%02u:%02uZ", static_cast<unsigned>(year),
           static_cast<unsigned>(month), static_cast<unsigned>(day),
           static_cast<unsigned>(rest / 3600), static_cast<unsigned>(rest / 60 % 60),
           static_cast<unsigned>(rest % 60));
}

// Writes one JSON object into a fixed buffer, keys in the order given.
// Nothing is written past the end; ok() says whether it all fitted.
class JsonOut {
 public:
  JsonOut(char *buffer, size_t size) : buffer_(buffer), size_(size) {
    if (size_) buffer_[0] = '\0';
  }

  void open(const char *key = nullptr) {
    name(key);
    put('{');
    first_ = true;
  }
  void close() {
    put('}');
    first_ = false;
  }
  void text(const char *key, const char *value) {
    name(key);
    quoted(value);
  }
  // Left out altogether when empty: an absent fact, not an empty one.
  void textIf(const char *key, const char *value) {
    if (value && value[0]) text(key, value);
  }
  void number(const char *key, double value, int decimals) {
    char digits[32];
    if (!isfinite(value)) value = 0.0;
    snprintf(digits, sizeof(digits), "%.*f", decimals, value);
    // 36.00 and 36 are the same number; send the short one.
    if (strchr(digits, '.')) {
      size_t n = strlen(digits);
      while (n > 0 && digits[n - 1] == '0') digits[--n] = '\0';
      if (n > 0 && digits[n - 1] == '.') digits[--n] = '\0';
    }
    if (strcmp(digits, "-0") == 0) strcpy(digits, "0");
    name(key);
    raw(digits);
  }
  void integer(const char *key, long value) {
    char digits[24];
    snprintf(digits, sizeof(digits), "%ld", value);
    name(key);
    raw(digits);
  }

  bool ok() const { return ok_; }
  size_t length() const { return length_; }

 private:
  void put(char c) {
    if (length_ + 1 < size_) {
      buffer_[length_++] = c;
      buffer_[length_] = '\0';
    } else {
      ok_ = false;
    }
  }
  void raw(const char *s) {
    for (; *s; ++s) put(*s);
  }
  void quoted(const char *s) {
    put('"');
    for (; s && *s; ++s) {
      const unsigned char c = static_cast<unsigned char>(*s);
      if (c == '"' || c == '\\') {
        put('\\');
        put(static_cast<char>(c));
      } else if (c < 0x20) {
        char escape[8];
        snprintf(escape, sizeof(escape), "\\u%04x", c);
        raw(escape);
      } else {
        put(static_cast<char>(c));
      }
    }
    put('"');
  }
  void name(const char *key) {
    if (!first_) put(',');
    first_ = false;
    if (key) {
      quoted(key);
      put(':');
    }
  }

  char *buffer_;
  size_t size_;
  size_t length_ = 0;
  bool first_ = true;
  bool ok_ = true;
};

// ---------------------------------------------------------------- records --
// Each returns the body's length, 0 if it did not fit. No names, addresses or
// network details go into any of them.

// The first thing a controller publishes: its public key.
inline size_t buildKeyRecord(const ISigner &signer, uint32_t epochS, char *out, size_t size) {
  char key[96], at[21];
  base64Encode(signer.publicKey(), PUBLIC_KEY_LEN, key, sizeof(key));
  timeText(epochS, at);
  JsonOut j(out, size);
  j.open();
  j.text("t", "key");
  j.integer("v", 1);
  j.textIf("at", at);
  j.text("alg", signer.algorithm());
  j.text("key", key);
  j.close();
  return j.ok() ? j.length() : 0;
}

inline void coffeeFields(JsonOut &j, const Coffee &c) {
  j.integer("local_id", c.id);
  j.textIf("library_id", c.libraryId);  // once it is in the library: which entry this is
  j.text("name", c.name);
  j.textIf("roaster", c.roaster);
  j.textIf("origin", c.origin);
  j.textIf("region", c.region);
  j.textIf("producer", c.producer);
  j.textIf("varietal", c.varietal);
  j.textIf("process", c.process);
  j.textIf("roast_level", roastLevelName(c.roastLevel));
  if (c.altitudeM) j.integer("altitude_m", c.altitudeM);
  j.textIf("notes", c.notes);
}

// A coffee as its owner describes it: a page of the wiki, for a reviewer.
// record: the sender's number for this version of it (its outbox number).
// The library's verdict names it, so a verdict on an older version cannot be
// taken for one on the latest.
inline size_t buildCoffeeRecord(const Coffee &coffee, uint32_t record, uint32_t epochS, char *out,
                                size_t size) {
  char at[21];
  timeText(epochS, at);
  JsonOut j(out, size);
  j.open();
  j.text("t", "coffee");
  j.integer("v", 1);
  j.textIf("at", at);
  j.integer("record", static_cast<long>(record));
  coffeeFields(j, coffee);
  j.close();
  return j.ok() ? j.length() : 0;
}

inline void grinderFields(JsonOut &j, const Grinder &g) {
  j.open("grinder");
  if (g.modelId) j.integer("model_id", g.modelId);
  j.textIf("library_id", g.libraryId);  // its entry in the library's list of grinders
  j.textIf("model", g.model);
  j.textIf("burrs", g.burrs);
  j.text("unit", g.unit);
  if (g.umPerUnit > 0.0f) j.number("um_per_unit", g.umPerUnit, 2);
  // How a stored setting is read (core/SettingFormat.h): only said when it is not whole numbers.
  if (g.divisor > 1) j.integer("divisor", g.divisor);
  if (g.subSteps >= 2) j.integer("sub_steps", g.subSteps);
  j.integer("calibration", g.calibration);
  j.close();
}

inline void recipeFields(JsonOut &j, float doseG, float yieldG, float targetTimeS,
                         uint16_t preinfusionHoldMs, uint16_t bloomPauseMs) {
  j.open("recipe");
  j.number("dose_g", doseG, 1);
  j.number("yield_g", yieldG, 1);
  if (targetTimeS > 0.0f) j.number("target_time_s", targetTimeS, 1);
  if (preinfusionHoldMs) j.number("preinfusion_hold_s", preinfusionHoldMs / 1000.0, 1);
  if (bloomPauseMs) j.number("bloom_pause_s", bloomPauseMs / 1000.0, 1);
  j.close();
}

// Where a preset has settled: its shots at this grind setting are inside the
// brew time tolerance. Only for a preset with a coffee: a recipe brewed with
// none attached says little to anyone else.
inline size_t buildDialinRecord(const Recipe &preset, const Coffee &coffee, const Grinder &grinder,
                                const GrindAdvice &advice, GrindTimeBasis basis, uint32_t epochS,
                                char *out, size_t size) {
  char at[21], roast[11];
  timeText(epochS, at);
  dateText(coffee.roastDate, roast);
  JsonOut j(out, size);
  j.open();
  j.text("t", "dialin");
  j.integer("v", 1);
  j.textIf("at", at);
  j.open("coffee");
  coffeeFields(j, coffee);
  j.close();
  j.textIf("roast_date", roast);
  // Days of the bag's age spent in the freezer, which the library does not count.
  if (const int frozen = frozenDaysAt(coffee.frozenDate, coffee.thawDate, epochS)) {
    j.integer("frozen_days", frozen);
  }
  recipeFields(j, preset.doseG, preset.targetWeightG, preset.targetTimeS,
               preset.preinfusion == Preinfusion::HOLD ? preset.preinfusionHoldMs : 0,
               preset.bloomPauseMs);
  grinderFields(j, grinder);
  j.integer("grind", advice.currentUm);
  j.open("result");
  j.number("time_s", advice.averageTimeS, 1);
  j.text("basis", basis == GrindTimeBasis::POUR ? "pour" : "total");
  j.integer("shots", advice.shotsUsed);
  j.number("min_s", advice.minTimeS, 1);
  j.number("max_s", advice.maxTimeS, 1);
  j.close();
  j.close();
  return j.ok() ? j.length() : 0;
}

// One shot that counts for grind advice. `withTaste`: the owner's rating
// goes along only if they chose to share it.
// How a machine is worked, as far as it says something about a shot made on it.
inline void machineFields(JsonOut &j, const MachineProfile &m) {
  j.open("machine");
  j.text("style", machineStyleName(m.style));
  if (m.startsOnFirstDrip()) j.text("clock_from", "first_drip");  // times are without pre-infusion
  j.textIf("maker", m.maker);
  j.textIf("model", m.model);
  j.textIf("control", m.control);
  j.textIf("library_id", m.libraryId);
  j.close();
}

// machine: the machine the shot was made on; nullptr leaves it out.
inline size_t buildShotRecord(const ShotRecord &s, const Coffee *coffee, const Grinder &grinder,
                              bool withTaste, char *out, size_t size,
                              const MachineProfile *machine = nullptr) {
  char at[21], roast[11];
  timeText(s.epochS, at);
  dateText(s.roastDate, roast);
  JsonOut j(out, size);
  j.open();
  j.text("t", "shot");
  j.integer("v", 1);
  j.integer("id", static_cast<long>(s.id));
  j.textIf("at", at);
  if (coffee) {
    j.open("coffee");
    coffeeFields(j, *coffee);
    j.close();
  } else if (s.coffee[0]) {
    j.open("coffee");  // no longer on the shelf: what the shot itself recorded
    j.text("name", s.coffee);
    j.textIf("region", s.region);
    j.textIf("varietal", s.varietal);
    j.close();
  }
  j.textIf("roast_date", roast);
  // Only for the bag the shot was made from: a coffee since moved on to a new bag has other dates.
  if (coffee && coffee->roastDate == s.roastDate) {
    if (const int frozen = frozenDaysAt(coffee->frozenDate, coffee->thawDate, s.epochS)) {
      j.integer("frozen_days", frozen);
    }
  }
  recipeFields(j, s.doseG, s.targetWeightG, s.targetTimeS, s.preinfusionHoldMs, s.bloomPauseMs);
  grinderFields(j, grinder);
  if (machine) machineFields(j, *machine);
  if (s.grindUm) j.integer("grind", s.grindUm);
  j.open("result");
  j.number("yield_g", s.finalWeightG, 1);
  // What came out for what went in, as brewed (the recipe's is yield over dose as planned).
  if (s.doseG > 0.0f) j.number("ratio", s.finalWeightG / s.doseG, 2);
  j.number("time_s", s.shotTimeS, 1);
  if (s.firstDropS > 0.0f) j.number("first_drop_s", s.firstDropS, 1);
  if (s.pourTimeS > 0.0f) {
    j.number("pour_time_s", s.pourTimeS, 1);
    j.number("pour_flow_gs", s.pourFlowGS, 2);
  }
  if (s.flow50GS > 0.0f) {
    j.number("flow_25_gs", s.flow25GS, 2);
    j.number("flow_50_gs", s.flow50GS, 2);
    j.number("flow_75_gs", s.flow75GS, 2);
  }
  j.text("end", endReasonName(static_cast<EndReason>(s.endReason)));
  j.close();
  if (withTaste && (s.rating || s.taste)) {
    j.open("taste");
    if (s.rating) j.integer("rating", s.rating);
    j.textIf("verdict", tasteName(s.taste));
    j.close();
  }
  j.close();
  return j.ok() ? j.length() : 0;
}

// ------------------------------------------------- where a coffee stands --
// A coffee is a page of the shared library, and pages are reviewed. The
// controller knows the first three states by itself; the last three are told
// to it by the library.

enum class ShareState : uint8_t {
  LOCAL = 0,   // on this controller only
  QUEUED,      // signed and waiting in the outbox
  SENT,        // has left the outbox
  IN_REVIEW,   // the library has it; a reviewer has not decided
  PUBLISHED,   // approved: in everyone's search
  DECLINED,    // not accepted; reviewNote says why
  REFUSED,     // the library's checks turned the record away (reviewNote says why): it may be sent again
};

inline const char *shareStateName(ShareState state) {
  switch (state) {
    case ShareState::QUEUED: return "queued";
    case ShareState::SENT: return "sent";
    case ShareState::IN_REVIEW: return "review";
    case ShareState::PUBLISHED: return "published";
    case ShareState::DECLINED: return "declined";
    case ShareState::REFUSED: return "refused";
    default: return "local";
  }
}

// Of everything a coffee record says about the coffee, and nothing else: the
// bag's roast date and the sharing fields are not part of the description.
inline uint32_t coffeeShareHash(const Coffee &c) {
  Sha256 hash;
  const char *fields[] = {c.name, c.roaster, c.origin, c.region, c.producer, c.varietal,
                          c.process, c.notes};
  for (const char *field : fields) hash.update(field, strlen(field) + 1);
  const uint8_t rest[3] = {c.roastLevel, static_cast<uint8_t>(c.altitudeM >> 8),
                           static_cast<uint8_t>(c.altitudeM)};
  hash.update(rest, sizeof(rest));
  uint8_t out[32];
  hash.finish(out);
  const uint32_t value = static_cast<uint32_t>(out[0]) << 24 | static_cast<uint32_t>(out[1]) << 16 |
                         static_cast<uint32_t>(out[2]) << 8 | out[3];
  return value ? value : 1;  // 0 means "never shared"
}

// outboxSent: records numbered below this have gone (Store::outboxSent()).
inline ShareState coffeeShareState(const Coffee &c, uint32_t outboxSent) {
  const ShareState stored = static_cast<ShareState>(c.shareState);
  if (stored == ShareState::QUEUED || stored == ShareState::SENT) {
    return c.shareSeq < outboxSent ? ShareState::SENT : ShareState::QUEUED;
  }
  return stored;
}

// The owner has edited the description since the version the library has.
inline bool coffeeChangedSinceShared(const Coffee &c) {
  return c.shareState != 0 && coffeeShareHash(c) != c.shareHash;
}

// A coffee taken from the shared library onto this shelf: already published,
// so there is nothing to send, and its dial-ins say which entry they are for.
// Returns its id here; the one already on the shelf if it was taken before;
// 0 if the shelf is full or the library id is missing.
inline uint16_t adoptCoffee(Library &lib, Coffee coffee, const char *libraryId) {
  if (!libraryId || !libraryId[0] || strlen(libraryId) >= LIBRARY_ID_LEN) return 0;
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
    if (strcmp(lib.coffees[i].libraryId, libraryId) == 0) return lib.coffees[i].id;
  }
  coffee.id = 0;
  const uint16_t id = saveCoffee(lib, coffee);
  Coffee *added = findCoffee(lib, id);
  if (!added) return 0;
  added->shareState = static_cast<uint8_t>(ShareState::PUBLISHED);
  added->shareHash = coffeeShareHash(*added);
  copyText(added->libraryId, libraryId, LIBRARY_ID_LEN);
  return id;
}

// Its record has just gone into the outbox as number `seq`.
inline void markCoffeeQueued(Coffee &c, uint32_t seq) {
  c.shareState = static_cast<uint8_t>(ShareState::QUEUED);
  c.shareSeq = seq;
  c.shareHash = coffeeShareHash(c);
  c.reviewNote[0] = '\0';  // any verdict was on an earlier version
}

// A reviewer's verdict on the version of something (a coffee, a grinder)
// numbered `seq`. Only a verdict on the version last sent counts: one about
// an older record has been overtaken by the owner's edit. Never touches what
// the owner typed. true if anything changed.
template <typename T>
inline bool applyReviewTo(T *item, uint32_t seq, ShareState state, const char *note,
                          const char *libraryId) {
  if (state != ShareState::IN_REVIEW && state != ShareState::PUBLISHED &&
      state != ShareState::DECLINED && state != ShareState::REFUSED) {
    return false;
  }
  if (!item || item->shareState == 0 || item->shareSeq != seq) return false;
  char cleanNote[NOTES_LEN] = {}, cleanId[LIBRARY_ID_LEN] = {};
  // Printable ASCII only, as everything else on the controller.
  size_t n = 0;
  for (const char *p = note; p && *p && n + 1 < NOTES_LEN; ++p) {
    if (*p >= 0x20 && *p <= 0x7e) cleanNote[n++] = *p;
  }
  n = 0;
  for (const char *p = libraryId; p && *p && n + 1 < LIBRARY_ID_LEN; ++p) {
    if (*p > 0x20 && *p <= 0x7e) cleanId[n++] = *p;
  }
  if (item->shareState == static_cast<uint8_t>(state) && strcmp(item->reviewNote, cleanNote) == 0 &&
      strcmp(item->libraryId, cleanId) == 0) {
    return false;
  }
  item->shareState = static_cast<uint8_t>(state);
  memcpy(item->reviewNote, cleanNote, NOTES_LEN);
  memcpy(item->libraryId, cleanId, LIBRARY_ID_LEN);
  return true;
}

inline bool applyCoffeeReview(Library &lib, uint16_t localId, uint32_t seq, ShareState state,
                              const char *note, const char *libraryId) {
  return applyReviewTo(findCoffee(lib, localId), seq, state, note, libraryId);
}

// ---------------------------------------------------- a grinder, likewise --
// A grinder that is not a known model can be described to the library the
// way a coffee is: kept here only, or sent and reviewed, so that the next
// owner of one picks it from a list and settings can be compared across
// everyone who has it.

inline Grinder *findGrinder(Library &lib, uint16_t id) {
  if (id == 0) return nullptr;
  if (lib.grinder.id == id) return &lib.grinder;
  if (lib.hasOtherGrinder && lib.otherGrinder.id == id) return &lib.otherGrinder;
  return nullptr;
}

// Of what a grinder record says: what it is and how its settings are read.
// Not when it was zeroed, nor which calibration it is under.
inline uint32_t grinderShareHash(const Grinder &g) {
  Sha256 hash;
  hash.update(g.model, strlen(g.model) + 1);
  hash.update(g.burrs, strlen(g.burrs) + 1);
  hash.update(g.unit, strlen(g.unit) + 1);
  const uint32_t hundredths = static_cast<uint32_t>(g.umPerUnit * 100.0f + 0.5f);
  hash.update(&hundredths, sizeof(hundredths));
  uint8_t out[32];
  hash.finish(out);
  const uint32_t value = static_cast<uint32_t>(out[0]) << 24 | static_cast<uint32_t>(out[1]) << 16 |
                         static_cast<uint32_t>(out[2]) << 8 | out[3];
  return value ? value : 1;
}

inline ShareState grinderShareState(const Grinder &g, uint32_t outboxSent) {
  const ShareState stored = static_cast<ShareState>(g.shareState);
  if (stored == ShareState::QUEUED || stored == ShareState::SENT) {
    return g.shareSeq < outboxSent ? ShareState::SENT : ShareState::QUEUED;
  }
  return stored;
}

inline bool grinderChangedSinceShared(const Grinder &g) {
  return g.shareState != 0 && grinderShareHash(g) != g.shareHash;
}

// Only a grinder the firmware does not already know, and the library has not
// already listed, has anything to tell.
inline bool grinderCanBeShared(const Grinder &g) {
  return g.model[0] != '\0' && g.modelId == 0 &&
         (g.shareState == 0 || g.shareState == static_cast<uint8_t>(ShareState::REFUSED) ||
          grinderChangedSinceShared(g));
}

inline void markGrinderQueued(Grinder &g, uint32_t seq) {
  g.shareState = static_cast<uint8_t>(ShareState::QUEUED);
  g.shareSeq = seq;
  g.shareHash = grinderShareHash(g);
  g.reviewNote[0] = '\0';
}

// A grinder picked from the library's list: published already.
inline void markGrinderFromLibrary(Grinder &g, const char *libraryId) {
  g.shareState = static_cast<uint8_t>(ShareState::PUBLISHED);
  g.shareSeq = 0;
  g.shareHash = grinderShareHash(g);
  g.reviewNote[0] = '\0';
  copyText(g.libraryId, libraryId, LIBRARY_ID_LEN);
}

inline bool applyGrinderReview(Library &lib, uint16_t localId, uint32_t seq, ShareState state,
                               const char *note, const char *libraryId) {
  return applyReviewTo(findGrinder(lib, localId), seq, state, note, libraryId);
}

// A grinder as its owner describes it, for a reviewer.
inline size_t buildGrinderRecord(const Grinder &g, uint32_t record, uint32_t epochS, char *out,
                                 size_t size) {
  char at[21];
  timeText(epochS, at);
  JsonOut j(out, size);
  j.open();
  j.text("t", "grinder");
  j.integer("v", 1);
  j.textIf("at", at);
  j.integer("record", static_cast<long>(record));
  j.integer("local_id", g.id);
  j.text("model", g.model);
  j.textIf("burrs", g.burrs);
  j.text("unit", g.unit);
  if (g.umPerUnit > 0.0f) j.number("um_per_unit", g.umPerUnit, 2);
  if (g.divisor > 1) j.integer("divisor", g.divisor);
  if (g.subSteps >= 2) j.integer("sub_steps", g.subSteps);
  j.close();
  return j.ok() ? j.length() : 0;
}
// Where a machine profile stands, as grinderShareState() for a grinder.
inline ShareState machineShareState(const MachineProfile &m, uint32_t outboxSent) {
  const ShareState stored = static_cast<ShareState>(m.shareState);
  if (stored == ShareState::QUEUED || stored == ShareState::SENT) {
    return m.shareSeq < outboxSent ? ShareState::SENT : ShareState::QUEUED;
  }
  return stored;
}

// A machine as its owner set it up, for a reviewer: which machine, which
// control, and the rules for working it.
inline size_t buildMachineRecord(const MachineProfile &m, uint32_t record, uint32_t epochS, char *out,
                                 size_t size) {
  char at[21];
  timeText(epochS, at);
  JsonOut j(out, size);
  j.open();
  j.text("t", "machine");
  j.integer("v", 1);
  j.textIf("at", at);
  j.integer("record", static_cast<long>(record));
  j.integer("local_id", 1);
  j.text("maker", m.maker);
  j.text("model", m.model);
  j.text("control", m.control[0] ? m.control : "brew control");
  j.text("style", machineStyleName(m.style));
  if (m.machineStyle() == MachineStyle::MANUAL) {
    j.text("manual_start", m.startsOnFirstDrip() ? "first_drip" : "tap");
  }
  j.integer("hold_preinfusion", m.holdPreinfusion);
  j.integer("tap_preinfusion", m.tapPreinfusion);
  j.integer("power_toggle", m.powerToggle);
  if (m.selfStopS) j.integer("self_stop_s", m.selfStopS);
  j.close();
  return j.ok() ? j.length() : 0;
}

// Forgets what has been sent: every coffee and grinder the owner described is
// "on this controller only" again, and no dial-in counts as sent. For when
// what was sent never reached the library, or the controller has a new key
// and the library knows nothing signed with it. What came from the library
// itself (a coffee or grinder picked from its lists) keeps its entry.
inline void forgetSharing(Library &lib) {
  auto forget = [](uint8_t &state, uint32_t &seq, uint32_t &hash, char *note, char *libraryId) {
    const bool fromLibrary = state == static_cast<uint8_t>(ShareState::PUBLISHED) && seq == 0;
    if (fromLibrary) return;
    state = 0;
    seq = hash = 0;
    note[0] = libraryId[0] = '\0';
  };
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
    Coffee &c = lib.coffees[i];
    forget(c.shareState, c.shareSeq, c.shareHash, c.reviewNote, c.libraryId);
  }
  forget(lib.grinder.shareState, lib.grinder.shareSeq, lib.grinder.shareHash,
         lib.grinder.reviewNote, lib.grinder.libraryId);
  if (lib.hasOtherGrinder) {
    Grinder &g = lib.otherGrinder;
    forget(g.shareState, g.shareSeq, g.shareHash, g.reviewNote, g.libraryId);
  }
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    lib.presets[i].sharedGrindUm = lib.presets[i].otherSharedGrindUm = 0;
  }
}

// ---------------------------------------------------------------- sealing --

// Signs `body` and wraps it. Returns the envelope's length; 0 if the signer
// is not ready, refuses, or the result does not fit.
inline size_t sealRecord(ISigner &signer, const char *body, char *out, size_t size) {
  if (!signer.ready() || !body || !body[0]) return 0;
  uint8_t hash[32], signature[SIGNATURE_LEN];
  sha256(body, strlen(body), hash);
  if (!signer.sign(hash, signature)) return 0;
  char kid[KEY_ID_LEN], sig[96];
  keyId(signer.publicKey(), kid);
  base64Encode(signature, SIGNATURE_LEN, sig, sizeof(sig));
  JsonOut j(out, size);
  j.open();
  j.integer("v", 1);
  j.text("kid", kid);
  j.text("alg", signer.algorithm());
  j.text("sig", sig);
  j.text("body", body);
  j.close();
  return j.ok() ? j.length() : 0;
}

}  // namespace bbw
