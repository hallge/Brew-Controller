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
  // How far owners of this model sit from each other, in its unit. 0: not known.
  float modelSpread = 0.0f;
  // How fast they ran: grams per second over the time the owners steer by, and
  // which time that is (0 total, 1 pour, 255 not known). `kappa` is how far the
  // grind moves, in this axis' unit, for each unit of ln(flow). 0: not known.
  float flowGs = 0.0f;
  uint8_t flowBasis = 255;
  float kappa = 0.0f;
  // Which recipe of the library's this is, against the one being brewed: how far
  // apart they are (recipeDistance; 0 = the same class) and what theirs was.
  float recipeDistance = 0.0f;
  float theirDoseG = 0.0f;
  float theirYieldG = 0.0f;
  // How long off roast the coffee was, typically, when those dial-ins were
  // made. -1: not known.
  int16_t ageDays = -1;
  // What finenessUm is measured in. 0: microns of burr gap. Otherwise the id of
  // a relative axis (a library with no grinder marked in microns near this
  // coffee): its numbers mean something only to a fit on the same axis.
  uint32_t axis = 0;
};

// How this controller's grinder sits against the common scale (index/fit/...).
// Only for the calibration the grinder is under now.
struct CommunityFit {
  bool known = false;
  float scale = 0.0f;         // units of the grinder per micron of fineness
  float offset = 0.0f;        // in the grinder's unit
  uint16_t coffeesInCommon = 0;
  uint32_t axis = 0;          // the axis scale and offset are on: 0 = microns
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
  // How much of `setting` is there because this bag is older or younger
  // than the contributors' were, same unit; negative = finer. 0: no
  // allowance made (ages or the ageing rate not known).
  int16_t ageShift = 0;
  // Likewise for this controller's target flow against theirs; negative = finer.
  int16_t flowShift = 0;
  bool nearRecipe = false;    // built on a similar recipe, not the same one
};

// ------------------------------------------------------------- bean age --
// Roasted coffee goes on changing: as it ages a shot runs faster at the same
// grind, so the grind has to follow it finer. How fast is not a constant of
// nature. The library measures it from contributors who settled the same
// coffee on the same grinder at two different ages (index/age.json), and
// until it has enough of those there is no rate and no allowance is made.

struct AgeModel {
  bool known = false;
  float umPerDay = 0.0f;      // microns of burr gap per day off roast; negative = finer with age
};

// Beyond this the rate measured on younger coffee is not stretched further.
constexpr int AGE_MODEL_MAX_DAYS = 90;
constexpr float AGE_SHIFT_MAX_UM = 60.0f;

// Days from 1970-01-01 to a YYYYMMDD date.
inline int32_t daysFromDate(uint32_t date) {
  int32_t y = static_cast<int32_t>(date / 10000);
  const int32_t m = static_cast<int32_t>(date / 100 % 100), d = static_cast<int32_t>(date % 100);
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const int32_t yoe = y - era * 400;
  const int32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// Days off roast at `epochS`. -1 if the roast date or the time is not known,
// or the date is in the future.
inline int beanAgeDays(uint32_t roastDate, uint32_t epochS) {
  if (roastDate == 0 || epochS == 0) return -1;
  const int32_t age = static_cast<int32_t>(epochS / 86400) - daysFromDate(roastDate);
  return age < 0 ? -1 : static_cast<int>(age);
}

// ------------------------------------------------------- frozen coffee --
// Coffee in the freezer goes stale far more slowly than coffee on the shelf, but not at all slowly
// enough to stand still. Beanconqueror and Visualizer both keep a frozen and a thawed date for a bag;
// so does this.
//
// How much of a frozen day still counts as a day of ageing: 0.10. WHAT RESEARCH SUPPORTS
// (research/assumptions-validation.md): low temperature slows both CO2 loss and the change of
// the volatile profile (verified: Gantner et al., Foods 2024, 5 C vs 20 C; Smrke et al., JAFC 2017
// measure degassing at 35 C only). CO2 release follows an Arrhenius law with an activation energy of
// about 17-18 kJ/mol (reported for Wang & Lim, Food Res Int 2014; not checked in the primary text),
// which makes a -18 C day about a third of a 22 C day (0.33). A secondary source quotes 70 days at
// -25 C as equivalent to 2 days at 35 C, which would be about 0.05 against 22 C (not traced to a
// primary source). Degassing is only a proxy for the change that moves the grind, so this is a
// value between 0.03 and 0.33, not a measurement; 0.10 is their geometric mean. The library
// can measure it from contributors who froze a bag (frozen_days in their records) and this is the
// one number to replace then. Not modelled: grinding straight from the freezer gives a finer
// grind than the same beans warm (Uman et al., Sci Rep 2016: cold beans, narrower distribution and
// smaller mean particle size), so a bag ground frozen needs a coarser setting than the age alone says.
constexpr float FROZEN_AGING_FRACTION = 0.10f;

// Days the bag had spent frozen by `epochS`: from frozenDate to thawDate, or to
// `epochS` if it is still frozen. 0 if it was not frozen, the dates do not
// make sense, or the time is not known.
inline int frozenDaysAt(uint32_t frozenDate, uint32_t thawDate, uint32_t epochS) {
  if (frozenDate == 0 || epochS == 0) return 0;
  const int32_t now = static_cast<int32_t>(epochS / 86400);
  const int32_t from = daysFromDate(frozenDate);
  int32_t to = thawDate != 0 ? daysFromDate(thawDate) : now;
  if (to > now) to = now;  // a thaw date in the future: still frozen today
  return to > from ? static_cast<int>(to - from) : 0;
}

// The bag is in the freezer at `epochS`.
inline bool frozenAt(uint32_t frozenDate, uint32_t thawDate, uint32_t epochS) {
  if (frozenDate == 0 || epochS == 0) return false;
  const int32_t now = static_cast<int32_t>(epochS / 86400);
  if (now < daysFromDate(frozenDate)) return false;
  return thawDate == 0 || now < daysFromDate(thawDate);
}

// Days off roast that count: the calendar age less the days in the freezer.
// -1 where beanAgeDays is.
inline int effectiveAgeDays(uint32_t roastDate, uint32_t frozenDate, uint32_t thawDate,
                            uint32_t epochS) {
  const int age = beanAgeDays(roastDate, epochS);
  if (age < 0) return -1;
  const int frozen = frozenDaysAt(frozenDate, thawDate, epochS);
  const int counted = age - static_cast<int>(static_cast<float>(frozen) *
                                              (1.0f - FROZEN_AGING_FRACTION) + 0.5f);
  return counted < 0 ? 0 : counted;
}

// How far the grind should move, in microns, for coffee going from one age to
// another. 0 if either age or the rate is unknown.
inline float ageShiftUm(const AgeModel &model, int fromDays, int toDays) {
  if (!model.known || fromDays < 0 || toDays < 0) return 0.0f;
  if (fromDays > AGE_MODEL_MAX_DAYS) fromDays = AGE_MODEL_MAX_DAYS;
  if (toDays > AGE_MODEL_MAX_DAYS) toDays = AGE_MODEL_MAX_DAYS;
  float shift = model.umPerDay * static_cast<float>(toDays - fromDays);
  if (!isfinite(shift)) return 0.0f;
  if (shift > AGE_SHIFT_MAX_UM) shift = AGE_SHIFT_MAX_UM;
  if (shift < -AGE_SHIFT_MAX_UM) shift = -AGE_SHIFT_MAX_UM;
  return shift;
}

// Units of this grinder's setting per micron, for turning a shift in microns
// into a number to dial. 0: not known.
inline float grinderUnitsPerUm(const CommunityFit &fit, const Grinder &grinder) {
  // A fit on a relative axis counts units per axis step, not per micron.
  if (fit.known && fit.scale > 0.0f && fit.axis == 0) return fit.scale;
  return grinder.umPerUnit > 0.0f ? 1.0f / grinder.umPerUnit : 0.0f;
}

// Units of this grinder's setting per step of the axis the library's fineness is on.
// 0: not known (no fit on that axis, and a relative axis cannot be read any other way).
inline float grinderUnitsPerAxisStep(uint32_t axis, const CommunityFit &fit, const Grinder &grinder) {
  if (fit.known && fit.scale > 0.0f && fit.axis == axis) return fit.scale;
  if (axis == 0 && grinder.umPerUnit > 0.0f) return 1.0f / grinder.umPerUnit;
  return 0.0f;
}

// How far to move the grind to brew at `myFlowGs` rather than `theirFlowGs`, in axis steps:
// a faster shot wants a coarser grind. Limited to a flow ratio of 1.65 either way, and 0
// unless both flows are on the same time basis and the sensitivity is known.
constexpr float FLOW_SHIFT_MAX_LN = 0.5f;
inline float flowShiftAxis(const CommunityDialin &dialin, float myFlowGs, uint8_t myBasis) {
  if (dialin.kappa <= 0.0f || dialin.flowGs <= 0.0f || myFlowGs <= 0.0f) return 0.0f;
  if (dialin.flowBasis == 255 || dialin.flowBasis != myBasis) return 0.0f;
  float ln = logf(myFlowGs / dialin.flowGs);
  if (!isfinite(ln)) return 0.0f;
  if (ln > FLOW_SHIFT_MAX_LN) ln = FLOW_SHIFT_MAX_LN;
  if (ln < -FLOW_SHIFT_MAX_LN) ln = -FLOW_SHIFT_MAX_LN;
  return dialin.kappa * ln;
}

// Two recipes close enough that the same grind suits both: half a gram of
// dose, two grams of yield.
inline bool sameRecipeClass(float doseA, float yieldA, float doseB, float yieldB) {
  return fabsf(doseA - doseB) <= 0.5f && fabsf(yieldA - yieldB) <= 2.0f;
}

// How far a recipe the library has is from the one being brewed, so that a 40 g yield can
// use what was settled at 36 g instead of finding nothing. 0 for the same class; up to 2
// at the limits of reach (2 g of dose, a yield ratio of about 1.35); -1 when out of reach.
// A different yield mostly changes how long a shot runs, not the grind it needs, and the
// flow shift (flowShiftAxis) takes that into account; dose is not corrected, so a
// farther recipe is trusted less (communityStart widens the range by the distance).
constexpr float RECIPE_REACH_DOSE_G = 2.0f;
constexpr float RECIPE_REACH_LN_YIELD = 0.3f;
inline float recipeDistance(float dose, float yield, float otherDose, float otherYield) {
  if (dose <= 0.0f || yield <= 0.0f || otherDose <= 0.0f || otherYield <= 0.0f) return -1.0f;
  if (sameRecipeClass(dose, yield, otherDose, otherYield)) return 0.0f;
  const float dDose = fabsf(dose - otherDose) / RECIPE_REACH_DOSE_G;
  const float dYield = fabsf(logf(yield / otherYield)) / RECIPE_REACH_LN_YIELD;
  if (!isfinite(dDose + dYield) || dDose > 1.0f || dYield > 1.0f) return -1.0f;
  return dDose + dYield;
}

// myAgeDays: how long off roast this controller's bag is, -1 if not known.
// myFlowGs, myFlowBasis: the flow this controller's recipe aims for (yield over target time) and
// the time it is measured on (0 total, 1 pour); 0 if not known.
inline CommunityStart communityStart(const CommunityDialin &dialin, const CommunityFit &fit,
                                     const Grinder &grinder, const AgeModel &age = AgeModel{},
                                     int myAgeDays = -1, float myFlowGs = 0.0f,
                                     uint8_t myFlowBasis = 255) {
  CommunityStart start;
  if (!dialin.found || dialin.contributors == 0) return start;
  float setting = 0.0f, range = 0.0f;
  // A fit only reads a fineness on its own axis. One from another axis is no
  // better than none.
  if (fit.known && fit.scale > 0.0f && fit.coffeesInCommon > 0 && dialin.finenessUm > 0.0f &&
      fit.axis == dialin.axis) {
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
    // Two grinders of one model are not zeroed alike. How much is measured where the
    // library has enough owners; failing that, 15 microns on a grinder marked in them, and
    // otherwise a sixth of the setting: an unmarked dial is not to be offered a tight range
    // on no evidence.
    if (dialin.modelSpread > 0.0f) {
      range = 1.5f * dialin.modelSpread;
      const float floorUnits = grinder.umPerUnit > 0.0f ? 5.0f / grinder.umPerUnit : 1.0f;
      if (range < floorUnits) range = floorUnits;
    } else if (grinder.umPerUnit > 0.0f) {
      range = 15.0f / grinder.umPerUnit;
    } else {
      range = setting / 6.0f;
    }
  } else if (grinder.umPerUnit > 0.0f && dialin.finenessUm > 0.0f && dialin.axis == 0) {
    start.basis = StartBasis::MICRONS;
    setting = dialin.finenessUm / grinder.umPerUnit;
    range = 25.0f / grinder.umPerUnit;  // nothing known about this grinder's zero
  } else {
    return start;
  }
  // This bag against theirs: older coffee wants a finer grind.
  // The ageing rate is in microns, so it only applies to a fineness in microns.
  const float shift = dialin.axis != 0
                          ? 0.0f
                          : grinderUnitsPerUm(fit, grinder) * ageShiftUm(age, dialin.ageDays, myAgeDays);
  setting += shift;
  // And this controller's target flow against the one theirs were settled at.
  const float flowUnits = grinderUnitsPerAxisStep(dialin.axis, fit, grinder);
  const float flowShift = flowUnits * flowShiftAxis(dialin, myFlowGs, myFlowBasis);
  setting += flowShift;
  if (!isfinite(setting) || setting < 1.0f || setting > 2000.0f) return CommunityStart{};
  start.setting = static_cast<uint16_t>(setting + 0.5f);
  start.ageShift = static_cast<int16_t>(shift < 0 ? shift - 0.5f : shift + 0.5f);
  start.flowShift = static_cast<int16_t>(flowShift < 0 ? flowShift - 0.5f : flowShift + 0.5f);
  range += (flowShift < 0 ? -flowShift : flowShift) * 0.25f;
  // A recipe that is not quite this one: 3% of the setting for each step of distance.
  if (dialin.recipeDistance > 0.0f) {
    range += setting * 0.03f * dialin.recipeDistance;
    start.nearRecipe = true;
  }
  // The further the allowance for age reaches, the less it can be relied on.
  range += (shift < 0 ? -shift : shift) * 0.5f;
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
