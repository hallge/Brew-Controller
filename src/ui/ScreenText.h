#pragma once
// The words on the controller's screen, as pure functions over plain inputs (no LVGL, no app::
// calls), so the board's screen (ui/Ui.cpp) and the iPhone app (through GET /api/screen) show the
// same text, character for character. What stays in Ui.cpp is only what depends on the glass:
// fonts, measuring a line's width to pick a shorter wording, and the touch key's arrow.
//
// Where the screen picks the longest wording that fits, the builders give every wording (text,
// shorter, shortest...) and the caller chooses. Buffer sizes are those the screen always used, so
// a long name is cut at the same place.
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/GrindAdvisor.h"
#include "core/Library.h"
#include "core/SettingFormat.h"
#include "core/Types.h"

namespace bbw {
namespace screen {

// The colour role a piece of text or a dot takes (ui/Palette.h names the roles).
enum class Tone : uint8_t { TEXT, DIM, OK, BAD, SLEEP, ACCENT };

inline const char *toneName(Tone t) {
  switch (t) {
    case Tone::DIM: return "dim";
    case Tone::OK: return "ok";
    case Tone::BAD: return "bad";
    case Tone::SLEEP: return "sleep";
    case Tone::ACCENT: return "accent";
    default: return "text";
  }
}

namespace detail {
inline void copy(char *out, size_t size, const char *in) {
  if (!size) return;
  size_t n = 0;
  for (; in && in[n] && n + 1 < size; ++n) out[n] = in[n];
  out[n] = '\0';
}
inline void append(char *out, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
inline void append(char *out, size_t size, const char *fmt, ...) {
  const size_t used = strlen(out);
  if (used + 1 >= size) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(out + used, size - used, fmt, args);
  va_end(args);
}
}  // namespace detail

// A grinder's setting, change and unit as this grinder's dial reads them (core/SettingFormat.h).
// Several results can go into one line: each call has its own buffer, six settings and three
// changes deep, as the screen always had.
class Dial {
 public:
  explicit Dial(const Grinder &grinder) : grinder_(grinder) {}
  const char *unit() const { return grinder_.unit; }
  const char *gs(uint16_t value) {
    char *out = settings_[nextSetting_++ % 6];
    formatSetting(value, grinder_, out, sizeof(settings_[0]));
    return out;
  }
  const char *gd(int delta) {
    char *out = deltas_[nextDelta_++ % 3];
    formatSettingDelta(delta, grinder_, out, sizeof(deltas_[0]));
    return out;
  }
  const char *gdu() const { return settingDeltaUnit(grinder_); }
  const Grinder &grinder() const { return grinder_; }

 private:
  const Grinder &grinder_;
  char settings_[6][12] = {};
  char deltas_[3][12] = {};
  uint8_t nextSetting_ = 0, nextDelta_ = 0;
};

// ------------------------------------------------------------------ faults --
// Short enough to fit beside the OK key; the exact state name is still what the web page, the log
// and the CSV use.
inline const char *faultTitle(State state) {
  switch (state) {
    case State::SCALE_DISCONNECTED: return "SCALE DISCONNECTED";
    case State::SWITCHBOT_DISCONNECTED: return "BOT NOT CONNECTED";
    case State::SCALE_DATA_STALE: return "SCALE WENT SILENT";
    case State::START_COMMAND_FAILED: return "START NOT CONFIRMED";
    case State::STOP_COMMAND_FAILED: return "STOP NOT CONFIRMED";
    case State::TARE_FAILED: return "TARE FAILED";
    case State::SHOT_TIMEOUT: return "SHOT TIMED OUT";
    case State::INVALID_WEIGHT: return "WEIGHT NOT VALID";
    case State::BLE_ERROR: return "BLUETOOTH ERROR";
    default: return stateName(state);
  }
}

inline const char *faultMessage(State state, bool manualStop, bool stopStartedPump) {
  if (manualStop) return "STOP THE MACHINE BY HAND.\nThe stop press could not be delivered or confirmed.";
  if (state == State::START_COMMAND_FAILED && stopStartedPump) {
    return "The start press never reached the button, so STOP started the pump. It was stopped "
           "again when coffee appeared. Check the Bot's arm.";
  }
  switch (state) {
    case State::SCALE_DISCONNECTED: return "The scale is not connected. Switch it on.";
    case State::SWITCHBOT_DISCONNECTED: return "The brew bot did not answer. Check it is paired, in range and has battery.";
    case State::SCALE_DATA_STALE: return "The scale stopped sending weight. The shot was stopped.";
    case State::START_COMMAND_FAILED: return "The start press was not confirmed by coffee flow. Check the machine.";
    case State::STOP_COMMAND_FAILED: return "The stop press was not confirmed. Check the machine.";
    case State::TARE_FAILED: return "The scale did not settle at zero. Keep the cup still and start again.";
    case State::SHOT_TIMEOUT: return "Maximum shot time reached. The shot was stopped.";
    case State::INVALID_WEIGHT: return "Implausible weight (cup removed?). The shot was stopped.";
    case State::BLE_ERROR: return "Bluetooth failed to start. Restart the controller.";
    default: return "";
  }
}

// ------------------------------------------------------- recipe and roast --
// Days from 1970-01-01 to a YYYYMMDD date (proleptic Gregorian).
inline long daysFromCivil(uint32_t yyyymmdd) {
  long y = yyyymmdd / 10000;
  const unsigned m = (yyyymmdd / 100) % 100, d = yyyymmdd % 100;
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}

// "12 d off roast", or the date itself until the clock has been set (nowEpoch 0). Brief, after the
// roast level: "12 d", or the date alone.
inline void describeRoastAge(const Recipe &r, uint32_t nowEpoch, bool brief, char *out, size_t size) {
  out[0] = '\0';
  if (r.roastDate == 0) return;
  const long days = nowEpoch ? static_cast<long>(nowEpoch / 86400) - daysFromCivil(r.roastDate) : -1;
  if (days >= 0 && days < 1000) {
    snprintf(out, size, brief ? "%ld d" : "%ld d off roast", days);
  } else {
    snprintf(out, size, brief ? "%04u-%02u-%02u" : "roasted %04u-%02u-%02u",
             static_cast<unsigned>(r.roastDate / 10000), static_cast<unsigned>((r.roastDate / 100) % 100),
             static_cast<unsigned>(r.roastDate % 100));
  }
}

inline void describePreinfusion(const Recipe &r, char *out, size_t size) {
  if (r.preinfusion == Preinfusion::HOLD) {
    if (r.bloomPauseMs) {
      snprintf(out, size, "pre %.1f s, bloom %.0f s", r.preinfusionHoldMs / 1000.0f, r.bloomPauseMs / 1000.0f);
    } else {
      snprintf(out, size, "pre-inf hold %.1f s", r.preinfusionHoldMs / 1000.0f);
    }
  } else {
    snprintf(out, size, "pre-inf machine");
  }
}

// One entry of the recipe list: the coffee, then the recipe with what it makes and the grind (a
// recipe listed by itself has its name on the first line).
inline void recipeListLine(const Recipe &r, Dial &dial, char *out, size_t size) {
  char grind[24] = "";
  if (r.grindUm) snprintf(grind, sizeof(grind), "  %s %s", dial.gs(r.grindUm), dial.unit());
  if (r.coffee[0]) {
    snprintf(out, size, "%s\n%s  %.1f -> %.1f g%s", r.coffee, r.name, r.doseG, r.targetWeightG, grind);
  } else {
    snprintf(out, size, "%s\n%.1f -> %.1f g%s", r.name, r.doseG, r.targetWeightG, grind);
  }
}

// The edit screen's field: its title, the value and the hint beside it. `size` is the room of each
// of the three buffers (the screen passes the title's, 48).
inline void describeEditField(const Recipe &r, int field, Dial &dial, const GrindAdvice &g, char *title,
                              char *value, char *hint, size_t size) {
  switch (field) {
    case 0:
      if (r.useRatio) {
        snprintf(title, size, "RATIO");
        snprintf(value, size, "1:%.1f", r.ratio);
        snprintf(hint, size, "%.1f g in gives\n%.1f g out", r.doseG, r.doseG * r.ratio);
      } else {
        snprintf(title, size, "TARGET OUT");
        snprintf(value, size, "%.1f g", r.targetWeightG);
        snprintf(hint, size, "tap 0.5 g,\nhold for 0.1 g");
      }
      break;
    case 1:
      snprintf(title, size, "DOSE IN");
      snprintf(value, size, "%.1f g", r.doseG);
      snprintf(hint, size, "dry coffee in\nthe basket");
      break;
    case 2:
      snprintf(title, size, "GRIND");
      if (r.grindUm) snprintf(value, size, "%s %s", dial.gs(r.grindUm), dial.unit());
      else snprintf(value, size, "not set");
      if (g.action == GrindAction::FINER || g.action == GrindAction::COARSER) {
        const char *way = g.action == GrindAction::FINER ? "finer" : "coarser";
        if (adviceHasNumber(g)) snprintf(hint, size, "advice: go %s,\nset %s %s", way, dial.gs(g.suggestedUm), dial.unit());
        else snprintf(hint, size, "advice: go %s\nhow far: see the\nweb page", way);
      } else {
        snprintf(hint, size, "number on the\ngrinder: tap 1,\nhold for 10");
      }
      break;
    case 3:
      snprintf(title, size, "TARGET TIME");
      snprintf(value, size, "%.1f s", r.targetTimeS);
      snprintf(hint, size, "brew time the\ngrind advice\naims for");
      break;
    default:
      snprintf(title, size, "PRE-INFUSION");
      if (r.preinfusion == Preinfusion::HOLD) {
        snprintf(value, size, "HOLD %.1f s", r.preinfusionHoldMs / 1000.0f);
        snprintf(hint, size, "pre-infusion lasts\nonly as long as\nthe hold");
      } else {
        snprintf(value, size, "MACHINE");
        snprintf(hint, size, "press and release:\nthe machine\npre-infuses");
      }
      break;
  }
}

// The key that takes the advised setting on the edit screen's grind field.
inline void editSetAdviceLabel(Dial &dial, uint16_t advised, char *out, size_t size) {
  snprintf(out, size, "SET %s %s", dial.gs(advised), dial.unit());
}

// ------------------------------------------------------------------- home --
// What the home screen's inputs are besides the live status (each is its own call on the board).
struct HomeState {
  bool scaleResting = false;       // an idle scale let go so that it can switch itself off
  bool scaleStartPending = false;  // a start waiting for the scale to come back
  bool scaleNearby = false;        // the resting scale is within reach: Start brings it back
};

struct HomeStatusText {
  char status[40] = "";      // "READY  36.2 g", "NO SCALE"...
  char hint[40] = "";        // under the card; empty while hintIsTouch
  bool hintIsTouch = false;  // the hint names the touch key: the screen writes it with its arrow, the app with its own start key
  Tone hintTone = Tone::DIM;
  Tone scaleDot = Tone::BAD, botDot = Tone::BAD;
  int scaleBattery = -1, botBattery = -1;  // percent; -1 = not shown
  Tone scaleBatteryTone = Tone::DIM, botBatteryTone = Tone::DIM;
  bool botWaking = false;  // ready, bot wanted but not linked yet (amber)
  bool canStart = false;   // the hint is a key that starts the shot
};

inline void homeStatusText(const LiveStatus &st, const HomeState &hs, HomeStatusText &o) {
  const bool manual = st.machineStyle == static_cast<uint8_t>(MachineStyle::MANUAL);
  o = HomeStatusText();
  const bool ready = st.state == State::READY;
  o.botWaking = ready && st.botPaired && !st.botConnected && !manual;
  if (!ready) {
    detail::copy(o.status, sizeof(o.status), hs.scaleResting ? "SCALE RESTING" : "NO SCALE");
  } else if (!st.botPaired && !manual) {
    detail::copy(o.status, sizeof(o.status), "NO BOT");
  } else if (st.cupRequired && !st.cupPresent) {
    snprintf(o.status, sizeof(o.status), "NO CUP  %.1f g", st.weightG);
  } else if (o.botWaking) {
    // BREW still works: the shot starts as soon as the bot answers.
    snprintf(o.status, sizeof(o.status), "WAKING BOT  %.1f g", st.weightG);
  } else {
    snprintf(o.status, sizeof(o.status), "READY  %.1f g", st.weightG);
  }
  // Red: no scale. Amber: scale, but no cup on it yet. Green: ready to brew.
  const bool cupMissing = st.state == State::READY && st.cupRequired && !st.cupPresent;
  o.scaleDot = !st.scaleConnected ? Tone::BAD : (cupMissing ? Tone::ACCENT : Tone::OK);
  o.scaleBattery = -1;
  if (st.scaleConnected && st.scaleBatteryPercent >= 0) {
    o.scaleBattery = st.scaleBatteryPercent;
    o.scaleBatteryTone = st.scaleBatteryPercent <= 15 ? Tone::ACCENT : Tone::DIM;
  }
  // Grey: paired and asleep, which is the normal idle state.
  o.botDot = st.botConnected ? Tone::OK : o.botWaking ? Tone::ACCENT : st.botPaired ? Tone::SLEEP : Tone::BAD;
  o.botBattery = -1;
  if (st.botBatteryPercent >= 0) {
    o.botBattery = st.botBatteryPercent;
    o.botBatteryTone = st.botBatteryPercent <= 15 ? Tone::ACCENT : Tone::DIM;
  }

  const bool startPending = !ready && hs.scaleStartPending;
  const bool scaleNear = !ready && !startPending && hs.scaleNearby;
  o.hintIsTouch = false;
  if (startPending) {
    detail::copy(o.hint, sizeof(o.hint), "CONNECTING TO THE SCALE");
    o.hintTone = Tone::DIM;
  } else if (scaleNear) {
    o.hintIsTouch = true;
    o.hintTone = Tone::OK;
  } else if (!ready) {
    detail::copy(o.hint, sizeof(o.hint), "SWITCH ON THE SCALE");
    o.hintTone = Tone::DIM;
  } else if (!st.botPaired && !manual) {
    detail::copy(o.hint, sizeof(o.hint), "PAIR THE BOT IN SETTINGS");
    o.hintTone = Tone::DIM;
  } else if (!st.weightFresh) {
    detail::copy(o.hint, sizeof(o.hint), "WAITING FOR WEIGHT");
    o.hintTone = Tone::DIM;
  } else if (st.cupRequired && !st.cupPresent) {
    detail::copy(o.hint, sizeof(o.hint), "PLACE CUP ON SCALE");
    o.hintTone = Tone::ACCENT;
  } else {
    o.hintIsTouch = true;
    o.hintTone = Tone::OK;
  }
  // A key only while it offers the start; plain text otherwise.
  o.canStart = scaleNear || (ready && (st.botPaired || manual) && st.weightFresh && !(st.cupRequired && !st.cupPresent));
}

// The recipe card on the home screen. Each line that can be too wide for the glass has a shorter
// wording; the screen takes the longest that fits.
struct HomeCard {
  char name[COFFEE_NAME_LEN > RECIPE_NAME_LEN ? COFFEE_NAME_LEN : RECIPE_NAME_LEN] = "";  // the coffee heads the card; a recipe listed by itself heads its own
  char yield[64] = "";       // "Triple - 24.0 in -> 48.0 out"
  char yieldRatio[64] = "";  // the same with "  1:2.0" after it (only meant while useRatio)
  bool useRatio = false;
  char grind[40] = "";       // "grind 246 um" / "grind not set"
  char advice[64] = "";      // the advisor's verdict or where to start; "" when there is none
  char adviceShort[64] = "";
  char adviceShortest[64] = "";  // community advice without its unit; else the same as adviceShort
  Tone adviceTone = Tone::ACCENT;
  char pre[48] = "";         // pre-infusion / bloom
  char preShort[48] = "";
  char foot[32] = "";        // "brew time 28 s"
};

// communityStart and similarStart are the settings those sources offer for the active coffee (0:
// none). The setting to go to is only shown once the advice's sensitivity was measured from this
// grinder's own shots, as on the result screen.
inline void homeCardText(const Recipe &r, const GrindAdvice &g, Dial &dial, uint16_t communityStart,
                         uint16_t similarStart, HomeCard &o) {
  const char *unit = dial.unit();
  detail::copy(o.name, sizeof(o.name), r.coffee[0] ? r.coffee : r.name);
  // With the coffee as the heading, this line names the recipe.
  if (r.coffee[0]) snprintf(o.yield, sizeof(o.yield), "%s - %.1f in -> %.1f out", r.name, r.doseG, r.targetWeightG);
  else snprintf(o.yield, sizeof(o.yield), "%.1f g in -> %.1f g out", r.doseG, r.targetWeightG);
  snprintf(o.yieldRatio, sizeof(o.yieldRatio), "%s  1:%.1f", o.yield, r.ratio);
  o.useRatio = r.useRatio;

  if (r.grindUm) snprintf(o.grind, sizeof(o.grind), "grind %s %s", dial.gs(r.grindUm), unit);
  else snprintf(o.grind, sizeof(o.grind), "grind not set");
  char *text = o.advice, *shorter = o.adviceShort;
  const size_t ts = sizeof(o.advice), ss = sizeof(o.adviceShort);
  o.adviceShortest[0] = '\0';
  if (r.grindUm && (g.action == GrindAction::FINER || g.action == GrindAction::COARSER)) {
    const char *way = g.action == GrindAction::FINER ? "finer" : "coarser";
    if (adviceHasNumber(g)) {
      const char *to = dial.gs(g.suggestedUm);
      if (g.refined) snprintf(text, ts, "liked: %s %s %s", way, to, unit);
      else snprintf(text, ts, "go %s: %s %s", way, to, unit);
      snprintf(shorter, ss, "%s%s %s", g.refined ? "liked: " : "", way, to);
    } else {
      snprintf(text, ts, "go %s", way);
      snprintf(shorter, ss, "%s", way);
    }
  } else if (r.grindUm && g.action == GrindAction::INCONSISTENT) {
    snprintf(text, ts, "brew times vary");
    snprintf(shorter, ss, "times vary");
  } else if (r.grindUm && g.action == GrindAction::HOLD && g.betweenSteps) {
    snprintf(text, ts, "closest step to target");
    snprintf(shorter, ss, "closest to target");
  } else if (r.grindUm && g.action == GrindAction::HOLD) {
    snprintf(text, ts, "brew time on target");
    snprintf(shorter, ss, "time on target");
  } else if (!r.grindUm && communityStart != 0) {
    // No setting entered yet, and the shared library has somewhere to start.
    const char *start = dial.gs(communityStart);
    snprintf(text, ts, "community: start %s %s", start, unit);
    snprintf(shorter, ss, "community %s %s", start, unit);
    snprintf(o.adviceShortest, sizeof(o.adviceShortest), "community %s", start);  // the unit is on the edit screen
  } else if (!r.grindUm && r.coffeeId != 0 && similarStart != 0) {
    // Nothing from the library either: the owner's own most similar coffee (a guess).
    const char *start = dial.gs(similarStart);
    snprintf(text, ts, "similar: try %s %s", start, unit);
    snprintf(shorter, ss, "similar: %s %s", start, unit);
  } else if (!r.grindUm && grinderFirstGuess(dial.grinder()).setting != 0) {
    // Nothing else to go on: a first guess from the grinder's range.
    const char *start = dial.gs(grinderFirstGuess(dial.grinder()).setting);
    snprintf(text, ts, "first guess: try %s %s", start, unit);
    snprintf(shorter, ss, "guess: %s %s", start, unit);
  } else {
    text[0] = shorter[0] = '\0';
  }
  if (!o.adviceShortest[0]) detail::copy(o.adviceShortest, sizeof(o.adviceShortest), shorter);
  o.adviceTone = g.action == GrindAction::HOLD ? Tone::OK : Tone::ACCENT;
  describePreinfusion(r, o.pre, sizeof(o.pre));
  snprintf(o.preShort, sizeof(o.preShort), "pre %.1f, bloom %.0f s", r.preinfusionHoldMs / 1000.0f,
           r.bloomPauseMs / 1000.0f);  // only ever needed with a bloom
  // The stop offset is learned and looks after itself; it is not shown here.
  snprintf(o.foot, sizeof(o.foot), "brew time %.0f s", r.targetTimeS);
}

// ------------------------------------------------------------------ brew --
struct BrewPhaseText {
  char phase[40] = "";     // the big status, lines split by \n
  char stop[16] = "STOP";  // the stop key's label
  Tone phaseTone = Tone::ACCENT;
  bool running = false;    // the machine has been started
};

// `confirming`: BOOT was pressed during the shot and the board asks before it stops it.
inline void brewPhaseText(const LiveStatus &st, bool confirming, BrewPhaseText &o) {
  const bool manual = st.machineStyle == static_cast<uint8_t>(MachineStyle::MANUAL);
  const char *phase = "";
  const char *stop = "STOP";
  o.phaseTone = Tone::ACCENT;
  o.running = false;
  char bloom[24];
  switch (st.state) {
    case State::TARING:
      phase = "AUTO TARE\nWAIT FOR ZERO";
      stop = "CANCEL";
      break;
    case State::WAITING_FOR_CUP:
      phase = "PLACE CUP\nON THE SCALE";
      stop = "CANCEL";
      break;
    case State::READY_TO_BREW:
      phase = st.botConnected || manual ? "TOUCH KEY\nTO BREW" : "WAKING BOT\nSTARTS WHEN LINKED";
      stop = "CANCEL";
      break;
    case State::STARTING_SHOT:
      phase = st.awaitingDrip ? "PULL THE SHOT\nWAITING FOR COFFEE"
              : st.holdingButton ? "PRE-INFUSION\nHOLDING BUTTON" : "STARTING";
      o.running = true;
      break;
    case State::BREWING:
      phase = st.targetCue ? "STOP NOW" : st.firstDropSeen ? "BREWING" : "PRE-INFUSION";
      o.running = true;
      break;
    case State::BLOOMING:
      snprintf(bloom, sizeof(bloom), "BLOOM\n%.0f s", st.bloomLeftS);
      phase = st.holdingButton ? "BLOOM DONE\nSTARTING" : bloom;
      o.running = true;
      break;
    case State::STOPPING_SHOT:
      phase = st.manualStopNeeded ? "STOP BY HAND!" : "STOPPING";
      if (st.manualStopNeeded) o.phaseTone = Tone::BAD;
      o.running = true;
      break;
    default:  // POST_SHOT_DRIP
      // After a stop with no coffee seen, the pump is watched for a while: one press toggles, so
      // the stop may have been a start.
      phase = st.verifyingStop ? "CHECKING\nPUMP IS OFF" : "FINALIZING\nLEAVE CUP";
      o.running = true;
      break;
  }
  if (confirming) {
    phase = "STOP SHOT?\nPRESS AGAIN";
    o.phaseTone = Tone::BAD;
    stop = "YES\nSTOP";
  }
  detail::copy(o.phase, sizeof(o.phase), phase);
  detail::copy(o.stop, sizeof(o.stop), stop);
}

// ---------------------------------------------------------------- result --
struct ResultInput {
  bool have = false;  // a last shot exists (shot below is valid)
  ShotRecord shot;
  State state = State::SHOT_COMPLETE;
  GrindTimeBasis basis = GrindTimeBasis::TOTAL;
  float toleranceS = 0.0f;  // Settings::grindToleranceS
};

struct ResultText {
  bool have = false;
  bool abnormal = false;  // anything but a normal finish: a red heading says what ended it
  bool timedOut = false;  // ran out of time (put to the user)
  Tone topTone = Tone::OK;
  char top[64] = "";     // heading, or "OVER +0.8 g   SLOW +1.2 s"
  char weight[24] = "";  // "36.1 g", with " *" when the weight did not settle
  char line1[96] = "";
  char line2[3][96] = {};  // wordings from the longest; the screen takes the first that fits
  uint8_t line2Count = 0;
  // The review question (IGNORE / COUNT) for an unusual shot, in place of the grind verdict.
  bool review = false;
  uint32_t shotId = 0;
  char reviewAdvice[48] = "";       // its advice line (the longest wording)
  char reviewText[160] = "";        // the whole question, with the longest advice
  char reviewTextShort[160] = "";   // ...and with the shorter one, if the advice line is too wide
  // Otherwise the grind verdict, and the keys beside READY.
  bool grindChange = false;  // the shot recommends a grind change: GRINDER key beside READY
  char grind[48] = "";
  char grindSub[144] = "";
  Tone grindTone = Tone::ACCENT;
  bool counted = false;
  GrindUse use = GrindUse::NO_TIME;
};

inline const char *endHeading(EndReason ended) {
  switch (ended) {
    case EndReason::TIMEOUT: return "SHOT TIMED OUT";
    // The pour stopped with nothing pressed here: the machine's own time-out, or someone pressed
    // its button. The scale cannot tell which.
    case EndReason::MACHINE_STOPPED: return "STOPPED AT THE MACHINE";
    case EndReason::CUP_REMOVED: return "CUP LIFTED: STOPPED";
    case EndReason::RECOVERED: return "STOPPED AFTER RESTART";
    default: return "SHOT ENDED EARLY";
  }
}

// Why a shot is not counted for grind advice, in the words of the result screen.
inline const char *notCountedReason(GrindUse use, EndReason ended) {
  switch (use) {
    case GrindUse::IGNORED_BY_USER: return "ignored for grind advice,\nas you chose";
    case GrindUse::NO_TIME: return "no first drops detected:\nnot counted for grind advice";
    case GrindUse::OTHER_BASIS: return "time basis was changed:\nnot counted for grind advice";
    case GrindUse::NOT_SETTLED: return "cup lifted, weight unsettled:\nnot counted for grind advice";
    case GrindUse::STOP_RETRIED: return "stop had to be repeated:\nnot counted for grind advice";
    case GrindUse::TOO_SHORT: return "under 15 s: check preparation;\ntap COUNT to use it";
    case GrindUse::NOT_WEIGHT_STOP:
      switch (ended) {
        case EndReason::USER: return "stopped with STOP:\nnot counted for grind advice";
        case EndReason::CUP_REMOVED: return "cup lifted during the shot:\nnot counted for grind advice";
        case EndReason::RECOVERED: return "restarted during the shot:\nnot counted for grind advice";
        default: return "did not stop on its weight:\nnot counted for grind advice";
      }
    default: return "this shot is not counted\nfor grind advice";
  }
}

// The result of a shot. Also used for a shot that ran out of time: the same details, under a
// heading that says so.
inline void resultText(const ResultInput &in, const GrindAdvice &g, Dial &dial, ResultText &o) {
  o = ResultText();
  const bool have = in.have;
  const ShotRecord &shot = in.shot;
  const EndReason ended = have ? static_cast<EndReason>(shot.endReason) : EndReason::NONE;
  o.have = have;
  o.abnormal = in.state != State::SHOT_COMPLETE;
  o.timedOut = o.abnormal && (ended == EndReason::TIMEOUT || ended == EndReason::MACHINE_STOPPED);
  if (have) {
    const float error = shot.finalWeightG - shotStopTargetG(shot);  // its own target, if it had one
    char yield[32];
    if (fabsf(error) < 0.05f) snprintf(yield, sizeof(yield), "ON TARGET");
    else snprintf(yield, sizeof(yield), "%s %+.1f g", error > 0 ? "OVER" : "UNDER", error);

    if (o.abnormal) {
      detail::copy(o.top, sizeof(o.top), endHeading(ended));
      o.topTone = Tone::BAD;
    } else {
      // How far off the weight, and how far off the brew time.
      const float brewS = grindShotTime(shot, in.basis);
      const float late = brewS - shot.targetTimeS;
      const bool timed = shot.targetTimeS > 0.0f && brewS > 0.0f;
      if (!timed) snprintf(o.top, sizeof(o.top), "%s", yield);
      else if (fabsf(late) < 0.05f) snprintf(o.top, sizeof(o.top), "%s   ON TIME", yield);
      else snprintf(o.top, sizeof(o.top), "%s   %s %+.1f s", yield, late > 0 ? "SLOW" : "FAST", late);
      const bool good = fabsf(error) <= 0.5f && (!timed || fabsf(late) <= in.toleranceS);
      o.topTone = good ? Tone::OK : Tone::ACCENT;
    }
    // An asterisk marks a weight that did not settle (cup lifted early).
    snprintf(o.weight, sizeof(o.weight), "%.1f g%s", shot.finalWeightG, (shot.flags & SHOT_FLAG_FINAL_VALID) ? "" : " *");
    if (o.abnormal && shot.pourTimeS > 0.0f) {
      snprintf(o.line1, sizeof(o.line1), "%s   %.1f s   pour %.1f s", yield, shot.shotTimeS, shot.pourTimeS);
    } else if (o.abnormal) {
      snprintf(o.line1, sizeof(o.line1), "%s   %.1f s", yield, shot.shotTimeS);
    } else if (shot.pourTimeS > 0.0f) {
      snprintf(o.line1, sizeof(o.line1), "%.1f s total   pour %.1f s", shot.shotTimeS, shot.pourTimeS);
    } else {
      snprintf(o.line1, sizeof(o.line1), "%.1f s", shot.shotTimeS);
    }
    const float flow = shot.pourTimeS > 0.0f ? shot.pourFlowGS : shot.averageFlowGS;
    const float change = shot.nextStopOffsetG - shot.stopOffsetG;
    if ((shot.flags & SHOT_FLAG_LEARNED) && fabsf(change) >= 0.005f) {
      // A larger offset means the next shot is stopped earlier. The longest line on the screen:
      // tighter wordings, for the screen to take the first that fits.
      const char *way = change > 0 ? "earlier" : "later";
      snprintf(o.line2[0], 64, "%.2f g/s  drip %.1f g  auto %s %.2f", flow, shot.dripWeightG, way, fabsf(change));
      snprintf(o.line2[1], 64, "%.1f g/s  drip %.1f g  auto %s %.2f", flow, shot.dripWeightG, way, fabsf(change));
      snprintf(o.line2[2], 64, "%.1f g/s drip %.1f g %s %.2f", flow, shot.dripWeightG, way, fabsf(change));
      o.line2Count = 3;
    } else {
      snprintf(o.line2[0], sizeof(o.line2[0]), "%.2f g/s   drip %.1f g", flow, shot.dripWeightG);
      o.line2Count = 1;
    }
  }

  // Grinder verdict. Two different things are kept apart here: how THIS shot ran, and what the
  // recent shots at this setting say about the grind. A single fast shot among consistent ones is
  // reported as fast, with the grind left alone until it happens again.
  o.use = have ? grindShotUse(shot, in.basis) : GrindUse::NO_TIME;
  o.counted = o.use == GrindUse::COUNTED;
  const float thisTime = have ? grindShotTime(shot, in.basis) : 0.0f;

  // Ask rather than decide, when the shot was unusual and has not been ruled on yet: a one-off
  // among consistent shots, or one that ran out of time.
  const bool undecided = have && shot.grindVote == static_cast<uint8_t>(GrindVote::AUTO) && shot.grindUm != 0 && thisTime > 0.0f;
  const bool oneOff = undecided && o.counted && g.newestIgnored;
  // ...or one that reached its weight but so far off the target time that it falls outside what
  // is counted automatically.
  const bool offTime = undecided && (o.use == GrindUse::TOO_SHORT || o.use == GrindUse::TOO_LONG);
  o.review = oneOff || offTime || (undecided && o.timedOut);
  // A grind change recommended by this shot: offer the way to the grinder setting beside READY.
  o.grindChange = !o.review && have && o.counted && !g.newestIgnored &&
                  (g.action == GrindAction::FINER || g.action == GrindAction::COARSER);
  if (o.review) {
    o.shotId = shot.id;
    // A shot that ran out of time was slow by definition.
    const bool fast = !o.timedOut && thisTime < shot.targetTimeS;
    if (g.firstDropOneOffS != 0.0f) {
      // The first drops came far from their usual time: the puck, not the grind.
      snprintf(o.reviewText, sizeof(o.reviewText), "First drops %.0f s %s.\nReview preparation routine.\nGrind kept for now.",
               fabsf(g.firstDropOneOffS), g.firstDropOneOffS < 0.0f ? "early" : "late");
      detail::copy(o.reviewTextShort, sizeof(o.reviewTextShort), o.reviewText);
    } else {
      // The advice goes with the warning. A shot too far off to count still says which way to go
      // and by how much (adviseOutOfRange); leaving the setting off this screen left the owner to guess.
      const bool way = g.action == (fast ? GrindAction::FINER : GrindAction::COARSER);
      const char *dir = fast ? "finer" : "coarser";
      char shorter[48];
      snprintf(o.reviewAdvice, sizeof(o.reviewAdvice), "Grind %s if necessary.", dir);
      detail::copy(shorter, sizeof(shorter), o.reviewAdvice);
      if (way && adviceHasNumber(g) && g.suggestedUm) {
        snprintf(o.reviewAdvice, sizeof(o.reviewAdvice), "Advice: %s, set %s %s", dir, dial.gs(g.suggestedUm), dial.unit());
        // One line only: the screen's label has room for three.
        snprintf(shorter, sizeof(shorter), "Advice: set %s %s", dial.gs(g.suggestedUm), dial.unit());
      } else if (way) {
        snprintf(o.reviewAdvice, sizeof(o.reviewAdvice), "Advice: grind %s.", dir);
        detail::copy(shorter, sizeof(shorter), o.reviewAdvice);
      }
      snprintf(o.reviewText, sizeof(o.reviewText), "Shot %s than expected.\nReview preparation routine.\n%s",
               fast ? "faster" : "slower", o.reviewAdvice);
      snprintf(o.reviewTextShort, sizeof(o.reviewTextShort), "Shot %s than expected.\nReview preparation routine.\n%s",
               fast ? "faster" : "slower", shorter);
    }
    return;
  }

  o.grindTone = Tone::ACCENT;
  char *sub = o.grindSub;
  const size_t ss = sizeof(o.grindSub);
  if (g.action == GrindAction::NO_GRIND_SET) {
    detail::copy(sub, ss, "set the grind in EDIT\nfor grind advice");
  } else if (!o.counted) {
    detail::copy(sub, ss, notCountedReason(o.use, ended));  // say why, in each case
  } else if (g.newestIgnored) {
    detail::copy(o.grind, sizeof(o.grind), thisTime < shot.targetTimeS ? "BREW FAST   ONE-OFF?" : "BREW SLOW   ONE-OFF?");
    snprintf(sub, ss, "%.1f s vs %.0f s: grind kept,\nadvice if it repeats", thisTime, shot.targetTimeS);
  } else if (g.checkPrep) {
    // Fast and uneven, or fast and sour: more likely the puck than the grind (audit G6); no change.
    detail::copy(o.grind, sizeof(o.grind), "CHECK PREPARATION   GRIND KEPT");
    snprintf(sub, ss, "%.1f s, %s:\ncheck dose and tamp, pull again", thisTime,
             g.checkPrepWhy == CHECK_PREP_UNEVEN ? "flow uneven" : "fast and sour");
  } else if (g.action == GrindAction::FINER || g.action == GrindAction::COARSER) {
    const bool finer = g.action == GrindAction::FINER;
    detail::copy(o.grind, sizeof(o.grind), finer ? "BREW FAST   GRIND FINER" : "BREW SLOW   GRIND COARSER");
    // The amount is only shown once it rests on a sensitivity measured from this grinder's own history.
    if (adviceHasNumber(g) && g.notePlay) {
      // Turning the dial back: the number includes the adjustment's play (GrindAdvice::notePlay).
      snprintf(sub, ss, "%s %s %s  ->  set %s %s\nincl. %s %s play", dial.gd(g.deltaUm), dial.gdu(),
               finer ? "finer" : "coarser", dial.gs(g.suggestedUm), dial.unit(),
               dial.gd(static_cast<int>(lroundf(g.backlashAddedSteps))), dial.gdu());
    } else if (adviceHasNumber(g)) {
      snprintf(sub, ss, "%s %s %s  ->  set %s %s", dial.gd(g.deltaUm), dial.gdu(), finer ? "finer" : "coarser",
               dial.gs(g.suggestedUm), dial.unit());
    } else {
      detail::copy(sub, ss, "see web page for details");
    }
  } else if (g.action == GrindAction::INCONSISTENT) {
    detail::copy(o.grind, sizeof(o.grind), "BREW TIMES VARY   NO ADVICE");
    snprintf(sub, ss, "%.0f to %.0f s at %s %s:\ncheck the preparation", g.minTimeS, g.maxTimeS,
             dial.gs(g.currentUm), dial.unit());
  } else if (g.action == GrindAction::HOLD) {
    detail::copy(o.grind, sizeof(o.grind), "BREW TIME OK   KEEP GRIND");
    o.grindTone = Tone::OK;
    if (g.shotsUsed > 1) {
      snprintf(sub, ss, "this shot %.1f s, last %u\naverage %.1f s, target %.0f s", thisTime, g.shotsUsed,
               g.averageTimeS, g.targetTimeS);
    } else {
      snprintf(sub, ss, "%.1f s, target %.0f s", thisTime, g.targetTimeS);
    }
  }
}

// ------------------------------------------------------- grind questions --
// The screen that asks about the grind: no setting yet, advice from the last shot not acted on, or
// a recommendation for a coffee or bag just put in use.
struct GrindPromptText {
  char title[24] = "";
  char setting[96] = "";  // the big line
  char why[200] = "";
  char use[48] = "";   // the left key
  char keep[48] = "";  // the right key
  uint16_t useUm = 0;  // the setting the left key takes (0: none to take)
};

// The pair in use has no grind setting: ask for one before the shot, so that it counts for grind
// advice. `last` is the setting the newest shot of the pair was pulled at (0: none).
inline void noGrindText(const Recipe &active, uint16_t last, Dial &dial, GrindPromptText &o) {
  const uint16_t guess = last ? 0 : grinderFirstGuess(dial.grinder()).setting;
  const uint16_t start = last ? last : guess;
  o.useUm = start;
  const char *unit = dial.unit();
  detail::copy(o.title, sizeof(o.title), "SET THE GRIND");
  if (start) snprintf(o.setting, sizeof(o.setting), "%s %s", dial.gs(start), unit);
  else snprintf(o.setting, sizeof(o.setting), "Not set");
  const char *coffee = active.coffee[0] ? active.coffee : "This recipe";
  if (last) {
    snprintf(o.why, sizeof(o.why), "%s: no grind setting. The last shot was at %s %s. Set it before you brew.", coffee,
             dial.gs(last), unit);
  } else if (guess) {
    snprintf(o.why, sizeof(o.why), "%s: no grind setting. A first guess from the grinder's range. Set it before you brew.", coffee);
  } else {
    snprintf(o.why, sizeof(o.why), "%s: no grind setting. Set it before you brew.", coffee);
  }
  if (start) snprintf(o.use, sizeof(o.use), "SET\n%s", dial.gs(start));
  else snprintf(o.use, sizeof(o.use), "SET");
  detail::copy(o.keep, sizeof(o.keep), "NOT NOW");
}

// Grind advice from the last shot that was not acted on: the grinder is still at the setting that
// shot was made at.
inline void grindReminderText(const GrindAdvice &g, Dial &dial, GrindPromptText &o) {
  const bool finer = g.action == GrindAction::FINER;
  const bool number = adviceHasNumber(g);
  o.useUm = number ? g.suggestedUm : 0;
  detail::copy(o.title, sizeof(o.title), "GRIND REMINDER");
  const char *unit = dial.unit();
  if (number) snprintf(o.setting, sizeof(o.setting), "%s -> %s %s", dial.gs(g.currentUm), dial.gs(g.suggestedUm), unit);
  else snprintf(o.setting, sizeof(o.setting), "GRIND %s", finer ? "FINER" : "COARSER");
  if (g.shotsUsed > 1) {
    snprintf(o.why, sizeof(o.why),
             "The last %u shots averaged %.1f s (aim %.0f s): the advice was %s. The grinder is still set at %s %s.",
             static_cast<unsigned>(g.shotsUsed), g.averageTimeS, g.targetTimeS, finer ? "finer" : "coarser",
             dial.gs(g.currentUm), unit);
  } else {
    snprintf(o.why, sizeof(o.why),
             "The last shot ran %.1f s (aim %.0f s): the advice was %s. The grinder is still set at %s %s.",
             g.averageTimeS, g.targetTimeS, finer ? "finer" : "coarser", dial.gs(g.currentUm), unit);
  }
  if (number && g.notePlay) {
    detail::append(o.why, sizeof(o.why), " Incl. %s %s play.", dial.gd(static_cast<int>(lroundf(g.backlashAddedSteps))), dial.gdu());
  }
  if (number) snprintf(o.use, sizeof(o.use), "SET\n%s", dial.gs(g.suggestedUm));
  else snprintf(o.use, sizeof(o.use), "OK");
  detail::copy(o.keep, sizeof(o.keep), "NOT NOW");
}

enum class RecSource : uint8_t { COMMUNITY, SIMILAR, RANGE };

// The recommendation for a coffee or bag just put in use. `coffee` is the active recipe's coffee,
// `from` the similar coffee it came from.
inline void grindRecommendationText(RecSource source, uint16_t setting, uint16_t range, uint16_t current,
                                    uint16_t people, const char *from, const char *coffee, Dial &dial,
                                    GrindPromptText &o) {
  detail::copy(o.title, sizeof(o.title), "NEW GRIND ADVICE");
  o.useUm = setting;
  const char *unit = dial.unit();
  snprintf(o.setting, sizeof(o.setting), "%s %s", dial.gs(setting), unit);
  char now[16];
  detail::copy(now, sizeof(now), current ? dial.gs(current) : "");
  if (source == RecSource::COMMUNITY) {
    snprintf(o.why, sizeof(o.why), "%s: from the community (%u %s), +/- %u.", coffee, static_cast<unsigned>(people),
             people == 1 ? "person" : "people", static_cast<unsigned>(range));
  } else if (source == RecSource::SIMILAR) {
    snprintf(o.why, sizeof(o.why), "%s: from your similar coffee %s, +/- %u.", coffee, from, static_cast<unsigned>(range));
  } else {
    snprintf(o.why, sizeof(o.why), "%s: a first guess from the grinder's range, +/- %u.", coffee, static_cast<unsigned>(range));
  }
  char more[80];
  if (current) snprintf(more, sizeof(more), " Grinder now at %s %s.", now, unit);
  else snprintf(more, sizeof(more), " No setting yet.");
  detail::append(o.why, sizeof(o.why), "%s", more);
  snprintf(o.use, sizeof(o.use), "SET\n%s", dial.gs(setting));
  if (current) snprintf(o.keep, sizeof(o.keep), "KEEP\n%s", now);
  else snprintf(o.keep, sizeof(o.keep), "NOT NOW");
}

}  // namespace screen
}  // namespace bbw
