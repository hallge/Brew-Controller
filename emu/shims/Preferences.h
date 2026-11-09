#pragma once
// NVS stand-in: one file per key under <data dir>/nvs/<namespace>/.
#include <Arduino.h>

class Preferences {
 public:
  bool begin(const char *name, bool readOnly = false);
  void end() {}
  bool isKey(const char *key);
  size_t getBytesLength(const char *key);
  size_t getBytes(const char *key, void *buffer, size_t maxLength);
  size_t putBytes(const char *key, const void *value, size_t length);
  // Typed values, kept as their bytes (the board's NVS keeps them typed; only this firmware reads them).
  template <typename T>
  T getValue(const char *key, T fallback) {
    T v = fallback;
    if (getBytesLength(key) == sizeof(T)) getBytes(key, &v, sizeof(T));
    return v;
  }
  bool getBool(const char *key, bool fallback = false) { return getValue<uint8_t>(key, fallback ? 1 : 0) != 0; }
  float getFloat(const char *key, float fallback = 0.0f) { return getValue<float>(key, fallback); }
  uint32_t getUInt(const char *key, uint32_t fallback = 0) { return getValue<uint32_t>(key, fallback); }
  size_t putBool(const char *key, bool value) {
    const uint8_t v = value ? 1 : 0;
    return putBytes(key, &v, 1);
  }
  size_t putFloat(const char *key, float value) { return putBytes(key, &value, sizeof(value)); }
  size_t putUInt(const char *key, uint32_t value) { return putBytes(key, &value, sizeof(value)); }

 private:
  std::string dir_;
};
