#pragma once
// The SwitchBot Bot as a PusherDriver. The bytes are core/SwitchBotProtocol.h,
// untouched; this holds what the Bot's untyped replies need: what was last
// asked, so that an answer is taken for what it answers.

#include <atomic>

#include "PusherDriver.h"
#include "SwitchBotProtocol.h"

namespace bbw {

class SwitchBotDriver : public PusherDriver {
 public:
  const char *id() const override { return "switchbot"; }
  const char *label() const override { return "SwitchBot Bot"; }
  const char *writeUuid() const override { return switchbot::WRITE_CHAR_UUID; }
  const char *notifyUuid() const override { return switchbot::NOTIFY_CHAR_UUID; }
  const char *serviceUuid() const override { return switchbot::SERVICE_UUID; }
  uint32_t caps() const override {
    return PUSHER_CAP_PRESS | PUSHER_CAP_DOWN_UP | PUSHER_CAP_ACK | PUSHER_CAP_BATTERY |
           PUSHER_CAP_HOLD_SECONDS;
  }

  void begin(const PusherSecrets &secrets, uint32_t (*)(), uint32_t (*)()) override {
    strncpy(password_, secrets.password, sizeof(password_) - 1);
    password_[sizeof(password_) - 1] = '\0';
  }

  bool send(PusherOp op, PusherWriter &writer) override {
    const switchbot::Action action = op == PusherOp::PRESS ? switchbot::Action::PRESS
                                     : op == PusherOp::DOWN ? switchbot::Action::DOWN
                                                            : switchbot::Action::UP;
    uint8_t buffer[switchbot::MAX_COMMAND_LEN];
    return write(writer, buffer, switchbot::encodeAction(action, password_, buffer), Expect::ACTION);
  }

  bool requestInfo(PusherWriter &writer) override {
    uint8_t buffer[switchbot::MAX_COMMAND_LEN];
    return write(writer, buffer, switchbot::encodeGetInfo(password_, buffer), Expect::INFO);
  }

  bool setHoldSeconds(uint8_t seconds, PusherWriter &writer) override {
    uint8_t buffer[switchbot::MAX_COMMAND_LEN];
    return write(writer, buffer, switchbot::encodeSetHoldSeconds(seconds, buffer), Expect::CONFIG);
  }

  void onNotify(const uint8_t *data, size_t length, PusherEvents &events, PusherWriter &) override {
    if (length == 0) return;
    events.onPusherStatus(data[0]);
    // The basic-info reply is the only long one; never mistake it for an
    // action acknowledgement.
    if (length >= 11) {
      const switchbot::BasicInfo from = switchbot::decodeBasicInfo(data, length);
      if (from.valid) {
        PusherInfo info;
        info.valid = true;
        info.batteryPercent = from.batteryPercent;
        info.firmwareTenths = from.firmwareTenths;
        info.switchMode = from.switchMode;
        info.holdSeconds = from.holdSeconds;
        events.onPusherInfo(info);
      }
      if (expect_ == Expect::INFO) expect_ = Expect::NONE;
      return;
    }
    if (expect_ == Expect::ACTION) {
      // pySwitchbot accepts 1 and 5 for arm actions; 5 is what a Bot in switch
      // mode answers to PRESS (bot.md), so only 1 counts as "the arm moved".
      const PusherAck ack =
          switchbot::actionAccepted(data[0]) ? PusherAck::ACCEPTED
          : data[0] == static_cast<uint8_t>(switchbot::Status::UNSUPPORTED) ? PusherAck::UNSUPPORTED
                                                                            : PusherAck::REFUSED;
      events.onPusherAck(ack, data[0]);
    }
    expect_ = Expect::NONE;
  }

  const char *statusName(uint8_t raw) const override { return switchbot::statusName(raw); }

 private:
  enum class Expect : uint8_t { NONE, ACTION, INFO, CONFIG };

  // Write without response; the acknowledgement arrives as a notification on
  // cba20003 (same as pySwitchbot: write_gatt_char(..., False)).
  bool write(PusherWriter &writer, const uint8_t *data, size_t length, Expect expect) {
    // What is expected only changes when the command really left.
    const Expect before = expect_;
    expect_ = expect;
    const bool ok = writer.write(data, length);
    if (!ok) expect_ = before;
    return ok;
  }

  char password_[24] = {};
  std::atomic<Expect> expect_{Expect::NONE};
};

}  // namespace bbw
