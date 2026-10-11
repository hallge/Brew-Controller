#pragma once
// The controller's own key pair: made on the first start, kept in NVS, used
// to sign everything it sends to the shared library (core/Share.h).
//
// On the board this is ECDSA P-256 from mbedTLS (DeviceKey.cpp). The PC
// emulator has no mbedTLS; emu/src/emu_devicekey.cpp stands in for it with
// something that is plainly not a signature and says so in its algorithm
// name, so nothing made in the emulator can pass for the real thing.

#include "core/Share.h"

namespace bbw {

// Loads the key, making and storing one if there is none. false: no key; the
// signer then reports not ready and nothing is queued for sending.
bool beginDeviceKey();
ISigner &deviceSigner();

}  // namespace bbw
