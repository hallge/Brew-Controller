#pragma once
// One scale connection, without the radio: the drivers this controller has,
// and what a connection does with the bytes it is given. Notifications go in,
// WeightSamples and the link's diagnostics come out; heartbeat, the idle poll
// and the silence watchdog are decided here. ble/Ble.cpp only moves the bytes.
//
// Pure C++, no Arduino: the host tests run whole notification streams through
// it for every driver.
//
// Threading on the controller: onNotify() runs on the NimBLE host task,
// select()/start()/close() on the BLE task while nothing is subscribed,
// service() on the control task. What crosses tasks is atomic.

#include <atomic>

#include "AcaiaDriver.h"
#include "ScaleDriver.h"
#include "Types.h"

namespace bbw {

// What the scale link has seen since it connected, for the Devices page.
struct ScaleDiag {
  uint32_t frames = 0;          // valid frames
  uint32_t weightFrames = 0;
  uint32_t timerFrames = 0;     // weight frames that carried the scale's timer
  uint32_t badFrames = 0;       // checksum or length wrong
  uint32_t splitFrames = 0;     // frames that arrived over two notifications
  uint32_t dropped = 0;         // notifications nothing could be made of
  int batteryPercent = -1;      // -1: the scale has not reported it
  int timerRunning = -1;        // -1 unknown, 0 stopped, 1 running
  int lastKey = -1;             // last key pressed on the scale itself, the scale's own code
  uint32_t caps = 0;            // ScaleCap bits of the connected scale; 0: none connected
  uint8_t lastFrame[SCALE_MAX_FRAME] = {};
  size_t lastFrameLength = 0;
};

class ScaleLink : private ScaleEventSink {
 public:
  using SampleFn = void (*)(void *context, const WeightSample &sample);

  // Every accepted reading is handed to this, on the caller of onNotify().
  void setSampleSink(SampleFn fn, void *context) {
    sampleFn_ = fn;
    sampleContext_ = context;
  }

  // The scales this controller can talk to, in the order they are tried on a
  // new connection (the order of the protocol registry).
  size_t driverCount() const { return sizeof(drivers_) / sizeof(drivers_[0]); }
  ScaleDriver *driverAt(size_t index) { return index < driverCount() ? drivers_[index] : nullptr; }

  ScaleDriver *driver() const { return driver_.load(); }
  uint32_t caps() const {
    const ScaleDriver *d = driver();
    return d ? d->caps() : 0;
  }

  // Connection lost or being rebuilt: notifications are ignored.
  void close() { driver_ = nullptr; }

  // The scale a connected device is: the first driver that takes a device of
  // this advertised name and whose characteristic pair the device has.
  // hasPair(const ScaleDriver &) says whether it has.
  template <typename HasPair>
  ScaleDriver *identify(const char *name, HasPair &&hasPair) {
    for (ScaleDriver *candidate : drivers_) {
      if (candidate->allowsName(name) && hasPair(*candidate)) return candidate;
    }
    return nullptr;
  }

  // The connected scale is this one. Before subscribing: nothing is feeding
  // the driver yet.
  void select(ScaleDriver *driver) {
    if (driver) driver->reset();
    driver_ = driver;
  }

  // Session set up, readings expected from now on.
  void start(uint32_t nowMs) {
    gotPacket_ = false;
    timerSeen_ = false;
    weightFrames_ = 0;
    timerFrames_ = 0;
    pollAsked_ = false;
    dropped_ = 0;
    battery_ = -1;
    timerRunning_ = -1;
    lastKey_ = -1;
    lastPacketMs_ = nowMs;
    readyAtMs_ = nowMs;
    const ScaleDriver *d = driver();
    lastHeartbeatMs_ = nowMs - (d ? d->protocol().features.heartbeatPeriodMs : 0);  // one right away
  }

  // The bytes of one notification. The timestamp of a reading is that of the
  // notification that completed it.
  void onNotify(const uint8_t *data, size_t length, uint32_t atMs) {
    ScaleDriver *d = driver();
    if (!d) return;
    notifyAtMs_ = atMs;
    if (!d->feed(data, length, *this)) ++dropped_;
  }

  bool encode(ScaleOp op, uint8_t *out, int *length) const {
    const ScaleDriver *d = driver();
    return d && d->encode(op, out, length);
  }

  // Heartbeat, idle poll and silence watchdog. write(const uint8_t *, size_t)
  // sends bytes to the scale. While a shot runs nothing is asked of the scale
  // that the shot does not need. False: the link is dead, drop it.
  template <typename Write>
  bool service(uint32_t nowMs, bool shotRunning, Write &&write) {
    const ScaleDriver *d = driver();
    if (!d) return true;
    const ScaleFeatureSet &features = d->protocol().features;
    if (features.has(ScaleFeatureHeartbeat) &&
        static_cast<uint32_t>(nowMs - lastHeartbeatMs_) >= features.heartbeatPeriodMs) {
      lastHeartbeatMs_ = nowMs;
      uint8_t buffer[SCALE_MAX_COMMAND_LENGTH];
      int length = 0;
      if (d->encode(ScaleOp::Heartbeat, buffer, &length)) {
        write(buffer, static_cast<size_t>(length));
      }
    }
    if (gotPacket_ && !shotRunning) {
      uint8_t request[SCALE_MAX_WRITE];
      size_t length = 0;
      const uint32_t since = nowMs - (pollAsked_ ? pollAskedMs_ : readyAtMs_);
      if (d->pollRequest(pollAsked_, since, request, &length)) {
        pollAsked_ = true;
        pollAskedMs_ = nowMs;
        write(request, length);
      }
    }
    // Same rule as both reference libraries: a link that stops sending packets
    // is dead even if the radio still says "connected".
    const uint32_t limit = gotPacket_ ? features.maxPacketSilenceMs : FIRST_PACKET_TIMEOUT_MS;
    const uint32_t anchor = gotPacket_ ? lastPacketMs_.load() : readyAtMs_;
    return limit == 0 || static_cast<int32_t>(nowMs - anchor) <= static_cast<int32_t>(limit);
  }

  // The limit service() last judged the silence by, for the log.
  uint32_t silenceLimitMs() const {
    if (!gotPacket_) return FIRST_PACKET_TIMEOUT_MS;
    const ScaleDriver *d = driver();
    return d ? d->protocol().features.maxPacketSilenceMs : 0;
  }

  // Timer value last reported by the scale; false if none arrived recently.
  bool timer(uint32_t nowMs, uint32_t &timerMs) const {
    constexpr uint32_t TIMER_FRESH_MS = 1500;
    if (!timerSeen_ || static_cast<uint32_t>(nowMs - timerAtMs_.load()) > TIMER_FRESH_MS) {
      return false;
    }
    timerMs = timerMs_;
    return true;
  }

  uint32_t droppedFrames() const { return dropped_.load(); }

  ScaleDiag diag() const {
    ScaleDiag d;
    d.weightFrames = weightFrames_;
    d.timerFrames = timerFrames_;
    d.dropped = dropped_;
    d.batteryPercent = battery_;
    d.timerRunning = timerRunning_;
    d.lastKey = lastKey_;
    // The driver's counters are plain words written by the BLE host task; a
    // reading that is one frame behind is fine for a status page.
    if (const ScaleDriver *driver = this->driver()) {
      d.caps = driver->caps();
      d.frames = driver->frames();
      d.badFrames = driver->badFrames();
      d.splitFrames = driver->splitFrames();
      d.lastFrameLength = driver->lastFrame(d.lastFrame, sizeof(d.lastFrame));
    }
    return d;
  }

 private:
  static constexpr uint32_t FIRST_PACKET_TIMEOUT_MS = 5000;  // Open Brew: first valid packet deadline

  void onScaleEvent(const ScaleEvent &event) override {
    if (event.hasBattery) battery_ = event.batteryPercent;
    if (event.hasTimerState) timerRunning_ = event.timerRunning ? 1 : 0;
    if (event.rawKey >= 0) {
      lastKey_ = event.rawKey;
      if (event.button == ScaleButton::START) timerRunning_ = 1;
      if (event.button == ScaleButton::STOP || event.button == ScaleButton::RESET) {
        timerRunning_ = 0;
      }
    }
    if (!event.hasWeight && !event.hasTimer) return;
    const uint32_t atMs = notifyAtMs_;
    lastPacketMs_ = atMs;
    gotPacket_ = true;
    if (event.hasTimer) {
      timerMs_ = event.timerMs;
      timerAtMs_ = atMs;
      timerSeen_ = true;
    }
    if (event.hasWeight) {
      ++weightFrames_;
      if (event.hasTimer) ++timerFrames_;
      WeightSample sample;
      sample.weightG = event.weightG;
      sample.atMs = atMs;  // the timestamp every flow/prediction calculation uses
      sample.sequence = ++sequence_;
      sample.scaleTimerMs = event.hasTimer ? static_cast<int32_t>(event.timerMs) : -1;
      sample.flags = event.weightFlags;
      if (sampleFn_) sampleFn_(sampleContext_, sample);
    }
  }

  // Legacy Acaia (characteristic 2a80) still goes through the vendored parser.
  TableDriver acaiaLegacy_{kScaleProtocolAcaiaLegacy, TableDriver::Timer::NONE};
  AcaiaDriver acaia_;
  TableDriver felicita_{kScaleProtocolFelicita, TableDriver::Timer::WITH_READINGS};
  TableDriver eclair_{kScaleProtocolEclair, TableDriver::Timer::WITH_READINGS};
  TableDriver decent_{kScaleProtocolDecent, TableDriver::Timer::NONE};
  TableDriver difluid_{kScaleProtocolDifluid, TableDriver::Timer::NONE};
  TableDriver myscale_{kScaleProtocolMyscale, TableDriver::Timer::NONE};
  TableDriver weighMyBru_{kScaleProtocolWeighMyBru, TableDriver::Timer::NONE};
  // Its timer comes in frames of its own, in whole seconds: known to the
  // link, but not stored with the readings and not shown as the shot time.
  TableDriver varia_{kScaleProtocolVaria, TableDriver::Timer::SEPARATE};
  TableDriver eureka_{kScaleProtocolEureka, TableDriver::Timer::NONE};
  ScaleDriver *drivers_[10] = {&acaiaLegacy_, &acaia_, &felicita_, &eclair_, &decent_,
                               &difluid_, &myscale_, &weighMyBru_, &varia_, &eureka_};

  std::atomic<ScaleDriver *> driver_{nullptr};
  SampleFn sampleFn_ = nullptr;
  void *sampleContext_ = nullptr;
  uint32_t notifyAtMs_ = 0;     // host task only
  uint32_t sequence_ = 0;       // host task only; never restarts
  std::atomic<uint32_t> lastPacketMs_{0};
  std::atomic<bool> gotPacket_{false};
  std::atomic<uint32_t> dropped_{0};
  std::atomic<uint32_t> timerMs_{0};
  std::atomic<uint32_t> timerAtMs_{0};
  std::atomic<bool> timerSeen_{false};
  std::atomic<uint32_t> weightFrames_{0};
  std::atomic<uint32_t> timerFrames_{0};
  std::atomic<int> battery_{-1};
  std::atomic<int> timerRunning_{-1};
  std::atomic<int> lastKey_{-1};
  uint32_t pollAskedMs_ = 0;
  bool pollAsked_ = false;
  uint32_t readyAtMs_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
};

}  // namespace bbw
