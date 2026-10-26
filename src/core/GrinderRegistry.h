#pragma once
// Grinder models the firmware knows about: what the setting on each is read
// in (unit, a dial read in fractions, or two rings), its range, and how many
// microns of burr gap one step is where the maker publishes it. Where not
// (umPerUnit 0), grind advice learns it from the owner's own shots.
//
// This is the copy bundled with the firmware, for a controller with no
// network. The same models are seeded into the shared library
// (library-template/seed/grinders/, libraryId), where owners can propose
// corrections through the usual review; a library entry with the same id is
// shown in place of the built-in.
//
// Ids are permanent: they go into shared records. Add at the end; an id that
// has been taken out (2) is not given to another model. How an existing
// model's setting is read (divisor, subSteps) must not change: settings
// already stored would read differently.
//
// Portable; no flash or UI access in here.

#include <stddef.h>
#include <stdint.h>

namespace bbw {

struct GrinderModel {
  uint16_t id;
  const char *model;
  const char *burrs;
  const char *unit;      // as printed beside a setting: "um", "setting", "dial", "click"
  float umPerUnit;       // per stored step; 0 = not published: learned from shots
  uint8_t divisor;       // steps per displayed unit (core/SettingFormat.h); 1 = whole numbers
  uint8_t subSteps;      // 0: one scale; N: an inner ring of N steps per outer step
  uint16_t minStep;      // the range, in stored steps; 0 = not given
  uint16_t maxStep;
  const char *libraryId; // its entry in the shared library's list of grinders
};

// Ranges in stored steps: on two rings, outer x subSteps (a Sette 270's 31 x 9 = 279).
constexpr GrinderModel GRINDER_MODELS[] = {
    {1, "Mahlkonig E64 WS", "64 mm flat", "um", 1.0f, 1, 0, 0, 0, "mahlkonig-e64-ws"},  // display reads burr gap in microns
    {3, "DF64", "64 mm flat", "dial", 0.0f, 1, 0, 1, 90, "df64"},
    {4, "Eureka Mignon Specialita", "55 mm flat", "dial", 0.0f, 1, 0, 0, 0, "eureka-mignon-specialita"},
    {5, "Niche Zero", "63 mm conical", "dial", 0.0f, 1, 0, 1, 50, "niche-zero"},
    {6, "Breville Smart Grinder Pro", "40 mm conical", "setting", 0.0f, 1, 0, 1, 60, "breville-smart-grinder-pro"},
    {7, "Breville Barista Express", "conical (built in)", "setting", 0.0f, 1, 0, 1, 16, "breville-barista-express"},
    {8, "Barista Express Impress", "conical (built in)", "setting", 0.0f, 1, 0, 1, 25,
     "breville-barista-express-impress"},
    {9, "Baratza Encore ESP", "40 mm conical", "setting", 0.0f, 1, 0, 1, 40, "baratza-encore-esp"},
    {10, "Baratza Encore ESP Pro", "40 mm conical", "setting", 0.0f, 1, 0, 1, 40, "baratza-encore-esp-pro"},
    {11, "Baratza Sette 270 / 270Wi", "40 mm conical", "setting", 0.0f, 1, 9, 1, 279, "baratza-sette-270"},
    {12, "Baratza Sette 30", "40 mm conical", "setting", 0.0f, 1, 0, 1, 30, "baratza-sette-30"},
    {13, "Baratza Vario / Vario-W", "54 mm flat", "setting", 0.0f, 1, 23, 1, 230, "baratza-vario"},
    {14, "DF83", "83 mm flat", "dial", 0.0f, 1, 0, 1, 90, "df83"},
    {15, "Eureka Mignon Silenzio / Notte", "50 mm flat", "dial", 0.0f, 1, 0, 0, 0, "eureka-mignon-silenzio"},
    {16, "1Zpresso J-Max", "48 mm conical", "click", 8.8f, 1, 0, 1, 450, "1zpresso-j-max"},
    {17, "1Zpresso K-Ultra", "48 mm conical", "click", 20.0f, 1, 0, 1, 200, "1zpresso-k-ultra"},
    {18, "Comandante C40", "39 mm conical", "click", 30.0f, 1, 0, 1, 40, "comandante-c40"},
    {19, "Timemore Chestnut C3", "38 mm conical", "click", 0.0f, 1, 0, 1, 36, "timemore-c3"},
    {20, "Timemore Sculptor 078", "78 mm flat", "dial", 0.0f, 1, 0, 0, 0, "timemore-sculptor-078"},
    {21, "Fellow Opus", "40 mm conical", "setting", 0.0f, 1, 0, 1, 41, "fellow-opus"},
};
constexpr size_t GRINDER_MODEL_COUNT = sizeof(GRINDER_MODELS) / sizeof(GRINDER_MODELS[0]);

inline const GrinderModel *findGrinderModel(uint16_t id) {
  for (size_t i = 0; i < GRINDER_MODEL_COUNT; ++i) {
    if (GRINDER_MODELS[i].id == id) return &GRINDER_MODELS[i];
  }
  return nullptr;
}

// The settings a grinder can be at, in stored steps: its model's range, else anything the
// controller stores (1 to 2000).
struct StepRange {
  uint16_t lo = 1, hi = 2000;
  bool known = false;
};
inline StepRange stepRange(uint16_t modelId) {
  StepRange r;
  const GrinderModel *m = findGrinderModel(modelId);
  if (m && m->maxStep > m->minStep) {
    r.lo = m->minStep ? m->minStep : 1;
    r.hi = m->maxStep;
    r.known = true;
  }
  return r;
}
inline uint16_t clampToRange(int value, const StepRange &r) {
  return static_cast<uint16_t>(value < r.lo ? r.lo : (value > r.hi ? r.hi : value));
}

}  // namespace bbw
