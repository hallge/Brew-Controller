# Brew Controller for iOS: Phase 0 spike

The spike from `research/ios-app-feasibility.md` (§5.3 bridge, §9 tests, Phase 0). It proves one
thing: the firmware's own `src/core` state machine can run unchanged inside an iPhone app, behind a
small Swift/C++ interop bridge, and bring a **simulated** shot to `SHOT_COMPLETE`. It uses simulated
devices only, because the real Lunar and SwitchBot Bot stay with the deployed board. Nothing in
`src/`, `lib/`, `test/` or `emu/` was changed.

This PC is Windows and cannot build iOS. Builds run on Codemagic and reach the iPhone through
TestFlight, the same way as the Sideline project.

## What is here

```
ios/
  project.yml                XcodeGen project: iOS 18, com.hallge.brewcontroller
  Config/Base.xcconfig       team + build number (Secrets.xcconfig is written by CI, not committed)
  BrewCoreBridge/            the C++ side of the bridge
    CoreSession.h/.cpp         one class Swift sees: owns a BrewController, simulator or real transport
    BridgeTypes.h              plain structs across the boundary, and the command queue
  Sim/MachineSim.h           the host tests' simulated Breville + Lunar + Bot, copied (attributed)
  BrewControllerApp/         SwiftUI app, ShotSession (core queue + timer), CoreBluetooth transport
  BrewCoreTests/             a full shot through the bridge, the command queue
  BrewControllerUITests/     a demo shot with screenshots
  scripts/                   asc-signing.py, pick-iphone.sh, write-secrets.sh (adapted from Sideline)
codemagic.yaml (repo root)   "tests" on pushes, "release" by hand
```

The firmware core is compiled **by path, never copied**: `../src/core/BrewController.cpp` and
`../lib/EspressoScaleProtocol/src/*.cpp`, with header search paths `../src` and
`../lib/EspressoScaleProtocol/src`, C++17.

### How the bridge works

- `CoreSession` is a C++ class imported into Swift as a **reference type**
  (`swift_attr("import_reference")` with retain/release). Its header holds no core types (pimpl), so
  Swift imports only plain structs: `LiveView`, `ShotSummary`, `ProbeEntry`, `BridgeCommand`,
  `CommandQueue`.
- Swift cannot subclass the core's virtual classes, so `IScale`, `IActuator` and `PusherWriter` are
  implemented in `CoreSession.cpp`. In **simulated** mode they forward to `brewsim::Sim`. In **real**
  mode they encode with the core's `ScaleLink`/`PusherLink` and queue the bytes as `BridgeCommand`s,
  which Swift drains after every call and writes with CoreBluetooth.
- The simulated Lunar sends real Acaia frames (`wireFrame()`) through the core's `ScaleLink`, so the
  decode path is the one the board uses.
- One serial queue runs everything: the 10 ms `DispatchSourceTimer`, the UI's requests and the
  `CBCentralManager`. Time is `clock_gettime(CLOCK_UPTIME_RAW)` in ms. The core is ticked from the
  timer and again after every scale notification.
- **Suspension is imitated, not just ticked slowly**: "Pause ticks" lets the simulated machine run on
  while the core gets no ticks and no readings, then ticks it with the gap. The Probe log shows the
  gap.

### The screens

One main screen: the ring (weight over target), weight, shot time, state, Start/Stop (Done after a
shot). Sheets: **Recipe** (target and dose), **Faults** (lost Bot acknowledgement, missed stop,
scale goes silent, pause ticks for N s), **Probe log** (timestamp of every tick, reading and
command, with the longest tick gap), **Devices** (the "Real devices" switch: off by default,
labelled untested). `isIdleTimerDisabled` is on while the app is open. No Live Activity yet.

## What was checked on this PC

`CoreSession.cpp` and `MachineSim.h` were compiled with `zig c++` (clang, C++17, `-Wall -Wextra`) and
driven from a small C++ program. A simulated shot reached `SHOT_COMPLETE` ending on weight (35.4 g
for a 36 g target, 27.7 s), a missed stop and a 5 s pause mid-shot both ended in a record, the
pause showed as a 5000 ms tick gap in the probe log, and the command queue refused the 65th entry
and counted it. No Swift, no Xcode, no iOS SDK was involved.

## One-time setup (you)

You do this once. The Sideline project already has a Codemagic account, an App Store Connect API key
and a team, so most of it is reuse.

1. **Add the repo in Codemagic.** codemagic.io, Applications, *Add application*, GitHub, pick
   `hallge/Brew-Controller`, project type *Other* (not a Flutter or native preset), and let it use the
   `codemagic.yaml` at the repo root. The `tests` workflow runs on pushes; `release` is manual.
2. **Register the bundle ID.** developer.apple.com, Certificates, Identifiers & Profiles,
   Identifiers, `+`, App IDs, App, description "Brew Controller", Explicit, Bundle ID
   `com.hallge.brewcontroller`. Tick no capabilities (Bluetooth needs none).
3. **Create the App Store Connect app record.** appstoreconnect.apple.com, Apps, `+`, New App:
   platform iOS, a name that is free in the store (for example "Brew Controller BBW"), a language,
   Bundle ID `com.hallge.brewcontroller`, any SKU (for example `bbw-controller`), User Access Full.
   The release workflow fails with "no App Store Connect app uses com.hallge.brewcontroller" until
   this exists.
4. **Create the `bbw_release` environment group.** In Codemagic: the app (or the team's *Global
   variables and secrets*), *Environment variables*, group name `bbw_release`, with these four, each
   marked **Secret**:
   - `ASC_KEY_ID` and `ASC_ISSUER_ID`: the same values as Sideline's API key (App Store Connect, Users
     and Access, Integrations, App Store Connect API shows both).
   - `ASC_KEY_P8_B64`: that key's `AuthKey_XXXX.p8` file, base64-encoded, from wherever you keep your
     own copy. In PowerShell: `[Convert]::ToBase64String([IO.File]::ReadAllBytes("C:\path\AuthKey_XXXX.p8"))`.
     Nothing here reads or copies Sideline's key files.
   - `TEAM_ID`: your Apple Developer Team ID.
5. **Start the tests.** Push this branch (`git push -u origin <branch>`). The `tests` workflow starts
   by itself because `ios/**` changed. If it does not, Codemagic, *Start new build*, branch, workflow
   "iOS tests and screenshots". Its artifacts hold the logs and the screenshots of the demo shot.
6. **Start the release by hand.** Codemagic, *Start new build*, the same branch, workflow
   "TestFlight release". It signs with a throw-away certificate, uploads, waits for Apple to process
   the build, and revokes the certificate.
7. **Install.** App Store Connect, the app, TestFlight: when the build shows as processed, add
   yourself as an Internal Tester, then install from the TestFlight app on the iPhone (iOS 18 or
   later). Open it: the screen shows READY after a second; tap Start.

Do not run Real devices against the Lunar and Bot while the deployed board owns them.

## Unverified until the first Codemagic run

Nothing in Swift, XcodeGen, Xcode or Codemagic has been run. Likeliest to need a fix, roughly in order:

1. **Swift/C++ interop import.** The attributes `import_reference` / `retain:` / `release:` /
   `returns_retained` and the `_Nonnull` pointer qualifiers; calling imported methods **without
   argument labels** (`core.tick(now)`, `core.setRecipe(a, b)`); `String(cString:)` on the
   `const char *` members; mutating C++ methods on an imported struct (`var q = CommandQueue();
   q.push(c)`); structs with fixed arrays (imported as tuples, a 64-element one in `CommandQueue`).
2. **`constexpr` constants** are deliberately *not* used from Swift: `BridgeConstants.swift` mirrors
   them by hand (`Cmd`, `Fault`, `Probe`). Nothing checks that the two agree.
3. **Interop scope.** `SWIFT_OBJC_INTEROP_MODE: objcxx` is per target, so the whole app target and
   the unit tests have it on (only the UI tests do not). Confining it to the bridge, as §5.3 wants,
   needs a framework target; that is Phase 1.
4. **Unit tests hosted in the app.** `BrewCoreTests` has its own bridging header and relies on
   `TEST_HOST`/`BUNDLE_LOADER` to find the C++ symbols in the app binary (the C++ is compiled once, in
   the app). If the linker cannot see them, compile the bridge files into the test target too.
5. **XcodeGen specifics.** External source paths with `group:`, `includes: ["*.cpp"]` on
   `../lib/EspressoScaleProtocol/src`, `buildPhase: none` for `Sim/`, and `info:` together with
   `GENERATE_INFOPLIST_FILE` (the same combination as Sideline).
6. **The test numbers.** The Swift tests' expectations (final weight within 1.5 g of 36 g, shot time
   20 to 35 s, end reason `weight` or `weight_predicted`) come from the same C++ run on the PC, not
   from a Swift run. The missed-stop test only requires the shot to end (done or a fault state).
7. **The UI test.** Accessibility queries (identifiers on the ring's `Text`s, `resultCard` as a
   combined element) and its length: a real-time shot takes about 50 s plus the fixed waits.
8. **Host tests under clang/libc++** on macOS (`CXX=clang++ bash test/host/run.sh`); they have run
   with g++ only.
9. **Codemagic.** `when: changeset` at the workflow level (and how it treats the first push of a new
   branch); that `-retry-tests-on-failure` re-runs the 50 s UI test acceptably.
10. **Signing and upload.** `asc-signing.py` was adapted by removing the HealthKit and watch parts,
    and has never run for this app: bundle ID registration, the ASC app record, the profile name
    passed as `APP_PROFILE`, the generated 1024 px app icon (a plain ring, replace it later), and
    `MIN_BUILD=1` for the first build number.
11. **CoreBluetooth transport: all of it.** Scale name prefixes, the Lunar's characteristic pair,
    write type, the Bot connected only while the core wants it, no reconnect back-off, no state
    restoration, no AccessorySetupKit. The background mode is declared, but nothing has shown that a
    background wake keeps the shot alive (that is what the real-hardware checklist in §9 is for).
12. **Idle timer and scene phase.** `isIdleTimerDisabled` is set on appear and on scene changes.
13. **Clock.** The core's `uint32_t` millisecond clock wraps after 49.7 days of uptime (the board has
    the same limit); not looked at here.

## Not in the spike

Persistence, the library and history, recipes beyond target and dose, the grind advisor, Live
Activity and widgets, App Intents, networking, AccessorySetupKit, the `main.cpp` orchestration move
(`AppHost`), and the golden-run replay of the host tests through the bridge.
