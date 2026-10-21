#pragma once
// Coffee taste (docs/COFFEE-TASTE.md): how a drinker liked a coffee, as opposed to how a shot
// extracted. One rating per drinker per bag; a later rating of the same bag replaces it.

#include <stdint.h>
#include <string.h>

namespace bbw {

// The cup: each 1..5, 0 = not given.
constexpr uint8_t TASTE_ATTRS = 5;
enum TasteAttr : uint8_t { ACIDITY = 0, SWEETNESS, BODY, BITTERNESS, FINISH };
inline const char *tasteAttrName(uint8_t i) {
  static const char *const NAMES[TASTE_ATTRS] = {"acidity", "sweetness", "body", "bitterness", "finish"};
  return i < TASTE_ATTRS ? NAMES[i] : "";
}

// The flavour families tasted: the first ring of the SCA flavour wheel, in plain words. A bit each.
constexpr uint8_t FLAVOUR_COUNT = 8;
enum FlavourFamily : uint8_t { FRUITY = 0, FLORAL, SWEET_CARAMEL, CHOCOLATE_NUTTY, SPICY, ROASTY, FERMENTED, EARTHY };
inline const char *flavourName(uint8_t i) {
  static const char *const NAMES[FLAVOUR_COUNT] = {"fruity", "floral", "sweet", "chocolate", "spicy",
                                                   "roasty", "fermented", "earthy"};
  return i < FLAVOUR_COUNT ? NAMES[i] : "";
}
constexpr uint16_t FLAVOUR_MASK = (1u << FLAVOUR_COUNT) - 1;

// 32 bytes, stored as is in a ring file (storage/Store.h). Fields are only ever added in `spare`.
struct CoffeeRating {
  uint32_t id = 0;          // 0 = an empty slot
  uint32_t epochS = 0;      // when it was given; 0 if the clock was not set
  uint32_t shareSeq = 0;    // outbox number of the record that carried it
  uint16_t coffeeId = 0;
  uint16_t bagId = 0;       // the bag rated (its roast date and age); 0 = no bag recorded
  uint8_t drinkerId = 0;    // 0 = the household; 1..MAX_DRINKERS a named drinker
  uint8_t stars = 0;        // 1..5
  uint8_t attr[TASTE_ATTRS] = {};
  uint8_t shareState = 0;   // ShareState (core/Share.h)
  uint16_t flavours = 0;    // a bit per FlavourFamily
  uint8_t spare[6] = {};
};
static_assert(sizeof(CoffeeRating) == 32, "CoffeeRating is stored as is");

// Named drinkers (Settings::namedDrinkers). Ids are positions + 1 and never move: a name left
// empty is a drinker no longer listed, whose ratings are kept.
constexpr uint8_t MAX_DRINKERS = 8;
constexpr uint8_t DRINKER_NAME_LEN = 17;
struct Drinkers {
  char names[MAX_DRINKERS][DRINKER_NAME_LEN] = {};
};

// What a rating may hold; false if it cannot be kept at all (no coffee, no stars).
inline bool sanitizeRating(CoffeeRating &r) {
  if (r.coffeeId == 0 || r.stars < 1 || r.stars > 5) return false;
  for (uint8_t &a : r.attr) {
    if (a > 5) a = 5;
  }
  r.flavours &= FLAVOUR_MASK;
  if (r.drinkerId > MAX_DRINKERS) r.drinkerId = 0;
  return true;
}

// The same rating, for replacing: one per drinker per bag (per coffee when no bag was recorded).
inline bool sameRated(const CoffeeRating &a, const CoffeeRating &b) {
  return a.coffeeId == b.coffeeId && a.bagId == b.bagId && a.drinkerId == b.drinkerId;
}

inline void sanitizeDrinkers(Drinkers &d) {
  for (auto &name : d.names) name[DRINKER_NAME_LEN - 1] = '\0';
}

}  // namespace bbw
