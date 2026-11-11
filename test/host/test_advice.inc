// Host tests for the grind-advice audit fixes of 10 Nov 2026 (research: audit C2, C6, C8):
//   G4  the room term's strict gate (core/GrindAdvisor.h grindRoomEffectStrict)
//   G6  guards against steering finer on a fast shot that may be the puck
//   G7  comparability (pre-infusion hold, bloom pause, own stop target) and the aim confidence is scored against
// Included by test_main.cpp after the CHECK macros. The real-shots room case stays in testGrindRoom.

static float adviceTestNoise(int i, float scale) { return scale * sinf(static_cast<float>(i) * 2.399f + 0.7f); }

static void testAdviceRoomGate() {
  printf("  room gate (G4)\n");
  const uint32_t t0 = 1793000000u;
  // Staling in a cooling week, no room effect at all: time rises 0.8 s a day while the room falls
  // 0.7 C a day. The original rule took this for a room effect (about -1.1 s/C); the age term and
  // the prior must not.
  {
    RoomShot shots[24];
    for (int i = 0; i < 24; ++i) {
      const float day = static_cast<float>(i / 2);
      shots[i].group = 1;
      shots[i].setting = 60.0f + 2.0f * static_cast<float>(i % 4);
      shots[i].epochS = t0 + static_cast<uint32_t>(i / 2) * 86400u + static_cast<uint32_t>(i % 2) * 3600u;
      shots[i].roomC = 22.0f - 0.7f * day + 1.2f * sinf(static_cast<float>(i) * 1.7f);
      shots[i].timeS = 25.0f + 0.8f * day - 0.3f * (shots[i].setting - 60.0f) + adviceTestNoise(i, 0.8f);
    }
    const GrindRoomFit legacy = grindRoomEffect(shots, 24, true);
    const GrindRoomFit strict = grindRoomEffect(shots, 24);
    printf("    staling+cooling: legacy %.2f s/C (used %d); strict %.2f s/C (z %.1f, used %d, suspected %d)\n",
           legacy.sPerC, legacy.used ? 1 : 0, strict.sPerC, strict.z, strict.used ? 1 : 0, strict.suspected ? 1 : 0);
    CHECK(legacy.used && legacy.sPerC < 0.0f);   // what the audit showed: the original rule accepts it
    CHECK(!strict.used && !strict.suspected);
  }
  // A true -3 s/C on two coffees, the room unrelated to the day: accepted, replicated, applied at
  // the cap (-1 s/C, the physical limit from the research note).
  RoomShot two[28];
  for (int g = 0; g < 2; ++g) {
    for (int i = 0; i < 14; ++i) {
      RoomShot &s = two[g * 14 + i];
      s.group = static_cast<uint16_t>(g + 1);
      s.setting = 60.0f + 4.0f * static_cast<float>(i % 3);
      s.epochS = t0 + static_cast<uint32_t>(i) * 86400u;
      s.roomC = 19.0f + 3.0f * sinf(static_cast<float>(i) * 1.3f + static_cast<float>(g));
      s.timeS = 30.0f + 3.0f * g - 0.3f * (s.setting - 60.0f) - 3.0f * (s.roomC - 19.0f) + adviceTestNoise(g * 14 + i, 0.6f);
    }
  }
  {
    const GrindRoomFit fit = grindRoomEffect(two, 28);
    printf("    two coffees at -3: %.2f s/C (se %.2f, z %.1f), agree %u, used %d\n", fit.sPerC, fit.se, fit.z,
           static_cast<unsigned>(fit.groupsAgree), fit.used ? 1 : 0);
    CHECK(fit.used && fit.replicated && !fit.suspected && !fit.legacy && fit.groupsAgree == 2 && fit.groupsDisagree == 0);
    CHECK_NEAR(fit.sPerC, GRIND_ROOM_STRICT_MAX_S_PER_C, 1e-4f);   // -3 is capped at -1
    CHECK(GRIND_ROOM_STRICT_MAX_S_PER_C == -1.0f);
    // One of the two coffees alone: significant, not replicated: suspected, not applied.
    const GrindRoomFit one = grindRoomEffect(two, 14);
    CHECK(!one.used && one.suspected && !one.replicated && one.sPerC < 0.0f && one.sPerC >= GRIND_ROOM_STRICT_MAX_S_PER_C);
    // A second coffee that disagrees in sign keeps it from replicating.
    RoomShot split[28];
    for (int i = 0; i < 28; ++i) {
      split[i] = two[i];
      if (i >= 14) split[i].timeS = 30.0f + 3.0f * (split[i].roomC - 19.0f) - 0.3f * (split[i].setting - 60.0f);
    }
    const GrindRoomFit sp = grindRoomEffect(split, 28);
    CHECK(!sp.used && !sp.replicated);
    // The original rule on the same two coffees: its own slope (about -3), not capped at -1.
    const GrindRoomFit legacy = grindRoomEffect(two, 28, true);
    CHECK(legacy.used && legacy.legacy && legacy.sPerC < -2.0f);
  }
  // Within-run pairs replace the second coffee: one coffee, one setting, morning 17 C and afternoon 21 C
  // on each of 12 days, a true -3 s/C: 23 pairs under 2 days apart.
  {
    RoomShot shots[24];
    for (int i = 0; i < 24; ++i) {
      shots[i].group = 1;
      shots[i].setting = 70.0f;
      shots[i].epochS = t0 + static_cast<uint32_t>(i / 2) * 86400u + static_cast<uint32_t>(i % 2) * 6u * 3600u;
      shots[i].roomC = (i % 2 ? 21.0f : 17.0f) + 0.4f * sinf(static_cast<float>(i));
      shots[i].timeS = 30.0f - 3.0f * (shots[i].roomC - 19.0f) + adviceTestNoise(i, 0.5f);
    }
    const GrindRoomFit fit = grindRoomEffect(shots, 24);
    printf("    pairs: %.2f s/C (z %.1f), pairs %u, used %d\n", fit.sPerC, fit.z, static_cast<unsigned>(fit.pairs),
           fit.used ? 1 : 0);
    CHECK(fit.pairs >= GRIND_ROOM_REPLICATE_PAIRS && fit.replicated && fit.used);
    // Too few pairs (only the first six shots): not replicated.
    const GrindRoomFit few = grindRoomEffect(shots, 7);
    CHECK(!few.used);
  }
  // Applying: the shift of one shot is capped at one tolerance with the strict cap; a suspected
  // effect is carried on the advice and moves nothing.
  {
    GrindShot shots[2];
    for (int i = 0; i < 2; ++i) {
      shots[i].timeS = 30.0f;
      shots[i].grindUm = 70;
      shots[i].epochS = 1793800000u - static_cast<uint32_t>(i) * 86400u;
      shots[i].roomC = 20.0f;
    }
    GrindConfig config;
    config.roomSPerC = -3.0f;
    config.roomNowC = 17.0f;
    config.roomCapTolerances = GRIND_ROOM_STRICT_MAX_TOLERANCES;
    const GrindAdvice capped = adviseGrind(shots, 2, 70, 29.0f, config);
    CHECK_NEAR(capped.roomShiftS, config.toleranceS, 0.01f);
    GrindConfig sus;
    sus.roomSuspectedSPerC = -0.8f;
    sus.roomNowC = 17.0f;
    const GrindAdvice a = adviseGrind(shots, 2, 70, 29.0f, sus);
    CHECK(a.roomSuspectedSPerC == -0.8f && a.roomSPerC == 0.0f && a.roomShiftS == 0.0f && a.action == GrindAction::HOLD);
    // The setting's values.
    Settings st;
    CHECK(!grindRoomOff(st) && !grindRoomLegacy(st));
    st.grindRoomMark = GRIND_ROOM_LEGACY;
    CHECK(grindRoomLegacy(st) && !grindRoomOff(st));
    st.grindRoomMark = GRIND_ROOM_OFF;
    CHECK(grindRoomOff(st) && !grindRoomLegacy(st));
  }
}

// A shot list newest first, all at one setting.
static void adviceTestShots(GrindShot *out, const float *times, int n, uint16_t setting) {
  for (int i = 0; i < n; ++i) {
    out[i] = GrindShot();
    out[i].timeS = times[i];
    out[i].grindUm = setting;
  }
}

static void testAdviceFastGuards() {
  printf("  fast-shot guards (G6)\n");
  GrindConfig base;
  GrinderElasticity none;
  const GrindConfig c = grindConfigForGrinder(base, 1.0f, 70, 29.0f, none);   // the E64's configuration
  GrindShot s[4];
  // The audit's four probes (target 29 +- 2 s).
  {
    const float t1[] = {20.0f};
    adviceTestShots(s, t1, 1, 70);
    // The half step for a lone fast shot is built and tested but off in the controller's config (see
    // GrindConfig::loneFastDamping): the settling simulations lose two points of two-shot success.
    const GrindAdvice a = adviseGrind(s, 1, 70, 29.0f, c);
    CHECK(a.action == GrindAction::FINER && a.deltaUm == -20 && !a.loneFastDamped && !c.loneFastDamping);
    GrindConfig damp = c;
    damp.loneFastDamping = true;
    const GrindAdvice d = adviseGrind(s, 1, 70, 29.0f, damp);
    CHECK(d.action == GrindAction::FINER && d.loneFastDamped && d.stepFactor == 0.5f && d.deltaUm == -10);
    damp.fastGuards = false;   // the guards off altogether: as it always was
    CHECK(adviseGrind(s, 1, 70, 29.0f, damp).deltaUm == -20);
  }
  {
    const float t2[] = {20.0f, 30.0f};
    adviceTestShots(s, t2, 2, 70);
    const GrindAdvice a = adviseGrind(s, 2, 70, 29.0f, c);   // FINER 10, unchanged: two shots, not a lone one
    CHECK(a.action == GrindAction::FINER && a.deltaUm == -10 && !a.loneFastDamped);
  }
  {
    const float t3[] = {20.0f, 30.0f, 29.0f};
    adviceTestShots(s, t3, 3, 70);
    const GrindAdvice a = adviseGrind(s, 3, 70, 29.0f, c);   // HOLD: the 20 s is a one-off
    CHECK(a.action == GrindAction::HOLD && a.outliersIgnored == 1 && !a.checkPrep);
  }
  {
    const float t4[] = {21.0f, 20.0f, 30.0f};
    adviceTestShots(s, t4, 3, 70);
    const GrindAdvice a = adviseGrind(s, 3, 70, 29.0f, c);   // FINER 20: two agreeing fast shots
    CHECK(a.action == GrindAction::FINER && a.deltaUm == -20 && !a.loneFastDamped);
  }
  // A shot marked uneven: FINER is held back (no move, the dial-in is not settled), unless the owner
  // said COUNT; a slow uneven shot still says coarser.
  {
    const float t[] = {20.0f};
    adviceTestShots(s, t, 1, 70);
    s[0].uneven = true;
    GrindAdvice a = adviseGrind(s, 1, 70, 29.0f, c);
    CHECK(a.action == GrindAction::NO_DATA && a.checkPrep && a.checkPrepWhy == CHECK_PREP_UNEVEN && a.deltaUm == 0 &&
          a.suggestedUm == 70);
    a = adviseGrindRated(s, 1, 70, 29.0f, c, true);
    CHECK(a.action == GrindAction::NO_DATA && a.baseAction == GrindAction::NO_DATA);   // not "settled", not refined
    s[0].forced = true;
    CHECK(adviseGrind(s, 1, 70, 29.0f, c).action == GrindAction::FINER);
    const float slow[] = {40.0f};
    adviceTestShots(s, slow, 1, 70);
    s[0].uneven = true;
    CHECK(adviseGrind(s, 1, 70, 29.0f, c).action == GrindAction::COARSER);
  }
  // Fast and sour: held back when it is more than two tolerances fast; an ordinary fast shot is not.
  {
    const float t[] = {24.0f};
    adviceTestShots(s, t, 1, 70);
    s[0].taste = 1;
    GrindAdvice a = adviseGrind(s, 1, 70, 29.0f, c);
    CHECK(a.action == GrindAction::NO_DATA && a.checkPrepWhy == CHECK_PREP_FAST_SOUR);
    s[0].timeS = 25.5f;   // 3.5 s fast: finer is fair
    a = adviseGrind(s, 1, 70, 29.0f, c);
    CHECK(a.action == GrindAction::FINER && !a.checkPrep);
    s[0].timeS = 24.0f;
    s[0].taste = 3;       // bitter and fast: no veto
    CHECK(adviseGrind(s, 1, 70, 29.0f, c).action == GrindAction::FINER);
  }
  // Under 15 s: check the preparation (the owner taps COUNT to use it); too long as it was.
  {
    GrindAdvice a = adviseGrind(nullptr, 0, 70, 29.0f, c);
    adviseOutOfRange(a, GrindUse::TOO_SHORT, 70, c);
    CHECK(a.action == GrindAction::NO_DATA && a.checkPrep && a.checkPrepWhy == CHECK_PREP_TOO_SHORT);
    a = adviseGrind(nullptr, 0, 70, 29.0f, c);
    adviseOutOfRange(a, GrindUse::TOO_LONG, 70, c);
    CHECK(a.action == GrindAction::COARSER && a.outOfRange && !a.checkPrep);
    GrindConfig off = c;
    off.fastGuards = false;
    a = adviseGrind(nullptr, 0, 70, 29.0f, off);
    adviseOutOfRange(a, GrindUse::TOO_SHORT, 70, off);
    CHECK(a.action == GrindAction::FINER && a.outOfRange && !a.checkPrep);
  }
}

static void testAdviceComparable() {
  printf("  comparable shots and the aim (G7)\n");
  Recipe recipe;
  recipe.recipeId = 3;
  recipe.coffeeId = 5;
  recipe.targetWeightG = 36.0f;
  recipe.doseG = 18.0f;
  recipe.preinfusion = Preinfusion::HOLD;
  recipe.preinfusionHoldMs = 10000;
  recipe.bloomPauseMs = 0;
  ShotRecord r;
  r.recipeId = 3;
  r.coffeeId = 5;
  r.targetWeightG = 36.0f;
  r.doseG = 18.0f;
  r.preinfusionHoldMs = 10000;
  r.bloomPauseMs = 0;
  CHECK(grindRunComparable(r, recipe));
  r.preinfusionHoldMs = 7000;   // a hold edited from 7 to 10 s
  CHECK(!grindRunComparable(r, recipe));
  r.preinfusionHoldMs = 10200;  // a tenth or two of a second is the same
  CHECK(grindRunComparable(r, recipe));
  r.preinfusionHoldMs = 10000;
  r.bloomPauseMs = 5000;
  CHECK(!grindRunComparable(r, recipe));
  r.bloomPauseMs = 0;
  r.flags |= SHOT_FLAG_OWN_TARGET;   // stopped for a target of its own: another yield
  CHECK(!grindRunComparable(r, recipe));
  r.flags = 0;
  recipe.preinfusion = Preinfusion::MACHINE;   // the machine's own: the record keeps 0
  r.preinfusionHoldMs = 0;
  CHECK(grindRunComparable(r, recipe));
  r.preinfusionHoldMs = 7000;
  CHECK(!grindRunComparable(r, recipe));
  // Two shots differing only in the hold are not averaged: 29 s at the current hold, 22 s at the old.
  {
    recipe.preinfusion = Preinfusion::HOLD;
    ShotRecord now = r, old = r;
    now.preinfusionHoldMs = 10000;
    old.preinfusionHoldMs = 7000;
    const float times[2] = {29.0f, 22.0f};   // newest first: now, old
    const ShotRecord *records[2] = {&now, &old};
    GrindShot kept[2];
    int n = 0;
    for (int i = 0; i < 2; ++i) {
      if (!grindRunComparable(*records[i], recipe)) continue;
      kept[n] = GrindShot();
      kept[n].timeS = times[i];
      kept[n].grindUm = 70;
      ++n;
    }
    CHECK(n == 1);
    GrindConfig c;
    CHECK(adviseGrind(kept, n, 70, 29.0f, c).action == GrindAction::HOLD);
    // Averaged, they would have read 25.5 s: "too fast, finer".
    GrindShot both[2];
    both[0].timeS = 29.0f; both[0].grindUm = 70;
    both[1].timeS = 22.0f; both[1].grindUm = 70;
    CHECK(adviseGrind(both, 2, 70, 29.0f, c).action != GrindAction::HOLD);
  }
  // The aim: a refined advice (liked time 27 s, recipe target 29 s) is stored and scored against it.
  {
    GrindShot shots[6];
    for (int i = 0; i < 6; ++i) {
      shots[i].grindUm = 70;
      shots[i].timeS = i < 3 ? 29.5f : 27.0f;
      shots[i].rating = i < 3 ? 0 : 5;
    }
    GrindConfig c;
    const GrindAdvice a = adviseGrindRated(shots, 6, 70, 29.0f, c, true);
    CHECK(a.refined && a.action == GrindAction::COARSER && fabsf(a.aimTimeS - 27.0f) < 0.01f && a.targetTimeS == 29.0f);
    ShotRecord rec;
    rec.id = 12;
    ShotContextInputs in;
    in.shot = &rec;
    in.advice = a;
    const ShotContext ctx = buildShotContext(in);
    CHECK(ctx.aimOffsetDs == -20);
    CHECK_NEAR(adviceAimS(29.0f, ctx.aimOffsetDs), 27.0f, 1e-4f);
    // A record from before it was kept, or advice not refining: the recipe's target.
    CHECK(adviceAimS(29.0f, 0) == 29.0f);
    GrindAdvice plain = a;
    plain.refined = false;
    in.advice = plain;
    CHECK(buildShotContext(in).aimOffsetDs == 0);
    // Six followed advices that landed on the aim (27.2 s): high enough to trust (> 0.5); scored against
    // the recipe's 29 s the same six read as 1.8 s misses and score lower.
    AdviceOutcome outcomes[6];
    AdviceOutcome versus[6];
    for (int i = 0; i < 6; ++i) {
      outcomes[i].followed = true;
      outcomes[i].nextTimeS = 27.0f + (i % 2 ? 0.3f : 0.1f);
      outcomes[i].targetS = adviceAimS(29.0f, ctx.aimOffsetDs);
      versus[i] = outcomes[i];
      versus[i].targetS = 29.0f;
    }
    const AdviceConfidence onAim = adviceConfidence(outcomes, 6, 2.0f);
    const AdviceConfidence onTarget = adviceConfidence(versus, 6, 2.0f);
    printf("    landed on the aim: %.2f; the same against the recipe target: %.2f\n", onAim.score, onTarget.score);
    CHECK(onAim.score > 0.5f && onTarget.score < 0.5f && onAim.score > onTarget.score);
  }
}

static void testAdvice() {
  printf("grind advice audit fixes\n");
  testAdviceRoomGate();
  testAdviceFastGuards();
  testAdviceComparable();
}
