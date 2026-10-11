# PC emulator

Runs the firmware on the PC, before the board arrives: the T-Display S3 AMOLED
Plus screen and touch, an Acaia Lunar, a SwitchBot Bot and the Breville's
MANUAL button, in one browser page.

```
emu\run.cmd
```

It builds on first use (about two minutes, a few seconds afterwards), starts
the emulator and opens `http://localhost:8090`. Stop it with Ctrl+C.

| Address | |
| --- | --- |
| `http://localhost:8090` | emulator page: the board, the three devices, the log |
| `http://localhost:8080` | the firmware's own web page (on the real board: `http://192.168.4.1`) |

Needs the libraries PlatformIO fetched into `.pio/libdeps` (`pio pkg install`)
and the `ziglang` package in `.venv` as the C++ compiler. Both are already
there in this checkout.

## What is real and what is emulated

The firmware is not rewritten or mocked. `emu/build.py` compiles the same
files as `pio run` does (`src/main.cpp`, `src/ui/Ui.cpp`, `src/ble/Ble.cpp`,
`src/net/Web.cpp`, `src/storage/Store.cpp`, `src/core/*`, the Acaia protocol
library and LVGL 8.4 with the project's `lv_conf.h`) for Windows, against
stand-in headers in `emu/shims`:

| On the board | In the emulator |
| --- | --- |
| RM67162 AMOLED 536x240, CST816T touch | LVGL frame buffer shown on the page, mouse as finger, backlight level and standby shown |
| BOOT button (GPIO0), RST | tabs on the top and bottom edges at the USB-C end |
| Touch key (orange circle beside the USB-C connector) | the orange circle; sent as the CST816T sends it, a press at x = 600 just past the screen |
| NimBLE-Arduino | `emu/shims/NimBLEDevice.h`: scan, connect, discover, subscribe, write, notify, against a virtual radio |
| Acaia Lunar | `Lunar` in `emu/src/emu_devices.cpp`: advertises by name, needs identify + notification request, expects heartbeats, streams checksummed weight packets, obeys tare and timer commands. Default model is the Lunar 2021 with 17-byte frames (weight plus a tagged timer record, as captured from real scales); `splitFrames` delivers each frame as 3 + 14 bytes |
| SwitchBot Bot | `Bot`: advertises service data `fd3d` (type `H`), wakes slowly, answers `57 01 ..`, `57 02`, `57 0f 08 ..` with status bytes, password and switch mode included |
| Breville Dual Boiler | one toggle button, pre-infusion delay, flow into the cup, drip after the stop |
| NVS, LittleFS | files under `emu/data` (kept between runs; "Erase flash" clears them) |
| Wi-Fi access point, WebServer | the same handlers on `localhost:8080` |
| Serial | the console, and the log panel with its input line |
| FreeRTOS tasks, RTC memory, reset reasons | threads; a crash or reset restarts the controller process and keeps RTC variables |

The scale and the Bot do not reuse the firmware's protocol code: they build
and parse the bytes themselves from the descriptions in `docs/RESEARCH.md`.
Every write and notification is in the log as hex.

## Orientation

The board is drawn as it will be mounted, USB-C on the left. That is rotation
2 of the LilyGo library (`ROTATION_USB_LEFT` in `src/ui/Ui.cpp`); rotation 0,
the library default, has the connector on the right. The emulator follows the
library's numbering and flips the drawing if the firmware picks the other
one. Which of the two edge buttons is BOOT is assumed (top, with USB-C on the
left), not taken from a drawing.

The touch key is the firmware's start / stop / acknowledge key, the same
action as a short BOOT press. The firmware installs the touch library's
home-button callback, and the emulator delivers the key through it as the
CST816T driver does: on every poll while held, and not as a point. On a dark
screen the first press only wakes it.

## Things to try

- First run: gear icon, PAIR NEAREST BOT, back, START.
- Change "Flow rate" between shots and watch the stop offset learn.
- Scale "Model": Felicita Arc, Decent Scale, DiFluid Microbalance, AtomHeart
  Eclair, Eureka Precisa, MyScale, Varia AKU or WeighMyBru instead of the
  Lunar. They are written from the protocol tables in
  `lib/EspressoScaleProtocol` (connect, stream, tare; the Felicita also its
  timer), so they prove the controller works with a scale that has less than
  the Lunar, not that those tables match the real scales.
- "Power bot in range" (under the Bot's faults): a second SwitchBot Bot on
  the machine's POWER button. Pair it as the power bot on the controller's
  web page (Setup, Devices, "Power"), then use "Machine power" there; the
  "machine on" box and the power-press count show what it did. There is no
  emulated Fingerbot or MicroBot.
- Lunar "power" off: after 30 s the screen goes dark; on again wakes it.
- Lunar "go silent" during a shot: stop is pressed, `SCALE_DATA_STALE`.
- Bot "in range" off, then START: `SWITCHBOT_DISCONNECTED` after 15 s, nothing pressed.
- Bot "switch mode", "lose acks", "lose commands", "arm jammed".
- "Crash the controller" during a shot: the pump keeps running, the firmware
  restarts and stops it.
- "Second Bot nearby" before pairing: the nearer one must be chosen.

`emu/tap.sh` has shell helpers (`tap x y`, `setv device key value`, `fw`,
`logs`) for scripting the same things.

## Limits

- No radio. Range, interference, Wi-Fi/BLE coexistence and real connection
  timing are not tested. Latency is a few milliseconds.
- The two peripherals are as accurate as the reverse-engineered sources they
  were written from. Points nobody has documented are settings, not facts:
  whether the Bot acknowledges on receipt or when the arm is done ("ack when
  done"), how long it takes to wake ("Wake s"), the Lunar's packet rate, and
  which packet length a given Lunar firmware sends ("Model"). The pre-2021
  packet layout is approximate.
- The machine model is a button and three numbers, not hydraulics.
- Timing is PC timing: no flash-write stalls, no CPU limit, the task watchdog
  only logs.
- Not emulated: battery charger, RTC chip, SD slot, display colour/brightness
  rendering of a real AMOLED.

README section 7 ("To check on real hardware") still applies in full.

## Real Bluetooth, later

Emulating the Lunar and the Bot over the air is possible, but not from this
PC: Windows cannot put a chosen local name (`LUNAR-...`) or SwitchBot service
data into its advertisements, and the firmware finds both devices by exactly
those. A second ESP32 (any model) running a NimBLE peripheral can advertise
both; the device models in `emu_devices.cpp` are plain C++ and would port.
Chip emulators do not help: Espressif's QEMU has no Bluetooth, and Wokwi
simulates Wi-Fi only and has no RM67162 panel.
