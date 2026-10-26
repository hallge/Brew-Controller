#include "Web.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <LittleFS.h>
#ifndef BBW_EMULATOR
#include <Update.h>
#include <esp_coexist.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#endif

#include "App.h"
#include "Log.h"
#include "Version.h"
#include "ShotFormat.h"
#include "WebPage.h"
#include "ExtensionZip.h"   // made from browser-extension/ by its make_zip.py
#include "core/TasteProfile.h"
#include "core/SettingsUtil.h"
#include "core/Telemetry.h"
#include "platform/BigMem.h"
#include "platform/FirmwareUpdate.h"
#include "platform/FsUtil.h"
#include "ui/Ui.h"

using bbw::State;

namespace web {

namespace {

WebServer server(80);
char apSsid[32];
bool restartPending = false;
uint32_t restartAtMs = 0;
// In PSRAM (platform/BigMem.h), made in begin(): the list a settings request
// reads or sends, MAX_PRESETS long.
bbw::Recipe *g_presetList;

void sendJson(JsonDocument &doc, int code = 200) {
  String body;
  serializeJson(doc, body);
  server.send(code, "application/json", body);
}

void sendOk() { server.send(200, "application/json", "{\"ok\":true}"); }

void sendError(int code, const char *message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(doc, code);
}

bool busyBrewing() {
  const State s = app::status().state;
  return s == State::STARTING_SHOT || s == State::BREWING || s == State::BLOOMING ||
         s == State::STOPPING_SHOT ||
         s == State::POST_SHOT_DRIP;
}

// Letters, digits, space, '-' and '_' only: safe in CSV, JSON and HTML.
void cleanName(const char *in, char *out, size_t size) {
  size_t n = 0;
  for (; in && *in && n + 1 < size; ++in) {
    const char c = *in;
    if (isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') out[n++] = c;
  }
  out[n] = '\0';
}

// Free text (coffee, region, varietal): printable ASCII, which is what the
// device's font can draw, minus the two characters that would need escaping
// in the CSV export.
void cleanText(const char *in, char *out, size_t size) {
  size_t n = 0;
  for (; in && *in && n + 1 < size; ++in) {
    const char c = *in;
    if (c >= 0x20 && c <= 0x7e && c != '"' && c != '\\') out[n++] = c;
  }
  while (n > 0 && out[n - 1] == ' ') --n;
  out[n] = '\0';
}

void handleStatus() {
  const bbw::LiveStatus st = app::status();
  const app::Devices dev = app::devices();
  JsonDocument doc;
  // What this controller speaks (docs/API.md). Additive changes keep apiVersion; a change that breaks a
  // reader raises it. capabilities say which groups of endpoints exist, so a client need not guess from
  // the firmware version.
  doc["api_version"] = 1;
  doc["maintenance_due"] = bbw::upkeepDueCount(app::maintenance());
  {
    JsonArray caps = doc["capabilities"].to<JsonArray>();
    for (const char *c : {"status_v1", "shots_v1", "shot_data_v1", "shot_csv_v1", "grind_advice_v1",
                          "library_v1", "bags_v1", "maintenance_v1", "ratings_refinement_v1", "tds_v1", "shot_context_v1", "shot_prep_v1", "learn_grinder_v1", "diagnostics_v1", "update_v1", "power_events_v1", "coffee_taste_v1", "subscriptions_v1"}) {
      caps.add(c);
    }
  }
  doc["state"] = bbw::stateName(st.state);
  doc["weight_g"] = bbw::round2(st.weightG);
  doc["flow_g_s"] = bbw::round2(st.flowGS);
  doc["shot_time_s"] = bbw::round2(st.shotTimeS);
  doc["target_weight_g"] = st.targetWeightG;
  doc["stop_offset_g"] = bbw::round2(st.stopOffsetG);
  doc["machine_style"] = bbw::machineStyleName(st.machineStyle);
  doc["target_cue"] = st.targetCue;
  doc["awaiting_drip"] = st.awaitingDrip;
  doc["recipe"] = st.recipe;
  doc["scale_connected"] = st.scaleConnected;
  doc["scale_resting"] = app::scaleResting();         // let go when idle so it can switch itself off
  doc["scale_start_pending"] = app::scaleStartPending();  // a start waiting for the scale to come back
  doc["bot_connected"] = st.botConnected;
  doc["bot_paired"] = st.botPaired;
  doc["cup_present"] = st.cupPresent;
  doc["cup_required"] = st.cupRequired;
  if (st.scaleConnected && dev.scale.batteryPercent >= 0) {
    doc["scale_battery"] = dev.scale.batteryPercent;
  }
  doc["weight_fresh"] = st.weightFresh;
  doc["sample_period_ms"] = st.samplePeriodMs;
  doc["manual_stop_needed"] = st.manualStopNeeded;
  if (st.state == State::BLOOMING) doc["bloom_left_s"] = bbw::round2(st.bloomLeftS);
  doc["web_start"] = app::netConfig().webStartEnabled;
  if (dev.bot.valid) {
    doc["bot_battery"] = dev.bot.batteryPercent;
    doc["bot_switch_mode"] = dev.bot.switchMode;
  }
  bbw::ShotRecord shot;
  doc["last_shot_id"] = app::lastShot(shot) ? shot.id : 0;
  sendJson(doc);
}

void handleGetSettings() {
  const bbw::Settings s = app::settings();
  const bbw::NetConfig net = app::netConfig();
  JsonDocument doc;
  doc["learningEnabled"] = s.learningEnabled;
  doc["learningFactor"] = s.learningFactor;
  doc["adaptiveLearning"] = s.adaptiveLearning;
  doc["minValidWeightG"] = s.minValidWeightG;
  doc["scaleTimeoutMs"] = s.scaleTimeoutMs;
  doc["dripObserveMs"] = s.dripObserveMs;
  doc["minShotTimeMs"] = s.minShotTimeMs;
  doc["noFlowTimeoutS"] = s.noFlowTimeoutS;
  doc["cupMode"] = static_cast<int>(s.cupMode);
  doc["cupMinWeightG"] = s.cupMinWeightG;
  doc["autoStartAfterTare"] = s.autoStartAfterTare;
  doc["scaleTimer"] = s.scaleTimer;
  doc["requireCup"] = s.requireCup;
  doc["keepPowerBotConnected"] = s.keepPowerBotConnected;
  doc["displayFlipped"] = s.displayFlipped;
  doc["uiTheme"] = s.uiTheme;
  doc["powerOffWithScale"] = s.powerOffWithScale;
  doc["diagnosticsOn"] = !s.diagnosticsOff;
  doc["namedDrinkers"] = s.namedDrinkers;  // colour scheme, 0 Dark .. 5 High contrast (bbw::UiTheme)
  // Minutes idle before the scale is let go so that it can switch itself off; 0: kept connected.
  doc["scaleRestMin"] = s.scaleRestMin == bbw::SCALE_REST_NEVER ? 0 : s.scaleRestMin;
  doc["actuationMode"] = static_cast<int>(s.actuationMode);
  doc["pressDurationMs"] = s.pressDurationMs;
  doc["releaseSettleMs"] = s.releaseSettleMs;
  doc["botHoldSeconds"] = s.botHoldSeconds;
  const uint8_t presetCount = app::presetCount(), activePreset = app::activePreset();
  doc["grindUm"] = app::activeRecipe().grindUm;
  doc["grindToleranceS"] = s.grindToleranceS;
  doc["grindUmPerS"] = s.grindUmPerS;
  doc["grindWindow"] = s.grindWindow;
  doc["grindMaxStepUm"] = s.grindMaxStepUm;
  doc["grindTimeBasis"] = static_cast<int>(s.grindTimeBasis);
  doc["shareEnabled"] = s.shareEnabled;
  doc["shareCoffees"] = s.shareCoffees;
  doc["shareDialIns"] = s.shareDialIns;
  doc["shareShots"] = s.shareShots;
  doc["shareRatings"] = s.shareRatings;
  doc["grindRatedTarget"] = s.grindRatedTarget;
  doc["activeRecipe"] = activePreset;
  doc["maxRecipes"] = bbw::MAX_PRESETS;
  // The controller's list, one flat entry per preset: the form this page's
  // recipe editor works on. /api/library has the three tables behind it.
  // The document points at the text in these: they must outlive it.
  bbw::Recipe *const resolved = g_presetList;
  JsonArray recipes = doc["recipes"].to<JsonArray>();
  for (uint8_t i = 0; i < presetCount && i < bbw::MAX_PRESETS; ++i) {
    resolved[i] = app::preset(i);
    const bbw::Recipe &p = resolved[i];
    JsonObject r = recipes.add<JsonObject>();
    // Which preset this is, handed back on save so an edit is told from a new
    // entry and what was learned stays with it.
    r["slot"] = i + 1;
    r["recipeId"] = p.recipeId;
    r["coffeeId"] = p.coffeeId;
    r["hidden"] = !app::presetVisible(i);  // its coffee's bag is finished
    r["name"] = p.name;
    r["targetWeightG"] = p.targetWeightG;
    r["baselineOffsetG"] = p.baselineOffsetG;
    r["stopOffsetG"] = p.stopOffsetG;
    r["maxShotTimeS"] = p.maxShotTimeS;
    r["timeLimit"] = static_cast<int>(p.timeLimit);
    r["useRatio"] = p.useRatio;
    r["doseG"] = p.doseG;
    r["ratio"] = p.ratio;
    r["targetTimeS"] = p.targetTimeS;
    r["grindUm"] = p.grindUm;
    r["coffee"] = p.coffee;
    r["region"] = p.region;
    r["varietal"] = p.varietal;
    r["roastLevel"] = p.roastLevel;
    r["roastDate"] = p.roastDate;
    r["preinfusion"] = static_cast<int>(p.preinfusion);
    r["preinfusionHoldS"] = p.preinfusionHoldMs / 1000.0f;
    r["bloomPauseS"] = p.bloomPauseMs / 1000.0f;
    // Learning factor in force for this preset (adaptive mode may have moved it).
    r["learningFactor"] =
        s.adaptiveLearning && p.learnedAlpha ? p.learnedAlpha / 100.0f : s.learningFactor;
  }
  // Passwords are write-only.
  doc["net"]["staSsid"] = net.staSsid;
  doc["net"]["webStartEnabled"] = net.webStartEnabled;
  sendJson(doc);
}

void handlePostSettings() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  bbw::Settings s = app::settings();
  const bbw::Settings before = s;
  s.learningEnabled = doc["learningEnabled"] | s.learningEnabled;
  s.learningFactor = doc["learningFactor"] | s.learningFactor;
  s.adaptiveLearning = doc["adaptiveLearning"] | s.adaptiveLearning;
  // A new baseline or a mode change restarts the adaptive factor from it.
  const bool alphaReset = s.learningFactor != before.learningFactor ||
                          s.adaptiveLearning != before.adaptiveLearning;
  s.minValidWeightG = doc["minValidWeightG"] | s.minValidWeightG;
  s.scaleTimeoutMs = doc["scaleTimeoutMs"] | s.scaleTimeoutMs;
  s.dripObserveMs = doc["dripObserveMs"] | s.dripObserveMs;
  s.minShotTimeMs = doc["minShotTimeMs"] | s.minShotTimeMs;
  s.noFlowTimeoutS = doc["noFlowTimeoutS"] | s.noFlowTimeoutS;
  s.cupMode = static_cast<bbw::CupMode>(doc["cupMode"] | static_cast<int>(s.cupMode));
  s.cupMinWeightG = doc["cupMinWeightG"] | s.cupMinWeightG;
  s.autoStartAfterTare = doc["autoStartAfterTare"] | s.autoStartAfterTare;
  s.scaleTimer = doc["scaleTimer"] | s.scaleTimer;
  s.requireCup = doc["requireCup"] | s.requireCup;
  s.keepPowerBotConnected = doc["keepPowerBotConnected"] | s.keepPowerBotConnected;
  s.displayFlipped = doc["displayFlipped"] | s.displayFlipped;
  s.powerOffWithScale = doc["powerOffWithScale"] | s.powerOffWithScale;
  s.namedDrinkers = doc["namedDrinkers"] | s.namedDrinkers;
  if (doc["diagnosticsOn"].is<bool>()) s.diagnosticsOff = !doc["diagnosticsOn"].as<bool>();
  if (doc["uiTheme"].is<int>()) {
    const int theme = doc["uiTheme"].as<int>();
    if (theme >= 0 && theme < bbw::UI_THEME_COUNT) s.uiTheme = static_cast<uint8_t>(theme);
  }
  if (doc["scaleRestMin"].is<int>()) {
    const int minutes = doc["scaleRestMin"].as<int>();
    s.scaleRestMin = minutes <= 0 ? bbw::SCALE_REST_NEVER
                     : static_cast<uint8_t>(minutes > bbw::SCALE_REST_MAX_MIN ? bbw::SCALE_REST_MAX_MIN : minutes);
  }
  s.actuationMode =
      static_cast<bbw::ActuationMode>(doc["actuationMode"] | static_cast<int>(s.actuationMode));
  s.pressDurationMs = doc["pressDurationMs"] | s.pressDurationMs;
  s.releaseSettleMs = doc["releaseSettleMs"] | s.releaseSettleMs;
  s.botHoldSeconds = doc["botHoldSeconds"] | s.botHoldSeconds;
  s.grindToleranceS = doc["grindToleranceS"] | s.grindToleranceS;
  s.grindUmPerS = doc["grindUmPerS"] | s.grindUmPerS;
  s.grindWindow = doc["grindWindow"] | s.grindWindow;
  s.grindMaxStepUm = doc["grindMaxStepUm"] | s.grindMaxStepUm;
  s.grindTimeBasis = static_cast<bbw::GrindTimeBasis>(
      doc["grindTimeBasis"] | static_cast<int>(s.grindTimeBasis));

  s.shareEnabled = doc["shareEnabled"] | s.shareEnabled;
  s.shareCoffees = doc["shareCoffees"] | s.shareCoffees;
  s.shareDialIns = doc["shareDialIns"] | s.shareDialIns;
  s.shareShots = doc["shareShots"] | s.shareShots;
  s.shareRatings = doc["shareRatings"] | s.shareRatings;
  s.grindRatedTarget = doc["grindRatedTarget"] | s.grindRatedTarget;

  JsonArrayConst recipes = doc["recipes"];
  if (!recipes.isNull() && recipes.size() >= 1) {
    bbw::Recipe *const list = g_presetList;
    uint8_t slots[bbw::MAX_PRESETS] = {};
    uint8_t count = 0;
    for (JsonObjectConst r : recipes) {
      if (count >= bbw::MAX_PRESETS) break;
      bbw::Recipe recipe;
      slots[count] = r["slot"] | 0;
      cleanName(r["name"] | "Recipe", recipe.name, sizeof(recipe.name));
      recipe.targetWeightG = r["targetWeightG"] | 36.0f;
      recipe.baselineOffsetG = r["baselineOffsetG"] | 1.5f;
      recipe.maxShotTimeS = r["maxShotTimeS"] | 50;
      recipe.timeLimit = static_cast<bbw::TimeLimit>(r["timeLimit"] | 0);
      recipe.useRatio = r["useRatio"] | false;
      recipe.doseG = r["doseG"] | 18.0f;
      recipe.ratio = r["ratio"] | 2.0f;
      recipe.targetTimeS = r["targetTimeS"] | 28.0f;
      recipe.grindUm = r["grindUm"] | 0;
      cleanText(r["coffee"] | "", recipe.coffee, sizeof(recipe.coffee));
      cleanText(r["region"] | "", recipe.region, sizeof(recipe.region));
      cleanText(r["varietal"] | "", recipe.varietal, sizeof(recipe.varietal));
      recipe.roastLevel = r["roastLevel"] | 0;
      recipe.roastDate = r["roastDate"] | 0;
      recipe.preinfusion = static_cast<bbw::Preinfusion>(r["preinfusion"] | 0);
      const float holdS = r["preinfusionHoldS"] | 3.0f;
      recipe.preinfusionHoldMs = static_cast<uint16_t>(
          bbw::clampTo(holdS, 0.3f, bbw::MAX_PREINFUSION_HOLD_MS / 1000.0f) * 1000.0f + 0.5f);
      const float bloomS = r["bloomPauseS"] | 0.0f;
      recipe.bloomPauseMs = bloomS > 0.0f
          ? static_cast<uint16_t>(bbw::clampTo(bloomS, 1.0f, bbw::MAX_BLOOM_PAUSE_MS / 1000.0f) * 1000.0f + 0.5f)
          : 0;
      bbw::sanitizeRecipe(recipe);  // ratio mode fixes the target here
      // The learned offset is not editable: a preset that is still in the
      // list keeps its own, a new one starts from the baseline.
      list[count++] = recipe;
    }
    if (!app::applyPresetList(list, slots, count, doc["activeRecipe"] | 0)) {
      return sendError(409, "recipes not saved: the recipe or coffee list is full");
    }
  }
  if (alphaReset) app::resetLearnedFactors();
  app::applySettings(s);
  sendOk();
}

void handleShots() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  uint32_t limit = server.hasArg("limit") ? server.arg("limit").toInt() : 50;
  limit = bbw::clampTo<uint32_t>(limit, 1, bbw::Store::HISTORY_CAPACITY);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  bbw::ShotRecord shot;
  for (uint32_t i = 0; i < limit && app::store().readShot(i, shot); ++i) {
    JsonDocument doc;
    bbw::shotToJson(shot, doc.to<JsonObject>());
    doc["has_data"] = app::store().hasCurve(shot.id);
    {
      bbw::ShotContext context;
      if (app::store().loadContext(shot.id, context)) {
        doc["dose_weighed_g"] = context.doseActualDg / 10.0f;   // 0: not weighed
        doc["purged"] = (context.prepFlags & bbw::CTX_PREP_PURGED) != 0;
        doc["uneven"] = (context.prepFlags & bbw::CTX_PREP_UNEVEN) != 0;
      }
    }
    String line;
    serializeJson(doc, line);
    if (i) server.sendContent(",");
    server.sendContent(line);
  }
  server.sendContent("]");
  server.sendContent("");
}

// The circumstances of the newest shots (core/ShotContext.h), joined to the shots by `shot_id`.
void handleContexts() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  uint32_t limit = server.hasArg("limit") ? server.arg("limit").toInt() : 100;
  limit = bbw::clampTo<uint32_t>(limit, 1, bbw::Store::HISTORY_CAPACITY);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  bbw::ShotRecord shot;
  bool first = true;
  for (uint32_t i = 0; i < limit && app::store().readShot(i, shot); ++i) {
    bbw::ShotContext context;
    if (!app::store().loadContext(shot.id, context)) continue;
    JsonDocument doc;
    bbw::shotContextToJson(context, doc.to<JsonObject>());
    String line;
    serializeJson(doc, line);
    if (!first) server.sendContent(",");
    first = false;
    server.sendContent(line);
  }
  server.sendContent("]");
  server.sendContent("");
}

void handleCsv() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  server.sendHeader("Content-Disposition", "attachment; filename=\"shots.csv\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent(bbw::shotCsvHeader());
  bbw::ShotRecord shot;
  char line[480];
  // Oldest first, the natural order for a spreadsheet.
  for (int32_t i = static_cast<int32_t>(app::store().shotCount()) - 1; i >= 0; --i) {
    if (!app::store().readShot(static_cast<uint32_t>(i), shot)) continue;
    bbw::shotCsvLine(shot, line, sizeof(line));
    server.sendContent(line);
  }
  server.sendContent("");
}

// ---- one shot's full data set -------------------------------------------
// Working memory for it; web handlers all run on the loop task.
// In PSRAM (platform/BigMem.h), made in begin().
constexpr size_t SHOT_HEAD_SIZE = 4096;
bbw::CurvePoint *g_points;       // MAX_CURVE_POINTS
float *g_curveT, *g_curveW;      // MAX_CURVE_POINTS each
char *g_shotHead;                // SHOT_HEAD_SIZE: a shot's summary as JSON

struct FindShot {
  uint32_t id;
  bbw::ShotRecord shot;
  bool found;
};

bool findShot(uint32_t id, bbw::ShotRecord &out) {
  static FindShot find;  // a ShotRecord is too big for this task's stack twice over
  find.id = id;
  find.found = false;
  app::store().scanShots(bbw::Store::HISTORY_CAPACITY,
                         [](const bbw::ShotRecord &shot, void *context) {
                           FindShot *f = static_cast<FindShot *>(context);
                           if (shot.id != f->id) return true;
                           f->shot = shot;
                           f->found = true;
                           return false;
                         },
                         &find);
  if (find.found) out = find.shot;
  return find.found;
}

// Loads shot `id` and its readings into the buffers above. 0 with the HTTP
// error already sent if there is nothing to show.
size_t loadShotData(bbw::ShotRecord &shot) {
  if (busyBrewing()) { sendError(409, "not while a shot is running"); return 0; }
  const uint32_t id = static_cast<uint32_t>(server.arg("id").toInt());
  if (!findShot(id, shot)) { sendError(404, "no such shot"); return 0; }
  const size_t n = app::store().loadCurve(id, g_points, bbw::MAX_CURVE_POINTS);
  if (n == 0) { sendError(404, "no data stored for this shot"); return 0; }
  bbw::unpackCurve(g_points, n, g_curveT, g_curveW, bbw::MAX_CURVE_POINTS);
  return n;
}

template <typename Value>
void sendSeries(const char *name, size_t count, Value value) {
  String chunk = ",\"";
  chunk += name;
  chunk += "\":[";
  char number[16];
  for (size_t i = 0; i < count; ++i) {
    const float v = value(i);
    if (i) chunk += ',';
    if (isnan(v)) {
      chunk += "null";
    } else {
      snprintf(number, sizeof(number), "%.2f", static_cast<double>(v));
      chunk += number;
    }
    if (chunk.length() > 1000) {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  chunk += "]";
  server.sendContent(chunk);
}

// Everything known about one shot: the stored summary, every reading, the
// flow at every reading, and what the readings say about themselves.
void handleShotData() {
  static bbw::ShotRecord shot;
  const size_t n = loadShotData(shot);
  if (n == 0) return;
  JsonDocument doc;
  doc["id"] = shot.id;
  bbw::shotToJson(shot, doc["shot"].to<JsonObject>());
  const bbw::ShotDataStats stats = bbw::shotDataStats(g_points, n);
  JsonObject st = doc["data"].to<JsonObject>();
  st["samples"] = stats.samples;
  st["duration_s"] = bbw::round2(stats.durationS);
  st["mean_period_ms"] = roundf(stats.meanPeriodMs);
  st["max_gap_ms"] = roundf(stats.maxGapMs);
  st["gaps_over_500_ms"] = stats.gapsOver500Ms;
  st["timer_samples"] = stats.timerSamples;
  st["timer_span_s"] = bbw::round2(stats.timerSpanS);
  st["clock_span_s"] = bbw::round2(stats.clockSpanS);
  st["timer_lag_s"] = bbw::round2(stats.timerLagS);
  // The flow features again, from the stored readings: the same numbers the
  // history holds unless the analysis has changed since the shot was made.
  const bbw::FlowFeatures flow = bbw::computeFlowFeatures(
      g_curveT, g_curveW, n, shot.firstDropS, shot.shotTimeS, shot.stopWeightG);
  JsonObject an = doc["analysis"].to<JsonObject>();
  an["valid"] = flow.valid;
  if (flow.valid) {
    an["flow_25_g_s"] = bbw::round2(flow.flow25GS);
    an["flow_50_g_s"] = bbw::round2(flow.flow50GS);
    an["flow_75_g_s"] = bbw::round2(flow.flow75GS);
    an["flow_acceleration"] = bbw::round2(flow.acceleration);
    an["flow_roughness_g_s"] = bbw::round2(flow.roughnessGS);
  }
  char *const head = g_shotHead;
  const size_t length = serializeJson(doc, head, SHOT_HEAD_SIZE);
  if (length == 0 || length >= SHOT_HEAD_SIZE - 1) return sendError(500, "shot summary too long");
  head[length - 1] = 0;  // drop the closing brace: the series go inside the same object
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent(head);
  sendSeries("time_s", n, [](size_t i) { return g_curveT[i]; });
  sendSeries("weight_g", n, [](size_t i) { return g_curveW[i]; });
  sendSeries("flow_g_s", n, [n](size_t i) { return bbw::curveFlowAt(g_curveT, g_curveW, n, i); });
  sendSeries("scale_timer_s", n, [](size_t i) {
    return bbw::pointHasTimer(g_points[i]) ? bbw::pointTimerS(g_points[i]) : NAN;
  });
  server.sendContent("}");
  server.sendContent("");
}

void handleShotCsv() {
  static bbw::ShotRecord shot;
  const size_t n = loadShotData(shot);
  if (n == 0) return;
  char line[96];
  snprintf(line, sizeof(line), "attachment; filename=\"shot_%u.csv\"",
           static_cast<unsigned>(shot.id));
  server.sendHeader("Content-Disposition", line);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  String chunk = "time_s,weight_g,flow_g_s,scale_timer_s,scale_flags\r\n";
  for (size_t i = 0; i < n; ++i) {
    const float flow = bbw::curveFlowAt(g_curveT, g_curveW, n, i);
    char flowText[16] = "", timerText[16] = "";
    if (!isnan(flow)) snprintf(flowText, sizeof(flowText), "%.2f", static_cast<double>(flow));
    if (bbw::pointHasTimer(g_points[i])) {
      snprintf(timerText, sizeof(timerText), "%.1f",
               static_cast<double>(bbw::pointTimerS(g_points[i])));
    }
    snprintf(line, sizeof(line), "%.2f,%.2f,%s,%s,%u\r\n", static_cast<double>(g_curveT[i]),
             static_cast<double>(g_curveW[i]), flowText, timerText,
             static_cast<unsigned>(g_points[i].flags));
    chunk += line;
    if (chunk.length() > 1000) {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  server.sendContent(chunk);
  server.sendContent("");
}

void handleLast() {
  JsonDocument doc;
  bbw::ShotRecord shot;
  if (app::lastShot(shot)) {
    bbw::shotToJson(shot, doc["shot"].to<JsonObject>());
    // Decimated copy of the weight curve, taken while idle only.
    if (!busyBrewing()) {
      float *const timeS = g_curveT, *const weightG = g_curveW;
      const size_t n = app::lastCurve(timeS, weightG, bbw::MAX_CURVE_POINTS);
      const size_t step = n > 300 ? (n + 299) / 300 : 1;
      JsonArray t = doc["time_s"].to<JsonArray>();
      JsonArray w = doc["weight_g"].to<JsonArray>();
      for (size_t i = 0; i < n; i += step) {
        t.add(bbw::round2(timeS[i]));
        w.add(bbw::round2(weightG[i]));
      }
    }
  } else if (app::store().readShot(0, shot)) {
    bbw::shotToJson(shot, doc["shot"].to<JsonObject>());
  }
  sendJson(doc);
}

void handleCmd() {
  const String c = server.arg("c");
  // An idle scale is not let go, one let go is taken back (Start does this itself, and starts once
  // the scale is back).
  if (c != "start") app::noteActivity();
  if (c == "stop") {
    app::stop();
  } else if (c == "tare") {
    app::tare();
  } else if (c == "ack") {
    app::acknowledge();
  } else if (c == "press") {
    app::manualPress();
  } else if (c == "start") {
    if (!app::netConfig().webStartEnabled) {
      return sendError(403, "Start from the web page is disabled (Network section)");
    }
    app::start();
  } else {
    return sendError(400, "unknown command");
  }
  sendOk();
}

// The library as it is stored: coffees, recipes and the presets pairing them.
void handleLibrary() {
  static JsonDocument doc;  // built with the library locked, sent afterwards
  static uint32_t outboxSent;
  outboxSent = app::shareStatus().sent;
  doc.clear();
  app::readLibrary([](const bbw::Library &lib, void *) {
    doc["activePreset"] = lib.activePreset;
    doc["maxCoffees"] = bbw::MAX_COFFEES;
    doc["maxRecipes"] = bbw::MAX_RECIPES;
    doc["maxPresets"] = bbw::MAX_PRESETS;
    JsonArray coffees = doc["coffees"].to<JsonArray>();
    for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
      const bbw::Coffee &c = lib.coffees[i];
      JsonObject o = coffees.add<JsonObject>();
      o["id"] = c.id;
      o["name"] = c.name;
      o["roaster"] = c.roaster;
      o["origin"] = c.origin;
      o["region"] = c.region;
      o["producer"] = c.producer;
      o["varietal"] = c.varietal;
      o["process"] = c.process;
      o["notes"] = c.notes;
      o["altitudeM"] = c.altitudeM;
      o["roastLevel"] = c.roastLevel;
      o["roastDate"] = c.roastDate;
      o["frozenDate"] = c.frozenDate;
      o["thawDate"] = c.thawDate;
      o["bagId"] = c.bagId;
      o["finished"] = c.finishedDate;  // YYYYMMDD the bag was finished; 0 = on the shelf
      // Where it stands with the shared library.
      o["share"] = bbw::shareStateName(bbw::coffeeShareState(c, outboxSent));
      o["shareChanged"] = bbw::coffeeChangedSinceShared(c);
      o["reviewNote"] = c.reviewNote;
      o["libraryId"] = c.libraryId;
    }
    JsonArray recipes = doc["recipes"].to<JsonArray>();
    for (uint8_t i = 0; i < lib.recipeCount; ++i) {
      const bbw::RecipeDef &d = lib.recipes[i];
      JsonObject o = recipes.add<JsonObject>();
      o["id"] = d.id;
      o["name"] = d.name;
      o["targetWeightG"] = d.targetWeightG;
      o["useRatio"] = d.useRatio;
      o["doseG"] = d.doseG;
      o["ratio"] = d.ratio;
      o["targetTimeS"] = d.targetTimeS;
      o["preinfusion"] = static_cast<int>(d.preinfusion);
      o["preinfusionHoldS"] = d.preinfusionHoldMs / 1000.0f;
      o["timeLimit"] = static_cast<int>(d.timeLimit);
      o["maxShotTimeS"] = d.maxShotTimeS;
      o["baselineOffsetG"] = d.baselineOffsetG;
      o["bloomPauseS"] = d.bloomPauseMs / 1000.0f;
    }
    auto grinderJson = [](JsonObject g, const bbw::Grinder &grinder) {
      g["id"] = grinder.id;
      g["modelId"] = grinder.modelId;
      g["model"] = grinder.model;
      g["burrs"] = grinder.burrs;
      g["unit"] = grinder.unit;
      g["umPerUnit"] = grinder.umPerUnit;        // microns per STORED step
      g["divisor"] = grinder.divisor;            // stored steps per displayed unit (core/SettingFormat.h)
      g["subSteps"] = grinder.subSteps;          // 0, or the inner ring's steps per outer step
      g["zeroDate"] = grinder.zeroDate;
      g["calibration"] = grinder.calibration;
      g["share"] = bbw::shareStateName(bbw::grinderShareState(grinder, outboxSent));
      g["shareChanged"] = bbw::grinderChangedSinceShared(grinder);
      g["canShare"] = bbw::grinderCanBeShared(grinder);
      g["reviewNote"] = grinder.reviewNote;
      g["libraryId"] = grinder.libraryId;
    };
    JsonArray bags = doc["bags"].to<JsonArray>();
    for (uint8_t i = 0; i < lib.bagCount; ++i) {
      const bbw::Bag &b = lib.bags[i];
      JsonObject o = bags.add<JsonObject>();
      o["id"] = b.id;
      o["coffeeId"] = b.coffeeId;
      o["roastDate"] = b.roastDate;
      o["openedDate"] = b.openedDate;
      o["frozenDate"] = b.frozenDate;
      o["thawDate"] = b.thawDate;
      o["weightG"] = b.weightG;
      const bbw::Coffee *owner = bbw::findCoffee(lib, b.coffeeId);
      o["current"] = owner && owner->bagId == b.id;   // the bag in use
      o["unopened"] = b.unopened == bbw::BAG_UNOPENED;  // waiting on the shelf, nothing tracked yet
      o["onTheWay"] = b.unopened == bbw::BAG_ON_ITS_WAY;   // ordered, not come yet
      o["expectedDate"] = b.expectedDate;
      o["subscriptionId"] = b.subscriptionId;          // the subscription it came with, 0 = none
    }
    grinderJson(doc["grinder"].to<JsonObject>(), lib.grinder);
    if (lib.hasOtherGrinder) grinderJson(doc["otherGrinder"].to<JsonObject>(), lib.otherGrinder);
    JsonArray models = doc["grinderModels"].to<JsonArray>();
    for (size_t i = 0; i < bbw::GRINDER_MODEL_COUNT; ++i) {
      const bbw::GrinderModel &m = bbw::GRINDER_MODELS[i];
      JsonObject o = models.add<JsonObject>();
      o["id"] = m.id;
      o["model"] = m.model;
      o["burrs"] = m.burrs;
      o["unit"] = m.unit;
      o["umPerUnit"] = m.umPerUnit;
      o["divisor"] = m.divisor;
      o["subSteps"] = m.subSteps;
      if (m.maxStep) {
        o["min"] = m.minStep;
        o["max"] = m.maxStep;
      }
      o["libraryId"] = m.libraryId;
    }
    JsonArray presets = doc["presets"].to<JsonArray>();
    for (uint8_t i = 0; i < lib.presetCount; ++i) {
      const bbw::Preset &p = lib.presets[i];
      JsonObject o = presets.add<JsonObject>();
      o["recipeId"] = p.recipeId;
      o["coffeeId"] = p.coffeeId;
      o["grindUm"] = p.grindUm;
      o["stopOffsetG"] = p.stopOffsetG;
      o["learnedAlpha"] = p.learnedAlpha;
      o["hidden"] = !bbw::presetVisible(lib, i);  // its coffee's bag is finished
    }
  }, nullptr);
  sendJson(doc);
  doc.clear();
}

// ---- the library edited one record at a time ------------------------------

void sendId(uint16_t id) {
  JsonDocument doc;
  doc["ok"] = true;
  doc["id"] = id;
  sendJson(doc);
}

// ---- subscriptions (core/Subscriptions.h) --------------------------------

// GET /api/subscriptions: the list, with what the page shows worked out here (days until the next
// date, reminders, how long supplies last at the recent rate of shots, the monthly total).
void handleGetSubscriptions() {
  JsonDocument doc;
  static uint32_t today, sent;
  static float perDay;
  today = bbw::dateFromEpoch(app::epochNow());
  sent = app::outboxSent();
  perDay = app::shotsPerDay();
  doc["today"] = today;
  doc["shots_per_day"] = bbw::round2(perDay);
  doc["max"] = bbw::MAX_SUBSCRIPTIONS;
  static JsonDocument *out;
  out = &doc;
  app::readSubscriptions([](const bbw::Subscription *subs, size_t count, void *) {
    JsonDocument &d = *out;
    d["monthly_cents"] = bbw::monthlyCents(subs, count);
    JsonArray list = d["subscriptions"].to<JsonArray>();
    for (size_t i = 0; i < count; ++i) {
      JsonObject o = list.add<JsonObject>();
      app::subscriptionToJson(subs[i], o);
      const int32_t until = bbw::daysUntilNext(subs[i], today);
      if (until != INT32_MIN) o["days_until"] = until;
      o["due_soon"] = bbw::subscriptionDueSoon(subs[i], today);
      const int32_t left = bbw::stockDaysLeft(subs[i], perDay);
      if (left >= 0) o["stock_days_left"] = left;
      o["stock_short"] = bbw::stockShort(subs[i], perDay, today);
      o["share"] = bbw::shareStateName(bbw::serviceShareState(subs[i], sent));
      o["share_changed"] = bbw::serviceChangedSinceShared(subs[i]);
      o["can_share"] = bbw::serviceCanBeShared(subs[i]);
      o.remove("share_state");
      o.remove("share_seq");
      o.remove("share_hash");
    }
  }, nullptr);
  const app::SubscriptionPrefs prefs = app::subscriptionPrefs();
  doc["country"] = prefs.country;
  JsonArray matters = doc["matters"].to<JsonArray>();
  for (uint8_t i = 0; i < bbw::SERVICE_SOURCING_COUNT; ++i) {
    if ((prefs.matters >> i) & 1u) matters.add(bbw::SERVICE_SOURCING[i]);
  }
  doc["sharing"] = app::settings().shareEnabled;
  doc["share_ratings"] = app::settings().shareEnabled && app::settings().shareRatings;
  sendJson(doc);
}

// POST /api/subscriptions {id?, name, url, ...}: adds one (no id) or replaces it.
void handlePostSubscription() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  bbw::Subscription s;
  app::subscriptionFromJson(doc.as<JsonObjectConst>(), s);
  if (!s.name[0] && !s.url[0]) return sendError(400, "give it a name or a link");
  const uint16_t id = app::saveSubscription(s);
  if (id == 0) return sendError(409, s.id ? "no such subscription" : "the list is full (12)");
  sendId(id);
}

uint16_t subscriptionIdArg() {
  JsonDocument doc;
  if (!deserializeJson(doc, server.arg("plain"))) return doc["id"] | 0;
  return static_cast<uint16_t>(server.arg("id").toInt());
}

void handleDeleteSubscription() {
  if (!app::deleteSubscription(subscriptionIdArg())) return sendError(404, "no such subscription");
  sendOk();
}

void handleSubscriptionArrived() {
  if (!app::subscriptionArrived(subscriptionIdArg())) return sendError(404, "no such subscription");
  sendOk();
}

void handleSubscriptionSkipped() {
  if (!app::subscriptionSkipped(subscriptionIdArg())) return sendError(404, "no such subscription");
  sendOk();
}

// POST /api/subscriptions/preview {url}: the controller reads that page for a card, when it is idle.
// GET: {state: none|waiting|done|failed, url, problem, and what the page said}.
void handlePostPreview() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  const char *url = doc["url"] | "";
  if (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0) {
    return sendError(400, "a link starting https://");
  }
  if (strlen(url) >= 250) return sendError(400, "that link is too long");
  app::requestPagePreview(url);
  sendOk();
}

void handleGetPreview() {
  static char url[256], problem[80];
  static bbw::PageMeta meta;
  const app::PreviewState state = app::pagePreview(url, sizeof(url), meta, problem, sizeof(problem));
  JsonDocument doc;
  static const char *names[] = {"none", "waiting", "done", "failed"};
  doc["state"] = names[static_cast<int>(state)];
  doc["url"] = url;
  doc["problem"] = problem;
  doc["title"] = meta.title;
  doc["site"] = meta.site;
  doc["description"] = meta.description;
  doc["image"] = meta.image;
  doc["icon"] = meta.icon;
  doc["color"] = meta.color;
  doc["platform"] = meta.platform;
  doc["manage_url"] = meta.manageUrl;
  sendJson(doc);
}

void handleSaveCoffee() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  static bbw::Coffee coffee;
  coffee = bbw::Coffee{};
  coffee.id = doc["id"] | 0;
  cleanText(doc["name"] | "", coffee.name, sizeof(coffee.name));
  cleanText(doc["roaster"] | "", coffee.roaster, sizeof(coffee.roaster));
  cleanText(doc["origin"] | "", coffee.origin, sizeof(coffee.origin));
  cleanText(doc["region"] | "", coffee.region, sizeof(coffee.region));
  cleanText(doc["producer"] | "", coffee.producer, sizeof(coffee.producer));
  cleanText(doc["varietal"] | "", coffee.varietal, sizeof(coffee.varietal));
  cleanText(doc["process"] | "", coffee.process, sizeof(coffee.process));
  cleanText(doc["notes"] | "", coffee.notes, sizeof(coffee.notes));
  coffee.altitudeM = doc["altitudeM"] | 0;
  coffee.roastLevel = doc["roastLevel"] | 0;
  coffee.roastDate = doc["roastDate"] | 0;
  coffee.frozenDate = doc["frozenDate"] | 0;
  coffee.thawDate = doc["thawDate"] | 0;
  if (!coffee.name[0]) return sendError(400, "a coffee needs a name");
  static uint16_t saved;
  saved = 0;
  if (!app::editLibrary([](bbw::Library &lib, void *) {
        saved = bbw::saveCoffee(lib, coffee);
        return saved != 0;
      }, nullptr)) {
    return sendError(409, "coffee not saved: the shelf is full, or it is no longer there");
  }
  app::shareCoffee(saved);
  sendId(saved);
}

// Reads a coffee as the page sends it. false, with the error sent, if it
// has no name.
bool readCoffee(JsonDocument &doc, bbw::Coffee &coffee) {
  coffee = bbw::Coffee{};
  coffee.id = doc["id"] | 0;
  cleanText(doc["name"] | "", coffee.name, sizeof(coffee.name));
  cleanText(doc["roaster"] | "", coffee.roaster, sizeof(coffee.roaster));
  cleanText(doc["origin"] | "", coffee.origin, sizeof(coffee.origin));
  cleanText(doc["region"] | "", coffee.region, sizeof(coffee.region));
  cleanText(doc["producer"] | "", coffee.producer, sizeof(coffee.producer));
  cleanText(doc["varietal"] | "", coffee.varietal, sizeof(coffee.varietal));
  cleanText(doc["process"] | "", coffee.process, sizeof(coffee.process));
  cleanText(doc["notes"] | "", coffee.notes, sizeof(coffee.notes));
  coffee.altitudeM = doc["altitudeM"] | 0;
  coffee.roastLevel = doc["roastLevel"] | 0;
  coffee.roastDate = doc["roastDate"] | 0;
  coffee.frozenDate = doc["frozenDate"] | 0;
  coffee.thawDate = doc["thawDate"] | 0;
  if (!coffee.name[0]) {
    sendError(400, "a coffee needs a name");
    return false;
  }
  return true;
}

// A coffee from the shared library put on this shelf.
void handleAdoptCoffee() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  static bbw::Coffee coffee;
  static char libraryId[bbw::LIBRARY_ID_LEN];
  static uint16_t saved;
  if (!readCoffee(doc, coffee)) return;
  char file[bbw::LIBRARY_ID_LEN * 2];
  strlcpy(libraryId, doc["libraryId"] | "", sizeof(libraryId));
  if (!bbw::libraryIdToFile(libraryId, file, sizeof(file))) {
    return sendError(400, "not a coffee from the shared library");
  }
  saved = 0;
  if (!app::editLibrary([](bbw::Library &lib, void *) {
        saved = bbw::adoptCoffee(lib, coffee, libraryId);
        return saved != 0;
      }, nullptr)) {
    return sendError(409, "not added: the shelf is full");
  }
  app::readLibrarySoon();
  sendId(saved);
}

void handleCommunity() {
  const long coffeeId = server.arg("coffee").toInt(), recipeId = server.arg("recipe").toInt();
  if (coffeeId < 1 || coffeeId > 65535 || recipeId < 1 || recipeId > 65535) {
    return sendError(400, "coffee and recipe ids are needed");
  }
  const app::CommunityView view =
      app::community(static_cast<uint16_t>(coffeeId), static_cast<uint16_t>(recipeId));
  JsonDocument doc;
  doc["reachable"] = app::libraryReachable();
  doc["found"] = view.dialin.found;
  if (view.dialin.found) {
    doc["contributors"] = view.dialin.contributors;
    doc["dialins"] = view.dialin.dialins;
    doc["time_s"] = bbw::round2(view.dialin.timeS);
    doc["fineness_um"] = bbw::round2(view.dialin.finenessUm);
    doc["axis_relative"] = view.dialin.axis != 0;  // fineness_um is then not in microns
    doc["recipe_match"] = view.dialin.recipeDistance > 0.0f ? "near" : "exact";
    doc["their_dose_g"] = bbw::round2(view.dialin.theirDoseG);
    doc["their_yield_g"] = bbw::round2(view.dialin.theirYieldG);
    doc["spread_um"] = bbw::round2(view.dialin.spreadUm);
    doc["same_model"] = view.dialin.modelCount;
    doc["their_age_days"] = view.dialin.ageDays;
  }
  doc["coffees_in_common"] = view.fit.known ? view.fit.coffeesInCommon : 0;
  JsonObject start = doc["start"].to<JsonObject>();
  start["basis"] = bbw::startBasisName(view.start.basis);
  start["setting"] = view.start.setting;
  start["range"] = view.start.range;
  start["age_shift"] = view.start.ageShift;
  start["flow_shift"] = view.start.flowShift;
  // The owner's own most similar coffee (core/SimilarCoffee.h): a guess to start from, offered
  // whether or not the shared library knows this coffee.
  const bbw::SimilarStart similar =
      app::similarStart(static_cast<uint16_t>(coffeeId), static_cast<uint16_t>(recipeId));
  JsonObject sim = doc["similar"].to<JsonObject>();
  sim["found"] = similar.found;
  if (similar.found) {
    sim["setting"] = similar.setting;
    sim["range"] = similar.range;
    sim["from_coffee"] = similar.fromName;
    sim["from_setting"] = similar.fromSetting;
    sim["shift"] = similar.shift;
    sim["basis"] = similar.fitted ? "fitted" : "nearest";   // fitted: moved by the owner's own coffees
    sim["samples"] = similar.samples;                        // coffees the fit or the range rests on
    sim["applied"] = similar.applied;                        // factors that moved it: 1 roast, 2 altitude, 4 process
    sim["hint_roast"] = similar.hintRoast;                   // -1 finer, +1 coarser than the nearest, by convention
    sim["hint_altitude"] = similar.hintAltitude;
    sim["hint_process"] = similar.hintProcess;
    sim["near_recipe"] = similar.nearRecipe;
    sim["from_settled"] = similar.fromSettled;
    sim["verified"] = false;                                 // no published size for any factor (SimilarCoffee.h)
  }
  doc["my_age_days"] = view.myAgeDays;
  doc["age_rate_known"] = view.age.known;
  if (view.age.known) doc["um_per_day"] = bbw::round2(view.age.umPerDay);
  sendJson(doc);
}

void handleSaveRecipe() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  static bbw::RecipeDef def;
  def = bbw::RecipeDef{};
  def.id = doc["id"] | 0;
  cleanName(doc["name"] | "Recipe", def.name, sizeof(def.name));
  def.targetWeightG = doc["targetWeightG"] | 36.0f;
  def.useRatio = doc["useRatio"] | false;
  def.doseG = doc["doseG"] | 18.0f;
  def.ratio = doc["ratio"] | 2.0f;
  def.targetTimeS = doc["targetTimeS"] | 28.0f;
  def.preinfusion = static_cast<bbw::Preinfusion>(doc["preinfusion"] | 0);
  const float holdS = doc["preinfusionHoldS"] | 3.0f;
  def.preinfusionHoldMs = static_cast<uint16_t>(
      bbw::clampTo(holdS, 0.3f, bbw::MAX_PREINFUSION_HOLD_MS / 1000.0f) * 1000.0f + 0.5f);
  def.timeLimit = static_cast<bbw::TimeLimit>(doc["timeLimit"] | 0);
  def.maxShotTimeS = doc["maxShotTimeS"] | 50;
  def.baselineOffsetG = doc["baselineOffsetG"] | 1.5f;
  const float bloomS = doc["bloomPauseS"] | 0.0f;
  def.bloomPauseMs = bloomS > 0.0f
      ? static_cast<uint16_t>(bbw::clampTo(bloomS, 1.0f, bbw::MAX_BLOOM_PAUSE_MS / 1000.0f) * 1000.0f + 0.5f)
      : 0;
  static uint16_t saved;
  saved = 0;
  if (!app::editLibrary([](bbw::Library &lib, void *) {
        saved = bbw::saveRecipeDef(lib, def);
        return saved != 0;
      }, nullptr)) {
    return sendError(409, "recipe not saved: the list is full, or it is no longer there");
  }
  sendId(saved);
}

void handleShare() {
  const app::ShareStatus st = app::shareStatus();
  JsonDocument doc;
  doc["key_ready"] = st.keyReady;
  doc["key_id"] = st.keyId;
  doc["algorithm"] = st.algorithm;
  // Only ES256 is a signature the library can check.
  doc["real_signatures"] = strcmp(st.algorithm, "ES256") == 0;
  doc["can_send"] = st.canSend;
  // The owner's own repository (empty when sharing goes through the relay), and the relay's host.
  doc["repository"] = st.link.viaRelay ? "" : st.link.repository;
  doc["via_relay"] = st.link.viaRelay;
  doc["relay"] = st.link.viaRelay ? st.link.repository : "";
  doc["configured"] = st.link.configured;
  doc["token_set"] = st.tokenSet;   // whether there is one; never the token itself
  doc["problem"] = st.link.problem;
  doc["via"] = st.link.via;
  // false: what is "sent" goes no further than this PC (the emulator's folder).
  doc["to_library"] = st.link.toLibrary;
  doc["last_answer"] = st.link.lastHttp;
  doc["last_ok"] = st.link.lastOkEpoch;
  doc["waiting"] = st.waiting;
  doc["sent"] = st.sent;
  doc["capacity"] = bbw::Store::OUTBOX_CAPACITY;
  sendJson(doc);
}

// The library's repository and the controller's token for it.
void handleShareLink() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  const char *repo = doc["repository"] | "";
  const char *token = doc["token"] | "";
  // "owner/name": letters, digits, '-', '_', '.', and exactly one '/'.
  size_t slashes = 0, length = strlen(repo);
  for (const char *p = repo; *p; ++p) {
    const char c = *p;
    if (c == '/') ++slashes;
    else if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') slashes = 9;
  }
  if (length != 0 && (slashes != 1 || length > 63 || repo[0] == '/' || repo[length - 1] == '/')) {
    return sendError(400, "the repository is written owner/name");
  }
  const size_t tokenLength = strlen(token);
  if (tokenLength > 127) return sendError(400, "that token is too long");
  for (const char *p = token; *p; ++p) {
    if (*p <= 0x20 || *p > 0x7e) return sendError(400, "a token has no spaces or special characters");
  }
  // A token with no repository would be dropped (there is nothing to keep it for): say so instead.
  if (tokenLength != 0 && length == 0) return sendError(400, "enter the library repository with the token");
  app::setLibraryLink(repo, token);
  sendOk();
}

void handleSaveGrinder() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  static bbw::Grinder grinder;
  static bool rezeroed;
  grinder = bbw::Grinder{};
  grinder.modelId = doc["modelId"] | 0;
  cleanText(doc["model"] | "", grinder.model, sizeof(grinder.model));
  cleanText(doc["burrs"] | "", grinder.burrs, sizeof(grinder.burrs));
  cleanName(doc["unit"] | "um", grinder.unit, sizeof(grinder.unit));
  grinder.umPerUnit = doc["umPerUnit"] | 0.0f;
  grinder.divisor = doc["divisor"] | 1;
  grinder.subSteps = doc["subSteps"] | 0;
  grinder.zeroDate = doc["zeroDate"] | 0;
  rezeroed = doc["rezeroed"] | false;
  // asOther: keep it as the owner's second grinder, beside the one in use.
  static bool asOther;
  asOther = doc["asOther"] | false;
  // From the library's list of grinders: it comes with its entry's id.
  static char libraryId[bbw::LIBRARY_ID_LEN];
  char file[bbw::LIBRARY_ID_LEN * 2];
  strlcpy(libraryId, doc["libraryId"] | "", sizeof(libraryId));
  if (libraryId[0] && !bbw::libraryIdToFile(libraryId, file, sizeof(file))) libraryId[0] = '\0';
  if (!bbw::findGrinderModel(grinder.modelId) && !grinder.model[0]) {
    return sendError(400, "pick a model, or type its name");
  }
  if (!app::editLibrary([](bbw::Library &lib, void *) {
        if (asOther) {
          if (!bbw::addOtherGrinder(lib, grinder)) return false;
          if (libraryId[0]) bbw::markGrinderFromLibrary(lib.otherGrinder, libraryId);
          return true;
        }
        bbw::setGrinder(lib, grinder, rezeroed);
        if (libraryId[0] && strcmp(lib.grinder.libraryId, libraryId) != 0) {
          bbw::markGrinderFromLibrary(lib.grinder, libraryId);
        }
        return true;
      }, nullptr)) {
    return sendError(409, asOther ? "not added: there is a second grinder already" : "grinder not saved");
  }
  sendOk();
}

// POST /api/library/do?what=...: the small changes, all by id or list index.
void handleLibraryDo() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  app::noteActivity();  // a change from the page: an idle scale is not let go
  static String what;
  static long a, b;
  what = server.arg("what");
  a = server.arg("a").toInt();
  b = server.arg("b").toInt();
  if (a < 0 || a > 65535 || b < 0 || b > 65535) return sendError(400, "bad value");
  static const char *error;
  static uint32_t today;
  error = "unknown change";
  today = bbw::dateFromEpoch(app::epochNow());
  const bool ok = app::editLibrary([](bbw::Library &lib, void *) {
    if (what == "coffee_finish") {  // a = coffee id; a new bag (POST /api/bag) puts it back
      error = "no such coffee";
      return bbw::finishCoffee(lib, static_cast<uint16_t>(a), today);
    }
    if (what == "coffee_delete") {
      error = "that coffee is still on the controller's list: take it off first";
      return bbw::removeCoffee(lib, static_cast<uint16_t>(a));
    }
    if (what == "recipe_delete") {
      error = "that recipe is still on the controller's list, or is the only one";
      return bbw::removeRecipeDef(lib, static_cast<uint16_t>(a));
    }
    if (what == "grinder_switch") {
      error = "there is no second grinder to switch to";
      return bbw::swapGrinders(lib);
    }
    if (what == "grinder_remove_other") {
      error = "there is no second grinder";
      return bbw::removeOtherGrinder(lib);
    }
    if (what == "bag_use") {  // a = coffee id, b = bag id
      error = "that is not a bag of this coffee";
      return bbw::useBag(lib, static_cast<uint16_t>(a), static_cast<uint16_t>(b));
    }
    if (what == "bag_arrived") {  // a = bag id: a bag on its way came; unopened on the shelf now
      error = "that bag is not on its way";
      return bbw::bagArrived(lib, static_cast<uint16_t>(a), 0, 0);
    }
    if (what == "bag_open") {  // a = bag id: an unopened bag put in use from today
      error = "that is not an unopened bag";
      return bbw::openBag(lib, static_cast<uint16_t>(a), today);
    }
    if (what == "bag_delete") {  // a = bag id
      error = "the bag in use cannot be removed: put another in use first";
      return bbw::removeBag(lib, static_cast<uint16_t>(a));
    }
    if (what == "preset_add") {  // a = recipe id, b = coffee id (0 = none)
      error = "not added: the list is full (16) or already has it";
      return bbw::addPreset(lib, static_cast<uint16_t>(a), static_cast<uint16_t>(b)) >= 0;
    }
    if (what == "preset_delete") {  // a = index
      error = "not removed: the controller needs at least one entry";
      return a < 256 && bbw::removePreset(lib, static_cast<uint8_t>(a));
    }
    if (what == "preset_select") {
      error = "no such entry, or its coffee's bag is finished";
      if (a >= lib.presetCount || !bbw::presetVisible(lib, static_cast<uint8_t>(a))) return false;
      lib.activePreset = static_cast<uint8_t>(a);
      return true;
    }
    if (what == "preset_grind") {  // a = index, b = microns (0 = not set)
      error = "no such entry, or not 0-2000 microns";
      if (a >= lib.presetCount || b > 2000) return false;
      lib.presets[a].grindUm = static_cast<uint16_t>(b);
      return true;
    }
    return false;
  }, nullptr);
  if (!ok) return sendError(409, error);
  sendOk();
}

// POST /api/bag {coffeeId, roastDate, openedDate?, weightG?}: a new bag of that coffee, in use from now.
// {coffeeId, expectedDate?, weightG?, onTheWay: true, subscriptionId?}: a bag ordered, not come yet.
// {coffeeId, roastDate, weightG?, unopened: true, subscriptionId?}: a bag put on the shelf unopened; the
// coffee keeps the bag it has in use (core/Library.h addUnopenedBag).
// {id, roastDate, openedDate, frozenDate, thawDate, weightG}: correct the dates and weight of a bag.
void handleBag() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  static uint16_t bagId, coffeeId, weight;
  static uint32_t roast, opened, frozen, thaw;
  static uint16_t result, subscriptionId;
  static bool unopened, onTheWay;
  static uint32_t expected;
  unopened = doc["unopened"] | false;
  onTheWay = doc["onTheWay"] | false;
  expected = doc["expectedDate"] | 0;
  subscriptionId = doc["subscriptionId"] | 0;
  bagId = doc["id"] | 0;
  coffeeId = doc["coffeeId"] | 0;
  weight = doc["weightG"] | 0;
  roast = doc["roastDate"] | 0;
  opened = doc["openedDate"] | 0;
  frozen = doc["frozenDate"] | 0;
  thaw = doc["thawDate"] | 0;
  if (bagId == 0 && coffeeId == 0) return sendError(400, "say which coffee, or which bag");
  if (roast != 0 && !bbw::validRoastDate(roast)) return sendError(400, "not a date");
  result = 0;
  if (!app::editLibrary([](bbw::Library &lib, void *) {
        if (bagId != 0) {
          if (!bbw::editBag(lib, bagId, roast, opened, frozen, thaw, weight)) return false;
          result = bagId;
          return true;
        }
        result = onTheWay   ? bbw::addOnTheWayBag(lib, coffeeId, expected, weight, subscriptionId)
                 : unopened ? bbw::addUnopenedBag(lib, coffeeId, roast, weight, subscriptionId)
                            : bbw::startBag(lib, coffeeId, roast, opened, weight);
        return result != 0;
      }, nullptr)) {
    return sendError(409, "not saved: no such coffee or bag, or the list holds only bags in use");
  }
  sendId(result);
}

void handleGetMaintenance() {
  const bbw::Maintenance m = app::maintenance();
  JsonDocument doc;
  doc["shots_total"] = m.shotsTotal;
  doc["due"] = bbw::upkeepDueCount(m);
  JsonArray jobs = doc["jobs"].to<JsonArray>();
  for (size_t i = 0; i < bbw::UPKEEP_COUNT; ++i) {
    const bbw::Upkeep job = static_cast<bbw::Upkeep>(i);
    JsonObject o = jobs.add<JsonObject>();
    o["id"] = bbw::upkeepName(job);
    o["since_shots"] = bbw::shotsSince(m, job);
    o["every_shots"] = m.everyShots[i];            // 0: no reminder
    o["due"] = bbw::upkeepDue(m, job);
    o["last_epoch"] = m.doneAtEpochS[i];           // 0: not known
  }
  sendJson(doc);
}

// POST /api/maintenance?job=backflush&action=done | action=every&every=200
void handlePostMaintenance() {
  bbw::Upkeep job;
  if (!bbw::upkeepFromName(server.arg("job").c_str(), job)) return sendError(400, "job is backflush, descale or grinder_clean");
  const String action = server.arg("action");
  if (action == "done") app::upkeepDone(job);
  else if (action == "every") {
    const long every = server.arg("every").toInt();
    if (every < 0 || every > bbw::UPKEEP_MAX_EVERY) return sendError(400, "every is 0 (never) to 5000 shots");
    app::upkeepEvery(job, static_cast<uint32_t>(every));
  } else {
    return sendError(400, "action is done or every");
  }
  sendOk();
}

void handleGrind() {
  const bbw::GrindAdvice a = app::grindAdvice();
  const bbw::Settings s = app::settings();
  const bbw::Recipe active = app::activeRecipe();
  JsonDocument doc;
  doc["recipe"] = active.name;
  doc["action"] = bbw::grindActionName(a.action);
  doc["current_um"] = a.currentUm;
  doc["suggested_um"] = a.suggestedUm;
  doc["delta_um"] = a.deltaUm;
  doc["target_time_s"] = a.targetTimeS;
  // The base verdict (Grind-by-Sync style, against the recipe's time) and the refinement by the
  // owner's ratings that may sit on top of it: `action` is the refined one only when `refined`.
  doc["base_action"] = bbw::grindActionName(a.baseAction);
  doc["refined"] = a.refined;
  doc["rated_state"] = bbw::ratedStateName(a.rated);
  doc["rated_shots"] = a.ratedShots;
  doc["rated_time_s"] = bbw::round2(a.ratedTimeS);   // median time of the liked shots
  doc["rated_band_s"] = bbw::round2(a.ratedBandS);   // the tighter range around it, +-
  doc["aim_time_s"] = bbw::round2(a.aimTimeS);       // 0 unless refining
  doc["rated_target_on"] = s.grindRatedTarget;
  doc["taste_hint"] = a.tasteHint;   // -1 sour (finer), 0 none, 1 bitter (coarser)
  doc["taste_shots"] = a.tasteShots;
  doc["average_time_s"] = bbw::round2(a.averageTimeS);
  doc["error_s"] = bbw::round2(a.errorS);
  doc["shots_used"] = a.shotsUsed;
  doc["window"] = s.grindWindow;
  doc["tolerance_s"] = s.grindToleranceS;
  doc["time_basis"] = s.grindTimeBasis == bbw::GrindTimeBasis::POUR ? "pour" : "total";
  doc["um_per_s"] = bbw::round2(a.umPerS);
  doc["learned"] = a.learned;
  doc["learn_shots"] = a.learnShots;
  // How far the advice rests on what was learned (research/grind-logic-review.md).
  doc["sensitivity_source"] = a.source == bbw::SensitivitySource::MEASURED ? "measured"
                              : a.source == bbw::SensitivitySource::GRINDER ? "grinder" : "guess";
  doc["has_number"] = bbw::adviceHasNumber(a);
  doc["data_weight"] = bbw::round2(a.dataWeight);
  doc["bracketed"] = a.bracketed;
  doc["between_steps"] = a.betweenSteps;
  doc["out_of_range"] = a.outOfRange;
  {
    const bbw::GrinderElasticity el = app::grinderElasticity();
    if (el.known) {
      doc["grinder_elasticity"] = bbw::round2(el.eps);
      doc["grinder_elasticity_se"] = bbw::round2(el.se);
      doc["grinder_shots"] = el.shots;
      doc["grinder_coffees"] = el.groups;
    }
  }
  doc["step_limited"] = a.stepLimited;
  doc["outliers_ignored"] = a.outliersIgnored;
  doc["min_time_s"] = bbw::round2(a.minTimeS);
  doc["max_time_s"] = bbw::round2(a.maxTimeS);
  doc["coffee"] = active.coffee;
  // Bean age, and what it means for the grind since this was last brewed.
  const app::BeanAge age = app::beanAge();
  doc["bean_age_days"] = age.ageDays;
  doc["bean_frozen"] = age.frozenNow;
  doc["bean_frozen_days"] = age.frozenDays;
  doc["days_since_brewed"] = age.daysSinceBrewed;
  doc["age_rate_known"] = age.rateKnown;
  doc["age_shift"] = age.shift;
  sendJson(doc);
}

void handleDevices() {
  const app::Devices dev = app::devices();
  JsonDocument doc;
  doc["scale_connected"] = dev.scaleConnected;
  doc["scale_name"] = dev.scaleName;
  doc["scale_protocol"] = dev.scaleProtocol;
  doc["bot_connected"] = dev.botConnected;
  if (dev.bot.valid) {
    char fw[8];
    snprintf(fw, sizeof(fw), "%u.%u", dev.bot.firmwareTenths / 10, dev.bot.firmwareTenths % 10);
    doc["bot_firmware"] = fw;
    doc["bot_battery"] = dev.bot.batteryPercent;
    doc["bot_switch_mode"] = dev.bot.switchMode;
    doc["bot_hold_s"] = dev.bot.holdSeconds;
  }
  // What the scale link has delivered since it connected.
  // The brew bot's make and what it can do, known once it has connected.
  doc["bot_protocol"] = dev.botProtocol;
  doc["bot_label"] = dev.botLabel;
  JsonArray botCaps = doc["bot_caps"].to<JsonArray>();
  for (uint32_t bit = 1; bit != 0 && bit <= dev.botCaps; bit <<= 1) {
    if (dev.botCaps & bit) botCaps.add(bbw::pusherCapName(static_cast<bbw::PusherCap>(bit)));
  }
  if (dev.botProblem) doc["bot_problem"] = dev.botProblem;
  // The power bot.
  doc["power_connected"] = dev.powerConnected;
  doc["power_protocol"] = dev.powerProtocol;
  doc["power_label"] = dev.powerLabel;
  if (dev.power.valid) {
    doc["power_battery"] = dev.power.batteryPercent;
    char pfw[8];
    snprintf(pfw, sizeof(pfw), "%u.%u", dev.power.firmwareTenths / 10, dev.power.firmwareTenths % 10);
    doc["power_firmware"] = pfw;
    doc["power_switch_mode"] = dev.power.switchMode;
    doc["power_hold_s"] = dev.power.holdSeconds;
  }
  if (dev.powerProblem) doc["power_problem"] = dev.powerProblem;
  doc["pairing"]["power"] = dev.powerAddress;
  // The stop bot.
  doc["stop_connected"] = dev.stopConnected;
  doc["stop_label"] = dev.stopLabel;
  if (dev.stop.valid) doc["stop_battery"] = dev.stop.batteryPercent;
  if (dev.stopProblem) doc["stop_problem"] = dev.stopProblem;
  doc["pairing"]["stop"] = dev.stopAddress;
  // What the connected scale can do; the page leaves out what it cannot.
  JsonArray caps = doc["scale_caps"].to<JsonArray>();
  for (uint32_t bit = 1; dev.scaleConnected && bit != 0 && bit <= dev.scale.caps; bit <<= 1) {
    if (dev.scale.caps & bit) caps.add(bbw::scaleCapName(static_cast<bbw::ScaleCap>(bit)));
  }
  JsonObject sd = doc["scale_data"].to<JsonObject>();
  sd["frames"] = dev.scale.frames;
  sd["weight_frames"] = dev.scale.weightFrames;
  sd["timer_frames"] = dev.scale.timerFrames;
  sd["bad_frames"] = dev.scale.badFrames;
  sd["split_frames"] = dev.scale.splitFrames;
  sd["dropped"] = dev.scale.dropped;
  if (dev.scale.batteryPercent >= 0) sd["battery"] = dev.scale.batteryPercent;
  if (dev.scale.timerRunning >= 0) sd["timer_running"] = dev.scale.timerRunning == 1;
  if (dev.scale.lastKey >= 0) sd["last_key"] = dev.scale.lastKey;
  char hex[2 * bbw::SCALE_MAX_FRAME + 1] = "";
  for (size_t i = 0; i < dev.scale.lastFrameLength; ++i) {
    snprintf(hex + 2 * i, 3, "%02x", dev.scale.lastFrame[i]);
  }
  sd["last_frame"] = hex;
  sd["zeroes_itself"] = app::scaleZeroesItself();
  const bbw::MemoryReport memory = bbw::memoryReport();
  if (memory.known) {
    JsonObject m = doc["memory"].to<JsonObject>();
    m["internal_free"] = memory.internalFree;
    m["internal_lowest"] = memory.internalLowest;
    m["internal_largest"] = memory.internalLargest;
    m["psram_total"] = memory.psramTotal;
    m["psram_free"] = memory.psramFree;
  }
  doc["pairing"]["scale"] = dev.pairing.scaleAddress;
  doc["pairing"]["bot"] = dev.pairing.botAddress;
  bbw::SeenDevice seen[bbw::BleManager::MAX_SEEN];
  const size_t n = app::seenDevices(seen, bbw::BleManager::MAX_SEEN);
  JsonArray list = doc["seen"].to<JsonArray>();
  for (size_t i = 0; i < n; ++i) {
    JsonObject o = list.add<JsonObject>();
    o["kind"] = seen[i].kind == bbw::DeviceKind::BOT ? "bot" : "scale";
    if (seen[i].kind == bbw::DeviceKind::BOT) {
      o["make"] = seen[i].make == bbw::PusherMake::TUYA ? "tuya"
                  : seen[i].make == bbw::PusherMake::MICROBOT ? "microbot" : "switchbot";
    }
    o["name"] = seen[i].name;
    o["address"] = seen[i].address;
    o["type"] = seen[i].addressType;
    o["rssi"] = seen[i].rssi;
  }
  sendJson(doc);
}

// The radio test switch (see bbw::RadioTest). POST any of the fields to
// change them, reset_counts to start measuring afresh; GET (or the POST's
// answer) gives the settings and what was heard since the counts were reset.
// Nothing is stored: a restart brings back the defaults.
void handleRadioTest(bool post) {
  static const char *wifiSleep = "min";
  static const char *coex = "bt";
  if (post) {
    JsonDocument in;
    if (deserializeJson(in, server.arg("plain"))) return sendError(400, "invalid JSON");
    bbw::RadioTest &t = bbw::g_radioTest;
    auto units = [&](const char *key, std::atomic<uint16_t> &field) {
      if (!in[key].is<int>()) return true;
      const int v = in[key];
      if (v < 4 || v > 16384) return false;
      field = static_cast<uint16_t>(v);
      return true;
    };
    if (!units("scan_interval", t.scanInterval) || !units("scan_window", t.scanWindow) ||
        !units("connect_interval", t.connectInterval) || !units("connect_window", t.connectWindow)) {
      return sendError(400, "intervals and windows are 4..16384 (0.625 ms units)");
    }
    if (t.scanWindow.load() > t.scanInterval.load()) t.scanWindow = t.scanInterval.load();
    if (t.connectWindow.load() > t.connectInterval.load()) t.connectWindow = t.connectInterval.load();
    if (in["active"].is<const char *>()) {
      const String a = in["active"].as<const char *>();
      t.active = a == "on" ? 1 : a == "off" ? 0 : -1;
    }
    if (in["always_scan"].is<bool>()) t.alwaysScan = in["always_scan"].as<bool>();
    if (in["hold_connects"].is<bool>()) t.holdConnects = in["hold_connects"].as<bool>();
#ifndef BBW_EMULATOR
    if (in["wifi_sleep"].is<const char *>()) {
      const String w = in["wifi_sleep"].as<const char *>();
      const wifi_ps_type_t ps = w == "none" ? WIFI_PS_NONE : w == "max" ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM;
      if (esp_wifi_set_ps(ps) != ESP_OK) return sendError(409, "Wi-Fi refused that sleep mode");
      wifiSleep = ps == WIFI_PS_NONE ? "none" : ps == WIFI_PS_MAX_MODEM ? "max" : "min";
    }
    if (in["coex"].is<const char *>()) {
      const String c = in["coex"].as<const char *>();
      const esp_coex_prefer_t p = c == "wifi" ? ESP_COEX_PREFER_WIFI
                                  : c == "balance" ? ESP_COEX_PREFER_BALANCE : ESP_COEX_PREFER_BT;
      if (esp_coex_preference_set(p) != ESP_OK) return sendError(409, "coexistence preference refused");
      coex = p == ESP_COEX_PREFER_WIFI ? "wifi" : p == ESP_COEX_PREFER_BALANCE ? "balance" : "bt";
    }
#endif
    ++t.generation;
    if (in["reset_counts"] | false) app::resetHeard();
  }
  const bbw::RadioTest &t = bbw::g_radioTest;
  JsonDocument doc;
  doc["scan_interval"] = t.scanInterval.load();
  doc["scan_window"] = t.scanWindow.load();
  doc["connect_interval"] = t.connectInterval.load();
  doc["connect_window"] = t.connectWindow.load();
  doc["active"] = t.active.load() < 0 ? "auto" : t.active.load() ? "on" : "off";
  doc["always_scan"] = t.alwaysScan.load();
  doc["hold_connects"] = t.holdConnects.load();
  doc["wifi_sleep"] = wifiSleep;
  doc["coex"] = coex;
  app::radioTestReport(doc.as<JsonObject>());
  sendJson(doc);
}

void handlePair() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  const char *kind = doc["kind"] | "";
  const char *address = doc["address"] | "";
  const uint8_t type = doc["type"] | 0;
  if (strlen(address) != 0 && strlen(address) != bbw::ADDR_LEN - 1) {
    return sendError(400, "bad address");
  }
  if (strcmp(kind, "bot") == 0) app::pairBot(address, type);
  else if (strcmp(kind, "power") == 0) app::pairPower(address, type);
  else if (strcmp(kind, "stop") == 0) app::pairStop(address, type);
  else if (strcmp(kind, "scale") == 0) app::pairScale(address, type);
  else return sendError(400, "bad kind");
  sendOk();
}

// The keys a pusher needs. They are write-only: the page is told which are
// set, never what they are.
void handleGetPusher() {
  const app::BotRole role = server.arg("role") == "power" ? app::BotRole::POWER
                            : server.arg("role") == "stop" ? app::BotRole::STOP : app::BotRole::BREW;
  const bool power = role != app::BotRole::BREW;  // a bot whose SwitchBot password is kept with its keys
  const bbw::PusherSecrets secrets = app::pusherSecrets(role);
  JsonDocument doc;
  doc["role"] = role == app::BotRole::POWER ? "power" : role == app::BotRole::STOP ? "stop" : "brew";
  doc["tuya_uuid"] = secrets.tuyaUuid;
  doc["tuya_device_id"] = secrets.tuyaDeviceId;
  doc["tuya_local_key_set"] = secrets.tuyaLocalKey[0] != '\0';
  doc["tuya_variant"] = secrets.tuyaVariant;
  doc["microbot_token_set"] = secrets.microbotTokenSet;
  if (power) doc["password_set"] = secrets.password[0] != '\0';
  sendJson(doc);
}

void handlePostPusher() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  const char *roleName = doc["role"] | "";
  const app::BotRole role = strcmp(roleName, "power") == 0 ? app::BotRole::POWER
                            : strcmp(roleName, "stop") == 0 ? app::BotRole::STOP : app::BotRole::BREW;
  const bool power = role != app::BotRole::BREW;
  bbw::PusherSecrets secrets = app::pusherSecrets(role);
  const char *uuid = doc["tuya_uuid"] | "";
  const char *deviceId = doc["tuya_device_id"] | "";
  const char *localKey = doc["tuya_local_key"] | "";
  const char *token = doc["microbot_token"] | "";
  const char *password = doc["password"] | "";
  if (strlen(uuid) >= sizeof(secrets.tuyaUuid) || strlen(deviceId) >= sizeof(secrets.tuyaDeviceId) ||
      strlen(localKey) >= sizeof(secrets.tuyaLocalKey) || strlen(password) >= sizeof(secrets.password)) {
    return sendError(400, "value too long");
  }
  if (localKey[0] && strlen(localKey) < 6) return sendError(400, "local key too short");
  strlcpy(secrets.tuyaUuid, uuid, sizeof(secrets.tuyaUuid));
  strlcpy(secrets.tuyaDeviceId, deviceId, sizeof(secrets.tuyaDeviceId));
  if (localKey[0]) strlcpy(secrets.tuyaLocalKey, localKey, sizeof(secrets.tuyaLocalKey));
  secrets.tuyaVariant = doc["tuya_variant"] | secrets.tuyaVariant;
  if (token[0]) {
    if (strlen(token) != 32) return sendError(400, "the MicroBot token is 32 hex digits");
    for (size_t i = 0; i < 16; ++i) {
      unsigned value = 0;
      if (sscanf(token + 2 * i, "%2x", &value) != 1) return sendError(400, "the MicroBot token is 32 hex digits");
      secrets.microbotToken[i] = static_cast<uint8_t>(value);
    }
    secrets.microbotTokenSet = true;
  }
  if (doc["forget"] | false) secrets = bbw::PusherSecrets();
  // A SwitchBot password: the power bot's is kept here, the brew bot's with the network settings.
  if (power && password[0]) strlcpy(secrets.password, password, sizeof(secrets.password));
  app::applyPusherSecrets(role, secrets);
  sendOk();
}

void machineToJson(const bbw::MachineProfile &m, JsonObject o) {
  o["maker"] = m.maker;
  o["model"] = m.model;
  o["control"] = m.control;
  o["style"] = bbw::machineStyleName(m.style);
  o["manual_start"] = m.manualStart == static_cast<uint8_t>(bbw::ManualStart::FIRST_DRIP) ? "first_drip" : "tap";
  o["hold_preinfusion"] = m.holdPreinfusion != 0;
  o["tap_preinfusion"] = m.tapPreinfusion != 0;
  o["power_toggle"] = m.powerToggle != 0;
  o["self_stop_s"] = m.selfStopS;
  o["builtin"] = m.builtin;
  o["library_id"] = m.libraryId;
}

void handleGetMachine() {
  const bbw::MachineProfile m = app::machineProfile();
  JsonDocument doc;
  JsonObject current = doc["current"].to<JsonObject>();
  machineToJson(m, current);
  current["can_share"] = bbw::machineCanBeShared(m);
  // Where it stands with the shared library, as for a coffee or a grinder.
  current["share_state"] = bbw::shareStateName(app::machineShareState());
  current["changed_since_shared"] = bbw::machineChangedSinceShared(m);
  if (m.reviewNote[0]) current["review_note"] = m.reviewNote;
  JsonArray builtins = doc["builtins"].to<JsonArray>();
  // Kept until the reply is written: the document points at their text
  // rather than copying it, so a temporary would leave every entry reading
  // as whatever was last on the stack.
  bbw::MachineProfile all[bbw::MACHINE_BUILTIN_COUNT];
  for (size_t i = 0; i < bbw::MACHINE_BUILTIN_COUNT; ++i) {
    all[i] = bbw::machineBuiltin(i);
    machineToJson(all[i], builtins.add<JsonObject>());
  }
  sendJson(doc);
}

// {"builtin": n} picks one that comes with the firmware; otherwise the
// fields describe the owner's own, or one picked from the shared library
// (then with its library_id).
void handlePostMachine() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  bbw::MachineProfile m;
  const int builtin = doc["builtin"] | 0;
  if (builtin >= 1 && builtin <= static_cast<int>(bbw::MACHINE_BUILTIN_COUNT)) {
    m = bbw::machineBuiltin(static_cast<size_t>(builtin - 1));
  } else {
    const char *maker = doc["maker"] | "";
    const char *model = doc["model"] | "";
    const char *control = doc["control"] | "";
    const char *libraryId = doc["library_id"] | "";
    if (!maker[0] || !model[0]) return sendError(400, "maker and model are needed");
    if (strlen(maker) >= sizeof(m.maker) || strlen(model) >= sizeof(m.model) ||
        strlen(control) >= sizeof(m.control) || strlen(libraryId) >= sizeof(m.libraryId)) {
      return sendError(400, "text too long");
    }
    const bbw::MachineProfile before = app::machineProfile();
    strlcpy(m.maker, maker, sizeof(m.maker));
    strlcpy(m.model, model, sizeof(m.model));
    strlcpy(m.control, control, sizeof(m.control));
    m.style = bbw::machineStyleFromName(doc["style"] | "toggle_button");
    m.manualStart = strcmp(doc["manual_start"] | "tap", "first_drip") == 0 ? 1 : 0;
    m.holdPreinfusion = (doc["hold_preinfusion"] | false) ? 1 : 0;
    m.tapPreinfusion = (doc["tap_preinfusion"] | false) ? 1 : 0;
    m.powerToggle = (doc["power_toggle"] | false) ? 1 : 0;
    m.selfStopS = doc["self_stop_s"] | 0;
    m.builtin = 0;
    bbw::sanitizeMachine(m);
    if (libraryId[0]) {
      // Picked from the library's list: published already.
      bbw::markMachineFromLibrary(m, libraryId);
    } else if (before.builtin == 0 && bbw::machineHash(before) == bbw::machineHash(m)) {
      // The same description as before keeps what is known about its sending.
      m.shareState = before.shareState;
      m.shareSeq = before.shareSeq;
      m.shareHash = before.shareHash;
      memcpy(m.reviewNote, before.reviewNote, sizeof(m.reviewNote));
      memcpy(m.libraryId, before.libraryId, sizeof(m.libraryId));
    } else if (before.builtin == 0 && before.shareState != 0) {
      // Changed since it was sent: it keeps its record, and can be sent again.
      m.shareState = before.shareState;
      m.shareSeq = before.shareSeq;
      m.shareHash = before.shareHash;
    }
  }
  if (!app::applyMachine(m)) return sendError(409, "not while a shot is running");
  sendOk();
}

void handleGetPower() {
  const app::PowerView v = app::powerView();
  JsonDocument doc;
  doc["paired"] = v.paired;
  doc["belief"] = v.belief == bbw::PowerBelief::ON ? "on" : v.belief == bbw::PowerBelief::OFF ? "off" : "unknown";
  doc["working"] = v.working;
  doc["last"] = v.lastOutcome;
  doc["last_by_schedule"] = v.lastWasSchedule;
  doc["utc_offset_min"] = v.utcOffsetMin;
  doc["clock_set"] = app::epochNow() != 0;
  JsonArray entries = doc["schedule"].to<JsonArray>();
  for (const bbw::PowerScheduleEntry &e : v.schedule) {
    JsonObject o = entries.add<JsonObject>();
    o["enabled"] = e.enabled != 0;
    o["on"] = e.turnOn != 0;
    o["minute"] = e.minuteOfDay;
    o["days"] = e.days;
  }
  sendJson(doc);
}

// The power presses the controller made in the last `days` days (default 30), oldest first.
void handleGetPowerEvents() {
  long days = server.hasArg("days") ? server.arg("days").toInt() : 30;
  if (days < 1) days = 1;
  if (days > 400) days = 400;
  const uint32_t now = app::epochNow();
  struct Gather {
    bbw::PowerEvent *events;
    uint32_t count;
    uint32_t since;
  };
  Gather gather = {new bbw::PowerEvent[bbw::Store::POWER_EVENT_CAPACITY], 0,
                   now > static_cast<uint32_t>(days) * 86400u ? now - static_cast<uint32_t>(days) * 86400u : 0};
  app::store().scanPowerEvents(
      [](const bbw::PowerEvent &e, void *ctx) {
        Gather &g = *static_cast<Gather *>(ctx);
        if (e.epochS == 0) return true;            // the clock was not set: no place on a calendar
        if (e.epochS < g.since) return false;      // older than asked for: the rest is older still
        g.events[g.count++] = e;
        return g.count < bbw::Store::POWER_EVENT_CAPACITY;
      },
      &gather);
  JsonDocument doc;
  JsonArray out = doc.to<JsonArray>();
  for (uint32_t i = gather.count; i-- > 0;) {
    const bbw::PowerEvent &e = gather.events[i];
    const auto outcome = static_cast<bbw::PowerControl::Outcome>(e.outcome);
    JsonObject o = out.add<JsonObject>();
    o["t"] = e.epochS;
    o["on"] = e.on != 0;
    o["by"] = e.by == static_cast<uint8_t>(bbw::PowerBy::SCHEDULE)    ? "schedule"
              : e.by == static_cast<uint8_t>(bbw::PowerBy::SCALE_OFF) ? "scale"
                                                                       : "owner";
    o["ok"] = outcome == bbw::PowerControl::Outcome::DONE;
    o["sure"] = e.sure != 0;
    o["outcome"] = bbw::PowerControl::outcomeText(outcome);
  }
  delete[] gather.events;
  sendJson(doc);
}

// ---- Coffee taste (core/CoffeeTaste.h, docs/COFFEE-TASTE.md)
void ratingJson(JsonObject o, const bbw::CoffeeRating &r) {
  o["id"] = r.id;
  o["coffee"] = r.coffeeId;
  o["bag"] = r.bagId;
  o["drinker"] = r.drinkerId;
  o["t"] = r.epochS;
  o["stars"] = r.stars;
  JsonArray attr = o["attr"].to<JsonArray>();
  for (uint8_t a : r.attr) attr.add(a);
  o["flavours"] = r.flavours;
  o["share"] = bbw::shareStateName(static_cast<bbw::ShareState>(r.shareState));
}

// Every rating, newest first, with the drinkers' names.
void handleGetRatings() {
  JsonDocument doc;
  doc["named"] = app::settings().namedDrinkers;
  const bbw::Drinkers d = app::store().loadDrinkers();
  JsonArray names = doc["drinkers"].to<JsonArray>();
  for (const auto &n : d.names) names.add(n);
  JsonArray attrs = doc["attrs"].to<JsonArray>();
  for (uint8_t i = 0; i < bbw::TASTE_ATTRS; ++i) attrs.add(bbw::tasteAttrName(i));
  JsonArray families = doc["families"].to<JsonArray>();
  for (uint8_t i = 0; i < bbw::FLAVOUR_COUNT; ++i) families.add(bbw::flavourName(i));
  JsonArray out = doc["ratings"].to<JsonArray>();
  app::store().scanRatings([](const bbw::CoffeeRating &r, void *ctx) {
    ratingJson(static_cast<JsonArray *>(ctx)->add<JsonObject>(), r);
    return true;
  }, &out);
  sendJson(doc);
}

// {id?, coffee, bag, drinker, stars, attr:[5], flavours}: a new rating, or a change to one.
void handlePostRating() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  bbw::CoffeeRating r;
  r.id = doc["id"] | 0u;
  r.coffeeId = doc["coffee"] | 0;
  r.bagId = doc["bag"] | 0;
  r.drinkerId = app::settings().namedDrinkers ? (doc["drinker"] | 0) : 0;
  r.stars = doc["stars"] | 0;
  JsonArrayConst attr = doc["attr"];
  for (uint8_t i = 0; i < bbw::TASTE_ATTRS && i < attr.size(); ++i) r.attr[i] = attr[i] | 0;
  r.flavours = doc["flavours"] | 0;
  r.epochS = app::epochNow();
  if (!app::coffeeExists(r.coffeeId)) return sendError(400, "no such coffee");
  if (!app::store().saveRating(r)) return sendError(400, "stars 1 to 5 are needed (or flash trouble)");
  app::ratingSaved(r.id);
  JsonDocument out;
  out["ok"] = true;
  out["id"] = r.id;
  sendJson(out);
}

// The rated coffees of one drinker (0: the household, everyone's), each as a taste: the rating's
// values, filled in from the coffee's description. Heap, freed by the caller.
struct RatedSet {
  bbw::RatedTaste *items = nullptr;
  size_t n = 0;
  uint8_t drinker = 0;
  bool all = true;
};
RatedSet collectRated(uint8_t drinker) {
  RatedSet set;
  set.drinker = drinker;
  set.all = drinker == 0 || !app::settings().namedDrinkers;
  set.items = new bbw::RatedTaste[bbw::Store::RATING_CAPACITY];
  struct Ctx {
    RatedSet *set;
    bbw::CoffeeRating *raw;
  };
  bbw::CoffeeRating *raw = new bbw::CoffeeRating[bbw::Store::RATING_CAPACITY];
  Ctx ctx = {&set, raw};
  app::store().scanRatings([](const bbw::CoffeeRating &r, void *c) {
    Ctx &x = *static_cast<Ctx *>(c);
    if (!x.set->all && r.drinkerId != x.set->drinker) return true;
    x.raw[x.set->n++] = r;
    return x.set->n < bbw::Store::RATING_CAPACITY;
  }, &ctx);
  app::readLibrary([](const bbw::Library &lib, void *c) {
    Ctx &x = *static_cast<Ctx *>(c);
    size_t kept = 0;
    for (size_t i = 0; i < x.set->n; ++i) {
      const bbw::CoffeeRating &r = x.raw[i];
      const bbw::Coffee *coffee = bbw::findCoffee(lib, r.coffeeId);
      if (!coffee) continue;  // deleted since
      bbw::TasteVector t = bbw::tasteFromRating(r);
      t = bbw::blendTaste(t, bbw::tasteFromDescription(*coffee));
      x.set->items[kept++] = {r.coffeeId, r.stars, r.epochS, t};
    }
    x.set->n = kept;
  }, &ctx);
  delete[] raw;
  return set;
}

void profileJson(JsonObject o, const bbw::TasteProfile &p) {
  o["coffees"] = p.coffees;
  o["ratings"] = p.ratings;
  o["shown"] = p.shown();
  o["settled"] = p.settled();
  JsonArray v = o["v"].to<JsonArray>(), k = o["known"].to<JsonArray>();
  for (uint8_t d = 0; d < bbw::TASTE_DIMS; ++d) {
    v.add(roundf(p.v[d] * 1000) / 1000);
    k.add(roundf(p.known[d] * 1000) / 1000);
  }
}

// A drinker's taste profile (?drinker=N; 0 or none: the household).
void handleGetTasteProfile() {
  const uint8_t drinker = static_cast<uint8_t>(server.arg("drinker").toInt());
  RatedSet set = collectRated(drinker);
  const bbw::TasteProfile p = bbw::buildProfile(set.items, set.n, app::epochNow());
  JsonDocument doc;
  doc["drinker"] = set.all ? 0 : drinker;
  JsonArray dims = doc["dims"].to<JsonArray>();
  for (uint8_t d = 0; d < bbw::TASTE_DIMS; ++d) dims.add(bbw::tasteDimName(d));
  profileJson(doc["profile"].to<JsonObject>(), p);
  // The coffees liked best: each coffee's best rating, highest first.
  JsonArray best = doc["best"].to<JsonArray>();
  for (uint8_t stars = 5; stars >= 4; --stars) {
    for (size_t i = 0; i < set.n && best.size() < 5; ++i) {
      if (set.items[i].stars != stars) continue;
      bool listed = false;
      for (JsonObject b : best) listed = listed || b["coffee"] == set.items[i].coffeeId;
      if (listed) continue;
      JsonObject b = best.add<JsonObject>();
      b["coffee"] = set.items[i].coffeeId;
      b["stars"] = stars;
    }
  }
  delete[] set.items;
  sendJson(doc);
}

// ---- Recommendations (docs/COFFEE-TASTE.md)
constexpr size_t COFFEE_KEY_PART = 24;
// A coffee's name in the library: its entry, else "~roaster/name" in lower case (the library tools'
// taste_key does the same).
void tasteKey(const char *libraryId, const char *roaster, const char *name, char *out, size_t size) {
  if (libraryId && libraryId[0]) {
    strlcpy(out, libraryId, size);
    return;
  }
  char r[COFFEE_KEY_PART], n[COFFEE_KEY_PART];
  auto squeeze = [](const char *in, char *o) {
    size_t k = 0;
    bool space = false;
    for (; in && *in && k + 1 < COFFEE_KEY_PART; ++in) {
      if (isspace(static_cast<unsigned char>(*in))) {
        space = k > 0;
        continue;
      }
      if (space && k + 2 < COFFEE_KEY_PART) o[k++] = ' ';
      space = false;
      o[k++] = static_cast<char>(tolower(static_cast<unsigned char>(*in)));
    }
    o[k] = '\0';
  };
  squeeze(roaster, r);
  squeeze(name, n);
  snprintf(out, size, "~%s/%s", r, n);
}

struct Candidate {
  char key[bbw::LIBRARY_ID_LEN + 8];
  char name[32], roaster[32], origin[24], process[16];
  uint16_t localId = 0;     // on the shelf: the coffee's id
  uint8_t from = 0;         // 0 shelf, 1 library, 2 community only
  bbw::TasteVector taste;
  float communityStars = 0;
  uint16_t communityN = 0;
  float peopleNum = 0, peopleDen = 0;
  float score = 0;
  bbw::TasteMatch match;
};

// Coffees for a drinker (?drinker=N): from the shelf they have not rated, the library's catalogue and
// the library's coffee ratings, ranked by how they match the drinker's profile.
// Why a taste matches: the two values it agrees on most, what you lean towards first (a flavour you
// like, more of a cup value), and a flavour you steer away from that it lacks ("no smoky") only to fill in.
void tasteWhy(JsonArray why, const bbw::TasteProfile &profile, const bbw::TasteVector &taste) {
  float score[bbw::TASTE_DIMS];
  for (uint8_t d = 0; d < bbw::TASTE_DIMS; ++d) {
    const float agree = profile.known[d] * taste.known[d] * (profile.v[d] - 0.5f) * (taste.v[d] - 0.5f);
    const bool liked = d < bbw::TASTE_ATTRS || profile.v[d] > 0.5f;
    score[d] = agree > 0.01f ? agree + (liked ? 1.0f : 0.0f) : -1.0f;
  }
  for (uint8_t n = 0; n < 2; ++n) {
    int best = -1;
    for (uint8_t d = 0; d < bbw::TASTE_DIMS; ++d) {
      if (score[d] > 0 && (best < 0 || score[d] > score[best])) best = d;
    }
    if (best < 0) break;
    score[best] = -1.0f;
    char t[24];
    if (best < bbw::TASTE_ATTRS) {
      snprintf(t, sizeof(t), "%s %s", profile.v[best] > 0.5f ? "more" : "less", bbw::tasteDimName(best));
    } else if (profile.v[best] < 0.5f) {
      snprintf(t, sizeof(t), "no %s", bbw::tasteDimName(best));
    } else {
      strlcpy(t, bbw::tasteDimName(best), sizeof(t));
    }
    why.add(t);
  }
}

// ---- subscription services: the library's list, reviews, recommendations ----

const char *SOURCING_WORDS[bbw::SERVICE_SOURCING_COUNT] = {"fair trade", "direct trade", "organic", "farms named",
                                                           "prices published", "sustainable packaging",
                                                           "carbon neutral", "B Corp"};

// GET /api/subscriptions/library: the library's list of services as cached (index/services.json).
void handleGetServiceLibrary() {
  const char *text = app::communityServices();
  server.send(200, "application/json", text && text[0] ? text : "{\"v\":1,\"services\":[]}");
}

// POST /api/subscriptions/share {id}: the service's description goes to the library for review.
void handleShareService() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  if (!app::settings().shareEnabled) return sendError(409, "sharing is off (Setup, Shared library)");
  if (!app::shareService(doc["id"] | 0)) {
    return sendError(409, "not sent: it needs a name and an https link, or it was sent already unchanged");
  }
  sendOk();
}

// POST /api/subscriptions/prefs {country, matters:[...]}
void handleSubscriptionPrefs() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  app::SubscriptionPrefs p = app::subscriptionPrefs();
  if (doc["country"].is<const char *>()) strlcpy(p.country, doc["country"] | "", sizeof(p.country));
  if (doc["matters"].is<JsonArrayConst>()) {
    p.matters = 0;
    for (JsonVariantConst v : doc["matters"].as<JsonArrayConst>()) {
      const int k = bbw::listIndex(bbw::SERVICE_SOURCING, bbw::SERVICE_SOURCING_COUNT, v | "");
      if (k >= 0) p.matters |= static_cast<uint16_t>(1u << k);
    }
  }
  app::setSubscriptionPrefs(p);
  sendOk();
}

// GET /api/subscriptions/reviews: every review on this controller.
void handleGetServiceReviews() {
  JsonDocument doc;
  static JsonArray list;
  list = doc["reviews"].to<JsonArray>();
  app::readServiceReviews([](const bbw::ServiceReview *rs, size_t n, void *) {
    for (size_t i = 0; i < n; ++i) {
      JsonObject o = list.add<JsonObject>();
      o["id"] = rs[i].id;
      o["subscription"] = rs[i].subscriptionId;
      o["drinker"] = rs[i].drinkerId;
      o["stars"] = rs[i].stars;
      o["at"] = rs[i].epochS;
      JsonObject a = o["aspects"].to<JsonObject>();
      for (uint8_t k = 0; k < bbw::SERVICE_ASPECTS; ++k) {
        if (rs[i].aspect[k]) a[bbw::SERVICE_ASPECT_NAMES[k]] = rs[i].aspect[k];
      }
      o["note"] = rs[i].note;
      o["share"] = bbw::shareStateName(static_cast<bbw::ShareState>(rs[i].shareState));
    }
  }, nullptr);
  sendJson(doc);
}

// POST /api/subscriptions/review {subscription, drinker, stars, aspects:{value:4,...}, note}
void handlePostServiceReview() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  bbw::ServiceReview r;
  r.subscriptionId = doc["subscription"] | 0;
  r.drinkerId = doc["drinker"] | 0;
  if (!app::settings().namedDrinkers) r.drinkerId = 0;
  const int stars = doc["stars"] | 0;
  if (stars < 1 || stars > 5) return sendError(400, "give it 1 to 5 stars");
  r.stars = static_cast<uint8_t>(stars);
  for (uint8_t k = 0; k < bbw::SERVICE_ASPECTS; ++k) {
    const int v = doc["aspects"][bbw::SERVICE_ASPECT_NAMES[k]] | 0;
    r.aspect[k] = v >= 1 && v <= 5 ? static_cast<uint8_t>(v) : 0;
  }
  cleanText(doc["note"] | "", r.note, sizeof(r.note));
  const uint32_t id = app::saveServiceReview(r);
  if (id == 0) return sendError(409, "not saved: no such subscription, or the list of reviews is full");
  sendId(static_cast<uint16_t>(id));
}

void handleDeleteServiceReview() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  if (!app::deleteServiceReview(doc["id"] | 0u)) return sendError(404, "no such review");
  sendOk();
}

// GET /api/subscriptions/recommend?drinker=N: services from the library's list the owner does not have,
// that ship to them. Ranked by how the coffees they send taste against the drinker's profile, by
// drinkers like this one, by reviews, and by the sourcing values the owner said matter.
void handleGetServiceRecommend() {
  const uint8_t drinker = static_cast<uint8_t>(server.arg("drinker").toInt());
  RatedSet set = collectRated(drinker);
  const bbw::TasteProfile profile = bbw::buildProfile(set.items, set.n, app::epochNow());
  const app::SubscriptionPrefs prefs = app::subscriptionPrefs();
  JsonDocument doc, taste, list;
  if (deserializeJson(taste, app::communityTaste())) taste.clear();
  if (deserializeJson(list, app::communityServices())) list.clear();
  doc["shown"] = profile.shown();
  doc["coffees"] = profile.coffees;
  doc["country"] = prefs.country;
  JsonArray matters = doc["matters"].to<JsonArray>();
  for (uint8_t i = 0; i < bbw::SERVICE_SOURCING_COUNT; ++i) {
    if ((prefs.matters >> i) & 1u) matters.add(bbw::SERVICE_SOURCING[i]);
  }
  // What the owner has: by library id, or by the shop's host.
  static char haveIds[bbw::MAX_SUBSCRIPTIONS][40], haveHosts[bbw::MAX_SUBSCRIPTIONS][64];
  static size_t haveN;
  haveN = 0;
  auto hostOf = [](const char *url, char *out, size_t size) {
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    if (strncmp(p, "www.", 4) == 0) p += 4;
    size_t n = 0;
    while (p[n] && p[n] != '/' && n + 1 < size) {
      out[n] = p[n];
      ++n;
    }
    out[n] = '\0';
  };
  app::readSubscriptions([](const bbw::Subscription *subs, size_t count, void *) {
    for (size_t i = 0; i < count && haveN < bbw::MAX_SUBSCRIPTIONS; ++i) {
      strlcpy(haveIds[haveN], subs[i].libraryId, sizeof(haveIds[haveN]));
      const char *p = strstr(subs[i].url, "://");
      p = p ? p + 3 : subs[i].url;
      if (strncmp(p, "www.", 4) == 0) p += 4;
      size_t n = 0;
      while (p[n] && p[n] != '/' && n + 1 < sizeof(haveHosts[haveN])) {
        haveHosts[haveN][n] = p[n];
        ++n;
      }
      haveHosts[haveN][n] = '\0';
      ++haveN;
    }
  }, nullptr);
  // My stars per coffee key, for "drinkers like you".
  struct Mine {
    char key[bbw::LIBRARY_ID_LEN + 8];
    uint8_t stars;
  };
  static Mine mine[bbw::MAX_COFFEES];
  static size_t nm;
  static RatedSet *rated;
  nm = 0;
  rated = &set;
  app::readLibrary([](const bbw::Library &lib, void *) {
    for (uint8_t i = 0; i < lib.coffeeCount && nm < bbw::MAX_COFFEES; ++i) {
      uint8_t best = 0;
      for (size_t k = 0; k < rated->n; ++k) {
        if (rated->items[k].coffeeId == lib.coffees[i].id && rated->items[k].stars > best) best = rated->items[k].stars;
      }
      if (!best) continue;
      tasteKey(lib.coffees[i].libraryId, lib.coffees[i].roaster, lib.coffees[i].name, mine[nm].key, sizeof(mine[nm].key));
      mine[nm++].stars = best;
    }
  }, nullptr);
  struct Cand {
    JsonObjectConst entry;
    bbw::TasteVector taste;
    bool haveTaste = false;
    float reviewStars = 0, peopleNum = 0, peopleDen = 0, score = 0;
    uint16_t reviews = 0;
    uint16_t sourcing = 0;
    bbw::TasteMatch match;
  };
  Cand *cands = new Cand[32];
  size_t nc = 0;
  JsonObjectConst serviceTastes = taste["services"];
  bool anyTaste = false;
  for (JsonObjectConst e : list["services"].as<JsonArrayConst>()) {
    const char *id = e["id"] | "";
    if (!id[0] || nc >= 32) continue;
    char host[64];
    hostOf(e["url"] | "", host, sizeof(host));
    bool have = false;
    for (size_t i = 0; i < haveN; ++i) have |= strcmp(haveIds[i], id) == 0 || (host[0] && strcmp(haveHosts[i], host) == 0);
    if (have) continue;
    if (prefs.country[0] && e["ships_to"].is<JsonArrayConst>()) {
      bool ships = false;
      for (JsonVariantConst c : e["ships_to"].as<JsonArrayConst>()) ships |= strcmp(c | "", prefs.country) == 0;
      if (!ships) continue;
    }
    Cand &cd = cands[nc++];
    cd.entry = e;
    for (JsonVariantConst v : e["sourcing"].as<JsonArrayConst>()) {
      const int k = bbw::listIndex(bbw::SERVICE_SOURCING, bbw::SERVICE_SOURCING_COUNT, v | "");
      if (k >= 0) cd.sourcing |= static_cast<uint16_t>(1u << k);
    }
    cd.reviewStars = e["reviews"]["stars"] | 0.0f;
    cd.reviews = e["reviews"]["n"] | 0;
    JsonObjectConst t = serviceTastes[id];
    if (!t.isNull()) {
      float cup[bbw::TASTE_ATTRS], fl[bbw::FLAVOUR_COUNT];
      for (uint8_t a = 0; a < bbw::TASTE_ATTRS; ++a) cup[a] = t["cup"][bbw::tasteAttrName(a)] | 0.0f;
      JsonObjectConst flv = t["flavours"];
      for (uint8_t f = 0; f < bbw::FLAVOUR_COUNT; ++f) fl[f] = flv.isNull() ? -1.0f : (flv[bbw::flavourName(f)] | 0.0f);
      cd.taste = bbw::tasteFromCommunity(cup, fl);
      cd.haveTaste = profile.shown();
      anyTaste |= cd.haveTaste;
    }
  }
  // Drinkers like this one: what they made of each service's coffees.
  for (JsonObjectConst person : taste["people"].as<JsonArrayConst>()) {
    JsonObjectConst r = person["r"];
    uint8_t a[bbw::MAX_COFFEES], b[bbw::MAX_COFFEES];
    for (size_t i = 0; i < nm; ++i) {
      a[i] = mine[i].stars;
      b[i] = r[mine[i].key] | 0;
    }
    const float w = bbw::peopleLikeness(a, b, nm);
    if (w <= 0.0f) continue;
    for (size_t i = 0; i < nc; ++i) {
      char key[48];
      snprintf(key, sizeof(key), "svc:%s", cands[i].entry["id"] | "");
      const uint8_t st = r[key] | 0;
      if (!st) continue;
      cands[i].peopleNum += w * st;
      cands[i].peopleDen += w;
    }
  }
  for (size_t i = 0; i < nc; ++i) {
    Cand &cd = cands[i];
    if (cd.haveTaste) cd.match = bbw::matchTaste(profile, cd.taste);
    const float people = cd.peopleDen > 0.3f ? cd.peopleNum / cd.peopleDen : 0.0f;
    uint16_t met = cd.sourcing & prefs.matters;
    uint8_t mattersMet = 0;
    for (; met; met &= static_cast<uint16_t>(met - 1)) ++mattersMet;
    cd.score = bbw::serviceScore(cd.match, cd.haveTaste, people, cd.reviewStars, mattersMet);
  }
  // By taste once there is any to go on; before that by reviews and the values that matter.
  doc["basis"] = anyTaste ? "taste" : nc ? "reviews" : "none";
  JsonArray items = doc["items"].to<JsonArray>();
  for (uint8_t pick = 0; pick < 5; ++pick) {
    int best = -1;
    for (size_t i = 0; i < nc; ++i) {
      if (cands[i].score > -9.0f && (best < 0 || cands[i].score > cands[best].score)) best = static_cast<int>(i);
    }
    if (best < 0 || (anyTaste && cands[best].score <= 0.02f)) break;
    Cand &cd = cands[best];
    cd.score = -10.0f;
    JsonObject o = items.add<JsonObject>();
    o["service"] = cd.entry;
    JsonArray why = o["why"].to<JsonArray>();
    if (cd.haveTaste && cd.match.score > 0.1f) tasteWhy(why, profile, cd.taste);
    for (uint8_t k = 0; k < bbw::SERVICE_SOURCING_COUNT; ++k) {
      if (((cd.sourcing & prefs.matters) >> k) & 1u) why.add(SOURCING_WORDS[k]);
    }
    if (cd.peopleDen > 0.3f) {
      char t[48];
      snprintf(t, sizeof(t), "drinkers like you gave its coffees %.1f\xE2\x98\x85",
               static_cast<double>(cd.peopleNum / cd.peopleDen));
      o["people"] = t;
    }
    if (cd.reviews) {
      char t[40];
      snprintf(t, sizeof(t), "%.1f\xE2\x98\x85 from %u reviews", static_cast<double>(cd.reviewStars),
               static_cast<unsigned>(cd.reviews));
      o["reviews"] = t;
    }
  }
  delete[] cands;
  delete[] set.items;
  sendJson(doc);
}

void handleGetRecommend() {
  const uint8_t drinker = static_cast<uint8_t>(server.arg("drinker").toInt());
  RatedSet set = collectRated(drinker);
  const bbw::TasteProfile profile = bbw::buildProfile(set.items, set.n, app::epochNow());
  JsonDocument doc;
  doc["drinker"] = set.all ? 0 : drinker;
  doc["shown"] = profile.shown();
  doc["coffees"] = profile.coffees;
  JsonArray items = doc["items"].to<JsonArray>();
  JsonArray again = doc["buy_again"].to<JsonArray>();
  if (!profile.shown()) {
    delete[] set.items;
    return sendJson(doc);
  }
  // The library's coffee ratings and catalogue (each read into its own document: they share a buffer).
  JsonDocument taste, catalog;
  if (deserializeJson(taste, app::communityTaste())) taste.clear();
  if (deserializeJson(catalog, app::communityCoffees())) catalog.clear();
  JsonObjectConst tasteCoffees = taste["coffees"];

  Candidate *cands = new Candidate[64];
  size_t nc = 0;
  // My stars per coffee key, for "drinkers like you".
  struct Mine {
    char key[bbw::LIBRARY_ID_LEN + 8];
    uint8_t stars;
  };
  Mine *mine = new Mine[bbw::MAX_COFFEES];
  size_t nm = 0;
  struct Ctx {
    RatedSet *set;
    Candidate *cands;
    size_t *nc;
    Mine *mine;
    size_t *nm;
    JsonArray *again;
    JsonObjectConst *tasteCoffees;
  } ctx = {&set, cands, &nc, mine, &nm, &again, &tasteCoffees};
  app::readLibrary([](const bbw::Library &lib, void *c) {
    Ctx &x = *static_cast<Ctx *>(c);
    for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
      const bbw::Coffee &coffee = lib.coffees[i];
      uint8_t best = 0;
      for (size_t k = 0; k < x.set->n; ++k) {
        if (x.set->items[k].coffeeId == coffee.id && x.set->items[k].stars > best) best = x.set->items[k].stars;
      }
      char key[bbw::LIBRARY_ID_LEN + 8];
      tasteKey(coffee.libraryId, coffee.roaster, coffee.name, key, sizeof(key));
      if (best) {
        strlcpy(x.mine[*x.nm].key, key, sizeof(x.mine[*x.nm].key));
        x.mine[(*x.nm)++].stars = best;
        if (coffee.finishedDate && best >= 4 && x.again->size() < 3) {
          JsonObject a = x.again->add<JsonObject>();
          a["coffee"] = coffee.id;
          a["name"] = coffee.name;
          a["stars"] = best;
        }
        continue;
      }
      if (coffee.finishedDate || *x.nc >= 64) continue;
      Candidate &cd = x.cands[(*x.nc)++];
      strlcpy(cd.key, key, sizeof(cd.key));
      strlcpy(cd.name, coffee.name, sizeof(cd.name));
      strlcpy(cd.roaster, coffee.roaster, sizeof(cd.roaster));
      strlcpy(cd.origin, coffee.origin, sizeof(cd.origin));
      strlcpy(cd.process, coffee.process, sizeof(cd.process));
      cd.localId = coffee.id;
      cd.from = 0;
      cd.taste = bbw::tasteFromDescription(coffee);
    }
  }, &ctx);
  auto known = [&](const char *key) {
    for (size_t i = 0; i < nc; ++i) if (strcmp(cands[i].key, key) == 0) return true;
    for (size_t i = 0; i < nm; ++i) if (strcmp(mine[i].key, key) == 0) return true;
    return false;
  };
  // The library's catalogue: published coffees not on the shelf.
  for (JsonObjectConst e : catalog["coffees"].as<JsonArrayConst>()) {
    const char *id = e["id"] | "";
    if (!id[0] || known(id) || nc >= 64) continue;
    bbw::Coffee c;
    strlcpy(c.name, e["name"] | "", sizeof(c.name));
    strlcpy(c.roaster, e["roaster"] | "", sizeof(c.roaster));
    strlcpy(c.origin, e["origin"] | "", sizeof(c.origin));
    strlcpy(c.process, e["process"] | "", sizeof(c.process));
    strlcpy(c.notes, e["notes"] | "", sizeof(c.notes));
    c.roastLevel = bbw::roastLevelFromName(e["roast_level"] | "");
    Candidate &cd = cands[nc++];
    strlcpy(cd.key, id, sizeof(cd.key));
    strlcpy(cd.name, c.name, sizeof(cd.name));
    strlcpy(cd.roaster, c.roaster, sizeof(cd.roaster));
    strlcpy(cd.origin, c.origin, sizeof(cd.origin));
    strlcpy(cd.process, c.process, sizeof(cd.process));
    cd.from = 1;
    cd.taste = bbw::tasteFromDescription(c);
  }
  // Coffees only the library's ratings know.
  for (JsonPairConst kv : tasteCoffees) {
    if (known(kv.key().c_str()) || nc >= 64) continue;
    Candidate &cd = cands[nc++];
    strlcpy(cd.key, kv.key().c_str(), sizeof(cd.key));
    strlcpy(cd.name, kv.value()["name"] | "", sizeof(cd.name));
    strlcpy(cd.roaster, kv.value()["roaster"] | "", sizeof(cd.roaster));
    cd.origin[0] = cd.process[0] = '\0';
    cd.from = 2;
  }
  // Each candidate's community taste and stars, over its own description.
  for (size_t i = 0; i < nc; ++i) {
    JsonObjectConst t = tasteCoffees[cands[i].key];
    if (t.isNull()) continue;
    float cup[bbw::TASTE_ATTRS], fl[bbw::FLAVOUR_COUNT];
    for (uint8_t a = 0; a < bbw::TASTE_ATTRS; ++a) cup[a] = t["cup"][bbw::tasteAttrName(a)] | 0.0f;
    JsonObjectConst flv = t["flavours"];
    for (uint8_t f = 0; f < bbw::FLAVOUR_COUNT; ++f) fl[f] = flv.isNull() ? -1.0f : (flv[bbw::flavourName(f)] | 0.0f);
    cands[i].taste = bbw::blendTaste(bbw::tasteFromCommunity(cup, fl), cands[i].taste);
    cands[i].communityStars = t["stars"] | 0.0f;
    cands[i].communityN = t["n"] | 0;
  }
  // Drinkers like this one: how alike their stars are, then what they gave each candidate.
  for (JsonObjectConst person : taste["people"].as<JsonArrayConst>()) {
    JsonObjectConst r = person["r"];
    uint8_t a[bbw::MAX_COFFEES], b[bbw::MAX_COFFEES];
    for (size_t i = 0; i < nm; ++i) {
      a[i] = mine[i].stars;
      b[i] = r[mine[i].key] | 0;
    }
    const float w = bbw::peopleLikeness(a, b, nm);
    if (w <= 0.0f) continue;
    for (size_t i = 0; i < nc; ++i) {
      const uint8_t st = r[cands[i].key] | 0;
      if (!st) continue;
      cands[i].peopleNum += w * st;
      cands[i].peopleDen += w;
    }
  }
  for (size_t i = 0; i < nc; ++i) {
    Candidate &cd = cands[i];
    cd.match = bbw::matchTaste(profile, cd.taste);
    const float people = cd.peopleDen > 0.3f ? cd.peopleNum / cd.peopleDen : 0.0f;
    cd.score = bbw::recommendScore(cd.match, people, cd.communityStars);
  }
  // The five best, best first; only those that lean towards the profile.
  for (uint8_t pick = 0; pick < 5; ++pick) {
    int best = -1;
    for (size_t i = 0; i < nc; ++i) {
      if (cands[i].score > 0.05f && (best < 0 || cands[i].score > cands[best].score)) best = static_cast<int>(i);
    }
    if (best < 0) break;
    Candidate &cd = cands[best];
    JsonObject o = items.add<JsonObject>();
    o["name"] = cd.name;
    o["roaster"] = cd.roaster;
    if (cd.origin[0]) o["origin"] = cd.origin;
    if (cd.process[0]) o["process"] = cd.process;
    o["key"] = cd.key;
    if (cd.localId) o["coffee"] = cd.localId;
    o["from"] = cd.from == 0 ? "shelf" : cd.from == 1 ? "library" : "community";
    o["score"] = roundf(cd.score * 100) / 100;
    o["sure"] = roundf(cd.match.confidence * 100) / 100;
    JsonArray why = o["why"].to<JsonArray>();
    tasteWhy(why, profile, cd.taste);
    // The rated coffee it is most like, if that one was loved.
    float like = 0.35f;
    int likeIdx = -1;
    for (size_t k = 0; k < set.n; ++k) {
      if (set.items[k].stars < 4) continue;
      const float sim = bbw::tasteSimilarity(cd.taste, set.items[k].taste);
      if (sim > like) {
        like = sim;
        likeIdx = static_cast<int>(k);
      }
    }
    if (likeIdx >= 0) {
      o["like"] = set.items[likeIdx].coffeeId;
      o["like_stars"] = set.items[likeIdx].stars;
    }
    if (cd.peopleDen > 0.3f) o["people_stars"] = roundf(cd.peopleNum / cd.peopleDen * 10) / 10;
    if (cd.communityN) {
      o["community_stars"] = roundf(cd.communityStars * 10) / 10;
      o["community_n"] = cd.communityN;
    }
    cd.score = -1e9f;  // taken
  }
  delete[] cands;
  delete[] mine;
  delete[] set.items;
  sendJson(doc);
}

void handleDeleteRating() {
  const uint32_t id = strtoul(server.arg("id").c_str(), nullptr, 10);
  if (!app::store().deleteRating(id)) return sendError(404, "no such rating");
  sendOk();
}

// {names:[up to 8]}: a drinker's id is its place in the list, so a name left empty keeps its ratings.
void handlePostDrinkers() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  bbw::Drinkers d;
  JsonArrayConst names = doc["names"];
  for (uint8_t i = 0; i < bbw::MAX_DRINKERS && i < names.size(); ++i) {
    strlcpy(d.names[i], names[i] | "", bbw::DRINKER_NAME_LEN);
  }
  bbw::sanitizeDrinkers(d);
  app::store().saveDrinkers(d);
  sendOk();
}

void handlePostPower() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  const char *action = doc["action"] | "";
  bool ok = true;
  if (strcmp(action, "on") == 0) ok = app::powerRequest(true);
  else if (strcmp(action, "off") == 0) ok = app::powerRequest(false);
  else if (strcmp(action, "press") == 0) ok = app::powerPressAnyway();
  else if (strcmp(action, "believe_on") == 0) app::powerBelieve(bbw::PowerBelief::ON);
  else if (strcmp(action, "believe_off") == 0) app::powerBelieve(bbw::PowerBelief::OFF);
  else if (strcmp(action, "schedule") == 0) {
    bbw::PowerScheduleEntry entries[bbw::POWER_SCHEDULE_ENTRIES];
    size_t i = 0;
    for (JsonObject o : doc["schedule"].as<JsonArray>()) {
      if (i >= bbw::POWER_SCHEDULE_ENTRIES) break;
      entries[i].enabled = (o["enabled"] | false) ? 1 : 0;
      entries[i].turnOn = (o["on"] | true) ? 1 : 0;
      entries[i].minuteOfDay = o["minute"] | 0;
      entries[i].days = o["days"] | 0x7f;
      ++i;
    }
    app::applyPowerSchedule(entries, doc["utc_offset_min"] | 0);
  } else return sendError(400, "bad action");
  if (!ok) return sendError(409, app::powerView().lastOutcome);
  sendOk();
}

void handleNet() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "invalid JSON");
  bbw::NetConfig net = app::netConfig();
  const char *ssid = doc["staSsid"] | "";
  const char *staPassword = doc["staPassword"] | "";
  const char *apPassword = doc["apPassword"] | "";
  const char *botPassword = doc["botPassword"] | "";
  if (strlen(ssid) >= sizeof(net.staSsid)) return sendError(400, "SSID too long");
  if (apPassword[0] && (strlen(apPassword) < 8 || strlen(apPassword) >= sizeof(net.apPassword))) {
    return sendError(400, "access point password must be 8-32 characters");
  }
  if (strcmp(ssid, net.staSsid) != 0) net.staPassword[0] = '\0';
  strlcpy(net.staSsid, ssid, sizeof(net.staSsid));
  if (staPassword[0]) strlcpy(net.staPassword, staPassword, sizeof(net.staPassword));
  if (apPassword[0]) strlcpy(net.apPassword, apPassword, sizeof(net.apPassword));
  if (botPassword[0]) strlcpy(net.botPassword, botPassword, sizeof(net.botPassword));
  net.webStartEnabled = doc["webStartEnabled"] | false;
  app::applyNetConfig(net);
  sendOk();
  restartPending = true;
  restartAtMs = millis() + 1500;
}

}  // namespace

// ---- log ------------------------------------------------------------------
char *g_logChunk;  // PSRAM, made in begin()
constexpr size_t LOG_CHUNK_SIZE = 8192;

// Live view: everything logged since position `from`, as plain text. The
// position to ask from next time comes back in a header.
void handleLogRead() {
  uint32_t from = static_cast<uint32_t>(strtoul(server.arg("from").c_str(), nullptr, 10));
  bbw::logRead(from, g_logChunk, LOG_CHUNK_SIZE);
  char next[16];
  snprintf(next, sizeof(next), "%u", static_cast<unsigned>(from));
  server.sendHeader("X-Log-Next", next);
  server.send(200, "text/plain", g_logChunk);
}

void sendLogFile(const char *path) {
  if (!bbw::fileExists(path)) return;
  File file = LittleFS.open(path, "r");
  if (!file) return;
  for (;;) {
    const size_t n = file.read(reinterpret_cast<uint8_t *>(g_logChunk), 1024);
    if (n == 0) break;
    g_logChunk[n] = '\0';
    server.sendContent(g_logChunk);
  }
  file.close();
}

// Everything kept: the previous file, the current one, and what is still
// only in RAM.
void handleLogDownload() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  const bool kept = bbw::logConfig().keepOnFlash;
  if (kept) bbw::logFlush();
  server.sendHeader("Content-Disposition", "attachment; filename=\"brew-controller-log.txt\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain", "");
  sendLogFile(bbw::LOG_FILE_OLD);
  sendLogFile(bbw::LOG_FILE);
  if (!kept) {
    uint32_t from = 0;
    while (bbw::logRead(from, g_logChunk, 1024) > 0) server.sendContent(g_logChunk);
  }
  server.sendContent("");
}

void handleLogConfig(bool change) {
  if (change) {
    JsonDocument in;
    if (deserializeJson(in, server.arg("plain"))) return sendError(400, "invalid JSON");
    bbw::LogConfig config = bbw::logConfig();
    config.level = in["level"] | config.level;
    config.keepOnFlash = in["keep"] | config.keepOnFlash;
    bbw::logSetConfig(config);
    LOGI("log: level %u, %s", static_cast<unsigned>(bbw::logConfig().level),
         config.keepOnFlash ? "kept on flash" : "in memory only");
  }
  const bbw::LogConfig config = bbw::logConfig();
  JsonDocument doc;
  doc["ok"] = true;
  doc["level"] = config.level;
  doc["keep"] = config.keepOnFlash;
  doc["flash_bytes"] = bbw::logFlashBytes();
  doc["flash_limit"] = 2 * bbw::LOG_FILE_BYTES;
  doc["memory_bytes"] = bbw::LOG_RING_BYTES;
  doc["position"] = bbw::logPosition();
  sendJson(doc);
}

// ---- test press -----------------------------------------------------------
void handleBotTest(bool press) {
  if (press) {
    const bool power = server.arg("role") == "power";
    const bool ok = power ? app::powerPressAnyway() : app::testPressBrew();
    if (!ok) {
      return sendError(409, power ? "no power bot paired, or it is busy"
                                  : "no brew bot paired, or a shot is in hand");
    }
    LOGI("%s bot: test press asked for from the Setup page", power ? "power" : "brew");
  }
  JsonDocument doc;
  doc["ok"] = true;
  doc["brew"] = app::testPressState();
  sendJson(doc);
}

// ---- firmware update ------------------------------------------------------
// A firmware may go in whenever no shot is in hand: on the home screen, or
// waiting for the scale (a deployed controller usually is: the scale is off).
bool idleForUpdate() {
  const State st = app::status().state;
  return !busyBrewing() && (st == State::READY || st == State::CONNECTING_SCALE);
}

char g_updateError[96];

void handleUpdateInfo() {
  JsonDocument doc;
  doc["version"] = BBW_VERSION;
  doc["built"] = BBW_BUILT;
  const bbw::UpdateConfig config = bbw::updateConfig();
  char releases[128];
  snprintf(releases, sizeof(releases), "https://github.com/%s/releases", config.repo);
  doc["releases"] = releases;
  doc["repo"] = config.repo;
  doc["token_set"] = config.token[0] != '\0';
  doc["auto_check"] = config.autoCheck;
  // The update from GitHub: what the last check found, and how it is going.
  const bbw::UpdateStatus u = bbw::updateStatus();
  doc["state"] = bbw::updatePhaseName(u.phase);
  doc["percent"] = u.percent;
  doc["message"] = u.message;
  doc["available"] = u.available;
  if (u.checkedEpoch) doc["checked_epoch"] = u.checkedEpoch;
  if (u.latest.valid) {
    JsonObject l = doc["latest"].to<JsonObject>();
    l["tag"] = u.latest.tag;
    l["name"] = u.latest.name;
    l["published"] = u.latest.published;
    l["bytes"] = u.latest.firmwareBytes;
  }
  bbw::SlotInfo slots[4];
  const size_t n = bbw::updateSlots(slots, 4);
  JsonArray list = doc["slots"].to<JsonArray>();
  for (size_t i = 0; i < n; ++i) {
    JsonObject o = list.add<JsonObject>();
    o["label"] = slots[i].label;
    o["version"] = slots[i].version;
    o["state"] = slots[i].state;
    o["bytes"] = slots[i].bytes;
    o["running"] = slots[i].running;
    o["boot_next"] = slots[i].bootNext;
  }
#ifdef BBW_EMULATOR
  doc["can_update"] = false;
  doc["slot"] = "emulator";
#else
  doc["can_update"] = true;
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  doc["slot"] = running ? running->label : "?";
  doc["slot_bytes"] = next ? next->size : 0;
  doc["sketch_bytes"] = ESP.getSketchSize();
#endif
  sendJson(doc);
}

#ifndef BBW_EMULATOR
// The file arrives in pieces; each is written straight to the spare firmware
// slot. The running firmware is untouched until the whole image has been
// received and checked.
void handleUpdateUpload() {
  HTTPUpload &upload = server.upload();
  auto fail = [](const char *why) {
    if (!g_updateError[0]) strlcpy(g_updateError, why, sizeof(g_updateError));
  };
  if (upload.status == UPLOAD_FILE_START) {
    g_updateError[0] = '\0';
    if (!idleForUpdate()) {
      fail("not while a shot is in hand");
    } else {
      LOGI("update: receiving %s", upload.filename.c_str());
      bbw::logFlush();
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) fail(Update.errorString());
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!g_updateError[0] && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      fail(Update.errorString());
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!g_updateError[0]) {
      if (Update.end(true)) {
        LOGI("update: %u bytes received and verified", static_cast<unsigned>(upload.totalSize));
      } else {
        fail(Update.errorString());
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    fail("the upload was interrupted");
  }
}
#endif

void handleUpdateDone() {
#ifdef BBW_EMULATOR
  sendError(501, "the emulator cannot be updated this way: rebuild it");
#else
  if (g_updateError[0]) {
    if (Update.isRunning()) Update.abort();
    LOGW("update: refused, %s", g_updateError);
    return sendError(400, g_updateError);
  }
  sendOk();
  restartPending = true;
  restartAtMs = millis() + 1500;
#endif
}

// POST /api/update/check | /api/update/install | /api/update/boot?slot=factory|previous
void handleUpdateCheck() {
  if (!bbw::updateCheck()) return sendError(409, "an update check or install is already running");
  sendOk();
}

void handleUpdateInstall() {
  if (!idleForUpdate()) return sendError(409, "not while a shot is in hand");
  if (!bbw::updateInstall()) return sendError(409, "an update check or install is already running");
  LOGI("update: install asked for from the web page");
  sendOk();
}

void handleUpdateBoot() {
  if (busyBrewing()) return sendError(409, "not while a shot is running");
  char why[96];
  if (!bbw::updateBootSlot(server.arg("slot").c_str(), why, sizeof(why))) return sendError(400, why);
  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = why;
  sendJson(doc);
  restartPending = true;
  restartAtMs = millis() + 1500;
}

// {"repo":"owner/name","token":"...","auto_check":true}. The token is write
// only; an empty one keeps the stored token, "-" forgets it.
void handleUpdateConfig() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) return sendError(400, "invalid JSON");
  bbw::UpdateConfig config = bbw::updateConfig();
  const char *repo = in["repo"] | config.repo;
  if (!strchr(repo, '/') || strlen(repo) >= sizeof(config.repo)) {
    return sendError(400, "repository as owner/name");
  }
  strlcpy(config.repo, repo, sizeof(config.repo));
  const char *token = in["token"] | "";
  if (strcmp(token, "-") == 0) config.token[0] = '\0';
  else if (token[0]) {
    if (strlen(token) >= sizeof(config.token)) return sendError(400, "token too long");
    strlcpy(config.token, token, sizeof(config.token));
  }
  config.autoCheck = in["auto_check"] | config.autoCheck;
  bbw::updateConfigure(config);
  app::store().saveUpdateConfig(config);
  LOGI("update: releases from %s%s", config.repo, config.token[0] ? ", with a token" : "");
  sendOk();
}

void begin() {
  g_logChunk = bbw::bigArray<char>(LOG_CHUNK_SIZE);
  g_points = bbw::bigArray<bbw::CurvePoint>(bbw::MAX_CURVE_POINTS);
  g_curveT = bbw::bigArray<float>(bbw::MAX_CURVE_POINTS);
  g_curveW = bbw::bigArray<float>(bbw::MAX_CURVE_POINTS);
  g_shotHead = bbw::bigArray<char>(SHOT_HEAD_SIZE);
  g_presetList = bbw::bigArray<bbw::Recipe>(bbw::MAX_PRESETS);
  for (size_t i = 0; i < bbw::MAX_PRESETS; ++i) new (&g_presetList[i]) bbw::Recipe();
  const bbw::NetConfig net = app::netConfig();
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apSsid, sizeof(apSsid), "BrewByWeight-%02X%02X", mac[4], mac[5]);

  // Always offer the local access point; joining a home network is optional.
  WiFi.mode(net.staSsid[0] ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(apSsid, net.apPassword);
  if (net.staSsid[0]) {
    WiFi.setAutoReconnect(true);
    WiFi.begin(net.staSsid, net.staPassword);
  }
  MDNS.begin("brewbyweight");
#ifndef BBW_EMULATOR
  // One radio for Wi-Fi and Bluetooth. The scale and the bots are the
  // controller's job; the web page can wait. Measured on the board with the
  // home network joined: the bots were heard about once every 30 s and the
  // power bot not at all, with the radio left to share evenly.
  const esp_err_t coex = esp_coex_preference_set(ESP_COEX_PREFER_BT);
  LOGI("radio: Bluetooth first%s", coex == ESP_OK ? "" : " (not accepted)");
#endif

  server.on("/", HTTP_GET, []() {
    // Always the page of the firmware now running: a browser holding on to
    // the page from before an update runs old code against the new API.
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html", WEB_PAGE);
  });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/library", HTTP_GET, handleLibrary);
  server.on("/api/coffee", HTTP_POST, handleSaveCoffee);
  server.on("/api/bag", HTTP_POST, handleBag);
  server.on("/api/maintenance", HTTP_GET, handleGetMaintenance);
  server.on("/api/maintenance", HTTP_POST, handlePostMaintenance);
  server.on("/api/coffee/adopt", HTTP_POST, handleAdoptCoffee);
  server.on("/api/community", HTTP_GET, handleCommunity);
  server.on("/api/community/coffees", HTTP_GET, []() {
    const char *list = app::communityCoffees();
    server.send(200, "application/json", list[0] ? list : "{\"v\":1,\"coffees\":[]}");
  });
  server.on("/api/recipe", HTTP_POST, handleSaveRecipe);
  server.on("/api/grinder", HTTP_POST, handleSaveGrinder);
  server.on("/api/share", HTTP_GET, handleShare);
  server.on("/api/share/link", HTTP_POST, handleShareLink);
  server.on("/api/share/coffee", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    if (!app::settings().shareEnabled) return sendError(409, "sharing is switched off (Setup)");
    const long id = server.arg("id").toInt();
    if (id < 1 || id > 65535 || !app::shareCoffee(static_cast<uint16_t>(id), true)) {
      return sendError(409, "nothing to send: the library already has this version");
    }
    sendOk();
  });
  server.on("/api/machine", HTTP_GET, handleGetMachine);
  server.on("/api/machine", HTTP_POST, handlePostMachine);
  server.on("/api/share/machine", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    if (!app::settings().shareEnabled) return sendError(409, "sharing is switched off (Setup)");
    if (!app::shareMachine()) {
      return sendError(409, "nothing to send: it came with the firmware or the library, or this version was sent");
    }
    sendOk();
  });
  server.on("/api/community/machines", HTTP_GET, []() {
    const char *text = app::communityMachines();
    server.send(200, "application/json", text[0] ? text : "{\"v\":1,\"machines\":[]}");
  });
  server.on("/api/share/grinder", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    if (!app::settings().shareEnabled) return sendError(409, "sharing is switched off (Setup)");
    const long id = server.arg("id").toInt();
    if (id < 1 || id > 65535 || !app::shareGrinder(static_cast<uint16_t>(id))) {
      return sendError(409, "nothing to send: it is a known model, or the library has this version");
    }
    sendOk();
  });
  server.on("/api/community/grinders", HTTP_GET, []() {
    const char *list = app::communityGrinders();
    server.send(200, "application/json", list[0] ? list : "{\"v\":1,\"grinders\":[]}");
  });
  server.on("/api/share/again", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    if (!app::settings().shareEnabled) return sendError(409, "sharing is switched off");
    JsonDocument doc;
    doc["ok"] = true;
    doc["queued"] = app::shareEverythingAgain();
    sendJson(doc);
  });
  server.on("/api/share/coffees", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    if (!app::settings().shareEnabled) return sendError(409, "sharing is switched off");
    JsonDocument doc;
    doc["ok"] = true;
    doc["queued"] = app::shareAllCoffees();
    sendJson(doc);
  });
  server.on("/api/library/do", HTTP_POST, handleLibraryDo);
  server.on("/api/shots", HTTP_GET, handleShots);
  server.on("/api/contexts", HTTP_GET, handleContexts);
  server.on("/shots.csv", HTTP_GET, handleCsv);
  server.on("/api/shot/data", HTTP_GET, handleShotData);
  server.on("/shot.csv", HTTP_GET, handleShotCsv);
  server.on("/api/last", HTTP_GET, handleLast);
  server.on("/api/cmd", HTTP_POST, handleCmd);
  server.on("/api/devices", HTTP_GET, handleDevices);
  server.on("/api/grind", HTTP_GET, handleGrind);
  server.on("/api/grind/set", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    const long um = server.arg("um").toInt();
    if (um < 0 || um > 2000) return sendError(400, "grind setting must be 0-2000 microns");
    app::setGrind(static_cast<uint16_t>(um));
    sendOk();
  });
  server.on("/api/shot/vote", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    const long vote = server.arg("vote").toInt();
    if (vote < 0 || vote > 2) return sendError(400, "vote must be 0 (auto), 1 (ignore) or 2 (count)");
    const uint32_t id = strtoul(server.arg("id").c_str(), nullptr, 10);
    if (!app::setShotGrindVote(id, static_cast<uint8_t>(vote))) return sendError(404, "shot not found");
    sendOk();
  });
  // POST /api/shot/prep?id=N&dose=18.2&purged=1&uneven=0 : what the owner says about the preparation.
  server.on("/api/shot/prep", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    const uint32_t id = strtoul(server.arg("id").c_str(), nullptr, 10);
    const float dose = static_cast<float>(atof(server.arg("dose").c_str()));   // grams; 0 or empty: not weighed
    if (!(dose == 0.0f || (dose >= 5.0f && dose <= 40.0f))) return sendError(400, "dose must be 5 to 40 g, or 0");
    const uint16_t doseDg = static_cast<uint16_t>(dose * 10.0f + 0.5f);
    if (!app::store().setShotPrep(id, doseDg, server.arg("purged") == "1", server.arg("uneven") == "1")) {
      return sendError(404, "shot not found");
    }
    sendOk();
  });
  server.on("/api/shot/tds", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    const float tds = static_cast<float>(atof(server.arg("tds").c_str()));   // percent TDS from a refractometer; 0 clears it
    if (!(tds == 0.0f || (tds >= 0.5f && tds <= 25.0f))) return sendError(400, "TDS must be 0.5 to 25 percent, or 0 to clear it");
    const uint32_t id = strtoul(server.arg("id").c_str(), nullptr, 10);
    if (!app::setShotTds(id, static_cast<uint16_t>(tds * 100.0f + 0.5f))) return sendError(404, "shot not found");
    sendOk();
  });
  server.on("/api/shot/rate", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    const long rating = server.arg("rating").toInt(), taste = server.arg("taste").toInt();
    if (rating < 0 || rating > 5 || taste < 0 || taste > 3) return sendError(400, "bad rating");
    const uint32_t id = strtoul(server.arg("id").c_str(), nullptr, 10);
    if (!app::setShotRating(id, static_cast<uint8_t>(rating), static_cast<uint8_t>(taste))) {
      return sendError(404, "shot not found");
    }
    sendOk();
  });
  server.on("/api/shot/grind", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    const long um = server.arg("um").toInt();
    if (um < 0 || um > 2000) return sendError(400, "grind setting must be 0-2000 microns");
    const uint32_t id = strtoul(server.arg("id").c_str(), nullptr, 10);
    if (!app::setShotGrind(id, static_cast<uint16_t>(um))) return sendError(404, "shot not found");
    sendOk();
  });
  server.on("/api/pair", HTTP_POST, handlePair);
  server.on("/api/pusher", HTTP_GET, handleGetPusher);
  server.on("/api/pusher", HTTP_POST, handlePostPusher);
  server.on("/api/power", HTTP_GET, handleGetPower);
  server.on("/api/power", HTTP_POST, handlePostPower);
  server.on("/api/power/events", HTTP_GET, handleGetPowerEvents);
  server.on("/api/coffee/ratings", HTTP_GET, handleGetRatings);
  server.on("/api/coffee/rate", HTTP_POST, handlePostRating);
  server.on("/api/coffee/rate/delete", HTTP_POST, handleDeleteRating);
  server.on("/api/drinkers", HTTP_POST, handlePostDrinkers);
  server.on("/api/taste/profile", HTTP_GET, handleGetTasteProfile);
  server.on("/api/taste/recommend", HTTP_GET, handleGetRecommend);
  // The Brew Controller helper for Chrome, as a zip to unpack and load (docs/SUBSCRIPTIONS.md).
  server.on("/extension.zip", HTTP_GET, []() {
    server.sendHeader("Content-Disposition", "attachment; filename=\"brew-controller-helper.zip\"");
    server.send_P(200, "application/zip", reinterpret_cast<const char *>(EXTENSION_ZIP), EXTENSION_ZIP_LEN);
  });
  server.on("/api/subscriptions", HTTP_GET, handleGetSubscriptions);
  server.on("/api/subscriptions", HTTP_POST, handlePostSubscription);
  server.on("/api/subscriptions/delete", HTTP_POST, handleDeleteSubscription);
  server.on("/api/subscriptions/arrived", HTTP_POST, handleSubscriptionArrived);
  server.on("/api/subscriptions/skip", HTTP_POST, handleSubscriptionSkipped);
  server.on("/api/subscriptions/preview", HTTP_GET, handleGetPreview);
  server.on("/api/subscriptions/preview", HTTP_POST, handlePostPreview);
  server.on("/api/subscriptions/library", HTTP_GET, handleGetServiceLibrary);
  server.on("/api/subscriptions/share", HTTP_POST, handleShareService);
  server.on("/api/subscriptions/prefs", HTTP_POST, handleSubscriptionPrefs);
  server.on("/api/subscriptions/reviews", HTTP_GET, handleGetServiceReviews);
  server.on("/api/subscriptions/review", HTTP_POST, handlePostServiceReview);
  server.on("/api/subscriptions/review/delete", HTTP_POST, handleDeleteServiceReview);
  server.on("/api/subscriptions/recommend", HTTP_GET, handleGetServiceRecommend);
  server.on("/api/net", HTTP_POST, handleNet);
  server.on("/api/time", HTTP_POST, []() {
    const uint32_t epoch = strtoul(server.arg("epoch").c_str(), nullptr, 10);
    if (epoch > 1700000000UL) app::setEpoch(epoch);
    sendOk();
  });
  server.on("/api/offset/reset", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    app::resetLearnedOffset(static_cast<uint8_t>(server.arg("recipe").toInt()));
    sendOk();
  });
  server.on("/api/history/clear", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    app::store().clearShots();
    sendOk();
  });
  server.on("/api/log", HTTP_GET, handleLogRead);
  server.on("/log.txt", HTTP_GET, handleLogDownload);
  server.on("/api/log/config", HTTP_GET, []() { handleLogConfig(false); });
  server.on("/api/log/config", HTTP_POST, []() { handleLogConfig(true); });
  server.on("/api/log/clear", HTTP_POST, []() {
    bbw::logClear();
    LOGI("log: cleared from the Logs page");
    sendOk();
  });
  server.on("/api/radio/test", HTTP_GET, []() { handleRadioTest(false); });
  server.on("/api/radio/test", HTTP_POST, []() { handleRadioTest(true); });
  server.on("/api/bot/wake", HTTP_POST, []() {
    const String role = server.arg("role");
    const app::BotRole r = role == "power" ? app::BotRole::POWER
                           : role == "stop" ? app::BotRole::STOP : app::BotRole::BREW;
    if (!app::wakeBot(r)) return sendError(409, "no such bot paired");
    sendOk();
  });
  server.on("/api/bot/test", HTTP_GET, []() { handleBotTest(false); });
  server.on("/api/bot/test", HTTP_POST, []() { handleBotTest(true); });
  server.on("/api/diagnostics", HTTP_GET, []() {
    JsonDocument doc;
    app::diagnostics(doc.to<JsonObject>());
    sendJson(doc);
  });
  // Diagnostics reports (docs/TELEMETRY.md): the next one as it would go, a send now, where they go.
  server.on("/api/diagnostics/preview", HTTP_GET, []() {
    String body;
    app::diagPreview(body);
    server.send(200, "application/json", body);
  });
  server.on("/api/diagnostics/report", HTTP_GET, []() {
    JsonDocument doc;
    app::diagStatus(doc.to<JsonObject>());
    sendJson(doc);
  });
  server.on("/api/diagnostics/send", HTTP_POST, []() {
    if (!app::diagSendNow()) return sendError(409, "Diagnostics are off (Setup, Diagnostics)");
    sendOk();
  });
  server.on("/api/diagnostics/relay", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) return sendError(400, "not JSON");
    const char *url = doc["url"] | "";
    if (url[0] && strncmp(url, "https://", 8) != 0 && strncmp(url, "http://127.0.0.1", 16) != 0) {
      return sendError(400, "the relay's address must be an https link");
    }
    if (strlen(url) >= 96) return sendError(400, "that address is too long");
    app::setDiagRelay(url);
    sendOk();
  });
  server.on("/api/crash", HTTP_GET, []() {
    static char *text = bbw::bigArray<char>(bbw::DIAG_BODY_MAX);  // PSRAM, on first use
    if (!app::lastCrash(text, bbw::DIAG_BODY_MAX)) return server.send(200, "application/json", "{}");
    server.send(200, "application/json", text);
  });
  server.on("/api/update", HTTP_GET, handleUpdateInfo);
  server.on("/api/update/check", HTTP_POST, handleUpdateCheck);
  server.on("/api/update/install", HTTP_POST, handleUpdateInstall);
  server.on("/api/update/boot", HTTP_POST, handleUpdateBoot);
  server.on("/api/update/config", HTTP_POST, handleUpdateConfig);
#ifdef BBW_EMULATOR
  server.on("/api/update", HTTP_POST, handleUpdateDone);
#else
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
#endif
  server.onNotFound([]() { sendError(404, "not found"); });
  server.begin();
  LOGI("web: access point %s, http://192.168.4.1", apSsid);
}

#ifndef BBW_EMULATOR
// The controller's own access point is for setting it up, and for when the
// home network cannot be reached. Once it has been on the home network for a
// minute the access point goes: it does not need both, and an access point
// keeps the shared radio busy all the time, at the scale's and the bots'
// expense. Lost for a minute, and it comes back.
void serviceAccessPoint(uint32_t now) {
  constexpr uint32_t SETTLE_MS = 60000;
  static bool apOn = true;
  static uint32_t changedAtMs = 0;  // when the station state last changed
  static bool wasConnected = false;
  if (!app::netConfig().staSsid[0]) return;  // no home network: the access point is the only way in
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wasConnected) {
    wasConnected = connected;
    changedAtMs = now;
  }
  if (now - changedAtMs < SETTLE_MS) return;
  if (connected && apOn) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);  // modem sleep between beacons: more time for Bluetooth
    apOn = false;
    LOGI("wifi: on the home network (%s), own access point off", WiFi.localIP().toString().c_str());
  } else if (!connected && !apOn) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid, app::netConfig().apPassword);
    apOn = true;
    LOGW("wifi: home network lost for a minute, own access point %s back on", apSsid);
  }
}
#endif

void loop() {
  server.handleClient();
#ifndef BBW_EMULATOR
  serviceAccessPoint(millis());
#endif

  static uint32_t lastLine = 0;
  const uint32_t now = millis();
  if (now - lastLine >= 2000) {
    lastLine = now;
    char line[128];
    if (WiFi.status() == WL_CONNECTED) {
      snprintf(line, sizeof(line), "AP %s  http://192.168.4.1\nLAN http://%s", apSsid,
               WiFi.localIP().toString().c_str());
    } else {
      // Shown on the device only: whoever can read it can already press Start.
      snprintf(line, sizeof(line), "Wi-Fi %s  password %s\nhttp://192.168.4.1", apSsid,
               app::netConfig().apPassword);
    }
    ui::setWifiLine(line);
  }
  if (restartPending && static_cast<int32_t>(now - restartAtMs) >= 0 && !busyBrewing()) {
    app::restartSoon();  // saves the log first
  }
}

}  // namespace web
