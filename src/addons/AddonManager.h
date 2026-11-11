#pragma once
// The add-ons compiled into this image, what the owner set for each, and what the controller does
// with a grinder's grinds (docs/ADDONS.md). Portable: the firmware's glue (AddonHost.cpp) gives it
// the controller through Actions, the host tests give it a stand-in.
//
// The grind policy, in short:
// - A grind is for one controller recipe: the grinder's recipe number is mapped to a recipe id, by
//   the owner or (when exactly one recipe matches its dose and yield) automatically. A grind for an
//   unmapped recipe changes nothing.
// - Its burr gap becomes that recipe's setting (with the coffee in use), never another recipe's.
// - Its measured dose never goes into the recipe. Grind advice groups shots by the recipe's dose and
//   target (within 0.05 g): a dose that differs a little at every grind would put each shot in a
//   group of its own. It gives the next shot on that recipe, within 15 minutes, a target of its own
//   (recipe target x actual / recipe dose), and is recorded on that shot's context when it is saved.
// - Nothing is done while a shot runs: the grind waits until it has finished.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addons/Addon.h"

namespace bbw {
namespace addon {

constexpr size_t MAX_ADDONS = 8;
constexpr uint32_t GRIND_WINDOW_MS = 15UL * 60UL * 1000UL;  // a grind is for a shot within this
constexpr float DOSE_MIN_G = 5.0f, DOSE_MAX_G = 40.0f;
constexpr float DOSE_TARGET_MAX_DIFF_G = 2.0f;  // further from the recipe's: likely the wrong recipe
constexpr float AUTO_MAP_DOSE_G = 0.5f, AUTO_MAP_YIELD_G = 2.0f;

// A controller recipe as the policy needs it.
struct RecipeView {
  uint16_t id = 0;
  char name[24] = {};
  float doseG = 0.0f;
  float targetG = 0.0f;
};

enum MapSource : uint8_t { MAP_NONE = 0, MAP_MANUAL = 1, MAP_AUTO = 2 };
struct MapEntry {
  uint16_t recipeId = 0;
  uint8_t source = MAP_NONE;
};

// What one grind leads to.
struct GrindPlan {
  bool mapped = false;
  uint16_t recipeId = 0;
  bool autoMapped = false;   // mapped just now, by its dose and yield
  float shotTargetG = 0.0f;  // the next shot's own target; 0: none
  uint16_t doseDg = 0;       // to record on that shot; 0: none
  uint16_t adoptSteps = 0;   // the recipe's new setting; 0: none
  bool select = false;       // select the recipe
  char note[96] = {};
};

// The owner's choices for a grinder add-on, beside its own settings.
struct GrindOptions {
  bool doseSetsTarget = true;   // the measured dose sets the next shot's own target
  bool selectRecipe = false;    // a grind selects its recipe
  bool shotFeedback = false;    // shots are reported to the add-on (onShotComplete)
};

// The controller, as the manager may change it.
class Actions {
 public:
  virtual ~Actions() = default;
  virtual bool brewing() const = 0;
  virtual size_t recipes(RecipeView *out, size_t max) const = 0;
  virtual GrinderInfo grinder() const = 0;
  virtual float umPerStep() const = 0;   // the grinder's microns per stored step; 0: not in microns
  // The preset of this recipe with the coffee in use. false: there is none, or a shot runs.
  virtual bool adoptSetting(uint16_t recipeId, uint16_t steps) = 0;
  virtual void setNextShotTarget(float targetG, uint16_t recipeId) = 0;  // 0, 0: none
  virtual void selectRecipe(uint16_t recipeId) = 0;
  virtual void changed() = 0;  // what is stored (enabled, settings, the map) changed: save it
  virtual void log(const char *text) = 0;
};

inline int roundToInt(float v) { return static_cast<int>(v < 0 ? v - 0.5f : v + 0.5f); }

// The policy for one grind, given the map (indexed by grinder recipe, 1..MAX_GRINDER_RECIPES).
// The map may gain an automatic entry.
inline GrindPlan planGrind(const GrindEvent &ev, const RecipeView *recipes, size_t count,
                           MapEntry map[MAX_GRINDER_RECIPES + 1], const GrindOptions &opt,
                           const GrinderInfo &grinder, float umPerStep) {
  GrindPlan plan;
  const uint8_t r = ev.grinderRecipe;
  if (r == 0 || r > MAX_GRINDER_RECIPES) {
    snprintf(plan.note, sizeof(plan.note), "the grinder did not say which of its recipes it ground");
    return plan;
  }
  const RecipeView *recipe = nullptr;
  if (map[r].recipeId) {
    for (size_t i = 0; i < count; ++i) {
      if (recipes[i].id == map[r].recipeId) recipe = &recipes[i];
    }
    if (!recipe) map[r] = MapEntry{};  // its recipe was deleted
  }
  if (!recipe && map[r].source != MAP_MANUAL && !isnan(ev.doseTargetG)) {
    const RecipeView *match = nullptr;
    int matches = 0;
    for (size_t i = 0; i < count; ++i) {
      if (fabsf(recipes[i].doseG - ev.doseTargetG) > AUTO_MAP_DOSE_G) continue;
      if (!isnan(ev.beverageTargetG) && fabsf(recipes[i].targetG - ev.beverageTargetG) > AUTO_MAP_YIELD_G) continue;
      match = &recipes[i];
      ++matches;
    }
    if (matches == 1) {
      recipe = match;
      map[r].recipeId = match->id;
      map[r].source = MAP_AUTO;
      plan.autoMapped = true;
    } else if (matches > 1) {
      snprintf(plan.note, sizeof(plan.note), "grinder recipe %u matches %d recipes: map it on the Add-ons card",
               static_cast<unsigned>(r), matches);
      return plan;
    }
  }
  if (!recipe) {
    if (!plan.note[0]) {
      snprintf(plan.note, sizeof(plan.note), "grinder recipe %u is not mapped to a recipe", static_cast<unsigned>(r));
    }
    return plan;
  }
  plan.mapped = true;
  plan.recipeId = recipe->id;
  plan.select = opt.selectRecipe;

  // The dose: recorded, and the next shot's own target.
  const float dose = ev.doseActualG;
  if (!isnan(dose) && dose >= DOSE_MIN_G && dose <= DOSE_MAX_G) {
    plan.doseDg = static_cast<uint16_t>(roundToInt(dose * 10.0f));
    if (opt.doseSetsTarget && recipe->doseG > 0.0f && recipe->targetG > 0.0f) {
      if (fabsf(dose - recipe->doseG) <= DOSE_TARGET_MAX_DIFF_G) {
        plan.shotTargetG = roundToInt(recipe->targetG * dose / recipe->doseG * 100.0f) / 100.0f;
      } else {
        snprintf(plan.note, sizeof(plan.note), "%.1f g is more than %.0f g from %s's %.1f g: target not changed",
                 dose, DOSE_TARGET_MAX_DIFF_G, recipe->name, recipe->doseG);
      }
    }
  } else if (!isnan(dose)) {
    snprintf(plan.note, sizeof(plan.note), "a dose of %.1f g is outside 5-40 g: not used", dose);
  }

  // The burr gap: the recipe's setting.
  if (!isnan(ev.gapUm) && ev.gapUm > 0.0f) {
    if (umPerStep > 0.0f) {
      int steps = roundToInt(ev.gapUm / umPerStep);
      const int lo = grinder.rangeKnown ? grinder.minStep : 1;
      const int hi = grinder.rangeKnown ? grinder.maxStep : 2000;
      if (steps < lo) steps = lo;
      if (steps > hi) steps = hi;
      plan.adoptSteps = static_cast<uint16_t>(steps);
    } else if (!plan.note[0]) {
      snprintf(plan.note, sizeof(plan.note), "the grinder set up here is not read in microns: setting not taken");
    }
  }
  return plan;
}

// The value of a setting as text, its default when not set.
struct Slot {
  const AddonEntry *entry = nullptr;
  Addon *addon = nullptr;
  bool apiOk = false;
  bool enabled = false;
  bool started = false;
  char values[MAX_FIELDS][VALUE_LEN] = {};
  bool set[MAX_FIELDS] = {};
  GrindOptions options;
  MapEntry map[MAX_GRINDER_RECIPES + 1];
  // The newest grind and what came of it, for the card.
  bool hasGrind = false;
  GrindEvent grind;
  GrindPlan plan;
  bool queued = false;   // taken while a shot ran: dealt with when it has finished
  GrindEvent queuedGrind;
  // Chip RAM (the board): free before and after begin(), and the least seen during its work.
  uint32_t chipBefore = 0, chipAfter = 0, chipLow = 0;
};

// A grind waiting for its shot.
struct PendingGrind {
  bool active = false;
  uint32_t atMs = 0;
  uint16_t recipeId = 0;
  uint16_t doseDg = 0;
  float targetG = 0.0f;
  uint32_t seq = 0;     // GrindEvent::seq
};

inline bool validRoutePrefix(const char *p) {
  if (!p) return false;
  const size_t n = strlen(p);
  // Two segments at least ("/api/dev/"), under /api/, never the controller's own add-on routes.
  if (n < 8 || n > 40 || strncmp(p, "/api/", 5) != 0 || p[n - 1] != '/') return false;
  if (strncmp(p, "/api/addons", 11) == 0) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = p[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_')) return false;
  }
  return true;
}

// "scheme://host:port" of a URL, lower case; false if it is not http(s).
inline bool urlOrigin(const char *url, char *out, size_t size) {
  if (!url || !out || size == 0) return false;
  size_t start;
  if (strncmp(url, "https://", 8) == 0) start = 8;
  else if (strncmp(url, "http://", 7) == 0) start = 7;
  else return false;
  size_t end = start;
  while (url[end] && url[end] != '/' && url[end] != '?' && url[end] != '#') ++end;
  if (end == start || end >= size) return false;
  for (size_t i = 0; i < end; ++i) {
    const char c = url[i];
    if (c == '@') return false;  // no credentials in a URL
    out[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
  }
  out[end] = '\0';
  return true;
}

class Manager {
 public:
  // The table of add-ons compiled in. Each is made once (make()), never freed.
  void attach(const AddonEntry *table, size_t count) {
    count_ = count > MAX_ADDONS ? MAX_ADDONS : count;
    for (size_t i = 0; i < count_; ++i) {
      Slot &s = slots_[i];
      s.entry = &table[i];
      s.addon = table[i].make ? table[i].make() : nullptr;
      s.apiOk = s.addon && s.addon->info().api == ADDON_API_VERSION &&
                strcmp(s.addon->info().id, table[i].id) == 0 && s.addon->info().fieldCount <= MAX_FIELDS;
    }
  }
  size_t count() const { return count_; }
  Slot &slot(size_t i) { return slots_[i]; }
  const Slot &slot(size_t i) const { return slots_[i]; }
  int find(const char *id) const {
    for (size_t i = 0; i < count_; ++i) {
      if (id && strcmp(slots_[i].entry->id, id) == 0) return static_cast<int>(i);
    }
    return -1;
  }
  void setHost(size_t i, Host *host) { if (i < count_) hosts_[i] = host; }

  // ---- settings ----
  int fieldIndex(size_t i, const char *key) const {
    const Slot &s = slots_[i];
    if (!s.addon || !key) return -1;
    const AddonInfo &info = s.addon->info();
    for (uint8_t f = 0; f < info.fieldCount && f < MAX_FIELDS; ++f) {
      if (strcmp(info.fields[f].key, key) == 0) return f;
    }
    return -1;
  }
  const char *value(size_t i, const char *key) const {
    const int f = fieldIndex(i, key);
    if (f < 0) return "";
    const Slot &s = slots_[i];
    return s.set[f] ? s.values[f] : s.addon->info().fields[f].def;
  }
  // false: no such setting, or too long. "" for a secret keeps what is stored; "-" forgets it.
  bool setValue(size_t i, const char *key, const char *text) {
    const int f = fieldIndex(i, key);
    if (f < 0 || !text || strlen(text) >= VALUE_LEN) return false;
    Slot &s = slots_[i];
    const bool secret = s.addon->info().fields[f].type == FieldType::SECRET;
    if (secret && text[0] == '\0') return true;
    if (secret && strcmp(text, "-") == 0) {
      s.values[f][0] = '\0';
      s.set[f] = false;
      return true;
    }
    strncpy(s.values[f], text, VALUE_LEN - 1);
    s.values[f][VALUE_LEN - 1] = '\0';
    s.set[f] = true;
    return true;
  }
  // Everything stored for it goes (the add-on itself stays, switched off).
  void forget(size_t i) {
    setEnabled(i, false);
    Slot &s = slots_[i];
    memset(s.values, 0, sizeof(s.values));
    memset(s.set, 0, sizeof(s.set));
    s.options = GrindOptions();
    for (auto &m : s.map) m = MapEntry{};
    s.hasGrind = s.queued = false;
  }

  // The fetch an add-on may make: to the origin of one of its URL settings, nowhere else.
  bool urlAllowed(size_t i, const char *url) const {
    char want[96], have[96];
    if (!urlOrigin(url, want, sizeof(want))) return false;
    const Slot &s = slots_[i];
    if (!s.addon) return false;
    const AddonInfo &info = s.addon->info();
    for (uint8_t f = 0; f < info.fieldCount && f < MAX_FIELDS; ++f) {
      if (info.fields[f].type != FieldType::URL) continue;
      if (urlOrigin(value(i, info.fields[f].key), have, sizeof(have)) && strcmp(want, have) == 0) return true;
    }
    return false;
  }

  // ---- switching on and off ----
  bool setEnabled(size_t i, bool on) {
    if (i >= count_) return false;
    Slot &s = slots_[i];
    if (on && !s.apiOk) return false;
    s.enabled = on;
    if (on && !s.started && hosts_[i]) {
      s.addon->begin(*hosts_[i]);
      s.started = true;
    } else if (!on && s.started) {
      s.addon->end();
      s.started = false;
    }
    return true;
  }
  // At start-up, after what was stored is loaded: begin the ones switched on.
  void startEnabled() {
    for (size_t i = 0; i < count_; ++i) {
      if (slots_[i].enabled) {
        slots_[i].enabled = false;
        setEnabled(i, true);
      }
    }
  }

  // ---- web routes ----
  // The add-on that answers this path, -1 none: switched on, and the path under one of its declared
  // (and valid) prefixes.
  int routeFor(const char *path) const {
    if (!path) return -1;
    for (size_t i = 0; i < count_; ++i) {
      const Slot &s = slots_[i];
      if (!s.started) continue;
      for (const char *p : s.entry->routes) {
        if (p && validRoutePrefix(p) && strncmp(path, p, strlen(p)) == 0) return static_cast<int>(i);
      }
    }
    return -1;
  }

  // ---- the loop ----
  void tick(uint32_t nowMs, Actions &act) {
    for (size_t i = 0; i < count_; ++i) {
      Slot &s = slots_[i];
      if (!s.started) continue;
      s.addon->tick(nowMs);
      GrinderDriver *g = s.addon->grinder();
      if (!g || !(g->caps() & GRIND_EVENTS)) continue;
      GrindEvent ev;
      while (g->takeGrind(ev)) {
        s.queued = true;   // the newest one wins
        s.queuedGrind = ev;
      }
      if (s.queued && !act.brewing()) {
        s.queued = false;
        if (static_cast<uint32_t>(nowMs - s.queuedGrind.atMs) <= GRIND_WINDOW_MS) apply(i, s.queuedGrind, act);
      }
    }
    if (pending_.active && static_cast<uint32_t>(nowMs - pending_.atMs) > GRIND_WINDOW_MS) {
      pending_ = PendingGrind{};
      act.setNextShotTarget(0.0f, 0);
    }
  }

  // A shot began (the controller left READY for a shot) at this time.
  void noteShotStart(uint32_t nowMs) { shotStartMs_ = nowMs; }

  // At the save of a shot on this recipe: the dose of the grind it followed, tenths of a gram, 0 none.
  uint16_t doseForShot(uint16_t recipeId) const {
    if (!pending_.active || pending_.doseDg == 0 || recipeId != pending_.recipeId) return 0;
    const int32_t after = static_cast<int32_t>(shotStartMs_ - pending_.atMs);
    if (after < 0 || static_cast<uint32_t>(after) > GRIND_WINDOW_MS) return 0;
    return pending_.doseDg;
  }
  // After the shot is saved: the grind is used up; add-ons that were asked hear about the shot.
  // The report's grindSeq is set here: the grind this shot followed (the same rule as doseForShot).
  void shotSaved(uint16_t recipeId, const ShotReport &given) {
    ShotReport report = given;
    report.grindSeq = grindSeqForShot(recipeId);
    if (pending_.active && recipeId == pending_.recipeId) pending_ = PendingGrind{};
    for (size_t i = 0; i < count_; ++i) {
      Slot &s = slots_[i];
      if (!s.started) continue;
      GrinderDriver *g = s.addon->grinder();
      if (!g || !(g->caps() & SHOT_FEEDBACK)) continue;
      // The owner's switch, or the add-on's own request (a recipe in shadow mode only watches).
      if (s.options.shotFeedback || g->wantsShotReports()) g->onShotComplete(report);
    }
  }
  // The grind a shot on this recipe followed, 0 none. Before shotSaved uses the grind up.
  uint32_t grindSeqForShot(uint16_t recipeId) const {
    if (!pending_.active || recipeId != pending_.recipeId) return 0;
    const int32_t after = static_cast<int32_t>(shotStartMs_ - pending_.atMs);
    if (after < 0 || static_cast<uint32_t>(after) > GRIND_WINDOW_MS) return 0;
    return pending_.seq;
  }
  const PendingGrind &pending() const { return pending_; }

  // The recipe a grinder recipe is linked to for this add-on, 0 none. recipes: the controller's now, so
  // a link to a deleted recipe answers 0.
  uint16_t recipeForGrinderRecipe(size_t i, uint8_t grinderRecipe, const RecipeView *recipes, size_t count) const {
    if (i >= count_ || grinderRecipe == 0 || grinderRecipe > MAX_GRINDER_RECIPES) return 0;
    const uint16_t id = slots_[i].map[grinderRecipe].recipeId;
    for (size_t k = 0; id && k < count; ++k) {
      if (recipes[k].id == id) return id;
    }
    return 0;
  }

  // Map a grinder recipe by hand: recipeId 0 takes the mapping away (and lets it be found again).
  bool setMap(size_t i, uint8_t grinderRecipe, uint16_t recipeId) {
    if (i >= count_ || grinderRecipe == 0 || grinderRecipe > MAX_GRINDER_RECIPES) return false;
    slots_[i].map[grinderRecipe] = recipeId ? MapEntry{recipeId, MAP_MANUAL} : MapEntry{};
    return true;
  }

 private:
  void apply(size_t i, const GrindEvent &ev, Actions &act) {
    Slot &s = slots_[i];
    RecipeView recipes[24];
    const size_t n = act.recipes(recipes, 24);
    s.grind = ev;
    s.hasGrind = true;
    s.plan = planGrind(ev, recipes, n, s.map, s.options, act.grinder(), act.umPerStep());
    const GrindPlan &p = s.plan;
    s.grind.recipeId = p.mapped ? p.recipeId : 0;
    if (p.autoMapped) act.changed();
    if (!p.mapped) {
      act.log(p.note);
      return;
    }
    if (p.adoptSteps && !act.adoptSetting(p.recipeId, p.adoptSteps)) {
      if (!s.plan.note[0]) snprintf(s.plan.note, sizeof(s.plan.note), "no preset of this recipe with the coffee in use");
    }
    if (p.select) act.selectRecipe(p.recipeId);
    pending_.active = true;
    pending_.atMs = ev.atMs;
    pending_.recipeId = p.recipeId;
    pending_.doseDg = p.doseDg;
    pending_.targetG = p.shotTargetG;
    pending_.seq = ev.seq;
    act.setNextShotTarget(p.shotTargetG, p.shotTargetG > 0.0f ? p.recipeId : 0);
    char line[160];
    snprintf(line, sizeof(line), "addon %s: grind for recipe %u: dose %.2f g, target %.2f g, setting %u%s%s",
             s.entry->id, static_cast<unsigned>(p.recipeId), p.doseDg / 10.0f, p.shotTargetG,
             static_cast<unsigned>(p.adoptSteps), p.note[0] ? "; " : "", p.note);
    act.log(line);
  }

  Slot slots_[MAX_ADDONS];
  Host *hosts_[MAX_ADDONS] = {};
  size_t count_ = 0;
  PendingGrind pending_;
  uint32_t shotStartMs_ = 0;
};

}  // namespace addon
}  // namespace bbw
