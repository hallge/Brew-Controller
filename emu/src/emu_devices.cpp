// The emulated world: an Acaia Lunar, a SwitchBot Bot (plus a decoy Bot on a
// "light switch") and the Breville's MANUAL button with a simple model of
// the shot it produces.
//
// The two peripherals are written from the protocol descriptions in
// docs/RESEARCH.md (AcaiaArduinoBLE / Open Brew for the Lunar, SwitchBot's
// bot.md / pySwitchbot for the Bot) and do not share code with the firmware:
// they build and parse the bytes themselves.

#include <ArduinoJson.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>

#include "emu.h"
#include "emu_radio.h"

extern "C" uint32_t millis(void);

namespace emu {

namespace {

double clampD(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

// ----------------------------------------------------------------- machine --

struct Machine {
  // Settings
  bool powered = true;       // off: the button does nothing
  double preinfusionS = 7.0;  // button press to first drops in the cup
  double flowGS = 2.0;        // steady flow into the cup
  double rampS = 2.5;         // first drops to steady flow
  double dripG = 1.5;         // lands in the cup after the pump stops
  bool cupOnScale = true;
  double cupG = 62.4;
  // State
  bool brewing = false;
  double brewS = 0.0;
  double flowNowGS = 0.0;
  double dripLeftG = 0.0, dripTauS = 0.45;
  double liquidG = 0.0;
  int presses = 0;

  // Other machines than the Breville: a start button that only starts, with
  // the stop on a button of its own; and a button that brews while it is held.
  bool separateButtons = false;
  bool holdToBrew = false;
  int stopPresses = 0;
  void stopPump(const char *how, const char *who) {
    if (!brewing) return;
    brewing = false;
    dripLeftG = flowGS > 0 ? dripG * clampD(flowNowGS / flowGS, 0.0, 1.0) : 0.0;
    dripTauS = flowNowGS > 0 ? clampD(dripLeftG / flowNowGS, 0.15, 2.0) : 0.45;
    log("machine", "%s by %s: pump OFF after %.1f s, %.1f g in the cup", how, who, brewS, liquidG);
  }
  void pressStop(const char *who) {
    ++stopPresses;
    if (!powered || !brewing) {
      log("machine", "STOP pressed by %s: nothing is running", who);
      return;
    }
    stopPump("STOP pressed", who);
  }
  // The arm coming back up lets go of the button.
  void releaseButton(const char *who) {
    if (holdToBrew) stopPump("button released", who);
  }

  int powerPresses = 0;
  // The power button: a toggle, like the real one.
  void pressPower(const char *who) {
    ++powerPresses;
    powered = !powered;
    if (!powered) brewing = false;
    log("machine", "POWER pressed by %s: machine %s", who, powered ? "ON" : "OFF");
  }

  void pressButton(const char *who) {
    ++presses;
    if (!powered) {
      log("machine", "MANUAL pressed by %s: machine is off, nothing happens", who);
      return;
    }
    if ((separateButtons || holdToBrew) && brewing) {
      log("machine", "start button pressed by %s: already brewing, nothing changes", who);
      return;
    }
    brewing = !brewing;
    if (brewing) {
      brewS = 0.0;
      dripLeftG = 0.0;
      log("machine", "MANUAL pressed by %s: pump ON (pre-infusion %.1f s)", who, preinfusionS);
    } else {
      dripLeftG = flowGS > 0 ? dripG * clampD(flowNowGS / flowGS, 0.0, 1.0) : 0.0;
      // The stream tails off from the rate it had, it does not jump.
      dripTauS = flowNowGS > 0 ? clampD(dripLeftG / flowNowGS, 0.15, 2.0) : 0.45;
      log("machine", "MANUAL pressed by %s: pump OFF after %.1f s, %.1f g in the cup", who,
          brewS, liquidG);
    }
  }

  void step(double dt) {
    if (brewing) {
      brewS += dt;
      const double pouring = brewS - preinfusionS;
      flowNowGS = pouring <= 0 ? 0.0 : flowGS * clampD(rampS > 0 ? pouring / rampS : 1.0, 0.0, 1.0);
      liquidG += flowNowGS * dt;
      if (brewS > 120.0) {  // nobody is stopping it; the tank is not endless
        brewing = false;
        log("machine", "pump stopped by the emulator after 120 s");
      }
    } else if (dripLeftG > 0.001) {
      const double out = dripLeftG * (1.0 - exp(-dt / dripTauS));
      dripLeftG -= out;
      liquidG += out;
      flowNowGS = out / dt;
    } else {
      flowNowGS = 0.0;
    }
  }

  // What rests on the scale platform.
  double load() const { return cupOnScale ? cupG + liquidG : 0.0; }
};

Machine g_machine;

// ------------------------------------------------------------------- Lunar --

const char *const ACAIA_SERVICE = "49535343-fe7d-4ae5-8fa9-9fafd205e455";
const char *const ACAIA_NOTIFY = "49535343-1e4d-4bd9-ba61-23c647249616";
const char *const ACAIA_WRITE = "49535343-8841-43f4-a8d4-ecbe34729bb3";
const char *const LEGACY_SERVICE = "00001820-0000-1000-8000-00805f9b34fb";
const char *const LEGACY_CHAR = "00002a80-0000-1000-8000-00805f9b34fb";
// The other makes, as far as their protocol tables in lib/EspressoScaleProtocol
// describe them: enough to connect, stream, tare and (Felicita) run the timer.
const char *const FELICITA_SERVICE = "0000ffe0-0000-1000-8000-00805f9b34fb";
const char *const FELICITA_CHAR = "0000ffe1-0000-1000-8000-00805f9b34fb";
const char *const DECENT_SERVICE = "0000fff0-0000-1000-8000-00805f9b34fb";
const char *const DECENT_NOTIFY = "0000fff4-0000-1000-8000-00805f9b34fb";
const char *const DECENT_WRITE = "000036f5-0000-1000-8000-00805f9b34fb";
const char *const DIFLUID_SERVICE = "000000ee-0000-1000-8000-00805f9b34fb";
const char *const DIFLUID_CHAR = "0000aa01-0000-1000-8000-00805f9b34fb";
// The controller looks for the characteristics in whatever service has them,
// so where the tables name no service, any will do here.
const char *const ECLAIR_SERVICE = "b905eaea-6c7e-4f73-b43d-2cdfcab29570";
const char *const ECLAIR_NOTIFY = "ad736c5f-bbc9-1f96-d304-cb5d5f41e160";
const char *const ECLAIR_WRITE = "4f9a45ba-8e1b-4e07-e157-0814d393b968";
const char *const FFF1_NOTIFY = "0000fff1-0000-1000-8000-00805f9b34fb";  // Eureka and Varia
const char *const FFF2_WRITE = "0000fff2-0000-1000-8000-00805f9b34fb";
const char *const MYSCALE_SERVICE = "0000ffb0-0000-1000-8000-00805f9b34fb";
const char *const MYSCALE_NOTIFY = "0000ffb2-0000-1000-8000-00805f9b34fb";
const char *const MYSCALE_WRITE = "0000ffb1-0000-1000-8000-00805f9b34fb";
const char *const WEIGHMYBRU_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const char *const WEIGHMYBRU_NOTIFY = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const char *const WEIGHMYBRU_WRITE = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

class Lunar : public Peripheral {
 public:
  // The scale on the bench. It is a Lunar unless another make is picked.
  enum Variant {
    LUNAR_2021 = 0, LUNAR_2021_TIMER = 1, LUNAR_LEGACY = 2,
    FELICITA = 3, DECENT = 4, DIFLUID = 5, ECLAIR = 6, EUREKA = 7, MYSCALE = 8, VARIA = 9,
    WEIGHMYBRU = 10, VARIANT_COUNT
  };
  int variaSecondSent = -1;  // the Varia sends its timer when the second changes
  bool isAcaia() const { return variant <= LUNAR_LEGACY; }

  // Settings
  bool powered = true;
  // The user's scale: Lunar 2021, firmware 1.0.019, 17-byte weight frames.
  int variant = LUNAR_2021_TIMER;
  int battery = 74;
  bool splitFrames = false;  // deliver each frame as 3 + n bytes, as real scales sometimes do
  double rateHz = 10.0;
  double noiseG = 0.0;
  bool heartbeatWatchdog = true;
  bool silent = false;   // fault: link stays up, no packets
  int ignoreTares = 0;   // fault: this many tare commands are dropped
  int rssi = -58;
  // State
  double filteredG = 0.0;  // load cell after its settling time
  double tareG = 0.0;
  bool identified = false, streaming = false;
  bool timerRunning = false;
  double timerS = 0.0;
  double sinceHeartbeatS = 0.0, sendDueS = 0.0;
  int tares = 0, heartbeats = 0, packets = 0;

  Lunar() {
    label = "Lunar";
    address = "00:1c:97:1a:45:e2";
    addressType = 0;
  }

  double displayG() const { return std::round((filteredG - tareG) * 10.0) / 10.0; }

  bool advertising() override { return powered; }

  Advertisement advertisement() override {
    Advertisement a;
    a.name = variant == LUNAR_LEGACY ? "ACAIAL1A45E2"
             : variant == FELICITA ? "FELICITA"
             : variant == DECENT ? "Decent Scale"
             : variant == DIFLUID ? "Microbalance"
             : variant == ECLAIR ? "ECLAIR-45E2"
             : variant == EUREKA ? "CFS-9002"
             : variant == MYSCALE ? "my_scale"
             : variant == VARIA ? "AKU MINI SCALE"
             : variant == WEIGHMYBRU ? "WeighMyBru"
             : "LUNAR-1A45E2";
    a.rssi = rssi;
    return a;
  }

  std::vector<GattService> gatt() override {
    if (variant == LUNAR_LEGACY) {
      return {{LEGACY_SERVICE, {{LEGACY_CHAR, true, false, false, true}}}};
    }
    if (variant == FELICITA) {
      return {{FELICITA_SERVICE, {{FELICITA_CHAR, true, false, false, true}}}};
    }
    if (variant == DECENT) {
      return {{DECENT_SERVICE,
               {{DECENT_NOTIFY, true, false, false, false}, {DECENT_WRITE, false, false, true, true}}}};
    }
    if (variant == DIFLUID) {
      return {{DIFLUID_SERVICE, {{DIFLUID_CHAR, true, false, true, true}}}};
    }
    if (variant == ECLAIR) {
      return {{ECLAIR_SERVICE,
               {{ECLAIR_NOTIFY, true, false, false, false}, {ECLAIR_WRITE, false, false, true, true}}}};
    }
    if (variant == EUREKA || variant == VARIA) {
      return {{DECENT_SERVICE,
               {{FFF1_NOTIFY, true, false, false, false}, {FFF2_WRITE, false, false, true, true}}}};
    }
    if (variant == MYSCALE) {
      return {{MYSCALE_SERVICE,
               {{MYSCALE_NOTIFY, true, false, false, false}, {MYSCALE_WRITE, false, false, true, true}}}};
    }
    if (variant == WEIGHMYBRU) {
      return {{WEIGHMYBRU_SERVICE,
               {{WEIGHMYBRU_NOTIFY, true, false, false, false},
                {WEIGHMYBRU_WRITE, false, false, true, true}}}};
    }
    return {{ACAIA_SERVICE,
             {{ACAIA_NOTIFY, true, false, false, false}, {ACAIA_WRITE, false, false, true, true}}}};
  }

  void onConnect() override {
    identified = false;
    // These send their weight unasked; the others wait for their setup writes.
    streaming = variant == DECENT || variant >= ECLAIR;
    variaSecondSent = -1;
    sinceHeartbeatS = 0.0;
  }
  void onDisconnect() override { identified = streaming = false; }

  void pressTare(const char *who) {
    tareG = filteredG;
    ++tares;
    log("scale", "tare (%s)", who);
  }

  void setPower(bool on) {
    if (on == powered) return;
    powered = on;
    log("scale", on ? "switched on" : "switched off");
    if (!on) {
      timerRunning = false;
      dropLink("scale switched off");
    } else {
      tareG = g_machine.load();  // a Lunar zeroes itself when it starts
      filteredG = tareG;
      timerS = 0.0;
    }
  }

  void bleTare() {
    if (ignoreTares > 0) {
      --ignoreTares;
      log("scale", "tare command received and ignored (fault injection)");
    } else {
      pressTare("BLE command");
    }
  }

  // 0 tare, 1 timer start, 2 timer stop, 3 timer reset.
  void simpleCommand(int what) {
    if (what == 0) {
      bleTare();
    } else if (what == 1) {
      timerRunning = true;
      log("scale", "timer start");
    } else if (what == 2) {
      timerRunning = false;
      log("scale", "timer stop at %.1f s", timerS);
    } else {
      timerRunning = false;
      timerS = 0.0;
      log("scale", "timer reset");
    }
  }

  // The commands of the makes that are not Acaia. True: it was one of theirs.
  bool onOtherWrite(const uint8_t *d, size_t n) {
    if (variant == ECLAIR && n == 3) {
      const int what = d[0] == 0x54 ? 0 : d[0] == 0x53 ? 1 : d[0] == 0x45 ? 2 : d[0] == 0x52 ? 3 : -1;
      if (what >= 0) simpleCommand(what);
      return what >= 0;
    }
    if (variant == EUREKA && n == 6 && d[0] == 0xaa && d[1] == 0x02) {
      const int what = d[2] == 0x31 ? 0 : d[2] == 0x33 ? 1 : d[2] == 0x34 ? 2 : d[2] == 0x35 ? 3 : -1;
      if (what >= 0) simpleCommand(what);
      return what >= 0;
    }
    if (variant == VARIA && n == 5 && d[0] == 0xfa) {
      const int what = d[1] == 0x82 ? 0 : d[1] == 0x88 ? 1 : d[1] == 0x89 ? 2 : d[1] == 0x8a ? 3 : -1;
      if (what >= 0) simpleCommand(what);
      return what >= 0;
    }
    if (variant == MYSCALE && n == 20 && d[0] == 0xac && d[1] == 0x40) {
      bleTare();
      return true;
    }
    if (variant == WEIGHMYBRU && n == 6 && d[0] == 0x03 && d[1] == 0x0a) {
      bleTare();
      return true;
    }
    if (variant == FELICITA && n == 1) {
      switch (d[0]) {
        case 0x32:
          streaming = true;
          log("scale", "weight and timer mode requested, weight stream on");
          break;
        case 0x54: bleTare(); break;
        case 0x52:
          timerRunning = true;
          log("scale", "timer start");
          break;
        case 0x53:
          timerRunning = false;
          log("scale", "timer stop at %.1f s", timerS);
          break;
        case 0x43:
          timerRunning = false;
          timerS = 0.0;
          log("scale", "timer reset");
          break;
        default: log("scale", "unknown command 0x%02x", d[0]); break;
      }
      return true;
    }
    if (variant == DECENT && n == 7 && d[0] == 0x03) {
      if (d[1] == 0x0f) bleTare();
      else if (d[1] == 0x0a) ++heartbeats;
      else log("scale", "unknown command type 0x%02x", d[1]);
      return true;
    }
    if (variant == DIFLUID && n >= 6 && d[0] == 0xdf && d[1] == 0xdf) {
      if (d[2] == 0x03 && d[3] == 0x02) bleTare();
      else if (d[2] == 0x03 && d[3] == 0x05) ++heartbeats;
      else if (d[2] == 0x01 && d[3] == 0x04) log("scale", "unit set to grams");
      else if (d[2] == 0x01 && d[3] == 0x00) {
        streaming = true;
        log("scale", "auto notify requested, weight stream on");
      } else log("scale", "unknown command %02x %02x", d[2], d[3]);
      return true;
    }
    return false;
  }

  void onWrite(const std::string &, const uint8_t *d, size_t n) override {
    if (!powered) return;
    if (!isAcaia()) {
      if (!onOtherWrite(d, n)) log("scale", "write not understood by this make (%u bytes)", static_cast<unsigned>(n));
      return;
    }
    if (n < 3 || d[0] != 0xef || d[1] != 0xdd) return;
    switch (d[2]) {
      case 0x0b:  // identify
        identified = true;
        log("scale", "identify received");
        break;
      case 0x0c:  // notification request
        if (identified) {
          streaming = true;
          log("scale", "notification request received, weight stream on");
        } else {
          log("scale", "notification request before identify: ignored");
        }
        break;
      case 0x00:  // heartbeat
        sinceHeartbeatS = 0.0;
        ++heartbeats;
        break;
      case 0x06: {  // get settings: battery in the low 7 bits, timer state on top
        std::vector<uint8_t> p = {0xef, 0xdd, 0x08, 0x09,
                                  static_cast<uint8_t>((battery & 0x7f) | (timerRunning ? 0x80 : 0)),
                                  0x02, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0, 0};
        checksum(p);
        notify(ACAIA_NOTIFY, p, true);
        log("scale", "settings requested (battery %d %%)", battery);
        break;
      }
      case 0x04:  // tare
        bleTare();
        break;
      case 0x0d:  // timer: byte 4 = 0 start, 1 reset, 2 stop
        if (n >= 5 && d[4] == 0x00) {
          timerRunning = true;
          log("scale", "timer start");
        } else if (n >= 5 && d[4] == 0x01) {
          timerRunning = false;
          timerS = 0.0;
          log("scale", "timer reset");
        } else if (n >= 5 && d[4] == 0x02) {
          timerRunning = false;
          log("scale", "timer stop at %.1f s", timerS);
        }
        break;
      default:
        log("scale", "unknown command type 0x%02x", d[2]);
        break;
    }
  }

  void step(double dt) override {
    if (!powered) return;
    filteredG += (g_machine.load() - filteredG) * (1.0 - exp(-dt / 0.12));
    if (timerRunning) timerS += dt;
    if (!linked()) return;
    sinceHeartbeatS += dt;
    if (isAcaia() && heartbeatWatchdog && sinceHeartbeatS > 6.0) {
      dropLink("no heartbeat for 6 s");
      return;
    }
    sendDueS -= dt;
    if (!streaming || silent || sendDueS > 0) return;
    sendDueS = 1.0 / clampD(rateHz, 1.0, 50.0);
    sendWeight();
  }

 private:
  // Two-byte additive checksum over the payload (from byte 3), even and odd
  // positions summed separately.
  static void checksum(std::vector<uint8_t> &p) {
    uint8_t even = 0, odd = 0;
    for (size_t i = 3; i + 2 < p.size(); ++i) {
      if ((i - 3) & 1) odd = static_cast<uint8_t>(odd + p[i]);
      else even = static_cast<uint8_t>(even + p[i]);
    }
    p[p.size() - 2] = even;
    p[p.size() - 1] = odd;
  }

  void sendWeight() {
    static std::mt19937 rng(12345);
    double shown = displayG();
    if (noiseG > 0) {
      shown += std::round(std::normal_distribution<double>(0.0, noiseG)(rng) * 10.0) / 10.0;
    }
    const uint32_t raw = static_cast<uint32_t>(std::llround(std::fabs(shown) * 10.0));
    const uint8_t flags = shown < 0 ? 0x02 : 0x00;
    const uint8_t w0 = raw & 0xff, w1 = (raw >> 8) & 0xff, w2 = (raw >> 16) & 0xff;
    const int tenths = static_cast<int>(timerS * 10.0);
    const uint8_t minutes = static_cast<uint8_t>((tenths / 600) % 100);
    const uint8_t seconds = static_cast<uint8_t>((tenths / 10) % 60);
    ++packets;
    if (variant == FELICITA) {
      // 18 bytes of text: sign, six digits of 0.01 g, the timer as mm ss t.
      char text[19];
      snprintf(text, sizeof(text), "\x01\x02%c%06u%02u%02u%ug \r\n", shown < 0 ? '-' : '+',
               static_cast<unsigned>(raw * 10), static_cast<unsigned>(minutes),
               static_cast<unsigned>(seconds), static_cast<unsigned>(tenths % 10));
      notify(FELICITA_CHAR, std::vector<uint8_t>(text, text + 18), true);
      return;
    }
    if (variant == ECLAIR) {
      // 'W' <weight, int32 LE, mg> <timer, uint32 LE, ms> <xor of those eight>
      const int32_t mg = (shown < 0 ? -1 : 1) * static_cast<int32_t>(raw) * 100;
      const uint32_t ms = static_cast<uint32_t>(tenths) * 100u;
      std::vector<uint8_t> p(10, 0x00);
      p[0] = 'W';
      for (int k = 0; k < 4; ++k) {
        p[1 + k] = static_cast<uint8_t>((static_cast<uint32_t>(mg) >> (8 * k)) & 0xff);
        p[5 + k] = static_cast<uint8_t>((ms >> (8 * k)) & 0xff);
      }
      for (size_t i = 1; i < 9; ++i) p[9] ^= p[i];
      notify(ECLAIR_NOTIFY, p, true);
      return;
    }
    if (variant == EUREKA) {
      // aa 09 41 .. .. .. <sign> <weight, uint16 LE, 0.1 g> .. ..
      notify(FFF1_NOTIFY, {0xaa, 0x09, 0x41, 0x00, 0x00, 0x00, static_cast<uint8_t>(shown < 0 ? 1 : 0),
                           w0, w1, 0x00, 0x00}, true);
      return;
    }
    if (variant == MYSCALE) {
      // 15 bytes: sign in the high nibble of byte 2, weight in mg, 28 bits BE, from byte 3
      const uint32_t mg = raw * 100;
      std::vector<uint8_t> p(15, 0x00);
      p[2] = shown < 0 ? 0x80 : 0x00;
      p[3] = (mg >> 24) & 0x0f; p[4] = (mg >> 16) & 0xff; p[5] = (mg >> 8) & 0xff; p[6] = mg & 0xff;
      notify(MYSCALE_NOTIFY, p, true);
      return;
    }
    if (variant == VARIA) {
      // fa 01 03 <sign bit 4 | weight, 20 bits BE, 0.01 g> <xor of bytes 1..5>
      const uint32_t hundredths = raw * 10;
      std::vector<uint8_t> p = {0xfa, 0x01, 0x03,
                                static_cast<uint8_t>(((hundredths >> 16) & 0x0f) | (shown < 0 ? 0x10 : 0)),
                                static_cast<uint8_t>((hundredths >> 8) & 0xff),
                                static_cast<uint8_t>(hundredths & 0xff), 0x00};
      for (size_t i = 1; i < 6; ++i) p[6] ^= p[i];
      notify(FFF1_NOTIFY, p, true);
      // Its timer, in a frame of its own, whole seconds: fa 87 02 <s, uint16 BE> <xor>
      const int second = tenths / 10;
      if (second != variaSecondSent) {
        variaSecondSent = second;
        std::vector<uint8_t> t = {0xfa, 0x87, 0x02, static_cast<uint8_t>((second >> 8) & 0xff),
                                  static_cast<uint8_t>(second & 0xff), 0x00};
        for (size_t i = 1; i < 5; ++i) t[5] ^= t[i];
        notify(FFF1_NOTIFY, t, true);
      }
      return;
    }
    if (variant == WEIGHMYBRU) {
      // 20 bytes: 03 0b ... <sign character> <weight, 24 bits BE, 0.01 g> ... <xor of the first 19>
      const uint32_t hundredths = raw * 10;
      std::vector<uint8_t> p(20, 0x00);
      p[0] = 0x03; p[1] = 0x0b;
      p[6] = shown < 0 ? '-' : '+';
      p[7] = (hundredths >> 16) & 0xff; p[8] = (hundredths >> 8) & 0xff; p[9] = hundredths & 0xff;
      for (size_t i = 0; i < 19; ++i) p[19] ^= p[i];
      notify(WEIGHMYBRU_NOTIFY, p, true);
      return;
    }
    if (variant == DECENT || variant == DIFLUID) {
      const int32_t signedRaw = shown < 0 ? -static_cast<int32_t>(raw) : static_cast<int32_t>(raw);
      const uint8_t b3 = (signedRaw >> 24) & 0xff, b2 = (signedRaw >> 16) & 0xff;
      const uint8_t b1 = (signedRaw >> 8) & 0xff, b0 = signedRaw & 0xff;
      if (variant == DECENT) {
        // 03 ce <weight, int16 BE, 0.1 g> 00 00 <xor of the first six>
        std::vector<uint8_t> p = {0x03, 0xce, b1, b0, 0x00, 0x00, 0x00};
        for (size_t i = 0; i < 6; ++i) p[6] ^= p[i];
        notify(DECENT_NOTIFY, p, true);
        return;
      }
      // df df 03 00 <13> <weight, int32 BE, 0.1 g> ... <sum of all before it>
      std::vector<uint8_t> p(19, 0x00);
      p[0] = p[1] = 0xdf;
      p[2] = 0x03;
      p[4] = 13;
      p[5] = b3; p[6] = b2; p[7] = b1; p[8] = b0;
      for (size_t i = 0; i < 18; ++i) p[18] = static_cast<uint8_t>(p[18] + p[i]);
      notify(DIFLUID_CHAR, p, true);
      return;
    }
    if (variant == LUNAR_LEGACY) {
      // 10 bytes: weight in 2..3, decimal exponent in 6, sign in 7.
      notify(LEGACY_CHAR, {0x0c, 0x05, w0, w1, w2, 0x00, 0x01, flags, 0x00, 0x00}, true);
      return;
    }
    // ef dd 0c <len> 05 <weight, 4 bytes LE> <exponent> <flags> ... <cks> <cks>
    std::vector<uint8_t> p = {0xef, 0xdd, 0x0c, 0x08, 0x05, w0, w1, w2, 0x00, 0x01, flags};
    if (variant == LUNAR_2021_TIMER) {
      // The weight record is followed by a timer record: tag 07, then minutes,
      // seconds, tenths. Layout from captures of a real 2021+ Lunar, e.g.
      //   ef dd 0c 0c 05 00 00 00 00 01 00 07 00 08 01 1c 06   (0 g, 8.1 s)
      p[3] = 0x0c;
      p.insert(p.end(), {0x07, minutes, seconds, static_cast<uint8_t>(tenths % 10)});
    }
    p.push_back(0);
    p.push_back(0);
    checksum(p);
    if (splitFrames) {
      notify(ACAIA_NOTIFY, std::vector<uint8_t>(p.begin(), p.begin() + 3), true);
      notify(ACAIA_NOTIFY, std::vector<uint8_t>(p.begin() + 3, p.end()), true);
      return;
    }
    notify(ACAIA_NOTIFY, p, true);
  }
};

Lunar g_lunar;

// --------------------------------------------------------------------- Bot --

const char *const BOT_SERVICE = "cba20d00-224d-11e6-9fb8-0002a5d5c51b";
const char *const BOT_WRITE = "cba20002-224d-11e6-9fb8-0002a5d5c51b";
const char *const BOT_NOTIFY = "cba20003-224d-11e6-9fb8-0002a5d5c51b";

uint32_t crc32Of(const std::string &text) {
  uint32_t crc = 0xFFFFFFFFu;
  for (unsigned char c : text) {
    crc ^= c;
    for (int bit = 0; bit < 8; ++bit) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
  }
  return ~crc;
}

class Bot : public Peripheral {
 public:
  enum Arm { ARM_UP, ARM_GOING_DOWN, ARM_DOWN, ARM_GOING_UP };

  // Settings
  bool inRange = true;
  bool mounted = true;        // on the machine's MANUAL button
  bool onPowerButton = false; // the power bot: its arm is on the POWER button instead
  bool onStopButton = false;  // the stop bot: its arm is on a separate STOP button
  int battery = 87;
  bool switchMode = false;    // "Switch" mode in the SwitchBot app; "Press" is needed
  int holdSeconds = 0;
  std::string password;
  double wakeS = 1.2;         // how long a sleeping Bot takes to accept a connection
  double travelS = 0.35;      // arm travel one way
  bool ackWhenDone = false;   // acknowledge after the arm has moved, not on receipt
  int rssi = -52;
  int dropAcks = 0;           // fault: act, but do not acknowledge
  int ignoreCommands = 0;     // fault: neither act nor acknowledge
  bool armJammed = false;     // fault: acknowledges, arm does not reach the button
  // State
  Arm arm = ARM_UP;
  double armS = 0.0;          // time in the current arm phase
  bool fullPress = false;     // a 57 01 00 cycle: down, hold, up
  double holdLeftS = 0.0;
  bool pendingAck = false;
  int presses = 0, commands = 0;
  std::string lastCommand;

  explicit Bot(const char *name, const char *mac) {
    label = name;
    address = mac;
    addressType = 1;  // random static
  }

  bool advertising() override { return inRange && battery > 0; }

  Advertisement advertisement() override {
    Advertisement a;  // no local name, like the real one
    a.rssi = rssi;
    std::string data;
    data += static_cast<char>(0x48);                                // 'H' = Bot
    data += static_cast<char>(switchMode ? 0x80 : 0x00);            // mode / state
    data += static_cast<char>(battery & 0x7f);
    a.serviceData.emplace_back("fd3d", data);
    a.serviceUuids.push_back(BOT_SERVICE);
    return a;
  }

  uint32_t connectDelayMs() override { return static_cast<uint32_t>(wakeS * 1000.0); }

  std::vector<GattService> gatt() override {
    return {{BOT_SERVICE,
             {{BOT_WRITE, false, false, true, true}, {BOT_NOTIFY, true, false, false, false}}}};
  }

  void reply(uint8_t status) {
    if (dropAcks > 0) {
      --dropAcks;
      log("bot", "%s: reply 0x%02x NOT sent (fault injection)", label.c_str(), status);
      return;
    }
    notify(BOT_NOTIFY, {status});
  }

  void onWrite(const std::string &, const uint8_t *d, size_t n) override {
    ++commands;
    lastCommand = hex(d, n);
    if (ignoreCommands > 0) {
      --ignoreCommands;
      log("bot", "%s: command lost (fault injection)", label.c_str());
      return;
    }
    if (n < 2 || d[0] != 0x57) return reply(0x02);
    const uint8_t command = d[1] & 0x0f;
    const bool encrypted = (d[1] & 0xf0) == 0x10;
    size_t at = 2;
    if (command == 0x01 || command == 0x02) {
      if (!password.empty() && !encrypted) return reply(0x07);  // password required
      if (password.empty() && encrypted) return reply(0x08);    // no password set
      if (encrypted) {
        if (n < 6) return reply(0x02);
        const uint32_t key = (uint32_t(d[2]) << 24) | (uint32_t(d[3]) << 16) |
                             (uint32_t(d[4]) << 8) | d[5];
        if (key != crc32Of(password)) return reply(0x09);       // wrong password
        at = 6;
      }
    }
    if (command == 0x02) {  // basic info: 13 bytes
      std::vector<uint8_t> info(13, 0);
      info[0] = 0x01;
      info[1] = static_cast<uint8_t>(battery);
      info[2] = 63;  // firmware 6.3
      info[9] = switchMode ? 0x10 : 0x00;
      info[10] = static_cast<uint8_t>(holdSeconds);
      log("bot", "%s: info requested (battery %d %%, %s mode, hold %d s)", label.c_str(),
          battery, switchMode ? "switch" : "press", holdSeconds);
      if (dropAcks > 0) return reply(0x01);
      notify(BOT_NOTIFY, info);
      return;
    }
    if (d[1] == 0x0f && n >= 4 && d[2] == 0x08) {  // hold time
      holdSeconds = d[3];
      log("bot", "%s: hold time set to %d s", label.c_str(), holdSeconds);
      return reply(0x01);
    }
    if (command != 0x01 || n <= at) return reply(0x02);
    const uint8_t action = d[at];
    if (battery < 5) return reply(0x06);  // low battery: refuses to move
    switch (action) {
      case 0x00:  // press
        if (switchMode) {
          log("bot", "%s: press refused, Bot is in switch mode", label.c_str());
          return reply(0x05);
        }
        if (arm != ARM_UP) return reply(0x03);  // busy
        fullPress = true;
        startDown();
        break;
      case 0x03:  // down and stay
        if (arm == ARM_GOING_DOWN || arm == ARM_DOWN) return reply(0x03);
        fullPress = false;
        startDown();
        break;
      case 0x04:  // up
        fullPress = false;
        if (arm == ARM_DOWN || arm == ARM_GOING_DOWN) {
          arm = ARM_GOING_UP;
          armS = 0.0;
        }
        break;
      case 0x01:  // on / off exist in switch mode only
      case 0x02:
        if (!switchMode) return reply(0x05);
        fullPress = false;
        if (action == 0x01) startDown();
        else if (arm != ARM_UP) { arm = ARM_GOING_UP; armS = 0.0; }
        break;
      default:
        return reply(0x02);
    }
    if (ackWhenDone) pendingAck = true;
    else reply(0x01);
  }

  void step(double dt) override {
    armS += dt;
    switch (arm) {
      case ARM_UP:
        break;
      case ARM_GOING_DOWN:
        if (armS < travelS) break;
        arm = ARM_DOWN;
        armS = 0.0;
        holdLeftS = holdSeconds;
        ++presses;
        if (armJammed) {
          log("bot", "%s: arm jammed, button not reached (fault injection)", label.c_str());
        } else if (mounted && onPowerButton) {
          g_machine.pressPower(label.c_str());
        } else if (mounted && onStopButton) {
          g_machine.pressStop(label.c_str());
        } else if (mounted) {
          g_machine.pressButton(label.c_str());
        } else {
          log("bot", "%s: arm down (not mounted on the machine)", label.c_str());
        }
        if (!fullPress) finishAck();
        break;
      case ARM_DOWN:
        if (!fullPress) break;
        holdLeftS -= dt;
        if (holdLeftS <= 0) {
          arm = ARM_GOING_UP;
          armS = 0.0;
        }
        break;
      case ARM_GOING_UP:
        if (armS < travelS) break;
        arm = ARM_UP;
        fullPress = false;
        if (mounted && !onPowerButton && !onStopButton && !armJammed) g_machine.releaseButton(label.c_str());
        finishAck();
        break;
    }
  }

 private:
  void startDown() {
    arm = ARM_GOING_DOWN;
    armS = 0.0;
  }
  void finishAck() {
    if (pendingAck) {
      pendingAck = false;
      reply(0x01);
    }
  }
};

Bot g_bot("Bot", "e5:8a:3c:11:9d:40");
Bot g_decoy("Decoy Bot", "f1:22:7b:5e:01:c8");
Bot g_powerBot("Power Bot", "d3:41:9a:07:c2:15");
Bot g_stopBot("Stop Bot", "c7:5e:20:b4:66:0a");

// --------------------------------------------------------- state, settings --

bool truthy(const std::string &v) { return v == "1" || v == "true" || v == "on"; }

const char *armName(int arm) {
  static const char *const names[] = {"up", "moving down", "down", "moving up"};
  return names[arm & 3];
}

void botState(const Bot &b, JsonObject o) {
  o["inRange"] = b.inRange;
  o["mounted"] = b.mounted;
  o["battery"] = b.battery;
  o["switchMode"] = b.switchMode;
  o["holdSeconds"] = b.holdSeconds;
  o["password"] = b.password;
  o["wakeS"] = b.wakeS;
  o["ackWhenDone"] = b.ackWhenDone;
  o["rssi"] = b.rssi;
  o["dropAcks"] = b.dropAcks;
  o["ignoreCommands"] = b.ignoreCommands;
  o["armJammed"] = b.armJammed;
  o["arm"] = armName(b.arm);
  o["linked"] = b.linked();
  o["presses"] = b.presses;
  o["commands"] = b.commands;
  o["lastCommand"] = b.lastCommand;
  o["address"] = b.address;
}

bool botSet(Bot &b, const std::string &key, const std::string &value) {
  const double number = atof(value.c_str());
  if (key == "inRange") {
    b.inRange = truthy(value);
    if (!b.inRange) b.dropLink("out of range");
  } else if (key == "mounted") b.mounted = truthy(value);
  else if (key == "battery") b.battery = static_cast<int>(clampD(number, 0, 100));
  else if (key == "switchMode") b.switchMode = truthy(value);
  else if (key == "holdSeconds") b.holdSeconds = static_cast<int>(clampD(number, 0, 60));
  else if (key == "password") b.password = value;
  else if (key == "wakeS") b.wakeS = clampD(number, 0, 30);
  else if (key == "ackWhenDone") b.ackWhenDone = truthy(value);
  else if (key == "rssi") b.rssi = static_cast<int>(clampD(number, -100, -20));
  else if (key == "dropAcks") b.dropAcks = static_cast<int>(clampD(number, 0, 99));
  else if (key == "ignoreCommands") b.ignoreCommands = static_cast<int>(clampD(number, 0, 99));
  else if (key == "armJammed") b.armJammed = truthy(value);
  else if (key == "dropLink") b.dropLink("fault injection");
  else return false;
  return true;
}

void fillState(JsonDocument &doc) {
  JsonObject m = doc["machine"].to<JsonObject>();
  m["powered"] = g_machine.powered;
  m["preinfusionS"] = g_machine.preinfusionS;
  m["flowGS"] = g_machine.flowGS;
  m["rampS"] = g_machine.rampS;
  m["dripG"] = g_machine.dripG;
  m["cupOnScale"] = g_machine.cupOnScale;
  m["cupG"] = g_machine.cupG;
  m["brewing"] = g_machine.brewing;
  m["brewS"] = g_machine.brewS;
  m["flowNowGS"] = g_machine.flowNowGS;
  m["dripLeftG"] = g_machine.dripLeftG;
  m["liquidG"] = g_machine.liquidG;
  m["presses"] = g_machine.presses;

  JsonObject l = doc["lunar"].to<JsonObject>();
  l["powered"] = g_lunar.powered;
  l["variant"] = g_lunar.variant;
  l["rateHz"] = g_lunar.rateHz;
  l["noiseG"] = g_lunar.noiseG;
  l["heartbeatWatchdog"] = g_lunar.heartbeatWatchdog;
  l["silent"] = g_lunar.silent;
  l["splitFrames"] = g_lunar.splitFrames;
  l["battery"] = g_lunar.battery;
  l["ignoreTares"] = g_lunar.ignoreTares;
  l["rssi"] = g_lunar.rssi;
  l["filteredG"] = g_lunar.filteredG;
  l["tareG"] = g_lunar.tareG;
  l["displayG"] = g_lunar.displayG();
  l["timerRunning"] = g_lunar.timerRunning;
  l["timerS"] = g_lunar.timerS;
  l["linked"] = g_lunar.linked();
  l["streaming"] = g_lunar.streaming;
  l["tares"] = g_lunar.tares;
  l["heartbeats"] = g_lunar.heartbeats;
  l["packets"] = g_lunar.packets;
  l["name"] = g_lunar.advertisement().name;
  l["address"] = g_lunar.address;

  botState(g_bot, doc["bot"].to<JsonObject>());
  botState(g_decoy, doc["decoy"].to<JsonObject>());
  botState(g_powerBot, doc["powerbot"].to<JsonObject>());
  botState(g_stopBot, doc["stopbot"].to<JsonObject>());
  doc["machine"]["stopPresses"] = g_machine.stopPresses;
  doc["machine"]["separateButtons"] = g_machine.separateButtons;
  doc["machine"]["holdToBrew"] = g_machine.holdToBrew;
  doc["machine"]["powerPresses"] = g_machine.powerPresses;
}

std::string snapshotPath() { return g_options.dataDir + "/world.json"; }

}  // namespace

std::string worldStateJson() {
  std::lock_guard<std::recursive_mutex> lock(g_world);
  JsonDocument doc;
  fillState(doc);
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool worldSet(const std::string &device, const std::string &key, const std::string &value) {
  std::lock_guard<std::recursive_mutex> lock(g_world);
  const double number = atof(value.c_str());
  if (device == "bot") return botSet(g_bot, key, value);
  if (device == "decoy") return botSet(g_decoy, key, value);
  if (device == "powerbot") return botSet(g_powerBot, key, value);
  if (device == "stopbot") return botSet(g_stopBot, key, value);
  if (device == "machine") {
    Machine &m = g_machine;
    if (key == "powered") {
      m.powered = truthy(value);
      if (!m.powered) m.brewing = false;
    } else if (key == "preinfusionS") m.preinfusionS = clampD(number, 0, 30);
    else if (key == "flowGS") m.flowGS = clampD(number, 0, 10);
    else if (key == "rampS") m.rampS = clampD(number, 0, 10);
    else if (key == "dripG") m.dripG = clampD(number, 0, 10);
    else if (key == "cupG") m.cupG = clampD(number, 0, 500);
    else if (key == "cupOnScale") {
      m.cupOnScale = truthy(value);
      log("machine", m.cupOnScale ? "cup placed on the scale" : "cup lifted off the scale");
    } else if (key == "emptyCup") {
      m.liquidG = 0.0;
      log("machine", "cup emptied");
    } else if (key == "press") m.pressButton("hand");
    else if (key == "separateButtons") m.separateButtons = truthy(value);
    else if (key == "holdToBrew") m.holdToBrew = truthy(value);
    else if (key == "pressStop") m.pressStop("hand");
    else return false;
    return true;
  }
  if (device == "lunar") {
    Lunar &l = g_lunar;
    if (key == "powered") l.setPower(truthy(value));
    else if (key == "variant") {
      l.variant = static_cast<int>(clampD(number, 0, Lunar::VARIANT_COUNT - 1));
      l.dropLink("scale model changed");
    } else if (key == "rateHz") l.rateHz = clampD(number, 1, 50);
    else if (key == "noiseG") l.noiseG = clampD(number, 0, 2);
    else if (key == "heartbeatWatchdog") l.heartbeatWatchdog = truthy(value);
    else if (key == "silent") l.silent = truthy(value);
    else if (key == "splitFrames") l.splitFrames = truthy(value);
    else if (key == "battery") l.battery = static_cast<int>(clampD(number, 0, 100));
    else if (key == "ignoreTares") l.ignoreTares = static_cast<int>(clampD(number, 0, 99));
    else if (key == "rssi") l.rssi = static_cast<int>(clampD(number, -100, -20));
    else if (key == "tare") { if (l.powered) l.pressTare("button"); }
    else if (key == "dropLink") l.dropLink("fault injection");
    else return false;
    return true;
  }
  return false;
}

// A controller restart must not reset the machine, the scale or the Bot, so
// their state is handed over to the next controller process.
void worldSaveSnapshot() {
  std::lock_guard<std::recursive_mutex> lock(g_world);
  JsonDocument doc;
  fillState(doc);
  doc["savedAtMs"] = static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  std::ofstream out(snapshotPath(), std::ios::binary);
  serializeJson(doc, out);
}

void worldBegin(bool restoreSnapshot) {
  std::lock_guard<std::recursive_mutex> lock(g_world);
  g_decoy.inRange = false;  // off until switched on from the page
  g_decoy.mounted = false;
  g_decoy.rssi = -84;
  g_decoy.battery = 64;
  g_stopBot.onStopButton = true;
  g_stopBot.rssi = -63;
  g_stopBot.battery = 81;
  g_stopBot.inRange = false;  // off until switched on from the page
  g_powerBot.onPowerButton = true;
  g_powerBot.rssi = -61;
  g_powerBot.battery = 92;
  g_lunar.tareG = g_lunar.filteredG = g_machine.load();  // cup on the scale, zeroed

  std::ifstream in(snapshotPath(), std::ios::binary);
  JsonDocument doc;
  if (restoreSnapshot && in && !deserializeJson(doc, in)) {
    in.close();
    remove(snapshotPath().c_str());
    static const char *const devices[] = {"machine", "lunar", "bot", "decoy", "powerbot", "stopbot"};
    for (const char *device : devices) {
      for (JsonPair kv : doc[device].as<JsonObject>()) {
        const std::string key = kv.key().c_str();
        // Actions and counters are not settings.
        if (key == "press" || key == "tare" || key == "emptyCup" || key == "dropLink" ||
            key == "powered" || key == "variant" || key == "cupOnScale") {
          continue;
        }
        const std::string value = kv.value().is<bool>() ? (kv.value().as<bool>() ? "1" : "0")
                                  : kv.value().is<const char *>() ? kv.value().as<std::string>()
                                  : std::to_string(kv.value().as<double>());
        worldSet(device, key, value);
      }
    }
    JsonObject m = doc["machine"], l = doc["lunar"];
    g_machine.powered = m["powered"] | true;
    g_machine.cupOnScale = m["cupOnScale"] | true;
    g_machine.brewing = m["brewing"] | false;
    g_machine.brewS = m["brewS"] | 0.0;
    g_machine.flowNowGS = m["flowNowGS"] | 0.0;
    g_machine.dripLeftG = m["dripLeftG"] | 0.0;
    g_machine.liquidG = m["liquidG"] | 0.0;
    g_machine.presses = m["presses"] | 0;
    g_lunar.powered = l["powered"] | true;
    g_lunar.variant = l["variant"] | 1;
    g_lunar.tareG = l["tareG"] | 0.0;
    g_lunar.timerRunning = l["timerRunning"] | false;
    g_lunar.timerS = l["timerS"] | 0.0;
    // The pump kept running while the controller was down.
    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    double gapS = clampD((nowMs - (doc["savedAtMs"] | nowMs)) / 1000.0, 0.0, 30.0);
    for (double t = 0; t < gapS; t += 0.01) {
      g_machine.step(0.01);
      if (g_lunar.timerRunning) g_lunar.timerS += 0.01;
    }
    g_lunar.filteredG = g_machine.load();
    log("emu", "world carried over the restart (%.1f s gap, pump %s)", gapS,
        g_machine.brewing ? "still ON" : "off");
  }
  // The radio thread is the world's clock: a peripheral that never
  // advertises gives the machine its time step, ahead of the scale.
  struct MachineStepper : Peripheral {
    bool advertising() override { return false; }
    Advertisement advertisement() override { return {}; }
    std::vector<GattService> gatt() override { return {}; }
    void onWrite(const std::string &, const uint8_t *, size_t) override {}
    void step(double dt) override { g_machine.step(dt); }
  };
  static MachineStepper stepper;
  stepper.label = "machine";
  stepper.address = "-";
  radioAdd(&stepper);
  radioAdd(&g_lunar);
  radioAdd(&g_bot);
  radioAdd(&g_decoy);
  radioAdd(&g_powerBot);
  radioAdd(&g_stopBot);
}

}  // namespace emu
