#pragma once
// What the shared library says about a coffee, turned into a grind setting
// to start from on this controller's grinder (docs/COMMUNITY.md, section 6).
//
// The library works out, from everyone's settled dial-ins, a "fineness" for
// each coffee and recipe on one common scale (microns of burr gap), and for
// each contributor's grinder how its settings sit against that scale:
//
//     setting = scale x fineness + offset
//
// A suggestion is never more than a place to start. The local grind advice
// takes it from there, steering by brew time.
//
// Portable and host-tested; no flash, network or JSON in here.

#include <math.h>

#include "Types.h"

namespace bbw {

// What the library has for one coffee and one recipe (index/dialin/...).
struct CommunityDialin {
  bool found = false;
  uint16_t contributors = 0;
  uint16_t dialins = 0;
  float timeS = 0.0f;         // brew time they settled at
  float finenessUm = 0.0f;    // on the common scale
  float spreadUm = 0.0f;      // how far contributors sit from it, typically
  // Settings of contributors with the same grinder model as this one, in
  // that model's own unit.
  float modelMedian = 0.0f;
  uint16_t modelCount = 0;
};

// How this controller's grinder sits against the common scale (index/fit/...).
// Only for the calibration the grinder is under now.
struct CommunityFit {
  bool known = false;
  float scale = 0.0f;         // units of the grinder per micron of fineness
  float offset = 0.0f;        // in the grinder's unit
  uint16_t coffeesInCommon = 0;
};

enum class StartBasis : uint8_t {
  NONE = 0,     // nothing to go on
  FITTED,       // this grinder's own scale and offset, from coffees in common
  SAME_MODEL,   // what others with the same grinder model set
  MICRONS,      // the common scale read straight off, no offset known
};

struct CommunityStart {
  StartBasis basis = StartBasis::NONE;
  uint16_t setting = 0;       // in the grinder's unit, 0 = none
  uint16_t range = 0;         // plus or minus, same unit
};

// Two recipes close enough that the same grind suits both: half a gram of
// dose, two grams of yield.
inline bool sameRecipeClass(float doseA, float yieldA, float doseB, float yieldB) {
  return fabsf(doseA - doseB) <= 0.5f && fabsf(yieldA - yieldB) <= 2.0f;
}

inline CommunityStart communityStart(const CommunityDialin &dialin, const CommunityFit &fit,
                                     const Grinder &grinder) {
  CommunityStart start;
  if (!dialin.found || dialin.contributors == 0) return start;
  float setting = 0.0f, range = 0.0f;
  if (fit.known && fit.scale > 0.0f && fit.coffeesInCommon > 0 && dialin.finenessUm > 0.0f) {
    start.basis = StartBasis::FITTED;
    setting = fit.scale * dialin.finenessUm + fit.offset;
    // However well the contributors agree with each other, this grinder's
    // own offset is only as good as the coffees it rests on: one coffee in
    // common pins it on a single point.
    float um = dialin.spreadUm;
    if (fit.coffeesInCommon < 2) um *= 2.0f;
    const float floorUm = fit.coffeesInCommon < 2 ? 10.0f : 5.0f;
    if (um < floorUm) um = floorUm;
    range = fit.scale * um;
  } else if (dialin.modelCount > 0 && dialin.modelMedian > 0.0f) {
    start.basis = StartBasis::SAME_MODEL;
    setting = dialin.modelMedian;
    // Two grinders of one model are not zeroed alike.
    range = grinder.umPerUnit > 0.0f ? 15.0f / grinder.umPerUnit : 0.0f;
  } else if (grinder.umPerUnit > 0.0f && dialin.finenessUm > 0.0f) {
    start.basis = StartBasis::MICRONS;
    setting = dialin.finenessUm / grinder.umPerUnit;
    range = 25.0f / grinder.umPerUnit;  // nothing known about this grinder's zero
  } else {
    return start;
  }
  if (!isfinite(setting) || setting < 1.0f || setting > 2000.0f) return CommunityStart{};
  start.setting = static_cast<uint16_t>(setting + 0.5f);
  if (range < 1.0f) range = 1.0f;
  if (range > 200.0f) range = 200.0f;
  start.range = static_cast<uint16_t>(range + 0.5f);
  return start;
}

inline const char *startBasisName(StartBasis basis) {
  switch (basis) {
    case StartBasis::FITTED: return "fitted";
    case StartBasis::SAME_MODEL: return "same_model";
    case StartBasis::MICRONS: return "microns";
    default: return "none";
  }
}

// A library id ("portrait/idjwi-island") as a file name ("portrait__idjwi-island").
// Anything but lower-case letters, digits, '-' and '/' means it is not one.
inline bool libraryIdToFile(const char *id, char *out, size_t size) {
  size_t n = 0;
  if (!id || !id[0]) return false;
  for (const char *p = id; *p; ++p) {
    const char c = *p;
    const bool plain = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!plain && c != '/') return false;
    if (n + (plain ? 1 : 2) >= size) return false;
    if (plain) {
      out[n++] = c;
    } else {
      out[n++] = '_';
      out[n++] = '_';
    }
  }
  out[n] = '\0';
  return true;
}

}  // namespace bbw
