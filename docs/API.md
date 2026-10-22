# The controller's web API

The page the controller serves is a client of this API, and so can anything else on the same network. It is **not
authenticated**: anyone who can reach the controller can read it and change settings. Use it on a network you trust. Remote
start is off unless the owner turns it on in Setup (`web_start` in the status).

`GET /api/status` carries `api_version` (now **1**) and `capabilities`, the groups of endpoints this firmware has, so a
client does not have to guess from the firmware version:

`status_v1`, `shots_v1`, `shot_data_v1`, `shot_csv_v1`, `grind_advice_v1`, `library_v1`, `bags_v1`, `maintenance_v1`,
`ratings_refinement_v1`, `tds_v1`

Rules: new fields and new endpoints do not change `api_version`; removing or re-typing a field does. Clients ignore fields they
do not know. All bodies are JSON unless a query string is shown; errors are `{"ok":false,"error":"..."}` with a status
code (400 bad value, 404 not found, 409 not now: a shot is running, or the change is refused).

## Routes

The list below is the route table of `src/net/Web.cpp` as of 13 Oct 2026. Only endpoints added or changed in that round are
described; the rest are as the page uses them.

| Route | | What it is |
| --- | --- | --- |
| `/api/status` | GET | live state: `state`, `weight_g`, `flow_g_s`, `shot_time_s`, devices, `scale_resting` (an idle scale let go so it can switch itself off) and `scale_start_pending` (a Start waiting for it to come back), `api_version`, `capabilities`, `maintenance_due` |
| `/api/shots` | GET | the last shots (newest first), each with the fields of the CSV, plus `tds_pct`, `extraction_pct` when measured |
| `/api/contexts` | GET | `?limit=` (default 100): the circumstances of the newest shots, joined to `/api/shots` by `shot_id`: bean age at the shot (effective, calendar, frozen days), time since the previous shot, shots since the grinder was cleaned, the start suggestions on offer (community and own-coffee, with ranges and basis), the advice given after the shot, curve quality (sample interval, weight step, points), the advisor settings in force, engine version, reserved owner fields. Recorded only; capability `shot_context_v1` (`core/ShotContext.h`, `research/dial-in-engine.md`) |
| `/api/shot/prep` | POST | `?id=&dose=&purged=0|1&uneven=0|1`: what the owner says about a shot's preparation afterwards: the dose they weighed (5-40 g, 0 = not weighed), whether the grinder was purged first, whether the flow looked uneven. Recorded only (the shot's context); an uneven shot that should not steer the grind is ruled out with `/api/shot/vote` (ignore). `/api/shots` then carries `dose_weighed_g`, `purged`, `uneven` for shots that have them. Capability `shot_prep_v1` |
| `/api/grind` (fields added 16 Oct 2026) | GET | `sensitivity_source` (`guess`, `grinder`: from this grinder's other coffees, `measured`: this coffee's shots); `has_number` (the advice can name a setting); `data_weight` (0-1, share of the sensitivity resting on this coffee's shots); `bracketed`, `between_steps` (kept short of, or at the closer of two, settings either side of the target); `out_of_range` (from a shot too long or short to count: the largest step); `grinder_elasticity`, `_se`, `grinder_shots`, `grinder_coffees` when known. Learning the grinder is part of the advice itself since 17 Oct 2026 (no setting); capability `learn_grinder_v1`. See `research/grind-logic-review.md` |
| `/api/shot/data?id=` | GET | one shot's readings: `time_s[]`, `weight_g[]`, `flow_g_s[]`, `scale_timer_s[]`, and a summary |
| `/shots.csv`, `/shot.csv?id=` | GET | CSV exports; `shots.csv` now ends with `tds_pct,extraction_pct` |
| `/api/grind` | GET | grind advice, see below |
| `/api/grind/set?um=` | POST | set the active preset's setting, in **stored steps** (1 to 2000) |
| `/api/library` | GET | coffees, recipes, presets, grinders, **bags**; each coffee's `finished` and each preset's `hidden` (see Bag finished) |
| `/api/coffee` | POST | save a coffee (edits correct the bag in use) |
| `/api/bag` | POST | new bag, or correct one; see below |
| `/api/library/do?what=&a=&b=` | POST | small changes by id; adds `bag_use` (a = coffee, b = bag), `bag_delete` (a = bag), `bag_open` (a = an unopened bag: in use from today) and `coffee_finish` (a = coffee) |
| `/api/grinder` | POST | save the grinder; takes `divisor` and `subSteps` (how a setting is read) |
| `/api/shot/rate?id=&rating=&taste=` | POST | rating 0 to 5, taste 0 to 3 |
| `/api/shot/tds?id=&tds=` | POST | percent TDS from a refractometer, 0.5 to 25, or 0 to clear |
| `/api/maintenance` | GET, POST | upkeep counters, see below |
| `/api/community`, `/api/community/*` | GET | the shared library's figures for a coffee; the owner's own similar-coffee start (`similar`) |
| `/api/settings` | GET, POST | settings, including `grindRatedTarget` and `scaleRestMin` (minutes the scale may sit idle before the controller lets go of it so that it can switch itself off; 0 keeps it connected; default 5), `uiTheme` (colour scheme, 0 Dark .. 5 High contrast) and `powerOffWithScale` (switch the machine off with the power bot when the scale switches off; default false) |
| `/api/diagnostics` | GET | how the controller itself is doing, for remote analysis of a deployed board: `firmware` (version, build, running slot, restart reason, `boot_count`, `uptime_s`, update state), `memory` (inside the chip: free, lowest since start-up, largest piece; PSRAM), `stack_left` per task, `wifi` (network, connected, RSSI, IP), `clock_set`, `ble` (each link: paired address, connected, `attempts`, `failures`, `connects`, `last_wait_ms`, `longest_wait_ms`; the scale's frame counters; what the radio has heard, with how often), `storage` (filesystem size and use, shots, log size), `controller` (state, cup, reading interval). Capability `diagnostics_v1` |
| `/api/update` | GET | firmware: running version, the three slots (`factory`, `app0`, `app1`: version last seen in each, `valid` / `on trial` / `failed` / `incomplete` (an install into it did not finish) / `unused` / `factory`, which runs, which starts next), the release repository, whether a token is stored, what the last check found (`latest`, `available`), and the progress of an install (`state`: idle, checking, downloading, installed, failed; `percent`, `message`). Capability `update_v1` |
| `/api/update/check` | POST | ask GitHub for the latest release now (otherwise once a day when `auto_check`) |
| `/api/update/install` | POST | download the latest release's `firmware.bin`, check it against the release's `SHA256SUMS.txt`, install it in the spare slot and restart into it. Home screen only, no shot in hand; Start is refused while it runs |
| `/api/update/boot` | POST | `?slot=factory` (the safe version flashed over USB) or `?slot=previous` (the other update slot): start from it at the restart that follows |
| `/api/update/config` | POST | `{"repo":"owner/name","token":"...","auto_check":true}`; the token (a private repository only) is write-only: blank keeps it, `-` forgets it |
| `/api/update` | POST | install a `firmware.bin` uploaded as a form file (no internet needed) |
| `/api/power/events?days=` | GET | the power presses the controller made (schedule, web page or screen) in the last `days` days (default 30, at most 400), oldest first: `t` (epoch seconds), `on` (switching on or off), `by` (`schedule`, `owner`, or `scale`: the scale switched off with the setting `powerOffWithScale` on), `ok` (the press went out), `sure` (false: "press anyway" while on/off was not known), `outcome` (text). Presses made by hand on the machine are not seen. Kept for the last 500 presses. Capability `power_events_v1` |
| `/api/coffee/ratings` | GET | coffee ratings, newest first: `id`, `coffee`, `bag`, `drinker` (0 household), `t` (epoch), `stars`, `attr` (acidity, sweetness, body, bitterness, finish: 0 not given, 1 to 5), `flavours` (a bit per family, in the order of `families`), `share`; with `named` and the 8 `drinkers` names. Capability `coffee_taste_v1` (docs/COFFEE-TASTE.md) |
| `/api/coffee/rate` | POST | `{id?, coffee, bag, drinker, stars, attr:[5], flavours}`: a new rating, or a change to one; one per drinker per bag, a later one replaces it. Answers `{ok, id}` |
| `/api/coffee/rate/delete?id=` | POST | delete a rating |
| `/api/drinkers` | POST | `{names:[up to 8]}`: a drinker's id is its place in the list; turn named drinkers on with the setting `namedDrinkers` |
| `/api/taste/profile?drinker=` | GET | a drinker's taste profile (0: the household): `dims` (5 cup values, 8 flavour families), `profile` (`v` 0 to 1 with 0.5 no lean, `known` 0 to 1, `coffees`, `shown` from 3, `settled` from 8), `best` (liked best) |
| `/api/taste/recommend?drinker=` | GET | up to 5 coffees not yet rated, from the shelf and the shared library: `name`, `roaster`, `from` (`shelf`, `library`, `community`), `score`, `sure`, `why`, `like` (a coffee rated 4 or 5 it is like), `people_stars`, `community_stars`; and `buy_again` (finished coffees rated 4 or 5) |
| `/api/subscriptions` | GET, POST | GET: `today`, `monthly_cents`, `shots_per_day` (last 14 days), `country`, `matters`, `sharing`, `share_ratings`, and `subscriptions[]`: `id`, `kind` (`coffee`, `supplies`, `other`), `status` (`active`, `paused`, `cancelled`), `name`, `url`, `manage_url`, `every_days`, `next_date`, `price_cents`, `last_arrived`, `note`, the page's `title`, `site`, `description`, `image`, `icon`, `color`, `platform`; supplies' `item`, `stock`, `per_shot`, `per_delivery`; the shared description's `ships_to`, `cadences`, `price_from_cents`, `currency`, `style`, `sourcing`, `library_id`, `share`, `share_changed`, `can_share`, `review_note`; and worked out: `days_until`, `due_soon`, `stock_days_left`, `stock_short`. POST: one subscription (no `id`: a new one; with `library_id`: added from the library's list). Capability `subscriptions_v1` (docs/SUBSCRIPTIONS.md) |
| `/api/subscriptions/delete`, `/arrived`, `/skip` | POST | `{id}`: delete (with its reviews); a delivery arrived (supplies: added to the stock); skip one. Both move the next date on |
| `/api/subscriptions/preview` | POST, GET | POST `{url}`: the controller reads that page while idle. GET: `state` (`none`, `waiting`, `done`, `failed`), `url`, `problem`, `title`, `site`, `description`, `image`, `icon`, `color`, `platform`, `manage_url` |
| `/api/subscriptions/library` | GET | the shared library's services as cached (`index/services.json`) |
| `/api/subscriptions/share` | POST | `{id}`: send the service's description for review (sharing on; a name and an https link) |
| `/api/subscriptions/reviews` | GET | reviews: `id`, `subscription`, `drinker`, `stars`, `aspects`, `note`, `share` |
| `/api/subscriptions/review`, `/review/delete` | POST | `{subscription, drinker, stars, aspects:{value, quality, consistency, reliability, flexibility, transparency}, note}`: one per drinker per subscription, a later one replaces it; `{id}` deletes |
| `/api/subscriptions/prefs` | POST | `{country, matters:[sourcing values]}` for recommendations |
| `/api/subscriptions/recommend?drinker=` | GET | up to 5 services from the library: `service` (its library entry), `why`, `people`; `basis` (`taste`, `reviews`, `none`) |
| others | | `/api/cmd`, `/api/devices`, `/api/pair`, `/api/pusher`, `/api/power`, `/api/machine`, `/api/recipe`, `/api/share*`, `/api/net`, `/api/time`, `/api/log*`, `/api/update`, `/api/history/clear`, `/api/offset/reset`, `/api/last`, `/api/bot/test`, `/api/shot/grind`, `/api/shot/vote`, `/api/coffee/adopt` |

## Settings are stored steps

Every setting in this API is a whole number of the grinder's smallest step (`grindUm`, `current_um`, `suggested_um`, `grind_um`,
`/api/grind/set?um=`). What it looks like on the dial depends on the grinder: `grinder.divisor` (stored steps per displayed
unit: 1, 2, 4, 5, 10, 20, 25, 50, 100) or `grinder.subSteps` (two rings: inner steps per outer step). `35` with `divisor` 10 is
shown as `3.5`; with `subSteps` 9 it is `4:8`. `umPerUnit` is microns per stored step.

## `GET /api/grind`

The base verdict is the Grind-by-Sync-style advice against the recipe's target time; ratings may refine it.

| field | meaning |
| --- | --- |
| `action` | `no_grind_set`, `no_data`, `hold`, `finer`, `coarser`, `inconsistent`. A refinement shows as `finer` / `coarser` with `refined: true` |
| `base_action` | what `action` would be without ratings; a dial-in counts as settled when this is `hold` |
| `refined` | `action` is a refinement towards the shots the owner liked |
| `rated_state` | `off`, `too_few`, `outside_band`, `base_moving`, `refining`, `on_liked` |
| `rated_shots`, `rated_time_s`, `rated_band_s`, `aim_time_s` | liked shots (rated 4 or 5) found, their median time, the tighter range (+-), the time being aimed for (0 unless refining) |
| `target_time_s` | the recipe's own target time, always |
| `taste_hint`, `taste_shots` | -1 sour, 1 bitter, 0 none; a note only |
| `bean_age_days`, `bean_frozen`, `bean_frozen_days` | age of the bag in use, less days in the freezer (counted at `FROZEN_AGING_FRACTION` = 0.10) |

## `POST /api/bag`

`{ "coffeeId": 3, "roastDate": 20260901, "openedDate": 20260905, "weightG": 250 }` starts a new bag of the coffee, in use from
now. `{ "id": 7, "roastDate": ..., "openedDate": ..., "frozenDate": ..., "thawDate": ..., "weightG": ... }` corrects bag 7.
Dates are `YYYYMMDD`, 0 for none. Answers `{ "ok": true, "id": <bag id> }`.

`{ "coffeeId": 3, "roastDate": 20261018, "weightG": 340, "unopened": true, "subscriptionId": 1 }` puts a bag on the shelf
unopened: the coffee keeps the bag it has in use. `/api/library` `bags[]` carries `unopened` and `subscriptionId`. An unopened
bag goes into use with `bag_open`, when its coffee is chosen and has no other bag in use, or at Bag finished (below).

## Bag finished

A coffee whose bag is used up is taken off the controller's list without deleting it. `POST /api/library/do?what=coffee_finish&a=<coffee id>`
dates it today by the controller's clock (`20000101` if the clock is not set); refused with 409 while a shot runs, 409 for no
such coffee. The controller's COFFEE screen does the same with BAG FINISHED.

- `/api/library` `coffees[].finished`: `YYYYMMDD` the bag was finished, `0` = on the shelf.
- `/api/library` `presets[].hidden` (and `/api/settings` `recipes[].hidden`): `true` when its coffee is finished. Hidden
  presets keep their grind, offset and learning, are not listed on the controller, are skipped by BOOT's next recipe and refused by
  `preset_select`. Presets with no coffee are never hidden.
- When the active preset becomes hidden, the shown preset whose coffee has the oldest roast date is selected (presets with no
  coffee or no roast date after those, ties in list order); `activePreset` says which. With every preset hidden it stays put.
- A new bag (`POST /api/bag` with `coffeeId`) or `bag_use` puts the coffee back on the shelf: `finished` returns to 0 and its
  presets show again as they were. Saving the coffee's details (`/api/coffee`) does not change `finished`.
- With unopened bags on the shelf (docs/SUBSCRIPTIONS.md): another bag of the same coffee waiting goes into use instead of the
  coffee being finished. Otherwise the next bag by roast date may be an unopened one; if its coffee has no preset, one is added
  with the recipe just used (room permitting) and selected, and the bag is opened.

## `/api/maintenance`

`GET` returns `{ "shots_total": 41, "due": 1, "jobs": [ { "id": "backflush", "since_shots": 12, "every_shots": 10, "due": true,
"last_epoch": 1791672710 }, ... ] }` for `backflush`, `descale`, `grinder_clean`. `POST ?job=backflush&action=done` marks it done;
`?job=descale&action=every&every=200` sets the reminder interval (0 = never, at most 5000). A new controller (or one updated from a build before these) starts with
backflush every 200 shots, descale off and grinder clean every 500, given once to each job still at 0; after that a 0 the owner
sets stays never.

## `/api/community`: `similar`

`similar` is the owner's own most similar coffee with a setting for a close recipe: `found`, `setting`, `range`, `from_coffee`,
`from_setting`, `shift`, `basis` (`nearest`, or `fitted` when the owner's own coffees moved it), `samples`, `applied` (bit
mask: 1 roast, 2 altitude, 4 process), `hint_roast`, `hint_altitude`, `hint_process` (-1 finer, +1 coarser, by convention),
`near_recipe`, `from_settled`, and `verified`, always `false`.
