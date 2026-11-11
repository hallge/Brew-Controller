#pragma once
// The plain structs that cross the Swift/C++ boundary. No standard-library types and no core headers
// here, so that what Swift has to import stays small: integers, floats, bools, fixed arrays, and a
// `const char *` that points at static text.

#include <stddef.h>
#include <stdint.h>

// What a BridgeCommand is for.
constexpr uint8_t kCmdNone = 0;
constexpr uint8_t kCmdWriteScale = 1;  // bytes for the scale's write characteristic
constexpr uint8_t kCmdWriteBot = 2;    // bytes for the Bot's write characteristic

// Fault switches of the simulator (Faults sheet).
constexpr uint8_t kFaultLostAck = 1;      // the next Bot press is made, its acknowledgement is lost
constexpr uint8_t kFaultMissedStop = 2;   // the next press made while the machine runs is ignored
constexpr uint8_t kFaultScaleSilent = 3;  // the scale sends nothing (stays until switched off)

// Probe log entry kinds.
constexpr uint8_t kProbeTick = 1;     // a = ms since the previous tick, c = state
constexpr uint8_t kProbeSample = 2;   // a = weight g, c = sample sequence
constexpr uint8_t kProbeCommand = 3;  // a = bytes (0: a simulated press or timer, no bytes), c = target
constexpr uint8_t kProbeState = 4;    // c = new state
constexpr uint8_t kProbeNote = 5;     // c = note code (kNote*)

constexpr uint8_t kNotePaused = 1;       // ticks paused: a = seconds
constexpr uint8_t kNoteResumed = 2;      // ticks resumed: a = seconds the core was not ticked
constexpr uint8_t kNoteFault = 3;        // a fault switch changed: b = kind, a = 1 on / 0 off
constexpr uint8_t kNoteRequest = 4;      // a user request: b = 1 start, 2 stop, 3 tare, 4 acknowledge
constexpr uint8_t kNoteCommandQueueFull = 5;

constexpr uint8_t kBridgeCommandBytes = 20;  // one BLE write of the smallest packet

// One write the core wants made: Swift drains these after each call and sends them with CoreBluetooth.
struct BridgeCommand {
  uint8_t kind = kCmdNone;
  uint8_t length = 0;
  uint32_t atMs = 0;
  uint8_t bytes[kBridgeCommandBytes] = {};

  // Swift sees a fixed array as a tuple: these two stand in for indexing it.
  uint8_t byte(uint8_t index) const { return index < kBridgeCommandBytes ? bytes[index] : 0; }
  void setByte(uint8_t index, uint8_t value) {
    if (index < kBridgeCommandBytes) bytes[index] = value;
  }
};

// First in, first out, fixed size. A full queue refuses the new command and counts it: dropping an
// old one would reorder a press sequence.
struct CommandQueue {
  static constexpr uint8_t kCapacity = 64;

  BridgeCommand items[kCapacity];
  uint8_t head = 0;
  uint8_t count = 0;
  uint32_t dropped = 0;
  uint32_t pushed = 0;

  bool push(const BridgeCommand &command) {
    if (count >= kCapacity) {
      ++dropped;
      return false;
    }
    items[static_cast<uint8_t>((head + count) % kCapacity)] = command;
    ++count;
    ++pushed;
    return true;
  }
  // An empty queue answers a command of kind kCmdNone.
  BridgeCommand pop() {
    BridgeCommand none;
    if (count == 0) return none;
    const BridgeCommand out = items[head];
    head = static_cast<uint8_t>((head + 1) % kCapacity);
    --count;
    return out;
  }
  uint8_t size() const { return count; }
  void clear() {
    head = 0;
    count = 0;
  }
};

// What the screen shows, taken in one call.
struct LiveView {
  const char *stateName = "";  // static text
  uint8_t state = 0;           // bbw::State
  bool fault = false;
  float weightG = 0.0f;
  float flowGS = 0.0f;
  float shotTimeS = 0.0f;
  float targetG = 0.0f;
  bool scaleConnected = false;
  bool botConnected = false;
  bool weightFresh = false;
  bool manualStopNeeded = false;
  bool botWanted = false;       // the core wants the Bot link up (real devices)
  bool scaleLinkLost = false;   // the core's watchdog says the scale link is dead (real devices)
  bool ticksPaused = false;
  uint32_t completedShots = 0;
};

// The last completed shot (bbw::ShotRecord, the fields the spike shows).
struct ShotSummary {
  bool valid = false;
  uint32_t id = 0;
  float targetWeightG = 0.0f;
  float stopOffsetG = 0.0f;
  float stopWeightG = 0.0f;
  float finalWeightG = 0.0f;
  float dripWeightG = 0.0f;
  float shotTimeS = 0.0f;
  float doseG = 0.0f;
  uint16_t stopLatencyMs = 0;
  uint8_t endReason = 0;  // bbw::EndReason
};

struct ProbeEntry {
  uint32_t seq = 0;  // 0: no such entry (not written yet, or overwritten)
  uint32_t atMs = 0;
  uint8_t kind = 0;
  float a = 0.0f;
  float b = 0.0f;
  uint32_t c = 0;
};
