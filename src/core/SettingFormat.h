#pragma once
// How a grinder's setting is written down and read back.
//
// Every setting is stored as a whole number of the grinder's own smallest step (Preset::grindUm,
// ShotRecord::grindUm, Recipe::grindUm), 1 to 2000; 0 means "not entered". What that number looks
// like to the person turning the dial depends on the grinder:
//
//   plain       divisor 1, no rings        246          a number of whole steps (microns, clicks, notches)
//   fractional  divisor 2, 4, 5, 10 ...    3.5, 4.25    a dial read in halves, quarters, tenths
//   two rings   subSteps N >= 2            12:3         an outer ring and an inner one (macro and micro
//                                                       steps, as on a Baratza Sette); the inner one
//                                                       has N steps per outer step
//
// The stored number is what the advisor, the fit and the shared library work on, so all of them keep
// working in whole steps; only what is shown and typed changes. Microns per unit (Grinder::umPerUnit)
// is per STORED step. The divisor has to divide 100 so that a decimal can always be written exactly.
//
// Portable and host-tested; no flash or UI access in here.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Types.h"

namespace bbw {

constexpr uint16_t SETTING_MAX = 2000;

inline bool validDivisor(uint8_t d) {
  switch (d) {
    case 1: case 2: case 4: case 5: case 10: case 20: case 25: case 50: case 100: return true;
    default: return false;
  }
}

// Decimal places needed to write a multiple of 1/d exactly: 0 for 1; 1 for 2, 5, 10; 2 for the rest.
inline uint8_t divisorDecimals(uint8_t d) {
  if (d <= 1) return 0;
  return (d == 2 || d == 5 || d == 10) ? 1 : 2;
}

// Puts a grinder's way of reading in order: a divisor that divides 100, rings of 2 to 99 inner steps,
// and no divisor on a ringed dial.
inline void sanitizeSettingFormat(Grinder &g) {
  if (!validDivisor(g.divisor)) g.divisor = 1;
  if (g.subSteps == 1 || g.subSteps > 99) g.subSteps = 0;
  if (g.subSteps >= 2) g.divisor = 1;
}

inline bool settingIsRinged(const Grinder &g) { return g.subSteps >= 2; }

// The setting as the dial reads, without its unit. "" for 0 (not entered). Returns the length.
inline size_t formatSetting(uint16_t value, const Grinder &g, char *out, size_t size) {
  if (!out || size == 0) return 0;
  out[0] = '\0';
  if (value == 0) return 0;
  int n;
  if (settingIsRinged(g)) {
    const unsigned macro = (value - 1u) / g.subSteps + 1u, micro = (value - 1u) % g.subSteps + 1u;
    n = snprintf(out, size, "%u:%u", macro, micro);
  } else if (g.divisor > 1) {
    const uint8_t places = divisorDecimals(g.divisor);
    const unsigned whole = value / g.divisor, rest = value % g.divisor;
    const unsigned scale = places == 1 ? 10u : 100u;
    n = snprintf(out, size, "%u.%0*u", whole, places, rest * (scale / g.divisor));
  } else {
    n = snprintf(out, size, "%u", static_cast<unsigned>(value));
  }
  return n > 0 ? static_cast<size_t>(n) : 0;
}

// A change of setting as the dial counts it: "5", "1.5", "2 steps"-style numbers without the unit.
inline size_t formatSettingDelta(int delta, const Grinder &g, char *out, size_t size) {
  if (!out || size == 0) return 0;
  const unsigned a = static_cast<unsigned>(delta < 0 ? -delta : delta);
  int n;
  if (!settingIsRinged(g) && g.divisor > 1) {
    const uint8_t places = divisorDecimals(g.divisor);
    const unsigned scale = places == 1 ? 10u : 100u;
    n = snprintf(out, size, "%u.%0*u", a / g.divisor, places, (a % g.divisor) * (scale / g.divisor));
  } else {
    n = snprintf(out, size, "%u", a);   // on a ringed dial: inner steps
  }
  return n > 0 ? static_cast<size_t>(n) : 0;
}

// What to call a change of setting: the grinder's unit, or "steps" on a ringed dial (where the unit
// names the whole reading, not one inner step).
inline const char *settingDeltaUnit(const Grinder &g) { return settingIsRinged(g) ? "steps" : g.unit; }

// Reads what was typed. "246", "3.5" or "3,5" (fractional), "12:3", "12-3" or "12/3" (ringed). A
// fraction is rounded to the nearest step the dial has. false if it is not a setting this grinder can
// have (not a number, a ring position out of range, 0, or over SETTING_MAX).
inline bool parseSetting(const char *text, const Grinder &g, uint16_t &out) {
  if (!text) return false;
  while (*text == ' ') ++text;
  if (!*text) return false;
  if (settingIsRinged(g)) {
    char *end = nullptr;
    const long macro = strtol(text, &end, 10);
    if (end == text || macro < 1) return false;
    long micro = 1;
    if (*end == ':' || *end == '-' || *end == '/') {
      const char *rest = end + 1;
      micro = strtol(rest, &end, 10);
      if (end == rest) return false;
    }
    while (*end == ' ') ++end;
    if (*end || micro < 1 || micro > g.subSteps) return false;
    const long v = (macro - 1) * g.subSteps + micro;
    if (v < 1 || v > SETTING_MAX) return false;
    out = static_cast<uint16_t>(v);
    return true;
  }
  char buf[24];
  size_t n = 0;
  for (const char *p = text; *p && n < sizeof(buf) - 1; ++p) buf[n++] = (*p == ',') ? '.' : *p;
  buf[n] = '\0';
  char *end = nullptr;
  const double d = strtod(buf, &end);
  while (end && *end == ' ') ++end;
  if (end == buf || (end && *end) || !isfinite(d) || d <= 0.0) return false;
  const double v = floor(d * g.divisor + 0.5);
  if (v < 1.0 || v > SETTING_MAX) return false;
  out = static_cast<uint16_t>(v);
  return true;
}

// A grinder's unit as the shared library's index groups it: a dial read in tenths and one read in
// whole steps are not the same unit even when both are called "dial". Keep equal to unit_key() in
// library-template/tools/build_index.py.
inline size_t grinderUnitKey(const Grinder &g, char *out, size_t size) {
  if (!out || size == 0) return 0;
  int n;
  if (g.subSteps >= 2) n = snprintf(out, size, "%s/r%u", g.unit, static_cast<unsigned>(g.subSteps));
  else if (g.divisor > 1) n = snprintf(out, size, "%s/d%u", g.unit, static_cast<unsigned>(g.divisor));
  else n = snprintf(out, size, "%s", g.unit);
  return n > 0 ? static_cast<size_t>(n) : 0;
}

}  // namespace bbw
