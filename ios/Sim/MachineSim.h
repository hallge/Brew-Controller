#pragma once
// A portable copy of the host tests' simulated machine, for the iOS spike's demo mode and its tests.
//
// Adapted from test/host/test_main.cpp (the file itself is untouched):
//   - struct Sim      from line 63 ("The simulation": a Breville + Lunar + Bot world: machine,
//                     scale, Bot models and fault switches);
//   - lunarFrame(), acaiaChecksum() and wireFrame() from lines 366 and 512-607 (a reading as each
//                     supported scale sends it, bytes and all).
// Changes from the original, all marked "iOS:":
//   - wrapped in namespace brewsim, `static` made `inline` (it is a header);
//   - two added fault switches: missNextStop (the stop press is taken but the machine ignores it)
//     and the helper armLostAck().
// Keep it in step with the original by hand; it has no behaviour of its own beyond those.
//
// Pure C++17, no Apple headers: it also compiles on the PC.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ScaleProtocol.h"
#include "core/BrewController.h"
#include "core/ScaleLink.h"

namespace brewsim {
using namespace bbw;

inline size_t wireFrame(const char *id, double weightG, uint32_t timerMs, uint8_t *out);

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
  bool missNextStop = false;    // iOS: the next press made while the machine runs is ignored (Bot says OK)
  void armLostAck() { unconfirmedPresses = 1; }  // iOS: the next press's acknowledgement is lost
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
    if (missNextStop && machineRunning) {   // iOS: the stop press is taken, the machine ignores it
      missNextStop = false;
    } else if (ignorePresses > 0) {
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


// ---- test/host/test_main.cpp:366 ----
inline void acaiaChecksum(uint8_t *p, int length) {
  uint8_t even = 0, odd = 0;
  for (int i = 3, k = 0; i < length - 2; ++i, ++k) ((k & 1) ? odd : even) += p[i];
  p[length - 2] = even;
  p[length - 1] = odd;
}

// ---- test/host/test_main.cpp:509-607 ----
// A 2021+ Lunar weight frame: ef dd 0c <len> 05 <weight, 4 bytes LE> <unit>
// <flags> [07 <minutes> <seconds> <tenths>] <cks> <cks>. timerTenths < 0: the
// 13-byte frame without a timer record.
inline std::vector<uint8_t> lunarFrame(uint32_t raw, uint8_t unit, uint8_t flags, int timerTenths) {
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

inline size_t wireFrame(const char *id, double weightG, uint32_t timerMs, uint8_t *out) {
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

}  // namespace brewsim
