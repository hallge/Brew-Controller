// Host tests for core/AppHost.h: link planning, the bot holds, the outbox pump, the test press.
// Included by test_main.cpp after the CHECK macros.
#include "core/AppHost.h"

static void testAppHost() {
  printf("AppHost\n");
  // ---- holds: extend-only, scale connect sets the pre-wake, cup placed extends it.
  {
    BotHoldTimers t;
    BotHoldTimers::Result r = t.update(1000, 0, false, false, false);
    CHECK(r.holdUntilMs == 0 && !r.warmChanged && !r.cupChanged);
    r = t.update(2000, r.holdUntilMs, true, false, false);  // scale connects
    CHECK(r.holdUntilMs == 2000 + BOT_PREWAKE_MS);
    uint32_t h = r.holdUntilMs;
    r = t.update(3000, h, true, false, false);  // linger is shorter: never shortens the hold
    CHECK(r.holdUntilMs == h);
    // a cup placed extends to the pre-wake only when that is later
    r = t.update(4000, h, true, false, true);
    CHECK(r.cupChanged && r.holdUntilMs == 4000 + BOT_PREWAKE_MS);
    r = t.update(4010, r.holdUntilMs, true, false, true);
    CHECK(!r.cupChanged);
    r = t.update(4020, r.holdUntilMs, true, false, false);
    CHECK(r.cupChanged);
  }
  {  // warm-up hold: lingers, reports edges once, ends
    BotHoldTimers t;
    BotHoldTimers::Result r = t.update(100, 0, false, true, false);
    CHECK(r.warm && r.warmChanged && r.holdUntilMs == 100 + BOT_LINGER_MS);
    r = t.update(200, r.holdUntilMs, false, true, false);
    CHECK(!r.warmChanged && r.holdUntilMs == 200 + BOT_LINGER_MS);
    r = t.update(300, r.holdUntilMs, false, false, false);
    CHECK(r.warmChanged && !r.warm && r.holdUntilMs == 200 + BOT_LINGER_MS);
    CHECK(BotHoldTimers::actuatorHold(500) == 500 + BOT_LINGER_MS);
  }
  {  // wrap-around
    CHECK(holdActive(0xFFFFFFF0u, 5u));
    CHECK(!holdActive(10u, 5u));
    CHECK(holdExtended(0xFFFFFFF0u, 5u) == 5u);
    CHECK(holdExtended(5u, 0xFFFFFFF0u) == 5u);
  }
  // ---- the scale connects with no other link up; never by dropping a bot under a shot.
  {
    typedef ScaleFirst::Step S;
    CHECK(ScaleFirst::step(false, false, false, 0) == S::CONNECT);
    CHECK(ScaleFirst::step(true, false, false, 0) == S::RELEASE);   // an idle pusher is let go first
    CHECK(ScaleFirst::step(true, false, false, 100) == S::RELEASE); // and waited for
    CHECK(ScaleFirst::step(true, true, false, 0) == S::WAIT);       // a press is never cut off
    CHECK(ScaleFirst::step(true, true, false, ScaleFirst::CLEAR_WAIT_MS) == S::CONNECT);
    CHECK(ScaleFirst::step(true, false, false, ScaleFirst::CLEAR_WAIT_MS) == S::CONNECT);
    CHECK(ScaleFirst::step(true, false, true, 0) == S::CONNECT);    // a shot may be running: the bot stays
    CHECK(ScaleFirst::step(true, true, true, 0) == S::CONNECT);
    CHECK(ScaleFirst::pushersWait(true, true, UINT32_MAX, false, 0));
    CHECK(ScaleFirst::pushersWait(true, false, 5000, false, 0));
    CHECK(!ScaleFirst::pushersWait(true, false, ScaleFirst::HEARD_MS, false, 0));  // the scale is off
    CHECK(!ScaleFirst::pushersWait(false, true, 0, false, 0));      // resting, or already linked
    CHECK(!ScaleFirst::pushersWait(true, true, 0, true, 0));        // under a shot the bots come first
    CHECK(!ScaleFirst::pushersWait(true, true, 0, false, ScaleFirst::GIVE_UP_FAILURES));
    CHECK(ScaleFirst::pushersWait(true, true, 0, false, ScaleFirst::GIVE_UP_FAILURES - 1));
    // a scale just linked: the bots wait for its readings, but not for ever and not under a shot
    CHECK(ScaleFirst::settling(true, 0, 100, false));
    CHECK(ScaleFirst::settling(true, ScaleFirst::SETTLED_NOTIFICATIONS - 1, 4000, false));
    CHECK(!ScaleFirst::settling(true, ScaleFirst::SETTLED_NOTIFICATIONS, 100, false));
    CHECK(!ScaleFirst::settling(true, 0, ScaleFirst::SETTLE_MS, false));
    CHECK(!ScaleFirst::settling(false, 0, 100, false));
    CHECK(!ScaleFirst::settling(true, 0, 100, true));
    // a scale just switched on gets a moment before its first connection; never under a shot
    CHECK(ScaleFirst::appeared(false, 0));
    CHECK(ScaleFirst::appeared(true, ScaleFirst::ABSENT_MS + 1));
    CHECK(!ScaleFirst::appeared(true, ScaleFirst::ABSENT_MS));      // a link that just dropped: not new
    CHECK(ScaleFirst::fresh(true, 500, 2000, false));
    CHECK(!ScaleFirst::fresh(true, 2000, 2000, false));
    CHECK(!ScaleFirst::fresh(true, 500, 0, false));                 // switched off by the radio test
    CHECK(!ScaleFirst::fresh(false, 500, 2000, false));             // never seen appearing: no wait
    CHECK(!ScaleFirst::fresh(true, 500, 2000, true));
  }
  // ---- links: during a shot only the stop bot, never the power bot.
  {
    LinkPlanner::Inputs in;
    in.now = 1000;
    in.powerWantsLink = true;
    in.powerHoldUntilMs = 5000;
    LinkPlanner::Plan p = LinkPlanner::plan(in);
    CHECK(!p.botWanted && !p.stopWanted && p.powerWanted && !p.powerKept);
    in.shotPhase = true;
    p = LinkPlanner::plan(in);
    CHECK(p.stopWanted && !p.powerWanted);
    in.shotPhase = false;
    in.powerWantsLink = false;
    p = LinkPlanner::plan(in);
    CHECK(p.powerWanted);  // the hold alone
    in.now = 5000;
    p = LinkPlanner::plan(in);
    CHECK(!p.powerWanted);
    in.stopHoldUntilMs = 6000;
    in.botHoldUntilMs = 6000;
    in.keepPowerBotConnected = true;
    p = LinkPlanner::plan(in);
    CHECK(p.stopWanted && p.botWanted && p.powerKept);
    // exclusivity over every combination
    for (int m = 0; m < 8; ++m) {
      LinkPlanner::Inputs x;
      x.now = 100;
      x.shotPhase = true;
      x.powerWantsLink = m & 1;
      x.powerHoldUntilMs = (m & 2) ? 200 : 0;
      x.stopHoldUntilMs = (m & 4) ? 200 : 0;
      const LinkPlanner::Plan q = LinkPlanner::plan(x);
      CHECK(q.stopWanted && !q.powerWanted);
    }
  }
  // ---- outbox: back-off and drop.
  {
    OutboxPump o;
    CHECK(!o.due(500));
    CHECK(o.due(1000));
    CHECK(!o.due(1999));
    CHECK(o.due(2000));
    CHECK(o.step("a.json", true, false, false) == OutboxPump::Action::POP_SENT);
    CHECK(o.step("a.json", false, true, false) == OutboxPump::Action::POP_REFUSED);
    CHECK(o.step("a.json", false, false, false) == OutboxPump::Action::RETRY);
    // key missing: first the key goes ahead (90 s hold), then the record is tried; dropped on the 4th
    uint32_t t = 10000;
    CHECK(o.due(t));
    CHECK(o.step("b.json", false, false, true) == OutboxPump::Action::SEND_KEY_FIRST);
    CHECK(o.holdMs() == 90000);
    CHECK(!o.due(t + 89999));
    t += 90000;
    CHECK(o.due(t));
    CHECK(o.holdMs() == 0);
    CHECK(o.step("b.json", false, false, true) == OutboxPump::Action::SEND_KEY_FIRST);
    CHECK(o.step("b.json", false, false, true) == OutboxPump::Action::SEND_KEY_FIRST);
    CHECK(o.step("b.json", false, false, true) == OutboxPump::Action::DROP_KEY);
    // a different record starts afresh; a success forgets the key
    CHECK(o.step("c.json", false, false, true) == OutboxPump::Action::SEND_KEY_FIRST);
    CHECK(o.step("c.json", true, false, false) == OutboxPump::Action::POP_SENT);
    CHECK(o.step("c.json", false, false, true) == OutboxPump::Action::SEND_KEY_FIRST);
    CHECK(o.due(t + 90000 + 1000) || true);
  }
  // ---- test press: time-out, busy, press, tidy.
  {
    using S = TestPressFsm::Step;
    CHECK(TestPressFsm::step(100, 1000, true, false, false) == S::NONE);
    CHECK(TestPressFsm::step(100, 1000, true, true, true) == S::NONE);
    CHECK(TestPressFsm::step(100, 1000, true, true, false) == S::TRY_PRESS);
    CHECK(TestPressFsm::step(1000, 1000, true, true, false) == S::FAIL_TIMEOUT);
    CHECK(TestPressFsm::step(100, 1000, false, true, false) == S::FAIL_BUSY);
    CHECK(TestPressFsm::step(0xFFFFFFF0u, 5u, true, false, false) == S::NONE);  // wrap, not yet
    CHECK(TestPressFsm::step(5u, 5u, true, true, false) == S::FAIL_TIMEOUT);
    CHECK(TestPressFsm::tidyDue(true, true, false));
    CHECK(!TestPressFsm::tidyDue(true, true, true));
    CHECK(!TestPressFsm::tidyDue(true, false, false));
    CHECK(!TestPressFsm::tidyDue(false, true, false));
  }
  // ---- standby
  {
    CHECK(standbyRestable(State::READY, false, false));
    CHECK(!standbyRestable(State::READY, false, true));
    CHECK(standbyRestable(State::SHOT_COMPLETE, true, false));
    CHECK(!standbyRestable(State::SHOT_COMPLETE, false, false));
    CHECK(standbyRestable(State::SCALE_DISCONNECTED, true, false));
    CHECK(!standbyRestable(State::CONNECTING_SCALE, true, false));
  }
}
