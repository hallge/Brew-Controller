#pragma once
// What a button pusher has to be for this controller, whatever its make.
//
// A PusherDriver turns "press", "down" and "up" into one make's bytes and its
// notifications into acknowledgements and device information. Everything
// above it (core/PusherLink.h, the brew logic, the power control, the pages)
// works on those and on capabilities only.
//
// The interface is modelled on the pusher this project was built around, the
// SwitchBot Bot: it can carry everything that one does. A pusher that does
// less says so in its capabilities; nothing is made up for it. The least a
// pusher must do is press the button once on command (PUSHER_CAP_PRESS).
//
// Pure C++, no Arduino: drivers run in the host tests.
//
// Adding a pusher: write a PusherDriver for it (core/SwitchBotDriver.h is the
// reference, core/MicroBotDriver.h the smallest) and list it in PusherLink.h.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace bbw {

enum PusherCap : uint32_t {
  PUSHER_CAP_PRESS = 1u << 0,         // pushes and pulls back by itself on one command
  PUSHER_CAP_DOWN_UP = 1u << 1,       // down and up are separate commands: a hold timed here
  PUSHER_CAP_ACK = 1u << 2,           // says whether it acted on a command
  PUSHER_CAP_BATTERY = 1u << 3,       // reports its battery
  PUSHER_CAP_HOLD_SECONDS = 1u << 4,  // the hold of its own press can be set, in whole seconds
  PUSHER_CAP_NEEDS_SECRET = 1u << 5,  // does nothing without a key from its maker's app or cloud
};

constexpr uint32_t PUSHER_CAPS_BASELINE = PUSHER_CAP_PRESS;

inline bool pusherCapsMeetBaseline(uint32_t caps) {
  return (caps & PUSHER_CAPS_BASELINE) == PUSHER_CAPS_BASELINE;
}

inline const char *pusherCapName(PusherCap cap) {
  switch (cap) {
    case PUSHER_CAP_PRESS: return "press";
    case PUSHER_CAP_DOWN_UP: return "down_up";
    case PUSHER_CAP_ACK: return "ack";
    case PUSHER_CAP_BATTERY: return "battery";
    case PUSHER_CAP_HOLD_SECONDS: return "hold_seconds";
    case PUSHER_CAP_NEEDS_SECRET: return "needs_secret";
  }
  return "";
}

enum class PusherOp : uint8_t { PRESS, DOWN, UP };

// What the pusher said to a command.
enum class PusherAck : uint8_t {
  ACCEPTED,     // the arm moved (or will)
  UNSUPPORTED,  // answered, but this command is not for the mode it is in
  REFUSED,      // busy, wrong password, low battery: the arm did not move
};

// Last values read from the pusher. Kept after the link is dropped.
struct PusherInfo {
  bool valid = false;
  uint8_t batteryPercent = 0;
  uint8_t firmwareTenths = 0;  // 44 => v4.4; 0: not reported
  bool switchMode = false;     // true: two-state mode, a press is not accepted
  uint8_t holdSeconds = 0;
};

// What a pusher needs that only its owner has. Which fields matter depends on
// the make; the rest stay empty.
struct PusherSecrets {
  char password[24] = {};      // SwitchBot: only if one was set in the SwitchBot app
  // Tuya (Fingerbot): the three values Tuya's cloud gives for the device.
  char tuyaUuid[17] = {};
  char tuyaDeviceId[23] = {};
  char tuyaLocalKey[17] = {};
  uint8_t tuyaVariant = 0;     // 0 Fingerbot / Fingerbot Plus, 1 CubeTouch
  // MicroBot Push: the 16-byte token it gave when it was paired.
  uint8_t microbotToken[16] = {};
  bool microbotTokenSet = false;
};

// Bytes to the pusher. False: nothing left the radio.
class PusherWriter {
 public:
  virtual ~PusherWriter() = default;
  virtual bool write(const uint8_t *data, size_t length) = 0;
};

class PusherEvents {
 public:
  virtual ~PusherEvents() = default;
  // The answer to the last press, down or up. raw: the pusher's own code.
  virtual void onPusherAck(PusherAck ack, uint8_t raw) = 0;
  // The pusher's own status code of anything it answered, for the Devices page.
  virtual void onPusherStatus(uint8_t raw) = 0;
  virtual void onPusherInfo(const PusherInfo &info) = 0;
};

constexpr size_t PUSHER_MAX_WRITE = 20;  // one write fits the smallest BLE packet

class PusherDriver {
 public:
  virtual ~PusherDriver() = default;

  virtual const char *id() const = 0;
  virtual const char *label() const = 0;
  // The characteristic pair that makes a connected device this pusher.
  virtual const char *writeUuid() const = 0;
  virtual const char *notifyUuid() const = 0;
  // The service they are in, where it is known: looked up directly, which is
  // quicker than reading the device's whole table. nullptr: look everywhere.
  virtual const char *serviceUuid() const { return nullptr; }
  // Its writes are acknowledged by the radio (write with response).
  virtual bool writeWithResponse() const { return false; }
  virtual uint32_t caps() const = 0;
  bool has(PusherCap cap) const { return (caps() & cap) != 0; }

  // A new connection: forget the last one. random() gives 32 random bits,
  // epoch() the wall clock in seconds (0: not known).
  virtual void begin(const PusherSecrets &secrets, uint32_t (*random)(), uint32_t (*epoch)()) = 0;
  // Subscribed: start whatever the pusher needs before it takes commands.
  virtual void onConnected(PusherWriter &writer) { (void)writer; }
  virtual bool sessionReady() const { return true; }
  // Gave up: wrong or missing key. In words, for the Devices page.
  virtual const char *sessionProblem() const { return nullptr; }

  // False: nothing left the radio.
  virtual bool send(PusherOp op, PusherWriter &writer) = 0;
  // Battery, mode, hold time. False: this pusher is not asked (it tells by itself, or cannot).
  virtual bool requestInfo(PusherWriter &writer) { (void)writer; return false; }
  virtual bool setHoldSeconds(uint8_t seconds, PusherWriter &writer) {
    (void)seconds; (void)writer;
    return false;
  }

  // The bytes of one notification. The writer is for what the pusher's
  // protocol itself needs answered.
  virtual void onNotify(const uint8_t *data, size_t length, PusherEvents &events,
                        PusherWriter &writer) = 0;
  virtual const char *statusName(uint8_t raw) const { (void)raw; return "unknown"; }
};

}  // namespace bbw
