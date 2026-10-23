#pragma once
// The brew-by-weight state machine. Portable: talks to the scale and the
// button actuator through two small interfaces and is driven by tick() plus
// onSample(), so the whole shot logic runs unmodified in the host tests.

#include "BbwAlgorithms.h"
#include "Library.h"
#include "MachineProfile.h"
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
  // A sound from the scale, for a scale that can be told to make one. False: it cannot.
  virtual bool beep() { return false; }
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
  // For a machine whose button brews for as long as it is held.
  // beginHold: push down and stay. false: busy, not connected, or this
  // actuator cannot hold. result() stays BUSY while the button is held.
  virtual bool beginHold() { return false; }
  // The arm is down and holding.
  virtual bool holding() const { return false; }
  // Let go. Also brings up an arm nothing is known about (after a restart).
  // result() then reports how that went. false: nothing left the radio.
  virtual bool release() { return false; }
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

// What was last known to be on the scale, kept across a restart of the
// controller (and a dropped scale link). Weight alone cannot tell a tared cup
// at 0 g from an empty scale at 0 g: the scale zeroes itself when it is
// switched on. The scale's own timer can: it keeps its value for as long as
// the scale stays on and is 0 after the scale has been switched on again.
struct CupMemory {
  bool present = false;
  float levelG = 0.0f;        // the still reading at the time
  int32_t scaleTimerMs = -1;  // the scale's timer at the time; -1 if it reports none
};

class BrewController {
 public:
  BrewController(IScale &scale, IActuator &actuator, Settings &settings, Library &library)
      : scale_(scale), actuator_(actuator), settings_(settings), library_(library) {}

  void tick(uint32_t nowMs);
  void onSample(const WeightSample &sample);

  // How this machine is worked (core/MachineProfile.h). Not set: the Breville
  // Dual Boiler's MANUAL button, which is what all of this was written for.
  // Change it only while no shot is in hand.
  void setMachine(const MachineProfile *machine) { machine_ = machine; }
  // The bot on the stop button of a machine with separate start and stop
  // buttons. Used for the stop only where the machine's profile says so.
  void setStopActuator(IActuator *stop) { stopActuator_ = stop; }
  // The stop bot is part of this machine: it has to be there before a shot starts.
  bool stopActuatorNeeded() const { return machine().separateStop() && actuatorNeeded(); }
  const MachineProfile &machine() const {
    static const MachineProfile breville;
    return machine_ ? *machine_ : breville;
  }

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
  // The cup as last seen still on the scale, to keep for after a restart, and
  // what to start from. A remembered cup is believed again only if the scale
  // shows the same reading and the same non-zero timer: the scale stayed on
  // and nothing was put on or taken off.
  const CupMemory &cupMemory() const { return cupMemory_; }
  bool cupPresent() const { return cupPresent_; }
  // For the log: why the scale was last tared (once), and whether a start was refused because no
  // cup was seen on the scale (once).
  const char *takeTareReason() {
    const char *why = tareWhy_;
    tareWhy_ = nullptr;
    return why;
  }
  bool takeStartRefusedNoCup() {
    const bool refused = startRefusedNoCup_;
    startRefusedNoCup_ = false;
    return refused;
  }
  // The scale has been seen zeroing itself after a cup went on or came off
  // (an auto-tare mode). It can then zero a cup coming off before reporting
  // it at all, and no reading shows the cup is gone.
  bool scaleZeroesItself() const { return scaleZeroesItself_; }
  void restoreCupMemory(const CupMemory &memory) { cupMemory_ = memory; }
  const ShotRecord &lastShot() const { return lastShot_; }
  bool hasLastShot() const { return hasLastShot_; }
  void setLastShotGrindVote(uint32_t id, uint8_t vote) {
    if (hasLastShot_ && lastShot_.id == id) lastShot_.grindVote = vote;
  }
  bool machineMayBeRunning() const;
  // No shot in hand (none being set up, run or dripped) and the machine known to be stopped: the
  // power button may be pressed, whatever screen is showing (home, a shot's result, a fault).
  bool idleForPower() const;
  // A shot's result or a fault (the machine stopped) still showing when the scale has been gone this
  // long, let go to rest or switched off: back to the home screen, which waits for the scale and lets
  // the display go dark.
  static constexpr uint32_t RESULT_SCALE_GONE_MS = 60000;
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
  // The shot has to end now: by a press where the bot stops this machine,
  // otherwise by closing the record (the owner has the machine in hand).
  void stopOrEnd(EndReason reason, uint32_t nowMs);
  // The pour stopped with nothing pressed here.
  void endAtMachine(uint32_t nowMs);
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
  const MachineProfile *machine_ = nullptr;
  IActuator *stopActuator_ = nullptr;
  // What stops this machine: the stop bot where it has its own stop button,
  // otherwise the brew bot.
  IActuator &stopper() const {
    return machine().separateStop() && stopActuator_ ? *stopActuator_ : actuator_;
  }
  // The stop itself: letting go of a held button, otherwise a press.
  bool stopPress() { return machine().holdsForShot() ? actuator_.release() : stopper().beginPress(); }
  // The machine as it was when this shot began.
  bool shotStarts_ = true;       // the bot started it
  bool shotStops_ = true;        // the bot stops it at the target
  bool cueGiven_ = false;        // the owner has been told the target is reached
  bool awaitingDrip_ = false;    // worked by hand, clock starts with the first coffee
  uint32_t dripAtMs_ = 0, dripSampleMs_ = 0;
  uint8_t dripConfirm_ = 0;

  State state_ = State::BOOT;
  uint32_t stateSinceMs_ = 0;
  Intent intent_ = Intent::NONE;

  // Scale stream
  WeightSample last_;        // last accepted reading
  uint32_t lastRxMs_ = 0;    // last notification, accepted or not
  uint32_t lastGoodRxMs_ = 0;
  uint32_t scaleGoneSinceMs_ = 0;  // the scale's link down while a result or fault shows; 0: it is up
  const char *tareWhy_ = nullptr;
  bool startRefusedNoCup_ = false;  // the last one while the scale was in use, before it went away
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
  uint32_t cupRemovedAtMs_ = 0;
  bool haveRemoved_ = false;
  bool scaleZeroesItself_ = false;
  float excursionMaxG_ = 0.0f, excursionMinG_ = 0.0f;  // since the last still reading
  uint32_t excursionMaxAtMs_ = 0, excursionMinAtMs_ = 0;
  CupMemory cupMemory_;
  // An empty scale left reading below zero (a tared cup taken off, on a scale
  // without auto-tare) is zeroed once: a Lunar showing a weight never switches
  // itself off, and the links follow the scale.
  uint32_t runSinceMs_ = 0;        // when the reading became still at runRefG_
  bool emptyZeroSent_ = false;     // for this still reading
  bool haveEmptyZero_ = false;
  uint32_t emptyZeroAtMs_ = 0;

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
  float stopFlowGS_ = 0.0f;  // flow at the moment the stop was asked for
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
