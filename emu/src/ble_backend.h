#pragma once
// The radio behind the NimBLE stand-in (emu/shims/NimBLEDevice.h).
//
// The firmware's BLE code (src/ble/Ble.cpp) talks to the NimBLE classes. Those
// classes are a thin facade (emu/src/ble_facade.cpp) that forwards to one
// BleBackend. Backends:
//   - the virtual radio (emu/src/virtual_radio.cpp): simulated Lunar, SwitchBot
//     and Breville; the default in the emulator and in demo mode;
//   - a CoreBluetooth backend on iOS (later).
//
// Portable C++17: no platform headers.
//
// ---------------------------------------------------------------- blocking --
// Backend methods are called from firmware threads (the main loop, the web
// server, the BLE worker) and may block; they are NOT called from a backend
// callback thread unless a callback itself calls into the firmware (see
// below), so a backend may implement a blocking call by posting work to its own
// queue and waiting on a condition variable for the result.
//   init            returns when the radio is usable.
//   scanStart/Stop  return at once. Adverts start/stop arriving shortly after.
//                   scanStart while scanning is a no-op returning true.
//   connect         blocks until the link is up AND all services and
//                   characteristics are discovered (the table is returned), or
//                   until `timeoutMs` has passed or the attempt failed. On
//                   failure it returns false and sets *lastError to a NimBLE /
//                   HCI style code (0 = no specific code, treated by the
//                   firmware as its own timeout). Nothing is delivered through
//                   onDisconnect for a connect that failed.
//   disconnect      returns at once (it may finish asynchronously). Every
//                   connected link ends with exactly one onDisconnect
//                   callback, whichever side ended it, including after the
//                   firmware's own disconnect(). Calling it on a dead link is a
//                   harmless no-op.
//   isConnected     non-blocking.
//   subscribe       blocks until the remote confirmed (or failed) the
//                   notification enable.
//   write           with response: blocks until the remote acknowledged or
//                   failed. Without response: returns once the data is queued
//                   to the radio, waiting at most ~100 ms for buffer space.
//                   Returns false when the link is gone or the write failed.
//
// ----------------------------------------------------------------- threads --
// Callbacks (BleCallbacks) may arrive on any single backend-owned thread (the
// virtual radio thread, the CoreBluetooth queue). They may arrive at any time
// from the moment the callbacks are installed. They must not be called while
// the backend holds a lock that a backend method also takes. The firmware's
// handlers behind them only touch queues and atomics (Ble.h), but a backend
// must never call them while holding, or on behalf of a thread that holds, a
// firmware mutex: the facade guarantees no firmware mutex is taken by the
// facade itself, and the callback receiver must not block for long.
// Callbacks from one backend arrive in order. A backend method called from the
// backend's own callback thread must run inline rather than wait for itself.
//
// ------------------------------------------------------------------- links --
// A BleLink is an opaque non-zero id chosen by the backend. Ids are never
// reused within a process. Notifications or disconnects for a link the facade
// no longer knows (stale generation) are dropped by the facade.
//
// -------------------------------------------------------------- addresses ---
// Addresses are 17 characters, lower case "aa:bb:cc:dd:ee:ff". A backend whose
// OS hides real addresses must synthesise stable ones.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

typedef uint64_t BleLink;  // 0 = none

struct BleAdvert {
  std::string address;       // "aa:bb:cc:dd:ee:ff"
  uint8_t addressType = 0;
  std::string name;          // empty: no local name
  int rssi = 0;
  std::vector<std::pair<std::string, std::string>> serviceData;  // uuid (any form), bytes
  std::string manufacturerData;  // including the 2-byte company id, little endian
  std::vector<std::string> serviceUuids;
};

struct BleCharInfo {
  std::string uuid;
  bool notify = false, indicate = false, write = false, writeNoResponse = false;
  uintptr_t handle = 0;  // backend's own reference to the characteristic, opaque
};

struct BleServiceInfo {
  std::string uuid;
  std::vector<BleCharInfo> characteristics;
  uintptr_t handle = 0;
};

// Identifies a characteristic in subscribe and write.
struct BleCharRef {
  std::string serviceUuid;
  std::string uuid;
  uintptr_t handle = 0;
};

struct BleCallbacks {
  // One advertisement, while scanning (duplicates included).
  std::function<void(const BleAdvert &)> onAdvert;
  // Notification or indication on a subscribed characteristic.
  std::function<void(BleLink, const std::string &charUuid, const uint8_t *, size_t)> onNotify;
  // The link ended (either side).
  std::function<void(BleLink)> onDisconnect;
};

class BleBackend {
 public:
  virtual ~BleBackend() = default;

  // Installs the callbacks; called once before init().
  virtual void setCallbacks(const BleCallbacks &callbacks) = 0;
  virtual void init(const std::string &deviceName) = 0;

  // `active`: also deliver the scan response (service data, service UUIDs).
  virtual void setActiveScan(bool active) = 0;
  virtual bool scanStart() = 0;
  virtual bool scanStop() = 0;
  virtual bool isScanning() = 0;

  virtual bool connect(const std::string &address, uint8_t addressType, uint32_t timeoutMs,
                       BleLink *link, std::vector<BleServiceInfo> *services, int *lastError) = 0;
  virtual void disconnect(BleLink link) = 0;
  virtual bool isConnected(BleLink link) = 0;

  virtual bool subscribe(BleLink link, const BleCharRef &characteristic, bool notifications) = 0;
  virtual bool write(BleLink link, const BleCharRef &characteristic, const uint8_t *data,
                     size_t length, bool response) = 0;
};

// Select the backend. Call once, before NimBLEDevice::init(). Without a call the
// virtual radio is used. The backend must outlive the process's BLE use.
void bleSetBackend(BleBackend *backend);

// The virtual radio (emu/src/virtual_radio.cpp), the default.
BleBackend *bleVirtualRadioBackend();
