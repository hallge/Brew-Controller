# CoreBluetooth backend (`ble_corebluetooth.mm`)

Implements `BleBackend` (`emu/src/ble_backend.h`) for the iPhone app. Install with
`bleSetBackend(static_cast<BleBackend *>(bbw_corebluetooth_backend_create(dataDir, restoreId)))`
before `NimBLEDevice::init()`. `bbw_corebluetooth_state()` gives powered on, off, unauthorized,
unsupported or unknown for the UI.

**Status: never compiled with the iOS SDK, never run on a device.** The pure C++ helpers (UUID text,
SwitchBot MAC, address book, error codes) are tested on a PC by compiling the file with
`-DBBW_CB_HELPERS_ONLY`. Everything else is untested.

## Threading

- One `CBCentralManager` on one serial queue (`bbw.corebluetooth`), never main. All CoreBluetooth
  state (peripherals, links, waiters, address book) is touched on that queue only.
- Firmware threads never `dispatch_sync`. A blocking call posts a block with `dispatch_async` and
  waits on its own `Waiter` / `ConnectOp` condition variable, completed by the delegate callback.
- `_mu` guards only the callbacks and the set of live link ids (for `isConnected`). It is never held
  while calling a callback, nor while waiting on the queue. Lock order, where two are held:
  `ConnectOp::m` then `_mu`.
- Callbacks (`onAdvert`, `onNotify`, `onDisconnect`) run on the CoreBluetooth queue.
- Reentrancy: a call made on the queue itself runs inline when it can (`disconnect`, `scanStart`,
  `scanStop`, write without response). `connect`, `subscribe` and write with response cannot wait for
  their own callbacks, so on the queue they return false (connect logs it).
- Timeouts: `connect` gives up after `timeoutMs` (error 0), then cancels on the queue; a link that
  finishes after that is torn down without an `onDisconnect`. `subscribe` 5 s, write with response 8 s.
  Write without response waits at most 100 ms for `canSendWriteWithoutResponse`, then writes anyway.
- Exactly one `onDisconnect` per link: whoever removes the link from `_links` reports it
  (`endLink`). Bluetooth turning off ends all links and pending attempts the same way.
- Connect errors: own timeout 0, CBError connection timeout and encryption timeout 13, peripheral
  disconnected 0x208, everything else 0x23e.

## Addresses

- 17 characters, lower case. Book: `<dataDir>/host/ble_ids.json` (outside `fs/`, so outside backups):
  flat JSON, `"aa:bb:cc:dd:ee:ff": "<identifier>"`, or `"<identifier>|mac"` for a real MAC. Keys
  starting with `_` hold the paired scale.
- SwitchBot (manufacturer company 0x0969, at least 8 bytes): the MAC is bytes 2 to 7 of the
  manufacturer data, in that order. `Ble.cpp` (about line 474) formats the same bytes and compares
  them with our address, so they must match. Once a MAC is known for an identifier it is kept even if
  a later advertisement (for example a scan response) has no manufacturer data.
- Anything else: first 6 bytes of `CBPeripheral.identifier`, top two bits of the first byte set.
- `connect` to an address: look up the identifier, `retrievePeripherals`. If unknown, scan until an
  advertisement maps to that address, within the timeout.

## Background (the paired scale)

"The paired scale" is the peripheral of the most recent successful firmware `connect` whose
characteristics include a scale-only UUID (legacy 2a80, the 49535343-... pairs, the other
scale makes' own UUIDs; list in `isScaleCharacteristic`). It is saved in the address book.

A background scan with no service filter returns nothing on iOS, so the backend keeps a pending
`connectPeripheral` to that peripheral (re-armed after Bluetooth comes on, after a restore, and 5 s
after a link ends). When it connects, the firmware gets a synthetic advertisement (address, name,
RSSI 0) each second, and when it next calls `connect` the peripheral is already connected and goes
straight to discovery. If the firmware does not take it within 15 s the connection is cancelled, so a
scale that is on but unwanted is not held awake, and retried after 60 s.

## Untested assumptions (need the iPhone)

1. Compiles as written (ARC, C++ members in the Objective-C class, blocks capturing `self`).
2. SwitchBot's manufacturer data arrives in the iOS advertisement dictionary, with company id first.
   iOS may merge or drop parts of the scan response.
3. SwitchBot MAC byte order is as `Ble.cpp` expects (copied from it, not seen on a phone).
4. The Lunar's characteristic UUIDs in the list identify it; the app really does connect to the Lunar
   by the address the book gave.
5. `canSendWriteWithoutResponse` and `peripheralIsReadyToSendWriteWithoutResponse` behave as assumed
   for SwitchBot writes at about 10 Hz or less.
6. A standing `connectPeripheral` to a Lunar really wakes the app in the background, and the scale then
   still accepts the firmware's service setup quickly. Whether a scale connected for up to 15 s keeps
   it awake enough to matter for battery.
7. State restoration: `willRestoreState` arrives before `didUpdateState` and the restored peripheral
   is usable. The restore identifier must be stable across launches.
8. Bonding / encryption prompts: none expected for the Lunar or SwitchBot; if a pairing prompt appears
   on connect, the 10 s firmware timeout may fire first.
9. `init` waits up to 2.5 s for the first state; if the permission prompt is still open the first
   `scanStart` returns true and the scan starts when Bluetooth turns on.
10. Peripheral objects are forgotten once 400 are known, except connected, linked and standing ones.

## Not done

- Service/characteristic changes (`didModifyServices`) are ignored.
- `setActiveScan` does nothing: iOS always reports scan responses.
- `addressType` is 0 for SwitchBot (real MAC), 1 otherwise. The firmware never uses it for connect.

## Checklist on the owner's iPhone

1. First launch: the Bluetooth prompt appears; allow. `bbw_corebluetooth_state()` becomes powered on.
2. Pair the Lunar: it appears in the scale list with its name; select it; weight readings arrive; tare.
3. Pair the SwitchBot: it appears as a bot (not just "SwitchBot", the type may take a moment); pair; a
   test press works. Check the pairing survives an app restart (address book file exists).
4. Pair the Tuya and MicroBot pushers if owned.
5. Run a shot: start, weight stream, stop by weight, no dropped writes, a single disconnect at the end.
6. Lock the phone mid-shot: the scale stream and the stop press keep working.
7. Switch the scale off and on with the app backgrounded: the app wakes and reconnects (standing connect).
8. Force-quit and let iOS relaunch for the scale (restoration), then check the paired scale is found.
9. Bluetooth off in Control Centre mid-shot: the app sees one disconnect, no hang; Bluetooth on: it
   recovers by itself.
10. Leave the scale on with the app open but the controller idle for a minute: it is not held connected
    forever (grace and hold-off behaviour); watch the scale's battery over a day.
11. Deny the permission once and check the state reads unauthorized.
