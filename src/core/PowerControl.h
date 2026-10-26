#pragma once
// The machine's power button, pressed by the second pusher (the power bot):
// on and off when asked, and by a weekly schedule.
//
// The power button is a toggle and the machine does not say whether it is on,
// so the controller keeps its own account of it: "believed on" or "believed
// off", changed by every press it makes and corrected by the owner on the
// web page when the two have drifted apart (someone pressed the button by
// hand, or the machine switched itself off). Until the owner has said once
// which it is, nothing is pressed by the schedule: a press on a wrong belief
// would switch the machine the wrong way.
//
// Pure C++, no Arduino: host tested.

#include <initializer_list>
#include <stddef.h>
#include <stdint.h>

#include "BrewController.h"
#include "PusherDriver.h"

namespace bbw {

constexpr size_t POWER_SCHEDULE_ENTRIES = 6;

struct PowerScheduleEntry {
  uint8_t enabled = 0;
  uint8_t turnOn = 1;          // 1: switch on, 0: switch off
  uint16_t minuteOfDay = 0;    // local time, 0..1439
  uint8_t days = 0x7f;         // bit 0 Sunday .. bit 6 Saturday
};

enum class PowerBelief : uint8_t { OFF = 0, ON = 1, UNKNOWN = 2 };

// Everything about the pushers that is kept across restarts and is not in the
// older "pairing" and "net" records. Fixed layout: stored as it is.
struct PusherConfig {
  char powerAddress[18] = {};  // empty: no power bot
  uint8_t powerAddressType = 0;
  PusherSecrets brew;          // password unused here: the brew Bot's is in the network record
  PusherSecrets power;
  PowerScheduleEntry schedule[POWER_SCHEDULE_ENTRIES];
  int16_t utcOffsetMin = 0;    // local time minus UTC, as the browser last reported it
  uint8_t belief = static_cast<uint8_t>(PowerBelief::UNKNOWN);
};

// The stop bot (a machine with separate start and stop buttons). A record of
// its own, added later: the one above is stored as it is and cannot grow.
struct StopBotConfig {
  char address[18] = {};       // empty: no stop bot
  uint8_t addressType = 0;
  PusherSecrets secrets;
};

inline void sanitizeStopBotConfig(StopBotConfig &c) {
  c.address[sizeof(c.address) - 1] = '\0';
  c.secrets.password[sizeof(c.secrets.password) - 1] = '\0';
  c.secrets.tuyaUuid[sizeof(c.secrets.tuyaUuid) - 1] = '\0';
  c.secrets.tuyaDeviceId[sizeof(c.secrets.tuyaDeviceId) - 1] = '\0';
  c.secrets.tuyaLocalKey[sizeof(c.secrets.tuyaLocalKey) - 1] = '\0';
  if (c.secrets.tuyaVariant > 1) c.secrets.tuyaVariant = 0;
}

inline void sanitizePusherConfig(PusherConfig &c) {
  c.powerAddress[sizeof(c.powerAddress) - 1] = '\0';
  for (PusherSecrets *s : {&c.brew, &c.power}) {
    s->password[sizeof(s->password) - 1] = '\0';
    s->tuyaUuid[sizeof(s->tuyaUuid) - 1] = '\0';
    s->tuyaDeviceId[sizeof(s->tuyaDeviceId) - 1] = '\0';
    s->tuyaLocalKey[sizeof(s->tuyaLocalKey) - 1] = '\0';
    if (s->tuyaVariant > 1) s->tuyaVariant = 0;
  }
  for (PowerScheduleEntry &e : c.schedule) {
    if (e.minuteOfDay > 1439) e.minuteOfDay = 0;
    e.days &= 0x7f;
    e.enabled = e.enabled ? 1 : 0;
    e.turnOn = e.turnOn ? 1 : 0;
  }
  if (c.utcOffsetMin < -14 * 60 || c.utcOffsetMin > 14 * 60) c.utcOffsetMin = 0;
  if (c.belief > static_cast<uint8_t>(PowerBelief::UNKNOWN)) {
    c.belief = static_cast<uint8_t>(PowerBelief::UNKNOWN);
  }
}

// Who asked for a power press.
enum class PowerBy : uint8_t { OWNER = 0, SCHEDULE = 1, SCALE_OFF = 2 };

// One press of the power button the controller made, as it ended: for the owner's record of when
// the machine was switched (the web page's Insights). 8 bytes, stored as is in a ring file.
struct PowerEvent {
  uint32_t epochS = 0;     // wall clock when the press ended; 0 if the clock was not set
  uint8_t on = 0;          // 1: to switch on, 0: to switch off
  uint8_t by = 0;          // PowerBy: 0 the owner (web page or screen), 1 the schedule, 2 the scale switching off
  uint8_t outcome = 0;     // PowerControl::Outcome: DONE, or why it did not happen
  uint8_t sure = 1;        // 0: "press anyway" while on/off was not known, so the direction is a guess
};

class PowerControl {
 public:
  enum class Job : uint8_t { IDLE, WAITING, PRESSING };
  enum class Outcome : uint8_t {
    NONE, DONE, ALREADY, NO_BOT, NOT_KNOWN, BUSY, BOT_UNREACHABLE, MACHINE_BUSY, PRESS_FAILED
  };

  // A sleeping SwitchBot advertises rarely (measured on the board: the power
  // bot about once in two minutes) and can only be connected to then.
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 180000;
  static constexpr uint32_t PRESS_RETRIES = 3;
  static constexpr uint32_t MAX_CATCH_UP_MIN = 3;        // a schedule time missed by more is skipped

  void begin(PusherConfig *config) { config_ = config; }

  // The machine switches itself off after this many minutes with nothing done on it (the Breville
  // Dual Boiler: 240, unless its "A OF" menu setting turned Auto Off off); 0: it does not, or it is
  // not known. A machine believed on with nothing done for that long is then believed off, so the
  // next "on" presses the button instead of finding it "on already" (seen on the board 26 Oct 2026).
  void setAutoOffMin(uint16_t minutes) { autoOffMin_ = minutes; }
  // Something was done on the machine (a shot ran): it is on, and its own off-timer starts again.
  void noteUse(uint32_t nowMs) {
    lastUseMs_ = nowMs ? nowMs : 1;
    if (belief() != PowerBelief::ON) {
      config_->belief = static_cast<uint8_t>(PowerBelief::ON);
      onSinceMs_ = lastUseMs_;
      dirty_ = true;
      believedOnByShot_ = true;
    }
  }
  // Once, after a shot showed the machine on while it was believed off or not known.
  bool takeBelievedOnByShot() {
    const bool b = believedOnByShot_;
    believedOnByShot_ = false;
    return b;
  }
  // Once, after the off-timer has run out on a machine believed on.
  bool takeAutoOff() {
    const bool b = autoOff_;
    autoOff_ = false;
    return b;
  }
  // Once, after a scheduled press was not made: what it was for, and why (ALREADY, NOT_KNOWN...).
  bool takeSkipped(bool &on, Outcome &why) {
    if (!skipped_) return false;
    skipped_ = false;
    on = skippedOn_;
    why = skippedWhy_;
    return true;
  }
  // Switched on (by a press or a shot) within the last `withinMs`: warming up, someone is about to brew.
  bool switchedOnWithin(uint32_t nowMs, uint32_t withinMs) const {
    return belief() == PowerBelief::ON && onSinceMs_ != 0 &&
           static_cast<uint32_t>(nowMs - onSinceMs_) < withinMs;
  }

  PowerBelief belief() const { return static_cast<PowerBelief>(config_->belief); }
  Job job() const { return job_; }
  Outcome lastOutcome() const { return outcome_; }
  bool lastWasSchedule() const { return by_ == PowerBy::SCHEDULE; }
  PowerBy lastBy() const { return by_; }
  // The power bot's link is wanted: a press is waiting for it or in progress.
  bool wantsLink() const { return job_ != Job::IDLE; }
  // The last press that ended, once; false if none ended since the last call. Refusals (already
  // in that state, not known, busy, no bot) are not presses and give none.
  bool takeEvent(PowerEvent &out) {
    if (!eventReady_) return false;
    eventReady_ = false;
    out = event_;
    return true;
  }
  // The configuration changed (belief): the caller saves it.
  bool takeDirty() {
    const bool dirty = dirty_;
    dirty_ = false;
    return dirty;
  }

  // The owner says what the machine is doing; nothing is pressed.
  void believe(PowerBelief belief) {
    if (config_->belief == static_cast<uint8_t>(belief)) return;
    config_->belief = static_cast<uint8_t>(belief);
    dirty_ = true;
  }

  // Switch the machine on or off. False: refused, lastOutcome() says why.
  bool request(bool on, uint32_t nowMs, PowerBy by = PowerBy::OWNER) {
    by_ = by;
    if (config_->powerAddress[0] == '\0') return refuse(Outcome::NO_BOT);
    if (job_ != Job::IDLE) return refuse(Outcome::BUSY);
    if (belief() == PowerBelief::UNKNOWN) return refuse(Outcome::NOT_KNOWN);
    if ((belief() == PowerBelief::ON) == on) return refuse(Outcome::ALREADY);
    start(on, nowMs);
    return true;
  }

  // Press the power button whatever is believed (the owner sees the machine).
  // The belief flips if it was known.
  bool pressAnyway(uint32_t nowMs) {
    by_ = PowerBy::OWNER;
    if (config_->powerAddress[0] == '\0') return refuse(Outcome::NO_BOT);
    if (job_ != Job::IDLE) return refuse(Outcome::BUSY);
    toggle_ = true;
    start(belief() != PowerBelief::ON, nowMs);
    return true;
  }

  // epochS: wall clock in seconds, 0 if not known. idle: no shot is running
  // or being set up, so the machine may be switched.
  void tick(uint32_t nowMs, uint32_t epochS, bool idle, IActuator &bot) {
    epochS_ = epochS;
    if (lastUseMs_ == 0) lastUseMs_ = nowMs ? nowMs : 1;  // from start-up: nothing known before
    if (autoOffMin_ && job_ == Job::IDLE && idle && belief() == PowerBelief::ON &&
        static_cast<uint32_t>(nowMs - lastUseMs_) >= static_cast<uint32_t>(autoOffMin_) * 60000u) {
      config_->belief = static_cast<uint8_t>(PowerBelief::OFF);
      dirty_ = true;
      autoOff_ = true;
    }
    schedule(nowMs, epochS);
    switch (job_) {
      case Job::IDLE:
        break;
      case Job::WAITING:
        if (static_cast<uint32_t>(nowMs - sinceMs_) >= CONNECT_TIMEOUT_MS) {
          end(idle ? Outcome::BOT_UNREACHABLE : Outcome::MACHINE_BUSY);
        } else if (idle && bot.connected()) {
          bot.clearResult();
          if (bot.beginPress()) job_ = Job::PRESSING;
        }
        break;
      case Job::PRESSING:
        switch (bot.result()) {
          case PressResult::BUSY:
            break;
          case PressResult::OK:
          case PressResult::UNCONFIRMED:  // it went out; the owner corrects the belief if not
            bot.clearResult();
            if (toggle_ && belief() == PowerBelief::UNKNOWN) {
              // still not known
            } else {
              config_->belief = static_cast<uint8_t>(target_ ? PowerBelief::ON : PowerBelief::OFF);
              dirty_ = true;
              if (target_) onSinceMs_ = lastUseMs_ = nowMs ? nowMs : 1;
            }
            end(Outcome::DONE);
            break;
          case PressResult::NOT_SENT:
          case PressResult::IDLE:
            bot.clearResult();
            if (++tries_ >= PRESS_RETRIES) end(Outcome::PRESS_FAILED);
            else job_ = Job::WAITING;  // nothing moved: safe to try again
            break;
        }
        break;
    }
  }

  static const char *outcomeText(Outcome outcome) {
    switch (outcome) {
      case Outcome::NONE: return "";
      case Outcome::DONE: return "power button pressed";
      case Outcome::ALREADY: return "the machine is believed to be in that state already";
      case Outcome::NO_BOT: return "no power bot is paired";
      case Outcome::NOT_KNOWN: return "say first whether the machine is on or off";
      case Outcome::BUSY: return "a press is already on its way";
      case Outcome::BOT_UNREACHABLE: return "the power bot did not connect";
      case Outcome::MACHINE_BUSY: return "a shot was running";
      case Outcome::PRESS_FAILED: return "the power bot refused the press";
    }
    return "";
  }

  // Minutes since the start of the local week (Sunday 00:00), for the schedule.
  static uint32_t localMinuteOfWeek(uint32_t epochS, int16_t utcOffsetMin) {
    const int64_t local = static_cast<int64_t>(epochS) + static_cast<int64_t>(utcOffsetMin) * 60;
    const int64_t minutes = local / 60;
    // 1970-01-01 was a Thursday: day 4 of a week that starts on Sunday.
    return static_cast<uint32_t>((minutes + 4 * 1440) % (7 * 1440));
  }

 private:
  bool refuse(Outcome why) {
    outcome_ = why;
    return false;
  }
  void start(bool on, uint32_t nowMs) {
    target_ = on;
    job_ = Job::WAITING;
    sinceMs_ = nowMs;
    tries_ = 0;
    outcome_ = Outcome::NONE;
  }
  void end(Outcome outcome) {
    event_.epochS = epochS_ >= 1700000000u ? epochS_ : 0;
    event_.on = target_ ? 1 : 0;
    event_.by = static_cast<uint8_t>(by_);
    event_.outcome = static_cast<uint8_t>(outcome);
    event_.sure = toggle_ && belief() == PowerBelief::UNKNOWN ? 0 : 1;
    eventReady_ = true;
    job_ = Job::IDLE;
    toggle_ = false;
    outcome_ = outcome;
  }

  void schedule(uint32_t nowMs, uint32_t epochS) {
    if (epochS < 1700000000u) return;  // clock not set
    const uint32_t minute = static_cast<uint32_t>(
        (static_cast<int64_t>(epochS) + static_cast<int64_t>(config_->utcOffsetMin) * 60) / 60);
    if (!haveMinute_) {
      haveMinute_ = true;
      lastMinute_ = minute;
      return;
    }
    if (minute == lastMinute_) return;
    // The clock was set or jumped: do not act on times long past, or again on times already done.
    uint32_t from = minute > lastMinute_ && minute - lastMinute_ <= MAX_CATCH_UP_MIN ? lastMinute_ + 1 : minute;
    if (minute < lastMinute_) from = minute + 1;  // went backwards: nothing is due
    lastMinute_ = minute;
    for (uint32_t m = from; m <= minute; ++m) {
      const uint32_t ofWeek = (m + 4 * 1440) % (7 * 1440);
      const uint8_t day = static_cast<uint8_t>(ofWeek / 1440);
      const uint16_t ofDay = static_cast<uint16_t>(ofWeek % 1440);
      for (const PowerScheduleEntry &e : config_->schedule) {
        if (e.enabled && e.minuteOfDay == ofDay && (e.days & (1u << day))) {
          if (!request(e.turnOn != 0, nowMs, PowerBy::SCHEDULE)) {
            skipped_ = true;
            skippedOn_ = e.turnOn != 0;
            skippedWhy_ = outcome_;
          }
        }
      }
    }
  }

  PusherConfig *config_ = nullptr;
  Job job_ = Job::IDLE;
  Outcome outcome_ = Outcome::NONE;
  bool target_ = false;
  bool toggle_ = false;
  PowerBy by_ = PowerBy::OWNER;
  bool dirty_ = false;
  uint32_t sinceMs_ = 0;
  uint32_t tries_ = 0;
  bool haveMinute_ = false;
  uint32_t lastMinute_ = 0;
  uint32_t epochS_ = 0;
  uint16_t autoOffMin_ = 0;
  uint32_t lastUseMs_ = 0;    // last press that switched it on, or shot; 0 until the first tick
  uint32_t onSinceMs_ = 0;    // when it was last switched on; 0: not since start-up
  bool autoOff_ = false;
  bool believedOnByShot_ = false;
  bool skipped_ = false;
  bool skippedOn_ = false;
  Outcome skippedWhy_ = Outcome::NONE;
  PowerEvent event_;
  bool eventReady_ = false;
};

}  // namespace bbw
