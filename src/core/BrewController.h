#pragma once
// The brew-by-weight state machine. Portable: talks to the scale and the
// button actuator through two small interfaces and is driven by tick() plus
// onSample(), so the whole shot logic runs unmodified in the host tests.

#include "BbwAlgorithms.h"
#include "Library.h"
#include "ShotData.h"
#include "Types.h"

namespace bbw {

class IScale {
 public:
  virtual ~IScale() = default;
  virtual bool connected() const = 0;
  virtual bool tare() = 0;
  virtual bool startTimer() = 0;
  virtual bool stopTimer() = 0;
  virtual bool resetTimer() = 0;
};

// A momentary button press: the actuator pushes the machine's button and
// releases it again. One press toggles the Breville between brewing and idle.
enum class PressResult : uint8_t {
  IDLE,         // nothing requested since clearResult()
  BUSY,         // press in progress (command sent, arm moving, settle time)
  OK,           // Bot acknowledged the press
  NOT_SENT,     // nothing reached the Bot: safe to try again
  UNCONFIRMED,  // command went out but no usable acknowledgement came back
};

class IActuator {
 public:
  virtual ~IActuator() = default;
  virtual bool connected() const = 0;
  // false when busy or not connected. holdMs = 0 is a normal momentary press;
  // otherwise the button is held down for that long before release.
  virtual bool beginPress(uint32_t holdMs = 0) = 0;
  virtual PressResult result() const = 0;
  virtual uint32_t pressSentAtMs() const = 0;
  virtual void clearResult() = 0;
};

constexpr size_t MAX_CURVE_POINTS = 2048;

// Every reading of the shot in progress, then of the last one. It is what
// the stop prediction and the flow features work on, and what is written to
// flash as the shot's data set (core/ShotData.h).
struct ShotCurve {
  float timeS[MAX_CURVE_POINTS];
  float weightG[MAX_CURVE_POINTS];
  uint16_t timerDs[MAX_CURVE_POINTS];  // scale's timer, 0.1 s; CURVE_NO_TIMER if none
  uint8_t flags[MAX_CURVE_POINTS];
  size_t count = 0;
};

class BrewController {
 public:
  BrewController(IScale &scale, IActuator &actuator, Settings &settings, Library &library)
      : scale_(scale), actuator_(actuator), settings_(settings), library_(library) {}

  void tick(uint32_t nowMs);
  void onSample(const WeightSample &sample);

  // User intents. All are safe to call in any state; the ones that do not
  // apply are ignored.
  void requestStart();        // READY: full cycle. READY_TO_BREW: go. Result/fault: acknowledge.
  void requestStop();         // stop the shot / cancel the preparation
  void requestTare();         // READY only
  void acknowledge();         // leave SHOT_COMPLETE or a fault state
  void requestManualPress();  // one raw button press, only from a fault state
  void raiseBleError();
  // After an unexpected restart during a shot: if the scale shows espresso
  // flowing once everything reconnects, press stop.
  void armRestartRecovery(uint32_t nowMs);

  State state() const { return state_; }
  LiveStatus status(uint32_t nowMs) const;
  const ShotCurve &curve() const { return curve_; }
  const ShotRecord &lastShot() const { return lastShot_; }
  bool hasLastShot() const { return hasLastShot_; }
  void setLastShotGrindVote(uint32_t id, uint8_t vote) {
    if (hasLastShot_ && lastShot_.id == id) lastShot_.grindVote = vote;
  }
  bool machineMayBeRunning() const;
  // True from the Start request until the shot is finished and confirmed
  // stopped. The Bot link is only held while this is true, to save its battery.
  bool actuatorNeeded() const;

  // Edge outputs for the application layer.
  bool takeCompletedShot(ShotRecord &out);  // a new record to persist
  bool takeSettingsDirty();                 // learned offset changed (it is in the library)

 private:
  enum class Intent : uint8_t { NONE, START, STOP, TARE, ACK, MANUAL_PRESS };

  void enter(State next, uint32_t nowMs);
  uint8_t activePreset() const;
  Recipe recipe() const;  // the active preset, resolved
  bool weightFresh(uint32_t nowMs) const;
  bool scaleUsable(uint32_t nowMs) const;
  State scaleFaultState(uint32_t nowMs) const;
  void sendTare(uint32_t nowMs);
  void beginShot(uint32_t nowMs);
  // pressAlreadySent: the stop press is in flight and its outcome unknown;
  // carry on from there instead of pressing.
  void beginStop(EndReason reason, uint32_t nowMs, bool pressAlreadySent = false);
  void finalizeShot(uint32_t nowMs, State nextState);
  void endWithoutPress(uint32_t nowMs, EndReason reason = EndReason::MACHINE_STOPPED,
                       State next = State::SHOT_TIMEOUT);
  void trackCup(const WeightSample &sample);

  void acceptBrewSample(const WeightSample &sample);
  float recordSample(const WeightSample &sample);
  void trackFlow(const WeightSample &sample);
  float shotSeconds(uint32_t nowMs) const;

  void tickPreparation(uint32_t nowMs, Intent intent);
  void tickStarting(uint32_t nowMs, Intent intent);
  void tickBrewing(uint32_t nowMs, Intent intent);
  void tickBlooming(uint32_t nowMs, Intent intent);
  void enterBrewingAfterBloom(uint32_t nowMs, uint32_t restartMs);
  void tickStopping(uint32_t nowMs);
  void tickDrip(uint32_t nowMs);
  void tickFault(uint32_t nowMs, Intent intent);

  IScale &scale_;
  IActuator &actuator_;
  Settings &settings_;
  Library &library_;

  State state_ = State::BOOT;
  uint32_t stateSinceMs_ = 0;
  Intent intent_ = Intent::NONE;

  // Scale stream
  WeightSample last_;        // last accepted reading
  uint32_t lastRxMs_ = 0;    // last notification, accepted or not
  bool haveSample_ = false;
  bool bleErrorPending_ = false;
  uint16_t samplePeriodMs_ = 0;
  FlowEstimator flow_;
  uint32_t flowHighSinceMs_ = 0;  // 0 = not currently high
  uint32_t flowLowSinceMs_ = 0;

  // Tare / cup
  uint32_t tareSentAtMs_ = 0;
  uint8_t tareAttempts_ = 0;
  uint8_t zeroSamples_ = 0;
  bool cupTared_ = false;
  uint32_t startRequestedMs_ = 0;
  float cupMinG_ = 0.0f, cupMaxG_ = 0.0f;
  uint32_t cupStableSinceMs_ = 0;
  uint8_t cupStableSamples_ = 0;

  // Cup on the scale while idle. Weight alone cannot tell an empty pan at
  // 0 g from a tared cup at 0 g, so this follows the steps: a stable rise of
  // a cup's weight is a cup arriving, the fall to zero straight after is its
  // tare, any other such fall is the cup leaving.
  bool cupPresent_ = false;
  bool cupInitPending_ = true;   // first stable reading after the scale connects
  bool haveStable_ = false;
  float stableLevelG_ = 0.0f;
  float runRefG_ = 0.0f;
  uint8_t runCount_ = 0;
  uint32_t cupPlacedAtMs_ = 0;

  // Shot
  ShotCurve curve_;
  Recipe shotRecipe_;           // snapshot taken at shot start
  uint8_t shotRecipeIndex_ = 0;  // preset index
  uint32_t shotStartMs_ = 0;
  float expectedEndS_ = 0.0f;
  uint8_t directConfirm_ = 0;
  uint8_t lowWeightConfirm_ = 0;
  uint8_t cupGoneConfirm_ = 0;   // consecutive readings far below the last good one
  float peakWeightG_ = 0.0f;     // most that has been in the cup this shot
  float pourRateGS_ = 0.0f;      // flow at the last reading that was not a falling one
  uint8_t rejectedSamples_ = 0;
  bool firstDropSeen_ = false;
  uint8_t firstDropConfirm_ = 0;
  float firstDropS_ = 0.0f;
  float peakFlow_ = 0.0f;
  bool runningConfirmed_ = false;

  // Stop
  EndReason endReason_ = EndReason::NONE;
  State faultAfterStop_ = State::SHOT_COMPLETE;
  uint32_t stopRequestedMs_ = 0;
  uint32_t stopSentMs_ = 0;       // restarts after a retry press
  uint32_t firstStopSentMs_ = 0;  // the press that defines the shot time
  uint32_t unconfirmedSinceMs_ = 0;
  float stopWeightG_ = 0.0f;
  float shotTimeS_ = 0.0f;
  bool stopConfirmed_ = false;
  uint16_t stopLatencyMs_ = 0;
  uint8_t stopRetries_ = 0;
  bool retryInFlight_ = false;
  bool manualStopNeeded_ = false;
  bool stopPressPending_ = false;
  // The stop press was sent with no evidence the machine was running. One
  // press toggles, so it may have started the pump instead: keep watching.
  bool stopUnverified_ = false;
  bool stopStartedPump_ = false;
  bool stopWanted_ = false;  // STOP tapped while the start press was still in progress
  uint32_t startHoldMs_ = 0;
  bool controllerTimeLimit_ = false;  // this shot is stopped by the Bot at its time limit

  // Bloom (Recipe::bloomPauseMs): the steps between the pre-infusion hold
  // and the brew.
  enum class BloomPhase : uint8_t {
    STOP_PRESS,     // waiting for the Bot to take the press that switches the pump off
    STOP_WAIT,      // that press is in flight
    PAUSE,          // pump off, the puck soaks
    RESTART_PRESS,  // waiting for the Bot to take the press that starts the brew
    RESTART_WAIT,   // that press, a short hold, is in flight
  };
  BloomPhase bloomPhase_ = BloomPhase::STOP_PRESS;
  uint32_t bloomPhaseSinceMs_ = 0;
  uint32_t bloomPauseStartMs_ = 0;
  float bloomPauseStartG_ = 0.0f;
  bool bloomed_ = false;          // this shot's pump has been off and on again
  // Wanted while a bloom press was in flight; carried out when it is done.
  EndReason bloomAbortReason_ = EndReason::NONE;
  State bloomAbortState_ = State::SHOT_COMPLETE;
  // Where "has coffee started to arrive" is judged from: the start press and
  // an empty cup, or after a bloom the second start and what the cup held.
  uint32_t brewPhaseStartMs_ = 0;
  float brewPhaseStartG_ = 0.0f;

  // Fault-state housekeeping
  bool manualPressInFlight_ = false;
  uint32_t recoveryUntilMs_ = 0;

  // Adaptive learning factor: evidence lives in RAM (as in Open Brew, a
  // restart keeps the learned factor but collects a fresh window).
  AlphaEvidence evidence_[MAX_PRESETS];
  float evidenceTargetG_[MAX_PRESETS] = {};

  // Outputs
  ShotRecord lastShot_;
  bool hasLastShot_ = false;
  bool completedPending_ = false;
  bool settingsDirty_ = false;
  uint32_t nextShotId_ = 1;

 public:
  void setNextShotId(uint32_t id) { nextShotId_ = id; }
  // Wall-clock provider, 0 when unknown. Set by the application.
  uint32_t (*epochNow)() = nullptr;
};

}  // namespace bbw
