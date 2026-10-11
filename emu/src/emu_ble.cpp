// NimBLE-Arduino stand-in and the virtual radio behind it.

#include <NimBLEDevice.h>

#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

#include "emu.h"
#include "emu_radio.h"

namespace emu {

namespace {

struct Event {
  enum Kind { NOTIFY, DISCONNECT } kind;
  NimBLEClient *client;
  std::string characteristic;
  std::vector<uint8_t> data;
};

std::vector<Peripheral *> g_peripherals;
std::deque<Event> g_events;  // guarded by g_world
NimBLEAdvertisedDeviceCallbacks *g_scanCallbacks = nullptr;
std::atomic<bool> g_scanning{false};
std::atomic<bool> g_activeScan{false};
bool g_initialized = false;
NimBLEScan g_scan;

constexpr uint32_t ADVERTISING_INTERVAL_MS = 200;

}  // namespace

// Friend of the NimBLE classes: everything that needs their internals.
class Radio {
 public:
  static void run() {
    auto last = std::chrono::steady_clock::now();
    uint32_t lastAdvertMs = 0;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      const auto now = std::chrono::steady_clock::now();
      double dt = std::chrono::duration<double>(now - last).count();
      last = now;
      if (dt > 0.25) dt = 0.25;  // PC stalled: do not jump the simulation

      std::vector<NimBLEAdvertisedDevice> adverts;
      std::deque<Event> events;
      {
        std::lock_guard<std::recursive_mutex> lock(g_world);
        for (Peripheral *p : g_peripherals) p->step(dt);
        if (g_scanning && millis() - lastAdvertMs >= ADVERTISING_INTERVAL_MS) {
          lastAdvertMs = millis();
          for (Peripheral *p : g_peripherals) {
            if (p->linked() || !p->advertising()) continue;
            const Advertisement a = p->advertisement();
            NimBLEAdvertisedDevice device;
            device.address_ = NimBLEAddress(p->address, p->addressType);
            device.name_ = a.name;
            device.rssi_ = a.rssi;
            // Service data and the service list are in the scan response:
            // a passive scan does not see them.
            if (g_activeScan) {
              for (const auto &sd : a.serviceData) {
                device.serviceData_.emplace_back(NimBLEUUID(sd.first), sd.second);
              }
              for (const auto &uuid : a.serviceUuids) device.serviceUuids_.emplace_back(uuid);
            }
            adverts.push_back(device);
          }
        }
        events.swap(g_events);
      }
      // Firmware callbacks run here, as on the NimBLE host task, with no
      // emulator lock held.
      NimBLEAdvertisedDeviceCallbacks *scanCallbacks = g_scanCallbacks;
      for (NimBLEAdvertisedDevice &device : adverts) {
        if (scanCallbacks && g_scanning) scanCallbacks->onResult(&device);
      }
      for (Event &event : events) deliver(event);
    }
  }

  static void deliver(Event &event) {
    if (event.kind == Event::DISCONNECT) {
      if (event.client->callbacks_) event.client->callbacks_->onDisconnect(event.client);
      return;
    }
    NimBLERemoteCharacteristic *target = nullptr;
    notify_callback callback;
    {
      std::lock_guard<std::recursive_mutex> lock(g_world);
      if (!event.client->peer_) return;  // link went away meanwhile
      const NimBLEUUID uuid(event.characteristic);
      for (NimBLERemoteService *service : event.client->services_) {
        for (NimBLERemoteCharacteristic *c : service->characteristics_) {
          if (c->uuid_ == uuid && c->subscribed_) {
            target = c;
            callback = c->callback_;
          }
        }
      }
    }
    if (target && callback) callback(target, event.data.data(), event.data.size(), true);
  }

  static Peripheral *find(const std::string &address) {
    for (Peripheral *p : g_peripherals) {
      if (p->address == address) return p;
    }
    return nullptr;
  }

  // Both sides of a link going down. Caller holds g_world.
  static void unlink(NimBLEClient *client, bool tellFirmware) {
    Peripheral *p = client->peer_;
    if (!p) return;
    client->peer_ = nullptr;
    p->central_ = nullptr;
    p->onDisconnect();
    if (tellFirmware) g_events.push_back({Event::DISCONNECT, client, "", {}});
  }
};

void Peripheral::notify(const std::string &characteristic, const std::vector<uint8_t> &data,
                        bool periodic) {
  std::lock_guard<std::recursive_mutex> lock(g_world);
  if (!central_) return;
  if (!periodic || g_logPeriodic) {
    log("ble", "%s -> controller  notify %.8s  [%s]", label.c_str(), characteristic.c_str(),
        hex(data.data(), data.size()).c_str());
  }
  g_events.push_back({Event::NOTIFY, central_, characteristic, data});
}

void Peripheral::dropLink(const char *why) {
  std::lock_guard<std::recursive_mutex> lock(g_world);
  if (!central_) return;
  log("ble", "%s closed the connection (%s)", label.c_str(), why);
  Radio::unlink(central_, true);
}

void radioAdd(Peripheral *peripheral) {
  std::lock_guard<std::recursive_mutex> lock(g_world);
  g_peripherals.push_back(peripheral);
}

void radioBegin() { std::thread(Radio::run).detach(); }

}  // namespace emu

using emu::g_world;
typedef std::lock_guard<std::recursive_mutex> WorldLock;

// ================================================================== NimBLE ==

namespace {

std::string normalizeUuid(std::string text) {
  for (char &c : text) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  if (text.size() == 4) return "0000" + text + "-0000-1000-8000-00805f9b34fb";
  if (text.size() == 8) return text + "-0000-1000-8000-00805f9b34fb";
  return text;
}

}  // namespace

NimBLEUUID::NimBLEUUID(const char *text) : value_(normalizeUuid(text ? text : "")) {}
NimBLEUUID::NimBLEUUID(const std::string &text) : value_(normalizeUuid(text)) {}
NimBLEUUID::NimBLEUUID(uint16_t value) {
  char text[8];
  snprintf(text, sizeof(text), "%04x", value);
  value_ = normalizeUuid(text);
}

NimBLEAddress::NimBLEAddress(const std::string &text, uint8_t type) : text_(text), type_(type) {
  for (char &c : text_) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
}

bool NimBLEAdvertisedDevice::isAdvertisingService(const NimBLEUUID &uuid) const {
  for (const NimBLEUUID &u : serviceUuids_) {
    if (u == uuid) return true;
  }
  return false;
}

void NimBLEDevice::init(const std::string &deviceName) {
  emu::g_initialized = true;
  emu::log("ble", "controller radio up as '%s'", deviceName.c_str());
}
bool NimBLEDevice::getInitialized() { return emu::g_initialized; }
void NimBLEDevice::setPower(int) {}
NimBLEScan *NimBLEDevice::getScan() { return &emu::g_scan; }
NimBLEClient *NimBLEDevice::createClient() { return new NimBLEClient; }

void NimBLEScan::setAdvertisedDeviceCallbacks(NimBLEAdvertisedDeviceCallbacks *callbacks, bool) {
  emu::g_scanCallbacks = callbacks;
}
void NimBLEScan::setActiveScan(bool active) { emu::g_activeScan = active; }
bool NimBLEScan::start(uint32_t, void (*)(void *), bool) {
  if (!emu::g_scanning.exchange(true)) emu::log("ble", "controller scanning");
  return true;
}
bool NimBLEScan::stop() {
  if (emu::g_scanning.exchange(false)) emu::log("ble", "controller scan stopped");
  return true;
}
bool NimBLEScan::isScanning() { return emu::g_scanning; }

bool NimBLEClient::connect(const NimBLEAddress &address, bool) {
  emu::Peripheral *p = nullptr;
  uint32_t wakeMs = 0;
  // A connection is made on one of the peripheral's advertising events, so
  // it has to be advertising within the connect timeout.
  const uint32_t deadline = millis() + connectTimeoutS_ * 1000u;
  for (;;) {
    {
      WorldLock lock(g_world);
      if (peer_) emu::Radio::unlink(this, false);
      emu::Peripheral *candidate = emu::Radio::find(address.toString());
      if (candidate && !candidate->linked() && candidate->advertising()) {
        p = candidate;
        wakeMs = p->connectDelayMs();
      }
    }
    if (p) break;
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      emu::log("ble", "connect to %s timed out (not advertising)", address.toString().c_str());
      return false;
    }
    delay(50);
  }
  delay(wakeMs);
  WorldLock lock(g_world);
  if (p->linked() || !p->advertising()) return false;  // went away while connecting
  peer_ = p;
  p->central_ = this;
  // Attribute objects of an earlier connection are left alone: a late
  // notification may still be on its way to them.
  services_.clear();
  for (const emu::GattService &s : p->gatt()) {
    NimBLERemoteService *service = new NimBLERemoteService;
    service->uuid_ = NimBLEUUID(s.uuid);
    for (const emu::GattCharacteristic &c : s.characteristics) {
      NimBLERemoteCharacteristic *characteristic = new NimBLERemoteCharacteristic;
      characteristic->client_ = this;
      characteristic->uuid_ = NimBLEUUID(c.uuid);
      characteristic->notify_ = c.notify;
      characteristic->indicate_ = c.indicate;
      characteristic->write_ = c.write;
      characteristic->writeNoResponse_ = c.writeNoResponse;
      service->characteristics_.push_back(characteristic);
    }
    services_.push_back(service);
  }
  emu::log("ble", "controller connected to %s (%s)", p->label.c_str(), p->address.c_str());
  p->onConnect();
  return true;
}

bool NimBLEClient::isConnected() const {
  WorldLock lock(g_world);
  return peer_ != nullptr;
}

int NimBLEClient::disconnect(uint8_t) {
  WorldLock lock(g_world);
  if (peer_) {
    emu::log("ble", "controller disconnected from %s", peer_->label.c_str());
    emu::Radio::unlink(this, true);
  }
  return 0;
}

std::vector<NimBLERemoteService *> *NimBLEClient::getServices(bool) { return &services_; }

NimBLERemoteService *NimBLEClient::getService(const NimBLEUUID &uuid) {
  for (NimBLERemoteService *service : services_) {
    if (service->getUUID() == uuid) return service;
  }
  return nullptr;
}

NimBLERemoteCharacteristic *NimBLERemoteService::getCharacteristic(const NimBLEUUID &uuid) {
  for (NimBLERemoteCharacteristic *c : characteristics_) {
    if (c->getUUID() == uuid) return c;
  }
  return nullptr;
}

bool NimBLERemoteCharacteristic::subscribe(bool, notify_callback callback, bool) {
  WorldLock lock(g_world);
  if (!client_->peer_ || !(notify_ || indicate_)) return false;
  callback_ = callback;
  subscribed_ = true;
  emu::log("ble", "controller -> %s  subscribe %.8s", client_->peer_->label.c_str(),
           uuid_.toString().c_str());
  return true;
}

bool NimBLERemoteCharacteristic::writeValue(const uint8_t *data, size_t length, bool) {
  WorldLock lock(g_world);
  emu::Peripheral *p = client_->peer_;
  if (!p || !(write_ || writeNoResponse_)) return false;
  // Acaia heartbeat: ef dd 00 ... every 2.75 s.
  const bool periodic = length == 7 && data[0] == 0xef && data[1] == 0xdd && data[2] == 0x00;
  if (!periodic || emu::g_logPeriodic) {
    emu::log("ble", "controller -> %s  write %.8s  [%s]", p->label.c_str(),
             uuid_.toString().c_str(), emu::hex(data, length).c_str());
  }
  p->onWrite(uuid_.toString(), data, length);
  return true;
}
