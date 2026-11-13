#pragma once
// The device-independent orchestration the firmware (src/main.cpp) and a future phone app share:
// which Bluetooth links are wanted, how long the brew bot is held awake, what becomes of a record in
// the share outbox, and the Setup page's test press. Time and state are passed in; nothing in here
// reads a clock, takes a lock or touches a radio, so the host tests can drive every edge.
//
// Portable C++17, no Arduino, no exceptions, no containers.

#include <stdint.h>
#include <string.h>

#include "Types.h"

namespace bbw {

// ------------------------------------------------------------------ bot holds --
constexpr uint32_t BOT_LINGER_MS = 10000;                           // the link is dropped this long after the need
constexpr uint32_t BOT_PREWAKE_MS = 120000;                         // scale connected or cup placed: a shot is coming
constexpr uint32_t BOT_WARMUP_HOLD_MAX_MS = 4UL * 60UL * 60UL * 1000UL;  // power-on to first shot, at most

// "now is before until", across the 32-bit wrap.
inline bool holdActive(uint32_t now, uint32_t untilMs) { return static_cast<int32_t>(now - untilMs) < 0; }
// An extend-only hold: `until` replaces `holdUntil` only when it is later.
inline uint32_t holdExtended(uint32_t holdUntil, uint32_t until) {
  return static_cast<int32_t>(until - holdUntil) > 0 ? until : holdUntil;
}

// The brew bot's hold (the time until which its link is wanted). Keeps the edge detectors that used
// to be static locals in the control task. The hold value itself lives with the caller (other
// threads extend it too); update() takes it and returns the new one.
class BotHoldTimers {
 public:
  struct Result {
    uint32_t holdUntilMs = 0;
    bool warm = false;         // the warm-up hold is on
    bool warmChanged = false;  // ... and that is a change since the last call
    bool cupChanged = false;   // a cup went on or came off since the last call
  };

  // Order as the firmware had it: scale connects (hold set, not extended), the 10 s linger while the
  // scale is connected, the warm-up linger, then a cup placed (extend to the pre-wake).
  Result update(uint32_t now, uint32_t holdUntilMs, bool scaleConnected, bool warm, bool cupPresent) {
    Result r;
    uint32_t hold = holdUntilMs;
    if (scaleConnected && !scaleWasConnected_) hold = now + BOT_PREWAKE_MS;
    if (scaleConnected) hold = holdExtended(hold, now + BOT_LINGER_MS);
    scaleWasConnected_ = scaleConnected;
    if (warm) hold = holdExtended(hold, now + BOT_LINGER_MS);
    r.warm = warm;
    r.warmChanged = warm != warmHeld_;
    warmHeld_ = warm;
    if (cupPresent && !cupWasPresent_) hold = holdExtended(hold, now + BOT_PREWAKE_MS);
    r.cupChanged = cupPresent != cupWasPresent_;
    cupWasPresent_ = cupPresent;
    r.holdUntilMs = hold;
    return r;
  }

  // From Start until the shot is confirmed over the bot is needed: the hold is set (not extended).
  static uint32_t actuatorHold(uint32_t now) { return now + BOT_LINGER_MS; }

 private:
  bool scaleWasConnected_ = false;
  bool warmHeld_ = false;
  bool cupWasPresent_ = false;
};

// ---------------------------------------------------------------- link planner --
// Three Bluetooth links at most with Wi-Fi running: the scale, the brew bot and one more. On a
// machine with its own stop button that one is the stop bot from Start until the shot is over, and the
// power bot the rest of the time (the machine is not switched off under a shot).
class LinkPlanner {
 public:
  struct Inputs {
    uint32_t now = 0;
    uint32_t botHoldUntilMs = 0;
    uint32_t stopHoldUntilMs = 0;
    uint32_t powerHoldUntilMs = 0;
    bool shotPhase = false;         // BrewController::stopActuatorNeeded()
    bool powerWantsLink = false;    // PowerControl::wantsLink()
    bool keepPowerBotConnected = false;
  };
  struct Plan {
    bool botWanted = false;
    bool stopWanted = false;
    bool powerWanted = false;
    bool powerKept = false;
  };

  static Plan plan(const Inputs &in) {
    Plan p;
    p.botWanted = holdActive(in.now, in.botHoldUntilMs);
    p.stopWanted = in.shotPhase || holdActive(in.now, in.stopHoldUntilMs);
    p.powerKept = in.keepPowerBotConnected;
    p.powerWanted = !in.shotPhase && (in.powerWantsLink || holdActive(in.now, in.powerHoldUntilMs));
    return p;
  }
};

// ------------------------------------------------------------------ scale first --
// The scale's connection is set up with no other Bluetooth link up. Measured from the board's stored
// log (13 Nov 2026, v0.1.30-v0.1.36): the Lunar's set-up worked 14 times of 14 with no pusher linked,
// 2 of 11 started in the instant the power bot was let go (its link was still up), and 0 of 28 with
// the brew bot linked: the link was made, went silent, and was lost 4 s later. So a pusher's link is
// let go first and taken again once the scale is up. Never under a shot: the bots are then the only
// way to stop the machine, and the scale is tried with them linked.
class ScaleFirst {
 public:
  static constexpr uint32_t CLEAR_WAIT_MS = 3000;  // a press ending, or a link coming down
  static constexpr uint8_t GIVE_UP_FAILURES = 6;   // a scale that will not connect does not keep the bots out
  static constexpr uint32_t HEARD_MS = 15000;      // the scale counts as there this long after an advertisement
  enum class Step : uint8_t { CONNECT, RELEASE, WAIT };

  // The scale is due a connection attempt now. waitedMs: since it first was, this time round.
  static Step step(bool pusherLinkUp, bool pusherBusy, bool machineMayRun, uint32_t waitedMs) {
    if (!pusherLinkUp || machineMayRun || waitedMs >= CLEAR_WAIT_MS) return Step::CONNECT;
    return pusherBusy ? Step::WAIT : Step::RELEASE;
  }

  // A scale just linked is left alone until its readings flow. Seen on the board (13 Nov 2026): the brew
  // bot's connection attempt started 100 ms after the scale's set-up, the scale's readings never came
  // ("no readings yet", 1 notification in 5 s), its link was dropped, and the bot, linked by then, kept
  // the scale from connecting again.
  static constexpr uint32_t SETTLE_MS = 5000;
  static constexpr uint32_t SETTLED_NOTIFICATIONS = 3;
  static bool settling(bool scaleLinked, uint32_t notifications, uint32_t linkedForMs, bool machineMayRun) {
    return scaleLinked && !machineMayRun && notifications < SETTLED_NOTIFICATIONS && linkedForMs < SETTLE_MS;
  }

  // A scale that has just appeared (not heard and not linked for a while: it was switched on) is given
  // a moment before the first connection. Seen on the board with v0.1.37 (13 Nov 2026): with no other
  // link up, the first attempt after switch-on made the link in 83 ms and lost it at the 4 s timeout,
  // the second set up in 4.3 s; the same in nearly every run that day. Never under a shot: a scale
  // whose link dropped mid-shot is taken back at once.
  static constexpr uint32_t ABSENT_MS = 5000;  // not heard and not linked this long: it was off
  static bool appeared(bool everPresent, uint32_t absentForMs) { return !everPresent || absentForMs > ABSENT_MS; }
  static bool fresh(bool appearedKnown, uint32_t sinceAppearedMs, uint32_t freshMs, bool machineMayRun) {
    return appearedKnown && !machineMayRun && sinceAppearedMs < freshMs;
  }

  // No new pusher connection is started while the scale is wanted and there to be connected.
  static bool pushersWait(bool scaleWanted, bool scaleDue, uint32_t heardAgoMs, bool machineMayRun,
                          uint8_t scaleFailures) {
    return scaleWanted && !machineMayRun && scaleFailures < GIVE_UP_FAILURES &&
           (scaleDue || heardAgoMs < HEARD_MS);
  }
};

// ------------------------------------------------------------------ outbox pump --
// What becomes of the record at the head of the share outbox after one try. The HTTPS work and the
// storage stay with the caller.
class OutboxPump {
 public:
  static constexpr uint32_t PERIOD_MS = 1000;        // one record a second
  static constexpr uint32_t KEY_HOLD_MS = 90000;     // the relay's store of keys takes up to a minute
  static constexpr uint8_t KEY_TRIES = 3;
  static constexpr size_t PATH_LEN = 96;

  enum class Action : uint8_t {
    RETRY,           // not taken, not refused: leave it, try again next period
    POP_SENT,        // taken
    POP_REFUSED,     // refused for good
    DROP_KEY,        // the key went ahead of it enough times and it is still not taken
    SEND_KEY_FIRST,  // the relay has no key: send it now, the record goes next time (a hold is set)
  };

  // True when a try is due now (and starts the period). The hold from a key send lasts one period.
  bool due(uint32_t now) {
    if (now - lastMs_ < (holdMs_ ? holdMs_ : PERIOD_MS)) return false;
    lastMs_ = now;
    holdMs_ = 0;
    return true;
  }

  Action step(const char *path, bool published, bool refused, bool keyMissing) {
    if (published) {
      keySentFor_[0] = '\0';
      return Action::POP_SENT;
    }
    if (refused) return Action::POP_REFUSED;
    if (keyMissing) {
      if (strncmp(keySentFor_, path, PATH_LEN) == 0 && ++keyTries_ >= KEY_TRIES) {
        keySentFor_[0] = '\0';
        return Action::DROP_KEY;
      }
      if (strncmp(keySentFor_, path, PATH_LEN) != 0) {
        strncpy(keySentFor_, path, PATH_LEN - 1);
        keySentFor_[PATH_LEN - 1] = '\0';
        keyTries_ = 0;
      }
      holdMs_ = KEY_HOLD_MS;
      return Action::SEND_KEY_FIRST;
    }
    return Action::RETRY;
  }

  uint32_t holdMs() const { return holdMs_; }

 private:
  uint32_t lastMs_ = 0;
  uint32_t holdMs_ = 0;
  char keySentFor_[PATH_LEN] = {};
  uint8_t keyTries_ = 0;
};

// -------------------------------------------------------------------- test press --
// The Setup page's test press: asked for by the page, done by the control task once the brew bot's
// link is up, and only while nothing is brewing or being set up.
enum : uint8_t { TEST_NONE, TEST_WAITING, TEST_PRESSED, TEST_FAILED };
constexpr uint32_t TEST_PRESS_PATIENCE_MS = 40000;  // a sleeping SwitchBot advertises only every 10-15 s

struct TestPressFsm {
  enum class Step : uint8_t { NONE, FAIL_BUSY, FAIL_TIMEOUT, TRY_PRESS };

  // One look while the state is TEST_WAITING. TRY_PRESS means the bot is up and free: the caller
  // starts the press and moves to TEST_PRESSED only if that succeeds.
  static Step step(uint32_t now, uint32_t untilMs, bool idle, bool botConnected, bool botBusy) {
    if (!idle) return Step::FAIL_BUSY;
    if (static_cast<int32_t>(now - untilMs) >= 0) return Step::FAIL_TIMEOUT;
    return botConnected && !botBusy ? Step::TRY_PRESS : Step::NONE;
  }
  // After a press: clear the bot's result once nothing is going on and the arm is back.
  static bool tidyDue(bool tidy, bool idle, bool botBusy) { return tidy && idle && !botBusy; }
};

// ------------------------------------------------------------------ scale standby --
// The scale may be let go (so it can switch itself off) only on READY, or on a result or fault
// screen when nothing is going on, and not while the machine is warming up.
inline bool standbyRestable(State state, bool idleForPower, bool warmingUp) {
  return (state == State::READY ||
          ((state == State::SHOT_COMPLETE || isFault(state)) && idleForPower)) &&
         !warmingUp;
}

}  // namespace bbw
