#pragma once
// CoreBluetooth implementation of the firmware's BleBackend (emu/src/ble_backend.h).
// See ios/Host/BLE.md for the threading model, the address rules and the test list.
//
// Plain C so the C API (bbw_host_set_ble_backend) can install it without
// including C++ or Objective-C headers.

#ifdef __cplusplus
extern "C" {
#endif

// Bluetooth state as the app UI needs it.
typedef enum {
  BBW_BT_UNKNOWN = 0,       // not determined yet (permission prompt pending, or radio resetting)
  BBW_BT_POWERED_ON = 1,
  BBW_BT_POWERED_OFF = 2,
  BBW_BT_UNAUTHORIZED = 3,  // the user denied Bluetooth, or it is restricted
  BBW_BT_UNSUPPORTED = 4
} BbwBluetoothState;

// Creates the backend. Cheap: it does not touch CoreBluetooth, so no permission
// prompt appears yet. The CBCentralManager is created in the backend's init(),
// which the firmware calls from BleManager::begin().
//   dataDir:   the app's data folder; the address book is <dataDir>/host/ble_ids.json.
//              NULL keeps the book in memory only.
//   restoreId: CBCentralManagerOptionRestoreIdentifierKey. NULL or "" disables
//              state restoration.
// The result is a `BleBackend *` converted to `void *` (from the BleBackend
// type, not from the derived class): pass it on with
//   bleSetBackend(static_cast<BleBackend *>(ptr)).
// Calling it again returns the same backend. The backend lives for the process.
void *bbw_corebluetooth_backend_create(const char *dataDir, const char *restoreId);

// Current Bluetooth state. Safe from any thread, before or after the backend
// exists (before the central exists it reads CBManager.authorization).
int bbw_corebluetooth_state(void);  // a BbwBluetoothState

#ifdef __cplusplus
}
#endif
