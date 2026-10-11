#pragma once
// Tuya's Fingerbot (and Fingerbot Plus, CubeTouch) as a PusherDriver.
//
// Tuya BLE devices only obey a client that proves it knows the device's
// local key, so this pusher needs three values from Tuya's cloud: the
// device's uuid, its device id and its local key (README, "Button pushers").
//
// Protocol as implemented by the Home Assistant integration
// github.com/PlusPlus-ua/ha_tuya_ble (tuya_ble/tuya_ble.py, devices.py):
//
//   message  = seq(4) responseTo(4) code(2) length(2) data crc16(2), all big
//              endian, zero padded to 16 bytes
//   sent as    flag(1) iv(16) AES-128-CBC(message), cut into 20-byte packets:
//              varint packet number, and in packet 0 also varint total length
//              and the protocol version in the high nibble of one byte
//   keys       flag 4: md5(localKey[0..5])          device information only
//              flag 5: md5(localKey[0..5] + srand)  everything after it
//   session    0x0000 device information -> srand; 0x0001 pair (uuid,
//              localKey[0..5], device id); then 0x0002 writes datapoints and
//              0x8001.. report them
//   Fingerbot  datapoint "switch" (bool) true is a press while "mode" is 0;
//              "hold time" in seconds; battery in percent
//
// Not checked against a real Fingerbot. Pure C++, host tested against a
// device model written from the same description.

#include <stdio.h>

#include "PusherDriver.h"
#include "TuyaCrypto.h"

namespace bbw {

namespace tuya {

constexpr const char *WRITE_CHAR_UUID = "00002b11-0000-1000-8000-00805f9b34fb";
constexpr const char *NOTIFY_CHAR_UUID = "00002b10-0000-1000-8000-00805f9b34fb";
constexpr uint16_t SERVICE_DATA_UUID = 0xA201;  // in the advertisement of any Tuya BLE device

constexpr uint16_t CODE_DEVICE_INFO = 0x0000, CODE_PAIR = 0x0001, CODE_DPS = 0x0002;
constexpr uint16_t CODE_RECEIVE_DP = 0x8001, CODE_RECEIVE_TIME_DP = 0x8003;
constexpr uint16_t CODE_TIME1_REQUEST = 0x8011, CODE_TIME2_REQUEST = 0x8012;
constexpr uint8_t DP_BOOL = 1, DP_VALUE = 2, DP_ENUM = 4;
constexpr uint8_t FLAG_LOGIN_KEY = 4, FLAG_SESSION_KEY = 5;

constexpr size_t MAX_DATA = 64;          // longest message body handled (device information: 46+)
constexpr size_t MAX_WIRE = 1 + 16 + 96; // flag, iv, encrypted message

struct Message {
  uint32_t seq = 0, responseTo = 0;
  uint16_t code = 0;
  uint8_t data[MAX_DATA] = {};
  size_t length = 0;
};

inline size_t packVarint(uint32_t value, uint8_t *out) {
  size_t n = 0;
  do {
    uint8_t b = value & 0x7f;
    value >>= 7;
    if (value != 0) b |= 0x80;
    out[n++] = b;
  } while (value != 0);
  return n;
}

// flag + iv + encrypted message into `wire`; returns its length (0: too long).
inline size_t seal(const Message &m, uint8_t flag, const uint8_t key[16], const uint8_t iv[16],
                   uint8_t wire[MAX_WIRE]) {
  if (m.length > MAX_DATA) return 0;
  uint8_t raw[96] = {};
  const uint32_t words[2] = {m.seq, m.responseTo};
  for (int w = 0; w < 2; ++w) {
    for (int i = 0; i < 4; ++i) raw[4 * w + i] = static_cast<uint8_t>(words[w] >> (24 - 8 * i));
  }
  raw[8] = static_cast<uint8_t>(m.code >> 8);
  raw[9] = static_cast<uint8_t>(m.code);
  raw[10] = static_cast<uint8_t>(m.length >> 8);
  raw[11] = static_cast<uint8_t>(m.length);
  memcpy(raw + 12, m.data, m.length);
  size_t n = 12 + m.length;
  const uint16_t crc = crc16(raw, n);
  raw[n++] = static_cast<uint8_t>(crc >> 8);
  raw[n++] = static_cast<uint8_t>(crc);
  n = (n + 15) / 16 * 16;
  wire[0] = flag;
  memcpy(wire + 1, iv, 16);
  memcpy(wire + 17, raw, n);
  Aes128(key).cbcEncrypt(iv, wire + 17, n);
  return 17 + n;
}

// The reverse. False: not decryptable to a message with a good checksum.
inline bool open(const uint8_t *wire, size_t length, const uint8_t key[16], Message &m) {
  if (length < 17 + 16 || length > MAX_WIRE || (length - 17) % 16 != 0) return false;
  uint8_t raw[96];
  const size_t n = length - 17;
  memcpy(raw, wire + 17, n);
  Aes128(key).cbcDecrypt(wire + 1, raw, n);
  m.seq = (static_cast<uint32_t>(raw[0]) << 24) | (static_cast<uint32_t>(raw[1]) << 16) |
          (static_cast<uint32_t>(raw[2]) << 8) | raw[3];
  m.responseTo = (static_cast<uint32_t>(raw[4]) << 24) | (static_cast<uint32_t>(raw[5]) << 16) |
                 (static_cast<uint32_t>(raw[6]) << 8) | raw[7];
  m.code = static_cast<uint16_t>((raw[8] << 8) | raw[9]);
  m.length = static_cast<size_t>((raw[10] << 8) | raw[11]);
  if (m.length > MAX_DATA || 12 + m.length + 2 > n) return false;
  memcpy(m.data, raw + 12, m.length);
  const uint16_t crc = static_cast<uint16_t>((raw[12 + m.length] << 8) | raw[13 + m.length]);
  return crc == crc16(raw, 12 + m.length);
}

// Cuts a sealed message into packets of at most 20 bytes and writes them.
inline bool sendPackets(const uint8_t *wire, size_t length, uint8_t protocolVersion,
                        PusherWriter &writer) {
  size_t at = 0;
  for (uint32_t number = 0; at < length; ++number) {
    uint8_t packet[PUSHER_MAX_WRITE];
    size_t n = packVarint(number, packet);
    if (number == 0) {
      n += packVarint(static_cast<uint32_t>(length), packet + n);
      packet[n++] = static_cast<uint8_t>(protocolVersion << 4);
    }
    const size_t part = length - at < PUSHER_MAX_WRITE - n ? length - at : PUSHER_MAX_WRITE - n;
    memcpy(packet + n, wire + at, part);
    if (!writer.write(packet, n + part)) return false;
    at += part;
  }
  return true;
}

// Puts the packets of one sealed message back together.
class Assembler {
 public:
  void reset() { expected_ = 0; used_ = 0; next_ = 0; }
  // True when `data` completed a message: wire()/length() hold it until the next call.
  bool feed(const uint8_t *data, size_t length) {
    size_t at = 0;
    uint32_t number = 0;
    if (!varint(data, length, at, number)) return false;
    if (number == 0) {
      uint32_t total = 0;
      if (!varint(data, length, at, total) || at >= length || total > MAX_WIRE) { reset(); return false; }
      ++at;  // protocol version
      expected_ = total;
      used_ = 0;
      next_ = 0;
    }
    if (expected_ == 0 || number != next_) { reset(); return false; }  // a packet went missing
    ++next_;
    const size_t part = length - at;
    if (used_ + part > expected_) { reset(); return false; }
    memcpy(wire_ + used_, data + at, part);
    used_ += part;
    if (used_ < expected_) return false;
    done_ = used_;
    expected_ = 0;
    return true;
  }
  const uint8_t *wire() const { return wire_; }
  size_t length() const { return done_; }

 private:
  static bool varint(const uint8_t *data, size_t length, size_t &at, uint32_t &value) {
    value = 0;
    for (int shift = 0; shift < 32 && at < length; shift += 7) {
      const uint8_t b = data[at++];
      value |= static_cast<uint32_t>(b & 0x7f) << shift;
      if (!(b & 0x80)) return true;
    }
    return false;
  }
  uint8_t wire_[MAX_WIRE] = {};
  size_t expected_ = 0, used_ = 0, done_ = 0;
  uint32_t next_ = 0;
};

// The datapoint numbers of one product line (ha_tuya_ble devices.py).
struct FingerbotPoints {
  uint8_t pressSwitch, mode, holdTime, battery;
};
inline FingerbotPoints fingerbotPoints(uint8_t variant) {
  return variant == 1 ? FingerbotPoints{1, 2, 3, 0}      // CubeTouch 1s / II; battery not known
                      : FingerbotPoints{2, 8, 10, 12};   // Fingerbot, Fingerbot Plus
}

}  // namespace tuya

class TuyaFingerbotDriver : public PusherDriver {
 public:
  enum : uint8_t {  // the pusher's "own status code" on the Devices page
    STATUS_OK = 0x00, STATUS_NO_KEY = 0xF0, STATUS_BAD_KEY = 0xF1, STATUS_PAIR_REFUSED = 0xF2,
    STATUS_WRITE_REFUSED = 0xF3
  };

  const char *id() const override { return "tuya_fingerbot"; }
  const char *label() const override { return "Tuya Fingerbot"; }
  const char *writeUuid() const override { return tuya::WRITE_CHAR_UUID; }
  const char *notifyUuid() const override { return tuya::NOTIFY_CHAR_UUID; }
  uint32_t caps() const override {
    return PUSHER_CAP_PRESS | PUSHER_CAP_ACK | PUSHER_CAP_BATTERY | PUSHER_CAP_HOLD_SECONDS |
           PUSHER_CAP_NEEDS_SECRET;
  }

  void begin(const PusherSecrets &secrets, uint32_t (*random)(), uint32_t (*epoch)()) override {
    secrets_ = secrets;
    random_ = random;
    epoch_ = epoch;
    points_ = tuya::fingerbotPoints(secrets.tuyaVariant);
    assembler_.reset();
    state_ = State::IDLE;
    problem_ = nullptr;
    seq_ = 0;
    waitingFor_ = 0;
    pressSeq_ = 0;
    protocolVersion_ = 3;
    info_ = PusherInfo();
    haveKey_ = strlen(secrets.tuyaLocalKey) >= 6 && secrets.tuyaUuid[0] && secrets.tuyaDeviceId[0];
    if (haveKey_) tuya::md5(reinterpret_cast<const uint8_t *>(secrets.tuyaLocalKey), 6, loginKey_);
  }

  void onConnected(PusherWriter &writer) override {
    if (!haveKey_) {
      state_ = State::FAILED;
      problem_ = "needs the device's uuid, device id and local key from Tuya";
      return;
    }
    tuya::Message m;
    m.code = tuya::CODE_DEVICE_INFO;
    state_ = State::INFO_SENT;
    waitingFor_ = send(m, tuya::FLAG_LOGIN_KEY, writer);
  }

  bool sessionReady() const override { return state_ == State::READY; }
  const char *sessionProblem() const override { return problem_; }

  bool send(PusherOp op, PusherWriter &writer) override {
    if (op != PusherOp::PRESS || state_ != State::READY) return false;
    tuya::Message m;
    m.code = tuya::CODE_DPS;
    m.length = boolPoint(points_.pressSwitch, true, m.data);
    pressSeq_ = send(m, tuya::FLAG_SESSION_KEY, writer);
    return pressSeq_ != 0;
  }

  bool setHoldSeconds(uint8_t seconds, PusherWriter &writer) override {
    if (state_ != State::READY) return false;
    tuya::Message m;
    m.code = tuya::CODE_DPS;
    m.length = valuePoint(points_.holdTime, seconds, m.data);
    return send(m, tuya::FLAG_SESSION_KEY, writer) != 0;
  }

  void onNotify(const uint8_t *data, size_t length, PusherEvents &events,
                PusherWriter &writer) override {
    if (!assembler_.feed(data, length)) return;
    const uint8_t flag = assembler_.wire()[0];
    tuya::Message m;
    const uint8_t *key = flag == tuya::FLAG_LOGIN_KEY ? loginKey_ : sessionKey_;
    if ((flag != tuya::FLAG_LOGIN_KEY && flag != tuya::FLAG_SESSION_KEY) ||
        !tuya::open(assembler_.wire(), assembler_.length(), key, m)) {
      // A message that does not decrypt while the session is being set up
      // means the local key is not this device's.
      if (state_ == State::INFO_SENT || state_ == State::PAIR_SENT) {
        fail("the local key does not fit this device", STATUS_BAD_KEY, events);
      }
      return;
    }
    switch (m.code) {
      case tuya::CODE_DEVICE_INFO:
        if (state_ != State::INFO_SENT || m.responseTo != waitingFor_ || m.length < 46) return;
        protocolVersion_ = m.data[2];
        {
          uint8_t seed[12];
          memcpy(seed, secrets_.tuyaLocalKey, 6);
          memcpy(seed + 6, m.data + 6, 6);  // srand
          tuya::md5(seed, 12, sessionKey_);
          tuya::Message pair;
          pair.code = tuya::CODE_PAIR;
          pair.length = 44;  // uuid, first six of the local key, device id, zero padded
          size_t at = 0;
          at += copyText(pair.data + at, secrets_.tuyaUuid, 44 - at);
          at += copyText(pair.data + at, secrets_.tuyaLocalKey, 6);
          copyText(pair.data + at, secrets_.tuyaDeviceId, 44 - at);
          state_ = State::PAIR_SENT;
          waitingFor_ = send(pair, tuya::FLAG_SESSION_KEY, writer);
        }
        break;
      case tuya::CODE_PAIR:
        if (state_ != State::PAIR_SENT || m.responseTo != waitingFor_ || m.length < 1) return;
        if (m.data[0] == 0 || m.data[0] == 2) {  // 2: already paired, which is as good
          state_ = State::READY;
          events.onPusherStatus(STATUS_OK);
          // "Press" behaviour: push and return. The switch datapoint is a press only in mode 0.
          tuya::Message mode;
          mode.code = tuya::CODE_DPS;
          mode.length = point(points_.mode, tuya::DP_ENUM, 0, 1, mode.data);
          send(mode, tuya::FLAG_SESSION_KEY, writer);
        } else {
          fail("the device refused the pairing (uuid or device id wrong?)", STATUS_PAIR_REFUSED, events);
        }
        break;
      case tuya::CODE_DPS:
        // The answer to a datapoint write: one result byte, 0 is accepted.
        if (pressSeq_ != 0 && m.responseTo == pressSeq_) {
          pressSeq_ = 0;
          const bool ok = m.length >= 1 && m.data[0] == 0;
          events.onPusherStatus(ok ? STATUS_OK : STATUS_WRITE_REFUSED);
          events.onPusherAck(ok ? PusherAck::ACCEPTED : PusherAck::REFUSED,
                             ok ? STATUS_OK : STATUS_WRITE_REFUSED);
        }
        break;
      case tuya::CODE_RECEIVE_DP:
      case tuya::CODE_RECEIVE_TIME_DP:
        datapoints(m, events);
        reply(m, nullptr, 0, writer);
        break;
      case tuya::CODE_TIME1_REQUEST: {
        // Milliseconds since 1970 as text, then the time zone in 1/100 hours (UTC here).
        uint8_t body[16];
        const uint32_t now = epoch_ ? epoch_() : 0;
        const int n = snprintf(reinterpret_cast<char *>(body), 14, "%010lu000", static_cast<unsigned long>(now));
        body[n] = body[n + 1] = 0;
        reply(m, body, static_cast<size_t>(n) + 2, writer);
        break;
      }
      default:
        break;
    }
  }

  const char *statusName(uint8_t raw) const override {
    switch (raw) {
      case STATUS_OK: return "ok";
      case STATUS_NO_KEY: return "key_missing";
      case STATUS_BAD_KEY: return "key_wrong";
      case STATUS_PAIR_REFUSED: return "pairing_refused";
      case STATUS_WRITE_REFUSED: return "command_refused";
    }
    return "unknown";
  }

 private:
  enum class State : uint8_t { IDLE, INFO_SENT, PAIR_SENT, READY, FAILED };

  static size_t copyText(uint8_t *out, const char *text, size_t max) {
    size_t n = strlen(text);
    if (n > max) n = max;
    memcpy(out, text, n);
    return n;
  }
  // id, type, length, value (big endian).
  static size_t point(uint8_t id, uint8_t type, uint32_t value, uint8_t width, uint8_t *out) {
    out[0] = id;
    out[1] = type;
    out[2] = width;
    for (uint8_t i = 0; i < width; ++i) out[3 + i] = static_cast<uint8_t>(value >> (8 * (width - 1 - i)));
    return 3u + width;
  }
  static size_t boolPoint(uint8_t id, bool value, uint8_t *out) {
    return point(id, tuya::DP_BOOL, value ? 1 : 0, 1, out);
  }
  static size_t valuePoint(uint8_t id, uint32_t value, uint8_t *out) {
    return point(id, tuya::DP_VALUE, value, 4, out);
  }

  // Returns the sequence number it went out under; 0: nothing left the radio.
  uint32_t send(tuya::Message &m, uint8_t flag, PusherWriter &writer) {
    m.seq = ++seq_;
    uint8_t iv[16];
    for (int i = 0; i < 16; i += 4) {
      const uint32_t r = random_ ? random_() : 0x9e3779b9u * (seq_ + i);
      for (int k = 0; k < 4; ++k) iv[i + k] = static_cast<uint8_t>(r >> (8 * k));
    }
    uint8_t wire[tuya::MAX_WIRE];
    const size_t n = tuya::seal(m, flag, flag == tuya::FLAG_LOGIN_KEY ? loginKey_ : sessionKey_, iv, wire);
    return n != 0 && tuya::sendPackets(wire, n, protocolVersion_, writer) ? m.seq : 0;
  }

  // The device expects what it reports to be acknowledged under the same code.
  void reply(const tuya::Message &to, const uint8_t *body, size_t length, PusherWriter &writer) {
    tuya::Message m;
    m.code = to.code;
    m.responseTo = to.seq;
    if (body) memcpy(m.data, body, length);
    m.length = body ? length : 0;
    send(m, tuya::FLAG_SESSION_KEY, writer);
  }

  void datapoints(const tuya::Message &m, PusherEvents &events) {
    bool changed = false;
    for (size_t at = 0; at + 3 <= m.length;) {
      const uint8_t id = m.data[at], type = m.data[at + 1], width = m.data[at + 2];
      if (at + 3 + width > m.length) break;
      uint32_t value = 0;
      for (uint8_t i = 0; i < width && i < 4; ++i) value = (value << 8) | m.data[at + 3 + i];
      if (id == points_.battery && points_.battery != 0 && type == tuya::DP_VALUE) {
        info_.batteryPercent = static_cast<uint8_t>(value > 100 ? 100 : value);
        info_.valid = changed = true;
      } else if (id == points_.holdTime && type == tuya::DP_VALUE) {
        info_.holdSeconds = static_cast<uint8_t>(value > 255 ? 255 : value);
        info_.valid = changed = true;
      } else if (id == points_.mode && type == tuya::DP_ENUM) {
        info_.switchMode = value != 0;
        info_.valid = changed = true;
      }
      at += 3u + width;
    }
    if (changed) events.onPusherInfo(info_);
  }

  void fail(const char *why, uint8_t status, PusherEvents &events) {
    state_ = State::FAILED;
    problem_ = why;
    events.onPusherStatus(status);
  }

  PusherSecrets secrets_;
  uint32_t (*random_)() = nullptr;
  uint32_t (*epoch_)() = nullptr;
  tuya::FingerbotPoints points_ = tuya::fingerbotPoints(0);
  tuya::Assembler assembler_;
  State state_ = State::IDLE;
  const char *problem_ = nullptr;
  bool haveKey_ = false;
  uint8_t loginKey_[16] = {}, sessionKey_[16] = {};
  uint32_t seq_ = 0, waitingFor_ = 0, pressSeq_ = 0;
  uint8_t protocolVersion_ = 3;
  PusherInfo info_;
};

}  // namespace bbw
