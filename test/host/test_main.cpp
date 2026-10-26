// Host tests for everything under src/core plus the vendored scale protocols.
// A simulated Breville + Lunar + Bot is driven through complete shots, and the
// same shot is run over the wire format of every supported scale.
// Build and run: see test/host/run.sh

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ScaleProtocol.h"
#include "core/AcaiaStream.h"
#include "core/BbwAlgorithms.h"
#include "core/BrewController.h"
#include "core/Community.h"
#include "core/FlowFeatures.h"
#include "core/GrindAdvisor.h"
#include "core/Library.h"
#include "core/Maintenance.h"
#include "core/PowerControl.h"
#include "core/PusherLink.h"
#include "core/ScaleLink.h"
#include "core/SettingFormat.h"
#include "core/SettingsUtil.h"
#include "core/Share.h"
#include "core/SimilarCoffee.h"
#include "core/Subscriptions.h"
#include "core/ShotContext.h"
#include "core/ShotData.h"
#include "core/SwitchBotProtocol.h"
#include "core/TasteProfile.h"
#include "core/Telemetry.h"

using namespace bbw;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    ++g_checks;                                                            \
    if (!(cond)) {                                                         \
      ++g_failures;                                                        \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
    }                                                                      \
  } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(fabs(static_cast<double>(a) - static_cast<double>(b)) <= (tol))

// ------------------------------------------------------------- simulation --

// A reading as the scale with this driver id sends it. Returns the length.
static size_t wireFrame(const char *id, double weightG, uint32_t timerMs, uint8_t *out);

struct Sim {
  // World
  uint32_t nowMs = 1000;
  bool machineRunning = false;
  uint32_t machineStartedMs = 0;
  uint32_t machineStoppedMs = 0;
  double trueWeightG = 0.0;   // what is physically on the pan
  double tareOffsetG = 0.0;
  double flowGS = 1.6;        // steady espresso flow
  uint32_t preinfusionMs = 6000;
  uint32_t machineTimeoutMs = 0;   // the machine's own time-out; 0 = none
  double dripGS = 1.2;        // decays to zero over dripMs after a stop
  uint32_t stopDelayMs = 0;   // full flow goes on this long after a stop press
  uint32_t dripMs = 1200;
  // Fault injection
  bool scaleLink = true;
  bool scaleSilent = false;
  bool scaleIgnoresTare = false;
  // The Bot link exists only while the controller asks for it, as on the
  // real device: it comes up botConnectDelayMs after the request.
  bool botLink = false;
  bool botAvailable = true;
  uint32_t botConnectDelayMs = 1500;
  uint32_t botNeededSinceMs = 0;
  // Set: readings travel as this link's scale sends them, bytes and all, and
  // the scale only does what that scale has a command for.
  ScaleLink *wire = nullptr;
  bool wireHas(ScaleOp op) const {
    uint8_t command[SCALE_MAX_COMMAND_LENGTH];
    int length = 0;
    return !wire || (wire->encode(op, command, &length) && length > 0);
  }
  int botConnects = 0;
  int ignorePresses = 0;        // machine does not react, Bot still says OK
  int unconfirmedPresses = 0;   // machine reacts, acknowledgement is lost
  uint32_t ackDelayMs = 150;
  uint32_t samplePeriodMs = 100;
  // Bookkeeping
  int presses = 0;
  uint32_t lastHoldMs = 0;
  std::vector<uint32_t> holds, pressTimes;   // of the presses that were made
  int tares = 0;
  int beeps = 0;              // times the scale was asked to make a sound
  // Other machines: a start button that only starts (the stop is elsewhere),
  // and a button that brews while the arm holds it down.
  bool startOnlyButton = false;
  bool armDown = false;
  int holdsBegun = 0, releases = 0, stopPresses = 0;
  uint32_t pressAtMs = 0;
  bool pressBusy = false;
  PressResult pressResult = PressResult::IDLE;
  bool pressUnconfirmed = false;
  uint32_t lastSampleMs = 0;
  uint32_t sequence = 0;
  uint32_t pendingTareAtMs = 0;
  // The scale's own timer, as a 2021+ Lunar reports it with every reading.
  bool scaleSendsTimer = true;
  bool timerRunning = false;
  uint32_t timerStartMs = 0;
  uint32_t timerHeldMs = 0;
  uint32_t timerCommandDelayMs = 60;  // BLE write on its way to the scale
  uint32_t timerMs() const {
    if (!timerRunning || nowMs < timerStartMs) return timerHeldMs;
    return timerHeldMs + (nowMs - timerStartMs);
  }

  bool tare() {
    ++tares;
    if (!scaleIgnoresTare) pendingTareAtMs = nowMs + 150;
    return true;
  }

  bool beginPress(uint32_t holdMs = 0) {
    lastHoldMs = holdMs;
    if (!botLink || pressBusy) return false;
    ++presses;
    holds.push_back(holdMs);
    pressTimes.push_back(nowMs);
    pressAtMs = nowMs;
    pressBusy = true;
    pressResult = PressResult::BUSY;
    pressUnconfirmed = false;
    if (ignorePresses > 0) {
      --ignorePresses;
    } else if (startOnlyButton) {
      if (!machineRunning) {
        machineRunning = true;
        machineStartedMs = nowMs;
      }
    } else {
      machineRunning = !machineRunning;
      if (machineRunning) machineStartedMs = nowMs; else machineStoppedMs = nowMs;
    }
    if (unconfirmedPresses > 0) {
      --unconfirmedPresses;
      pressUnconfirmed = true;
    }
    return true;
  }
  PressResult result() const { return pressResult; }
  uint32_t pressSentAtMs() const { return pressAtMs; }
  void clearResult() { if (pressResult != PressResult::BUSY) pressResult = PressResult::IDLE; }

  double flowNow() const {
    if (machineRunning) {
      return nowMs - machineStartedMs >= preinfusionMs ? flowGS : 0.0;
    }
    if (machineStoppedMs != 0 && machineStoppedMs - machineStartedMs >= preinfusionMs) {
      const uint32_t after = nowMs - machineStoppedMs;
      if (after < stopDelayMs) return flowGS;  // the pump has not stopped yet
      if (after - stopDelayMs < dripMs) {
        return dripGS * (1.0 - static_cast<double>(after - stopDelayMs) / dripMs);
      }
    }
    return 0.0;
  }
};

// Adapters: the simulated world behind the two controller interfaces.
struct ScalePort : IScale {
  Sim &sim;
  explicit ScalePort(Sim &s) : sim(s) {}
  bool connected() const override { return sim.scaleLink; }
  bool tare() override { return sim.wireHas(ScaleOp::Tare) && sim.tare(); }
  bool beep() override {
    ++sim.beeps;
    return true;
  }
  bool startTimer() override {
    if (!sim.wireHas(ScaleOp::StartTimer)) return false;
    sim.timerRunning = true;
    sim.timerStartMs = sim.nowMs + sim.timerCommandDelayMs;
    return true;
  }
  bool stopTimer() override {
    if (!sim.wireHas(ScaleOp::StopTimer)) return false;
    sim.timerHeldMs = sim.timerMs();
    sim.timerRunning = false;
    return true;
  }
  bool resetTimer() override {
    if (!sim.wireHas(ScaleOp::ResetTimer)) return false;
    sim.timerRunning = false;
    sim.timerHeldMs = 0;
    return true;
  }
};
struct BotPort : IActuator {
  Sim &sim;
  explicit BotPort(Sim &s) : sim(s) {}
  bool connected() const override { return sim.botLink; }
  bool beginPress(uint32_t holdMs = 0) override { return sim.beginPress(holdMs); }
  PressResult result() const override { return sim.result(); }
  uint32_t pressSentAtMs() const override { return sim.pressSentAtMs(); }
  void clearResult() override { sim.clearResult(); }
  // A button that brews while it is held: the machine runs while the arm is down.
  bool beginHold() override {
    if (!sim.botLink || sim.pressBusy || sim.armDown) return false;
    ++sim.holdsBegun;
    sim.pressAtMs = sim.nowMs;
    sim.armDown = true;
    sim.pressResult = PressResult::BUSY;
    sim.machineRunning = true;
    sim.machineStartedMs = sim.nowMs;
    return true;
  }
  bool holding() const override { return sim.armDown && sim.nowMs - sim.pressAtMs >= sim.ackDelayMs; }
  bool release() override {
    if (!sim.botLink) return false;
    ++sim.releases;
    if (sim.armDown && sim.machineRunning) {
      sim.machineRunning = false;
      sim.machineStoppedMs = sim.nowMs;
    }
    sim.armDown = false;
    sim.pressAtMs = sim.nowMs;
    sim.pressBusy = true;            // the step loop reports OK after the acknowledgement delay
    sim.pressResult = PressResult::BUSY;
    sim.pressUnconfirmed = false;
    return true;
  }
};
// The bot on a separate stop button: it only ever stops the machine.
struct StopPort : IActuator {
  Sim &sim;
  BrewController *ctl = nullptr;
  uint32_t neededSinceMs = 0, sentAtMs = 0;
  PressResult now = PressResult::IDLE;
  explicit StopPort(Sim &s) : sim(s) {}
  // Asleep until wanted, like the brew bot.
  void step() {
    if (ctl && ctl->stopActuatorNeeded()) {
      if (neededSinceMs == 0) neededSinceMs = sim.nowMs;
    } else {
      neededSinceMs = 0;
    }
    if (now == PressResult::BUSY && sim.nowMs - sentAtMs >= sim.ackDelayMs) now = PressResult::OK;
  }
  bool connected() const override { return neededSinceMs != 0 && sim.nowMs - neededSinceMs >= 1500; }
  bool beginPress(uint32_t = 0) override {
    if (!connected() || now == PressResult::BUSY) return false;
    ++sim.stopPresses;
    sentAtMs = sim.nowMs;
    now = PressResult::BUSY;
    if (sim.machineRunning) {
      sim.machineRunning = false;
      sim.machineStoppedMs = sim.nowMs;
    }
    return true;
  }
  PressResult result() const override { return now; }
  uint32_t pressSentAtMs() const override { return sentAtMs; }
  void clearResult() override { if (now != PressResult::BUSY) now = PressResult::IDLE; }
};

struct Rig {
  Sim sim;
  ScalePort scale{sim};
  BotPort bot{sim};
  Settings settings = defaultSettings();
  Library library = defaultLibrary();
  BrewController ctl{scale, bot, settings, library};
  std::vector<State> trail;
  // Most tests are about the shot itself and start with the pan as it is.
  // The cup check has its own test.
  Rig() { settings.requireCup = false; }

  void step() {
    sim.nowMs += 10;
    if (sim.machineRunning && sim.machineTimeoutMs != 0 &&
        sim.nowMs - sim.machineStartedMs >= sim.machineTimeoutMs) {
      sim.machineRunning = false;   // stopped by the machine, not by a press
      sim.machineStoppedMs = sim.nowMs;
    }
    sim.trueWeightG += sim.flowNow() * 0.010;
    if (sim.pendingTareAtMs && sim.nowMs >= sim.pendingTareAtMs) {
      sim.tareOffsetG = sim.trueWeightG;
      sim.pendingTareAtMs = 0;
    }
    if (ctl.actuatorNeeded()) {
      if (sim.botNeededSinceMs == 0) sim.botNeededSinceMs = sim.nowMs;
      const bool up = sim.botAvailable &&
                      sim.nowMs - sim.botNeededSinceMs >= sim.botConnectDelayMs;
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
    if (sim.scaleLink && !sim.scaleSilent && sim.nowMs - sim.lastSampleMs >= sim.samplePeriodMs) {
      sim.lastSampleMs = sim.nowMs;
      if (sim.wire) {
        uint8_t frame[SCALE_MAX_FRAME];
        const size_t n = wireFrame(sim.wire->driver()->id(), sim.trueWeightG - sim.tareOffsetG,
                                   sim.timerMs(), frame);
        sim.wire->onNotify(frame, n, sim.nowMs);  // its sample sink calls ctl.onSample()
      } else {
        WeightSample s;
        // 0.1 g display resolution, like the Lunar.
        s.weightG = static_cast<float>(round((sim.trueWeightG - sim.tareOffsetG) * 10.0) / 10.0);
        s.atMs = sim.nowMs;
        s.sequence = ++sim.sequence;
        // Tenths of a second, as the scale sends it.
        if (sim.scaleSendsTimer) s.scaleTimerMs = static_cast<int32_t>(sim.timerMs() / 100 * 100);
        ctl.onSample(s);
      }
    }
    ctl.tick(sim.nowMs);
    if (trail.empty() || trail.back() != ctl.state()) trail.push_back(ctl.state());
  }
  void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 10) step(); }
  bool runUntil(State target, uint32_t maxMs) {
    for (uint32_t t = 0; t < maxMs; t += 10) {
      step();
      if (ctl.state() == target) return true;
    }
    return false;
  }
  bool visited(State s) const {
    for (State t : trail) if (t == s) return true;
    return false;
  }
  void toReady() {
    CHECK(runUntil(State::READY, 3000));
  }
  // Empties the cup and returns to READY for the next shot.
  void nextShot() {
    ctl.acknowledge();
    run(100);
    sim.trueWeightG = 0.0;
    sim.tareOffsetG = 0.0;
    sim.machineStoppedMs = 0;
    run(1500);
    trail.clear();
  }
};

// ------------------------------------------------------------------ tests --

static void acaiaChecksum(uint8_t *p, int length) {
  uint8_t even = 0, odd = 0;
  for (int i = 3, k = 0; i < length - 2; ++i, ++k) ((k & 1) ? odd : even) += p[i];
  p[length - 2] = even;
  p[length - 1] = odd;
}

static void testAcaiaProtocol() {
  printf("acaia protocol\n");
  const ScaleProtocol &p = kScaleProtocolAcaia;
  // Lunar 2021 13-byte weight event: ef dd 0c len 05 w_lo w_hi .. .. unit sign ck ck
  uint8_t pkt[13] = {0xef, 0xdd, 0x0c, 0x08, 0x05, 0x69, 0x01, 0x00, 0x00, 0x01, 0x00, 0, 0};
  acaiaChecksum(pkt, 13);
  float w = 0;
  CHECK(p.supportedPacketLength(13));
  CHECK(p.parseWeight(pkt, 13, &w));
  CHECK_NEAR(w, 36.1, 0.001);
  pkt[10] = 0x02;  // sign bit
  acaiaChecksum(pkt, 13);
  CHECK(p.parseWeight(pkt, 13, &w));
  CHECK_NEAR(w, -36.1, 0.001);
  pkt[5] ^= 0xff;  // corrupt without fixing the checksum
  CHECK(!p.parseWeight(pkt, 13, &w));

  uint8_t cmd[SCALE_MAX_COMMAND_LENGTH];
  int n = 0;
  CHECK(p.encodeCommand(ScaleOp::Tare, 0, cmd, &n));
  const uint8_t tare[6] = {0xef, 0xdd, 0x04, 0x00, 0x00, 0x00};
  CHECK(n == 6 && memcmp(cmd, tare, 6) == 0);
  CHECK(p.encodeCommand(ScaleOp::Heartbeat, 0, cmd, &n) && n == 7);
  CHECK(scaleNameIsCompatible("LUNAR-1A2B3C"));
  CHECK(!scaleNameIsCompatible("WoHand"));
  CHECK(p.features.heartbeatPeriodMs == 2750 && p.features.maxPacketSilenceMs == 5000);
}

// Frames below marked "capture" are verbatim from real scales (test fixtures
// of github.com/zweckj/aioacaia, MIT licence); the rest are built to the same
// layout.
static std::vector<uint8_t> hexBytes(const char *hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
    unsigned value = 0;
    sscanf(hex + i, "%2x", &value);
    out.push_back(static_cast<uint8_t>(value));
  }
  return out;
}

static std::vector<AcaiaEvent> feedHex(AcaiaStream &stream, const char *hex) {
  std::vector<AcaiaEvent> events;
  const std::vector<uint8_t> bytes = hexBytes(hex);
  stream.feed(bytes.data(), bytes.size(), [&events](const AcaiaEvent &e) { events.push_back(e); });
  return events;
}

static void testAcaiaStream() {
  printf("acaia 17-byte frames (Lunar 2021+, firmware 1.0.x)\n");
  // Capture, 2021+ Lunar: 0.0 g with the timer at 8.1 s.
  const char *running = "efdd0c0c05000000000100070008011c06";
  {
    AcaiaStream s;
    auto e = feedHex(s, running);
    CHECK(e.size() == 1 && e[0].hasWeight && e[0].hasTimer);
    CHECK_NEAR(e[0].weightG, 0.0, 0.001);
    CHECK(e[0].timerMs == 8100);
    CHECK(s.frames() == 1 && s.badFrames() == 0 && s.skippedBytes() == 0);
    // The vendored parser takes the tag byte for the minutes: 7 min 0.8 s.
    // This is why the stream decoder exists.
    const std::vector<uint8_t> raw = hexBytes(running);
    uint32_t vendored = 0;
    CHECK(kScaleProtocolAcaia.parseTimer(raw.data(), 17, &vendored) && vendored == 420800);
  }
  {
    // Capture: 175.9 g, timer idling at 0.2 s.
    AcaiaStream s;
    auto e = feedHex(s, "efdd0c0c05df060000010007000002f30d");
    CHECK(e.size() == 1 && e[0].hasWeight && e[0].hasTimer && e[0].timerMs == 200);
    CHECK_NEAR(e[0].weightG, 175.9, 0.001);
    // Same frame the way it also arrives: header first, the rest later.
    AcaiaStream split;
    CHECK(feedHex(split, "efdd0c").empty());
    e = feedHex(split, "0c05df060000010007000002f30d");
    CHECK(e.size() == 1 && e[0].timerMs == 200);
    CHECK_NEAR(e[0].weightG, 175.9, 0.001);
    CHECK(split.frames() == 1 && split.continuedFrames() == 1 && split.badFrames() == 0);
    // Junk in front, and two frames in one notification.
    AcaiaStream two;
    e = feedHex(two, "000102efdd0c0c05df060000010007000002f30d"
                     "efdd0c0c05000000000100070008011c06");
    CHECK(e.size() == 2 && e[1].timerMs == 8100 && two.skippedBytes() == 3);
  }
  {
    // A seconds value past 9 in the timer: the case the old offsets rejected.
    uint8_t f[17] = {0xef, 0xdd, 0x0c, 0x0c, 0x05, 0x69, 0x01, 0, 0, 0x01, 0x02,
                     0x07, 0x01, 0x1b, 0x04, 0, 0};
    acaiaChecksum(f, 17);
    AcaiaStream s;
    std::vector<AcaiaEvent> e;
    s.feed(f, 17, [&e](const AcaiaEvent &ev) { e.push_back(ev); });
    CHECK(e.size() == 1 && e[0].timerMs == 87400);   // 1 min 27.4 s
    CHECK_NEAR(e[0].weightG, -36.1, 0.001);          // sign bit
    CHECK(e[0].weightFlags == 0x02);
    // Corrupted: dropped, and the stream recovers on the next good frame.
    f[5] ^= 0xff;
    e.clear();
    s.feed(f, 17, [&e](const AcaiaEvent &ev) { e.push_back(ev); });
    CHECK(e.empty() && s.badFrames() == 1);
    f[5] ^= 0xff;
    s.feed(f, 17, [&e](const AcaiaEvent &ev) { e.push_back(ev); });
    CHECK(e.size() == 1 && e[0].timerMs == 87400);
  }
  {
    // 13-byte weight frame (no timer record): still a reading.
    uint8_t f[13] = {0xef, 0xdd, 0x0c, 0x08, 0x05, 0x69, 0x01, 0, 0, 0x01, 0x00, 0, 0};
    acaiaChecksum(f, 13);
    AcaiaStream s;
    std::vector<AcaiaEvent> e;
    s.feed(f, 13, [&e](const AcaiaEvent &ev) { e.push_back(ev); });
    CHECK(e.size() == 1 && e[0].hasWeight && !e[0].hasTimer);
    CHECK_NEAR(e[0].weightG, 36.1, 0.001);
  }
  {
    // Capture, 2021+ scale: stop key with weight 0 g and the timer at 10.8 s.
    uint8_t f[32] = {0xef, 0xdd, 0x0c, 14, 0x08};
    const std::vector<uint8_t> body = hexBytes("0a0500000000010107000a08");
    memcpy(f + 5, body.data(), body.size());
    acaiaChecksum(f, 19);
    AcaiaStream s;
    std::vector<AcaiaEvent> e;
    s.feed(f, 19, [&e](const AcaiaEvent &ev) { e.push_back(ev); });
    CHECK(e.size() == 1 && e[0].key == ACAIA_KEY_STOP && e[0].hasWeight && e[0].hasTimer);
    CHECK(e[0].timerMs == 10800);
    // Capture, 2021+ Lunar settings frame: battery 74 %, timer running.
    AcaiaStream t;
    auto st = feedHex(t, "efdd0809ca020101000101010ecc");
    CHECK(st.size() == 1 && st[0].hasStatus && st[0].batteryPercent == 74 && st[0].timerRunning);
    st = feedHex(t, "efdd08094a020101000101010e4c");
    CHECK(st.size() == 1 && st[0].batteryPercent == 74 && !st[0].timerRunning);
  }
}

// ------------------------------------------------- scale drivers and link --

// A 2021+ Lunar weight frame: ef dd 0c <len> 05 <weight, 4 bytes LE> <unit>
// <flags> [07 <minutes> <seconds> <tenths>] <cks> <cks>. timerTenths < 0: the
// 13-byte frame without a timer record.
static std::vector<uint8_t> lunarFrame(uint32_t raw, uint8_t unit, uint8_t flags, int timerTenths) {
  std::vector<uint8_t> f = {0xef, 0xdd, 0x0c, 0x08, 0x05, static_cast<uint8_t>(raw & 0xff),
                            static_cast<uint8_t>((raw >> 8) & 0xff), 0x00, 0x00, unit, flags};
  if (timerTenths >= 0) {
    f[3] = 0x0c;
    f.push_back(0x07);
    f.push_back(static_cast<uint8_t>((timerTenths / 600) % 100));
    f.push_back(static_cast<uint8_t>((timerTenths / 10) % 60));
    f.push_back(static_cast<uint8_t>(timerTenths % 10));
  }
  f.push_back(0);
  f.push_back(0);
  acaiaChecksum(f.data(), static_cast<int>(f.size()));
  return f;
}

static size_t wireFrame(const char *id, double weightG, uint32_t timerMs, uint8_t *out) {
  const long tenthsG = lround(fabs(weightG) * 10.0);
  const bool negative = weightG < 0 && tenthsG != 0;
  const int timerTenths = static_cast<int>(timerMs / 100);
  std::vector<uint8_t> f;
  if (strcmp(id, "acaia") == 0) {
    f = lunarFrame(static_cast<uint32_t>(tenthsG), 0x01, negative ? 0x02 : 0x00, timerTenths);
  } else if (strcmp(id, "acaia_legacy") == 0) {
    // 10 bytes: weight in 2..3, decimal exponent in 6, sign in 7.
    f = {0x0c, 0x05, static_cast<uint8_t>(tenthsG & 0xff), static_cast<uint8_t>((tenthsG >> 8) & 0xff),
         0x00, 0x00, 0x01, static_cast<uint8_t>(negative ? 0x02 : 0x00), 0x00, 0x00};
  } else if (strcmp(id, "felicita") == 0) {
    // 18 bytes of text: sign, six digits of 0.01 g, then mm ss t of the timer.
    char text[19];
    snprintf(text, sizeof(text), "\x01\x02%c%06ld%02d%02d%dg \r\n", negative ? '-' : '+', tenthsG * 10,
             (timerTenths / 600) % 100, (timerTenths / 10) % 60, timerTenths % 10);
    f.assign(text, text + 18);
  } else if (strcmp(id, "decent") == 0) {
    // 03 ce <weight, int16 BE, 0.1 g> 00 00 <xor>
    const int16_t raw = static_cast<int16_t>(negative ? -tenthsG : tenthsG);
    f = {0x03, 0xce, static_cast<uint8_t>((raw >> 8) & 0xff), static_cast<uint8_t>(raw & 0xff), 0x00, 0x00, 0x00};
    f[6] = scaleXorBytes(f.data(), 6);
  } else if (strcmp(id, "difluid") == 0) {
    // df df 03 00 <13> <weight, int32 BE, 0.1 g> ... <sum>
    const int32_t raw = static_cast<int32_t>(negative ? -tenthsG : tenthsG);
    f.assign(19, 0x00);
    f[0] = f[1] = 0xdf;
    f[2] = 0x03;
    f[4] = 13;
    f[5] = static_cast<uint8_t>((raw >> 24) & 0xff);
    f[6] = static_cast<uint8_t>((raw >> 16) & 0xff);
    f[7] = static_cast<uint8_t>((raw >> 8) & 0xff);
    f[8] = static_cast<uint8_t>(raw & 0xff);
    uint8_t sum = 0;
    for (size_t i = 0; i < 18; ++i) sum = static_cast<uint8_t>(sum + f[i]);
    f[18] = sum;
  } else if (strcmp(id, "atomheart_eclair") == 0) {
    // 'W' <weight, int32 LE, mg> <timer, uint32 LE, ms> <xor of those eight>
    const int32_t mg = static_cast<int32_t>((negative ? -tenthsG : tenthsG) * 100);
    f.assign(10, 0x00);
    f[0] = 'W';
    for (int k = 0; k < 4; ++k) {
      f[1 + k] = static_cast<uint8_t>((static_cast<uint32_t>(mg) >> (8 * k)) & 0xff);
      f[5 + k] = static_cast<uint8_t>((timerMs >> (8 * k)) & 0xff);
    }
    f[9] = scaleXorBytes(f.data() + 1, 8);
  } else if (strcmp(id, "eureka") == 0) {
    // aa 09 41 .. .. .. <sign> <weight, uint16 LE, 0.1 g> .. ..
    f = {0xaa, 0x09, 0x41, 0x00, 0x00, 0x00, static_cast<uint8_t>(negative ? 1 : 0),
         static_cast<uint8_t>(tenthsG & 0xff), static_cast<uint8_t>((tenthsG >> 8) & 0xff), 0x00, 0x00};
  } else if (strcmp(id, "myscale") == 0) {
    // 15 bytes: sign in the high nibble of byte 2, weight in mg, 28 bits BE, from byte 3
    const uint32_t mg = static_cast<uint32_t>(tenthsG * 100);
    f.assign(15, 0x00);
    f[2] = negative ? 0x80 : 0x00;
    f[3] = static_cast<uint8_t>((mg >> 24) & 0x0f);
    f[4] = static_cast<uint8_t>((mg >> 16) & 0xff);
    f[5] = static_cast<uint8_t>((mg >> 8) & 0xff);
    f[6] = static_cast<uint8_t>(mg & 0xff);
  } else if (strcmp(id, "varia") == 0) {
    // fa 01 03 <sign bit 4 | weight, 20 bits BE, 0.01 g> <xor of bytes 1..5>
    const uint32_t hundredths = static_cast<uint32_t>(tenthsG * 10);
    f = {0xfa, 0x01, 0x03, static_cast<uint8_t>(((hundredths >> 16) & 0x0f) | (negative ? 0x10 : 0)),
         static_cast<uint8_t>((hundredths >> 8) & 0xff), static_cast<uint8_t>(hundredths & 0xff), 0x00};
    f[6] = scaleXorBytes(f.data() + 1, 5);
  } else if (strcmp(id, "weighmybru") == 0) {
    // 20 bytes: 03 0b ... <sign character> <weight, 24 bits BE, 0.01 g> ... <xor of the first 19>
    const uint32_t hundredths = static_cast<uint32_t>(tenthsG * 10);
    f.assign(20, 0x00);
    f[0] = 0x03;
    f[1] = 0x0b;
    f[6] = negative ? '-' : '+';
    f[7] = static_cast<uint8_t>((hundredths >> 16) & 0xff);
    f[8] = static_cast<uint8_t>((hundredths >> 8) & 0xff);
    f[9] = static_cast<uint8_t>(hundredths & 0xff);
    f[19] = scaleXorBytes(f.data(), 19);
  }
  memcpy(out, f.data(), f.size());
  return f.size();
}

// Whether this scale's readings carry its timer. The Varia reports its timer,
// but in frames of its own, so it is not what the screen counts the shot by.
static bool timerWithReadings(const ScaleDriver *d) {
  return d->has(SCALE_CAP_TIMER_WITH_READINGS);
}

static void collectSample(void *context, const WeightSample &sample) {
  static_cast<std::vector<WeightSample> *>(context)->push_back(sample);
}

static ScaleDriver *driverById(ScaleLink &link, const char *id) {
  for (size_t i = 0; i < link.driverCount(); ++i) {
    if (strcmp(link.driverAt(i)->id(), id) == 0) return link.driverAt(i);
  }
  return nullptr;
}

static void testScaleDrivers() {
  printf("scale drivers: registry, capabilities, wire formats\n");
  ScaleLink link;
  // One driver per registered protocol, tried in the registry's order, and
  // every one of them is at least a scale: weight stream plus tare.
  CHECK(link.driverCount() == scaleProtocolCount());
  for (size_t i = 0; i < link.driverCount() && i < scaleProtocolCount(); ++i) {
    CHECK(&link.driverAt(i)->protocol() == scaleProtocolAt(i));
    CHECK(scaleCapsMeetBaseline(link.driverAt(i)->caps()));
    uint8_t command[SCALE_MAX_COMMAND_LENGTH];
    int n = 0;
    CHECK(link.driverAt(i)->encode(ScaleOp::Tare, command, &n) && n > 0);
  }
  CHECK(link.driverAt(link.driverCount()) == nullptr);
  CHECK(!scaleCapsMeetBaseline(SCALE_CAP_WEIGHT));

  // The Lunar keeps everything it has.
  const uint32_t all = SCALE_CAP_WEIGHT | SCALE_CAP_TARE | SCALE_CAP_TIMER_CONTROL |
                       SCALE_CAP_TIMER_READBACK | SCALE_CAP_BATTERY | SCALE_CAP_BUTTONS |
                       SCALE_CAP_TIMER_STATE | SCALE_CAP_WEIGHT_FLAGS | SCALE_CAP_TIMER_WITH_READINGS;
  CHECK(driverById(link, "acaia")->caps() == all);
  CHECK(driverById(link, "acaia_legacy")->caps() ==
        (SCALE_CAP_WEIGHT | SCALE_CAP_TARE | SCALE_CAP_TIMER_CONTROL));
  CHECK(driverById(link, "felicita")->caps() ==
        (SCALE_CAP_WEIGHT | SCALE_CAP_TARE | SCALE_CAP_TIMER_CONTROL | SCALE_CAP_TIMER_READBACK |
         SCALE_CAP_TIMER_WITH_READINGS));
  CHECK(driverById(link, "decent")->caps() == SCALE_CAPS_BASELINE);
  CHECK(driverById(link, "difluid")->caps() == SCALE_CAPS_BASELINE);
  CHECK(driverById(link, "myscale")->caps() == SCALE_CAPS_BASELINE);
  CHECK(driverById(link, "weighmybru")->caps() == SCALE_CAPS_BASELINE);
  CHECK(driverById(link, "atomheart_eclair")->caps() == driverById(link, "felicita")->caps());
  CHECK(driverById(link, "varia")->caps() ==
        (SCALE_CAP_WEIGHT | SCALE_CAP_TARE | SCALE_CAP_TIMER_CONTROL | SCALE_CAP_TIMER_READBACK));
  CHECK(driverById(link, "eureka")->caps() ==
        (SCALE_CAP_WEIGHT | SCALE_CAP_TARE | SCALE_CAP_TIMER_CONTROL));
  CHECK(scaleNameIsCompatible("LUNAR-1A2B3C") && scaleNameIsCompatible("FELICITA") &&
        scaleNameIsCompatible("Decent Scale") && scaleNameIsCompatible("Microbalance") &&
        scaleNameIsCompatible("ECLAIR-12") && scaleNameIsCompatible("CFS-9002") &&
        scaleNameIsCompatible("my_scale") && scaleNameIsCompatible("AKU MINI SCALE") &&
        scaleNameIsCompatible("WeighMyBru") && !scaleNameIsCompatible("WoHand"));

  // Which scale a connected device is: its characteristic pair, and where two
  // makes share one (Varia and Eureka: fff1/fff2), its advertised name.
  auto hasPairOf = [](const char *id) {
    return [id](const ScaleDriver &candidate) {
      if (strcmp(id, "fff1/fff2") == 0) {
        return strcmp(candidate.protocol().readUuid, "fff1") == 0 &&
               strcmp(candidate.protocol().writeUuid, "fff2") == 0;
      }
      return strcmp(candidate.id(), id) == 0;
    };
  };
  CHECK(link.identify("AKU MINI SCALE", hasPairOf("fff1/fff2")) == driverById(link, "varia"));
  CHECK(link.identify("CFS-9002", hasPairOf("fff1/fff2")) == driverById(link, "eureka"));
  CHECK(link.identify("LSJ-001", hasPairOf("fff1/fff2")) == driverById(link, "eureka"));
  CHECK(link.identify("", hasPairOf("fff1/fff2")) == nullptr);        // name not known: not guessed
  CHECK(link.identify("LUNAR-1A2B3C", hasPairOf("fff1/fff2")) == nullptr);
  // The others go by what the device has, whatever it is called (a paired
  // scale reconnects even if its advertisement came without a name).
  CHECK(link.identify("", hasPairOf("acaia")) == driverById(link, "acaia"));
  CHECK(link.identify("anything", hasPairOf("decent")) == driverById(link, "decent"));
  CHECK(link.identify("LUNAR-1A2B3C", [](const ScaleDriver &) { return false; }) == nullptr);

  std::vector<WeightSample> got;
  link.setSampleSink(collectSample, &got);
  uint8_t frame[SCALE_MAX_FRAME];
  uint8_t command[SCALE_MAX_COMMAND_LENGTH];
  int n = 0;
  for (size_t i = 0; i < link.driverCount(); ++i) {
    ScaleDriver *d = link.driverAt(i);
    got.clear();
    link.select(d);
    link.start(1000);
    CHECK(link.caps() == d->caps());
    size_t length = wireFrame(d->id(), 36.1, 87400, frame);
    link.onNotify(frame, length, 1100);
    length = wireFrame(d->id(), -2.5, 87500, frame);
    link.onNotify(frame, length, 1200);
    CHECK(got.size() == 2);
    if (got.size() != 2) continue;
    CHECK_NEAR(got[0].weightG, 36.1, 0.001);
    CHECK_NEAR(got[1].weightG, -2.5, 0.001);
    CHECK(got[0].atMs == 1100 && got[1].atMs == 1200 && got[1].sequence == got[0].sequence + 1);
    const bool timer = timerWithReadings(d);
    CHECK(got[0].scaleTimerMs == (timer ? 87400 : -1) && got[1].scaleTimerMs == (timer ? 87500 : -1));
    CHECK(got[1].flags == (d->has(SCALE_CAP_WEIGHT_FLAGS) ? 0x02 : 0x00));
    uint32_t timerMs = 0;
    CHECK(link.timer(1300, timerMs) == timer);
    ScaleDiag diag = link.diag();
    CHECK(diag.frames == 2 && diag.weightFrames == 2 && diag.dropped == 0 && diag.caps == d->caps());
    CHECK(diag.timerFrames == (timer ? 2u : 0u));
    CHECK(diag.lastFrameLength == length && memcmp(diag.lastFrame, frame, length) == 0);
    // A scale that has not said so has no battery, timer state or key.
    CHECK(diag.batteryPercent == -1 && diag.timerRunning == -1 && diag.lastKey == -1);
    // Damaged on the way: not a reading.
    frame[3] ^= 0x55;
    // No checksum in these two: damage what they do check.
    if (strcmp(d->id(), "acaia_legacy") == 0) frame[6] = 9;
    if (strcmp(d->id(), "eureka") == 0) frame[2] = 0x00;
    link.onNotify(frame, length, 1300);
    CHECK(got.size() == 2 && link.diag().dropped == 1);
    // Timer commands exist exactly where the capability says so.
    const bool control = d->has(SCALE_CAP_TIMER_CONTROL);
    CHECK(link.encode(ScaleOp::StartTimer, command, &n) == control);
    CHECK(link.encode(ScaleOp::StopTimer, command, &n) == control);
    CHECK(link.encode(ScaleOp::ResetTimer, command, &n) == control);
  }

  // The tare each make expects.
  link.select(driverById(link, "felicita"));
  CHECK(link.encode(ScaleOp::Tare, command, &n) && n == 1 && command[0] == 0x54);
  link.select(driverById(link, "decent"));
  const uint8_t decentTare[7] = {0x03, 0x0f, 0x00, 0x00, 0x00, 0x01, 0x0d};
  CHECK(link.encode(ScaleOp::Tare, command, &n) && n == 7 && memcmp(command, decentTare, 7) == 0);
  link.select(driverById(link, "difluid"));
  const uint8_t difluidTare[7] = {0xdf, 0xdf, 0x03, 0x02, 0x01, 0x01, 0xc5};
  CHECK(link.encode(ScaleOp::Tare, command, &n) && n == 7 && memcmp(command, difluidTare, 7) == 0);
  link.select(driverById(link, "atomheart_eclair"));
  CHECK(link.encode(ScaleOp::Tare, command, &n) && n == 3 && command[0] == 0x54);
  link.select(driverById(link, "eureka"));
  const uint8_t eurekaTare[6] = {0xaa, 0x02, 0x31, 0x31, 0x00, 0x00};
  CHECK(link.encode(ScaleOp::Tare, command, &n) && n == 6 && memcmp(command, eurekaTare, 6) == 0);
  link.select(driverById(link, "myscale"));
  CHECK(link.encode(ScaleOp::Tare, command, &n) && n == 20 && command[0] == 0xac && command[1] == 0x40);
  link.select(driverById(link, "weighmybru"));
  const uint8_t weighMyBruTare[6] = {0x03, 0x0a, 0x01, 0x01, 0x00, 0x09};
  CHECK(link.encode(ScaleOp::Tare, command, &n) && n == 6 && memcmp(command, weighMyBruTare, 6) == 0);
  link.select(driverById(link, "varia"));
  const uint8_t variaTare[5] = {0xfa, 0x82, 0x01, 0x01, 0x82};
  CHECK(link.encode(ScaleOp::Tare, command, &n) && n == 5 && memcmp(command, variaTare, 5) == 0);
  // The Varia's timer: a frame of its own, whole seconds. It is known to the
  // link, it is not a reading, and readings do not claim to carry it.
  got.clear();
  link.start(5000);
  uint8_t variaTimer[6] = {0xfa, 0x87, 0x02, 0x00, 0x1b, 0x00};  // 27 s
  variaTimer[5] = scaleXorBytes(variaTimer + 1, 4);
  link.onNotify(variaTimer, 6, 5100);
  uint32_t variaTimerMs = 0;
  CHECK(got.empty() && link.timer(5200, variaTimerMs) && variaTimerMs == 27000);
  size_t variaLength = wireFrame("varia", 12.3, 0, frame);
  link.onNotify(frame, variaLength, 5200);
  CHECK(got.size() == 1 && got[0].scaleTimerMs == -1 && link.diag().dropped == 0);
  CHECK_NEAR(got[0].weightG, 12.3, 0.001);
  // No connection: nothing is decoded, nothing can be sent.
  link.close();
  got.clear();
  link.onNotify(frame, 7, 2000);
  CHECK(got.empty() && link.caps() == 0 && !link.encode(ScaleOp::Tare, command, &n));
}

// The Lunar link exactly as it was before there were drivers: the body of
// AcaiaScale::onNotify / onEvent / accept / timer from ble/Ble.cpp, kept here
// as the reference. Whatever it made of a stream of notifications, the driver
// path must make the same of it, reading for reading and field for field.
struct LunarReference {
  AcaiaStream stream;
  std::vector<WeightSample> samples;
  uint32_t dropped = 0, weightFrames = 0, timerFrames = 0, sequence = 0;
  int battery = -1, timerRunning = -1, lastKey = -1;
  uint32_t timerMs = 0, timerAtMs = 0;
  bool timerSeen = false;

  void onNotify(const uint8_t *data, size_t length, uint32_t at) {
    const uint32_t before = stream.frames();
    stream.feed(data, length, [this, at](const AcaiaEvent &event) { onEvent(event, at); });
    if (stream.frames() == before && length >= 6) ++dropped;
  }
  void onEvent(const AcaiaEvent &event, uint32_t atMs) {
    if (event.hasStatus) {
      battery = event.batteryPercent;
      timerRunning = event.timerRunning ? 1 : 0;
    }
    if (event.key != ACAIA_KEY_NONE) {
      lastKey = event.key;
      if (event.key == ACAIA_KEY_START) timerRunning = 1;
      if (event.key == ACAIA_KEY_STOP || event.key == ACAIA_KEY_RESET) timerRunning = 0;
    }
    const bool hasWeight = event.hasWeight && fabsf(event.weightG) <= 5000.0f;
    if (hasWeight || event.hasTimer) {
      accept(hasWeight, event.weightG, event.weightFlags, event.hasTimer, event.timerMs, atMs);
    }
  }
  void accept(bool hasWeight, float weightG, uint8_t flags, bool hasTimer, uint32_t timer,
              uint32_t atMs) {
    if (hasTimer) {
      timerMs = timer;
      timerAtMs = atMs;
      timerSeen = true;
    }
    if (hasWeight) {
      ++weightFrames;
      if (hasTimer) ++timerFrames;
      WeightSample sample;
      sample.weightG = weightG;
      sample.atMs = atMs;
      sample.sequence = ++sequence;
      sample.scaleTimerMs = hasTimer ? static_cast<int32_t>(timer) : -1;
      sample.flags = flags;
      samples.push_back(sample);
    }
  }
  bool timer(uint32_t nowMs, uint32_t &out) const {
    if (!timerSeen || static_cast<uint32_t>(nowMs - timerAtMs) > 1500) return false;
    out = timerMs;
    return true;
  }
};

static void testScaleLinkKeepsLunarData() {
  printf("scale link: the Lunar's data is what it was before the drivers\n");
  LunarReference reference;
  ScaleLink link;
  std::vector<WeightSample> got;
  link.setSampleSink(collectSample, &got);
  link.select(driverById(link, "acaia"));
  link.start(1000);

  uint32_t seed = 20261011;
  auto rnd = [&seed](uint32_t below) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % below;
  };
  uint32_t at = 1000;
  int timerTenths = 0;
  size_t notifications = 0;
  auto send = [&](const std::vector<uint8_t> &bytes) {
    reference.onNotify(bytes.data(), bytes.size(), at);
    link.onNotify(bytes.data(), bytes.size(), at);
    ++notifications;
  };
  auto weightFrame = [&](bool withTimer) {
    // Mostly a cup filling; now and then a negative, a 0.01 g or an out-of-range reading.
    uint32_t raw = rnd(600);
    uint8_t unit = 0x01, flags = 0x00;
    const uint32_t kind = rnd(20);
    if (kind == 0) flags = 0x02;
    if (kind == 1) unit = 0x02;
    if (kind == 2) raw = 60000;            // 6000.0 g: not a reading, its timer still counts
    if (kind == 3) flags = static_cast<uint8_t>(rnd(256));
    return lunarFrame(raw, unit, flags, withTimer ? timerTenths : -1);
  };
  bool same = true;
  for (int step = 0; step < 4000; ++step) {
    at += 60 + rnd(90);
    if (rnd(3) != 0) ++timerTenths;
    const uint32_t kind = rnd(40);
    if (kind < 24) {
      send(weightFrame(true));
    } else if (kind < 28) {                // the same frame in two notifications: 3 + 14 bytes
      const std::vector<uint8_t> f = weightFrame(true);
      send(std::vector<uint8_t>(f.begin(), f.begin() + 3));
      at += 2;
      send(std::vector<uint8_t>(f.begin() + 3, f.end()));
    } else if (kind < 30) {                // older firmware: 13 bytes, no timer record
      send(weightFrame(false));
    } else if (kind < 32) {                // damaged on the way
      std::vector<uint8_t> f = weightFrame(true);
      f[5 + rnd(6)] ^= static_cast<uint8_t>(1 + rnd(255));
      send(f);
    } else if (kind < 34) {                // a key pressed on the scale, with weight and timer
      static const uint8_t keys[] = {0, 8, 9, 10, 3};
      std::vector<uint8_t> f = {0xef, 0xdd, 0x0c, 14, 0x08, keys[rnd(5)], 0x05,
                                static_cast<uint8_t>(rnd(256)), static_cast<uint8_t>(rnd(2)), 0, 0,
                                0x01, 0x00, 0x07, static_cast<uint8_t>((timerTenths / 600) % 100),
                                static_cast<uint8_t>((timerTenths / 10) % 60),
                                static_cast<uint8_t>(timerTenths % 10), 0, 0};
      acaiaChecksum(f.data(), static_cast<int>(f.size()));
      send(f);
    } else if (kind < 36) {                // settings frame: battery and timer state
      std::vector<uint8_t> f = {0xef, 0xdd, 0x08, 0x09,
                                static_cast<uint8_t>(rnd(101) | (rnd(2) ? 0x80 : 0)),
                                0x02, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0, 0};
      acaiaChecksum(f.data(), static_cast<int>(f.size()));
      send(f);
    } else if (kind < 37) {                // two frames in one notification
      std::vector<uint8_t> f = weightFrame(true);
      const std::vector<uint8_t> second = weightFrame(true);
      f.insert(f.end(), second.begin(), second.end());
      send(f);
    } else if (kind < 39) {                // bytes that are no frame at all
      std::vector<uint8_t> junk(1 + rnd(12));
      for (uint8_t &b : junk) b = static_cast<uint8_t>(rnd(256));
      send(junk);
    } else {                               // heartbeat reply: a valid frame with nothing in it
      std::vector<uint8_t> f = {0xef, 0xdd, 0x0c, 0x04, 0x0b, 0x00, 0x00, 0, 0};
      acaiaChecksum(f.data(), 9);
      send(f);
    }
    // After every notification: counters, state and the timer agree.
    const ScaleDiag d = link.diag();
    uint8_t last[AcaiaStream::MAX_FRAME];
    const size_t lastLength = reference.stream.lastFrame(last, sizeof(last));
    uint32_t timerA = 0, timerB = 0;
    const uint32_t ask = at + rnd(3000);
    const bool hasA = reference.timer(ask, timerA), hasB = link.timer(ask, timerB);
    same = same && got.size() == reference.samples.size() &&
           d.frames == reference.stream.frames() && d.badFrames == reference.stream.badFrames() &&
           d.splitFrames == reference.stream.continuedFrames() &&
           d.weightFrames == reference.weightFrames && d.timerFrames == reference.timerFrames &&
           d.dropped == reference.dropped && d.batteryPercent == reference.battery &&
           d.timerRunning == reference.timerRunning && d.lastKey == reference.lastKey &&
           d.lastFrameLength == lastLength && memcmp(d.lastFrame, last, lastLength) == 0 &&
           hasA == hasB && timerA == timerB;
    if (!same) {
      printf("  first difference after notification %u\n", static_cast<unsigned>(notifications));
      break;
    }
  }
  CHECK(same);
  // Every reading, every field, bit for bit.
  CHECK(got.size() == reference.samples.size() && got.size() > 3000);
  size_t different = 0, withTimer = 0, withFlags = 0;
  for (size_t i = 0; i < got.size() && i < reference.samples.size(); ++i) {
    const WeightSample &a = reference.samples[i], &b = got[i];
    if (memcmp(&a.weightG, &b.weightG, sizeof(float)) != 0 || a.atMs != b.atMs ||
        a.sequence != b.sequence || a.scaleTimerMs != b.scaleTimerMs || a.flags != b.flags) {
      ++different;
    }
    if (b.scaleTimerMs >= 0) ++withTimer;
    if (b.flags != 0) ++withFlags;
  }
  CHECK(different == 0);
  // The stream really exercised what it was meant to.
  const ScaleDiag d = link.diag();
  CHECK(withTimer > 2500 && withFlags > 100 && d.splitFrames > 200 && d.badFrames > 100 &&
        d.dropped > 100 && d.batteryPercent >= 0 && d.lastKey >= 0);

  // Legacy Lunar (2a80): still the vendored parser, flag byte not carried.
  got.clear();
  link.select(driverById(link, "acaia_legacy"));
  link.start(at);
  size_t expected = 0, wrong = 0;
  for (int step = 0; step < 500; ++step) {
    at += 100;
    uint8_t f[10] = {0x0c, 0x05, static_cast<uint8_t>(rnd(256)), static_cast<uint8_t>(rnd(40)), 0, 0,
                     static_cast<uint8_t>(rnd(6)), static_cast<uint8_t>(rnd(4)), 0, 0};
    float weight = 0.0f;
    const bool ok = kScaleProtocolAcaiaLegacy.parseWeight(f, 10, &weight);
    link.onNotify(f, 10, at);
    if (ok) {
      ++expected;
      if (got.size() != expected || memcmp(&got.back().weightG, &weight, sizeof(float)) != 0 ||
          got.back().atMs != at || got.back().scaleTimerMs != -1 || got.back().flags != 0) {
        ++wrong;
      }
    }
  }
  CHECK(wrong == 0 && got.size() == expected && expected > 200 && expected < 500);
  CHECK(link.diag().dropped == 500 - expected && link.diag().weightFrames == expected);
}

static void testScaleLinkService() {
  printf("scale link: heartbeat, idle poll, silence watchdog\n");
  ScaleLink link;
  std::vector<std::vector<uint8_t>> writes;
  auto write = [&writes](const uint8_t *data, size_t length) {
    writes.emplace_back(data, data + length);
  };
  uint8_t frame[SCALE_MAX_FRAME];
  const uint8_t acaiaHeartbeat[7] = {0xef, 0xdd, 0x00, 0x02, 0x00, 0x02, 0x00};

  // Lunar: a heartbeat right away, then every 2750 ms.
  link.select(driverById(link, "acaia"));
  link.start(10000);
  CHECK(link.service(10000, false, write));
  CHECK(writes.size() == 1 && writes[0].size() == 7 && memcmp(writes[0].data(), acaiaHeartbeat, 7) == 0);
  CHECK(link.service(12749, false, write) && writes.size() == 1);
  CHECK(link.service(12750, false, write) && writes.size() == 2);
  // Nothing from the scale: five seconds for the first packet, then it is dead.
  CHECK(link.service(15000, false, write));
  CHECK(!link.service(15001, false, write));
  CHECK(link.silenceLimitMs() == 5000);

  // Readings flowing: the settings (battery) are asked for 1.5 s after the
  // link came up, never during a shot, then every five minutes.
  writes.clear();
  link.start(20000);
  size_t n = wireFrame("acaia", 1.0, 0, frame);
  link.onNotify(frame, n, 20100);
  auto settingsRequests = [&writes]() {
    size_t count = 0;
    for (const auto &w : writes) {
      if (w.size() == 21 && w[0] == 0xef && w[1] == 0xdd && w[2] == 0x06) {
        bool zeros = true;
        for (size_t i = 3; i < 21; ++i) zeros = zeros && w[i] == 0;
        if (zeros) ++count;
      }
    }
    return count;
  };
  link.service(21499, false, write);
  CHECK(settingsRequests() == 0);
  link.service(21500, true, write);
  CHECK(settingsRequests() == 0);
  link.service(21500, false, write);
  CHECK(settingsRequests() == 1);
  link.onNotify(frame, n, 321000);
  link.service(321499, false, write);
  CHECK(settingsRequests() == 1);
  link.service(321500, false, write);
  CHECK(settingsRequests() == 2);
  // Silence after readings: the scale's own limit, counted from the last one.
  CHECK(link.service(326000, false, write));
  CHECK(!link.service(326001, false, write));
  // A settings frame alone is not a reading and does not feed the watchdog.
  const std::vector<uint8_t> settings = hexBytes("efdd0809ca020101000101010ecc");
  link.onNotify(settings.data(), settings.size(), 326000);
  CHECK(link.diag().batteryPercent == 74 && !link.service(326001, false, write));

  // Decent: its own heartbeat every 5 s, and nothing else is ever asked of it.
  writes.clear();
  link.select(driverById(link, "decent"));
  link.start(400000);
  n = wireFrame("decent", 1.0, 0, frame);
  for (uint32_t t = 400000; t <= 420000; t += 10) {
    if (t % 100 == 0) link.onNotify(frame, n, t);
    CHECK(link.service(t, false, write));
  }
  const uint8_t decentHeartbeat[7] = {0x03, 0x0a, 0x03, 0xff, 0xff, 0x00, 0x0a};
  CHECK(writes.size() == 5);
  for (const auto &w : writes) CHECK(w.size() == 7 && memcmp(w.data(), decentHeartbeat, 7) == 0);

  // Felicita: no heartbeat at all; silence still ends the link.
  writes.clear();
  link.select(driverById(link, "felicita"));
  link.start(500000);
  n = wireFrame("felicita", 1.0, 0, frame);
  link.onNotify(frame, n, 500100);
  CHECK(link.service(505100, false, write) && !link.service(505101, false, write));
  CHECK(writes.empty());
}

// The same shot, with the readings travelling as each scale sends them and
// the scale doing only what it has a command for. A scale with nothing but
// weight and tare brews the same shot; what it lacks is absent from the data,
// not invented.
static void testShotOverEveryScale() {
  printf("a whole shot over each scale's wire format\n");
  ScaleLink probe;
  for (size_t i = 0; i < probe.driverCount(); ++i) {
    Rig r;
    ScaleLink link;
    ScaleDriver *d = link.driverAt(i);
    link.setSampleSink(
        [](void *ctl, const WeightSample &s) { static_cast<BrewController *>(ctl)->onSample(s); },
        &r.ctl);
    link.select(d);
    link.start(r.sim.nowMs);
    r.sim.wire = &link;
    r.toReady();
    r.ctl.requestStart();
    const bool done = r.runUntil(State::SHOT_COMPLETE, 60000);
    CHECK(done);
    if (!done) {
      printf("  %s: stuck in %s\n", d->id(), stateName(r.ctl.state()));
      continue;
    }
    ShotRecord shot;
    CHECK(r.ctl.takeCompletedShot(shot));
    CHECK((shot.flags & SHOT_FLAG_FINAL_VALID) && (shot.flags & SHOT_FLAG_STOP_CONFIRMED));
    CHECK(r.sim.presses == 2 && !r.sim.machineRunning && r.sim.tares == 1);
    CHECK_NEAR(shot.finalWeightG, 36.0, 1.5);
    CHECK(shot.shotTimeS > 20 && shot.shotTimeS < 35);
    const ShotCurve &curve = r.ctl.curve();
    CHECK(curve.count > 200);
    size_t withTimer = 0, moving = 0;
    for (size_t k = 0; k < curve.count; ++k) {
      if (curve.timerDs[k] != CURVE_NO_TIMER) ++withTimer;
      if (k > 0 && curve.timerDs[k] != CURVE_NO_TIMER && curve.timerDs[k] > curve.timerDs[k - 1]) ++moving;
    }
    if (timerWithReadings(d)) {
      CHECK(withTimer == curve.count && moving > 150);   // its own timer, driven by the controller
    } else {
      CHECK(withTimer == 0);
    }
    CHECK(r.sim.timerRunning == false);
    CHECK(link.diag().dropped == 0 && link.diag().weightFrames >= curve.count);
  }
}

// ------------------------------------------- button pushers and power bot --

static void testTuyaCrypto() {
  printf("md5, aes-128-cbc, crc16 against the published vectors\n");
  auto hexOf = [](const uint8_t *data, size_t n) {
    std::string out;
    char two[3];
    for (size_t i = 0; i < n; ++i) {
      snprintf(two, sizeof(two), "%02x", data[i]);
      out += two;
    }
    return out;
  };
  uint8_t digest[16];
  // RFC 1321, appendix A.5
  CHECK(tuya::md5(reinterpret_cast<const uint8_t *>(""), 0, digest) &&
        hexOf(digest, 16) == "d41d8cd98f00b204e9800998ecf8427e");
  CHECK(tuya::md5(reinterpret_cast<const uint8_t *>("abc"), 3, digest) &&
        hexOf(digest, 16) == "900150983cd24fb0d6963f7d28e17f72");
  CHECK(tuya::md5(reinterpret_cast<const uint8_t *>("message digest"), 14, digest) &&
        hexOf(digest, 16) == "f96b697d7cb7938d525a2f31aaf161d0");
  const char *digits = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
  CHECK(tuya::md5(reinterpret_cast<const uint8_t *>(digits), 80, digest) &&  // two blocks
        hexOf(digest, 16) == "57edf4a22be3c955ac49da2e2107b67a");
  // FIPS 197, appendix C.1
  std::vector<uint8_t> key = hexBytes("000102030405060708090a0b0c0d0e0f");
  std::vector<uint8_t> block = hexBytes("00112233445566778899aabbccddeeff");
  tuya::Aes128 aes(key.data());
  aes.encryptBlock(block.data());
  CHECK(hexOf(block.data(), 16) == "69c4e0d86a7b0430d8cdb78070b4c55a");
  aes.decryptBlock(block.data());
  CHECK(hexOf(block.data(), 16) == "00112233445566778899aabbccddeeff");
  // NIST SP 800-38A, F.2.1 (CBC-AES128), first two blocks
  key = hexBytes("2b7e151628aed2a6abf7158809cf4f3c");
  const std::vector<uint8_t> iv = hexBytes("000102030405060708090a0b0c0d0e0f");
  std::vector<uint8_t> text = hexBytes("6bc1bee22e409f96e93d7e117393172a"
                                       "ae2d8a571e03ac9c9eb76fac45af8e51");
  tuya::Aes128 cbc(key.data());
  cbc.cbcEncrypt(iv.data(), text.data(), text.size());
  CHECK(hexOf(text.data(), 32) == "7649abac8119b246cee98e9b12e9197d"
                                  "5086cb9b507219ee95db113a917678b2");
  cbc.cbcDecrypt(iv.data(), text.data(), text.size());
  CHECK(hexOf(text.data(), 32) == "6bc1bee22e409f96e93d7e117393172a"
                                  "ae2d8a571e03ac9c9eb76fac45af8e51");
  // CRC-16/MODBUS check value
  CHECK(tuya::crc16(reinterpret_cast<const uint8_t *>("123456789"), 9) == 0x4B37);
}

struct WriteLog : PusherWriter {
  bool up = true;
  std::vector<std::vector<uint8_t>> writes;
  bool write(const uint8_t *data, size_t length) override {
    if (!up) return false;
    writes.emplace_back(data, data + length);
    return true;
  }
};

// The SwitchBot link exactly as it was before there were drivers: the body of
// SwitchBotBot::write / onNotify / beginPress / tick / finish from ble/Ble.cpp
// with millis() passed in, kept here as the reference. Whatever it did with a
// run of presses, answers, lost answers and dropped links, the driver path
// must do the same: the same bytes at the same time and the same results.
struct OldSwitchBot {
  enum class Phase : uint8_t { IDLE, WAIT_ACK, WAIT_DOWN_ACK, HOLD, WAIT_UP_ACK, SETTLE };
  enum class Expect : uint8_t { NONE, ACTION, INFO, CONFIG };
  const Settings *settings = nullptr;
  char password[24] = {};
  WriteLog out;
  bool ready = false;
  Phase phase = Phase::IDLE;
  PressResult result = PressResult::IDLE;
  uint32_t sentAtMs = 0, phaseSinceMs = 0, settleUntilMs = 0;
  bool downAcked = false, atomic = true;
  uint32_t holdMs = 0;
  bool needUp = false;
  uint32_t lastUpTryMs = 0;
  uint32_t upFirstMs = 0, upRetryAtMs = 0;
  bool upRetrying = false;
  Expect expect = Expect::NONE;
  bool ackPending = false;
  uint8_t ackStatus = 0, lastStatus = 0;
  switchbot::BasicInfo info;
  uint32_t lastInfoMs = 0;
  bool holdConfigured = false;

  void connect(uint32_t now) {
    holdConfigured = false;
    lastInfoMs = now - 120000;
    ready = true;
  }
  bool write(const uint8_t *data, size_t length, Expect e) {
    if (!ready) return false;
    ackPending = false;
    expect = e;
    return out.write(data, length);
  }
  bool sendAction(switchbot::Action action) {
    uint8_t buffer[switchbot::MAX_COMMAND_LEN];
    return write(buffer, switchbot::encodeAction(action, password, buffer), Expect::ACTION);
  }
  void onNotify(const uint8_t *data, size_t length) {
    if (length == 0) return;
    lastStatus = data[0];
    if (length >= 11) {
      const switchbot::BasicInfo i = switchbot::decodeBasicInfo(data, length);
      if (i.valid) info = i;
      if (expect == Expect::INFO) expect = Expect::NONE;
      return;
    }
    if (expect == Expect::ACTION) {
      ackStatus = data[0];
      ackPending = true;
    }
    expect = Expect::NONE;
  }
  void finish(PressResult r, uint32_t nowMs) {
    result = r;
    phase = Phase::SETTLE;
    uint32_t settle = settings->releaseSettleMs;
    if (atomic) settle += static_cast<uint32_t>(settings->botHoldSeconds) * 1000u;
    settleUntilMs = nowMs + settle;
  }
  bool beginPress(uint32_t hold, uint32_t now) {
    if (!ready || phase != Phase::IDLE || needUp) return false;
    const bool a = hold == 0 && settings->actuationMode == ActuationMode::ATOMIC_PRESS;
    atomic = a;
    holdMs = hold != 0 ? hold : settings->pressDurationMs;
    result = PressResult::BUSY;
    downAcked = false;
    sentAtMs = now;
    phaseSinceMs = now;
    if (!sendAction(a ? switchbot::Action::PRESS : switchbot::Action::DOWN)) {
      result = PressResult::NOT_SENT;
      return true;
    }
    if (!a) needUp = true;
    phase = a ? Phase::WAIT_ACK : Phase::WAIT_DOWN_ACK;
    return true;
  }
  void clearResult() { if (result != PressResult::BUSY) result = PressResult::IDLE; }
  bool busy() const { return phase != Phase::IDLE || needUp; }
  bool takeAck() {
    const bool was = ackPending;
    ackPending = false;
    return was;
  }
  void tick(uint32_t nowMs) {
    if (needUp && phase == Phase::IDLE && ready && static_cast<uint32_t>(nowMs - lastUpTryMs) >= 300) {
      lastUpTryMs = nowMs;
      if (sendAction(switchbot::Action::UP)) needUp = false;
    }
    switch (phase) {
      case Phase::IDLE:
        if (ready && result != PressResult::BUSY && static_cast<uint32_t>(nowMs - lastInfoMs) >= 120000) {
          lastInfoMs = nowMs;
          uint8_t buffer[switchbot::MAX_COMMAND_LEN];
          write(buffer, switchbot::encodeGetInfo(password, buffer), Expect::INFO);
        } else if (ready && info.valid && !holdConfigured &&
                   settings->actuationMode == ActuationMode::ATOMIC_PRESS &&
                   static_cast<uint32_t>(nowMs - lastInfoMs) >= 1000) {
          holdConfigured = true;
          if (info.holdSeconds != settings->botHoldSeconds) {
            uint8_t buffer[switchbot::MAX_COMMAND_LEN];
            write(buffer, switchbot::encodeSetHoldSeconds(settings->botHoldSeconds, buffer), Expect::CONFIG);
            lastInfoMs = nowMs - 120000 + 2000;
          }
        }
        break;
      case Phase::WAIT_ACK:
        if (takeAck()) {
          if (switchbot::actionAccepted(ackStatus)) finish(PressResult::OK, nowMs);
          else if (ackStatus == static_cast<uint8_t>(switchbot::Status::UNSUPPORTED)) finish(PressResult::UNCONFIRMED, nowMs);
          else finish(PressResult::NOT_SENT, nowMs);
        } else if (!ready || static_cast<uint32_t>(nowMs - phaseSinceMs) >= 3000) {
          finish(PressResult::UNCONFIRMED, nowMs);
        }
        break;
      case Phase::WAIT_DOWN_ACK:
        if (takeAck()) {
          if (switchbot::actionAccepted(ackStatus)) {
            downAcked = true;
          } else if (ackStatus != static_cast<uint8_t>(switchbot::Status::UNSUPPORTED)) {
            needUp = false;
            finish(PressResult::NOT_SENT, nowMs);
            break;
          }
          phase = Phase::HOLD;
          phaseSinceMs = nowMs;
        } else if (!ready) {
          finish(PressResult::UNCONFIRMED, nowMs);
          phase = Phase::IDLE;
        } else if (static_cast<uint32_t>(nowMs - phaseSinceMs) >= 1500) {
          phase = Phase::HOLD;
          phaseSinceMs = nowMs;
        }
        break;
      case Phase::HOLD:
        if (static_cast<uint32_t>(nowMs - phaseSinceMs) >= holdMs) {
          lastUpTryMs = nowMs;
          if (ready && sendAction(switchbot::Action::UP)) {
            needUp = false;
            upFirstMs = nowMs;
            upRetrying = false;
            phase = Phase::WAIT_UP_ACK;
            phaseSinceMs = nowMs;
          } else {
            finish(PressResult::UNCONFIRMED, nowMs);
            phase = Phase::IDLE;
          }
        }
        break;
      case Phase::WAIT_UP_ACK:
        if (takeAck()) {
          const bool upOk = switchbot::actionAccepted(ackStatus);
          const bool refused = !upOk && ackStatus != static_cast<uint8_t>(switchbot::Status::UNSUPPORTED);
          if (refused && static_cast<uint32_t>(nowMs - upFirstMs) < 6000) {
            upRetrying = true;
            upRetryAtMs = nowMs + 300;
          } else {
            if (refused) needUp = true;
            finish(downAcked && upOk ? PressResult::OK : PressResult::UNCONFIRMED, nowMs);
          }
        } else if (upRetrying) {
          if (static_cast<int32_t>(nowMs - upRetryAtMs) >= 0) {
            upRetrying = false;
            lastUpTryMs = nowMs;
            if (ready && sendAction(switchbot::Action::UP)) {
              phaseSinceMs = nowMs;
            } else {
              needUp = true;
              finish(PressResult::UNCONFIRMED, nowMs);
            }
          }
        } else if (!ready || static_cast<uint32_t>(nowMs - phaseSinceMs) >= 1500) {
          needUp = true;
          finish(PressResult::UNCONFIRMED, nowMs);
        }
        break;
      case Phase::SETTLE:
        if (static_cast<int32_t>(nowMs - settleUntilMs) >= 0) phase = Phase::IDLE;
        break;
    }
  }
};

static void testPusherLinkKeepsSwitchBot() {
  printf("pusher link: the SwitchBot behaves as it did before the drivers\n");
  uint32_t seed = 20261012;
  auto rnd = [&seed](uint32_t below) {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % below;
  };
  size_t totalWrites = 0, presses = 0, results[5] = {};
  bool same = true;
  for (int run = 0; run < 40 && same; ++run) {
    Settings settings = defaultSettings();
    settings.actuationMode = run % 2 ? ActuationMode::DOWN_UP : ActuationMode::ATOMIC_PRESS;
    settings.botHoldSeconds = static_cast<uint8_t>(run % 3);
    settings.pressDurationMs = static_cast<uint16_t>(100 + 100 * (run % 4));
    const char *password = run % 5 == 0 ? "secret" : "";
    OldSwitchBot old;
    old.settings = &settings;
    strcpy(old.password, password);
    PusherLink link;
    WriteLog out;
    PusherSecrets secrets;
    strcpy(secrets.password, password);
    link.begin("bot", &settings, &out);
    link.setSecrets(secrets);
    PusherDriver *switchBot = link.driverAt(0);
    struct Reply { uint32_t at; std::vector<uint8_t> bytes; };
    std::vector<Reply> replies;
    int holdOnBot = run % 2;  // what the Bot has stored
    bool up = false;
    PressResult lastResult = PressResult::IDLE;
    for (uint32_t now = 1000; now < 1000 + 120000 && same; now += 10) {
      // The link comes and goes.
      // (The driver path takes up a new connection on its next tick, at most
      // 10 ms later, so no press is asked for in the tick it connects in.)
      bool justConnected = false;
      if (!up && rnd(40) == 0) {
        up = justConnected = true;
        old.connect(now);
        link.connected(switchBot);
      } else if (up && rnd(1500) == 0) {
        up = false;
        old.ready = false;
        replies.clear();
      }
      old.out.up = out.up = up;
      // What the Bot answered, to both.
      for (size_t i = 0; i < replies.size();) {
        if (replies[i].at <= now) {
          old.onNotify(replies[i].bytes.data(), replies[i].bytes.size());
          link.onNotify(replies[i].bytes.data(), replies[i].bytes.size());
          replies.erase(replies.begin() + i);
        } else {
          ++i;
        }
      }
      const size_t before = old.out.writes.size();
      // What the controller asks.
      if (!justConnected && rnd(300) == 0) {
        const uint32_t hold = rnd(3) == 0 ? 200 + rnd(800) : 0;
        const bool a = old.beginPress(hold, now), b = link.beginPress(hold, now, up);
        same = same && a == b;
        if (a) ++presses;
      }
      if (rnd(200) == 0) {
        old.clearResult();
        link.clearResult();
      }
      old.tick(now);
      link.tick(now, up);
      // The same bytes left, at the same tick.
      same = same && old.out.writes == out.writes;
      // The Bot answers what was written this tick, now and then late, wrongly or not at all.
      for (size_t i = before; i < old.out.writes.size(); ++i) {
        const std::vector<uint8_t> &w = old.out.writes[i];
        const size_t at = (w[1] & 0xf0) == 0x10 ? 6 : 2;
        const uint8_t command = w[1] & 0x0f;
        if (rnd(12) == 0) continue;  // lost
        const uint32_t when = now + 10 + 10 * rnd(rnd(6) == 0 ? 400 : 30);
        if (command == 0x02) {
          std::vector<uint8_t> info(13, 0);
          info[0] = 0x01;
          info[1] = 87;
          info[2] = 63;
          info[9] = 0;
          info[10] = static_cast<uint8_t>(holdOnBot);
          replies.push_back({when, info});
        } else if (w[1] == 0x0f) {
          holdOnBot = w[3];
          replies.push_back({when, {0x01}});
        } else if (command == 0x01 && at < w.size()) {
          static const uint8_t statuses[] = {1, 1, 1, 1, 1, 1, 1, 3, 5, 6, 9};
          replies.push_back({when, {statuses[rnd(sizeof(statuses))]}});
        }
      }
      const PusherInfo info = link.info();
      same = same && old.result == link.result() && old.sentAtMs == link.pressSentAtMs() &&
             old.busy() == link.busy() && old.lastStatus == link.lastStatus() &&
             old.info.valid == info.valid && old.info.batteryPercent == info.batteryPercent &&
             old.info.holdSeconds == info.holdSeconds && old.info.switchMode == info.switchMode &&
             old.info.firmwareTenths == info.firmwareTenths && old.ready == link.ready(up);
      if (!same) printf("  first difference in run %d at %u ms\n", run, static_cast<unsigned>(now));
      if (link.result() != lastResult) {
        lastResult = link.result();
        ++results[static_cast<int>(lastResult)];
      }
    }
    totalWrites += out.writes.size();
  }
  CHECK(same);
  // The runs really went through every kind of ending.
  CHECK(presses > 400 && totalWrites > 1000);
  printf("  %u presses: %u ok, %u not sent, %u unconfirmed\n", static_cast<unsigned>(presses),
         static_cast<unsigned>(results[static_cast<int>(PressResult::OK)]),
         static_cast<unsigned>(results[static_cast<int>(PressResult::NOT_SENT)]),
         static_cast<unsigned>(results[static_cast<int>(PressResult::UNCONFIRMED)]));
  CHECK(results[static_cast<int>(PressResult::OK)] > 50 &&
        results[static_cast<int>(PressResult::NOT_SENT)] > 10 &&
        results[static_cast<int>(PressResult::UNCONFIRMED)] > 10);
}

// A Tuya Fingerbot as the driver's description has it: answers device
// information under the login key, pairing and datapoint writes under the
// session key, and reports its datapoints.
struct TuyaDevice {
  const char *uuid = "a1b2c3d4e5f60718";
  const char *deviceId = "bf0123456789abcdefghij";
  const char *localKey = "0123456789abcdef";
  uint8_t srand[6] = {9, 8, 7, 6, 5, 4};
  uint8_t loginKey[16], sessionKey[16];
  tuya::Assembler assembler;
  uint32_t seq = 100;
  int presses = 0, mode = 7, holdSeconds = 0, acksOfReports = 0;
  bool refuseWrites = false;
  std::vector<std::vector<uint8_t>> out;  // notifications to send

  TuyaDevice() {
    tuya::md5(reinterpret_cast<const uint8_t *>(localKey), 6, loginKey);
    uint8_t seedBytes[12];
    memcpy(seedBytes, localKey, 6);
    memcpy(seedBytes + 6, srand, 6);
    tuya::md5(seedBytes, 12, sessionKey);
  }
  struct Collect : PusherWriter {
    std::vector<std::vector<uint8_t>> *to;
    bool write(const uint8_t *d, size_t n) override { to->emplace_back(d, d + n); return true; }
  };
  void send(tuya::Message m, uint8_t flag) {
    m.seq = ++seq;
    uint8_t iv[16], wire[tuya::MAX_WIRE];
    for (int i = 0; i < 16; ++i) iv[i] = static_cast<uint8_t>(seq * 7 + i);
    const size_t n = tuya::seal(m, flag, flag == 4 ? loginKey : sessionKey, iv, wire);
    Collect c;
    c.to = &out;
    tuya::sendPackets(wire, n, 3, c);
  }
  void report(uint8_t id, uint8_t type, uint32_t value, uint8_t width) {
    tuya::Message m;
    m.code = tuya::CODE_RECEIVE_DP;
    m.data[0] = id; m.data[1] = type; m.data[2] = width;
    for (uint8_t i = 0; i < width; ++i) m.data[3 + i] = static_cast<uint8_t>(value >> (8 * (width - 1 - i)));
    m.length = 3u + width;
    send(m, 5);
  }
  void onWrite(const std::vector<uint8_t> &packet) {
    if (!assembler.feed(packet.data(), packet.size())) return;
    const uint8_t flag = assembler.wire()[0];
    tuya::Message m;
    if (!tuya::open(assembler.wire(), assembler.length(), flag == 4 ? loginKey : sessionKey, m)) return;
    tuya::Message r;
    r.code = m.code;
    r.responseTo = m.seq;
    if (m.code == tuya::CODE_DEVICE_INFO && flag == 4) {
      r.length = 46;
      r.data[2] = 3;
      memcpy(r.data + 6, srand, 6);
      send(r, 4);
    } else if (m.code == tuya::CODE_PAIR && flag == 5) {
      uint8_t expected[44] = {};
      memcpy(expected, uuid, 16);
      memcpy(expected + 16, localKey, 6);
      memcpy(expected + 22, deviceId, 22);
      r.length = 1;
      r.data[0] = m.length == 44 && memcmp(m.data, expected, 44) == 0 ? 0 : 1;
      send(r, 5);
    } else if (m.code == tuya::CODE_DPS && flag == 5) {
      r.length = 1;
      r.data[0] = refuseWrites ? 1 : 0;
      if (!refuseWrites && m.length >= 4) {
        if (m.data[0] == 8 && m.data[1] == tuya::DP_ENUM) mode = m.data[3];
        if (m.data[0] == 10 && m.data[1] == tuya::DP_VALUE && m.length >= 7) holdSeconds = m.data[6];
        if (m.data[0] == 2 && m.data[1] == tuya::DP_BOOL && m.data[3] == 1 && mode == 0) ++presses;
      }
      send(r, 5);
    } else if (m.code == tuya::CODE_RECEIVE_DP && m.responseTo != 0) {
      ++acksOfReports;
    }
  }
};

static void testPusherDrivers() {
  printf("pusher drivers: capabilities, SwitchBot, Tuya Fingerbot, MicroBot Push\n");
  Settings settings = defaultSettings();
  {
    PusherLink link;
    CHECK(link.driverCount() == 3);
    for (size_t i = 0; i < link.driverCount(); ++i) {
      CHECK(pusherCapsMeetBaseline(link.driverAt(i)->caps()));
    }
    CHECK(!pusherCapsMeetBaseline(PUSHER_CAP_ACK | PUSHER_CAP_BATTERY));
    CHECK(strcmp(link.driverAt(0)->id(), "switchbot") == 0);
    // The SwitchBot keeps everything it has.
    CHECK(link.driverAt(0)->caps() == (PUSHER_CAP_PRESS | PUSHER_CAP_DOWN_UP | PUSHER_CAP_ACK |
                                       PUSHER_CAP_BATTERY | PUSHER_CAP_HOLD_SECONDS));
    CHECK(link.driverAt(1)->has(PUSHER_CAP_NEEDS_SECRET) && !link.driverAt(1)->has(PUSHER_CAP_DOWN_UP));
    CHECK(link.driverAt(2)->caps() == (PUSHER_CAP_PRESS | PUSHER_CAP_NEEDS_SECRET));
    // A connected device is the pusher whose characteristics it has.
    auto has = [](const char *uuid) {
      return [uuid](const PusherDriver &d) { return strcmp(d.notifyUuid(), uuid) == 0; };
    };
    CHECK(link.identify(has(switchbot::NOTIFY_CHAR_UUID)) == link.driverAt(0));
    CHECK(link.identify(has(tuya::NOTIFY_CHAR_UUID)) == link.driverAt(1));
    CHECK(link.identify(has(microbot::CHAR_UUID)) == link.driverAt(2));
    CHECK(link.identify(has("0000ffe1-0000-1000-8000-00805f9b34fb")) == nullptr);
  }

  auto tuyaSecrets = [](const TuyaDevice &device) {
    PusherSecrets s;
    strcpy(s.tuyaUuid, device.uuid);
    strcpy(s.tuyaDeviceId, device.deviceId);
    strcpy(s.tuyaLocalKey, device.localKey);
    return s;
  };
  // Runs the link against the device model until nothing more is exchanged.
  auto exchange = [](PusherLink &link, WriteLog &out, TuyaDevice &device, size_t &done, uint32_t &now) {
    for (int round = 0; round < 50; ++round) {
      now += 10;
      link.tick(now, true);
      bool moved = false;
      for (; done < out.writes.size(); ++done, moved = true) {
        CHECK(out.writes[done].size() <= PUSHER_MAX_WRITE);
        device.onWrite(out.writes[done]);
      }
      for (const auto &n : device.out) link.onNotify(n.data(), n.size());
      moved = moved || !device.out.empty();
      device.out.clear();
      if (!moved && round > 2) break;
    }
  };

  {  // Tuya: session, press, acknowledgement, battery, hold time
    TuyaDevice device;
    PusherLink link;
    WriteLog out;
    link.begin("bot", &settings, &out);
    link.setSecrets(tuyaSecrets(device));
    size_t done = 0;
    uint32_t now = 1000;
    link.connected(link.driverAt(1));
    CHECK(!link.ready(true));
    exchange(link, out, device, done, now);
    CHECK(link.ready(true) && !link.sessionFailed() && link.problem() == nullptr);
    CHECK(device.mode == 0);                        // set to "press" behaviour
    CHECK(link.beginPress(0, now, true) && link.result() == PressResult::BUSY);
    exchange(link, out, device, done, now);
    CHECK(device.presses == 1 && link.result() == PressResult::OK);
    // What the Fingerbot reports reaches the link, and is acknowledged to it.
    device.report(12, tuya::DP_VALUE, 64, 4);
    device.report(10, tuya::DP_VALUE, 2, 4);
    exchange(link, out, device, done, now);
    CHECK(link.info().valid && link.info().batteryPercent == 64 && link.info().holdSeconds == 2);
    CHECK(device.acksOfReports == 2);
    // The hold time the settings ask for is written to it.
    device.holdSeconds = 5;
    now += 2000;
    exchange(link, out, device, done, now);
    CHECK(device.holdSeconds == settings.botHoldSeconds);
    // A hold timed by the controller is not something it can do: it presses its own way.
    now += 5000;
    link.tick(now, true);
    link.clearResult();
    CHECK(link.beginPress(700, now, true));
    exchange(link, out, device, done, now);
    CHECK(device.presses == 2 && link.result() == PressResult::OK);
    // Refused by the device: nothing moved, and the link says so.
    now += 5000;
    link.tick(now, true);
    link.clearResult();
    device.refuseWrites = true;
    CHECK(link.beginPress(0, now, true));
    exchange(link, out, device, done, now);
    CHECK(device.presses == 2 && link.result() == PressResult::NOT_SENT);
    // The first packet of a message carries its number, length and version.
    CHECK(out.writes[0][0] == 0 && out.writes[0][1] == 33 && out.writes[0][2] == 0x30 &&
          out.writes[0][3] == tuya::FLAG_LOGIN_KEY && out.writes[1][0] == 1);
  }
  {  // Tuya: a key that is not this device's, and no key at all
    TuyaDevice device;
    PusherLink link;
    WriteLog out;
    link.begin("bot", &settings, &out);
    PusherSecrets wrong = tuyaSecrets(device);
    strcpy(wrong.tuyaLocalKey, "ffffff9999999999");
    link.setSecrets(wrong);
    size_t done = 0;
    uint32_t now = 1000;
    link.connected(link.driverAt(1));
    exchange(link, out, device, done, now);
    CHECK(!link.ready(true) && !link.beginPress(0, now, true));
    now += PusherLink::SESSION_TIMEOUT_MS;
    link.tick(now, true);
    CHECK(link.sessionFailed() && link.problem() != nullptr && device.presses == 0);

    PusherLink bare;
    WriteLog none;
    bare.begin("bot", &settings, &none);
    bare.connected(bare.driverAt(1));
    bare.tick(2000, true);
    bare.tick(2010, true);
    CHECK(!bare.ready(true) && bare.sessionFailed() && none.writes.empty());
  }
  {  // MicroBot: session with the token, a press is two writes, never confirmed
    PusherLink link;
    WriteLog out;
    link.begin("power", &settings, &out);
    PusherSecrets secrets;
    for (int i = 0; i < 16; ++i) secrets.microbotToken[i] = static_cast<uint8_t>(0xa0 + i);
    link.connected(link.driverAt(2));
    link.tick(1000, true);
    CHECK(!link.ready(true) && out.writes.empty() && link.problem() != nullptr);  // no token
    secrets.microbotTokenSet = true;
    link.setSecrets(secrets);
    link.connected(link.driverAt(2));
    link.tick(2000, true);
    CHECK(link.ready(true) && out.writes.size() == 2);
    const std::vector<uint8_t> head = hexBytes("00010000000000fa0000070000000000decd");
    CHECK(out.writes[0].size() == 20 && memcmp(out.writes[0].data() + 2, head.data(), 18) == 0);
    CHECK(out.writes[1].size() == 20 && out.writes[1][2] == 0x0f && out.writes[1][3] == 0xff &&
          memcmp(out.writes[1].data() + 4, secrets.microbotToken, 16) == 0);
    CHECK(out.writes[0][0] == out.writes[1][0] && out.writes[0][1] == out.writes[1][1]);
    CHECK(link.beginPress(0, 3000, true));
    CHECK(out.writes.size() == 4 && link.result() == PressResult::UNCONFIRMED);
    const std::vector<uint8_t> press = hexBytes("000100000008020000000a0000000000decd");
    const std::vector<uint8_t> body = hexBytes("0fffffffffff000000000000000000000000");
    CHECK(memcmp(out.writes[2].data() + 2, press.data(), 18) == 0 &&
          memcmp(out.writes[3].data() + 2, body.data(), 18) == 0);
    // Out of range: nothing left, and that is what the controller is told.
    link.tick(3000 + settings.releaseSettleMs + 10, true);
    link.clearResult();
    out.up = false;
    CHECK(link.beginPress(0, 9000, true) && link.result() == PressResult::NOT_SENT);
  }
}

// The down / up press with a Bot still moving its arm down: it answers the
// up "busy" (0x03) and ignores it. Seen on the board: the up went out 0.43 s
// after the down, was refused, and the arm stayed on the button. The up is
// asked for again until it is done. And the power bot always gets the Bot's
// own complete press, whatever the press style.
static void testUpRefusedWhileMoving() {
  printf("pusher link: an up refused while the arm moves is sent again\n");
  Settings settings = defaultSettings();
  settings.actuationMode = ActuationMode::DOWN_UP;
  settings.pressDurationMs = 300;
  const uint8_t down[3] = {0x57, 0x01, 0x03}, up[3] = {0x57, 0x01, 0x04}, press[3] = {0x57, 0x01, 0x00};
  const uint8_t ok = 0x01, busy = 0x03;
  auto ups = [&](const WriteLog &log, size_t from) {
    size_t n = 0;
    for (size_t i = from; i < log.writes.size(); ++i) {
      n += log.writes[i].size() == 3 && memcmp(log.writes[i].data(), up, 3) == 0;
    }
    return n;
  };
  PusherLink link;
  WriteLog out;
  link.begin("bot", &settings, &out);
  link.connected(link.driverAt(0));
  uint32_t now = 1000;
  link.tick(now, true);
  link.onNotify(&ok, 1);  // (answer to the info request)
  const size_t base = out.writes.size();
  CHECK(link.beginPress(0, now += 10, true));
  CHECK(memcmp(out.writes.back().data(), down, 3) == 0);
  link.onNotify(&ok, 1);
  for (int i = 0; i < 31; ++i) link.tick(now += 10, true);  // the 300 ms hold
  CHECK(ups(out, base) == 1);
  // Busy twice: still moving. Each refusal brings another up.
  link.onNotify(&busy, 1);
  for (int i = 0; i < 31; ++i) link.tick(now += 10, true);
  CHECK(ups(out, base) == 2 && link.busy());
  link.onNotify(&busy, 1);
  for (int i = 0; i < 31; ++i) link.tick(now += 10, true);
  CHECK(ups(out, base) == 3 && link.busy());
  link.onNotify(&ok, 1);
  link.tick(now += 10, true);
  CHECK(link.result() == PressResult::OK);
  for (int i = 0; i < 300; ++i) link.tick(now += 10, true);
  CHECK(!link.busy() && ups(out, base) == 3);

  // An up with no answer at all is sent once more after settling.
  const size_t base2 = out.writes.size();
  link.clearResult();
  CHECK(link.beginPress(0, now += 10, true));
  link.onNotify(&ok, 1);
  for (int i = 0; i < 31; ++i) link.tick(now += 10, true);
  CHECK(ups(out, base2) == 1);
  for (int i = 0; i < 400; ++i) link.tick(now += 10, true);  // no answer
  CHECK(ups(out, base2) == 2);

  // The power bot: one complete press, never a down left for an up.
  PusherLink power;
  WriteLog outP;
  power.begin("power", &settings, &outP);
  power.connected(power.driverAt(0));
  power.tick(1000, true);
  power.onNotify(&ok, 1);
  CHECK(power.beginPress(0, 1010, true) && memcmp(outP.writes.back().data(), press, 3) == 0);
  CHECK(ups(outP, 0) == 0);
}

// Holding a button down for a whole shot, on the SwitchBot's own bytes.
static void testPusherHold() {
  printf("pusher link: hold for the shot, release, and an arm left down\n");
  Settings settings = defaultSettings();
  const uint8_t down[3] = {0x57, 0x01, 0x03}, up[3] = {0x57, 0x01, 0x04};
  const uint8_t ok = 0x01;
  PusherLink link;
  WriteLog out;
  link.begin("bot", &settings, &out);
  link.connected(link.driverAt(0));
  uint32_t now = 1000;
  link.tick(now, true);                               // session up; asks for the Bot's info
  const size_t base = out.writes.size();
  CHECK(link.beginHold(now += 10, true) && link.result() == PressResult::BUSY && !link.holding());
  CHECK(out.writes.size() == base + 1 && out.writes.back().size() == 3 &&
        memcmp(out.writes.back().data(), down, 3) == 0);
  const uint32_t downAt = now;
  link.onNotify(&ok, 1);
  link.tick(now += 10, true);
  CHECK(link.holding() && link.busy() && link.pressSentAtMs() == downAt);
  // It stays down: no up by itself, however long, and no other press meanwhile.
  for (int i = 0; i < 6000; ++i) link.tick(now += 10, true);
  CHECK(link.holding() && link.result() == PressResult::BUSY && out.writes.size() == base + 1);
  CHECK(!link.beginPress(0, now, true) && !link.beginHold(now, true));
  // Release: the up goes out on the next tick, and its answer ends the hold.
  CHECK(link.release(now += 10, true));
  const uint32_t releasedAt = now;
  link.tick(now += 10, true);
  CHECK(out.writes.size() == base + 2 && memcmp(out.writes.back().data(), up, 3) == 0 && !link.holding());
  link.onNotify(&ok, 1);
  link.tick(now += 10, true);
  CHECK(link.result() == PressResult::OK && link.pressSentAtMs() == releasedAt);
  now += 5000;
  link.tick(now, true);
  link.clearResult();
  CHECK(!link.busy());

  // The link drops while holding: reported at once, and the arm is brought
  // up as soon as the Bot is back, before anything else.
  CHECK(link.beginHold(now += 10, true));
  link.onNotify(&ok, 1);
  link.tick(now += 10, true);
  CHECK(link.holding());
  link.tick(now += 10, false);
  CHECK(link.result() == PressResult::UNCONFIRMED && !link.holding() && link.busy());
  const size_t before = out.writes.size();
  link.connected(link.driverAt(0));
  for (int i = 0; i < 40; ++i) link.tick(now += 10, true);
  bool upSent = false;
  for (size_t i = before; i < out.writes.size(); ++i) {
    upSent = upSent || (out.writes[i].size() == 3 && memcmp(out.writes[i].data(), up, 3) == 0);
  }
  CHECK(upSent);
  for (int i = 0; i < 400; ++i) link.tick(now += 10, true);
  link.clearResult();
  CHECK(!link.busy());

  // After a restart nothing is known about the arm: release sends an up all the same.
  PusherLink fresh;
  WriteLog out2;
  fresh.begin("bot", &settings, &out2);
  fresh.connected(fresh.driverAt(0));
  fresh.tick(1000, true);
  CHECK(fresh.release(1010, true) && memcmp(out2.writes.back().data(), up, 3) == 0);
  fresh.onNotify(&ok, 1);
  fresh.tick(1020, true);
  CHECK(fresh.result() == PressResult::OK);

  // A pusher without separate down and up cannot hold: it says so, and nothing is sent.
  PusherLink micro;
  WriteLog out3;
  PusherSecrets secrets;
  secrets.microbotTokenSet = true;
  micro.begin("bot", &settings, &out3);
  micro.setSecrets(secrets);
  micro.connected(micro.driverAt(2));
  micro.tick(1000, true);
  const size_t sent = out3.writes.size();
  CHECK(micro.ready(true) && !micro.beginHold(1010, true) && !micro.release(1020, true) &&
        out3.writes.size() == sent);
}

struct FakeBot : IActuator {
  bool up = false;
  int presses = 0;
  PressResult next = PressResult::OK, now = PressResult::IDLE;
  bool connected() const override { return up; }
  bool beginPress(uint32_t = 0) override {
    if (!up) return false;
    ++presses;
    now = PressResult::BUSY;
    return true;
  }
  PressResult result() const override { return now; }
  uint32_t pressSentAtMs() const override { return 0; }
  void clearResult() override { if (now != PressResult::BUSY) now = PressResult::IDLE; }
  void done() { now = next; }
};

// Each press that ends leaves one event: what it was for, who asked, how it went, and when.
static void testPowerEvents() {
  printf("power bot: event log\n");
  PusherConfig config;
  PowerControl power;
  power.begin(&config);
  FakeBot bot;
  bot.up = true;
  uint32_t now = 1000;
  const uint32_t epoch = 1791763200u;
  PowerEvent ev;
  strcpy(config.powerAddress, "aa:bb:cc:dd:ee:ff");
  // Refusals are not presses.
  CHECK(!power.request(true, now) && !power.takeEvent(ev));             // not known
  power.believe(PowerBelief::ON);
  CHECK(!power.request(true, now) && !power.takeEvent(ev));             // already on
  // The owner switches off: one event, handed over once.
  CHECK(power.request(false, now));
  power.tick(now += 10, epoch, true, bot);
  CHECK(!power.takeEvent(ev));                                          // still pressing
  bot.done();
  power.tick(now += 10, epoch + 5, true, bot);
  CHECK(power.takeEvent(ev) && ev.epochS == epoch + 5 && ev.on == 0 && ev.by == 0 &&
        ev.outcome == static_cast<uint8_t>(PowerControl::Outcome::DONE) && ev.sure == 1);
  CHECK(!power.takeEvent(ev));
  CHECK(sizeof(PowerEvent) == 8);
  // A press the bot refuses ends as failed, with the clock not yet set.
  bot.next = PressResult::NOT_SENT;
  CHECK(power.request(true, now));
  for (int i = 0; i < 20; ++i) {
    power.tick(now += 10, 0, true, bot);
    bot.done();
  }
  CHECK(power.takeEvent(ev) && ev.on == 1 && ev.epochS == 0 &&
        ev.outcome == static_cast<uint8_t>(PowerControl::Outcome::PRESS_FAILED));
  // "Press anyway" while on/off is not known: logged, but not sure which way.
  bot.next = PressResult::OK;
  power.believe(PowerBelief::UNKNOWN);
  CHECK(power.pressAnyway(now));
  power.tick(now += 10, epoch + 60, true, bot);
  bot.done();
  power.tick(now += 10, epoch + 61, true, bot);
  CHECK(power.takeEvent(ev) && ev.sure == 0 && ev.outcome == static_cast<uint8_t>(PowerControl::Outcome::DONE));
  // The schedule: on, by schedule.
  power.believe(PowerBelief::OFF);
  config.utcOffsetMin = 0;
  config.schedule[0] = {1, 1, 8 * 60, 0x7f};
  const uint32_t at0800 = epoch + 8 * 3600;
  for (uint32_t e = at0800 - 120; e <= at0800 + 120; e += 20) {
    power.tick(now += 20000, e, true, bot);
    if (bot.result() == PressResult::BUSY) bot.done();
  }
  CHECK(power.takeEvent(ev) && ev.on == 1 && ev.by == 1 && ev.sure == 1 &&
        ev.epochS >= at0800 && ev.epochS <= at0800 + 60 &&
        ev.outcome == static_cast<uint8_t>(PowerControl::Outcome::DONE));
}

// The scale switching off switches the machine off (when asked for): the press is marked as the scale's,
// and the moment is seen once, not at start-up, not while the scale rests or still advertises.
static void testPowerOffWithScale() {
  printf("power bot: off when the scale switches off\n");
  const uint32_t Q = ScaleRest::QUIET_MS;
  ScaleOffWatch w;
  uint32_t t = 1000;
  CHECK(!w.update(t, false, false, UINT32_MAX, Q));          // start-up, scale off: nothing seen
  CHECK(!w.update(t += 1000, true, false, 0, Q));            // on, connected for an hour: not advertising
  t += 3600000;
  CHECK(!w.update(t, true, false, 0, Q));
  // The link drops. Last heard advertising an hour ago, but the link was there a moment ago: not off.
  CHECK(!w.update(t += 500, false, false, 3600000, Q));
  CHECK(!w.update(t += Q - 1000, false, false, 3600000, Q)); // still under the quiet limit
  CHECK(!w.update(t += 100, false, true, 3600000, Q));       // let go to rest: not off
  CHECK(w.update(t += 1000, false, false, 3600000, Q));      // silent long enough: switched off
  CHECK(!w.update(t += 5000, false, false, 3600000, Q));     // once
  CHECK(!w.update(t += 1000, false, false, 1000, Q));        // heard advertising again: on
  CHECK(w.update(t += Q, false, false, Q, Q));               // and off again

  PusherConfig config;
  PowerControl power;
  power.begin(&config);
  FakeBot bot;
  bot.up = true;
  strcpy(config.powerAddress, "aa:bb:cc:dd:ee:ff");
  uint32_t now = 1000;
  PowerEvent ev;
  power.believe(PowerBelief::OFF);
  CHECK(!power.request(false, now, PowerBy::SCALE_OFF) && !power.takeEvent(ev));  // already off
  power.believe(PowerBelief::ON);
  CHECK(power.request(false, now, PowerBy::SCALE_OFF) && !power.lastWasSchedule());
  power.tick(now += 10, 1791763200u, true, bot);
  bot.done();
  power.tick(now += 10, 1791763201u, true, bot);
  CHECK(power.takeEvent(ev) && ev.on == 0 && ev.by == static_cast<uint8_t>(PowerBy::SCALE_OFF) &&
        ev.outcome == static_cast<uint8_t>(PowerControl::Outcome::DONE));
  CHECK(power.belief() == PowerBelief::OFF);
  Settings s;
  CHECK(!s.powerOffWithScale);                          // off unless asked for
}

static Coffee tasteCoffee(uint16_t id, const char *notes, const char *process, uint8_t roast) {
  Coffee c;
  c.id = id;
  strcpy(c.notes, notes);
  strcpy(c.process, process);
  c.roastLevel = roast;
  return c;
}

static void testCoffeeTaste() {
  printf("coffee taste: ratings, descriptions, profiles, matching\n");
  // A rating: stars are needed; values are kept in range.
  CoffeeRating r;
  r.coffeeId = 3;
  CHECK(!sanitizeRating(r));
  r.stars = 4;
  r.attr[ACIDITY] = 9;
  r.flavours = 0xFFFF;
  r.drinkerId = 40;
  CHECK(sanitizeRating(r) && r.attr[ACIDITY] == 5 && r.flavours == FLAVOUR_MASK && r.drinkerId == 0);
  CoffeeRating same = r;
  same.stars = 2;
  CHECK(sameRated(r, same));
  same.bagId = 9;
  CHECK(!sameRated(r, same));
  CHECK(sizeof(CoffeeRating) == 32);

  // Notes to flavour families, at the start of words only.
  const uint16_t f = familiesInNotes("Grape Jam & Burnt Sugars");
  CHECK((f >> FRUITY & 1) && (f >> SWEET_CARAMEL & 1) && (f >> ROASTY & 1) && !(f >> FLORAL & 1));
  CHECK(familiesInNotes("a sublime cup") == 0);
  CHECK(familiesInNotes("Jasmine, bergamot") >> FLORAL & 1);

  // A light natural reads bright and fruity; a dark roast heavy, bitter and roasty.
  const Coffee light = tasteCoffee(1, "blueberry, jasmine", "natural", 1);
  const Coffee dark = tasteCoffee(2, "dark chocolate, smoky", "washed", 5);
  const TasteVector lt = tasteFromDescription(light), dt = tasteFromDescription(dark);
  CHECK(lt.v[ACIDITY] > dt.v[ACIDITY] && dt.v[BITTERNESS] > lt.v[BITTERNESS]);
  CHECK(lt.v[TASTE_ATTRS + FRUITY] == 1.0f && lt.known[TASTE_ATTRS + FRUITY] < 1.0f);
  CHECK(dt.v[TASTE_ATTRS + ROASTY] == 1.0f && dt.v[TASTE_ATTRS + FRUITY] == 0.0f);

  // A rating's own values win over the description's guesses; what it left out is filled in.
  CoffeeRating lr;
  lr.coffeeId = 1;
  lr.stars = 5;
  lr.attr[ACIDITY] = 5;
  lr.attr[SWEETNESS] = 4;
  lr.flavours = (1u << FRUITY) | (1u << FLORAL);
  const TasteVector lrated = blendTaste(tasteFromRating(lr), lt);
  CHECK(lrated.v[ACIDITY] == 1.0f && lrated.known[ACIDITY] == 1.0f);
  CHECK(lrated.known[BITTERNESS] > 0.0f && lrated.known[BITTERNESS] < 1.0f);   // from the roast level

  // Loving the light natural and disliking the dark roast: the profile leans bright, fruity, not roasty.
  CoffeeRating dr;
  dr.coffeeId = 2;
  dr.stars = 1;
  dr.attr[BITTERNESS] = 5;
  dr.attr[ACIDITY] = 1;
  dr.flavours = (1u << ROASTY) | (1u << CHOCOLATE_NUTTY);
  const uint32_t now = 1791763200u;
  RatedTaste rated[] = {{1, 5, now, lrated}, {2, 1, now, blendTaste(tasteFromRating(dr), dt)}};
  TasteProfile p = buildProfile(rated, 2, now);
  CHECK(p.coffees == 2 && !p.shown() && !p.settled());
  CHECK(p.v[ACIDITY] > 0.6f && p.v[BITTERNESS] < 0.4f);
  CHECK(p.v[TASTE_ATTRS + FRUITY] > 0.6f && p.v[TASTE_ATTRS + ROASTY] < 0.4f);

  // A coffee like the loved one matches; one like the disliked one does not.
  const TasteMatch good = matchTaste(p, tasteFromDescription(tasteCoffee(7, "peach, floral", "natural", 2)));
  const TasteMatch bad = matchTaste(p, tasteFromDescription(tasteCoffee(8, "smoky, cocoa", "washed", 5)));
  CHECK(good.score > 0.3f && bad.score < 0.0f && good.score > bad.score);
  CHECK(good.why[0] != 255);
  // Older ratings count less, a 3-star rating pulls only a little.
  CHECK(recencyWeight(now - 2 * 365 * 86400u, now) < 0.5f && recencyWeight(now, now) == 1.0f);
  CHECK(starPull(3) > 0.0f && starPull(3) < 0.3f && starPull(1) < 0.0f);
  // The shared record: the cup values given, the flavours by name, the drinker by number.
  {
    Coffee c = tasteCoffee(5, "grape", "natural", 2);
    strcpy(c.name, "Fugi Ubumwe");
    strcpy(c.libraryId, "example/fugi");
    CoffeeRating sr;
    sr.id = 12;
    sr.coffeeId = 5;
    sr.drinkerId = 2;
    sr.stars = 4;
    sr.attr[ACIDITY] = 5;
    sr.attr[BODY] = 2;
    sr.flavours = (1u << FRUITY) | (1u << FLORAL);
    char body[1024];
    CHECK(buildTasteRecord(sr, c, 20260429, body, sizeof(body)) > 0);
    CHECK(strstr(body, "\"t\":\"taste\"") && strstr(body, "\"id\":12") && strstr(body, "\"stars\":4"));
    CHECK(strstr(body, "\"drinker\":2") && strstr(body, "\"library_id\":\"example/fugi\""));
    CHECK(strstr(body, "\"cup\":{\"acidity\":5,\"body\":2}") && strstr(body, "\"flavours\":[\"fruity\",\"floral\"]"));
    CHECK(strstr(body, "\"roast_date\":\"2026-04-29\""));
    CHECK(buildTasteRecord(sr, c, 0, body, 40) == 0);   // does not fit: nothing
  }
  // Recommendations: the community's taste, how alike two coffees and two drinkers are, the score.
  CHECK(roastLevelFromName("medium-dark") == 4 && roastLevelFromName("purple") == 0);
  const float ccup[TASTE_ATTRS] = {5, 0, 2, 1, 4};
  const float cfl[FLAVOUR_COUNT] = {0.9f, 0.6f, 0.2f, 0, 0, 0, 0.1f, 0};
  const TasteVector comm = tasteFromCommunity(ccup, cfl);
  CHECK(comm.v[ACIDITY] == 1.0f && comm.known[SWEETNESS] == 0.0f && comm.known[ACIDITY] > lt.known[ACIDITY]);
  CHECK(tasteSimilarity(comm, lrated) > 0.5f && tasteSimilarity(comm, blendTaste(tasteFromRating(dr), dt)) < 0.0f);
  const uint8_t me[4] = {5, 1, 4, 0}, twin[4] = {5, 1, 5, 3}, opposite[4] = {1, 5, 1, 0}, little[4] = {5, 0, 0, 2};
  CHECK(peopleLikeness(me, twin, 4) > 0.7f);
  CHECK(peopleLikeness(me, opposite, 4) == 0.0f);   // disagreeing says nothing
  CHECK(peopleLikeness(me, little, 4) == 0.0f);     // one coffee in common is not enough
  const TasteMatch tm = matchTaste(p, comm);
  CHECK(recommendScore(tm, 5, 0) > recommendScore(tm, 0, 0));     // drinkers like you loved it
  CHECK(recommendScore(tm, 1, 0) < recommendScore(tm, 0, 0));
  CHECK(recommendScore(tm, 0, 4.5f) > recommendScore(tm, 0, 0));  // the community likes it
  CHECK(recommendScore(bad, 0, 0) < 0.0f);
  // Shown from three coffees, settled from eight.
  RatedTaste three[] = {{1, 5, now, lrated}, {2, 1, now, lrated}, {3, 4, now, lrated}};
  CHECK(buildProfile(three, 3, now).shown());
}

static void testPowerControl() {
  printf("power bot: on, off, belief, schedule\n");
  PusherConfig config;
  PowerControl power;
  power.begin(&config);
  FakeBot bot;
  uint32_t now = 1000;
  // No power bot paired: nothing happens.
  CHECK(!power.request(true, now) && power.lastOutcome() == PowerControl::Outcome::NO_BOT);
  strcpy(config.powerAddress, "aa:bb:cc:dd:ee:ff");
  // Not known whether the machine is on: on/off is refused, the owner says which.
  CHECK(!power.request(true, now) && power.lastOutcome() == PowerControl::Outcome::NOT_KNOWN);
  power.believe(PowerBelief::OFF);
  CHECK(power.takeDirty() && !power.takeDirty());
  CHECK(!power.request(false, now) && power.lastOutcome() == PowerControl::Outcome::ALREADY);
  // On: the link is asked for, the press waits for it, then the belief changes.
  CHECK(power.request(true, now) && power.wantsLink());
  power.tick(now += 10, 0, true, bot);
  CHECK(bot.presses == 0);
  bot.up = true;
  power.tick(now += 10, 0, false, bot);          // a shot is running: not now
  CHECK(bot.presses == 0);
  power.tick(now += 10, 0, true, bot);
  CHECK(bot.presses == 1 && power.belief() == PowerBelief::OFF && power.wantsLink());
  CHECK(!power.request(false, now) && power.lastOutcome() == PowerControl::Outcome::BUSY);
  bot.done();
  power.tick(now += 10, 0, true, bot);
  CHECK(power.belief() == PowerBelief::ON && !power.wantsLink() && power.takeDirty());
  CHECK(power.lastOutcome() == PowerControl::Outcome::DONE);
  // Refused by the bot: tried again, then given up, belief unchanged.
  bot.next = PressResult::NOT_SENT;
  CHECK(power.request(false, now));
  for (int i = 0; i < 20; ++i) {
    power.tick(now += 10, 0, true, bot);
    bot.done();
  }
  CHECK(bot.presses == 1 + 3 && power.belief() == PowerBelief::ON &&
        power.lastOutcome() == PowerControl::Outcome::PRESS_FAILED && !power.takeDirty());
  // The bot never connects: given up after the time-out.
  bot.up = false;
  bot.next = PressResult::OK;
  CHECK(power.request(false, now));
  power.tick(now += PowerControl::CONNECT_TIMEOUT_MS - 10, 0, true, bot);
  CHECK(power.wantsLink());
  power.tick(now += 20, 0, true, bot);
  CHECK(!power.wantsLink() && power.lastOutcome() == PowerControl::Outcome::BOT_UNREACHABLE);
  // "Press anyway" flips a known belief and leaves an unknown one unknown.
  bot.up = true;
  CHECK(power.pressAnyway(now));
  power.tick(now += 10, 0, true, bot);
  bot.done();
  power.tick(now += 10, 0, true, bot);
  CHECK(power.belief() == PowerBelief::OFF);
  power.believe(PowerBelief::UNKNOWN);
  CHECK(power.pressAnyway(now));
  power.tick(now += 10, 0, true, bot);
  bot.done();
  power.tick(now += 10, 0, true, bot);
  CHECK(power.belief() == PowerBelief::UNKNOWN);

  // Schedule. 2026-10-12 is a Monday; 06:30 local at UTC+11 is 2026-10-11 19:30 UTC.
  const uint32_t mondayMidnightUtc = 1791763200u;  // 2026-10-12 00:00:00 UTC
  CHECK(PowerControl::localMinuteOfWeek(mondayMidnightUtc, 0) == 1 * 1440);
  CHECK(PowerControl::localMinuteOfWeek(mondayMidnightUtc, -60) == 1 * 1440 - 60);  // still Sunday
  config.utcOffsetMin = 11 * 60;
  config.schedule[0] = {1, 1, 6 * 60 + 30, 0x3e};   // on at 06:30, Monday to Friday
  config.schedule[1] = {1, 0, 9 * 60, 0x7f};        // off at 09:00, every day
  const uint32_t monday0630 = mondayMidnightUtc - 11 * 3600 + 6 * 3600 + 30 * 60;
  const int pressesBefore = bot.presses;
  auto runTo = [&](uint32_t fromEpoch, uint32_t toEpoch) {
    for (uint32_t e = fromEpoch; e <= toEpoch; e += 20) {
      power.tick(now += 20000, e, true, bot);
      if (bot.result() == PressResult::BUSY) bot.done();
    }
  };
  // Belief unknown: the schedule presses nothing.
  runTo(monday0630 - 120, monday0630 + 120);
  CHECK(bot.presses == pressesBefore && power.lastOutcome() == PowerControl::Outcome::NOT_KNOWN &&
        power.lastWasSchedule());
  // Known off: on at 06:30 on Monday, once.
  power.believe(PowerBelief::OFF);
  runTo(monday0630 + 86400 - 120, monday0630 + 86400 + 300);   // Tuesday
  CHECK(bot.presses == pressesBefore + 1 && power.belief() == PowerBelief::ON);
  // Off at 09:00.
  runTo(monday0630 + 86400 + 9000 - 120, monday0630 + 86400 + 9000 + 120);
  CHECK(bot.presses == pressesBefore + 2 && power.belief() == PowerBelief::OFF);
  // Saturday 06:30 is not a day of the first entry; and 09:00 finds it off already.
  runTo(monday0630 + 5 * 86400 - 120, monday0630 + 5 * 86400 + 9000 + 120);
  CHECK(bot.presses == pressesBefore + 2);
  // The clock jumps over a time by hours (it was just set): not acted on late.
  power.tick(now += 20, monday0630 + 7 * 86400 - 7200, true, bot);
  power.tick(now += 20, monday0630 + 7 * 86400 + 3600, true, bot);
  CHECK(bot.presses == pressesBefore + 2 && !power.wantsLink());
  // What is stored is made safe before use.
  config.schedule[2].minuteOfDay = 5000;
  config.belief = 9;
  config.utcOffsetMin = 5000;
  sanitizePusherConfig(config);
  CHECK(config.schedule[2].minuteOfDay == 0 && config.belief == 2 && config.utcOffsetMin == 0);
}

static void testBuiltinGrinders() {
  printf("grinders: every built-in model reads and writes its own range\n");
  // Ids that existed before must keep how their setting is read (stored settings would change).
  CHECK(findGrinderModel(1) && findGrinderModel(1)->umPerUnit == 1.0f && findGrinderModel(1)->divisor == 1);
  for (uint16_t id : {3, 4, 5}) {
    const GrinderModel *m = findGrinderModel(id);
    CHECK(m && m->divisor == 1 && m->subSteps == 0);
  }
  CHECK(!findGrinderModel(2));  // taken out; never given again
  for (size_t i = 0; i < GRINDER_MODEL_COUNT; ++i) {
    const GrinderModel &m = GRINDER_MODELS[i];
    for (size_t k = i + 1; k < GRINDER_MODEL_COUNT; ++k) {
      CHECK(GRINDER_MODELS[k].id != m.id);
      CHECK(strcmp(GRINDER_MODELS[k].libraryId, m.libraryId) != 0);
    }
    CHECK(strlen(m.model) < GRINDER_NAME_LEN && strlen(m.burrs) < GRINDER_BURRS_LEN && strlen(m.unit) < GRINDER_UNIT_LEN);
    CHECK(strlen(m.libraryId) > 0 && strlen(m.libraryId) < LIBRARY_ID_LEN);
    // Picked by an owner: the model's name, unit and reading come with it.
    Grinder g;
    g.modelId = m.id;
    g.divisor = 1;
    g.subSteps = 0;
    normalizeGrinder(g);
    CHECK(g.modelId == m.id && strcmp(g.model, m.model) == 0 && strcmp(g.unit, m.unit) == 0);
    if (m.subSteps >= 2) CHECK(g.subSteps == m.subSteps);
    if (!m.maxStep) continue;
    CHECK(m.minStep >= 1 && m.minStep < m.maxStep && m.maxStep <= SETTING_MAX);
    // Both ends written in its own unit and read back to the same step.
    for (uint16_t v : {m.minStep, static_cast<uint16_t>((m.minStep + m.maxStep) / 2), m.maxStep}) {
      char text[24];
      uint16_t back = 0;
      CHECK(formatSetting(v, g, text, sizeof(text)) > 0);
      CHECK(parseSetting(text, g, back) && back == v);
    }
  }
  // A Sette 270 reads macro:micro; its top is 31:9.
  Grinder sette;
  sette.modelId = 11;
  normalizeGrinder(sette);
  char text[24];
  formatSetting(279, sette, text, sizeof(text));
  CHECK(strcmp(text, "31:9") == 0);
  // An owner's own divisor on a model without one of its own is kept.
  Grinder niche;
  niche.modelId = 5;
  niche.divisor = 4;
  normalizeGrinder(niche);
  CHECK(niche.divisor == 4);
}

static void testPowerBeliefDrift() {
  printf("power bot: a machine that switches itself off; a skipped scheduled press is told\n");
  PusherConfig config;
  strcpy(config.powerAddress, "aa:bb:cc:dd:ee:ff");
  PowerControl power;
  power.begin(&config);
  power.setAutoOffMin(20);
  FakeBot bot;
  bot.up = true;
  uint32_t now = 1000;
  power.believe(PowerBelief::ON);
  power.takeDirty();
  // Nothing done on it for 19 minutes: still believed on.
  power.tick(now, 0, true, bot);
  power.tick(now += 19 * 60000, 0, true, bot);
  CHECK(power.belief() == PowerBelief::ON && !power.takeAutoOff());
  // A shot at 19 minutes restarts the machine's own timer.
  power.noteUse(now);
  CHECK(!power.takeBelievedOnByShot());  // believed on already
  power.tick(now += 19 * 60000, 0, true, bot);
  CHECK(power.belief() == PowerBelief::ON);
  // 20 minutes after the shot: it has switched itself off.
  power.tick(now += 60000, 0, true, bot);
  CHECK(power.belief() == PowerBelief::OFF && power.takeAutoOff() && power.takeDirty());
  // So "on" presses the button again instead of finding it on already.
  CHECK(power.request(true, now));
  power.tick(now += 10, 0, true, bot);
  bot.done();
  power.tick(now += 10, 0, true, bot);
  CHECK(power.belief() == PowerBelief::ON);
  // Just switched on: warming up for 30 minutes.
  CHECK(power.switchedOnWithin(now, 30 * 60000));
  CHECK(!power.switchedOnWithin(now + 31 * 60000, 30 * 60000));
  // Not while a shot is in hand (idle false), however long.
  power.tick(now += 60 * 60000, 0, false, bot);
  CHECK(power.belief() == PowerBelief::ON);
  // A shot while it is believed off: it is on after all.
  power.believe(PowerBelief::OFF);
  power.noteUse(now);
  CHECK(power.belief() == PowerBelief::ON && power.takeBelievedOnByShot());
  // Without an off-timer nothing changes by itself.
  power.setAutoOffMin(0);
  power.tick(now += 24 * 3600000u, 0, true, bot);
  CHECK(power.belief() == PowerBelief::ON);

  // The schedule's "on" while believed on already: not pressed, and that is told.
  const uint32_t mondayMidnightUtc = 1791763200u;
  config.utcOffsetMin = 0;
  config.schedule[0] = {1, 1, 8 * 60, 0x7f};
  const int presses = bot.presses;
  for (uint32_t e = mondayMidnightUtc + 8 * 3600 - 120; e <= mondayMidnightUtc + 8 * 3600 + 120; e += 20) {
    power.tick(now += 20000, e, true, bot);
  }
  bool on = false;
  PowerControl::Outcome why;
  CHECK(bot.presses == presses && power.takeSkipped(on, why) && on && why == PowerControl::Outcome::ALREADY);
  CHECK(!power.takeSkipped(on, why));
}

static void testCupNotTrustedAfterScaleSleep() {
  printf("cup: not believed after the scale dropped out or slept, only after a controller restart\n");
  // As testCupRemembered: a shot, then a fresh cup put on and tared; the scale's timer holds the
  // shot's time, so the remembered cup would match on the timer.
  Rig r;
  r.settings.requireCup = true;
  r.toReady();
  r.run(1000);
  r.sim.trueWeightG += 62.0;
  r.run(3000);
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  r.ctl.acknowledge();
  r.run(500);
  r.sim.trueWeightG -= 62.0 + 36.0;
  r.run(2000);
  r.sim.trueWeightG += 70.0;                    // fresh cup
  r.run(3000);
  CHECK(r.ctl.status(r.sim.nowMs).cupPresent);
  const CupMemory kept = r.ctl.cupMemory();
  CHECK(kept.present && kept.scaleTimerMs > 0);
  // The scale is let go and naps for 40 s, its stopped timer held; meanwhile the cup is lifted,
  // and the scale wakes reading 0 g: everything the old rule checked matches.
  r.sim.timerHeldMs = r.sim.timerMs();
  r.sim.scaleLink = false;
  r.run(20000);
  r.sim.trueWeightG -= 70.0;
  r.sim.tareOffsetG = r.sim.trueWeightG;
  r.run(20000);
  r.sim.scaleLink = true;
  CHECK(r.runUntil(State::READY, 10000));
  r.run(3000);
  CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);   // not believed: no shot onto nothing
  r.ctl.requestStart();
  r.run(500);
  CHECK(r.ctl.state() == State::READY);           // refused until a cup is seen going on
}

static void testResultScreenLetsGo() {
  printf("a shot's result left showing: power may be pressed, and the scale gone sends it home\n");
  Rig r;
  r.toReady();
  CHECK(r.ctl.idleForPower());
  r.ctl.requestStart();
  CHECK(r.runUntil(State::BREWING, 10000));
  CHECK(!r.ctl.idleForPower());                       // never under a shot
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK(r.ctl.idleForPower());                        // the result showing is not a shot
  // The scale stays: the result stays.
  r.run(BrewController::RESULT_SCALE_GONE_MS + 5000);
  CHECK(r.ctl.state() == State::SHOT_COMPLETE);
  // The scale let go (or switched off): home after a while, not at once.
  r.sim.scaleLink = false;
  r.run(BrewController::RESULT_SCALE_GONE_MS / 2);
  CHECK(r.ctl.state() == State::SHOT_COMPLETE);
  r.run(BrewController::RESULT_SCALE_GONE_MS / 2 + 1000);
  CHECK(r.ctl.state() == State::CONNECTING_SCALE);
  CHECK(r.ctl.idleForPower());
  // Back again: ready as usual.
  r.sim.scaleLink = true;
  CHECK(r.runUntil(State::READY, 10000));
}

static void testReadingNewerThanTick() {
  printf("a reading stamped after the tick's clock was read\n");
  // Reported from the emulator: "scale went silent" in the middle of shots
  // whose readings had no gap at all. The BLE task stamps a reading while the
  // control task, which has already read its clock, is still busy; the
  // reading is then newer than "now" and its age, unsigned, is enormous.
  Rig r;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::BREWING, 10000));
  r.run(9000);                                   // pouring
  CHECK(r.ctl.state() == State::BREWING);
  for (int k = 0; k < 20 && r.ctl.state() == State::BREWING; ++k) {
    r.run(90);
    WeightSample s;
    s.weightG = static_cast<float>(round((r.sim.trueWeightG - r.sim.tareOffsetG) * 10.0) / 10.0);
    s.atMs = r.sim.nowMs + 3;                    // stamped 3 ms after the clock was read
    s.sequence = ++r.sim.sequence;
    r.ctl.onSample(s);
    r.ctl.tick(r.sim.nowMs);
  }
  CHECK(r.ctl.state() == State::BREWING);
  CHECK(r.ctl.status(r.sim.nowMs).weightFresh);
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(shot.endReason == static_cast<uint8_t>(EndReason::WEIGHT) ||
        shot.endReason == static_cast<uint8_t>(EndReason::WEIGHT_PREDICTED));
  // A scale that really stops sending is still caught.
  Rig quiet;
  quiet.toReady();
  quiet.ctl.requestStart();
  CHECK(quiet.runUntil(State::BREWING, 10000));
  quiet.run(9000);
  quiet.sim.scaleSilent = true;
  CHECK(quiet.runUntil(State::SCALE_DATA_STALE, 15000));
}

// -------------------------------------------------------- machine profiles --

// The owner works the machine: lever on, and off again a moment after the cue.
// Returns true if the cue came. reactionMs: how long the owner takes.
static bool pullByHand(Rig &r, uint32_t reactionMs, float stopAtG = 0.0f) {
  r.sim.machineRunning = true;
  r.sim.machineStartedMs = r.sim.nowMs;
  bool cue = false;
  for (uint32_t t = 0; t < 90000; t += 10) {
    r.step();
    const LiveStatus st = r.ctl.status(r.sim.nowMs);
    if (st.targetCue) {
      cue = true;
      break;
    }
    if (stopAtG > 0.0f && st.weightG >= stopAtG) break;
    if (r.ctl.state() != State::BREWING && r.ctl.state() != State::STARTING_SHOT) break;
  }
  if (cue) r.run(reactionMs);
  r.sim.machineRunning = false;
  r.sim.machineStoppedMs = r.sim.nowMs;
  return cue;
}

static void testMachineProfiles() {
  printf("machine profiles: Breville unchanged, generic button, dose button, by hand\n");
  {
    // The default is the Breville Dual Boiler's MANUAL button, and saying so
    // changes nothing: the same shot, press for press.
    const MachineProfile breville = machineBuiltin(0);
    CHECK(breville.controllerStarts() && breville.controllerStops() && breville.holdPreinfusion &&
          strcmp(breville.maker, "Breville") == 0 && strcmp(breville.control, "MANUAL button") == 0);
    Rig a, b;
    b.ctl.setMachine(&breville);
    ShotRecord shot[2];
    Rig *rigs[2] = {&a, &b};
    for (int i = 0; i < 2; ++i) {
      rigs[i]->library.recipes[0].preinfusion = Preinfusion::HOLD;
      rigs[i]->library.recipes[0].preinfusionHoldMs = 4000;
      rigs[i]->sim.ackDelayMs = 4150;
      rigs[i]->toReady();
      rigs[i]->ctl.requestStart();
      CHECK(rigs[i]->runUntil(State::SHOT_COMPLETE, 60000));
      CHECK(rigs[i]->ctl.takeCompletedShot(shot[i]));
    }
    CHECK(a.sim.pressTimes == b.sim.pressTimes && a.sim.holds == b.sim.holds && a.trail == b.trail);
    CHECK(a.sim.presses == 2 && a.sim.holds[0] == 4000 && a.sim.beeps == 0 && b.sim.beeps == 0);
    CHECK(shot[0].finalWeightG == shot[1].finalWeightG && shot[0].stopWeightG == shot[1].stopWeightG &&
          shot[0].shotTimeS == shot[1].shotTimeS && shot[0].flags == shot[1].flags &&
          shot[0].endReason == shot[1].endReason && shot[0].nextStopOffsetG == shot[1].nextStopOffsetG &&
          shot[0].preinfusionHoldMs == 4000 && shot[1].preinfusionHoldMs == 4000);
    CHECK(!a.ctl.status(a.sim.nowMs).targetCue && a.ctl.status(a.sim.nowMs).machineStyle == 0);
  }
  {
    // Another one-button machine: the bot starts and stops it, but its button
    // does not hold pre-infusion, so a recipe's hold is not used on it.
    const MachineProfile button = machineBuiltin(1);
    Rig r;
    r.ctl.setMachine(&button);
    r.library.recipes[0].preinfusion = Preinfusion::HOLD;
    r.library.recipes[0].preinfusionHoldMs = 4000;
    r.toReady();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
    ShotRecord shot;
    CHECK(r.ctl.takeCompletedShot(shot));
    CHECK(r.sim.presses == 2 && r.sim.holds[0] == 0 && shot.preinfusionHoldMs == 0);
    CHECK((shot.flags & SHOT_FLAG_FINAL_VALID) && (shot.flags & SHOT_FLAG_STOP_CONFIRMED));
    CHECK_NEAR(shot.finalWeightG, 36.0, 1.5);
  }
  {
    // A dose button: the bot starts the shot, the machine ends it by itself.
    // The target is a cue and nothing is pressed for it.
    const MachineProfile dose = machineBuiltin(2);
    CHECK(dose.controllerStarts() && !dose.controllerStops());
    Rig r;
    r.ctl.setMachine(&dose);
    r.sim.machineTimeoutMs = 29500;
    r.toReady();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 20000));
    CHECK(r.sim.presses == 1);
    bool cue = false;
    for (uint32_t t = 0; t < 60000 && r.ctl.state() == State::BREWING; t += 10) {
      r.step();
      cue = cue || r.ctl.status(r.sim.nowMs).targetCue;
    }
    CHECK(cue && r.ctl.state() == State::SHOT_COMPLETE && r.sim.presses == 1 && r.sim.beeps == 1);
    ShotRecord shot;
    CHECK(r.ctl.takeCompletedShot(shot));
    CHECK(shot.endReason == static_cast<uint8_t>(EndReason::WEIGHT) && (shot.flags & SHOT_FLAG_FINAL_VALID));
    // STOP on the controller ends the record; it does not press.
    r.nextShot();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 20000));
    r.run(12000);
    r.ctl.requestStop();
    r.run(200);
    CHECK(r.ctl.state() == State::SHOT_COMPLETE && r.sim.presses == 2 && r.sim.machineRunning);
  }
  {
    // By hand, the clock from the tap. No bot is asked for, nothing is pressed.
    const MachineProfile lever = machineBuiltin(3);
    CHECK(!lever.controllerStarts() && !lever.controllerStops() && !lever.startsOnFirstDrip());
    Rig r;
    r.ctl.setMachine(&lever);
    r.sim.preinfusionMs = 4000;
    r.sim.botAvailable = false;            // there is no bot at all
    r.toReady();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 10000));
    CHECK(!r.ctl.actuatorNeeded() && r.sim.presses == 0 && r.sim.tares == 1 && r.sim.timerRunning);
    const uint32_t tappedAt = r.sim.nowMs;
    r.run(800);
    const float offset = r.ctl.status(r.sim.nowMs).stopOffsetG;
    CHECK(pullByHand(r, 700));
    CHECK(r.sim.beeps == 1);               // the scale was asked to sound, once
    CHECK(r.runUntil(State::SHOT_COMPLETE, 15000));
    ShotRecord shot;
    CHECK(r.ctl.takeCompletedShot(shot));
    CHECK(r.sim.presses == 0 && r.sim.botConnects == 0 && r.sim.beeps == 1);
    CHECK(shot.endReason == static_cast<uint8_t>(EndReason::WEIGHT));
    CHECK((shot.flags & SHOT_FLAG_FINAL_VALID) && (shot.flags & SHOT_FLAG_LEARNED));
    // The cue came at target minus offset; what the owner's hand and the drip
    // added on top is what the offset now learns.
    CHECK(shot.finalWeightG > 36.0f && shot.finalWeightG < 39.0f && shot.nextStopOffsetG > offset);
    CHECK(shot.shotTimeS > 20 && shot.shotTimeS < (r.sim.nowMs - tappedAt) / 1000.0f);
    CHECK(shot.firstDropS > 4.0f);         // tap, then lever, then pre-infusion
    // The next shot's cue comes earlier, and the cup ends nearer the target.
    r.nextShot();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 10000));
    r.run(800);
    CHECK(pullByHand(r, 700));
    CHECK(r.runUntil(State::SHOT_COMPLETE, 15000));
    ShotRecord second;
    CHECK(r.ctl.takeCompletedShot(second));
    CHECK(fabsf(second.finalWeightG - 36.0f) < fabsf(shot.finalWeightG - 36.0f));

    // Stopped short by the owner: a shot like any other, and no lesson for the offset.
    r.nextShot();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 10000));
    CHECK(!pullByHand(r, 0, 15.0f));
    CHECK(r.runUntil(State::SHOT_COMPLETE, 15000));
    ShotRecord shortShot;
    CHECK(r.ctl.takeCompletedShot(shortShot));
    CHECK(shortShot.endReason == static_cast<uint8_t>(EndReason::MACHINE_STOPPED) &&
          !(shortShot.flags & SHOT_FLAG_LEARNED) && shortShot.finalWeightG < 20.0f);

    // Tapped, lever never pulled: back to the home screen, not a fault.
    r.nextShot();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 10000));
    CHECK(r.runUntil(State::READY, static_cast<uint32_t>(r.settings.noFlowTimeoutS) * 1000u + 2000));
    CHECK(!r.ctl.takeCompletedShot(shortShot) && r.sim.presses == 0);
  }
  {
    // By hand, the clock from the first coffee: armed until it lands, and the
    // time is then the pour without the pre-infusion.
    MachineProfile lever = machineBuiltin(3);
    lever.manualStart = static_cast<uint8_t>(ManualStart::FIRST_DRIP);
    lever.builtin = 0;
    CHECK(lever.startsOnFirstDrip());
    Rig r;
    r.ctl.setMachine(&lever);
    r.sim.preinfusionMs = 5000;
    r.toReady();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::STARTING_SHOT, 10000));
    r.run(3000);
    CHECK(r.ctl.state() == State::STARTING_SHOT && r.ctl.status(r.sim.nowMs).awaitingDrip);
    CHECK(!r.sim.timerRunning && r.sim.presses == 0);
    const uint32_t leverAt = r.sim.nowMs;
    CHECK(pullByHand(r, 700));
    CHECK(r.sim.timerRunning);             // the scale's timer started with the coffee
    const uint32_t leverOffAt = r.sim.nowMs;
    CHECK(r.runUntil(State::SHOT_COMPLETE, 15000));
    ShotRecord shot;
    CHECK(r.ctl.takeCompletedShot(shot));
    const float leverS = (leverOffAt - leverAt) / 1000.0f;
    CHECK(shot.shotTimeS > leverS - 5.0f - 0.8f && shot.shotTimeS < leverS - 5.0f + 2.5f);
    CHECK(shot.firstDropS < 0.5f && r.sim.presses == 0);
    CHECK(shot.endReason == static_cast<uint8_t>(EndReason::WEIGHT));
    // Armed, then cancelled.
    r.nextShot();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::STARTING_SHOT, 10000));
    r.ctl.requestStop();
    r.run(100);
    CHECK(r.ctl.state() == State::READY);
  }

  {
    // Separate start and stop buttons: the brew bot starts, the stop bot
    // stops, and the shot does not begin until both are there.
    const MachineProfile two = machineBuiltin(4);
    CHECK(two.controllerStarts() && two.controllerStops() && two.separateStop() && !two.holdsForShot());
    Rig r;
    StopPort stop(r.sim);
    stop.ctl = &r.ctl;
    r.ctl.setMachine(&two);
    r.ctl.setStopActuator(&stop);
    r.sim.startOnlyButton = true;
    r.toReady();
    r.ctl.requestStart();
    bool done = false;
    for (uint32_t t = 0; t < 60000 && !done; t += 10) {
      stop.step();
      r.step();
      done = r.ctl.state() == State::SHOT_COMPLETE;
    }
    CHECK(done && r.sim.presses == 1 && r.sim.stopPresses == 1 && !r.sim.machineRunning);
    ShotRecord shot;
    CHECK(r.ctl.takeCompletedShot(shot));
    CHECK((shot.flags & SHOT_FLAG_FINAL_VALID) && (shot.flags & SHOT_FLAG_STOP_CONFIRMED) &&
          (shot.flags & SHOT_FLAG_LEARNED));
    CHECK_NEAR(shot.finalWeightG, 36.0, 1.5);
    CHECK(shot.endReason == static_cast<uint8_t>(EndReason::WEIGHT) ||
          shot.endReason == static_cast<uint8_t>(EndReason::WEIGHT_PREDICTED));
    for (uint32_t t = 0; t < 12000; t += 10) { stop.step(); r.step(); }
    CHECK(!r.ctl.stopActuatorNeeded() && !stop.connected());   // asleep again after the shot

    // No stop bot within reach: the shot is not started at all.
    Rig alone;
    StopPort absent(alone.sim);   // never asked for, so never there
    alone.ctl.setMachine(&two);
    alone.ctl.setStopActuator(&absent);
    alone.sim.startOnlyButton = true;
    alone.toReady();
    alone.ctl.requestStart();
    alone.run(45000);                   // the controller waits up to 40 s for a bot
    CHECK(alone.sim.presses == 0 && alone.ctl.state() == State::SWITCHBOT_DISCONNECTED);
    // On the Breville the stop bot is not part of the machine, set or not.
    Rig breville;
    breville.ctl.setStopActuator(&absent);
    breville.toReady();
    breville.ctl.requestStart();
    CHECK(breville.runUntil(State::SHOT_COMPLETE, 60000) && breville.sim.presses == 2 &&
          !breville.ctl.stopActuatorNeeded());
  }
  {
    // A button that brews while it is held: down for the shot, up to stop.
    const MachineProfile held = machineBuiltin(5);
    CHECK(held.controllerStarts() && held.controllerStops() && held.holdsForShot() && !held.separateStop());
    Rig r;
    r.ctl.setMachine(&held);
    r.toReady();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 20000));
    CHECK(r.sim.holdsBegun == 1 && r.sim.armDown && r.sim.machineRunning && r.sim.presses == 0);
    r.run(10000);
    CHECK(r.ctl.state() == State::BREWING && r.sim.armDown && r.sim.releases == 0);   // still held
    CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
    CHECK(r.sim.releases == 1 && !r.sim.armDown && !r.sim.machineRunning && r.sim.presses == 0);
    ShotRecord shot;
    CHECK(r.ctl.takeCompletedShot(shot));
    CHECK((shot.flags & SHOT_FLAG_FINAL_VALID) && (shot.flags & SHOT_FLAG_STOP_CONFIRMED) &&
          (shot.flags & SHOT_FLAG_LEARNED));
    CHECK_NEAR(shot.finalWeightG, 36.0, 1.5);
    CHECK(shot.shotTimeS > 20 && shot.shotTimeS < 35);
    // STOP while it is held: let go at once.
    r.nextShot();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 20000));
    r.run(9000);
    r.ctl.requestStop();
    r.run(300);
    CHECK(r.sim.releases == 2 && !r.sim.armDown && !r.sim.machineRunning);
    CHECK(r.runUntil(State::SHOT_COMPLETE, 20000));
    // The bot goes quiet while holding: the stop sequence takes over and
    // lets go as soon as it can be reached; until then the owner is told.
    r.nextShot();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::BREWING, 20000));
    r.run(8000);
    r.sim.pressResult = PressResult::UNCONFIRMED;    // the link's word for "lost while holding"
    r.run(100);
    CHECK(r.ctl.state() != State::BREWING && r.sim.releases == 3 && !r.sim.machineRunning);
  }

  // What is stored and sent.
  MachineProfile mine = machineBuiltin(0);
  CHECK(!machineCanBeShared(mine));        // came with the firmware
  strcpy(mine.control, "2 CUP button");
  mine.style = static_cast<uint8_t>(MachineStyle::START_ONLY);
  mine.builtin = 0;
  sanitizeMachine(mine);
  CHECK(mine.holdPreinfusion == 0);        // only a button the bot holds can be held
  CHECK(machineCanBeShared(mine) && machineHash(mine) != machineHash(machineBuiltin(0)));
  char body[SHARE_BODY_MAX];
  CHECK(buildMachineRecord(mine, 7, 0, body, sizeof(body)) != 0);
  CHECK(strstr(body, "\"t\":\"machine\",\"v\":1") && strstr(body, "\"maker\":\"Breville\"") &&
        strstr(body, "\"control\":\"2 CUP button\"") && strstr(body, "\"style\":\"start_button\"") &&
        strstr(body, "\"hold_preinfusion\":0") && !strstr(body, "manual_start"));
  markMachineQueued(mine, 7);
  CHECK(!machineCanBeShared(mine));        // this version was sent
  // What the library says about it reaches it the way it does a coffee or a grinder.
  CHECK(!applyReviewTo(&mine, 6, ShareState::IN_REVIEW, "", ""));      // about an older record
  CHECK(applyReviewTo(&mine, 7, ShareState::IN_REVIEW, "", ""));
  CHECK(mine.shareState == static_cast<uint8_t>(ShareState::IN_REVIEW));
  CHECK(applyReviewTo(&mine, 7, ShareState::DECLINED, "The 2 CUP button is volumetric", ""));
  CHECK(strcmp(mine.reviewNote, "The 2 CUP button is volumetric") == 0 && !machineCanBeShared(mine));
  CHECK(applyReviewTo(&mine, 7, ShareState::PUBLISHED, "", "breville-dual-boiler-2-cup-button"));
  CHECK(!applyReviewTo(&mine, 7, ShareState::PUBLISHED, "", "breville-dual-boiler-2-cup-button"));  // no news
  CHECK(strcmp(mine.libraryId, "breville-dual-boiler-2-cup-button") == 0 && mine.reviewNote[0] == '\0');
  CHECK(machineShareState(mine, 8) == ShareState::PUBLISHED && !machineCanBeShared(mine));
  mine.selfStopS = 30;
  CHECK(machineCanBeShared(mine));         // changed since: it can be sent again
  markMachineQueued(mine, 9);
  CHECK(machineShareState(mine, 9) == ShareState::QUEUED && machineShareState(mine, 10) == ShareState::SENT);
  // The library's checks turned record 9 away: the reason is kept, and the same version may go again.
  CHECK(!machineCanBeShared(mine));
  CHECK(applyReviewTo(&mine, 9, ShareState::REFUSED, "unknown record type 'machine'", ""));
  CHECK(mine.shareState == static_cast<uint8_t>(ShareState::REFUSED) &&
        strcmp(mine.reviewNote, "unknown record type 'machine'") == 0);
  CHECK(strcmp(shareStateName(ShareState::REFUSED), "refused") == 0);
  CHECK(machineCanBeShared(mine));         // refused: send again, unchanged
  markMachineQueued(mine, 10);
  CHECK(!machineCanBeShared(mine) && mine.reviewNote[0] == '\0');
  {
    MachineProfile kept = mine;
    kept.shareState = static_cast<uint8_t>(ShareState::REFUSED);
    sanitizeMachine(kept);
    CHECK(kept.shareState == static_cast<uint8_t>(ShareState::REFUSED));  // survives a reload
  }
  MachineProfile picked = machineBuiltin(3);
  picked.builtin = 0;
  markMachineFromLibrary(picked, "la-pavoni-europiccola-lever");
  CHECK(!machineCanBeShared(picked) && machineShareState(picked, 0) == ShareState::PUBLISHED);
  CHECK(machineStyleFromName("manual") == 2 && machineStyleFromName("nonsense") == 0 &&
        machineStyleFromName("start_stop_buttons") == 3 && machineStyleFromName("hold_button") == 4);
  mine.style = 9;
  sanitizeMachine(mine);
  CHECK(mine.style == 0 && strcmp(mine.control, "MANUAL button") == 0);  // unusable: the default

  // A shared shot says what it was made on, and its ratio as brewed.
  ShotRecord shot;
  shot.id = 61;
  shot.doseG = 18.0f;
  shot.targetWeightG = 36.0f;
  shot.finalWeightG = 37.8f;
  shot.shotTimeS = 29.0f;
  Grinder grinder;
  MachineProfile lever = machineBuiltin(3);
  lever.manualStart = static_cast<uint8_t>(ManualStart::FIRST_DRIP);
  CHECK(buildShotRecord(shot, nullptr, grinder, false, body, sizeof(body), &lever) != 0);
  CHECK(strstr(body, "\"machine\":{\"style\":\"manual\",\"clock_from\":\"first_drip\"") &&
        strstr(body, "\"yield_g\":37.8,\"ratio\":2.1"));
  CHECK(buildShotRecord(shot, nullptr, grinder, false, body, sizeof(body)) != 0);
  CHECK(!strstr(body, "\"machine\"") && strstr(body, "\"ratio\":2.1"));
}

static void testCupAutoTareScale() {
  printf("cup on a scale that tares itself (Lunar auto-tare)\n");
  // Measured on a real Lunar: the cup's weight shows for a second or two,
  // then the scale is back at 0; lifting it shows the negative weight
  // briefly, then 0 again. Both brief (never still) and longer transients.
  for (uint32_t showMs : {300u, 1500u}) {
    Rig r;
    r.settings.requireCup = true;
    r.toReady();
    r.run(1000);
    CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);
    r.sim.trueWeightG += 233.7;                 // cup on...
    r.run(showMs);
    r.sim.tareOffsetG = r.sim.trueWeightG;      // ...and the scale zeroes itself
    r.run(2000);
    CHECK(r.ctl.status(r.sim.nowMs).cupPresent);
    r.run(8000);
    r.sim.trueWeightG -= 233.7;                 // cup off...
    r.run(showMs);
    r.sim.tareOffsetG = r.sim.trueWeightG;      // ...and it zeroes again
    r.run(2000);
    CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);
    r.run(8000);
    CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);  // the zeroing was not a cup
    r.sim.trueWeightG += 233.7;                 // and back on
    r.run(showMs);
    r.sim.tareOffsetG = r.sim.trueWeightG;
    r.run(2000);
    CHECK(r.ctl.status(r.sim.nowMs).cupPresent);
    r.ctl.requestStart();
    CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  }
}

// The first real shot (19 Oct 2026): on the result screen the full cup was
// lifted, put down again for a moment and lifted (the reading went back to
// -268 g, not to 0), and that was taken for a cup going on with the scale
// zeroing itself: the scale was tared empty and a cup believed on it, so the
// next Start would not have asked for one. A scale that zeroes itself comes
// back to 0; a jump that comes back anywhere else is not a cup.
static void testCupPutBackBriefly() {
  printf("cup lifted, put back for a moment and lifted again: no cup\n");
  for (uint32_t backMs : {400u, 900u, 1500u}) {
    Rig r;
    r.settings.requireCup = true;
    r.toReady();
    r.run(1000);
    const double empty = r.sim.trueWeightG;
    r.sim.trueWeightG += 233.7;
    r.run(3000);
    CHECK(r.ctl.status(r.sim.nowMs).cupPresent);
    r.ctl.requestStart();
    CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
    r.run(3000);
    const double full = r.sim.trueWeightG;
    r.sim.trueWeightG = empty;                  // lifted
    r.run(3000);
    CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);
    r.sim.trueWeightG = full;                   // down again for a moment
    r.run(backMs);
    r.sim.trueWeightG = empty;                  // and lifted
    r.run(4000);
    CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);
    CHECK(r.ctl.state() == State::SHOT_COMPLETE);
    r.run(10000);
    CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);
  }
}

static void testEmptyScaleZeroed() {
  printf("empty scale left below zero is zeroed, so the scale can switch itself off\n");
  // A shot on a scale without auto-tare, then the cup with the coffee lifted:
  // the reading sits at minus the cup and coffee until something zeroes it.
  Rig r;
  r.settings.requireCup = true;
  r.toReady();
  r.run(1000);
  r.sim.trueWeightG += 268.5;
  r.run(3000);
  CHECK(r.ctl.status(r.sim.nowMs).cupPresent);
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  r.ctl.acknowledge();
  r.run(2000);
  CHECK(r.ctl.state() == State::READY);
  const int taresBefore = r.sim.tares;
  r.sim.trueWeightG -= 268.5 + 36.0;            // cup and coffee lifted
  r.run(3000);
  CHECK(r.ctl.status(r.sim.nowMs).weightG < -250.0f);
  CHECK(r.sim.tares == taresBefore);            // not at once: it has to be still for a while
  r.run(4000);
  CHECK(r.sim.tares == taresBefore + 1);        // zeroed
  r.run(5000);
  CHECK_NEAR(r.ctl.status(r.sim.nowMs).weightG, 0.0, 0.2);
  CHECK(!r.ctl.status(r.sim.nowMs).cupPresent); // the climb back to 0 is not a cup
  r.run(30000);
  CHECK(r.sim.tares == taresBefore + 1);        // once

  // A fresh cup goes on afterwards and is seen and tared as usual.
  r.sim.trueWeightG += 250.0;
  r.run(3000);
  CHECK(r.ctl.status(r.sim.nowMs).cupPresent);
  CHECK(r.sim.tares == taresBefore + 2);

  // A cup standing on the scale (a positive reading) is never zeroed by this.
  Rig c;
  c.settings.requireCup = true;
  c.toReady();
  c.run(1000);
  c.sim.trueWeightG += 250.0;
  c.run(3000);
  const int withCup = c.sim.tares;
  c.run(30000);
  CHECK(c.sim.tares == withCup);

  // A scale that zeroes itself never sits below zero: nothing extra is sent.
  Rig a;
  a.settings.requireCup = true;
  a.toReady();
  a.run(1000);
  a.sim.trueWeightG += 250.0;
  a.run(500);
  a.sim.tareOffsetG = a.sim.trueWeightG;        // its own auto-tare
  a.run(3000);
  const int beforeLift = a.sim.tares;
  a.sim.trueWeightG -= 250.0;
  a.run(500);
  a.sim.tareOffsetG = a.sim.trueWeightG;        // zeroes itself again
  a.run(30000);
  CHECK(a.sim.tares == beforeLift);
}

static void testScaleRest() {
  printf("idle scale let go so it can switch itself off, taken back on use\n");
  const uint32_t MIN = 60000;
  {
    ScaleRest r;
    uint32_t t = 1000;
    CHECK(!r.update(t, 5, true, true, 0.0f, 0));
    t += 5 * MIN - 1000;
    CHECK(!r.update(t, 5, true, true, 0.1f, 0));    // a little noise is still
    t += 1000;
    CHECK(r.update(t, 5, true, true, 0.0f, 0));      // five minutes: let go
    CHECK(r.resting());
    // Just let go: last heard long ago (it does not advertise while connected), but it is not off.
    CHECK(r.update(t + 50, 5, false, false, 0.0f, 10 * MIN));
    // Let go: no link, still heard advertising for a while.
    CHECK(r.update(t + 10000, 5, false, false, 0.0f, 5000));
    // Gone quiet for 30 s: it has switched itself off; free to connect when switched on.
    CHECK(!r.update(t + 60000, 5, false, false, 0.0f, ScaleRest::QUIET_MS));
  }
  {
    ScaleRest r;  // a reading that moves, or the owner doing something, starts the count again
    uint32_t t = 0;
    r.update(t, 5, true, true, 0.0f, 0);
    t += 4 * MIN;
    r.update(t, 5, true, true, 250.0f, 0);           // a cup went on
    t += 4 * MIN;
    CHECK(!r.update(t, 5, true, true, 250.0f, 0));
    r.activity(t);
    t += 4 * MIN;
    CHECK(!r.update(t, 5, true, true, 250.0f, 0));
    t += MIN;
    CHECK(r.update(t, 5, true, true, 250.0f, 0));    // five minutes after the last of them
    r.activity(t + 1000);                            // the owner is back
    CHECK(!r.resting());
  }
  {
    ScaleRest r;  // never outside READY, never with the setting off
    r.update(0, 5, false, true, 0.0f, 0);
    CHECK(!r.update(30 * MIN, 5, false, true, 0.0f, 0));
    ScaleRest n;
    n.update(0, SCALE_REST_NEVER, true, true, 0.0f, 0);
    CHECK(!n.update(600 * MIN, SCALE_REST_NEVER, true, true, 0.0f, 0));
  }
  {
    ScaleRest r;  // a start asked for while resting goes ahead once, when ready with a cup
    r.update(0, 1, true, true, 0.0f, 0);
    CHECK(r.update(MIN, 1, true, true, 0.0f, 0));
    r.startWhenBack(MIN + 100);
    CHECK(!r.resting() && r.startPending(MIN + 200));
    CHECK(!r.takeStart(MIN + 5000, false, false, false));   // still connecting
    CHECK(!r.takeStart(MIN + 9000, true, true, false));     // back, but no cup yet
    CHECK(r.takeStart(MIN + 12000, true, true, true));      // now
    CHECK(!r.takeStart(MIN + 12100, true, true, true));     // once
    r.startWhenBack(2 * MIN);
    CHECK(!r.takeStart(2 * MIN + ScaleRest::PENDING_START_MS, true, true, true));  // too late
    CHECK(!r.startPending(2 * MIN + ScaleRest::PENDING_START_MS + 1));
  }
  // The setting: 0 is a record saved before it existed, and means the default.
  CHECK(sanitizeScaleRestMin(0) == SCALE_REST_DEFAULT_MIN);
  CHECK(sanitizeScaleRestMin(SCALE_REST_NEVER) == SCALE_REST_NEVER);
  CHECK(sanitizeScaleRestMin(200) == SCALE_REST_MAX_MIN);
  CHECK(sanitizeScaleRestMin(3) == 3);
  Settings s;
  s.scaleRestMin = 0;
  sanitize(s);
  CHECK(s.scaleRestMin == 5);
  CHECK(defaultSettings().scaleRestMin == 5);
  // The colour scheme: 0 (a record from before it) is Dark; an unknown one falls back to Dark.
  CHECK(defaultSettings().uiTheme == static_cast<uint8_t>(UiTheme::DARK));
  s.uiTheme = static_cast<uint8_t>(UiTheme::DRACULA);
  sanitize(s);
  CHECK(s.uiTheme == static_cast<uint8_t>(UiTheme::DRACULA));
  s.uiTheme = UI_THEME_COUNT;
  sanitize(s);
  CHECK(s.uiTheme == 0);
}

static void testCupRemembered() {
  printf("cup remembered across a controller restart\n");
  // A shot, then a fresh cup put on and tared. The scale's timer now holds
  // the shot's time.
  Rig a;
  a.settings.requireCup = true;
  a.toReady();
  a.run(1000);
  a.sim.trueWeightG += 62.0;
  a.run(3000);
  a.ctl.requestStart();
  CHECK(a.runUntil(State::SHOT_COMPLETE, 60000));
  a.ctl.acknowledge();
  a.run(500);
  a.sim.trueWeightG -= 62.0 + 36.0;
  a.run(2000);
  a.sim.trueWeightG += 70.0;                    // fresh cup
  a.run(3000);
  CHECK(a.ctl.status(a.sim.nowMs).cupPresent);
  const CupMemory kept = a.ctl.cupMemory();
  CHECK(kept.present && kept.scaleTimerMs > 0);
  CHECK_NEAR(kept.levelG, 0.0, 0.2);            // tared

  // The controller restarts; the scale stays on with the cup, as it was.
  Rig b;
  b.settings.requireCup = true;
  b.sim.trueWeightG = a.sim.trueWeightG;
  b.sim.tareOffsetG = a.sim.tareOffsetG;
  b.sim.timerHeldMs = a.sim.timerMs();
  b.ctl.restoreCupMemory(kept);
  b.toReady();
  b.run(2000);
  CHECK(b.ctl.status(b.sim.nowMs).cupPresent);
  CHECK(b.sim.tares == 0);                      // nothing new: not tared again
  b.ctl.requestStart();
  CHECK(b.runUntil(State::SHOT_COMPLETE, 60000));

  // The scale was switched off and on meanwhile: it zeroed itself and its
  // timer is back at 0. The same 0 g could be an empty scale: not believed.
  Rig c;
  c.settings.requireCup = true;
  c.sim.trueWeightG = a.sim.trueWeightG;
  c.sim.tareOffsetG = a.sim.trueWeightG;        // zeroed at power-up
  c.sim.timerHeldMs = 0;
  c.ctl.restoreCupMemory(kept);
  c.toReady();
  c.run(2000);
  CHECK(!c.ctl.status(c.sim.nowMs).cupPresent);

  // The cup was taken off while the controller was off: the reading differs.
  Rig d;
  d.settings.requireCup = true;
  d.sim.trueWeightG = a.sim.trueWeightG - 70.0;
  d.sim.tareOffsetG = a.sim.tareOffsetG;
  d.sim.timerHeldMs = a.sim.timerMs();
  d.ctl.restoreCupMemory(kept);
  d.toReady();
  d.run(2000);
  CHECK(!d.ctl.status(d.sim.nowMs).cupPresent);

  // A used cup is not remembered as a fresh one.
  Rig e;
  e.settings.requireCup = true;
  e.toReady();
  e.run(1000);
  e.sim.trueWeightG += 62.0;
  e.run(3000);
  e.ctl.requestStart();
  CHECK(e.runUntil(State::SHOT_COMPLETE, 60000));
  e.run(2000);
  CHECK(!e.ctl.cupMemory().present);
}

// Seen on the board on 21 Oct 2026: a cup remembered at 0 g (tared) was believed again the next
// morning on an empty scale, because a Lunar that comes on empty zeroes itself and its timer still
// shows the value it had. The controller was then ready to brew onto nothing.
static void testCupNotBelievedAfterAWhile() {
  printf("cup: a remembered cup is not believed after the scale was away\n");
  Rig r;
  r.settings.requireCup = true;
  r.toReady();
  r.run(1000);
  r.sim.trueWeightG += 62.0;
  r.run(3000);
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  r.ctl.acknowledge();
  r.run(500);
  r.sim.trueWeightG -= 62.0 + 36.0;
  r.run(2000);
  r.sim.trueWeightG += 268.7;                   // a fresh cup, tared
  r.run(3000);
  CHECK(r.ctl.status(r.sim.nowMs).cupPresent && r.ctl.cupMemory().present);
  const int32_t timer = static_cast<int32_t>(r.sim.timerMs());
  // The scale goes away for the night; the cup is taken off meanwhile. In the morning the scale
  // comes on empty and zeroed, its timer as it was.
  r.sim.scaleLink = false;
  r.run(10 * 60 * 1000);
  r.sim.trueWeightG -= 268.7;
  r.sim.tareOffsetG = r.sim.trueWeightG;
  r.sim.timerHeldMs = static_cast<uint32_t>(timer);
  r.sim.scaleLink = true;
  CHECK(r.runUntil(State::READY, 5000));
  r.run(3000);
  CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);       // nothing on it
  r.ctl.requestStart();
  r.run(1000);
  CHECK(r.ctl.state() == State::READY);               // and so no brew onto it
  // The cup goes on: seen, and the shot brews.
  r.sim.trueWeightG += 268.7;
  r.run(3000);
  CHECK(r.ctl.status(r.sim.nowMs).cupPresent);
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));

  // A short drop of the link (seconds) still keeps the cup, as before.
  Rig k;
  k.settings.requireCup = true;
  k.toReady();
  k.run(1000);
  k.sim.trueWeightG += 268.7;
  k.run(3000);
  CHECK(k.ctl.status(k.sim.nowMs).cupPresent);
  k.sim.scaleLink = false;
  k.run(8000);
  k.sim.scaleLink = true;
  CHECK(k.runUntil(State::READY, 5000));
  k.run(3000);
  // (no scale timer in this one: a cup that was placed and tared is kept only with the timer check)

  // With a cup wrongly believed (the old behaviour), a real cup put on must still be seen and brew.
  Rig q;
  q.settings.requireCup = true;
  q.toReady();
  q.run(1000);
  q.sim.trueWeightG += 62.0;
  q.run(3000);
  q.ctl.requestStart();
  CHECK(q.runUntil(State::SHOT_COMPLETE, 60000));
  q.ctl.acknowledge();
  q.run(500);
  q.sim.trueWeightG -= 62.0 + 36.0;
  q.run(2000);
  q.sim.trueWeightG += 268.7;
  q.run(3000);
  const CupMemory kept = q.ctl.cupMemory();
  Rig w;
  w.settings.requireCup = true;
  w.sim.trueWeightG = q.sim.trueWeightG - 268.7;      // empty
  w.sim.tareOffsetG = w.sim.trueWeightG;              // zeroed
  w.sim.timerHeldMs = q.sim.timerMs();
  w.ctl.restoreCupMemory(kept);
  w.toReady();
  w.run(3000);
  const bool wrongly = w.ctl.status(w.sim.nowMs).cupPresent;
  w.sim.trueWeightG += 268.7;                          // the real cup
  w.run(3000);
  CHECK(w.ctl.status(w.sim.nowMs).cupPresent);
  w.ctl.requestStart();
  CHECK(w.runUntil(State::SHOT_COMPLETE, 60000));
  printf("  (restored on an empty scale after a restart: %s)\n", wrongly ? "yes" : "no");
}

static void testShotDataSet() {
  printf("full data set of a shot\n");
  Rig r;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  const ShotCurve &curve = r.ctl.curve();
  CHECK(curve.count > 200);                       // 10 readings a second, whole shot
  static CurvePoint points[MAX_CURVE_POINTS];
  static float t[MAX_CURVE_POINTS], w[MAX_CURVE_POINTS];
  for (size_t i = 0; i < curve.count; ++i) {
    points[i] = packPoint(curve.timeS[i], curve.weightG[i], curve.timerDs[i], curve.flags[i]);
  }
  // Packing loses nothing that matters: 0.01 s, 0.01 g.
  const size_t n = unpackCurve(points, curve.count, t, w, MAX_CURVE_POINTS);
  CHECK(n == curve.count);
  double worstT = 0, worstW = 0;
  for (size_t i = 0; i < n; ++i) {
    worstT = fmax(worstT, fabs(t[i] - curve.timeS[i]));
    worstW = fmax(worstW, fabs(w[i] - curve.weightG[i]));
  }
  CHECK(worstT <= 0.0051 && worstW <= 0.0051);
  // It runs past the stop: the drip is in it, ending on the final weight.
  CHECK(t[n - 1] > shot.shotTimeS + 0.5f);
  CHECK_NEAR(w[n - 1], shot.finalWeightG, 0.11);

  const ShotDataStats stats = shotDataStats(points, n);
  CHECK(stats.samples == n && stats.timerSamples == n);
  CHECK_NEAR(stats.meanPeriodMs, 100.0, 2.0);
  CHECK(stats.maxGapMs <= 115.0f && stats.gapsOver500Ms == 0);
  // The scale's timer and this controller's clock ran side by side.
  CHECK(stats.timerSpanS > shot.shotTimeS - 1.0f);
  CHECK_NEAR(stats.timerSpanS, stats.clockSpanS, 0.15);
  CHECK(stats.timerLagS >= 0.0f && stats.timerLagS < 0.3f);   // command latency + 0.1 s steps

  // The analysis repeated from the stored readings gives the stored numbers.
  const FlowFeatures again =
      computeFlowFeatures(t, w, n, shot.firstDropS, shot.shotTimeS, shot.stopWeightG);
  CHECK(again.valid);
  CHECK_NEAR(again.flow50GS, shot.flow50GS, 0.02);
  CHECK_NEAR(again.flow25GS, shot.flow25GS, 0.02);
  CHECK_NEAR(again.roughnessGS, shot.flowRoughnessGS, 0.02);
  // Flow at a reading in the middle of the pour: the simulated 1.6 g/s.
  size_t mid = 0;
  while (mid < n && t[mid] < (shot.firstDropS + shot.shotTimeS) / 2) ++mid;
  CHECK_NEAR(curveFlowAt(t, w, n, mid), 1.6, 0.1);
  CHECK_NEAR(curveFlowAt(t, w, n, 2), 0.0, 0.05);       // pre-infusion: nothing yet
  CHECK(std::isnan(curveFlowAt(t, w, 3, 1)));           // too few readings to say

  // A scale that sends no timer (13-byte frames): readings are still complete.
  Rig plain;
  plain.sim.scaleSendsTimer = false;
  plain.toReady();
  plain.ctl.requestStart();
  CHECK(plain.runUntil(State::SHOT_COMPLETE, 60000));
  const ShotCurve &c2 = plain.ctl.curve();
  CHECK(c2.count > 200 && c2.timerDs[c2.count / 2] == CURVE_NO_TIMER);
  for (size_t i = 0; i < c2.count; ++i) {
    points[i] = packPoint(c2.timeS[i], c2.weightG[i], c2.timerDs[i], c2.flags[i]);
  }
  const ShotDataStats none = shotDataStats(points, c2.count);
  CHECK(none.timerSamples == 0 && none.timerSpanS == 0.0f);
  // Limits of the packed point.
  const CurvePoint edge = packPoint(700.0f, -400.0f, packTimerDs(-1), 0);
  CHECK(edge.timeCs == 65535 && edge.weightCg == -32768 && !pointHasTimer(edge));
  CHECK(packTimerDs(87449) == 874);
}

static void testSwitchBotProtocol() {
  printf("switchbot protocol\n");
  uint8_t b[switchbot::MAX_COMMAND_LEN];
  size_t n = switchbot::encodeAction(switchbot::Action::PRESS, "", b);
  CHECK(n == 3 && b[0] == 0x57 && b[1] == 0x01 && b[2] == 0x00);
  n = switchbot::encodeAction(switchbot::Action::DOWN, nullptr, b);
  CHECK(n == 3 && b[2] == 0x03);
  n = switchbot::encodeAction(switchbot::Action::UP, nullptr, b);
  CHECK(n == 3 && b[2] == 0x04);
  // CRC-32 check value for "123456789" is 0xCBF43926.
  CHECK(switchbot::crc32("123456789") == 0xCBF43926u);
  n = switchbot::encodeAction(switchbot::Action::PRESS, "123456789", b);
  CHECK(n == 7 && b[1] == 0x11 && b[2] == 0xCB && b[3] == 0xF4 && b[4] == 0x39 &&
        b[5] == 0x26 && b[6] == 0x00);
  n = switchbot::encodeSetHoldSeconds(3, b);
  CHECK(n == 4 && b[1] == 0x0F && b[2] == 0x08 && b[3] == 3);
  // Example reply from bot.md: 01 64 2C 64 00 00 00 A1 00 00 00 48 00
  const uint8_t info[13] = {0x01, 0x64, 0x2C, 0x64, 0, 0, 0, 0xA1, 0, 0, 0, 0x48, 0};
  const switchbot::BasicInfo i = switchbot::decodeBasicInfo(info, sizeof(info));
  CHECK(i.valid && i.batteryPercent == 100 && i.firmwareTenths == 44 && !i.switchMode);
  CHECK(switchbot::actionAccepted(0x01) && !switchbot::actionAccepted(0x05) &&
        !switchbot::actionAccepted(0x03));
}

static void testAlgorithms() {
  printf("algorithms\n");
  // Worked example from Open Brew's brew-by-weight.md.
  float next = 0;
  CHECK(learnStopOffset(1.5f, 36.2f, 36.0f, 0.30f, next));
  CHECK_NEAR(next, 1.56, 0.001);
  CHECK(learnStopOffset(1.5f, 36.2f, 36.0f, 1.0f, next));
  CHECK_NEAR(next, 1.70, 0.001);
  CHECK(learnStopOffset(0.2f, 35.0f, 36.0f, 1.0f, next));
  CHECK_NEAR(next, 0.0, 0.001);                              // clamped at zero
  CHECK(!learnStopOffset(1.5f, 45.0f, 36.0f, 0.3f, next));   // brewing error, not drip

  float t[20], w[20];
  for (int k = 0; k < 20; ++k) { t[k] = 10.0f + 0.1f * k; w[k] = 12.0f + 2.0f * (0.1f * k); }
  // 2 g/s, 15.8 g at t=11.9 -> reaches 34.5 g at t = 11.9 + 18.7/2 = 21.25 s
  CHECK_NEAR(predictStopTimeS(t, w, 20, 34.5f, 50.0f), 21.25, 0.02);
  CHECK_NEAR(predictStopTimeS(t, w, 9, 34.5f, 50.0f), 50.0, 0.001);   // too few points
  for (int k = 0; k < 20; ++k) w[k] = 12.0f;                           // no flow
  CHECK_NEAR(predictStopTimeS(t, w, 20, 34.5f, 50.0f), 50.0, 0.001);

  FlowEstimator f;
  for (int k = 0; k <= 12; ++k) f.add(1.5f * (k * 0.13f), 5000 + k * 130);  // irregular-ish 130 ms
  CHECK_NEAR(f.flowGS(), 1.5, 0.02);
}

static void testNormalShotAndLearning() {
  printf("normal shot + offset learning\n");
  Rig r;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  for (State s : {State::TARING, State::WAITING_FOR_CUP, State::READY_TO_BREW,
                  State::STARTING_SHOT, State::BREWING, State::STOPPING_SHOT,
                  State::POST_SHOT_DRIP}) {
    CHECK(r.visited(s));
  }
  CHECK(r.sim.presses == 2);
  CHECK(!r.sim.machineRunning);
  CHECK(r.sim.tares == 1);
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(!r.ctl.takeCompletedShot(shot));
  CHECK(shot.flags & SHOT_FLAG_FINAL_VALID);
  CHECK(shot.flags & SHOT_FLAG_STOP_CONFIRMED);
  CHECK(shot.flags & SHOT_FLAG_LEARNED);
  CHECK(shot.stopWeightG < shot.targetWeightG);            // stopped early on purpose
  CHECK(shot.finalWeightG > shot.stopWeightG);             // drip was observed
  CHECK_NEAR(shot.dripWeightG, shot.finalWeightG - shot.stopWeightG, 0.001);
  CHECK_NEAR(shot.finalWeightG, 36.0, 1.5);
  CHECK(shot.shotTimeS > 20 && shot.shotTimeS < 35);
  CHECK_NEAR(shot.averageFlowGS, shot.finalWeightG / shot.shotTimeS, 0.001);
  CHECK_NEAR(shot.peakFlowGS, 1.6, 0.25);
  CHECK(shot.firstDropS > 5.5 && shot.firstDropS < 7.5);
  // Pour time excludes pre-infusion; flow over it is the real pour rate
  // (simulated at 1.6 g/s), where flow over the whole shot reads low.
  CHECK_NEAR(shot.pourTimeS, shot.shotTimeS - shot.firstDropS, 0.001);
  CHECK_NEAR(shot.pourFlowGS, 1.6, 0.2);
  CHECK(shot.averageFlowGS < shot.pourFlowGS - 0.2f);
  CHECK(r.ctl.takeSettingsDirty());
  CHECK_NEAR(r.library.presets[0].stopOffsetG, shot.nextStopOffsetG, 0.0001);

  // The learned offset has to converge on the machine's real drip.
  float lastError = 99;
  for (int k = 0; k < 8; ++k) {
    r.nextShot();
    r.ctl.requestStart();
    CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
    CHECK(r.ctl.takeCompletedShot(shot));
    lastError = shot.finalWeightG - shot.targetWeightG;
  }
  printf("  after learning: final %.2f g, offset %.2f g\n", shot.finalWeightG, shot.stopOffsetG);
  CHECK(fabsf(lastError) <= 0.4f);
  CHECK(r.sim.presses == 18);
}

static void testLearningDisabledAndUserStop() {
  printf("learning off / user stop\n");
  Rig r;
  r.settings.learningEnabled = false;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK_NEAR(r.library.presets[0].stopOffsetG, 1.5, 0.0001);

  Rig u;
  u.toReady();
  u.ctl.requestStart();
  CHECK(u.runUntil(State::BREWING, 10000));
  u.run(12000);
  u.ctl.requestStop();
  CHECK(u.runUntil(State::SHOT_COMPLETE, 15000));
  ShotRecord shot;
  CHECK(u.ctl.takeCompletedShot(shot));
  CHECK(shot.endReason == static_cast<uint8_t>(EndReason::USER));
  CHECK(!(shot.flags & SHOT_FLAG_LEARNED));   // a manual stop does not measure the cutoff
  CHECK(!u.sim.machineRunning && u.sim.presses == 2);
}

static void testCupAfterTare() {
  printf("cup placed after tare\n");
  Rig r;
  r.settings.cupMode = CupMode::CUP_AFTER_TARE;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::WAITING_FOR_CUP, 5000));
  r.run(2000);
  CHECK(r.ctl.state() == State::WAITING_FOR_CUP);
  CHECK(r.sim.presses == 0);
  r.sim.trueWeightG += 62.0;   // cup lands on the pan
  CHECK(r.runUntil(State::BREWING, 10000));
  CHECK(r.sim.tares == 2);
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK_NEAR(shot.finalWeightG, 36.0, 1.5);   // beverage weight, not cup + beverage
}

static void testTimeout() {
  printf("shot timeout\n");
  Rig r;
  r.library.recipes[0].timeLimit = TimeLimit::CONTROLLER;   // "stop after 50 s"
  r.sim.flowGS = 0.25;   // choked puck: never reaches the target
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_TIMEOUT, 70000));
  CHECK(!r.sim.machineRunning && r.sim.presses == 2);
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(shot.endReason == static_cast<uint8_t>(EndReason::TIMEOUT));
  CHECK_NEAR(shot.shotTimeS, 50.0, 0.3);
  CHECK(!(shot.flags & SHOT_FLAG_LEARNED));
}

static void testMachineTimeLimit() {
  printf("machine's own time-out\n");
  // Default: the Bot does not stop a slow shot. The machine ends it, and the
  // controller must notice and never press, because a press would restart it.
  Rig r;
  r.sim.flowGS = 0.8;              // runs slow: 36 g would need about 50 s
  r.sim.machineTimeoutMs = 30000;  // the machine gives up at 30 s
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_TIMEOUT, 60000));
  CHECK(r.sim.presses == 1);       // the start only
  CHECK(!r.sim.machineRunning);
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(shot.endReason == static_cast<uint8_t>(EndReason::MACHINE_STOPPED));
  // When the pour died away (the drip after the machine stopped included),
  // not five seconds later when that was acted on.
  CHECK(shot.shotTimeS > 30.0f && shot.shotTimeS < 33.0f);
  CHECK(shot.finalWeightG > 10.0f && shot.finalWeightG < 30.0f);
  CHECK(shot.flags & SHOT_FLAG_FINAL_VALID);
  CHECK(!(shot.flags & SHOT_FLAG_LEARNED));
  CHECK(!grindShotValid(shot));                   // not a shot to dial in from
  r.run(20000);
  CHECK(r.sim.presses == 1 && !r.sim.machineRunning);

  // A normal shot under the machine's time-out is unaffected.
  Rig n;
  n.sim.machineTimeoutMs = 60000;
  n.toReady();
  n.ctl.requestStart();
  CHECK(n.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK(n.sim.presses == 2 && !n.sim.machineRunning);

  // "Stop after N s" set longer than the machine allows: the machine stops
  // first, and the controller's own limit must not then press.
  Rig c;
  c.library.recipes[0].timeLimit = TimeLimit::CONTROLLER;
  c.library.recipes[0].maxShotTimeS = 50;
  c.sim.flowGS = 0.8;
  c.sim.machineTimeoutMs = 30000;
  c.toReady();
  c.ctl.requestStart();
  CHECK(c.runUntil(State::SHOT_TIMEOUT, 70000));
  c.run(30000);
  CHECK(c.sim.presses == 1 && !c.sim.machineRunning);

  // "Stop after N s" shorter than the machine's: the Bot stops it at N.
  Rig e;
  e.library.recipes[0].timeLimit = TimeLimit::CONTROLLER;
  e.library.recipes[0].maxShotTimeS = 20;
  e.sim.flowGS = 0.8;
  e.sim.machineTimeoutMs = 60000;
  e.toReady();
  e.ctl.requestStart();
  CHECK(e.runUntil(State::SHOT_TIMEOUT, 40000));
  CHECK(e.sim.presses == 2 && !e.sim.machineRunning);
  CHECK(e.ctl.takeCompletedShot(shot));
  CHECK(shot.endReason == static_cast<uint8_t>(EndReason::TIMEOUT));
  CHECK_NEAR(shot.shotTimeS, 20.0, 0.3);

  // Machine in charge, but it never times out and the puck is choked: at the
  // absolute ceiling the controller lets go without pressing.
  Rig k;
  k.sim.flowGS = 0.1;
  k.toReady();
  k.ctl.requestStart();
  CHECK(k.runUntil(State::SHOT_TIMEOUT, 120000));
  CHECK(k.sim.presses == 1);
}

static void testScaleLost() {
  printf("scale lost mid-shot\n");
  Rig r;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::BREWING, 10000));
  r.run(12000);
  r.sim.scaleLink = false;
  r.run(100);
  CHECK(!r.sim.machineRunning);               // stopped within 100 ms of the loss
  CHECK(r.runUntil(State::SCALE_DISCONNECTED, 10000));
  CHECK(r.sim.presses == 2);

  Rig s;
  s.toReady();
  s.ctl.requestStart();
  CHECK(s.runUntil(State::BREWING, 10000));
  s.run(12000);
  s.sim.scaleSilent = true;                   // link up, no data
  s.run(s.settings.scaleTimeoutMs + 100);
  CHECK(!s.sim.machineRunning);
  CHECK(s.runUntil(State::SCALE_DATA_STALE, 10000));
  CHECK(s.sim.presses == 2);
}

static void testCupRemoved() {
  printf("cup removed mid-shot\n");
  Rig r;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::BREWING, 10000));
  r.run(12000);
  const float before = r.ctl.status(r.sim.nowMs).weightG;
  r.sim.trueWeightG -= 150.0;
  r.run(500);
  CHECK(!r.sim.machineRunning);                 // stopped within half a second of the lift
  CHECK(r.runUntil(State::INVALID_WEIGHT, 10000));
  CHECK(r.sim.presses == 2);
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(shot.endReason == static_cast<uint8_t>(EndReason::CUP_REMOVED));
  CHECK_NEAR(shot.finalWeightG, before, 0.5);   // what was in the cup when it left
  CHECK(!(shot.flags & SHOT_FLAG_FINAL_VALID));
  CHECK(grindShotUse(shot) != GrindUse::COUNTED);

  // A real scale smooths its output, so the lift arrives as a slide down
  // over several readings rather than one jump. The recorded weight must be
  // what the cup held, not a reading caught on the way down.
  Rig slide;
  slide.toReady();
  slide.ctl.requestStart();
  CHECK(slide.runUntil(State::BREWING, 10000));
  slide.run(12000);
  const float held = slide.ctl.status(slide.sim.nowMs).weightG;
  for (int k = 0; k < 6; ++k) {                  // -12 g per reading
    slide.sim.trueWeightG -= 12.0;
    slide.run(100);
  }
  slide.run(400);
  CHECK(!slide.sim.machineRunning && slide.sim.presses == 2);
  CHECK(slide.runUntil(State::INVALID_WEIGHT, 10000));
  CHECK(slide.ctl.takeCompletedShot(shot));
  CHECK(shot.endReason == static_cast<uint8_t>(EndReason::CUP_REMOVED));
  CHECK_NEAR(shot.finalWeightG, held, 1.0);

  // A light paper cup: the reading only falls a little below zero.
  Rig light;
  light.toReady();
  light.ctl.requestStart();
  CHECK(light.runUntil(State::BREWING, 10000));
  light.run(9000);                               // a few grams in
  light.sim.trueWeightG -= 12.0;
  CHECK(light.runUntil(State::INVALID_WEIGHT, 10000));
  CHECK(!light.sim.machineRunning && light.sim.presses == 2);

  // The shot is stopped at the machine by hand, and the cup is taken a moment
  // later, before the controller has registered the stop. It must not press:
  // that would start the pump again with no cup under it.
  Rig hand;
  hand.toReady();
  hand.ctl.requestStart();
  CHECK(hand.runUntil(State::BREWING, 10000));
  hand.run(14000);
  hand.sim.machineRunning = false;               // MANUAL pressed by hand
  hand.sim.machineStoppedMs = hand.sim.nowMs;
  hand.run(2500);                                // drip ends
  hand.sim.trueWeightG -= 150.0;                 // cup lifted
  CHECK(hand.runUntil(State::SHOT_TIMEOUT, 5000));
  hand.run(5000);
  CHECK(hand.sim.presses == 1 && !hand.sim.machineRunning);
  CHECK(hand.ctl.takeCompletedShot(shot));
  CHECK(shot.endReason == static_cast<uint8_t>(EndReason::MACHINE_STOPPED));

  // Stopped at the machine and the cup left alone: noticed after 5 s, still
  // without a press.
  Rig quiet;
  quiet.toReady();
  quiet.ctl.requestStart();
  CHECK(quiet.runUntil(State::BREWING, 10000));
  quiet.run(14000);
  quiet.sim.machineRunning = false;
  quiet.sim.machineStoppedMs = quiet.sim.nowMs;
  CHECK(quiet.runUntil(State::SHOT_TIMEOUT, 10000));
  CHECK(quiet.sim.presses == 1);
}

static void testStopPressMissed() {
  printf("stop press missed by the machine\n");
  Rig r;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::BREWING, 10000));
  r.sim.ignorePresses = 1;                    // arm moves, button not actuated
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK(!r.sim.machineRunning);
  CHECK(r.sim.presses == 3);                  // start, missed stop, retry
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(shot.flags & SHOT_FLAG_STOP_RETRIED);
  CHECK(!(shot.flags & SHOT_FLAG_LEARNED));
  CHECK_NEAR(r.library.presets[0].stopOffsetG, 1.5, 0.0001);
}

// The first real shots (19 Oct 2026): the Breville went on at full flow for
// about 1.5 s after the stop, 3.6-3.8 g needed against the 1.5 g seed. At
// 30% a step the stop point took 4-5 shots to get there, each a gram or
// more over. With the warm-up the first shots take most of the error.
static void testStopOffsetWarmUp() {
  printf("stop offset warm-up: a new recipe gets close in two or three shots\n");
  Rig r;
  r.sim.flowGS = 1.7;
  r.sim.stopDelayMs = 1500;
  r.sim.dripGS = 0.6;
  r.sim.dripMs = 800;
  r.toReady();
  float errors[6] = {};
  uint8_t used[6] = {};
  for (int k = 0; k < 6; ++k) {
    r.ctl.requestStart();
    CHECK(r.runUntil(State::SHOT_COMPLETE, 90000));
    ShotRecord shot;
    CHECK(r.ctl.takeCompletedShot(shot));
    CHECK(shot.flags & SHOT_FLAG_LEARNED);
    errors[k] = shot.finalWeightG - shot.targetWeightG;
    used[k] = shot.alphaUsed;
    r.nextShot();
  }
  printf("  over target: %.2f %.2f %.2f %.2f %.2f %.2f g\n", errors[0], errors[1], errors[2],
         errors[3], errors[4], errors[5]);
  CHECK(errors[0] > 1.0f);                    // the seed is well short
  CHECK(fabsf(errors[2]) < 0.6f);             // close by the third shot
  CHECK(fabsf(errors[5]) < 0.4f);
  CHECK(used[0] == 70 && used[3] == 70 && used[4] == 30);  // four warm-up steps, then the usual
  CHECK(r.library.presets[0].learnedShots == 6);
}

static void testSlowStopIsNotPressedAgain() {
  printf("a stop that takes the machine 1.7 s is not pressed again\n");
  // Measured on the Breville with an empty basket: 7-8 g/s goes on for about
  // 1.7 s after the stop press, then tails off. The old rule pressed again
  // 2.5 s after the stop, on the tail, which starts the pump again.
  Rig r;
  r.sim.flowGS = 7.5;
  r.sim.stopDelayMs = 1700;
  r.sim.dripGS = 3.0;
  r.sim.dripMs = 1500;
  {
    // Water at 7.5 g/s: about 15 g over, more than any coffee shot. Not pressed
    // again, and not learned from (it would teach a coffee recipe nonsense).
    Rig w;
    w.sim.flowGS = 7.5;
    w.sim.stopDelayMs = 1700;
    w.sim.dripGS = 3.0;
    w.sim.dripMs = 1500;
    w.toReady();
    w.ctl.requestStart();
    CHECK(w.runUntil(State::SHOT_COMPLETE, 60000));
    CHECK(w.sim.presses == 2 && !w.sim.machineRunning);
    ShotRecord water;
    CHECK(w.ctl.takeCompletedShot(water));
    CHECK(!(water.flags & SHOT_FLAG_STOP_RETRIED) && !(water.flags & SHOT_FLAG_LEARNED));
  }
  // Coffee at 2.5 g/s with the same slow stop: about 5 g over, learned from.
  r.sim.flowGS = 2.5;
  r.sim.dripGS = 1.0;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK(r.sim.presses == 2);                  // start and stop, nothing more
  CHECK(!r.sim.machineRunning);
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(!(shot.flags & SHOT_FLAG_STOP_RETRIED));
  CHECK(shot.flags & SHOT_FLAG_LEARNED);      // and the overshoot is learned from
  CHECK(r.library.presets[0].stopOffsetG > 2.0f);
  CHECK(shot.finalWeightG - shot.stopWeightG > 4.0f);
}

static void testStopNeverWorks() {
  printf("stop never takes effect\n");
  Rig r;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::BREWING, 10000));
  r.sim.ignorePresses = 100;
  CHECK(r.runUntil(State::STOP_COMMAND_FAILED, 60000));
  CHECK(r.ctl.status(r.sim.nowMs).manualStopNeeded);
  CHECK(r.sim.presses == 4);                  // start + stop + 2 bounded retries
  CHECK(r.ctl.machineMayBeRunning());
}

static void testStartNotDelivered() {
  printf("start press has no effect\n");
  Rig r;
  r.sim.ignorePresses = 1;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::START_COMMAND_FAILED, 40000));
  // No coffee was seen, so no second press: it would have started the pump.
  CHECK(r.sim.presses == 1);
  CHECK(!r.sim.machineRunning);
  ShotRecord shot;
  CHECK(!r.ctl.takeCompletedShot(shot));
  r.ctl.acknowledge();
  CHECK(r.runUntil(State::READY, 1000));
}

static void testLateFlowIsAdopted() {
  printf("late flow after a 'failed' start is supervised\n");
  Rig r;
  r.sim.preinfusionMs = 24000;                // first drops arrive after the no-flow timeout
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::START_COMMAND_FAILED, 30000));
  CHECK(r.sim.machineRunning);
  CHECK(r.runUntil(State::BREWING, 15000));   // flow appeared: back under control
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000) || r.ctl.state() == State::SHOT_TIMEOUT);
  r.run(8000);
  CHECK(!r.sim.machineRunning);
}

static void testBotDropsAtStop() {
  printf("bot link down at stop time\n");
  Rig r;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::BREWING, 10000));
  r.run(15000);
  r.sim.botAvailable = false;
  CHECK(r.runUntil(State::STOPPING_SHOT, 30000));
  r.run(2000);
  CHECK(r.ctl.status(r.sim.nowMs).manualStopNeeded);
  CHECK(r.sim.machineRunning);
  r.sim.botAvailable = true;                       // reconnected
  r.run(100);
  CHECK(!r.sim.machineRunning);                // stop delivered immediately
  CHECK(r.runUntil(State::SHOT_COMPLETE, 15000));
  CHECK(!r.ctl.status(r.sim.nowMs).manualStopNeeded);
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(shot.finalWeightG > 36.5);             // overshoot is recorded honestly
}

static void testUnconfirmedStop() {
  printf("stop acknowledgement lost, press did happen\n");
  Rig r;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::BREWING, 10000));
  r.run(8000);
  r.sim.unconfirmedPresses = 1;
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK(r.sim.presses == 2);                   // no second press: it would restart the pump
  CHECK(!r.sim.machineRunning);
}

static void testPreconditions() {
  printf("start preconditions / tare failure\n");
  Rig r;
  r.sim.botAvailable = false;                  // dead battery / out of range
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::READY_TO_BREW, 5000));   // tare does not wait for the Bot
  CHECK(r.runUntil(State::SWITCHBOT_DISCONNECTED, 45000));  // waits up to 40 s
  CHECK(r.sim.presses == 0);
  CHECK(!r.ctl.actuatorNeeded());              // gives the link up again
  r.ctl.acknowledge();
  CHECK(r.runUntil(State::READY, 1000));

  Rig t;
  t.sim.trueWeightG = 50.0;
  t.sim.scaleIgnoresTare = true;
  t.toReady();
  t.ctl.requestStart();
  CHECK(t.runUntil(State::TARE_FAILED, 10000));
  CHECK(t.sim.tares == 3 && t.sim.presses == 0);

  Rig c;
  c.toReady();
  c.sim.scaleLink = false;                     // Lunar goes to sleep while idle
  c.run(200);
  CHECK(c.ctl.state() == State::CONNECTING_SCALE);   // not a fault
  c.sim.scaleLink = true;
  CHECK(c.runUntil(State::READY, 3000));

  Rig m;
  m.settings.autoStartAfterTare = false;
  m.toReady();
  m.ctl.requestStart();
  CHECK(m.runUntil(State::READY_TO_BREW, 5000));
  m.run(3000);
  CHECK(m.ctl.state() == State::READY_TO_BREW && m.sim.presses == 0);
  m.ctl.requestStop();                          // cancel
  m.run(50);
  CHECK(m.ctl.state() == State::READY && m.sim.presses == 0);
}

static void testBotOnlyConnectedAroundShot() {
  printf("bot link only held around a shot\n");
  Rig r;
  r.sim.botConnectDelayMs = 4000;              // slow to wake
  r.toReady();
  r.run(5000);
  CHECK(!r.ctl.actuatorNeeded() && !r.sim.botLink);   // idle: Bot left asleep
  r.ctl.requestStart();
  r.run(50);
  CHECK(r.ctl.actuatorNeeded());               // woken by Start
  CHECK(r.runUntil(State::READY_TO_BREW, 5000));
  CHECK(r.sim.presses == 0);                   // waits for the link, no fault
  CHECK(r.runUntil(State::BREWING, 6000));
  bool heldThroughout = true;
  while (r.ctl.state() != State::SHOT_COMPLETE && r.sim.nowMs < 200000) {
    r.step();
    if (r.ctl.state() != State::SHOT_COMPLETE && !r.ctl.actuatorNeeded()) heldThroughout = false;
  }
  CHECK(r.ctl.state() == State::SHOT_COMPLETE);
  CHECK(heldThroughout);                       // never released mid-shot
  CHECK(r.sim.botConnects == 1);
  CHECK(!r.ctl.actuatorNeeded());              // released once the stop is confirmed
  CHECK(!r.sim.machineRunning && r.sim.presses == 2);
}

static void testAdaptiveLearningFactor() {
  printf("adaptive learning factor\n");
  // Selector on its own: the offset should be 2.0 g but learning crawls at
  // 0.10. With clean data full correction wins every evaluation, so the
  // switch comes at the earliest point Open Brew's rules allow: 20 held
  // observations, then two winning evaluations five apart = observation 25.
  AlphaEvidence e;
  uint8_t alpha = 10;
  int switchedAt = 0;
  float offset = 0.5f;
  for (int k = 1; k <= 40; ++k) {
    const uint8_t next = e.observe(2.0f, offset, alpha);
    offset = clampOffset(offset + (alpha / 100.0f) * (2.0f - offset));
    if (next != alpha && switchedAt == 0) switchedAt = k;
    alpha = next;
  }
  CHECK(switchedAt == 25);
  CHECK(alpha == 100);

  // Same situation with +-0.05 g of noise: the best candidate changes from
  // one evaluation to the next, so the switch takes longer but still comes.
  // (Once the offset has caught up, noise alone favours a slow gain again,
  // so the final value is not asserted.)
  AlphaEvidence noisy;
  alpha = 10;
  switchedAt = 0;
  offset = 0.5f;
  for (int k = 1; k <= 60; ++k) {
    const float observation = 2.0f + ((k % 2) ? 0.05f : -0.05f);
    const uint8_t next = noisy.observe(observation, offset, alpha);
    offset = clampOffset(offset + (alpha / 100.0f) * (observation - offset));
    if (next != alpha && switchedAt == 0) switchedAt = k;
    alpha = next;
  }
  CHECK(switchedAt >= 25);

  // Noise around a correct offset: no reason to switch from 0.30.
  AlphaEvidence steady;
  alpha = 30;
  const float noise[5] = {0.12f, -0.08f, 0.03f, -0.11f, 0.05f};
  for (int k = 0; k < 60; ++k) {
    alpha = steady.observe(1.5f + noise[k % 5], 1.5f, alpha);
  }
  CHECK(alpha == 30 || alpha == 10);          // never towards a twitchier gain
  CHECK(steady.observe(NAN, 1.5f, 30) == 30); // bad data is ignored
  CHECK(steady.observe(9.0f, 1.5f, 30) == 30);

  // End to end: fixed mode never touches the per-recipe factor...
  Rig fixed;
  fixed.toReady();
  fixed.ctl.requestStart();
  CHECK(fixed.runUntil(State::SHOT_COMPLETE, 60000));
  ShotRecord shot;
  CHECK(fixed.ctl.takeCompletedShot(shot));
  CHECK(shot.alphaUsed == 70 && shot.alphaNext == 30);  // a warm-up step, then the usual
  CHECK(fixed.library.presets[0].learnedAlpha == 0);

  // ...adaptive mode starts from the baseline and records what it uses.
  Rig a;
  a.settings.adaptiveLearning = true;
  a.settings.learningFactor = 0.10f;
  a.library.presets[0].stopOffsetG = 0.0f;    // far from the machine's real drip
  a.toReady();
  for (int k = 0; k < 30; ++k) {
    a.ctl.requestStart();
    CHECK(a.runUntil(State::SHOT_COMPLETE, 60000));
    CHECK(a.ctl.takeCompletedShot(shot));
    if (k == 0) CHECK(shot.alphaUsed == 70 && shot.alphaNext == 10);  // warm-up
    if (k == 4) CHECK(shot.alphaUsed == 10);
    a.nextShot();
  }
  CHECK(validAlpha(a.library.presets[0].learnedAlpha));
  CHECK(shot.alphaUsed == a.library.presets[0].learnedAlpha ||
        shot.alphaNext == a.library.presets[0].learnedAlpha);
  printf("  after 30 shots: learning factor %.2f, offset %.2f g, last final %.2f g\n",
         a.library.presets[0].learnedAlpha / 100.0f, a.library.presets[0].stopOffsetG,
         shot.finalWeightG);
  CHECK_NEAR(shot.finalWeightG, 36.0, 0.5);
}

static ShotRecord grindRecord(float timeS, uint16_t um, float targetTimeS = 28.0f) {
  ShotRecord s;
  s.shotTimeS = timeS;
  s.grindUm = um;
  s.targetTimeS = targetTimeS;
  s.endReason = static_cast<uint8_t>(EndReason::WEIGHT);
  s.flags = SHOT_FLAG_FINAL_VALID | SHOT_FLAG_STOP_CONFIRMED;
  return s;
}

static void testGrindAdvisor() {
  printf("grind advisor\n");
  GrindConfig cfg;  // +-2 s, 3 um/s guess, window 3, max step 20 um

  // Mahlkoenig's valid brew event: 15 s < time < target + 15 s, not interfered.
  CHECK(grindShotValid(grindRecord(27.0f, 250)));
  CHECK(!grindShotValid(grindRecord(14.9f, 250)));          // a flush, not a shot
  CHECK(!grindShotValid(grindRecord(43.5f, 250)));          // more than target + 15 s
  CHECK(!grindShotValid(grindRecord(27.0f, 0)));            // grind not recorded
  ShotRecord manual = grindRecord(27.0f, 250);
  manual.endReason = static_cast<uint8_t>(EndReason::USER);
  CHECK(!grindShotValid(manual));                           // stopped by hand
  ShotRecord lifted = grindRecord(27.0f, 250);
  lifted.flags = 0;
  CHECK(!grindShotValid(lifted));                           // cup lifted early

  // No setting entered, then no shot at the setting.
  CHECK(adviseGrind(nullptr, 0, 0, 28.0f, cfg).action == GrindAction::NO_GRIND_SET);
  CHECK(adviseGrind(nullptr, 0, 250, 28.0f, cfg).action == GrindAction::NO_DATA);

  // Inside tolerance: leave it alone.
  const GrindShot ok[] = {{29.0f, 250}, {27.5f, 250}, {28.6f, 250}};
  GrindAdvice a = adviseGrind(ok, 3, 250, 28.0f, cfg);
  CHECK(a.action == GrindAction::HOLD && a.deltaUm == 0 && a.shotsUsed == 3);

  // Fast shots = too coarse = go finer. 4 s fast x 3 um/s guess = 12 um.
  const GrindShot fast[] = {{24.0f, 250}, {24.5f, 250}, {23.5f, 250}};
  a = adviseGrind(fast, 3, 250, 28.0f, cfg);
  CHECK(a.action == GrindAction::FINER && a.deltaUm == -12 && a.suggestedUm == 238);
  CHECK(!a.learned);
  CHECK_NEAR(a.errorS, -4.0, 0.01);

  // Slow shots = too fine = go coarser, and the step is capped.
  const GrindShot slow[] = {{38.0f, 250}};
  a = adviseGrind(slow, 1, 250, 28.0f, cfg);
  CHECK(a.action == GrindAction::COARSER && a.deltaUm == 20 && a.stepLimited);

  // Only shots at the current setting are averaged; the window caps how many.
  const GrindShot mixed[] = {{25.0f, 244}, {31.0f, 250}, {31.5f, 250}};
  a = adviseGrind(mixed, 3, 244, 28.0f, cfg);
  CHECK(a.shotsUsed == 1);
  CHECK_NEAR(a.averageTimeS, 25.0, 0.001);
  cfg.window = 2;
  const GrindShot many[] = {{30.5f, 250}, {30.5f, 250}, {20.0f, 250}, {20.0f, 250}};
  a = adviseGrind(many, 4, 250, 28.0f, cfg);
  CHECK(a.shotsUsed == 2);
  CHECK_NEAR(a.averageTimeS, 30.5, 0.001);
  cfg.window = 3;

  // Sensitivity measured from history: 10 um coarser made shots 5 s faster,
  // so 2 um per second. A 3 s slow shot then calls for 6 um coarser.
  const GrindShot hist[] = {{31.0f, 240}, {31.2f, 240}, {26.0f, 250}, {26.1f, 250},
                            {36.0f, 230}, {36.2f, 230}};
  float umPerS = 0;
  CHECK(learnGrindSensitivity(hist, 6, umPerS));
  CHECK_NEAR(umPerS, 2.0, 0.1);
  a = adviseGrind(hist, 6, 240, 28.0f, cfg);
  CHECK(a.learned && a.action == GrindAction::COARSER);
  CHECK(a.deltaUm >= 5 && a.deltaUm <= 7);

  // History that says "coarser is slower" is noise: keep the guess.
  const GrindShot noise[] = {{26.0f, 240}, {31.0f, 250}, {36.0f, 260}};
  CHECK(!learnGrindSensitivity(noise, 3, umPerS));
  // One setting only: nothing to measure.
  CHECK(!learnGrindSensitivity(ok, 3, umPerS));

  // Just outside tolerance still moves at least one micron.
  cfg.defaultUmPerS = 0.5f;
  cfg.toleranceS = 0.5f;
  const GrindShot slight[] = {{28.6f, 250}};
  a = adviseGrind(slight, 1, 250, 28.0f, cfg);
  CHECK(a.action == GrindAction::COARSER && a.deltaUm == 1);

  // The controller stamps each shot with the grinder setting, dose and target time.
  Rig r;
  r.library.presets[0].grindUm = 246;
  r.library.recipes[0].targetTimeS = 30.0f;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(shot.grindUm == 246);
  CHECK_NEAR(shot.doseG, 18.0, 0.001);
  CHECK_NEAR(shot.targetTimeS, 30.0, 0.001);
  CHECK(grindShotValid(shot));

  // Time basis. A shot is only compared on the basis its target was set for,
  // so switching basis starts a fresh comparison instead of mixing the two.
  CHECK(!grindShotValid(shot, GrindTimeBasis::POUR));
  ShotRecord pour = grindRecord(27.0f, 250, 20.0f);
  pour.timeBasis = static_cast<uint8_t>(GrindTimeBasis::POUR);
  pour.pourTimeS = 19.0f;
  CHECK(grindShotValid(pour, GrindTimeBasis::POUR));
  CHECK_NEAR(grindShotTime(pour, GrindTimeBasis::POUR), 19.0, 0.001);
  CHECK_NEAR(grindShotTime(pour, GrindTimeBasis::TOTAL), 27.0, 0.001);
  pour.pourTimeS = 0.0f;                                    // first drops never detected
  CHECK(!grindShotValid(pour, GrindTimeBasis::POUR));

  Rig p;
  p.library.presets[0].grindUm = 246;
  p.settings.grindTimeBasis = GrindTimeBasis::POUR;
  p.library.recipes[0].targetTimeS = 21.0f;
  p.toReady();
  p.ctl.requestStart();
  CHECK(p.runUntil(State::BREWING, 10000));
  p.run(3000);
  CHECK(!p.ctl.status(p.sim.nowMs).firstDropSeen);          // still pre-infusing
  p.run(6000);
  CHECK(p.ctl.status(p.sim.nowMs).firstDropSeen);           // coffee in the cup
  CHECK(p.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK(p.ctl.takeCompletedShot(shot));
  CHECK(shot.timeBasis == static_cast<uint8_t>(GrindTimeBasis::POUR));
  CHECK(grindShotValid(shot, GrindTimeBasis::POUR));
}

static void testFlowFeatures() {
  printf("flow features\n");
  // 10 readings a second, first drops at 7 s, stop at 25 s.
  static float t[400], w[400];
  size_t n = 0;
  auto build = [&](float (*weightAt)(float)) {
    n = 0;
    for (float s = 0.0f; s <= 25.0f; s += 0.1f) {
      t[n] = s;
      w[n] = s < 7.0f ? 0.0f : roundf(weightAt(s - 7.0f) * 10.0f) / 10.0f;  // 0.1 g scale
      ++n;
    }
  };

  build([](float x) { return 2.0f * x; });                 // steady 2 g/s
  FlowFeatures f = computeFlowFeatures(t, w, n, 7.0f, 25.0f, w[n - 1]);
  CHECK(f.valid);
  CHECK_NEAR(f.flow25GS, 2.0, 0.1);
  CHECK_NEAR(f.flow50GS, 2.0, 0.1);
  CHECK_NEAR(f.flow75GS, 2.0, 0.1);
  CHECK_NEAR(f.acceleration, 1.0, 0.08);
  const float steadyRoughness = f.roughnessGS;
  CHECK(steadyRoughness < 0.08f);

  build([](float x) { return 1.0f * x + 0.06f * x * x; }); // speeds up: 1 -> 3.2 g/s
  f = computeFlowFeatures(t, w, n, 7.0f, 25.0f, w[n - 1]);
  CHECK(f.valid);
  CHECK(f.flow25GS < f.flow50GS && f.flow50GS < f.flow75GS);
  CHECK(f.acceleration > 1.3f);
  CHECK(f.roughnessGS < 0.12f);                            // smooth, just not constant

  build([](float x) { return 2.0f * x + 1.2f * sinf(x * 1.3f); });  // surging
  f = computeFlowFeatures(t, w, n, 7.0f, 25.0f, w[n - 1]);
  CHECK(f.valid);
  CHECK(f.roughnessGS > 5.0f * steadyRoughness && f.roughnessGS > 0.5f);

  // Too short or too small to say anything.
  CHECK(!computeFlowFeatures(t, w, n, 7.0f, 8.5f, 3.0f).valid);
  CHECK(!computeFlowFeatures(t, w, 5, 7.0f, 25.0f, 36.0f).valid);

  // A whole simulated shot carries them in its record.
  Rig r;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK_NEAR(shot.flow50GS, 1.6, 0.15);
  CHECK_NEAR(shot.flowAcceleration, 1.0, 0.15);
  CHECK(shot.flowRoughnessGS < 0.15f);
}

static void testGrindOutliers() {
  printf("grind advice: one-off shots\n");
  GrindConfig cfg;  // +-2 s tolerance, outlier beyond 4 s from the median

  // Three good shots, then one that gushed. The grind was not the problem.
  const GrindShot oneOff[] = {{21.0f, 250}, {28.5f, 250}, {28.0f, 250}, {27.6f, 250}};
  GrindAdvice a = adviseGrind(oneOff, 4, 250, 28.0f, cfg);
  CHECK(a.action == GrindAction::HOLD);
  CHECK(a.outliersIgnored == 1 && a.shotsUsed == 3);

  // Fast and slow at the same setting (reported: 18.7, 36.6, 19.3 s against
  // 26 s averaged to 24.9 s and "keep grind"). No advice from that average.
  const GrindShot swing[] = {{18.74f, 246}, {36.57f, 246}, {19.33f, 246}, {31.19f, 246}};
  a = adviseGrind(swing, 4, 246, 26.0f, cfg);
  CHECK(a.action == GrindAction::INCONSISTENT);
  CHECK(a.deltaUm == 0 && a.suggestedUm == 246);
  CHECK_NEAR(a.minTimeS, 18.74, 0.01);
  CHECK_NEAR(a.maxTimeS, 36.57, 0.01);
  // Fast and on-target together is not a contradiction: the trend is advised.
  const GrindShot drifting[] = {{21.0f, 246}, {25.5f, 246}};
  a = adviseGrind(drifting, 2, 246, 26.0f, cfg);
  CHECK(a.action == GrindAction::FINER);
  // After the setting is changed and entered, the old shots no longer count.
  const GrindShot moved[] = {{25.0f, 240}, {18.74f, 246}, {36.57f, 246}};
  a = adviseGrind(moved, 3, 240, 26.0f, cfg);
  CHECK(a.action == GrindAction::HOLD && a.shotsUsed == 1);

  // The same fast time twice running is a real change: it gets through.
  const GrindShot lasting[] = {{21.0f, 250}, {21.5f, 250}, {28.0f, 250}, {28.2f, 250}};
  a = adviseGrind(lasting, 4, 250, 28.0f, cfg);
  CHECK(a.action == GrindAction::FINER);
  CHECK(a.outliersIgnored == 0);

  // And once it has lasted, it is the old shots that no longer fit.
  const GrindShot settled[] = {{21.0f, 250}, {21.5f, 250}, {21.2f, 250}, {28.0f, 250}};
  a = adviseGrind(settled, 4, 250, 28.0f, cfg);
  CHECK(a.action == GrindAction::FINER && a.outliersIgnored == 0 && a.shotsUsed == 3);
  CHECK_NEAR(a.averageTimeS, 21.23, 0.05);

  // With fewer than three shots there is nothing to compare against.
  const GrindShot two[] = {{21.0f, 250}, {28.0f, 250}};
  a = adviseGrind(two, 2, 250, 28.0f, cfg);
  CHECK(a.outliersIgnored == 0 && a.shotsUsed == 2);

  // The advice says when the newest shot is the one it set aside, so the
  // screen can say "this one was fast" instead of "brew time OK".
  a = adviseGrind(oneOff, 4, 250, 28.0f, cfg);
  CHECK(a.newestIgnored);
  CHECK_NEAR(a.newestTimeS, 21.0, 0.001);
  a = adviseGrind(lasting, 4, 250, 28.0f, cfg);
  CHECK(!a.newestIgnored);

  // The user's ruling overrides the rule. "Count it": the same one-off now
  // moves the average and brings a recommendation.
  GrindShot ruled[4];
  for (int i = 0; i < 4; ++i) ruled[i] = oneOff[i];
  ruled[0].forced = true;
  a = adviseGrind(ruled, 4, 250, 28.0f, cfg);
  CHECK(!a.newestIgnored && a.outliersIgnored == 0);
  CHECK(a.action == GrindAction::FINER);
  CHECK_NEAR(a.averageTimeS, (21.0 + 28.5 + 28.0) / 3.0, 0.01);

  // ...and on the stored shot: ignore removes it, count admits a shot that
  // would not otherwise qualify (here one that ran out of time).
  ShotRecord voted = grindRecord(27.0f, 250);
  CHECK(grindShotValid(voted));
  voted.grindVote = static_cast<uint8_t>(GrindVote::IGNORE);
  CHECK(!grindShotValid(voted));
  ShotRecord late = grindRecord(52.0f, 250);
  late.endReason = static_cast<uint8_t>(EndReason::MACHINE_STOPPED);
  CHECK(!grindShotValid(late));
  late.grindVote = static_cast<uint8_t>(GrindVote::COUNT);
  CHECK(grindShotValid(late));
  late.grindUm = 0;
  CHECK(!grindShotValid(late));                 // still no use without a grind setting

  // Every shot that is not counted has a stated reason, so the screen can
  // either ask about it or say why.
  CHECK(grindShotUse(grindRecord(27.0f, 250)) == GrindUse::COUNTED);
  CHECK(grindShotUse(grindRecord(46.0f, 250)) == GrindUse::TOO_LONG);    // 18 s over a 28 s target
  CHECK(grindShotUse(grindRecord(12.0f, 250)) == GrindUse::TOO_SHORT);
  CHECK(grindShotUse(grindRecord(27.0f, 0)) == GrindUse::NO_GRIND);
  ShotRecord byHand = grindRecord(27.0f, 250);
  byHand.endReason = static_cast<uint8_t>(EndReason::USER);
  CHECK(grindShotUse(byHand) == GrindUse::NOT_WEIGHT_STOP);
  ShotRecord unsettled = grindRecord(27.0f, 250);
  unsettled.flags = 0;
  CHECK(grindShotUse(unsettled) == GrindUse::NOT_SETTLED);
  CHECK(grindShotUse(voted) == GrindUse::IGNORED_BY_USER);
  // "Count it" admits a shot that ran far too long as well.
  ShotRecord longShot = grindRecord(46.0f, 250);
  longShot.grindVote = static_cast<uint8_t>(GrindVote::COUNT);
  CHECK(grindShotUse(longShot) == GrindUse::COUNTED);

  // A one-off in the middle of the run is skipped, not a reason to stop.
  const GrindShot middle[] = {{28.4f, 250}, {36.0f, 250}, {27.9f, 250}, {28.1f, 250}};
  a = adviseGrind(middle, 4, 250, 28.0f, cfg);
  CHECK(a.action == GrindAction::HOLD && a.outliersIgnored == 1);
}

static void testStopBeforeAnyCoffee() {
  printf("stop tapped before any coffee was seen\n");
  // Reported case: the start press never reached the button (jammed arm, Bot
  // still says OK). The screen says BREWING, the user taps STOP, and that
  // toggle press is what starts the pump. It must not be left running.
  Rig r;
  r.sim.ignorePresses = 1;
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::BREWING, 10000));
  CHECK(!r.sim.machineRunning);                 // start never happened
  r.run(3000);
  r.ctl.requestStop();
  r.run(500);
  CHECK(r.sim.machineRunning);                  // the "stop" started it
  CHECK(r.ctl.state() != State::SHOT_COMPLETE);
  CHECK(r.ctl.status(r.sim.nowMs).verifyingStop);
  CHECK(r.ctl.actuatorNeeded());                // Bot link kept for the correction
  CHECK(r.runUntil(State::START_COMMAND_FAILED, 40000));
  CHECK(!r.sim.machineRunning);                 // stopped again once coffee showed it running
  CHECK(r.sim.presses == 3);
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(shot.flags & SHOT_FLAG_STOP_STARTED_PUMP);
  CHECK(!(shot.flags & SHOT_FLAG_LEARNED));

  // Ordinary case: the machine really is pre-infusing. One stop press, the
  // pump stays off through the watch period, and it ends as a normal shot.
  Rig n;
  n.toReady();
  n.ctl.requestStart();
  CHECK(n.runUntil(State::BREWING, 10000));
  n.run(3000);
  CHECK(n.sim.machineRunning);
  n.ctl.requestStop();
  n.run(500);
  CHECK(!n.sim.machineRunning);
  CHECK(n.runUntil(State::SHOT_COMPLETE, 40000));
  CHECK(!n.sim.machineRunning && n.sim.presses == 2);
  CHECK(n.ctl.takeCompletedShot(shot));
  CHECK(!(shot.flags & SHOT_FLAG_STOP_STARTED_PUMP));

  // Same blind stop, but the scale then drops out: there is nothing to check
  // against, so it must not be reported as a clean stop.
  Rig s;
  s.sim.ignorePresses = 1;
  s.toReady();
  s.ctl.requestStart();
  CHECK(s.runUntil(State::BREWING, 10000));
  s.run(2000);
  s.ctl.requestStop();
  s.run(300);
  s.sim.scaleLink = false;
  CHECK(s.runUntil(State::STOP_COMMAND_FAILED, 10000));
  CHECK(s.ctl.status(s.sim.nowMs).manualStopNeeded);

  // Once coffee has been seen, a stop is not blind and ends promptly.
  Rig late;
  late.toReady();
  late.ctl.requestStart();
  CHECK(late.runUntil(State::BREWING, 10000));
  late.run(12000);
  late.ctl.requestStop();
  CHECK(late.runUntil(State::SHOT_COMPLETE, 8000));
}

static void testPreinfusionHold() {
  printf("pre-infusion by holding the button\n");
  Rig r;
  r.library.recipes[0].preinfusion = Preinfusion::HOLD;
  r.library.recipes[0].preinfusionHoldMs = 4000;
  r.sim.ackDelayMs = 4150;                      // the Bot reports once the arm is back up
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::STARTING_SHOT, 10000));
  CHECK(r.sim.lastHoldMs == 4000);
  r.run(2000);
  CHECK(r.ctl.state() == State::STARTING_SHOT); // still holding, no timeout
  CHECK(r.ctl.status(r.sim.nowMs).holdingButton);
  CHECK(r.runUntil(State::BREWING, 5000));
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(shot.preinfusionHoldMs == 4000);
  CHECK(r.sim.lastHoldMs == 0);                 // the stop is an ordinary press
  CHECK(r.sim.presses == 2 && !r.sim.machineRunning);

  // STOP during the hold is carried out as soon as the button is released.
  Rig c;
  c.library.recipes[0].preinfusion = Preinfusion::HOLD;
  c.library.recipes[0].preinfusionHoldMs = 4000;
  c.sim.ackDelayMs = 4150;
  c.toReady();
  c.ctl.requestStart();
  CHECK(c.runUntil(State::STARTING_SHOT, 10000));
  c.run(1000);
  c.ctl.requestStop();
  c.run(1000);
  CHECK(c.ctl.state() == State::STARTING_SHOT);
  CHECK(c.runUntil(State::POST_SHOT_DRIP, 9000));
  c.run(300);
  CHECK(!c.sim.machineRunning && c.sim.presses == 2);

  // Machine pre-infusion (the default) is a plain press.
  Rig m;
  m.toReady();
  m.ctl.requestStart();
  CHECK(m.runUntil(State::BREWING, 10000));
  CHECK(m.sim.lastHoldMs == 0);
}

static void testCupRequired() {
  printf("cup on the scale before brewing\n");
  // Scale connects reading 0: empty pan or tared cup, no telling. No Start.
  Rig r;
  r.settings.requireCup = true;
  r.toReady();
  r.run(1500);
  CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);
  CHECK(r.ctl.status(r.sim.nowMs).cupRequired);
  r.ctl.requestStart();
  r.run(1000);
  CHECK(r.ctl.state() == State::READY && r.sim.presses == 0 && r.sim.tares == 0);

  // A cup goes on: seen, tared by the controller, and Start now works.
  r.sim.trueWeightG += 62.0;
  r.run(2000);
  CHECK(r.ctl.status(r.sim.nowMs).cupPresent);
  CHECK(r.sim.tares == 1);
  CHECK_NEAR(r.ctl.status(r.sim.nowMs).weightG, 0.0, 0.2);
  r.run(8000);                                  // sitting there tared: still present
  CHECK(r.ctl.status(r.sim.nowMs).cupPresent);
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK(r.sim.presses == 2);

  // Same cup, now full: a second Start needs a fresh cup.
  r.ctl.acknowledge();
  r.run(2000);
  CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);
  r.ctl.requestStart();
  r.run(1000);
  CHECK(r.ctl.state() == State::READY && r.sim.presses == 2);
  r.sim.trueWeightG -= 62.0 + 36.0;             // cup and coffee lifted off: reads about -98
  r.run(2000);
  CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);
  r.sim.trueWeightG += 70.0;                    // a different cup
  r.run(2000);
  CHECK(r.ctl.status(r.sim.nowMs).cupPresent);
  r.sim.trueWeightG -= 70.0;                    // and taken away again
  r.run(2000);
  CHECK(!r.ctl.status(r.sim.nowMs).cupPresent);

  // Reported: cup swapped while the result is still showing. It has to be
  // known by the time the home screen is back.
  Rig swap;
  swap.settings.requireCup = true;
  swap.toReady();
  swap.run(1000);
  swap.sim.trueWeightG += 62.0;
  swap.run(3000);
  swap.ctl.requestStart();
  CHECK(swap.runUntil(State::SHOT_COMPLETE, 60000));
  swap.run(1500);
  // The coffee arriving in the cup is not a cup being placed.
  CHECK(!swap.ctl.status(swap.sim.nowMs).cupPresent);
  const int taresBefore = swap.sim.tares;
  swap.sim.trueWeightG -= 62.0 + 36.0;          // full cup off
  swap.run(2000);
  CHECK(swap.ctl.state() == State::SHOT_COMPLETE && !swap.ctl.status(swap.sim.nowMs).cupPresent);
  swap.sim.trueWeightG += 70.0;                 // fresh cup on
  swap.run(2500);
  CHECK(swap.ctl.state() == State::SHOT_COMPLETE);   // the screen decides when to leave
  CHECK(swap.ctl.status(swap.sim.nowMs).cupPresent);
  CHECK(swap.sim.tares == taresBefore + 1);     // and it has been zeroed
  swap.ctl.acknowledge();
  swap.run(500);
  CHECK(swap.ctl.state() == State::READY && swap.ctl.status(swap.sim.nowMs).cupPresent);
  swap.ctl.requestStart();                      // straight into the next shot
  CHECK(swap.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK(swap.sim.presses == 4);

  // The scale's own auto-tare zeroes the cup before the controller's command
  // matters: the fall to zero right after placing is not the cup leaving.
  Rig a;
  a.settings.requireCup = true;
  a.sim.scaleIgnoresTare = true;                // only the scale's own tare acts
  a.toReady();
  a.run(1000);
  a.sim.trueWeightG += 62.0;
  a.run(1200);
  a.sim.tareOffsetG = a.sim.trueWeightG;        // scale auto-tares
  a.run(2000);
  CHECK(a.ctl.status(a.sim.nowMs).cupPresent);
  CHECK_NEAR(a.ctl.status(a.sim.nowMs).weightG, 0.0, 0.2);

  // A scale that connects already showing a cup's weight has a cup on it.
  Rig c;
  c.settings.requireCup = true;
  c.sim.trueWeightG = 62.0;
  c.toReady();
  c.run(2000);
  CHECK(c.ctl.status(c.sim.nowMs).cupPresent);

  // Something light (a spoon, a few beans) is not a cup.
  Rig s;
  s.settings.requireCup = true;
  s.toReady();
  s.run(1000);
  s.sim.trueWeightG += 4.0;
  s.run(2000);
  CHECK(!s.ctl.status(s.sim.nowMs).cupPresent);

  // With the check switched off, Start works as before.
  Rig off;
  off.toReady();
  off.ctl.requestStart();
  CHECK(off.runUntil(State::BREWING, 10000));
}

static void testRestartRecovery() {
  printf("restart during a shot\n");
  Rig r;
  r.sim.machineRunning = true;                  // controller rebooted mid-shot
  r.sim.machineStartedMs = r.sim.nowMs - 10000;
  r.sim.trueWeightG = 8.0;
  r.ctl.armRestartRecovery(r.sim.nowMs);
  r.run(6000);
  CHECK(!r.sim.machineRunning);
  CHECK(r.sim.presses == 1);
  r.run(8000);
  ShotRecord recovered;
  CHECK(r.ctl.takeCompletedShot(recovered));
  CHECK(recovered.endReason == static_cast<uint8_t>(EndReason::RECOVERED));

  Rig idle;                                     // rebooted, but nothing is flowing
  idle.ctl.armRestartRecovery(idle.sim.nowMs);
  idle.run(60000);
  CHECK(idle.sim.presses == 0 && idle.ctl.state() == State::READY);
}

static void testSanitize() {
  printf("settings sanitize\n");
  Settings s = defaultSettings();
  s.learningFactor = 7.0f;
  sanitize(s);
  CHECK_NEAR(s.learningFactor, 1.0, 0.0001);

  Library lib = defaultLibrary();
  lib.recipes[0].targetWeightG = 5000.0f;
  lib.recipes[0].maxShotTimeS = 9999;
  lib.presets[0].stopOffsetG = NAN;
  lib.presetCount = 40;
  lib.activePreset = 39;
  sanitizeLibrary(lib);
  CHECK_NEAR(lib.recipes[0].targetWeightG, MAX_TARGET_G, 0.0001);
  CHECK(std::isfinite(lib.presets[0].stopOffsetG));
  CHECK(lib.recipes[0].maxShotTimeS == HARD_MAX_SHOT_MS / 1000);
  // The 14 slots beyond the two real presets point at no recipe: dropped.
  CHECK(lib.presetCount == 2 && lib.activePreset < lib.presetCount);

  // Ratio mode: the target is derived, 18.0 g at 1:2.0 gives 36.0 g.
  Recipe r;
  r.useRatio = true;
  r.doseG = 18.0f;
  r.ratio = 2.0f;
  r.targetWeightG = 99.0f;
  sanitizeRecipe(r);
  CHECK_NEAR(r.targetWeightG, 36.0, 0.0001);
  r.doseG = 20.0f;
  r.ratio = 9.0f;                              // clamped to 1:5.0
  sanitizeRecipe(r);
  CHECK_NEAR(r.targetWeightG, 100.0, 0.0001);
  r.useRatio = false;
  r.targetWeightG = 30.0f;
  sanitizeRecipe(r);
  CHECK_NEAR(r.targetWeightG, 30.0, 0.0001);   // target mode leaves it alone

  r.roastDate = 20261340;                      // month 13
  r.roastLevel = 99;
  r.grindUm = 9999;
  r.preinfusionHoldMs = 60000;
  sanitizeRecipe(r);
  CHECK(r.roastDate == 0 && r.roastLevel == 0 && r.grindUm == 0);
  CHECK(r.preinfusionHoldMs == MAX_PREINFUSION_HOLD_MS);
  r.roastDate = 20260921;
  sanitizeRecipe(r);
  CHECK(r.roastDate == 20260921);
}

// ------------------------------------------------------------------ library --

static void testLibrary() {
  printf("library: presets, edits, saved format\n");
  Library lib = defaultLibrary();
  CHECK(lib.recipeCount == 2 && lib.presetCount == 2 && lib.coffeeCount == 0);
  Recipe r = resolvePreset(lib, 1);
  CHECK(strcmp(r.name, "Single") == 0 && r.coffeeId == 0 && r.recipeId == lib.recipes[1].id);
  CHECK_NEAR(r.targetWeightG, 18.0, 0.0001);
  CHECK(strcmp(resolvePreset(lib, 9).name, "Double") == 0);  // out of range: the first

  // Two coffees on one recipe, as the Coffees page will make them: the list
  // has two lines, the recipe exists once.
  Library before = lib;
  Recipe a = resolvePreset(lib, 0);
  copyText(a.coffee, "Idjwi Island", COFFEE_NAME_LEN);
  a.roastDate = 20260303;
  a.grindUm = 246;
  CHECK(editPreset(lib, before, 0, a));
  CHECK(lib.coffeeCount == 1 && lib.presets[0].coffeeId == lib.coffees[0].id);
  lib.presets[lib.presetCount] = lib.presets[0];          // a second preset on "Double"
  lib.presets[lib.presetCount].coffeeId = 0;
  lib.presets[lib.presetCount].grindUm = 260;
  ++lib.presetCount;
  sanitizeLibrary(lib);
  CHECK(lib.presetCount == 3 && lib.recipeCount == 2);

  // Changing the dose through one of them changes the recipe, so both.
  before = lib;
  Recipe edited = resolvePreset(lib, 0);
  edited.doseG = 19.0f;
  CHECK(editPreset(lib, before, 0, edited));
  CHECK_NEAR(resolvePreset(lib, 2).doseG, 19.0, 0.0001);
  CHECK(resolvePreset(lib, 0).grindUm == 246 && resolvePreset(lib, 2).grindUm == 260);
  CHECK(lib.recipeCount == 2);

  // The whole list sent back as the web page does, with one field changed in
  // the first entry only. The untouched entry that shares its recipe must
  // not put the old value back.
  before = lib;
  Recipe list[MAX_PRESETS];
  uint8_t slots[MAX_PRESETS];
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    list[i] = resolvePreset(lib, i);
    slots[i] = i + 1;
  }
  list[0].targetTimeS = 31.0f;
  lib.presets[0].stopOffsetG = 2.8f;   // learned since
  before = lib;
  CHECK(applyPresetList(lib, before, list, slots, 3, 2));
  CHECK_NEAR(resolvePreset(lib, 0).targetTimeS, 31.0, 0.0001);
  CHECK_NEAR(resolvePreset(lib, 2).targetTimeS, 31.0, 0.0001);
  CHECK_NEAR(lib.presets[0].stopOffsetG, 2.8, 0.0001);   // what was learned stays
  CHECK(lib.activePreset == 2 && lib.presetCount == 3);

  // Duplicate (the page copies the entry, slot and all, under a new name) and
  // delete the middle one. The copy is new: its own recipe, nothing learned.
  before = lib;
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    list[i] = resolvePreset(lib, i);
    slots[i] = i + 1;
  }
  const uint16_t doubleId = list[0].recipeId, singleId = list[1].recipeId;
  list[1] = list[2];                    // "Single" deleted
  slots[1] = slots[2];
  list[2] = list[0];                    // duplicate of the first
  slots[2] = slots[0];
  copyText(list[2].name, "Double 2", RECIPE_NAME_LEN);
  CHECK(applyPresetList(lib, before, list, slots, 3, 0));
  CHECK(lib.presetCount == 3 && lib.recipeCount == 2);
  CHECK(findRecipe(lib, singleId) == nullptr && findRecipe(lib, doubleId) != nullptr);
  CHECK(strcmp(resolvePreset(lib, 0).name, "Double") == 0);       // not renamed by its copy
  CHECK(strcmp(resolvePreset(lib, 2).name, "Double 2") == 0);
  CHECK(resolvePreset(lib, 2).recipeId != doubleId);
  CHECK(resolvePreset(lib, 2).coffeeId == resolvePreset(lib, 0).coffeeId);  // same coffee
  CHECK_NEAR(lib.presets[2].stopOffsetG, resolvePreset(lib, 2).baselineOffsetG, 0.0001);
  CHECK(lib.coffeeCount == 1);

  // Another coffee name is another coffee; the first stays on the shelf.
  before = lib;
  Recipe other = resolvePreset(lib, 0);
  const uint16_t firstCoffee = other.coffeeId;
  copyText(other.coffee, "Guji", COFFEE_NAME_LEN);
  CHECK(editPreset(lib, before, 0, other));
  CHECK(lib.coffeeCount == 2 && resolvePreset(lib, 0).coffeeId != firstCoffee);
  CHECK(findCoffee(lib, firstCoffee) != nullptr);
  // A change of bag is the same coffee.
  before = lib;
  other = resolvePreset(lib, 0);
  other.roastDate = 20261001;
  CHECK(editPreset(lib, before, 0, other));
  CHECK(lib.coffeeCount == 2 && resolvePreset(lib, 0).roastDate == 20261001);
  // Coffee removed from a preset: the preset stays, without one.
  before = lib;
  other = resolvePreset(lib, 0);
  other.coffee[0] = '\0';
  other.roastDate = 0;
  CHECK(editPreset(lib, before, 0, other));
  CHECK(resolvePreset(lib, 0).coffeeId == 0 && lib.coffeeCount == 2);

  // The tables edited one record at a time, as the web page does.
  {
    Library w = defaultLibrary();
    Coffee c;
    copyText(c.name, "Idjwi Island", COFFEE_NAME_LEN);
    copyText(c.roaster, "Portrait", COFFEE_NAME_LEN);
    copyText(c.process, "washed", PROCESS_LEN);
    c.roastDate = 20260303;
    c.altitudeM = 9000;                                  // not on this planet
    const uint16_t cid = saveCoffee(w, c);
    CHECK(cid != 0 && w.coffeeCount == 1 && findCoffee(w, cid)->altitudeM == 0);
    c.id = cid;
    c.roastDate = 20261001;                              // a new bag
    CHECK(saveCoffee(w, c) == cid && w.coffeeCount == 1 && findCoffee(w, cid)->roastDate == 20261001);
    c.id = 77;
    CHECK(saveCoffee(w, c) == 0);                        // no such coffee
    const uint16_t doubleId = w.recipes[0].id, singleId = w.recipes[1].id;
    CHECK(addPreset(w, doubleId, cid) == 2);
    CHECK(addPreset(w, doubleId, cid) == -1);            // already on the list
    CHECK(addPreset(w, 99, cid) == -1 && addPreset(w, doubleId, 99) == -1);
    w.presets[2].grindUm = 246;
    const int second = addPreset(w, singleId, cid);
    CHECK(second == 3 && w.presets[3].grindUm == 246);   // starts from the same coffee's grind
    CHECK(!removeCoffee(w, cid) && !removeRecipeDef(w, doubleId));   // both in use
    w.activePreset = 3;
    CHECK(removePreset(w, 2) && w.presetCount == 3 && w.activePreset == 2);
    CHECK(removePreset(w, 2) && removeCoffee(w, cid) && w.coffeeCount == 0);
    RecipeDef d;
    copyText(d.name, "Bloom", RECIPE_NAME_LEN);
    d.bloomPauseMs = 20000;
    const uint16_t bloomId = saveRecipeDef(w, d);
    CHECK(bloomId != 0 && findRecipe(w, bloomId)->preinfusion == Preinfusion::HOLD);
    CHECK(removeRecipeDef(w, bloomId) && w.recipeCount == 2);
    CHECK(removePreset(w, 0) && !removePreset(w, 0));    // the last line stays
  }

  // An empty list is refused and changes nothing.
  before = lib;
  CHECK(!applyPresetList(lib, before, list, slots, 0, 0));
  CHECK(lib.presetCount == before.presetCount);

  // A preset pointing at a recipe or coffee that is gone.
  Library broken = lib;
  broken.presets[1].recipeId = 999;
  broken.presets[2].coffeeId = 999;
  broken.activePreset = 2;
  sanitizeLibrary(broken);
  CHECK(broken.presetCount == 2 && broken.presets[1].coffeeId == 0);
  CHECK(broken.activePreset == 1);     // still the same preset

  // Saved and read back.
  static uint8_t bytes[LIBRARY_MAX_BYTES];
  const size_t length = encodeLibrary(lib, bytes, sizeof(bytes));
  CHECK(length > sizeof(LibraryFileHeader) && length < sizeof(bytes));
  CHECK(encodeLibrary(lib, bytes, 10) == 0);
  Library back;
  CHECK(decodeLibrary(bytes, length, back));
  CHECK(back.presetCount == lib.presetCount && back.coffeeCount == lib.coffeeCount &&
        back.recipeCount == lib.recipeCount && back.nextCoffeeId == lib.nextCoffeeId);
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    const Recipe x = resolvePreset(lib, i), y = resolvePreset(back, i);
    CHECK(!flatRecipeDiffers(x, y) && !flatCoffeeDiffers(x, y) && x.grindUm == y.grindUm &&
          x.stopOffsetG == y.stopOffsetG && x.recipeId == y.recipeId && x.coffeeId == y.coffeeId);
  }
  CHECK(!decodeLibrary(bytes, length - 1, back));          // cut short
  bytes[0] ^= 0xFF;
  CHECK(!decodeLibrary(bytes, length, back));               // not a library
  bytes[0] ^= 0xFF;
  // A file from a build whose records were shorter: what it has is read, the
  // fields added since keep their defaults.
  LibraryFileHeader h;
  memcpy(&h, bytes, sizeof(h));
  CHECK(h.presetSize == sizeof(Preset));
  static uint8_t shorter[LIBRARY_MAX_BYTES];
  const size_t tables = sizeof(h) + sizeof(LibraryFileHeaderV2) + h.coffeeCount * sizeof(Coffee) +
                        h.recipeCount * sizeof(RecipeDef);
  memcpy(shorter, bytes, tables);
  const uint16_t cut = offsetof(Preset, stopOffsetG);       // as if stopOffsetG were new
  for (uint8_t i = 0; i < h.presetCount; ++i) {
    memcpy(shorter + tables + i * cut, bytes + tables + i * sizeof(Preset), cut);
  }
  memcpy(shorter + tables + h.presetCount * cut, bytes + tables + h.presetCount * sizeof(Preset),
         sizeof(Grinder));
  h.presetSize = cut;
  memcpy(shorter, &h, sizeof(h));
  CHECK(decodeLibrary(shorter, tables + h.presetCount * cut + sizeof(Grinder), back));
  CHECK(back.presets[0].grindUm == lib.presets[0].grindUm);
  CHECK_NEAR(back.presets[0].stopOffsetG, Preset{}.stopOffsetG, 0.0001);

  // A version 1 file (before the grinder was stored): the same tables with
  // neither the second header nor the grinder.
  static uint8_t v1[LIBRARY_MAX_BYTES];
  const size_t body = h.coffeeCount * sizeof(Coffee) + h.recipeCount * sizeof(RecipeDef) +
                      h.presetCount * sizeof(Preset);
  h.presetSize = sizeof(Preset);
  h.version = LIBRARY_VERSION_V1;
  memcpy(v1, &h, sizeof(h));
  memcpy(v1 + sizeof(h), bytes + sizeof(h) + sizeof(LibraryFileHeaderV2), body);
  CHECK(decodeLibrary(v1, sizeof(h) + body, back));
  CHECK(back.presetCount == lib.presetCount && back.presets[0].grindUm == lib.presets[0].grindUm);
  CHECK(back.grinder.calibration == 0 && strcmp(back.grinder.unit, "um") == 0);

  // The grinder. Naming it for the first time changes nothing about the
  // shots made so far; zeroing it again, or other burrs, starts afresh.
  Library gl = defaultLibrary();
  Grinder g;
  g.modelId = 1;
  setGrinder(gl, g, false);
  CHECK(strcmp(gl.grinder.model, "Mahlkonig E64 WS") == 0 && strcmp(gl.grinder.unit, "um") == 0);
  CHECK_NEAR(gl.grinder.umPerUnit, 1.0, 0.0001);
  CHECK(strcmp(gl.grinder.burrs, "64 mm flat") == 0 && gl.grinder.calibration == 0);
  setGrinder(gl, g, false);                                   // saved again unchanged
  CHECK(gl.grinder.calibration == 0);
  g.zeroDate = 20261010;
  setGrinder(gl, g, true);                                    // zeroed again
  CHECK(gl.grinder.calibration == 1 && gl.grinder.zeroDate == 20261010);
  copyText(g.burrs, "64 mm SSP", GRINDER_BURRS_LEN);
  g.zeroDate = 0;
  setGrinder(gl, g, false);                                   // other burrs
  CHECK(gl.grinder.calibration == 2 && strcmp(gl.grinder.burrs, "64 mm SSP") == 0);
  gl.presets[0].grindUm = 246;
  g = Grinder{};                                              // another grinder, typed in
  copyText(g.model, "Stepped grinder", GRINDER_NAME_LEN);
  copyText(g.unit, "steps", GRINDER_UNIT_LEN);
  g.umPerUnit = 5.0f;
  setGrinder(gl, g, false);
  CHECK(gl.presets[0].grindUm == 0);                          // 246 um is not 246 steps
  CHECK(gl.grinder.calibration == 3 && strcmp(gl.grinder.unit, "steps") == 0);
  CHECK_NEAR(gl.grinder.umPerUnit, 5.0, 0.0001);
  g = Grinder{};
  g.modelId = 999;                                            // not in the registry: as typed
  copyText(g.model, "Home built", GRINDER_NAME_LEN);
  copyText(g.unit, "dial", GRINDER_UNIT_LEN);
  g.umPerUnit = NAN;
  setGrinder(gl, g, false);
  CHECK(gl.grinder.modelId == 0 && strcmp(gl.grinder.model, "Home built") == 0);
  CHECK(gl.grinder.umPerUnit == 0.0f && gl.grinder.calibration == 4);
  static uint8_t gb[LIBRARY_MAX_BYTES];
  const size_t gn = encodeLibrary(gl, gb, sizeof(gb));
  CHECK(gn != 0 && decodeLibrary(gb, gn, back) && back.grinder.calibration == 4 &&
        strcmp(back.grinder.model, "Home built") == 0);
}

// What an earlier build stored, brought forward: its settings (recipes with
// the coffee written into each) and its shot history.
static void testMigration() {
  printf("migration: settings v7 and history v8\n");
  static legacy::SettingsV7 old;
  memset(&old, 0, sizeof(old));
  old.learningEnabled = true;
  old.learningFactor = 0.3f;
  old.minValidWeightG = -5.0f;
  old.scaleTimeoutMs = 1500;
  old.dripObserveMs = 3000;
  old.minShotTimeMs = 3000;
  old.noFlowTimeoutS = 20;
  old.cupMinWeightG = 10.0f;
  old.autoStartAfterTare = true;
  old.scaleTimer = true;
  old.pressDurationMs = 300;
  old.releaseSettleMs = 1200;
  old.grindToleranceS = 2.5f;
  old.grindUmPerS = 3.0f;
  old.grindWindow = 4;
  old.grindMaxStepUm = 20;
  old.grindTimeBasis = GrindTimeBasis::POUR;
  old.requireCup = false;
  struct Row { const char *name; float dose, target; uint16_t um; const char *coffee; uint32_t roast; };
  const Row rows[] = {
      {"DR Congo Idjwi Island", 18, 36, 246, "Portrait", 20260303},
      {"Guji Double", 18, 36, 250, "Ethiopia Guji", 0},
      {"Decaf", 17, 34, 262, "Colombia Decaf", 0},
      {"Ristretto", 18, 27, 231, "", 0},
      {"Portrait lungo", 18, 48, 270, "Portrait", 20260303},   // same coffee, same bag
      {"Portrait new bag", 18, 36, 240, "Portrait", 20261001}, // same name, another bag
      {"Guest", 18, 36, 0, "", 0},
  };
  old.recipeCount = sizeof(rows) / sizeof(rows[0]);
  old.activeRecipe = 2;
  for (uint8_t i = 0; i < old.recipeCount; ++i) {
    legacy::RecipeV7 &o = old.recipes[i];
    copyText(o.name, rows[i].name, RECIPE_NAME_LEN);
    o.doseG = rows[i].dose;
    o.targetWeightG = rows[i].target;
    o.ratio = 2.0f;
    o.baselineOffsetG = 1.5f;
    o.stopOffsetG = 2.0f + 0.1f * i;
    o.learnedAlpha = static_cast<uint8_t>(10 + i);
    o.maxShotTimeS = 50;
    o.targetTimeS = 26.0f;
    o.grindUm = rows[i].um;
    copyText(o.coffee, rows[i].coffee, COFFEE_NAME_LEN);
    o.roastDate = rows[i].roast;
    o.preinfusionHoldMs = 3000;
  }
  copyText(old.recipes[0].region, "Idjwi Island, Kivu", ORIGIN_LEN);
  copyText(old.recipes[4].region, "Idjwi Island, Kivu", ORIGIN_LEN);
  copyText(old.recipes[5].region, "Idjwi Island, Kivu", ORIGIN_LEN);
  old.recipes[3].preinfusion = Preinfusion::HOLD;
  old.recipes[3].preinfusionHoldMs = 4000;

  static Library lib;
  libraryFromV7(old, lib);
  CHECK(lib.presetCount == 7 && lib.recipeCount == 7);
  CHECK(lib.coffeeCount == 4);            // Portrait twice (two bags), Guji, Decaf
  CHECK(lib.activePreset == 2);
  for (uint8_t i = 0; i < lib.presetCount; ++i) {
    const Recipe r = resolvePreset(lib, i);
    CHECK(strcmp(r.name, rows[i].name) == 0 && strcmp(r.coffee, rows[i].coffee) == 0);
    CHECK_NEAR(r.doseG, rows[i].dose, 0.0001);
    CHECK_NEAR(r.targetWeightG, rows[i].target, 0.0001);
    CHECK_NEAR(r.targetTimeS, 26.0, 0.0001);
    CHECK(r.grindUm == rows[i].um && r.roastDate == rows[i].roast);
    CHECK_NEAR(r.stopOffsetG, 2.0f + 0.1f * i, 0.0001);   // nothing learned is lost
    CHECK(r.learnedAlpha == 10 + i);
    CHECK((r.coffeeId != 0) == (rows[i].coffee[0] != '\0'));
  }
  CHECK(resolvePreset(lib, 0).coffeeId == resolvePreset(lib, 4).coffeeId);
  CHECK(resolvePreset(lib, 0).coffeeId != resolvePreset(lib, 5).coffeeId);
  CHECK(strcmp(resolvePreset(lib, 4).region, "Idjwi Island, Kivu") == 0);
  CHECK(resolvePreset(lib, 3).preinfusion == Preinfusion::HOLD &&
        resolvePreset(lib, 3).preinfusionHoldMs == 4000);

  const Settings s = settingsFromV7(old, sizeof(old));
  CHECK(s.learningEnabled && !s.adaptiveLearning && s.grindWindow == 4 && !s.requireCup);
  CHECK_NEAR(s.grindToleranceS, 2.5, 0.0001);
  CHECK(s.grindTimeBasis == GrindTimeBasis::POUR && s.scaleTimeoutMs == 1500);
  // The oldest version 7 blobs stop before requireCup: it keeps its default.
  CHECK(settingsFromV7(old, offsetof(legacy::SettingsV7, requireCup)).requireCup);

  // A shot from the old history: every value kept, ids found by name.
  legacy::ShotRecordV8 v8;
  memset(&v8, 0, sizeof(v8));
  v8.id = 59;
  v8.epochS = 1791651283;
  copyText(v8.recipe, "DR Congo Idjwi Island", RECIPE_NAME_LEN);
  v8.targetWeightG = 36.0f;
  v8.stopOffsetG = 2.88f;
  v8.stopWeightG = 33.4f;
  v8.finalWeightG = 35.8f;
  v8.dripWeightG = 2.4f;
  v8.shotTimeS = 25.42f;
  v8.firstDropS = 8.39f;
  v8.averageFlowGS = 1.41f;
  v8.peakFlowGS = 2.06f;
  v8.nextStopOffsetG = 2.82f;
  v8.stopLatencyMs = 410;
  v8.endReason = static_cast<uint8_t>(EndReason::WEIGHT);
  v8.flags = SHOT_FLAG_FINAL_VALID | SHOT_FLAG_LEARNED | SHOT_FLAG_STOP_CONFIRMED;
  v8.alphaUsed = v8.alphaNext = 30;
  v8.grindUm = 246;
  v8.doseG = 18.0f;
  v8.targetTimeS = 26.0f;
  v8.pourTimeS = 17.03f;
  v8.pourFlowGS = 2.1f;
  v8.timeBasis = 1;
  copyText(v8.coffee, "Portrait", COFFEE_NAME_LEN);
  copyText(v8.region, "Idjwi Island, Kivu", ORIGIN_LEN);
  v8.roastDate = 20260303;
  v8.roastLevel = 2;
  v8.preinfusionHoldMs = 4000;
  v8.flow25GS = 2.03f;
  v8.flow50GS = 1.97f;
  v8.flow75GS = 2.0f;
  v8.flowAcceleration = 0.99f;
  v8.flowRoughnessGS = 0.1f;
  v8.rating = 4;
  v8.taste = static_cast<uint8_t>(Taste::BALANCED);
  v8.grindVote = static_cast<uint8_t>(GrindVote::COUNT);
  const ShotRecord shot = shotFromV8(v8, lib);
  CHECK(shot.id == 59 && shot.epochS == 1791651283 && strcmp(shot.recipe, v8.recipe) == 0);
  CHECK(shot.targetWeightG == v8.targetWeightG && shot.stopOffsetG == v8.stopOffsetG &&
        shot.stopWeightG == v8.stopWeightG && shot.finalWeightG == v8.finalWeightG &&
        shot.dripWeightG == v8.dripWeightG && shot.shotTimeS == v8.shotTimeS &&
        shot.firstDropS == v8.firstDropS && shot.averageFlowGS == v8.averageFlowGS &&
        shot.peakFlowGS == v8.peakFlowGS && shot.nextStopOffsetG == v8.nextStopOffsetG);
  CHECK(shot.stopLatencyMs == 410 && shot.endReason == v8.endReason && shot.flags == v8.flags &&
        shot.alphaUsed == 30 && shot.alphaNext == 30 && shot.grindUm == 246);
  CHECK(shot.doseG == v8.doseG && shot.targetTimeS == v8.targetTimeS &&
        shot.pourTimeS == v8.pourTimeS && shot.pourFlowGS == v8.pourFlowGS && shot.timeBasis == 1);
  CHECK(strcmp(shot.coffee, "Portrait") == 0 && strcmp(shot.region, v8.region) == 0 &&
        shot.varietal[0] == '\0' && shot.roastDate == 20260303 && shot.roastLevel == 2 &&
        shot.preinfusionHoldMs == 4000);
  CHECK(shot.flow25GS == v8.flow25GS && shot.flow50GS == v8.flow50GS &&
        shot.flow75GS == v8.flow75GS && shot.flowAcceleration == v8.flowAcceleration &&
        shot.flowRoughnessGS == v8.flowRoughnessGS);
  CHECK(shot.rating == 4 && shot.taste == v8.taste && shot.grindVote == v8.grindVote);
  CHECK(shot.recipeId == resolvePreset(lib, 0).recipeId);
  CHECK(shot.coffeeId == resolvePreset(lib, 0).coffeeId);   // the bag it was made from
  CHECK(grindShotValid(shot, GrindTimeBasis::POUR));          // still counts for advice
  v8.roastDate = 20261001;
  CHECK(shotFromV8(v8, lib).coffeeId == resolvePreset(lib, 5).coffeeId);
  v8.roastDate = 20250101;                                    // a bag long gone: by origin
  CHECK(shotFromV8(v8, lib).coffeeId != 0);
  copyText(v8.recipe, "Deleted since", RECIPE_NAME_LEN);
  copyText(v8.coffee, "Unknown", COFFEE_NAME_LEN);
  CHECK(shotFromV8(v8, lib).recipeId == 0 && shotFromV8(v8, lib).coffeeId == 0);
  v8.coffee[0] = v8.region[0] = '\0';
  CHECK(shotFromV8(v8, lib).coffeeId == 0);

  // A shot made now carries the ids of the preset it was made with.
  Rig r;
  Library before = r.library;
  Recipe withCoffee = resolvePreset(r.library, 0);
  copyText(withCoffee.coffee, "Portrait", COFFEE_NAME_LEN);
  CHECK(editPreset(r.library, before, 0, withCoffee));
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  ShotRecord made;
  CHECK(r.ctl.takeCompletedShot(made));
  CHECK(made.recipeId == r.library.recipes[0].id && made.coffeeId == r.library.coffees[0].id);
  CHECK(strcmp(made.coffee, "Portrait") == 0 && strcmp(made.recipe, "Double") == 0);
  CHECK(made.grinderId == 0);
  r.library.grinder.calibration = 3;
  r.sim.trueWeightG = r.sim.tareOffsetG = 0;
  r.ctl.acknowledge();
  r.toReady();
  r.ctl.requestStart();
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK(r.ctl.takeCompletedShot(made) && made.grinderId == 3);   // the calibration it was made under
}

// What goes to the shared library: hashing, the records, the signed envelope.
struct FakeSigner : ISigner {
  uint8_t key[PUBLIC_KEY_LEN];
  uint8_t lastHash[32] = {};
  bool refuse = false;
  FakeSigner() { for (size_t i = 0; i < PUBLIC_KEY_LEN; ++i) key[i] = static_cast<uint8_t>(i + 4); }
  bool ready() const override { return true; }
  const uint8_t *publicKey() const override { return key; }
  const char *algorithm() const override { return "ES256"; }
  bool sign(const uint8_t hash[32], uint8_t signature[SIGNATURE_LEN]) override {
    memcpy(lastHash, hash, 32);
    for (size_t i = 0; i < SIGNATURE_LEN; ++i) signature[i] = hash[i % 32] ^ 0x5a;
    return !refuse;
  }
};

static void testDiagnostics() {
  printf("diagnostics: cleaning, the warning tally, the reports\n");
  // Lines as the board logs them (made-up addresses and names).
  ScrubList list;
  list.add("aa:bb:cc:11:22:33", "<scale>");
  list.add("aa:bb:cc:44:55:66", "<power bot>");
  list.add("MyHomeNet", "<wifi>");
  list.add("BrewByWeight-1234", "<ap>");
  list.add("LUNAR-9E4079", "LUNAR");
  list.add("Finca La Unice", "<coffee>");
  list.add("Greg", "<name>");
  char out[DIAG_LINE_MAX];
  scrubLine("[59640.030] I scale: connecting to AA:BB:CC:11:22:33 (LUNAR-9E4079)", out, sizeof(out), &list);
  CHECK(strcmp(out, "[59640.030] I scale: connecting to <scale> (LUNAR)") == 0);
  scrubLine("[60282.002] I power: connecting to aa:bb:cc:44:55:66", out, sizeof(out), &list);
  CHECK(strcmp(out, "[60282.002] I power: connecting to <power bot>") == 0);
  scrubLine("[  62.506] I wifi: on the home network (192.168.0.242), own access point off", out, sizeof(out), &list);
  CHECK(strcmp(out, "[  62.506] I wifi: on the home network (<ip>), own access point off") == 0);
  scrubLine("[   2.833] I web: access point BrewByWeight-1234, http://192.168.4.1 on MyHomeNet", out, sizeof(out), &list);
  CHECK(strcmp(out, "[   2.833] I web: access point <ap>, http://<ip> on <wifi>") == 0);
  scrubLine("W bot: unknown device 12:34:56:78:9a:bc near", out, sizeof(out), &list);
  CHECK(strcmp(out, "W bot: unknown device <mac> near") == 0);
  scrubLine("I coffee: Honduras finca la unice for greg", out, sizeof(out), &list);
  CHECK(strcmp(out, "I coffee: Honduras <coffee> for <name>") == 0);
  scrubLine("I library: token github_pat_11ABCDEFG0123456789abcdef used", out, sizeof(out), &list);
  CHECK(strcmp(out, "I library: token <token> used") == 0);
  // Versions, times and short codes are kept.
  scrubLine("[   0.815] I boot: firmware v0.1.19, status 0x01, 3.5 s", out, sizeof(out), &list);
  CHECK(strcmp(out, "[   0.815] I boot: firmware v0.1.19, status 0x01, 3.5 s") == 0);
  // A long line is cut, never overrun.
  char longLine[400];
  memset(longLine, 'x', sizeof(longLine) - 1);
  longLine[sizeof(longLine) - 1] = '\0';
  CHECK(scrubLine(longLine, out, sizeof(out), &list) == DIAG_LINE_MAX - 1);

  // Patterns: numbers become '#', the time stamp and level go.
  char pattern[DIAG_PATTERN_MAX];
  warningPattern("[65179.623] W power: connect failed after 10237 ms: timed out (code 13); last heard 4 s before, -71 dBm",
                 pattern, sizeof(pattern));
  CHECK(strcmp(pattern, "power: connect failed after # ms: timed out (code #); last heard # s before, # dBm") == 0);

  WarnTally *w = new WarnTally();
  for (int i = 0; i < 5; ++i) {
    char line[160];
    snprintf(line, sizeof(line), "[%d.000] W power: connect failed after %d ms: timed out (code 13)", 100 + i, 10000 + i);
    w->add(line);
  }
  w->add("[200.000] W scale: no supported characteristic pair found\n");
  CHECK(w->patternCount == 2 && w->patterns[0].count == 5 && w->patterns[1].count == 1);
  CHECK(w->recentCount == 6 && strcmp(w->recentLine(5), "[200.000] W scale: no supported characteristic pair found") == 0);
  for (int i = 0; i < 40; ++i) {
    char line[64];
    snprintf(line, sizeof(line), "W thing %c: broke", 'A' + i % 26);
    w->add(line);
  }
  CHECK(w->recentCount == DIAG_RECENT && w->patternCount <= DIAG_PATTERNS && w->valid());
  CHECK(strcmp(w->recentLine(DIAG_RECENT - 1), "W thing N: broke") == 0);  // the newest last

  // The daily report.
  DiagInput in;
  in.firmware = "v0.1.20";
  in.atS = 1792806690;
  in.sinceS = 1792720290;
  in.bootCount = 58;
  in.addReset("restart requested");
  in.addReset("CRASH (exception)");
  in.addReset("restart requested");
  CHECK(in.resetCount == 2);
  in.scaleDriver = "acaia";
  scaleModelName("LUNAR-9E4079", in.scaleName, sizeof(in.scaleName));
  CHECK(strcmp(in.scaleName, "LUNAR") == 0);
  in.bots[in.botCount++] = {"brew", "SwitchBot Bot"};
  in.power.used = true;
  in.power.attempts = 11;
  in.power.failures = 9;
  in.power.rssi = -77;
  ShotRecord a{}, b{};
  a.endReason = static_cast<uint8_t>(EndReason::WEIGHT);
  a.flags = SHOT_FLAG_FINAL_VALID;
  a.targetWeightG = 48.0f;
  a.finalWeightG = 49.8f;
  a.stopLatencyMs = 3691;
  b = a;
  b.finalWeightG = 45.8f;
  b.stopLatencyMs = 3232;
  b.flags |= SHOT_FLAG_STOP_RETRIED;
  in.addShot(a);
  in.addShot(b);
  PowerEvent e{};
  e.outcome = static_cast<uint8_t>(PowerControl::Outcome::MACHINE_BUSY);
  in.addPower(e);
  static char body[DIAG_BODY_MAX];
  const size_t n = buildDailyReport(in, *w, body, sizeof(body));
  CHECK(n > 0 && n < DIAG_BODY_MAX);
  CHECK(strstr(body, "\"t\":\"diag_daily\"") && strstr(body, "\"resets\":[\"restart requested\",\"CRASH (exception)\"]"));
  CHECK(strstr(body, "\"scale\":{\"driver\":\"acaia\",\"name\":\"LUNAR\"}"));
  CHECK(strstr(body, "\"power\":{\"attempts\":11,\"failures\":9"));
  CHECK(strstr(body, "\"longest_wait_ms\":0,\"rssi\":-77}"));
  CHECK(strstr(body, "\"end\":{\"weight\":2}") && strstr(body, "\"stop_retried\":1"));
  CHECK(strstr(body, "\"stop_latency_ms\":{\"median\":3691,\"max\":3691}"));
  CHECK(strstr(body, "\"overshoot_g\":{\"median\":1.8,\"max\":1.8}"));
  CHECK(strstr(body, "\"machine_busy\":1"));
  CHECK(strstr(body, "\"n\":5,\"p\":\"power: connect failed after # ms: timed out (code #)\""));
  CHECK(!strstr(body, "brew\":{"));  // no stats for a role with nothing paired
  CHECK(!strstr(body, "\"emu\""));
  in.emulator = true;
  CHECK(buildDailyReport(in, *w, body, sizeof(body)) > 0 && strstr(body, "\"v\":1,\"emu\":1,"));
  in.emulator = false;
  // Too small: nothing rather than half a report.
  CHECK(buildDailyReport(in, *w, body, 300) == 0);

  // The crash report.
  CrashInput c;
  c.firmware = "v0.1.19";
  c.reset = "CRASH (exception)";
  c.haveDump = true;
  strcpy(c.task, "loopTask");
  c.pc = 0x42012345;
  c.cause = "LoadProhibited";
  c.backtrace[0] = 0x42012345;
  c.backtrace[1] = 0x4200abcd;
  c.depth = 2;
  const char *lines[] = {"[  48.768] I update: up to date: v0.1.19 is the latest release"};
  CHECK(buildCrashReport(c, lines, 1, body, sizeof(body)) > 0);
  CHECK(strstr(body, "\"dump\":{\"task\":\"loopTask\",\"pc\":\"0x42012345\",\"cause\":\"LoadProhibited\",\"vaddr\":\"0x00000000\",\"bt\":[\"0x42012345\",\"0x4200abcd\"]"));

  // The envelope: the library's, plus the key; signed over the body as it is sent.
  FakeSigner signer;
  static char envelope[DIAG_ENVELOPE_MAX];
  CHECK(sealReport(signer, body, envelope, sizeof(envelope)) > 0);
  char pub[100];
  base64Encode(signer.key, PUBLIC_KEY_LEN, pub, sizeof(pub));
  char expectPub[128];
  snprintf(expectPub, sizeof(expectPub), "\"pub\":\"%s\"", pub);
  CHECK(strstr(envelope, expectPub) && strstr(envelope, "\"alg\":\"ES256\""));
  uint8_t hash[32];
  sha256(body, strlen(body), hash);
  CHECK(memcmp(hash, signer.lastHash, 32) == 0);
  delete w;
}

static void hexOf(const uint8_t *bytes, size_t n, char *out) {
  for (size_t i = 0; i < n; ++i) snprintf(out + 2 * i, 3, "%02x", bytes[i]);
}

static void testShare() {
  printf("share: hash, records, signed envelope\n");
  uint8_t hash[32];
  char hex[65];
  sha256("abc", 3, hash);
  hexOf(hash, 32, hex);
  CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
  sha256("", 0, hash);
  hexOf(hash, 32, hex);
  CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
  const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";   // two blocks
  sha256(two, strlen(two), hash);
  hexOf(hash, 32, hex);
  CHECK(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
  Sha256 parts;                                    // fed in pieces: the same result
  parts.update(two, 10);
  parts.update(two + 10, strlen(two) - 10);
  uint8_t again[32];
  parts.finish(again);
  CHECK(memcmp(hash, again, 32) == 0);

  char b64[32];
  CHECK(base64Encode(reinterpret_cast<const uint8_t *>("foobar"), 6, b64, sizeof(b64)) == 8 &&
        strcmp(b64, "Zm9vYmFy") == 0);
  base64Encode(reinterpret_cast<const uint8_t *>("f"), 1, b64, sizeof(b64));
  CHECK(strcmp(b64, "Zg==") == 0);
  base64Encode(reinterpret_cast<const uint8_t *>("fo"), 2, b64, sizeof(b64));
  CHECK(strcmp(b64, "Zm8=") == 0);
  CHECK(base64Encode(reinterpret_cast<const uint8_t *>("foobar"), 6, b64, 8) == 0);   // no room

  char at[21], date[11];
  timeText(1791672710, at);
  CHECK(strcmp(at, "2026-10-10T22:51:50Z") == 0);
  timeText(951782400, at);                         // a leap day
  CHECK(strcmp(at, "2000-02-29T00:00:00Z") == 0);
  timeText(0, at);
  CHECK(at[0] == '\0');
  dateText(20260303, date);
  CHECK(strcmp(date, "2026-03-03") == 0);

  char small[64];
  JsonOut j(small, sizeof(small));
  j.open();
  j.text("a", "say \"hi\"\\\n");
  j.number("n", 36.0, 2);
  j.number("f", 2.105, 2);
  j.textIf("skip", "");
  j.open("o");
  j.integer("i", -3);
  j.close();
  j.close();
  CHECK(j.ok());
  CHECK(strcmp(small, "{\"a\":\"say \\\"hi\\\"\\\\\\u000a\",\"n\":36,\"f\":2.1,\"o\":{\"i\":-3}}") == 0 ||
        strcmp(small, "{\"a\":\"say \\\"hi\\\"\\\\\\u000a\",\"n\":36,\"f\":2.11,\"o\":{\"i\":-3}}") == 0);
  char tiny[8];
  JsonOut full(tiny, sizeof(tiny));
  full.open();
  full.text("key", "does not fit");
  full.close();
  CHECK(!full.ok() && strlen(tiny) < sizeof(tiny));

  FakeSigner signer;
  char kid[KEY_ID_LEN];
  keyId(signer.publicKey(), kid);
  CHECK(strlen(kid) == 20);

  Coffee coffee;
  coffee.id = 4;
  copyText(coffee.name, "Idjwi Island", COFFEE_NAME_LEN);
  copyText(coffee.roaster, "Portrait", COFFEE_NAME_LEN);
  copyText(coffee.process, "washed", PROCESS_LEN);
  coffee.roastLevel = 2;
  coffee.altitudeM = 1900;
  coffee.roastDate = 20260303;
  static char body[SHARE_BODY_MAX], envelope[SHARE_ENVELOPE_MAX];
  CHECK(buildCoffeeRecord(coffee, 7, 1791672710, body, sizeof(body)) != 0);
  CHECK(strcmp(body, "{\"t\":\"coffee\",\"v\":1,\"at\":\"2026-10-10T22:51:50Z\",\"record\":7,\"local_id\":4,"
                     "\"name\":\"Idjwi Island\",\"roaster\":\"Portrait\",\"process\":\"washed\","
                     "\"roast_level\":\"medium-light\",\"altitude_m\":1900}") == 0);
  CHECK(buildCoffeeRecord(coffee, 7, 0, body, 20) == 0);          // does not fit: nothing

  CHECK(buildKeyRecord(signer, 0, body, sizeof(body)) != 0);
  CHECK(strstr(body, "\"t\":\"key\"") && strstr(body, "\"alg\":\"ES256\"") && !strstr(body, "\"at\""));

  Grinder grinder;
  grinder.modelId = 1;
  copyText(grinder.model, "Mahlkonig E64 WS", GRINDER_NAME_LEN);
  copyText(grinder.burrs, "64 mm flat", GRINDER_BURRS_LEN);
  Recipe preset;
  preset.doseG = 18.0f;
  preset.targetWeightG = 36.0f;
  preset.targetTimeS = 26.0f;
  GrindAdvice advice;
  advice.action = GrindAction::HOLD;
  advice.currentUm = 246;
  advice.averageTimeS = 25.62f;
  advice.shotsUsed = 3;
  advice.minTimeS = 25.3f;
  advice.maxTimeS = 25.9f;
  CHECK(buildDialinRecord(preset, coffee, grinder, advice, GrindTimeBasis::TOTAL, 1791672710,
                          body, sizeof(body)) != 0);
  CHECK(strstr(body, "\"t\":\"dialin\"") && strstr(body, "\"roast_date\":\"2026-03-03\"") &&
        strstr(body, "\"recipe\":{\"dose_g\":18,\"yield_g\":36,\"target_time_s\":26}") &&
        strstr(body, "\"grinder\":{\"model_id\":1,\"library_id\":\"mahlkonig-e64-ws\",\"model\":\"Mahlkonig E64 WS\",\"burrs\":\"64 mm flat\","
                     "\"unit\":\"um\",\"um_per_unit\":1,\"calibration\":0}") &&
        strstr(body, "\"grind\":246,\"result\":{\"time_s\":25.6,\"basis\":\"total\",\"shots\":3,"
                     "\"min_s\":25.3,\"max_s\":25.9}}"));

  ShotRecord shot = grindRecord(25.4f, 246, 26.0f);
  shot.id = 60;
  shot.epochS = 1791672710;
  shot.finalWeightG = 35.8f;
  shot.doseG = 18.0f;
  shot.targetWeightG = 36.0f;
  shot.firstDropS = 8.4f;
  shot.pourTimeS = 17.0f;
  shot.pourFlowGS = 2.1f;
  shot.roastDate = 20260303;
  shot.rating = 4;
  shot.taste = static_cast<uint8_t>(Taste::BALANCED);
  shot.bloomPauseMs = 8000;
  shot.preinfusionHoldMs = 5000;
  CHECK(buildShotRecord(shot, &coffee, grinder, false, body, sizeof(body)) != 0);
  CHECK(strstr(body, "\"t\":\"shot\",\"v\":1,\"id\":60") && strstr(body, "\"yield_g\":35.8") &&
        strstr(body, "\"preinfusion_hold_s\":5,\"bloom_pause_s\":8") &&
        strstr(body, "\"end\":\"weight\"") && !strstr(body, "taste"));
  CHECK(buildShotRecord(shot, &coffee, grinder, true, body, sizeof(body)) != 0);
  CHECK(strstr(body, "\"taste\":{\"rating\":4,\"verdict\":\"balanced\"}"));
  copyText(shot.coffee, "Gone since", COFFEE_NAME_LEN);
  CHECK(buildShotRecord(shot, nullptr, grinder, false, body, sizeof(body)) != 0);
  CHECK(strstr(body, "\"coffee\":{\"name\":\"Gone since\"}"));

  // The envelope: the body as a string, signed as it stands.
  CHECK(buildCoffeeRecord(coffee, 7, 1791672710, body, sizeof(body)) != 0);
  const size_t sealed = sealRecord(signer, body, envelope, sizeof(envelope));
  CHECK(sealed != 0 && sealed == strlen(envelope));
  sha256(body, strlen(body), hash);
  CHECK(memcmp(hash, signer.lastHash, 32) == 0);               // what was signed is the body
  char expect[64];
  snprintf(expect, sizeof(expect), "{\"v\":1,\"kid\":\"%s\",\"alg\":\"ES256\",\"sig\":\"", kid);
  CHECK(strncmp(envelope, expect, strlen(expect)) == 0);
  CHECK(strstr(envelope, "\",\"body\":\"{\\\"t\\\":\\\"coffee\\\""));
  signer.refuse = true;
  CHECK(sealRecord(signer, body, envelope, sizeof(envelope)) == 0);
  signer.refuse = false;

  // Where a coffee stands with the library.
  Library lib = defaultLibrary();
  Coffee fresh = coffee;
  fresh.id = 0;                                      // new to this shelf
  const uint16_t cid = saveCoffee(lib, fresh);
  Coffee *mine = findCoffee(lib, cid);
  CHECK(mine && coffeeShareState(*mine, 0) == ShareState::LOCAL && !coffeeChangedSinceShared(*mine));
  markCoffeeQueued(*mine, 7);                        // its record is number 7 in the outbox
  CHECK(coffeeShareState(*mine, 7) == ShareState::QUEUED);    // 7 has not gone yet
  CHECK(coffeeShareState(*mine, 8) == ShareState::SENT);      // now it has
  CHECK(!coffeeChangedSinceShared(*mine));
  // The owner edits it through the web form: what they typed changes, where
  // it stands does not, and the edit is noticed.
  Coffee edit = *mine;
  edit.shareState = 0;                               // the form knows nothing of these
  edit.shareSeq = edit.shareHash = 0;
  copyText(edit.process, "natural", PROCESS_LEN);
  CHECK(saveCoffee(lib, edit) == cid);
  mine = findCoffee(lib, cid);
  CHECK(mine->shareSeq == 7 && coffeeShareState(*mine, 8) == ShareState::SENT);
  CHECK(strcmp(mine->process, "natural") == 0 && coffeeChangedSinceShared(*mine));
  edit = *mine;
  edit.roastDate = 20261001;                         // a new bag is not a new description
  copyText(edit.process, "washed", PROCESS_LEN);
  saveCoffee(lib, edit);
  mine = findCoffee(lib, cid);
  CHECK(!coffeeChangedSinceShared(*mine));
  // The library's verdict, on the version it was given.
  CHECK(!applyCoffeeReview(lib, cid, 6, ShareState::PUBLISHED, "", "x/y"));   // an older record
  CHECK(!applyCoffeeReview(lib, 99, 7, ShareState::PUBLISHED, "", "x/y"));    // not ours
  CHECK(!applyCoffeeReview(lib, cid, 7, ShareState::SENT, "", ""));           // not a verdict
  CHECK(applyCoffeeReview(lib, cid, 7, ShareState::IN_REVIEW, "", ""));
  CHECK(coffeeShareState(*mine, 8) == ShareState::IN_REVIEW);
  CHECK(!applyCoffeeReview(lib, cid, 7, ShareState::IN_REVIEW, "", ""));      // told twice: no change
  CHECK(applyCoffeeReview(lib, cid, 7, ShareState::DECLINED, "already listed as\tIdjwi", ""));
  CHECK(coffeeShareState(*mine, 8) == ShareState::DECLINED);
  CHECK(strcmp(mine->reviewNote, "already listed asIdjwi") == 0);            // control characters dropped
  CHECK(strcmp(mine->name, "Idjwi Island") == 0);                             // what was typed stays
  CHECK(applyCoffeeReview(lib, cid, 7, ShareState::PUBLISHED, "", "portrait/idjwi-island"));
  CHECK(strcmp(mine->libraryId, "portrait/idjwi-island") == 0 && mine->reviewNote[0] == '\0');
  // Sent again after an edit: a fresh start for the new version.
  copyText(mine->notes, "plum", NOTES_LEN);
  CHECK(coffeeChangedSinceShared(*mine));
  markCoffeeQueued(*mine, 12);
  CHECK(coffeeShareState(*mine, 12) == ShareState::QUEUED && !coffeeChangedSinceShared(*mine));
  CHECK(!applyCoffeeReview(lib, cid, 7, ShareState::DECLINED, "late", ""));   // about the old version
  // A coffee never shared is never touched by a verdict.
  Coffee other;
  copyText(other.name, "Guji", COFFEE_NAME_LEN);
  other.shareState = static_cast<uint8_t>(ShareState::PUBLISHED);            // as if a form sent it
  const uint16_t oid = saveCoffee(lib, other);
  CHECK(findCoffee(lib, oid)->shareState == 0);
  CHECK(!applyCoffeeReview(lib, oid, 0, ShareState::PUBLISHED, "", "a/b"));
  // And it all survives being saved.
  static uint8_t bytes[LIBRARY_MAX_BYTES];
  Library back;
  CHECK(decodeLibrary(bytes, encodeLibrary(lib, bytes, sizeof(bytes)), back));
  CHECK(findCoffee(back, cid)->shareSeq == 12 &&
        strcmp(findCoffee(back, cid)->libraryId, "portrait/idjwi-island") == 0);

  CHECK(sealRecord(signer, body, envelope, 40) == 0);          // no room
  CHECK(sealRecord(signer, "", envelope, sizeof(envelope)) == 0);
}

// What the shared library says, turned into a setting for this grinder.
static void testCommunityModel() {
  printf("community: a new owner placed by what the library learned of their grinder model\n");
  // A Sette 270 owner with no coffee in common with anyone; a coffee dialled by others on grinders
  // linked into one relative axis (no grinder marked in microns there).
  Grinder sette;
  sette.modelId = 11;
  normalizeGrinder(sette);
  CommunityDialin d;
  d.found = true;
  d.contributors = 3;
  d.dialins = 4;
  d.finenessUm = 50.0f;      // on axis 7
  d.spreadUm = 1.0f;
  d.axis = 7;
  CommunityFit noFit;
  CommunityModel m;
  m.known = true;
  m.scale = 2.0f;            // two of its steps per axis step
  m.zero = 30.0f;
  m.zeroSpread = 4.0f;
  m.zeroN = 4;
  m.axis = 7;
  CommunityStart s = communityStart(d, noFit, sette, AgeModel{}, -1, 0.0f, 255, m);
  CHECK(s.basis == StartBasis::MODEL && s.setting == 130);                // 2 x 50 + 30
  CHECK(s.range == 8);                                                     // 1.5 x 4 + 2 x 1
  // With two owners only, the range doubles.
  m.zeroN = 2;
  CHECK(communityStart(d, noFit, sette, AgeModel{}, -1, 0.0f, 255, m).range == 16);
  // One grinder's zero is not the model's: nothing from one owner.
  m.zeroN = 1;
  CHECK(communityStart(d, noFit, sette, AgeModel{}, -1, 0.0f, 255, m).basis == StartBasis::NONE);
  m.zeroN = 4;
  // A fineness on another axis cannot be read with this model's scale.
  m.axis = 8;
  CHECK(communityStart(d, noFit, sette, AgeModel{}, -1, 0.0f, 255, m).basis == StartBasis::NONE);
  m.axis = 7;
  // Others with the same model dialled this coffee: their median comes first.
  d.modelCount = 2;
  d.modelMedian = 128.0f;
  CHECK(communityStart(d, noFit, sette, AgeModel{}, -1, 0.0f, 255, m).basis == StartBasis::SAME_MODEL);
  d.modelCount = 0;
  d.modelMedian = 0.0f;
  // Once this grinder has its own fit on the axis, that is what is used.
  CommunityFit own;
  own.known = true;
  own.scale = 2.1f;
  own.offset = 25.0f;
  own.coffeesInCommon = 2;
  own.axis = 7;
  s = communityStart(d, own, sette, AgeModel{}, -1, 0.0f, 255, m);
  CHECK(s.basis == StartBasis::FITTED && s.setting == 130);                // 2.1 x 50 + 25
  CHECK(strcmp(startBasisName(StartBasis::MODEL), "model") == 0);
  // The model's library entry goes out in records, so its owners are pooled under one key.
  static char body[SHARE_BODY_MAX];
  JsonOut j(body, sizeof(body));
  j.open();
  grinderFields(j, sette);
  j.close();
  CHECK(strstr(body, "\"model_id\":11,\"library_id\":\"baratza-sette-270\"") != nullptr);
}

static void testCommunity() {
  printf("community: a starting grind from the shared library\n");
  Grinder e64;
  e64.modelId = 1;
  CommunityDialin d;
  CommunityFit fit;
  CHECK(communityStart(d, fit, e64).basis == StartBasis::NONE);   // nothing in the library
  d.found = true;
  d.contributors = 4;
  d.dialins = 6;
  d.finenessUm = 236.0f;
  d.spreadUm = 3.0f;
  // Nothing known about this grinder: the common scale read as microns, wide.
  CommunityStart s = communityStart(d, fit, e64);
  CHECK(s.basis == StartBasis::MICRONS && s.setting == 236 && s.range == 25);
  // Others with the same model: their median, narrower.
  d.modelMedian = 235.0f;
  d.modelCount = 2;
  s = communityStart(d, fit, e64);
  CHECK(s.basis == StartBasis::SAME_MODEL && s.setting == 235 && s.range == 15);
  // This grinder fitted from coffees in common: its own offset, narrowest.
  fit.known = true;
  fit.scale = 1.0f;
  fit.offset = -4.0f;
  fit.coffeesInCommon = 1;
  s = communityStart(d, fit, e64);
  CHECK(s.basis == StartBasis::FITTED && s.setting == 232 && s.range == 10);  // one coffee: never tighter
  fit.coffeesInCommon = 3;
  CHECK(communityStart(d, fit, e64).range == 5);
  d.spreadUm = 9.0f;
  CHECK(communityStart(d, fit, e64).range == 9);             // contributors disagree: wider
  d.spreadUm = 3.0f;
  // A grinder read in steps of 5 microns (not a listed model).
  Grinder stepped;
  stepped.umPerUnit = 5.0f;
  CommunityFit steps;
  steps.known = true;
  steps.scale = 0.2f;
  steps.offset = 2.0f;
  steps.coffeesInCommon = 2;
  s = communityStart(d, steps, stepped);
  CHECK(s.setting == 49 && s.range == 1);                    // 0.2 x 236 + 2
  d.modelCount = 0;
  CHECK(communityStart(d, CommunityFit{}, stepped).setting == 47);   // 236 / 5, unfitted
  // An unmarked dial with no fit and nobody on the same model: no suggestion.
  Grinder dial;
  dial.umPerUnit = 0.0f;
  CHECK(communityStart(d, CommunityFit{}, dial).basis == StartBasis::NONE);
  // A fit that gives nonsense is not passed on.
  steps.offset = -500.0f;
  CHECK(communityStart(d, steps, stepped).setting == 0);

  // A library with no grinder marked in microns near this coffee: its fineness is on a
  // relative axis, and means something only to a fit on that same axis.
  {
    CommunityDialin rel;
    rel.found = true;
    rel.contributors = 3;
    rel.finenessUm = 236.0f;
    rel.spreadUm = 3.0f;
    rel.axis = 4711;
    rel.ageDays = 10;
    AgeModel rate;
    rate.known = true;
    rate.umPerDay = -0.8f;
    CommunityFit onAxis;
    onAxis.known = true;
    onAxis.scale = 0.2f;
    onAxis.offset = 2.0f;
    onAxis.coffeesInCommon = 1;
    onAxis.axis = 4711;
    CommunityStart r = communityStart(rel, onAxis, dial, rate, 31);
    CHECK(r.basis == StartBasis::FITTED && r.setting == 49);   // 0.2 x 236 + 2
    CHECK(r.ageShift == 0);                                    // the rate is in microns: not applied here
    // The same fit on another axis, or on microns, is not used...
    CommunityFit other = onAxis;
    other.axis = 99;
    CHECK(communityStart(rel, other, dial).basis == StartBasis::NONE);
    other.axis = 0;
    CHECK(communityStart(rel, other, dial).basis == StartBasis::NONE);
    // ...and a number that is not microns is never read as microns.
    CHECK(communityStart(rel, CommunityFit{}, stepped).basis == StartBasis::NONE);
    // Owners of the same model still help, on any axis.
    rel.modelMedian = 41.0f;
    rel.modelCount = 2;
    CommunityStart m = communityStart(rel, other, dial);
    CHECK(m.basis == StartBasis::SAME_MODEL && m.setting == 41);
    // A coffee nothing could place (no fineness) but with a same-model median.
    CommunityDialin unplaced;
    unplaced.found = true;
    unplaced.contributors = 2;
    unplaced.modelMedian = 17.0f;
    unplaced.modelCount = 2;
    CHECK(communityStart(unplaced, CommunityFit{}, dial).basis == StartBasis::SAME_MODEL);
    CHECK(communityStart(unplaced, onAxis, dial).basis == StartBasis::SAME_MODEL);
    // A fit on a relative axis does not turn a micron shift into dial units.
    CHECK(grinderUnitsPerUm(onAxis, dial) == 0.0f && grinderUnitsPerUm(onAxis, stepped) == 0.2f);
  }

  // How wide a same-model suggestion is: measured where the library can say, a sixth of the
  // setting for an unmarked dial it cannot, and never the old 1-unit claim.
  {
    CommunityDialin sm;
    sm.found = true;
    sm.contributors = 3;
    sm.modelMedian = 30.0f;
    sm.modelCount = 3;
    Grinder niche;
    niche.umPerUnit = 0.0f;
    CommunityStart w = communityStart(sm, CommunityFit{}, niche);
    CHECK(w.basis == StartBasis::SAME_MODEL && w.setting == 30 && w.range == 5);   // 30 / 6
    sm.modelSpread = 2.0f;
    CHECK(communityStart(sm, CommunityFit{}, niche).range == 3);                   // 1.5 x 2
    sm.modelSpread = 0.2f;
    CHECK(communityStart(sm, CommunityFit{}, niche).range == 1);                   // never under one unit
    sm.modelSpread = 4.0f;
    CHECK(communityStart(sm, CommunityFit{}, e64).range == 6);                     // measured, in its unit
    sm.modelSpread = 0.0f;
    CHECK(communityStart(sm, CommunityFit{}, e64).range == 15);                    // as before
  }

  // Flow. Their dial-ins were settled at 1.1 g/s; this recipe aims for a faster 1.4.
  {
    CommunityDialin fl;
    fl.found = true;
    fl.contributors = 3;
    fl.finenessUm = 240.0f;
    fl.spreadUm = 3.0f;
    fl.flowGs = 1.1f;
    fl.flowBasis = 0;
    fl.kappa = 70.0f;
    CommunityStart same = communityStart(fl, CommunityFit{}, e64, AgeModel{}, -1, 1.1f, 0);
    CHECK(same.setting == 240 && same.flowShift == 0);                  // the same flow: no move
    CommunityStart fast = communityStart(fl, CommunityFit{}, e64, AgeModel{}, -1, 1.4f, 0);
    CHECK(fast.setting == 257 && fast.flowShift == 17);                 // 70 x ln(1.4 / 1.1), coarser
    CommunityStart slow = communityStart(fl, CommunityFit{}, e64, AgeModel{}, -1, 0.9f, 0);
    CHECK(slow.flowShift == -14 && slow.setting == 226);
    CHECK(communityStart(fl, CommunityFit{}, e64, AgeModel{}, -1, 1.4f, 1).flowShift == 0);   // other basis
    CHECK(communityStart(fl, CommunityFit{}, e64, AgeModel{}, -1, 0.0f, 0).flowShift == 0);   // no target
    CommunityDialin noKappa = fl;
    noKappa.kappa = 0.0f;
    CHECK(communityStart(noKappa, CommunityFit{}, e64, AgeModel{}, -1, 1.4f, 0).flowShift == 0);
    CHECK(communityStart(fl, CommunityFit{}, e64, AgeModel{}, -1, 9.0f, 0).flowShift == 35);  // capped: ln 1.65
    // In steps of 5 microns, through the grinder's own fit: 17 microns is 3 steps.
    CommunityFit lf;
    lf.known = true;
    lf.scale = 0.2f;
    lf.offset = 2.0f;
    lf.coffeesInCommon = 2;
    Grinder st;
    st.umPerUnit = 5.0f;
    CommunityStart sn = communityStart(fl, lf, st, AgeModel{}, -1, 1.4f, 0);
    CHECK(sn.setting == 53 && sn.flowShift == 3);                       // 0.2 x 240 + 2 = 50, plus 3.4
    // On a relative axis the shift is in that axis' steps, through the fit on the same axis.
    fl.axis = 4711;
    lf.axis = 4711;
    fl.kappa = 20.0f;                                                   // units of the axis, not microns
    CHECK(communityStart(fl, lf, st, AgeModel{}, -1, 1.4f, 0).flowShift == 1);
    CHECK(communityStart(fl, CommunityFit{}, st, AgeModel{}, -1, 1.4f, 0).basis == StartBasis::NONE);
  }

  CHECK(sameRecipeClass(18.0f, 36.0f, 18.5f, 37.5f) && !sameRecipeClass(18.0f, 36.0f, 18.0f, 27.0f));

  // A recipe the library does not have exactly: the nearest one within reach.
  CHECK(recipeDistance(18.0f, 36.0f, 18.5f, 37.5f) == 0.0f);                    // the same class
  CHECK(recipeDistance(18.0f, 40.0f, 18.0f, 36.0f) > 0.0f);                     // 40 g out from 36 g out: near
  CHECK_NEAR(recipeDistance(18.0f, 40.0f, 18.0f, 36.0f), 0.351, 0.01);         // ln(40/36) / 0.3
  CHECK_NEAR(recipeDistance(19.0f, 36.0f, 18.0f, 36.0f), 0.5, 0.001);           // 1 g of 2 g
  CHECK(recipeDistance(18.0f, 54.0f, 18.0f, 36.0f) < 0.0f);                     // a 1:3 against a 1:2: too far
  CHECK(recipeDistance(21.0f, 36.0f, 18.0f, 36.0f) < 0.0f);                     // 3 g more dose
  CHECK(recipeDistance(0.0f, 36.0f, 18.0f, 36.0f) < 0.0f);                      // not set
  {
    CommunityDialin exact, near;
    exact.found = near.found = true;
    exact.contributors = near.contributors = 3;
    exact.finenessUm = near.finenessUm = 236.0f;
    exact.spreadUm = near.spreadUm = 3.0f;
    near.recipeDistance = 1.0f;
    CommunityFit f;
    f.known = true;
    f.scale = 1.0f;
    f.offset = -4.0f;
    f.coffeesInCommon = 3;
    Grinder g;
    g.umPerUnit = 1.0f;
    CommunityStart a = communityStart(exact, f, g), b = communityStart(near, f, g);
    CHECK(a.setting == 232 && a.range == 5 && !a.nearRecipe);
    CHECK(b.setting == 232 && b.range == 12 && b.nearRecipe);                    // 5 + 3% of 232
  }

  // Bean age. Dates first: the bag in the history was 221 days off roast on 10 Oct 2026.
  CHECK(daysFromDate(19700101) == 0 && daysFromDate(20000229) == 11016 && daysFromDate(20261010) == 20736);
  CHECK(beanAgeDays(20260303, 1791672710) == 221);
  CHECK(beanAgeDays(20261010, 1791672710) == 0 && beanAgeDays(20261011, 1791672710) == -1);   // not roasted yet
  CHECK(beanAgeDays(0, 1791672710) == -1 && beanAgeDays(20260303, 0) == -1);
  AgeModel none, aging;
  aging.known = true;
  aging.umPerDay = -0.8f;                                    // finer as it ages
  CHECK(ageShiftUm(none, 10, 30) == 0.0f);                   // no rate measured: no allowance
  CHECK_NEAR(ageShiftUm(aging, 10, 30), -16.0, 0.001);
  CHECK_NEAR(ageShiftUm(aging, 30, 10), 16.0, 0.001);        // a younger bag: coarser
  CHECK(ageShiftUm(aging, -1, 30) == 0.0f && ageShiftUm(aging, 10, -1) == 0.0f);
  CHECK_NEAR(ageShiftUm(aging, 10, 400), -60.0, 0.001);      // not stretched past 90 days, nor past 60 um
  CHECK_NEAR(ageShiftUm(aging, 100, 200), 0.0, 0.001);       // both beyond what was measured
  // A suggestion for a bag three weeks older than the contributors' were.
  CommunityDialin aged;
  aged.found = true;
  aged.contributors = 3;
  aged.finenessUm = 240.0f;
  aged.spreadUm = 3.0f;
  aged.ageDays = 10;
  CommunityFit mine;
  mine.known = true;
  mine.scale = 1.0f;
  mine.offset = -4.0f;
  mine.coffeesInCommon = 2;
  CommunityStart plain = communityStart(aged, mine, e64);
  CHECK(plain.setting == 236 && plain.ageShift == 0);        // age not given: as before
  CommunityStart older = communityStart(aged, mine, e64, aging, 31);
  CHECK(older.setting == 219 && older.ageShift == -17);      // 236 - 0.8 x 21
  CHECK(communityStart(aged, mine, e64, aging, 10).ageShift == 0);
  CHECK(communityStart(aged, mine, e64, none, 31).setting == 236);    // no rate: no allowance
  aged.ageDays = -1;
  CHECK(communityStart(aged, mine, e64, aging, 31).setting == 236);   // their age unknown: none either
  aged.ageDays = 10;
  // On a grinder read in steps of 5 microns the same 16.8 microns is 3 steps.
  CommunityFit stepFit;
  stepFit.known = true;
  stepFit.scale = 0.2f;
  stepFit.offset = 2.0f;
  stepFit.coffeesInCommon = 2;
  Grinder stepper;
  stepper.umPerUnit = 5.0f;
  CommunityStart inSteps = communityStart(aged, stepFit, stepper, aging, 31);
  CHECK(inSteps.setting == 47 && inSteps.ageShift == -3);    // 0.2 x 240 + 2 = 50, less 3.36
  // An unmarked dial with no fit: nothing to convert microns with, so no allowance.
  Grinder unmarked;
  unmarked.umPerUnit = 0.0f;
  CHECK(grinderUnitsPerUm(CommunityFit{}, unmarked) == 0.0f);
  char file[80];
  CHECK(libraryIdToFile("portrait/idjwi-island", file, sizeof(file)) &&
        strcmp(file, "portrait__idjwi-island") == 0);
  CHECK(!libraryIdToFile("../etc/passwd", file, sizeof(file)));      // only what an id may hold
  CHECK(!libraryIdToFile("Portrait/Idjwi", file, sizeof(file)) && !libraryIdToFile("", file, sizeof(file)));
  CHECK(!libraryIdToFile("a/b", file, 4));

  // A coffee taken from the library: published already, and its records say which entry it is.
  Library lib = defaultLibrary();
  Coffee c;
  copyText(c.name, "Ethiopia Guji", COFFEE_NAME_LEN);
  c.shareSeq = 99;                                           // whatever a form might send
  const uint16_t id = adoptCoffee(lib, c, "example/ethiopia-guji");
  const Coffee *got = findCoffee(lib, id);
  CHECK(got && coffeeShareState(*got, 0) == ShareState::PUBLISHED && got->shareSeq == 0);
  CHECK(strcmp(got->libraryId, "example/ethiopia-guji") == 0 && !coffeeChangedSinceShared(*got));
  CHECK(adoptCoffee(lib, c, "example/ethiopia-guji") == id && lib.coffeeCount == 1);   // not twice
  CHECK(adoptCoffee(lib, c, "") == 0);
  static char body[SHARE_BODY_MAX];
  CHECK(buildCoffeeRecord(*got, 0, 0, body, sizeof(body)) != 0 &&
        strstr(body, "\"library_id\":\"example/ethiopia-guji\""));
}

// One grinder in use, perhaps a second kept beside it; a grinder that is not
// a known model can be sent to the library the way a coffee is.
static void testGrinders() {
  printf("grinders: a second grinder, and sharing one\n");
  Library lib = defaultLibrary();
  Grinder e64;
  e64.modelId = 1;
  setGrinder(lib, e64, false);
  const uint16_t firstId = lib.grinder.id, firstCal = lib.grinder.calibration;
  CHECK(firstId != 0 && firstCal == 0 && !lib.hasOtherGrinder);
  lib.presets[0].grindUm = 246;
  lib.presets[0].sharedGrindUm = 246;
  lib.presets[1].grindUm = 250;

  // A second grinder, typed in: not a known model.
  Grinder own;
  copyText(own.model, "Home Built 64", GRINDER_NAME_LEN);
  copyText(own.burrs, "64 mm flat", GRINDER_BURRS_LEN);
  copyText(own.unit, "dial", GRINDER_UNIT_LEN);
  own.umPerUnit = 0.0f;
  own.shareState = static_cast<uint8_t>(ShareState::PUBLISHED);   // as if a form claimed it
  CHECK(addOtherGrinder(lib, own));
  CHECK(lib.hasOtherGrinder && lib.otherGrinder.id != firstId && lib.otherGrinder.shareState == 0);
  CHECK(lib.otherGrinder.calibration != firstCal);
  CHECK(!addOtherGrinder(lib, own));                              // two is the limit
  CHECK(lib.presets[0].grindUm == 246 && lib.presets[0].otherGrindUm == 0);   // nothing in use changed

  // Put it in use: every preset's setting for it comes with it.
  const uint16_t dialCal = lib.otherGrinder.calibration, dialId = lib.otherGrinder.id;
  CHECK(swapGrinders(lib));
  CHECK(strcmp(lib.grinder.model, "Home Built 64") == 0 && lib.grinder.calibration == dialCal);
  CHECK(lib.presets[0].grindUm == 0 && lib.presets[0].otherGrindUm == 246 &&
        lib.presets[0].otherSharedGrindUm == 246 && lib.presets[1].otherGrindUm == 250);
  lib.presets[0].grindUm = 14;                                    // dialled in on the dial
  // And back: the first grinder's settings and calibration are as they were.
  CHECK(swapGrinders(lib));
  CHECK(lib.grinder.id == firstId && lib.grinder.calibration == firstCal);
  CHECK(lib.presets[0].grindUm == 246 && lib.presets[0].sharedGrindUm == 246 &&
        lib.presets[0].otherGrindUm == 14 && lib.presets[1].grindUm == 250);
  // Zeroing the one in use again does not touch the other.
  setGrinder(lib, e64, true);
  CHECK(lib.grinder.calibration != firstCal && lib.grinder.calibration != dialCal &&
        lib.otherGrinder.calibration == dialCal);

  // Sharing. A known model has nothing to send; the typed one has.
  CHECK(!grinderCanBeShared(lib.grinder) && grinderCanBeShared(lib.otherGrinder));
  Grinder *typed = findGrinder(lib, dialId);
  CHECK(typed == &lib.otherGrinder && findGrinder(lib, 999) == nullptr);
  static char body[SHARE_BODY_MAX];
  CHECK(buildGrinderRecord(*typed, 21, 1791672710, body, sizeof(body)) != 0);
  char expect[200];
  snprintf(expect, sizeof(expect),
           "{\"t\":\"grinder\",\"v\":1,\"at\":\"2026-10-10T22:51:50Z\",\"record\":21,\"local_id\":%u,"
           "\"model\":\"Home Built 64\",\"burrs\":\"64 mm flat\",\"unit\":\"dial\"}",
           static_cast<unsigned>(dialId));
  CHECK(strcmp(body, expect) == 0);
  markGrinderQueued(*typed, 21);
  CHECK(grinderShareState(*typed, 21) == ShareState::QUEUED &&
        grinderShareState(*typed, 22) == ShareState::SENT && !grinderCanBeShared(*typed));
  {
    // Refused by the library's checks: it may be sent again as it is; declined it may not.
    Grinder refused = *typed, declined = *typed;
    CHECK(applyReviewTo(&refused, 21, ShareState::REFUSED, "burrs is not text of 1 to 23 characters", ""));
    CHECK(grinderCanBeShared(refused) && grinderShareState(refused, 22) == ShareState::REFUSED);
    CHECK(applyReviewTo(&declined, 21, ShareState::DECLINED, "already listed", ""));
    CHECK(!grinderCanBeShared(declined));
  }
  CHECK(!applyGrinderReview(lib, dialId, 20, ShareState::PUBLISHED, "", "home-built-64"));   // an older version
  CHECK(applyGrinderReview(lib, dialId, 21, ShareState::IN_REVIEW, "", ""));
  CHECK(applyGrinderReview(lib, dialId, 21, ShareState::PUBLISHED, "", "home-built-64"));
  CHECK(strcmp(typed->libraryId, "home-built-64") == 0);
  // Its dial-ins now say which entry of the library's list it is.
  JsonOut j(body, sizeof(body));
  j.open();
  grinderFields(j, *typed);
  j.close();
  CHECK(strstr(body, "\"library_id\":\"home-built-64\"") != nullptr);
  // Brought into use, it keeps its standing; described differently, that shows.
  CHECK(swapGrinders(lib));
  CHECK(grinderShareState(lib.grinder, 22) == ShareState::PUBLISHED);
  Grinder edit = lib.grinder;
  edit.shareState = 0;
  edit.libraryId[0] = '\0';
  copyText(edit.burrs, "64 mm SSP", GRINDER_BURRS_LEN);
  setGrinder(lib, edit, false);
  CHECK(grinderShareState(lib.grinder, 22) == ShareState::PUBLISHED &&
        grinderChangedSinceShared(lib.grinder) && grinderCanBeShared(lib.grinder));
  // Replaced by another model altogether: that one starts with no standing.
  Grinder other;
  copyText(other.model, "Something Else", GRINDER_NAME_LEN);
  setGrinder(lib, other, false);
  CHECK(lib.grinder.shareState == 0 && lib.grinder.libraryId[0] == '\0' && lib.grinder.id == dialId);
  // One picked from the library's list is published already.
  markGrinderFromLibrary(lib.grinder, "something-else");
  CHECK(grinderShareState(lib.grinder, 0) == ShareState::PUBLISHED && !grinderCanBeShared(lib.grinder));

  // Both survive being saved, and a file from before there were two still loads.
  static uint8_t bytes[LIBRARY_MAX_BYTES];
  Library back;
  const size_t n = encodeLibrary(lib, bytes, sizeof(bytes));
  CHECK(n != 0 && decodeLibrary(bytes, n, back));
  CHECK(back.hasOtherGrinder && strcmp(back.otherGrinder.model, "Mahlkonig E64 WS") == 0 &&
        strcmp(back.grinder.libraryId, "something-else") == 0 &&
        back.presets[0].otherGrindUm == lib.presets[0].otherGrindUm &&
        back.grinder.id != back.otherGrinder.id &&
        back.grinder.calibration != back.otherGrinder.calibration);
  CHECK(removeOtherGrinder(lib) && !lib.hasOtherGrinder && lib.presets[0].otherGrindUm == 0);
  CHECK(!swapGrinders(lib) && !removeOtherGrinder(lib));
  const size_t one = encodeLibrary(lib, bytes, sizeof(bytes));
  CHECK(one == n - sizeof(Grinder) && decodeLibrary(bytes, one, back) && !back.hasOtherGrinder);
}

// Bloom: pre-infuse, pump off, wait, start again, brew to the target.
static void testBloom() {
  printf("bloom: pause between pre-infusion and brew\n");
  auto bloomRig = [](Rig &r, uint16_t pauseMs) {
    r.sim.preinfusionMs = 3000;
    r.library.recipes[0].preinfusion = Preinfusion::HOLD;
    r.library.recipes[0].preinfusionHoldMs = 5000;
    r.library.recipes[0].bloomPauseMs = pauseMs;
    r.toReady();
    r.ctl.requestStart();
  };
  Rig r;
  bloomRig(r, 10000);
  CHECK(r.runUntil(State::BLOOMING, 10000));
  CHECK(r.runUntil(State::BREWING, 20000));
  CHECK(r.sim.presses == 3 && r.sim.machineRunning);
  CHECK(r.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK(r.sim.presses == 4 && !r.sim.machineRunning);
  CHECK(r.sim.holds.size() == 4 && r.sim.holds[0] == 5000 && r.sim.holds[1] == 0 &&
        r.sim.holds[2] == BLOOM_RESTART_HOLD_MS && r.sim.holds[3] == 0);
  // The pump was off for the pause, give or take the Bot's own delays.
  const uint32_t off = r.sim.pressTimes[2] - r.sim.pressTimes[1];
  CHECK(off >= 10000 && off <= 10600);
  ShotRecord shot;
  CHECK(r.ctl.takeCompletedShot(shot));
  CHECK(shot.endReason == static_cast<uint8_t>(EndReason::WEIGHT));
  CHECK(shot.bloomPauseMs == 10000);
  CHECK_NEAR(shot.finalWeightG, 36.0, 1.5);
  CHECK(shot.shotTimeS > 13.0f);                       // the pause is part of the shot
  CHECK((shot.flags & SHOT_FLAG_STOP_CONFIRMED) != 0);

  // STOP during the pause: the pump is already off, so nothing is pressed.
  Rig u;
  bloomRig(u, 20000);
  CHECK(u.runUntil(State::BLOOMING, 10000));
  u.run(4000);
  CHECK(u.sim.presses == 2 && !u.sim.machineRunning);
  CHECK(u.ctl.status(u.sim.nowMs).bloomLeftS > 10.0f);
  u.ctl.requestStop();
  u.run(8000);
  CHECK(u.sim.presses == 2 && !u.sim.machineRunning);
  CHECK(u.ctl.state() == State::SHOT_COMPLETE);
  CHECK(u.ctl.takeCompletedShot(shot) && shot.endReason == static_cast<uint8_t>(EndReason::USER));

  // The scale drops out during the pause: again nothing to press.
  Rig lost;
  bloomRig(lost, 20000);
  CHECK(lost.runUntil(State::BLOOMING, 10000));
  lost.run(4000);
  lost.sim.scaleLink = false;
  lost.run(8000);
  CHECK(lost.sim.presses == 2 && !lost.sim.machineRunning);
  CHECK(isFault(lost.ctl.state()));

  // The first press never reached the button. The "off" press then starts
  // the machine. Coffee arriving during the pause gives it away: the shot is
  // supervised to its target and stopped, with no start press on top.
  Rig missed;
  missed.sim.ignorePresses = 1;
  bloomRig(missed, 20000);
  CHECK(missed.runUntil(State::BLOOMING, 10000));
  CHECK(missed.runUntil(State::BREWING, 12000));       // long before the pause is over
  CHECK(missed.sim.presses == 2 && missed.sim.machineRunning);
  CHECK(missed.runUntil(State::SHOT_COMPLETE, 60000));
  CHECK(missed.sim.presses == 3 && !missed.sim.machineRunning);
  CHECK(missed.ctl.takeCompletedShot(shot));
  CHECK(shot.endReason == static_cast<uint8_t>(EndReason::WEIGHT));

  // STOP while the pre-infusion hold is still on: no bloom, one stop press.
  Rig early;
  bloomRig(early, 10000);
  CHECK(early.runUntil(State::STARTING_SHOT, 10000));
  early.ctl.requestStop();
  early.run(20000);
  CHECK(early.sim.presses == 2 && !early.sim.machineRunning);

  // A bloom needs a held pre-infusion, and a sane length.
  Recipe recipe;
  recipe.bloomPauseMs = 100;
  recipe.preinfusion = Preinfusion::MACHINE;
  sanitizeRecipe(recipe);
  CHECK(recipe.bloomPauseMs == MIN_BLOOM_PAUSE_MS && recipe.preinfusion == Preinfusion::HOLD);
  recipe.bloomPauseMs = 60000;
  sanitizeRecipe(recipe);
  CHECK(recipe.bloomPauseMs == MAX_BLOOM_PAUSE_MS);
  recipe.bloomPauseMs = 0;
  recipe.preinfusion = Preinfusion::MACHINE;
  sanitizeRecipe(recipe);
  CHECK(recipe.bloomPauseMs == 0 && recipe.preinfusion == Preinfusion::MACHINE);
}

// ---------------------------------------------------------------------------
// Ratings-driven target, frozen beans, similar coffee (added with docs/COMMUNITY.md 8e, 8f, 8g)

static uint32_t epochOfDate(uint32_t yyyymmdd) {
  return static_cast<uint32_t>(daysFromDate(yyyymmdd)) * 86400u + 43200u;  // midday
}

static GrindShot ratedShot(float timeS, uint16_t um, uint8_t rating, uint8_t taste = 0) {
  GrindShot s;
  s.timeS = timeS;
  s.grindUm = um;
  s.rating = rating;
  s.taste = taste;
  return s;
}

static void testRatedTarget() {
  printf("grind advice: ratings refine the time advice, inside its tolerance\n");
  GrindConfig cfg;  // +-2 s tolerance, window 3, max step 20 um, 3 um/s guess
  const float target = 28.0f;

  // The base is untouched: with ratings off, or with too few liked shots, the answer is exactly
  // the old advice, field for field.
  {
    GrindShot shots[] = {ratedShot(29.5f, 250, 5), ratedShot(29.3f, 250, 4), ratedShot(29.1f, 250, 2)};
    const GrindAdvice plain = adviseGrind(shots, 3, 250, target, cfg);
    const GrindAdvice few = adviseGrindRated(shots, 3, 250, target, cfg, true);
    CHECK(few.rated == RatedState::TOO_FEW && few.ratedShots == 2 && few.aimTimeS == 0.0f);
    CHECK(few.action == plain.action && few.deltaUm == plain.deltaUm && few.suggestedUm == plain.suggestedUm &&
          few.errorS == plain.errorS && few.targetTimeS == target && !few.refined);
    const GrindAdvice off = adviseGrindRated(shots, 3, 250, target, cfg, false);
    CHECK(off.rated == RatedState::OFF && off.action == plain.action && off.deltaUm == plain.deltaUm &&
          off.baseAction == plain.action);
  }

  // Three liked shots at an earlier setting ran 27 s; the shots at today's setting (unrated) run
  // 29.3 s. Against the recipe's 28 s that is inside the tolerance: the base says keep. The
  // ratings say the owner liked 27 s: a small move, to the setting those shots were made at.
  GrindShot refine[] = {ratedShot(29.5f, 250, 0), ratedShot(29.3f, 250, 0), ratedShot(29.1f, 250, 0),
                        ratedShot(27.0f, 255, 5), ratedShot(27.4f, 255, 4), ratedShot(26.8f, 255, 5)};
  {
    const GrindAdvice plain = adviseGrind(refine, 6, 250, target, cfg);
    CHECK(plain.action == GrindAction::HOLD);                       // the base, unchanged, says keep
    const GrindAdvice a = adviseGrindRated(refine, 6, 250, target, cfg, true);
    CHECK(a.baseAction == GrindAction::HOLD && a.rated == RatedState::REFINING && a.refined);
    CHECK(a.action == GrindAction::COARSER && a.deltaUm > 0 && a.deltaUm <= 6 && a.suggestedUm == 250 + a.deltaUm);
    CHECK(a.targetTimeS == target);                                  // the recipe's time is still the target
    CHECK(fabsf(a.aimTimeS - 27.0f) < 0.001f && a.ratedShots == 3 && fabsf(a.ratedTimeS - 27.0f) < 0.001f);
    CHECK(a.ratedBandS >= RATED_BAND_FLOOR_S && a.ratedBandS <= cfg.toleranceS);
    // It moves by what the base's own sensitivity says the gap is worth, and never more.
    CHECK(a.learned && fabsf(static_cast<float>(a.deltaUm) - (a.averageTimeS - 27.0f) * a.umPerS) <= 1.0f);
    // And it is still inside the base range: the average, after the move, would be inside +-2 s of 28.
    CHECK(fabsf(27.0f - target) <= cfg.toleranceS);
  }
  // The same, but the owner liked a faster shot: it goes the other way (finer when slower is liked).
  {
    GrindShot slowLiked[] = {ratedShot(26.8f, 250, 0), ratedShot(26.6f, 250, 0), ratedShot(27.0f, 250, 0),
                             ratedShot(29.0f, 245, 5), ratedShot(29.4f, 245, 5), ratedShot(28.8f, 245, 4)};
    const GrindAdvice a = adviseGrindRated(slowLiked, 6, 250, target, cfg, true);
    CHECK(a.baseAction == GrindAction::HOLD && a.rated == RatedState::REFINING && a.action == GrindAction::FINER &&
          a.deltaUm < 0 && a.suggestedUm < 250);
  }

  // On the liked time already: keep, and say so.
  {
    GrindShot on[] = {ratedShot(27.2f, 250, 5), ratedShot(27.0f, 250, 5), ratedShot(26.9f, 250, 4)};
    const GrindAdvice a = adviseGrindRated(on, 3, 250, target, cfg, true);
    CHECK(a.baseAction == GrindAction::HOLD && a.rated == RatedState::ON_LIKED && a.action == GrindAction::HOLD &&
          !a.refined && a.deltaUm == 0 && fabsf(a.aimTimeS - 27.0f) < 0.001f);
  }

  // The base wins whenever it is asking for a move: ratings never override it, and never delay it.
  {
    GrindShot slow[] = {ratedShot(31.5f, 250, 0), ratedShot(31.2f, 250, 0), ratedShot(31.0f, 250, 0),
                        ratedShot(27.0f, 255, 5), ratedShot(27.4f, 255, 4), ratedShot(26.8f, 255, 5)};
    const GrindAdvice plain = adviseGrind(slow, 6, 250, target, cfg);
    const GrindAdvice a = adviseGrindRated(slow, 6, 250, target, cfg, true);
    CHECK(plain.action == GrindAction::COARSER);
    CHECK(a.rated == RatedState::BASE_MOVING && !a.refined && a.action == plain.action && a.deltaUm == plain.deltaUm &&
          a.suggestedUm == plain.suggestedUm && a.aimTimeS == 0.0f);
  }
  // A liked time outside the tolerance is not used: the range is never widened.
  {
    GrindShot far[] = {ratedShot(28.5f, 250, 0), ratedShot(28.7f, 250, 0), ratedShot(28.3f, 250, 0),
                       ratedShot(24.0f, 270, 5), ratedShot(24.5f, 270, 5), ratedShot(24.2f, 270, 4)};
    const GrindAdvice a = adviseGrindRated(far, 6, 250, target, cfg, true);
    CHECK(a.rated == RatedState::OUTSIDE_BAND && a.action == GrindAction::HOLD && !a.refined && a.aimTimeS == 0.0f);
    // Right on the edge of the tolerance it is still inside.
    GrindShot edge[] = {ratedShot(29.0f, 250, 0), ratedShot(29.2f, 250, 0), ratedShot(28.8f, 250, 0),
                        ratedShot(26.0f, 255, 5), ratedShot(26.0f, 255, 5), ratedShot(26.0f, 255, 5)};
    CHECK(adviseGrindRated(edge, 6, 250, target, cfg, true).rated == RatedState::REFINING);
  }
  // The tighter range follows the liked shots' own scatter, within [0.5 s, the tolerance].
  {
    GrindShot tight[] = {ratedShot(27.0f, 250, 5), ratedShot(27.0f, 250, 5), ratedShot(27.0f, 250, 5)};
    CHECK(likedTime(tight, 3).seS == 0.0f);
    const GrindAdvice t = adviseGrindRated(tight, 3, 250, target, cfg, true);
    CHECK(fabsf(t.ratedBandS - RATED_BAND_FLOOR_S) < 0.001f);                 // floored
    GrindShot loose[] = {ratedShot(26.0f, 250, 5), ratedShot(28.0f, 250, 5), ratedShot(30.0f, 250, 4)};
    const LikedTime l = likedTime(loose, 3);
    CHECK(fabsf(l.seS - 1.2533f * 2.0f / sqrtf(3.0f)) < 0.01f && l.spreadS == 4.0f);
    const GrindAdvice w = adviseGrindRated(loose, 3, 250, target, cfg, true);
    CHECK(w.ratedBandS > 1.4f && w.ratedBandS <= cfg.toleranceS);             // wider, never past the base
    GrindShot wild[] = {ratedShot(20.0f, 250, 5), ratedShot(28.0f, 250, 5), ratedShot(36.0f, 250, 4)};
    CHECK(adviseGrindRated(wild, 3, 250, target, cfg, true).ratedBandS <= cfg.toleranceS);
  }
  // Ratings below the "liked" line do not count, whatever the time.
  {
    GrindShot meh[] = {ratedShot(27.0f, 250, 3), ratedShot(27.0f, 250, 3), ratedShot(27.0f, 250, 3)};
    CHECK(adviseGrindRated(meh, 3, 250, target, cfg, true).rated == RatedState::TOO_FEW);
  }
  // No setting entered: nothing to refine.
  CHECK(adviseGrindRated(refine, 6, 0, target, cfg, true).rated == RatedState::OFF);
  // Ratings never hide a problem the base reports: brew times that vary stay "vary", and no data stays no data.
  {
    GrindShot mixed[] = {ratedShot(22.0f, 250, 5), ratedShot(34.0f, 250, 5), ratedShot(23.0f, 250, 4), ratedShot(33.0f, 250, 4)};
    const GrindAdvice plain = adviseGrind(mixed, 4, 250, target, cfg);
    CHECK(plain.action == GrindAction::INCONSISTENT);
    const GrindAdvice a = adviseGrindRated(mixed, 4, 250, target, cfg, true);
    CHECK(a.action == GrindAction::INCONSISTENT && a.baseAction == GrindAction::INCONSISTENT && !a.refined);
    GrindShot elsewhere[] = {ratedShot(27.0f, 255, 5), ratedShot(27.2f, 255, 5), ratedShot(26.9f, 255, 4)};
    const GrindAdvice none = adviseGrindRated(elsewhere, 3, 250, target, cfg, true);   // nothing at 250 yet
    CHECK(none.action == GrindAction::NO_DATA && none.baseAction == GrindAction::NO_DATA && !none.refined);
  }

  // Taste: only a note, and only when the last shots at the setting agree.
  {
    uint8_t used = 0;
    const uint8_t sour2[] = {1, 1, 0};
    CHECK(grindTasteHint(sour2, 3, used) == -1 && used == 2);
    const uint8_t bitter2[] = {3, 3, 3};
    CHECK(grindTasteHint(bitter2, 3, used) == 1 && used == 3);
    const uint8_t mixed[] = {1, 3, 1};
    CHECK(grindTasteHint(mixed, 3, used) == 0);
    const uint8_t balanced[] = {1, 1, 2};
    CHECK(grindTasteHint(balanced, 3, used) == 0);             // one balanced shot contradicts it
    const uint8_t one[] = {1, 0, 0};
    CHECK(grindTasteHint(one, 3, used) == 0 && used == 1);     // one verdict is not enough
    const uint8_t old[] = {0, 0, 0, 1, 1};
    CHECK(grindTasteHint(old, 5, used) == 0);                  // only the newest three count
    // It does not move the advice: time stays the rule.
    GrindShot sour[] = {ratedShot(28.0f, 250, 0, 1), ratedShot(28.2f, 250, 0, 1), ratedShot(27.8f, 250, 0, 1)};
    const GrindAdvice a = adviseGrindRated(sour, 3, 250, target, cfg, true);
    CHECK(a.action == GrindAction::HOLD && a.tasteHint == -1 && a.tasteShots == 3);
    // A shot at another setting ends the run it was judged on.
    GrindShot moved[] = {ratedShot(28.0f, 248, 0, 1), ratedShot(28.2f, 250, 0, 1), ratedShot(27.8f, 250, 0, 1)};
    CHECK(adviseGrindRated(moved, 3, 248, target, cfg, true).tasteHint == 0);
  }
}

static void testSettingsGrowAtTheEnd() {
  printf("settings: an older, shorter blob keeps the defaults of what was added after it\n");
  // Store::loadBlob copies the stored bytes over a struct already holding the defaults.
  Settings stored;
  stored.shareRatings = true;                     // the last field before the one added later
  stored.grindRatedTarget = false;                // what a newer build wrote
  Settings loaded;                                // defaults
  const size_t olderLength = offsetof(Settings, grindRatedTarget);
  memcpy(&loaded, &stored, olderLength);          // an earlier build wrote only this much
  CHECK(loaded.shareRatings && loaded.grindRatedTarget == Settings{}.grindRatedTarget && loaded.grindRatedTarget);
  CHECK(Settings{}.grindRatedTarget);              // refinement by ratings is on unless the owner turns it off
  // Settings saved by the builds of 16-17 Oct carry two retired bytes at the end; the record still
  // ends with them, so such a blob is not longer than the record and still loads.
  CHECK(offsetof(Settings, retiredLearnGrinderFlag) + sizeof(uint8_t) <= sizeof(Settings));
}

static void testFrozenBeans() {
  printf("frozen beans: the freezer is not ageing\n");
  const uint32_t apr1 = epochOfDate(20260401);
  // Days spent frozen.
  CHECK(frozenDaysAt(20260301, 20260315, apr1) == 14);
  CHECK(frozenDaysAt(20260301, 0, apr1) == 31);                   // still frozen: up to today
  CHECK(frozenDaysAt(20260301, 20260315, epochOfDate(20260310)) == 9);   // asked part-way through
  CHECK(frozenDaysAt(20260301, 20260315, epochOfDate(20260220)) == 0);   // before it went in
  CHECK(frozenDaysAt(0, 0, apr1) == 0);
  CHECK(frozenDaysAt(20260301, 20260315, 0) == 0);                // no clock
  CHECK(frozenDaysAt(20260301, 20260515, apr1) == 31);            // thaw date in the future: frozen today
  CHECK(frozenDaysAt(20260315, 20260301, apr1) == 0);             // thawed before it froze: nonsense
  CHECK(frozenAt(20260301, 20260315, epochOfDate(20260310)));
  CHECK(!frozenAt(20260301, 20260315, apr1));
  CHECK(frozenAt(20260301, 0, apr1));
  CHECK(!frozenAt(20260301, 0, epochOfDate(20260201)));
  CHECK(!frozenAt(0, 0, apr1));

  // Age: calendar days off roast less the days in the freezer.
  CHECK(effectiveAgeDays(20260201, 0, 0, apr1) == 59);                      // no freezer: as before
  CHECK(effectiveAgeDays(20260201, 20260210, 20260310, apr1) == 34);        // 28 frozen days, 25 of them left out
  CHECK(effectiveAgeDays(20260201, 20260210, 0, apr1) == 14);               // still frozen: 59 days, 50 frozen
  CHECK(effectiveAgeDays(20260201, 20260210, 0, epochOfDate(20260301)) == 11);  // it ages, slowly
  CHECK(effectiveAgeDays(20260201, 20260210, 0, apr1) > effectiveAgeDays(20260201, 20260210, 0, epochOfDate(20260301)));
  CHECK(FROZEN_AGING_FRACTION > 0.0f && FROZEN_AGING_FRACTION < 0.34f);       // inside what the research supports
  CHECK(effectiveAgeDays(20260201, 20260210, 20260310, epochOfDate(20260205)) == 4);  // before freezing
  CHECK(effectiveAgeDays(0, 20260210, 20260310, apr1) == -1);               // no roast date, no age
  CHECK(effectiveAgeDays(20260201, 0, 0, 0) == -1);                         // no clock
  CHECK(effectiveAgeDays(20260401, 0, 0, apr1) == 0);
  CHECK(beanAgeDays(20260201, apr1) == 59);                                 // the calendar age is untouched

  // What the ageing allowance makes of it: a bag frozen for the whole gap between two shots has
  // aged only a tenth as much, so it is shifted a tenth as far.
  AgeModel model;
  model.known = true;
  model.umPerDay = -0.8f;
  const int thenAge = effectiveAgeDays(20260201, 20260210, 20260310, epochOfDate(20260210));
  const int nowAge = effectiveAgeDays(20260201, 20260210, 20260310, epochOfDate(20260310));
  CHECK(thenAge == 9 && nowAge == 12);
  CHECK(ageShiftUm(model, thenAge, nowAge) > -3.0f && ageShiftUm(model, thenAge, nowAge) < -2.0f);
  CHECK(ageShiftUm(model, 9, 59) < -30.0f);                                 // unfrozen it would have been

  // What is kept: dates in order, after the roast.
  Coffee c;
  c.roastDate = 20260201;
  c.frozenDate = 20260210;
  c.thawDate = 20260310;
  sanitizeCoffee(c);
  CHECK(c.frozenDate == 20260210 && c.thawDate == 20260310);
  c.frozenDate = 20260110;                                        // before the roast
  sanitizeCoffee(c);
  CHECK(c.frozenDate == 0 && c.thawDate == 0);
  c.frozenDate = 0;
  c.thawDate = 20260310;                                          // out of the freezer, never in
  sanitizeCoffee(c);
  CHECK(c.thawDate == 0);
  c.frozenDate = 20260310;
  c.thawDate = 20260210;                                          // out before in
  sanitizeCoffee(c);
  CHECK(c.frozenDate == 20260310 && c.thawDate == 0);             // treated as still frozen
  c.frozenDate = 20261340;                                        // not a date
  sanitizeCoffee(c);
  CHECK(c.frozenDate == 0);

  // Typing new dates corrects the bag in use (a new bag is startBag: see testBags).
  Library lib;
  Coffee a;
  copyText(a.name, "Idjwi", COFFEE_NAME_LEN);
  a.roastDate = 20260201;
  a.frozenDate = 20260210;
  a.thawDate = 20260310;
  const uint16_t id = saveCoffee(lib, a);
  CHECK(id != 0 && findCoffee(lib, id)->frozenDate == 20260210);
  Coffee again = *findCoffee(lib, id);
  again.frozenDate = 20260325;
  again.thawDate = 20260401;
  CHECK(saveCoffee(lib, again) == id && findCoffee(lib, id)->frozenDate == 20260325 &&
        findCoffee(lib, id)->thawDate == 20260401);
  Coffee moved = *findCoffee(lib, id);
  moved.roastDate = 20260320;                                     // the roast date was mistyped: corrected, dates kept
  CHECK(saveCoffee(lib, moved) == id && findCoffee(lib, id)->roastDate == 20260320 &&
        findCoffee(lib, id)->frozenDate == 20260325);

  // Saved and read back; and a library saved before the two fields existed still reads.
  uint8_t buffer[LIBRARY_MAX_BYTES];
  const size_t written = encodeLibrary(lib, buffer, sizeof(buffer));
  CHECK(written > 0);
  Library back;
  CHECK(decodeLibrary(buffer, written, back));
  CHECK(back.coffees[0].frozenDate == 20260325 && back.coffees[0].thawDate == 20260401);
  {
    // Rebuild the file as an earlier build wrote it: Coffee records without the last 8 bytes.
    const size_t oldCoffee = offsetof(Coffee, frozenDate);          // before the freezer dates and the bag
    LibraryFileHeader h;
    memcpy(&h, buffer, sizeof(h));
    const size_t headers = sizeof(LibraryFileHeader) + sizeof(LibraryFileHeaderV2);
    std::vector<uint8_t> old(buffer, buffer + headers);
    h.coffeeSize = static_cast<uint16_t>(oldCoffee);
    memcpy(old.data(), &h, sizeof(h));
    const uint8_t *at = buffer + headers;
    for (uint8_t i = 0; i < h.coffeeCount; ++i, at += sizeof(Coffee)) old.insert(old.end(), at, at + oldCoffee);
    old.insert(old.end(), at, static_cast<const uint8_t *>(buffer + written));                  // recipes, presets, grinders
    Library read;
    CHECK(decodeLibrary(old.data(), old.size(), read));
    CHECK(read.coffeeCount == 1 && strcmp(read.coffees[0].name, "Idjwi") == 0);
    CHECK(read.coffees[0].roastDate == 20260320 && read.coffees[0].frozenDate == 0 &&
          read.coffees[0].thawDate == 0);                         // the new fields read as "never frozen"
  }

  // Shared records say how many days to leave out, so the library's ageing rate is not fooled.
  Grinder grinder;
  grinder.modelId = 1;
  copyText(grinder.model, "Mahlkonig E64 WS", GRINDER_NAME_LEN);
  Recipe preset;
  preset.doseG = 18.0f;
  preset.targetWeightG = 36.0f;
  preset.targetTimeS = 26.0f;
  GrindAdvice advice;
  advice.action = GrindAction::HOLD;
  advice.currentUm = 246;
  advice.averageTimeS = 25.6f;
  advice.shotsUsed = 3;
  Coffee shared;
  shared.id = 4;
  copyText(shared.name, "Idjwi Island", COFFEE_NAME_LEN);
  shared.roastDate = 20260801;
  shared.frozenDate = 20260901;
  shared.thawDate = 20260920;
  static char body[SHARE_BODY_MAX];
  CHECK(buildDialinRecord(preset, shared, grinder, advice, GrindTimeBasis::TOTAL, 1791672710, body,
                          sizeof(body)) != 0);
  CHECK(strstr(body, "\"roast_date\":\"2026-08-01\",\"frozen_days\":19,\"recipe\""));
  shared.frozenDate = shared.thawDate = 0;
  CHECK(buildDialinRecord(preset, shared, grinder, advice, GrindTimeBasis::TOTAL, 1791672710, body,
                          sizeof(body)) != 0);
  CHECK(!strstr(body, "frozen_days"));                            // never frozen: nothing said
  ShotRecord shot = grindRecord(25.4f, 246, 26.0f);
  shot.epochS = 1791672710;
  shot.roastDate = 20260801;
  shot.finalWeightG = 35.8f;
  shared.frozenDate = 20260901;
  shared.thawDate = 20260920;
  CHECK(buildShotRecord(shot, &shared, grinder, false, body, sizeof(body)) != 0);
  CHECK(strstr(body, "\"frozen_days\":19"));
  shared.roastDate = 20261001;                                    // since moved on to another bag
  CHECK(buildShotRecord(shot, &shared, grinder, false, body, sizeof(body)) != 0);
  CHECK(!strstr(body, "frozen_days"));
}

static Coffee shelfCoffee(uint16_t id, const char *name, uint8_t roast, const char *process,
                          uint16_t altitude, const char *origin) {
  Coffee c;
  c.id = id;
  copyText(c.name, name, COFFEE_NAME_LEN);
  c.roastLevel = roast;
  copyText(c.process, process, PROCESS_LEN);
  c.altitudeM = altitude;
  copyText(c.origin, origin, COFFEE_NAME_LEN);
  return c;
}

static SimilarCandidate cand(const Coffee *c, uint16_t um, float dose = 18.0f, float yield = 36.0f,
                             bool settled = false) {
  SimilarCandidate k;
  k.coffee = c;
  k.grindUm = um;
  k.doseG = dose;
  k.yieldG = yield;
  k.settled = settled;
  return k;
}

static void testSimilarCoffee() {
  printf("similar coffee: a place to start from the owner's own coffees\n");
  CHECK(processClass("Washed") == ProcessClass::WASHED);
  CHECK(processClass("fully washed") == ProcessClass::WASHED);
  CHECK(processClass("Natural") == ProcessClass::NATURAL);
  CHECK(processClass("anaerobic natural") == ProcessClass::NATURAL);
  CHECK(processClass("Red honey") == ProcessClass::HONEY);
  CHECK(processClass("carbonic maceration") == ProcessClass::NATURAL);
  CHECK(processClass("experimental") == ProcessClass::UNKNOWN);
  CHECK(processClass("") == ProcessClass::UNKNOWN);

  Grinder e64;                  // microns: 1 um a unit
  e64.umPerUnit = 1.0f;
  Grinder dial;                 // an unmarked dial
  dial.umPerUnit = 0.0f;
  const Coffee target = shelfCoffee(9, "New", 3, "washed", 1900, "Ethiopia");

  // Nothing to compare with: nothing offered.
  CHECK(!similarStart(target, 18.0f, 36.0f, nullptr, 0, e64).found);

  // ONE coffee dialled: the number is that coffee's, never moved (no figure says how far), with a
  // wide range and the direction as a hint only.
  const Coffee base = shelfCoffee(1, "Yirg medium", 3, "washed", 1900, "Ethiopia");
  SimilarCandidate one[1] = {cand(&base, 250)};
  SimilarStart s = similarStart(target, 18.0f, 36.0f, one, 1, e64);
  CHECK(s.found && s.setting == 250 && s.shift == 0 && !s.fitted && s.applied == 0 && s.samples == 1);
  CHECK(s.fromCoffeeId == 1 && strcmp(s.fromName, "Yirg medium") == 0);
  CHECK(s.range == 25);                                                  // 10% of 250: wide on purpose
  CHECK(s.hintRoast == 0 && s.hintAltitude == 0 && s.hintProcess == 0);  // alike: nothing to hint
  s = similarStart(shelfCoffee(9, "Light", 2, "washed", 1900, "Ethiopia"), 18.0f, 36.0f, one, 1, e64);
  CHECK(s.found && s.setting == 250 && !s.fitted && s.hintRoast == -1);  // lighter: usually finer, but not applied
  s = similarStart(shelfCoffee(9, "Dark", 4, "washed", 1900, "Ethiopia"), 18.0f, 36.0f, one, 1, e64);
  CHECK(s.setting == 250 && s.hintRoast == +1);
  s = similarStart(shelfCoffee(9, "High", 3, "washed", 2400, "Ethiopia"), 18.0f, 36.0f, one, 1, e64);
  CHECK(s.setting == 250 && s.hintAltitude == -1 && s.hintRoast == 0);
  s = similarStart(shelfCoffee(9, "Low", 3, "washed", 1500, "Ethiopia"), 18.0f, 36.0f, one, 1, e64);
  CHECK(s.hintAltitude == +1);
  s = similarStart(shelfCoffee(9, "Close alt", 3, "washed", 2000, "Ethiopia"), 18.0f, 36.0f, one, 1, e64);
  CHECK(s.hintAltitude == 0);                                            // 100 m is not worth a word
  s = similarStart(shelfCoffee(9, "Natural", 3, "natural", 1900, "Ethiopia"), 18.0f, 36.0f, one, 1, e64);
  CHECK(s.setting == 250 && s.hintProcess == +1);

  // Not enough in common to say anything.
  CHECK(!similarStart(shelfCoffee(9, "Blank", 0, "", 0, ""), 18.0f, 36.0f, one, 1, e64).found);
  CHECK(!similarStart(shelfCoffee(9, "Roast only", 3, "", 0, ""), 18.0f, 36.0f, one, 1, e64).found);   // 1 known
  CHECK(similarStart(shelfCoffee(9, "Roast and origin", 3, "", 0, "Ethiopia"), 18.0f, 36.0f, one, 1, e64).found);
  // A coffee is not compared with itself, and a coffee with no setting is no guide.
  CHECK(!similarStart(base, 18.0f, 36.0f, one, 1, e64).found);
  SimilarCandidate unset[1] = {cand(&base, 0)};
  CHECK(!similarStart(target, 18.0f, 36.0f, unset, 1, e64).found);

  // The recipe it was set for has to be close: 18 g -> 36 g does not carry to 22 g -> 44 g.
  CHECK(!similarStart(target, 22.0f, 44.0f, one, 1, e64).found);
  const SimilarStart exact = similarStart(target, 18.0f, 36.0f, one, 1, e64);
  s = similarStart(target, 18.5f, 40.0f, one, 1, e64);
  CHECK(s.found && s.nearRecipe && !exact.nearRecipe && s.range > exact.range);

  // The nearest coffee wins; a settled setting breaks a tie.
  const Coffee mediumDark = shelfCoffee(4, "Medium dark", 4, "washed", 1800, "Colombia");
  SimilarCandidate two[2] = {cand(&base, 250), cand(&mediumDark, 262)};
  s = similarStart(shelfCoffee(9, "Medium dark too", 4, "washed", 1850, "Colombia"), 18.0f, 36.0f, two, 2, e64);
  CHECK(s.found && s.fromCoffeeId == 4 && s.setting == 262 && !s.fitted);     // 2 coffees: too few to fit
  SimilarCandidate tie[2] = {cand(&base, 250), cand(&base, 255, 18.0f, 36.0f, true)};
  s = similarStart(target, 18.0f, 36.0f, tie, 2, e64);
  CHECK(s.fromSettled && s.fromSetting == 255);

  // The range is the scatter of the owner's own settings, not a constant.
  {
    SimilarCandidate tight[3] = {cand(&base, 250), cand(&base, 250), cand(&base, 250)};
    SimilarCandidate loose[3] = {cand(&base, 230), cand(&base, 250), cand(&base, 270)};
    const SimilarStart t = similarStart(target, 18.0f, 36.0f, tight, 3, e64);
    const SimilarStart l = similarStart(target, 18.0f, 36.0f, loose, 3, e64);
    CHECK(t.range == 10 && l.range > 20 && l.range > t.range && t.samples == 3 && l.samples == 3);
  }

  // MEASURED: five of the owner's coffees whose settings rise with roast level, 5% a level
  // (250 at level 3). The new coffee is a level 2: the number is moved finer, by what the data show
  // (a little less than 5%, the ridge pulling it towards "no effect").
  {
    const Coffee c1 = shelfCoffee(11, "L1", 1, "washed", 1900, "Ethiopia");
    const Coffee c3 = shelfCoffee(12, "L3", 3, "washed", 1900, "Ethiopia");
    const Coffee c4 = shelfCoffee(13, "L4", 4, "washed", 1900, "Ethiopia");
    const Coffee c5 = shelfCoffee(14, "L5", 5, "washed", 1900, "Ethiopia");
    SimilarCandidate rising[4] = {cand(&c3, 250), cand(&c1, 226), cand(&c4, 263), cand(&c5, 276)};
    const Coffee l2 = shelfCoffee(9, "New level 2", 2, "washed", 1900, "Ethiopia");
    s = similarStart(l2, 18.0f, 36.0f, rising, 4, e64);
    CHECK(s.found && s.fitted && (s.applied & SIMILAR_ROAST) && s.fromCoffeeId == 12 && s.samples == 4);
    CHECK(s.setting >= 236 && s.setting <= 243 && s.shift < 0);
    // The same coffees, settings falling with roast (the other way round): not applied.
    SimilarCandidate falling[4] = {cand(&c3, 250), cand(&c1, 276), cand(&c4, 238), cand(&c5, 226)};
    s = similarStart(l2, 18.0f, 36.0f, falling, 4, e64);
    CHECK(s.found && !s.fitted && s.applied == 0 && s.setting == 250 && s.hintRoast == -1);
    // Three coffees are not enough to fit one factor with any confidence (needs factors + 2 = 3: just enough).
    SimilarCandidate three[3] = {cand(&c3, 250), cand(&c1, 226), cand(&c5, 276)};
    s = similarStart(l2, 18.0f, 36.0f, three, 3, e64);
    CHECK(s.found && s.samples == 3);
    // Two are not.
    SimilarCandidate pair[2] = {cand(&c3, 250), cand(&c1, 226)};
    s = similarStart(l2, 18.0f, 36.0f, pair, 2, e64);
    CHECK(s.found && !s.fitted && s.setting == 250);
    // A factor that does not vary among the coffees cannot be measured: all one roast level.
    SimilarCandidate flat[4] = {cand(&c3, 250), cand(&c3, 245), cand(&c3, 255), cand(&c3, 252)};
    s = similarStart(l2, 18.0f, 36.0f, flat, 4, e64);
    CHECK(s.found && !s.fitted && s.applied == 0);
  }
  // Altitude measured the same way (higher-grown coffee ground finer, as practitioners say).
  {
    const Coffee a1 = shelfCoffee(21, "A1200", 3, "washed", 1200, "Ethiopia");
    const Coffee a2 = shelfCoffee(22, "A1600", 3, "washed", 1600, "Ethiopia");
    const Coffee a3 = shelfCoffee(23, "A2000", 3, "washed", 2000, "Ethiopia");
    const Coffee a4 = shelfCoffee(24, "A2400", 3, "washed", 2400, "Ethiopia");
    SimilarCandidate fall[4] = {cand(&a2, 250), cand(&a1, 270), cand(&a3, 232), cand(&a4, 216)};
    const Coffee high = shelfCoffee(9, "High", 3, "washed", 2200, "Ethiopia");
    s = similarStart(high, 18.0f, 36.0f, fall, 4, e64);
    CHECK(s.found && s.fitted && (s.applied & SIMILAR_ALTITUDE) && s.setting < 250 && s.shift < 0);
  }
  // Roast and altitude both varying: the fit may use both; it must not crash or go out of range.
  {
    const Coffee m1 = shelfCoffee(31, "m1", 2, "washed", 1500, "Kenya");
    const Coffee m2 = shelfCoffee(32, "m2", 3, "washed", 1800, "Kenya");
    const Coffee m3 = shelfCoffee(33, "m3", 4, "washed", 1600, "Kenya");
    const Coffee m4 = shelfCoffee(34, "m4", 3, "washed", 2100, "Kenya");
    const Coffee m5 = shelfCoffee(35, "m5", 5, "washed", 1400, "Kenya");
    SimilarCandidate mix[5] = {cand(&m2, 250), cand(&m1, 238), cand(&m3, 268), cand(&m4, 241), cand(&m5, 285)};
    s = similarStart(shelfCoffee(9, "Mix", 3, "washed", 1900, "Kenya"), 18.0f, 36.0f, mix, 5, e64);
    CHECK(s.found && s.setting >= 150 && s.setting <= 350 && s.range >= 10 && s.samples >= 3);
  }

  // Units: an unmarked dial moves in whole units; a grinder marked in microns never closer than 5.
  const Coffee dialCoffee = shelfCoffee(1, "Dial coffee", 3, "washed", 1900, "Ethiopia");
  SimilarCandidate onDial[1] = {cand(&dialCoffee, 12)};
  s = similarStart(target, 18.0f, 36.0f, onDial, 1, dial);
  CHECK(s.found && s.setting == 12 && s.range >= 2);                      // 10% of 12 is 1.2: two units at least
  SimilarCandidate small[1] = {cand(&dialCoffee, 3)};
  s = similarStart(target, 18.0f, 36.0f, small, 1, e64);
  CHECK(s.found && s.range >= 10);                                         // 2 x 5 microns
}

static void testBags() {
  printf("bags: a coffee keeps every bag it has had\n");
  Library lib;
  // A coffee saved with dates gets a bag made from them; the coffee mirrors it.
  Coffee a;
  copyText(a.name, "Idjwi", COFFEE_NAME_LEN);
  a.roastDate = 20260201;
  a.frozenDate = 20260210;
  a.thawDate = 20260310;
  const uint16_t id = saveCoffee(lib, a);
  CHECK(id != 0 && lib.bagCount == 1 && findCoffee(lib, id)->bagId == lib.bags[0].id);
  CHECK(lib.bags[0].coffeeId == id && lib.bags[0].roastDate == 20260201 && lib.bags[0].frozenDate == 20260210 &&
        lib.bags[0].thawDate == 20260310);
  const uint16_t first = lib.bags[0].id;
  // A coffee with no dates has no bag.
  Coffee none;
  copyText(none.name, "Plain", COFFEE_NAME_LEN);
  const uint16_t plain = saveCoffee(lib, none);
  CHECK(plain != 0 && findCoffee(lib, plain)->bagId == 0 && lib.bagCount == 1);

  // Editing the dates corrects the bag in use: same bag, same id.
  Coffee fix = *findCoffee(lib, id);
  fix.roastDate = 20260205;
  CHECK(saveCoffee(lib, fix) == id && lib.bagCount == 1 && findCoffee(lib, id)->bagId == first &&
        lib.bags[0].roastDate == 20260205 && lib.bags[0].frozenDate == 20260210);
  fix.roastDate = 20260320;                                 // after it went in the freezer: not a bag that was frozen
  CHECK(saveCoffee(lib, fix) == id && findCoffee(lib, id)->frozenDate == 0 && lib.bags[0].frozenDate == 0 &&
        lib.bags[0].thawDate == 0);
  fix.roastDate = 20260201;
  fix.frozenDate = 20260210;
  fix.thawDate = 20260310;
  CHECK(saveCoffee(lib, fix) == id && lib.bags[0].frozenDate == 20260210 && lib.bags[0].thawDate == 20260310);

  // A new bag: the old one stays with its own dates; the coffee now mirrors a fresh one, not frozen.
  const uint16_t second = startBag(lib, id, 20260401, 20260403, 250);
  CHECK(second != 0 && second != first && lib.bagCount == 2);
  CHECK(findCoffee(lib, id)->bagId == second && findCoffee(lib, id)->roastDate == 20260401 &&
        findCoffee(lib, id)->frozenDate == 0 && findCoffee(lib, id)->thawDate == 0);
  CHECK(findBag(lib, first)->roastDate == 20260201 && findBag(lib, first)->frozenDate == 20260210);
  CHECK(findBag(lib, second)->openedDate == 20260403 && findBag(lib, second)->weightG == 250);
  // The preset sees the bag.
  lib.presetCount = 1;
  lib.presets[0].coffeeId = id;
  CHECK(resolvePreset(lib, 0).bagId == second && resolvePreset(lib, 0).roastDate == 20260401);
  // Editing the coffee now corrects the NEW bag and leaves the old one alone.
  Coffee again = *findCoffee(lib, id);
  again.frozenDate = 20260410;
  CHECK(saveCoffee(lib, again) == id && findBag(lib, second)->frozenDate == 20260410 &&
        findBag(lib, first)->frozenDate == 20260210);
  // Putting an earlier bag back in use.
  CHECK(useBag(lib, id, first) && findCoffee(lib, id)->bagId == first && findCoffee(lib, id)->roastDate == 20260201 &&
        findCoffee(lib, id)->thawDate == 20260310);
  CHECK(!useBag(lib, plain, first));                        // another coffee's bag is not its
  CHECK(!useBag(lib, id, 999));
  // A bag in use cannot be removed; one that is not, can.
  CHECK(!removeBag(lib, first) && removeBag(lib, second) && lib.bagCount == 1 && !findBag(lib, second));
  CHECK(!startBag(lib, 999, 20260401));                     // no such coffee
  // Editing a bag directly keeps the coffee's mirror in step, and keeps the dates in order.
  CHECK(editBag(lib, first, 20260201, 20260205, 20260215, 20260301, 340));
  CHECK(findCoffee(lib, id)->frozenDate == 20260215 && findBag(lib, first)->weightG == 340);
  CHECK(editBag(lib, first, 20260201, 0, 20260110, 0, 0) && findBag(lib, first)->frozenDate == 0);   // frozen before roast
  CHECK(!editBag(lib, 999, 0, 0, 0, 0, 0));
  // A shot belongs to a bag by id; one from before bags by the roast date it copied.
  CHECK(sameBag(5, 20260201, 5, 20260301));                  // same id wins over differing dates
  CHECK(!sameBag(5, 20260201, 6, 20260201));
  CHECK(sameBag(0, 20260201, 6, 20260201));                  // an old shot: by date
  CHECK(!sameBag(0, 20260201, 6, 20260301));
  CHECK(sameBag(5, 20260201, 0, 20260201));
  // Deleting a coffee takes its bags (a coffee a preset uses cannot be deleted).
  CHECK(!removeCoffee(lib, id));
  lib.presets[0].coffeeId = 0;
  CHECK(removeCoffee(lib, id) && lib.bagCount == 0);

  // The list does not overflow: the oldest bag nobody has in use goes first.
  Library big;
  Coffee c;
  copyText(c.name, "Many", COFFEE_NAME_LEN);
  c.roastDate = 20260101;
  const uint16_t many = saveCoffee(big, c);
  uint16_t firstOfMany = big.bags[0].id;
  for (int i = 0; i < 60; ++i) startBag(big, many, 20260101 + (i % 28));
  CHECK(big.bagCount == MAX_BAGS && !findBag(big, firstOfMany) && findBag(big, findCoffee(big, many)->bagId));
  // Every coffee keeps its own in use: a full list of in-use bags says no instead of dropping one.
  Library crowded;
  for (size_t i = 0; i < MAX_COFFEES; ++i) {
    Coffee x;
    snprintf(x.name, sizeof(x.name), "C%u", static_cast<unsigned>(i));
    x.roastDate = 20260101;
    CHECK(saveCoffee(crowded, x) != 0);
  }
  CHECK(crowded.bagCount == MAX_COFFEES);

  // Saved and read back, version 3; a library from before bags is given them from its coffees.
  Library src;
  Coffee s1;
  copyText(s1.name, "Saved", COFFEE_NAME_LEN);
  s1.roastDate = 20260301;
  s1.frozenDate = 20260310;
  const uint16_t sid = saveCoffee(src, s1);
  const uint16_t sBag2 = startBag(src, sid, 20260501);
  uint8_t buffer[LIBRARY_MAX_BYTES];
  const size_t written = encodeLibrary(src, buffer, sizeof(buffer));
  CHECK(written > 0);
  Library back;
  CHECK(decodeLibrary(buffer, written, back));
  CHECK(back.bagCount == 2 && back.nextBagId == src.nextBagId && findCoffee(back, sid)->bagId == sBag2 &&
        findBag(back, sBag2)->roastDate == 20260501 && findBag(back, back.bags[0].id)->frozenDate == 20260310);
  {
    // Version 2 on disk: no bag section, the version number 2.
    LibraryFileHeader h;
    memcpy(&h, buffer, sizeof(h));
    h.version = LIBRARY_VERSION_V2;
    std::vector<uint8_t> v2(buffer, buffer + written - sizeof(LibraryFileHeaderV3) - 2 * sizeof(Bag));
    memcpy(v2.data(), &h, sizeof(h));
    Library old;
    CHECK(decodeLibrary(v2.data(), v2.size(), old));
    CHECK(old.bagCount == 1 && findCoffee(old, sid)->bagId == old.bags[0].id && old.bags[0].roastDate == 20260501);
    // The same file claiming version 3 but cut off before the bags reads the same way.
    h.version = LIBRARY_VERSION;
    memcpy(v2.data(), &h, sizeof(h));
    Library cut;
    CHECK(decodeLibrary(v2.data(), v2.size(), cut) && cut.bagCount == 1);
    // A bag section that claims more bags than there are bytes for is refused.
    std::vector<uint8_t> lie(buffer, buffer + written - sizeof(Bag));
    Library refused;
    CHECK(!decodeLibrary(lie.data(), lie.size(), refused));
  }

  // History: a version 9 shot reads as version 10 with no bag and no TDS.
  legacy::ShotRecordV9 old9 = {};
  old9.id = 77;
  old9.epochS = 1791672710;
  old9.finalWeightG = 36.4f;
  old9.rating = 4;
  old9.coffeeId = 3;
  old9.roastDate = 20260301;
  const ShotRecord up = shotFromV9(old9);
  CHECK(up.id == 77 && up.finalWeightG == 36.4f && up.rating == 4 && up.coffeeId == 3 && up.roastDate == 20260301 &&
        up.bagId == 0 && up.tdsX100 == 0);
}

static void testBagFinished() {
  printf("bag finished: hidden entries, next by roast age, a new bag restores\n");
  static Library lib;
  setDefaultLibrary(lib);                                          // 0 Double, 1 Single, no coffee
  const uint16_t dbl = lib.presets[0].recipeId, sgl = lib.presets[1].recipeId;
  auto coffee = [](const char *name, uint32_t roast) {
    Coffee c;
    copyText(c.name, name, COFFEE_NAME_LEN);
    c.roastDate = roast;
    return c;
  };
  const uint16_t oldId = saveCoffee(lib, coffee("Old", 20260901));
  const uint16_t newId = saveCoffee(lib, coffee("New", 20261001));
  const uint16_t undatedId = saveCoffee(lib, coffee("Undated", 0));
  const uint16_t sameId = saveCoffee(lib, coffee("Same day", 20260901));
  CHECK(oldId && newId && undatedId && sameId);
  CHECK(findCoffee(lib, oldId)->finishedDate == 0);               // a new coffee is on the shelf
  const int pOld = addPreset(lib, dbl, oldId), pNew = addPreset(lib, dbl, newId);
  const int pUndated = addPreset(lib, dbl, undatedId), pSame = addPreset(lib, sgl, sameId);
  const int pOld2 = addPreset(lib, sgl, oldId);
  CHECK(pOld == 2 && pNew == 3 && pUndated == 4 && pSame == 5 && pOld2 == 6 && lib.presetCount == 7);
  for (uint8_t i = 0; i < lib.presetCount; ++i) CHECK(presetVisible(lib, i));
  CHECK(!presetVisible(lib, 7) && !coffeeFinished(lib, oldId) && !coffeeFinished(lib, 0));

  // Oldest roast first; the same roast date keeps list order; undated and no-coffee entries after.
  CHECK(nextPresetByRoastAge(lib) == pOld);
  CHECK(nextPresetByRoastAge(lib, pOld) == pSame);
  lib.presets[pOld].grindUm = 250;                                  // what was learned on it
  lib.presets[pOld].stopOffsetG = 2.1f;
  lib.presets[pOld].learnedShots = 7;
  lib.presets[pOld].learnedAlpha = 40;
  const Preset learned = lib.presets[pOld];

  // Finished: both its entries are hidden, the rest stay, and the active entry moves on by roast age.
  lib.activePreset = static_cast<uint8_t>(pOld);
  CHECK(finishCoffee(lib, oldId, 20261020));
  CHECK(findCoffee(lib, oldId)->finishedDate == 20261020 && coffeeFinished(lib, oldId));
  CHECK(!presetVisible(lib, pOld) && !presetVisible(lib, pOld2));
  CHECK(presetVisible(lib, 0) && presetVisible(lib, 1) && presetVisible(lib, pNew) && presetVisible(lib, pSame));
  CHECK(lib.activePreset == pSame);                                 // the same roast date, next in the list
  CHECK(nextShownPreset(lib, pSame) == 0 && nextShownPreset(lib, 1) == pNew);  // the list skips hidden ones
  CHECK(finishCoffee(lib, oldId, 20261101) && findCoffee(lib, oldId)->finishedDate == 20261020);  // keeps its day
  CHECK(!finishCoffee(lib, 999, 20261020) && !finishCoffee(lib, 0, 20261020));
  CHECK(lib.presets[pOld].grindUm == 250 && lib.presets[pOld].learnedShots == 7);  // nothing learned is lost

  // The coffee edited on the form stays finished: only finishCoffee and a new bag say.
  Coffee edit = *findCoffee(lib, oldId);
  edit.finishedDate = 0;
  copyText(edit.notes, "chocolate", NOTES_LEN);
  CHECK(saveCoffee(lib, edit) == oldId && findCoffee(lib, oldId)->finishedDate == 20261020);

  // Finishing the coffee of an entry that is not active leaves the active one alone.
  CHECK(finishCoffee(lib, newId, 20261020) && lib.activePreset == pSame);
  // The active one finished with only undated and coffee-less entries left: the first in the list.
  CHECK(finishCoffee(lib, sameId, 0) && findCoffee(lib, sameId)->finishedDate == FINISHED_DATE_UNKNOWN);
  CHECK(lib.activePreset == 0);
  // A hidden active entry from anywhere (a file, the web page) is moved by sanitizeLibrary.
  lib.activePreset = static_cast<uint8_t>(pNew);
  sanitizeLibrary(lib);
  CHECK(lib.activePreset == 0);
  CHECK(dateFromEpoch(0) == 0 && dateFromEpoch(1792540799) == 20261020 && dateFromEpoch(1835438400) == 20280229);

  // Saved and read back finished; and a file saved before the field reads every coffee as on the shelf.
  static uint8_t buffer[LIBRARY_MAX_BYTES];
  const size_t written = encodeLibrary(lib, buffer, sizeof(buffer));
  CHECK(written > 0);
  static Library back;
  CHECK(decodeLibrary(buffer, written, back));
  CHECK(findCoffee(back, oldId)->finishedDate == 20261020 && !presetVisible(back, pOld) &&
        presetVisible(back, pUndated) && back.activePreset == 0);
  {
    const size_t oldCoffee = offsetof(Coffee, finishedDate);
    LibraryFileHeader h;
    memcpy(&h, buffer, sizeof(h));
    const size_t headers = sizeof(LibraryFileHeader) + sizeof(LibraryFileHeaderV2);
    std::vector<uint8_t> old(buffer, buffer + headers);
    h.coffeeSize = static_cast<uint16_t>(oldCoffee);
    memcpy(old.data(), &h, sizeof(h));
    const uint8_t *at = buffer + headers;
    for (uint8_t i = 0; i < h.coffeeCount; ++i, at += sizeof(Coffee)) old.insert(old.end(), at, at + oldCoffee);
    old.insert(old.end(), at, static_cast<const uint8_t *>(buffer + written));  // recipes, presets, grinders, bags
    static Library read;
    CHECK(decodeLibrary(old.data(), old.size(), read));
    CHECK(read.coffeeCount == 4 && strcmp(findCoffee(read, oldId)->notes, "chocolate") == 0 &&
          findCoffee(read, oldId)->roastDate == 20260901);
    for (uint8_t i = 0; i < read.coffeeCount; ++i) CHECK(read.coffees[i].finishedDate == 0);
    for (uint8_t i = 0; i < read.presetCount; ++i) CHECK(presetVisible(read, i));
  }

  // A new bag puts the coffee back, its entries exactly as they were.
  CHECK(startBag(lib, oldId, 20261015) != 0);
  CHECK(findCoffee(lib, oldId)->finishedDate == 0 && presetVisible(lib, pOld) && presetVisible(lib, pOld2));
  CHECK(memcmp(&lib.presets[pOld], &learned, sizeof(Preset)) == 0);
  CHECK(nextPresetByRoastAge(lib) == pOld);                          // the new bag, and New is finished
  // So does putting one of its earlier bags back in use.
  const uint16_t sameBag = findCoffee(lib, sameId)->bagId;
  CHECK(sameBag != 0 && useBag(lib, sameId, sameBag) && presetVisible(lib, pSame));

  // Every entry hidden: the active one stays (Start always needs something), and nothing else is next.
  static Library one;
  setDefaultLibrary(one);
  const uint16_t only = saveCoffee(one, coffee("Only", 20260901));
  CHECK(addPreset(one, one.presets[0].recipeId, only) == 2 && removePreset(one, 0) && removePreset(one, 0));
  CHECK(one.presetCount == 1 && one.activePreset == 0);
  CHECK(finishCoffee(one, only, 20261020) && one.activePreset == 0 && !presetVisible(one, 0));
  CHECK(nextPresetByRoastAge(one) == -1 && nextShownPreset(one, 0) == 0);
  // A damaged date is not a finish.
  one.coffees[0].finishedDate = 20261399;
  sanitizeLibrary(one);
  CHECK(one.coffees[0].finishedDate == 0 && presetVisible(one, 0));
}

static void testUpkeepDefaults() {
  printf("upkeep defaults: backflush 200, descale off, grinder clean 500, once\n");
  Maintenance fresh;                                                // a new controller
  sanitizeMaintenance(fresh);
  CHECK(fresh.everyShots[0] == 200 && fresh.everyShots[1] == 0 && fresh.everyShots[2] == 500 &&
        fresh.defaultsApplied == 1);
  // A blob saved before the field: the owner had set descale only. The others get their starting
  // interval, the owner's descale stays. The store zeroes a blob before copying the record in.
  Maintenance before;
  before.shotsTotal = 321;
  before.everyShots[1] = 300;
  uint8_t blob[sizeof(Maintenance)];
  memset(blob, 0, sizeof(blob));
  memcpy(blob, &before, offsetof(Maintenance, defaultsApplied));
  Maintenance loaded;
  memcpy(&loaded, blob, sizeof(blob));
  CHECK(loaded.defaultsApplied == 0);
  sanitizeMaintenance(loaded);
  CHECK(loaded.shotsTotal == 321 && loaded.everyShots[0] == 200 && loaded.everyShots[1] == 300 &&
        loaded.everyShots[2] == 500 && loaded.defaultsApplied == 1);
  // After that a 0 is the owner's "never", and stays never through a save and a load.
  setUpkeepEvery(loaded, Upkeep::BACKFLUSH, 0);
  setUpkeepEvery(loaded, Upkeep::GRINDER_CLEAN, 0);
  Maintenance again;
  memcpy(&again, &loaded, sizeof(Maintenance));
  sanitizeMaintenance(again);
  CHECK(again.everyShots[0] == 0 && again.everyShots[1] == 300 && again.everyShots[2] == 0);
  CHECK(!upkeepDue(again, Upkeep::BACKFLUSH));
}

static void testUpkeep() {
  printf("upkeep counters\n");
  Maintenance m;
  CHECK(m.shotsTotal == 0 && upkeepDueCount(m) == 0);
  for (int i = 0; i < 5; ++i) countUpkeepShot(m);
  CHECK(shotsSince(m, Upkeep::BACKFLUSH) == 5 && !upkeepDue(m, Upkeep::BACKFLUSH));    // no interval, no reminder
  setUpkeepEvery(m, Upkeep::BACKFLUSH, 5);
  CHECK(upkeepDue(m, Upkeep::BACKFLUSH) && upkeepDueCount(m) == 1);
  markUpkeepDone(m, Upkeep::BACKFLUSH, 1791672710);
  CHECK(shotsSince(m, Upkeep::BACKFLUSH) == 0 && !upkeepDue(m, Upkeep::BACKFLUSH) && m.doneAtEpochS[0] == 1791672710);
  for (int i = 0; i < 4; ++i) countUpkeepShot(m);
  CHECK(!upkeepDue(m, Upkeep::BACKFLUSH));
  countUpkeepShot(m);
  CHECK(upkeepDue(m, Upkeep::BACKFLUSH) && shotsSince(m, Upkeep::DESCALE) == 10);       // the others count from the start
  setUpkeepEvery(m, Upkeep::DESCALE, 99999);
  CHECK(m.everyShots[1] == UPKEEP_MAX_EVERY);
  m.doneAtShots[2] = 9999;                                                              // nonsense from a damaged blob
  m.everyShots[0] = 60000;
  sanitizeMaintenance(m);
  CHECK(m.doneAtShots[2] == m.shotsTotal && m.everyShots[0] == UPKEEP_MAX_EVERY);
  Upkeep job;
  CHECK(upkeepFromName("descale", job) && job == Upkeep::DESCALE);
  CHECK(upkeepFromName("grinder_clean", job) && job == Upkeep::GRINDER_CLEAN);
  CHECK(!upkeepFromName("descal", job) && !upkeepFromName("descaler", job) && !upkeepFromName("", job) &&
        !upkeepFromName(nullptr, job));
  // A rinse or a flush is not a shot.
  ShotRecord shot;
  shot.shotTimeS = 27.0f;
  shot.finalWeightG = 36.0f;
  CHECK(upkeepCountsShot(shot));
  shot.shotTimeS = 6.0f;
  CHECK(!upkeepCountsShot(shot));
  shot.shotTimeS = 27.0f;
  shot.finalWeightG = 1.0f;
  CHECK(!upkeepCountsShot(shot));
}

static void testShotContext() {
  printf("shot context\n");
  ShotRecord shot;
  shot.id = 77;
  shot.epochS = 1789992000;  // 2026-09-21 12:00 UTC
  ShotContextInputs in;
  in.shot = &shot;
  in.previousEpochS = shot.epochS - 95;
  in.roastDate = 20260901;
  in.frozenDate = 20260905;
  in.thawDate = 20260915;    // ten days in the freezer
  in.grinderCleanShots = 41;
  in.advice.action = GrindAction::FINER;
  in.advice.baseAction = GrindAction::HOLD;
  in.advice.suggestedUm = 66;
  in.advice.deltaUm = -3;
  in.advice.umPerS = 2.75f;
  in.advice.shotsUsed = 2;
  in.advice.learned = true;
  in.advice.learnShots = 5;
  in.advice.refined = true;
  in.toleranceS = 2.0f;
  in.window = 3;
  in.maxStepUm = 20;
  in.defaultUmPerS = 3.0f;
  in.ratedTarget = true;
  // A 10 Hz curve in 0.1 g steps.
  static CurvePoint curve[300];
  for (int i = 0; i < 300; ++i) {
    curve[i] = packPoint(i * 0.1f, 0.1f * (i / 3), CURVE_NO_TIMER, 0);
  }
  in.curve = curve;
  in.curvePoints = 300;
  ShotContext c = buildShotContext(in);
  CHECK(c.shotId == 77 && c.engineVersion == SHOT_ENGINE_VERSION);
  CHECK(c.calendarDays == 20 && c.frozenDays == 10);
  CHECK(c.ageDays == 11);                                        // 20 days less 9 of the 10 frozen ones
  CHECK((c.beanFlags & CTX_BEAN_FROZEN) == 0);                   // thawed on the 15th
  CHECK(c.sinceLastShotS == 95 && c.grinderCleanShots == 41);
  CHECK(c.adviceUm == 66 && c.adviceDeltaUm == -3 && c.adviceUmPerSx100 == 275);
  CHECK(c.adviceAction == static_cast<uint8_t>(GrindAction::FINER));
  CHECK(c.adviceBaseAction == static_cast<uint8_t>(GrindAction::HOLD));
  CHECK(c.adviceShots == 2 && c.adviceLearnShots == 5 && c.adviceFlags == 5);   // learned + refined
  CHECK(c.sampleIntervalMs >= 95 && c.sampleIntervalMs <= 105);
  CHECK(c.weightStepCg == 10 && c.curvePoints == 300);           // 0.1 g steps
  CHECK(c.toleranceDs == 20 && c.window == 3 && c.maxStepUm == 20 && c.defaultUmPerSx100 == 300 &&
        c.ratedTarget == 1);
  CHECK(c.doseActualDg == 0 && c.prepFlags == 0 && c.machineOnS == 0);
  // The owner's entries afterwards: dose weighed, purged, uneven. Other flags are left alone.
  c.prepFlags = CTX_PREP_WDT;
  CHECK(applyShotPrep(c, 182, true, false));
  CHECK(c.doseActualDg == 182 && (c.prepFlags & CTX_PREP_PURGED) && !(c.prepFlags & CTX_PREP_UNEVEN) &&
        (c.prepFlags & CTX_PREP_WDT));
  CHECK(applyShotPrep(c, 0, false, true));
  CHECK(c.doseActualDg == 0 && !(c.prepFlags & CTX_PREP_PURGED) && (c.prepFlags & CTX_PREP_UNEVEN));
  CHECK(!applyShotPrep(c, 30, true, true) && !applyShotPrep(c, 401, true, true));   // 3 g, 40.1 g: typing errors
  CHECK(c.doseActualDg == 0 && (c.prepFlags & CTX_PREP_UNEVEN));                    // unchanged by a refusal

  // Still frozen at the shot: the flag, and the frozen days up to that day.
  shot.epochS = 1789128000;  // 2026-09-11
  in.thawDate = 0;
  c = buildShotContext(in);
  CHECK((c.beanFlags & CTX_BEAN_FROZEN) != 0 && c.frozenDays == 6 && c.calendarDays == 10 && c.ageDays == 5);
  // Nothing known: no clock, no roast date, no curve.
  ShotRecord bare;
  bare.id = 5;
  ShotContextInputs none;
  none.shot = &bare;
  c = buildShotContext(none);
  CHECK(c.ageDays == -1 && c.calendarDays == -1 && c.sinceLastShotS == 0 && c.sampleIntervalMs == 0 &&
        c.weightStepCg == 0 && c.startSetting == 0 && c.similarSetting == 0);
  CHECK(buildShotContext(ShotContextInputs()).shotId == 0);
  // A community start and an own-coffee start are both kept.
  ShotContextInputs offered;
  offered.shot = &bare;
  offered.start.basis = StartBasis::FITTED;
  offered.start.setting = 71;
  offered.start.range = 9;
  offered.similar.found = true;
  offered.similar.setting = 64;
  offered.similar.range = 12;
  offered.similar.fitted = true;
  offered.similar.samples = 3;
  c = buildShotContext(offered);
  CHECK(c.startSetting == 71 && c.startRange == 9 && c.startBasis == static_cast<uint8_t>(StartBasis::FITTED));
  CHECK(c.similarSetting == 64 && c.similarRange == 12 && c.similarFitted == 1 && c.similarSamples == 3);
  // Curve quality on a coarse, slow scale: 0.5 s apart, whole-gram steps.
  static CurvePoint slow[40];
  for (int i = 0; i < 40; ++i) slow[i] = packPoint(i * 0.5f, 1.0f * (i / 2), CURVE_NO_TIMER, 0);
  uint16_t interval;
  uint8_t step;
  curveQuality(slow, 40, interval, step);
  CHECK(interval >= 495 && interval <= 510 && step == 100);
  curveQuality(slow, 2, interval, step);
  CHECK(interval == 0 && step == 0);                             // too short to say
  printf("  ShotContext is %u bytes\n", static_cast<unsigned>(sizeof(ShotContext)));
}

static void testLearnGrinder() {
  printf("grind advice: learn the grinder\n");
  const GrindConfig base;
  GrinderElasticity none;
  // The plain rule (a default-constructed config) is the original Grind-by-Sync advice, kept for
  // comparison: one shot 5 s fast, 3 um per second, 15 um finer, direction only.
  {
    GrindShot one[1];
    one[0].timeS = 23.0f;
    one[0].grindUm = 70;
    const GrindAdvice a = adviseGrind(one, 1, 70, 28.0f, base);
    CHECK(a.action == GrindAction::FINER && a.deltaUm == -15 && !adviceHasNumber(a) &&
          a.source == SensitivitySource::GUESS);
  }
  // Units: microns per step known -> converted; not known -> relative to the setting.
  {
    const GrindConfig atom = grindConfigForGrinder(base, 5.0f, 14, 28.0f, none);
    CHECK_NEAR(atom.defaultUmPerS, 0.6f, 1e-4);
    CHECK(atom.maxStepUm == 4 && atom.minLearnSpan == 1.0f && atom.bracket && atom.priorCv > 0.0f);
    CHECK_NEAR(atom.minUmPerS, 0.1f, 1e-4);
    CHECK_NEAR(atom.maxUmPerS, 4.0f, 1e-4);
    const GrindConfig e64 = grindConfigForGrinder(base, 1.0f, 70, 28.0f, none);
    CHECK(e64.defaultUmPerS == base.defaultUmPerS && e64.maxStepUm == base.maxStepUm);   // microns: same numbers
    const GrindConfig dial = grindConfigForGrinder(base, 0.0f, 15, 28.0f, none);
    CHECK_NEAR(dial.defaultUmPerS, 15.0f / (0.9f * 28.0f), 1e-4);
    CHECK(dial.maxStepUm == 2 && dial.minLearnSpan == 1.0f);
    CHECK(dial.minUmPerS < 0.595f && dial.maxUmPerS > 0.595f);
  }
  // Pooled elasticity: three coffees at different settings on one power law (eps 1.2).
  {
    GrinderShot s[12];
    const uint16_t settings[3] = {60, 75, 220};
    size_t n = 0;
    for (int g = 0; g < 3; ++g) {
      for (int k = 0; k < 4; ++k) {
        const float x = settings[g] * (1.0f + 0.05f * (k - 1.5f));
        s[n].setting = static_cast<uint16_t>(lroundf(x));
        s[n].timeS = 28.0f * powf(s[n].setting / static_cast<float>(settings[g]), -1.2f) * (1.0f + 0.02f * g);
        s[n].group = static_cast<uint16_t>(100 + g);
        ++n;
      }
    }
    const GrinderElasticity el = pooledGrindElasticity(s, n);
    CHECK(el.known && el.groups == 3 && el.shots == 12);
    CHECK_NEAR(el.eps, 1.2f, 0.02f);
    // A new coffee on this grinder starts from it, and can show a number at its first correction.
    const GrindConfig c = grindConfigForGrinder(base, 1.0f, 70, 28.0f, el);
    CHECK_NEAR(c.defaultUmPerS, 70.0f / (el.eps * 28.0f), 1e-3);
    CHECK(c.guessSource == static_cast<uint8_t>(SensitivitySource::GRINDER));
    GrindShot one[1];
    one[0].timeS = 23.0f;
    one[0].grindUm = 70;
    const GrindAdvice a = adviseGrind(one, 1, 70, 28.0f, c);
    CHECK(a.action == GrindAction::FINER && adviceHasNumber(a) && a.source == SensitivitySource::GRINDER);
    // All at one setting per coffee: nothing to learn.
    for (size_t i = 0; i < n; ++i) s[i].setting = settings[i / 4];
    CHECK(!pooledGrindElasticity(s, n).known);
    // Time rising with a coarser setting is not a grinder: rejected.
    for (size_t i = 0; i < n; ++i) {
      s[i].setting = static_cast<uint16_t>(settings[i / 4] + (i % 4) * 4);
      s[i].timeS = 25.0f + (i % 4);
    }
    CHECK(!pooledGrindElasticity(s, n).known);
    CHECK(!pooledGrindElasticity(nullptr, 0).known);
  }
  // Weighing: three shots a few steps apart barely move the guess; a wide, long series takes over.
  {
    GrindConfig c = base;
    c.priorCv = GRIND_PRIOR_CV;
    GrindShot noisy[3] = {};
    noisy[0].grindUm = 70; noisy[0].timeS = 26.0f;
    noisy[1].grindUm = 72; noisy[1].timeS = 25.9f;   // noise hides most of the response
    noisy[2].grindUm = 74; noisy[2].timeS = 25.5f;
    float sens = 0.0f, w = 0.0f;
    const bool ok = weighGrindSensitivity(noisy, 3, c, sens, w);
    CHECK(ok && w < 0.5f && sens > 2.0f && sens < 6.0f);
    float legacy = 0.0f;
    CHECK(learnGrindSensitivity(noisy, 3, legacy) && legacy > 7.0f);   // the old rule believed a 3x too big figure
    GrindShot wide[10] = {};
    for (int i = 0; i < 10; ++i) {
      wide[i].grindUm = static_cast<uint16_t>(60 + 3 * i);
      wide[i].timeS = 28.0f - (wide[i].grindUm - 70) / 6.0f + ((i % 2) ? 0.4f : -0.4f);   // true 6 um/s
    }
    CHECK(weighGrindSensitivity(wide, 10, c, sens, w) && w > 0.85f);
    CHECK_NEAR(sens, 6.0f, 0.6f);
    GrindShot single[1] = {};
    single[0].grindUm = 70;
    single[0].timeS = 25.0f;
    CHECK(!weighGrindSensitivity(single, 1, c, sens, w) && w == 0.0f);
  }
  // Not stepping past a setting seen on the other side of the target.
  {
    GrindConfig c = grindConfigForGrinder(base, 1.0f, 66, 28.0f, none);
    GrindShot h[2] = {};
    h[0].grindUm = 66; h[0].timeS = 23.0f;   // now: 5 s fast
    h[1].grindUm = 60; h[1].timeS = 34.0f;   // before: 6 s slow
    GrindAdvice a = adviseGrind(h, 2, 66, 28.0f, c);
    CHECK(a.action == GrindAction::FINER && a.bracketed && !a.betweenSteps);
    CHECK(a.suggestedUm == 63);              // where the target lies between them, not past 60
    GrindConfig plain = base;
    a = adviseGrind(h, 2, 66, 28.0f, plain);
    CHECK(a.suggestedUm == 51 && !a.bracketed);   // as before: straight past the slow setting
    // A dial whose one step moves more than the tolerance: the closer of the two is kept.
    c = grindConfigForGrinder(base, 0.0f, 9, 28.0f, none);
    h[0].grindUm = 9; h[0].timeS = 24.0f;    // 4 s fast
    h[1].grindUm = 8; h[1].timeS = 33.0f;    // 5 s slow
    a = adviseGrind(h, 2, 9, 28.0f, c);
    CHECK(a.action == GrindAction::HOLD && a.betweenSteps && a.suggestedUm == 9);
    h[0].timeS = 22.0f;                      // 6 s fast: the slow one was closer
    a = adviseGrind(h, 2, 9, 28.0f, c);
    CHECK(a.action == GrindAction::FINER && a.betweenSteps && a.suggestedUm == 8);
    // Ratings do not try to aim between two steps.
    GrindShot r[5] = {};
    for (int i = 0; i < 5; ++i) { r[i].grindUm = 9; r[i].timeS = 24.0f; r[i].rating = 5; }
    r[3].grindUm = 8; r[3].timeS = 33.0f; r[3].rating = 0;
    r[4].grindUm = 8; r[4].timeS = 33.0f; r[4].rating = 0;
    GrindConfig wide = c;
    wide.toleranceS = 2.0f;
    a = adviseGrindRated(r, 5, 9, 28.0f, wide, true);
    CHECK(a.betweenSteps && a.rated == RatedState::OUTSIDE_BAND);   // liked time 24 s is outside +-2 s anyway
  }  // A shot too long or too short to count still gives the direction, by the largest step.
  {
    const GrindConfig c = grindConfigForGrinder(base, 1.0f, 60, 28.0f, none);
    GrindAdvice a = adviseGrind(nullptr, 0, 60, 28.0f, c);
    CHECK(a.action == GrindAction::NO_DATA);
    adviseOutOfRange(a, GrindUse::TOO_LONG, 60, c);
    CHECK(a.action == GrindAction::COARSER && a.suggestedUm == 80 && a.outOfRange && adviceHasNumber(a));
    a = adviseGrind(nullptr, 0, 60, 28.0f, c);
    adviseOutOfRange(a, GrindUse::TOO_SHORT, 60, c);
    CHECK(a.action == GrindAction::FINER && a.suggestedUm == 40);
    a = adviseGrind(nullptr, 0, 60, 28.0f, c);
    adviseOutOfRange(a, GrindUse::NOT_WEIGHT_STOP, 60, c);   // an interfered shot says nothing
    CHECK(a.action == GrindAction::NO_DATA && !a.outOfRange);
    GrindShot one[1] = {};
    one[0].grindUm = 60;
    one[0].timeS = 27.0f;
    a = adviseGrind(one, 1, 60, 28.0f, c);
    adviseOutOfRange(a, GrindUse::TOO_LONG, 60, c);           // a counted shot at this setting: its advice stands
    CHECK(a.action == GrindAction::HOLD && !a.outOfRange);
  }
}

static void testExtraction() {
  printf("extraction yield from a measured TDS\n");
  ShotRecord s;
  s.doseG = 18.0f;
  s.finalWeightG = 36.0f;
  CHECK(shotExtractionPct(s) == 0.0f);                     // nothing measured, nothing said
  s.tdsX100 = 900;                                          // 9.00 % TDS
  CHECK(fabsf(shotExtractionPct(s) - 18.0f) < 0.001f);      // 9.00 x 36 / 18
  s.finalWeightG = 40.0f;
  CHECK(fabsf(shotExtractionPct(s) - 20.0f) < 0.001f);
  s.doseG = 0.0f;
  CHECK(shotExtractionPct(s) == 0.0f);                      // no dose, no yield
  s.doseG = 18.0f;
  s.finalWeightG = 0.0f;
  CHECK(shotExtractionPct(s) == 0.0f);
  // The record that came from history version 9 has none.
  legacy::ShotRecordV9 old9 = {};
  old9.doseG = 18.0f;
  old9.finalWeightG = 36.0f;
  CHECK(shotExtractionPct(shotFromV9(old9)) == 0.0f);
}

static void testGrinderUnitTidy() {
  printf("grinder unit: microns typed any way read as um\n");
  for (const char *typed : {"um", "1um", "1 um", "UM", "\xC2\xB5m", "1\xC2\xB5m", "\xCE\xBCm", "microns"}) {
    Grinder g;
    copyText(g.unit, typed, GRINDER_UNIT_LEN);
    sanitizeGrinder(g);
    CHECK(strcmp(g.unit, "um") == 0);
  }
  for (const char *kept : {"steps", "dial", "clicks", "12", "1 step"}) {
    Grinder g;
    copyText(g.unit, kept, GRINDER_UNIT_LEN);
    sanitizeGrinder(g);
    CHECK(strcmp(g.unit, kept) == 0);
  }
  // Saving "1um" over a grinder stored as "um" is not a change of unit: settings are kept.
  Library lib;
  Grinder g;
  copyText(g.model, "Flat 64", GRINDER_NAME_LEN);
  setGrinder(lib, g, false);
  lib.presetCount = 1;
  lib.presets[0].grindUm = 85;
  const uint16_t calibration = lib.grinder.calibration;
  copyText(g.unit, "1um", GRINDER_UNIT_LEN);
  setGrinder(lib, g, false);
  CHECK(strcmp(lib.grinder.unit, "um") == 0);
  CHECK(lib.presets[0].grindUm == 85 && lib.grinder.calibration == calibration);
}

static void testSettingFormat() {
  printf("settings: whole steps, fractions, two rings\n");
  char out[24];
  uint16_t v = 0;
  Grinder plain;
  CHECK(formatSetting(246, plain, out, sizeof(out)) == 3 && strcmp(out, "246") == 0);
  CHECK(formatSetting(0, plain, out, sizeof(out)) == 0 && out[0] == '\0');
  CHECK(parseSetting("246", plain, v) && v == 246);
  CHECK(parseSetting(" 12 ", plain, v) && v == 12);
  CHECK(parseSetting("12.4", plain, v) && v == 12);          // a whole-step dial rounds
  CHECK(!parseSetting("", plain, v) && !parseSetting("abc", plain, v) && !parseSetting("0", plain, v) &&
        !parseSetting("2001", plain, v) && !parseSetting("-3", plain, v) && !parseSetting("12x", plain, v));

  Grinder tenths;                       // a dial read in tenths: 3.5 is stored 35
  tenths.divisor = 10;
  CHECK(formatSetting(35, tenths, out, sizeof(out)) && strcmp(out, "3.5") == 0);
  CHECK(formatSetting(40, tenths, out, sizeof(out)) && strcmp(out, "4.0") == 0);
  CHECK(formatSetting(5, tenths, out, sizeof(out)) && strcmp(out, "0.5") == 0);
  CHECK(parseSetting("3.5", tenths, v) && v == 35);
  CHECK(parseSetting("3,5", tenths, v) && v == 35);          // a decimal comma
  CHECK(parseSetting("4", tenths, v) && v == 40);
  CHECK(parseSetting("3.54", tenths, v) && v == 35);         // to the nearest step it has
  CHECK(parseSetting("3.56", tenths, v) && v == 36);
  Grinder quarters;
  quarters.divisor = 4;
  CHECK(formatSetting(7, quarters, out, sizeof(out)) && strcmp(out, "1.75") == 0);
  CHECK(formatSetting(8, quarters, out, sizeof(out)) && strcmp(out, "2.00") == 0);
  CHECK(parseSetting("1.75", quarters, v) && v == 7);
  CHECK(parseSetting("1.8", quarters, v) && v == 7);         // 1.8 is nearer 1.75 than 2.0
  Grinder halves;
  halves.divisor = 2;
  CHECK(formatSetting(3, halves, out, sizeof(out)) && strcmp(out, "1.5") == 0);
  CHECK(parseSetting("1.5", halves, v) && v == 3);
  CHECK(parseSetting("1999", halves, v) == false);           // 1999 x 2 is over the largest setting
  CHECK(parseSetting("1000", halves, v) && v == 2000);

  Grinder rings;                        // outer 1..31, 9 inner steps each (a Baratza Sette's shape)
  rings.subSteps = 9;
  CHECK(formatSetting(1, rings, out, sizeof(out)) && strcmp(out, "1:1") == 0);
  CHECK(formatSetting(9, rings, out, sizeof(out)) && strcmp(out, "1:9") == 0);
  CHECK(formatSetting(10, rings, out, sizeof(out)) && strcmp(out, "2:1") == 0);
  CHECK(formatSetting(11 * 9 + 3 - 9 + 0, rings, out, sizeof(out)) && strcmp(out, "11:3") == 0);
  CHECK(parseSetting("2:1", rings, v) && v == 10);
  CHECK(parseSetting("12-3", rings, v) && v == 11 * 9 + 3);
  CHECK(parseSetting("12/3", rings, v) && v == 11 * 9 + 3);
  CHECK(parseSetting("7", rings, v) && v == 6 * 9 + 1);      // an outer position alone: its first inner step
  CHECK(!parseSetting("12:0", rings, v) && !parseSetting("12:10", rings, v) && !parseSetting("0:3", rings, v) &&
        !parseSetting("12:x", rings, v));
  CHECK(!parseSetting("300:9", rings, v));                   // past SETTING_MAX
  // The round trip is exact for every setting a ringed dial can have.
  bool roundTrip = true;
  for (uint16_t s = 1; s <= SETTING_MAX; ++s) {
    uint16_t back = 0;
    formatSetting(s, rings, out, sizeof(out));
    roundTrip = roundTrip && parseSetting(out, rings, back) && back == s;
    formatSetting(s, tenths, out, sizeof(out));
    roundTrip = roundTrip && parseSetting(out, tenths, back) && back == s;
    formatSetting(s, quarters, out, sizeof(out));
    roundTrip = roundTrip && parseSetting(out, quarters, back) && back == s;
    formatSetting(s, plain, out, sizeof(out));
    roundTrip = roundTrip && parseSetting(out, plain, back) && back == s;
  }
  CHECK(roundTrip);

  // Changes of setting, and what to call them.
  CHECK(formatSettingDelta(-5, plain, out, sizeof(out)) && strcmp(out, "5") == 0);
  CHECK(formatSettingDelta(-15, tenths, out, sizeof(out)) && strcmp(out, "1.5") == 0);
  CHECK(formatSettingDelta(3, quarters, out, sizeof(out)) && strcmp(out, "0.75") == 0);
  CHECK(formatSettingDelta(4, rings, out, sizeof(out)) && strcmp(out, "4") == 0);
  CHECK(strcmp(settingDeltaUnit(rings), "steps") == 0 && strcmp(settingDeltaUnit(plain), "um") == 0);

  // A grinder's way of reading is kept in order.
  Grinder odd;
  odd.divisor = 3;
  sanitizeSettingFormat(odd);
  CHECK(odd.divisor == 1);                                   // 3 does not divide 100
  odd.subSteps = 1;
  sanitizeSettingFormat(odd);
  CHECK(odd.subSteps == 0);                                  // one inner step is no ring
  odd.subSteps = 120;
  sanitizeSettingFormat(odd);
  CHECK(odd.subSteps == 0);
  odd.subSteps = 9;
  odd.divisor = 10;
  sanitizeSettingFormat(odd);
  CHECK(odd.subSteps == 9 && odd.divisor == 1);              // rings and a divisor do not mix
  for (uint8_t d : {1, 2, 4, 5, 10, 20, 25, 50, 100}) CHECK(validDivisor(d));
  for (uint8_t d : {0, 3, 6, 7, 8, 9, 11, 40, 99, 101}) CHECK(!validDivisor(d));

  // Changing how a grinder is read starts a new calibration and clears the settings entered.
  Library lib;
  lib.grinder.id = 1;
  copyText(lib.grinder.model, "Home Built", GRINDER_NAME_LEN);
  copyText(lib.grinder.unit, "dial", GRINDER_UNIT_LEN);
  lib.grinder.calibration = 1;
  lib.nextCalibration = 2;
  lib.presets[0].grindUm = 35;
  lib.presetCount = 1;
  Grinder next = lib.grinder;
  next.divisor = 10;                                         // now read in tenths: 35 would mean 3.5, not 35
  setGrinder(lib, next, false);
  CHECK(lib.grinder.divisor == 10 && lib.grinder.calibration == 2 && lib.presets[0].grindUm == 0);
  next = lib.grinder;
  next.umPerUnit = 2.0f;                                     // a microns figure alone does not
  const uint16_t cal = lib.grinder.calibration;
  lib.presets[0].grindUm = 12;
  setGrinder(lib, next, false);
  CHECK(lib.grinder.calibration == cal && lib.presets[0].grindUm == 12);
  // The grinder is described to the library with its way of reading.
  static char body[SHARE_BODY_MAX];
  Grinder shared = lib.grinder;
  shared.subSteps = 9;
  shared.divisor = 1;
  Recipe preset;
  GrindAdvice advice;
  advice.currentUm = 40;
  advice.averageTimeS = 26.0f;
  advice.shotsUsed = 3;
  Coffee coffee;
  coffee.id = 1;
  copyText(coffee.name, "X", COFFEE_NAME_LEN);
  CHECK(buildDialinRecord(preset, coffee, shared, advice, GrindTimeBasis::TOTAL, 1791672710, body, sizeof(body)) != 0);
  CHECK(strstr(body, "\"sub_steps\":9") && !strstr(body, "\"divisor\""));
  shared.subSteps = 0;
  shared.divisor = 10;
  CHECK(buildDialinRecord(preset, coffee, shared, advice, GrindTimeBasis::TOTAL, 1791672710, body, sizeof(body)) != 0);
  CHECK(strstr(body, "\"divisor\":10") && !strstr(body, "sub_steps"));
  shared.divisor = 1;
  CHECK(buildDialinRecord(preset, coffee, shared, advice, GrindTimeBasis::TOTAL, 1791672710, body, sizeof(body)) != 0);
  CHECK(!strstr(body, "divisor") && !strstr(body, "sub_steps"));       // whole numbers: nothing extra said
}

static void testUnopenedBags() {
  printf("bags: unopened bags wait on the shelf until put in use\n");
  Library lib;
  lib.recipeCount = 1;
  lib.recipes[0] = RecipeDef{};
  lib.recipes[0].id = 7;
  // Coffee A in use (roasted 1 Oct), coffee B on the list too (roasted 5 Oct).
  Coffee a;
  copyText(a.name, "A", COFFEE_NAME_LEN);
  a.roastDate = 20261001;
  const uint16_t ida = saveCoffee(lib, a);
  Coffee b;
  copyText(b.name, "B", COFFEE_NAME_LEN);
  b.roastDate = 20261005;
  const uint16_t idb = saveCoffee(lib, b);
  lib.presetCount = 2;
  lib.presets[0] = Preset{};
  lib.presets[0].recipeId = 7;
  lib.presets[0].coffeeId = ida;
  lib.presets[1] = Preset{};
  lib.presets[1].recipeId = 7;
  lib.presets[1].coffeeId = idb;
  lib.activePreset = 0;
  const uint16_t bagA = findCoffee(lib, ida)->bagId;

  // A re-order of A arrives: unopened, A keeps its bag in use, nothing about A changes.
  const uint16_t a2 = addUnopenedBag(lib, ida, 20261015, 340, 3);
  CHECK(a2 != 0 && findBag(lib, a2)->unopened == 1 && findBag(lib, a2)->subscriptionId == 3);
  CHECK(findCoffee(lib, ida)->bagId == bagA && findCoffee(lib, ida)->roastDate == 20261001);
  CHECK(findBag(lib, a2)->openedDate == 0);
  CHECK(!addUnopenedBag(lib, 999, 20261015, 0));
  // Choosing A again does not open it: A has a bag in use.
  CHECK(!openActiveCoffeeBag(lib, 20261020));
  // BAG FINISHED on A: the waiting bag of A goes into use from today; A stays on the shelf.
  CHECK(finishCoffee(lib, ida, 20261020));
  CHECK(findCoffee(lib, ida)->bagId == a2 && findCoffee(lib, ida)->finishedDate == 0);
  CHECK(findBag(lib, a2)->unopened == 0 && findBag(lib, a2)->openedDate == 20261020);
  CHECK(findCoffee(lib, ida)->roastDate == 20261015 && lib.activePreset == 0);

  // A new coffee C arrives unopened (roasted 3 Oct, before B): it has no bag in use and no entry yet.
  Coffee c;
  copyText(c.name, "C", COFFEE_NAME_LEN);
  const uint16_t idc = saveCoffee(lib, c);
  const uint16_t c1 = addUnopenedBag(lib, idc, 20261003, 250, 4);
  CHECK(findCoffee(lib, idc)->bagId == 0 && findCoffee(lib, idc)->roastDate == 0);
  CHECK(shelfRoastDate(lib, *findCoffee(lib, idc)) == 20261003);
  // A finished with nothing of its own waiting: the next bag by roast date is C's (3 Oct, before B's
  // 5 Oct). C gets an entry with the same recipe, chosen, and its bag opened today.
  CHECK(finishCoffee(lib, ida, 20261101));
  CHECK(lib.presetCount == 3 && lib.presets[2].coffeeId == idc && lib.presets[2].recipeId == 7);
  CHECK(lib.activePreset == 2 && findCoffee(lib, idc)->bagId == c1 && findBag(lib, c1)->unopened == 0);
  CHECK(findBag(lib, c1)->openedDate == 20261101 && findCoffee(lib, idc)->roastDate == 20261003);
  CHECK(coffeeFinished(lib, ida) && !presetVisible(lib, 0));

  // An unopened bag of B, roasted after B's bag in use: finishing C moves on to B's bag in use (5 Oct),
  // not to the newer unopened one.
  const uint16_t b2 = addUnopenedBag(lib, idb, 20261025, 0);
  const uint16_t bagB = findCoffee(lib, idb)->bagId;
  CHECK(finishCoffee(lib, idc, 20261110));
  CHECK(lib.activePreset == 1 && findCoffee(lib, idb)->bagId == bagB && findBag(lib, b2)->unopened == 1);
  // Then B finished: its own waiting bag.
  CHECK(finishCoffee(lib, idb, 20261120) && findCoffee(lib, idb)->bagId == b2 && findBag(lib, b2)->unopened == 0);

  // A finished coffee with an unopened bag added later is back on the shelf; choosing it opens the bag.
  const uint16_t a3 = addUnopenedBag(lib, ida, 20261118, 0);
  CHECK(presetVisible(lib, 0) && !coffeeFinished(lib, ida));
  lib.activePreset = 0;
  CHECK(openActiveCoffeeBag(lib, 20261121) && findCoffee(lib, ida)->bagId == a3 &&
        findCoffee(lib, ida)->finishedDate == 0 && findBag(lib, a3)->openedDate == 20261121);
  // Opening by hand.
  const uint16_t a4 = addUnopenedBag(lib, ida, 20261120, 0);
  CHECK(openBag(lib, a4, 20261122) && findCoffee(lib, ida)->bagId == a4 && !openBag(lib, a4, 20261122));

  // With no room on the list, the other coffee's bag waits: nothing is added, it stays unopened.
  Library full;
  full.recipeCount = 1;
  full.recipes[0] = RecipeDef{};
  full.recipes[0].id = 1;
  Coffee f;
  copyText(f.name, "F", COFFEE_NAME_LEN);
  f.roastDate = 20261001;
  const uint16_t idf = saveCoffee(full, f);
  Coffee g;
  copyText(g.name, "G", COFFEE_NAME_LEN);
  const uint16_t idg = saveCoffee(full, g);
  const uint16_t g1 = addUnopenedBag(full, idg, 20261002, 0);
  full.presetCount = MAX_PRESETS;
  for (size_t i = 0; i < MAX_PRESETS; ++i) {
    full.presets[i] = Preset{};
    full.presets[i].recipeId = 1;
    full.presets[i].coffeeId = idf;
  }
  CHECK(finishCoffee(full, idf, 20261020) && full.presetCount == MAX_PRESETS && findBag(full, g1)->unopened == 1);

  // Unopened bags are never dropped to make room.
  Library many;
  Coffee m;
  copyText(m.name, "M", COFFEE_NAME_LEN);
  m.roastDate = 20260101;
  const uint16_t idm = saveCoffee(many, m);
  const uint16_t keep = addUnopenedBag(many, idm, 20260102, 0);
  for (int i = 0; i < 60; ++i) startBag(many, idm, 20260101 + (i % 28));
  CHECK(findBag(many, keep) && findBag(many, keep)->unopened == 1);

  // Saved and read back with the new fields; a bag record from before them reads them as 0 even
  // when its padding held something.
  uint8_t buffer[LIBRARY_MAX_BYTES];
  size_t written = encodeLibrary(lib, buffer, sizeof(buffer));
  Library back;
  CHECK(written > 0 && decodeLibrary(buffer, written, back));
  const uint16_t u = addUnopenedBag(lib, idb, 20261201, 0, 9);
  written = encodeLibrary(lib, buffer, sizeof(buffer));
  CHECK(decodeLibrary(buffer, written, back) && findBag(back, u) && findBag(back, u)->unopened == 1 &&
        findBag(back, u)->subscriptionId == 9);
  // The same library written with 24-byte bag records (as before), padding set to 0xFF.
  {
    std::vector<uint8_t> old(buffer, buffer + written);
    // Find the bag header: bagSize equals sizeof(Bag) followed by the count.
    size_t at = 0;
    for (size_t i = 0; i + 8 <= written; ++i) {
      uint16_t size, count;
      memcpy(&size, &buffer[i], 2);
      memcpy(&count, &buffer[i + 2], 2);
      if (size == sizeof(Bag) && count == lib.bagCount && i + 8 + count * sizeof(Bag) == written) at = i;
    }
    CHECK(at != 0);
    std::vector<uint8_t> made(buffer, buffer + at);
    uint8_t h3[8];
    memcpy(h3, &buffer[at], 8);
    const uint16_t oldSize = 24;
    memcpy(h3, &oldSize, 2);
    made.insert(made.end(), h3, h3 + 8);
    for (uint8_t i = 0; i < lib.bagCount; ++i) {
      uint8_t rec[24];
      memcpy(rec, &lib.bags[i], 22);
      rec[22] = rec[23] = 0xFF;
      made.insert(made.end(), rec, rec + 24);
    }
    Library fromOld;
    CHECK(decodeLibrary(made.data(), made.size(), fromOld) && fromOld.bagCount == lib.bagCount);
    bool none = true;
    for (uint8_t i = 0; i < fromOld.bagCount; ++i) none = none && fromOld.bags[i].unopened == 0 && fromOld.bags[i].subscriptionId == 0;
    CHECK(none);
  }
}

static void testOnTheWayBags() {
  printf("bags: a bag on its way is not on the shelf until it comes\n");
  Library lib;
  lib.recipeCount = 1;
  lib.recipes[0] = RecipeDef{};
  lib.recipes[0].id = 1;
  Coffee a;
  copyText(a.name, "A", COFFEE_NAME_LEN);
  a.roastDate = 20261001;
  const uint16_t ida = saveCoffee(lib, a);
  Coffee club;
  copyText(club.name, "Club pick", COFFEE_NAME_LEN);
  const uint16_t idc = saveCoffee(lib, club);
  lib.presetCount = 2;
  lib.presets[0] = Preset{};
  lib.presets[0].recipeId = 1;
  lib.presets[0].coffeeId = ida;
  lib.presets[1] = Preset{};
  lib.presets[1].recipeId = 1;
  lib.presets[1].coffeeId = idc;
  lib.activePreset = 0;
  // A re-order of A on its way, and a new coffee on its way.
  const uint16_t a2 = addOnTheWayBag(lib, ida, 20261116, 340, 2);
  const uint16_t c1 = addOnTheWayBag(lib, idc, 20261340, 0, 2);   // not a date: not known
  CHECK(a2 && c1 && findBag(lib, a2)->unopened == BAG_ON_ITS_WAY && findBag(lib, a2)->expectedDate == 20261116);
  CHECK(findBag(lib, c1)->expectedDate == 0 && findBag(lib, a2)->subscriptionId == 2);
  CHECK(!addOnTheWayBag(lib, 999, 20261116, 0));
  // Not on the shelf: never the next bag, never opened by hand or by choosing the coffee.
  CHECK(!oldestUnopenedBag(lib, 0) && !openBag(lib, a2, 20261101));
  lib.activePreset = 1;
  CHECK(!openActiveCoffeeBag(lib, 20261101) && findCoffee(lib, idc)->bagId == 0);
  lib.activePreset = 0;
  // BAG FINISHED on A with only a bag on its way: A is finished, the bag stays on its way.
  CHECK(finishCoffee(lib, ida, 20261105) && coffeeFinished(lib, ida) && findBag(lib, a2)->unopened == BAG_ON_ITS_WAY);
  // It came: unopened on the shelf, with its roast date; then it can be opened.
  CHECK(bagArrived(lib, a2, 20261110, 0) && findBag(lib, a2)->unopened == BAG_UNOPENED &&
        findBag(lib, a2)->roastDate == 20261110 && findBag(lib, a2)->weightG == 340 && findBag(lib, a2)->expectedDate == 0);
  CHECK(!bagArrived(lib, a2, 0, 0) && !coffeeFinished(lib, ida));
  CHECK(openBag(lib, a2, 20261112) && findCoffee(lib, ida)->bagId == a2);
  // Never dropped to make room.
  Library many;
  Coffee m;
  copyText(m.name, "M", COFFEE_NAME_LEN);
  m.roastDate = 20260101;
  const uint16_t idm = saveCoffee(many, m);
  const uint16_t keep = addOnTheWayBag(many, idm, 20260201, 0);
  for (int i = 0; i < 60; ++i) startBag(many, idm, 20260101 + (i % 28));
  CHECK(findBag(many, keep) && findBag(many, keep)->unopened == BAG_ON_ITS_WAY);
  // Saved and read back; a 28-byte record from before reads expectedDate as 0.
  uint8_t buffer[LIBRARY_MAX_BYTES];
  const size_t written = encodeLibrary(lib, buffer, sizeof(buffer));
  Library back;
  CHECK(written > 0 && decodeLibrary(buffer, written, back) && findBag(back, c1)->unopened == BAG_ON_ITS_WAY);
  {
    size_t at = 0;
    for (size_t i = 0; i + 8 <= written; ++i) {
      uint16_t size, count;
      memcpy(&size, &buffer[i], 2);
      memcpy(&count, &buffer[i + 2], 2);
      if (size == sizeof(Bag) && count == lib.bagCount && i + 8 + count * sizeof(Bag) == written) at = i;
    }
    CHECK(at != 0);
    std::vector<uint8_t> made(buffer, buffer + at);
    uint8_t h3[8];
    memcpy(h3, &buffer[at], 8);
    const uint16_t oldSize = 28;
    memcpy(h3, &oldSize, 2);
    made.insert(made.end(), h3, h3 + 8);
    for (uint8_t i = 0; i < lib.bagCount; ++i) {
      uint8_t rec[28];
      memcpy(rec, &lib.bags[i], 28);
      made.insert(made.end(), rec, rec + 28);
    }
    Library old;
    CHECK(decodeLibrary(made.data(), made.size(), old));
    bool zero = true;
    for (uint8_t i = 0; i < old.bagCount; ++i) zero = zero && old.bags[i].expectedDate == 0;
    CHECK(zero && findBag(old, c1)->unopened == BAG_ON_ITS_WAY);
  }
}

static void testSubscriptions() {
  printf("subscriptions: dates, cost, stock, page preview\n");
  CHECK(daysBetween(20261022, 20261119) == 28 && addDays(20261231, 1) == 20270101 && addDays(20240228, 1) == 20240229);
  CHECK(validSubDate(20261022) && !validSubDate(20261131) && !validSubDate(0));
  Subscription s;
  s.everyDays = 28;
  s.nextDate = 20261020;
  // Arrived two days late: the next is one period on from when it was due.
  CHECK(nextDateAfter(s, 20261022) == 20261117);
  // Long overdue: moved on until it is in the future.
  s.nextDate = 20260801;
  CHECK(nextDateAfter(s, 20261022) == 20261024);
  // Not regular: the date stays.
  Subscription once;
  once.nextDate = 20261101;
  CHECK(nextDateAfter(once, 20261022) == 20261101);
  // No date known: one period from today.
  Subscription fresh;
  fresh.everyDays = 14;
  CHECK(nextDateAfter(fresh, 20261022) == 20261105);

  // Supplies: a delivery adds its count, each shot takes its share, never below 0.
  Subscription filters;
  filters.kind = SubKind::SUPPLIES;
  filters.everyDays = 60;
  filters.nextDate = 20261201;
  filters.perShot = 1;
  filters.perDelivery = 100;
  filters.stock = 2;
  CHECK(subscriptionShotTaken(filters) && filters.stock == 1);
  CHECK(subscriptionShotTaken(filters) && filters.stock == 0 && !subscriptionShotTaken(filters));
  subscriptionArrived(filters, 20261201);
  CHECK(filters.stock == 100 && filters.nextDate == 20270130 && filters.lastArrived == 20261201);
  // Two shots a day: 100 filters last 50 days, past the next delivery in 60? No: short.
  CHECK(stockDaysLeft(filters, 2.0f) == 50 && stockShort(filters, 2.0f, 20261201));
  CHECK(!stockShort(filters, 1.0f, 20261201) && stockDaysLeft(filters, 0.0f) == -1);
  Subscription coffee;
  CHECK(!subscriptionShotTaken(coffee) && stockDaysLeft(coffee, 2.0f) == -1);

  // Monthly cost: active ones only, scaled to 30.44 days.
  Subscription list[3];
  list[0].everyDays = 28;
  list[0].priceCents = 2200;
  list[1].everyDays = 14;
  list[1].priceCents = 1800;
  list[2].everyDays = 30;
  list[2].priceCents = 5000;
  list[2].status = SubStatus::PAUSED;
  CHECK(monthlyCents(list, 3) == 6305);  // 2391.6 + 3913.4
  // Reminders: active, within 3 days, or overdue.
  list[0].nextDate = 20261025;
  CHECK(subscriptionDueSoon(list[0], 20261022) && !subscriptionDueSoon(list[0], 20261021));
  list[2].nextDate = 20261023;
  CHECK(!subscriptionDueSoon(list[2], 20261022));
  CHECK(daysUntilNext(list[1], 20261022) == INT32_MIN);

  // Shots a day over two weeks.
  const uint32_t now = 1792688400;
  const uint32_t shots[] = {now - 3600, now - 86400 * 2, now - 86400 * 13, now - 86400 * 20};
  CHECK_NEAR(shotsPerDay(shots, 4, now), 3.0f / 14.0f, 0.001);
  // A history that began five days ago: seven shots are seven in five days.
  const uint32_t young[] = {now - 3600, now - 86400, now - 86400 * 2, now - 86400 * 3, now - 86400 * 4,
                            now - 86400 * 4 - 100, now - 86400 * 5};
  CHECK_NEAR(shotsPerDay(young, 7, now, 14, now - 86400 * 5), 7.0f / 5.0f, 0.001);
  CHECK_NEAR(shotsPerDay(young, 1, now, 14, now - 3600), 1.0f, 0.001);            // never less than a day
  CHECK_NEAR(shotsPerDay(shots, 4, now, 14, now - 86400 * 30), 3.0f / 14.0f, 0.001);   // older history: 14 days

  // Links to share: the shop's general pages, never one customer's (made-up ids and tokens).
  char link[200];
  shareableLink("https://shop.example/tools/recurring/portal/0123456789abcdef0123456789ab/subscriptions?token=abcdef0123456789",
                true, "recharge", link, sizeof(link));
  CHECK(strcmp(link, "https://shop.example/tools/recurring/login") == 0);
  shareableLink("https://shop.example/a/account/9f8e7d6c5b4a39281706f5e4?x=1", true, "", link, sizeof(link));
  CHECK(strcmp(link, "https://shop.example/account") == 0);
  shareableLink("https://shopify.com/12345678901/account/orders", true, "", link, sizeof(link));
  CHECK(strcmp(link, "https://shopify.com/12345678901/account/orders") == 0);   // a shop number, not a person
  shareableLink("https://shop.example/?country=US", false, "", link, sizeof(link));
  CHECK(strcmp(link, "https://shop.example/") == 0);
  shareableLink("https://www.shop.example/pages/home#top", false, "", link, sizeof(link));
  CHECK(strcmp(link, "https://www.shop.example/pages/home") == 0);
  shareableLink("https://shop.example", false, "", link, sizeof(link));
  CHECK(strcmp(link, "https://shop.example/") == 0);
  shareableLink("https://shop.example/login/session-token/x", true, "", link, sizeof(link));
  CHECK(strcmp(link, "https://shop.example/account") == 0);
  shareableLink("javascript:alert(1)", true, "", link, sizeof(link));
  CHECK(link[0] == '\0');
  CHECK(linkHasCredential("https://x.example/i.png?token=abc") && linkHasCredential("https://x.example/i.png?X-Amz-Signature=1"));
  CHECK(!linkHasCredential("https://x.example/i.png?v=17&width=1200") && !linkHasCredential("https://x.example/i.png"));

  // Sanitizing: only web links, a real colour, sane numbers.
  Subscription bad;
  subCopy(bad.url, "javascript:alert(1)", sizeof(bad.url));
  subCopy(bad.manageUrl, "https://shop.example/account", sizeof(bad.manageUrl));
  subCopy(bad.color, "red", sizeof(bad.color));
  bad.stock = -5;
  bad.nextDate = 20261340;
  sanitizeSubscription(bad);
  CHECK(bad.url[0] == '\0' && strcmp(bad.manageUrl, "https://shop.example/account") == 0 &&
        bad.color[0] == '\0' && bad.stock == 0 && bad.nextDate == 0);
  CHECK(subKindFrom("supplies") == SubKind::SUPPLIES && subStatusFrom("paused") == SubStatus::PAUSED &&
        strcmp(subStatusName(SubStatus::CANCELLED), "cancelled") == 0);

  // Links resolved against the page.
  char out[256];
  resolveUrl("https://shop.example/products/beans?x=1", "/cdn/logo.png", out, sizeof(out));
  CHECK(strcmp(out, "https://shop.example/cdn/logo.png") == 0);
  resolveUrl("https://shop.example/products/beans", "//cdn.shop.example/i.jpg", out, sizeof(out));
  CHECK(strcmp(out, "https://cdn.shop.example/i.jpg") == 0);
  resolveUrl("https://shop.example/products/beans", "icon.png", out, sizeof(out));
  CHECK(strcmp(out, "https://shop.example/products/icon.png") == 0);
  resolveUrl("https://shop.example", "icon.png", out, sizeof(out));
  CHECK(strcmp(out, "https://shop.example/icon.png") == 0);

  // A Shopify shop on Recharge, written the way such pages are.
  const char *recharge =
      "<!doctype html><html><head><meta charset=\"utf-8\">"
      "<title>Paper Filters &ndash; Example Roasters</title>\n"
      "<meta name=\"theme-color\" content=\"#3a5f4b\">"
      "<meta property=\"og:site_name\" content=\"Example Roasters\">"
      "<meta property=\"og:title\" content=\"Espresso Paper Filters &amp; More\">"
      "<meta property=\"og:description\" content=\"  Paper filters for\n your portafilter. \">"
      "<meta property=\"og:image\" content=\"http://shop.example/cdn/shop/files/filters.jpg?v=1\">"
      "<meta property=\"og:image:secure_url\" content=\"https://shop.example/cdn/shop/files/filters.jpg?v=1\">"
      "<link rel=\"icon\" type=\"image/png\" href=\"//shop.example/cdn/shop/files/fav.png\">"
      "<link rel='apple-touch-icon' href='/apple-icon.png'>"
      "<script src=\"https://static.rechargecdn.com/assets/js/widget.min.js\"></script>"
      "<script>var Shopify = Shopify || {}; Shopify.shop = \"example.myshopify.com\";</script>"
      "</head><body><meta property=\"og:title\" content=\"not this\"></body></html>";
  PageMeta m;
  parsePageHead(recharge, strlen(recharge), "https://shop.example/products/filters", m);
  CHECK(strcmp(m.title, "Espresso Paper Filters & More") == 0);
  CHECK(strcmp(m.site, "Example Roasters") == 0);
  CHECK(strcmp(m.description, "Paper filters for your portafilter.") == 0);
  CHECK(strcmp(m.image, "https://shop.example/cdn/shop/files/filters.jpg?v=1") == 0);
  CHECK(strcmp(m.icon, "https://shop.example/apple-icon.png") == 0);
  CHECK(strcmp(m.color, "#3a5f4b") == 0);
  CHECK(strcmp(m.platform, "recharge") == 0);
  CHECK(strcmp(m.manageUrl, "https://shop.example/tools/recurring/login") == 0);

  // Ordergroove on Shopify; no og:title: the <title> is used; no site name: the host.
  const char *og =
      "<html><HEAD><TITLE>Coffee Subscription | Matched to You</TITLE>"
      "<META NAME=\"description\" CONTENT=\"Roasted to order &#8212; delivered.\">"
      "<link rel=\"shortcut icon\" href=\"/favicon.ico\">"
      "<script src=\"https://static.ordergroove.com/x/main.js\"></script>"
      "<script>Shopify.shop='x';</script></HEAD><body></body></html>";
  parsePageHead(og, strlen(og), "https://www.coffee.example/", m);
  CHECK(strcmp(m.title, "Coffee Subscription | Matched to You") == 0);
  CHECK(strcmp(m.description, "Roasted to order \xE2\x80\x94 delivered.") == 0);
  CHECK(strcmp(m.site, "coffee.example") == 0);
  CHECK(strcmp(m.icon, "https://www.coffee.example/favicon.ico") == 0);
  CHECK(strcmp(m.platform, "ordergroove") == 0 && strcmp(m.manageUrl, "https://www.coffee.example/account") == 0);

  // A page that is not a shop, cut short, or empty: nothing is made up.
  const char *plain = "<html><head><title>Hello</title><meta property=\"og:image\" content=\"javascript:x\">";
  parsePageHead(plain, strlen(plain), "https://plain.example/a", m);
  CHECK(strcmp(m.title, "Hello") == 0 && m.platform[0] == '\0' && m.manageUrl[0] == '\0' && m.image[0] == '\0');
  parsePageHead("", 0, "https://plain.example/a", m);
  CHECK(m.title[0] == '\0' && strcmp(m.site, "plain.example") == 0);
  const char *cut = "<html><head><meta property=\"og:title\" content=\"Never clos";
  parsePageHead(cut, strlen(cut), "https://cut.example/", m);
  CHECK(m.title[0] == '\0');
}

static void testServiceSharing() {
  printf("subscriptions: shared services, reviews and the service score\n");
  Subscription s;
  s.id = 3;
  subCopy(s.name, "Trade Coffee", sizeof(s.name));
  subCopy(s.url, "https://www.drinktrade.com/", sizeof(s.url));
  subCopy(s.manageUrl, "https://www.drinktrade.com/account", sizeof(s.manageUrl));
  subCopy(s.platform, "ordergroove", sizeof(s.platform));
  subCopy(s.shipsTo, " us, ca,xyz,G", sizeof(s.shipsTo));
  subCopy(s.currency, "usd", sizeof(s.currency));
  subCopy(s.description, "Coffee \xE2\x80\x9Cmatched\xE2\x80\x9D to you \xE2\x80\x94 500+ coffees", sizeof(s.description));
  s.cadences[0] = 14;
  s.cadences[1] = 28;
  s.cadences[2] = 999;
  s.priceFromCents = 1500;
  s.styleMask = (1u << 1) | (1u << 3) | (1u << 15);
  s.sourcingMask = (1u << 0) | (1u << 3);
  s.priceCents = 2150;           // the owner's own: never in the record
  s.stock = 5;
  subCopy(s.note, "my secret note", sizeof(s.note));
  sanitizeSubscription(s);
  CHECK(strcmp(s.shipsTo, "US,CA") == 0 && strcmp(s.currency, "USD") == 0 && s.cadences[2] == 0);
  CHECK(s.styleMask == ((1u << 1) | (1u << 3)));
  CHECK(shipsTo(s.shipsTo, "CA") && !shipsTo(s.shipsTo, "GB") && shipsTo("", "GB"));
  Subscription badPlatform = s;
  subCopy(badPlatform.platform, "evil", sizeof(badPlatform.platform));
  sanitizeSubscription(badPlatform);
  CHECK(badPlatform.platform[0] == '\0');

  char out[SHARE_BODY_MAX];
  const size_t n = buildServiceRecord(s, 41, 1792688400, out, sizeof(out));
  CHECK(n > 0);
  const std::string rec(out);
  CHECK(rec.find("\"t\":\"service\"") != std::string::npos && rec.find("\"local_id\":3") != std::string::npos);
  CHECK(rec.find("\"ships_to\":[\"US\",\"CA\"]") != std::string::npos);
  CHECK(rec.find("\"cadences\":[14,28]") != std::string::npos);
  CHECK(rec.find("\"style\":[\"multi_roaster\",\"matched\"]") != std::string::npos);
  CHECK(rec.find("\"sourcing\":[\"fair_trade\",\"farm_named\"]") != std::string::npos);
  CHECK(rec.find("\"price_from_cents\":1500,\"currency\":\"USD\"") != std::string::npos);
  CHECK(rec.find("\"about\":\"Coffee \\\"matched\\\" to you - 500+ coffees\"") != std::string::npos);
  CHECK(rec.find("2150") == std::string::npos && rec.find("secret") == std::string::npos &&
        rec.find("stock") == std::string::npos);
  {
    Subscription mine = s;
    subCopy(mine.url, "https://shop.example/?country=US", sizeof(mine.url));
    subCopy(mine.manageUrl, "https://shop.example/tools/recurring/portal/0123456789abcdef0123456789ab/subscriptions?token=feedface01234567",
            sizeof(mine.manageUrl));
    subCopy(mine.platform, "recharge", sizeof(mine.platform));
    CHECK(buildServiceRecord(mine, 50, 0, out, sizeof(out)) > 0);
    const std::string r2(out);
    CHECK(r2.find("\"url\":\"https://shop.example/\"") != std::string::npos);
    CHECK(r2.find("\"manage_url\":\"https://shop.example/tools/recurring/login\"") != std::string::npos);
    CHECK(r2.find("feedface") == std::string::npos && r2.find("0123456789abcdef0123") == std::string::npos &&
          r2.find("country=") == std::string::npos);
  }

  // Changed since shared: only the shared fields count.
  markServiceQueued(s, 41);
  CHECK(s.shareState == static_cast<uint8_t>(ShareState::QUEUED) && !serviceChangedSinceShared(s));
  s.priceCents = 9999;
  s.nextDate = 20261201;
  CHECK(!serviceChangedSinceShared(s));
  s.sourcingMask |= 1u << 2;
  CHECK(serviceChangedSinceShared(s));
  CHECK(serviceShareState(s, 40) == ShareState::QUEUED && serviceShareState(s, 42) == ShareState::SENT);
  CHECK(applyReviewTo(&s, 41, ShareState::PUBLISHED, "ok", "trade-coffee") && strcmp(s.libraryId, "trade-coffee") == 0);
  CHECK(!applyReviewTo(&s, 40, ShareState::DECLINED, "old", ""));
  Subscription none;
  CHECK(!serviceCanBeShared(none) && serviceCanBeShared(s));

  // A review: stars and the aspects given; the note stays here.
  ServiceReview r;
  r.id = 7;
  r.epochS = 1792688400;
  r.drinkerId = 2;
  r.stars = 9;
  r.aspect[0] = 4;
  r.aspect[2] = 5;
  r.aspect[5] = 8;
  subCopy(r.note, "late twice", sizeof(r.note));
  sanitizeServiceReview(r);
  CHECK(r.stars == 5 && r.aspect[5] == 0);
  CHECK(buildServiceReviewRecord(r, "trade-coffee", out, sizeof(out)) > 0);
  const std::string rv(out);
  CHECK(rv.find("\"t\":\"service_review\"") != std::string::npos && rv.find("\"service\":\"trade-coffee\"") != std::string::npos);
  CHECK(rv.find("\"aspects\":{\"value\":4,\"consistency\":5}") != std::string::npos && rv.find("late") == std::string::npos);
  CHECK(buildServiceReviewRecord(r, "", out, sizeof(out)) == 0);    // only for a service in the library

  // A coffee rating says which service the bag came with.
  CoffeeRating cr;
  cr.id = 1;
  cr.stars = 4;
  Coffee c;
  copyText(c.name, "Alma", COFFEE_NAME_LEN);
  CHECK(buildTasteRecord(cr, c, 20261018, out, sizeof(out), "trade-coffee") > 0 &&
        std::string(out).find("\"service\":\"trade-coffee\"") != std::string::npos);
  CHECK(buildTasteRecord(cr, c, 20261018, out, sizeof(out)) > 0 && std::string(out).find("service") == std::string::npos);

  // The service score: a good taste match leads; values that matter lift; reviews alone still rank.
  TasteMatch good;
  good.score = 0.8f;
  good.confidence = 1.0f;
  TasteMatch poor;
  poor.score = -0.4f;
  poor.confidence = 1.0f;
  CHECK(serviceScore(good, true, 0, 0, 0) > serviceScore(poor, true, 0, 0, 0));
  CHECK(serviceScore(poor, true, 0, 0, 2) > serviceScore(poor, true, 0, 0, 0));
  CHECK_NEAR(serviceScore(good, false, 0, 0, 5) - serviceScore(good, false, 0, 0, 3), 0.0, 1e-6);   // at most three
  CHECK(serviceScore(good, false, 0, 4.5f, 0) > serviceScore(good, false, 0, 3.5f, 0));
  CHECK(serviceScore(good, false, 5.0f, 0, 0) == 0.0f);   // people's stars mean nothing without taste
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);  // keep the output if a test crashes
  testAcaiaProtocol();
  testAcaiaStream();
  testScaleDrivers();
  testScaleLinkKeepsLunarData();
  testScaleLinkService();
  testShotOverEveryScale();
  testTuyaCrypto();
  testPusherLinkKeepsSwitchBot();
  testUpRefusedWhileMoving();
  testPusherDrivers();
  testPusherHold();
  testPowerControl();
  testPowerEvents();
  testPowerOffWithScale();
  testCoffeeTaste();
  testMachineProfiles();
  testShotDataSet();
  testReadingNewerThanTick();
  testResultScreenLetsGo();
  testCommunityModel();
  testPowerBeliefDrift();
  testCupNotTrustedAfterScaleSleep();
  testBuiltinGrinders();
  testDiagnostics();
  testEmptyScaleZeroed();
  testScaleRest();
  testCupRemembered();
  testCupNotBelievedAfterAWhile();
  testCupAutoTareScale();
  testCupPutBackBriefly();
  testSwitchBotProtocol();
  testAlgorithms();
  testSanitize();
  testLibrary();
  testMigration();
  testNormalShotAndLearning();
  testLearningDisabledAndUserStop();
  testCupAfterTare();
  testTimeout();
  testMachineTimeLimit();
  testScaleLost();
  testCupRemoved();
  testStopPressMissed();
  testSlowStopIsNotPressedAgain();
  testStopOffsetWarmUp();
  testStopNeverWorks();
  testStartNotDelivered();
  testLateFlowIsAdopted();
  testBotDropsAtStop();
  testUnconfirmedStop();
  testPreconditions();
  testBotOnlyConnectedAroundShot();
  testAdaptiveLearningFactor();
  testGrindAdvisor();
  testFlowFeatures();
  testGrindOutliers();
  testStopBeforeAnyCoffee();
  testPreinfusionHold();
  testBloom();
  testShare();
  testCommunity();
  testGrinders();
  testCupRequired();
  testRestartRecovery();
  testRatedTarget();
  testSettingsGrowAtTheEnd();
  testFrozenBeans();
  testSimilarCoffee();
  testBags();
  testUnopenedBags();
  testOnTheWayBags();
  testSubscriptions();
  testServiceSharing();
  testExtraction();
  testUpkeep();
  testUpkeepDefaults();
  testBagFinished();
  testShotContext();
  testLearnGrinder();
  testSettingFormat();
  testGrinderUnitTidy();
  printf("\n%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
