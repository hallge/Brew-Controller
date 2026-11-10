// CoreSession: the BrewController behind one small surface for Swift. See CoreSession.h.
//
// Simulated mode runs the host tests' machine (Sim/MachineSim.h) in step with the clock given to
// tick(): the world advances in 10 ms steps exactly as Rig::step() in test/host/test_main.cpp does,
// and readings reach the core as the Lunar's own bytes through a ScaleLink. Real mode runs the core's
// ScaleLink and PusherLink on bytes that Swift moves with CoreBluetooth.

#include "CoreSession.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "Sim/MachineSim.h"
#include "core/BrewController.h"
#include "core/Library.h"
#include "core/PusherLink.h"
#include "core/ScaleLink.h"
#include "core/SettingsUtil.h"

using namespace bbw;

namespace {
constexpr uint32_t kProbeCapacity = 16384;  // about 2.5 minutes of ticks at 100 Hz plus the rest
constexpr uint32_t kStepMs = 10;
}  // namespace

struct CoreSessionImpl;

// The simulator or the radio behind IScale. Adapted from ScalePort in test/host/test_main.cpp:183.
struct ScalePort : IScale {
  CoreSessionImpl &s;
  explicit ScalePort(CoreSessionImpl &impl) : s(impl) {}
  bool connected() const override;
  bool tare() override { return op(ScaleOp::Tare); }
  bool startTimer() override { return op(ScaleOp::StartTimer); }
  bool stopTimer() override { return op(ScaleOp::StopTimer); }
  bool resetTimer() override { return op(ScaleOp::ResetTimer); }
  bool op(ScaleOp operation);
};

// The simulator or the Bot's link behind IActuator. Adapted from BotPort in test/host/test_main.cpp.
struct BotPort : IActuator {
  CoreSessionImpl &s;
  explicit BotPort(CoreSessionImpl &impl) : s(impl) {}
  bool connected() const override;
  bool beginPress(uint32_t holdMs = 0) override;
  PressResult result() const override;
  uint32_t pressSentAtMs() const override;
  void clearResult() override;
};

// What the PusherLink writes: queued as bytes for Swift.
struct BotWriter : PusherWriter {
  CoreSessionImpl &s;
  explicit BotWriter(CoreSessionImpl &impl) : s(impl) {}
  bool write(const uint8_t *data, size_t length) override;
};

struct CoreSessionImpl {
  Settings settings = defaultSettings();
  Library library = defaultLibrary();
  bool simulated = true;
  uint32_t nowMs = 0;  // the core's clock, as last given

  brewsim::Sim sim;
  bool simStarted = false;
  ScaleLink scaleLink;
  PusherLink botLink;
  bool scaleUp = false;  // real devices
  bool botUp = false;
  bool scaleLost = false;

  ScalePort scalePort{*this};
  BotPort botPort{*this};
  BotWriter botWriter{*this};
  BrewController ctl{scalePort, botPort, settings, library};

  CommandQueue commands;
  ShotSummary last;
  uint32_t completed = 0;
  State lastState = State::BOOT;
  uint32_t lastCoreTickMs = 0;
  bool haveCoreTick = false;
  bool paused = false;
  uint32_t pausedSinceMs = 0, pausedUntilMs = 0;
  uint32_t emptyCupAtMs = 0;

  ProbeEntry *ring = new ProbeEntry[kProbeCapacity];
  uint32_t probeSeq = 0;

  CoreSessionImpl() {
    settings.requireCup = false;  // as in the host tests' Rig: the pan is as it is
    driverSelect("acaia");
    scaleLink.setSampleSink(&CoreSessionImpl::onSample, this);
    botLink.begin("bot", &settings, &botWriter);
    botLink.setLog(&CoreSessionImpl::onPusherLog);
    sim.wire = &scaleLink;
  }
  ~CoreSessionImpl() { delete[] ring; }

  // ---- probe log ----
  void probe(uint8_t kind, uint32_t atMs, float a, float b, uint32_t c) {
    ProbeEntry &e = ring[probeSeq % kProbeCapacity];
    e.seq = ++probeSeq;
    e.atMs = atMs;
    e.kind = kind;
    e.a = a;
    e.b = b;
    e.c = c;
  }

  static void onPusherLog(const char *) {
    // The press sequence's own log lines are not kept: the commands are in the probe log already.
  }

  static void onSample(void *context, const WeightSample &sample) {
    auto *self = static_cast<CoreSessionImpl *>(context);
    self->probe(kProbeSample, sample.atMs, sample.weightG, 0.0f, sample.sequence);
    self->ctl.onSample(sample);
  }

  bool driverSelect(const char *id) {
    for (size_t i = 0; i < scaleLink.driverCount(); ++i) {
      if (strcmp(scaleLink.driverAt(i)->id(), id) == 0) {
        scaleLink.select(scaleLink.driverAt(i));
        return true;
      }
    }
    return false;
  }

  // ---- commands to Swift ----
  bool enqueue(uint8_t kind, const uint8_t *data, size_t length, uint32_t at) {
    if (length == 0 || length > kBridgeCommandBytes) return false;
    BridgeCommand command;
    command.kind = kind;
    command.length = static_cast<uint8_t>(length);
    command.atMs = at;
    memcpy(command.bytes, data, length);
    if (!commands.push(command)) {
      probe(kProbeNote, at, 0.0f, 0.0f, kNoteCommandQueueFull);
      return false;
    }
    probe(kProbeCommand, at, static_cast<float>(length), data[0], kind);
    return true;
  }

  // ---- the core's tick, and what is taken from it ----
  void coreTick(uint32_t now) {
    nowMs = now;
    probe(kProbeTick, now, haveCoreTick ? static_cast<float>(now - lastCoreTickMs) : 0.0f, 0.0f,
          static_cast<uint32_t>(ctl.state()));
    lastCoreTickMs = now;
    haveCoreTick = true;
    ctl.tick(now);
    if (ctl.state() != lastState) {
      lastState = ctl.state();
      probe(kProbeState, now, 0.0f, 0.0f, static_cast<uint32_t>(lastState));
    }
    ShotRecord record;
    if (ctl.takeCompletedShot(record)) {
      ++completed;
      last.valid = true;
      last.id = record.id;
      last.targetWeightG = record.targetWeightG;
      last.stopOffsetG = record.stopOffsetG;
      last.stopWeightG = record.stopWeightG;
      last.finalWeightG = record.finalWeightG;
      last.dripWeightG = record.dripWeightG;
      last.shotTimeS = record.shotTimeS;
      last.doseG = record.doseG;
      last.stopLatencyMs = record.stopLatencyMs;
      last.endReason = record.endReason;
    }
    ctl.takeSettingsDirty();
  }

  // ---- simulated world: one 10 ms step, as Rig::step() in test/host/test_main.cpp ----
  void stepOnce() {
    sim.nowMs += kStepMs;
    if (paused && static_cast<int32_t>(sim.nowMs - pausedUntilMs) >= 0) {
      paused = false;
      probe(kProbeNote, sim.nowMs, static_cast<float>(sim.nowMs - pausedSinceMs) / 1000.0f, 0.0f,
            kNoteResumed);
    }
    sim.trueWeightG += sim.flowNow() * 0.010;
    if (sim.pendingTareAtMs && sim.nowMs >= sim.pendingTareAtMs) {
      sim.tareOffsetG = sim.trueWeightG;
      sim.pendingTareAtMs = 0;
    }
    if (emptyCupAtMs && sim.nowMs >= emptyCupAtMs) {  // the owner empties the cup (Rig::nextShot)
      emptyCupAtMs = 0;
      sim.trueWeightG = 0.0;
      sim.tareOffsetG = 0.0;
      sim.machineStoppedMs = 0;
    }
    if (ctl.actuatorNeeded()) {
      if (sim.botNeededSinceMs == 0) sim.botNeededSinceMs = sim.nowMs;
      const bool up = sim.botAvailable && sim.nowMs - sim.botNeededSinceMs >= sim.botConnectDelayMs;
      if (up && !sim.botLink) ++sim.botConnects;
      sim.botLink = up;
    } else {
      sim.botNeededSinceMs = 0;
      sim.botLink = false;
    }
    if (sim.pressBusy && sim.nowMs - sim.pressAtMs >= sim.ackDelayMs) {
      sim.pressBusy = false;
      sim.pressResult = sim.pressUnconfirmed ? PressResult::UNCONFIRMED : PressResult::OK;
    }
    if (paused) return;  // the app is suspended: no reading arrives, nothing ticks
    if (sim.scaleLink && !sim.scaleSilent && sim.nowMs - sim.lastSampleMs >= sim.samplePeriodMs) {
      sim.lastSampleMs = sim.nowMs;
      uint8_t frame[SCALE_MAX_FRAME];
      const size_t n = brewsim::wireFrame(sim.wire->driver()->id(), sim.trueWeightG - sim.tareOffsetG,
                                          sim.timerMs(), frame);
      sim.wire->onNotify(frame, n, sim.nowMs);  // its sample sink calls ctl.onSample()
    }
    coreTick(sim.nowMs);
  }

  void tickSimulated(uint32_t now) {
    if (!simStarted) {
      simStarted = true;
      sim.nowMs = now;
      sim.lastSampleMs = now;
      scaleLink.start(now);
    }
    while (static_cast<int32_t>(now - sim.nowMs) >= static_cast<int32_t>(kStepMs)) stepOnce();
  }

  void tickReal(uint32_t now) {
    if (paused) {
      if (static_cast<int32_t>(now - pausedUntilMs) < 0) return;
      paused = false;
      probe(kProbeNote, now, static_cast<float>(now - pausedSinceMs) / 1000.0f, 0.0f, kNoteResumed);
    }
    nowMs = now;
    botLink.tick(now, botUp);
    if (scaleUp) {
      const bool alive = scaleLink.service(now, ctl.actuatorNeeded(), [this, now](const uint8_t *d, size_t n) {
        return enqueue(kCmdWriteScale, d, n, now);
      });
      if (!alive) {
        scaleUp = false;
        scaleLost = true;
        scaleLink.close();
      }
    }
    coreTick(now);
  }
};

// ------------------------------------------------------------------ ports --

bool ScalePort::connected() const {
  return s.simulated ? s.sim.scaleLink : (s.scaleUp && s.scaleLink.driver() != nullptr);
}

bool ScalePort::op(ScaleOp operation) {
  if (!s.simulated) {
    uint8_t buffer[SCALE_MAX_COMMAND_LENGTH];
    int length = 0;
    // A scale without this command (a timer, say) is simply not asked.
    if (!s.scaleLink.encode(operation, buffer, &length) || length <= 0) return false;
    return s.enqueue(kCmdWriteScale, buffer, static_cast<size_t>(length), s.nowMs);
  }
  brewsim::Sim &sim = s.sim;
  if (!sim.wireHas(operation)) return false;
  s.probe(kProbeCommand, sim.nowMs, 0.0f, static_cast<float>(operation), kCmdWriteScale);
  switch (operation) {
    case ScaleOp::Tare: return sim.tare();
    case ScaleOp::StartTimer:
      sim.timerRunning = true;
      sim.timerStartMs = sim.nowMs + sim.timerCommandDelayMs;
      return true;
    case ScaleOp::StopTimer:
      sim.timerHeldMs = sim.timerMs();
      sim.timerRunning = false;
      return true;
    case ScaleOp::ResetTimer:
      sim.timerRunning = false;
      sim.timerHeldMs = 0;
      return true;
    default: return false;
  }
}

bool BotPort::connected() const {
  return s.simulated ? s.sim.botLink : s.botLink.ready(s.botUp);
}

bool BotPort::beginPress(uint32_t holdMs) {
  if (s.simulated) {
    const bool ok = s.sim.beginPress(holdMs);
    if (ok) s.probe(kProbeCommand, s.sim.nowMs, 0.0f, static_cast<float>(holdMs), kCmdWriteBot);
    return ok;
  }
  return s.botLink.beginPress(holdMs, s.nowMs, s.botUp);
}

PressResult BotPort::result() const { return s.simulated ? s.sim.result() : s.botLink.result(); }
uint32_t BotPort::pressSentAtMs() const {
  return s.simulated ? s.sim.pressSentAtMs() : s.botLink.pressSentAtMs();
}
void BotPort::clearResult() {
  if (s.simulated) s.sim.clearResult(); else s.botLink.clearResult();
}

bool BotWriter::write(const uint8_t *data, size_t length) {
  return s.enqueue(kCmdWriteBot, data, length, s.nowMs);
}

// ---------------------------------------------------------------- the class --

CoreSession::CoreSession() : impl_(new CoreSessionImpl()), refs_(1) {}
CoreSession::~CoreSession() { delete impl_; }

void bbCoreSessionRetain(CoreSession *session) { __atomic_fetch_add(&session->refs_, 1, __ATOMIC_RELAXED); }
void bbCoreSessionRelease(CoreSession *session) {
  if (__atomic_fetch_sub(&session->refs_, 1, __ATOMIC_ACQ_REL) == 1) delete session;
}

CoreSession *CoreSession::create() { return new CoreSession(); }

uint32_t CoreSession::uptimeMs() {
#ifdef CLOCK_UPTIME_RAW
  const clockid_t clock = CLOCK_UPTIME_RAW;  // iOS: stops in sleep, never goes back
#else
  const clockid_t clock = CLOCK_MONOTONIC;   // the PC, for compile checks
#endif
  struct timespec ts;
  clock_gettime(clock, &ts);
  return static_cast<uint32_t>(static_cast<uint64_t>(ts.tv_sec) * 1000u +
                               static_cast<uint64_t>(ts.tv_nsec) / 1000000u);
}

const char *CoreSession::endReasonText(uint8_t endReason) {
  return endReasonName(static_cast<EndReason>(endReason));
}

const char *CoreSession::stateText(uint8_t state) { return stateName(static_cast<State>(state)); }

void CoreSession::tick(uint32_t nowMs) {
  if (impl_->simulated) impl_->tickSimulated(nowMs); else impl_->tickReal(nowMs);
}

void CoreSession::requestStart() {
  impl_->probe(kProbeNote, impl_->nowMs, 0.0f, 1.0f, kNoteRequest);
  impl_->ctl.requestStart();
}
void CoreSession::requestStop() {
  impl_->probe(kProbeNote, impl_->nowMs, 0.0f, 2.0f, kNoteRequest);
  impl_->ctl.requestStop();
}
void CoreSession::requestTare() {
  impl_->probe(kProbeNote, impl_->nowMs, 0.0f, 3.0f, kNoteRequest);
  impl_->ctl.requestTare();
}
void CoreSession::acknowledge() {
  impl_->probe(kProbeNote, impl_->nowMs, 0.0f, 4.0f, kNoteRequest);
  impl_->ctl.acknowledge();
  // The simulated owner takes the cup away a moment later (Rig::nextShot).
  if (impl_->simulated) impl_->emptyCupAtMs = impl_->sim.nowMs + 100;
}

LiveView CoreSession::live(uint32_t nowMs) const {
  const LiveStatus st = impl_->ctl.status(nowMs);
  LiveView v;
  v.stateName = stateName(st.state);
  v.state = static_cast<uint8_t>(st.state);
  v.fault = isFault(st.state);
  v.weightG = st.weightG;
  v.flowGS = st.flowGS;
  v.shotTimeS = st.shotTimeS;
  v.targetG = st.targetWeightG;
  v.scaleConnected = st.scaleConnected;
  v.botConnected = st.botConnected;
  v.weightFresh = st.weightFresh;
  v.manualStopNeeded = st.manualStopNeeded;
  v.botWanted = impl_->ctl.actuatorNeeded();
  v.scaleLinkLost = impl_->scaleLost;
  v.ticksPaused = impl_->paused;
  v.completedShots = impl_->completed;
  return v;
}

ShotSummary CoreSession::lastShot() const { return impl_->last; }

static RecipeDef *activeRecipe(Library &lib) {
  if (lib.presetCount == 0) return nullptr;
  const uint8_t index = lib.activePreset < lib.presetCount ? lib.activePreset : 0;
  const uint16_t id = lib.presets[index].recipeId;
  for (size_t i = 0; i < lib.recipeCount; ++i) {
    if (lib.recipes[i].id == id) return &lib.recipes[i];
  }
  return nullptr;
}

void CoreSession::setRecipe(float targetG, float doseG) {
  RecipeDef *r = activeRecipe(impl_->library);
  if (!r) return;
  r->useRatio = false;
  r->targetWeightG = targetG < MIN_TARGET_G ? MIN_TARGET_G : (targetG > MAX_TARGET_G ? MAX_TARGET_G : targetG);
  r->doseG = doseG < MIN_DOSE_G ? MIN_DOSE_G : (doseG > MAX_DOSE_G ? MAX_DOSE_G : doseG);
}
float CoreSession::recipeTargetG() const {
  const RecipeDef *r = activeRecipe(impl_->library);
  return r ? r->targetWeightG : 0.0f;
}
float CoreSession::recipeDoseG() const {
  const RecipeDef *r = activeRecipe(impl_->library);
  return r ? r->doseG : 0.0f;
}

void CoreSession::useSimulator(bool simulated) {
  CoreSessionImpl &s = *impl_;
  s.simulated = simulated;
  if (simulated) {  // the simulated Lunar sends through the link: give it its driver back
    s.driverSelect("acaia");
    s.scaleLink.start(s.nowMs);
    s.simStarted = false;
  }
}
bool CoreSession::simulated() const { return impl_->simulated; }

void CoreSession::setFault(uint8_t fault, bool on) {
  brewsim::Sim &sim = impl_->sim;
  switch (fault) {
    case kFaultLostAck:
      sim.unconfirmedPresses = on ? 1 : 0;
      break;
    case kFaultMissedStop:
      sim.missNextStop = on;
      break;
    case kFaultScaleSilent:
      sim.scaleSilent = on;
      break;
    default: return;
  }
  impl_->probe(kProbeNote, impl_->nowMs, on ? 1.0f : 0.0f, fault, kNoteFault);
}

bool CoreSession::faultActive(uint8_t fault) const {
  const brewsim::Sim &sim = impl_->sim;
  switch (fault) {
    case kFaultLostAck: return sim.unconfirmedPresses > 0;
    case kFaultMissedStop: return sim.missNextStop;
    case kFaultScaleSilent: return sim.scaleSilent;
    default: return false;
  }
}

void CoreSession::pauseTicks(uint32_t forMs, uint32_t nowMs) {
  impl_->paused = true;
  impl_->pausedSinceMs = nowMs;
  impl_->pausedUntilMs = nowMs + forMs;
  impl_->probe(kProbeNote, nowMs, static_cast<float>(forMs) / 1000.0f, 0.0f, kNotePaused);
}

uint32_t CoreSession::probeTotal() const { return impl_->probeSeq; }

ProbeEntry CoreSession::probeAt(uint32_t seq) const {
  ProbeEntry none;
  if (seq == 0 || seq > impl_->probeSeq || impl_->probeSeq - seq >= kProbeCapacity) return none;
  const ProbeEntry &e = impl_->ring[(seq - 1) % kProbeCapacity];
  return e.seq == seq ? e : none;
}

uint8_t CoreSession::pendingCommands() const { return impl_->commands.size(); }
BridgeCommand CoreSession::popCommand() { return impl_->commands.pop(); }
uint32_t CoreSession::droppedCommands() const { return impl_->commands.dropped; }

void CoreSession::scaleLink(bool up, const char *driverId, uint32_t nowMs) {
  CoreSessionImpl &s = *impl_;
  s.nowMs = nowMs;
  if (!up) {
    s.scaleUp = false;
    s.scaleLink.close();
    return;
  }
  if (!s.driverSelect(driverId)) return;
  s.scaleUp = true;
  s.scaleLost = false;
  s.scaleLink.start(nowMs);
  const ScaleProtocol &protocol = s.scaleLink.driver()->protocol();
  for (size_t i = 0; i < protocol.initWriteCount; ++i) {
    s.enqueue(kCmdWriteScale, protocol.initWrites[i].data,
              static_cast<size_t>(protocol.initWrites[i].length), nowMs);
  }
}

void CoreSession::onScaleBytes(const uint8_t *data, size_t length, uint32_t nowMs) {
  CoreSessionImpl &s = *impl_;
  if (s.simulated || s.paused) return;  // a suspended app hears nothing
  s.nowMs = nowMs;
  s.scaleLink.onNotify(data, length, nowMs);  // the sample sink calls onSample() on the core
  s.tickReal(nowMs);                          // and the state machine moves at once
}

void CoreSession::botLink(bool up) {
  impl_->botUp = up;
  if (up) impl_->botLink.connected(impl_->botLink.driverAt(0));  // the SwitchBot Bot
}

void CoreSession::onBotBytes(const uint8_t *data, size_t length, uint32_t nowMs) {
  CoreSessionImpl &s = *impl_;
  if (s.simulated || s.paused) return;
  s.nowMs = nowMs;
  s.botLink.onNotify(data, length);
}
