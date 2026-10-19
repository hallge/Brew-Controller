#pragma once
// One button pusher connection, without the radio: the drivers this
// controller has, the session a pusher may need before it takes commands, and
// the press itself as a sequence (press, or down / hold / up, with the
// acknowledgements, time-outs and the rule that an arm left down is brought
// back up before anything else). ble/Ble.cpp only moves the bytes.
//
// The sequence is the one written for the SwitchBot Bot, unchanged; the host
// tests hold it to that. The controller has two of these: the brew bot on
// the machine's brew button and the power bot on its power button.
//
// Pure C++, no Arduino. Threading on the controller: connected() is called
// from the BLE task; everything else, onNotify() included, from the control
// task (ble/Ble.cpp queues the notifications for it).

#include <atomic>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "BrewController.h"
#include "MicroBotDriver.h"
#include "PusherDriver.h"
#include "SwitchBotDriver.h"
#include "TuyaFingerbotDriver.h"
#include "Types.h"

namespace bbw {

class PusherLink : private PusherEvents {
 public:
  static constexpr uint32_t ACK_TIMEOUT_MS = 3000;
  static constexpr uint32_t STEP_ACK_TIMEOUT_MS = 1500;
  static constexpr uint32_t UP_RETRY_MS = 300;
  // How long an up the pusher refuses (busy: its arm still travelling down)
  // is asked for again before the arm is left to the retries after settling.
  static constexpr uint32_t UP_GIVE_UP_MS = 6000;
  static constexpr uint32_t INFO_PERIOD_MS = 120000;
  static constexpr uint32_t SESSION_TIMEOUT_MS = 8000;
  static constexpr uint32_t HOLD_UNTIL_RELEASED = 0xFFFFFFFFu;

  // name: "bot" for the brew bot, "power" for the power bot; it leads the log lines.
  void begin(const char *name, const Settings *settings, PusherWriter *writer) {
    name_ = name;
    // Only the brew button is ever held (pre-infusion) or pressed down and up
    // separately. The power and stop buttons get the pusher's own complete
    // press: down and straight back up.
    plainPressOnly_ = strcmp(name, "bot") != 0;
    settings_ = settings;
    writer_ = writer;
  }
  void setSecrets(const PusherSecrets &secrets) { secrets_ = secrets; }
  void setSources(uint32_t (*random)(), uint32_t (*epoch)()) {
    random_ = random;
    epoch_ = epoch;
  }
  void setLog(void (*log)(const char *line)) { log_ = log; }

  // The pushers this controller can work, in the order they are tried.
  size_t driverCount() const { return sizeof(drivers_) / sizeof(drivers_[0]); }
  PusherDriver *driverAt(size_t index) { return index < driverCount() ? drivers_[index] : nullptr; }
  // The pusher a connected device is: the first driver whose characteristic
  // pair it has. hasPair(const PusherDriver &) says whether it has.
  template <typename HasPair>
  PusherDriver *identify(HasPair &&hasPair) {
    for (PusherDriver *candidate : drivers_) {
      if (hasPair(*candidate)) return candidate;
    }
    return nullptr;
  }

  PusherDriver *driver() const { return driver_.load(); }
  uint32_t caps() const {
    const PusherDriver *d = driver();
    return d ? d->caps() : 0;
  }

  // BLE task: the device just connected is this pusher, and it is subscribed.
  void connected(PusherDriver *driver) {
    started_ = false;
    driver_ = driver;
    startPending_ = true;
  }

  // The radio link is up and the pusher takes commands.
  bool ready(bool up) const {
    const PusherDriver *d = driver();
    return up && started_ && d && d->sessionReady();
  }
  // The session could not be set up (key missing or wrong): drop the link.
  bool sessionFailed() const { return sessionFailed_; }
  // Why, in words; nullptr if nothing is wrong.
  const char *problem() const {
    const PusherDriver *d = driver();
    if (d && d->sessionProblem()) return d->sessionProblem();
    return sessionFailed_ ? "no answer to the session setup (wrong key?)" : nullptr;
  }

  void onNotify(const uint8_t *data, size_t length) {
    PusherDriver *d = driver();
    if (d && started_ && writer_) d->onNotify(data, length, *this, *writer_);
  }

  // false when busy or not ready. holdMs = 0 is a normal momentary press;
  // otherwise the button is held down for that long before release.
  bool beginPress(uint32_t holdMs, uint32_t nowMs, bool up) {
    PusherDriver *d = driver();
    if (!ready(up) || phase_ != Phase::IDLE || needUp_) return false;
    // A timed hold (pre-infusion) always needs separate down and up commands:
    // the pusher's own press only holds in whole seconds set in its memory.
    // A pusher without them presses its own way, whatever was asked.
    const bool atomic = !d->has(PUSHER_CAP_DOWN_UP) ||
                        (holdMs == 0 && (plainPressOnly_ ||
                                         settings_->actuationMode == ActuationMode::ATOMIC_PRESS));
    if (holdMs != 0 && atomic) logf("%s: this pusher cannot hold the button, plain press", name_);
    atomic_ = atomic;
    holdMs_ = holdMs != 0 ? holdMs : settings_->pressDurationMs;
    result_ = PressResult::BUSY;
    downAcked_ = false;
    sentAtMs_ = nowMs;
    phaseSinceMs_ = nowMs;
    if (!sendAction(atomic ? PusherOp::PRESS : PusherOp::DOWN)) {
      result_ = PressResult::NOT_SENT;  // nothing left the radio; phase stays IDLE
      return true;
    }
    if (!atomic) needUp_ = true;
    phase_ = atomic ? Phase::WAIT_ACK : Phase::WAIT_DOWN_ACK;
    // A pusher that never says whether it acted: the command went out, and
    // that is all that is known.
    if (!d->has(PUSHER_CAP_ACK)) finish(PressResult::UNCONFIRMED, nowMs);
    return true;
  }

  // Down, and stay down until release(): for a button that brews while it is
  // held. false: busy, not ready, or this pusher has no separate down and up.
  bool beginHold(uint32_t nowMs, bool up) {
    PusherDriver *d = driver();
    if (!ready(up) || phase_ != Phase::IDLE || needUp_ || !d->has(PUSHER_CAP_DOWN_UP)) return false;
    atomic_ = false;
    holdMs_ = HOLD_UNTIL_RELEASED;
    result_ = PressResult::BUSY;
    downAcked_ = false;
    sentAtMs_ = nowMs;
    phaseSinceMs_ = nowMs;
    if (!sendAction(PusherOp::DOWN)) {
      result_ = PressResult::NOT_SENT;
      return true;
    }
    needUp_ = true;
    phase_ = Phase::WAIT_DOWN_ACK;
    return true;
  }
  // The arm is down and held (the down was acknowledged, or its answer was lost).
  bool holding() const { return phase_ == Phase::HOLD && holdMs_ == HOLD_UNTIL_RELEASED; }
  // Let go of a held button. With nothing held, the arm is told to come up
  // all the same: after a restart nothing is known about where it is.
  bool release(uint32_t nowMs, bool up) {
    if (holdMs_ == HOLD_UNTIL_RELEASED && (phase_ == Phase::HOLD || phase_ == Phase::WAIT_DOWN_ACK)) {
      holdMs_ = 0;  // the hold is over: the next tick sends the up
      sentAtMs_ = nowMs;  // this is the command the shot's end is timed by
      return true;
    }
    PusherDriver *d = driver();
    if (!ready(up) || phase_ != Phase::IDLE || !d->has(PUSHER_CAP_DOWN_UP)) return false;
    atomic_ = false;
    result_ = PressResult::BUSY;
    sentAtMs_ = nowMs;
    phaseSinceMs_ = nowMs;
    lastUpTryMs_ = nowMs;
    if (!sendAction(PusherOp::UP)) {
      result_ = PressResult::NOT_SENT;
      return false;
    }
    needUp_ = false;
    downAcked_ = true;  // only the up is in question
    upFirstMs_ = nowMs;
    upRetrying_ = false;
    phase_ = Phase::WAIT_UP_ACK;
    return true;
  }

  PressResult result() const { return result_; }
  uint32_t pressSentAtMs() const { return sentAtMs_; }
  void clearResult() {
    if (result_ != PressResult::BUSY) result_ = PressResult::IDLE;
  }
  // True while a press is in progress or the arm still has to come back up.
  bool busy() const { return phase_ != Phase::IDLE || needUp_; }
  // Last info read from the pusher; kept after the link is dropped.
  PusherInfo info() const { return info_; }
  uint8_t lastStatus() const { return lastStatus_.load(); }

  void tick(uint32_t nowMs, bool up) {
    PusherDriver *d = driver();
    if (up && d && writer_ && startPending_.exchange(false)) {
      d->begin(secrets_, random_, epoch_);
      started_ = true;
      sessionFailed_ = false;
      sessionSinceMs_ = nowMs;
      infoRequested_ = false;
      holdConfigured_ = false;
      lastInfoMs_ = nowMs - INFO_PERIOD_MS;  // ask for battery/mode right away
      d->onConnected(*writer_);
    }
    if (!d) return;
    if (up && started_ && !d->sessionReady() && !sessionFailed_ &&
        (d->sessionProblem() ||
         static_cast<uint32_t>(nowMs - sessionSinceMs_) >= SESSION_TIMEOUT_MS)) {
      sessionFailed_ = true;
      logf("%s: %s", name_, problem());
    }
    const bool ready = this->ready(up);

    // An arm left down keeps the button pressed. Getting it back up outranks
    // everything else, including across a reconnect.
    if (needUp_ && phase_ == Phase::IDLE && ready &&
        static_cast<uint32_t>(nowMs - lastUpTryMs_) >= UP_RETRY_MS) {
      lastUpTryMs_ = nowMs;
      if (sendAction(PusherOp::UP)) needUp_ = false;
    }

    switch (phase_) {
      case Phase::IDLE:
        if (ready && result_ != PressResult::BUSY &&
            static_cast<uint32_t>(nowMs - lastInfoMs_) >= INFO_PERIOD_MS) {
          lastInfoMs_ = nowMs;
          if (d->requestInfo(*writer_)) ackPending_ = false;
          infoRequested_ = true;
        } else if (ready && info_.valid && !holdConfigured_ && d->has(PUSHER_CAP_HOLD_SECONDS) &&
                   settings_->actuationMode == ActuationMode::ATOMIC_PRESS &&
                   static_cast<uint32_t>(nowMs - lastInfoMs_) >= 1000) {
          holdConfigured_ = true;
          if (info_.holdSeconds != settings_->botHoldSeconds) {
            if (d->setHoldSeconds(settings_->botHoldSeconds, *writer_)) ackPending_ = false;
            logf("%s: hold time set to %u s", name_, static_cast<unsigned>(settings_->botHoldSeconds));
            lastInfoMs_ = nowMs - INFO_PERIOD_MS + 2000;  // re-read to verify
          }
        }
        break;

      case Phase::WAIT_ACK:
        if (ackPending_.exchange(false)) {
          const PusherAck ack = ack_;
          if (ack == PusherAck::ACCEPTED) {
            finish(PressResult::OK, nowMs);
          } else if (ack == PusherAck::UNSUPPORTED) {
            finish(PressResult::UNCONFIRMED, nowMs);  // answered, but may not have moved
          } else {
            logf("%s: press refused: %s", name_, d->statusName(lastStatus_));
            finish(PressResult::NOT_SENT, nowMs);  // busy/password/battery: arm did not move
          }
        } else if (!ready || static_cast<uint32_t>(nowMs - phaseSinceMs_) >= ACK_TIMEOUT_MS) {
          finish(PressResult::UNCONFIRMED, nowMs);
        }
        break;

      case Phase::WAIT_DOWN_ACK:
        if (ackPending_.exchange(false)) {
          const PusherAck ack = ack_;
          if (ack == PusherAck::ACCEPTED) {
            downAcked_ = true;
          } else if (ack != PusherAck::UNSUPPORTED) {
            needUp_ = false;
            finish(PressResult::NOT_SENT, nowMs);
            break;
          }
          phase_ = Phase::HOLD;
          phaseSinceMs_ = nowMs;
        } else if (!ready) {
          finish(PressResult::UNCONFIRMED, nowMs);  // needUp_ stays set
          phase_ = Phase::IDLE;
        } else if (static_cast<uint32_t>(nowMs - phaseSinceMs_) >= STEP_ACK_TIMEOUT_MS) {
          phase_ = Phase::HOLD;
          phaseSinceMs_ = nowMs;
        }
        break;

      case Phase::HOLD:
        if (holdMs_ == HOLD_UNTIL_RELEASED) {
          // Held for a shot. The link going down leaves the arm where it is:
          // say so, and bring it up as soon as the pusher is back.
          if (!ready) {
            finish(PressResult::UNCONFIRMED, nowMs);
            phase_ = Phase::IDLE;
          }
        } else if (static_cast<uint32_t>(nowMs - phaseSinceMs_) >= holdMs_) {
          lastUpTryMs_ = nowMs;
          if (ready && sendAction(PusherOp::UP)) {
            needUp_ = false;
            upFirstMs_ = nowMs;
            upRetrying_ = false;
            phase_ = Phase::WAIT_UP_ACK;
            phaseSinceMs_ = nowMs;
          } else {
            finish(PressResult::UNCONFIRMED, nowMs);
            phase_ = Phase::IDLE;  // the needUp_ path above keeps retrying
          }
        }
        break;

      case Phase::WAIT_UP_ACK:
        // The up must be done, not just sent: a SwitchBot still moving its
        // arm down answers busy and ignores the up, and the arm then stays on
        // the button (measured on the board: up sent 0.43 s after the down,
        // status 0x03, button held). So a refused up is asked for again, and
        // an up not known to be done is sent once more after settling.
        if (ackPending_.exchange(false)) {
          const PusherAck ack = ack_;
          if (ack == PusherAck::REFUSED &&
              static_cast<uint32_t>(nowMs - upFirstMs_) < UP_GIVE_UP_MS) {
            upRetrying_ = true;
            upRetryAtMs_ = nowMs + UP_RETRY_MS;
          } else {
            if (ack == PusherAck::REFUSED) needUp_ = true;
            finish(downAcked_ && ack == PusherAck::ACCEPTED ? PressResult::OK
                                                            : PressResult::UNCONFIRMED, nowMs);
          }
        } else if (upRetrying_) {
          if (static_cast<int32_t>(nowMs - upRetryAtMs_) >= 0) {
            upRetrying_ = false;
            lastUpTryMs_ = nowMs;
            if (ready && sendAction(PusherOp::UP)) {
              phaseSinceMs_ = nowMs;
            } else {
              needUp_ = true;
              finish(PressResult::UNCONFIRMED, nowMs);
            }
          }
        } else if (!ready ||
                   static_cast<uint32_t>(nowMs - phaseSinceMs_) >= STEP_ACK_TIMEOUT_MS) {
          needUp_ = true;
          finish(PressResult::UNCONFIRMED, nowMs);
        }
        break;

      case Phase::SETTLE:
        if (static_cast<int32_t>(nowMs - settleUntilMs_) >= 0) phase_ = Phase::IDLE;
        break;
    }
  }

 private:
  enum class Phase : uint8_t { IDLE, WAIT_ACK, WAIT_DOWN_ACK, HOLD, WAIT_UP_ACK, SETTLE };

  uint32_t upFirstMs_ = 0;    // the first up of this press
  uint32_t upRetryAtMs_ = 0;  // when a refused up is sent again
  bool upRetrying_ = false;
  bool plainPressOnly_ = false;

  void onPusherAck(PusherAck ack, uint8_t raw) override {
    (void)raw;
    ack_ = ack;
    ackPending_ = true;
  }
  void onPusherStatus(uint8_t raw) override { lastStatus_ = raw; }
  void onPusherInfo(const PusherInfo &info) override { info_ = info; }

  bool sendAction(PusherOp op) {
    // An acknowledgement still waiting belongs to an earlier command.
    const bool before = ackPending_.exchange(false);
    const bool ok = driver()->send(op, *writer_);
    if (!ok) ackPending_ = before;
    logf("%s: action %u %s", name_, static_cast<unsigned>(op), ok ? "sent" : "NOT SENT");
    return ok;
  }

  void finish(PressResult result, uint32_t nowMs) {
    result_ = result;
    phase_ = Phase::SETTLE;
    uint32_t settle = settings_->releaseSettleMs;
    if (atomic_) settle += static_cast<uint32_t>(settings_->botHoldSeconds) * 1000u;
    settleUntilMs_ = nowMs + settle;
    logf("%s: press result %d (status 0x%02x)", name_, static_cast<int>(result),
         static_cast<unsigned>(lastStatus_.load()));
  }

  void logf(const char *format, ...) {
    if (!log_) return;
    char line[112];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log_(line);
  }

  SwitchBotDriver switchBot_;
  TuyaFingerbotDriver tuya_;
  MicroBotDriver microBot_;
  PusherDriver *drivers_[3] = {&switchBot_, &tuya_, &microBot_};

  const char *name_ = "bot";
  const Settings *settings_ = nullptr;
  PusherWriter *writer_ = nullptr;
  PusherSecrets secrets_;
  uint32_t (*random_)() = nullptr;
  uint32_t (*epoch_)() = nullptr;
  void (*log_)(const char *) = nullptr;

  std::atomic<PusherDriver *> driver_{nullptr};
  std::atomic<bool> startPending_{false};
  std::atomic<bool> started_{false};
  bool sessionFailed_ = false;
  uint32_t sessionSinceMs_ = 0;

  Phase phase_ = Phase::IDLE;
  PressResult result_ = PressResult::IDLE;
  uint32_t sentAtMs_ = 0;
  uint32_t phaseSinceMs_ = 0;
  uint32_t settleUntilMs_ = 0;
  bool downAcked_ = false;
  bool atomic_ = true;           // current press is the pusher's own push-and-return
  uint32_t holdMs_ = 0;          // down time of the current down/up press
  bool needUp_ = false;          // arm was commanded down and not yet back up
  uint32_t lastUpTryMs_ = 0;

  std::atomic<bool> ackPending_{false};
  PusherAck ack_ = PusherAck::REFUSED;
  std::atomic<uint8_t> lastStatus_{0};
  PusherInfo info_;
  bool infoRequested_ = false;
  uint32_t lastInfoMs_ = 0;
  bool holdConfigured_ = false;
};

}  // namespace bbw
