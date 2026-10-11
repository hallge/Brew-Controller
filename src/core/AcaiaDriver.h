#pragma once
// The current Acaia protocol (Lunar 2021 and later, Pyxis) as a ScaleDriver.
// The decoding is core/AcaiaStream.h, untouched; this only hands what it
// finds on as ScaleEvents, with nothing dropped: weight, the flag byte, the
// timer, keys, battery and timer state.

#include <math.h>

#include "AcaiaStream.h"
#include "ScaleDriver.h"

namespace bbw {

static_assert(AcaiaStream::MAX_FRAME <= SCALE_MAX_FRAME, "diagnostics frame buffer too small");

inline ScaleButton acaiaButton(int key) {
  switch (key) {
    case ACAIA_KEY_NONE: return ScaleButton::NONE;
    case ACAIA_KEY_TARE: return ScaleButton::TARE;
    case ACAIA_KEY_START: return ScaleButton::START;
    case ACAIA_KEY_STOP: return ScaleButton::STOP;
    case ACAIA_KEY_RESET: return ScaleButton::RESET;
    default: return ScaleButton::OTHER;
  }
}

class AcaiaDriver : public ScaleDriver {
 public:
  AcaiaDriver()
      : ScaleDriver(kScaleProtocolAcaia,
                    SCALE_CAP_WEIGHT | SCALE_CAP_TARE | SCALE_CAP_TIMER_CONTROL |
                        SCALE_CAP_TIMER_READBACK | SCALE_CAP_BATTERY | SCALE_CAP_BUTTONS |
                        SCALE_CAP_TIMER_STATE | SCALE_CAP_WEIGHT_FLAGS |
                        SCALE_CAP_TIMER_WITH_READINGS) {}

  void reset() override { stream_ = AcaiaStream(); }

  // A frame can be split over notifications and carries its timer behind a
  // tag, so the bytes go through the stream decoder.
  bool feed(const uint8_t *data, size_t length, ScaleEventSink &sink) override {
    const uint32_t before = stream_.frames();
    stream_.feed(data, length, [&sink](const AcaiaEvent &from) {
      ScaleEvent event;
      // Only readings in the scale's range count as readings (2 kg scale).
      event.hasWeight = from.hasWeight && fabsf(from.weightG) <= 5000.0f;
      event.weightG = from.weightG;
      event.weightFlags = from.weightFlags;
      event.hasTimer = from.hasTimer;
      event.timerMs = from.timerMs;
      event.rawKey = from.key;
      event.button = acaiaButton(from.key);
      event.hasBattery = event.hasTimerState = from.hasStatus;
      event.batteryPercent = from.batteryPercent;
      event.timerRunning = from.timerRunning;
      sink.onScaleEvent(event);
    });
    // A few bytes that complete nothing are the first part of a split frame.
    return stream_.frames() != before || length < 6;
  }

  // The scale reports its battery only in its settings frame, and sends that
  // only when asked: ef dd 06, sixteen zero bytes, checksum (the request both
  // pyacaia and aioacaia use). Asked once the readings are flowing, then
  // every few minutes.
  bool pollRequest(bool asked, uint32_t sinceMs, uint8_t *out, size_t *length) const override {
    constexpr uint32_t SETTINGS_FIRST_MS = 1500, SETTINGS_PERIOD_MS = 5UL * 60UL * 1000UL;
    if (sinceMs < (asked ? SETTINGS_PERIOD_MS : SETTINGS_FIRST_MS)) return false;
    static const uint8_t GET_SETTINGS[21] = {0xef, 0xdd, 0x06};
    memcpy(out, GET_SETTINGS, sizeof(GET_SETTINGS));
    *length = sizeof(GET_SETTINGS);
    return true;
  }

  uint32_t frames() const override { return stream_.frames(); }
  uint32_t badFrames() const override { return stream_.badFrames(); }
  uint32_t splitFrames() const override { return stream_.continuedFrames(); }
  size_t lastFrame(uint8_t *out, size_t max) const override { return stream_.lastFrame(out, max); }

 private:
  AcaiaStream stream_;
};

}  // namespace bbw
