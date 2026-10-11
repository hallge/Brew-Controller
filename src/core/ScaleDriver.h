#pragma once
// What a scale has to be for this controller, whatever its make.
//
// A ScaleDriver turns the bytes of one scale family into ScaleEvents and the
// controller's requests into that family's commands. Everything above it
// (core/ScaleLink.h, the brew logic, the shot data, the pages) works on
// ScaleEvents and capabilities only.
//
// The event is modelled on the scale this project was built around, the Acaia
// Lunar 2021: it can carry everything that scale reports. A scale that reports
// less leaves those parts out and says so in its capabilities; nothing is
// made up for it. The least a scale must do is SCALE_CAPS_BASELINE: stream
// its weight and tare on command.
//
// Pure C++, no Arduino: drivers run in the host tests against captured frames.
//
// Adding a scale
//   - one notification is one frame, weight (and timer) only: copy its
//     protocol table into lib/EspressoScaleProtocol, list it in Registry.cpp
//     and add a TableDriver for it in ScaleLink.h. That is all.
//   - anything richer (frames split over notifications, battery, buttons):
//     write a ScaleDriver for it, as core/AcaiaDriver.h does.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ScaleProtocol.h"

namespace bbw {

enum ScaleCap : uint32_t {
  SCALE_CAP_WEIGHT = 1u << 0,          // streams its weight without being asked each time
  SCALE_CAP_TARE = 1u << 1,            // tares on command
  SCALE_CAP_TIMER_CONTROL = 1u << 2,   // its timer can be started, stopped and reset
  SCALE_CAP_TIMER_READBACK = 1u << 3,  // reports its timer
  SCALE_CAP_BATTERY = 1u << 4,         // reports its battery
  SCALE_CAP_BUTTONS = 1u << 5,         // reports keys pressed on the scale itself
  SCALE_CAP_TIMER_STATE = 1u << 6,     // reports whether its timer is running
  SCALE_CAP_WEIGHT_FLAGS = 1u << 7,    // readings carry the scale's own flag byte
  SCALE_CAP_TIMER_WITH_READINGS = 1u << 8,  // its timer comes with each reading, in tenths or finer
};

constexpr uint32_t SCALE_CAPS_BASELINE = SCALE_CAP_WEIGHT | SCALE_CAP_TARE;

inline bool scaleCapsMeetBaseline(uint32_t caps) {
  return (caps & SCALE_CAPS_BASELINE) == SCALE_CAPS_BASELINE;
}

inline const char *scaleCapName(ScaleCap cap) {
  switch (cap) {
    case SCALE_CAP_WEIGHT: return "weight";
    case SCALE_CAP_TARE: return "tare";
    case SCALE_CAP_TIMER_CONTROL: return "timer_control";
    case SCALE_CAP_TIMER_READBACK: return "timer";
    case SCALE_CAP_BATTERY: return "battery";
    case SCALE_CAP_BUTTONS: return "buttons";
    case SCALE_CAP_TIMER_STATE: return "timer_state";
    case SCALE_CAP_WEIGHT_FLAGS: return "weight_flags";
    case SCALE_CAP_TIMER_WITH_READINGS: return "timer_with_readings";
  }
  return "";
}

// A key pressed on the scale itself, whatever code the scale uses for it.
enum class ScaleButton : uint8_t { NONE, TARE, START, STOP, RESET, OTHER };

// One thing the scale said. Any part can be missing.
struct ScaleEvent {
  bool hasWeight = false;
  float weightG = 0.0f;
  uint8_t weightFlags = 0;     // the scale's own flag byte, as sent; 0 if it has none
  bool hasTimer = false;
  uint32_t timerMs = 0;        // the scale's own timer
  ScaleButton button = ScaleButton::NONE;
  int rawKey = -1;             // the scale's own code for that key; -1: no key
  bool hasBattery = false;
  uint8_t batteryPercent = 0;
  bool hasTimerState = false;
  bool timerRunning = false;
};

class ScaleEventSink {
 public:
  virtual ~ScaleEventSink() = default;
  virtual void onScaleEvent(const ScaleEvent &event) = 0;
};

constexpr size_t SCALE_MAX_FRAME = 40;   // longest frame any driver keeps for diagnostics
constexpr size_t SCALE_MAX_WRITE = 24;   // longest request a driver sends

class ScaleDriver {
 public:
  ScaleDriver(const ScaleProtocol &protocol, uint32_t caps) : protocol_(protocol), caps_(caps) {}
  virtual ~ScaleDriver() = default;

  // Names, characteristics, session-setup writes, heartbeat and silence limits.
  const ScaleProtocol &protocol() const { return protocol_; }
  const char *id() const { return protocol_.id; }
  uint32_t caps() const { return caps_; }
  bool has(ScaleCap cap) const { return (caps_ & cap) != 0; }

  // Some makes share their characteristics with another (Varia and Eureka
  // both use fff1/fff2) and can only be told apart by the advertised name.
  // False: a device of this name is not this scale, whatever it exposes.
  bool allowsName(const char *name) const {
    return !protocol_.requireAdvertisedName || scaleNameMatchesProtocol(name, &protocol_);
  }

  // A new connection: forget everything of the last one.
  virtual void reset() = 0;

  // The bytes of one notification. Calls the sink for everything they
  // complete. False if nothing could be made of them.
  virtual bool feed(const uint8_t *data, size_t length, ScaleEventSink &sink) = 0;

  virtual bool encode(ScaleOp op, uint8_t *out, int *length) const {
    return protocol_.encodeCommand != nullptr && protocol_.encodeCommand(op, 0, out, length);
  }

  // Something this scale only reports when asked (the Lunar's battery). Asked
  // by the link while readings flow and no shot runs. `asked`: it has been
  // asked on this connection; `sinceMs`: time since then, or since the link
  // came up. `out` holds SCALE_MAX_WRITE bytes.
  virtual bool pollRequest(bool asked, uint32_t sinceMs, uint8_t *out, size_t *length) const {
    (void)asked; (void)sinceMs; (void)out; (void)length;
    return false;
  }

  // For the Devices page.
  virtual uint32_t frames() const = 0;       // valid frames
  virtual uint32_t badFrames() const { return 0; }    // checksum or length wrong
  virtual uint32_t splitFrames() const { return 0; }  // frames that arrived in parts
  virtual size_t lastFrame(uint8_t *out, size_t max) const = 0;

 private:
  const ScaleProtocol &protocol_;
  const uint32_t caps_;
};

// A scale that is fully described by its protocol table: one notification is
// one frame, and it holds a weight, a timer, or both.
class TableDriver : public ScaleDriver {
 public:
  // What the table's parseTimer finds: nothing, a timer in the frame of each
  // reading, or one in frames of its own (the Varia: whole seconds).
  enum class Timer : uint8_t { NONE, WITH_READINGS, SEPARATE };
  TableDriver(const ScaleProtocol &protocol, Timer timer)
      : ScaleDriver(protocol, capsOf(protocol, timer)) {}

  void reset() override {
    frames_ = 0;
    lastLength_ = 0;
  }

  bool feed(const uint8_t *data, size_t length, ScaleEventSink &sink) override {
    const ScaleProtocol &p = protocol();
    if (length > SCALE_MAX_PACKET_LENGTH || p.supportedPacketLength == nullptr ||
        !p.supportedPacketLength(static_cast<int>(length))) {
      return false;
    }
    ScaleEvent event;
    event.hasWeight = p.parseWeight != nullptr &&
                      p.parseWeight(data, static_cast<int>(length), &event.weightG);
    event.hasTimer = p.parseTimer != nullptr &&
                     p.parseTimer(data, static_cast<int>(length), &event.timerMs);
    if (!event.hasWeight && !event.hasTimer) return false;
    ++frames_;
    memcpy(last_, data, length);
    lastLength_ = static_cast<uint8_t>(length);
    sink.onScaleEvent(event);
    return true;
  }

  uint32_t frames() const override { return frames_; }
  size_t lastFrame(uint8_t *out, size_t max) const override {
    const size_t n = lastLength_ < max ? lastLength_ : max;
    memcpy(out, last_, n);
    return n;
  }

 private:
  static uint32_t capsOf(const ScaleProtocol &p, Timer timer) {
    uint32_t caps = 0;
    if (p.features.has(ScaleFeatureWeight)) caps |= SCALE_CAP_WEIGHT;
    if (p.features.has(ScaleFeatureTare)) caps |= SCALE_CAP_TARE;
    if (p.features.has(ScaleFeatureStartTimer) && p.features.has(ScaleFeatureStopTimer) &&
        p.features.has(ScaleFeatureResetTimer)) {
      caps |= SCALE_CAP_TIMER_CONTROL;
    }
    if (timer != Timer::NONE && p.parseTimer != nullptr) {
      caps |= SCALE_CAP_TIMER_READBACK;
      if (timer == Timer::WITH_READINGS) caps |= SCALE_CAP_TIMER_WITH_READINGS;
    }
    return caps;
  }

  uint32_t frames_ = 0;
  uint8_t last_[SCALE_MAX_PACKET_LENGTH] = {};
  uint8_t lastLength_ = 0;
};

}  // namespace bbw
