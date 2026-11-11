# iPhone app parity checklist

Dated 2026-11-10. This is the acceptance list for the iPhone app. The app runs the board's own
controller code, so the behaviour matches the board. The open work is a native screen for
everything the board's screen and web page offer. A row is done only when its native screen exists
**and** a UI test in demo mode covers it.

The design is in `research/ios-app-feasibility.md`. The host plan and the full inventories (file and
line for every item) are in the coordinator's notes, `ios-parity/`.

**Key**

- Source:
  - **S**: the board's screen (`src/ui/Ui.cpp`).
  - **W**: the web page (`src/net/WebPage.h`).
  - **S+W**: both.
- Status: ☐ not started · ◐ screen built · ☑ screen built and UI-tested · ✕ deliberately left out
  (the reason is given).

## 1. Brew tab: the board's screen, and the web page's Home

### Home (S: HOME panel; W: `#home`)

| Feature | Src | Route / function | Status |
|---|---|---|---|
| Recipe card: coffee or recipe name, yield line `in → out 1:r`, grind and advice line, pre-infusion and bloom, brew time | S | `/api/status`, `/api/library`, `/api/grind` | ☐ |
| Status line: SCALE RESTING / NO SCALE / NO BOT / NO CUP x g / WAKING BOT x g / READY x g | S | `bbw_live` | ☐ |
| Scale and bot dots (red, amber, grey, green) with battery %, amber at 15 % or less | S+W | `bbw_live` | ☐ |
| Hint: connecting, switch on the scale, pair the bot, waiting for weight, place cup, tap to brew | S | `bbw_live` | ☐ |
| Start (device start), Tare, Done / Clear the warning | S+W | `bbw_command` | ☐ |
| Recipe picker; hidden finished-bag entries except the active one | S+W | `preset_select` | ☐ |
| Next recipe (the board's long press) | S | `nextRecipe` | ☐ |
| Edit the active entry: target or ratio, dose, grind, brew time, pre-infusion; tap and hold steps; SET advice | S | `/api/screen/do` (applyPresetEdit) | ☐ |
| Coffee details: name, roaster, origin, roast level, roast age, and the rest | S | `/api/library` | ☐ |
| Bag finished, with confirm | S+W | `coffee_finish` / `finishActiveCoffee` | ☐ |
| Machine power button and state pill (believe on/off) | W | `/api/power` | ☐ |
| Grinder card: setting, advice pill, reason, age | W | `/api/grind` | ☐ |
| Previous grinder recommendation | W | `/api/contexts?limit=1` | ☐ |
| Most recent shot: summary, chart, rate, taste, TDS; to Beanconqueror; share | W | `/api/shot/data`, `/api/shot/rate`, `/api/shot/tds` | ☐ |
| For you chips (Discover on) | W | `/api/taste/recommend`, `/api/subscriptions/recommend` | ☐ |
| Community card | W | `/api/community` | ☐ |
| Alerts: manual stop needed; SwitchBot in Switch mode | W | `bbw_live` | ☐ |

### Grind prompts (S: GRINDREC; W: Home banners)

| Feature | Src | Route | Status |
|---|---|---|---|
| NEW GRIND ADVICE: setting, reason (community, similar coffee, first guess), SET / KEEP / NOT NOW | S+W | `/api/grind/recommendation?use=` | ☐ |
| SET THE GRIND: starting value; SET opens the editor on GRIND; NOT NOW until the next wake | S+W | `/api/grind/set` | ☐ |
| GRIND REMINDER, once per wake: current → suggested, reason, play note | S | `/api/screen` | ☐ |

### Live shot (S: BREW)

| Feature | Src | Status |
|---|---|---|
| Ring: grey before the pump runs, red while running with an empty cup, green from the first drop, fills to the target | S | ☐ |
| Centre: target (grey) until coffee is seen, then the live weight (green); time (the scale's timer when valid); `/ target g`; flow g/s | S+W | ☐ |
| Phase labels: AUTO TARE, PLACE CUP, TOUCH KEY TO BREW / WAKING BOT, PULL THE SHOT, PRE-INFUSION (HOLDING BUTTON), STARTING, BREWING, STOP NOW, BLOOM n s / BLOOM DONE, STOPPING, STOP BY HAND!, CHECKING (PUMP IS OFF), FINALIZING (LEAVE CUP) | S | ☐ |
| Cancel before the shot, Stop during it (with confirm, as the BOOT double press) | S+W | ☐ |
| Live Activity and lock screen; haptic on stop and done | iOS | ☐ |

### Result (S: RESULT)

| Feature | Src | Status |
|---|---|---|
| OVER / UNDER / ON TARGET, ON TIME / SLOW / FAST, colour by tolerance | S | ☐ |
| Abnormal endings: ENDED EARLY, TIMED OUT, STOPPED AT THE MACHINE, CUP LIFTED, STOPPED AFTER RESTART | S | ☐ |
| Weight (`*` if not settled), total and pour time, flow, drip, auto adjust earlier/later | S | ☐ |
| Grind verdict lines (fast/slow → finer/coarser and the amount, ONE-OFF?, CHECK PREPARATION, VARY, OK, SET THE GRIND, not counted and why) | S | ☐ |
| READY; READY + GRINDER; IGNORE SHOT / COUNT FOR GRIND | S | ☐ |
| Auto-acknowledge when a fresh cup goes on the empty scale | S | ☐ |

### Faults (S: FAULT)

| Feature | Src | Status |
|---|---|---|
| Titles and messages: SCALE DISCONNECTED, BOT NOT CONNECTED, SCALE WENT SILENT, START NOT CONFIRMED, STOP NOT CONFIRMED, TARE FAILED, SHOT TIMED OUT, WEIGHT NOT VALID, BLUETOOTH ERROR, STOP MACHINE | S | ☐ |
| PRESS BUTTON (start or stop not confirmed, bot connected); OK | S | ☐ |

### Screen behaviour moved to iOS

| Board | iPhone | Status |
|---|---|---|
| Dim after 3 min idle; standby 30 s after the scale goes | Dim overlay; auto-lock allowed 30 s after the scale goes; idle timer off while the scale is connected or a shot is in hand | ☐ |
| Six colour themes (`uiTheme`) | Same palettes (`src/ui/Palette.h`), plus Dynamic Type | ☐ |
| Rotate screen | ✕ the phone rotates | ✕ |
| Touch key and BOOT button | On-screen keys; Action Button and Shortcuts intent for Start | ☐ |

## 2. Shots (W: `#shots`)

| Feature | Route | Status |
|---|---|---|
| Summary tiles: shots, on time %, typical miss, rated average | `/api/shots` | ☐ |
| Filters: coffee; all, stopped by weight, off target, not rated | `/api/shots` | ☐ |
| List: time, coffee and recipe, rating, taste, end reason, out, brew time, grind; 20 more | `/api/shots` | ☐ |
| Detail tiles: in the cup, brew time, pour and first drops, grind (change), flow 25/50/75, ended by | `/api/shot/data`, `/api/shot/grind` | ☐ |
| Curve, with the best-rated shot of the same coffee and recipe as a dashed reference | `/api/shot/data` | ☐ |
| Vote Auto / Count / Ignore | `/api/shot/vote` | ☐ |
| Taste sour / balanced / bitter, stars 1-5, TDS %, prep (dose weighed, purged, uneven) | `/api/shot/rate`, `/tds`, `/prep` | ☐ |
| Compare up to 8: flow, line-up | client | ☐ |
| CSV / JSON / one shot's CSV through the share sheet | `/shots.csv`, `/api/shots`, `/shot.csv` | ☐ |
| Clear history, with confirm | `/api/history/clear` | ☐ |

## 3. Coffees (W: `#coffees`)

| Feature | Route | Status |
|---|---|---|
| Shelf, search, finished bags folded | `/api/library` | ☐ |
| Form: name, roaster, roast level, notes, roast/freezer/thaw dates, origin, region, producer, varietal, process, altitude | `/api/coffee` | ☐ |
| New bag, bag finished, delete; bags: use, forget, arrived, open | `/api/bag`, `/api/library/do` | ☐ |
| Ratings: who, stars, 5 attribute sliders, 8 flavour families, list with edit and delete | `/api/coffee/rate`, `/rate/delete` | ☐ |
| Brew it with (recipe ticks) | `preset_add` / `preset_delete` | ☐ |
| Share this coffee | `/api/share/coffee` | ☐ |
| In the shared library: Add | `/api/coffee/adopt` | ☐ |
| Coffee directory: search, origin, process and roast filters, pages, add | `/api/directory` | ☐ |
| Community dial-in; use as the starting grind | `/api/community`, `preset_grind` | ☐ |
| Visualizer lookup (the app calls visualizer.coffee) | external | ☐ |
| Beanconqueror backup import (Files) and shot export (`beanconqueror://` link) | client | ☐ |
| Scan a bag label (VisionKit) | iOS extra, later | ☐ |

## 4. Recipes (W: `#recipes`)

| Feature | Route | Status |
|---|---|---|
| List, new, duplicate, delete | `/api/recipe`, `recipe_delete` | ☐ |
| Dose; yield by target or ratio; target brew time | `/api/recipe` | ☐ |
| Pre-infusion (machine, or hold 0.3-30 s); bloom 0-45 s | `/api/recipe` | ☐ |
| If the target is never reached: who stops, stop after, starting offset | `/api/recipe` | ☐ |
| Used with: coffee, grind, learned offset, in use, reset offset, take off, list by itself | `/api/offset/reset`, `preset_*` | ☐ |
| Share to the recipe book | `/api/recipes/share` | ☐ |

## 5. Grinder (W: `#grinder`)

| Feature | Route | Status |
|---|---|---|
| Setting now and measured by the add-on; I moved it to | `/api/grind`, `/api/grind/set` | ☐ |
| Advice pill, why, age, rate; apply suggested | `/api/grind` | ☐ |
| Brew time against grind (scatter, all coffees) | `/api/shots` | ☐ |
| Grind shot by shot: a line per coffee, dots by time, rings for 4-5 stars, new-bag lines; tap opens the shot | `/api/shots` | ☐ |
| Advice settings: tolerance, window, rated target, confidence, room, µm/s guess, max step, time basis | `/api/settings` | ☐ |
| Upkeep: clean; zeroed today | `/api/maintenance`, `/api/grinder` | ☐ |
| My grinder: model by maker (built-in, own, shared), name, adjusts by, divisor, unit, range, covers, zeroed date, micro ring | `/api/grinder` | ☐ |
| Second grinder: add, use instead, remove; send to the library | `grinder_switch`, `grinder_remove_other`, `/api/share/grinder` | ☐ |
| Shared grinder list | `/api/community/grinders` | ☐ |

## 6. Machine (W: `#machine`)

| Feature | Route | Status |
|---|---|---|
| Power: state, last press, switch on/off, not right? on/off, press anyway | `/api/power` | ☐ |
| Schedule: 6 rows (in use, on/off, time, weekdays); time zone | `/api/power` schedule | ☐ |
| Options: keep power bot connected, off with the scale, scale sleeps after 0-60 min | `/api/settings` | ☐ |
| Upkeep: backflush and descale, remind after, done today | `/api/maintenance` | ☐ |
| Make and model: built-in, shared, typed; maker, model, control, style (5), manual start, pre-infusion flags, power toggle, self-stop, auto-off | `/api/machine` | ☐ |
| Send to the shared library | `/api/share/machine` | ☐ |
| Power events log | `/api/power/events` | ☐ |

## 7. Insights (W: `#insights`)

| Feature | Route | Status |
|---|---|---|
| Taste radars (5 values, 8 families), drinker switch, liked best | `/api/taste/profile` | ☐ |
| Roast level against grind | `/api/shots` | ☐ |
| Coffee age against brew time | `/api/shots` | ☐ |
| Brewing calendar: shots, coffee & recipe, drift, time of day | `/api/shots`, `/api/power/events` | ☐ |
| Dial-in shots per bag | `/api/shots` | ☐ |
| Grind advice against target | `/api/grind/accuracy` | ☐ |

## 8. Subscriptions (W: `#subscriptions`)

| Feature | Route | Status |
|---|---|---|
| Tiles: per month, next up, stock | `/api/subscriptions` | ☐ |
| Reminders: due soon, running out | `/api/subscriptions` | ☐ |
| Cards: manage, arrived, skip one, edit | `/arrived`, `/skip` | ☐ |
| Form: link and read the page, name, kind, status, every, next, price, supplies, manage link, library details, note, look | `/api/subscriptions`, `/preview` | ☐ |
| Reviews: who, stars, 6 aspects, note; delete | `/review`, `/review/delete` | ☐ |
| Arrival dialog: pick or make the coffee, roast date, weight | `/arrived`, `/api/coffee`, `/api/bag` | ☐ |
| Shared library: search, add; send mine | `/api/subscriptions`, `/share` | ☐ |
| Update from the shops (Chrome helper) | ✕ Chrome-only; "Read the page" covers it | ✕ |

## 9. Discover and Community (W: `#discover`, `#community`)

| Feature | Route | Status |
|---|---|---|
| Who (household or a named drinker) | client | ☐ |
| Coffees you might like; buy again | `/api/taste/recommend` | ☐ |
| Subscriptions you might like; country; what matters | `/api/subscriptions/recommend`, `/prefs` | ☐ |
| Coffee library: published (add); waiting for review (approve or decline with a note) | `/api/community/coffees`, `/api/library/pending`, `/api/library/review` | ☐ |
| Recipe book: published (make it a recipe); waiting for review | `/api/community/recipes` | ☐ |
| Community feed with topics; suggest a link (read the page, kind, title, topics, why); yours (delete) | `/api/links`, `/links/suggest`, `/links/delete` | ☐ |
| Share a shot to Reddit: shot, photo, subreddit, title, 1200x1200 image, text, through the share sheet | client | ☐ |
| Trending recipes; for what you're brewing; shared grinders; what you share | `/api/trending`, `/api/share` | ☐ |

## 10. Devices (W: Setup → Devices; S: Settings)

| Feature | Route | Status |
|---|---|---|
| Summary: scale, brew, power and stop bot (signal, battery, firmware, problems); scale link stats | `/api/devices` | ☐ |
| Seen list with role buttons (Use, Brew, Power, Stop); refresh | `/api/pair`, `/api/devices` | ☐ |
| Pair the nearest bot; forget the scale | S | `/api/screen/do` | ☐ |
| Test press brew or power; swap brew and power; forget any | `/api/bot/test`, `/api/pair` | ☐ |
| Pusher keys: role; Tuya uuid, id, local key, model; MicroBot token; SwitchBot password | `/api/pusher`, `/api/net` | ☐ |
| AccessorySetupKit pairing sheet | iOS | ☐ |

## 11. Setup (W: `#setup`; S: Settings)

| Feature | Route | Status |
|---|---|---|
| Appearance: 6 themes | `/api/settings uiTheme` | ☐ |
| Drinkers: named on/off, 8 names | `/api/settings`, `/api/drinkers` | ☐ |
| Brew: cup workflow, cup required, after tare, minimum cup; learning on/off, factor, fixed/adaptive, scale timer; details (min weight, timeouts, drip watch, no-stop-before, no-flow, press style, durations, bot hold) | `/api/settings` | ☐ |
| Offset learning toggle (the board's OFFSET: LEARNING/FIXED) | S+W | `/api/settings` | ☐ |
| Home Assistant: address, token, entities, webhook, MQTT; test the reading; says what can't work on iPhone | `/api/homeassistant`, `/api/ambient/test` | ☐ |
| Backup (history, secrets) to Files; restore from Files, summary, apply, tap to restart | `/api/backup`, `/api/restore`, `/restore/apply` | ☐ |
| Shared library: state, key id, link, own repo and token, share on, what it shares, queue, send again | `/api/share*` | ☐ |
| Diagnostics: on/off, status, last crash, preview, send now; report a problem; relay address | `/api/diagnostics*`, `/api/crash` | ☐ |
| Add-ons: on, fields, map, read now, forget (only when built in) | `/api/addons/*` | ☐ |
| Getting started: 8 steps and "when you want more" | `/api/start` | ☐ |
| Web page panels (which sections show) | `webPanels` | ☐ (as "show in More") |
| Wi-Fi, access point, network restart | ✕ the board's Wi-Fi | ✕ |
| Firmware update, slots, rollback | ✕ App Store updates | ✕ |
| Controller screen upside down | ✕ | ✕ |

## 12. Logs (W: `#logs`)

| Feature | Route | Status |
|---|---|---|
| Level 0-3, keep on flash | `/api/log/config` | ☐ |
| Live tail: follow, filter, clear view | `/api/log?from=` | ☐ |
| Download through the share sheet; clear | `/log.txt`, `/api/log/clear` | ☐ |

## 13. App-level

| Feature | Status |
|---|---|
| Demo mode (simulated Lunar, SwitchBot, Breville), switchable in Setup | ☐ |
| First run: safety notice, demo or real, Bluetooth permission, diagnostics consent | ☐ |
| Board → app: open a `.bbwbak` from Files or AirDrop, then pair again where needed | ☐ |
| Notifications: shot finished, upkeep due, beans running low, subscription arriving | ☐ |
| iPad layout; VoiceOver on the shot screen; Dynamic Type | ☐ |
| `hold_button` machines refused until measured (`ios-app-feasibility.md` §3.1) | ☐ |
