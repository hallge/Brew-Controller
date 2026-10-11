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
#include <string.h>

#include "GrinderRegistry.h"
#include "Legacy.h"
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
  c.reviewNote[NOTES_LEN - 1] = c.libraryId[LIBRARY_ID_LEN - 1] = '\0';
  if (c.shareState > 5) c.shareState = 0;  // ShareState::DECLINED
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

inline void sanitizeGrinder(Grinder &g) {
  g.model[GRINDER_NAME_LEN - 1] = g.burrs[GRINDER_BURRS_LEN - 1] = '\0';
  g.unit[GRINDER_UNIT_LEN - 1] = '\0';
  if (g.unit[0] == '\0') copyText(g.unit, "um", GRINDER_UNIT_LEN);
  g.umPerUnit = clampTo(finiteOr(g.umPerUnit, 0.0f), 0.0f, 1000.0f);
  if (!validRoastDate(g.zeroDate)) g.zeroDate = 0;
  g.reviewNote[NOTES_LEN - 1] = g.libraryId[LIBRARY_ID_LEN - 1] = '\0';
  if (g.shareState > 5) g.shareState = 0;
}

// A model from the bundled registry brings its own name, unit and microns
// per unit; burrs can still be given (other burrs in the same grinder).
inline void normalizeGrinder(Grinder &g) {
  sanitizeGrinder(g);
  if (const GrinderModel *m = findGrinderModel(g.modelId)) {
    copyText(g.model, m->model, GRINDER_NAME_LEN);
    copyText(g.unit, m->unit, GRINDER_UNIT_LEN);
    g.umPerUnit = m->umPerUnit;
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
  const bool changed = strcmp(in.model, was.model) != 0 || strcmp(in.burrs, was.burrs) != 0 ||
                       strcmp(in.unit, was.unit) != 0;
  in.id = was.id;
  in.calibration = was.calibration;
  if (rezeroed || (changed && !firstNaming)) in.calibration = lib.nextCalibration++;
  else if (in.zeroDate == 0) in.zeroDate = was.zeroDate;
  // Read in another unit, the numbers entered so far mean nothing.
  if (!firstNaming && strcmp(in.unit, was.unit) != 0) {
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
      if (Coffee *coffee = findCoffee(lib, was.coffeeId)) flatToCoffee(flat, *coffee);
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
    *existing = coffee;
    return coffee.id;
  }
  if (lib.coffeeCount >= MAX_COFFEES) return 0;
  coffee.id = lib.nextCoffeeId++;
  coffee.shareState = 0;  // new here, whatever was sent in
  coffee.shareSeq = coffee.shareHash = 0;
  coffee.reviewNote[0] = coffee.libraryId[0] = '\0';
  lib.coffees[lib.coffeeCount++] = coffee;
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
constexpr uint16_t LIBRARY_VERSION = 2;      // 2: the grinder follows the tables
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

constexpr size_t LIBRARY_MAX_BYTES = sizeof(LibraryFileHeader) + sizeof(LibraryFileHeaderV2) +
                                     MAX_COFFEES * sizeof(Coffee) +
                                     MAX_RECIPES * sizeof(RecipeDef) +
                                     MAX_PRESETS * sizeof(Preset) + 2 * sizeof(Grinder);

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
  const size_t need = sizeof(h) + sizeof(h2) + h.coffeeCount * sizeof(Coffee) +
                      h.recipeCount * sizeof(RecipeDef) + h.presetCount * sizeof(Preset) +
                      (1 + others) * sizeof(Grinder);
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
  if (others) memcpy(at, &lib.otherGrinder, sizeof(Grinder));
  return need;
}

// false, with `out` untouched, if the bytes are not a library.
inline bool decodeLibrary(const uint8_t *in, size_t size, Library &out) {
  LibraryFileHeader h;
  if (!in || size < sizeof(h)) return false;
  memcpy(&h, in, sizeof(h));
  if (h.magic != LIBRARY_MAGIC ||
      (h.version != LIBRARY_VERSION && h.version != LIBRARY_VERSION_V1)) {
    return false;
  }
  LibraryFileHeaderV2 h2 = {0, 0};  // version 1 had no grinder
  size_t headers = sizeof(h);
  if (h.version == LIBRARY_VERSION) {
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
