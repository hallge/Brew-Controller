#pragma once
// Add-on modules on the board (docs/ADDONS.md, "Add-ons as modules"): the `.bbwmod` files on LittleFS,
// loading them into PSRAM with the ELF loader (lib/elf_loader), the crash guard, and delivery from a
// GitHub release or an upload. The module file format and the rules are the portable
// addons/ModuleFile.h; the two ends of the boundary are addons/ModuleAddon.h and addons/AddonModule.h.
//
// Board only: the PC emulator keeps the compiled-in path (python emu/build.py --addons=...).
// Loop task, except where it says "network task".

#include <stddef.h>
#include <stdint.h>

#include "addons/Addon.h"

namespace bbw {
namespace addonloader {

constexpr size_t MAX_MODULES = 4;

enum class State : uint8_t {
  LOADED = 0,
  CRASHED = 1,         // the board came back after a panic or a watchdog while it was loading or running
  NEEDS_REBUILD = 2,   // built for another add-on interface (ABI)
  NEEDS_FIRMWARE = 3,  // wants a newer firmware
  REFUSED = 4,         // not a valid module, or the loader could not place it
};
const char *stateName(State s);

struct Module {
  char id[32] = {};
  char version[16] = {};
  State state = State::REFUSED;
  char problem[96] = {};
  uint32_t fileBytes = 0;
  uint32_t loadMs = 0;     // time to read, relocate and make the add-on
  uint32_t chipUsed = 0;   // chip RAM taken by loading it
  addon::Addon *addon = nullptr;  // the module's add-on, or a stub that is listed but never started
};

// Once at start-up, before addons::begin(): the crash guard decides from the reset reason, the files in
// /addons are looked at, and the modules that may start are loaded. Returns how many modules are listed.
size_t scan(uint32_t nowMs);
size_t count();
const Module &module(size_t i);
int find(const char *id);

// Every loop pass: the crash guard's 120 s clock.
void service(uint32_t nowMs);

// Try a module that stopped after a crash again, at the next restart. false: it is not blocked.
bool retry(const char *id, const char *&problem);
// Delete a module's file (it stops being listed at the next restart).
bool remove(const char *id, const char *&problem);

// ---- where modules come from ----
struct Source {
  char repo[64] = {};   // "owner/name"; empty: none set (nothing is fetched)
  char token[128] = {}; // only for a private repository; never sent to the page
};
Source source();
// false: the repository is not "owner/name" (problem). An empty token keeps the stored one, "-" forgets it.
bool setSource(const char *repo, const char *token, const char *&problem);

// A release's module that is not installed, or differs from what is.
struct Offer {
  char id[32] = {};
  char tag[32] = {};
  uint32_t bytes = 0;
  bool installed = false;
  bool differs = false;   // installed, but the release's file is not the same one
};
enum class Phase : uint8_t { IDLE, CHECKING, DOWNLOADING, DONE, FAILED };
struct Delivery {
  Phase phase = Phase::IDLE;
  char message[112] = {};
  Offer offers[MAX_MODULES];
  size_t offerCount = 0;
  uint32_t checkedEpoch = 0;
  bool restartNeeded = false;   // a module was stored: it loads at the next restart
};
Delivery delivery();

// Look at the newest release of the repository for `<id>.bbwmod` files (network task, returns at once).
bool check(const char *&problem);
// Download one of them into /addons, check SHA256SUMS.txt and the header, swap it in. Refused while a shot
// is in hand (idle: no shot) or while another job runs.
bool install(const char *id, bool idle, const char *&problem);

// ---- a module file from the browser (POST /api/addons/module, a multipart upload) ----
void uploadBegin(bool idle);
void uploadWrite(const uint8_t *data, size_t size);
// After the last chunk. true: stored (id says which). false: refused (problem says why); nothing changed.
bool uploadEnd(char *id, size_t idSize, const char *&problem);
void uploadAbort();
// For the request's answer, after the last chunk: stored, or why not ("" when no file came at all).
bool uploadStored();
const char *uploadError();

}  // namespace addonloader
}  // namespace bbw
