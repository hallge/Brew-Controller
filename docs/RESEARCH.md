# Research notes

What was read, what it established, and what could not be established.
"Verified in source" means the statement was read in code or a vendor
document. "Not verified on hardware" means nobody has run it against a real
Lunar, Bot or Breville from this project yet.

## 1. Acaia Lunar BLE protocol

Sources read in full: `AcaiaArduinoBLE` v3.3.0 (tatemazer), `AcaiaEspressoIOT`
(an older fork of the same library plus an unrelated web-server example), and
Open Brew by Weight's `EspressoScaleBLE` (`protocols/Acaia.cpp`,
`ScaleProtocolCommon.cpp`, `EspressoScaleBLENimble.cpp`).

| Question | Answer (verified in source) |
| --- | --- |
| Discovery | By advertised local name prefix: `LUNAR`, `ACAIA`, `PYXIS`, `PEARL`, `PROCH`, `CINCO`. No service-UUID filter. |
| Lunar pre-2021 | One characteristic `2a80` for both notify and write. 10-byte weight packets (14 on AL008 firmware). |
| Lunar 2021 / Pyxis | Notify `49535343-1e4d-4bd9-ba61-23c647249616`, write `49535343-8841-43f4-a8d4-ecbe34729bb3`. 13-byte weight packets, 17 on newer firmware (those also carry the timer). |
| Session setup | Subscribe, then write `IDENTIFY` (20 bytes) and `NOTIFICATION_REQUEST` (14 bytes). Without them the scale sends nothing. |
| Heartbeat | `ef dd 00 02 00 02 00` every 2750 ms, or the scale drops the session. |
| Weight decode (2021) | `ef dd 0c <len> 05`, weight = bytes 5..6 little-endian / 10^byte 9, negative if byte 10 bit 1. Two-byte additive checksum at the end. |
| Tare | `ef dd 04 00 00 00` |
| Timer | start `ef dd 0d 00 00 00 00`, stop `ef dd 0d 00 02 00 02`, reset `ef dd 0d 00 01 00 01` |
| Dead-link rule | No valid packet for 5000 ms means disconnect and reconnect (both libraries). |
| Writes | Without response. AcaiaArduinoBLE patches ArduinoBLE internals to force this; Open Brew calls `ble_gattc_write_no_rsp_flat`. |
| Pairing / auth | None. Any central can connect. |
| Scale state | Weight, timer, keys pressed on the scale (tare, start, stop, reset) and, in the settings frame, battery and whether the timer is running. There is no "tare done" notification, so a tare is confirmed by watching the weight return to zero. |

Reused here: the Acaia protocol table, discovery, checksum, command encoder
and the legacy parser are the unmodified Open Brew files in
`lib/EspressoScaleProtocol`. The transport is new (NimBLE-Arduino instead of
ESP-IDF NimBLE), and so is the decoder for the current protocol's event
stream, for the reason below. Since the scale abstraction (README, "Supported
scales") the Felicita, Decent, DiFluid, Eclair, Eureka, MyScale, Varia and
WeighMyBru tables from the same source are vendored too, also unmodified.

### The 17-byte weight frame (Lunar 2021, firmware 1.0.019)

Checked against two more decoders, `pyacaia` and `aioacaia` (the Home
Assistant library), and against the frames `aioacaia` ships as test fixtures,
which are verbatim captures from a 2021+ Lunar:

```
ef dd 0c 0c 05 00 00 00 00 01 00 07 00 08 01 1c 06      0.0 g, timer 8.1 s
|     |  |  |  '---------------' |  '------' '---'
|     |  |  |   weight record    |   timer    checksum
|     |  |  |   value(2) .. unit |   min, s, tenths
|     |  |  type 05 = weight     tag 07 = a timer record follows
|     |  length (0c = 12; frame is length + 5 = 17 bytes)
|     0c = event notification
header
```

Everything after the type byte is a chain of tagged records: `05` weight
(6 bytes), `07` timer (3 bytes), `08` key (1 byte), `0b` acknowledgement
(2 bytes), `06` battery (1 byte). The 13-byte frame is the same thing without
the timer record.

Two things follow that the vendored Open Brew parser does not allow for:

1. **The timer is at bytes 12..14, behind the `07` tag at byte 11.** Open Brew
   reads bytes 11..13, so it takes the tag for the minutes: the capture above
   decodes to 7 min 0.8 s instead of 8.1 s, and from 10 s on (when its
   "tenths" byte exceeds 9) to nothing at all. The weight is unaffected.
2. **A frame can arrive split over two notifications** (3 + 14 bytes in the
   captures). Open Brew accepts a notification only if it is exactly one
   10, 13 or 17 byte frame, so both halves are dropped and that reading is
   lost.

`src/core/AcaiaStream.h` therefore decodes the stream itself: it reassembles
frames across notifications, verifies the checksum, and walks the record
chain. It is tested in `test/host` against those captures, including a check
that pins down the vendored parser's wrong answer. The vendored files are
left unmodified and still handle discovery, commands and legacy scales.

Not decoded: bit 0 of the weight flag byte. Neither library interprets it
(it may be a "stable" flag). The raw flag byte is stored with every reading
so that it can be looked at once a real scale is connected.

Limits found for the Lunar 2021 specifically:

- Open Brew's compatibility table rates its connection "Hit or Miss"
  (firmware v1.0.016), against "Great" for the pre-2021 Lunar.
- The AcaiaArduinoBLE README states the tare command "is less reliable than
  pressing the tare button". This firmware therefore verifies every tare
  against the weight stream and retries (3 attempts) before raising
  `TARE_FAILED`.
- Grams only.

Packet frequency: none of the three sources states a rate, and none assumes
one. This firmware does not either. Every sample is stamped with the local
receive time and all flow and prediction maths uses those stamps; the
measured interval is shown on the web page and in `status` on serial.

## 2. Can the Lunar be woken over BLE?

Conclusion: no reliable wake mechanism was found. The firmware treats the
scale as something the user wakes with its power button.

| Layer | Finding |
| --- | --- |
| Documented BLE wake command | None. Acaia's manuals and help pages describe power-on only by the power button. The reverse-engineered command set in all three libraries, and in pyacaia, has no power opcode. Open Brew's own support table lists Acaia as "no power-off command is documented"; its only power-off opcode (`0x15`) belongs to the Bookoo Themis Ultra. |
| Acaia firmware wake | Nothing in any source sends a packet to a sleeping Lunar. Open Brew's `MicraScalePowerOnTracker` looks like a wake feature but is the opposite: it reacts to the scale reconnecting after the user switched it on, and uses that to wake the espresso machine. |
| Bluetooth controller wake | Unknown, and this is the open point. Whether the Lunar's radio keeps advertising while the scale is "off" (as opposed to merely idle) is not stated anywhere I could find, and I could not measure it. Home Assistant's Acaia integration requires the scale to be turned on and simply retries every 15 s, which suggests it does not advertise when off, but that is inference, not measurement. |
| Reconnecting to an awake scale | Works, and is all the references do. Once connected, the heartbeat keeps the session up; Acaia's auto-off timer only runs while no device is connected (manual, "Auto Switch Off"). |

How to settle the controller-wake question in two minutes: power the Lunar
off (double-tap the power button), open nRF Connect on a phone and scan. If
`LUNAR-xxxxxx` still appears, try connecting. Nothing appearing confirms
there is nothing to wake. If it does appear and a connection turns the
display on, that is an undocumented wake path and worth adding.

Practical consequence already in the firmware: the controller scans
continuously while the scale is missing and connects within about a second
of the scale being switched on, and an idle disconnect is shown as "waiting
for Lunar", not as a fault.

## 3. SwitchBot Bot BLE protocol

Sources: `OpenWonderLabs/SwitchBotAPI-BLE` `devicetypes/bot.md` (vendor
document) and `pySwitchbot` (`devices/bot.py`, `devices/device.py`,
`adv_parser.py`).

| Question | Answer |
| --- | --- |
| Hub / cloud / phone needed | No. Plain GATT. |
| Advertising | Manufacturer data in the advertisement; scan response carries service UUID `cba20d00-224d-11e6-9fb8-0002a5d5c51b` and service data (UUID `0xFD3D`, older firmware `0x0D00`). Service data byte 0, low 7 bits = device type; `0x48` 'H' is the Bot. No device name. An active scan is required to see the type byte. |
| Characteristics | Write `cba20002-...`, notify `cba20003-...`. 20-byte messages. |
| Authentication | None by default. If a password was set in the SwitchBot app, commands become `57 11 <crc32(password)> <action>`. Supported; set the password on the web page. |
| Press | `57 01 00`: push and pull back, one complete momentary press. |
| Push / release separately | `57 01 03` (down, stay) and `57 01 04` (up). |
| Press duration | `57 0F 08 <seconds>` sets how long the arm stays down during a press. Whole seconds only. |
| Completion | A notification whose first byte is the status: `01` OK, `03` busy, `05` unsupported in this mode, `06` low battery, `07`/`09` password problems. |
| Mode trap | In "switch" (two-state) mode the Bot answers `05` to a press. The firmware reads the mode with `57 02` and warns on the display and web page. |
| Reconnect | Not specified by the vendor. pySwitchbot connects per command and drops the link 8.5 s later. |

Two things the documents do not settle, so they are not assumed:

- Whether the `01` status is sent when the command is accepted or when the
  arm has finished moving. The firmware therefore never treats the Bot's
  acknowledgement as proof the machine changed state. Proof comes from the
  scale: flow starting after a start, flow ceasing after a stop.
- How quickly a sleeping Bot accepts a connection. See the connection policy
  below; the firmware allows 15 s and the tare covers part of that.

Connection policy (changed after review): the Bot is **not** kept connected.
The link is opened when Start is pressed, held from then until the shot has
finished and the stop is confirmed (plus 10 s), and then released so the Bot
can sleep. Faults that may still need a press keep it up. The address is
stored at pairing, so the controller connects straight to it without
scanning. Its battery level is read on each connection and shown from the
last reading while it sleeps. Not verified on hardware: how long a real Bot
takes to accept the connection.

ESP32-S3 with both links: the Bluetooth controller supports several
simultaneous central connections and NimBLE-Arduino defaults to three, so
Lunar plus Bot is within limits. Scanning continues only while a device is
missing. Wi-Fi shares the same radio; scan window and connection interval are
set to leave it airtime.

## 4. Breville Dual Boiler button

From the BES920 manual in this folder, page 15:

- "Press MANUAL button once to start the espresso extraction. The extraction
  will start using the low pressure pre-infusion. Press MANUAL button again to
  stop extraction."
- "press and hold the MANUAL button" extends pre-infusion until release.
- Espresso "will start to flow after 9-14 seconds".

Consequences:

- Mount the Bot on **MANUAL**. The 1 CUP and 2 CUP buttons stop themselves by
  volume and would fight the controller.
- The press must be short. A long hold is not a longer press, it is a longer
  pre-infusion. Bot hold time defaults to 0 s.
- One press toggles. A second "stop" press on a machine that already stopped
  starts it again. All retry logic is built around that (section 6 of the
  README).
- The default no-flow timeout is 20 s, beyond the manual's 14 s.

## 5. LILYGO T-Display S3 AMOLED Plus

From `Xinyuan-LilyGO/LilyGo-AMOLED-Series` (`LilyGo_AMOLED.h/.cpp`) and the
LilyGO wiki page for the board:

- ESP32-S3R8, 16 MB flash, 8 MB PSRAM, 1.91" RM67162 AMOLED 536x240, driven
  over SPI on this revision (the library's `BOARD_AMOLED_191_SPI`).
- CST816T capacitive touch on I2C (SDA 3, SCL 2, IRQ 21).
- BQ25896 charger, RTC at I2C address 0x51, microSD slot (unused here).
- One user button: GPIO0 (BOOT). There is no rotary encoder. BOOT and RST
  sit on the two long edges.
- A touch key (orange circle) is printed on the glass at the USB-C end. The
  CST816T reports it as a touch at (600, 120), past the 536-pixel axis; the
  library only filters it out if a home-button callback is installed. Not
  used by this firmware.
- Orientation: `setRotation(0)` and `(2)` are landscape, `(1)` and `(3)`
  portrait. In rotation 0 the touch x axis runs unmirrored towards the key at
  x = 600, so the key and the USB-C connector are on the right. The firmware
  uses rotation 2 to have the connector on the left. Inferred from the
  library source, not verified on hardware: if the picture is upside down on
  the real board, change `ROTATION_USB_LEFT` in `src/ui/Ui.cpp` to 0.

So the controls are the touch screen plus one button; the Yield Target
Pro's knob has no equivalent here and is replaced by touch keys. The library's
`begin()` tells the Plus apart from the plain 1.91" board by probing the RTC
address; if detection ever picks the wrong one, call `beginAMOLED_191_SPI()`
in `ui/Ui.cpp`. The on-board RTC is not used: the clock comes from the
browser (and NTP when joined to a network), and shots are stored with an
empty timestamp until one of them has set it.

## 6. Yield Target Pro and Mini

Source: the two user guides in this folder (`brew-by-weight-pro-user-guide`,
`brew-by-weight-mini-user-guide`). The Etsy listings themselves return HTTP
403 to this tool, so physical dimensions and anything stated only on the
listing page were not available. Used as a UI and workflow reference only.

What the product is, from the guides: a servo arm that presses the brewer's
button, a BOOKOO scale over Bluetooth, and either a small display with a
rotary knob (Pro) or one button, an LED and a phone page on its own Wi-Fi
access point (Mini). Same idea as this project with a servo where this one
has a SwitchBot.

| Yield Target behaviour | Here |
| --- | --- |
| Three-line screens: status line, one large value, detail line | Same structure on every shot screen |
| Ready: `READY 0.0G 90%` / `START` / `P1 TARGET 36.0G` | `READY 0.0 g` + link dots / START / recipe and target. The Lunar protocol used here does not report scale battery, so there is no percentage |
| START is the first menu item: one action brews the loaded profile | START is the largest thing on the home screen; BOOT button does the same |
| Three profiles, each with its own mode, values and stop adjustment | Up to 16 recipes, each with its own mode, values, learned offset and learning factor |
| Target mode or Ratio mode (dose x ratio, 5-30 g, 1:1.0-1:5.0) | Both, same ranges. In ratio mode - and + change the dose |
| Target 5.0-100.0 g in 0.1 g clicks | 5-200 g. Tap is 0.5 g, holding the key repeats in 0.1 g steps |
| `AUTO TARE` / `WAIT FOR ZERO` | Same |
| Brewing: `00:28.4 /36.0G` / live weight / `2.1G/S EARLY 2.1G` | Same three lines: time and target, live weight, flow and how early the stop comes |
| `FINALIZING` / `LEAVE CUP ON SCALE` | Same |
| Result: `OVER +0.2G` / final weight / `AUTO EARLIER 0.1S` | `OVER +0.2 g` / final weight / time, average flow, drip / `AUTO EARLIER 0.06 g`. The adjustment here is a weight, not a time |
| Result marked `*` when the weight did not settle; such shots do not adjust future stops | Same |
| Manual stops do not count towards the automatic adjustment | Same |
| Press during a shot stops it; press during tare cancels | Same, touch or BOOT button |
| Mini: hold the button 1.2 s to select the next profile | Same on the BOOT button |
| Mini: own Wi-Fi access point and a status page with weight, target, flow, shot time, final result | Same, plus history, CSV and all settings |
| `CAL`: set how far the arm presses | Not applicable: SwitchBot travel is set mechanically or in its app |
| Sleep after 10 min idle, sleep when the scale disconnects | Display dims after 3 min. The controller is USB powered and stays up so it can reconnect to the Lunar on its own |

## 7. What was taken from Open Brew by Weight

| Concept | Open Brew source | Here |
| --- | --- | --- |
| Acaia protocol | `EspressoScaleBLE/src/protocols/Acaia.cpp` etc. | vendored unmodified |
| Stop at target minus offset | `ShotStopperBbwCutoff.h` | `BrewController::acceptBrewSample` |
| Two fresh samples to confirm | `DIRECT_STOP_CONFIRMATION_SAMPLES` | same constant |
| Linear prediction of the cut time | `bbwEwma::predict` | `predictStopTimeS`, same maths and constants |
| Offset learning | `bbwEwma::learn`, legacy `learn` | `learnStopOffset`; factor 1.0 equals the legacy rule |
| Offset bounds 0..5 g, reject implausible shots | `MAX_OFFSET_G` | same |
| Only weight-target stops train the offset | `ShotStopperBbwFinalize.inc` | `finalizeShot` |
| Drip delay then final weight | `DEFAULT_DRIP_DELAY_MS` 3000 | `dripObserveMs` |
| Lifted-cup check on the final weight | `plausibleSettledBrewWeight` | same 2 g rule |
| Sample plausibility (range, slew) | `MAX_AUTOMATION_WEIGHT_SLEW_G_PER_S` | same constants |
| Running / stopped inferred from flow | `ShotStopperMachineMomentaryOnlyState.h` (0.6 / 0.2 g/s, 500 ms) | same thresholds |
| Never pulse an unconfirmed start | same file | `START_COMMAND_FAILED` path |
| Adaptive learning-factor selector | `bbwEwma::Evidence` | `AlphaEvidence`, same rules; a web setting chooses fixed or adaptive |
| Presets | `ShotStopperPresets.h` | `Recipe`, up to 16 |

Left out on purpose: relay and GPIO control, relay safety timers, reed
switch, paddle modes, machine electrical profiles, Linea Micra cloud
integration, OTA, webhooks, Home Assistant, buzzer, fast/slow extraction
guards, accidental-touch detection, quick rinse.

## 8. Mahlkoenig Grind-by-Sync and the grind advisor

Sources: Mahlkoenig "Software Manual Grind-by-Sync" v1.36, the Sync Scale user
manual, Mahlkoenig's Sync System product page and technical FAQ, and Prima
Coffee's first-impressions article. Retail listings were used only for the
E64 WS's grind display.

### What Grind-by-Sync does (documented)

- A recipe on the grinder holds a **target brew time**, a brew weight, a
  reference burr distance ("Disc Distance", in microns) and a "GbS start DD".
- The espresso machine, or the Sync Scale, reports each shot's **brew time**.
  That is the only brew measurement the algorithm is documented to use. Weight
  and flow are not inputs; the Sync Scale uses flow only to start and stop its
  timer.
- "The grinder compares the moving average of the actual brew time to the
  recipe target time." Too fast means too coarse, so it closes the burrs; too
  slow, it opens them.
- **GbS Brew Time Tolerance**: a setting. "The cycle is considered correct if
  the brew time falls within the set value." Inside it nothing moves.
- **Valid brew events** (Sync Scale manual): the time must be above 15 s and
  below target + 15 s, and the event must not be interfered with (for example
  the cup removed before the timer stops). Invalid events cause no adjustment.
- A burr-distance lower limit can be set. Turning GbS off for a recipe resets
  its history.
- Reported in use (Prima): starting far from the right grind makes it
  overcorrect and "chase its tail", because grounds from the old setting are
  still in the grinder; large changes want a purge. Channelling from uneven
  puck preparation makes it compensate for something that is not the grind.

### What is not published

- How many shots the moving average covers.
- How many microns it moves per second of error, or whether that is fixed.
- The default tolerance.

### The E64 WS

Retail listings describe motorised, stepless adjustment with burr position
tracked to 0.001 mm and shown on screen in microns, and some describe it as a
Grind-by-Sync grinder. Mahlkoenig's own Sync System page lists the E65W, E80W,
E65T and E80T as GbS models and does not name the E64 WS, so whether your unit
can take part in Sync is not confirmed here. This project does not talk to
the grinder either way: you read the micron value off its display and type it
in. I did not find the E64 WS manual, so its usable micron range is unknown;
the firmware accepts 1 to 2000.

### How this project does it (`src/core/GrindAdvisor.h`)

Collected with every shot: grinder setting in microns (whatever is entered as
"current grind" when the shot is pulled; editable per shot afterwards), dose,
the recipe's target brew time, brew time, weights and flow.

| Step | Rule | Origin |
| --- | --- | --- |
| Which shots count | Ended on the weight target, final weight settled, no stop retry, more than 15 s and less than target + 15 s, grind recorded | Mahlkoenig's valid brew event, with "interfered" mapped onto what this controller can observe |
| Which shots compare | Same recipe, same target weight, same dose | Mine: brew times at different yields are not comparable |
| Moving average | Newest shots at the **current** grind setting, up to the window (default 3) | Mahlkoenig says "last shots". Restricting to the current setting is mine: a shot at the old setting says nothing about the new one |
| Decision | Average inside target +/- tolerance (default 2 s): keep. Faster: finer. Slower: coarser | Mahlkoenig |
| Size of the change | error in seconds x microns-per-second, rounded, capped (default 20 microns) | Mine; Mahlkoenig does not publish theirs |
| Microns per second | Least-squares fit of brew time against grind setting over your valid shots, once they span at least two settings 4 microns apart. Until then a starting guess (default 3) | Mine |

Two durations are stored with every shot, and a setting ("Brew time
measured") chooses which one the advisor compares with the target:

- **Total**: start press to stop press. On the Breville this includes
  pre-infusion, and matches what an espresso machine reports to a GbS grinder.
- **Pour**: first drops in the cup to stop press. This is what a
  flow-detecting scale timer such as the Sync Scale measures, and it leaves
  out the pre-infusion dead time, which does not depend on the grind in the
  same way. First drops are detected as two consecutive readings of 0.3 g or
  more after the tare.

Pour flow (final weight divided by pour time) is stored alongside and is the
flow figure shown on the device and the web page; flow over the whole shot
reads low because it counts the seconds before anything reached the cup.

The target brew time means whichever basis is selected, and each shot records
the basis it was pulled under. Shots from the other basis are not mixed in,
so switching starts a fresh comparison.

Because every shot stops at the same weight, brew time alone carries the
information and average flow is just yield divided by time. Flow is stored and
shown but adds nothing to the calculation, which is also why Mahlkoenig can
work from time alone.

Limits worth knowing:

- The starting guess of 3 microns per second is a placeholder, not a
  Mahlkoenig figure and not measured on an E64 WS. The first recommendation or
  two may over- or undershoot; the cap exists for that. After shots at two or
  three settings the measured value takes over and is shown on the page.
- The measured value assumes the same coffee. A new bag changes it; history
  from the old bag will mislead until newer shots outweigh it. There is no
  bean tracking yet.
- It cannot tell a grind problem from a puck-preparation problem.
