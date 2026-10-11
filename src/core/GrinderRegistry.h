#pragma once
// Grinder models the firmware knows about: what the setting on each is read
// in, and how many microns of burr gap one unit of it is, where the maker
// says. This is the copy bundled with the firmware; the shared library will
// replace it with one that grows (docs/COMMUNITY.md, section 6).
//
// Ids are permanent: they go into shared records. Add at the end.
//
// Portable; no flash or UI access in here.

#include <stddef.h>
#include <stdint.h>

namespace bbw {

struct GrinderModel {
  uint16_t id;
  const char *model;
  const char *burrs;
  const char *unit;     // as printed beside a setting: "um", "steps", "dial"
  float umPerUnit;      // 0 = not published; learned from shared shots later
};

constexpr GrinderModel GRINDER_MODELS[] = {
    {1, "Mahlkonig E64 WS", "64 mm flat", "um", 1.0f},     // display reads burr gap in microns
    {2, "LUCCA Atom 75", "75 mm flat", "steps", 5.0f},     // position sensor, 5 microns a step
    {3, "DF64", "64 mm flat", "dial", 0.0f},
    {4, "Eureka Mignon Specialita", "55 mm flat", "dial", 0.0f},
    {5, "Niche Zero", "63 mm conical", "dial", 0.0f},
};
constexpr size_t GRINDER_MODEL_COUNT = sizeof(GRINDER_MODELS) / sizeof(GRINDER_MODELS[0]);

inline const GrinderModel *findGrinderModel(uint16_t id) {
  for (size_t i = 0; i < GRINDER_MODEL_COUNT; ++i) {
    if (GRINDER_MODELS[i].id == id) return &GRINDER_MODELS[i];
  }
  return nullptr;
}

}  // namespace bbw
