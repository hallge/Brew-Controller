#include "Ble.h"

#include <algorithm>

#include "Log.h"

namespace bbw {

namespace {


// 15-30 ms connection interval, 4 s supervision timeout.
void configureClient(NimBLEClient *client) {
  client->setConnectionParams(12, 24, 0, 400);
  client->setConnectTimeout(6);
}

}  // namespace

// ================================================================= BleScale ==

class BleScale::ClientCallbacks : public NimBLEClientCallbacks {
 public:
  explicit ClientCallbacks(BleScale &owner) : owner_(owner) {}
  void onDisconnect(NimBLEClient *) override {
    owner_.ready_ = false;
    LOGI("scale: disconnected");
  }

 private:
  BleScale &owner_;
};

void BleScale::begin() {
  mutex_ = xSemaphoreCreateMutex();
  samples_ = xQueueCreate(64, sizeof(WeightSample));
  link_.setSampleSink(
      [](void *queue, const WeightSample &sample) {
        xQueueSend(static_cast<QueueHandle_t>(queue), &sample, 0);
      },
      samples_);
}

void BleScale::setName(const char *name) {
  strlcpy(name_, name ? name : "", sizeof(name_));
}

const char *BleScale::protocolId() const {
  const ScaleDriver *driver = link_.driver();
  return driver ? driver->id() : "";
}

void BleScale::disconnect() {
  ready_ = false;
  if (client_ && client_->isConnected()) client_->disconnect();
}

bool BleScale::connectTo(const NimBLEAddress &address) {
  if (!client_) {
    client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(new ClientCallbacks(*this), false);
    configureClient(client_);
  }
  ready_ = false;
  // Holding the mutex keeps the control task's writes away from the
  // attribute objects while connect() rebuilds them.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  writeChar_ = nullptr;
  link_.close();
  bool ok = client_->connect(address, true);
  ScaleDriver *driver = nullptr;
  NimBLERemoteCharacteristic *readChar = nullptr;
  NimBLERemoteCharacteristic *writeChar = nullptr;
  if (ok) {
    // Which scale this is: the first driver whose characteristic pair the
    // device has. Same order as AcaiaArduinoBLE / Open Brew: legacy 2a80
    // first, then the 49535343-... pair used by the Lunar 2021 and Pyxis,
    // then the other makes. Where two makes share a pair, the advertised
    // name (set before connecting) decides.
    std::vector<NimBLERemoteService *> *services = client_->getServices(true);
    driver = link_.identify(name_, [&](const ScaleDriver &candidate) {
      const NimBLEUUID readUuid(candidate.protocol().readUuid);
      const NimBLEUUID writeUuid(candidate.protocol().writeUuid);
      for (size_t i = 0; services && i < services->size(); ++i) {
        NimBLERemoteCharacteristic *r = (*services)[i]->getCharacteristic(readUuid);
        NimBLERemoteCharacteristic *w = (*services)[i]->getCharacteristic(writeUuid);
        if (r && w && (r->canNotify() || r->canIndicate()) &&
            (w->canWrite() || w->canWriteNoResponse())) {
          readChar = r;
          writeChar = w;
          return true;
        }
      }
      return false;
    });
    ok = driver != nullptr;
    if (!ok) LOGW("scale: no supported characteristic pair found");
  }
  if (ok) {
    LOGI("scale: %s", driver->protocol().debugLabel);
    // Before subscribing: nothing is feeding the driver yet.
    link_.select(driver);
    ok = readChar->subscribe(readChar->canNotify(),
                             [this](NimBLERemoteCharacteristic *, uint8_t *data,
                                    size_t length, bool) { link_.onNotify(data, length, millis()); });
    if (!ok) LOGW("scale: subscribe failed");
  }
  if (ok) {
    writeChar_ = writeChar;
    const ScaleProtocol &protocol = driver->protocol();
    for (size_t i = 0; ok && i < protocol.initWriteCount; ++i) {
      const ScalePayload &payload = protocol.initWrites[i];
      ok = writeChar->writeValue(payload.data, payload.length,
                                 !writeChar->canWriteNoResponse());
      LOGI("scale: %s write %s", payload.label, ok ? "ok" : "failed");
      delay(50);
    }
  }
  if (!ok) {
    writeChar_ = nullptr;
    xSemaphoreGive(mutex_);
    if (client_->isConnected()) client_->disconnect();
    return false;
  }
  link_.start(millis());
  xQueueReset(samples_);
  ready_ = true;
  xSemaphoreGive(mutex_);
  return true;
}

bool BleScale::write(const uint8_t *data, size_t length) {
  // Never wait: if the BLE task is (re)connecting, the command simply fails.
  if (!ready_ || xSemaphoreTake(mutex_, 0) != pdTRUE) return false;
  bool ok = false;
  if (writeChar_ && client_ && client_->isConnected()) {
    // Without response where the scale allows it (what AcaiaArduinoBLE forces
    // with its properties patch), so the control loop never blocks on a write.
    ok = writeChar_->writeValue(data, length, !writeChar_->canWriteNoResponse());
  }
  xSemaphoreGive(mutex_);
  return ok;
}

bool BleScale::command(ScaleOp op) {
  uint8_t buffer[SCALE_MAX_COMMAND_LENGTH];
  int length = 0;
  // A scale without this command (a timer, say) is simply not asked.
  if (!link_.encode(op, buffer, &length)) return false;
  const bool ok = write(buffer, static_cast<size_t>(length));
  LOGI("scale: cmd %d %s", static_cast<int>(op), ok ? "sent" : "FAILED");
  return ok;
}

bool BleScale::nextSample(WeightSample &out) {
  return samples_ && xQueueReceive(samples_, &out, 0) == pdTRUE;
}

void BleScale::service(uint32_t nowMs) {
  if (!ready_) return;
  const bool alive = link_.service(nowMs, shotRunning_, [this](const uint8_t *data, size_t length) {
    write(data, length);
  });
  if (!alive) {
    LOGW("scale: no packets for %u ms, dropping link",
         static_cast<unsigned>(link_.silenceLimitMs()));
    disconnect();
  }
}

// ================================================================ BlePusher ==

class BlePusher::ClientCallbacks : public NimBLEClientCallbacks {
 public:
  explicit ClientCallbacks(BlePusher &owner) : owner_(owner) {}
  void onDisconnect(NimBLEClient *) override {
    owner_.ready_ = false;
    LOGI("%s: disconnected", owner_.name_);
  }

 private:
  BlePusher &owner_;
};

void BlePusher::begin(const char *name, const Settings *settings, const PusherSecrets &secrets) {
  name_ = name;
  mutex_ = xSemaphoreCreateMutex();
  notes_ = xQueueCreate(16, sizeof(Note));
  link_.begin(name, settings, this);
  link_.setSecrets(secrets);
  link_.setSources([]() -> uint32_t { return esp_random(); },
                   []() -> uint32_t { return static_cast<uint32_t>(time(nullptr)); });
  link_.setLog([](const char *line) { LOGI("%s", line); });
}

void BlePusher::setSecrets(const PusherSecrets &secrets) {
  link_.setSecrets(secrets);
  gaveUp_ = false;
}

const char *BlePusher::protocolId() const {
  const PusherDriver *driver = link_.driver();
  return driver ? driver->id() : "";
}

const char *BlePusher::protocolLabel() const {
  const PusherDriver *driver = link_.driver();
  return driver ? driver->label() : "";
}

void BlePusher::disconnect() {
  ready_ = false;
  if (client_ && client_->isConnected()) client_->disconnect();
}

bool BlePusher::connectTo(const NimBLEAddress &address) {
  if (!client_) {
    client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(new ClientCallbacks(*this), false);
    configureClient(client_);
  }
  ready_ = false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  writeChar_ = nullptr;
  bool ok = client_->connect(address, true);
  PusherDriver *driver = nullptr;
  NimBLERemoteCharacteristic *notifyChar = nullptr;
  NimBLERemoteCharacteristic *writeChar = nullptr;
  if (ok) {
    const auto usable = [&](NimBLERemoteService *service, const PusherDriver &candidate) {
      NimBLERemoteCharacteristic *n = service->getCharacteristic(candidate.notifyUuid());
      NimBLERemoteCharacteristic *w = service->getCharacteristic(candidate.writeUuid());
      if (!n || !w || !(n->canNotify() || n->canIndicate()) ||
          !(w->canWrite() || w->canWriteNoResponse())) {
        return false;
      }
      notifyChar = n;
      writeChar = w;
      return true;
    };
    // Which pusher this is: the first driver whose characteristic pair the
    // device has. A make whose service is known (the SwitchBot's) is asked
    // for by it first, as before; only then is the whole table read.
    driver = link_.identify([&](const PusherDriver &candidate) {
      if (!candidate.serviceUuid()) return false;
      NimBLERemoteService *service = client_->getService(candidate.serviceUuid());
      return service && usable(service, candidate);
    });
    if (!driver) {
      std::vector<NimBLERemoteService *> *services = client_->getServices(true);
      driver = link_.identify([&](const PusherDriver &candidate) {
        for (size_t i = 0; services && i < services->size(); ++i) {
          if (usable((*services)[i], candidate)) return true;
        }
        return false;
      });
    }
    ok = driver != nullptr &&
         notifyChar->subscribe(notifyChar->canNotify(),
                               [this](NimBLERemoteCharacteristic *, uint8_t *data, size_t length,
                                      bool) {
                                 Note note;
                                 note.length = static_cast<uint8_t>(
                                     length < sizeof(note.data) ? length : sizeof(note.data));
                                 memcpy(note.data, data, note.length);
                                 xQueueSend(notes_, &note, 0);
                               });
    if (!ok) LOGW("%s: no supported pusher service/characteristics usable", name_);
  }
  if (!ok) {
    xSemaphoreGive(mutex_);
    if (client_->isConnected()) client_->disconnect();
    return false;
  }
  writeChar_ = writeChar;
  writeWithResponse_ = driver->writeWithResponse();
  xQueueReset(notes_);
  xSemaphoreGive(mutex_);
  // The control task takes the new connection up in its next tick: it starts
  // the pusher's session, if it has one, and asks for battery and mode.
  link_.connected(driver);
  ready_ = true;
  LOGI("%s: connected (%s)", name_, driver->label());
  return true;
}

bool BlePusher::write(const uint8_t *data, size_t length) {
  if (!ready_ || xSemaphoreTake(mutex_, 0) != pdTRUE) return false;
  bool ok = false;
  if (writeChar_ && client_ && client_->isConnected()) {
    // Without response where the pusher allows it; the acknowledgement of a
    // SwitchBot arrives as a notification on cba20003 (same as pySwitchbot:
    // write_gatt_char(..., False)).
    ok = writeChar_->writeValue(data, length,
                                writeWithResponse_ || !writeChar_->canWriteNoResponse());
  }
  xSemaphoreGive(mutex_);
  return ok;
}

void BlePusher::tick(uint32_t nowMs) {
  Note note;
  while (notes_ && xQueueReceive(notes_, &note, 0) == pdTRUE) {
    link_.onNotify(note.data, note.length);
  }
  link_.tick(nowMs, ready_);
  if (ready_ && link_.sessionFailed() && !link_.busy()) {
    gaveUp_ = true;
    disconnect();
  }
}

// =============================================================== BleManager ==

class BleManager::ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
 public:
  explicit ScanCallbacks(BleManager &owner) : owner_(owner) {}
  void onResult(NimBLEAdvertisedDevice *device) override { owner_.onAdvertisement(device); }

 private:
  BleManager &owner_;
};

bool BleManager::begin(const Pairing &pairing, const char *powerAddress,
                       uint8_t powerAddressType) {
  mutex_ = xSemaphoreCreateMutex();
  pairing_ = pairing;
  strlcpy(powerAddress_, powerAddress ? powerAddress : "", ADDR_LEN);
  powerAddressType_ = powerAddressType;
  NimBLEDevice::init("BrewByWeight");
  if (!NimBLEDevice::getInitialized()) return false;
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCallbacks(*this), true);
  // Active scan: the Bot's device-type byte is in its scan response.
  scan->setActiveScan(true);
  // 50 ms listen every 100 ms leaves airtime for two connections and Wi-Fi.
  scan->setInterval(160);
  scan->setWindow(80);
  scan->setMaxResults(0);
  return true;
}

void BleManager::remember(const SeenDevice &device) {
  size_t slot = seenCount_;
  size_t oldest = 0;
  for (size_t i = 0; i < seenCount_; ++i) {
    if (strcmp(seen_[i].address, device.address) == 0) { slot = i; break; }
    if (seen_[i].seenAtMs < seen_[oldest].seenAtMs) oldest = i;
  }
  if (slot == seenCount_) {
    if (seenCount_ < MAX_SEEN) ++seenCount_; else slot = oldest;
  }
  seen_[slot] = device;
}

void BleManager::onAdvertisement(NimBLEAdvertisedDevice *device) {
  SeenDevice seen;
  strlcpy(seen.address, device->getAddress().toString().c_str(), sizeof(seen.address));
  seen.addressType = device->getAddress().getType();
  strlcpy(seen.name, device->getName().c_str(), sizeof(seen.name));
  seen.rssi = static_cast<int8_t>(device->getRSSI());
  seen.seenAtMs = millis();

  // Scales are found by advertised name prefix (LUNAR, ACAIA, PYXIS, FELIC,
  // Decent Scale...), exactly as the reference libraries do.
  const bool isScale = scaleNameIsCompatible(seen.name);

  // SwitchBot: service data under 0xFD3D (current) or 0x0D00 (older firmware),
  // byte 0 bits 6..0 = device type, 'H' = Bot.
  bool isBot = false;
  bool hasSwitchBotData = false;
  const uint8_t count = device->getServiceDataCount();
  for (uint8_t i = 0; i < count; ++i) {
    const NimBLEUUID uuid = device->getServiceDataUUID(i);
    if (uuid == NimBLEUUID(switchbot::SERVICE_DATA_UUID_NEW) ||
        uuid == NimBLEUUID(switchbot::SERVICE_DATA_UUID_OLD)) {
      const std::string data = device->getServiceData(i);
      hasSwitchBotData = true;
      if (!data.empty() &&
          (static_cast<uint8_t>(data[0]) & 0x7F) == switchbot::DEVICE_TYPE_BOT) {
        isBot = true;
      }
    }
  }
  // Pre-v3.0 Bot firmware: no service data, just the vendor service UUID.
  if (!isBot && !hasSwitchBotData &&
      device->isAdvertisingService(NimBLEUUID(switchbot::SERVICE_UUID))) {
    isBot = true;
  }
  // The other makes. A MicroBot advertises service abcd. Any Tuya BLE device
  // carries service data a201, a Fingerbot like a lamp: it is listed, and it
  // is up to the owner to pick the right one.
  const char *defaultName = "SwitchBot Bot";
  if (!isBot && device->isAdvertisingService(NimBLEUUID(microbot::ADVERTISED_SERVICE_UUID))) {
    isBot = true;
    seen.make = PusherMake::MICROBOT;
    defaultName = "MicroBot Push";
  }
  for (uint8_t i = 0; !isBot && i < count; ++i) {
    if (device->getServiceDataUUID(i) == NimBLEUUID(tuya::SERVICE_DATA_UUID)) {
      isBot = true;
      seen.make = PusherMake::TUYA;
      defaultName = "Tuya BLE device";
    }
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool pairedBot = (pairing_.botAddress[0] != '\0' &&
                          strcmp(pairing_.botAddress, seen.address) == 0) ||
                         (powerAddress_[0] != '\0' && strcmp(powerAddress_, seen.address) == 0);
  const bool pairedScale = pairing_.scaleAddress[0] != '\0' &&
                           strcmp(pairing_.scaleAddress, seen.address) == 0;
  if (isBot || pairedBot) {
    seen.kind = DeviceKind::BOT;
    if (seen.name[0] == '\0') strlcpy(seen.name, defaultName, sizeof(seen.name));
    remember(seen);
  } else if (isScale || pairedScale) {
    seen.kind = DeviceKind::SCALE;
    remember(seen);
    if (pairedScale || (pairing_.scaleAddress[0] == '\0' && isScale)) {
      scaleCandidate_ = seen;
      scaleFound_ = true;
    }
  }
  xSemaphoreGive(mutex_);
}

void BleManager::servePusher(BlePusher &pusher, const char *name, const char *address,
                             uint8_t type, bool wanted, uint8_t &failures, uint32_t &retryAtMs,
                             NimBLEScan *scan) {
  const bool paired = address[0] != '\0';
  // No longer needed: let it go back to sleep. Never mid-press, and never
  // while its arm still has to be brought back up.
  if (paired && !wanted && pusher.linkUp() && !pusher.busy()) {
    LOGI("%s: idle, releasing link", name);
    pusher.disconnect();
  }
  if (!wanted) pusher.forgive();
  if (!paired || !wanted || pusher.linkUp() || pusher.gaveUp() ||
      static_cast<int32_t>(millis() - retryAtMs) < 0) {
    return;
  }
  scan->stop();
  LOGI("%s: connecting to %s", name, address);
  if (pusher.connectTo(NimBLEAddress(address, type))) {
    failures = 0;
  } else {
    // A press is waiting on this link: retry fast, back off to 2 s at most.
    if (failures < 10) ++failures;
    retryAtMs = millis() + std::min<uint32_t>(2000, 250u * failures);
  }
}

void BleManager::run() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  for (;;) {
    const uint32_t now = millis();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const Pairing paired = pairing_;
    char powerAddress[ADDR_LEN];
    strlcpy(powerAddress, powerAddress_, ADDR_LEN);
    const uint8_t powerType = powerAddressType_;
    const bool botPaired = paired.botAddress[0] != '\0';
    const bool wantScale = !scale_.linkUp();
    const bool connectScale = wantScale && scaleFound_ &&
                              static_cast<int32_t>(now - scaleRetryAtMs_) >= 0;
    const SeenDevice scaleTarget = scaleCandidate_;
    if (connectScale) scaleFound_ = false;
    xSemaphoreGive(mutex_);

    // The brew bot first: it is the only way to stop the machine.
    servePusher(bot_, "bot", paired.botAddress, paired.botAddressType, botWanted_, botFailures_,
                botRetryAtMs_, scan);
    servePusher(power_, "power", powerAddress, powerType, powerWanted_, powerFailures_,
                powerRetryAtMs_, scan);
    if (connectScale) {
      scan->stop();
      LOGI("scale: connecting to %s (%s)", scaleTarget.address, scaleTarget.name);
      scale_.setName(scaleTarget.name);  // some makes are only told apart by it
      if (scale_.connectTo(NimBLEAddress(scaleTarget.address, scaleTarget.addressType))) {
        scaleFailures_ = 0;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (pairing_.scaleAddress[0] == '\0') {
          // "First detected": adopt the first scale that completes a connection.
          strlcpy(pairing_.scaleAddress, scaleTarget.address, ADDR_LEN);
          pairing_.scaleAddressType = scaleTarget.addressType;
          pairingDirty_ = true;
        }
        xSemaphoreGive(mutex_);
      } else {
        // Quick first retries, then spread out up to 5 s (Open Brew's policy).
        if (scaleFailures_ < 10) ++scaleFailures_;
        scaleRetryAtMs_ = millis() + std::min<uint32_t>(5000, 500u * scaleFailures_);
      }
    }

    // Scan while the scale is missing, while no Bot is paired, and while the
    // user is looking at the pairing list. A paired Bot needs no scan.
    const bool discovering = static_cast<int32_t>(millis() - discoveryUntilMs_.load()) < 0;
    const bool needScan = !scale_.linkUp() || !botPaired || discovering;
    if (needScan && !scan->isScanning()) {
      scan->start(0, nullptr, false);
    } else if (!needScan && scan->isScanning()) {
      scan->stop();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

size_t BleManager::seen(SeenDevice *out, size_t max) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const size_t n = seenCount_ < max ? seenCount_ : max;
  for (size_t i = 0; i < n; ++i) out[i] = seen_[i];
  xSemaphoreGive(mutex_);
  return n;
}

Pairing BleManager::pairing() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const Pairing copy = pairing_;
  xSemaphoreGive(mutex_);
  return copy;
}

void BleManager::setScale(const char *address, uint8_t type) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  strlcpy(pairing_.scaleAddress, address ? address : "", ADDR_LEN);
  pairing_.scaleAddressType = type;
  pairingDirty_ = true;
  scaleFound_ = false;
  xSemaphoreGive(mutex_);
  scale_.disconnect();
}

void BleManager::setBot(const char *address, uint8_t type) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  strlcpy(pairing_.botAddress, address ? address : "", ADDR_LEN);
  pairing_.botAddressType = type;
  pairingDirty_ = true;
  xSemaphoreGive(mutex_);
  bot_.disconnect();
}

void BleManager::setPower(const char *address, uint8_t type) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  strlcpy(powerAddress_, address ? address : "", ADDR_LEN);
  powerAddressType_ = type;
  xSemaphoreGive(mutex_);
  power_.disconnect();
}

void BleManager::powerAddress(char out[ADDR_LEN]) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  strlcpy(out, powerAddress_, ADDR_LEN);
  xSemaphoreGive(mutex_);
}

bool BleManager::pairStrongestBot(uint32_t withinMs) {
  SeenDevice best;
  bool found = false;
  const uint32_t now = millis();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (size_t i = 0; i < seenCount_; ++i) {
    const SeenDevice &d = seen_[i];
    // Not the power bot, and not a Tuya device: that could as well be a lamp.
    if (d.kind == DeviceKind::BOT && d.make != PusherMake::TUYA &&
        strcmp(d.address, powerAddress_) != 0 &&
        static_cast<uint32_t>(now - d.seenAtMs) <= withinMs && (!found || d.rssi > best.rssi)) {
      best = d;
      found = true;
    }
  }
  xSemaphoreGive(mutex_);
  if (found) setBot(best.address, best.addressType);
  return found;
}

bool BleManager::takePairingDirty() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool dirty = pairingDirty_;
  pairingDirty_ = false;
  xSemaphoreGive(mutex_);
  return dirty;
}

}  // namespace bbw
