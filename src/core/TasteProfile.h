#pragma once
// Coffee taste profiles (docs/COFFEE-TASTE.md). Pure and portable: no I/O, host tested.
//
// A taste is 13 values from 0 to 1: the cup (acidity, sweetness, body, bitterness, finish) and the
// eight flavour families. Each value carries how sure it is (`known`, 0..1): a value someone rated is
// sure, one guessed from a description is not. A drinker's profile is the coffees they rated,
// weighted by their stars: loved coffees pull it towards their taste, disliked ones push it away.

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "core/CoffeeTaste.h"
#include "core/Types.h"

namespace bbw {

constexpr uint8_t TASTE_DIMS = TASTE_ATTRS + FLAVOUR_COUNT;  // 13
enum class TasteSource : uint8_t { NONE = 0, DESCRIBED, COMMUNITY, RATED };

struct TasteVector {
  float v[TASTE_DIMS] = {};
  float known[TASTE_DIMS] = {};
  TasteSource source = TasteSource::NONE;
};

inline const char *tasteDimName(uint8_t d) {
  return d < TASTE_ATTRS ? tasteAttrName(d) : flavourName(static_cast<uint8_t>(d - TASTE_ATTRS));
}

// ---- A rating as a taste: what the drinker said they tasted.
inline TasteVector tasteFromRating(const CoffeeRating &r) {
  TasteVector t;
  t.source = TasteSource::RATED;
  for (uint8_t i = 0; i < TASTE_ATTRS; ++i) {
    if (r.attr[i] == 0) continue;
    t.v[i] = (r.attr[i] - 1) / 4.0f;
    t.known[i] = 1.0f;
  }
  // Flavours: the ones tapped are there; when some were tapped, the others were probably not.
  if (r.flavours != 0) {
    for (uint8_t f = 0; f < FLAVOUR_COUNT; ++f) {
      const bool on = (r.flavours >> f) & 1u;
      t.v[TASTE_ATTRS + f] = on ? 1.0f : 0.0f;
      t.known[TASTE_ATTRS + f] = on ? 1.0f : 0.5f;
    }
  }
  return t;
}

// ---- A coffee's description as a taste: the roaster's tasting notes, its process and roast level.
// Guesses, so they are never sure.
struct NoteWord {
  const char *word;     // lower case, matched inside the text
  uint16_t families;    // bits of FlavourFamily
};
inline const NoteWord *noteWords(size_t &count) {
  static const NoteWord WORDS[] = {
      // fruity
      {"berry", 1u << FRUITY}, {"blueberr", 1u << FRUITY}, {"strawberr", 1u << FRUITY},
      {"raspberr", 1u << FRUITY}, {"blackberr", 1u << FRUITY}, {"cherry", 1u << FRUITY},
      {"grape", 1u << FRUITY}, {"plum", 1u << FRUITY}, {"peach", 1u << FRUITY}, {"apricot", 1u << FRUITY},
      {"apple", 1u << FRUITY}, {"pear", 1u << FRUITY}, {"citrus", 1u << FRUITY}, {"lemon", 1u << FRUITY},
      {"lime", 1u << FRUITY}, {"orange", 1u << FRUITY}, {"grapefruit", 1u << FRUITY},
      {"bergamot", (1u << FRUITY) | (1u << FLORAL)}, {"mango", 1u << FRUITY}, {"pineapple", 1u << FRUITY},
      {"tropical", 1u << FRUITY}, {"passion", 1u << FRUITY}, {"lychee", (1u << FRUITY) | (1u << FLORAL)},
      {"fruit", 1u << FRUITY}, {"currant", 1u << FRUITY}, {"cranberr", 1u << FRUITY}, {"fig", 1u << FRUITY},
      {"date", 1u << SWEET_CARAMEL}, {"raisin", (1u << FRUITY) | (1u << SWEET_CARAMEL)},
      {"jam", (1u << FRUITY) | (1u << SWEET_CARAMEL)},
      // floral
      {"floral", 1u << FLORAL}, {"jasmine", 1u << FLORAL}, {"rose", 1u << FLORAL}, {"hibiscus", 1u << FLORAL},
      {"lavender", 1u << FLORAL}, {"blossom", 1u << FLORAL}, {"chamomile", 1u << FLORAL}, {"tea", 1u << FLORAL},
      // sweet / caramel
      {"caramel", 1u << SWEET_CARAMEL}, {"toffee", 1u << SWEET_CARAMEL}, {"honey", 1u << SWEET_CARAMEL},
      {"sugar", 1u << SWEET_CARAMEL}, {"molasses", 1u << SWEET_CARAMEL}, {"syrup", 1u << SWEET_CARAMEL},
      {"vanilla", 1u << SWEET_CARAMEL}, {"butterscotch", 1u << SWEET_CARAMEL}, {"candy", 1u << SWEET_CARAMEL},
      {"sweet", 1u << SWEET_CARAMEL}, {"panela", 1u << SWEET_CARAMEL},
      // chocolate / nutty
      {"chocolate", 1u << CHOCOLATE_NUTTY}, {"cocoa", 1u << CHOCOLATE_NUTTY}, {"cacao", 1u << CHOCOLATE_NUTTY},
      {"nut", 1u << CHOCOLATE_NUTTY}, {"almond", 1u << CHOCOLATE_NUTTY}, {"hazelnut", 1u << CHOCOLATE_NUTTY},
      {"pecan", 1u << CHOCOLATE_NUTTY}, {"walnut", 1u << CHOCOLATE_NUTTY}, {"praline", (1u << CHOCOLATE_NUTTY) | (1u << SWEET_CARAMEL)},
      {"peanut", 1u << CHOCOLATE_NUTTY}, {"marzipan", (1u << CHOCOLATE_NUTTY) | (1u << SWEET_CARAMEL)},
      // spicy
      {"spice", 1u << SPICY}, {"cinnamon", 1u << SPICY}, {"clove", 1u << SPICY}, {"pepper", 1u << SPICY},
      {"nutmeg", 1u << SPICY}, {"cardamom", 1u << SPICY}, {"ginger", 1u << SPICY}, {"anise", 1u << SPICY},
      // roasty / smoky
      {"roast", 1u << ROASTY}, {"smok", 1u << ROASTY}, {"burnt", 1u << ROASTY}, {"toast", 1u << ROASTY},
      {"tobacco", (1u << ROASTY) | (1u << EARTHY)}, {"malt", (1u << ROASTY) | (1u << SWEET_CARAMEL)},
      {"cereal", 1u << ROASTY}, {"grain", 1u << ROASTY},
      // fermented / winey
      {"wine", 1u << FERMENTED}, {"winey", 1u << FERMENTED}, {"boozy", 1u << FERMENTED},
      {"rum", 1u << FERMENTED}, {"whisk", 1u << FERMENTED}, {"ferment", 1u << FERMENTED},
      {"anaerobic", 1u << FERMENTED}, {"funk", 1u << FERMENTED}, {"liqueur", 1u << FERMENTED},
      // earthy / green
      {"earth", 1u << EARTHY}, {"wood", 1u << EARTHY}, {"cedar", 1u << EARTHY}, {"herb", 1u << EARTHY},
      {"grass", 1u << EARTHY}, {"vegetal", 1u << EARTHY}, {"mushroom", 1u << EARTHY}, {"leather", 1u << EARTHY},
  };
  count = sizeof(WORDS) / sizeof(WORDS[0]);
  return WORDS;
}

// `needle` found at the start of a word in `hay` ("lime" in "Lime zest", not in "sublime"), compared
// lower case; hay is short (a coffee's fields).
inline bool containsWord(const char *hay, const char *needle) {
  const size_t n = strlen(needle);
  for (const char *p = hay; *p; ++p) {
    if (p != hay && isalpha(static_cast<unsigned char>(p[-1]))) continue;
    size_t i = 0;
    while (i < n && p[i] && tolower(static_cast<unsigned char>(p[i])) == needle[i]) ++i;
    if (i == n) return true;
  }
  return false;
}

// The flavour families a coffee's notes mention.
inline uint16_t familiesInNotes(const char *notes) {
  size_t count = 0;
  const NoteWord *words = noteWords(count);
  uint16_t found = 0;
  for (size_t i = 0; i < count; ++i) {
    if (containsWord(notes, words[i].word)) found |= words[i].families;
  }
  return found;
}

inline TasteVector tasteFromDescription(const Coffee &c) {
  TasteVector t;
  t.source = TasteSource::DESCRIBED;
  // The cup from the roast level: lighter roasts are brighter and lighter, darker ones heavier and
  // more bitter. 0 = not set: nothing guessed.
  static const float ROAST[6][3] = {  // acidity, body, bitterness
      {0, 0, 0}, {0.80f, 0.35f, 0.15f}, {0.68f, 0.45f, 0.25f}, {0.50f, 0.55f, 0.40f},
      {0.35f, 0.68f, 0.60f}, {0.20f, 0.80f, 0.80f}};
  if (c.roastLevel >= 1 && c.roastLevel <= 5) {
    t.v[ACIDITY] = ROAST[c.roastLevel][0];
    t.v[BODY] = ROAST[c.roastLevel][1];
    t.v[BITTERNESS] = ROAST[c.roastLevel][2];
    t.known[ACIDITY] = t.known[BODY] = t.known[BITTERNESS] = 0.3f;
  }
  uint16_t families = familiesInNotes(c.notes);
  // The process: naturals lean fruity, sweet and heavier; anaerobic and carbonic ones fermented;
  // washed coffees brighter.
  const bool natural = containsWord(c.process, "natural") || containsWord(c.process, "dry");
  const bool honey = containsWord(c.process, "honey") || containsWord(c.process, "pulped");
  const bool washed = containsWord(c.process, "wash") || containsWord(c.process, "wet");
  const bool fermented = containsWord(c.process, "anaerob") || containsWord(c.process, "carbon") ||
                         containsWord(c.process, "ferment");
  if (natural) families |= 1u << FRUITY;
  if (fermented) families |= 1u << FERMENTED;
  if (natural || honey) {
    t.v[SWEETNESS] = honey ? 0.70f : 0.65f;
    t.known[SWEETNESS] = 0.25f;
    if (t.known[BODY] > 0) t.v[BODY] = fminf(1.0f, t.v[BODY] + 0.08f);
  }
  if (washed && t.known[ACIDITY] > 0) t.v[ACIDITY] = fminf(1.0f, t.v[ACIDITY] + 0.07f);
  if (c.roastLevel >= 4) families |= 1u << ROASTY;
  for (uint8_t f = 0; f < FLAVOUR_COUNT; ++f) {
    const bool on = (families >> f) & 1u;
    t.v[TASTE_ATTRS + f] = on ? 1.0f : 0.0f;
    // A family named is likely there; one not named says a little, and only when some were named.
    t.known[TASTE_ATTRS + f] = on ? 0.45f : (families ? 0.15f : 0.0f);
  }
  return t;
}

// Each value from the surer of the two: a rating's values over a description's guesses.
inline TasteVector blendTaste(const TasteVector &sure, const TasteVector &guess) {
  TasteVector t = sure;
  for (uint8_t d = 0; d < TASTE_DIMS; ++d) {
    if (sure.known[d] <= 0.0f && guess.known[d] > 0.0f) {
      t.v[d] = guess.v[d];
      t.known[d] = guess.known[d];
    }
  }
  return t;
}

// ---- A drinker's profile.
struct TasteProfile {
  float v[TASTE_DIMS] = {};       // 0..1; 0.5 is no lean either way
  float known[TASTE_DIMS] = {};   // 0..1, how much the ratings say about each
  uint16_t coffees = 0;           // distinct coffees rated
  uint16_t ratings = 0;
  static constexpr uint16_t SHOW_FROM = 3;
  static constexpr uint16_t SETTLED_FROM = 8;
  bool shown() const { return coffees >= SHOW_FROM; }
  bool settled() const { return coffees >= SETTLED_FROM; }
};

// How much a rating pulls: 5 stars fully towards, 3 a little, 1 away.
inline float starPull(uint8_t stars) { return (static_cast<float>(stars) - 2.5f) / 2.5f; }
// Older ratings count less: half after a year, never below a third.
inline float recencyWeight(uint32_t ratedEpochS, uint32_t nowEpochS) {
  if (ratedEpochS == 0 || nowEpochS <= ratedEpochS) return 1.0f;
  const float years = (nowEpochS - ratedEpochS) / (365.0f * 86400.0f);
  return fmaxf(0.33f, powf(0.5f, years));
}

// One rated coffee: the rating, and the coffee's taste for it (the rating blended with the
// description's guesses).
struct RatedTaste {
  uint16_t coffeeId;
  uint8_t stars;
  uint32_t epochS;
  TasteVector taste;
};

inline TasteProfile buildProfile(const RatedTaste *rated, size_t n, uint32_t nowEpochS) {
  TasteProfile p;
  float num[TASTE_DIMS] = {}, den[TASTE_DIMS] = {};
  uint16_t seen[64];
  uint16_t seenCount = 0;
  for (size_t i = 0; i < n; ++i) {
    const RatedTaste &r = rated[i];
    ++p.ratings;
    bool counted = false;
    for (uint16_t k = 0; k < seenCount; ++k) counted = counted || seen[k] == r.coffeeId;
    if (!counted && seenCount < 64) seen[seenCount++] = r.coffeeId;
    const float w = starPull(r.stars) * recencyWeight(r.epochS, nowEpochS);
    for (uint8_t d = 0; d < TASTE_DIMS; ++d) {
      const float k = r.taste.known[d];
      if (k <= 0.0f) continue;
      num[d] += w * k * (r.taste.v[d] - 0.5f);
      den[d] += fabsf(w) * k;
    }
  }
  p.coffees = seenCount;
  for (uint8_t d = 0; d < TASTE_DIMS; ++d) {
    p.v[d] = den[d] > 0.0f ? fminf(1.0f, fmaxf(0.0f, 0.5f + num[d] / den[d])) : 0.5f;
    p.known[d] = fminf(1.0f, den[d] / 3.0f);  // three sure, loved ratings make it sure
  }
  return p;
}

// ---- How well a coffee's taste matches a profile: -1 (opposite) .. 1 (alike), with how much the
// two say about each other (0..1). Both are measured from 0.5, the middle.
struct TasteMatch {
  float score = 0.0f;
  float confidence = 0.0f;
  uint8_t why[2] = {255, 255};  // the two dimensions that agree most, 255 = none
};

inline TasteMatch matchTaste(const TasteProfile &p, const TasteVector &c) {
  TasteMatch m;
  float dot = 0, pp = 0, cc = 0, kSum = 0;
  float best[2] = {0, 0};
  for (uint8_t d = 0; d < TASTE_DIMS; ++d) {
    const float k = p.known[d] * c.known[d];
    if (k <= 0.0f) continue;
    const float a = p.v[d] - 0.5f, b = c.v[d] - 0.5f;
    dot += k * a * b;
    pp += k * a * a;
    cc += k * b * b;
    kSum += k;
    const float agree = k * a * b;
    if (agree > best[0]) {
      best[1] = best[0];
      m.why[1] = m.why[0];
      best[0] = agree;
      m.why[0] = d;
    } else if (agree > best[1]) {
      best[1] = agree;
      m.why[1] = d;
    }
  }
  if (pp > 0.0f && cc > 0.0f) m.score = dot / sqrtf(pp * cc);
  m.confidence = fminf(1.0f, kSum / 4.0f);
  return m;
}

// ---- Recommendations (docs/COFFEE-TASTE.md).

// The controller's roast level names (core/Types.h roastLevelName) back to their numbers; 0 = none.
inline uint8_t roastLevelFromName(const char *name) {
  static const char *const NAMES[] = {"light", "medium-light", "medium", "medium-dark", "dark"};
  for (uint8_t i = 0; i < 5; ++i) {
    if (name && strcmp(name, NAMES[i]) == 0) return static_cast<uint8_t>(i + 1);
  }
  return 0;
}

// A coffee's taste as the community rated it: cup means 1..5 (0 = not enough), flavour shares 0..1
// (negative = not enough). Surer than a description, less sure than your own rating.
inline TasteVector tasteFromCommunity(const float cup[TASTE_ATTRS], const float flavours[FLAVOUR_COUNT]) {
  TasteVector t;
  t.source = TasteSource::COMMUNITY;
  for (uint8_t i = 0; i < TASTE_ATTRS; ++i) {
    if (cup[i] < 1.0f) continue;
    t.v[i] = fminf(1.0f, fmaxf(0.0f, (cup[i] - 1.0f) / 4.0f));
    t.known[i] = 0.7f;
  }
  for (uint8_t f = 0; f < FLAVOUR_COUNT; ++f) {
    if (flavours[f] < 0.0f) continue;
    t.v[TASTE_ATTRS + f] = fminf(1.0f, flavours[f]);
    t.known[TASTE_ATTRS + f] = 0.6f;
  }
  return t;
}

// How alike two coffees taste: -1..1 over what both say, measured from the middle.
inline float tasteSimilarity(const TasteVector &a, const TasteVector &b) {
  float dot = 0, aa = 0, bb = 0;
  for (uint8_t d = 0; d < TASTE_DIMS; ++d) {
    const float k = a.known[d] * b.known[d];
    if (k <= 0.0f) continue;
    const float x = a.v[d] - 0.5f, y = b.v[d] - 0.5f;
    dot += k * x * y;
    aa += k * x * x;
    bb += k * y * y;
  }
  return aa > 0.0f && bb > 0.0f ? dot / sqrtf(aa * bb) : 0.0f;
}

// How much another drinker's stars say about yours: 0..1. mine[i] and theirs[i] are stars 1..5 for
// the same coffees, 0 where not rated. Two or more coffees in common are needed; three or more count
// fully. Drinkers who disagree say nothing (0), rather than the opposite.
inline float peopleLikeness(const uint8_t *mine, const uint8_t *theirs, size_t n) {
  float agree = 0;
  size_t common = 0;
  for (size_t i = 0; i < n; ++i) {
    if (!mine[i] || !theirs[i]) continue;
    agree += 1.0f - fabsf(static_cast<float>(mine[i]) - theirs[i]) / 4.0f;
    ++common;
  }
  if (common < 2) return 0.0f;
  const float alike = agree / common;   // 1: the same stars everywhere
  return fmaxf(0.0f, (alike - 0.5f) * 2.0f) * fminf(1.0f, common / 3.0f);
}

// The score a candidate coffee is ranked by: mostly how its taste matches the profile (discounted by
// how sure that is), then what drinkers like you gave it, then a little for good community stars.
// peopleStars / communityStars: 1..5, or 0 when there is none.
inline float recommendScore(const TasteMatch &m, float peopleStars, float communityStars) {
  const float content = m.score * (0.4f + 0.6f * m.confidence);
  const bool people = peopleStars >= 1.0f, community = communityStars >= 1.0f;
  float score = (people ? 0.6f : 0.85f) * content;
  if (people) score += 0.3f * (peopleStars - 3.0f) / 2.0f;
  if (community) score += (people ? 0.1f : 0.15f) * (communityStars - 3.0f) / 2.0f;
  return score;
}

}  // namespace bbw
