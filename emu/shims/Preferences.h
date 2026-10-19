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

 private:
  std::string dir_;
};
