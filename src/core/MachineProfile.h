#pragma once
// What the brew logic has to know about the espresso machine: how a shot is
// started and stopped on it, by whom.
//
// There is nothing to talk to on these machines, so a profile is not a
// driver: it is a short description that the brew logic asks instead of
// assuming. The logic was written for one machine and one button, the
// Breville Dual Boiler's MANUAL button; that is profile 0 and the default,
// and with it the logic does exactly what it did before there were profiles.
//
// A profile is set up once and can be sent to the shared library
// (docs/COMMUNITY.md), so that the next owner of the same machine picks it
// from a list. The same machine worked by another button is another profile.
//
// Pure C++, no Arduino: host tested.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "Types.h"

namespace bbw {

enum class MachineStyle : uint8_t {
  // One button: a press starts the pump, a press of the same button stops it.
  // The brew bot does both.
  TOGGLE = 0,
  // A button starts the shot and the machine ends it by itself (a dose
  // button). The brew bot starts it; nothing here stops it.
  START_ONLY = 1,
  // A lever, knob, wheel or paddle, worked by hand. Nothing is pressed: the
  // controller weighs, times, records and advises.
  MANUAL = 2,
  // Two buttons: one starts the shot, another stops it. The brew bot is on
  // the start button and a second bot, the stop bot, on the stop button.
  START_STOP_BUTTONS = 3,
  // One button that brews for as long as it is held down. The brew bot holds
  // it for the whole shot and lets go to stop. Needs a pusher that can hold.
  HOLD_BUTTON = 4,
};
constexpr uint8_t MACHINE_STYLE_LAST = static_cast<uint8_t>(MachineStyle::HOLD_BUTTON);

// MANUAL only: what begins the shot's clock.
enum class ManualStart : uint8_t {
  TAP = 0,         // Start is tapped as the lever is pulled
  FIRST_DRIP = 1,  // the first coffee on the scale; the time is then without pre-infusion
};

constexpr size_t MACHINE_MAKER_LEN = 24, MACHINE_MODEL_LEN = 32, MACHINE_CONTROL_LEN = 32;

// Fixed layout: stored as it is.
struct MachineProfile {
  char maker[MACHINE_MAKER_LEN] = "Breville";
  char model[MACHINE_MODEL_LEN] = "Dual Boiler";
  char control[MACHINE_CONTROL_LEN] = "MANUAL button";  // which button, lever or knob this is about
  uint8_t style = static_cast<uint8_t>(MachineStyle::TOGGLE);
  uint8_t manualStart = static_cast<uint8_t>(ManualStart::TAP);
  // Holding the button keeps the machine in pre-infusion until it is let go
  // (what a recipe's held pre-infusion and bloom rely on).
  uint8_t holdPreinfusion = 1;
  // A tap starts with the machine's own programmed pre-infusion. Descriptive.
  uint8_t tapPreinfusion = 1;
  // The machine has a power button a bot can press, and it is a toggle. Descriptive.
  uint8_t powerToggle = 1;
  uint8_t builtin = 1;          // 1 + index in machineBuiltins(); 0: the owner's own
  uint16_t selfStopS = 0;       // the machine ends a shot itself after this long; 0: not known / never
  // Where it stands with the shared library, as for a coffee or a grinder
  // (core/Share.h): ShareState, the record it was last sent as, what was
  // sent, what a reviewer said, and its entry in the library's list.
  uint8_t shareState = 0;
  uint32_t shareSeq = 0;
  uint32_t shareHash = 0;       // machineHash() of what was sent
  char reviewNote[NOTES_LEN] = {};
  char libraryId[LIBRARY_ID_LEN] = {};
  // Added at the end of the record. The machine switches itself off after this many minutes with
  // nothing done on it (core/PowerControl.h setAutoOffMin); 0: it does not, or not known. Kept on
  // this controller only (an owner's setting, not part of a shared description). The Breville Dual
  // Boiler (BES920XL instruction book, "Power Save Mode"): power save after 1 hour idle, off
  // completely after 4 hours unused; Auto Off is on by default, and its menu setting (A OF) only
  // turns it on or off.
  uint16_t autoOffMin = 240;

  MachineStyle machineStyle() const { return static_cast<MachineStyle>(style); }
  // The brew bot starts the shot.
  bool controllerStarts() const { return machineStyle() != MachineStyle::MANUAL; }
  // A bot stops the shot at the target. Otherwise the target is a cue.
  bool controllerStops() const {
    return machineStyle() == MachineStyle::TOGGLE || machineStyle() == MachineStyle::START_STOP_BUTTONS ||
           machineStyle() == MachineStyle::HOLD_BUTTON;
  }
  // The stop is a press of another button, by the stop bot.
  bool separateStop() const { return machineStyle() == MachineStyle::START_STOP_BUTTONS; }
  // The brew bot holds the button down for the whole shot.
  bool holdsForShot() const { return machineStyle() == MachineStyle::HOLD_BUTTON; }
  bool startsOnFirstDrip() const {
    return machineStyle() == MachineStyle::MANUAL &&
           manualStart == static_cast<uint8_t>(ManualStart::FIRST_DRIP);
  }
};

inline const char *machineStyleName(uint8_t style) {
  switch (static_cast<MachineStyle>(style)) {
    case MachineStyle::TOGGLE: return "toggle_button";
    case MachineStyle::START_ONLY: return "start_button";
    case MachineStyle::MANUAL: return "manual";
    case MachineStyle::START_STOP_BUTTONS: return "start_stop_buttons";
    case MachineStyle::HOLD_BUTTON: return "hold_button";
  }
  return "toggle_button";
}

inline uint8_t machineStyleFromName(const char *name) {
  for (uint8_t style = 0; style <= MACHINE_STYLE_LAST; ++style) {
    if (name && strcmp(name, machineStyleName(style)) == 0) return style;
  }
  return static_cast<uint8_t>(MachineStyle::TOGGLE);
}

inline MachineProfile makeMachine(const char *maker, const char *model, const char *control,
                                  MachineStyle style, bool holdPreinfusion, bool tapPreinfusion,
                                  bool powerToggle, uint8_t builtin) {
  MachineProfile m;
  strncpy(m.maker, maker, sizeof(m.maker) - 1);
  m.maker[sizeof(m.maker) - 1] = '\0';
  strncpy(m.model, model, sizeof(m.model) - 1);
  m.model[sizeof(m.model) - 1] = '\0';
  strncpy(m.control, control, sizeof(m.control) - 1);
  m.control[sizeof(m.control) - 1] = '\0';
  m.style = static_cast<uint8_t>(style);
  m.holdPreinfusion = holdPreinfusion ? 1 : 0;
  m.tapPreinfusion = tapPreinfusion ? 1 : 0;
  m.powerToggle = powerToggle ? 1 : 0;
  m.builtin = builtin;
  m.autoOffMin = 0;  // a general profile: not known
  return m;
}

constexpr size_t MACHINE_BUILTIN_COUNT = 6;

// The profiles that come with the firmware. 0 is the machine this was built on.
inline MachineProfile machineBuiltin(size_t index) {
  switch (index) {
    default:
    case 0: return MachineProfile();
    case 1:
      return makeMachine("Any", "One button, press to start and to stop", "brew button",
                         MachineStyle::TOGGLE, false, false, false, 2);
    case 2:
      return makeMachine("Any", "Dose button, the machine stops itself", "dose button",
                         MachineStyle::START_ONLY, false, false, false, 3);
    case 3:
      return makeMachine("Any", "Lever, knob, wheel or paddle", "by hand",
                         MachineStyle::MANUAL, false, false, false, 4);
    case 4:
      return makeMachine("Any", "Separate start and stop buttons", "start and stop buttons",
                         MachineStyle::START_STOP_BUTTONS, false, false, false, 5);
    case 5:
      return makeMachine("Any", "Button held down for the whole shot", "brew button, held",
                         MachineStyle::HOLD_BUTTON, false, false, false, 6);
  }
}

inline void sanitizeMachine(MachineProfile &m) {
  m.maker[sizeof(m.maker) - 1] = m.model[sizeof(m.model) - 1] = '\0';
  m.control[sizeof(m.control) - 1] = m.libraryId[sizeof(m.libraryId) - 1] = '\0';
  m.reviewNote[sizeof(m.reviewNote) - 1] = '\0';
  if (m.style > MACHINE_STYLE_LAST) m = MachineProfile();
  if (m.shareState > 6) m.shareState = 0;  // ShareState::REFUSED
  if (m.manualStart > static_cast<uint8_t>(ManualStart::FIRST_DRIP)) m.manualStart = 0;
  m.holdPreinfusion = m.holdPreinfusion ? 1 : 0;
  m.tapPreinfusion = m.tapPreinfusion ? 1 : 0;
  m.powerToggle = m.powerToggle ? 1 : 0;
  if (m.builtin > MACHINE_BUILTIN_COUNT) m.builtin = 0;
  // Only a button the bot holds can be held.
  if (m.machineStyle() != MachineStyle::TOGGLE) m.holdPreinfusion = 0;
  if (m.selfStopS > 600) m.selfStopS = 0;
  if (m.autoOffMin > 1440) m.autoOffMin = 0;
}

// Of what a machine record says: what it is and how it is worked.
inline uint32_t machineHash(const MachineProfile &m) {
  uint32_t h = 2166136261u;
  auto mix = [&h](const void *data, size_t n) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
  };
  mix(m.maker, strlen(m.maker));
  mix("|", 1);
  mix(m.model, strlen(m.model));
  mix("|", 1);
  mix(m.control, strlen(m.control));
  const uint8_t rules[] = {m.style, m.manualStart, m.holdPreinfusion, m.tapPreinfusion, m.powerToggle,
                           static_cast<uint8_t>(m.selfStopS >> 8), static_cast<uint8_t>(m.selfStopS)};
  mix(rules, sizeof(rules));
  return h;
}

inline bool machineChangedSinceShared(const MachineProfile &m) {
  return m.shareState != 0 && machineHash(m) != m.shareHash;
}

// Worth sending: the owner's own description, named, and never sent or
// changed since. One picked from the library's list is there already, and
// one that came with the firmware needs no entry.
inline bool machineCanBeShared(const MachineProfile &m) {
  return m.builtin == 0 && m.maker[0] != '\0' && m.model[0] != '\0' &&
         (m.shareState == 0 || m.shareState == 6 /* ShareState::REFUSED: send again */ ||
          machineChangedSinceShared(m));
}

// QUEUED = 1 and PUBLISHED = 4 of ShareState (core/Share.h).
inline void markMachineQueued(MachineProfile &m, uint32_t seq) {
  m.shareState = 1;
  m.shareSeq = seq;
  m.shareHash = machineHash(m);
  m.reviewNote[0] = m.libraryId[0] = '\0';
}

// Picked from the library's list: published already.
inline void markMachineFromLibrary(MachineProfile &m, const char *libraryId) {
  m.shareState = 4;
  m.shareSeq = 0;
  strncpy(m.libraryId, libraryId, sizeof(m.libraryId) - 1);
  m.libraryId[sizeof(m.libraryId) - 1] = '\0';
  m.reviewNote[0] = '\0';
  m.shareHash = machineHash(m);
}

}  // namespace bbw
