// GET /api/screen and POST /api/screen/do (docs/API.md). The controller's screen, as data: which
// panel the board would show, and the words on it from ui/ScreenText.h, so a native copy of the
// screen (the iPhone app) reads exactly what the glass shows. Everything here runs on the loop task,
// like the other routes, and changes nothing but the few screen-only answers kept below.
#include "ScreenApi.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>

#include <math.h>

#include "App.h"
#include "core/SettingsUtil.h"
#include "ShotFormat.h"
#include "ui/ScreenText.h"

using bbw::State;

namespace web {

namespace screenapi {

namespace screen = bbw::screen;

WebServer *g_server = nullptr;

void sendJson(JsonDocument &doc, int code = 200) {
  String body;
  serializeJson(doc, body);
  g_server->send(code, "application/json", body);
}

void sendError(int code, const char *message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(doc, code);
}

// The same test as the web API's "not while a shot is running" (Web.cpp busyBrewing).
bool shotInHand() {
  const State s = app::status().state;
  return s == State::STARTING_SHOT || s == State::BREWING || s == State::BLOOMING ||
         s == State::STOPPING_SHOT || s == State::POST_SHOT_DRIP;
}

// ------------------------------------------------------------- screen state --
// What the board's screen remembers between refreshes, kept the same way for the app: "not now"
// on the no-grind question, and whether advice not acted on is still to be reminded of.
uint32_t g_noGrindDismissed = 0;
bool g_reminderArmed = true;
bool g_scaleWas = false;

uint32_t noGrindKey() {
  const bbw::Recipe r = app::activeRecipe();
  return (static_cast<uint32_t>(r.coffeeId) << 16) ^ r.recipeId ^ (static_cast<uint32_t>(app::activePreset()) << 8) ^ 1u;
}

enum class Prompt : uint8_t { NONE, NEW_ADVICE, NO_GRIND, REMINDER };

const char *promptName(Prompt p) {
  switch (p) {
    case Prompt::NEW_ADVICE: return "new_advice";
    case Prompt::NO_GRIND: return "no_grind";
    case Prompt::REMINDER: return "reminder";
    default: return "none";
  }
}

// The grind question the board would ask now, in the screen's order: a recommendation for a coffee
// or bag just put in use, then a missing setting, then advice not acted on (once per wake).
Prompt currentPrompt(const bbw::LiveStatus &st, screen::Dial &dial, screen::GrindPromptText &t) {
  // The screen wakes with the scale: asked again then.
  if (st.scaleConnected && !g_scaleWas) {
    g_noGrindDismissed = 0;
    g_reminderArmed = true;
  }
  g_scaleWas = st.scaleConnected;
  if (st.state != State::READY && st.state != State::CONNECTING_SCALE && st.state != State::BOOT &&
      st.state != State::INITIALIZING) {
    g_reminderArmed = false;  // a shot (or a fault) came first: not reminded after it
  }
  if (st.state != State::READY) return Prompt::NONE;

  const app::GrindRecommendation r = app::grindRecommendation();
  if (r.setting) {
    const bbw::Recipe active = app::activeRecipe();
    const screen::RecSource source = r.source == app::GrindRecSource::COMMUNITY ? screen::RecSource::COMMUNITY
                                     : r.source == app::GrindRecSource::SIMILAR ? screen::RecSource::SIMILAR
                                                                                 : screen::RecSource::RANGE;
    screen::grindRecommendationText(source, r.setting, r.range, r.current, r.people, r.from, active.coffee, dial, t);
    return Prompt::NEW_ADVICE;
  }
  const bbw::Recipe active = app::activeRecipe();
  if (active.grindUm == 0 && g_noGrindDismissed != noGrindKey()) {
    screen::noGrindText(active, app::lastGrindSetting(), dial, t);
    return Prompt::NO_GRIND;
  }
  if (g_reminderArmed && st.scaleConnected) {
    if (!app::grindReminderDue()) {
      g_reminderArmed = false;  // nothing now: not asked later in this wake either
    } else {
      screen::grindReminderText(app::grindAdvice(), dial, t);
      return Prompt::REMINDER;
    }
  }
  return Prompt::NONE;
}

// ------------------------------------------------------------------- JSON --
void putTone(JsonObject o, const char *key, screen::Tone tone) { o[key] = screen::toneName(tone); }

void fillLive(JsonObject o, const bbw::LiveStatus &st) {
  o["state"] = bbw::stateName(st.state);
  o["verifying_stop"] = st.verifyingStop;
  o["holding_button"] = st.holdingButton;
  o["stop_started_pump"] = st.stopStartedPump;
  o["first_drop_seen"] = st.firstDropSeen;
  o["scale_timer_valid"] = st.scaleTimerValid;
  o["scale_timer_s"] = bbw::round2(st.scaleTimerS);
  o["bloom_left_s"] = bbw::round2(st.bloomLeftS);
  o["scale_resting"] = app::scaleResting();
  o["scale_near"] = app::scaleNearby();
  o["target_cue"] = st.targetCue;
  o["manual_stop_needed"] = st.manualStopNeeded;
  o["awaiting_drip"] = st.awaitingDrip;
  o["scale_start_pending"] = app::scaleStartPending();
}

void fillHome(JsonObject o, const bbw::LiveStatus &st, screen::Dial &dial) {
  const bool ready = st.state == State::READY;
  screen::HomeState hs;
  hs.scaleResting = app::scaleResting();
  hs.scaleStartPending = !ready && app::scaleStartPending();
  hs.scaleNearby = !ready && app::scaleNearby();
  screen::HomeStatusText ht;
  screen::homeStatusText(st, hs, ht);
  o["status"] = ht.status;
  o["hint"] = ht.hint;
  o["hint_is_touch"] = ht.hintIsTouch;
  putTone(o, "hint_tone", ht.hintTone);
  putTone(o, "scale_dot", ht.scaleDot);
  putTone(o, "bot_dot", ht.botDot);
  if (ht.scaleBattery >= 0) {
    o["scale_battery"] = ht.scaleBattery;
    putTone(o, "scale_battery_tone", ht.scaleBatteryTone);
  }
  if (ht.botBattery >= 0) {
    o["bot_battery"] = ht.botBattery;
    putTone(o, "bot_battery_tone", ht.botBatteryTone);
  }
  o["bot_waking"] = ht.botWaking;
  o["can_start"] = ht.canStart;

  const bbw::Recipe r = app::activeRecipe();
  const bbw::GrindAdvice g = app::grindAdvice();
  const uint16_t communityStart = !r.grindUm ? app::communityStartForActive().setting : 0;
  const uint16_t similarStart = !r.grindUm && r.coffeeId != 0 ? app::similarStart(r.coffeeId, r.recipeId).setting : 0;
  screen::HomeCard card;
  screen::homeCardText(r, g, dial, communityStart, similarStart, card);
  JsonObject c = o["card"].to<JsonObject>();
  c["name"] = card.name;
  c["yield"] = card.yield;
  c["yield_ratio"] = card.yieldRatio;
  c["use_ratio"] = card.useRatio;
  c["grind"] = card.grind;
  c["advice"] = card.advice;
  c["advice_short"] = card.adviceShort;
  c["advice_shortest"] = card.adviceShortest;
  putTone(c, "advice_tone", card.adviceTone);
  c["pre"] = card.pre;
  c["pre_short"] = card.preShort;
  c["foot"] = card.foot;
  char age[32];
  screen::describeRoastAge(r, app::epochNow(), false, age, sizeof(age));
  c["roast_age"] = age;
  screen::describeRoastAge(r, app::epochNow(), true, age, sizeof(age));
  c["roast_age_brief"] = age;
}

void fillBrew(JsonObject o, const bbw::LiveStatus &st) {
  screen::BrewPhaseText bp;
  screen::brewPhaseText(st, false, bp);
  o["phase"] = bp.phase;
  o["stop"] = bp.stop;
  putTone(o, "phase_tone", bp.phaseTone);
  o["running"] = bp.running;
  // The time is the scale's own timer whenever the scale reports it, so the two displays agree.
  const float seconds = st.scaleTimerValid && st.state == State::BREWING ? st.scaleTimerS : st.shotTimeS;
  o["clock_s"] = bbw::round2(bp.running ? seconds : 0.0f);
  // The centre shows the target until coffee is detected, then the live weight.
  o["show_weight"] = st.firstDropSeen || st.state == State::TARING || st.state == State::WAITING_FOR_CUP;
  float share = 0.0f;
  if (bp.running && st.firstDropSeen && st.targetWeightG > 0.0f) {
    share = st.weightG / st.targetWeightG;
    share = share < 0 ? 0 : (share > 1 ? 1 : share);
  }
  o["ring_progress"] = bbw::round2(share);
}

void fillResult(JsonObject o, const bbw::LiveStatus &st, screen::Dial &dial) {
  static screen::ResultInput in;  // the loop task only; too big to be worth the stack
  static screen::ResultText rt;
  in = screen::ResultInput();
  rt = screen::ResultText();
  in.have = app::lastShot(in.shot);
  in.state = st.state;
  const bbw::Settings cfg = app::settings();
  in.basis = cfg.grindTimeBasis;
  in.toleranceS = cfg.grindToleranceS;
  screen::resultText(in, app::grindAdvice(), dial, rt);
  o["have"] = rt.have;
  o["abnormal"] = rt.abnormal;
  o["timed_out"] = rt.timedOut;
  if (rt.have) {
    o["top"] = rt.top;
    putTone(o, "top_tone", rt.topTone);
    o["weight"] = rt.weight;
    o["line1"] = rt.line1;
    JsonArray l2 = o["line2"].to<JsonArray>();  // the longest wording first
    for (uint8_t i = 0; i < rt.line2Count; ++i) l2.add(rt.line2[i]);
    o["shot_id"] = in.shot.id;
  }
  o["review"] = rt.review;
  o["grind_change"] = rt.grindChange;
  o["counted"] = rt.counted;
  if (rt.review) {
    o["review_text"] = rt.reviewText;
    o["review_text_short"] = rt.reviewTextShort;
  } else {
    o["grind"] = rt.grind;
    o["grind_sub"] = rt.grindSub;
    putTone(o, "grind_tone", rt.grindTone);
  }
  // The board takes the screen back to the home screen on its own once the used cup is swapped.
  o["auto_ack"] = st.cupPresent && !rt.review;
}

void fillFault(JsonObject o, const bbw::LiveStatus &st) {
  o["title"] = st.manualStopNeeded ? "STOP MACHINE" : screen::faultTitle(st.state);
  o["message"] = screen::faultMessage(st.state, st.manualStopNeeded, st.stopStartedPump);
  o["offer_press"] = st.botConnected && (st.state == State::START_COMMAND_FAILED || st.state == State::STOP_COMMAND_FAILED);
}

// Which panel the board would show (Ui.cpp refresh), leaving out the ones the owner opens by hand.
const char *panelFor(const bbw::LiveStatus &st, Prompt prompt) {
  if (prompt != Prompt::NONE) return "grind_prompt";
  switch (st.state) {
    case State::BOOT:
    case State::INITIALIZING:
    case State::CONNECTING_SCALE:
    case State::READY:
      return "home";
    case State::TARING:
    case State::WAITING_FOR_CUP:
    case State::READY_TO_BREW:
    case State::STARTING_SHOT:
    case State::BREWING:
    case State::BLOOMING:
    case State::STOPPING_SHOT:
    case State::POST_SHOT_DRIP:
      return "brew";
    case State::SHOT_COMPLETE:
      return "result";
    case State::SHOT_TIMEOUT:
    case State::INVALID_WEIGHT: {
      // A shot that ran out of time, or was stopped because the cup was lifted, still gets its
      // summary; a stop that could not be confirmed, or an implausible weight, gets the warning.
      bbw::ShotRecord last;
      const bool cupLifted = app::lastShot(last) && last.endReason == static_cast<uint8_t>(bbw::EndReason::CUP_REMOVED);
      if (!st.manualStopNeeded && (st.state == State::SHOT_TIMEOUT || cupLifted)) return "result";
      return "fault";
    }
    default:
      return "fault";
  }
}

void handleScreen() {
  const bbw::LiveStatus st = app::status();
  const bbw::Grinder grinder = app::grinder();
  screen::Dial dial(grinder);
  screen::GrindPromptText pt;
  const Prompt prompt = currentPrompt(st, dial, pt);
  const char *panel = panelFor(st, prompt);

  JsonDocument doc;
  doc["ok"] = true;
  doc["panel"] = panel;
  doc["theme"] = app::settings().uiTheme;
  fillLive(doc["live"].to<JsonObject>(), st);
  fillHome(doc["home"].to<JsonObject>(), st, dial);
  if (!strcmp(panel, "brew")) fillBrew(doc["brew"].to<JsonObject>(), st);
  if (!strcmp(panel, "result")) fillResult(doc["result"].to<JsonObject>(), st, dial);
  if (!strcmp(panel, "fault")) fillFault(doc["fault"].to<JsonObject>(), st);
  if (prompt != Prompt::NONE) {
    JsonObject p = doc["grind_prompt"].to<JsonObject>();
    p["kind"] = promptName(prompt);
    p["title"] = pt.title;
    p["setting"] = pt.setting;
    p["why"] = pt.why;
    p["use"] = pt.use;
    p["keep"] = pt.keep;
    p["use_um"] = pt.useUm;
  }
  sendJson(doc);
}

// ---------------------------------------------------------------- actions --
bool argFloat(const char *name, float lo, float hi, float &out, const char *&bad) {
  if (!g_server->hasArg(name)) return true;
  const String v = g_server->arg(name);
  char *end = nullptr;
  const float f = strtof(v.c_str(), &end);
  if (end == v.c_str() || *end != '\0' || !isfinite(f) || f < lo || f > hi) {
    bad = name;
    return false;
  }
  out = f;
  return true;
}

bool argUInt(const char *name, long lo, long hi, long &out, const char *&bad) {
  if (!g_server->hasArg(name)) return true;
  const String v = g_server->arg(name);
  char *end = nullptr;
  const long n = strtol(v.c_str(), &end, 10);
  if (end == v.c_str() || *end != '\0' || n < lo || n > hi) {
    bad = name;
    return false;
  }
  out = n;
  return true;
}

bool argBool(const char *name, bool &out, const char *&bad) {
  if (!g_server->hasArg(name)) return true;
  const String v = g_server->arg(name);
  if (v == "1" || v == "true") out = true;
  else if (v == "0" || v == "false") out = false;
  else {
    bad = name;
    return false;
  }
  return true;
}

// The edit screen's SAVE, with the reasons the screen leaves unsaid. Fields (all optional, the rest
// stay as they are): index, dose_g, target_g, ratio, use_ratio, grind_um, target_time_s,
// preinfusion (machine|hold), preinfusion_hold_ms.
void doApplyPresetEdit() {
  if (shotInHand()) return sendError(409, "not while a shot is running");
  long index = app::activePreset();
  const char *bad = nullptr;
  if (!argUInt("index", 0, 255, index, bad)) return sendError(400, "index: a preset number");
  if (index >= app::presetCount()) return sendError(400, "index: no such preset");
  bbw::Recipe r = app::preset(static_cast<uint8_t>(index));
  float dose = r.doseG, target = r.targetWeightG, ratio = r.ratio, time = r.targetTimeS;
  long grind = r.grindUm, hold = r.preinfusionHoldMs;
  bool useRatio = r.useRatio;
  if (!argFloat("dose_g", bbw::MIN_DOSE_G, bbw::MAX_DOSE_G, dose, bad)) return sendError(400, "dose_g out of range");
  if (!argFloat("target_g", bbw::MIN_TARGET_G, bbw::MAX_TARGET_G, target, bad)) return sendError(400, "target_g out of range");
  if (!argFloat("ratio", bbw::MIN_RATIO, bbw::MAX_RATIO, ratio, bad)) return sendError(400, "ratio out of range");
  if (!argFloat("target_time_s", 5.0f, 60.0f, time, bad)) return sendError(400, "target_time_s: 5 to 60");
  if (!argUInt("grind_um", 0, 2000, grind, bad)) return sendError(400, "grind_um: 0 to 2000");
  if (!argUInt("preinfusion_hold_ms", 300, bbw::MAX_PREINFUSION_HOLD_MS, hold, bad)) {
    return sendError(400, "preinfusion_hold_ms out of range");
  }
  if (!argBool("use_ratio", useRatio, bad)) return sendError(400, "use_ratio: true or false");
  if (g_server->hasArg("preinfusion")) {
    const String p = g_server->arg("preinfusion");
    if (p == "hold") r.preinfusion = bbw::Preinfusion::HOLD;
    else if (p == "machine") r.preinfusion = bbw::Preinfusion::MACHINE;
    else return sendError(400, "preinfusion: machine or hold");
  }
  if (r.bloomPauseMs && r.preinfusion != bbw::Preinfusion::HOLD) {
    return sendError(400, "a recipe with a bloom keeps a pre-infusion hold");
  }
  r.doseG = dose;
  r.targetWeightG = target;
  r.ratio = ratio;
  r.targetTimeS = time;
  r.grindUm = static_cast<uint16_t>(grind);
  r.preinfusionHoldMs = static_cast<uint16_t>(hold);
  r.useRatio = useRatio;
  bbw::sanitizeRecipe(r);
  if (!app::applyPresetEdit(static_cast<uint8_t>(index), r)) return sendError(409, "not saved (a shot is running or the list is full)");
  JsonDocument doc;
  doc["ok"] = true;
  doc["index"] = index;
  sendJson(doc);
}

void doAnswerGrindReco() {
  if (shotInHand()) return sendError(409, "not while a shot is running");
  bool use = false;
  const char *bad = nullptr;
  if (!g_server->hasArg("use") || !argBool("use", use, bad)) return sendError(400, "use: true or false");
  const bbw::LiveStatus st = app::status();
  const bbw::Grinder grinder = app::grinder();
  screen::Dial dial(grinder);
  screen::GrindPromptText pt;
  const Prompt prompt = currentPrompt(st, dial, pt);
  JsonDocument doc;
  doc["ok"] = true;
  doc["kind"] = promptName(prompt);
  switch (prompt) {
    case Prompt::NEW_ADVICE:
      app::answerGrindRecommendation(use);
      break;
    case Prompt::REMINDER:
      g_reminderArmed = false;
      if (use && pt.useUm) app::setGrind(pt.useUm);
      break;
    case Prompt::NO_GRIND:
      // The board opens its edit screen on the suggested setting; the app does the same with this.
      if (use) doc["edit_grind_um"] = pt.useUm;
      else g_noGrindDismissed = noGrindKey();
      break;
    default:
      return sendError(409, "no grind question is open");
  }
  sendJson(doc);
}

void doScreenAction() {
  const String what = g_server->arg("what");
  if (what == "pair_nearest_bot") {
    JsonDocument doc;
    doc["ok"] = true;
    doc["pairing"] = app::pairStrongestBot();  // false: none found nearby
    return sendJson(doc);
  }
  if (what == "discover") {
    app::discover();
  } else if (what == "finish_active_coffee") {
    if (shotInHand()) return sendError(409, "not while a shot is running");
    if (!app::finishActiveCoffee()) return sendError(409, "no coffee on this recipe, or a shot is running");
  } else if (what == "apply_preset_edit") {
    return doApplyPresetEdit();
  } else if (what == "next_recipe") {
    if (shotInHand()) return sendError(409, "not while a shot is running");
    app::nextRecipe();
  } else if (what == "select_recipe") {
    if (shotInHand()) return sendError(409, "not while a shot is running");
    const char *bad = nullptr;
    long index = 0;
    if (!g_server->hasArg("index") || !argUInt("index", 0, 255, index, bad) || index >= app::presetCount()) {
      return sendError(400, "index: a preset number");
    }
    app::selectRecipe(static_cast<uint8_t>(index));
  } else if (what == "answer_grind_reco") {
    return doAnswerGrindReco();
  } else if (what == "dismiss_grind_prompt") {
    // NOT NOW on any of the grind questions.
    g_reminderArmed = false;
    g_noGrindDismissed = noGrindKey();
  } else if (what == "set_grind") {
    if (shotInHand()) return sendError(409, "not while a shot is running");
    long um = 0;
    const char *bad = nullptr;
    if (!g_server->hasArg("um") || !argUInt("um", 1, 2000, um, bad)) return sendError(400, "um: 1 to 2000");
    app::setGrind(static_cast<uint16_t>(um));
  } else if (what == "vote") {
    long id = 0;
    const char *bad = nullptr;
    const String v = g_server->arg("vote");
    if (!g_server->hasArg("shot_id") || !argUInt("shot_id", 1, 2147483647L, id, bad)) return sendError(400, "shot_id: a shot number");
    if (v != "ignore" && v != "count") return sendError(400, "vote: ignore or count");
    const uint8_t vote = static_cast<uint8_t>(v == "ignore" ? bbw::GrindVote::IGNORE : bbw::GrindVote::COUNT);
    if (!app::setShotGrindVote(static_cast<uint32_t>(id), vote)) return sendError(404, "no such shot");
  } else if (what == "manual_press") {
    app::manualPress();
  } else if (what == "rotate") {
    bbw::Settings s = app::settings();
    s.displayFlipped = !s.displayFlipped;
    app::applySettings(s);
  } else if (what == "learning") {
    bbw::Settings s = app::settings();
    bool on = !s.learningEnabled;
    const char *bad = nullptr;
    if (!argBool("on", on, bad)) return sendError(400, "on: true or false");
    s.learningEnabled = on;
    app::applySettings(s);
  } else {
    return sendError(400, "what: pair_nearest_bot, discover, finish_active_coffee, apply_preset_edit, next_recipe, "
                          "select_recipe, answer_grind_reco, dismiss_grind_prompt, set_grind, vote, manual_press, "
                          "rotate or learning");
  }
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(doc);
}

}  // namespace screenapi

void registerScreenApi(WebServer &server) {
  screenapi::g_server = &server;
  server.on("/api/screen", HTTP_GET, screenapi::handleScreen);
  server.on("/api/screen/do", HTTP_POST, screenapi::doScreenAction);
}

}  // namespace web
