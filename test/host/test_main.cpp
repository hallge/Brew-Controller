// Host tests for everything under src/core plus the vendored scale protocols.
// A simulated Breville + Lunar + Bot is driven through complete shots, and the
// same shot is run over the wire format of every supported scale.
// Build and run: see test/host/run.sh

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
#include "core/PowerControl.h"
#include "core/PusherLink.h"
#include "core/ScaleLink.h"
#include "core/SettingsUtil.h"
#include "core/Share.h"
#include "core/ShotData.h"
#include "core/SwitchBotProtocol.h"

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
    if (machineStoppedMs != 0 && nowMs - machineStoppedMs < dripMs &&
        machineStoppedMs - machineStartedMs >= preinfusionMs) {
      return dripGS * (1.0 - static_cast<double>(nowMs - machineStoppedMs) / dripMs);
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
          finish(downAcked && upOk ? PressResult::OK : PressResult::UNCONFIRMED, nowMs);
        } else if (!ready || static_cast<uint32_t>(nowMs - phaseSinceMs) >= 1500) {
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
  CHECK(r.runUntil(State::SWITCHBOT_DISCONNECTED, 20000));
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
  CHECK(shot.alphaUsed == 30 && shot.alphaNext == 30);
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
    if (k == 0) CHECK(shot.alphaUsed == 10);
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
  g = Grinder{};
  g.modelId = 2;                                              // another grinder
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
        strstr(body, "\"grinder\":{\"model_id\":1,\"model\":\"Mahlkonig E64 WS\",\"burrs\":\"64 mm flat\","
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
  // A grinder read in steps of 5 microns.
  Grinder lucca;
  lucca.modelId = 2;
  lucca.umPerUnit = 5.0f;
  CommunityFit steps;
  steps.known = true;
  steps.scale = 0.2f;
  steps.offset = 2.0f;
  steps.coffeesInCommon = 2;
  s = communityStart(d, steps, lucca);
  CHECK(s.setting == 49 && s.range == 1);                    // 0.2 x 236 + 2
  d.modelCount = 0;
  CHECK(communityStart(d, CommunityFit{}, lucca).setting == 47);   // 236 / 5, unfitted
  // An unmarked dial with no fit and nobody on the same model: no suggestion.
  Grinder dial;
  dial.umPerUnit = 0.0f;
  CHECK(communityStart(d, CommunityFit{}, dial).basis == StartBasis::NONE);
  // A fit that gives nonsense is not passed on.
  steps.offset = -500.0f;
  CHECK(communityStart(d, steps, lucca).setting == 0);

  CHECK(sameRecipeClass(18.0f, 36.0f, 18.5f, 37.5f) && !sameRecipeClass(18.0f, 36.0f, 18.0f, 27.0f));
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
  testPusherDrivers();
  testPowerControl();
  testShotDataSet();
  testReadingNewerThanTick();
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
  printf("\n%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
