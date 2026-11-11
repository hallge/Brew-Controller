// The virtual radio: emulated peripherals (emu_radio.h) behind the BleBackend
// interface (ble_backend.h). The behaviour is that of the former NimBLE
// stand-in in emu_ble.cpp: the same timing, log lines and delivery order.

#include <Arduino.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "ble_backend.h"
#include "emu.h"
#include "emu_radio.h"

namespace emu {

namespace {

struct Event {
  enum Kind { NOTIFY, DISCONNECT } kind;
  BleLink link;
  std::string characteristic;
  std::vector<uint8_t> data;
};

constexpr uint32_t ADVERTISING_INTERVAL_MS = 200;

}  // namespace

class VirtualRadio : public BleBackend {
 public:
  void setCallbacks(const BleCallbacks &callbacks) override {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    callbacks_ = callbacks;
  }

  void init(const std::string &deviceName) override {
    log("ble", "controller radio up as '%s'", deviceName.c_str());
  }

  void setActiveScan(bool active) override { activeScan_ = active; }
  bool scanStart() override {
    if (!scanning_.exchange(true)) log("ble", "controller scanning");
    return true;
  }
  bool scanStop() override {
    if (scanning_.exchange(false)) log("ble", "controller scan stopped");
    return true;
  }
  bool isScanning() override { return scanning_; }

  bool connect(const std::string &address, uint8_t, uint32_t timeoutMs, BleLink *linkOut,
               std::vector<BleServiceInfo> *services, int *lastError) override {
    if (lastError) *lastError = 0;  // the emulator's failures are all timeouts
    Peripheral *p = nullptr;
    uint32_t wakeMs = 0;
    // A connection is made on one of the peripheral's advertising events, so
    // it has to be advertising within the connect timeout.
    const uint32_t deadline = millis() + timeoutMs;
    for (;;) {
      {
        std::lock_guard<std::recursive_mutex> lock(g_world);
        Peripheral *candidate = find(address);
        if (candidate && !candidate->linked() && candidate->advertising()) {
          p = candidate;
          wakeMs = p->connectDelayMs();
        }
      }
      if (p) break;
      if (static_cast<int32_t>(millis() - deadline) >= 0) {
        log("ble", "connect to %s timed out (not advertising)", address.c_str());
        return false;
      }
      delay(50);
    }
    delay(wakeMs);
    std::lock_guard<std::recursive_mutex> lock(g_world);
    if (p->linked() || !p->advertising()) return false;  // went away while connecting
    const BleLink link = ++nextLink_;
    p->link_ = link;
    links_[link] = p;
    if (services) {
      services->clear();
      for (const GattService &s : p->gatt()) {
        BleServiceInfo service;
        service.uuid = s.uuid;
        for (const GattCharacteristic &c : s.characteristics) {
          BleCharInfo info;
          info.uuid = c.uuid;
          info.notify = c.notify;
          info.indicate = c.indicate;
          info.write = c.write;
          info.writeNoResponse = c.writeNoResponse;
          service.characteristics.push_back(info);
        }
        services->push_back(service);
      }
    }
    *linkOut = link;
    log("ble", "controller connected to %s (%s)", p->label.c_str(), p->address.c_str());
    p->onConnect();
    return true;
  }

  void disconnect(BleLink link) override {
    std::lock_guard<std::recursive_mutex> lock(g_world);
    Peripheral *p = peripheral(link);
    if (!p) return;
    log("ble", "controller disconnected from %s", p->label.c_str());
    unlink(p, true);
  }

  bool isConnected(BleLink link) override {
    std::lock_guard<std::recursive_mutex> lock(g_world);
    return peripheral(link) != nullptr;
  }

  bool subscribe(BleLink link, const BleCharRef &characteristic, bool) override {
    std::lock_guard<std::recursive_mutex> lock(g_world);
    Peripheral *p = peripheral(link);
    if (!p) return false;
    log("ble", "controller -> %s  subscribe %.8s", p->label.c_str(), characteristic.uuid.c_str());
    return true;
  }

  bool write(BleLink link, const BleCharRef &characteristic, const uint8_t *data, size_t length,
             bool) override {
    std::lock_guard<std::recursive_mutex> lock(g_world);
    Peripheral *p = peripheral(link);
    if (!p) return false;
    // Acaia heartbeat: ef dd 00 ... every 2.75 s.
    const bool periodic = length == 7 && data[0] == 0xef && data[1] == 0xdd && data[2] == 0x00;
    if (!periodic || g_logPeriodic) {
      log("ble", "controller -> %s  write %.8s  [%s]", p->label.c_str(),
          characteristic.uuid.c_str(), hex(data, length).c_str());
    }
    p->onWrite(characteristic.uuid, data, length);
    return true;
  }

  // ------------------------------------------------ peripheral-side helpers --
  static VirtualRadio &instance() {
    static VirtualRadio radio;
    return radio;
  }

  void add(Peripheral *p) {
    std::lock_guard<std::recursive_mutex> lock(g_world);
    peripherals_.push_back(p);
  }

  void queue(Event event) { events_.push_back(std::move(event)); }  // caller holds g_world

  // Both sides of a link going down. Caller holds g_world.
  void unlink(Peripheral *p, bool tellFirmware) {
    const BleLink link = p->link_;
    if (!link) return;
    p->link_ = 0;
    links_.erase(link);
    p->onDisconnect();
    if (tellFirmware) events_.push_back({Event::DISCONNECT, link, "", {}});
  }

  void run() {
    auto last = std::chrono::steady_clock::now();
    uint32_t lastAdvertMs = 0;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      const auto now = std::chrono::steady_clock::now();
      double dt = std::chrono::duration<double>(now - last).count();
      last = now;
      if (dt > 0.25) dt = 0.25;  // PC stalled: do not jump the simulation

      std::vector<BleAdvert> adverts;
      std::deque<Event> events;
      {
        std::lock_guard<std::recursive_mutex> lock(g_world);
        for (Peripheral *p : peripherals_) p->step(dt);
        if (scanning_ && millis() - lastAdvertMs >= ADVERTISING_INTERVAL_MS) {
          lastAdvertMs = millis();
          for (Peripheral *p : peripherals_) {
            if (p->linked() || !p->advertising()) continue;
            const Advertisement a = p->advertisement();
            BleAdvert device;
            device.address = p->address;
            device.addressType = p->addressType;
            device.name = a.name;
            device.rssi = a.rssi;
            // Service data and the service list are in the scan response:
            // a passive scan does not see them.
            if (activeScan_) {
              device.serviceData = a.serviceData;
              device.serviceUuids = a.serviceUuids;
            }
            adverts.push_back(device);
          }
        }
        events.swap(events_);
      }
      // Firmware callbacks run here, as on the NimBLE host task, with no
      // emulator lock held.
      BleCallbacks callbacks;
      {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        callbacks = callbacks_;
      }
      for (BleAdvert &device : adverts) {
        if (callbacks.onAdvert && scanning_) callbacks.onAdvert(device);
      }
      for (Event &event : events) deliver(event, callbacks);
    }
  }

 private:
  void deliver(Event &event, const BleCallbacks &callbacks) {
    if (event.kind == Event::DISCONNECT) {
      if (callbacks.onDisconnect) callbacks.onDisconnect(event.link);
      return;
    }
    {
      std::lock_guard<std::recursive_mutex> lock(g_world);
      if (!peripheral(event.link)) return;  // link went away meanwhile
    }
    if (callbacks.onNotify) {
      callbacks.onNotify(event.link, event.characteristic, event.data.data(), event.data.size());
    }
  }

  Peripheral *find(const std::string &address) {
    for (Peripheral *p : peripherals_) {
      if (p->address == address) return p;
    }
    return nullptr;
  }

  Peripheral *peripheral(BleLink link) {  // caller holds g_world
    auto it = links_.find(link);
    return it == links_.end() ? nullptr : it->second;
  }

  friend class Peripheral;

  std::vector<Peripheral *> peripherals_;   // guarded by g_world
  std::deque<Event> events_;                // guarded by g_world
  std::map<BleLink, Peripheral *> links_;   // guarded by g_world
  BleLink nextLink_ = 0;                    // guarded by g_world
  std::atomic<bool> scanning_{false};
  std::atomic<bool> activeScan_{false};
  std::mutex callbacksMutex_;
  BleCallbacks callbacks_;
};

void Peripheral::notify(const std::string &characteristic, const std::vector<uint8_t> &data,
                        bool periodic) {
  std::lock_guard<std::recursive_mutex> lock(g_world);
  if (!link_) return;
  if (!periodic || g_logPeriodic) {
    log("ble", "%s -> controller  notify %.8s  [%s]", label.c_str(), characteristic.c_str(),
        hex(data.data(), data.size()).c_str());
  }
  VirtualRadio::instance().queue({Event::NOTIFY, link_, characteristic, data});
}

void Peripheral::dropLink(const char *why) {
  std::lock_guard<std::recursive_mutex> lock(g_world);
  if (!link_) return;
  log("ble", "%s closed the connection (%s)", label.c_str(), why);
  VirtualRadio::instance().unlink(this, true);
}

void radioAdd(Peripheral *peripheral) { VirtualRadio::instance().add(peripheral); }

void radioBegin() {
  std::thread([] { VirtualRadio::instance().run(); }).detach();
}

}  // namespace emu

BleBackend *bleVirtualRadioBackend() { return &emu::VirtualRadio::instance(); }
