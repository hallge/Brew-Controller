#pragma once
// Frame decoder for the current Acaia protocol (Lunar 2021 and later,
// firmware 1.0.x; characteristic 49535343-1e4d...).
//
// The vendored Open Brew parser in lib/EspressoScaleProtocol accepts a
// notification only if it is exactly one 10, 13 or 17 byte frame, and reads
// the timer of the 17-byte weight frame from bytes 11..13. Captures from a
// real 2021+ Lunar (the test fixtures of github.com/zweckj/aioacaia, MIT) show
// two things it does not allow for:
//
//   ef dd 0c 0c 05 00 00 00 00 01 00 07 00 08 01 1c 06     0.0 g, timer 8.1 s
//                  '-- weight record --' |  '------'
//                                        |  minutes, seconds, tenths
//                                        '- tag 07: a timer record follows
//
//   - the timer is behind a tag byte, so it sits at 12..14, not 11..13;
//   - a frame can arrive split over two notifications (3 + 14 bytes).
//
// So the event stream is decoded here instead: bytes are reassembled into
// frames, the checksum is verified, and the payload is walked as the chain of
// [tag][body] records it is. Pure C++, no Arduino: it runs in the host tests
// against those captures. Legacy scales (characteristic 2a80) still go through
// the vendored parser.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace bbw {

// Keys the scale reports when they are pressed on the scale itself.
constexpr int ACAIA_KEY_NONE = -1;
constexpr int ACAIA_KEY_TARE = 0;
constexpr int ACAIA_KEY_START = 8;
constexpr int ACAIA_KEY_RESET = 9;
constexpr int ACAIA_KEY_STOP = 10;

struct AcaiaEvent {
  bool hasWeight = false;
  float weightG = 0.0f;
  uint8_t weightFlags = 0;     // raw flag byte of the weight record; bit 1 = negative
  bool hasTimer = false;
  uint32_t timerMs = 0;        // the scale's own timer
  int key = ACAIA_KEY_NONE;    // button pressed on the scale
  bool hasStatus = false;      // settings frame
  uint8_t batteryPercent = 0;
  bool timerRunning = false;
};

class AcaiaStream {
 public:
  static constexpr size_t MAX_FRAME = 40;  // longest frame seen is 21 bytes

  void reset() { used_ = 0; }

  // Feed the bytes of one notification. onEvent(const AcaiaEvent &) is called
  // for every complete, checksum-valid frame that carried something.
  template <typename OnEvent>
  void feed(const uint8_t *data, size_t length, OnEvent &&onEvent) {
    if (used_ > 0) ++continued_;
    for (size_t i = 0; i < length; ++i) {
      if (used_ == sizeof(buffer_)) drop(1);
      buffer_[used_++] = data[i];
    }
    for (;;) {
      // Align on the two header bytes.
      size_t start = 0;
      while (start + 1 < used_ && !(buffer_[start] == 0xef && buffer_[start + 1] == 0xdd)) ++start;
      if (start + 1 >= used_) {
        // No header: keep a trailing 0xef, it may be the first half of one.
        const bool keepLast = used_ > 0 && buffer_[used_ - 1] == 0xef;
        skipped_ += static_cast<uint32_t>(used_ - (keepLast ? 1 : 0));
        if (keepLast) { buffer_[0] = 0xef; used_ = 1; } else { used_ = 0; }
        return;
      }
      if (start > 0) { skipped_ += static_cast<uint32_t>(start); drop(start); }
      if (used_ < 4) return;                       // length byte not here yet
      const size_t frameLength = static_cast<size_t>(buffer_[3]) + 5;
      if (buffer_[3] < 1 || frameLength > MAX_FRAME) {
        ++bad_;
        drop(2);                                   // not a frame start after all
        continue;
      }
      if (used_ < frameLength) return;             // rest comes in the next notification
      if (!checksumOk(buffer_, frameLength)) {
        ++bad_;
        drop(2);
        continue;
      }
      ++frames_;
      memcpy(last_, buffer_, frameLength);
      lastLength_ = static_cast<uint8_t>(frameLength);
      AcaiaEvent event;
      if (decodeFrame(buffer_, frameLength, event)) onEvent(event);
      drop(frameLength);
    }
  }

  uint32_t frames() const { return frames_; }        // checksum-valid frames
  uint32_t badFrames() const { return bad_; }        // checksum or length wrong
  uint32_t skippedBytes() const { return skipped_; } // bytes outside any frame
  uint32_t continuedFrames() const { return continued_; }  // frames split over notifications
  // Most recent valid frame, for diagnostics.
  size_t lastFrame(uint8_t *out, size_t max) const {
    const size_t n = lastLength_ < max ? lastLength_ : max;
    memcpy(out, last_, n);
    return n;
  }

  // One complete frame, header to checksum. False if it carried nothing known.
  static bool decodeFrame(const uint8_t *frame, size_t length, AcaiaEvent &event) {
    if (length < 6) return false;
    const uint8_t command = frame[2];
    if (command == 0x08) {  // settings: battery in the low 7 bits, timer state on top
      if (length < 7) return false;
      event.hasStatus = true;
      event.batteryPercent = frame[4] & 0x7f;
      event.timerRunning = (frame[4] & 0x80) != 0;
      return true;
    }
    if (command != 0x0c) return false;  // not an event notification
    // Everything from the type byte to the checksum is a chain of records.
    const uint8_t *p = frame + 4;
    const size_t n = length - 6;
    size_t i = 0;
    bool any = false;
    while (i < n) {
      const uint8_t tag = p[i];
      size_t width = 0;
      switch (tag) {
        case 0x05: width = 6; break;  // weight
        case 0x06: width = 1; break;  // battery
        case 0x07: width = 3; break;  // timer
        case 0x08: width = 1; break;  // key
        case 0x0b: width = 2; break;  // acknowledgement / heartbeat reply
        default: return any;          // unknown: nothing behind it can be trusted
      }
      if (i + 1 + width > n) return any;  // cut short
      const uint8_t *body = p + i + 1;
      if (tag == 0x05) {
        const uint8_t unit = body[4];
        if (unit < 1 || unit > 4) return any;
        static const float divisor[5] = {1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f};
        const uint32_t raw = (static_cast<uint32_t>(body[1]) << 8) | body[0];
        float weight = static_cast<float>(raw) / divisor[unit];
        if (body[5] & 0x02) weight = -weight;
        event.hasWeight = true;
        event.weightG = weight;
        event.weightFlags = body[5];
        any = true;
      } else if (tag == 0x07) {
        if (body[1] > 59 || body[2] > 9) return any;
        event.hasTimer = true;
        event.timerMs = (static_cast<uint32_t>(body[0]) * 60u + body[1]) * 1000u + body[2] * 100u;
        any = true;
      } else if (tag == 0x08) {
        event.key = body[0];
        any = true;
      }
      i += 1 + width;
    }
    return any;
  }

  // Sum of the even and of the odd payload bytes, from the length byte on.
  static bool checksumOk(const uint8_t *frame, size_t length) {
    uint8_t even = 0, odd = 0;
    for (size_t i = 3; i + 2 < length; ++i) {
      if ((i - 3) & 1) odd = static_cast<uint8_t>(odd + frame[i]);
      else even = static_cast<uint8_t>(even + frame[i]);
    }
    return frame[length - 2] == even && frame[length - 1] == odd;
  }

 private:
  void drop(size_t count) {
    if (count >= used_) { used_ = 0; return; }
    memmove(buffer_, buffer_ + count, used_ - count);
    used_ -= count;
  }

  uint8_t buffer_[2 * MAX_FRAME] = {};
  size_t used_ = 0;
  uint8_t last_[MAX_FRAME] = {};
  uint8_t lastLength_ = 0;
  uint32_t frames_ = 0, bad_ = 0, skipped_ = 0, continued_ = 0;
};

}  // namespace bbw
