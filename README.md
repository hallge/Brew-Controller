# Brew by Weight: Breville Dual Boiler + Acaia Lunar + SwitchBot Bot

Firmware for a LILYGO T-Display S3 AMOLED Plus that pulls an espresso shot to
a target weight. It reads an Acaia Lunar over BLE and stops the shot by having
one SwitchBot Bot press the machine's MANUAL button. No relay, no wiring into
the machine, no hub, no cloud.

Status: compiles; the brew logic passes its host test suite against a
simulated machine. The workflow and screens follow the Yield Target Pro guide. **It has not been run on real hardware.** Section 7 lists
what to check on the first shots.

## 1. How a shot goes

Tap START (touch, the BOOT button, or the web page if enabled). The last used
recipe is used; there are no menus in the way.

```
READY -> TARING -> WAITING_FOR_CUP -> READY_TO_BREW -> STARTING_SHOT
      -> BREWING -> STOPPING_SHOT -> POST_SHOT_DRIP -> SHOT_COMPLETE -> READY
```

1. **TARING**: tare is sent and confirmed by three readings within 0.5 g of
   zero. Up to three attempts.
2. **WAITING_FOR_CUP**: with the default workflow the cup is already on the
   scale, so this passes straight through. With "Tare, then place cup" it waits
   for a stable load of at least the minimum cup weight and tares again.
3. **STARTING_SHOT**: the Lunar timer is reset and started, and the Bot presses
   MANUAL.
4. **BREWING**: the stop is requested when two consecutive readings reach
   `target - stop offset`, or slightly earlier when a straight-line fit of the
   last ten readings says that point will be crossed between readings.
5. **STOPPING_SHOT**: the Bot presses MANUAL again.
6. **POST_SHOT_DRIP**: the scale is watched for the drip observation period.
   Flow falling below 0.2 g/s for half a second confirms the machine stopped.
7. **SHOT_COMPLETE**: final weight, time, average flow and drip are shown, the
   shot is stored, and the stop offset is adjusted for next time.

Offset learning: `offset += learning factor x (final - target)`, kept between 0
and 5 g, per recipe. Only shots that ended on the weight target teach it;
manual stops, timeouts, retried stops and results that did not settle do not.

Learning factor mode (web page, Brew settings):

- **Fixed**: always the value you enter.
- **Adaptive**: Open Brew's selector. Each recipe starts from the value you
  enter. After 20 eligible shots it compares gains of 0.10, 0.30, 0.50 and
  1.00 against the one in use every fifth shot, and switches when another has
  been at least 10 % better twice running. The earliest possible switch is
  shot 25, so this matters over weeks, not days. The factor in use is shown
  per recipe and recorded with every shot. Changing the entered value or the
  mode restarts it.

**Bloom.** A recipe can pause between pre-infusion and the brew ("Bloom
pause" on the Recipes page, 1 to 45 s). It needs a held pre-infusion. The Bot
holds MANUAL for the hold time and lets go, presses again as soon as it is
ready (a little over a second) to switch the pump off, waits for the pause,
then holds MANUAL for one second, which starts the pump with next to none of
the machine's own pre-infusion, and the shot runs to its target as usual.
The display shows `BLOOM` and the seconds left. The pause is part of the
brew time, so set the recipe's target time with it in mind, and leave room
for it in "Stop after".

One press toggles the machine and nothing reports which way it went, so the
controller checks each step against the scale. STOP during the pause presses
nothing, because the pump is already off. If espresso keeps arriving three
seconds into the pause, the pump is on (the first press was missed, or the
off press was): the shot is then supervised to its target and stopped, and
no start press is added on top. If the Bot cannot make a press, the shot
ends as `bloom_failed`. Tested against the simulated machine and in the
emulator; like the rest, not yet on the real one.
Recipes are either **Target** (a weight in the cup) or **Ratio** (dose x
ratio, for example 18.0 g at 1:2.0 gives 36.0 g).

While a shot runs the display shows a ring around the weight: red from the
moment the machine starts, with the target in the middle, then green growing
round as coffee reaches the cup and the live weight takes over, closing at the
target. Time (the Lunar's own timer when it reports one), target and flow sit
beside it.

Standby follows the scale. Thirty seconds after the Lunar switches off, the
screen goes dark and the processor slows down; Bluetooth keeps listening and
the web page stays up. Switching the Lunar on brings the controller back and
wakes the SwitchBot for two minutes so Start is immediate. A touch or a BOOT
press also wakes the screen, and it goes back to standby when left alone.

The SwitchBot is otherwise only connected around a shot. Start wakes it while the scale
tares; if it has not answered within 15 s the shot is not started and
`SWITCHBOT_DISCONNECTED` is shown. About 10 s after the shot is finished the
link is released so the Bot can sleep. A grey Bot dot on the home screen means
paired and asleep, which is the normal idle state.

## 2. Hardware setup

- Mount the Bot on the **MANUAL** button. 1 CUP and 2 CUP stop by volume on
  their own and would fight the controller.
- In the SwitchBot app, leave the Bot in **Press** mode (not Switch mode) and
  leave the hold time at 0 s. Holding MANUAL down extends pre-infusion (BES920
  manual, page 15).
- Power the board from USB. Keep it within a metre or two of both devices.

## 3. Build and flash

PlatformIO, Arduino-ESP32 2.0.x, NimBLE-Arduino 1.4.3, LVGL 8.4.

```bash
pio run
```

```bash
pio run -t upload
```

```bash
pio device monitor
```

If upload does not start, hold BOOT while plugging in USB.

No board yet? `emu
un.cmd` runs this firmware on the PC with an emulated
display, Lunar, Bot and machine; see `emu/README.md`.

Host tests for the brew logic (any C++17 compiler):

```bash
CXX=g++ test/host/run.sh
```

## 4. First run

1. Switch the scale on. The controller adopts the first supported scale it
   connects to and remembers it (see "Supported scales" below).
2. Open settings (gear icon), hold the controller next to the Bot and tap
   **PAIR NEAREST BOT**. A Bot is never adopted automatically, so a Bot on your
   light switch cannot be picked up by accident. The web page lists every Bot
   in range with its address if you prefer to choose.
3. The Lunar dot is green and the Bot dot grey (asleep). Set the target with -
   and + (tap 0.5 g, hold for 0.1 g steps), tap the recipe name to change
   recipe, tap START.

Controls:

- **Touch key** (on the glass beside the USB-C connector): the start button.
  It starts the recipe on the screen and acknowledges a result. It never
  stops a shot. From the recipe list, the settings page or the edit page it
  goes back to the home screen (an unsaved edit is dropped). On a dark screen
  the first press only wakes it.
- **BOOT button**: stops a running shot, but only on the second press: the
  first puts `STOP SHOT? PRESS AGAIN` on the screen for five seconds. Before
  the machine has started it cancels at once. Held for 1.2 s while idle it
  selects the next recipe. From the home screen it also starts a shot, as a
  spare should the touch key ever fail.
- **STOP on the screen** during a shot is a single touch.

The home screen has no start, tare or grind keys. It shows the selected
recipe in full (dose in, target out, grind setting and any grind advice,
pre-infusion, coffee with roast level and age, target brew time and stop
offset) and two keys:

- **EDIT RECIPE**: target (or ratio), dose, grind setting, target brew time
  and pre-infusion of the selected recipe. Everything else is on the web page.
- **Gear**: offset learning, Bot pairing, forget scale, TARE SCALE and the
  Wi-Fi details.

The grind setting is part of the recipe: change it under EDIT RECIPE, where
the advisor's suggested setting is shown when it has one.

Recipes: tap the recipe card for a scrolling list (up to 16) and tap one.
Drag the list itself or the wide bar beside it.

Pre-infusion per recipe, with the Bot on MANUAL:

- **Machine** (press and release): the machine's own programmed pre-infusion.
- **Hold** (press and hold): the Bot holds the button for the time you set,
  then releases. The machine pre-infuses only while the button is held
  (BES920 manual, page 15), so 0.5 s is in effect no pre-infusion, and a
  longer hold is a pre-infusion of that length.

Time limit per recipe ("If the target is never reached", web page):

- **Machine's own time-out** (default): the Bot never stops a shot on time.
  The machine ends it, and the controller only records it. Once the pour has
  stopped for five seconds the controller will not press, because a press on
  a machine that has stopped would start it again.
- **Bot stops the shot after N s**: for a limit shorter than the machine's.

A shot that ends this way still shows its full summary, and is not used for
grind advice or offset learning.

The manual describes a different behaviour for the 1 CUP and 2 CUP buttons:
holding one of those skips pre-infusion and brews at full pressure until it
is released. That mode is not implemented: the Bot goes on MANUAL.

## 5. Web interface and data export

The board always runs its own access point, `BrewByWeight-XXXX`. The password
is shown on the settings screen (default `brewbyweight`; change it under
Network). Browse to `http://192.168.4.1`. It can also join your home network,
after which it is reachable at the address shown on the settings screen and as
`http://brewbyweight.local`.

The page shows live weight, time and flow, the most recent shot with its
weight curve, shot history, and every setting: recipes, learning, timeouts,
cup workflow, SwitchBot press timing, device pairing and network.

| Endpoint | |
| --- | --- |
| `GET /shots.csv` | whole history as CSV, oldest first |
| `GET /api/shots?limit=N` | history as JSON, newest first |
| `GET /api/last` | most recent shot plus its weight curve |
| `GET /api/shot/data?id=N` | one shot: summary, every reading, flow at every reading, data quality |
| `GET /shot.csv?id=N` | the same readings as CSV: time, weight, flow, scale timer, scale flags |
| `GET /api/status` | live values |
| `GET`/`POST /api/settings` | all brew settings and recipes |
| `GET /api/grind` | grind recommendation for the active recipe |
| `POST /api/grind/set?um=N` | record the grinder's current setting |
| `POST /api/shot/grind?id=N&um=N` | correct the grind stored with a shot |
| `POST /api/cmd?c=stop\|tare\|ack\|start` | `start` only if enabled under Network |

Serial (115200 baud): every completed shot prints a `SHOT_JSON {...}` and a
`SHOT_CSV ...` line; during a shot `LIVE,time,weight,flow,state` four times a
second. Commands: `status`, `start`, `stop`, `tare`, `ack`, `csv`, `json`.

### The data kept for every shot

Every reading the scale sends, from the start press until the weight has
settled after the drip, is kept with the shot: time since the start (this
controller's clock, 0.01 s), weight (0.01 g), the scale's own timer where the
frame carried it (0.1 s; the Lunar 2021 on firmware 1.0.x sends it with every
reading), and the scale's raw flag byte. A 30 s shot is about 300 readings,
2.5 kB.

- In the history table, **chart** opens any stored shot: weight, flow, first
  drops and stop marked, with a link to that shot's readings as CSV.
- The flow figures stored with a shot (flow at 25 / 50 / 75 % of the yield,
  acceleration, roughness) are computed from these readings, and
  `/api/shot/data` computes them again from what is on flash, so an older shot
  can be re-analysed if the analysis changes.
- Under the chart: how many readings there were, their spacing and the longest
  gap, and what the scale's timer counted against what this controller
  counted over the same stretch. That last pair is the check on the timing
  everything else rests on.
- The Devices section shows what the scale link has delivered since it
  connected: readings, how many carried the timer, frames that arrived in two
  parts, checksum failures, and the last frame in hex.

The data stays as long as the shot is in the history. If flash runs short
(very long shots), the oldest shots lose their readings first; their summary
line in the history stays.

The last 500 shots are kept in internal flash. Timestamps come from the
browser the first time the page is opened after power-up (or NTP when joined
to a network); shots made before that are stored with an empty timestamp.

There is no login on the web page. Anyone on the same network can change
settings and stop a shot. Starting a shot from the page is off by default.

### Grinder tracking and grind advice

The Grinder section of the web page records the grinder's burr setting in
microns with every shot and recommends changes, in the manner of Mahlkoenig
Grind-by-Sync. Nothing is sent to the grinder: you read the value off the
E64 WS display, and set the grinder yourself.

1. Enter the grinder's current setting once ("Set current grind").
2. Give each recipe a target brew time and dose (Recipes table).
3. Pull shots. After each one the page says keep, go finer or go coarser, by
   how many microns and to what setting, and why. The same line appears on the
   device's result screen.
4. After changing the grinder, press "I set the grinder to N" so the next
   shots are stored against the new setting.

The grind setting belongs to the recipe. A wrong value on a past shot can be
corrected by clicking it in the history table, and a shot can be rated there
(1 to 5, and sour / balanced / bitter). `grind_um`, `dose_g` and `target_time_s` are in the CSV and JSON.

Brew time can be measured two ways (Grinder section, "Brew time measured"):
total, from start press to stop, or pour, from first drops in the cup to stop.
Both are stored with every shot. The target brew time means whichever one is
selected.

On the device: the home screen shows the recipe's grind setting; change it
under EDIT RECIPE (tap 1 micron, hold for 10). The result
screen says `BREW FAST  GRIND FINER`, `BREW SLOW  GRIND COARSER` or
`BREW TIME OK  KEEP GRIND`. It adds the number of microns only once the
sensitivity has been measured from your own shots; until then it points to the
web page.

How it decides is in `docs/RESEARCH.md` section 8. In short: it averages the
brew time of the last few valid shots at the current setting, does nothing
inside the tolerance, and otherwise converts the error in seconds to microns
using a sensitivity it measures from your own history. Shots are compared
only within the same recipe, coffee and roast date, so a new bag starts
clean. With three or more shots at a setting, one that sits more than 4 s
(or twice the tolerance) from the others is treated as a one-off and left
out, so a single bad puck does not move the grind; two in a row do. The
result screen does not decide that silently. For a one-off, and for a shot
that ran out of time, it shows the usual details with "Shot faster (or
slower) than expected. Review preparation routine. Grind finer (or coarser)
if necessary." and two keys:

- **IGNORE SHOT**: never use it for grind advice.
- **COUNT FOR GRIND**: use it, as if it were an ordinary shot.

Leaving the screen without choosing keeps the automatic behaviour: a one-off
waits for a repeat, a timed-out shot is not counted. The ruling can be changed
later in the history table ("For grind advice": auto, counted, ignored).

Each shot also stores the shape of its pour: flow at 25, 50 and 75 % of the
yield, how much it sped up, and how rough it was. Nothing acts on these yet;
they are recorded so that later analysis has history (`docs/FEATURES.md`).

### When no grind advice is given

Advice compares shots made at the same recorded grind setting. If the last
few at that setting ran both clearly fast and clearly slow (outside the
tolerance on both sides), their average would say "on target" for shots none
of which were, so the advice is withheld: `BREW TIMES VARY  NO ADVICE` on the
result screen, "brew times vary" on the home card. Two causes: the puck
preparation is varying, or the grinder was changed without the new setting
being entered (EDIT RECIPE, GRIND, or the Grinder section of the web page).
Entering the new setting starts a fresh comparison, and is also what lets the
controller learn how many microns a second is worth.

### Cup on the scale

By default a shot cannot be started until a cup has been seen going onto the
scale ("Cup on the scale", web page, Brew settings). The home screen says
`PLACE CUP ON SCALE` until then.

Weight alone cannot tell an empty pan reading 0 g from a tared cup reading
0 g, so the controller follows the steps instead: a steady rise of at least
the minimum cup weight is a cup arriving, and it is tared; a fall of that size
is the cup leaving. One consequence: a cup that was already on the scale and
tared when the scale connected is not known to be there. Lift it and put it
back. After a shot the cup counts as used; the next shot needs a cup placed
again.

### How a shot can end, and what the screen shows

| What happened | How it is recognised | Bot | Screen |
| --- | --- | --- | --- |
| Reached the target | two readings at target minus offset, or the predicted crossing | stops | result |
| ...far off the target time, or a one-off | brew time under 15 s, over target + 15 s, or far from the other shots at this grind | stops | result with IGNORE SHOT / COUNT FOR GRIND |
| STOP on the screen, BOOT twice, or the web page | the request itself | stops | result, "stopped with STOP: not counted" |
| Cup lifted mid-pour | reading falls 10 g or more below the most the cup has held, for three readings | stops, within about 0.3 s | CUP LIFTED: STOPPED, with the weight the cup held |
| Stopped at the machine (its time-out, or its button by hand) | coffee had been flowing and has been still for 5 s; or the cup is lifted after the flow had already stopped | does not press | STOPPED AT THE MACHINE, with IGNORE / COUNT |
| Recipe's own time limit reached | elapsed time | stops | SHOT TIMED OUT, with IGNORE / COUNT |
| Cup lifted after the stop, before the weight settled | final reading well below the stop weight | already stopped | result with `*`, "weight did not settle" |
| Stop had to be pressed again | espresso still flowing 2.5 s after the stop | presses again, twice at most | result, "stop had to be repeated" |
| Nothing reached the cup | no weight after the no-flow timeout | does not press | START NOT CONFIRMED |
| Scale lost or silent | link down, or no reading for the scale timeout | stops | SCALE DISCONNECTED / SCALE WENT SILENT |
| Readings make no sense | five implausible readings in a row that are not a lifted cup | stops | WEIGHT NOT VALID |
| Controller restarted mid-shot | flow seen after an unexpected restart | stops | STOPPED AFTER RESTART |

The scale cannot tell the machine's own time-out from someone pressing its
button: both are a pour that stops with nothing pressed here, so both are
reported as "stopped at the machine".

A recipe edited while a shot is running does not affect that shot: the shot
uses the values it started with.

## 6. What happens when something goes wrong

One press toggles the machine, and the Bot cannot tell the controller whether
the machine is brewing. So the scale is the only evidence, and every rule
below follows from two facts: a missed stop leaves the pump running, and an
unnecessary second press starts it again.

| Situation | Behaviour | Ends in |
| --- | --- | --- |
| Scale disconnects or goes silent mid-shot | Stop is pressed immediately | `SCALE_DISCONNECTED` / `SCALE_DATA_STALE` |
| Cup lifted, impossible readings | Stop is pressed | `INVALID_WEIGHT` |
| Target never reached, recipe set to "Machine's own time-out" (default) | Nothing is pressed. The machine ends the shot; the controller sees the pour stop and shows the summary under MACHINE ENDED THE SHOT | `SHOT_TIMEOUT` |
| Target never reached, recipe set to "Bot stops the shot after N s" | Stop is pressed at N s; summary under SHOT TIMED OUT. If the machine stopped first, nothing is pressed | `SHOT_TIMEOUT` |
| Bot link down when the stop is due | "STOP BY HAND" after 1.5 s; the press goes out the instant the Bot reconnects; gives up after 30 s | `SHOT_COMPLETE` or `STOP_COMMAND_FAILED` |
| Stop pressed but espresso keeps flowing 2.5 s later and 3 g more has landed | Press again, at most twice | `SHOT_COMPLETE`, or `STOP_COMMAND_FAILED` |
| Stop sent, acknowledgement lost | No blind retry. If flow stops, it worked. If flow continues, press again | as above |
| Start pressed, nothing in the cup after the no-flow timeout | No second press, because it could start the pump | `START_COMMAND_FAILED` |
| STOP tapped before any coffee has been seen | The press is sent (the machine is normally pre-infusing), then the scale is watched for the no-flow timeout. If coffee arrives, that press started the pump because the start had never reached the button; it is pressed again | `SHOT_COMPLETE`, or `START_COMMAND_FAILED` with the pump off |
| ...and coffee appears later anyway | The shot is taken back under control and stopped on target | `SHOT_COMPLETE` |
| Tare not confirmed after three tries | Nothing is pressed | `TARE_FAILED` |
| Bot does not connect within 15 s of Start | Nothing is pressed | `SWITCHBOT_DISCONNECTED` |
| Controller crashes or the watchdog fires mid-shot | After restart, if the scale shows espresso flowing, stop is pressed once | `SHOT_TIMEOUT` |
| Lunar asleep while idle | Not a fault: "waiting for Lunar" | |

What this cannot cover: if the controller loses power, or both the scale and
the Bot are unreachable, during a shot, nothing stops the machine. The
Breville has to be stopped by hand. **Do not leave it unattended while
brewing.**

`START_COMMAND_FAILED` and `STOP_COMMAND_FAILED` screens offer a PRESS BUTTON
key that sends one raw press, for when you can see what the machine is doing.

## 7. To check on real hardware

None of this could be tested here.

1. **Scale link.** Does the Lunar connect and stream? `status` on serial shows
   the protocol (`acaia` for the 2021 model, `acaia_legacy` for older) and the
   measured interval between readings. On the web page, Devices, "Scale link"
   should show the readings climbing, nearly all of them "with the scale's
   timer", no checksum failures, and a last frame of 34 hex digits (17 bytes)
   starting `efdd0c0c05`. If it shows something else, send me that line.
   After a shot, the line under the chart should show the scale's timer and
   the controller counting the same time to within a few tenths.
2. **Bot press.** Does one press reliably start the machine, and one more stop
   it? If the arm does not travel far enough, that is fixed mechanically or in
   the SwitchBot app, not here.
3. **Press style.** The default lets the Bot do its own push-and-return. If
   that is too slow or unreliable, try "Controller-timed down / up" and adjust
   the press duration.
4. **How long does the Bot take to wake?** The serial log shows
   `bot: connecting` and `bot: connected`. It has the length of the tare plus
   up to 15 s. If it regularly takes more than a few seconds, the shot simply
   starts later; if it times out, tell me the figures.
5. **First shots.** Expect the first one or two to miss by a gram or so while
   the offset settles. Watch that `drip` and `stop latency` in the CSV look
   plausible and stable.
6. **Wi-Fi and two BLE links together.** If readings stutter while the web
   page is open, that is radio contention.

7. **Another make of scale.** The drivers for every make but Acaia have
   only run against the emulator, which was written from the same protocol
   tables as the drivers, so it cannot show a table to be wrong. On a real one:
   Devices should name the protocol (as in the table in section 8), "Scale
   link" should show readings climbing with none dropped, TARE SCALE in
   settings should zero it, and the interval between readings in `status`
   should be near 100 ms. If readings are dropped, send me the "last frame".

8. **Another make of pusher.** The Tuya Fingerbot and MicroBot Push drivers
   were written from the open-source Home Assistant libraries for them and
   have never met the real devices. On a real one: Devices should name the
   make once it has connected, a Tuya with a wrong key shows "wrong key?"
   there, and PRESS BUTTON in the settings screen (or "Press the power button anyway" on the web page) should move
   the arm. For a Fingerbot also check that it returns by itself.
9. **Power bot.** One press of the power button should switch the machine
   each way; check the belief on the page against the machine after a few
   scheduled days.

## 8. Supported scales

The Acaia Lunar 2021 is the scale this was built around, and it is the
reference: everything it reports is kept. Other scales connect through a
driver and deliver what they have.

| Scale | Protocol | Weight, tare | Timer driven | Timer read back | Battery | Keys | Flag byte |
|---|---|---|---|---|---|---|---|
| Acaia Lunar 2021+, Pyxis | `acaia` | yes | yes | yes | yes | yes | yes |
| Acaia pre-2021 | `acaia_legacy` | yes | yes | no | no | no | no |
| Felicita Arc | `felicita` | yes | yes | yes | no | no | no |
| AtomHeart Eclair | `atomheart_eclair` | yes | yes | yes | no | no | no |
| Varia AKU | `varia` | yes | yes | whole seconds, not with each reading; not used for the screen's shot time | no | no | no |
| Eureka Precisa | `eureka` | yes | yes | no | no | no | no |
| Decent Scale | `decent` | yes | no | no | no | no | no |
| DiFluid Microbalance | `difluid` | yes | no | no | no | no | no |
| MyScale / blackcoffee | `myscale` | yes | no | no | no | no | no |
| WeighMyBru | `weighmybru` | yes | no | no | no | no | no |

The Varia and the Eureka use the same characteristics and are told apart by
their advertised name, so those two only connect once the controller has seen
the name (`AKU...`, `VARIA AKU`, `CFS-9002`, `LSJ-001`).

The least a scale must do is stream its weight and tare on command. The shot
logic needs nothing else: it works on the weight and the controller's own
clock. What a scale lacks is left out, not made up. Without a timer the
screen shows the controller's shot time, the stored readings have an empty
timer column, and "Drive the scale's timer" does nothing. Without a battery
report there is no percentage. The web page, Devices, says what the connected
scale does not offer.

How it is built:

- `src/core/ScaleDriver.h`: what a driver is. It turns one make's bytes into
  events (weight, flag byte, timer, key, battery, timer state, each optional)
  and the controller's requests into that make's commands, and states its
  capabilities.
- `src/core/AcaiaDriver.h`: the Lunar. It wraps `AcaiaStream` unchanged.
- `TableDriver` (in `ScaleDriver.h`): any scale whose notification is one
  frame holding a weight and perhaps a timer. It runs straight off a protocol
  table in `lib/EspressoScaleProtocol`.
- `src/core/ScaleLink.h`: one connection without the radio. Bytes in,
  readings and diagnostics out; heartbeat, idle poll and silence watchdog.
- `src/ble/Ble.cpp`, `BleScale`: picks the driver whose characteristics the
  connected device has, and moves the bytes.

To add a scale that fits a table: copy its file from Open Brew by Weight's
`protocols/` into `lib/EspressoScaleProtocol/src`, list it in `Registry.cpp`,
add one `TableDriver` line and its slot in `ScaleLink.h`, and add its frame
to `wireFrame()` in the host tests. The tests then run a whole shot over it.
A scale that reports more (battery, keys, split frames) gets its own driver,
as the Lunar has.

The host tests hold the Lunar path to what it was before the drivers existed:
4000 notifications (split, damaged, keys, settings, junk) go through the old
code, kept in the test as the reference, and through the driver, and every
reading and counter must be identical.

## 9. Button pushers: brew bot and power bot

The controller works two button pushers. The **brew bot** sits on the brew
button and is what it always was: it starts and stops the shot. The **power
bot** is optional and sits on the machine's power button.

| Pusher | Protocol | Press | Down / up (timed hold) | Says whether it acted | Battery | Needs |
|---|---|---|---|---|---|---|
| SwitchBot Bot | `switchbot` | yes | yes | yes | yes | nothing (a password only if one was set in its app) |
| Tuya Fingerbot, Fingerbot Plus, CubeTouch | `tuya_fingerbot` | yes | no | yes | yes (not CubeTouch) | uuid, device id and local key from Tuya's cloud |
| MicroBot Push | `microbot_push` | yes | no | no | no | the token from its first pairing |

The SwitchBot Bot is the reference: it does everything it did. The least a
pusher must do is press once on command. What follows from a pusher doing
less:

- **No down / up:** held pre-infusion and the bloom restart need the button
  held for a time set here. A pusher without them makes a plain press
  instead, and the Devices page says so.
- **No answer (MicroBot):** a press that went out is reported as unconfirmed,
  and the scale decides whether the machine reacted, as it already does when
  a SwitchBot's answer is lost.
- **Keys:** entered on the web page, Setup, "Pusher keys", per bot. They are
  stored on the controller and not shown again. Tuya's three values come from
  a Tuya IoT cloud project (the same ones Home Assistant's Tuya BLE
  integration asks for); the MicroBot token is the one Home Assistant or
  pyMicroBot obtained when the MicroBot was first paired. Getting a MicroBot
  token is not done by the controller.

Pairing: Setup, Devices lists the pushers in range; "Brew" makes one the brew
bot, "Power" the power bot. Any Tuya BLE device shows up as a candidate (a
lamp advertises like a Fingerbot), so pick by address. The touch screen's
"hold next to the Bot" pairing only ever takes a SwitchBot or MicroBot, and
never the power bot.

### Machine power

Setup, "Machine power": switch on, switch off, and a weekly schedule of up to
six times. The power button is a toggle and the machine does not report its
state, so the controller keeps count of what it believes. Rules:

- Until you have said once whether the machine is on or off, nothing is
  pressed by "on", "off" or the schedule.
- "On" while believed on (and "off" while believed off) presses nothing.
- If someone used the button by hand, or the machine switched itself off
  (the Breville does after a while), correct the belief on the page; that
  presses nothing. "Press the power button anyway" presses regardless.
- The power button is never pressed while a shot is running or being set up.
- The schedule needs the clock (set by the browser or by NTP) and uses the
  time zone of the browser that saved it. Save it again after a
  daylight-saving change. A time missed by more than three minutes (the
  controller was off) is skipped, not caught up.

The power bot is only connected for a press, like the brew bot around a shot.

How it is built:

- `src/core/PusherDriver.h`: what a driver is, and the capabilities.
- `src/core/SwitchBotDriver.h`, `TuyaFingerbotDriver.h` (with
  `TuyaCrypto.h`: MD5, AES-128-CBC, CRC-16), `MicroBotDriver.h`.
- `src/core/PusherLink.h`: one pusher connection without the radio: the
  session a pusher may need, and the press sequence (unchanged from the
  SwitchBot-only code). One instance per bot.
- `src/core/PowerControl.h`: belief, on / off requests, schedule.
- `src/ble/Ble.cpp`, `BlePusher`: picks the driver whose characteristics the
  connected device has, and moves the bytes.

To add a pusher: write a `PusherDriver` and list it in `PusherLink.h`.

The host tests hold the SwitchBot path to what it was: 40 runs of presses,
answers, lost answers and dropped links go through the old code, kept in the
test as the reference, and through the driver, and the bytes sent, their
timing and every result must be identical. The Tuya driver is tested against
a device model written from the same protocol description, and its MD5, AES
and CRC against the published test vectors.

## 10. Layout

```
src/core/     portable brew logic, no Arduino dependencies (host tested)
  BrewController   state machine
  BbwAlgorithms    cut prediction, offset learning, flow estimate
  ScaleDriver      what a scale driver is; capabilities; table-driven scales
  ScaleLink        one scale connection without the radio, all drivers
  AcaiaDriver      the Lunar 2021+ as a driver
  AcaiaStream      frame decoder for the Lunar 2021+ event stream
  ShotData         the stored readings of a shot and what is derived from them
  GrindAdvisor     grind recommendation from brew time history
  SwitchBotProtocol  Bot command encode / decode
  PusherDriver     what a button pusher driver is; capabilities
  PusherLink       one pusher connection without the radio, all drivers
  PowerControl     the power bot: belief, on / off, schedule
src/ble/      NimBLE: scale link, SwitchBot link, scanner / reconnect
src/ui/       LVGL touch UI
src/net/      Wi-Fi, web page, JSON / CSV
src/storage/  NVS settings, LittleFS shot history and per-shot readings
lib/EspressoScaleProtocol/  scale protocol tables from Open Brew by Weight
docs/RESEARCH.md  protocol findings, scale wake investigation, sources
docs/FEATURES.md  barista-assistant features: what is viable, what is not
emu/              PC emulator of the board, scale, Bot and machine
```

## 11. Licence

AGPL-3.0-or-later. `lib/EspressoScaleProtocol` contains unmodified files from
Open Brew by Weight (Felipe Urzua and contributors, AGPL-3.0), and the cut
prediction and offset learning in `src/core/BbwAlgorithms.h` are adapted from
it. That project derives from Tate Mazer's AcaiaArduinoBLE. If you distribute
this firmware, or a device running it, the AGPL's source-sharing terms apply.
