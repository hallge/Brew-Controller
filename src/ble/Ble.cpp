#include "Ble.h"

#include <algorithm>

#include "Log.h"

namespace bbw {

RadioTest g_radioTest;

namespace {


// 15-30 ms connection interval, 4 s supervision timeout.
void configureClient(NimBLEClient *client) {
  client->setConnectionParams(12, 24, 0, 400, g_radioTest.connectInterval.load(),
                              g_radioTest.connectWindow.load());
  // Long enough to span one of a sleeping SwitchBot's advertisements
  // (every 10-15 s); each restart of the attempt loses time.
  client->setConnectTimeout(10);
}

// A failed connection attempt in words, from the BLE host's code (NimBLE:
// host codes below 0x100, controller (HCI) codes from 0x200).
const char *connectFailText(int code) {
  switch (code) {
    case -1: return "connected, but its service could not be set up";
    case 0: return "no answer within the connect time";
    case 2: return "an attempt was already running";
    case 13: case 19: return "timed out";
    case 14: return "already connected";
    case 15: return "radio busy";
    case 0x208: return "link lost (supervision timeout)";
    case 0x213: return "the pusher closed the link";
    case 0x216: return "closed by this controller";
    case 0x21a: case 0x23b: return "connection settings refused";
    case 0x23e: return "the link could not be set up";
    default: return "BLE error";
  }
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
  }
  configureClient(client_);
  ready_ = false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  writeChar_ = nullptr;
  bool ok = client_->connect(address, true);
  connectError_ = ok ? 0 : client_->getLastError();
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
    if (!ok) {
      connectError_ = -1;
      LOGW("%s: no supported pusher service/characteristics usable", name_);
    }
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
                       uint8_t powerAddressType, const char *stopAddress,
                       uint8_t stopAddressType) {
  mutex_ = xSemaphoreCreateMutex();
  pairing_ = pairing;
  strlcpy(powerAddress_, powerAddress ? powerAddress : "", ADDR_LEN);
  powerAddressType_ = powerAddressType;
  strlcpy(stopAddress_, stopAddress ? stopAddress : "", ADDR_LEN);
  stopAddressType_ = stopAddressType;
  NimBLEDevice::init("BrewByWeight");
  if (!NimBLEDevice::getInitialized()) return false;
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCallbacks(*this), true);
  // Active scan: the Bot's device-type byte is in its scan response.
  scan->setActiveScan(true);
  // 50 ms listen every 100 ms leaves airtime for the connections and Wi-Fi.
  // Not more: scanning goes on while the Setup page is open, also with the
  // scale connected, and at full time the scale's readings stopped arriving
  // (measured on the board: the link dropped after 5 s without a packet).
  scan->setInterval(160);
  scan->setWindow(80);
  scan->setMaxResults(0);
  // Every advertisement, not only the first per device. The radio's own
  // filter reports a device once per scan; if that first report comes
  // without the scan response, a SwitchBot is not yet recognisable as a Bot
  // (its type byte is in the response) and a scale may have no name yet, and
  // the device was then never looked at again. Measured on the board: paired
  // bots missing from the list, and the scale not found, while scanning.
  scan->setDuplicateFilter(false);
  return true;
}

void BleManager::resetHeard() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (size_t i = 0; i < seenCount_; ++i) seen_[i].heard = 0;
  advertsAtReset_ = advertsHeard_.load();
  heardSinceMs_ = millis();
  xSemaphoreGive(mutex_);
}

void BleManager::remember(const SeenDevice &device) {
  size_t slot = seenCount_;
  size_t oldest = 0;
  for (size_t i = 0; i < seenCount_; ++i) {
    if (strcmp(seen_[i].address, device.address) == 0) { slot = i; break; }
    if (seen_[i].seenAtMs < seen_[oldest].seenAtMs) oldest = i;
  }
  uint16_t heard = 1;
  if (slot == seenCount_) {
    if (seenCount_ < MAX_SEEN) ++seenCount_; else slot = oldest;
  } else {
    heard = seen_[slot].heard < 0xFFFF ? seen_[slot].heard + 1 : 0xFFFF;
    // A scan response carries no name: keep the one already known.
    const bool defaultName = device.name[0] == '\0' || strcmp(device.name, "SwitchBot") == 0 ||
                             (strcmp(device.name, "SwitchBot Bot") == 0 &&
                              strcmp(seen_[slot].name, "SwitchBot") != 0);
    if (defaultName) {
      if (seen_[slot].name[0] != '\0') {
        SeenDevice merged = device;
        strlcpy(merged.name, seen_[slot].name, sizeof(merged.name));
        merged.heard = heard;
        seen_[slot] = merged;
        return;
      }
    }
  }
  seen_[slot] = device;
  seen_[slot].heard = heard;
}

void BleManager::onAdvertisement(NimBLEAdvertisedDevice *device) {
  ++advertsHeard_;
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
  // Every SwitchBot also puts Woan's manufacturer data in the advertisement
  // itself: company 0x0969, then its own address. The type byte above is
  // only in the scan response, which a passive scan never asks for and an
  // active one here often misses. Captured beside the board from a PC: both
  // Bots send exactly this, every 2-3 s, while the board listed neither
  // unless paired. So a SwitchBot is recognised from the advertisement; the
  // response, once it comes, tells a Bot from the other SwitchBot products.
  bool switchBotByMaker = false;
  if (!hasSwitchBotData) {
    const std::string m = device->getManufacturerData();
    if (m.size() >= 8 && static_cast<uint8_t>(m[0]) == 0x69 && static_cast<uint8_t>(m[1]) == 0x09) {
      char own[ADDR_LEN];
      snprintf(own, sizeof(own), "%02x:%02x:%02x:%02x:%02x:%02x", static_cast<uint8_t>(m[2]),
               static_cast<uint8_t>(m[3]), static_cast<uint8_t>(m[4]), static_cast<uint8_t>(m[5]),
               static_cast<uint8_t>(m[6]), static_cast<uint8_t>(m[7]));
      switchBotByMaker = strcasecmp(own, seen.address) == 0;
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
  if (!isBot && switchBotByMaker) {
    isBot = true;
    defaultName = "SwitchBot";  // type not known yet
  }
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
                         (powerAddress_[0] != '\0' && strcmp(powerAddress_, seen.address) == 0) ||
                         (stopAddress_[0] != '\0' && strcmp(stopAddress_, seen.address) == 0);
  const bool pairedScale = pairing_.scaleAddress[0] != '\0' &&
                           strcmp(pairing_.scaleAddress, seen.address) == 0;
  if (isBot || pairedBot) {
    seen.kind = DeviceKind::BOT;
    if (seen.name[0] == '\0') strlcpy(seen.name, defaultName, sizeof(seen.name));
    remember(seen);
  } else if (isScale || pairedScale) {
    seen.kind = DeviceKind::SCALE;
    remember(seen);
    if (pairedScale) scaleHeardAtMs_ = seen.seenAtMs ? seen.seenAtMs : 1;
    if (pairedScale || (pairing_.scaleAddress[0] == '\0' && isScale)) {
      scaleCandidate_ = seen;
      scaleFound_ = true;
    }
  }
  xSemaphoreGive(mutex_);
}

namespace {
void attemptStarted(LinkStats &s) {
  if (s.runStartMs == 0) s.runStartMs = millis() ? millis() : 1;
  ++s.attempts;
}
void attemptEnded(LinkStats &s, bool ok) {
  if (!ok) {
    ++s.failures;
    return;
  }
  ++s.connects;
  s.lastWaitMs = millis() - s.runStartMs;
  if (s.lastWaitMs > s.longestWaitMs) s.longestWaitMs = s.lastWaitMs;
  s.runStartMs = 0;
}
}  // namespace

void BleManager::servePusher(BlePusher &pusher, const char *name, const char *address,
                             uint8_t type, bool wanted, bool blocked, uint8_t &failures,
                             uint32_t &retryAtMs, NimBLEScan *scan, LinkStats &stats) {
  if (!wanted) stats.runStartMs = 0;  // a run of attempts ends when the link is no longer wanted
  const bool paired = address[0] != '\0';
  // No longer needed: let it go back to sleep. Never mid-press, and never
  // while its arm still has to be brought back up.
  if (paired && !wanted && pusher.linkUp() && !pusher.busy()) {
    LOGI("%s: idle, releasing link", name);
    pusher.disconnect();
  }
  if (!wanted) pusher.forgive();
  if (!paired || !wanted || blocked || pusher.linkUp() || pusher.gaveUp() ||
      g_radioTest.holdConnects.load() ||
      static_cast<int32_t>(millis() - retryAtMs) < 0) {
    return;
  }
  scan->stop();
  LOGI("%s: connecting to %s", name, address);
  attemptStarted(stats);
  const uint32_t startMs = millis();
  const bool connected = pusher.connectTo(NimBLEAddress(address, type));
  attemptEnded(stats, connected);
  if (connected) {
    failures = 0;
  } else {
    // Why it failed, and whether the pusher was being heard: a pusher heard
    // just before at a strong signal that still does not answer is busy
    // (another app or hub connected to it) or asleep, not out of range.
    const int code = pusher.lastConnectError();
    char heard[64] = "not heard advertising";
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (size_t i = 0; i < seenCount_; ++i) {
      if (strcasecmp(seen_[i].address, address) != 0 || seen_[i].seenAtMs == 0) continue;
      snprintf(heard, sizeof(heard), "last heard %lu s before, %d dBm",
               static_cast<unsigned long>((startMs - seen_[i].seenAtMs) / 1000), seen_[i].rssi);
      break;
    }
    xSemaphoreGive(mutex_);
    LOGW("%s: connect failed after %lu ms: %s (code %d); %s", name,
         static_cast<unsigned long>(millis() - startMs), connectFailText(code), code, heard);
    // A press is waiting on this link: retry fast, back off to 2 s at most.
    if (failures < 10) ++failures;
    retryAtMs = millis() + std::min<uint32_t>(2000, 250u * failures);
  }
}

void BleManager::run() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  uint32_t testGeneration = 0;
  for (;;) {
    const uint32_t now = millis();
    if (g_radioTest.generation.load() != testGeneration) {
      testGeneration = g_radioTest.generation.load();
      if (scan->isScanning()) scan->stop();
      scan->setInterval(g_radioTest.scanInterval.load());
      scan->setWindow(g_radioTest.scanWindow.load());
      LOGI("radio test: scan %u/%u, connect scan %u/%u, active %d, always scan %d, hold connects %d",
           g_radioTest.scanWindow.load(), g_radioTest.scanInterval.load(),
           g_radioTest.connectWindow.load(), g_radioTest.connectInterval.load(),
           g_radioTest.active.load(), g_radioTest.alwaysScan.load(), g_radioTest.holdConnects.load());
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const Pairing paired = pairing_;
    char powerAddress[ADDR_LEN], stopAddress[ADDR_LEN];
    strlcpy(powerAddress, powerAddress_, ADDR_LEN);
    const uint8_t powerType = powerAddressType_;
    strlcpy(stopAddress, stopAddress_, ADDR_LEN);
    const uint8_t stopType = stopAddressType_;
    const bool botPaired = paired.botAddress[0] != '\0';
    const bool resting = scaleResting_.load();
    const bool wantScale = !scale_.linkUp() && !resting;
    const bool connectScale = wantScale && scaleFound_ &&
                              static_cast<int32_t>(now - scaleRetryAtMs_) >= 0;
    const SeenDevice scaleTarget = scaleCandidate_;
    if (connectScale || resting) scaleFound_ = false;
    xSemaphoreGive(mutex_);
    // Let go of an idle scale so that it can switch itself off.
    if (resting && scale_.linkUp()) {
      LOGI("scale: idle, letting it go so it can switch itself off");
      scale_.disconnect();
    }

    // The bots a shot needs first: they are the only way to stop the machine.
    servePusher(bot_, "bot", paired.botAddress, paired.botAddressType, botWanted_, false,
                botFailures_, botRetryAtMs_, scan, botStats_);
    // Three links at most: the scale, the brew bot, and one of these two.
    // Whichever is up keeps its place until it has been let go (the
    // application decides which is wanted when; a press in progress is
    // never cut off), and only then is the other connected.
    servePusher(stop_, "stop", stopAddress, stopType, stopWanted_, power_.linkUp(), stopFailures_,
                stopRetryAtMs_, scan, stopStats_);
    // Kept while the scale is off: not connected, and not heard for 30 s
    // (a scale switched on advertises until it is connected). Also 30 s since
    // its link was last up: while connected it does not advertise, so when
    // the link has just dropped (an idle scale let go to rest) the last time
    // it was heard is old, and the power bot would be tried as a third link.
    if (scale_.linkUp()) scaleLinkUpAtMs_ = now ? now : 1;
    const uint32_t scaleHeardAt = scaleHeardAtMs_.load();
    // A scale let go to rest counts as off at once: it will not be taken back unasked, so the power bot
    // is linked while the scale goes to sleep and is ready when the machine is to be switched off.
    const bool scaleOff = !scale_.linkUp() &&
                          (resting ||
                           ((scaleHeardAt == 0 || static_cast<int32_t>(now - scaleHeardAt) > 30000) &&
                            (scaleLinkUpAtMs_ == 0 || static_cast<int32_t>(now - scaleLinkUpAtMs_) > 30000)));
    const bool powerWanted = powerWanted_.load() || (powerKept_.load() && scaleOff);
    servePusher(power_, "power", powerAddress, powerType, powerWanted, stop_.linkUp(),
                powerFailures_, powerRetryAtMs_, scan, powerStats_);
    if (connectScale) {
      scan->stop();
      LOGI("scale: connecting to %s (%s)", scaleTarget.address, scaleTarget.name);
      scale_.setName(scaleTarget.name);  // some makes are only told apart by it
      attemptStarted(scaleStats_);
      const bool scaleConnected = scale_.connectTo(NimBLEAddress(scaleTarget.address, scaleTarget.addressType));
      attemptEnded(scaleStats_, scaleConnected);
      if (scaleConnected) {
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
    const bool needScan = !scale_.linkUp() || !botPaired || discovering || g_radioTest.alwaysScan.load();
    // Active scanning (asking each device for its scan response) only while
    // something has to be recognised that is not paired yet: a SwitchBot's
    // type byte and often a scale's name are in the response. Otherwise
    // passive. The BLE library reports an advertisement of a device that
    // offers a scan response only once that response has arrived too, and
    // the response is easily lost: measured on the board, a paired bot was
    // then "heard" once in minutes. Paired devices are known by address.
    // SwitchBots are known by the advertisement itself now (above), so a
    // passive scan finds them too. Active is left for a scale not paired yet
    // (its name may be in the response), and then in 3 s turns with passive:
    // while active, the library holds back each advertisement until its
    // response arrives, and every restart drops those it still holds.
    const bool needActive = discovering || paired.scaleAddress[0] == '\0';
    const int8_t forced = g_radioTest.active.load();
    const bool wantActive = forced >= 0 ? forced == 1 : needActive && (millis() / 3000) % 2 == 0;
    if (scan->isScanning() && wantActive != activeScan_) scan->stop();
    if (needScan && !scan->isScanning()) {
      scan->setActiveScan(wantActive);
      activeScan_ = wantActive;
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

void BleManager::setStop(const char *address, uint8_t type) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  strlcpy(stopAddress_, address ? address : "", ADDR_LEN);
  stopAddressType_ = type;
  xSemaphoreGive(mutex_);
  stop_.disconnect();
}

void BleManager::stopAddress(char out[ADDR_LEN]) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  strlcpy(out, stopAddress_, ADDR_LEN);
  xSemaphoreGive(mutex_);
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
        strcmp(d.address, powerAddress_) != 0 && strcmp(d.address, stopAddress_) != 0 &&
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
