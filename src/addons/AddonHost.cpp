// The add-ons compiled into this image (docs/ADDONS.md): their host, what is stored for them, and
// their part of the web API. The table comes from scripts/addons.py (.pio/gen/AddonList.h); in the
// public firmware it is empty and everything here is idle.

#include "addons/AddonHost.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#ifndef BBW_EMULATOR
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "AddonList.h"
#include "App.h"
#include "Log.h"
#include "addons/AddonManager.h"
#include "addons/AddonRules.h"
#include "core/GrindAdvisor.h"
#include "platform/BigMem.h"
#include "platform/FsUtil.h"
#include "platform/NetWorker.h"
#include "platform/RamWatch.h"
#include "storage/Store.h"

namespace bbw {
namespace addon {
// The request itself, on the network task (platform/AddonNet.cpp on the board, emu_addonnet.cpp).
void fetchHere(FetchJob &job);
}  // namespace addon
}  // namespace bbw

namespace addons {

using namespace bbw::addon;

namespace {

constexpr const char *STATE_PATH = "/addons.json";
constexpr const char *SECRET_NS = "bbwaddon";  // NVS, not in backups (storage/Backup.cpp NVS_KEYS)
constexpr const char *SECRET_KEY = "secrets";
constexpr size_t STATE_MAX = 8192;

Manager *g_manager = nullptr;  // PSRAM
bool g_dirty = false;
char *g_text = nullptr;         // STATE_MAX, PSRAM

uint32_t chipFree() {
  const bbw::MemoryReport r = bbw::memoryReport();
  return r.known ? r.internalFree : 0;
}

// The newest notice (Host::notice), for the screens.
char g_notice[NOTICE_LEN] = {};
uint32_t g_noticeAtMs = 0;

// ---- an add-on's files: /addons/<id>/<name> ----

// What the add-on's folder holds now, bytes. The sizes are the files' own (a whole block each on the
// partition is not counted: the free-space reserve is far larger than that).
uint32_t folderBytes(const char *id) {
  uint32_t total = 0;
#ifdef BBW_EMULATOR
  struct Ctx {
    char prefix[48];
    uint32_t total;
  } ctx;
  snprintf(ctx.prefix, sizeof(ctx.prefix), "/addons/%s/", id);
  ctx.total = 0;
  LittleFS.list([](const char *path, size_t size, void *c) {
    Ctx &x = *static_cast<Ctx *>(c);
    if (strncmp(path, x.prefix, strlen(x.prefix)) == 0) x.total += static_cast<uint32_t>(size);
  }, &ctx);
  total = ctx.total;
#else
  char dir[64];
  snprintf(dir, sizeof(dir), "/littlefs/addons/%s", id);
  DIR *d = opendir(dir);
  if (!d) return 0;
  char full[160];
  while (struct dirent *e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
    struct stat info;
    if (stat(full, &info) == 0 && S_ISREG(info.st_mode)) total += static_cast<uint32_t>(info.st_size);
  }
  closedir(d);
#endif
  return total;
}

uint32_t fsFree() {
  const size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  return total > used ? static_cast<uint32_t>(total - used) : 0;
}

int32_t sizeOfFile(const char *path) {
  if (!bbw::fileExists(path)) return -1;
  File f = LittleFS.open(path, "r");
  if (!f) return -1;
  const int32_t n = static_cast<int32_t>(f.size());
  f.close();
  return n;
}

// Appends (append = true) or replaces the add-on's file. nullptr: done, else why not.
const char *writeAddonFile(const char *id, const char *name, const char *data, size_t len, bool append) {
  char path[FILE_PATH_MAX + 1];
  if (!addonFilePath(id, name, path, sizeof(path))) return "that file name is not allowed";
  if (len > 0 && !data) return "no data";
  const int32_t now = sizeOfFile(path);
  const uint32_t replaced = append || now < 0 ? 0 : static_cast<uint32_t>(now);
  if (const char *why = fileWriteRefusal(folderBytes(id), replaced, static_cast<uint32_t>(len), fsFree())) return why;
  char dir[40];
  LittleFS.mkdir("/addons");
  snprintf(dir, sizeof(dir), "/addons/%s", id);
  LittleFS.mkdir(dir);
  if (append) {
    File f = LittleFS.open(path, "a");
    if (!f) return "the file could not be opened";
    const bool ok = len == 0 || f.write(reinterpret_cast<const uint8_t *>(data), len) == len;
    f.close();
    return ok ? nullptr : "the write failed";
  }
  char tmp[FILE_PATH_MAX + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  File f = LittleFS.open(tmp, "w");
  if (!f) return "the file could not be opened";
  const bool ok = len == 0 || f.write(reinterpret_cast<const uint8_t *>(data), len) == len;
  f.close();
  if (!ok || !LittleFS.rename(tmp, path)) {
    LittleFS.remove(tmp);
    return "the write failed";
  }
  return nullptr;
}

class SlotHost : public Host {
 public:
  void attach(size_t index) { index_ = index; }
  uint32_t nowMs() const override { return millis(); }
  uint32_t epochS() const override { return app::epochNow(); }
  const char *config(const char *key) const override { return g_manager->value(index_, key); }
  void log(const char *text) override { LOGI("addon %s: %s", g_manager->slot(index_).entry->id, text); }
  MachineState machine() const override { return app::addonMachineState(); }
  GrinderInfo grinder() const override {
    float um = 0.0f;
    return app::addonGrinder(um);
  }
  void *allocate(size_t size) override { return bbw::bigAlloc(size); }
  bool fetchStart(FetchJob &job) override {
    job.problem[0] = '\0';
    if (job.running()) return false;
    if (!g_manager->urlAllowed(index_, job.url)) {
      strlcpy(job.problem, "not this add-on's address", sizeof(job.problem));
      return false;
    }
    if (strncmp(job.url, "https://", 8) == 0 && strlen(job.certSha256) < 64) {
      strlcpy(job.problem, "https needs the certificate's fingerprint", sizeof(job.problem));
      return false;
    }
    job.outLen = 0;
    job.truncated = false;
    job.status = 0;
    job.setCookie[0] = '\0';
    job.state.store(FetchJob::RUNNING);
    if (!bbw::netSubmit([](void *c) {
          bbw::RamJobScope ram(bbw::RAM_ADDON);
          FetchJob &j = *static_cast<FetchJob *>(c);
          bbw::addon::fetchHere(j);
          j.state.store(FetchJob::DONE);
        }, &job)) {
      job.state.store(FetchJob::IDLE);
      strlcpy(job.problem, "the network is busy", sizeof(job.problem));
      return false;
    }
    return true;
  }

  void notice(const char *text) override {
    if (!text || !text[0]) {
      g_notice[0] = '\0';
      return;
    }
    strlcpy(g_notice, text, sizeof(g_notice));
    g_noticeAtMs = millis();
  }
  uint32_t chipFree() const override { return addons::chipFree(); }
  const char *requestShotStart(uint16_t recipeId) override {
    const char *why = app::addonRequestShotStart(recipeId);
    if (why) LOGI("addon %s: shot start refused: %s", id(), why);
    else LOGI("addon %s: shot start asked for recipe %u", id(), static_cast<unsigned>(recipeId));
    return why;
  }
  const char *appendFile(const char *name, const char *data, size_t len) override {
    return writeAddonFile(id(), name, data, len, true);
  }
  const char *writeFile(const char *name, const char *data, size_t len) override {
    return writeAddonFile(id(), name, data, len, false);
  }
  int32_t fileSize(const char *name) const override {
    char path[FILE_PATH_MAX + 1];
    return addonFilePath(id(), name, path, sizeof(path)) ? sizeOfFile(path) : -1;
  }
  int32_t readFile(const char *name, uint32_t offset, uint8_t *out, size_t cap) const override {
    char path[FILE_PATH_MAX + 1];
    if (!out || !addonFilePath(id(), name, path, sizeof(path)) || !bbw::fileExists(path)) return -1;
    File f = LittleFS.open(path, "r");
    if (!f) return -1;
    int32_t n = 0;
    if (offset < f.size() && f.seek(offset)) n = static_cast<int32_t>(f.read(out, cap));
    f.close();
    return n;
  }

  uint16_t recipeForGrinderRecipe(uint8_t grinderRecipe) const override {
    RecipeView recipes[24];
    const size_t n = app::addonRecipes(recipes, 24);
    return g_manager->recipeForGrinderRecipe(index_, grinderRecipe, recipes, n);
  }

 private:
  const char *id() const { return g_manager->slot(index_).entry->id; }
  size_t index_ = 0;
};

SlotHost *g_hosts = nullptr;  // one per add-on, PSRAM

// The controller as the manager may change it.
class ControllerActions : public Actions {
 public:
  bool brewing() const override { return app::addonMachineState().brewing; }
  size_t recipes(RecipeView *out, size_t max) const override { return app::addonRecipes(out, max); }
  GrinderInfo grinder() const override {
    float um = 0.0f;
    return app::addonGrinder(um);
  }
  float umPerStep() const override {
    float um = 0.0f;
    app::addonGrinder(um);
    return um;
  }
  bool adoptSetting(uint16_t recipeId, uint16_t steps) override { return app::addonAdoptSetting(recipeId, steps); }
  void setNextShotTarget(float g, uint16_t recipeId) override { app::addonNextShotTarget(g, recipeId); }
  void selectRecipe(uint16_t recipeId) override { app::addonSelectRecipe(recipeId); }
  void changed() override { g_dirty = true; }
  void log(const char *text) override {
    if (text && text[0]) LOGI("%s", text);
  }
};
ControllerActions g_actions;

const char *typeName(FieldType t) {
  switch (t) {
    case FieldType::NUMBER: return "number";
    case FieldType::BOOL: return "bool";
    case FieldType::URL: return "url";
    case FieldType::SECRET: return "secret";
    default: return "text";
  }
}
const char *linkName(Link l) {
  switch (l) {
    case Link::WAITING: return "waiting";
    case Link::CONNECTED: return "connected";
    case Link::FAILED: return "failed";
    default: return "off";
  }
}

// ---- what is stored ----

void load() {
  JsonDocument doc;
  if (app::store().readText(STATE_PATH, g_text, STATE_MAX) && !deserializeJson(doc, g_text)) {
    JsonObjectConst all = doc["a"];
    for (JsonPairConst kv : all) {
      const int i = g_manager->find(kv.key().c_str());
      if (i < 0) continue;  // an add-on not in this image: its record waits in the file
      Slot &s = g_manager->slot(i);
      JsonObjectConst a = kv.value();
      s.enabled = a["on"] | false;
      s.options.doseSetsTarget = a["o"]["t"] | true;
      s.options.selectRecipe = a["o"]["s"] | false;
      s.options.shotFeedback = a["o"]["f"] | false;
      for (JsonPairConst c : a["c"].as<JsonObjectConst>()) g_manager->setValue(i, c.key().c_str(), c.value() | "");
      for (JsonArrayConst m : a["m"].as<JsonArrayConst>()) {
        const uint8_t r = m[0] | 0;
        if (r >= 1 && r <= MAX_GRINDER_RECIPES) {
          s.map[r].recipeId = m[1] | 0;
          s.map[r].source = m[2] | 0;
        }
      }
    }
  }
  Preferences prefs;
  if (prefs.begin(SECRET_NS, true)) {
    const size_t n = prefs.getBytesLength(SECRET_KEY);
    if (n > 0 && n < STATE_MAX) {
      prefs.getBytes(SECRET_KEY, g_text, n);
      g_text[n] = '\0';
      JsonDocument sec;
      if (!deserializeJson(sec, g_text)) {
        for (JsonPairConst kv : sec.as<JsonObjectConst>()) {
          const int i = g_manager->find(kv.key().c_str());
          if (i < 0) continue;
          for (JsonPairConst c : kv.value().as<JsonObjectConst>()) g_manager->setValue(i, c.key().c_str(), c.value() | "");
        }
      }
    }
    prefs.end();
  }
}

void save() {
  JsonDocument doc, sec;
  // Keep the records of add-ons not in this image (another build may have them).
  JsonDocument old;
  if (app::store().readText(STATE_PATH, g_text, STATE_MAX) && !deserializeJson(old, g_text)) {
    for (JsonPairConst kv : old["a"].as<JsonObjectConst>()) {
      if (g_manager->find(kv.key().c_str()) < 0) doc["a"][kv.key()] = kv.value();
    }
  }
  doc["v"] = 1;
  for (size_t i = 0; i < g_manager->count(); ++i) {
    const Slot &s = g_manager->slot(i);
    JsonObject a = doc["a"][s.entry->id].to<JsonObject>();
    a["on"] = s.enabled;
    a["o"]["t"] = s.options.doseSetsTarget;
    a["o"]["s"] = s.options.selectRecipe;
    a["o"]["f"] = s.options.shotFeedback;
    if (s.addon) {
      const AddonInfo &info = s.addon->info();
      for (uint8_t f = 0; f < info.fieldCount && f < MAX_FIELDS; ++f) {
        if (!s.set[f]) continue;
        if (info.fields[f].type == FieldType::SECRET) sec[s.entry->id][info.fields[f].key] = s.values[f];
        else a["c"][info.fields[f].key] = s.values[f];
      }
    }
    JsonArray m = a["m"].to<JsonArray>();
    for (uint8_t r = 1; r <= MAX_GRINDER_RECIPES; ++r) {
      if (!s.map[r].recipeId) continue;
      JsonArray e = m.add<JsonArray>();
      e.add(r);
      e.add(s.map[r].recipeId);
      e.add(s.map[r].source);
    }
  }
  serializeJson(doc, g_text, STATE_MAX);
  if (!app::store().writeTextIfChanged(STATE_PATH, g_text)) LOGW("addons: %s not saved", STATE_PATH);
  const size_t n = serializeJson(sec, g_text, STATE_MAX);
  Preferences prefs;
  if (prefs.begin(SECRET_NS, false)) {
    prefs.putBytes(SECRET_KEY, g_text, n);
    prefs.end();
  }
}

int slotOf(const char *id, const char *&problem) {
  if (!g_manager) {
    problem = "no add-ons in this firmware";
    return -1;
  }
  const int i = g_manager->find(id);
  if (i < 0) problem = "no such add-on in this firmware";
  return i;
}

bool shotRunning() { return app::addonMachineState().brewing; }

}  // namespace

size_t compiledCount() { return BBW_ADDON_COUNT; }

void begin() {
  if (BBW_ADDON_COUNT == 0) return;  // the public firmware: nothing allocated, nothing running
  g_text = bbw::bigArray<char>(STATE_MAX);
  g_manager = bbw::bigNew<Manager>();
  g_hosts = bbw::bigArray<SlotHost>(MAX_ADDONS);
  for (size_t i = 0; i < MAX_ADDONS; ++i) new (&g_hosts[i]) SlotHost();
  {
    bbw::RamJobScope ram(bbw::RAM_ADDON);
    g_manager->attach(bbw::addon::addonTable(), BBW_ADDON_COUNT);
  }
  for (size_t i = 0; i < g_manager->count(); ++i) {
    g_hosts[i].attach(i);
    g_manager->setHost(i, &g_hosts[i]);
    const Slot &s = g_manager->slot(i);
    LOGI("addon %s: compiled in%s", s.entry->id, s.apiOk ? "" : " (built for another add-on interface: not started)");
  }
  load();
  for (size_t i = 0; i < g_manager->count(); ++i) {
    Slot &s = g_manager->slot(i);
    if (!s.enabled) continue;
    s.enabled = false;
    s.chipBefore = chipFree();
    bbw::RamJobScope ram(bbw::RAM_ADDON);
    g_manager->setEnabled(i, true);
    s.chipAfter = s.chipLow = chipFree();
    LOGI("addon %s: switched on", s.entry->id);
  }
}

void loop() {
  if (!g_manager) return;
  static bool wasShot = false;
  const bool shot = shotRunning();
  if (shot && !wasShot) g_manager->noteShotStart(millis());
  wasShot = shot;
  {
    bbw::RamJobScope ram(bbw::RAM_ADDON);
    g_manager->tick(millis(), g_actions);
  }
  for (size_t i = 0; i < g_manager->count(); ++i) {
    Slot &s = g_manager->slot(i);
    if (!s.started) continue;
    const uint32_t now = chipFree();
    if (now && (s.chipLow == 0 || now < s.chipLow)) s.chipLow = now;
  }
  if (g_dirty && !shot) {
    g_dirty = false;
    save();
  }
}

bool route(bool post, const char *path, const char *body, int &status, const char *&type, const char *&text) {
  if (!g_manager) return false;
  const int i = g_manager->routeFor(path);
  if (i < 0) return false;
  static Response *answer = bbw::bigNew<Response>();  // PSRAM, loop task only
  Response &response = *answer;
  response = Response();
  Request request;
  request.post = post;
  request.path = path;
  request.body = body ? body : "";
  bbw::RamJobScope ram(bbw::RAM_ADDON);
  if (!g_manager->slot(i).addon->handleRoute(request, response)) return false;
  status = response.status;
  type = response.type;
  text = response.body;
  return true;
}

void namesJson(JsonArray out) {
  if (!g_manager) return;
  for (size_t i = 0; i < g_manager->count(); ++i) out.add(g_manager->slot(i).entry->id);
}

void listJson(JsonObject out) {
  out["compiled"] = static_cast<unsigned>(BBW_ADDON_COUNT);
  out["api"] = ADDON_API_VERSION;
  JsonArray list = out["addons"].to<JsonArray>();
  if (!g_manager) return;
  const uint32_t now = millis();
  bbw::addon::RecipeView recipes[24];
  const size_t recipeCount = app::addonRecipes(recipes, 24);
  for (size_t i = 0; i < g_manager->count(); ++i) {
    const Slot &s = g_manager->slot(i);
    JsonObject a = list.add<JsonObject>();
    a["id"] = s.entry->id;
    a["api_ok"] = s.apiOk;
    a["enabled"] = s.enabled;
    if (!s.addon) continue;
    const AddonInfo &info = s.addon->info();
    a["name"] = info.name;
    a["version"] = info.version;
    a["kind"] = info.kind == Kind::GRINDER ? "grinder" : "other";
    char line[160];
    s.addon->statusText(line, sizeof(line));
    a["status"] = line;
    GrinderDriver *g = s.addon->grinder();
    a["link"] = g && s.started ? linkName(g->link()) : "off";
    a["shot_feedback_possible"] = g && (g->caps() & SHOT_FEEDBACK);
    // What it declares for its card (steering modes, the comparison table): the page draws it as given.
    static char *cardText = bbw::bigArray<char>(CARD_JSON_MAX);  // PSRAM, on first use
    if (s.started && cardText) {
      cardText[0] = '\0';
      bbw::RamJobScope cardRam(bbw::RAM_ADDON);
      if (s.addon->cardJson(cardText, CARD_JSON_MAX)) {
        cardText[CARD_JSON_MAX - 1] = '\0';
        JsonDocument card;
        if (!deserializeJson(card, cardText) && card.is<JsonObject>()) a["card"] = card;
      }
    }
    JsonArray routes = a["routes"].to<JsonArray>();
    for (const char *p : s.entry->routes) {
      if (p) routes.add(p);
    }
    JsonArray fields = a["fields"].to<JsonArray>();
    for (uint8_t f = 0; f < info.fieldCount && f < MAX_FIELDS; ++f) {
      JsonObject o = fields.add<JsonObject>();
      o["key"] = info.fields[f].key;
      o["label"] = info.fields[f].label;
      o["type"] = typeName(info.fields[f].type);
      o["help"] = info.fields[f].help;
      if (info.fields[f].type == FieldType::SECRET) o["set"] = s.set[f] && s.values[f][0];
      else o["value"] = g_manager->value(i, info.fields[f].key);
    }
    JsonObject opt = a["options"].to<JsonObject>();
    opt["dose_sets_target"] = s.options.doseSetsTarget;
    opt["select_recipe"] = s.options.selectRecipe;
    opt["shot_feedback"] = s.options.shotFeedback;
    JsonArray map = a["map"].to<JsonArray>();
    for (uint8_t r = 1; r <= MAX_GRINDER_RECIPES; ++r) {
      if (!s.map[r].recipeId) continue;
      JsonObject m = map.add<JsonObject>();
      m["grinder_recipe"] = r;
      m["recipe_id"] = s.map[r].recipeId;
      m["source"] = s.map[r].source == MAP_AUTO ? "auto" : "manual";
      for (size_t k = 0; k < recipeCount; ++k) {
        if (recipes[k].id == s.map[r].recipeId) m["recipe"] = recipes[k].name;
      }
    }
    if (s.hasGrind) {
      JsonObject gr = a["last_grind"].to<JsonObject>();
      gr["age_s"] = (now - s.grind.atMs) / 1000u;
      gr["grinder_recipe"] = s.grind.grinderRecipe;
      if (!isnan(s.grind.doseActualG)) gr["dose_g"] = s.grind.doseActualG;
      if (!isnan(s.grind.doseTargetG)) gr["dose_target_g"] = s.grind.doseTargetG;
      if (!isnan(s.grind.gapUm)) gr["gap_um"] = s.grind.gapUm;
      if (!isnan(s.grind.beverageTargetG)) gr["beverage_target_g"] = s.grind.beverageTargetG;
      gr["mapped"] = s.plan.mapped;
      gr["recipe_id"] = s.plan.recipeId;
      gr["shot_target_g"] = s.plan.shotTargetG;
      gr["setting"] = s.plan.adoptSteps;
      gr["note"] = s.plan.note;
    }
    if (s.chipBefore) {
      a["chip_ram_used"] = s.chipBefore > s.chipAfter ? s.chipBefore - s.chipAfter : 0;
      a["chip_low_during"] = s.chipLow;
    }
  }
}

void diagnosticsJson(JsonObject out) {
  out["compiled"] = static_cast<unsigned>(BBW_ADDON_COUNT);
  if (!g_manager) return;
  JsonArray list = out["addons"].to<JsonArray>();
  for (size_t i = 0; i < g_manager->count(); ++i) {
    const Slot &s = g_manager->slot(i);
    JsonObject a = list.add<JsonObject>();
    a["id"] = s.entry->id;
    a["enabled"] = s.enabled;
    if (s.chipBefore) {
      a["chip_ram_used"] = s.chipBefore > s.chipAfter ? s.chipBefore - s.chipAfter : 0;
      a["chip_low_during"] = s.chipLow;
    }
  }
}

bool enable(const char *id, bool on, const char *&problem) {
  const int i = slotOf(id, problem);
  if (i < 0) return false;
  if (shotRunning()) {
    problem = "not while a shot runs";
    return false;
  }
  Slot &s = g_manager->slot(i);
  if (on && !s.apiOk) {
    problem = "built for another add-on interface";
    return false;
  }
  if (on && !s.started) s.chipBefore = chipFree();
  {
    bbw::RamJobScope ram(bbw::RAM_ADDON);
    g_manager->setEnabled(i, on);
  }
  if (on) s.chipAfter = s.chipLow = chipFree();
  LOGI("addon %s: switched %s", id, on ? "on" : "off");
  save();
  return true;
}

bool configure(const char *id, JsonObjectConst values, JsonObjectConst options, const char *&problem) {
  const int i = slotOf(id, problem);
  if (i < 0) return false;
  for (JsonPairConst kv : values) {
    const char *v = kv.value().is<const char *>() ? kv.value().as<const char *>() : nullptr;
    char number[24];
    if (!v && (kv.value().is<float>() || kv.value().is<bool>())) {
      if (kv.value().is<bool>()) snprintf(number, sizeof(number), "%d", kv.value().as<bool>() ? 1 : 0);
      else snprintf(number, sizeof(number), "%g", kv.value().as<float>());
      v = number;
    }
    if (!v || !g_manager->setValue(i, kv.key().c_str(), v)) {
      problem = "a setting is unknown or too long";
      return false;
    }
  }
  Slot &s = g_manager->slot(i);
  if (options["dose_sets_target"].is<bool>()) s.options.doseSetsTarget = options["dose_sets_target"];
  if (options["select_recipe"].is<bool>()) s.options.selectRecipe = options["select_recipe"];
  if (options["shot_feedback"].is<bool>()) s.options.shotFeedback = options["shot_feedback"];
  save();
  // Settings are read by the add-on when it starts: start it again so they apply.
  if (s.started && !shotRunning()) {
    bbw::RamJobScope ram(bbw::RAM_ADDON);
    g_manager->setEnabled(i, false);
    g_manager->setEnabled(i, true);
  }
  return true;
}

bool map(const char *id, uint8_t grinderRecipe, uint16_t recipeId, const char *&problem) {
  const int i = slotOf(id, problem);
  if (i < 0) return false;
  if (recipeId) {
    bbw::addon::RecipeView recipes[24];
    const size_t n = app::addonRecipes(recipes, 24);
    bool found = false;
    for (size_t k = 0; k < n; ++k) found = found || recipes[k].id == recipeId;
    if (!found) {
      problem = "no such recipe";
      return false;
    }
  }
  if (!g_manager->setMap(i, grinderRecipe, recipeId)) {
    problem = "the grinder recipe is 1 to 8";
    return false;
  }
  save();
  return true;
}

bool command(const char *id, const char *cmd, const char *arg, char *reply, size_t size) {
  const char *problem = "";
  const int i = slotOf(id, problem);
  if (i < 0 || !g_manager->slot(i).started) {
    strlcpy(reply, i < 0 ? problem : "switched off", size);
    return false;
  }
  bbw::RamJobScope ram(bbw::RAM_ADDON);
  return g_manager->slot(i).addon->command(cmd ? cmd : "", arg ? arg : "", reply, size);
}

bool forget(const char *id) {
  const char *problem = "";
  const int i = slotOf(id, problem);
  if (i < 0) return false;
  g_manager->forget(i);
  save();
  return true;
}

bool lastGrind(float &doseG, float &gapUm, uint32_t &ageS) {
  if (!g_manager) return false;
  for (size_t i = 0; i < g_manager->count(); ++i) {
    const Slot &s = g_manager->slot(i);
    if (!s.started || !s.hasGrind) continue;
    doseG = s.grind.doseActualG;
    gapUm = s.grind.gapUm;
    ageS = (millis() - s.grind.atMs) / 1000u;
    return true;
  }
  return false;
}

const char *notice() {
  if (g_notice[0] && millis() - g_noticeAtMs > NOTICE_TTL_MS) g_notice[0] = '\0';
  return g_notice;
}

bool filePath(const char *id, const char *name, char *path, size_t size, const char *&problem) {
  if (!g_manager || g_manager->find(id) < 0) {
    problem = "no such add-on";
    return false;
  }
  if (!addonFilePath(id, name, path, size)) {
    problem = "that file name is not allowed";
    return false;
  }
  return true;
}

uint16_t doseForShot(const bbw::ShotRecord &shot) { return g_manager ? g_manager->doseForShot(shot.recipeId) : 0; }

void shotSaved(const bbw::ShotRecord &shot, float roomTempC) {
  if (!g_manager) return;
  ShotReport r;
  r.shotId = shot.id;
  r.coffeeId = shot.coffeeId;
  r.bagId = shot.bagId;
  r.roomTempC = roomTempC;
  strlcpy(r.coffee, shot.coffee, sizeof(r.coffee));
  r.totalTimeS = shot.shotTimeS;
  r.yieldG = shot.finalWeightG;
  r.weighed = (shot.flags & bbw::SHOT_FLAG_FINAL_VALID) != 0;
  const auto end = static_cast<bbw::EndReason>(shot.endReason);
  r.aborted = !(end == bbw::EndReason::WEIGHT || end == bbw::EndReason::WEIGHT_PREDICTED);
  r.recipeId = shot.recipeId;
  r.targetTimeS = shot.targetTimeS;
  r.timeBasis = shot.timeBasis;
  r.pourTimeS = shot.pourTimeS;
  r.firstDropS = shot.firstDropS;
  // The advice, worked out with this shot included (the save marked it stale; grindAdvice() works it
  // out again), and only when it is for this shot's recipe and coffee.
  const bbw::Recipe active = app::activeRecipe();
  if (active.recipeId == shot.recipeId && active.coffeeId == shot.coffeeId) {
    const bbw::GrindAdvice a = app::grindAdvice();
    AdviceSnapshot &s = r.advice;
    s.valid = true;
    switch (a.action) {
      case bbw::GrindAction::HOLD: s.action = AdviceSnapshot::HOLD; break;
      case bbw::GrindAction::FINER: s.action = AdviceSnapshot::FINER; break;
      case bbw::GrindAction::COARSER: s.action = AdviceSnapshot::COARSER; break;
      default: s.action = AdviceSnapshot::NONE; break;
    }
    s.hasNumber = bbw::adviceHasNumber(a);
    s.currentSteps = a.currentUm;
    s.suggestedSteps = a.suggestedUm;
    float um = 0.0f;
    app::addonGrinder(um);
    s.umPerStep = um;
    s.umPerS = a.umPerS;
    s.stepLimited = a.stepLimited;
    s.counted = bbw::grindShotUse(shot, app::settings().grindTimeBasis) == bbw::GrindUse::COUNTED;
  }
  bbw::RamJobScope ram(bbw::RAM_ADDON);
  g_manager->shotSaved(shot.recipeId, r);
}

}  // namespace addons
