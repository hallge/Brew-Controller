#pragma once
// The rules the controller holds an add-on to, as pure functions (docs/ADDONS.md section 5): which
// files it may touch, how much it may keep, and when it may start a shot. The firmware's glue
// (AddonHost.cpp, main.cpp) gathers the facts and calls these; the host tests call them directly.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "addons/Addon.h"

namespace bbw {
namespace addon {

// ------------------------------------------------------------------------------ files --

// "/addons/<id>/<name>" for the add-on's own file. false: the name is refused (no path, no "..", no
// leading dot, [a-z0-9._-] only, at most FILE_NAME_MAX), or the id is not a valid add-on id, or the
// whole path would not fit a backup entry. The id is always the calling add-on's own, never an
// argument from outside; a name that carries a slash, so another folder or id, is refused here.
inline bool addonFilePath(const char *id, const char *name, char *out, size_t size) {
  if (!id || !name || !out || size == 0) return false;
  const size_t idLen = strlen(id), nameLen = strlen(name);
  if (idLen == 0 || idLen > 32 || nameLen == 0 || nameLen > FILE_NAME_MAX || name[0] == '.') return false;
  for (size_t i = 0; i < idLen; ++i) {
    const char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  for (size_t i = 0; i < nameLen; ++i) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
  }
  if (strstr(name, "..")) return false;
  const size_t total = 8 + idLen + 1 + nameLen;  // "/addons/" id "/" name
  if (total > FILE_PATH_MAX || total >= size) return false;
  snprintf(out, size, "/addons/%s/%s", id, name);
  return true;
}

// Whether a write of `add` bytes is allowed: `used` is what the add-on's files take now (and `replaced`
// the bytes of the file being replaced, 0 for an append), `fsFree` the free bytes on the file system.
// nullptr: allowed. Otherwise the reason, for the card.
inline const char *fileWriteRefusal(uint32_t used, uint32_t replaced, uint32_t add, uint32_t fsFree) {
  const uint32_t after = (used > replaced ? used - replaced : 0) + add;
  if (after > ADDON_FILE_LIMIT) return "the add-on's file space is full";
  const uint32_t growth = add > replaced ? add - replaced : 0;
  // A write that does not grow the file (a roll-over that shortens it) is never refused for space.
  if (growth > 0 && (fsFree < growth || fsFree - growth < ADDON_FILE_RESERVE)) return "storage low";
  return nullptr;
}

// ---------------------------------------------------------------------------- start --

// What the controller knows when an add-on asks for a shot.
struct StartContext {
  bool updateBusy = false;          // a firmware download or install is running
  bool ready = false;               // READY or READY_TO_BREW
  bool scaleConnected = false;
  bool dripOrResult = false;        // POST_SHOT_DRIP or SHOT_COMPLETE: a drip or the result screen is up
  uint16_t activeRecipeId = 0;
  uint16_t wantedRecipeId = 0;
  bool cupRequired = false;
  bool cupPresent = false;
};

// nullptr: the controller may be asked to start (app::start(), the same path as /api/start).
// "Start from the web page" is not asked: an add-on asks only when its owner chose that it may (the grinder
// add-on's "Knob press: Starts the shot"), and that choice is the consent (owner, 12 Nov 2026).
inline const char *startRefusal(const StartContext &c) {
  if (c.updateBusy) return "a firmware update is running";
  if (c.dripOrResult) return "the last shot's drip or result is still up";
  if (!c.ready) return "the controller is not ready";
  if (!c.scaleConnected) return "the scale is not connected";
  if (c.wantedRecipeId == 0 || c.activeRecipeId != c.wantedRecipeId) return "another recipe is selected";
  if (c.cupRequired && !c.cupPresent) return "no cup on the scale";
  return nullptr;
}

}  // namespace addon
}  // namespace bbw
