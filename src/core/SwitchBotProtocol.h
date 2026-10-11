#pragma once
// SwitchBot Bot BLE protocol, from OpenWonderLabs/SwitchBotAPI-BLE
// devicetypes/bot.md, cross-checked against Danielhiversen/pySwitchbot
// (switchbot/devices/bot.py, device.py, adv_parser.py). Pure encode/decode,
// no BLE stack dependency.

#include <stddef.h>
#include <stdint.h>

namespace bbw {
namespace switchbot {

// Communication service and characteristics.
constexpr const char *SERVICE_UUID = "cba20d00-224d-11e6-9fb8-0002a5d5c51b";
constexpr const char *WRITE_CHAR_UUID = "cba20002-224d-11e6-9fb8-0002a5d5c51b";   // controller -> Bot
constexpr const char *NOTIFY_CHAR_UUID = "cba20003-224d-11e6-9fb8-0002a5d5c51b";  // Bot -> controller

// Service-data UUIDs that carry the device-type byte in the scan response.
constexpr uint16_t SERVICE_DATA_UUID_NEW = 0xFD3D;
constexpr uint16_t SERVICE_DATA_UUID_OLD = 0x0D00;
constexpr uint8_t DEVICE_TYPE_BOT = 0x48;  // 'H' (WoHand)

constexpr uint8_t MAGIC = 0x57;

enum class Action : uint8_t {
  PRESS = 0x00,  // push and pull back (one complete momentary press)
  ON = 0x01,     // switch mode only
  OFF = 0x02,    // switch mode only
  DOWN = 0x03,   // push and stay
  UP = 0x04,     // pull back
};

enum class Status : uint8_t {
  OK = 0x01,
  ERROR = 0x02,
  BUSY = 0x03,
  VERSION_INCOMPATIBLE = 0x04,
  UNSUPPORTED = 0x05,
  LOW_BATTERY = 0x06,
  ENCRYPTED = 0x07,
  UNENCRYPTED = 0x08,
  PASSWORD_ERROR = 0x09,
};

constexpr size_t MAX_COMMAND_LEN = 8;

// IEEE CRC-32 (the one binascii.crc32 computes); the Bot password key.
inline uint32_t crc32(const char *text) {
  uint32_t crc = 0xFFFFFFFFu;
  for (; text && *text; ++text) {
    crc ^= static_cast<uint8_t>(*text);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

// 57 01 <action>                      no password
// 57 11 <crc32 big-endian> <action>   password set on the Bot
inline size_t encodeAction(Action action, const char *password, uint8_t *out) {
  size_t n = 0;
  out[n++] = MAGIC;
  if (password && password[0] != '\0') {
    const uint32_t key = crc32(password);
    out[n++] = 0x11;
    out[n++] = static_cast<uint8_t>(key >> 24);
    out[n++] = static_cast<uint8_t>(key >> 16);
    out[n++] = static_cast<uint8_t>(key >> 8);
    out[n++] = static_cast<uint8_t>(key);
  } else {
    out[n++] = 0x01;
  }
  out[n++] = static_cast<uint8_t>(action);
  return n;
}

// 57 02: battery, firmware, mode, hold time.
inline size_t encodeGetInfo(const char *password, uint8_t *out) {
  size_t n = 0;
  out[n++] = MAGIC;
  if (password && password[0] != '\0') {
    const uint32_t key = crc32(password);
    out[n++] = 0x12;
    out[n++] = static_cast<uint8_t>(key >> 24);
    out[n++] = static_cast<uint8_t>(key >> 16);
    out[n++] = static_cast<uint8_t>(key >> 8);
    out[n++] = static_cast<uint8_t>(key);
  } else {
    out[n++] = 0x02;
  }
  return n;
}

// 57 0F 08 <seconds>: how long the arm stays down during action PRESS.
// Whole seconds only; this is the Bot's own resolution.
inline size_t encodeSetHoldSeconds(uint8_t seconds, uint8_t *out) {
  out[0] = MAGIC;
  out[1] = 0x0F;
  out[2] = 0x08;
  out[3] = seconds;
  return 4;
}

struct BasicInfo {
  bool valid = false;
  uint8_t batteryPercent = 0;
  uint8_t firmwareTenths = 0;  // 44 => v4.4
  bool switchMode = false;     // true: two-state mode, PRESS is not accepted
  uint8_t holdSeconds = 0;
};

// Response to 57 02: status byte, then the payload documented in bot.md.
inline BasicInfo decodeBasicInfo(const uint8_t *data, size_t length) {
  BasicInfo info;
  if (!data || length < 11 || data[0] != static_cast<uint8_t>(Status::OK)) {
    return info;
  }
  info.valid = true;
  info.batteryPercent = data[1];
  info.firmwareTenths = data[2];
  info.switchMode = (data[9] & 0x10) != 0;
  info.holdSeconds = data[10];
  return info;
}

// pySwitchbot accepts 1 and 5 for arm actions; 5 is what a Bot in switch mode
// answers to PRESS (bot.md), so only 1 counts as "the arm moved" here.
inline bool actionAccepted(uint8_t status) {
  return status == static_cast<uint8_t>(Status::OK);
}

inline const char *statusName(uint8_t status) {
  switch (static_cast<Status>(status)) {
    case Status::OK: return "ok";
    case Status::ERROR: return "error";
    case Status::BUSY: return "busy";
    case Status::VERSION_INCOMPATIBLE: return "version_incompatible";
    case Status::UNSUPPORTED: return "unsupported_in_this_mode";
    case Status::LOW_BATTERY: return "low_battery";
    case Status::ENCRYPTED: return "password_required";
    case Status::UNENCRYPTED: return "password_not_set";
    case Status::PASSWORD_ERROR: return "password_error";
  }
  return "unknown";
}

}  // namespace switchbot
}  // namespace bbw
