#pragma once
// BLE central for both peripherals, on NimBLE-Arduino:
//   BleScale     - the scale link, whatever the make. Which scale it is, and
//                  everything about its bytes, is a driver in core/ScaleLink.h;
//                  this class finds the driver whose characteristics the
//                  connected device has and moves the bytes.
//   BlePusher    - a button pusher link, whatever the make (SwitchBot Bot,
//                  Tuya Fingerbot, MicroBot Push). The make, its bytes and
//                  the press sequence are in core/PusherLink.h. There are
//                  two: the brew bot and the power bot.
//   BleManager   - one scanner, keeps the links up at the same time.
//
// Threading: BleManager::run() (its own task) does the blocking scan/connect
// work. Commands and tick()/service() are called from the control task.
// Notifications arrive on the NimBLE host task and only touch queues/atomics;
// a pusher's are queued and handled in its tick().

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>

#include "core/BrewController.h"
#include "core/PusherLink.h"
#include "core/ScaleLink.h"

namespace bbw {

constexpr size_t ADDR_LEN = 18;  // "aa:bb:cc:dd:ee:ff"

class BleScale : public IScale {
 public:
  void begin();
  // Control task
  bool connected() const override { return ready_.load(); }
  bool tare() override { return command(ScaleOp::Tare); }
  bool startTimer() override { return command(ScaleOp::StartTimer); }
  bool stopTimer() override { return command(ScaleOp::StopTimer); }
  bool resetTimer() override { return command(ScaleOp::ResetTimer); }
  bool nextSample(WeightSample &out);  // drains the notification queue
  void service(uint32_t nowMs);        // heartbeat + silence watchdog
  // BLE task
  bool connectTo(const NimBLEAddress &address);
  bool linkUp() const { return client_ && client_->isConnected(); }
  void disconnect();
  const char *protocolId() const;
  const char *name() const { return name_; }
  void setName(const char *name);  // the advertised name; before connectTo()
  uint32_t droppedFrames() const { return link_.droppedFrames(); }
  ScaleDiag diag() const { return link_.diag(); }
  // What the connected scale can do (ScaleCap bits); 0 while none is connected.
  uint32_t caps() const { return ready_ ? link_.caps() : 0; }
  // While a shot runs nothing is asked of the scale that the shot does not need.
  void setShotRunning(bool running) { shotRunning_ = running; }
  // Timer value last reported by the scale; false if none arrived recently.
  bool timer(uint32_t nowMs, uint32_t &timerMs) const {
    return ready_ && link_.timer(nowMs, timerMs);
  }

 private:
  class ClientCallbacks;
  bool command(ScaleOp op);
  bool write(const uint8_t *data, size_t length);

  NimBLEClient *client_ = nullptr;
  NimBLERemoteCharacteristic *writeChar_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  QueueHandle_t samples_ = nullptr;
  std::atomic<bool> ready_{false};
  ScaleLink link_;
  std::atomic<bool> shotRunning_{false};
  char name_[24] = {};
};

class BlePusher : public IActuator, private PusherWriter {
 public:
  // name: "bot" (brew button) or "power" (power button); it leads the log lines.
  void begin(const char *name, const Settings *settings, const PusherSecrets &secrets);
  // Takes effect at the next connection. Control task (or with its lock held).
  void setSecrets(const PusherSecrets &secrets);
  // Control task
  bool connected() const override { return link_.ready(ready_); }
  bool beginPress(uint32_t holdMs = 0) override { return link_.beginPress(holdMs, millis(), ready_); }
  PressResult result() const override { return link_.result(); }
  uint32_t pressSentAtMs() const override { return link_.pressSentAtMs(); }
  void clearResult() override { link_.clearResult(); }
  void tick(uint32_t nowMs);
  // BLE task
  bool connectTo(const NimBLEAddress &address);
  bool linkUp() const { return client_ && client_->isConnected(); }
  void disconnect();
  // True while a press is in progress or the arm still has to come back up.
  bool busy() const { return link_.busy(); }
  // The session could not be set up (key missing or wrong): not retried
  // until the key changes or the link is no longer wanted.
  bool gaveUp() const { return gaveUp_.load(); }
  void forgive() { gaveUp_ = false; }
  // Last info read from the pusher; kept after the link is dropped.
  PusherInfo info() const { return link_.info(); }
  uint8_t lastStatus() const { return link_.lastStatus(); }
  // Which make the last connected pusher was, and what it can do.
  const char *protocolId() const;
  const char *protocolLabel() const;
  uint32_t caps() const { return link_.caps(); }
  const char *problem() const { return link_.problem(); }

 private:
  class ClientCallbacks;
  struct Note {
    uint8_t length;
    uint8_t data[32];
  };
  bool write(const uint8_t *data, size_t length) override;

  const char *name_ = "bot";
  NimBLEClient *client_ = nullptr;
  NimBLERemoteCharacteristic *writeChar_ = nullptr;
  bool writeWithResponse_ = false;
  SemaphoreHandle_t mutex_ = nullptr;
  QueueHandle_t notes_ = nullptr;
  std::atomic<bool> ready_{false};
  std::atomic<bool> gaveUp_{false};
  PusherLink link_;
};

// Which make an advertisement looks like. A guess for the pairing list only:
// what a device is, is decided by its characteristics when it connects.
enum class PusherMake : uint8_t { SWITCHBOT, TUYA, MICROBOT };

enum class DeviceKind : uint8_t { SCALE, BOT };

struct SeenDevice {
  DeviceKind kind = DeviceKind::SCALE;
  char address[ADDR_LEN] = {};
  uint8_t addressType = 0;
  char name[24] = {};
  int8_t rssi = 0;
  uint32_t seenAtMs = 0;
  PusherMake make = PusherMake::SWITCHBOT;  // kind BOT only
};

// Which peripherals this controller is bound to. Persisted by the app.
struct Pairing {
  char scaleAddress[ADDR_LEN] = {};  // empty: adopt the first supported scale that connects
  uint8_t scaleAddressType = 0;
  char botAddress[ADDR_LEN] = {};    // the brew bot. Empty: none. Never auto-adopted.
  uint8_t botAddressType = 0;
};

class BleManager {
 public:
  static constexpr size_t MAX_SEEN = 10;

  BleManager(BleScale &scale, BlePusher &bot, BlePusher &power)
      : scale_(scale), bot_(bot), power_(power) {}
  bool begin(const Pairing &pairing, const char *powerAddress, uint8_t powerAddressType);
  void run();  // never returns; call from a dedicated task

  // Thread-safe accessors for the UI / web layer.
  size_t seen(SeenDevice *out, size_t max);
  Pairing pairing();
  void setScale(const char *address, uint8_t type);  // "" forgets the scale
  void setBot(const char *address, uint8_t type);    // "" forgets the Bot
  // The power bot is not in Pairing (an older stored record): the app keeps its address.
  void setPower(const char *address, uint8_t type);  // "" forgets the power bot
  void powerAddress(char out[ADDR_LEN]);
  bool pairStrongestBot(uint32_t withinMs = 15000);  // hold the controller next to it
  bool takePairingDirty();
  // The Bot link is held only while wanted (around a shot). Otherwise the Bot
  // is left alone so its battery lasts.
  void setBotWanted(bool wanted) { botWanted_ = wanted; }
  bool botWanted() const { return botWanted_.load(); }
  // The power bot's link is held only while a press of the power button is due.
  void setPowerWanted(bool wanted) { powerWanted_ = wanted; }
  // Keep scanning for a while so the pairing list fills in.
  void requestDiscovery(uint32_t forMs = 30000) { discoveryUntilMs_ = millis() + forMs; }

 private:
  class ScanCallbacks;
  void onAdvertisement(NimBLEAdvertisedDevice *device);
  void remember(const SeenDevice &device);

  BleScale &scale_;
  BlePusher &bot_;
  BlePusher &power_;
  // Keeps one pusher's link up while it is wanted. BLE task.
  void servePusher(BlePusher &pusher, const char *name, const char *address, uint8_t type,
                   bool wanted, uint8_t &failures, uint32_t &retryAtMs, NimBLEScan *scan);
  SemaphoreHandle_t mutex_ = nullptr;
  Pairing pairing_;
  bool pairingDirty_ = false;
  SeenDevice seen_[MAX_SEEN];
  size_t seenCount_ = 0;

  std::atomic<bool> botWanted_{false};
  std::atomic<bool> powerWanted_{false};
  char powerAddress_[ADDR_LEN] = {};
  uint8_t powerAddressType_ = 0;
  uint8_t powerFailures_ = 0;
  uint32_t powerRetryAtMs_ = 0;
  std::atomic<uint32_t> discoveryUntilMs_{0};

  // Scale candidate found by the scanner, consumed by run(). The Bot is
  // connected straight from its stored address, no scan needed.
  bool scaleFound_ = false;
  SeenDevice scaleCandidate_;
  uint8_t scaleFailures_ = 0, botFailures_ = 0;
  uint32_t scaleRetryAtMs_ = 0, botRetryAtMs_ = 0;
};

}  // namespace bbw
