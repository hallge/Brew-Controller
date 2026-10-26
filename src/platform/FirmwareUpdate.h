#pragma once
// Firmware updates from the GitHub repository's releases, with no cable.
//
// Flash layout (partitions.csv): a factory slot and two update slots.
//   factory  the firmware installed over USB: a known-good version that
//            updates never write to. The last resort, and "Safe firmware" on
//            the Setup page.
//   ota_0/1  updates are written to whichever of the two is not running, so
//            the version before stays where it was.
// A new firmware is on trial for its first 45 seconds (confirmFirmware() in
// main.cpp): if it crashes or hangs before then, the bootloader starts the one
// before it again.
//
// What is installed is what a release published: the release's firmware.bin
// is downloaded over HTTPS (only GitHub's roots are trusted: GithubRoots.h,
// ReleaseRoots.h), its SHA-256 has to match the one in the release's
// SHA256SUMS.txt, and only then is the new slot made the one to start from.
// Anything short of that leaves the running firmware as it is.
//
// The work runs in a task of its own, so the web page keeps answering while a
// firmware downloads. It is only ever started with no shot in hand, and Start
// is refused while it runs.
//
// The PC emulator has none of this: it is updated by rebuilding it.

#include <stddef.h>
#include <stdint.h>

namespace bbw {

constexpr const char *DEFAULT_RELEASE_REPO = "hallge/Brew-Controller";

struct UpdateConfig {
  char repo[64] = "hallge/Brew-Controller";  // "owner/name"
  char token[128] = {};  // only for a private repository: Contents read-only, that repository
  bool autoCheck = true; // look for a new release once a day (never installs by itself)
};

struct ReleaseInfo {
  bool valid = false;
  char tag[32] = {};
  char name[64] = {};
  char published[24] = {};
  uint32_t firmwareId = 0;     // asset ids at the GitHub API
  uint32_t firmwareBytes = 0;
  uint32_t sumsId = 0;         // SHA256SUMS.txt
};

enum class UpdatePhase : uint8_t { IDLE, CHECKING, DOWNLOADING, INSTALLED, FAILED };

struct UpdateStatus {
  UpdatePhase phase = UpdatePhase::IDLE;
  uint8_t percent = 0;
  char message[112] = {};      // what happened last, in words
  ReleaseInfo latest;          // as of the last check
  uint32_t checkedEpoch = 0;   // 0: not checked since start-up
  bool available = false;      // the latest release is not what is running
};

struct SlotInfo {
  char label[17] = {};
  char version[32] = {};       // the version last seen running from it; empty: unknown
  uint32_t bytes = 0;          // partition size
  bool running = false;
  bool bootNext = false;       // the one the bootloader will start next time
  const char *state = "";      // "valid", "on trial", "failed", "unused", "factory"
};

const char *updatePhaseName(UpdatePhase phase);

// Loop task, once at start-up: the running slot is recorded with this
// firmware's version, so the page can say what is in each slot.
void updateBegin(const UpdateConfig &config);
void updateConfigure(const UpdateConfig &config);
UpdateConfig updateConfig();

bool updateCheck();      // look for the latest release; false if busy or not possible
bool updateInstall();    // download and install it; false if busy, nothing found, or not idle
bool updateBusy();
// A firmware file is arriving from the browser (Web.cpp): both write through the one Update, so
// neither starts while the other is under way.
void updateUploadActive(bool active);
UpdateStatus updateStatus();
// Loop task. idle: no shot in hand. Starts the daily check.
void updateService(bool idle, bool online);

size_t updateSlots(SlotInfo *out, size_t max);
// Start from another slot at the next restart: "factory" (the safe version)
// or "previous" (the other update slot, if it holds a firmware that ran).
bool updateBootSlot(const char *which, char *why, size_t whySize);

}  // namespace bbw
