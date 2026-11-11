// Host tests for the words on the controller's screen (ui/ScreenText.h) and its palettes
// (ui/Palette.h), which the board's screen and the iPhone app share. Included by test_main.cpp after
// the CHECK macros. The strings are the ones ui/Ui.cpp always drew: a change here is a change of
// what the owner reads, so each is pinned character for character.
#include "ui/Palette.h"
#include "ui/ScreenText.h"

static void testPalettes() {
  printf("  palettes\n");
  // Within a palette every colour differs, so a change of scheme can tell each role from its value.
  for (int t = 0; t < UI_THEME_COUNT; ++t) {
    const uint32_t *c = reinterpret_cast<const uint32_t *>(&PALETTES[t]);
    for (int i = 0; i < 10; ++i)
      for (int j = i + 1; j < 10; ++j) CHECK(c[i] != c[j]);
  }
  CHECK(sizeof(Palette) == 10 * sizeof(uint32_t));
  CHECK(PALETTES[0].bg == 0x000000 && PALETTES[0].accent == 0xE67E22);
  CHECK(PALETTES[1].bg == 0xF2F1EE && PALETTES[5].ok == 0x00E676);
  CHECK(&paletteFor(200) == &PALETTES[0]);  // an unknown scheme draws as Dark
  CHECK(&paletteFor(3) == &PALETTES[3]);
  CHECK(strcmp(PALETTE_ROLE_NAMES[0], "bg") == 0 && strcmp(PALETTE_ROLE_NAMES[9], "select") == 0);
  CHECK(strcmp(PALETTE_THEME_NAMES[5], "High contrast") == 0);
}

static Recipe screenRecipe() {
  Recipe r;
  strcpy(r.name, "Double");
  r.doseG = 18.0f;
  r.targetWeightG = 36.0f;
  r.ratio = 2.0f;
  r.useRatio = true;
  r.targetTimeS = 28.0f;
  r.grindUm = 0;
  r.preinfusion = Preinfusion::MACHINE;
  return r;
}

static void testScreenFaults() {
  printf("  fault texts\n");
  using namespace screen;
  CHECK(strcmp(faultTitle(State::SCALE_DISCONNECTED), "SCALE DISCONNECTED") == 0);
  CHECK(strcmp(faultTitle(State::STOP_COMMAND_FAILED), "STOP NOT CONFIRMED") == 0);
  CHECK(strcmp(faultTitle(State::INVALID_WEIGHT), "WEIGHT NOT VALID") == 0);
  CHECK(strcmp(faultTitle(State::READY), stateName(State::READY)) == 0);  // the rest: the state's own name
  CHECK(strcmp(faultMessage(State::TARE_FAILED, false, false), "The scale did not settle at zero. Keep the cup still and start again.") == 0);
  CHECK(strncmp(faultMessage(State::STOP_COMMAND_FAILED, true, false), "STOP THE MACHINE BY HAND.\n", 26) == 0);
  CHECK(strstr(faultMessage(State::START_COMMAND_FAILED, false, true), "STOP started the pump") != nullptr);
  CHECK(strstr(faultMessage(State::START_COMMAND_FAILED, false, false), "not confirmed by coffee flow") != nullptr);
  CHECK(faultMessage(State::READY, false, false)[0] == '\0');
}

static void testScreenRecipeLines() {
  printf("  recipe lines\n");
  using namespace screen;
  Grinder grinder;
  Dial dial(grinder);
  Recipe r = screenRecipe();
  char out[96];
  r.roastDate = 20260101;
  describeRoastAge(r, 0, false, out, sizeof(out));  // clock not set: the date itself
  CHECK(strcmp(out, "roasted 2026-01-01") == 0);
  describeRoastAge(r, 0, true, out, sizeof(out));
  CHECK(strcmp(out, "2026-01-01") == 0);
  const uint32_t jan1 = static_cast<uint32_t>(daysFromCivil(20260101)) * 86400u;
  describeRoastAge(r, jan1 + 12u * 86400u + 3600u, false, out, sizeof(out));
  CHECK(strcmp(out, "12 d off roast") == 0);
  describeRoastAge(r, jan1 + 12u * 86400u, true, out, sizeof(out));
  CHECK(strcmp(out, "12 d") == 0);
  r.roastDate = 0;
  describeRoastAge(r, jan1, false, out, sizeof(out));
  CHECK(out[0] == '\0');
  CHECK(daysFromCivil(19700101) == 0 && daysFromCivil(20000301) == 11017);

  describePreinfusion(r, out, sizeof(out));
  CHECK(strcmp(out, "pre-inf machine") == 0);
  r.preinfusion = Preinfusion::HOLD;
  r.preinfusionHoldMs = 2500;
  describePreinfusion(r, out, sizeof(out));
  CHECK(strcmp(out, "pre-inf hold 2.5 s") == 0);
  r.bloomPauseMs = 8000;
  describePreinfusion(r, out, sizeof(out));
  CHECK(strcmp(out, "pre 2.5 s, bloom 8 s") == 0);

  r = screenRecipe();
  r.grindUm = 246;
  recipeListLine(r, dial, out, sizeof(out));
  CHECK(strcmp(out, "Double\n18.0 -> 36.0 g  246 um") == 0);
  strcpy(r.coffee, "Portrait");
  recipeListLine(r, dial, out, sizeof(out));
  CHECK(strcmp(out, "Portrait\nDouble  18.0 -> 36.0 g  246 um") == 0);

  char title[48], value[48], hint[96];
  GrindAdvice none;
  describeEditField(r, 0, dial, none, title, value, hint, sizeof(title));
  CHECK(strcmp(title, "RATIO") == 0 && strcmp(value, "1:2.0") == 0 && strcmp(hint, "18.0 g in gives\n36.0 g out") == 0);
  r.useRatio = false;
  describeEditField(r, 0, dial, none, title, value, hint, sizeof(title));
  CHECK(strcmp(title, "TARGET OUT") == 0 && strcmp(value, "36.0 g") == 0);
  describeEditField(r, 2, dial, none, title, value, hint, sizeof(title));
  CHECK(strcmp(value, "246 um") == 0 && strcmp(hint, "number on the\ngrinder: tap 1,\nhold for 10") == 0);
  GrindAdvice finer;
  finer.action = GrindAction::FINER;
  finer.suggestedUm = 240;
  finer.source = SensitivitySource::GUESS;
  describeEditField(r, 2, dial, finer, title, value, hint, sizeof(title));
  CHECK(strcmp(hint, "advice: go finer\nhow far: see the\nweb page") == 0);
  finer.source = SensitivitySource::MEASURED;
  describeEditField(r, 2, dial, finer, title, value, hint, sizeof(title));
  CHECK(strcmp(hint, "advice: go finer,\nset 240 um") == 0);
  r.grindUm = 0;
  describeEditField(r, 2, dial, none, title, value, hint, sizeof(title));
  CHECK(strcmp(value, "not set") == 0);
  describeEditField(r, 3, dial, none, title, value, hint, sizeof(title));
  CHECK(strcmp(title, "TARGET TIME") == 0 && strcmp(value, "28.0 s") == 0);
  describeEditField(r, 4, dial, none, title, value, hint, sizeof(title));
  CHECK(strcmp(title, "PRE-INFUSION") == 0 && strcmp(value, "MACHINE") == 0);
  r.preinfusion = Preinfusion::HOLD;
  r.preinfusionHoldMs = 1500;
  describeEditField(r, 4, dial, none, title, value, hint, sizeof(title));
  CHECK(strcmp(value, "HOLD 1.5 s") == 0);
  editSetAdviceLabel(dial, 240, out, sizeof(out));
  CHECK(strcmp(out, "SET 240 um") == 0);
}

static void testScreenHome() {
  printf("  home texts\n");
  using namespace screen;
  LiveStatus st;
  HomeState hs;
  HomeStatusText t;
  st.state = State::CONNECTING_SCALE;
  homeStatusText(st, hs, t);
  CHECK(strcmp(t.status, "NO SCALE") == 0 && strcmp(t.hint, "SWITCH ON THE SCALE") == 0 && t.hintTone == Tone::DIM);
  CHECK(t.scaleDot == Tone::BAD && !t.canStart && t.scaleBattery == -1);
  hs.scaleResting = true;
  homeStatusText(st, hs, t);
  CHECK(strcmp(t.status, "SCALE RESTING") == 0);
  hs.scaleStartPending = true;
  homeStatusText(st, hs, t);
  CHECK(strcmp(t.hint, "CONNECTING TO THE SCALE") == 0);
  hs.scaleStartPending = false;
  hs.scaleNearby = true;
  homeStatusText(st, hs, t);
  CHECK(t.hintIsTouch && t.hint[0] == '\0' && t.hintTone == Tone::OK && t.canStart);

  st = LiveStatus();
  hs = HomeState();
  st.state = State::READY;
  st.scaleConnected = true;
  st.weightFresh = true;
  st.weightG = 0.04f;
  st.machineStyle = static_cast<uint8_t>(MachineStyle::TOGGLE);
  homeStatusText(st, hs, t);
  CHECK(strcmp(t.status, "NO BOT") == 0 && strcmp(t.hint, "PAIR THE BOT IN SETTINGS") == 0 && t.botDot == Tone::BAD);
  st.botPaired = true;
  homeStatusText(st, hs, t);
  CHECK(strcmp(t.status, "WAKING BOT  0.0 g") == 0 && t.botWaking && t.botDot == Tone::ACCENT && t.canStart);
  CHECK(t.hintIsTouch && t.scaleDot == Tone::OK);
  st.botConnected = true;
  st.scaleBatteryPercent = 12;
  st.botBatteryPercent = 80;
  homeStatusText(st, hs, t);
  CHECK(strcmp(t.status, "READY  0.0 g") == 0 && t.botDot == Tone::OK);
  CHECK(t.scaleBattery == 12 && t.scaleBatteryTone == Tone::ACCENT && t.botBattery == 80 && t.botBatteryTone == Tone::DIM);
  st.cupRequired = true;
  homeStatusText(st, hs, t);
  CHECK(strcmp(t.status, "NO CUP  0.0 g") == 0 && strcmp(t.hint, "PLACE CUP ON SCALE") == 0 && t.hintTone == Tone::ACCENT);
  CHECK(t.scaleDot == Tone::ACCENT && !t.canStart);
  st.cupRequired = false;
  st.weightFresh = false;
  homeStatusText(st, hs, t);
  CHECK(strcmp(t.hint, "WAITING FOR WEIGHT") == 0 && !t.canStart);
  st.machineStyle = static_cast<uint8_t>(MachineStyle::MANUAL);  // no bot wanted
  st.botPaired = false;
  st.weightFresh = true;
  homeStatusText(st, hs, t);
  CHECK(strcmp(t.status, "READY  0.0 g") == 0 && t.canStart);
}

static void testScreenHomeCard() {
  printf("  home card\n");
  using namespace screen;
  Grinder grinder;
  Dial dial(grinder);
  Recipe r = screenRecipe();
  GrindAdvice g;
  HomeCard c;
  homeCardText(r, g, dial, 0, 0, c);
  CHECK(strcmp(c.name, "Double") == 0 && strcmp(c.yield, "18.0 g in -> 36.0 g out") == 0);
  CHECK(strcmp(c.yieldRatio, "18.0 g in -> 36.0 g out  1:2.0") == 0 && c.useRatio);
  CHECK(strcmp(c.grind, "grind not set") == 0 && c.advice[0] == '\0' && strcmp(c.foot, "brew time 28 s") == 0);
  CHECK(strcmp(c.pre, "pre-inf machine") == 0);
  strcpy(r.coffee, "Portrait");
  r.grindUm = 250;
  r.preinfusion = Preinfusion::HOLD;
  r.preinfusionHoldMs = 2000;
  r.bloomPauseMs = 6000;
  g.action = GrindAction::FINER;
  g.suggestedUm = 244;
  g.source = SensitivitySource::MEASURED;
  homeCardText(r, g, dial, 0, 0, c);
  CHECK(strcmp(c.name, "Portrait") == 0 && strcmp(c.yield, "Double - 18.0 in -> 36.0 out") == 0);
  CHECK(strcmp(c.grind, "grind 250 um") == 0);
  CHECK(strcmp(c.advice, "go finer: 244 um") == 0 && strcmp(c.adviceShort, "finer 244") == 0);
  CHECK(c.adviceTone == Tone::ACCENT);
  CHECK(strcmp(c.pre, "pre 2.0 s, bloom 6 s") == 0 && strcmp(c.preShort, "pre 2.0, bloom 6 s") == 0);
  g.refined = true;
  homeCardText(r, g, dial, 0, 0, c);
  CHECK(strcmp(c.advice, "liked: finer 244 um") == 0 && strcmp(c.adviceShort, "liked: finer 244") == 0);
  g.source = SensitivitySource::GUESS;
  g.refined = false;
  homeCardText(r, g, dial, 0, 0, c);
  CHECK(strcmp(c.advice, "go finer") == 0 && strcmp(c.adviceShort, "finer") == 0);
  g.action = GrindAction::HOLD;
  homeCardText(r, g, dial, 0, 0, c);
  CHECK(strcmp(c.advice, "brew time on target") == 0 && strcmp(c.adviceShort, "time on target") == 0 && c.adviceTone == Tone::OK);
  g.betweenSteps = true;
  homeCardText(r, g, dial, 0, 0, c);
  CHECK(strcmp(c.advice, "closest step to target") == 0);
  g.action = GrindAction::INCONSISTENT;
  homeCardText(r, g, dial, 0, 0, c);
  CHECK(strcmp(c.advice, "brew times vary") == 0 && strcmp(c.adviceShort, "times vary") == 0);
  // No setting yet: where to start, from the library, then a similar coffee.
  r.grindUm = 0;
  GrindAdvice none;
  homeCardText(r, none, dial, 262, 270, c);
  CHECK(strcmp(c.advice, "community: start 262 um") == 0 && strcmp(c.adviceShort, "community 262 um") == 0);
  CHECK(strcmp(c.adviceShortest, "community 262") == 0);
  r.coffeeId = 3;
  homeCardText(r, none, dial, 0, 270, c);
  CHECK(strcmp(c.advice, "similar: try 270 um") == 0 && strcmp(c.adviceShort, "similar: 270 um") == 0);
  CHECK(strcmp(c.adviceShortest, c.adviceShort) == 0);
  r.coffeeId = 0;  // a similar coffee needs a coffee
  homeCardText(r, none, dial, 0, 270, c);
  CHECK(c.advice[0] == '\0');
}

static void testScreenBrew() {
  printf("  brew phases\n");
  using namespace screen;
  LiveStatus st;
  BrewPhaseText b;
  st.state = State::TARING;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "AUTO TARE\nWAIT FOR ZERO") == 0 && strcmp(b.stop, "CANCEL") == 0 && !b.running);
  st.state = State::READY_TO_BREW;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "WAKING BOT\nSTARTS WHEN LINKED") == 0);
  st.botConnected = true;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "TOUCH KEY\nTO BREW") == 0);
  st.state = State::STARTING_SHOT;
  st.awaitingDrip = true;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "PULL THE SHOT\nWAITING FOR COFFEE") == 0 && strcmp(b.stop, "STOP") == 0 && b.running);
  st.awaitingDrip = false;
  st.holdingButton = true;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "PRE-INFUSION\nHOLDING BUTTON") == 0);
  st.holdingButton = false;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "STARTING") == 0);
  st.state = State::BREWING;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "PRE-INFUSION") == 0);
  st.firstDropSeen = true;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "BREWING") == 0);
  st.targetCue = true;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "STOP NOW") == 0);
  brewPhaseText(st, true, b);  // BOOT pressed: asks first
  CHECK(strcmp(b.phase, "STOP SHOT?\nPRESS AGAIN") == 0 && strcmp(b.stop, "YES\nSTOP") == 0 && b.phaseTone == Tone::BAD);
  st.state = State::BLOOMING;
  st.bloomLeftS = 4.4f;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "BLOOM\n4 s") == 0);
  st.holdingButton = true;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "BLOOM DONE\nSTARTING") == 0);
  st.state = State::STOPPING_SHOT;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "STOPPING") == 0 && b.phaseTone == Tone::ACCENT);
  st.manualStopNeeded = true;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "STOP BY HAND!") == 0 && b.phaseTone == Tone::BAD);
  st.state = State::POST_SHOT_DRIP;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "FINALIZING\nLEAVE CUP") == 0);
  st.verifyingStop = true;
  brewPhaseText(st, false, b);
  CHECK(strcmp(b.phase, "CHECKING\nPUMP IS OFF") == 0);
}

static ShotRecord screenShot() {
  ShotRecord s;
  s.id = 7;
  s.targetWeightG = 36.0f;
  s.stopOffsetG = 1.5f;
  s.finalWeightG = 36.4f;
  s.shotTimeS = 28.6f;
  s.pourTimeS = 0.0f;
  s.targetTimeS = 28.0f;
  s.dripWeightG = 2.1f;
  s.averageFlowGS = 1.27f;
  s.flags = SHOT_FLAG_FINAL_VALID;
  s.endReason = static_cast<uint8_t>(EndReason::WEIGHT);
  s.grindUm = 250;
  s.timeBasis = static_cast<uint8_t>(GrindTimeBasis::TOTAL);
  s.grindVote = static_cast<uint8_t>(GrindVote::AUTO);
  return s;
}

static void testScreenResult() {
  printf("  result texts\n");
  using namespace screen;
  Grinder grinder;
  Dial dial(grinder);
  ResultInput in;
  in.have = true;
  in.shot = screenShot();
  in.toleranceS = 2.0f;
  GrindAdvice g;
  g.action = GrindAction::HOLD;
  g.targetTimeS = 28.0f;
  ResultText r;
  resultText(in, g, dial, r);
  CHECK(strcmp(r.top, "OVER +0.4 g   SLOW +0.6 s") == 0 && r.topTone == Tone::OK && !r.abnormal);
  CHECK(strcmp(r.weight, "36.4 g") == 0 && strcmp(r.line1, "28.6 s") == 0);
  CHECK(r.line2Count == 1 && strcmp(r.line2[0], "1.27 g/s   drip 2.1 g") == 0);
  CHECK(!r.review && !r.grindChange && r.counted);
  CHECK(strcmp(r.grind, "BREW TIME OK   KEEP GRIND") == 0 && r.grindTone == Tone::OK);
  CHECK(strcmp(r.grindSub, "28.6 s, target 28 s") == 0);
  g.shotsUsed = 3;
  g.averageTimeS = 27.9f;
  resultText(in, g, dial, r);
  CHECK(strcmp(r.grindSub, "this shot 28.6 s, last 3\naverage 27.9 s, target 28 s") == 0);

  in.shot.finalWeightG = 36.0f;
  in.shot.shotTimeS = 28.0f;
  in.shot.pourTimeS = 25.0f;
  in.shot.pourFlowGS = 1.5f;
  in.shot.flags |= SHOT_FLAG_LEARNED;
  in.shot.stopOffsetG = 1.5f;
  in.shot.nextStopOffsetG = 1.2f;
  resultText(in, g, dial, r);
  CHECK(strcmp(r.top, "ON TARGET   ON TIME") == 0);
  CHECK(strcmp(r.line1, "28.0 s total   pour 25.0 s") == 0);
  CHECK(r.line2Count == 3 && strcmp(r.line2[0], "1.50 g/s  drip 2.1 g  auto later 0.30") == 0);
  CHECK(strcmp(r.line2[1], "1.5 g/s  drip 2.1 g  auto later 0.30") == 0 && strcmp(r.line2[2], "1.5 g/s drip 2.1 g later 0.30") == 0);

  // A shot that did not settle, and one stopped early.
  in.shot = screenShot();
  in.shot.flags = 0;
  resultText(in, g, dial, r);
  CHECK(strcmp(r.weight, "36.4 g *") == 0);
  CHECK(strcmp(r.grindSub, "cup lifted, weight unsettled:\nnot counted for grind advice") == 0 && r.grind[0] == '\0');
  in.shot = screenShot();
  in.state = State::SHOT_TIMEOUT;
  in.shot.endReason = static_cast<uint8_t>(EndReason::TIMEOUT);
  resultText(in, g, dial, r);
  CHECK(r.abnormal && r.timedOut && strcmp(r.top, "SHOT TIMED OUT") == 0 && r.topTone == Tone::BAD);
  CHECK(strcmp(r.line1, "OVER +0.4 g   28.6 s") == 0);
  CHECK(r.review);  // it ran out of time and has not been ruled on: asked
  CHECK(strncmp(r.reviewText, "Shot slower than expected.\nReview preparation routine.\n", 54) == 0);
  CHECK(strcmp(endHeading(EndReason::MACHINE_STOPPED), "STOPPED AT THE MACHINE") == 0);
  CHECK(strcmp(endHeading(EndReason::CUP_REMOVED), "CUP LIFTED: STOPPED") == 0);
  CHECK(strcmp(endHeading(EndReason::RECOVERED), "STOPPED AFTER RESTART") == 0);
  CHECK(strcmp(endHeading(EndReason::USER), "SHOT ENDED EARLY") == 0);

  // Not counted, in each case.
  CHECK(strcmp(notCountedReason(GrindUse::IGNORED_BY_USER, EndReason::NONE), "ignored for grind advice,\nas you chose") == 0);
  CHECK(strcmp(notCountedReason(GrindUse::NO_TIME, EndReason::NONE), "no first drops detected:\nnot counted for grind advice") == 0);
  CHECK(strcmp(notCountedReason(GrindUse::OTHER_BASIS, EndReason::NONE), "time basis was changed:\nnot counted for grind advice") == 0);
  CHECK(strcmp(notCountedReason(GrindUse::STOP_RETRIED, EndReason::NONE), "stop had to be repeated:\nnot counted for grind advice") == 0);
  CHECK(strcmp(notCountedReason(GrindUse::TOO_SHORT, EndReason::NONE), "under 15 s: check preparation;\ntap COUNT to use it") == 0);
  CHECK(strcmp(notCountedReason(GrindUse::NOT_WEIGHT_STOP, EndReason::USER), "stopped with STOP:\nnot counted for grind advice") == 0);
  CHECK(strcmp(notCountedReason(GrindUse::NOT_WEIGHT_STOP, EndReason::CUP_REMOVED), "cup lifted during the shot:\nnot counted for grind advice") == 0);
  CHECK(strcmp(notCountedReason(GrindUse::NOT_WEIGHT_STOP, EndReason::RECOVERED), "restarted during the shot:\nnot counted for grind advice") == 0);
  CHECK(strcmp(notCountedReason(GrindUse::NOT_WEIGHT_STOP, EndReason::NONE), "did not stop on its weight:\nnot counted for grind advice") == 0);
  CHECK(strcmp(notCountedReason(GrindUse::TOO_LONG, EndReason::NONE), "this shot is not counted\nfor grind advice") == 0);

  // Grind verdicts.
  in.state = State::SHOT_COMPLETE;
  in.shot = screenShot();
  GrindAdvice slow;
  slow.action = GrindAction::COARSER;
  slow.source = SensitivitySource::MEASURED;
  slow.deltaUm = 12;
  slow.suggestedUm = 262;
  resultText(in, slow, dial, r);
  CHECK(r.grindChange && strcmp(r.grind, "BREW SLOW   GRIND COARSER") == 0);
  CHECK(strcmp(r.grindSub, "+12 um coarser  ->  set 262 um") == 0 || strstr(r.grindSub, "coarser  ->  set 262 um") != nullptr);
  slow.source = SensitivitySource::GUESS;
  resultText(in, slow, dial, r);
  CHECK(strcmp(r.grindSub, "see web page for details") == 0);
  GrindAdvice vary;
  vary.action = GrindAction::INCONSISTENT;
  vary.minTimeS = 22.0f;
  vary.maxTimeS = 34.0f;
  vary.currentUm = 250;
  resultText(in, vary, dial, r);
  CHECK(strcmp(r.grind, "BREW TIMES VARY   NO ADVICE") == 0 && strcmp(r.grindSub, "22 to 34 s at 250 um:\ncheck the preparation") == 0);
  GrindAdvice noGrind;
  resultText(in, noGrind, dial, r);
  CHECK(r.grind[0] == '\0' && strcmp(r.grindSub, "set the grind in EDIT\nfor grind advice") == 0);
  GrindAdvice prep;
  prep.action = GrindAction::HOLD;
  prep.checkPrep = true;
  prep.checkPrepWhy = CHECK_PREP_UNEVEN;
  resultText(in, prep, dial, r);
  CHECK(strcmp(r.grind, "CHECK PREPARATION   GRIND KEPT") == 0 && strcmp(r.grindSub, "28.6 s, flow uneven:\ncheck dose and tamp, pull again") == 0);
  GrindAdvice oneOff;
  oneOff.action = GrindAction::HOLD;
  oneOff.newestIgnored = true;
  resultText(in, oneOff, dial, r);
  CHECK(r.review);  // a one-off among consistent shots is put to the owner
  in.shot.grindVote = static_cast<uint8_t>(GrindVote::COUNT);
  resultText(in, oneOff, dial, r);
  CHECK(!r.review && strcmp(r.grind, "BREW SLOW   ONE-OFF?") == 0 && strcmp(r.grindSub, "28.6 s vs 28 s: grind kept,\nadvice if it repeats") == 0);
}

static void testScreenGrindPrompts() {
  printf("  grind questions\n");
  using namespace screen;
  Grinder grinder;
  Dial dial(grinder);
  Recipe r = screenRecipe();
  GrindPromptText t;
  noGrindText(r, 250, dial, t);
  CHECK(strcmp(t.title, "SET THE GRIND") == 0 && strcmp(t.setting, "250 um") == 0 && t.useUm == 250);
  CHECK(strcmp(t.why, "This recipe: no grind setting. The last shot was at 250 um. Set it before you brew.") == 0);
  CHECK(strcmp(t.use, "SET\n250") == 0 && strcmp(t.keep, "NOT NOW") == 0);
  strcpy(r.coffee, "Portrait");
  noGrindText(r, 0, dial, t);  // no last shot and no range: nothing known
  CHECK(strcmp(t.setting, "Not set") == 0 && t.useUm == 0 && strcmp(t.use, "SET") == 0);
  CHECK(strcmp(t.why, "Portrait: no grind setting. Set it before you brew.") == 0);

  GrindAdvice g;
  g.action = GrindAction::COARSER;
  g.source = SensitivitySource::GUESS;
  g.currentUm = 250;
  g.averageTimeS = 31.5f;
  g.targetTimeS = 28.0f;
  g.shotsUsed = 1;
  grindReminderText(g, dial, t);
  CHECK(strcmp(t.title, "GRIND REMINDER") == 0 && strcmp(t.setting, "GRIND COARSER") == 0 && t.useUm == 0);
  CHECK(strcmp(t.why, "The last shot ran 31.5 s (aim 28 s): the advice was coarser. The grinder is still set at 250 um.") == 0);
  CHECK(strcmp(t.use, "OK") == 0);
  g.source = SensitivitySource::MEASURED;
  g.suggestedUm = 262;
  g.shotsUsed = 3;
  grindReminderText(g, dial, t);
  CHECK(strcmp(t.setting, "250 -> 262 um") == 0 && t.useUm == 262 && strcmp(t.use, "SET\n262") == 0);
  CHECK(strncmp(t.why, "The last 3 shots averaged 31.5 s (aim 28 s)", 42) == 0);

  grindRecommendationText(RecSource::COMMUNITY, 262, 8, 250, 1, "", "Portrait", dial, t);
  CHECK(strcmp(t.title, "NEW GRIND ADVICE") == 0 && strcmp(t.setting, "262 um") == 0);
  CHECK(strcmp(t.why, "Portrait: from the community (1 person), +/- 8. Grinder now at 250 um.") == 0);
  CHECK(strcmp(t.use, "SET\n262") == 0 && strcmp(t.keep, "KEEP\n250") == 0);
  grindRecommendationText(RecSource::SIMILAR, 262, 8, 0, 0, "Other", "Portrait", dial, t);
  CHECK(strcmp(t.why, "Portrait: from your similar coffee Other, +/- 8. No setting yet.") == 0 && strcmp(t.keep, "NOT NOW") == 0);
  grindRecommendationText(RecSource::RANGE, 262, 8, 0, 0, "", "Portrait", dial, t);
  CHECK(strcmp(t.why, "Portrait: a first guess from the grinder's range, +/- 8. No setting yet.") == 0);
  grindRecommendationText(RecSource::COMMUNITY, 262, 8, 0, 5, "", "Portrait", dial, t);
  CHECK(strstr(t.why, "(5 people)") != nullptr);
}

static void testScreenText() {
  testPalettes();
  testScreenFaults();
  testScreenRecipeLines();
  testScreenHome();
  testScreenHomeCard();
  testScreenBrew();
  testScreenResult();
  testScreenGrindPrompts();
}
