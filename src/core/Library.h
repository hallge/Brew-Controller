#pragma once
// The library: coffees, recipes and the presets that pair them (Types.h).
// Everything that reads or changes it goes through here, so the display, the
// web page and the brew controller cannot disagree about what a preset is.
//
// Also here, because they produce a Library: reading what earlier builds
// stored (core/Legacy.h) and the byte format the library is saved in.
//
// Portable and host-tested; no flash or UI access in here.

#include <new>
#include <stddef.h>
#include <string.h>

#include "GrinderRegistry.h"
#include "Legacy.h"
#include "SettingFormat.h"
#include "SettingsUtil.h"
#include "Types.h"

namespace bbw {

// ------------------------------------------------------------------ lookup --

inline const Coffee *findCoffee(const Library &lib, uint16_t id) {
  if (id == 0) return nullptr;
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
    if (lib.coffees[i].id == id) return &lib.coffees[i];
  }
  return nullptr;
}
inline Coffee *findCoffee(Library &lib, uint16_t id) {
  return const_cast<Coffee *>(findCoffee(static_cast<const Library &>(lib), id));
}

inline const RecipeDef *findRecipe(const Library &lib, uint16_t id) {
  if (id == 0) return nullptr;
  for (uint8_t i = 0; i < lib.recipeCount; ++i) {
    if (lib.recipes[i].id == id) return &lib.recipes[i];
  }
  return nullptr;
}
inline RecipeDef *findRecipe(Library &lib, uint16_t id) {
  return const_cast<RecipeDef *>(findRecipe(static_cast<const Library &>(lib), id));
}

// Copies and always terminates; the rest of the field is zeroed so that equal
// text gives equal bytes on flash.
// ------------------------------------------------------------------ bags --
inline const Bag *findBag(const Library &lib, uint16_t id) {
  if (id == 0) return nullptr;
  for (uint8_t i = 0; i < lib.bagCount && i < MAX_BAGS; ++i) {
    if (lib.bags[i].id == id) return &lib.bags[i];
  }
  return nullptr;
}

inline Bag *findBag(Library &lib, uint16_t id) {
  return const_cast<Bag *>(findBag(static_cast<const Library &>(lib), id));
}

// The coffee in use mirrors its bag's dates, so code that only needs "the coffee" reads them there.
inline void mirrorBag(Coffee &c, const Bag &b) {
  c.bagId = b.id;
  c.roastDate = b.roastDate;
  c.frozenDate = b.frozenDate;
  c.thawDate = b.thawDate;
}

inline bool bagInUse(const Library &lib, uint16_t id) {
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) if (lib.coffees[i].bagId == id) return true;
  return false;
}

// Makes room for one more: forgets the oldest bag that no coffee has in use. false if there is none.
inline bool dropOldestBag(Library &lib) {
  int oldest = -1;
  for (uint8_t i = 0; i < lib.bagCount; ++i) {
    if (bagInUse(lib, lib.bags[i].id) || lib.bags[i].unopened) continue;
    if (oldest < 0 || lib.bags[i].id < lib.bags[oldest].id) oldest = i;
  }
  if (oldest < 0) return false;
  for (uint8_t i = static_cast<uint8_t>(oldest); i + 1 < lib.bagCount; ++i) lib.bags[i] = lib.bags[i + 1];
  --lib.bagCount;
  return true;
}

// A new bag record; 0 if the list is full of bags in use.
inline uint16_t addBag(Library &lib, uint16_t coffeeId, uint32_t roastDate, uint32_t openedDate,
                       uint32_t frozenDate, uint32_t thawDate, uint16_t weightG) {
  if (lib.bagCount >= MAX_BAGS && !dropOldestBag(lib)) return 0;
  Bag &b = lib.bags[lib.bagCount++];
  b = Bag{};
  b.id = lib.nextBagId++;
  if (lib.nextBagId == 0) lib.nextBagId = 1;
  b.coffeeId = coffeeId;
  b.roastDate = validRoastDate(roastDate) ? roastDate : 0;
  b.openedDate = validRoastDate(openedDate) ? openedDate : 0;
  b.frozenDate = validRoastDate(frozenDate) ? frozenDate : 0;
  b.thawDate = validRoastDate(thawDate) ? thawDate : 0;
  b.weightG = weightG;
  return b.id;
}

// A coffee's record was edited, or made: the bag in use follows what it now says (a correction of the
// same bag, not a new one), or is made from it if the coffee had none. A coffee with no dates at all
// has no bag until it has something to put in one.
inline void syncBagFromCoffee(Library &lib, Coffee &c) {
  Bag *b = c.bagId ? findBag(lib, c.bagId) : nullptr;
  if (b && b->coffeeId != c.id) b = nullptr;
  if (!b) {
    if (c.roastDate == 0 && c.frozenDate == 0 && c.thawDate == 0) {
      c.bagId = 0;
      return;
    }
    c.bagId = addBag(lib, c.id, c.roastDate, 0, c.frozenDate, c.thawDate, 0);
    return;
  }
  b->roastDate = c.roastDate;
  b->frozenDate = c.frozenDate;
  b->thawDate = c.thawDate;
}

// A new bag of a coffee, in use from now: the dates of the last one stay with it, the coffee's
// mirror starts fresh (not frozen). Returns the new bag's id, 0 if the coffee is not there or the
// list is full of bags in use.
inline uint16_t startBag(Library &lib, uint16_t coffeeId, uint32_t roastDate, uint32_t openedDate = 0,
                         uint16_t weightG = 0) {
  Coffee *c = nullptr;
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) if (lib.coffees[i].id == coffeeId) c = &lib.coffees[i];
  if (!c) return 0;
  // Keep the bag it had: a coffee edited without ever having had a bag gets one first.
  syncBagFromCoffee(lib, *c);
  const uint16_t id = addBag(lib, coffeeId, roastDate, openedDate, 0, 0, weightG);
  if (id == 0) return 0;
  mirrorBag(*c, *findBag(lib, id));
  c->finishedDate = 0;  // back on the shelf: its presets show again, as they were
  return id;
}

// Puts an earlier bag of the coffee back in use.
inline bool useBag(Library &lib, uint16_t coffeeId, uint16_t bagId) {
  Bag *b = findBag(lib, bagId);
  if (!b || b->coffeeId != coffeeId) return false;
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
    if (lib.coffees[i].id == coffeeId) {
      mirrorBag(lib.coffees[i], *b);
      lib.coffees[i].finishedDate = 0;  // a bag in use again: back on the shelf
      return true;
    }
  }
  return false;
}

// Forgets a bag that is not in use. Its shots stay in the history, matched by their roast date.
inline bool removeBag(Library &lib, uint16_t bagId) {
  if (bagInUse(lib, bagId)) return false;
  for (uint8_t i = 0; i < lib.bagCount; ++i) {
    if (lib.bags[i].id != bagId) continue;
    for (uint8_t j = i; j + 1 < lib.bagCount; ++j) lib.bags[j] = lib.bags[j + 1];
    --lib.bagCount;
    return true;
  }
  return false;
}

// The bag record in use of a coffee, edited in place: dates, weight. false if there is none.
inline bool editBag(Library &lib, uint16_t bagId, uint32_t roastDate, uint32_t openedDate,
                    uint32_t frozenDate, uint32_t thawDate, uint16_t weightG) {
  Bag *b = findBag(lib, bagId);
  if (!b) return false;
  b->roastDate = validRoastDate(roastDate) ? roastDate : 0;
  b->openedDate = validRoastDate(openedDate) ? openedDate : 0;
  b->frozenDate = validRoastDate(frozenDate) ? frozenDate : 0;
  b->thawDate = validRoastDate(thawDate) ? thawDate : 0;
  if (b->frozenDate != 0 && b->roastDate != 0 && b->frozenDate < b->roastDate) b->frozenDate = 0;
  if (b->frozenDate == 0) b->thawDate = 0;
  if (b->thawDate != 0 && b->thawDate < b->frozenDate) b->thawDate = 0;
  b->weightG = weightG;
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
    if (lib.coffees[i].bagId == bagId) mirrorBag(lib.coffees[i], *b);
  }
  return true;
}

// ---------------------------------------------------------- bag finished --
// A coffee whose bag is used up comes off the controller's list without being deleted: its presets
// are hidden, everything learned on them stays, and a new bag (startBag, or useBag) brings them back
// exactly as they were. Presets with no coffee are always shown.

// Stored when the controller's clock is not set: finished, just not known when.
constexpr uint32_t FINISHED_DATE_UNKNOWN = 20000101;

// Seconds since 1970 (UTC) as YYYYMMDD; 0 for 0 (clock not set).
inline uint32_t dateFromEpoch(uint32_t epochS) {
  if (epochS == 0) return 0;
  // Days to civil date (H. Hinnant's algorithm), as core/Share.h timeText.
  const uint32_t z = epochS / 86400 + 719468;
  const uint32_t era = z / 146097, doe = z - era * 146097;
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  const uint32_t day = doy - (153 * mp + 2) / 5 + 1;
  const uint32_t month = mp < 10 ? mp + 3 : mp - 9;
  const uint32_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
  return year * 10000 + month * 100 + day;
}

// ---------------------------------------------------------- unopened bags --
// The unopened bag of a coffee (0: of any coffee) with the oldest roast date; a bag with no roast
// date comes after those, and ties keep list order. nullptr if there is none.
inline const Bag *oldestUnopenedBag(const Library &lib, uint16_t coffeeId) {
  const Bag *best = nullptr;
  for (uint8_t i = 0; i < lib.bagCount && i < MAX_BAGS; ++i) {
    const Bag &b = lib.bags[i];
    if (b.unopened != BAG_UNOPENED || (coffeeId != 0 && b.coffeeId != coffeeId)) continue;
    const uint32_t key = b.roastDate ? b.roastDate : 0xFFFFFFFFu;
    if (!best || key < (best->roastDate ? best->roastDate : 0xFFFFFFFFu)) best = &b;
  }
  return best;
}

// A coffee with no bag to brew from: none in use, or the one in use is finished.
inline bool coffeeWaiting(const Coffee &c) { return c.bagId == 0 || c.finishedDate != 0; }

// The roast date that decides which bag on the shelf goes first: the bag in use, or for a coffee
// waiting on an unopened bag, that bag's. 0 if not known.
inline uint32_t shelfRoastDate(const Library &lib, const Coffee &c) {
  if (coffeeWaiting(c)) {
    const Bag *u = oldestUnopenedBag(lib, c.id);
    if (u) return u->roastDate;
  }
  return c.roastDate;
}

// A new bag on the shelf, not opened: the coffee keeps the bag it has in use. 0 if the list is full
// of bags in use and unopened ones, or there is no such coffee.
inline uint16_t addUnopenedBag(Library &lib, uint16_t coffeeId, uint32_t roastDate, uint16_t weightG,
                               uint16_t subscriptionId = 0) {
  if (!findCoffee(lib, coffeeId)) return 0;
  const uint16_t id = addBag(lib, coffeeId, roastDate, 0, 0, 0, weightG);
  if (id == 0) return 0;
  Bag *b = findBag(lib, id);
  b->unopened = 1;
  b->subscriptionId = subscriptionId;
  return id;
}

// An unopened bag is put in use: opened `today` (YYYYMMDD; 0 when the clock is not set), the coffee
// mirrors it and is back on the shelf. Tracking (bag age, the run of shots for grind advice) starts
// from here. false if it is not an unopened bag.
inline bool openBag(Library &lib, uint16_t bagId, uint32_t today) {
  Bag *b = findBag(lib, bagId);
  Coffee *c = b ? findCoffee(lib, b->coffeeId) : nullptr;
  if (!b || !c || b->unopened != BAG_UNOPENED) return false;
  b->unopened = 0;
  b->expectedDate = 0;
  if (b->openedDate == 0 && validRoastDate(today)) b->openedDate = today;
  mirrorBag(*c, *b);
  c->finishedDate = 0;
  return true;
}

// A bag ordered and not yet come (a subscription's next delivery): on its way, expected on `expected`
// (YYYYMMDD, 0 = not known). It is not on the shelf: it is never opened, never keeps a finished coffee
// on the shelf, and is never dropped to make room. 0 if there is no such coffee or no room.
inline uint16_t addOnTheWayBag(Library &lib, uint16_t coffeeId, uint32_t expected, uint16_t weightG,
                               uint16_t subscriptionId = 0) {
  if (!findCoffee(lib, coffeeId)) return 0;
  const uint16_t id = addBag(lib, coffeeId, 0, 0, 0, 0, weightG);
  if (id == 0) return 0;
  Bag *b = findBag(lib, id);
  b->unopened = BAG_ON_ITS_WAY;
  b->subscriptionId = subscriptionId;
  b->expectedDate = validRoastDate(expected) ? expected : 0;
  return id;
}

// A bag on its way came: it is on the shelf, unopened, with the roast date printed on it (0 = not
// entered yet) and its weight (0 = keep). false if it was not on its way.
inline bool bagArrived(Library &lib, uint16_t bagId, uint32_t roastDate, uint16_t weightG) {
  Bag *b = findBag(lib, bagId);
  if (!b || b->unopened != BAG_ON_ITS_WAY) return false;
  b->unopened = BAG_UNOPENED;
  b->expectedDate = 0;
  if (validRoastDate(roastDate)) b->roastDate = roastDate;
  if (weightG) b->weightG = weightG;
  return true;
}

inline bool coffeeFinished(const Library &lib, uint16_t coffeeId) {
  const Coffee *c = coffeeId != 0 ? findCoffee(lib, coffeeId) : nullptr;
  // A finished coffee with an unopened bag waiting is still on the shelf.
  return c && c->finishedDate != 0 && !oldestUnopenedBag(lib, coffeeId);
}

// false for an index out of range, or a preset whose coffee is finished.
inline bool presetVisible(const Library &lib, uint8_t index) {
  return index < lib.presetCount && !coffeeFinished(lib, lib.presets[index].coffeeId);
}

// The shown preset to brew next: the one whose coffee was roasted longest ago (the bag to use up
// first). Presets with no coffee, or a coffee with no roast date, come after those, and ties keep
// list order. `skip` is left out (-1: none). -1 if no other preset is shown.
inline int nextPresetByRoastAge(const Library &lib, int skip = -1) {
  int best = -1;
  uint32_t bestKey = 0;
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    if (static_cast<int>(i) == skip || !presetVisible(lib, i)) continue;
    const Coffee *c = lib.presets[i].coffeeId != 0 ? findCoffee(lib, lib.presets[i].coffeeId) : nullptr;
    const uint32_t roast = c ? shelfRoastDate(lib, *c) : 0;
    const uint32_t key = roast != 0 ? roast : 0xFFFFFFFFu;
    if (best < 0 || key < bestKey) {
      best = i;
      bestKey = key;
    }
  }
  return best;
}

// The next shown preset after `from` in list order, wrapping round; `from` itself if no other is
// shown (BOOT's "next recipe" and the controller's list step through these).
inline uint8_t nextShownPreset(const Library &lib, uint8_t from) {
  for (uint8_t k = 1; k <= lib.presetCount; ++k) {
    const uint8_t i = static_cast<uint8_t>((from + k) % lib.presetCount);
    if (presetVisible(lib, i)) return i;
  }
  return from;
}

// The active preset hidden (its coffee finished): the next one by roast age is selected. With every
// preset hidden it stays where it is: Start always needs something to brew.
inline void keepActivePresetShown(Library &lib) {
  if (lib.presetCount == 0 || presetVisible(lib, lib.activePreset)) return;
  const int next = nextPresetByRoastAge(lib);
  if (next >= 0) lib.activePreset = static_cast<uint8_t>(next);
}

// The bag of a coffee is finished: off the shelf from `today` (YYYYMMDD; 0 or not a date when the
// clock is not set). A coffee finished already keeps its date. false if there is no such coffee.
inline bool openActiveCoffeeBag(Library &lib, uint32_t today);
inline bool finishCoffee(Library &lib, uint16_t coffeeId, uint32_t today) {
  Coffee *c = coffeeId != 0 ? findCoffee(lib, coffeeId) : nullptr;
  if (!c) return false;
  // Another bag of the same coffee waiting unopened: it is the next one, in use from today.
  const Bag *same = oldestUnopenedBag(lib, coffeeId);
  if (same) return openBag(lib, same->id, today);
  if (c->finishedDate == 0) c->finishedDate = validRoastDate(today) ? today : FINISHED_DATE_UNKNOWN;
  // The next bag by roast date may be an unopened bag of a coffee with nothing on the controller's
  // list: it gets an entry with the recipe just used, if the list has room (else it waits).
  const Bag *next = oldestUnopenedBag(lib, 0);
  if (next) {
    bool listed = false;
    for (uint8_t i = 0; i < lib.presetCount && !listed; ++i) listed = lib.presets[i].coffeeId == next->coffeeId;
    const int other = nextPresetByRoastAge(lib);
    const Coffee *oc = other >= 0 && lib.presets[other].coffeeId ? findCoffee(lib, lib.presets[other].coffeeId) : nullptr;
    const uint32_t otherRoast = oc ? shelfRoastDate(lib, *oc) : 0;
    const uint32_t nextRoast = next->roastDate ? next->roastDate : 0xFFFFFFFFu;
    const bool first = other < 0 || nextRoast < (otherRoast ? otherRoast : 0xFFFFFFFFu);
    if (!listed && first && lib.presetCount < MAX_PRESETS && lib.activePreset < lib.presetCount) {
      Preset p = lib.presets[lib.activePreset];
      const RecipeDef *d = findRecipe(lib, p.recipeId);
      Preset made = Preset{};
      made.recipeId = p.recipeId;
      made.coffeeId = next->coffeeId;
      made.stopOffsetG = d ? d->baselineOffsetG : p.stopOffsetG;
      lib.presets[lib.presetCount] = made;
      lib.activePreset = lib.presetCount++;
    }
  }
  keepActivePresetShown(lib);
  openActiveCoffeeBag(lib, today);
  return true;
}

// The coffee chosen (the active preset's) has no bag to brew from but an unopened one waiting: the
// one roasted first is opened. Choosing it is putting it in use. true if a bag was opened.
inline bool openActiveCoffeeBag(Library &lib, uint32_t today) {
  if (lib.activePreset >= lib.presetCount) return false;
  Coffee *c = findCoffee(lib, lib.presets[lib.activePreset].coffeeId);
  if (!c || !coffeeWaiting(*c)) return false;
  const Bag *u = oldestUnopenedBag(lib, c->id);
  return u && openBag(lib, u->id, today);
}

inline void copyText(char *to, const char *from, size_t size) {
  strncpy(to, from, size - 1);
  to[size - 1] = '\0';
}

// ---------------------------------------------- a preset as the flat Recipe --

inline void recipeDefToFlat(const RecipeDef &d, Recipe &r) {
  copyText(r.name, d.name, RECIPE_NAME_LEN);
  r.targetWeightG = d.targetWeightG;
  r.useRatio = d.useRatio;
  r.doseG = d.doseG;
  r.ratio = d.ratio;
  r.targetTimeS = d.targetTimeS;
  r.preinfusion = d.preinfusion;
  r.preinfusionHoldMs = d.preinfusionHoldMs;
  r.timeLimit = d.timeLimit;
  r.maxShotTimeS = d.maxShotTimeS;
  r.baselineOffsetG = d.baselineOffsetG;
  r.bloomPauseMs = d.bloomPauseMs;
  r.recipeId = d.id;
}

inline void flatToRecipeDef(const Recipe &r, RecipeDef &d) {
  copyText(d.name, r.name, RECIPE_NAME_LEN);
  d.targetWeightG = r.targetWeightG;
  d.useRatio = r.useRatio;
  d.doseG = r.doseG;
  d.ratio = r.ratio;
  d.targetTimeS = r.targetTimeS;
  d.preinfusion = r.preinfusion;
  d.preinfusionHoldMs = r.preinfusionHoldMs;
  d.timeLimit = r.timeLimit;
  d.maxShotTimeS = r.maxShotTimeS;
  d.baselineOffsetG = r.baselineOffsetG;
  d.bloomPauseMs = r.bloomPauseMs;
}

// The flat Recipe carries five of a coffee's fields: the ones shown on the
// controller and copied into every shot.
inline void flatToCoffee(const Recipe &r, Coffee &c) {
  copyText(c.name, r.coffee, COFFEE_NAME_LEN);
  copyText(c.region, r.region, ORIGIN_LEN);
  copyText(c.varietal, r.varietal, ORIGIN_LEN);
  c.roastLevel = r.roastLevel;
  c.roastDate = r.roastDate;
}

// Preset `index` with its recipe and coffee filled in. An index out of range
// gives the first preset; an empty library gives the defaults.
inline Recipe resolvePreset(const Library &lib, uint8_t index) {
  Recipe r;
  if (lib.presetCount == 0) return r;
  const Preset &p = lib.presets[index < lib.presetCount ? index : 0];
  if (const RecipeDef *d = findRecipe(lib, p.recipeId)) recipeDefToFlat(*d, r);
  if (const Coffee *c = findCoffee(lib, p.coffeeId)) {
    copyText(r.coffee, c->name, COFFEE_NAME_LEN);
    copyText(r.region, c->region, ORIGIN_LEN);
    copyText(r.varietal, c->varietal, ORIGIN_LEN);
    r.roastLevel = c->roastLevel;
    r.roastDate = c->roastDate;
    r.coffeeId = c->id;
    r.bagId = c->bagId;
  }
  r.grindUm = p.grindUm;
  r.stopOffsetG = p.stopOffsetG;
  r.learnedAlpha = p.learnedAlpha;
  return r;
}

inline bool flatHasCoffee(const Recipe &r) {
  return r.coffee[0] || r.region[0] || r.varietal[0] || r.roastLevel || r.roastDate;
}

inline bool flatRecipeDiffers(const Recipe &a, const Recipe &b) {
  return strcmp(a.name, b.name) != 0 || a.targetWeightG != b.targetWeightG ||
         a.useRatio != b.useRatio || a.doseG != b.doseG || a.ratio != b.ratio ||
         a.targetTimeS != b.targetTimeS || a.preinfusion != b.preinfusion ||
         a.preinfusionHoldMs != b.preinfusionHoldMs || a.timeLimit != b.timeLimit ||
         a.maxShotTimeS != b.maxShotTimeS || a.baselineOffsetG != b.baselineOffsetG ||
         a.bloomPauseMs != b.bloomPauseMs;
}

inline bool flatCoffeeDiffers(const Recipe &a, const Recipe &b) {
  return strcmp(a.coffee, b.coffee) != 0 || strcmp(a.region, b.region) != 0 ||
         strcmp(a.varietal, b.varietal) != 0 || a.roastLevel != b.roastLevel ||
         a.roastDate != b.roastDate;
}

// ---------------------------------------------------------------- sanitize --

inline void sanitizeCoffee(Coffee &c) {
  c.name[COFFEE_NAME_LEN - 1] = c.roaster[COFFEE_NAME_LEN - 1] = '\0';
  c.origin[COFFEE_NAME_LEN - 1] = '\0';
  c.region[ORIGIN_LEN - 1] = c.producer[ORIGIN_LEN - 1] = c.varietal[ORIGIN_LEN - 1] = '\0';
  c.process[PROCESS_LEN - 1] = c.notes[NOTES_LEN - 1] = '\0';
  if (c.altitudeM > 4000) c.altitudeM = 0;
  if (c.roastLevel > static_cast<uint8_t>(RoastLevel::DARK)) c.roastLevel = 0;
  if (!validRoastDate(c.roastDate)) c.roastDate = 0;
  // Frozen and thawed dates only mean something in order, and after the roast.
  if (!validRoastDate(c.frozenDate)) c.frozenDate = 0;
  if (!validRoastDate(c.thawDate)) c.thawDate = 0;
  if (c.frozenDate != 0 && c.roastDate != 0 && c.frozenDate < c.roastDate) c.frozenDate = 0;
  if (c.frozenDate == 0) c.thawDate = 0;
  if (c.thawDate != 0 && c.thawDate < c.frozenDate) c.thawDate = 0;
  c.reviewNote[NOTES_LEN - 1] = c.libraryId[LIBRARY_ID_LEN - 1] = '\0';
  if (c.shareState > 6) c.shareState = 0;  // ShareState::REFUSED
  if (!validRoastDate(c.finishedDate)) c.finishedDate = 0;
}

inline void sanitizeRecipeDef(RecipeDef &d) {
  Recipe flat;
  recipeDefToFlat(d, flat);
  sanitizeRecipe(flat);  // one set of limits for both forms
  flatToRecipeDef(flat, d);
}

inline uint16_t addRecipeDef(Library &lib, const Recipe &from) {
  if (lib.recipeCount >= MAX_RECIPES) return 0;
  RecipeDef &d = lib.recipes[lib.recipeCount++];
  d = RecipeDef{};
  flatToRecipeDef(from, d);
  d.id = lib.nextRecipeId++;
  return d.id;
}

inline uint16_t addCoffee(Library &lib, const Recipe &from) {
  if (lib.coffeeCount >= MAX_COFFEES) return 0;
  Coffee &c = lib.coffees[lib.coffeeCount++];
  c = Coffee{};
  flatToCoffee(from, c);
  c.id = lib.nextCoffeeId++;
  syncBagFromCoffee(lib, c);
  return c.id;
}

// A coffee whose name, region, varietal, roast level and roast date all match.
inline uint16_t findSameCoffee(const Library &lib, const Recipe &r) {
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
    const Coffee &c = lib.coffees[i];
    if (strcmp(c.name, r.coffee) == 0 && strcmp(c.region, r.region) == 0 &&
        strcmp(c.varietal, r.varietal) == 0 && c.roastLevel == r.roastLevel &&
        c.roastDate == r.roastDate) {
      return c.id;
    }
  }
  return 0;
}

// A unit typed as microns in any of the usual ways ("um", "1um", "1 µm", "μm", "microns") is
// stored as "um", so that a setting reads "85 um" and not "85 1um", and the controller's
// font, which has no µ, can show it. Any other unit is kept as typed.
inline void tidyGrinderUnit(char *unit) {
  const char *p = unit;
  while (*p >= '0' && *p <= '9') ++p;
  while (*p == ' ') ++p;
  char rest[GRINDER_UNIT_LEN];
  size_t n = 0;
  for (; *p && n < sizeof(rest) - 1; ++p) rest[n++] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
  rest[n] = '\0';
  if (strcmp(rest, "um") == 0 || strcmp(rest, "\xC2\xB5m") == 0 || strcmp(rest, "\xCE\xBCm") == 0 ||
      strcmp(rest, "micron") == 0 || strcmp(rest, "microns") == 0) {
    copyText(unit, "um", GRINDER_UNIT_LEN);
  }
}

inline void sanitizeGrinder(Grinder &g) {
  g.model[GRINDER_NAME_LEN - 1] = g.burrs[GRINDER_BURRS_LEN - 1] = '\0';
  g.unit[GRINDER_UNIT_LEN - 1] = '\0';
  tidyGrinderUnit(g.unit);
  if (g.unit[0] == '\0') copyText(g.unit, "um", GRINDER_UNIT_LEN);
  g.umPerUnit = clampTo(finiteOr(g.umPerUnit, 0.0f), 0.0f, 1000.0f);
  sanitizeSettingFormat(g);
  if (!validRoastDate(g.zeroDate)) g.zeroDate = 0;
  g.reviewNote[NOTES_LEN - 1] = g.libraryId[LIBRARY_ID_LEN - 1] = '\0';
  if (g.shareState > 6) g.shareState = 0;  // ShareState::REFUSED
}

// A model from the bundled registry brings its own name, unit and microns
// per unit; burrs can still be given (other burrs in the same grinder).
inline void normalizeGrinder(Grinder &g) {
  sanitizeGrinder(g);
  if (const GrinderModel *m = findGrinderModel(g.modelId)) {
    copyText(g.model, m->model, GRINDER_NAME_LEN);
    copyText(g.unit, m->unit, GRINDER_UNIT_LEN);
    g.umPerUnit = m->umPerUnit;
    if (m->subSteps >= 2) {
      g.subSteps = m->subSteps;
      g.divisor = 1;
    } else if (m->divisor > 1) {
      g.divisor = m->divisor;
      g.subSteps = 0;
    }
    if (g.burrs[0] == '\0') copyText(g.burrs, m->burrs, GRINDER_BURRS_LEN);
  } else {
    g.modelId = 0;
  }
}

// Where a grinder stands with the shared library, set from the outside:
// nothing typed into a form may claim it.
inline void clearGrinderSharing(Grinder &g) {
  g.shareState = 0;
  g.shareSeq = g.shareHash = 0;
  g.reviewNote[0] = g.libraryId[0] = '\0';
}

// Changes the grinder in use. Anything that changes what a setting means
// starts a new calibration: another model, other burrs, another unit, or
// `rezeroed`. Naming it for the first time does not: it is the grinder the
// shots so far were made on.
inline void setGrinder(Library &lib, Grinder in, bool rezeroed) {
  normalizeGrinder(in);
  const Grinder &was = lib.grinder;
  const bool firstNaming = was.model[0] == '\0' && was.modelId == 0;
  // A different divisor or ring count changes what every stored number means, as another unit does.
  const bool unitChanged = strcmp(in.unit, was.unit) != 0 || in.divisor != was.divisor ||
                           in.subSteps != was.subSteps;
  const bool changed = strcmp(in.model, was.model) != 0 || strcmp(in.burrs, was.burrs) != 0 || unitChanged;
  in.id = was.id;
  in.calibration = was.calibration;
  if (rezeroed || (changed && !firstNaming)) in.calibration = lib.nextCalibration++;
  else if (in.zeroDate == 0) in.zeroDate = was.zeroDate;
  // Read in another unit, the numbers entered so far mean nothing.
  if (!firstNaming && unitChanged) {
    for (uint8_t i = 0; i < lib.presetCount; ++i) lib.presets[i].grindUm = 0;
  }
  // The same grinder keeps its standing with the library; another model is
  // another grinder and starts with none.
  if (strcmp(in.model, was.model) == 0) {
    in.shareState = was.shareState;
    in.shareSeq = was.shareSeq;
    in.shareHash = was.shareHash;
    memcpy(in.reviewNote, was.reviewNote, NOTES_LEN);
    memcpy(in.libraryId, was.libraryId, LIBRARY_ID_LEN);
  } else {
    clearGrinderSharing(in);
  }
  lib.grinder = in;
}

// The owner's second grinder: kept beside the one in use, with its own
// setting for every preset. false if there is one already.
inline bool addOtherGrinder(Library &lib, Grinder in) {
  if (lib.hasOtherGrinder) return false;
  normalizeGrinder(in);
  if (in.model[0] == '\0') return false;
  clearGrinderSharing(in);
  in.id = lib.nextGrinderId++;
  in.calibration = lib.nextCalibration++;
  lib.otherGrinder = in;
  lib.hasOtherGrinder = true;
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    lib.presets[i].otherGrindUm = lib.presets[i].otherSharedGrindUm = 0;
  }
  return true;
}

// Puts the other grinder in use. Each preset's setting for it comes with it,
// and the settings for the one put aside are kept for when it comes back.
// Nothing about either grinder's calibration changes: its shots are still
// its shots.
inline bool swapGrinders(Library &lib) {
  if (!lib.hasOtherGrinder) return false;
  const Grinder aside = lib.grinder;
  lib.grinder = lib.otherGrinder;
  lib.otherGrinder = aside;
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    Preset &p = lib.presets[i];
    const uint16_t grind = p.grindUm, shared = p.sharedGrindUm;
    p.grindUm = p.otherGrindUm;
    p.sharedGrindUm = p.otherSharedGrindUm;
    p.otherGrindUm = grind;
    p.otherSharedGrindUm = shared;
  }
  return true;
}

inline bool removeOtherGrinder(Library &lib) {
  if (!lib.hasOtherGrinder) return false;
  lib.hasOtherGrinder = false;
  new (&lib.otherGrinder) Grinder();
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    lib.presets[i].otherGrindUm = lib.presets[i].otherSharedGrindUm = 0;
  }
  return true;
}
// Empties a library where it stands. A Library is several kilobytes: on the
// controller it is never copied through a temporary on a task's stack.
inline void resetLibrary(Library &lib) { new (&lib) Library(); }

// Makes any Library safe to use: counts in range, every id unique and
// non-zero, no preset pointing at a recipe that is not there, at least one
// recipe and one preset.
inline void sanitizeLibrary(Library &lib) {
  if (lib.coffeeCount > MAX_COFFEES) lib.coffeeCount = MAX_COFFEES;
  if (lib.recipeCount > MAX_RECIPES) lib.recipeCount = MAX_RECIPES;
  if (lib.presetCount > MAX_PRESETS) lib.presetCount = MAX_PRESETS;
  if (lib.nextCoffeeId == 0) lib.nextCoffeeId = 1;
  if (lib.nextRecipeId == 0) lib.nextRecipeId = 1;
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
    if (lib.coffees[i].id >= lib.nextCoffeeId) lib.nextCoffeeId = lib.coffees[i].id + 1;
  }
  for (uint8_t i = 0; i < lib.recipeCount; ++i) {
    if (lib.recipes[i].id >= lib.nextRecipeId) lib.nextRecipeId = lib.recipes[i].id + 1;
  }
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
    Coffee &c = lib.coffees[i];
    sanitizeCoffee(c);
    bool taken = c.id == 0;
    for (uint8_t j = 0; j < i && !taken; ++j) taken = lib.coffees[j].id == c.id;
    if (taken) c.id = lib.nextCoffeeId++;
  }
  for (uint8_t i = 0; i < lib.recipeCount; ++i) {
    RecipeDef &d = lib.recipes[i];
    sanitizeRecipeDef(d);
    bool taken = d.id == 0;
    for (uint8_t j = 0; j < i && !taken; ++j) taken = lib.recipes[j].id == d.id;
    if (taken) d.id = lib.nextRecipeId++;
  }
  // Bags: every id unique and non-zero, each one of a coffee that is there; each coffee's bag in use is
  // one of its own, its dates mirrored from it, or made from them (a library saved before bags).
  if (lib.bagCount > MAX_BAGS) lib.bagCount = MAX_BAGS;
  if (lib.nextBagId == 0) lib.nextBagId = 1;
  for (uint8_t i = 0; i < lib.bagCount; ++i) {
    if (lib.bags[i].id >= lib.nextBagId) lib.nextBagId = lib.bags[i].id + 1;
  }
  {
    uint8_t keep = 0;
    for (uint8_t i = 0; i < lib.bagCount; ++i) {
      Bag b = lib.bags[i];
      bool taken = b.id == 0 || !findCoffee(lib, b.coffeeId);
      for (uint8_t j = 0; j < keep && !taken; ++j) taken = lib.bags[j].id == b.id;
      if (taken) continue;
      if (!validRoastDate(b.roastDate)) b.roastDate = 0;
      if (!validRoastDate(b.openedDate)) b.openedDate = 0;
      if (!validRoastDate(b.frozenDate)) b.frozenDate = 0;
      if (!validRoastDate(b.thawDate)) b.thawDate = 0;
      if (b.frozenDate == 0) b.thawDate = 0;
      if (b.unopened > BAG_ON_ITS_WAY) b.unopened = 0;
      if (b.unopened != BAG_ON_ITS_WAY || !validRoastDate(b.expectedDate)) b.expectedDate = 0;
      b.spare = 0;
      lib.bags[keep++] = b;
    }
    lib.bagCount = keep;
  }
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
    Coffee &c = lib.coffees[i];
    const Bag *b = findBag(lib, c.bagId);
    if (b && b->coffeeId == c.id && b->unopened) {  // in use: opened
      findBag(lib, c.bagId)->unopened = 0;
      findBag(lib, c.bagId)->expectedDate = 0;
    }
    if (b && b->coffeeId == c.id) mirrorBag(c, *b);
    else {
      c.bagId = 0;
      syncBagFromCoffee(lib, c);
    }
  }
  uint8_t kept = 0;
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    Preset p = lib.presets[i];
    const RecipeDef *d = findRecipe(lib, p.recipeId);
    if (!d) {
      if (lib.activePreset > kept) --lib.activePreset;  // keep pointing at the same one
      continue;
    }
    if (!findCoffee(lib, p.coffeeId)) p.coffeeId = 0;
    if (p.grindUm > 2000) p.grindUm = 0;
    if (p.learnedAlpha > 100) p.learnedAlpha = 0;
    p.stopOffsetG = clampTo(finiteOr(p.stopOffsetG, d->baselineOffsetG), 0.0f, MAX_OFFSET_G);
    lib.presets[kept++] = p;
  }
  lib.presetCount = kept;
  if (lib.recipeCount == 0) {
    lib.recipes[0] = RecipeDef{};
    lib.recipes[0].id = lib.nextRecipeId++;
    lib.recipeCount = 1;
  }
  if (lib.presetCount == 0) {
    lib.presets[0] = Preset{};
    lib.presets[0].recipeId = lib.recipes[0].id;
    lib.presets[0].stopOffsetG = lib.recipes[0].baselineOffsetG;
    lib.presetCount = 1;
  }
  if (lib.activePreset >= lib.presetCount) lib.activePreset = 0;
  keepActivePresetShown(lib);
  sanitizeGrinder(lib.grinder);
  if (lib.hasOtherGrinder) sanitizeGrinder(lib.otherGrinder);
  else new (&lib.otherGrinder) Grinder();
  // Each grinder has an id of its own, and no two calibrations share a number.
  if (lib.nextGrinderId == 0) lib.nextGrinderId = 1;
  if (lib.grinder.id >= lib.nextGrinderId) lib.nextGrinderId = lib.grinder.id + 1;
  if (lib.hasOtherGrinder && lib.otherGrinder.id >= lib.nextGrinderId) {
    lib.nextGrinderId = lib.otherGrinder.id + 1;
  }
  if (lib.grinder.id == 0) lib.grinder.id = lib.nextGrinderId++;
  if (lib.hasOtherGrinder &&
      (lib.otherGrinder.id == 0 || lib.otherGrinder.id == lib.grinder.id)) {
    lib.otherGrinder.id = lib.nextGrinderId++;
  }
  if (lib.grinder.calibration >= lib.nextCalibration) {
    lib.nextCalibration = lib.grinder.calibration + 1;
  }
  if (lib.hasOtherGrinder && lib.otherGrinder.calibration >= lib.nextCalibration) {
    lib.nextCalibration = lib.otherGrinder.calibration + 1;
  }
  if (lib.hasOtherGrinder && lib.otherGrinder.calibration == lib.grinder.calibration) {
    lib.otherGrinder.calibration = lib.nextCalibration++;
  }
}

// The library of a controller that has never been set up, written in place.
inline void setDefaultLibrary(Library &lib) {
  resetLibrary(lib);
  Recipe r;
  copyText(r.name, "Double", RECIPE_NAME_LEN);
  r.targetWeightG = 36.0f;
  lib.presets[0].recipeId = addRecipeDef(lib, r);
  copyText(r.name, "Single", RECIPE_NAME_LEN);
  r.targetWeightG = 18.0f;
  lib.presets[1].recipeId = addRecipeDef(lib, r);
  lib.presetCount = 2;
  sanitizeLibrary(lib);
}

inline Library defaultLibrary() {
  Library lib;
  setDefaultLibrary(lib);
  return lib;
}

// ------------------------------------------------------------------ editing --

// Works out the preset for one flat Recipe as the display or the web page
// hands it over, changing the library's coffees and recipes as needed.
//
// slot: which preset of `before` this is an edit of (1-based), 0 for a new
// one. `before` is the library as the editor last saw it. Only what differs
// from that is written, so two presets sharing a recipe or a coffee do not
// overwrite each other's change when a whole list is applied.
//
// false: the recipe or coffee table is full, or `slot` is not there.
inline bool placePreset(Library &lib, const Library &before, uint8_t slot, Recipe flat,
                        Preset &out) {
  sanitizeRecipe(flat);
  const bool hasCoffee = flatHasCoffee(flat);
  if (slot != 0) {
    if (slot > before.presetCount) return false;
    const Preset &was = before.presets[slot - 1];
    const Recipe old = resolvePreset(before, slot - 1);
    RecipeDef *def = findRecipe(lib, was.recipeId);
    if (!def) return false;
    out = was;
    if (flatRecipeDiffers(old, flat)) flatToRecipeDef(flat, *def);
    if (!hasCoffee) {
      out.coffeeId = 0;
    } else if (was.coffeeId == 0 || strcmp(old.coffee, flat.coffee) != 0) {
      // Another name is another coffee, not this one renamed: the one it was
      // stays on the shelf.
      out.coffeeId = findSameCoffee(lib, flat);
      if (out.coffeeId == 0) out.coffeeId = addCoffee(lib, flat);
      if (out.coffeeId == 0) return false;
    } else if (flatCoffeeDiffers(old, flat)) {
      if (Coffee *coffee = findCoffee(lib, was.coffeeId)) {
        flatToCoffee(flat, *coffee);
        syncBagFromCoffee(lib, *coffee);
      }
    }
    out.grindUm = flat.grindUm;
    return true;
  }
  // New: its own recipe, so that editing it later changes nothing else. A
  // coffee already on the shelf is shared.
  out = Preset{};
  out.recipeId = addRecipeDef(lib, flat);
  if (out.recipeId == 0) return false;
  if (hasCoffee) {
    out.coffeeId = findSameCoffee(lib, flat);
    if (out.coffeeId == 0) out.coffeeId = addCoffee(lib, flat);
    if (out.coffeeId == 0) return false;
  }
  out.grindUm = flat.grindUm;
  out.stopOffsetG = flat.stopOffsetG;
  out.learnedAlpha = flat.learnedAlpha;
  return true;
}

// Removes recipes no preset uses.
inline void pruneRecipes(Library &lib) {
  uint8_t kept = 0;
  for (uint8_t i = 0; i < lib.recipeCount; ++i) {
    bool used = false;
    for (uint8_t p = 0; p < lib.presetCount && !used; ++p) {
      used = lib.presets[p].recipeId == lib.recipes[i].id;
    }
    if (used) lib.recipes[kept++] = lib.recipes[i];
  }
  lib.recipeCount = kept;
}

// ---- the three tables edited one record at a time (the web page) ----------

// Adds a coffee (id 0) or replaces the one with that id. Returns its id, 0 if
// the shelf is full or the id is not there.
inline uint16_t saveCoffee(Library &lib, Coffee coffee) {
  sanitizeCoffee(coffee);
  if (coffee.id != 0) {
    Coffee *existing = findCoffee(lib, coffee.id);
    if (!existing) return 0;
    // What the owner typed replaces what was there; where it stands with
    // the shared library is not theirs to type and stays.
    coffee.shareState = existing->shareState;
    coffee.shareSeq = existing->shareSeq;
    coffee.shareHash = existing->shareHash;
    memcpy(coffee.reviewNote, existing->reviewNote, NOTES_LEN);
    memcpy(coffee.libraryId, existing->libraryId, LIBRARY_ID_LEN);
    // The bag in use stays; what is typed here corrects it (a new bag is startBag).
    coffee.bagId = existing->bagId;
    coffee.finishedDate = existing->finishedDate;  // finishCoffee and startBag say, not the form
    *existing = coffee;
    syncBagFromCoffee(lib, *existing);
    return coffee.id;
  }
  if (lib.coffeeCount >= MAX_COFFEES) return 0;
  coffee.id = lib.nextCoffeeId++;
  coffee.shareState = 0;  // new here, whatever was sent in
  coffee.shareSeq = coffee.shareHash = 0;
  coffee.reviewNote[0] = coffee.libraryId[0] = '\0';
  coffee.bagId = 0;
  coffee.finishedDate = 0;
  lib.coffees[lib.coffeeCount++] = coffee;
  syncBagFromCoffee(lib, lib.coffees[lib.coffeeCount - 1]);
  return coffee.id;
}

inline bool coffeeInUse(const Library &lib, uint16_t id) {
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    if (lib.presets[i].coffeeId == id) return true;
  }
  return false;
}

inline bool recipeInUse(const Library &lib, uint16_t id) {
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    if (lib.presets[i].recipeId == id) return true;
  }
  return false;
}

// A coffee still on the controller's list is not removed: take it off the
// list first. Shots made with it keep its name.
inline bool removeCoffee(Library &lib, uint16_t id) {
  if (id == 0 || coffeeInUse(lib, id)) return false;
  for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
    if (lib.coffees[i].id != id) continue;
    for (uint8_t j = i + 1; j < lib.coffeeCount; ++j) lib.coffees[j - 1] = lib.coffees[j];
    --lib.coffeeCount;
    for (uint8_t b = 0; b < lib.bagCount;) {   // its bags go too (the shots keep their roast date)
      if (lib.bags[b].coffeeId != id) { ++b; continue; }
      for (uint8_t j = b; j + 1 < lib.bagCount; ++j) lib.bags[j] = lib.bags[j + 1];
      --lib.bagCount;
    }
    return true;
  }
  return false;
}

inline uint16_t saveRecipeDef(Library &lib, RecipeDef def) {
  sanitizeRecipeDef(def);
  if (def.id != 0) {
    RecipeDef *existing = findRecipe(lib, def.id);
    if (!existing) return 0;
    *existing = def;
    return def.id;
  }
  if (lib.recipeCount >= MAX_RECIPES) return 0;
  def.id = lib.nextRecipeId++;
  lib.recipes[lib.recipeCount++] = def;
  return def.id;
}

inline bool removeRecipeDef(Library &lib, uint16_t id) {
  if (id == 0 || recipeInUse(lib, id) || lib.recipeCount <= 1) return false;
  for (uint8_t i = 0; i < lib.recipeCount; ++i) {
    if (lib.recipes[i].id != id) continue;
    for (uint8_t j = i + 1; j < lib.recipeCount; ++j) lib.recipes[j - 1] = lib.recipes[j];
    --lib.recipeCount;
    return true;
  }
  return false;
}

inline int findPreset(const Library &lib, uint16_t recipeId, uint16_t coffeeId) {
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    if (lib.presets[i].recipeId == recipeId && lib.presets[i].coffeeId == coffeeId) return i;
  }
  return -1;
}

// Puts a recipe, with or without a coffee, on the controller's list. The
// grind setting starts from another preset of the same coffee if there is
// one: the same beans in the same grinder will not be far off. Returns the
// new preset's index; -1 if the list is full, the pair is already on it, or
// either id is not there.
inline int addPreset(Library &lib, uint16_t recipeId, uint16_t coffeeId) {
  const RecipeDef *def = findRecipe(lib, recipeId);
  if (!def || lib.presetCount >= MAX_PRESETS) return -1;
  if (coffeeId != 0 && !findCoffee(lib, coffeeId)) return -1;
  if (findPreset(lib, recipeId, coffeeId) >= 0) return -1;
  Preset preset;
  preset.recipeId = recipeId;
  preset.coffeeId = coffeeId;
  preset.stopOffsetG = def->baselineOffsetG;
  for (uint8_t i = 0; coffeeId != 0 && i < lib.presetCount; ++i) {
    if (lib.presets[i].coffeeId == coffeeId && lib.presets[i].grindUm != 0) {
      preset.grindUm = lib.presets[i].grindUm;
      break;
    }
  }
  lib.presets[lib.presetCount] = preset;
  return lib.presetCount++;
}

// Takes a line off the controller's list. The recipe and the coffee stay.
// The last one cannot go: Start always needs something to brew.
inline bool removePreset(Library &lib, uint8_t index) {
  if (index >= lib.presetCount || lib.presetCount <= 1) return false;
  for (uint8_t j = index + 1; j < lib.presetCount; ++j) lib.presets[j - 1] = lib.presets[j];
  --lib.presetCount;
  if (lib.activePreset > index) --lib.activePreset;
  if (lib.activePreset >= lib.presetCount) lib.activePreset = 0;
  return true;
}

// One preset edited in place (the display's quick edit, the +/- buttons).
inline bool editPreset(Library &lib, const Library &before, uint8_t index, const Recipe &flat) {
  if (index >= lib.presetCount || index >= before.presetCount) return false;
  Preset preset;
  if (!placePreset(lib, before, index + 1, flat, preset)) return false;
  lib.presets[index] = preset;
  sanitizeLibrary(lib);
  return true;
}

// The whole preset list replaced by a list of flat Recipes: the web page's
// recipe editor, which knows nothing of coffees and recipes as separate
// things. slots[i] says which preset of `before` flat[i] was (1-based, 0 or
// a repeat = new). Presets left out are deleted, and with them the recipes
// nothing uses any more. Coffees stay on the shelf.
//
// false, with the library unchanged, if nothing usable was given or a table
// filled up.
inline bool applyPresetList(Library &lib, const Library &before, const Recipe *flat,
                            const uint8_t *slots, uint8_t count, uint8_t active) {
  Preset next[MAX_PRESETS];
  bool claimed[MAX_PRESETS] = {};
  uint8_t made = 0;
  bool ok = count > 0;
  for (uint8_t i = 0; ok && i < count && made < MAX_PRESETS; ++i) {
    uint8_t slot = slots ? slots[i] : 0;
    if (slot < 1 || slot > before.presetCount || claimed[slot - 1]) slot = 0;
    if (slot) claimed[slot - 1] = true;
    Recipe item = flat[i];
    if (slot == 0) {  // nothing learned yet
      sanitizeRecipe(item);
      item.stopOffsetG = item.baselineOffsetG;
      item.learnedAlpha = 0;
    }
    ok = placePreset(lib, before, slot, item, next[made]);
    if (ok) ++made;
  }
  if (!ok || made == 0) {
    lib = before;
    return false;
  }
  for (uint8_t i = 0; i < made; ++i) lib.presets[i] = next[i];
  lib.presetCount = made;
  lib.activePreset = active < made ? active : 0;
  pruneRecipes(lib);
  sanitizeLibrary(lib);
  return true;
}

// ------------------------------------------------- from earlier builds ------

// Settings version 7 held up to 16 recipes with the coffee written into each.
// Every one becomes a recipe and a preset of the same name, in the same
// order; recipes naming the same coffee share one Coffee. Nothing learned is
// lost: grind setting, stop offset and learning factor move to the preset.
inline void libraryFromV7(const legacy::SettingsV7 &old, Library &out) {
  resetLibrary(out);
  const uint8_t count =
      old.recipeCount < legacy::MAX_RECIPES_V7 ? old.recipeCount : legacy::MAX_RECIPES_V7;
  for (uint8_t i = 0; i < count && out.presetCount < MAX_PRESETS; ++i) {
    const legacy::RecipeV7 &o = old.recipes[i];
    Recipe r;
    copyText(r.name, o.name, RECIPE_NAME_LEN);
    r.targetWeightG = o.targetWeightG;
    r.baselineOffsetG = o.baselineOffsetG;
    r.stopOffsetG = o.stopOffsetG;
    r.timeLimit = o.timeLimit;
    r.maxShotTimeS = o.maxShotTimeS;
    r.useRatio = o.useRatio;
    r.doseG = o.doseG;
    r.ratio = o.ratio;
    r.learnedAlpha = o.learnedAlpha;
    r.targetTimeS = o.targetTimeS;
    r.grindUm = o.grindUm;
    copyText(r.coffee, o.coffee, COFFEE_NAME_LEN);
    copyText(r.region, o.region, ORIGIN_LEN);
    copyText(r.varietal, o.varietal, ORIGIN_LEN);
    r.roastLevel = o.roastLevel;
    r.roastDate = o.roastDate;
    r.preinfusion = o.preinfusion;
    r.preinfusionHoldMs = o.preinfusionHoldMs;
    Preset preset;
    if (!placePreset(out, out, 0, r, preset)) continue;  // slot 0 never reads `before`
    if (i == old.activeRecipe) out.activePreset = out.presetCount;
    out.presets[out.presetCount++] = preset;
  }
  sanitizeLibrary(out);
}

// storedBytes: how much of `old` was actually on flash. The oldest version 7
// blobs end before requireCup.
inline Settings settingsFromV7(const legacy::SettingsV7 &old, size_t storedBytes) {
  Settings s;
  s.learningEnabled = old.learningEnabled;
  s.learningFactor = old.learningFactor;
  s.adaptiveLearning = old.adaptiveLearning;
  s.minValidWeightG = old.minValidWeightG;
  s.scaleTimeoutMs = old.scaleTimeoutMs;
  s.dripObserveMs = old.dripObserveMs;
  s.minShotTimeMs = old.minShotTimeMs;
  s.noFlowTimeoutS = old.noFlowTimeoutS;
  s.cupMode = old.cupMode;
  s.cupMinWeightG = old.cupMinWeightG;
  s.autoStartAfterTare = old.autoStartAfterTare;
  s.scaleTimer = old.scaleTimer;
  s.actuationMode = old.actuationMode;
  s.pressDurationMs = old.pressDurationMs;
  s.releaseSettleMs = old.releaseSettleMs;
  s.botHoldSeconds = old.botHoldSeconds;
  s.grindToleranceS = old.grindToleranceS;
  s.grindUmPerS = old.grindUmPerS;
  s.grindWindow = old.grindWindow;
  s.grindMaxStepUm = old.grindMaxStepUm;
  s.grindTimeBasis = old.grindTimeBasis;
  if (storedBytes > offsetof(legacy::SettingsV7, requireCup)) s.requireCup = old.requireCup;
  sanitize(s);
  return s;
}

// A history version 8 shot as a current record. Every stored value is kept.
// The ids are looked up by the names the shot carries: the recipe by name,
// the coffee by name and bag (roast date), then by name and origin, then by
// name alone. A shot of something no longer in the library gets 0.
// Version 10 is version 9 with two fields at the end: the layout up to there is the same.
inline ShotRecord shotFromV9(const legacy::ShotRecordV9 &o) {
  ShotRecord s;
  static_assert(sizeof(legacy::ShotRecordV9) <= sizeof(ShotRecord), "version 10 only adds");
  memcpy(&s, &o, sizeof(o));
  s.bagId = 0;
  s.tdsX100 = 0;
  return s;
}

inline ShotRecord shotFromV8(const legacy::ShotRecordV8 &o, const Library &lib) {
  ShotRecord s;
  s.id = o.id;
  s.epochS = o.epochS;
  copyText(s.recipe, o.recipe, RECIPE_NAME_LEN);
  s.targetWeightG = o.targetWeightG;
  s.stopOffsetG = o.stopOffsetG;
  s.stopWeightG = o.stopWeightG;
  s.finalWeightG = o.finalWeightG;
  s.dripWeightG = o.dripWeightG;
  s.shotTimeS = o.shotTimeS;
  s.firstDropS = o.firstDropS;
  s.averageFlowGS = o.averageFlowGS;
  s.peakFlowGS = o.peakFlowGS;
  s.nextStopOffsetG = o.nextStopOffsetG;
  s.stopLatencyMs = o.stopLatencyMs;
  s.endReason = o.endReason;
  s.flags = o.flags;
  s.alphaUsed = o.alphaUsed;
  s.alphaNext = o.alphaNext;
  s.grindUm = o.grindUm;
  s.doseG = o.doseG;
  s.targetTimeS = o.targetTimeS;
  s.pourTimeS = o.pourTimeS;
  s.pourFlowGS = o.pourFlowGS;
  s.timeBasis = o.timeBasis;
  copyText(s.coffee, o.coffee, COFFEE_NAME_LEN);
  copyText(s.region, o.region, ORIGIN_LEN);
  copyText(s.varietal, o.varietal, ORIGIN_LEN);
  s.roastDate = o.roastDate;
  s.roastLevel = o.roastLevel;
  s.preinfusionHoldMs = o.preinfusionHoldMs;
  s.flow25GS = o.flow25GS;
  s.flow50GS = o.flow50GS;
  s.flow75GS = o.flow75GS;
  s.flowAcceleration = o.flowAcceleration;
  s.flowRoughnessGS = o.flowRoughnessGS;
  s.rating = o.rating;
  s.taste = o.taste;
  s.grindVote = o.grindVote;

  for (uint8_t i = 0; i < lib.recipeCount && s.recipeId == 0; ++i) {
    if (strcmp(lib.recipes[i].name, s.recipe) == 0) s.recipeId = lib.recipes[i].id;
  }
  if (s.coffee[0] || s.region[0] || s.varietal[0]) {
    uint16_t byOrigin = 0, byName = 0;
    for (uint8_t i = 0; i < lib.coffeeCount && s.coffeeId == 0; ++i) {
      const Coffee &c = lib.coffees[i];
      if (strcmp(c.name, s.coffee) != 0) continue;
      const bool origin = strcmp(c.region, s.region) == 0 && strcmp(c.varietal, s.varietal) == 0;
      if (origin && c.roastDate == s.roastDate) s.coffeeId = c.id;
      else if (origin && byOrigin == 0) byOrigin = c.id;
      else if (byName == 0) byName = c.id;
    }
    if (s.coffeeId == 0) s.coffeeId = byOrigin ? byOrigin : byName;
  }
  return s;
}

// ------------------------------------------------------------ saved format --
// A header, then the three tables as raw records. The header says how long
// each record was when written. A build that has since added fields at the
// end of a record reads the shorter ones and leaves its new fields at their
// defaults; nothing else about a record may change.

constexpr uint32_t LIBRARY_MAGIC = 0x4C574242;  // "BBWL"
constexpr uint16_t LIBRARY_VERSION = 3;      // 3: the bags follow the grinders
constexpr uint16_t LIBRARY_VERSION_V2 = 2;   // 2: the grinder follows the tables
constexpr uint16_t LIBRARY_VERSION_V1 = 1;

struct LibraryFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t coffeeSize;
  uint16_t recipeSize;
  uint16_t presetSize;
  uint8_t coffeeCount;
  uint8_t recipeCount;
  uint8_t presetCount;
  uint8_t activePreset;
  uint16_t nextCoffeeId;
  uint16_t nextRecipeId;
};
// Version 2 adds, straight after that header: the grinder record's length,
// and whether a second grinder follows the first (written as 0 by the first
// builds of version 2, which had only the one).
struct LibraryFileHeaderV2 {
  uint16_t grinderSize;
  uint16_t otherGrinders;
};

// Version 3 adds, straight after the grinders: the bag record's length, how many follow and the next id.
struct LibraryFileHeaderV3 {
  uint16_t bagSize;
  uint16_t bagCount;
  uint16_t nextBagId;
  uint16_t reserved;
};

constexpr size_t LIBRARY_MAX_BYTES = sizeof(LibraryFileHeader) + sizeof(LibraryFileHeaderV2) +
                                     MAX_COFFEES * sizeof(Coffee) +
                                     MAX_RECIPES * sizeof(RecipeDef) +
                                     MAX_PRESETS * sizeof(Preset) + 2 * sizeof(Grinder) +
                                     sizeof(LibraryFileHeaderV3) + MAX_BAGS * sizeof(Bag);

// Returns the number of bytes written, 0 if `size` is too small.
inline size_t encodeLibrary(const Library &lib, uint8_t *out, size_t size) {
  LibraryFileHeader h = {};
  h.magic = LIBRARY_MAGIC;
  h.version = LIBRARY_VERSION;
  h.coffeeSize = sizeof(Coffee);
  h.recipeSize = sizeof(RecipeDef);
  h.presetSize = sizeof(Preset);
  h.coffeeCount = lib.coffeeCount < MAX_COFFEES ? lib.coffeeCount : MAX_COFFEES;
  h.recipeCount = lib.recipeCount < MAX_RECIPES ? lib.recipeCount : MAX_RECIPES;
  h.presetCount = lib.presetCount < MAX_PRESETS ? lib.presetCount : MAX_PRESETS;
  h.activePreset = lib.activePreset;
  h.nextCoffeeId = lib.nextCoffeeId;
  h.nextRecipeId = lib.nextRecipeId;
  const uint16_t others = lib.hasOtherGrinder ? 1 : 0;
  const LibraryFileHeaderV2 h2 = {static_cast<uint16_t>(sizeof(Grinder)), others};
  const uint8_t bags = lib.bagCount < MAX_BAGS ? lib.bagCount : MAX_BAGS;
  const LibraryFileHeaderV3 h3 = {static_cast<uint16_t>(sizeof(Bag)), bags, lib.nextBagId, 0};
  const size_t need = sizeof(h) + sizeof(h2) + h.coffeeCount * sizeof(Coffee) +
                      h.recipeCount * sizeof(RecipeDef) + h.presetCount * sizeof(Preset) +
                      (1 + others) * sizeof(Grinder) + sizeof(h3) + bags * sizeof(Bag);
  if (!out || size < need) return 0;
  uint8_t *at = out;
  memcpy(at, &h, sizeof(h));
  at += sizeof(h);
  memcpy(at, &h2, sizeof(h2));
  at += sizeof(h2);
  memcpy(at, lib.coffees, h.coffeeCount * sizeof(Coffee));
  at += h.coffeeCount * sizeof(Coffee);
  memcpy(at, lib.recipes, h.recipeCount * sizeof(RecipeDef));
  at += h.recipeCount * sizeof(RecipeDef);
  memcpy(at, lib.presets, h.presetCount * sizeof(Preset));
  at += h.presetCount * sizeof(Preset);
  memcpy(at, &lib.grinder, sizeof(Grinder));
  at += sizeof(Grinder);
  if (others) {
    memcpy(at, &lib.otherGrinder, sizeof(Grinder));
    at += sizeof(Grinder);
  }
  memcpy(at, &h3, sizeof(h3));
  at += sizeof(h3);
  memcpy(at, lib.bags, bags * sizeof(Bag));
  return need;
}

// false, with `out` untouched, if the bytes are not a library.
inline bool decodeLibrary(const uint8_t *in, size_t size, Library &out) {
  LibraryFileHeader h;
  if (!in || size < sizeof(h)) return false;
  memcpy(&h, in, sizeof(h));
  if (h.magic != LIBRARY_MAGIC ||
      (h.version != LIBRARY_VERSION && h.version != LIBRARY_VERSION_V2 && h.version != LIBRARY_VERSION_V1)) {
    return false;
  }
  LibraryFileHeaderV2 h2 = {0, 0};  // version 1 had no grinder
  size_t headers = sizeof(h);
  if (h.version >= LIBRARY_VERSION_V2) {
    if (size < sizeof(h) + sizeof(h2)) return false;
    memcpy(&h2, in + sizeof(h), sizeof(h2));
    headers += sizeof(h2);
    if (h2.otherGrinders > 1) return false;
  }
  if (h.coffeeCount > MAX_COFFEES || h.recipeCount > MAX_RECIPES || h.presetCount > MAX_PRESETS ||
      h.coffeeSize == 0 || h.recipeSize == 0 || h.presetSize == 0) {
    return false;
  }
  const size_t need = headers + static_cast<size_t>(h.coffeeCount) * h.coffeeSize +
                      static_cast<size_t>(h.recipeCount) * h.recipeSize +
                      static_cast<size_t>(h.presetCount) * h.presetSize +
                      static_cast<size_t>(1 + h2.otherGrinders) * h2.grinderSize;
  if (size < need) return false;
  LibraryFileHeaderV3 h3 = {0, 0, 1, 0};   // versions 1 and 2 had no bags
  bool haveBags = false;
  if (h.version >= LIBRARY_VERSION && size >= need + sizeof(h3)) {
    // A file cut short before its bags is read without them (they are rebuilt from the coffees).
    memcpy(&h3, in + need, sizeof(h3));
    if (h3.bagSize == 0 || h3.bagCount > MAX_BAGS ||
        size < need + sizeof(h3) + static_cast<size_t>(h3.bagCount) * h3.bagSize) {
      return false;
    }
    haveBags = true;
  }
  resetLibrary(out);
  Library &lib = out;
  const uint8_t *at = in + headers;
  for (uint8_t i = 0; i < h.coffeeCount; ++i, at += h.coffeeSize) {
    memcpy(&lib.coffees[i], at, h.coffeeSize < sizeof(Coffee) ? h.coffeeSize : sizeof(Coffee));
  }
  for (uint8_t i = 0; i < h.recipeCount; ++i, at += h.recipeSize) {
    memcpy(&lib.recipes[i], at,
           h.recipeSize < sizeof(RecipeDef) ? h.recipeSize : sizeof(RecipeDef));
  }
  for (uint8_t i = 0; i < h.presetCount; ++i, at += h.presetSize) {
    memcpy(&lib.presets[i], at, h.presetSize < sizeof(Preset) ? h.presetSize : sizeof(Preset));
  }
  if (h2.grinderSize != 0) {
    const size_t take = h2.grinderSize < sizeof(Grinder) ? h2.grinderSize : sizeof(Grinder);
    memcpy(&lib.grinder, at, take);
    if (h2.otherGrinders == 1) {
      memcpy(&lib.otherGrinder, at + h2.grinderSize, take);
      lib.hasOtherGrinder = true;
    }
  }
  if (haveBags) {
    const uint8_t *bagAt = in + need + sizeof(h3);
    for (uint16_t i = 0; i < h3.bagCount; ++i, bagAt += h3.bagSize) {
      memcpy(&lib.bags[i], bagAt, h3.bagSize < sizeof(Bag) ? h3.bagSize : sizeof(Bag));
      if (h3.bagSize < offsetof(Bag, expectedDate) + sizeof(uint32_t)) lib.bags[i].expectedDate = 0;
      if (h3.bagSize <= offsetof(Bag, unopened) + 2) {  // saved before unopened bags: its padding
        lib.bags[i].unopened = 0;
        lib.bags[i].spare = 0;
        lib.bags[i].subscriptionId = 0;
      }
    }
    lib.bagCount = static_cast<uint8_t>(h3.bagCount);
    lib.nextBagId = h3.nextBagId;
  }
  lib.coffeeCount = h.coffeeCount;
  lib.recipeCount = h.recipeCount;
  lib.presetCount = h.presetCount;
  lib.activePreset = h.activePreset;
  lib.nextCoffeeId = h.nextCoffeeId;
  lib.nextRecipeId = h.nextRecipeId;
  sanitizeLibrary(lib);
  return true;
}

}  // namespace bbw
