#pragma once
// PC emulator stand-in for NimBLE-Arduino 1.4 (central role only). The
// firmware's BLE code runs unchanged against this; behind it is a virtual
// radio with emulated peripherals (emu/src/emu_radio.h).

#include <Arduino.h>

#include <functional>
#include <string>
#include <vector>

#define ESP_PWR_LVL_P9 9

namespace emu {
class Peripheral;
class Radio;
}  // namespace emu

class NimBLEUUID {
 public:
  NimBLEUUID() {}
  NimBLEUUID(const char *text);
  NimBLEUUID(const std::string &text);
  NimBLEUUID(uint16_t value);
  bool operator==(const NimBLEUUID &other) const { return value_ == other.value_; }
  bool operator!=(const NimBLEUUID &other) const { return value_ != other.value_; }
  std::string toString() const { return value_; }

 private:
  std::string value_;  // always the full 128-bit form, lower case
};

class NimBLEAddress {
 public:
  NimBLEAddress() {}
  NimBLEAddress(const std::string &text, uint8_t type = 0);
  std::string toString() const { return text_; }
  uint8_t getType() const { return type_; }

 private:
  std::string text_;
  uint8_t type_ = 0;
};

class NimBLEClient;
class NimBLERemoteCharacteristic;
typedef std::function<void(NimBLERemoteCharacteristic *, uint8_t *, size_t, bool)> notify_callback;

class NimBLERemoteCharacteristic {
 public:
  bool canNotify() const { return notify_; }
  bool canIndicate() const { return indicate_; }
  bool canWrite() const { return write_; }
  bool canWriteNoResponse() const { return writeNoResponse_; }
  bool subscribe(bool notifications, notify_callback callback, bool response = false);
  bool writeValue(const uint8_t *data, size_t length, bool response = false);
  NimBLEUUID getUUID() const { return uuid_; }

 private:
  friend class NimBLEClient;
  friend class emu::Radio;
  NimBLEClient *client_ = nullptr;
  NimBLEUUID uuid_;
  bool notify_ = false, indicate_ = false, write_ = false, writeNoResponse_ = false;
  bool subscribed_ = false;
  notify_callback callback_;
};

class NimBLERemoteService {
 public:
  NimBLERemoteCharacteristic *getCharacteristic(const NimBLEUUID &uuid);
  NimBLERemoteCharacteristic *getCharacteristic(const char *uuid) {
    return getCharacteristic(NimBLEUUID(uuid));
  }
  NimBLEUUID getUUID() const { return uuid_; }

 private:
  friend class NimBLEClient;
  friend class emu::Radio;
  NimBLEUUID uuid_;
  std::vector<NimBLERemoteCharacteristic *> characteristics_;
};

class NimBLEClientCallbacks {
 public:
  virtual ~NimBLEClientCallbacks() = default;
  virtual void onConnect(NimBLEClient *) {}
  virtual void onDisconnect(NimBLEClient *) {}
};

class NimBLEClient {
 public:
  void setConnectionParams(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t = 16, uint16_t = 16) {}
  void setConnectTimeout(uint8_t seconds) { connectTimeoutS_ = seconds; }
  void setClientCallbacks(NimBLEClientCallbacks *callbacks, bool deleteCallbacks = true) {
    (void)deleteCallbacks;
    callbacks_ = callbacks;
  }
  bool connect(const NimBLEAddress &address, bool deleteAttributes = true);
  bool isConnected() const;
  int disconnect(uint8_t reason = 0x13);
  std::vector<NimBLERemoteService *> *getServices(bool refresh = false);
  NimBLERemoteService *getService(const NimBLEUUID &uuid);
  NimBLERemoteService *getService(const char *uuid) { return getService(NimBLEUUID(uuid)); }

 private:
  friend class NimBLERemoteCharacteristic;
  friend class emu::Radio;
  uint8_t connectTimeoutS_ = 30;
  NimBLEClientCallbacks *callbacks_ = nullptr;
  emu::Peripheral *peer_ = nullptr;
  std::vector<NimBLERemoteService *> services_;
};

class NimBLEAdvertisedDevice {
 public:
  NimBLEAddress getAddress() const { return address_; }
  std::string getName() const { return name_; }
  int getRSSI() const { return rssi_; }
  uint8_t getServiceDataCount() const { return static_cast<uint8_t>(serviceData_.size()); }
  NimBLEUUID getServiceDataUUID(uint8_t index) const { return serviceData_[index].first; }
  std::string getServiceData(uint8_t index) const { return serviceData_[index].second; }
  bool isAdvertisingService(const NimBLEUUID &uuid) const;
  std::string getManufacturerData(uint8_t = 0) const { return manufacturerData_; }

 private:
  friend class emu::Radio;
  NimBLEAddress address_;
  std::string manufacturerData_;
  std::string name_;
  int rssi_ = 0;
  std::vector<std::pair<NimBLEUUID, std::string>> serviceData_;
  std::vector<NimBLEUUID> serviceUuids_;
};

class NimBLEAdvertisedDeviceCallbacks {
 public:
  virtual ~NimBLEAdvertisedDeviceCallbacks() = default;
  virtual void onResult(NimBLEAdvertisedDevice *device) = 0;
};

class NimBLEScan {
 public:
  void setAdvertisedDeviceCallbacks(NimBLEAdvertisedDeviceCallbacks *callbacks,
                                    bool wantDuplicates = false);
  void setActiveScan(bool active);
  void setInterval(uint16_t) {}
  void setWindow(uint16_t) {}
  void setMaxResults(uint8_t) {}
  void setDuplicateFilter(bool) {}  // every advertisement is delivered anyway
  bool start(uint32_t duration, void (*completed)(void *) = nullptr, bool isContinue = false);
  bool stop();
  bool isScanning();
};

class NimBLEDevice {
 public:
  static void init(const std::string &deviceName);
  static bool getInitialized();
  static void setPower(int level);
  static NimBLEScan *getScan();
  static NimBLEClient *createClient();
};
