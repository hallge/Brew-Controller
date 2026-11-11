// Host tests for the stop and restart safety fixes of the audit of 10 Nov 2026 (C5, C10): the stop
// retry after an unconfirmed press, and the recovery of a shot that was running at a restart. The
// storage and backup tests of the same audit (C3, C4, B5, B6) are in test_store_main.cpp, a program of
// its own. Included by test_main.cpp after Rig, and called from main().

// C5. The Breville goes on at full flow for about 1.7 s after the stop press (stopDelayMs). A stop whose
// acknowledgement was lost reached its decision at 1.5 s, on that tail, and pressed again: the second
// press started the pump. The decision now comes 4 s after the press, needs the same gain as the
// retry in the drip phase, and counts against the limit.
static void testUnconfirmedStopWithTail() {
  printf("unconfirmed stop on a machine that pours 1.7 s after the press\n");
  {
    Rig r;
    r.sim.stopDelayMs = 1700;
    r.toReady();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 10000));
    r.run(8000);
    r.sim.unconfirmedPresses = 1;
    CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
    CHECK(r.sim.presses == 2);                  // start and stop: the tail is not a running pump
    CHECK(!r.sim.machineRunning);
    ShotRecord shot;
    CHECK(r.ctl.takeCompletedShot(shot));
    CHECK(!(shot.flags & SHOT_FLAG_STOP_RETRIED));
  }
  {
    // The same, with a stop that really was missed (the arm moved, the button did not): still pouring
    // 4 s after the press, with a real gain, so it is pressed again once, and the shot ends stopped.
    Rig r;
    r.sim.stopDelayMs = 1700;
    r.toReady();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 10000));
    r.run(8000);
    r.sim.unconfirmedPresses = 1;
    r.sim.ignorePresses = 1;
    CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
    CHECK(r.sim.presses == 3);                  // start, missed stop, one retry
    CHECK(!r.sim.machineRunning);
    ShotRecord shot;
    CHECK(r.ctl.takeCompletedShot(shot));
    CHECK(shot.flags & SHOT_FLAG_STOP_RETRIED);
    // The retry is not the stop the shot was timed to, nor the weight it stopped at.
    CHECK(shot.stopWeightG < shot.finalWeightG - 3.0f);
    CHECK_NEAR(shot.shotTimeS, (r.sim.pressTimes[1] - r.sim.pressTimes[0]) / 1000.0, 0.2);
    CHECK(shot.stopLatencyMs < 20000);
  }
  {
    // It counts against the limit: a button that never answers is pressed MAX_STOP_RETRIES times and
    // then reported, not pressed for ever.
    Rig r;
    r.toReady();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 10000));
    r.run(8000);
    r.sim.ignorePresses = 100;
    r.sim.unconfirmedPresses = 100;
    CHECK(r.runUntil(State::STOP_COMMAND_FAILED, 90000));
    CHECK(r.sim.presses <= 4);                  // start, stop and at most the 2 retries
    CHECK(r.ctl.status(r.sim.nowMs).manualStopNeeded);
  }
}

// A cup knocked after the stop reads as a burst of "flow" far above what a pump pours. It must not be
// answered with a press (which would start the pump), nor may the tail of the burst, while it is still
// inside the flow window.
static void testCupBumpAfterStop() {
  printf("a cup knocked after the stop is not a running pump\n");
  for (double bump : {25.0, 14.0}) {
    Rig r;
    r.sim.stopDelayMs = 1700;
    r.toReady();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 10000));
    r.run(8000);
    r.sim.unconfirmedPresses = 1;
    CHECK(r.runUntil(State::STOPPING_SHOT, 60000));
    const uint32_t pressedAt = r.sim.pressTimes.back();
    while (r.sim.nowMs - pressedAt < 3300) r.step();   // the tail is over, the retry decision is near
    r.sim.trueWeightG += bump;
    CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
    CHECK(r.sim.presses == 2);
    CHECK(!r.sim.machineRunning);
  }
  {
    // The same knock after an acknowledged stop, while the drip phase is still unconfirmed.
    Rig r;
    r.sim.stopDelayMs = 1700;
    r.toReady();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 10000));
    r.run(8000);
    CHECK(r.runUntil(State::STOPPING_SHOT, 60000));
    const uint32_t pressedAt = r.sim.pressTimes.back();
    while (r.sim.nowMs - pressedAt < 3300) r.step();   // the stop is not confirmed yet: the tail was long
    r.sim.trueWeightG += 25.0;
    CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
    CHECK(r.sim.presses == 2 && !r.sim.machineRunning);
  }
  {
    // A pump that really runs on is still stopped: 9 g/s is not a bump when the flow at the stop was
    // 7.5 g/s (the Breville with an empty basket), and the retry limit holds.
    Rig r;
    r.sim.flowGS = 7.5;
    r.toReady();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 10000));
    r.sim.ignorePresses = 1;
    CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
    CHECK(r.sim.presses == 3 && !r.sim.machineRunning);
  }
}

// C10. A sleeping brew bot can take 40 s to connect after a restart. The recovery looks for a shot that
// was running for HARD_MAX_SHOT_MS, and a cup holding that shot's coffee is not zeroed on the way.
static void testRecoveryOutlastsSlowBot() {
  printf("restart during a shot, the brew bot takes 50 s to connect\n");
  Rig r;
  r.sim.botConnectDelayMs = 50000;
  r.sim.flowGS = 0.8;                           // slow enough for the reading to be "still"
  r.sim.machineRunning = true;
  r.sim.machineStartedMs = r.sim.nowMs - 10000;
  r.sim.trueWeightG = 20.0;                     // the cup already holds coffee
  r.ctl.armRestartRecovery(r.sim.nowMs);
  r.run(45000);
  CHECK(r.sim.machineRunning && r.sim.presses == 0);   // the old window had run out by now
  r.run(15000);
  CHECK(!r.sim.machineRunning);
  CHECK(r.sim.presses == 1);
  CHECK(r.sim.tares == 0);                      // the cup was not zeroed while the shot was running
  r.run(8000);
  ShotRecord recovered;
  CHECK(r.ctl.takeCompletedShot(recovered));
  CHECK(recovered.endReason == static_cast<uint8_t>(EndReason::RECOVERED));

  // Nothing flowing after a restart: the same window, no press and no tare of a cup that is just there.
  Rig idle;
  idle.sim.trueWeightG = 20.0;
  idle.ctl.armRestartRecovery(idle.sim.nowMs);
  idle.run(100000);
  CHECK(idle.sim.presses == 0 && idle.ctl.state() == State::READY);
}

static void testSafety() {
  testUnconfirmedStopWithTail();
  testCupBumpAfterStop();
  testRecoveryOutlastsSlowBot();
}
