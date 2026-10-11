#pragma once
// The MicroBot Push (Keymitt / Naran) as a PusherDriver.
//
// Bytes as sent by the Home Assistant library github.com/spycle/pyMicroBot
// (microbot/__init__.py). Every command is two 20-byte writes to
// characteristic 2a89, both starting with the same two random bytes:
//
//   session  <id> 00 01 00 00 00 00 00 fa 00 00 07 00 00 00 00 00 de cd
//            <id> 0f ff <token, 16 bytes>
//   press    <id> 00 01 00 00 00 08 02 00 00 00 0a 00 00 00 00 00 de cd
//            <id> 0f ff ff ff ff ff 00 .. 00
//
// The token is what the MicroBot hands out when it is first paired (its
// button has to be touched for that). Getting one is not done here: it is
// taken from a pairing made with Home Assistant or pyMicroBot and entered on
// the web page, as 32 hex digits.
//
// That library does not show the MicroBot answering a press, and it has no
// separate "down" and "up", so this pusher only presses and never confirms:
// a press that went out is reported as unconfirmed and the scale decides,
// which the brew logic already does. Not checked against a real MicroBot.

#include "PusherDriver.h"

namespace bbw {

namespace microbot {
constexpr const char *CHAR_UUID = "00002a89-0000-1000-8000-00805f9b34fb";
constexpr const char *ADVERTISED_SERVICE_UUID = "0000abcd-0000-1000-8000-00805f9b34fb";
}  // namespace microbot

class MicroBotDriver : public PusherDriver {
 public:
  const char *id() const override { return "microbot_push"; }
  const char *label() const override { return "MicroBot Push"; }
  const char *writeUuid() const override { return microbot::CHAR_UUID; }
  const char *notifyUuid() const override { return microbot::CHAR_UUID; }
  bool writeWithResponse() const override { return true; }
  uint32_t caps() const override { return PUSHER_CAP_PRESS | PUSHER_CAP_NEEDS_SECRET; }

  void begin(const PusherSecrets &secrets, uint32_t (*random)(), uint32_t (*)()) override {
    memcpy(token_, secrets.microbotToken, sizeof(token_));
    haveToken_ = secrets.microbotTokenSet;
    random_ = random;
    ready_ = false;
    counter_ = 0;
  }

  void onConnected(PusherWriter &writer) override {
    if (!haveToken_) return;
    static const uint8_t HEAD[18] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfa, 0x00,
                                     0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xde, 0xcd};
    uint8_t body[18] = {0x0f, 0xff};
    memcpy(body + 2, token_, 16);
    ready_ = pair(HEAD, body, writer);
  }

  bool sessionReady() const override { return ready_; }
  const char *sessionProblem() const override {
    return haveToken_ ? nullptr : "needs the token from its first pairing (32 hex digits)";
  }

  bool send(PusherOp op, PusherWriter &writer) override {
    if (op != PusherOp::PRESS || !ready_) return false;
    static const uint8_t HEAD[18] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x02, 0x00, 0x00,
                                     0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0xde, 0xcd};
    static const uint8_t BODY[18] = {0x0f, 0xff, 0xff, 0xff, 0xff, 0xff};
    return pair(HEAD, BODY, writer);
  }

  void onNotify(const uint8_t *, size_t, PusherEvents &, PusherWriter &) override {}

 private:
  bool pair(const uint8_t head[18], const uint8_t body[18], PusherWriter &writer) {
    const uint32_t r = random_ ? random_() : ++counter_ * 0x9e3779b9u;
    uint8_t packet[PUSHER_MAX_WRITE] = {static_cast<uint8_t>(r >> 8), static_cast<uint8_t>(r)};
    memcpy(packet + 2, head, 18);
    if (!writer.write(packet, sizeof(packet))) return false;
    memcpy(packet + 2, body, 18);
    return writer.write(packet, sizeof(packet));
  }

  uint8_t token_[16] = {};
  bool haveToken_ = false;
  bool ready_ = false;
  uint32_t (*random_)() = nullptr;
  uint32_t counter_ = 0;
};

}  // namespace bbw
