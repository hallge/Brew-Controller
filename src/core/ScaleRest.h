#pragma once
// When the controller lets go of an idle scale, so that the scale can switch itself off.
//
// An Acaia scale counts down its sleep timer only while no app is connected to it (Acaia's own help
// page; measured on the board 20 Oct 2026: connected, a Lunar set to sleep after 5 minutes stayed on
// for half an hour; let go, it was off two minutes later). The controller keeps its link up with a
// heartbeat, so it has to let go itself. Which bot is connected follows the scale: the brew bot while
// the scale is on, the power bot once it is off.
//
// The scale is let go once nothing has happened for the owner's number of minutes: the controller is
// READY, the reading has not moved, and nobody has touched the screen, the touch key or the web page.
// It is taken back the moment the owner does something, and is free to connect again by itself once it
// has gone quiet (it has switched itself off; the next time it is heard, it was switched on).
//
// A press of the touch key while the scale is let go takes it back and asks for the shot to start as
// soon as the controller is ready again (a pending start, given up after a minute).
//
// Portable and host-tested; no BLE or UI access in here.

#include <math.h>
#include <stdint.h>

namespace bbw {

// Settings::scaleRestMin: minutes idle before the scale is let go. 0 is what a record saved before
// the setting existed reads (padding), so it means "not set" and gives the default; SCALE_REST_NEVER
// keeps the scale connected all the time.
constexpr uint8_t SCALE_REST_DEFAULT_MIN = 5;
constexpr uint8_t SCALE_REST_NEVER = 255;
constexpr uint8_t SCALE_REST_MAX_MIN = 60;

inline uint8_t sanitizeScaleRestMin(uint8_t stored) {
  if (stored == 0) return SCALE_REST_DEFAULT_MIN;
  if (stored == SCALE_REST_NEVER) return stored;
  return stored > SCALE_REST_MAX_MIN ? SCALE_REST_MAX_MIN : stored;
}

// The scale switching itself off (or being switched off): it was on (connected, let go to rest, or
// heard advertising) and is now none of these, silent for ScaleRest::QUIET_MS. True once, at that
// change. A scale already off when the controller starts gives nothing: no change was seen.
// quietMs: since the scale was last heard advertising. The silence is also counted from the link going
// down: a connected scale does not advertise, so a dropped link must not look like a scale switched off.
class ScaleOffWatch {
 public:
  bool update(uint32_t nowMs, bool connected, bool resting, uint32_t quietMs, uint32_t quietLimitMs) {
    if (connected) {
      linkSeenMs_ = nowMs;
      haveLink_ = true;
    } else if (haveLink_ && static_cast<uint32_t>(nowMs - linkSeenMs_) < quietMs) {
      quietMs = static_cast<uint32_t>(nowMs - linkSeenMs_);
    }
    const bool off = !connected && !resting && quietMs >= quietLimitMs;
    const bool switched = wasOn_ && off;
    wasOn_ = !off;
    return switched;
  }

 private:
  bool wasOn_ = false;
  bool haveLink_ = false;
  uint32_t linkSeenMs_ = 0;
};

class ScaleRest {
 public:
  static constexpr float STILL_BAND_G = 0.5f;          // a reading moving less than this is still
  static constexpr uint32_t QUIET_MS = 30000;          // not heard this long: the scale is off
  static constexpr uint32_t PENDING_START_MS = 60000;  // a start asked for while resting

  // The owner did something: the idle time starts again, and a scale let go is taken back.
  void activity(uint32_t nowMs) {
    lastActivityMs_ = nowMs;
    haveActivity_ = true;
    resting_ = false;
  }

  // Once per loop. ready: the controller is READY. linked: the scale's link is up. quietMs: how long
  // since the scale was last heard advertising (0 while it is connected). Returns resting().
  bool update(uint32_t nowMs, uint8_t restMin, bool ready, bool linked, float weightG,
              uint32_t quietMs) {
    if (resting_) {
      // Let go and now silent: it has switched itself off. Free to connect when it is switched on.
      // Counted from when it was let go too: while it was connected it did not advertise, so the last
      // time it was heard is older than that.
      if (!linked && quietMs >= QUIET_MS && static_cast<uint32_t>(nowMs - restedAtMs_) >= QUIET_MS) {
        resting_ = false;
      }
      if (resting_) return true;
    }
    if (restMin == 0 || restMin == SCALE_REST_NEVER || !ready || !linked) {
      counting_ = false;
      return false;
    }
    if (!counting_ || fabsf(weightG - refG_) > STILL_BAND_G) {
      counting_ = true;
      refG_ = weightG;
      idleSinceMs_ = nowMs;
    }
    uint32_t since = idleSinceMs_;
    if (haveActivity_ && static_cast<int32_t>(lastActivityMs_ - since) > 0) since = lastActivityMs_;
    if (static_cast<uint32_t>(nowMs - since) >= static_cast<uint32_t>(restMin) * 60000u) {
      resting_ = true;
      restedAtMs_ = nowMs;
    }
    return resting_;
  }

  bool resting() const { return resting_; }

  // The touch key (or the screen's start key) while the scale is let go: take it back and start the
  // shot once ready.
  void startWhenBack(uint32_t nowMs) {
    activity(nowMs);
    pendingSinceMs_ = nowMs;
    pending_ = true;
  }
  bool startPending(uint32_t nowMs) const {
    return pending_ && static_cast<uint32_t>(nowMs - pendingSinceMs_) < PENDING_START_MS;
  }
  // True once, when a pending start may go ahead: the controller is ready with fresh weight and,
  // if one is required, a cup on the scale. A start not possible within the minute is dropped.
  bool takeStart(uint32_t nowMs, bool ready, bool weightFresh, bool cupOk) {
    if (!pending_) return false;
    if (static_cast<uint32_t>(nowMs - pendingSinceMs_) >= PENDING_START_MS) {
      pending_ = false;
      return false;
    }
    if (!ready || !weightFresh || !cupOk) return false;
    pending_ = false;
    return true;
  }
  void cancelStart() { pending_ = false; }

 private:
  bool resting_ = false;
  bool counting_ = false;          // READY with the scale connected since idleSinceMs_
  uint32_t restedAtMs_ = 0;
  bool haveActivity_ = false;
  uint32_t lastActivityMs_ = 0;
  uint32_t idleSinceMs_ = 0;
  float refG_ = 0.0f;
  bool pending_ = false;
  uint32_t pendingSinceMs_ = 0;
};

}  // namespace bbw
