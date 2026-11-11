// The NimBLE-Arduino stand-in (emu/shims/NimBLEDevice.h) as a facade over a
// BleBackend (ble_backend.h). No platform headers: portable to iOS.
//
// The facade takes only its own leaf mutex, never a firmware one, and never
// calls out to firmware code while holding it.

#include <NimBLEDevice.h>

#include <atomic>
#include <map>
#include <mutex>
#include <set>

#include "ble_backend.h"

namespace {

std::atomic<BleBackend *> g_backend{nullptr};
std::mutex g_mutex;                      // leaf lock: the tables below
std::map<BleLink, NimBLEClient *> g_clients;  // live links the firmware knows about
std::set<BleLink> g_orphanDisconnects;   // disconnects that beat the registration
std::atomic<NimBLEAdvertisedDeviceCallbacks *> g_scanCallbacks{nullptr};
std::atomic<bool> g_initialized{false};
std::once_flag g_callbacksOnce;
NimBLEScan g_scan;

std::string normalizeUuid(std::string text) {
  for (char &c : text) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  if (text.size() == 4) return "0000" + text + "-0000-1000-8000-00805f9b34fb";
  if (text.size() == 8) return text + "-0000-1000-8000-00805f9b34fb";
  return text;
}

}  // namespace

// Friend of the NimBLE classes: everything that needs their internals.
struct BleFacade {
  static BleBackend *backend() {
    BleBackend *b = g_backend.load();
    if (!b) {
      b = bleVirtualRadioBackend();
      g_backend = b;
    }
    std::call_once(g_callbacksOnce, [b] {
      BleCallbacks callbacks;
      callbacks.onAdvert = onAdvert;
      callbacks.onNotify = onNotify;
      callbacks.onDisconnect = onDisconnect;
      b->setCallbacks(callbacks);
    });
    return b;
  }

  static void onAdvert(const BleAdvert &advert) {
    NimBLEAdvertisedDeviceCallbacks *callbacks = g_scanCallbacks;
    if (!callbacks) return;
    NimBLEAdvertisedDevice device;
    device.address_ = NimBLEAddress(advert.address, advert.addressType);
    device.name_ = advert.name;
    device.rssi_ = advert.rssi;
    device.manufacturerData_ = advert.manufacturerData;
    for (const auto &sd : advert.serviceData) {
      device.serviceData_.emplace_back(NimBLEUUID(sd.first), sd.second);
    }
    for (const std::string &uuid : advert.serviceUuids) device.serviceUuids_.emplace_back(uuid);
    callbacks->onResult(&device);
  }

  static void onDisconnect(BleLink link) {
    NimBLEClient *client = nullptr;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      auto it = g_clients.find(link);
      if (it == g_clients.end()) {
        // Either a link the firmware already replaced (drop it), or one that
        // ended before connect() registered it (connect() picks it up).
        if (g_orphanDisconnects.size() > 64) g_orphanDisconnects.clear();
        g_orphanDisconnects.insert(link);
        return;
      }
      client = it->second;
      g_clients.erase(it);
    }
    if (client->callbacks_) client->callbacks_->onDisconnect(client);
  }

  static void onNotify(BleLink link, const std::string &charUuid, const uint8_t *data,
                       size_t length) {
    NimBLEClient *client = nullptr;
    NimBLERemoteCharacteristic *target = nullptr;
    notify_callback callback;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      auto it = g_clients.find(link);
      if (it == g_clients.end()) return;  // stale link
      client = it->second;
      if (client->link_ != link) return;
      const NimBLEUUID uuid(charUuid);
      for (NimBLERemoteService *service : client->services_) {
        for (NimBLERemoteCharacteristic *c : service->characteristics_) {
          if (c->uuid_ == uuid && c->subscribed_ && c->generation_ == client->generation_) {
            target = c;
            callback = c->callback_;
          }
        }
      }
    }
    // Attribute objects are never freed, so `target` stays valid.
    if (target && callback) {
      std::vector<uint8_t> copy(data, data + length);
      callback(target, copy.data(), copy.size(), true);
    }
  }

  static BleCharRef ref(const NimBLERemoteCharacteristic &c) {
    BleCharRef r;
    r.serviceUuid = c.serviceUuid_.toString();
    r.uuid = c.uuid_.toString();
    r.handle = c.handle_;
    return r;
  }
};

void bleSetBackend(BleBackend *backend) { g_backend = backend; }

// ================================================================== NimBLE ==

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
  BleFacade::backend()->init(deviceName);
  g_initialized = true;
}
bool NimBLEDevice::getInitialized() { return g_initialized; }
void NimBLEDevice::setPower(int) {}
NimBLEScan *NimBLEDevice::getScan() { return &g_scan; }
NimBLEClient *NimBLEDevice::createClient() { return new NimBLEClient; }

void NimBLEScan::setAdvertisedDeviceCallbacks(NimBLEAdvertisedDeviceCallbacks *callbacks, bool) {
  g_scanCallbacks = callbacks;
}
void NimBLEScan::setActiveScan(bool active) { BleFacade::backend()->setActiveScan(active); }
bool NimBLEScan::start(uint32_t, void (*)(void *), bool) { return BleFacade::backend()->scanStart(); }
bool NimBLEScan::stop() { return BleFacade::backend()->scanStop(); }
bool NimBLEScan::isScanning() { return BleFacade::backend()->isScanning(); }

bool NimBLEClient::connect(const NimBLEAddress &address, bool) {
  BleBackend *backend = BleFacade::backend();
  lastError_ = 0;
  // A connect on a client that is still linked replaces the link: the old
  // one is dropped without telling the firmware.
  if (link_ && backend->isConnected(link_)) {
    const BleLink old = link_;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_clients.erase(old);
    }
    backend->disconnect(old);
  }
  BleLink link = 0;
  std::vector<BleServiceInfo> table;
  int error = 0;
  const bool ok = backend->connect(address.toString(), address.getType(),
                                   connectTimeoutS_ * 1000u, &link, &table, &error);
  lastError_ = error;
  if (!ok || !link) return false;

  // Attribute objects of an earlier connection are left alone (never freed): a
  // late notification may still be on its way to them. The generation keeps
  // them from being used.
  std::vector<NimBLERemoteService *> services;
  const uint32_t generation = generation_ + 1;
  for (const BleServiceInfo &s : table) {
    NimBLERemoteService *service = new NimBLERemoteService;
    service->uuid_ = NimBLEUUID(s.uuid);
    service->handle_ = s.handle;
    for (const BleCharInfo &c : s.characteristics) {
      NimBLERemoteCharacteristic *characteristic = new NimBLERemoteCharacteristic;
      characteristic->client_ = this;
      characteristic->uuid_ = NimBLEUUID(c.uuid);
      characteristic->serviceUuid_ = service->uuid_;
      characteristic->handle_ = c.handle;
      characteristic->generation_ = generation;
      characteristic->notify_ = c.notify;
      characteristic->indicate_ = c.indicate;
      characteristic->write_ = c.write;
      characteristic->writeNoResponse_ = c.writeNoResponse;
      service->characteristics_.push_back(characteristic);
    }
    services.push_back(service);
  }
  bool endedEarly = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    services_.swap(services);
    generation_ = generation;
    link_ = link;
    g_clients[link] = this;
    endedEarly = g_orphanDisconnects.erase(link) > 0;
  }
  if (endedEarly) {
    // The link ended before it was registered: tell the firmware now.
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_clients.erase(link);
    }
    if (callbacks_) callbacks_->onDisconnect(this);
  }
  return true;
}

bool NimBLEClient::isConnected() const {
  const BleLink link = link_;
  return link != 0 && BleFacade::backend()->isConnected(link);
}

int NimBLEClient::disconnect(uint8_t) {
  const BleLink link = link_;
  if (link) BleFacade::backend()->disconnect(link);
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

bool NimBLERemoteCharacteristic::subscribe(bool notifications, notify_callback callback, bool) {
  if (!client_->link_ || generation_ != client_->generation_ || !(notify_ || indicate_)) {
    return false;
  }
  notify_callback previous;
  bool wasSubscribed;
  {
    // Installed first so that no notification right after the enable is lost.
    std::lock_guard<std::mutex> lock(g_mutex);
    previous = callback_;
    wasSubscribed = subscribed_;
    callback_ = callback;
    subscribed_ = true;
  }
  if (BleFacade::backend()->subscribe(client_->link_, BleFacade::ref(*this), notifications)) {
    return true;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  callback_ = previous;
  subscribed_ = wasSubscribed;
  return false;
}

bool NimBLERemoteCharacteristic::writeValue(const uint8_t *data, size_t length, bool response) {
  if (!client_->link_ || generation_ != client_->generation_ || !(write_ || writeNoResponse_)) {
    return false;
  }
  return BleFacade::backend()->write(client_->link_, BleFacade::ref(*this), data, length,
                                     response);
}
