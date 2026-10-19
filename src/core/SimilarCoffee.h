#pragma once
// A place to start the grinder for a coffee the owner has not dialled in yet, taken from the
// owner's own most similar coffee.
//
// Visualizer.coffee does this for a new bag (app/models/coffee_bag/grind_suggestable.rb): it
// finds the user's dial-in of the most similar earlier coffee on the same grinder and profile
// and says how the new one should be ground relative to it (much finer ... much coarser),
// asking a language model to judge roast, density, altitude and process. Here the neighbour is
// found by rules and the size of any shift is MEASURED, from the owner's own coffees.
//
// WHAT RESEARCH COULD AND COULD NOT CONFIRM (research/assumptions-validation.md):
//   * Roast: at a fixed setting a darker roast grinds to a finer particle size (one
//     uncontrolled EK43 experiment: median 305 -> 120 um from just past first crack to past
//     second crack) and its shots run slower, which points to a COARSER setting for a darker
//     roast. Direction supported by one informal test and by practitioner convention; no size
//     that converts to a grinder setting.
//   * Process: Uman et al. 2016 (Scientific Reports 6:24483) found the particle size
//     distribution independent of bean origin and processing method. Advice on natural vs
//     washed is contradictory and one logged home dataset (993 brews) found no extraction
//     difference. Not supported as a grind factor.
//   * Altitude: higher-grown coffee is denser (two studies), but no study ties density to the
//     espresso grind setting, and Uman et al. found grinder output independent of origin.
//     Practitioner claim only.
//   * No source gives any factor's size in grinder units. The earlier 3%-of-setting step was a
//     guess and is gone.
//
// So: the neighbour's own setting is the starting point, with a range taken from the scatter of
// the owner's own settings. A factor moves the number only when the owner's own coffees say so:
// a ridge regression of ln(setting) on roast level, altitude and process, fitted to their other
// dialled coffees on this grinder and a close recipe, needing at least two more coffees than
// factors, shrunk towards zero, and keeping a factor only if its sign matches the convention
// (darker coarser, higher-grown finer, natural coarser). Without that, the page is given the
// direction as a hint ("darker than X: coffees like this are usually ground coarser") and the
// number is not moved. It is always marked unverified.
//
// Portable and host-tested; no flash, network or JSON in here.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "Community.h"
#include "Types.h"

namespace bbw {

// How a coffee was processed, as far as it can be read from free text: how much of the fruit
// the bean dried inside.
enum class ProcessClass : int8_t { UNKNOWN = -1, WASHED = 0, HONEY = 1, NATURAL = 2 };

inline bool textHas(const char *text, const char *word) {
  if (!text || !word || !*word) return false;
  const size_t n = strlen(word);
  for (const char *p = text; *p; ++p) {
    size_t i = 0;
    while (i < n && p[i] && (p[i] | 0x20) == (word[i] | 0x20)) ++i;
    if (i == n) return true;
  }
  return false;
}

inline ProcessClass processClass(const char *process) {
  if (!process || !process[0]) return ProcessClass::UNKNOWN;
  if (textHas(process, "natural") || textHas(process, "dry") || textHas(process, "anaerob") ||
      textHas(process, "carbonic") || textHas(process, "ferment")) {
    return ProcessClass::NATURAL;
  }
  if (textHas(process, "honey") || textHas(process, "pulped") || textHas(process, "semi")) {
    return ProcessClass::HONEY;
  }
  if (textHas(process, "wash") || textHas(process, "wet")) return ProcessClass::WASHED;
  return ProcessClass::UNKNOWN;
}

// One of the owner's other coffees that has a grind setting for a recipe like this one.
struct SimilarCandidate {
  const Coffee *coffee = nullptr;
  uint16_t grindUm = 0;      // its setting, in this grinder's unit
  float doseG = 0.0f;        // the recipe it was set for
  float yieldG = 0.0f;
  bool settled = false;      // that setting was last sent to the library as a settled dial-in
};

enum SimilarFactor : uint8_t { SIMILAR_ROAST = 1, SIMILAR_ALTITUDE = 2, SIMILAR_PROCESS = 4 };

struct SimilarStart {
  bool found = false;
  uint16_t setting = 0;          // where to start, in the grinder's unit
  uint16_t range = 0;            // plus or minus, same unit
  uint16_t fromSetting = 0;      // the nearest coffee's setting
  uint16_t fromCoffeeId = 0;
  char fromName[COFFEE_NAME_LEN] = {};
  int16_t shift = 0;             // setting - fromSetting; negative = finer
  bool fitted = false;           // the shift is measured from the owner's own coffees
  uint8_t samples = 0;           // coffees the fit or the range rests on
  uint8_t applied = 0;           // SimilarFactor bits that moved the number
  // How this coffee sits against the nearest one, from the usual direction of each factor
  // (-1 finer expected, 0 none known, +1 coarser expected). A hint only unless in `applied`.
  int8_t hintRoast = 0, hintAltitude = 0, hintProcess = 0;
  float distance = 0.0f;         // how unlike they are, lower = more alike
  bool nearRecipe = false;       // its recipe is close to this one, not the same class
  bool fromSettled = false;
};

constexpr float SIMILAR_MAX_DISTANCE = 4.0f;       // beyond this nothing is similar enough
constexpr size_t SIMILAR_MIN_KNOWN = 2;            // attributes known on both sides
constexpr float SIMILAR_ALTITUDE_UNIT_M = 500.0f;  // altitude enters the fit in these
constexpr float SIMILAR_ALTITUDE_HINT_M = 300.0f;  // a difference worth a hint
constexpr float SIMILAR_RIDGE = 1.0f;              // shrinkage towards "no effect"
constexpr float SIMILAR_NO_DATA_FRACTION = 0.10f;  // range with nothing to measure it by: wide on purpose

inline bool sameText(const char *a, const char *b) {
  if (!a || !b) return false;
  while (*a && *b && (*a | 0x20) == (*b | 0x20)) { ++a; ++b; }
  return *a == '\0' && *b == '\0';
}

// How alike two coffees are (lower = more alike), and how many attributes both have.
struct SimilarComparison {
  float distance = 0.0f;
  uint8_t known = 0;
};

inline SimilarComparison compareCoffees(const Coffee &target, const Coffee &other) {
  SimilarComparison c;
  if (target.roastLevel != 0 && other.roastLevel != 0) {
    c.distance += fabsf(static_cast<float>(other.roastLevel) - static_cast<float>(target.roastLevel));
    ++c.known;
  } else {
    c.distance += 1.5f;
  }
  const ProcessClass tp = processClass(target.process), op = processClass(other.process);
  if (tp != ProcessClass::UNKNOWN && op != ProcessClass::UNKNOWN) {
    c.distance += fabsf(static_cast<float>(op) - static_cast<float>(tp));
    ++c.known;
  } else {
    c.distance += 0.75f;
  }
  if (target.altitudeM > 0 && other.altitudeM > 0) {
    const float d = fabsf(static_cast<float>(target.altitudeM) - static_cast<float>(other.altitudeM));
    c.distance += d / 300.0f < 2.0f ? d / 300.0f : 2.0f;
    ++c.known;
  } else {
    c.distance += 0.5f;
  }
  if (target.origin[0] && other.origin[0]) {
    c.distance += sameText(target.origin, other.origin) ? 0.0f : 0.75f;
    ++c.known;
  } else {
    c.distance += 0.25f;
  }
  return c;
}

inline size_t similarBits(uint8_t mask) {
  size_t n = 0;
  for (; mask; mask >>= 1) n += mask & 1u;
  return n;
}

// Solves the n x n system a x = b in place (n <= 3, a symmetric positive definite). false if singular.
inline bool similarSolve(float a[3][3], float b[3], size_t n) {
  for (size_t i = 0; i < n; ++i) {
    size_t pivot = i;
    for (size_t r = i + 1; r < n; ++r) if (fabsf(a[r][i]) > fabsf(a[pivot][i])) pivot = r;
    if (fabsf(a[pivot][i]) < 1e-9f) return false;
    if (pivot != i) {
      for (size_t c = 0; c < n; ++c) { const float t = a[i][c]; a[i][c] = a[pivot][c]; a[pivot][c] = t; }
      const float t = b[i]; b[i] = b[pivot]; b[pivot] = t;
    }
    for (size_t r = i + 1; r < n; ++r) {
      const float f = a[r][i] / a[i][i];
      for (size_t c = i; c < n; ++c) a[r][c] -= f * a[i][c];
      b[r] -= f * b[i];
    }
  }
  for (size_t i = n; i-- > 0;) {
    float v = b[i];
    for (size_t c = i + 1; c < n; ++c) v -= a[i][c] * b[c];
    b[i] = v / a[i][i];
  }
  return true;
}

// The factors of one coffee as the fit sees them.
struct SimilarX {
  float v[3];     // roast level, altitude in units of SIMILAR_ALTITUDE_UNIT_M, process class
  bool has[3];
};

inline SimilarX similarX(const Coffee &c) {
  SimilarX x = {{0, 0, 0}, {false, false, false}};
  if (c.roastLevel != 0) { x.v[0] = static_cast<float>(c.roastLevel); x.has[0] = true; }
  if (c.altitudeM > 0) { x.v[1] = static_cast<float>(c.altitudeM) / SIMILAR_ALTITUDE_UNIT_M; x.has[1] = true; }
  const ProcessClass p = processClass(c.process);
  if (p != ProcessClass::UNKNOWN) { x.v[2] = static_cast<float>(p); x.has[2] = true; }
  return x;
}

// The owner's other coffees against this one. target's own setting is not asked for: this is
// for a coffee with none. dose, yield: the recipe about to be brewed.
inline SimilarStart similarStart(const Coffee &target, float doseG, float yieldG,
                                 const SimilarCandidate *candidates, size_t count,
                                 const Grinder &grinder) {
  SimilarStart out;
  // 1. The nearest coffee.
  float bestRank = 1e9f;
  const SimilarCandidate *best = nullptr;
  SimilarComparison bestCompare;
  float bestRecipe = 0.0f;
  for (size_t i = 0; i < count; ++i) {
    const SimilarCandidate &c = candidates[i];
    if (!c.coffee || c.grindUm == 0 || c.coffee->id == target.id) continue;
    const float recipe = recipeDistance(doseG, yieldG, c.doseG, c.yieldG);
    if (recipe < 0.0f) continue;                    // too different a recipe to carry a grind over
    const SimilarComparison cmp = compareCoffees(target, *c.coffee);
    if (cmp.known < SIMILAR_MIN_KNOWN || cmp.distance > SIMILAR_MAX_DISTANCE) continue;
    const float rank = cmp.distance + 0.5f * recipe - (c.settled ? 0.1f : 0.0f);
    if (rank < bestRank) { bestRank = rank; best = &c; bestCompare = cmp; bestRecipe = recipe; }
  }
  if (!best) return out;

  const SimilarX tx = similarX(target), bx = similarX(*best->coffee);
  const float from = static_cast<float>(best->grindUm);

  // 2. The owner's own coffees (the nearest included) as data: ln(setting) against the factors.
  struct Point { SimilarX x; float y; };
  Point pts[16];
  size_t n = 0;
  for (size_t i = 0; i < count && n < 16; ++i) {
    const SimilarCandidate &c = candidates[i];
    if (!c.coffee || c.grindUm == 0 || c.coffee->id == target.id) continue;
    if (recipeDistance(doseG, yieldG, c.doseG, c.yieldG) < 0.0f) continue;
    pts[n].x = similarX(*c.coffee);
    pts[n].y = logf(static_cast<float>(c.grindUm));
    ++n;
  }

  // 3. The richest model the data supports: a set of factors that the target has, that vary among
  //    the points having them all, with at least two more points than factors. The expected sign
  //    of each factor (darker coarser, higher-grown finer, natural coarser) is the convention; a
  //    fitted factor of the other sign is not applied.
  const float expected[3] = {+1.0f, -1.0f, +1.0f};
  float bestCoef[3] = {0, 0, 0};
  uint8_t bestMask = 0;
  size_t bestN = 0;
  float bestSigma = 0.0f;
  size_t bestDof = 0;
  for (uint8_t mask = 1; mask < 8; ++mask) {
    size_t k = 0;
    int idx[3];
    bool usable = true;
    for (int f = 0; f < 3; ++f) {
      if (!(mask & (1 << f))) continue;
      if (!tx.has[f]) { usable = false; break; }
      idx[k++] = f;
    }
    if (!usable) continue;
    Point sub[16];
    size_t m = 0;
    for (size_t i = 0; i < n; ++i) {
      bool all = true;
      for (size_t j = 0; j < k; ++j) all = all && pts[i].x.has[idx[j]];
      if (all) sub[m++] = pts[i];
    }
    if (m < k + 2) continue;
    // each factor must vary among them (a level, 300 m, or a process class apart)
    for (size_t j = 0; j < k && usable; ++j) {
      float lo = sub[0].x.v[idx[j]], hi = lo;
      for (size_t i = 1; i < m; ++i) {
        if (sub[i].x.v[idx[j]] < lo) lo = sub[i].x.v[idx[j]];
        if (sub[i].x.v[idx[j]] > hi) hi = sub[i].x.v[idx[j]];
      }
      const float need = idx[j] == 1 ? SIMILAR_ALTITUDE_HINT_M / SIMILAR_ALTITUDE_UNIT_M : 1.0f;
      usable = (hi - lo) >= need - 1e-4f;
    }
    if (!usable) continue;
    // centred ridge regression
    float mx[3] = {0, 0, 0}, my = 0;
    for (size_t i = 0; i < m; ++i) {
      my += sub[i].y;
      for (size_t j = 0; j < k; ++j) mx[j] += sub[i].x.v[idx[j]];
    }
    my /= static_cast<float>(m);
    for (size_t j = 0; j < k; ++j) mx[j] /= static_cast<float>(m);
    float a3[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, b3[3] = {0, 0, 0};
    for (size_t i = 0; i < m; ++i) {
      float xc[3];
      for (size_t j = 0; j < k; ++j) xc[j] = sub[i].x.v[idx[j]] - mx[j];
      for (size_t j = 0; j < k; ++j) {
        b3[j] += xc[j] * (sub[i].y - my);
        for (size_t l = 0; l < k; ++l) a3[j][l] += xc[j] * xc[l];
      }
    }
    for (size_t j = 0; j < k; ++j) a3[j][j] += SIMILAR_RIDGE;
    float coef[3] = {b3[0], b3[1], b3[2]};
    if (!similarSolve(a3, coef, k)) continue;
    float rss = 0.0f;
    for (size_t i = 0; i < m; ++i) {
      float fit = my;
      for (size_t j = 0; j < k; ++j) fit += coef[j] * (sub[i].x.v[idx[j]] - mx[j]);
      rss += (sub[i].y - fit) * (sub[i].y - fit);
    }
    const size_t dof = m - k - 1;
    const float sigma = sqrtf(rss / static_cast<float>(dof));
    // More factors first; then more points.
    if (k > static_cast<size_t>(similarBits(bestMask)) || (k == static_cast<size_t>(similarBits(bestMask)) && m > bestN)) {
      bestMask = mask;
      bestN = m;
      bestSigma = sigma;
      bestDof = dof;
      for (int f = 0; f < 3; ++f) bestCoef[f] = 0;
      for (size_t j = 0; j < k; ++j) bestCoef[idx[j]] = coef[j];
    }
  }

  // 4. The shift, from the fitted factors whose sign agrees with the convention.
  float lnShift = 0.0f;
  if (bestMask) {
    for (int f = 0; f < 3; ++f) {
      if (!(bestMask & (1 << f))) continue;
      if (bestCoef[f] * expected[f] <= 0.0f) continue;       // contradicts the convention on this little data: not applied
      if (!bx.has[f] || !tx.has[f]) continue;
      const float d = bestCoef[f] * (tx.v[f] - bx.v[f]);
      if (fabsf(d) < 0.0025f) continue;
      lnShift += d;
      out.applied |= static_cast<uint8_t>(1 << f);
    }
    out.fitted = out.applied != 0;
  }
  float setting = from * expf(lnShift);

  // 5. The range: the scatter of the owner's own settings, as it is after the fit if there is one.
  float sigma = SIMILAR_NO_DATA_FRACTION;
  if (bestMask) {
    static const float t[] = {0.0f, 1.84f, 1.32f, 1.20f, 1.14f};   // ~ one standard deviation, small samples
    sigma = bestSigma * (bestDof < 5 ? t[bestDof] : 1.08f);
    out.samples = static_cast<uint8_t>(bestN);
  } else if (n >= 2) {
    float mean = 0.0f;
    for (size_t i = 0; i < n; ++i) mean += pts[i].y;
    mean /= static_cast<float>(n);
    float var = 0.0f;
    for (size_t i = 0; i < n; ++i) var += (pts[i].y - mean) * (pts[i].y - mean);
    sigma = sqrtf(var / static_cast<float>(n - 1)) * (n - 1 < 5 ? (n == 2 ? 1.84f : n == 3 ? 1.32f : n == 4 ? 1.20f : 1.14f) : 1.08f);
    out.samples = static_cast<uint8_t>(n);
  } else {
    out.samples = static_cast<uint8_t>(n);
  }
  float minUnit = 1.0f;
  if (grinder.umPerUnit > 0.0f) {
    minUnit = 5.0f / grinder.umPerUnit;     // never finer than 5 microns on a grinder marked in them
    if (minUnit < 1.0f) minUnit = 1.0f;
  }
  float range = from * sigma + bestRecipe * from * 0.03f;   // 3% per step of recipe distance: as in communityStart
  if (range < 2.0f * minUnit) range = 2.0f * minUnit;
  if (range > 200.0f) range = 200.0f;
  if (!isfinite(setting) || setting < 1.0f || setting > 2000.0f) return SimilarStart{};

  // 6. Hints: which way the usual convention points, for the factors the data did not settle.
  if (tx.has[0] && bx.has[0] && tx.v[0] != bx.v[0]) out.hintRoast = tx.v[0] > bx.v[0] ? +1 : -1;
  if (tx.has[1] && bx.has[1]) {
    const float dm = (tx.v[1] - bx.v[1]) * SIMILAR_ALTITUDE_UNIT_M;
    if (dm >= SIMILAR_ALTITUDE_HINT_M) out.hintAltitude = -1;
    else if (dm <= -SIMILAR_ALTITUDE_HINT_M) out.hintAltitude = +1;
  }
  if (tx.has[2] && bx.has[2] && tx.v[2] != bx.v[2]) out.hintProcess = tx.v[2] > bx.v[2] ? +1 : -1;

  out.found = true;
  out.setting = static_cast<uint16_t>(setting + 0.5f);
  out.range = static_cast<uint16_t>(range + 0.5f);
  out.fromSetting = best->grindUm;
  out.fromCoffeeId = best->coffee->id;
  strncpy(out.fromName, best->coffee->name, COFFEE_NAME_LEN - 1);
  out.shift = static_cast<int16_t>(static_cast<int>(out.setting) - static_cast<int>(out.fromSetting));
  out.distance = bestCompare.distance;
  out.nearRecipe = bestRecipe > 0.0f;
  out.fromSettled = best->settled;
  return out;
}

}  // namespace bbw
