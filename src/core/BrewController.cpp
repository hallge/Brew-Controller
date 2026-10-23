#include "BrewController.h"

#include "FlowFeatures.h"

#include <string.h>

namespace bbw {

namespace {

constexpr uint32_t TARE_SETTLE_DELAY_MS = 250;
constexpr uint32_t TARE_RETRY_MS = 2000;
constexpr uint8_t TARE_MAX_ATTEMPTS = 3;
constexpr uint8_t TARE_ZERO_SAMPLES = 3;
// Cup placement stability: Open Brew's retare defaults.
constexpr uint8_t CUP_STABLE_SAMPLES = 3;
constexpr float CUP_STABLE_TOLERANCE_G = 2.0f;
constexpr uint32_t CUP_STABLE_MIN_MS = 300;
constexpr uint32_t PREPARATION_TIMEOUT_MS = 120000;
constexpr uint32_t START_ACK_TIMEOUT_MS = 8000;
// The Bot link is only brought up for a shot; this is how long Start waits for it.
// A SwitchBot Bot that is not connected advertises only every 10-15 s
// (measured on the bench), and it can only be connected to when it does.
constexpr uint32_t BOT_CONNECT_TIMEOUT_MS = 40000;
constexpr uint32_t MANUAL_WARN_MS = 1500;
constexpr uint32_t STOP_GIVE_UP_MS = 30000;
constexpr uint32_t UNCONFIRMED_DECIDE_MS = 1500;
constexpr uint32_t UNCONFIRMED_ACCEPT_MS = 4000;
// A stop is only pressed again when the pump is plainly still running. On the
// Breville, measured on the board: full flow goes on for about 1.7 s after
// the Bot has pressed, then tapers off over another second. A second press
// on that tail would start the pump again.
constexpr uint32_t STOP_RETRY_AFTER_MS = 4000;
// Stop-offset warm-up: the first shots of a recipe and coffee learn this
// share of the error (hundredths), then the usual factor takes over.
constexpr uint8_t WARMUP_SHOTS = 4;
constexpr uint8_t WARMUP_ALPHA = 70;
constexpr float STOP_RETRY_FLOW_SHARE = 0.5f;  // still at least half the flow of the moment of the stop
constexpr float STOP_RETRY_MIN_GAIN_G = 3.0f;
constexpr uint8_t MAX_STOP_RETRIES = 2;
constexpr uint32_t DRIP_EXTRA_WAIT_MS = 3000;
constexpr uint8_t LOW_WEIGHT_CONFIRM_SAMPLES = 2;
constexpr uint8_t MAX_REJECTED_SAMPLES = 5;
constexpr float RUNNING_MIN_WEIGHT_G = 1.0f;
constexpr float NO_FLOW_BAND_G = 1.0f;
constexpr uint32_t RECOVERY_WINDOW_MS = 45000;
// Coffee was flowing and has now been still this long: the machine has ended
// the shot itself (or the puck has choked). Either way a press now would not
// stop anything, and on a machine that has stopped it would start it again.
constexpr uint32_t MACHINE_STOPPED_MS = 5000;
// Bloom: how long to wait for the Bot to take a press before giving up, and
// how long after the pump is switched off the cup may still be filling from
// what was already on its way.
constexpr uint32_t BLOOM_PRESS_PATIENCE_MS = 5000;
constexpr uint32_t BLOOM_DRIP_GRACE_MS = 3000;
// Cup lifted off the scale: the reading falls by at least this much below the
// last good one and stays there. Coffee landing in a cup never does that.
constexpr float CUP_DROP_G = 10.0f;
constexpr uint8_t CUP_GONE_CONFIRM_SAMPLES = 3;
// If less than this arrived in the last second before the cup went, the pour
// had already stopped (the machine's time-out, or its button pressed by
// hand). Pressing then would start the pump again, with no cup under it.
constexpr float STILL_POURING_G_PER_S = 0.5f;
// Readings this far under the most the cup has held are the cup leaving, not
// the pour; they are kept out of the "was it still pouring" figure.
constexpr float FALLING_BAND_G = 2.0f;
// Idle cup tracking.
constexpr float CUP_STILL_BAND_G = 0.5f;      // readings this close together are "still"
constexpr uint8_t CUP_STILL_SAMPLES = 5;
constexpr float CUP_ZERO_BAND_G = 1.5f;
constexpr uint32_t CUP_TARE_WINDOW_MS = 6000; // a fall to zero this soon after placing is the tare
constexpr uint32_t EMPTY_ZERO_DELAY_MS = 5000; // an empty scale still below zero this long is zeroed
// A remembered cup is believed again only if the scale is back this soon after its last reading (a
// dropped link, a controller restart). After longer the scale may have been off: a Lunar that comes
// on empty zeroes itself and still shows the timer it had, which looks just like a tared cup.
constexpr uint32_t CUP_MEMORY_MAX_GAP_MS = 120000;

inline uint32_t nonZero(uint32_t ms) { return ms == 0 ? 1 : ms; }

}  // namespace

uint8_t BrewController::activePreset() const {
  return library_.activePreset < library_.presetCount ? library_.activePreset : 0;
}

Recipe BrewController::recipe() const { return resolvePreset(library_, activePreset()); }

void BrewController::enter(State next, uint32_t nowMs) {
  state_ = next;
  stateSinceMs_ = nowMs;
}

bool BrewController::weightFresh(uint32_t nowMs) const {
  // Signed: a reading stamped just after nowMs was read is fresh, not ancient.
  return haveSample_ &&
         static_cast<int32_t>(nowMs - lastRxMs_) <= static_cast<int32_t>(settings_.scaleTimeoutMs);
}

bool BrewController::scaleUsable(uint32_t nowMs) const {
  return scale_.connected() && weightFresh(nowMs);
}

State BrewController::scaleFaultState(uint32_t nowMs) const {
  (void)nowMs;
  return scale_.connected() ? State::SCALE_DATA_STALE : State::SCALE_DISCONNECTED;
}

float BrewController::shotSeconds(uint32_t nowMs) const {
  return static_cast<uint32_t>(nowMs - shotStartMs_) / 1000.0f;
}

bool BrewController::machineMayBeRunning() const {
  switch (state_) {
    case State::STARTING_SHOT:
    case State::BREWING:
    case State::BLOOMING:
    case State::STOPPING_SHOT:
      return true;
    case State::POST_SHOT_DRIP:
      return !stopConfirmed_;
    default:
      return manualStopNeeded_;
  }
}

bool BrewController::idleForPower() const {
  if (machineMayBeRunning()) return false;
  switch (state_) {
    case State::TARING:
    case State::WAITING_FOR_CUP:
    case State::READY_TO_BREW:
    case State::STARTING_SHOT:
    case State::BREWING:
    case State::BLOOMING:
    case State::STOPPING_SHOT:
    case State::POST_SHOT_DRIP:
      return false;
    default:
      return true;
  }
}

bool BrewController::actuatorNeeded() const {
  // A machine worked by hand has no bot on its brew control.
  if (!machine().controllerStarts()) return false;
  switch (state_) {
    case State::TARING:
    case State::WAITING_FOR_CUP:
    case State::READY_TO_BREW:
    case State::STARTING_SHOT:
    case State::BREWING:
    case State::BLOOMING:
    case State::STOPPING_SHOT:
    case State::POST_SHOT_DRIP:
    case State::START_COMMAND_FAILED:  // late flow may still need a stop press
    case State::STOP_COMMAND_FAILED:
      return true;
    default:
      return manualStopNeeded_ || manualPressInFlight_ || recoveryUntilMs_ != 0;
  }
}

// ---------------------------------------------------------------- intents --

void BrewController::requestStart() { intent_ = Intent::START; }
void BrewController::requestStop() { intent_ = Intent::STOP; }
void BrewController::requestTare() { intent_ = Intent::TARE; }
void BrewController::acknowledge() { intent_ = Intent::ACK; }
void BrewController::requestManualPress() { intent_ = Intent::MANUAL_PRESS; }
void BrewController::raiseBleError() { bleErrorPending_ = true; }

void BrewController::armRestartRecovery(uint32_t nowMs) {
  recoveryUntilMs_ = nonZero(nowMs + RECOVERY_WINDOW_MS);
}

bool BrewController::takeCompletedShot(ShotRecord &out) {
  if (!completedPending_) return false;
  completedPending_ = false;
  out = lastShot_;
  return true;
}

bool BrewController::takeSettingsDirty() {
  const bool dirty = settingsDirty_;
  settingsDirty_ = false;
  return dirty;
}

// ---------------------------------------------------------------- samples --

void BrewController::trackFlow(const WeightSample &sample) {
  const float flow = flow_.flowGS();
  if (flow >= FLOW_ON_G_S) {
    if (flowHighSinceMs_ == 0) flowHighSinceMs_ = nonZero(sample.atMs);
  } else {
    flowHighSinceMs_ = 0;
  }
  if (flow < FLOW_OFF_G_S) {
    if (flowLowSinceMs_ == 0) flowLowSinceMs_ = nonZero(sample.atMs);
  } else {
    flowLowSinceMs_ = 0;
  }
}

void BrewController::onSample(const WeightSample &sample) {
  if (haveSample_) {
    const uint32_t period = sample.atMs - lastRxMs_;
    samplePeriodMs_ = period > 65535 ? 65535 : static_cast<uint16_t>(period);
  }
  if (state_ == State::BREWING || state_ == State::BLOOMING) {
    acceptBrewSample(sample);
    return;
  }
  lastRxMs_ = sample.atMs;
  if (!std::isfinite(sample.weightG)) return;
  last_ = sample;
  haveSample_ = true;
  flow_.add(sample.weightG, sample.atMs);
  trackFlow(sample);
  // The cup is followed whenever no shot is in hand: on the home screen, and
  // also while a result or a fault is still showing, so that swapping the cup
  // there is known by the time the home screen is back.
  if (state_ == State::READY || state_ == State::SHOT_COMPLETE ||
      (isFault(state_) && !machineMayBeRunning())) {
    trackCup(sample);
  }
  // The data set of a shot runs on past the stop: the drip, and the weight
  // the cup settles at, belong to it.
  if ((state_ == State::STOPPING_SHOT || state_ == State::POST_SHOT_DRIP) &&
      static_cast<int32_t>(sample.atMs - shotStartMs_) >= 0) {
    recordSample(sample);
  }

  if (state_ == State::TARING) {
    if (static_cast<uint32_t>(sample.atMs - tareSentAtMs_) >= TARE_SETTLE_DELAY_MS &&
        static_cast<int32_t>(sample.atMs - tareSentAtMs_) > 0) {
      zeroSamples_ = fabsf(sample.weightG) <= TARE_SETTLE_BAND_G ? zeroSamples_ + 1 : 0;
    }
  } else if (state_ == State::WAITING_FOR_CUP) {
    if (sample.weightG >= settings_.cupMinWeightG) {
      if (cupStableSamples_ == 0) {
        cupMinG_ = cupMaxG_ = sample.weightG;
        cupStableSinceMs_ = sample.atMs;
      }
      cupMinG_ = fminf(cupMinG_, sample.weightG);
      cupMaxG_ = fmaxf(cupMaxG_, sample.weightG);
      if (cupMaxG_ - cupMinG_ > CUP_STABLE_TOLERANCE_G) {
        cupStableSamples_ = 1;
        cupMinG_ = cupMaxG_ = sample.weightG;
        cupStableSinceMs_ = sample.atMs;
      } else if (cupStableSamples_ < 255) {
        ++cupStableSamples_;
      }
    } else {
      cupStableSamples_ = 0;
    }
  }
}

void BrewController::trackCup(const WeightSample &sample) {
  // The furthest the reading went up and down since it was last still. A
  // scale with auto-tare (the Lunar's espresso modes) zeroes itself within a
  // second or two of a cup going on or coming off, so the reading may never
  // be still anywhere but 0: the jump is what shows the cup.
  if (haveStable_) {
    if (sample.weightG > excursionMaxG_) { excursionMaxG_ = sample.weightG; excursionMaxAtMs_ = sample.atMs; }
    if (sample.weightG < excursionMinG_) { excursionMinG_ = sample.weightG; excursionMinAtMs_ = sample.atMs; }
  }
  if (runCount_ == 0 || fabsf(sample.weightG - runRefG_) > CUP_STILL_BAND_G) {
    runRefG_ = sample.weightG;
    runCount_ = 1;
    runSinceMs_ = sample.atMs;
    emptyZeroSent_ = false;
    return;
  }
  if (runCount_ < 255) ++runCount_;
  const float cupG = settings_.cupMinWeightG;
  // Nothing on the scale and it reads a cup's weight or more below zero, still
  // for a while: a tared cup was taken off and the scale does not zero itself.
  // Zero it, once for this reading, so that it reads 0 g again.
  if (runCount_ >= CUP_STILL_SAMPLES && !emptyZeroSent_ && !cupPresent_ &&
      (state_ == State::READY || state_ == State::SHOT_COMPLETE) && runRefG_ <= -cupG &&
      static_cast<uint32_t>(sample.atMs - runSinceMs_) >= EMPTY_ZERO_DELAY_MS) {
    emptyZeroSent_ = true;
    haveEmptyZero_ = true;
    emptyZeroAtMs_ = sample.atMs;
    tareWhy_ = "the empty scale read below zero";
    scale_.tare();
  }
  if (runCount_ != CUP_STILL_SAMPLES) return;  // act once, when a reading has just become still

  const float level = sample.weightG;
  auto placed = [&]() {
    cupPresent_ = true;
    cupPlacedAtMs_ = sample.atMs;
    // Zero it, unless the scale's own auto-tare already has.
    if (fabsf(level) > CUP_ZERO_BAND_G) {
      tareWhy_ = "a cup went on";
      scale_.tare();
    }
  };
  auto remember = [&]() {
    cupMemory_.present = cupPresent_;
    cupMemory_.levelG = level;
    cupMemory_.scaleTimerMs = sample.scaleTimerMs;
  };
  if (!haveStable_) {
    haveStable_ = true;
    stableLevelG_ = level;
    if (cupInitPending_ && level >= cupG) {
      // Scale just connected with a cup's weight showing: a cup is on it.
      placed();
    } else if (cupInitPending_ && cupMemory_.present && cupMemory_.scaleTimerMs > 0 &&
               sample.scaleTimerMs == cupMemory_.scaleTimerMs &&
               fabsf(level - cupMemory_.levelG) <= CUP_STILL_BAND_G) {
      // The cup that was there before the controller restarted (or the link
      // dropped): same reading, and the scale's timer has not moved, so the
      // scale was not switched off and on, and nothing changed on it.
      cupPresent_ = true;
      cupPlacedAtMs_ = sample.atMs - CUP_TARE_WINDOW_MS - 1;  // not a fresh placement
    }
    cupInitPending_ = false;
    excursionMaxG_ = excursionMinG_ = level;
    remember();
    return;
  }
  const float base = stableLevelG_;
  const float step = level - base;
  const float rise = excursionMaxG_ - base;
  const float fall = base - excursionMinG_;
  const bool riseLast = excursionMaxAtMs_ >= excursionMinAtMs_;
  stableLevelG_ = level;
  excursionMaxG_ = excursionMinG_ = level;
  const bool nearZero = fabsf(level) <= CUP_ZERO_BAND_G;
  const auto sincePlaced = static_cast<uint32_t>(sample.atMs - cupPlacedAtMs_);
  const auto sinceRemoved = static_cast<uint32_t>(sample.atMs - cupRemovedAtMs_);
  auto removed = [&]() {
    cupPresent_ = false;
    cupRemovedAtMs_ = sample.atMs;
  };
  if (step >= cupG) {
    // Settled higher by a cup's weight. Straight after a cup came off, a
    // climb back to 0 is the scale zeroing itself, not a cup.
    // Nor is one straight after the controller zeroed the empty scale itself.
    const bool emptyZero = haveEmptyZero_ && static_cast<uint32_t>(sample.atMs - emptyZeroAtMs_) <= CUP_TARE_WINDOW_MS;
    if (!(nearZero && ((haveRemoved_ && sinceRemoved <= CUP_TARE_WINDOW_MS) || emptyZero))) placed();
  } else if (step <= -cupG) {
    // Settled lower. Straight after a cup went on, a fall to 0 is its tare
    // (ours, or the scale's own).
    const bool tare = cupPresent_ && nearZero && sincePlaced <= CUP_TARE_WINDOW_MS;
    if (!tare) removed();
  } else if (nearZero && rise >= cupG && (fall < cupG || riseLast)) {
    // Back at 0 after a jump up: a cup went on and the scale tared it by
    // itself. Only at 0: a scale that zeroes itself comes back to 0, and a
    // jump that comes back anywhere else is a cup put down for a moment and
    // lifted again (seen on the first real shot: back at -268 g, read as a
    // cup placed, and an empty scale was tared as if it held one).
    placed();
    scaleZeroesItself_ = true;
  } else if (nearZero && fall >= cupG) {
    // Back at 0 after a drop: a cup came off and the scale zeroed itself.
    removed();
    scaleZeroesItself_ = true;
  }
  if (cupRemovedAtMs_ == sample.atMs) haveRemoved_ = true;
  remember();
}

void BrewController::acceptBrewSample(const WeightSample &sample) {
  lastRxMs_ = sample.atMs;
  // Plausibility gates from Open Brew (range and slew). A rejected reading
  // keeps the link "alive" but never reaches the stop logic.
  if (!std::isfinite(sample.weightG) ||
      fabsf(sample.weightG) > MAX_AUTOMATION_WEIGHT_G) {
    ++rejectedSamples_;
    return;
  }
  // Cup lifted: judged against the most the cup has held, before the slew
  // gate below throws the reading away. A scale smooths its readings, so a
  // lift arrives as a slide over several of them; each step can be small
  // enough to pass the gate, which is why the peak is the reference.
  if (haveSample_ && sample.weightG < peakWeightG_ - CUP_DROP_G) {
    if (cupGoneConfirm_ < 255) ++cupGoneConfirm_;
  } else {
    cupGoneConfirm_ = 0;
  }
  if (haveSample_) {
    const float dtS = static_cast<uint32_t>(sample.atMs - last_.atMs) / 1000.0f;
    const float allowed = MAX_WEIGHT_SLEW_G_PER_S * dtS + WEIGHT_SLEW_ALLOWANCE_G;
    if (fabsf(sample.weightG - last_.weightG) > allowed) {
      ++rejectedSamples_;
      return;
    }
  }
  rejectedSamples_ = 0;
  last_ = sample;
  haveSample_ = true;
  flow_.add(sample.weightG, sample.atMs);
  trackFlow(sample);

  lowWeightConfirm_ =
      sample.weightG < settings_.minValidWeightG ? lowWeightConfirm_ + 1 : 0;
  // How fast it was pouring, as of the last reading that was still part of
  // the pour. A light cup lifting can pass the slew gate above, and must not
  // be mistaken for "the pour had stopped".
  if (sample.weightG > peakWeightG_) peakWeightG_ = sample.weightG;
  if (sample.weightG >= peakWeightG_ - FALLING_BAND_G) pourRateGS_ = flow_.flowGS();

  if (static_cast<int32_t>(sample.atMs - shotStartMs_) < 0) return;
  const float tS = recordSample(sample);

  if (!firstDropSeen_) {
    firstDropConfirm_ =
        sample.weightG >= FIRST_DROP_THRESHOLD_G ? firstDropConfirm_ + 1 : 0;
    if (firstDropConfirm_ >= 2) {
      firstDropSeen_ = true;
      firstDropS_ = tS;
    }
  }
  const float flow = flow_.flowGS();
  if (firstDropSeen_ && flow > peakFlow_) peakFlow_ = flow;
  if (!runningConfirmed_ && flowHighSinceMs_ != 0 &&
      static_cast<uint32_t>(sample.atMs - flowHighSinceMs_) >= FLOW_HOLD_MS &&
      sample.weightG - brewPhaseStartG_ >= RUNNING_MIN_WEIGHT_G) {
    runningConfirmed_ = true;
  }

  const float cutG = shotRecipe_.targetWeightG - shotRecipe_.stopOffsetG;
  directConfirm_ = sample.weightG >= cutG ? directConfirm_ + 1 : 0;
  const float fallbackS = shotRecipe_.maxShotTimeS;
  expectedEndS_ = predictStopTimeS(curve_.timeS, curve_.weightG, curve_.count,
                                   cutG, fallbackS);
}

// Adds a reading to the shot's data set. Returns its time in the shot.
float BrewController::recordSample(const WeightSample &sample) {
  const float tS = static_cast<uint32_t>(sample.atMs - shotStartMs_) / 1000.0f;
  if (curve_.count == MAX_CURVE_POINTS) {
    memmove(&curve_.timeS[0], &curve_.timeS[1], (MAX_CURVE_POINTS - 1) * sizeof(float));
    memmove(&curve_.weightG[0], &curve_.weightG[1], (MAX_CURVE_POINTS - 1) * sizeof(float));
    memmove(&curve_.timerDs[0], &curve_.timerDs[1], (MAX_CURVE_POINTS - 1) * sizeof(uint16_t));
    memmove(&curve_.flags[0], &curve_.flags[1], MAX_CURVE_POINTS - 1);
    --curve_.count;
  }
  curve_.timeS[curve_.count] = tS;
  curve_.weightG[curve_.count] = sample.weightG;
  curve_.timerDs[curve_.count] = packTimerDs(sample.scaleTimerMs);
  curve_.flags[curve_.count] = sample.flags;
  ++curve_.count;
  return tS;
}

// ------------------------------------------------------------------- tick --

void BrewController::tick(uint32_t nowMs) {
  const Intent intent = intent_;
  intent_ = Intent::NONE;

  // Readings are stamped by the BLE task as they arrive, so one can carry a
  // time a millisecond or two later than the "now" the caller read before
  // handing it over. Every age in here is an unsigned difference: a reading
  // from the future would come out 49 days old and be taken for a scale gone
  // silent. Time never runs behind the newest reading.
  if (haveSample_ && static_cast<int32_t>(nowMs - lastRxMs_) < 0) nowMs = lastRxMs_;
  // When the scale was last heard while in use (not while it is being connected again).
  if (state_ != State::CONNECTING_SCALE && scale_.connected()) lastGoodRxMs_ = lastRxMs_;

  if (recoveryUntilMs_ != 0 && static_cast<int32_t>(nowMs - recoveryUntilMs_) >= 0) {
    recoveryUntilMs_ = 0;
  }

  if (bleErrorPending_) {
    bleErrorPending_ = false;
    if (!machineMayBeRunning() && state_ != State::BLE_ERROR) {
      enter(State::BLE_ERROR, nowMs);
      return;
    }
  }

  // A result or a fault left showing (the machine stopped) while the scale is gone: the scale was let
  // go to rest or switched off, nobody is there. Back to the home screen after a while, as from READY.
  // Not BLE_ERROR, which has its own recovery.
  if ((state_ == State::SHOT_COMPLETE || (isFault(state_) && state_ != State::BLE_ERROR)) &&
      !machineMayBeRunning() && !scale_.connected()) {
    if (scaleGoneSinceMs_ == 0) {
      scaleGoneSinceMs_ = nowMs ? nowMs : 1;
    } else if (static_cast<uint32_t>(nowMs - scaleGoneSinceMs_) >= RESULT_SCALE_GONE_MS) {
      scaleGoneSinceMs_ = 0;
      enter(State::CONNECTING_SCALE, nowMs);
      return;
    }
  } else {
    scaleGoneSinceMs_ = 0;
  }

  switch (state_) {
    case State::BOOT:
      enter(State::INITIALIZING, nowMs);
      break;
    case State::INITIALIZING:
      enter(State::CONNECTING_SCALE, nowMs);
      break;
    case State::CONNECTING_SCALE:
      if (scaleUsable(nowMs)) {
        // Nothing is known about what is on a scale that has just connected.
        if (cupMemory_.present && static_cast<uint32_t>(nowMs - lastGoodRxMs_) > CUP_MEMORY_MAX_GAP_MS) {
          cupMemory_.present = false;  // away too long: the remembered cup proves nothing
        }
        cupPresent_ = false;
        cupInitPending_ = true;
        haveStable_ = false;
        runCount_ = 0;
        enter(State::READY, nowMs);
      }
      break;
    case State::READY:
      if (!scale_.connected()) {
        // The Lunar sleeping while idle is normal, not a fault.
        enter(State::CONNECTING_SCALE, nowMs);
        break;
      }
      if (recoveryUntilMs_ != 0) {
        if (static_cast<int32_t>(nowMs - recoveryUntilMs_) >= 0) {
          recoveryUntilMs_ = 0;
        } else if (weightFresh(nowMs) && flowHighSinceMs_ != 0 &&
                   static_cast<uint32_t>(nowMs - flowHighSinceMs_) >= FLOW_HOLD_MS &&
                   last_.weightG >= RUNNING_MIN_WEIGHT_G && stopper().connected()) {
          recoveryUntilMs_ = 0;
          runningConfirmed_ = true;  // the flow just seen is the evidence
          shotRecipe_ = recipe();
          shotRecipeIndex_ = activePreset();
          shotRecipe_.bloomPauseMs = 0;  // whatever was planned, this is a plain brew now
          shotStartMs_ = brewPhaseStartMs_ = nowMs;
          brewPhaseStartG_ = 0.0f;
          bloomed_ = false;
          curve_.count = 0;
          firstDropSeen_ = false;
          firstDropS_ = 0.0f;
          peakFlow_ = 0.0f;
          faultAfterStop_ = State::SHOT_TIMEOUT;
          beginStop(EndReason::RECOVERED, nowMs);
          break;
        }
      }
      if (intent == Intent::TARE && scaleUsable(nowMs)) {
        tareWhy_ = "asked for";
        scale_.tare();
      } else if (intent == Intent::START) {
        if (!scale_.connected()) {
          enter(State::SCALE_DISCONNECTED, nowMs);
        } else if (!weightFresh(nowMs)) {
          enter(State::SCALE_DATA_STALE, nowMs);
        } else if (settings_.requireCup && !cupPresent_) {
          // No cup has been seen going onto the scale: do not brew onto it.
          // The home screen is already asking for one.
          startRefusedNoCup_ = true;
        } else {
          // The Bot is asleep between shots. It is woken now and has until
          // shortly after the tare to connect (see READY_TO_BREW).
          startRequestedMs_ = nowMs;
          recoveryUntilMs_ = 0;
          cupTared_ = false;
          tareAttempts_ = 0;
          sendTare(nowMs);
          enter(State::TARING, nowMs);
        }
      }
      break;
    case State::TARING:
    case State::WAITING_FOR_CUP:
    case State::READY_TO_BREW:
      tickPreparation(nowMs, intent);
      break;
    case State::STARTING_SHOT:
      tickStarting(nowMs, intent);
      break;
    case State::BREWING:
      tickBrewing(nowMs, intent);
      break;
    case State::BLOOMING:
      tickBlooming(nowMs, intent);
      break;
    case State::STOPPING_SHOT:
      tickStopping(nowMs);
      break;
    case State::POST_SHOT_DRIP:
      tickDrip(nowMs);
      break;
    default:
      tickFault(nowMs, intent);
      break;
  }
}

void BrewController::sendTare(uint32_t nowMs) {
  ++tareAttempts_;
  zeroSamples_ = 0;
  tareSentAtMs_ = nowMs;
  tareWhy_ = "the shot is starting";
  scale_.tare();
}

void BrewController::tickPreparation(uint32_t nowMs, Intent intent) {
  if (intent == Intent::STOP) {
    enter(State::READY, nowMs);
    return;
  }
  if (!scaleUsable(nowMs)) {
    enter(scaleFaultState(nowMs), nowMs);
    return;
  }
  const uint32_t inState = nowMs - stateSinceMs_;

  if (state_ == State::TARING) {
    if (zeroSamples_ >= TARE_ZERO_SAMPLES) {
      cupStableSamples_ = 0;
      enter(State::WAITING_FOR_CUP, nowMs);
    } else if (static_cast<uint32_t>(nowMs - tareSentAtMs_) >= TARE_RETRY_MS) {
      // "Tare command is less reliable than pressing the tare button"
      // (AcaiaArduinoBLE README), hence verify-and-retry.
      if (tareAttempts_ >= TARE_MAX_ATTEMPTS) {
        enter(State::TARE_FAILED, nowMs);
      } else {
        sendTare(nowMs);
      }
    }
    return;
  }

  if (state_ == State::WAITING_FOR_CUP) {
    if (settings_.cupMode == CupMode::CUP_BEFORE_START || cupTared_) {
      enter(State::READY_TO_BREW, nowMs);
    } else if (cupStableSamples_ >= CUP_STABLE_SAMPLES &&
               static_cast<uint32_t>(last_.atMs - cupStableSinceMs_) >= CUP_STABLE_MIN_MS) {
      cupTared_ = true;
      tareAttempts_ = 0;
      sendTare(nowMs);
      enter(State::TARING, nowMs);
    } else if (inState >= PREPARATION_TIMEOUT_MS) {
      enter(State::READY, nowMs);
    }
    return;
  }

  // READY_TO_BREW
  if (machine().controllerStarts() && (!actuator_.connected() || !stopper().connected())) {
    if (static_cast<uint32_t>(nowMs - startRequestedMs_) >= BOT_CONNECT_TIMEOUT_MS) {
      enter(State::SWITCHBOT_DISCONNECTED, nowMs);
    } else if (intent == Intent::START) {
      intent_ = Intent::START;  // keep the tap until the Bot is there
    }
    return;
  }
  if (settings_.autoStartAfterTare || intent == Intent::START) {
    beginShot(nowMs);
  } else if (inState >= PREPARATION_TIMEOUT_MS) {
    enter(State::READY, nowMs);
  }
}

void BrewController::beginShot(uint32_t nowMs) {
  shotRecipeIndex_ = activePreset();
  shotRecipe_ = resolvePreset(library_, shotRecipeIndex_);
  shotStarts_ = machine().controllerStarts();
  shotStops_ = machine().controllerStops();
  cueGiven_ = false;
  awaitingDrip_ = false;
  // Holding the button for pre-infusion, and the bloom built on it, are for a
  // machine whose button does that.
  if (!machine().holdPreinfusion) {
    shotRecipe_.preinfusion = Preinfusion::MACHINE;
    shotRecipe_.bloomPauseMs = 0;
  }
  // With the machine's own time-out in charge, the controller's only limit
  // is the absolute ceiling.
  controllerTimeLimit_ = shotRecipe_.timeLimit == TimeLimit::CONTROLLER;
  if (!controllerTimeLimit_ || shotRecipe_.maxShotTimeS * 1000u > HARD_MAX_SHOT_MS) {
    shotRecipe_.maxShotTimeS = HARD_MAX_SHOT_MS / 1000u;
  }
  curve_.count = 0;
  flow_.reset();
  flowHighSinceMs_ = flowLowSinceMs_ = 0;
  shotStartMs_ = brewPhaseStartMs_ = nowMs;
  brewPhaseStartG_ = 0.0f;
  bloomed_ = false;
  bloomAbortReason_ = EndReason::NONE;
  expectedEndS_ = shotRecipe_.maxShotTimeS;
  directConfirm_ = lowWeightConfirm_ = rejectedSamples_ = 0;
  // This cup is now in use; the next shot needs to see a cup placed again.
  cupPresent_ = false;
  cupMemory_.present = false;  // this cup is in use: after a restart it is not a fresh one
  haveStable_ = false;
  runCount_ = 0;
  cupGoneConfirm_ = 0;
  peakWeightG_ = 0.0f;
  pourRateGS_ = 0.0f;
  firstDropSeen_ = false;
  firstDropConfirm_ = 0;
  firstDropS_ = 0.0f;
  peakFlow_ = 0.0f;
  runningConfirmed_ = false;
  endReason_ = EndReason::NONE;
  manualStopNeeded_ = false;
  shotTimeS_ = 0.0f;

  const bool onDrip = machine().startsOnFirstDrip();
  if (settings_.scaleTimer) {
    scale_.resetTimer();
    if (!onDrip) scale_.startTimer();
  }
  stopWanted_ = false;
  stopUnverified_ = false;
  stopStartedPump_ = false;
  startHoldMs_ = shotRecipe_.preinfusion == Preinfusion::HOLD ? shotRecipe_.preinfusionHoldMs : 0;
  if (!shotStarts_) {
    // Worked by hand: nothing is pressed. The clock runs from this tap, or
    // waits for the first coffee on the scale.
    startHoldMs_ = 0;
    if (onDrip) {
      awaitingDrip_ = true;
      dripConfirm_ = 0;
      dripSampleMs_ = haveSample_ ? last_.atMs : 0;
      enter(State::STARTING_SHOT, nowMs);
    } else {
      enter(State::BREWING, nowMs);
    }
    return;
  }
  actuator_.clearResult();
  // A button that brews while it is held: down, and stay down.
  if (!(machine().holdsForShot() ? actuator_.beginHold() : actuator_.beginPress(startHoldMs_))) {
    enter(State::START_COMMAND_FAILED, nowMs);
    return;
  }
  enter(State::STARTING_SHOT, nowMs);
}

void BrewController::tickStarting(uint32_t nowMs, Intent intent) {
  // The press (and a pre-infusion hold) cannot be interrupted half way; a
  // STOP during it is carried out as soon as the button has been released.
  if (awaitingDrip_) {
    // Armed, the machine in the owner's hand: the shot begins with the first
    // coffee to land, at the time of the reading that showed it.
    if (intent == Intent::STOP) {
      awaitingDrip_ = false;
      enter(State::READY, nowMs);
    } else if (!scaleUsable(nowMs)) {
      awaitingDrip_ = false;
      enter(scaleFaultState(nowMs), nowMs);
    } else {
      if (haveSample_ && last_.atMs != dripSampleMs_) {
        dripSampleMs_ = last_.atMs;
        if (last_.weightG >= FIRST_DROP_THRESHOLD_G) {
          if (dripConfirm_++ == 0) dripAtMs_ = last_.atMs;
        } else {
          dripConfirm_ = 0;
        }
      }
      if (dripConfirm_ >= 2) {
        awaitingDrip_ = false;
        shotStartMs_ = brewPhaseStartMs_ = dripAtMs_;
        if (settings_.scaleTimer) scale_.startTimer();
        enter(State::BREWING, nowMs);
      } else if (static_cast<uint32_t>(nowMs - stateSinceMs_) >= PREPARATION_TIMEOUT_MS) {
        awaitingDrip_ = false;
        enter(State::READY, nowMs);
      }
    }
    return;
  }
  if (intent == Intent::STOP || intent == Intent::START) stopWanted_ = true;
  if (machine().holdsForShot()) {
    // The shot runs from the moment the arm is down. Until then the result
    // stays BUSY; anything else means the arm never got there.
    if (actuator_.holding()) {
      shotStartMs_ = brewPhaseStartMs_ = actuator_.pressSentAtMs();
      enter(State::BREWING, nowMs);
      if (stopWanted_) {
        stopWanted_ = false;
        faultAfterStop_ = State::SHOT_COMPLETE;
        stopOrEnd(EndReason::USER, nowMs);
      }
    } else if (actuator_.result() != PressResult::BUSY) {
      actuator_.clearResult();
      enter(State::START_COMMAND_FAILED, nowMs);
    } else if (static_cast<uint32_t>(nowMs - stateSinceMs_) >= START_ACK_TIMEOUT_MS) {
      actuator_.release();  // whatever it is doing, the button must not stay down
      enter(State::START_COMMAND_FAILED, nowMs);
    }
    return;
  }
  switch (actuator_.result()) {
    case PressResult::OK:
    case PressResult::UNCONFIRMED:
      // UNCONFIRMED: the arm may well have pressed. Supervise as a running
      // shot; the no-flow timeout covers the case where it did not.
      shotStartMs_ = brewPhaseStartMs_ = actuator_.pressSentAtMs();
      actuator_.clearResult();
      if (shotRecipe_.bloomPauseMs != 0 && !stopWanted_) {
        // The hold is over and the pump is at full pressure: switch it off.
        bloomPhase_ = BloomPhase::STOP_PRESS;
        bloomPhaseSinceMs_ = nowMs;
        enter(State::BLOOMING, nowMs);
        tickBlooming(nowMs, Intent::NONE);  // press in this very tick if the Bot is ready
        break;
      }
      enter(State::BREWING, nowMs);
      if (stopWanted_) {
        stopWanted_ = false;
        faultAfterStop_ = State::SHOT_COMPLETE;
        stopOrEnd(EndReason::USER, nowMs);
      }
      break;
    case PressResult::NOT_SENT:
      actuator_.clearResult();
      enter(State::START_COMMAND_FAILED, nowMs);
      break;
    default:
      if (static_cast<uint32_t>(nowMs - stateSinceMs_) >= START_ACK_TIMEOUT_MS + startHoldMs_) {
        enter(State::START_COMMAND_FAILED, nowMs);
      }
      break;
  }
}

void BrewController::tickBrewing(uint32_t nowMs, Intent intent) {
  const uint32_t elapsedMs = nowMs - shotStartMs_;
  uint32_t maxMs = static_cast<uint32_t>(shotRecipe_.maxShotTimeS) * 1000u;
  if (maxMs == 0 || maxMs > HARD_MAX_SHOT_MS) maxMs = HARD_MAX_SHOT_MS;

  faultAfterStop_ = State::SHOT_COMPLETE;
  // Where the bot does not stop this machine, reaching the target is a cue to
  // the owner (and a sound from the scale, if it has one), given once. The
  // shot then ends when the pour does.
  if (!shotStops_ && !cueGiven_ && elapsedMs >= settings_.minShotTimeMs &&
      (directConfirm_ >= DIRECT_STOP_CONFIRMATION_SAMPLES ||
       (expectedEndS_ < shotRecipe_.maxShotTimeS && shotSeconds(nowMs) >= expectedEndS_))) {
    cueGiven_ = true;
    scale_.beep();
  }
  if (machine().holdsForShot() && actuator_.result() != PressResult::BUSY) {
    // The bot holding the button has gone quiet. Its arm may still be down:
    // the stop sequence lets go as soon as the bot can be reached again.
    faultAfterStop_ = State::STOP_COMMAND_FAILED;
    beginStop(EndReason::BOT_LOST, nowMs);
  } else if (intent == Intent::STOP || intent == Intent::START) {
    stopOrEnd(EndReason::USER, nowMs);
  } else if (!scale_.connected()) {
    faultAfterStop_ = State::SCALE_DISCONNECTED;
    stopOrEnd(EndReason::SCALE_LOST, nowMs);
  } else if (!weightFresh(nowMs)) {
    faultAfterStop_ = State::SCALE_DATA_STALE;
    stopOrEnd(EndReason::SCALE_LOST, nowMs);
  } else if (cupGoneConfirm_ >= CUP_GONE_CONFIRM_SAMPLES ||
             lowWeightConfirm_ >= LOW_WEIGHT_CONFIRM_SAMPLES) {
    // The cup has been lifted off the scale.
    if (runningConfirmed_ && pourRateGS_ < STILL_POURING_G_PER_S) {
      // Coffee had been flowing and had already stopped before the cup went:
      // the shot was ended at the machine. Nothing to press.
      endAtMachine(nowMs);
    } else {
      faultAfterStop_ = State::INVALID_WEIGHT;
      stopOrEnd(EndReason::CUP_REMOVED, nowMs);
    }
  } else if (rejectedSamples_ >= MAX_REJECTED_SAMPLES) {
    faultAfterStop_ = State::INVALID_WEIGHT;
    stopOrEnd(EndReason::INVALID_WEIGHT, nowMs);
  } else if (shotStops_ && elapsedMs >= settings_.minShotTimeMs &&
             directConfirm_ >= DIRECT_STOP_CONFIRMATION_SAMPLES) {
    beginStop(EndReason::WEIGHT, nowMs);
  } else if (shotStops_ && elapsedMs >= settings_.minShotTimeMs &&
             expectedEndS_ < shotRecipe_.maxShotTimeS &&
             shotSeconds(nowMs) >= expectedEndS_) {
    beginStop(EndReason::WEIGHT_PREDICTED, nowMs);
  } else if (runningConfirmed_ && flowLowSinceMs_ != 0 &&
             static_cast<uint32_t>(nowMs - flowLowSinceMs_) >= MACHINE_STOPPED_MS) {
    // The pour has stopped short of the target with nothing pressed.
    endAtMachine(nowMs);
  } else if (elapsedMs >= maxMs) {
    const bool pouring = flowHighSinceMs_ != 0;
    if (controllerTimeLimit_ || pouring) {
      // The recipe asks for a stop at this time, or the absolute ceiling has
      // been reached with coffee still pouring: the machine is running.
      faultAfterStop_ = State::SHOT_TIMEOUT;
      stopOrEnd(EndReason::TIMEOUT, nowMs);
    } else {
      endAtMachine(nowMs);
    }
  } else if (!runningConfirmed_ &&
             static_cast<uint32_t>(nowMs - brewPhaseStartMs_) >=
                 static_cast<uint32_t>(settings_.noFlowTimeoutS) * 1000u &&
             fabsf(last_.weightG - brewPhaseStartG_) <= NO_FLOW_BAND_G) {
    // Nothing reached the cup. Like Open Brew's momentary inference, never
    // pulse a start that was not confirmed: a press now could start the pump.
    endReason_ = EndReason::NO_FLOW;
    if (settings_.scaleTimer) scale_.stopTimer();
    // Worked by hand and nothing came: the lever was not pulled. Not a fault.
    enter(shotStarts_ ? State::START_COMMAND_FAILED : State::READY, nowMs);
  }
}

void BrewController::stopOrEnd(EndReason reason, uint32_t nowMs) {
  if (shotStops_) {
    beginStop(reason, nowMs);
  } else {
    endWithoutPress(nowMs, reason, faultAfterStop_);
  }
}

void BrewController::endAtMachine(uint32_t nowMs) {
  if (shotStops_) {
    endWithoutPress(nowMs);
  } else {
    // The owner, or the machine by itself, ended it: that is how a shot on
    // this machine ends. After the cue it counts as a cut at the target, so
    // the offset learns how early the cue has to come.
    endWithoutPress(nowMs, cueGiven_ ? EndReason::WEIGHT : EndReason::MACHINE_STOPPED,
                    State::SHOT_COMPLETE);
  }
}

// ------------------------------------------------------------------ bloom --
// Between the pre-infusion hold and the brew: press to switch the pump off,
// wait, press and hold briefly to start it again. One press toggles the
// machine and nothing reports which way it went, so each step is checked
// against what the scale shows, and anything doubtful ends the bloom rather
// than risk a press that would start a pump believed to be off.

void BrewController::enterBrewingAfterBloom(uint32_t nowMs, uint32_t restartMs) {
  bloomed_ = true;
  brewPhaseStartMs_ = restartMs;
  brewPhaseStartG_ = haveSample_ ? last_.weightG : 0.0f;
  // Whatever flowed before the pause says nothing about this start.
  runningConfirmed_ = false;
  flowHighSinceMs_ = flowLowSinceMs_ = 0;
  directConfirm_ = 0;
  enter(State::BREWING, nowMs);
}

void BrewController::tickBlooming(uint32_t nowMs, Intent intent) {
  // What would end the shot here, if anything.
  EndReason abort = EndReason::NONE;
  State abortState = State::SHOT_COMPLETE;
  if (intent == Intent::STOP || intent == Intent::START) {
    abort = EndReason::USER;
  } else if (!scale_.connected()) {
    abort = EndReason::SCALE_LOST;
    abortState = State::SCALE_DISCONNECTED;
  } else if (!weightFresh(nowMs)) {
    abort = EndReason::SCALE_LOST;
    abortState = State::SCALE_DATA_STALE;
  } else if (cupGoneConfirm_ >= CUP_GONE_CONFIRM_SAMPLES ||
             lowWeightConfirm_ >= LOW_WEIGHT_CONFIRM_SAMPLES) {
    abort = EndReason::CUP_REMOVED;
    abortState = State::INVALID_WEIGHT;
  } else if (static_cast<uint32_t>(nowMs - shotStartMs_) >= HARD_MAX_SHOT_MS) {
    abort = EndReason::TIMEOUT;
    abortState = State::SHOT_TIMEOUT;
  }
  if (abort != EndReason::NONE && bloomAbortReason_ == EndReason::NONE) {
    bloomAbortReason_ = abort;
    bloomAbortState_ = abortState;
  }
  const bool aborting = bloomAbortReason_ != EndReason::NONE;
  const uint32_t inPhase = nowMs - bloomPhaseSinceMs_;

  switch (bloomPhase_) {
    case BloomPhase::STOP_PRESS:
      // The pump is running. Whether the bloom goes on or the shot is being
      // ended, the next thing is the same press.
      if (aborting) {
        faultAfterStop_ = bloomAbortState_;
        beginStop(bloomAbortReason_, nowMs);
      } else if (actuator_.beginPress()) {
        bloomPhase_ = BloomPhase::STOP_WAIT;
        bloomPhaseSinceMs_ = nowMs;
      } else if (inPhase >= BLOOM_PRESS_PATIENCE_MS) {
        faultAfterStop_ = State::SHOT_COMPLETE;
        beginStop(EndReason::BLOOM_FAILED, nowMs);  // keeps trying, warns if it cannot
      }
      break;

    case BloomPhase::STOP_WAIT:
      switch (actuator_.result()) {
        case PressResult::OK:
          actuator_.clearResult();
          if (aborting) {
            endWithoutPress(nowMs, bloomAbortReason_, bloomAbortState_);  // it is off already
            break;
          }
          bloomPhase_ = BloomPhase::PAUSE;
          bloomPhaseSinceMs_ = nowMs;
          bloomPauseStartMs_ = nowMs;
          bloomPauseStartG_ = haveSample_ ? last_.weightG : 0.0f;
          flowHighSinceMs_ = 0;
          break;
        case PressResult::NOT_SENT:
          actuator_.clearResult();
          bloomPhase_ = BloomPhase::STOP_PRESS;  // nothing happened: try again
          break;
        case PressResult::BUSY:
          if (inPhase < START_ACK_TIMEOUT_MS) break;
          [[fallthrough]];  // no answer
        default:
          // Pressed or not? The stop logic lets the scale decide, and does not
          // press again unless coffee is plainly still pouring.
          faultAfterStop_ = aborting ? bloomAbortState_ : State::SHOT_COMPLETE;
          beginStop(aborting ? bloomAbortReason_ : EndReason::BLOOM_FAILED, nowMs, true);
          break;
      }
      break;

    case BloomPhase::PAUSE: {
      if (aborting) {
        endWithoutPress(nowMs, bloomAbortReason_, bloomAbortState_);
        break;
      }
      // Espresso still arriving well into the pause: the pump is on. Either
      // the first press never reached the button, so the "off" press started
      // the machine, or the off press missed. Both ways a shot is running:
      // supervise it as one and press nothing more until the target.
      if (inPhase >= BLOOM_DRIP_GRACE_MS && flowHighSinceMs_ != 0 &&
          static_cast<uint32_t>(nowMs - flowHighSinceMs_) >= FLOW_HOLD_MS &&
          last_.weightG - bloomPauseStartG_ >= RUNNING_MIN_WEIGHT_G) {
        bloomed_ = true;
        runningConfirmed_ = true;
        brewPhaseStartMs_ = nowMs;
        enter(State::BREWING, nowMs);
        break;
      }
      if (static_cast<uint32_t>(nowMs - bloomPauseStartMs_) >= shotRecipe_.bloomPauseMs) {
        bloomPhase_ = BloomPhase::RESTART_PRESS;
        bloomPhaseSinceMs_ = nowMs;
      }
      break;
    }

    case BloomPhase::RESTART_PRESS:
      // The pump is off. If the brew cannot be started, there is nothing to stop.
      if (aborting) {
        endWithoutPress(nowMs, bloomAbortReason_, bloomAbortState_);
      } else if (actuator_.beginPress(BLOOM_RESTART_HOLD_MS)) {
        startHoldMs_ = BLOOM_RESTART_HOLD_MS;
        bloomPhase_ = BloomPhase::RESTART_WAIT;
        bloomPhaseSinceMs_ = nowMs;
      } else if (inPhase >= BLOOM_PRESS_PATIENCE_MS) {
        endWithoutPress(nowMs, EndReason::BLOOM_FAILED, State::START_COMMAND_FAILED);
      }
      break;

    case BloomPhase::RESTART_WAIT: {
      const PressResult result = actuator_.result();
      if (result == PressResult::NOT_SENT) {
        actuator_.clearResult();
        bloomPhase_ = BloomPhase::RESTART_PRESS;  // still off: try again
        break;
      }
      if (result == PressResult::BUSY && inPhase < START_ACK_TIMEOUT_MS + BLOOM_RESTART_HOLD_MS) {
        break;  // a hold cannot be interrupted
      }
      // Done, or unconfirmed: as with the first start, supervise it as a
      // running shot; the no-flow timeout covers a press that did not happen.
      const uint32_t sentMs = actuator_.pressSentAtMs();
      actuator_.clearResult();
      enterBrewingAfterBloom(nowMs, sentMs);
      if (aborting) {
        faultAfterStop_ = bloomAbortState_;
        beginStop(bloomAbortReason_, nowMs);
      }
      break;
    }
  }
}

// The shot is over with the pump already off: record it, press nothing.
void BrewController::endWithoutPress(uint32_t nowMs, EndReason reason, State next) {
  endReason_ = reason;
  stopRetries_ = 0;
  retryInFlight_ = false;
  stopUnverified_ = false;
  stopStartedPump_ = false;
  manualStopNeeded_ = false;
  stopConfirmed_ = true;
  stopLatencyMs_ = 0;
  // The most the cup held: the latest reading may already be the cup leaving.
  stopWeightG_ = peakWeightG_;
  // It ended when the flow did, not when that was noticed.
  const uint32_t endedMs = flowLowSinceMs_ != 0 ? flowLowSinceMs_ : nowMs;
  shotTimeS_ = static_cast<uint32_t>(endedMs - shotStartMs_) / 1000.0f;
  if (settings_.scaleTimer) scale_.stopTimer();
  finalizeShot(nowMs, next);
}

void BrewController::beginStop(EndReason reason, uint32_t nowMs, bool pressAlreadySent) {
  endReason_ = reason;
  stopRequestedMs_ = nowMs;
  stopWeightG_ = haveSample_ ? last_.weightG : 0.0f;
  stopFlowGS_ = flow_.flowGS();
  stopRetries_ = 0;
  retryInFlight_ = false;
  stopConfirmed_ = false;
  stopLatencyMs_ = 0;
  manualStopNeeded_ = false;
  stopPressPending_ = !pressAlreadySent;
  unconfirmedSinceMs_ = 0;
  // No coffee and no espresso-like flow seen yet: the machine is probably in
  // pre-infusion, but it is also possible the start press never reached the
  // button, in which case this press will start the pump.
  // After a bloom, coffee seen before the pause proves nothing about the
  // second start.
  stopUnverified_ = !runningConfirmed_ && (!firstDropSeen_ || bloomed_);
  stopStartedPump_ = false;
  shotTimeS_ = shotSeconds(nowMs);
  if (!pressAlreadySent) stopper().clearResult();
  enter(State::STOPPING_SHOT, nowMs);
  tickStopping(nowMs);  // send in this very tick, not 10 ms later
}

void BrewController::tickStopping(uint32_t nowMs) {
  const uint32_t waited = nowMs - stopRequestedMs_;
  if (waited >= MANUAL_WARN_MS) manualStopNeeded_ = true;

  auto delivered = [&]() {
    stopSentMs_ = stopper().pressSentAtMs();
    firstStopSentMs_ = stopSentMs_;
    shotTimeS_ = static_cast<uint32_t>(stopSentMs_ - shotStartMs_) / 1000.0f;
    stopper().clearResult();
    if (settings_.scaleTimer) scale_.stopTimer();
    manualStopNeeded_ = false;
    flowLowSinceMs_ = 0;  // stop confirmation must come from post-stop samples
    enter(State::POST_SHOT_DRIP, nowMs);
  };

  if (stopPressPending_) {
    if (stopPress()) {
      stopPressPending_ = false;
      if (haveSample_ && weightFresh(nowMs)) stopWeightG_ = last_.weightG;
    } else if (waited >= STOP_GIVE_UP_MS) {
      finalizeShot(nowMs, State::STOP_COMMAND_FAILED);
    }
    return;
  }

  switch (stopper().result()) {
    case PressResult::OK:
      delivered();
      break;
    case PressResult::NOT_SENT:
      stopper().clearResult();
      stopPressPending_ = true;
      break;
    case PressResult::UNCONFIRMED: {
      // The press may or may not have happened, and a second press would
      // restart the pump. Let the scale decide.
      if (unconfirmedSinceMs_ == 0) unconfirmedSinceMs_ = nonZero(nowMs);
      const uint32_t since = nowMs - unconfirmedSinceMs_;
      if (!scaleUsable(nowMs)) {
        manualStopNeeded_ = true;
        finalizeShot(nowMs, State::STOP_COMMAND_FAILED);
      } else if (flowLowSinceMs_ != 0 &&
                 static_cast<int32_t>(flowLowSinceMs_ - unconfirmedSinceMs_) >= 0 &&
                 static_cast<uint32_t>(nowMs - flowLowSinceMs_) >= FLOW_HOLD_MS) {
        delivered();
      } else if (since >= UNCONFIRMED_DECIDE_MS && flow_.flowGS() >= FLOW_ON_G_S) {
        stopper().clearResult();
        unconfirmedSinceMs_ = 0;
        stopPressPending_ = true;
      } else if (since >= UNCONFIRMED_ACCEPT_MS) {
        delivered();
      }
      break;
    }
    default:
      if (waited >= STOP_GIVE_UP_MS) {
        finalizeShot(nowMs, State::STOP_COMMAND_FAILED);
      }
      break;
  }
}

void BrewController::tickDrip(uint32_t nowMs) {
  if (retryInFlight_) {
    if (stopper().result() == PressResult::BUSY) return;
    retryInFlight_ = false;
    stopper().clearResult();
    stopSentMs_ = nowMs;  // observe a full drip period after the retry
    flowLowSinceMs_ = 0;
    return;
  }
  const uint32_t since = nowMs - stopSentMs_;
  const bool fresh = scaleUsable(nowMs);

  if (stopUnverified_) {
    if (!fresh) {
      // Nothing to verify against. Say so rather than report a clean stop.
      manualStopNeeded_ = true;
      finalizeShot(nowMs, State::STOP_COMMAND_FAILED);
      return;
    }
    const bool pumping = flowHighSinceMs_ != 0 &&
                         static_cast<uint32_t>(nowMs - flowHighSinceMs_) >= FLOW_HOLD_MS &&
                         last_.weightG - stopWeightG_ >= RUNNING_MIN_WEIGHT_G;
    if (pumping) {
      // Coffee is arriving after the "stop": that press started the pump.
      // It is running for certain now, so this press does stop it.
      if (stopPress()) {
        stopUnverified_ = false;
        stopStartedPump_ = true;
        retryInFlight_ = true;
        faultAfterStop_ = State::START_COMMAND_FAILED;
      }
      return;
    }
    if (since < static_cast<uint32_t>(settings_.noFlowTimeoutS) * 1000u) return;  // keep watching
    stopUnverified_ = false;  // quiet for as long as a start would need: it is off
  }

  if (!stopConfirmed_ && fresh && flowLowSinceMs_ != 0 &&
      static_cast<uint32_t>(nowMs - flowLowSinceMs_) >= FLOW_HOLD_MS) {
    stopConfirmed_ = true;
    const uint32_t latency = flowLowSinceMs_ - firstStopSentMs_;
    stopLatencyMs_ = latency > 65535 ? 65535 : static_cast<uint16_t>(latency);
  }

  // Still pouring like espresso well after the press, not tailing off: the
  // button was missed.
  const float retryFlow = stopFlowGS_ * STOP_RETRY_FLOW_SHARE > FLOW_ON_G_S
                              ? stopFlowGS_ * STOP_RETRY_FLOW_SHARE : FLOW_ON_G_S;
  if (!stopConfirmed_ && fresh && since >= STOP_RETRY_AFTER_MS &&
      flow_.flowGS() >= retryFlow &&
      last_.weightG - stopWeightG_ >= STOP_RETRY_MIN_GAIN_G) {
    if (stopRetries_ >= MAX_STOP_RETRIES) {
      manualStopNeeded_ = true;
      finalizeShot(nowMs, State::STOP_COMMAND_FAILED);
    } else if (stopPress()) {
      ++stopRetries_;
      retryInFlight_ = true;
    }
    return;
  }

  if (since >= settings_.dripObserveMs &&
      (stopConfirmed_ || !fresh ||
       since >= static_cast<uint32_t>(settings_.dripObserveMs) + DRIP_EXTRA_WAIT_MS)) {
    finalizeShot(nowMs, faultAfterStop_);
  }
}

void BrewController::finalizeShot(uint32_t nowMs, State nextState) {
  ShotRecord record;
  record.id = nextShotId_++;
  record.epochS = epochNow ? epochNow() : 0;
  memcpy(record.recipe, shotRecipe_.name, RECIPE_NAME_LEN);
  record.recipe[RECIPE_NAME_LEN - 1] = '\0';
  record.targetWeightG = shotRecipe_.targetWeightG;
  record.stopOffsetG = shotRecipe_.stopOffsetG;
  record.nextStopOffsetG = shotRecipe_.stopOffsetG;
  // Cup lifted mid-pour: what was in it is the most it held, not a reading
  // taken while it was on its way off the scale.
  if (endReason_ == EndReason::CUP_REMOVED) stopWeightG_ = peakWeightG_;
  record.stopWeightG = stopWeightG_;
  // A reading that collapsed versus the stop weight means the cup was lifted
  // (Open Brew: plausibleSettledBrewWeight).
  const bool finalValid = scaleUsable(nowMs) &&
                          last_.weightG >= stopWeightG_ - MAX_SETTLED_WEIGHT_DROP_G;
  record.finalWeightG = finalValid ? last_.weightG : stopWeightG_;
  record.dripWeightG = record.finalWeightG - record.stopWeightG;
  record.shotTimeS = shotTimeS_;
  record.firstDropS = firstDropSeen_ ? firstDropS_ : 0.0f;
  record.averageFlowGS = shotTimeS_ > 0.0f ? record.finalWeightG / shotTimeS_ : 0.0f;
  record.peakFlowGS = peakFlow_;
  record.stopLatencyMs = stopLatencyMs_;
  record.endReason = static_cast<uint8_t>(endReason_);
  record.grindUm = shotRecipe_.grindUm;
  memcpy(record.coffee, shotRecipe_.coffee, COFFEE_NAME_LEN);
  record.coffee[COFFEE_NAME_LEN - 1] = '\0';
  memcpy(record.region, shotRecipe_.region, ORIGIN_LEN);
  memcpy(record.varietal, shotRecipe_.varietal, ORIGIN_LEN);
  record.region[ORIGIN_LEN - 1] = record.varietal[ORIGIN_LEN - 1] = '\0';
  record.roastDate = shotRecipe_.roastDate;
  record.bagId = shotRecipe_.bagId;
  record.roastLevel = shotRecipe_.roastLevel;
  record.coffeeId = shotRecipe_.coffeeId;
  record.recipeId = shotRecipe_.recipeId;
  record.grinderId = library_.grinder.calibration;
  record.preinfusionHoldMs =
      shotRecipe_.preinfusion == Preinfusion::HOLD ? shotRecipe_.preinfusionHoldMs : 0;
  if (stopStartedPump_) record.flags |= SHOT_FLAG_STOP_STARTED_PUMP;
  record.doseG = shotRecipe_.doseG;
  record.targetTimeS = shotRecipe_.targetTimeS;
  record.timeBasis = static_cast<uint8_t>(settings_.grindTimeBasis);
  record.bloomPauseMs = shotRecipe_.bloomPauseMs;
  if (firstDropSeen_ && shotTimeS_ > firstDropS_ + 1.0f) {
    record.pourTimeS = shotTimeS_ - firstDropS_;
    record.pourFlowGS = record.finalWeightG / record.pourTimeS;
    const FlowFeatures flow = computeFlowFeatures(curve_.timeS, curve_.weightG, curve_.count,
                                                  firstDropS_, shotTimeS_, stopWeightG_);
    if (flow.valid) {
      record.flow25GS = flow.flow25GS;
      record.flow50GS = flow.flow50GS;
      record.flow75GS = flow.flow75GS;
      record.flowAcceleration = flow.acceleration;
      record.flowRoughnessGS = flow.roughnessGS;
    }
  }
  if (finalValid) record.flags |= SHOT_FLAG_FINAL_VALID;
  if (stopConfirmed_) record.flags |= SHOT_FLAG_STOP_CONFIRMED;
  if (stopRetries_ > 0) record.flags |= SHOT_FLAG_STOP_RETRIED;

  // Only a normal weight-target cut measures the cutoff error (Open Brew:
  // manual stops, time limits and forced stops do not train the offset).
  const bool weightCut = endReason_ == EndReason::WEIGHT ||
                         endReason_ == EndReason::WEIGHT_PREDICTED;
  if (settings_.learningEnabled && weightCut && finalValid && stopConfirmed_ &&
      stopRetries_ == 0 && nextState == State::SHOT_COMPLETE &&
      record.finalWeightG >= MIN_LEARN_WEIGHT_G &&
      shotRecipeIndex_ < library_.presetCount) {
    Preset &live = library_.presets[shotRecipeIndex_];
    const Recipe now = resolvePreset(library_, shotRecipeIndex_);
    float updated = 0.0f;
    const int baseline = static_cast<int>(settings_.learningFactor * 100.0f + 0.5f);
    uint8_t alpha = static_cast<uint8_t>(baseline < 1 ? 1 : (baseline > 100 ? 100 : baseline));
    if (settings_.adaptiveLearning && validAlpha(shotRecipe_.learnedAlpha)) {
      alpha = shotRecipe_.learnedAlpha;
    }
    // Warm-up: a new pair starts from a guessed offset, and the machine's
    // real one can be twice that (the Breville goes on at full flow for
    // 1.5 s after the stop: 3.6-3.8 g on the first real shots, against the
    // 1.5 g seed). At the usual 30% a step, that is 4-5 shots each over by a
    // gram or more. So the first shots take most of the error. The adaptive
    // selector still sees the usual factor: warm-up steps are not its.
    uint8_t stepAlpha = alpha;
    if (live.learnedShots < WARMUP_SHOTS && stepAlpha < WARMUP_ALPHA) stepAlpha = WARMUP_ALPHA;
    record.alphaUsed = stepAlpha;
    record.alphaNext = alpha;
    // Skip when the preset was edited, or became another, while the shot ran.
    if (now.recipeId == shotRecipe_.recipeId && now.coffeeId == shotRecipe_.coffeeId &&
        now.targetWeightG == shotRecipe_.targetWeightG &&
        live.stopOffsetG == shotRecipe_.stopOffsetG &&
        learnStopOffset(shotRecipe_.stopOffsetG, record.finalWeightG,
                        shotRecipe_.targetWeightG, stepAlpha / 100.0f, updated)) {
      live.stopOffsetG = updated;
      if (live.learnedShots < 255) ++live.learnedShots;
      record.nextStopOffsetG = updated;
      record.flags |= SHOT_FLAG_LEARNED;
      settingsDirty_ = true;
      if (settings_.adaptiveLearning) {
        // A changed target is a different recipe: start a fresh window.
        if (evidenceTargetG_[shotRecipeIndex_] != shotRecipe_.targetWeightG) {
          evidence_[shotRecipeIndex_] = AlphaEvidence{};
          evidenceTargetG_[shotRecipeIndex_] = shotRecipe_.targetWeightG;
        }
        const float observation =
            shotRecipe_.stopOffsetG + (record.finalWeightG - shotRecipe_.targetWeightG);
        live.learnedAlpha = evidence_[shotRecipeIndex_].observe(
            observation, shotRecipe_.stopOffsetG, alpha);
        record.alphaNext = live.learnedAlpha;
      }
    }
  }

  lastShot_ = record;
  hasLastShot_ = true;
  completedPending_ = true;
  enter(nextState, nowMs);
}

void BrewController::tickFault(uint32_t nowMs, Intent intent) {
  if (manualPressInFlight_ && stopper().result() != PressResult::BUSY) {
    manualPressInFlight_ = false;
    stopper().clearResult();
  }

  // A start that looked failed but the scale now shows espresso: take the
  // shot back under control instead of leaving the machine unsupervised.
  if (state_ == State::START_COMMAND_FAILED && scaleUsable(nowMs) &&
      flowHighSinceMs_ != 0 &&
      static_cast<uint32_t>(nowMs - flowHighSinceMs_) >= FLOW_HOLD_MS &&
      last_.weightG >= RUNNING_MIN_WEIGHT_G &&
      static_cast<uint32_t>(nowMs - shotStartMs_) < HARD_MAX_SHOT_MS) {
    runningConfirmed_ = true;
    endReason_ = EndReason::NONE;
    directConfirm_ = lowWeightConfirm_ = rejectedSamples_ = 0;
    enter(State::BREWING, nowMs);
    return;
  }

  if (intent == Intent::MANUAL_PRESS && isFault(state_) && !manualPressInFlight_) {
    stopper().clearResult();
    manualPressInFlight_ = stopPress();
    return;
  }
  if (intent == Intent::ACK || intent == Intent::START || intent == Intent::STOP) {
    manualStopNeeded_ = false;
    enter(scale_.connected() ? State::READY : State::CONNECTING_SCALE, nowMs);
  }
}

LiveStatus BrewController::status(uint32_t nowMs) const {
  LiveStatus s;
  s.state = state_;
  s.scaleConnected = scale_.connected();
  s.botConnected = actuator_.connected();
  s.weightFresh = weightFresh(nowMs);
  s.weightG = haveSample_ ? last_.weightG : 0.0f;
  s.flowGS = s.weightFresh ? flow_.flowGS() : 0.0f;
  s.samplePeriodMs = samplePeriodMs_;
  s.manualStopNeeded = manualStopNeeded_;
  s.activeRecipe = activePreset();

  const bool inShot = state_ == State::STARTING_SHOT || state_ == State::BREWING ||
                      state_ == State::BLOOMING || state_ == State::STOPPING_SHOT || state_ == State::POST_SHOT_DRIP;
  const Recipe &r = inShot ? shotRecipe_ : recipe();
  s.targetWeightG = r.targetWeightG;
  s.stopOffsetG = r.stopOffsetG;
  memcpy(s.recipe, r.name, RECIPE_NAME_LEN);
  s.recipe[RECIPE_NAME_LEN - 1] = '\0';
  s.firstDropSeen = inShot && firstDropSeen_;
  s.verifyingStop = state_ == State::POST_SHOT_DRIP && stopUnverified_;
  s.holdingButton = (state_ == State::STARTING_SHOT && startHoldMs_ > 0) ||
                    (state_ == State::BLOOMING && (bloomPhase_ == BloomPhase::RESTART_PRESS ||
                                                   bloomPhase_ == BloomPhase::RESTART_WAIT));
  if (state_ == State::BLOOMING) {
    const uint32_t gone = bloomPhase_ == BloomPhase::PAUSE ? nowMs - bloomPauseStartMs_
                          : (bloomPhase_ < BloomPhase::PAUSE ? 0 : shotRecipe_.bloomPauseMs);
    s.bloomLeftS = gone < shotRecipe_.bloomPauseMs ? (shotRecipe_.bloomPauseMs - gone) / 1000.0f : 0.0f;
  }
  s.stopStartedPump = stopStartedPump_;
  s.machineStyle = machine().style;
  s.targetCue = state_ == State::BREWING && cueGiven_;
  s.awaitingDrip = state_ == State::STARTING_SHOT && awaitingDrip_;
  s.cupPresent = cupPresent_;
  s.cupRequired = settings_.requireCup;
  s.useRatio = r.useRatio;
  s.doseG = r.doseG;
  s.ratio = r.ratio;

  if (state_ == State::BREWING || state_ == State::BLOOMING ||
      state_ == State::STOPPING_SHOT) {
    s.shotTimeS = shotSeconds(nowMs);
  } else if (state_ == State::STARTING_SHOT) {
    s.shotTimeS = 0.0f;
  } else {
    s.shotTimeS = shotTimeS_;
  }
  return s;
}

}  // namespace bbw
