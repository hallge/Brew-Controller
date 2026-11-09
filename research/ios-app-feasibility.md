# A native iOS app in place of the board: feasibility and plan

Dated 2026-11-09, against v0.1.30. Research and planning only: no app code exists. Claims about iOS
carry a source; where the source is a forum post or could not be read in full it says so, and the
points still to be settled on a device are listed in §12 and become Phase 0 of the plan.

## 1. Verdict

**Yes, with one condition and one blocker to clear first.**

- **Feasible.** Everything the board does can run in an iPhone or iPad app, and most of it better
  (screens, history, library, sharing). The brew logic in `src/core` is already free of Arduino,
  exceptions, STL containers and global state, takes time as an argument, and is driven by
  `tick(nowMs)` and `onSample()`: it can be compiled into the app unchanged.
- **Condition: a shot runs with the app open on a mounted, charging device.** iOS gives a backgrounded
  Bluetooth app short wake-ups, not a guaranteed loop, and gives nothing at all after a force-quit
  (§3.1). The app must therefore start shots only in the foreground, keep the screen on while it is
  open, and fail safe if it is sent to the background mid-shot. That is the same fail-safe the
  firmware already has for a lost pusher (`manual_stop_needed`, the machine's own time-out).
- **Things that need an always-on box degrade.** The 08:00 power-on, Home Assistant polling the
  controller, and "switch the scale on to wake everything" can't be promised from an app. Each has a
  workaround (§3.4, §3.6), and the best one for the schedule is to write it into the power bot
  itself, which still needs testing.
- **Blocker: the licence.** The repository is AGPL-3.0 because it contains Open Brew by Weight code
  (`lib/EspressoScaleProtocol`, and the cut prediction and offset learning in `BbwAlgorithms.h`).
  GPL-family code from other copyright holders and the App Store's terms don't mix (§4.1). Either
  OBBW's authors agree to a licence exception, or those parts are rewritten clean before anything
  ships. Everything else in `src/core` is the owner's own and can be dual-licensed.

**The recommendation:** build the app as a second controller beside the board, not as its end.
Both use one C++ core, one data format and one community library, and an owner can move from one
to the other with a backup file. The board stays the choice for an appliance that runs unattended.
The app is the choice for an owner who already has a spare iPhone or iPad and doesn't want to build
hardware.

A cheaper option the owner should weigh first: **an iOS app as the board's screen** (a client of
`docs/API.md`, replacing the web page) needs none of §3 and none of the licence work. It doesn't
remove the board, which is the point of this brief, so it is only mentioned here.

## 2. What the board does today, and where each part goes

| Board part | Size today | In the app |
| --- | --- | --- |
| `src/core/*` brew logic, drivers, advisor, library, share, subscriptions, power, HA payloads | ~14,000 lines, host tested | **Kept as C++**, compiled into a Swift package (§5) |
| `lib/EspressoScaleProtocol` scale tables | 11 files, OBBW, AGPL | Kept only with OBBW's permission, else rewritten (§4.1) |
| `src/ble/Ble.cpp` NimBLE glue | 749 + 273 lines | **Rewritten** in Swift on CoreBluetooth; same seam (`ScaleLink`, `PusherLink` take bytes) |
| `src/main.cpp` orchestration: control task, link arbitration, standby, outbox, power schedule | 5,021 lines | **Moved into core first** (Phase 0, also helps the firmware), then driven from Swift |
| `src/ui/Ui.cpp` (LVGL) and `src/net/WebPage.h` + `Web.cpp` | ~2,100 + ~5,700 lines | **Rewritten** as SwiftUI screens. No web UI. |
| `src/storage` NVS + LittleFS binary files | ~1,500 lines | SQLite database; backup format kept byte for byte (§6.3) |
| `src/platform` TLS, GitHub, relay, HA, device key, OTA | ~2,000 lines | `URLSession`, CryptoKit, Keychain. OTA is gone (App Store updates). |
| `src/addons` grinder add-ons over the network | ~1,300 lines | Later phase; Swift drivers (§5.4) |
| `emu/` PC build with Lunar and Bot models | — | Device models reused as the app's **demo mode** and BLE simulator (§9) |

## 3. The iOS limits that decide it

### 3.1 Bluetooth with the app in the background or the phone locked

- With `bluetooth-central` in `UIBackgroundModes`, the system wakes a suspended app for
  CoreBluetooth events: a connection, a disconnection, a characteristic update. Each wake gives
  "around 10 seconds", and an app that does too much in the background "can be throttled back by the
  system or killed".
  [Apple, Core Bluetooth background processing](https://developer.apple.com/library/archive/documentation/NetworkingInternetWeb/Conceptual/CoreBluetooth_concepts/CoreBluetoothBackgroundProcessingForIOSApps/PerformingTasksWhileYourAppIsInTheBackground.html)
- So a running shot keeps going while the scale's ~10 Hz notifications keep waking the app, and the
  app can answer a notification with a write to the bot. Nothing documents that this is steady
  enough to stop a shot on weight, so the design can't count on it.
- **State restoration** (`CBCentralManagerOptionRestoreIdentifierKey`) relaunches an app the system
  killed, **not one the user force-quit** (iOS 26 relaxes this for accessories paired through
  AccessorySetupKit, per Apple staff on the forums). Turning Bluetooth off also stops relaunch.
  [TN3115](https://developer.apple.com/documentation/technotes/tn3115-bluetooth-state-restoration-app-relaunch-rules),
  [forum 844477](https://developer.apple.com/forums/thread/844477)
- Background scanning is coalesced and slower, and duplicates are dropped. Pairing and the
  bots-in-range list are foreground-only work anyway.
- Low Power Mode: Apple says nothing official. Forum reports say it makes apps get killed sooner.
  It has to be measured (§12).

**What the app does about it (the shot policy):**

1. **Start only in the foreground.** Start, the bloom, and the held pre-infusion all need the app
   active. `isIdleTimerDisabled` keeps the screen on while the app is open.
2. **If the app goes to the background mid-shot** (the user switches app, a call comes in, the side
   button is pressed), it keeps supervising the shot from each notification wake, because the stop
   is the same code path. It also posts a time-sensitive local notification: "Shot still running,
   open to watch it".
3. **If it can't stop** (the app is suspended or killed, or Bluetooth is off), the machine's own
   time-out ends the shot, exactly as when the board loses its pusher today
   (`docs/SAFETY.md`, `TimeLimit::MACHINE`). On relaunch the shot is recorded as
   `manual_stop_needed` or `machine_stopped`. It isn't taught to the offset.
4. **Machines where that is unsafe are refused.** With `hold_button` the arm stays down until the
   bot lets go. If the app dies, the arm stays down and the pump runs to the machine's limit. The
   firmware already handles "arm down, link lost" (README §10), but there the board doesn't vanish.
   The app should support `hold_button` only after Phase 0 measures the background behaviour, and
   should tell the owner the risk plainly.

This is no worse than the board on its two real failure paths (a flat bot, a lost link). It is
worse on one new path, the app being killed, so the safety statement needs a line for the app.

### 3.2 Four links at once

- Apple publishes no limit. Apple staff say one exists and varies by device. Older reports give 6 to
  15 links on 2016–2017 devices.
  [forum 766537](https://developer.apple.com/forums/thread/766537),
  [forum 78857](https://developer.apple.com/forums/thread/78857)
- Four links (scale plus three bots) is very likely fine on a current iPhone. The app keeps the
  firmware's rule anyway: the scale, the brew bot, and one more (the stop bot during a shot, the
  power bot otherwise; README §10). That logic is already in the code being moved into core, so
  this costs nothing.
- A pending `connect()` never times out ("attempts to connect to a peripheral don't time out").
  That is how the app takes up a bot as soon as it advertises.
  [CBCentralManager.connect](https://developer.apple.com/documentation/corebluetooth/cbcentralmanager/connect(_:options:)).
  Forum reports describe pending connects that got stuck after restoration on iOS 17 and later, so
  the app keeps its own time-outs, as `PusherLink` already does
  ([forum 742497](https://developer.apple.com/forums/thread/742497)).

### 3.3 Stop latency against the ESP32

- On iOS, the central sets the connection interval and an app can't request one. Developers report
  30 ms granted where 15 ms was asked, on iOS 16 and later
  ([forum 733968](https://developer.apple.com/forums/thread/733968)). NimBLE on the ESP32 can use 7.5 ms.
- From notification to a written press, the estimate is about 30–60 ms in the foreground against
  about 10–20 ms on the board. This is an estimate, not a measurement (§12). At 2–2.5 g/s that is
  0.05–0.1 g more coffee.
- The stop budget is dominated by the machine: flow carries on ~1.7 s after the press
  (`BrewController.cpp:29-33`). The SwitchBot's arm takes a few hundred ms. The learned
  `stopOffsetG` (default 1.5 g, clamp 10 g) absorbs any steady lag. **Latency is not a feasibility
  issue. Its jitter is what needs measuring**, because jitter can't be learned away.
- Use write-without-response where the protocol allows it, and keep the bot connected from the tare
  to ~10 s after the shot, as the firmware does. Connect-on-demand takes seconds and must never sit
  on the stop path.

### 3.4 Timed tasks: the 08:00 power-on

An app can't run code at a set time while it is suspended.

| Option | Reliable? | Notes |
| --- | --- | --- |
| `BGTaskScheduler` | No | Runs when the system chooses. `earliestBeginDate` is not a promise. |
| Local notification at 08:00 | Yes, but someone must tap it | A tap opens the app, which presses. Works, but isn't automatic. |
| Shortcuts automation "Time of Day", Ask Before Running off, calling an App Intent "Switch the machine on" | Mostly | iOS 26 App Intents can run in the background (`supportedModes`). Users report missed early-morning runs on an idle phone and prompts despite the setting ([forum 665845](https://developer.apple.com/forums/thread/665845), [supportedModes](https://developer.apple.com/documentation/appintents/appintent/supportedmodes)). The intent must connect to the bot and press within its time. |
| **Write the timers into the power bot** | Likely the best option | The SwitchBot Bot keeps its own timers and runs them with no phone. They are set over BLE in SwitchBot's public protocol ([OpenWonderLabs/SwitchBotAPI-BLE](https://github.com/OpenWonderLabs/SwitchBotAPI-BLE), timer commands; the firmware doesn't use them yet). The catch: the bot presses whatever the app believes, which breaks `PowerControl.h`'s "on while believed on presses nothing". The app would correct the belief on its next wake, from the timer log. Fingerbot and MicroBot have no such timers. |
| Home Assistant | Yes, for HA owners | HA's own SwitchBot integration presses the power bot on HA's schedule. The app then only needs to read back that it happened. |

**Plan:** the SwitchBot's own timers are the default, with a Shortcuts intent and a notification
as fallbacks. The Power screen says which one is in use and that the app can't see a hand press.
Phase 0 confirms the timer commands on the owner's bot.

"Switch the scale on to wake everything" carries over partly. A pending connect to the Lunar wakes
the suspended app when the scale appears, and the app can then connect the brew bot. That doesn't
work after a force-quit or a reboot.

### 3.5 Local network permission

Talking to Home Assistant or to a board on the LAN needs `NSLocalNetworkUsageDescription`. Bonjour
browsing for `_home-assistant._tcp` also needs that type listed in `NSBonjourServices`, or discovery
fails silently.
[TN3179](https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy),
[forum 741503](https://developer.apple.com/forums/thread/741503),
[HA discovery](https://developers.home-assistant.io/docs/api/instance_discovery)

### 3.6 Home Assistant: the app can't be a server

A suspended iOS app can't keep a listening HTTP or MQTT server, and HA doesn't treat phone apps as
devices. Of the five ways in `docs/HOME_ASSISTANT.md`:

| Way | In the app |
| --- | --- |
| 1. Read room climate from HA | **Kept**: `GET /api/states/<id>` with a long-lived token in the Keychain, read while the app is open and before each shot ([HA REST API](https://contributors.home-assistant.io/core/internals/rest-api/)) |
| 2. Climate pushed to the controller | **Dropped**: there is nothing to push to. Way 1 covers it. |
| 3. Webhook after each shot | **Kept**: the app posts the same JSON body to `/api/webhook/<id>` |
| 4. MQTT discovery and sensors | **Reduced**: state published, retained, while the app is open ([CocoaMQTT](https://github.com/emqx/CocoaMQTT), MIT). No Start button from HA; it would need the app awake. |
| 5. HA polling the JSON API | **Dropped**: replaced by retained MQTT topics and the webhook |

The custom HA integration (`integrations/`) would need a "push" mode that reads the webhook and
MQTT, not the controller's API. Matter or HomeKit as a bridge isn't open to third-party apps
(Matter.framework is for commissioning and control).

### 3.7 The screen, Live Activities, widgets

- `UIApplication.isIdleTimerDisabled` keeps the screen on while the app is in front. For a device
  that stays at the machine, Auto-Lock "Never" or Guided Access makes it a kiosk.
- **Live Activity** for a running shot (weight, target, time) on the Lock Screen and Dynamic Island:
  it runs up to 8 h, with 4 KB of state, which is ample. Updates come from the app while it is awake
  in the background, with no push server needed. Updates aren't promised while suspended, so it is a
  view, not a safety feature. ([forum 797676](https://developer.apple.com/forums/thread/797676))
- **Widgets:** the last shot, today's grind advice, beans running low, upkeep due. WidgetKit has
  refresh budgets, so these show static snapshots written when a shot ends.
- **StandBy** (iPhone on its side, charging) is a natural dock view for the mounted phone.

## 4. App Store review

### 4.1 Licence (the blocker)

- The repository is AGPL-3.0-or-later. `LICENSE` names Felipe Urzúa and contributors.
  `lib/EspressoScaleProtocol` is OBBW code vendored unmodified, and `BbwAlgorithms.h` adapts OBBW's
  cut prediction and offset learning (README §12, `research/license-and-risk-register.md`).
- The FSF's position is that App Store usage terms conflict with the GPL. VLC was pulled in 2011 on
  one copyright holder's complaint
  ([vlc-devel, 2010](https://mailman.videolan.org/pipermail/vlc-devel/2010-November/077027.html)).
  GPL apps that stay listed, Beanconqueror among them, hold all the copyrights themselves through a
  CLA, so no outside holder can object
  ([Beanconqueror](https://github.com/graphefruit/Beanconqueror)).
- The owner can dual-license their own code (most of `src/core`) for the App Store build. OBBW's
  code can't be relicensed without its authors.

**Options, in order:**

1. Ask OBBW's authors for an App Store exception, an "additional permission" under AGPL §7.
2. Replace the OBBW parts.
   - The scale tables are protocol facts. They can be rewritten from the protocol descriptions in
     `docs/RESEARCH.md` and the devices themselves, not from OBBW's files.
   - The Lunar path already has its own `AcaiaStream`. Its MIT lineage from AcaiaArduinoBLE must be
     confirmed.
   - The cut prediction is a least-squares fit, and offset learning is one line. Both are rewritten
     clean with a note on provenance.
3. Ship only the owner's code: the Lunar and the SwitchBot first, other scales added as they are
   rewritten.

A lawyer should confirm this before Phase 1 ends (⚖, as in the risk register). The tests' golden
records make a clean rewrite safe: the new code must give identical readings.

### 4.2 Third-party devices, names, hardware the reviewer lacks

- **MFi isn't needed** for BLE accessories driven through CoreBluetooth
  ([MFi FAQ](https://mfi.apple.com/faqs)).
- **Brands:** 4.1(c) and 5.2.1 forbid another company's name or brand in the app's name or icon
  without permission. Name it "Brew Controller", not "…for Acaia". "Works with Acaia Lunar,
  SwitchBot Bot" belongs in the description and on the devices screen.
  ([Guidelines](https://developer.apple.com/app-store/review/guidelines/))
- **Precedent:** Beanconqueror talks to Acaia scales on the App Store. Acaia also offers an official
  iOS SDK by request ([Acaia SDK](https://acaia.co/blogs/news/new-ios-sdk-api-release)), worth
  asking for as a fallback if a firmware update breaks the protocol.
- **SwitchBot's BLE protocol is published** by its maker
  ([SwitchBotAPI-BLE](https://github.com/OpenWonderLabs/SwitchBotAPI-BLE)). Tuya's needs keys from
  the owner's Tuya cloud project, and its terms must be checked. MicroBot's maker appears to have
  stopped trading ([PyPush](https://github.com/VRGhost/PyPush)), so it is low priority.
- **2.1 hardware the reviewer lacks:** a demo mode with a simulated Lunar and Bot (§9), a short
  video of a real shot, and review notes that explain both.
- **2.5.4 background modes:** `bluetooth-central` is justified by keeping a running shot supervised.
  The notes say so.
- **1.4 physical harm:** the app presses a button on a mains appliance. The safety statement and
  "be present while a shot runs" appear at first run, which matches `docs/SAFETY.md`.

### 4.3 User content, network traffic, sign-in, privacy

- **1.2 user-generated content** requires filtering, reporting, and blocking. The library is already
  moderated before publishing: records land in an inbox and a reviewer merges them (COMMUNITY.md §7).
  The app adds:
  - "Report this coffee/profile" (a relay endpoint or a pre-filled GitHub issue);
  - a local "hide this contributor";
  - a contact address.
- **4.8 Sign in with Apple** only applies if the app offers a third-party login for its account.
  The relay is the default, and owners need no account, so there is no login. "Use your own library
  repository" takes a pasted fine-grained token, as the board does.
- **Privacy manifest** (`PrivacyInfo.xcprivacy`) with required-reason APIs (UserDefaults, file
  timestamps). The App Privacy label covers diagnostics and optional shot sharing, both opt-in and
  not linked to identity. There is no tracking, so no ATT.
- **Export compliance:** HTTPS and Apple's CryptoKit signatures are exempt. The Tuya driver's
  AES-128 authenticates to a device, which looks like the authentication exemption but is
  unverified. Answer the App Store Connect questions with that in mind, and confirm before release.

## 5. Reuse or rewrite: keep the C++ core

### 5.1 Why the core can be kept

- No core file includes an Arduino, ESP-IDF, NimBLE, LVGL or FreeRTOS header. Time comes in as
  `nowMs`/`atMs`. Callbacks are plain function pointers.
- It has no exceptions, no RTTI, and no `std::string`, `vector` or `function` (repo map,
  2026-11-09).
- `test/host/run.sh` already builds it with `g++ -std=c++17` and no stubs. The emulator builds the
  whole firmware on a PC.
- Swift's C++ interop supports this subset well: C++14+, plain structs and arrays, virtual member
  functions on reference types, and callbacks
  ([Swift C++ interop status](https://swift.org/documentation/cxx-interop/status/)).
  Exceptions aren't caught across the boundary, but the core throws none.

### 5.2 The trade

| | Keep the C++ core (recommended) | Full Swift rewrite |
| --- | --- | --- |
| Source of truth | One, shared with the firmware | Two, which drift apart |
| 5,700+ host checks | Run unchanged on macOS CI, and again through the bridge | Rewritten, or only replayed as golden files |
| Up-front cost | A bridge (~1–2 kloc) and a Phase 0 move of orchestration out of `main.cpp` | ~14 kloc of core logic to port, including the advisor (1,500 lines) and the Tuya crypto |
| Running cost | Interop sharp edges: it is viral to dependent modules, class templates need aliases, and previews may be slow | None from interop; full cost of every core change made twice |
| Licence | Owner's own parts dual-licensed; OBBW parts as §4.1 | Same work, since ported OBBW logic is still derived |

A rewrite only wins if the firmware is going to be abandoned, and this plan keeps the board.

### 5.3 How the bridge is built

- **`BrewCore`**: a Swift package target holding `src/core` and the cleared scale protocol code as
  C++, with a module map. `test/host/test_main.cpp` builds as its own C++ test executable on macOS CI.
- **`BrewCoreBridge`**: a small C++ layer. It is the only place C++ interop is switched on, so the
  rest of the app and the SwiftUI previews don't need the flag. It holds:
  - `AppHost`: owns `BrewController`, `ScaleLink`, the `PusherLink`s, `PowerControl` and `Library`.
    It is the orchestration moved out of `main.cpp` in Phase 0.
  - Concrete `IScale`, `IActuator` and `PusherWriter`. Swift can't subclass C++ virtual classes, so
    these queue outgoing byte frames into a ring buffer. Swift drains it after each call and writes
    them with CoreBluetooth.
  - Plain value structs out: the state, the live weight, the finished `ShotRecord` and curve,
    advice, and events.
- **Threading:** the core is single-threaded, as on the board's control task. All calls go through
  one serial dispatch queue, which is also the `CBCentralManager` queue.
  - `onSample` runs on every notification.
  - `tick(now)` runs from a 10 ms `DispatchSourceTimer` in the foreground, and also after every
    notification, so a background wake still advances the state machine.
  - Time comes from a monotonic clock (`clock_gettime(CLOCK_UPTIME_RAW)` in ms). The epoch comes
    from the system clock, so the board's "clock not set" cases go away.
- **Limits:** `Types.h` fixes `MAX_COFFEES = 24`, `MAX_RECIPES = 16` and others for the board's RAM.
  The app keeps them in Phase 1, since they shape `library.bin` and the backup. Later they become
  build-time constants, with the backup reader taking either size.

### 5.4 What is rewritten in Swift

- The CoreBluetooth layer. It replaces `Ble.cpp`'s ~750 lines, using the same driver selection by
  characteristics.
- Networking: the relay, GitHub, Home Assistant and MQTT, which replace `platform/*Link.cpp`.
- Persistence, and every screen.
- Grinder add-ons (`docs/ADDONS.md`) are compiled into the image on the board. In the app they would
  be Swift modules behind the same `Addon.h` contract, and come after v1.

## 6. Architecture

### 6.1 Modules

```
BrewControllerApp (SwiftUI, iOS 18+, iPhone + iPad)
├─ Features/       Home(brew) · Result · Recipes · Coffees&Bags · Grind · History · Library
│                  · Subscriptions · Maintenance · Power · Devices · Settings · Diagnostics
├─ Session/        ShotSession (observable): drives BrewCoreBridge on the core queue, publishes state
├─ BLE/            CentralController (one CBCentralManager, restore id), ScaleTransport,
│                  PusherTransport, AccessorySetupKit pairing, link budget (3 links)
├─ BrewCoreBridge/ C++ host + adapters (only module with C++ interop)
├─ BrewCore/       src/core (shared with firmware, via a git subtree or submodule path)
├─ Store/          SQLite (GRDB) schema, migrations, .bbwbak import/export, outbox
├─ Net/            RelayClient, GitHubClient, HomeAssistantClient, MQTTPublisher
├─ Identity/       P-256 key in the Secure Enclave, record signing
├─ Intents/        App Intents: start shot (foreground), power on/off, last shot; Shortcuts
├─ Widgets/        WidgetKit + ActivityKit Live Activity for the running shot
└─ Demo/           Simulated Lunar and Bot (ported from emu/src/emu_devices.cpp)
```

### 6.2 BLE layer

- **One `CBCentralManager`** with a restore identifier, on the core queue.
- **AccessorySetupKit (iOS 18+)** for pairing. The system's own picker shows the Lunar and the bots
  without the app holding broad Bluetooth permission. On iOS 26 it also restores the app after a
  force-quit (§3.1). Bots that don't advertise a fixed service (some Tuya) need the classic scan path
  as a fallback.
- **Transports** pass raw bytes both ways and nothing more: `ScaleLink::onNotify`,
  `service(now, …, write)`, and `PusherLink` through the bridge. Driver choice, heartbeats, the
  silence watchdog and the press sequence all stay in core.
- **Link budget:** the firmware's three-link rule, plus a measured cap per device.

### 6.3 Persistence: SQLite (GRDB), not SwiftData

- The data is relational and queried hard: history by coffee and grinder, the advisor's windows,
  similar coffees. It needs migrations that mirror the firmware's format versions and an exact
  binary export.
- SwiftData's CloudKit sync forbids unique constraints and needs every field optional, which fits
  this data badly ([rules](https://fatbobman.com/en/snippet/rules-for-adapting-data-models-to-cloudkit/),
  [forum 770413](https://developer.apple.com/forums/thread/770413)).
- GRDB is mature. Point-Free's SQLiteData adds CloudKit sync on top of it if iCloud is wanted later
  ([SQLiteData 1.0](https://www.pointfree.co/blog/posts/184-sqlitedata-1-0-an-alternative-to-swiftdata-with-cloudkit-sync-and-sharing)).
- **Tables:** `shots` (`ShotRecord` columns, plus the raw 232-byte record kept as a blob for exact
  export), `curves` (the 8-byte `CurvePoint` arrays as blobs), `contexts`, `ratings`, `library`
  (coffees, recipes, grinders, bags), `subscriptions`, `power_events`, `maintenance`, `outbox`, and
  `settings` (key → the same stored steps as `docs/API.md`).
- **Secrets** (HA token, Tuya keys, MicroBot token, own-repo token) go in the Keychain, never in
  the database. A backup includes them only when ticked, as on the board.
- **Backup:** `BackupFormat.h` is a template over a source or sink and is already portable. The app
  writes and reads the same `.bbwbak` (`BBWB`, format 1, `BEND` + CRC-32), mapping NVS entries and
  LittleFS files to tables. This is the owner's way across (§8).

### 6.4 Community library and relay

- The same relay protocol: `POST /library/put` and `GET /library/get`, and `/report` for
  diagnostics. The same outbox rules as the board, minus "not while BLE timing matters". The app
  doesn't send during a shot.
- **Identity:** the board signs with ES256 and sends a 64-byte `r|s` signature and a 65-byte
  uncompressed public key (`Share.h:30-45`). These are exactly CryptoKit's
  `P256.Signing.ECDSASignature.rawRepresentation` and `PublicKey.x963Representation`. The private
  key is made in the Secure Enclave, so it can't be extracted. The canonical bytes and `sealRecord`
  stay in core, so the relay sees no difference.
- **Caching:** the `index/` cache moves from `/lib/` on LittleFS into the app's Caches directory,
  fetched with `URLSession` and ETags.

### 6.5 iCloud (optional, later)

- Phase 4 at the earliest: shots and the library synced through SQLiteData/CloudKit between an
  owner's iPhone and iPad.
- **Only one device may be the controller at a time.** The SwitchBot takes one central. The power
  belief and the offset learning must not run on two devices at once.

### 6.6 What the app adds over the board

- History and grind charts at full size, with no 24-coffee cap once the limits are lifted.
- Scanning a bag's label with the camera into a coffee. VisionKit could do this; it is a candidate,
  not a commitment.
- Share sheets and Files export (CSV, `.bbwbak`, Beanconqueror and Visualizer handoff, which already
  exist as URLs).
- Notifications: shot finished, beans running low, upkeep due, a subscription arriving.

## 7. Hardware and use at the machine

- **Device:** an iPhone or iPad on a stand within 1–2 m of the scale and the bots, plugged in.
  - An old phone works as a dedicated kiosk if it runs iOS 18; AccessorySetupKit and the Live
    Activity behaviour need it.
  - iOS 26 needs an iPhone 11 or later.
- **Power and battery:** on iPhone 15 and later, set Charge Limit to 80 %. Optimized Charging does
  the same more loosely on older models ([Apple 108055](https://support.apple.com/en-us/108055)).
  Keep it away from steam and the cup tray's heat.
- **Screen:** while the app is open, the idle timer is off. For a kiosk, use Auto-Lock Never and
  Guided Access, which locks the device to the app. StandBy makes a good dock view.

**Reliability against the board, said plainly in the app's help:**

| | Board | App |
| --- | --- | --- |
| Always on, starts by itself after a power cut | Yes | No: someone opens the app |
| Unattended 08:00 power-on | Yes | Through the bot's own timers or Shortcuts (§3.4) |
| Can be killed mid-shot | No | Yes (force-quit, memory, an iOS update overnight); fails safe to the machine's time-out |
| Stop timing | 7.5 ms links, dedicated task | ~30 ms links, shared phone; to be measured |
| Other software interfering | None | Notifications, calls, Control Center Bluetooth off |
| Screens, history, library | Small screen plus web page | Much better |
| Cost to the owner | Board, case, build | A device they may already have |

## 8. Living alongside the board

- **One library, one format.** The relay, the records, the schema validation, and signatures that
  verify the same way. A record's `"device"` field can carry a `"platform":"ios"` hint for
  diagnostics. It doesn't change the schema.
- **One core.** `src/core` stays in this repository. The app takes it by path, as a submodule or
  subtree, and a change made for one is built and tested for both on CI.
- **Moving board → app.** Download the backup from the board (`/api/backup`, secrets ticked or
  not), open it in the app (Files / AirDrop), and the app imports shots, curves, contexts, ratings,
  the library, the power belief and the settings. The device key isn't carried across, since the
  board's private key never leaves it. The app makes a new key, and the library sees a new
  contributor. A later option: the board signs a "key handover" record so that history follows.
- **Moving app → board.** The app exports a `.bbwbak` format 1 that the board's `/api/restore`
  accepts. The record limits stay at the board's sizes until the reader handles both (§5.3).
- **Both owned at once.** Only one controls the devices at a time. The app's Devices screen warns
  when a bot or the scale is already connected elsewhere, which shows as "not advertising".

## 9. Test strategy

- **Core:** `test/host/run.sh` runs unchanged on macOS CI (clang, libc++), as well as on Windows and
  Linux. Clang finding what g++ didn't is a benefit, not a risk.
- **Bridge:** an XCTest/Swift Testing suite replays the host tests' golden runs through
  `BrewCoreBridge`. These are the 4,000 Lunar notifications, the 40 SwitchBot press runs, and
  `testShotRecordGolden`. The bytes out and the records must be identical.
- **Simulated devices:** `emu/src/emu_devices.cpp`'s Lunar and Bot models are plain C++, written
  from the protocol research rather than the firmware's own code. They become a `SimulatedPeripheral`
  behind the same transport protocol, which serves three purposes:
  - unit tests;
  - SwiftUI previews;
  - **demo mode for App Review** and for owners trying the app before they buy a bot.
- **Timing faults:** a fake clock and fault injection (lost answers, a dropped link at the stop, an
  app suspended for N seconds mid-shot) through the simulator, covering what `SAFETY.md` lists, plus
  "suspended" and "killed".
- **Real hardware, by hand, per release:** a checklist modelled on README §7.
  - Lock the phone mid-shot; background it; force-quit it.
  - Low Power Mode; Bluetooth off from Control Center.
  - Measure the stop timing with 4 links up.
  - The owner runs these, never against the deployed board, and on a test bag, not their
    `emu/data`.
- **UI:** XCUITest on the demo mode for the main flows, plus snapshot tests of the shot and result
  screens in light, dark and Dynamic Type.

## 10. Plan

The effort figures assume one developer working with Claude, part time. They are a guide, not a
quote.

### Phase 0: settle what decides it (3–4 weeks)

- **Licence:** write to OBBW's authors (§4.1), and list exactly which lines derive from OBBW.
- **A bare-bones device spike.** This is the one prototype this plan asks for, to be approved first.
  It connects a Lunar and a SwitchBot and measures:
  - notification-to-press latency and its jitter in the foreground;
  - behaviour when locked or backgrounded mid-shot, and when the app is force-quit;
  - four links at once;
  - Low Power Mode;
  - the SwitchBot timer commands.
- **Firmware-side groundwork, useful on the board too:**
  - move the orchestration from `main.cpp` (link arbitration, standby, outbox, the schedule
    trigger, the shot-end bookkeeping) into a portable `core/AppHost.h`, with host tests;
  - make `ShotFormat.h`'s JSON independent of ArduinoJson, or give it a second writer.
- **Exit:** a go/no-go on the numbers, and a licence route chosen.

### Phase 1: MVP, brew by weight with a Lunar and a SwitchBot (8–10 weeks)

- Xcode project, `BrewCore` package, bridge, and CI running the host tests and the bridge tests.
- BLE: AccessorySetupKit pairing, the Lunar, the SwitchBot brew bot, restoration, the link budget.
- Screens:
  - Home/brew: the ring, the weight, the time, and the states from README §1;
  - Result;
  - Recipes (Target/Ratio, pre-infusion, bloom);
  - History, with the curve;
  - Devices;
  - Settings, with the steps from `docs/API.md`.
- The shot policy (§3.1), the safety notice at first run, the idle timer, and the Live Activity.
- Backup import from the board and export back.
- Demo mode.
- **Milestone:** TestFlight build. The owner pulls a week of real shots, and the stop error is
  within the board's on the same machine.

### Phase 2: the barista features (6–8 weeks)

- Coffees and bags, grinders, the grind advisor and its screens, ratings and taste.
- Similar coffees and recommendations, maintenance, shot context (room climate from HA).
- Machine profiles (all five), the stop bot, and the power bot with timers and the Shortcuts intent.
- Other scales and pushers as the licence route allows: Tuya with keys, MicroBot last.
- **Milestone:** everything in README §1–10 that doesn't need a network.

### Phase 3: community and connections (5–6 weeks)

- Secure Enclave identity, the relay client, the outbox, and the library: browse, send, status,
  review state, reports and hiding.
- The own-repository option, subscriptions and the shared catalog, and the coffee directory.
- Home Assistant: climate, the webhook, and MQTT while open. Update the HA integration for push.
- Diagnostics reports through the relay, as `docs/TELEMETRY.md` with `"platform":"ios"`.
- **Milestone:** a board and the app contribute to the same library side by side.

### Phase 4: polish and the App Store (4–6 weeks)

- **Polish:** iPad layouts, widgets, StandBy, App Intents/Shortcuts, accessibility (VoiceOver on
  the shot screen, Dynamic Type), and localisation hooks.
- **Store requirements:** privacy manifest and label, export answers, review notes, the demo video,
  the App Store page, and the safety and support pages.
- **Optional:** iCloud sync between one owner's devices.
- **Milestone:** a public TestFlight, then App Store submission.

**Total:** about 6–8 months part time from the Phase 0 go decision.

## 11. Risks

| Risk | Likelihood | Impact | What is done |
| --- | --- | --- | --- |
| OBBW authors decline; clean rewrite of scale tables needed | M | M | Lunar + SwitchBot first (owner's code); tables rewritten one by one against golden tests |
| Backgrounded shot not supervised reliably | H | M | Foreground-only start; machine time-out fallback; `hold_button` gated; documented |
| Stop jitter on iOS worse than measured on the board | M | M | Phase 0 measures; write-without-response; offset learning; go/no-go gate |
| SwitchBot timers can't be set as hoped / break the power belief | M | L | Shortcuts and notification fallbacks; HA |
| App Review asks for hardware or rejects background mode | M | M | Demo mode, video, precise notes; background mode justified by shot supervision |
| Acaia firmware change breaks the protocol | L | H | Same risk as the board; Acaia SDK as fallback; driver tests |
| C++ interop friction (build times, previews, Xcode updates) | M | L | Interop confined to one bridge module; core stays plain C++ |
| Core drift between firmware and app | L | M | One source tree, both built on CI |
| Two controllers on one set of devices | L | M | One central per bot; Devices screen warns |
| Owner leaves the app in a pocket, not at the machine | M | L | Range warnings; "be present" notice |

## 12. Still to be measured or confirmed (Phase 0)

1. Notification-to-press latency and jitter, foreground, 4 links, on the owner's iPhone.
2. What a locked or backgrounded app does during a 30 s shot: does every notification wake it, and
   does the stop go out on time?
3. Low Power Mode, and iOS 26 background behaviour with AccessorySetupKit, after a force-quit.
4. SwitchBot Bot timer commands: setting, reading back, and the timer log.
5. Shortcuts "Time of Day" with an iOS 26 background App Intent that connects and presses, on a
   phone left idle overnight.
6. Export compliance for the Tuya AES, and the exact 4.8 and 1.2 wording on the live guidelines page.
7. That `ShotRecord` and the curve layouts match bit for bit under Apple clang on arm64. The
   `static_assert`s should catch any difference, and the golden test will show it.

## Sources

Repository: README §1, §8–12; `docs/SAFETY.md`, `docs/HOME_ASSISTANT.md`, `docs/COMMUNITY.md` §5,
§7; `docs/TELEMETRY.md`; `docs/API.md`; `src/core/Share.h`, `Types.h`, `BrewController.*`,
`BackupFormat.h`; `test/host/run.sh`; `emu/`; `LICENSE`; `research/license-and-risk-register.md`.

External sources are linked inline. Where a page couldn't be read in full and the claim rests on a
forum or secondary source, the text says so ("forum", "per Apple staff on the forums",
"unverified").
