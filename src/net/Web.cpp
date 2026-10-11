#include "Web.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "App.h"
#include "Log.h"
#include "ShotFormat.h"
#include "WebPage.h"
#include "core/SettingsUtil.h"
#include "platform/BigMem.h"
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
  doc["state"] = bbw::stateName(st.state);
  doc["weight_g"] = bbw::round2(st.weightG);
  doc["flow_g_s"] = bbw::round2(st.flowGS);
  doc["shot_time_s"] = bbw::round2(st.shotTimeS);
  doc["target_weight_g"] = st.targetWeightG;
  doc["stop_offset_g"] = bbw::round2(st.stopOffsetG);
  doc["recipe"] = st.recipe;
  doc["scale_connected"] = st.scaleConnected;
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
    String line;
    serializeJson(doc, line);
    if (i) server.sendContent(",");
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
      g["umPerUnit"] = grinder.umPerUnit;
      g["zeroDate"] = grinder.zeroDate;
      g["calibration"] = grinder.calibration;
      g["share"] = bbw::shareStateName(bbw::grinderShareState(grinder, outboxSent));
      g["shareChanged"] = bbw::grinderChangedSinceShared(grinder);
      g["canShare"] = bbw::grinderCanBeShared(grinder);
      g["reviewNote"] = grinder.reviewNote;
      g["libraryId"] = grinder.libraryId;
    };
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
    doc["spread_um"] = bbw::round2(view.dialin.spreadUm);
    doc["same_model"] = view.dialin.modelCount;
  }
  doc["coffees_in_common"] = view.fit.known ? view.fit.coffeesInCommon : 0;
  JsonObject start = doc["start"].to<JsonObject>();
  start["basis"] = bbw::startBasisName(view.start.basis);
  start["setting"] = view.start.setting;
  start["range"] = view.start.range;
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
  doc["sends_to"] = st.sendsTo;
  doc["waiting"] = st.waiting;
  doc["sent"] = st.sent;
  doc["capacity"] = bbw::Store::OUTBOX_CAPACITY;
  sendJson(doc);
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
  static String what;
  static long a, b;
  what = server.arg("what");
  a = server.arg("a").toInt();
  b = server.arg("b").toInt();
  if (a < 0 || a > 65535 || b < 0 || b > 65535) return sendError(400, "bad value");
  static const char *error;
  error = "unknown change";
  const bool ok = app::editLibrary([](bbw::Library &lib, void *) {
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
    if (what == "preset_add") {  // a = recipe id, b = coffee id (0 = none)
      error = "not added: the list is full (16) or already has it";
      return bbw::addPreset(lib, static_cast<uint16_t>(a), static_cast<uint16_t>(b)) >= 0;
    }
    if (what == "preset_delete") {  // a = index
      error = "not removed: the controller needs at least one entry";
      return a < 256 && bbw::removePreset(lib, static_cast<uint8_t>(a));
    }
    if (what == "preset_select") {
      error = "no such entry";
      if (a >= lib.presetCount) return false;
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
  doc["average_time_s"] = bbw::round2(a.averageTimeS);
  doc["error_s"] = bbw::round2(a.errorS);
  doc["shots_used"] = a.shotsUsed;
  doc["window"] = s.grindWindow;
  doc["tolerance_s"] = s.grindToleranceS;
  doc["time_basis"] = s.grindTimeBasis == bbw::GrindTimeBasis::POUR ? "pour" : "total";
  doc["um_per_s"] = bbw::round2(a.umPerS);
  doc["learned"] = a.learned;
  doc["learn_shots"] = a.learnShots;
  doc["step_limited"] = a.stepLimited;
  doc["outliers_ignored"] = a.outliersIgnored;
  doc["min_time_s"] = bbw::round2(a.minTimeS);
  doc["max_time_s"] = bbw::round2(a.maxTimeS);
  doc["coffee"] = active.coffee;
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
  if (dev.power.valid) doc["power_battery"] = dev.power.batteryPercent;
  if (dev.powerProblem) doc["power_problem"] = dev.powerProblem;
  doc["pairing"]["power"] = dev.powerAddress;
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
  else if (strcmp(kind, "scale") == 0) app::pairScale(address, type);
  else return sendError(400, "bad kind");
  sendOk();
}

// The keys a pusher needs. They are write-only: the page is told which are
// set, never what they are.
void handleGetPusher() {
  const bool power = server.arg("role") == "power";
  const bbw::PusherSecrets secrets = app::pusherSecrets(power);
  JsonDocument doc;
  doc["role"] = power ? "power" : "brew";
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
  const bool power = strcmp(doc["role"] | "", "power") == 0;
  bbw::PusherSecrets secrets = app::pusherSecrets(power);
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
  app::applyPusherSecrets(power, secrets);
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

void begin() {
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

  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", WEB_PAGE); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/library", HTTP_GET, handleLibrary);
  server.on("/api/coffee", HTTP_POST, handleSaveCoffee);
  server.on("/api/coffee/adopt", HTTP_POST, handleAdoptCoffee);
  server.on("/api/community", HTTP_GET, handleCommunity);
  server.on("/api/community/coffees", HTTP_GET, []() {
    const char *list = app::communityCoffees();
    server.send(200, "application/json", list[0] ? list : "{\"v\":1,\"coffees\":[]}");
  });
  server.on("/api/recipe", HTTP_POST, handleSaveRecipe);
  server.on("/api/grinder", HTTP_POST, handleSaveGrinder);
  server.on("/api/share", HTTP_GET, handleShare);
  server.on("/api/share/coffee", HTTP_POST, []() {
    if (busyBrewing()) return sendError(409, "not while a shot is running");
    if (!app::settings().shareEnabled) return sendError(409, "sharing is switched off (Setup)");
    const long id = server.arg("id").toInt();
    if (id < 1 || id > 65535 || !app::shareCoffee(static_cast<uint16_t>(id), true)) {
      return sendError(409, "nothing to send: the library already has this version");
    }
    sendOk();
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
  server.onNotFound([]() { sendError(404, "not found"); });
  server.begin();
  LOGI("web: access point %s, http://192.168.4.1", apSsid);
}

void loop() {
  server.handleClient();

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
    ESP.restart();
  }
}

}  // namespace web
