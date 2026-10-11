#pragma once
// The virtual air between the firmware's NimBLE central and the emulated
// peripherals. A peripheral advertises, exposes a GATT table, receives
// writes and sends notifications, as raw bytes.

#include <cstdint>
#include <string>
#include <vector>

class NimBLEClient;

namespace emu {

struct GattCharacteristic {
  std::string uuid;
  bool notify = false, indicate = false, write = false, writeNoResponse = false;
};

struct GattService {
  std::string uuid;
  std::vector<GattCharacteristic> characteristics;
};

struct Advertisement {
  std::string name;  // empty: no local name in the advertisement
  int rssi = -60;
  std::vector<std::pair<std::string, std::string>> serviceData;  // uuid, bytes
  std::vector<std::string> serviceUuids;
};

class Peripheral {
 public:
  virtual ~Peripheral() = default;
  std::string label;    // for the log
  std::string address;  // "aa:bb:cc:dd:ee:ff"
  uint8_t addressType = 0;

  virtual bool advertising() = 0;
  virtual Advertisement advertisement() = 0;
  virtual uint32_t connectDelayMs() { return 150; }
  virtual std::vector<GattService> gatt() = 0;
  virtual void onConnect() {}
  virtual void onDisconnect() {}
  virtual void onWrite(const std::string &characteristic, const uint8_t *data, size_t length) = 0;
  virtual void step(double dtS) { (void)dtS; }

  // `periodic` packets are only logged when the page asks for them.
  void notify(const std::string &characteristic, const std::vector<uint8_t> &data,
              bool periodic = false);
  void dropLink(const char *why);  // peripheral side ends the connection
  bool linked() const { return central_ != nullptr; }

 private:
  friend class Radio;
  friend class ::NimBLEClient;
  NimBLEClient *central_ = nullptr;
};

void radioAdd(Peripheral *peripheral);
void radioBegin();

}  // namespace emu
