# Brew Controller for iOS: the board's firmware, in the phone

The iPhone app runs the Brew Controller's own firmware in-process: `src/main.cpp`, `Web.cpp`,
`Ble.cpp`, `Store.cpp` and `core/`, compiled with `-DBBW_EMULATOR -DBBW_HOST_IOS -DBBW_CLEAN` against
the PC emulator's portable stand-ins (`emu/src`). Native SwiftUI screens sit on the plain-C API in
`Host/BrewHost.h`. Design notes: `ios-parity/design.md` (sections 1, 5, 6). This PC is Windows and
cannot build iOS: builds run on Codemagic and reach the phone through TestFlight.

## What is built

```
ios/
  project.yml                XcodeGen project: iOS 18, com.hallge.brewcontroller
  Config/Base.xcconfig       team + build number (Secrets.xcconfig is written by CI, not committed)
  Host/                      BrewHost.h/.cpp (C API), ios_runtime.mm/.h (clock, RTC, QoS, data folder),
                             ble_corebluetooth.mm/.h (radio), host_stubs.cpp, ui_stub.cpp
  Vendor/ArduinoJson/        ArduinoJson 7.4.3 (MIT), copied from the PlatformIO libdeps
  Generated/AddonList.h      the (empty) add-on list
  BrewControllerApp/         BrewControllerApp.swift: app entry and a placeholder ContentView;
                             BrewHost-Bridging-Header.h: imports BrewHost.h, ble_corebluetooth.h, ios_runtime.h
  BrewCoreTests/             XCTest: a demo shot through the C API (port of test/hostapp/hostapp_main.cpp)
  BrewControllerUITests/     launch, tap Start, see the state change; screenshots
  scripts/                   asc-signing.py, pick-iphone.sh, write-secrets.sh (adapted from Sideline)
codemagic.yaml (repo root)   "tests" on pushes to non-main branches, "release" by hand
```

The app target compiles exactly the C++ sources of `test/hostapp/run.sh` (minus its runner), with the
same defines and the same header search order, plus `ble_corebluetooth.mm` and `ios_runtime.mm`
(and `ios_net.mm` / `ios_devicekey.mm` when the networking work lands). `USE_HEADERMAP=NO` and
`gnu++17`. Keep `project.yml` and `run.sh` in step when a source is added. All Swift under
`BrewControllerApp/` is compiled, so the `Core/` folder of the Swift core layer is picked up.

**ios_runtime.mm** installs the host hooks (`emu::g_hooks`): the clock from
`clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW)`; the RTC region (`__DATA,__bbw_rtc`) saved to
`<data>/rtc.bin` by a 100 ms watcher and restored at start; `onRestart` parks the thread and never
exits; no web listener (requests go through `bbw_request`). FreeRTOS tasks become pthreads whose QoS
follows the priority (control: user-interactive, BLE: user-initiated, the rest default), through a new
minimal hook, `HostHooks::taskStarted`, in `emu/src/emu.h` and `host_runtime.cpp`. The data folder is
`Application Support/Controller` (real) or `Controller-demo` (demo), file protection
`CompleteUntilFirstUserAuthentication`. `bbw_ios_prepare(demo)` does all of it and calls
`bbw_host_configure`; in real mode it installs the CoreBluetooth backend.

**Simulated:** demo mode (the default until the real screens switch it; `-bbwReal` as a launch argument
selects real devices) runs the simulated Lunar, SwitchBot and Breville on the virtual radio. The
unit test and the UI test use demo mode only. Real devices are not exercised by any test.

**Tests:** the unit tests are hosted in the app, which does not start the controller when XCTest hosts
it (`XCTestConfigurationFilePath`), so the test starts it on a temporary folder
(`bbw_ios_prepare_at`). The UI test passes `-bbwDemoReady`, which makes the app pair the simulated
bot, pick the Breville profile and place the cup, as the unit test does.

**Retired:** the Phase 0 spike (`BrewCoreBridge`, `Sim/MachineSim.h`, its Swift screens and tests) and
every `EspressoScaleProtocol` path. The `tests` workflow fails if `project.yml` or the Xcode build log
mention it.

## How CI runs

`tests` (Codemagic, pushes to non-main branches that touch `ios/`, `src/`, `lib/`, `emu/` or `test/`):
EspressoScaleProtocol grep, `test/host/run.sh` and `BBW_CLEAN=1 test/host/run.sh` with clang,
`test/hostapp/run.sh` with clang, `xcodegen`, then build-for-testing and the unit and UI tests on a
simulator, with the logs and screenshots as artifacts.

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
   later). Open it: the placeholder screen shows the controller's state and weight; tap Start.

Do not run Real devices against the Lunar and Bot while the deployed board owns them.

## Unverified until the first Codemagic run

Nothing in Swift, XcodeGen, Xcode or Codemagic has been run, and `ios_runtime.mm` and
`ble_corebluetooth.mm` have never been compiled with the iOS SDK. Likeliest to need a fix:

1. XcodeGen: `path:` entries outside `ios/` with `group:`, single-file sources, `optional: true` for the
   networking files, a `Host` entry with `buildPhase: none` that lists only headers, and `info:` with
   `GENERATE_INFOPLIST_FILE`.
2. Header resolution with `USE_HEADERMAP=NO` and the search-path order; `emu/shims` headers
   (`Arduino.h`, `WiFi.h`) must win over nothing in the SDK.
3. `getsectiondata` + `dladdr` finding `__DATA,__bbw_rtc`; `ENABLE_DEBUG_DYLIB=NO` keeps it in one
   image. If the section is dropped (dead strip), RTC persistence silently does nothing.
4. `BrewHost.cpp` always boots as `POWER_ON`, so a shot cut short by a kill is not recovered through the
   restored RTC flag yet (design section 6); a follow-up there.
5. Swift: `BbwLive()` zero init, `state_name` as a tuple read with `withUnsafeBytes`, async XCTest on a
   blocking C API.
6. The unit test needs the C symbols from the app through `TEST_HOST`/`BUNDLE_LOADER`.
7. Both tests are timing based: a demo shot takes about 30 s in real time.
8. `test/host/run.sh` and `test/hostapp/run.sh` with Apple clang/libc++.
9. Codemagic `when: changeset` on the first push of a branch; `-retry-tests-on-failure`.
10. Signing and upload: as for the spike, never run for this app.
