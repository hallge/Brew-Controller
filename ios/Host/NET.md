# Network, device key and MQTT in the iPhone build (W6)

Design: `ios-parity/design.md` section 4. Everything the firmware used to get from the Windows-only
emulator files (`emu_devicekey`, `emu_homeassistant`, `emu_telemetry`, `emu_preview`,
`emu_addonnet`) is here, with the board's behaviour (`src/platform/*`).

## Files

| File | What |
|---|---|
| `net_portable.h/.cpp` | Library link, device signer, MQTT glue, DER to r‖s, `httpRequest`. Compiles everywhere. |
| `host_stubs.cpp` | Home Assistant REST, diagnostics post, page preview, add-on fetch, and the C `hostHttp()`. Compiles everywhere. |
| `mqtt_client.h/.cpp` | Minimal MQTT 3.1.1 client (packets, session, POSIX socket transport). |
| `ios_net.mm` | `hostHttpNative`: URLSession. iOS only. |
| `ios_devicekey.mm` | `hostP256Generate/Sign`: Security.framework. iOS only. |

Off Apple (the Windows runner) `net_portable.cpp` provides stand-ins for the two `.mm` files:
`hostHttpNative` answers -1 "no network in this build", the key falls back to the
`HOST-UNSIGNED` signer the library refuses. Nothing in the tests contacts a service.

## What is implemented

- **One transport seam**: `hostnet::httpRequest` -> `hostHttpNative` (URLSession, blocking on a
  semaphore, refuses the main thread, honours `timeoutMs`, truncates at the buffer and reports it).
  `hostHttp()` from `BrewHost.h` is a thin wrapper.
- **Shared library** (`LibraryLink.h`): configure, relay put/get, own repository + token put/get
  (GitHub contents API, `inbox` branch, base64), status and `via` text, backoff 60 s doubling to 1 h,
  409 -> key missing, 400/401/403/413 -> refused for good, 422 -> already there, 404 on get -> not a
  failure, too-big answer -> false. Same strings as the board. State is behind a mutex; no request
  holds it. "Ready" needs a configured link, a set clock and no backoff (there is no Wi-Fi check:
  URLSession fails fast and the backoff covers it).
- **Diagnostics post**: https only, the relay's `"error"` text in the problem.
- **Home Assistant**: REST climate pull and the webhook (`haRequest`), both over `http://` or `https://`
  with a bearer token when given. MQTT: discovery configs (retained, one per entity), availability
  will + `online`, state publishing (`mqttPublish`), subscription to the Start topic and `PRESS`
  handling, 30 s keep-alive, reconnect at most once a minute and only when idle.
- **Page preview**: browser-style agent, `Accept-Language`, redirects followed by the system, reads up
  to `</head` or 192 KB, tried twice, board's wording for 403/429/503.
- **Add-on fetch**: GET/POST, Cookie in, first Set-Cookie out, no redirects, the job's own time-out;
  https only with the job's SHA-256 pin: the leaf certificate's SHA-256 is compared in the URLSession
  challenge (no host name check, like the board); a mismatch is status -3.
- **Device key**: P-256 by `SecKeyCreateRandomKey` (in memory, not permanent), signed with
  `kSecKeyAlgorithmECDSASignatureDigestX962SHA256` over the firmware's own 32-byte hash, DER converted
  to the 64-byte raw r‖s. Algorithm `ES256`, 65-byte uncompressed public key.
  **Persistence is the Preferences way**, in the board's own `devkey` record
  (`{u16 version=1, priv[32], pub[65]}`), so a backup carries it (when "with secrets") and a key from
  a board backup works here and the other way round. Not in the Keychain: if App Review or the owner
  wants that, the seam is `HostSigner::begin()` in `net_portable.cpp`; but then a backup would no
  longer carry it.

### MQTT: own client, not PubSubClient

PubSubClient needs an Arduino `Client` (`connect/write/read/available/flush/stop`), `IPAddress`,
`String`, `Stream` and a socket class built on them, none of which the portable shims have; a
minimal client on POSIX sockets is about 300 lines, has no dependencies, and its packet encoding and
session logic are tested on Windows with a fake transport. It sends QoS 0 only (CONNECT with
auth and a retained will, PUBLISH retained or not, SUBSCRIBE, PINGREQ, DISCONNECT) and understands
incoming QoS 0/1 PUBLISH. TLS (`mqtts://`) is not supported; neither is it on the board
(`mqttBrokerFromUrl` only takes `mqtt://`).

## What only the device can confirm

- **Local Network permission.** The first connection to a LAN address (Home Assistant, the MQTT
  broker, an add-on's device) raises the system prompt; the text is `NSLocalNetworkUsageDescription`.
  If refused, requests fail with "could not connect" / "no route"; nothing in the app can tell the
  two apart, so the problem text is generic. The prompt appears for the POSIX-socket MQTT too.
- **ATS.** HTTPS only, except plain `http://` to a Home Assistant or device on the LAN, which
  `NSAllowsLocalNetworking` covers (IP addresses, unqualified names, `.local`). A Home Assistant at
  `http://something.lan:8123` or any other qualified name is **not** covered and is refused with
  "plain http is allowed only to local network addresses". The alternative, `NSAllowsArbitraryLoads`,
  is an App Review risk; not recommended. Whether the pinned self-signed https of an add-on's device
  passes under `NSAllowsLocalNetworking` (the challenge handler overrides trust) is untested.
- **Background.** URLSession and the sockets run only while the app is awake; MQTT drops when the app
  is suspended and reconnects (after up to a minute) when the loop runs again. Nothing here keeps
  the app alive. The firmware's HA polling loop keeps its board periods.
- **`.local` names** resolve through the system's mDNS (needs the Local Network permission; no
  `NSBonjourServices` entry since nothing browses).
- **Clock.** The library waits for the clock (`time(nullptr) > 1700000000`), which on iOS is always true.
- `SecKeyCreateSignature` with `ECDSASignatureDigestX962SHA256` and the 97-byte X9.63 import are
  documented behaviour but only exercised on a device or simulator. The DER to r‖s conversion is
  tested here.

## Info.plist keys for the project thread

```xml
<key>NSLocalNetworkUsageDescription</key>
<string>Connects to Home Assistant, an MQTT broker or a device on your network when you set them up.</string>
<key>NSAppTransportSecurity</key>
<dict>
  <key>NSAllowsLocalNetworking</key>
  <true/>
</dict>
```

No `NSBonjourServices`, no `NSAllowsArbitraryLoads`, no Keychain access group, no background modes
for networking. Link `Security.framework` and `CommonCrypto` (both implicit with Foundation/Security
imports; no extra library for the sockets).

## Tests

`test/hostapp/net_test.cpp` (built and run by `test/hostapp/run.sh`, before the headless runner)
uses a fake transport: DER to r‖s vectors, MQTT packet bytes and the Start/availability/discovery flow,
library link requests and backoff, HA/diagnostics/preview/add-on request shape and wording.
