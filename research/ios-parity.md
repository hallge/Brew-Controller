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
| Recipe card: coffee or recipe name, yield line `in → out 1:r`, grind and advice line, pre-infusion and bloom, brew time | S | `/api/status`, `/api/library`, `/api/grind` | ◐ `BrewTabUITests.testHomeCardShowsAndTheTabsAreThere` |
| Status line: SCALE RESTING / NO SCALE / NO BOT / NO CUP x g / WAKING BOT x g / READY x g | S | `bbw_live` | ◐ `BrewTabUITests.testHomeCardShowsAndTheTabsAreThere` (READY) |
| Scale and bot dots (red, amber, grey, green) with battery %, amber at 15 % or less | S+W | `bbw_live` | ◐ `BrewTabUITests.testHomeCardShowsAndTheTabsAreThere` |
| Hint: connecting, switch on the scale, pair the bot, waiting for weight, place cup, tap to brew | S | `bbw_live` | ◐ hint text or Start key, the board's key wording said for the phone (TAP START); no test of each wording |
| Start (device start), Tare, Done / Clear the warning | S+W | `bbw_command` | ◐ Start, READY: `BrewTabUITests.testFullDemoShotReachesTheResult`; manual-style Start: `BrewGapsUITests.testManualMachineStartsFromTheApp`; Tare built, untested |
| Recipe picker; hidden finished-bag entries except the active one | S+W | `preset_select` | ◐ `BrewTabUITests.testRecipePickerOpensAndSelects` |
| Next recipe (the board's long press) | S | `nextRecipe` | ◐ NEXT RECIPE key built, untested |
| Edit the active entry: target or ratio, dose, grind, brew time, pre-infusion; tap and hold steps; SET advice | S | `/api/screen/do` (applyPresetEdit) | ◐ `BrewTabUITests.testEditPanelStepsAndCancels`; hold to repeat: `BrewGapsUITests.testEditKeyRepeatsWhenHeld`; Brew data is reloaded on appear and before the sheet opens |
| Coffee details: name, roaster, origin, roast level, roast age, and the rest | S | `/api/library` | ◐ `BrewTabUITests.testCoffeeDetailsOpenAndClose` |
| Bag finished, with confirm | S+W | `coffee_finish` / `finishActiveCoffee` | ◐ two-tap confirm built; untested (it finishes the demo bag) |
| Machine power button and state pill (believe on/off) | W | `/api/power` | ◐ built, polled every 5 s while Brew is on show; untested (the demo has no power bot) |
| Grinder card: setting, advice pill, reason, age | W | `/api/grind` | ◐ setting with unit, advice pill, reason, bean age, Details to the Grinder tab; card exists in `BrewGapsUITests.testLastShotCardIsThereWithAnEmptyStateOrASummary` |
| Previous grinder recommendation | W | `/api/contexts?limit=1` | ◐ full web sentence, pill, All shots link; card exists in `BrewGapsUITests.testLastShotCardIsThereWithAnEmptyStateOrASummary` |
| Most recent shot: summary, chart, rate, taste, TDS; to Beanconqueror; share | W | `/api/shot/data`, `/api/shot/rate`, `/api/shot/tds` | ◐ `BrewGapsUITests.testLastShotCardAfterAFullShotHasEveryControl`: empty state, summary against targets, weight/flow/best-rated curves, stars, taste, TDS, vote, preparation, Beanconqueror, Share to Reddit, link to the shot |
| For you chips (Discover on) | W | `/api/taste/recommend`, `/api/subscriptions/recommend` | ◐ taste, subscriptions and next-grind chips that lead on (Discover, Coffees, Grinder); untested |
| Community card | W | `/api/community` | ◐ every state, start tile with why (age and flow shift), good-shot tile, similar-coffee block, Use as starting grind with confirm; follows the Community panel; untested |
| Alerts: manual stop needed; SwitchBot in Switch mode | W | `bbw_live` | ◐ built (manual stop, Switch mode), untested |

### Grind prompts (S: GRINDREC; W: Home banners)

| Feature | Src | Route | Status |
|---|---|---|---|
| NEW GRIND ADVICE: setting, reason (community, similar coffee, first guess), SET / KEEP / NOT NOW | S+W | `/api/grind/recommendation?use=` | ◐ NEW GRIND ADVICE panel built; no test (a prompt is only dismissed by the shared helper) |
| SET THE GRIND: starting value; SET opens the editor on GRIND; NOT NOW until the next wake | S+W | `/api/grind/set` | ◐ built; SET opens the editor on GRIND; untested |
| GRIND REMINDER, once per wake: current → suggested, reason, play note | S | `/api/screen` | ◐ built, untested |

### Live shot (S: BREW)

| Feature | Src | Status |
|---|---|---|
| Ring: grey before the pump runs, red while running with an empty cup, green from the first drop, fills to the target | S | ◐ `BrewTabUITests.testFullDemoShotReachesTheResult` |
| Centre: target (grey) until coffee is seen, then the live weight (green); time (the scale's timer when valid); `/ target g`; flow g/s | S+W | ◐ `BrewTabUITests.testFullDemoShotReachesTheResult` |
| Phase labels: AUTO TARE, PLACE CUP, TOUCH KEY TO BREW / WAKING BOT, PULL THE SHOT, PRE-INFUSION (HOLDING BUTTON), STARTING, BREWING, STOP NOW, BLOOM n s / BLOOM DONE, STOPPING, STOP BY HAND!, CHECKING (PUMP IS OFF), FINALIZING (LEAVE CUP) | S | ◐ words from `/api/screen`; the first labels are seen in `BrewTabUITests.testFullDemoShotReachesTheResult` |
| Cancel before the shot, Stop during it (with confirm, as the BOOT double press) | S+W | ◐ `BrewTabUITests.testStopAsksForConfirmation` |
| Live Activity and lock screen; haptic on stop and done | iOS | ◐ haptic on stop and done built; Live Activity ☐ not done (needs a widget extension target) |

### Result (S: RESULT)

| Feature | Src | Status |
|---|---|---|
| OVER / UNDER / ON TARGET, ON TIME / SLOW / FAST, colour by tolerance | S | ◐ `BrewTabUITests.testFullDemoShotReachesTheResult` |
| Abnormal endings: ENDED EARLY, TIMED OUT, STOPPED AT THE MACHINE, CUP LIFTED, STOPPED AFTER RESTART | S | ◐ words from `/api/screen`; untested |
| Weight (`*` if not settled), total and pour time, flow, drip, auto adjust earlier/later | S | ◐ `BrewTabUITests.testFullDemoShotReachesTheResult` |
| Grind verdict lines (fast/slow → finer/coarser and the amount, ONE-OFF?, CHECK PREPARATION, VARY, OK, SET THE GRIND, not counted and why) | S | ◐ words from `/api/screen`; untested |
| READY; READY + GRINDER; IGNORE SHOT / COUNT FOR GRIND | S | ◐ READY: `BrewTabUITests.testFullDemoShotReachesTheResult`; GRINDER and the vote keys built, untested |
| Auto-acknowledge when a fresh cup goes on the empty scale | S | ◐ built (`auto_ack`), untested |

### Faults (S: FAULT)

| Feature | Src | Status |
|---|---|---|
| Titles and messages: SCALE DISCONNECTED, BOT NOT CONNECTED, SCALE WENT SILENT, START NOT CONFIRMED, STOP NOT CONFIRMED, TARE FAILED, SHOT TIMED OUT, WEIGHT NOT VALID, BLUETOOTH ERROR, STOP MACHINE | S | ◐ one fault in `BrewTabUITests.testFaultShowsItsTitleAndMessage`; the other titles are the firmware words |
| PRESS BUTTON (start or stop not confirmed, bot connected); OK | S | ◐ `BrewTabUITests.testFaultShowsItsTitleAndMessage` (OK; PRESS BUTTON when the bot is connected) |

### Screen behaviour moved to iOS

| Board | iPhone | Status |
|---|---|---|
| Dim after 3 min idle; standby 30 s after the scale goes | Dim overlay; auto-lock allowed 30 s after the scale goes; idle timer off while the scale is connected or a shot is in hand | ◐ overlay and idle-timer rules (`AppLifecycle`, `RootView`); untested |
| Six colour themes (`uiTheme`) | Same palettes (`src/ui/Palette.h`), plus Dynamic Type | ◐ palettes from `bbw_palette` on every Brew view; system fonts for Dynamic Type; untested |
| Rotate screen | ✕ the phone rotates | ✕ |
| Touch key and BOOT button | On-screen keys; Action Button and Shortcuts intent for Start | ◐ on-screen keys built; Action Button and Shortcuts intent ☐ not done |

## 2. Shots (W: `#shots`)

| Feature | Route | Status |
|---|---|---|
| Summary tiles: shots, on time %, typical miss, rated average | `/api/shots` | ◐ `LibraryScreensUITests.testShotsScreenOpens`; "Latest shot" link to the newest shot: `LibraryScreensUITests.testShotsLatestShotAndDetailScreenAfterADemoShot` |
| Filters: coffee; all, stopped by weight, off target, not rated | `/api/shots` | ◐ `LibraryScreensUITests.testShotsScreenOpens` (the two menus) |
| List: time, coffee and recipe, rating, taste, end reason, out, brew time, grind; 20 more | `/api/shots` | ◐ `LibraryScreensUITests.testShotDetailOpensAfterADemoShot` |
| Detail tiles: in the cup, brew time, pour and first drops, grind (change), flow 25/50/75, ended by | `/api/shot/data`, `/api/shot/grind` | ◐ `LibraryScreensUITests.testShotDetailOpensAfterADemoShot` |
| Curve, with the best-rated shot of the same coffee and recipe as a dashed reference | `/api/shot/data` | ◐ `LibraryScreensUITests.testShotDetailOpensAfterADemoShot` (the curve; the reference needs a 4-star shot of the same coffee) |
| Vote Auto / Count / Ignore | `/api/shot/vote` | ◐ `LibraryScreensUITests.testShotDetailOpensAfterADemoShot` (control present) |
| Taste sour / balanced / bitter, stars 1-5, TDS %, prep (dose weighed, purged, uneven) | `/api/shot/rate`, `/tds`, `/prep` | ◐ `LibraryScreensUITests.testShotDetailOpensAfterADemoShot` (stars; taste, TDS and prep fields present) |
| Compare up to 8: flow, line-up | client | ◐ `LibraryScreensUITests.testShotDetailOpensAfterADemoShot` (one shot added) |
| CSV / JSON / one shot's CSV through the share sheet | `/shots.csv`, `/api/shots`, `/shot.csv` | ◐ `LibraryScreensUITests.testShotsExportAndClearHistoryAsk` (menu present; the share sheet is system UI) |
| Clear history, with confirm | `/api/history/clear` | ◐ `LibraryScreensUITests.testShotsExportAndClearHistoryAsk` (the confirmation, cancelled); `ShotsDetailScreen(shotID:)` and Share to Reddit (`CommunityScreen(redditShotID:)`) in shot detail: `LibraryScreensUITests.testShotsLatestShotAndDetailScreenAfterADemoShot`. No touch tooltip on the curve: the web `draw` has none |

## 3. Coffees (W: `#coffees`)

| Feature | Route | Status |
|---|---|---|
| Shelf, search, finished bags folded | `/api/library` | ◐ `LibraryScreensUITests.testCoffeeSearchDirectoryVisualizerAndImportAreReachable, testCoffeeFinishAndDelete`; shared-library hint when empty/off and the Recipes link: `LibraryScreensUITests.testCoffeesLinksToRecipesAndHintsAtTheSharedLibrary` |
| Form: name, roaster, roast level, notes, roast/freezer/thaw dates, origin, region, producer, varietal, process, altitude | `/api/coffee` | ◐ `LibraryScreensUITests.testCoffeesScreenCreatesAndEditsACoffeeAndABag` |
| New bag, bag finished, delete; bags: use, forget, arrived, open | `/api/bag`, `/api/library/do` | ◐ `LibraryScreensUITests.testCoffeesScreenCreatesAndEditsACoffeeAndABag` (new bag), testCoffeeFinishAndDelete (finished, delete); use, forget, arrived and open are untested |
| Ratings: who, stars, 5 attribute sliders, 8 flavour families, list with edit and delete | `/api/coffee/rate`, `/rate/delete` | ◐ `LibraryScreensUITests.testCoffeeRatingBrewWithAndCommunity` (stars, flavour, save, delete; who and sliders untested); "unavailable" with Retry: `LibraryScreensUITests.testCoffeeRatingsSectionIsNeverMissing` |
| Brew it with (recipe ticks) | `preset_add` / `preset_delete` | ◐ `LibraryScreensUITests.testCoffeeRatingBrewWithAndCommunity` |
| Share this coffee | `/api/share/coffee` | ◐ `LibraryScreensUITests.testCoffeeRatingBrewWithAndCommunity` (the state is shown; Send needs sharing on) |
| In the shared library: Add | `/api/coffee/adopt` | ◐ no UI test: the demo has an empty shared library |
| Coffee directory: search, origin, process and roast filters, pages, add | `/api/directory` | ◐ `LibraryScreensUITests.testCoffeeSearchDirectoryVisualizerAndImportAreReachable` (opens, empty state; results and Add need a read directory) |
| Community dial-in; use as the starting grind | `/api/community`, `preset_grind` | ◐ `LibraryScreensUITests.testCoffeeRatingBrewWithAndCommunity` (the card; "use" needs shared data) |
| Visualizer lookup (the app calls visualizer.coffee) | external | ◐ `LibraryScreensUITests.testCoffeeSearchDirectoryVisualizerAndImportAreReachable` (sheet opens; the lookup needs the network) |
| Beanconqueror backup import (Files) and shot export (`beanconqueror://` link) | client | ◐ `LibraryScreensUITests.testCoffeeSearchDirectoryVisualizerAndImportAreReachable` (button; the Files picker is system UI), testShotDetailOpensAfterADemoShot (export button); parsing and the link are in LibraryScreensModelTests |
| Scan a bag label (VisionKit) | iOS extra, later | ☐ |

## 4. Recipes (W: `#recipes`)

| Feature | Route | Status |
|---|---|---|
| List, new, duplicate, delete | `/api/recipe`, `recipe_delete` | ◐ `LibraryScreensUITests.testRecipesScreenCreatesAndDuplicatesARecipe, testRecipeFormFieldsUsedWithAndDelete` |
| Dose; yield by target or ratio; target brew time | `/api/recipe` | ◐ `LibraryScreensUITests.testRecipeFormFieldsUsedWithAndDelete, testRecipesScreenCreatesAndDuplicatesARecipe` |
| Pre-infusion (machine, or hold 0.3-30 s); bloom 0-45 s | `/api/recipe` | ◐ `LibraryScreensUITests.testRecipeFormFieldsUsedWithAndDelete` |
| If the target is never reached: who stops, stop after, starting offset | `/api/recipe` | ◐ `LibraryScreensUITests.testRecipeFormFieldsUsedWithAndDelete` |
| Used with: coffee, grind, learned offset, in use, reset offset, take off, list by itself | `/api/offset/reset`, `preset_*` | ◐ `LibraryScreensUITests.testRecipeFormFieldsUsedWithAndDelete`; "Add to a coffee" link: `LibraryScreensUITests.testRecipeLinksBackToCoffees` |
| Share to the recipe book | `/api/recipes/share` | ◐ `LibraryScreensUITests.testRecipeFormFieldsUsedWithAndDelete` (disabled while the library is not linked) |

## 5. Grinder (W: `#grinder`)

| Feature | Route | Status |
|---|---|---|
| Setting now and measured by the add-on; I moved it to | `/api/grind`, `/api/grind/set` | ◐ `testSetTheGrinderSetting` |
| Advice pill, why, age, rate; apply suggested | `/api/grind` | ◐ testSetTheGrinderSetting (pill); apply: testOpenEachScreen |
| Brew time against grind (scatter, all coffees) | `/api/shots` | ◐ `testOpenEachScreen` |
| Grind shot by shot: a line per coffee, dots by time, rings for 4-5 stars, new-bag lines; tap opens the shot; legend per coffee, current coffee larger | `/api/shots` | ◐ testOpenEachScreen (a tap shows the shot's summary with an "Open the shot" link to `ShotsDetailScreen(shotID:)`) |
| Advice settings: tolerance, window, rated target, confidence, room, µm/s guess, max step, time basis | `/api/settings` | ◐ `testOpenEachScreen` |
| Upkeep: clean; zeroed today | `/api/maintenance`, `/api/grinder` | ◐ `testOpenEachScreen` |
| My grinder: model by maker (built-in, own, shared) in a searchable list, name, adjusts by, divisor, unit, range, covers, zeroed date, micro ring | `/api/grinder` | ◐ `testGrinderModelPickerIsASearchableList` |
| Second grinder: add, use instead, remove; send to the library | `grinder_switch`, `grinder_remove_other`, `/api/share/grinder` | ◐ `testOpenEachScreen` |
| Shared grinder list | `/api/community/grinders` | ◐ `testOpenEachScreen` |

## 6. Machine (W: `#machine`)

| Feature | Route | Status |
|---|---|---|
| Power: state, last press, switch on/off, not right? on/off, press anyway | `/api/power` | ◐ testToggleAMachineOption (screen); press: testOpenEachScreen |
| Schedule: 6 rows (in use, on/off, time, weekdays); time zone | `/api/power` schedule | ◐ testOpenEachScreen (the phone's clock and time-zone rule are sent with `POST /api/time` when the controller's differ; a note says the zone in use; the schedule loads only once `/api/power` answered) |
| Options: keep power bot connected, off with the scale, scale sleeps after 0-60 min | `/api/settings` | ◐ `testToggleAMachineOption` |
| Upkeep: backflush and descale, remind after, done today | `/api/maintenance` | ◐ `testOpenEachScreen` |
| Make and model: built-in, shared, typed; maker, model, control, style (5), manual start, pre-infusion flags, power toggle, self-stop, auto-off | `/api/machine` | ◐ `testOpenEachScreen` |
| Send to the shared library | `/api/share/machine` | ◐ `testOpenEachScreen` |
| Power events log | `/api/power/events` | ◐ `testOpenEachScreen` |

## 7. Insights (W: `#insights`)

| Feature | Route | Status |
|---|---|---|
| Taste radars (5 values, 8 families), drinker switch, liked best | `/api/taste/profile` | ◐ `DiscoverScreensUITests.testOpenEachScreen` |
| Roast level against grind | `/api/shots` | ◐ `DiscoverScreensUITests.testInsightsChartsAfterADemoShot` (empty note only: the demo shot has no roast level or grind); tap tooltip, second tap opens the shot (untested) |
| Coffee age against brew time | `/api/shots` | ◐ `DiscoverScreensUITests.testInsightsChartsAfterADemoShot` (empty note only: no roast date) |
| Brewing calendar: shots, coffee & recipe, drift, time of day | `/api/shots`, `/api/power/events` | ◐ `DiscoverScreensUITests.testInsightsChartsAfterADemoShot` (all 4 modes); tap a day or a dot, second tap opens the shot (untested) |
| Dial-in shots per bag | `/api/shots` | ◐ `DiscoverScreensUITests.testInsightsChartsAfterADemoShot`; tap tooltip (untested) |
| Grind advice against target | `/api/grind/accuracy` | ◐ `DiscoverScreensUITests.testInsightsChartsAfterADemoShot` (empty note only: no advised change yet) |

## 8. Subscriptions (W: `#subscriptions`)

| Feature | Route | Status |
|---|---|---|
| Tiles: per month, next up, stock | `/api/subscriptions` | ◐ `DiscoverScreensUITests.testCreateEditAndMarkASubscriptionArrived` |
| Reminders: due soon, running out | `/api/subscriptions` | ◐ `DiscoverScreensModelTests.testSubscriptionList` (the logic; no UI test: needs a due date in the demo) |
| Cards: manage, arrived, skip one, edit | `/arrived`, `/skip` | ◐ `DiscoverScreensUITests.testCreateEditAndMarkASubscriptionArrived` (arrived, edit; skip and manage are not tapped) |
| Form: link and read the page, name, kind, status, every, next, price, supplies, manage link, library details, note, look | `/api/subscriptions`, `/preview` | ◐ `DiscoverScreensUITests.testCreateEditAndMarkASubscriptionArrived` (read the page needs the network: not run) |
| Reviews: who, stars, 6 aspects, note; delete | `/review`, `/review/delete` | ◐ `DiscoverScreensModelTests.testSubscriptionReviewsPreviewAndLibrary` (model only; no UI test) |
| Arrival dialog: pick or make the coffee, roast date, weight | `/arrived`, `/api/coffee`, `/api/bag` | ◐ none yet (the UI test arrives a supplies subscription, which skips the dialog; the roast date picker is always shown now) |
| Shared library: search, add; send mine | `/api/subscriptions`, `/share` | ◐ `DiscoverScreensUITests.testOpenEachScreen` (empty state only: no relay in demo) |
| Update from the shops (Chrome helper) | ✕ Chrome-only (a browser extension); "Read the page" on the form covers it, and the app has no "Apply what is ticked" import | ✕ |

## 9. Discover and Community (W: `#discover`, `#community`)

| Feature | Route | Status |
|---|---|---|
| Who (household or a named drinker) | client | ◐ none yet (needs named drinkers on) |
| Coffees you might like; buy again | `/api/taste/recommend` | ◐ `DiscoverScreensUITests.testOpenEachScreen` (empty state: fewer than 3 rated) |
| Subscriptions you might like; country; what matters | `/api/subscriptions/recommend`, `/prefs` | ◐ `DiscoverScreensUITests.testOpenEachScreen` (card opens; no relay, so no recommendations); country saves on focus loss |
| Coffee library: published (add); waiting for review (approve or decline with a note) | `/api/community/coffees`, `/api/library/pending`, `/api/library/review` | ◐ `DiscoverScreensUITests.testOpenEachScreen` (empty state; filled lists only in the model tests) |
| Recipe book: published (make it a recipe); waiting for review | `/api/community/recipes` | ◐ `DiscoverScreensModelTests.testLibraryOfflineAndPublished` (model only); "Make it a recipe" form has ratio, hold, bloom, time limit and offset |
| Community feed with topics; suggest a link (read the page, kind, title, topics, why); yours (delete) | `/api/links`, `/links/suggest`, `/links/delete` | ◐ `DiscoverScreensUITests.testCommunityFeedIsEmptyWithoutTheLibrary` (empty state) |
| Share a shot to Reddit: shot, photo, subreddit, title, 1200x1200 image, text, through the share sheet | client | ◐ `DiscoverScreensModelTests.testRedditText` (the text; the ImageRenderer picture and the share sheet are untested) |
| Trending recipes; for what you're brewing; shared grinders; what you share | `/api/trending`, `/api/share` | ◐ `DiscoverScreensUITests.testCommunityFeedIsEmptyWithoutTheLibrary` (empty states); `DiscoverScreensModelTests.testForWhatYouAreBrewing` (age/flow wording, similar and range guess); text links navigate; Reddit takes a shot id |

## 10. Devices (W: Setup → Devices; S: Settings)

| Feature | Route | Status |
|---|---|---|
| Summary: scale, brew, power and stop bot (signal, battery, firmware, problems); scale link stats | `/api/devices` | ◐ `testDevicesShowThePairedScaleAndBot` |
| Seen list with role buttons (Use, Brew, Power, Stop); refresh | `/api/pair`, `/api/devices` | ◐ `testDevicesShowThePairedScaleAndBot` |
| Pair the nearest bot; forget the scale | S | `/api/screen/do` | ◐ `testOpenEachScreen` |
| Test press brew or power; swap brew and power; forget any | `/api/bot/test`, `/api/pair` | ◐ `testOpenEachScreen` |
| Pusher keys: role; Tuya uuid, id, local key, model; MicroBot token; SwitchBot password | `/api/pusher`, `/api/net` | ◐ `testOpenEachScreen` |
| AccessorySetupKit pairing sheet | iOS | ☐ |

## 11. Setup (W: `#setup`; S: Settings)

| Feature | Route | Status |
|---|---|---|
| Appearance: 6 themes | `/api/settings uiTheme` | ◐ `testChangeTheme` |
| Drinkers: named on/off, 8 names | `/api/settings`, `/api/drinkers` | ◐ `testOpenEachScreen` |
| Brew: cup workflow, cup required, after tare, minimum cup; learning on/off, factor, fixed/adaptive, scale timer; details (min weight, timeouts, drip watch, no-stop-before, no-flow, press style, durations, bot hold) | `/api/settings` | ◐ `testOpenEachScreen` |
| Offset learning toggle (the board's OFFSET: LEARNING/FIXED) | S+W | `/api/settings` | ◐ `testOpenEachScreen` |
| Home Assistant: address, token, entities, webhook, MQTT; test the reading; says what can't work on iPhone | `/api/homeassistant`, `/api/ambient/test` | ◐ testOpenEachScreen (the screen says plainly that Home Assistant cannot poll the phone) |
| Backup (history, secrets) to Files; restore from Files, summary, apply, tap to restart | `/api/backup`, `/api/restore`, `/restore/apply` | ◐ testOpenEachScreen (backup through the share sheet, restore through Files; restore ends on "Restored, tap to restart") |
| Shared library: state, key id, link, own repo and token, share on, what it shares, queue, send again | `/api/share*` | ◐ `testOpenEachScreen` |
| Diagnostics: on/off, status, last crash, preview, send now; report a problem; relay address | `/api/diagnostics*`, `/api/crash` | ◐ `testOpenEachScreen` |
| Add-ons: on, fields, map, read now, forget (only when the controller lists any) | `/api/addons/*` | ◐ testOpenEachScreen (Setup › Add-ons appears only with add-ons; the add-on's own steering/comparison card, charts and module delivery are not drawn) |
| Getting started: 8 steps and "when you want more"; each step and card opens its screen | `/api/start` | ◐ `testGettingStartedStepsOpenTheirScreens` (7 steps: the Wi-Fi step is the board's; `SetupStartedScreen()` is the entry for Brew and More) |
| Web page panels (which sections show) | `webPanels` | ◐ testOpenEachScreen ("Show in More" toggles; the shell has to read `webPanels`) |
| App and controller firmware versions; show the first run again; read the safety notice again | `/api/update`, `Onboarding.reset()` | ◐ `testSetupShowsTheVersionsAndFirstRunRow` |
| Wi-Fi, access point, network restart | ✕ the board's Wi-Fi | ✕ |
| Firmware update, slots, rollback | ✕ App Store updates | ✕ |
| Controller screen upside down | ✕ | ✕ |

## 12. Logs (W: `#logs`)

| Feature | Route | Status |
|---|---|---|
| Level 0-3, keep on flash | `/api/log/config` | ◐ `testChangeTheLogLevel` |
| Live tail: follow, filter, clear view | `/api/log?from=` | ◐ `testOpenEachScreen` |
| Download through the share sheet; clear | `/log.txt`, `/api/log/clear` | ◐ `testOpenEachScreen` |

## 13. App-level

| Feature | Status |
|---|---|
| Demo mode (simulated Lunar, SwitchBot, Breville), switchable in Setup | ☐ |
| First run: safety notice, demo or real, Bluetooth permission, diagnostics consent | ☐ |
| Board → app: open a `.bbwbak` from Files or AirDrop, then pair again where needed | ☐ |
| Notifications: shot finished, upkeep due, beans running low, subscription arriving | ☐ |
| iPad layout; VoiceOver on the shot screen; Dynamic Type | ☐ |
| `hold_button` machines refused until measured (`ios-app-feasibility.md` §3.1) | ☐ |
