# The controller's web API

The page the controller serves is a client of this API, and so can anything else on the same network. It is **not
authenticated**: anyone who can reach the controller can read it and change settings. Use it on a network you trust. Remote
start is off unless the owner turns it on in Setup (`web_start` in the status).

`GET /api/status` carries `api_version` (now **1**) and `capabilities`, the groups of endpoints this firmware has, so a
client does not have to guess from the firmware version:

`status_v1`, `shots_v1`, `shot_data_v1`, `shot_csv_v1`, `grind_advice_v1`, `library_v1`, `bags_v1`, `maintenance_v1`,
`ratings_refinement_v1`, `tds_v1`, `shot_context_v1`, `shot_prep_v1`, `learn_grinder_v1`, `diagnostics_v1`, `update_v1`,
`power_events_v1`, `coffee_taste_v1`, `subscriptions_v1`, `ambient_v1`, `home_assistant_v1`, `shot_room_v1`,
`grind_room_v1`, `addons_v1` (the full list `Web.cpp` sends, 10 Nov 2026; each is described with its endpoints below)

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
| `/api/contexts` | GET | `?limit=` (default 100): the circumstances of the newest shots, joined to `/api/shots` by `shot_id`: bean age at the shot (effective, calendar, frozen days), time since the previous shot, shots since the grinder was cleaned, the start suggestions on offer (community and own-coffee, with ranges and basis), the advice given after the shot, curve quality (sample interval, weight step, points), the advisor settings in force, engine version, reserved owner fields, the room's slow temperature (`room_eff_c`) and the aim the advice used. The grind advice reads the room and the aim (confidence, the room term); capability `shot_context_v1` (`core/ShotContext.h`, `research/dial-in-engine.md`) |
| `/api/contexts`, `/api/shots` (fields added 3 Nov 2026) | GET | `ambient_rh_pct`, `ambient_temp_c` (the room at the shot, when a reading under 30 minutes old was known); on contexts also `ambient_source` (`ha` or `push`) and `ambient_age_s`. The context file is version 2; a version 1 file is converted at start-up, keeping every record |
| `/api/ambient` | GET / POST | The room's latest humidity and temperature (`rh_pct`, `temp_c`, `source`, `age_s`, `fresh`) / push one: `{"rh_pct": 48.5, "temp_c": 21.2}`, either may be left out. Capability `ambient_v1`. See `docs/HOME_ASSISTANT.md` |
| `/api/ambient/test` | GET | One read of the two Home Assistant entities now: `ok`, `problem`, `reading` |
| `/api/homeassistant` | GET / POST | Home Assistant settings: address, token, the two entities, webhook id, MQTT broker, user and password. The token, webhook id and password are never returned (`token_set`, `webhook_set`, `mqtt_password_set`); empty ones keep what is stored, `clear_*` removes them. Capability `home_assistant_v1`. See `docs/HOME_ASSISTANT.md` |
| `/api/shot/prep` | POST | `?id=&dose=&purged=0|1&uneven=0|1`: what the owner says about a shot's preparation afterwards: the dose they weighed (5-40 g, 0 = not weighed), whether the grinder was purged first, whether the flow looked uneven. Recorded only (the shot's context); an uneven shot that should not steer the grind is ruled out with `/api/shot/vote` (ignore). `/api/shots` then carries `dose_weighed_g`, `purged`, `uneven` for shots that have them. Capability `shot_prep_v1` |
| `/api/grind` (fields added 16 Oct 2026) | GET | `sensitivity_source` (`guess`, `grinder`: from this grinder's other coffees, `measured`: this coffee's shots); `has_number` (the advice can name a setting); `data_weight` (0-1, share of the sensitivity resting on this coffee's shots); `bracketed`, `between_steps` (kept short of, or at the closer of two, settings either side of the target); `out_of_range` (from a shot too long or short to count: the largest step); `grinder_elasticity`, `_se`, `grinder_shots`, `grinder_coffees` when known. Learning the grinder is part of the advice itself since 17 Oct 2026 (no setting); capability `learn_grinder_v1`. See `research/grind-logic-review.md` |
| `/api/grind` (fields added 2 Nov 2026) | GET | `shot_noise_s` (how much one shot's time scatters with nothing changed, learned from `shot_noise_repeats` repeats: two shots of one coffee in a row at one setting within two days, blended with 1.5 s); `usual_first_drop_s` (median first drop of the newest shots on this recipe, any coffee; absent until five are known); `first_drop_one_off_s` and `newest_time_s` when the newest shot was left out because its first drops came this many seconds early (negative) or late and it ran off the target the same way; `sensitivity_cv` (how uncertain `um_per_s` still is, relative) and `step_factor` (the step was multiplied by 1/(1+cv^2) for it; 1 for errors over two tolerances); `coffee_spread_cv` (how much one coffee's sensitivity may differ from the grinder's figure). See `research/grind-logic-review.md` "Real shots" |
| `/api/grind` (fields added 3 Nov 2026) | GET | `backlash_um` (the adjustment's dead zone learned from `backlash_reversals` pairs where the dial turned back, stored steps; 0 until the shots show one); `reversal_weight` (what the first shot after turning the dial back counts, 0.5 until five reversals are known); `play_um` when the suggested move turns the dial back and includes that much play; `newest_after_reversal` when the newest shot was such a first shot; `drift_s_per_day` (this coffee's drift at one setting, + slower; 0 when not known) and `drift_pairs` (shots behind it). See `research/grind-logic-review.md` section 8 |
| `/api/shot/room` | POST | `?id=&c=`: the room's slow temperature (12 h average, degrees C) at a past shot, for shots made before the controller kept it (backfilled from a home's history); `c` empty clears it. The grind advice reads it. Capability `shot_room_v1` |
| `/api/grind` (field added 9 Nov 2026) | GET | `room`: the room's effect on brew time (`core/GrindAdvisor.h` grindRoomEffect): `on` (setting `grindRoom`), `learned` (it stands clear of zero), `s_per_c` (seconds per degree C, negative: colder is slower), `se`, `shots`, `span_c`, `now_c` (the room's 12 h average now), `applied`, `shift_s` (how far it moved the newest shot's time). `/api/contexts` and `/api/shots` carry `room_eff_c`. Capability `grind_room_v1` |
| `/api/grind` (fields added 10 Nov 2026) | GET | `room.mode` (`strict`, `legacy`, `off`), `room.replicated` (two coffees, or ten same-setting pairs, agree), `room.suspected` and `room.suspected_s_per_c` (seen on one coffee, reported, not applied), `room.z`, `room.pairs`. The strict gate caps the effect at −1 s per °C and one shot's shift at one tolerance. `check_prep` with `check_prep_why` (`too_short`, `uneven`, `fast_sour`): no move, the shot may be the puck rather than the grind; COUNT on the shot uses it. `lone_fast_damped`. Setting `grindRoom`: `true` the strict gate (default), `false` off, `2` the original rule |
| `/api/grind` (field added 6 Nov 2026) | GET | `reminder_due`: the advice says finer or coarser and the setting held now is still the one the newest shot was made at (not acted on). The screen then shows a GRIND REMINDER, with SET and NOT NOW, once at start-up and once each time it wakes with the scale |
| `/api/addons` | GET | The add-ons compiled into this image (docs/ADDONS.md; none in the public firmware): `compiled`, `api`, and per add-on `id`, `name`, `version`, `api_ok`, `enabled`, `link`, `status`, `routes`, `fields` (secrets only as `set`), `options` (`dose_sets_target`, `select_recipe`, `shot_feedback`), `map` (grinder recipe to recipe id, `auto` or `manual`), `last_grind` (dose, gap, recipe, the next shot's own target, `note`), `chip_ram_used`, `chip_low_during`, and `card` (what the add-on declares for its card: steering modes, the comparison table; docs/ADDONS.md "The card"). Capabilities `addons_v1`, `addons_v2` |
| `/api/addons/file` | GET | `?id=&name=`: one of the add-on's own files (`/addons/<id>/<name>`, a text file such as `compare.csv`), as a download. 400 for a name outside that folder, 404 if it is not there, 409 while a shot runs |
| `/api/addons/enable`, `/config`, `/map`, `/do`, `/forget` | POST | `?id=&on=1` / `{id, values:{...}, options:{...}}` (a secret: blank keeps it, `-` forgets it) / `?id=&grinder=1..8&recipe=<id, 0 unmaps>` / `?id=&cmd=&arg=` (its card's buttons) / `?id=` (its settings and map go; it is switched off). 409 while a shot runs |
| `/api/shots`, `/shots.csv` (field added 9 Nov 2026) | GET | `stop_target_g`: the weight the shot stopped for. The same as `target_weight_g` (the recipe's) unless a grinder add-on's measured dose gave the shot a target of its own. The CSV has it as its last column. `/api/contexts` adds `dose_from_grinder` and, for such a shot, `stop_target_g` |
| `/api/shots`, `/` (caching, 11 Nov 2026) | GET | `/api/shots` sends `ETag` (a number chosen at boot, the history's revision, the limit) and `Cache-Control: no-cache`; `If-None-Match` with that tag gets a 304 with no body. The revision goes up with every change the list shows (a shot added, edited or cleared, its context or data set written). The answer is the same JSON as before, sent in 4 KB pieces. `/` sends the page gzipped when the browser accepts gzip, with `ETag` (a hash of the page) and a 304 for a browser that has it |
| `/api/grind` (fields added 9 Nov 2026) | GET | `grinder_gap_um`, `grinder_dose_g`, `grinder_age_s`: the newest grind a grinder add-on reported |
| `/api/update` (field added 9 Nov 2026) | GET | `addons`: the ids compiled into the running image. A release from another repository may not have them |
| `/api/shot/data?id=` | GET | one shot's readings: `time_s[]`, `weight_g[]`, `flow_g_s[]`, `scale_timer_s[]`, and a summary |
| `/shots.csv`, `/shot.csv?id=` | GET | CSV exports; `shots.csv` now ends with `tds_pct,extraction_pct` |
| `/api/grind` | GET | grind advice, see below |
| `/api/grind/set?um=` | POST | set the active preset's setting, in **stored steps** (1 to 2000) |
| `/api/library` | GET | coffees, recipes, presets, grinders, **bags**; each coffee's `finished` and each preset's `hidden` (see Bag finished) |
| `/api/coffee` | POST | save a coffee (edits correct the bag in use) |
| `/api/bag` | POST | new bag, or correct one; see below |
| `/api/library/do?what=&a=&b=` | POST | small changes by id; adds `bag_use` (a = coffee, b = bag), `bag_delete` (a = bag), `bag_open` (a = an unopened bag: in use from today) and `coffee_finish` (a = coffee) |
| `/api/grinder` | POST | save the grinder; takes `divisor` and `subSteps` (how a setting is read), `min` / `max` (its range in stored steps, 0 = not given) and `covers` (0 not said, 1 espresso only, 2 espresso to filter). `burrs` left empty keeps the ones it had; unit `um` is always 1 µm per step |
| `/api/shot/rate?id=&rating=&taste=` | POST | rating 0 to 5, taste 0 to 3 |
| `/api/shot/tds?id=&tds=` | POST | percent TDS from a refractometer, 0.5 to 25, or 0 to clear |
| `/api/maintenance` | GET, POST | upkeep counters, see below |
| `/api/community`, `/api/community/*` | GET | the shared library's figures for a coffee; the owner's own similar-coffee start (`similar`) |
| `/api/settings` | GET, POST | settings, including `grindRatedTarget` and `scaleRestMin` (minutes the scale may sit idle before the controller lets go of it so that it can switch itself off; 0 keeps it connected; default 5), `uiTheme` (colour scheme, 0 Dark .. 5 High contrast), `powerOffWithScale` (switch the machine off with the power bot when the scale switches off; default false) and `webPanels` (the web page's optional panels shown, bits: 1 Insights, 2 Discover, 4 Subscriptions, 8 Community, 16 Logs; posting it records the owner's choice, and `webPanelsChosen` says whether one was made: until then the panels already in use are shown) |
| `/api/diagnostics` | GET | how the controller itself is doing, for remote analysis of a deployed board: `firmware` (version, build, running slot, restart reason, `boot_count`, `uptime_s`, update state), `memory` (inside the chip: free, lowest since start-up, largest piece; PSRAM), `stack_left` per task, `wifi` (network, connected, RSSI, IP), `clock_set`, `ble` (each link: paired address, connected, `attempts`, `failures`, `connects`, `last_wait_ms`, `longest_wait_ms`; the scale's frame counters; what the radio has heard, with how often), `storage` (filesystem size and use, shots, log size), `controller` (state, cup, reading interval). Capability `diagnostics_v1` |
| `/api/diagnostics/report` | GET | diagnostics reports (docs/TELEMETRY.md): `on`, `relay` (and `relay_default`), `waiting` (reports queued), `since` (start of the period the next covers), `last_sent`, `last_code` and `last_problem`, and the `key_id` reports are signed with |
| `/api/diagnostics/preview` | GET | the next daily report's body, exactly as it would be sent now |
| `/api/diagnostics/send` | POST | builds a report now and sends it as soon as the controller is idle; 409 when diagnostics are off |
| `/api/links` | GET | the Community Hub: `listed` (the library's reviewed links, `index/links.json`) and `mine` (links this owner suggested: `state` queued, sent, review, published, declined or refused, and the reviewer's `note`) |
| `/api/links/suggest` | POST | `{"url","title","kind","topics":[...],"why","image"}`: kept here and sent for review at once. `kind` is video, playlist, post, guide, article or podcast; one to three `topics` of preparation, dialing_in, brewing, beans, gear, milk, maintenance, science. An http link is made https; a link with a credential in its query is refused |
| `/api/links/delete?id=` | POST | forgets a suggestion here (what a reviewer listed stays) |
| `/api/trending` | GET | `index/trending.json` as kept: the recipes people settled on lately |
| `/api/directory` | GET | `?q=&origin=&process=&roast=&page=`: the coffee directory kept on the controller, 20 coffees at a time, with `matched`, `count`, `facets` and `on_shelf` per coffee |
| `/api/diagnostics/bug` | POST | `{"kind","text","contact"}`: a problem in the owner's words (at most 1000 characters; `contact` optional, at most 64), scrubbed and sent with a diagnostics report at once. `kind` is one of `other`, `brewing`, `grind_advice`, `scale`, `pushers`, `machine_power`, `web_page`, `sharing`, `firmware_update`. 409 when diagnostics are off |
| `/api/diagnostics/ram` | POST | `{"small_to_psram": true|false}`: allocations over 256 bytes from PSRAM unless they ask for chip RAM; stored, and the controller restarts to apply it (409 during a shot). `/api/diagnostics` `memory` shows `small_to_psram` (now), `small_to_psram_next`, and for the newest chip RAM low `low_during` (the jobs running), `low_drop`, `low_at_uptime_s` |
| `/api/diagnostics` `loop` (11 Nov 2026) | GET | The loop's passes over the last full minute (or the one under way): `passes`, `avg_ms`, `max_ms` and the job that took most of that pass (`max_job`, `max_job_ms`), `job_max_ms` (the slowest run of each job: `ui`, `web`, `addon`, `shot save`, `log`, `update`, `sharing`, `diag`, `subs`, `ha`, `other`), and since boot `boot_max_ms`, `boot_max_job`, `boot_max_job_ms`, `boot_max_at_uptime_s`. The web server answers a few requests per pass (idle connections no longer hold the others up), so a slow pass makes every request slow. A pass of 3 s or more is logged (`loop: a pass took ...`) |
| `/api/diagnostics/relay` | POST | `{"url":"https://..."}` sends reports to another relay, for testing; `""` goes back to the built-in one |
| `/api/crash` | GET | the last crash's report body (`diag_crash`: version, restart reason, failing task, PC, cause, backtrace, the log before it); `{}` when there has been none |
| `/api/update` | GET | firmware: running version, the three slots (`factory`, `app0`, `app1`: version last seen in each, `valid` / `on trial` / `failed` / `incomplete` (an install into it did not finish) / `unused` / `factory`, which runs, which starts next), the release repository, whether a token is stored, what the last check found (`latest`, `available`), and the progress of an install (`state`: idle, checking, downloading, installed, failed; `percent`, `message`). Capability `update_v1` |
| `/api/update/check` | POST | ask GitHub for the latest release now (otherwise once a day when `auto_check`) |
| `/api/update/install` | POST | download the latest release's `firmware.bin`, check it against the release's `SHA256SUMS.txt`, install it in the spare slot and restart into it. Home screen only, no shot in hand; Start is refused while it runs |
| `/api/update/boot` | POST | `?slot=factory` (the safe version flashed over USB) or `?slot=previous` (the other update slot): start from it at the restart that follows |
| `/api/update/config` | POST | `{"repo":"owner/name","token":"...","auto_check":true}`; the token (a private repository only) is write-only: blank keeps it, `-` forgets it |
| `/api/update` | POST | install a `firmware.bin` uploaded as a form file (no internet needed) |
| `/api/backup` | GET | the whole controller as one `.bbwbak` file (`src/core/BackupFormat.h`): every NVS record (settings, network, pairing, machine, maintenance...) and every file (library, ratings, links, shots...). `?history=0` leaves out the shot history; `?secrets=1` adds the Wi-Fi and bot passwords, the library token and the signing key (otherwise blanked or left out). Not while a shot runs |
| `/api/restore` | POST | upload a `.bbwbak` as a form file; it is checked (format, CRC) and kept, nothing changes yet. Answers `{created, firmware, key_id, history, secrets, records, files, bytes}` or a 400 with the reason |
| `/api/restore/apply` | POST | write the checked backup back and restart at once. Records a backup without secrets lacks (passwords, token, key) keep this controller's own; a backup with the history replaces the history |
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
| `/extension.zip` | GET | the Brew Controller helper for Chrome (browser-extension/), packed when the firmware was built |
| `/api/subscriptions/recommend?drinker=` | GET | up to 5 services from the library: `service` (its library entry), `why`, `people`; `basis` (`taste`, `reviews`, `none`) |
| `/api/share` | GET | the shared library link, among the sharing figures: `via_relay` (true: through the Brew Controller relay, no token), `relay` (its host), `repository` (the owner's own, empty with the relay), `configured`, `token_set`, `problem`, `via`, `to_library`, `last_answer` |
| `/api/time?epoch=&tz=` | POST | sets the clock if it is not set; `tz` (optional) is the local time zone as a POSIX rule, e.g. `<-05>5<-04>,M3.2.0,M11.1.0` (`core/TimeZone.h` reads the `M` form); stored when it changed, 400 if it cannot be read |
| `/api/power` | GET | the power bot and schedule; `utc_offset_min` (the fixed offset, used without a rule), `tz_rule` (empty: none), `utc_offset_min_now` (what the schedule uses now) |
| others | | `/api/cmd`, `/api/devices`, `/api/pair`, `/api/pusher`, `/api/power`, `/api/machine`, `/api/recipe`, `/api/share*`, `/api/net`, `/api/time`, `/api/log*`, `/api/update`, `/api/history/clear`, `/api/offset/reset`, `/api/last`, `/api/bot/test`, `/api/shot/grind`, `/api/shot/vote`, `/api/coffee/adopt` |

## Settings are stored steps

Every setting in this API is a whole number of the grinder's smallest step (`grindUm`, `current_um`, `suggested_um`, `grind_um`,
`/api/grind/set?um=`). What it looks like on the dial depends on the grinder: `grinder.divisor` (stored steps per displayed
unit: 1, 2, 4, 5, 10, 20, 25, 50, 100) or `grinder.subSteps` (two rings: inner steps per outer step). `35` with `divisor` 10 is
shown as `3.5`; with `subSteps` 9 it is `4:8`. `umPerUnit` is microns per stored step.

`GET /api/grind` adds `recommendation` {`source` (community, similar, range), `setting`, `range`, `current`, `people`,
`from`} when the coffee, bag and recipe in use have no shot yet and a starting grind differs from the setting by more
than its range. `POST /api/grind/recommendation?use=1` sets it, `use=0` keeps the setting; either way it is not offered
again for that coffee, recipe and bag. The controller shows the same as a "New grind advice" screen (SET / KEEP).
A coffee newly put on a recipe takes the setting of the pair in use (the grinder as it is now); the web page no longer
takes a grind per coffee. Its stop offset starts from what the recipe learned on its other coffees.

An automatic power-off (the scale switched off, the schedule) waits up to 30 minutes for the power bot and is called off
when the machine is used again (power event outcome "not switched off: the machine was in use again").

`GET /api/start` says where a new controller's setup stands, for the web page's Getting started guide: `wifi`
{`joined`, `connected`}, `scale` and `brew_bot` {`paired`, `connected`}, `power_bot` {`paired`}, `machine` {`maker`,
`model`}, `grinder` {`model`, `set`, `range`}, `recipes`, `coffees_on_recipe`, and `shots` (any shot yet).

Grind advice is sized by its confidence (setting `grindConfidence`, on unless turned off): how close the next shot
landed after each followed change on this grinder. `GET /api/grind` adds `confidence` {`score` 0..1, `followed`,
`rms_s`, `window` (± around the suggestion, grinder units), `scaled`, `on`}. Low confidence makes a change smaller with
a wider window; high confidence allows the full correction past the usual largest step (up to 1.5 times it) with a
tighter window. `GET /api/grind/accuracy` lists the changes advised, oldest first: `at`, `coffee_id`, `from`, `advised`,
`next`, `followed`, `target_s`, `time_s`, `confidence` (percent, when recorded), with `score`, `followed`, `rms_s` and
`tolerance_s` for now.

The shared library's recipe book and owner review: `GET /api/community/recipes` is `index/recipes.json` as cached;
`GET /api/library/pending` gives `index` (`index/pending.json`, the coffees and recipes waiting for review), `kid`
(this controller's key id: its own items are not for it to review), `can_review` (the library has published something
it sent), `mine` [{`target`, `approve`}], `linked`, `sharing`, and `recipes` [{`id`, `state`, `note`, `library_id`}]
(where each recipe it shared stands). `POST /api/library/review` {`target`, `verdict`: approve|decline, `note`
(needed to decline)} sends a signed review; 409 for its own item or before it may review.
`POST /api/recipes/share?id=N` sends a recipe to the recipe book. Two approvals from owners whose controllers have
something published, and no decline, publish an item; a decline holds it for a person.

`GET /api/community` adds `range_guess` {`setting`, `range`} when the grinder has a range and says what it covers: a first
guess for espresso (a fifth of the way up an espresso-to-filter range, the middle of an espresso-only one, ± a sixth of
the span), for when no shots, community figure or similar coffee give one.

## `GET /api/grind`

The base verdict is the Grind-by-Sync-style advice against the recipe's target time; ratings may refine it.

| field | meaning |
| --- | --- |
| `action` | `no_grind_set`, `no_data`, `hold`, `finer`, `coarser`, `inconsistent`. A refinement shows as `finer` / `coarser` with `refined: true` |
| `base_action` | what `action` would be without ratings; a dial-in counts as settled when this is `hold` |
| `last_um` | only when the pair in use has no setting (`current_um` 0): the setting its newest shot on this grinder calibration was pulled at, 0 if none. The controller asks for a setting before the shot ("SET THE GRIND") and the Home page shows a banner |
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
unopened: the coffee keeps the bag it has in use. `{ "coffeeId": 3, "onTheWay": true, "expectedDate": 20261116, "subscriptionId": 1 }` adds a bag
on its way (ordered, not come); `/api/library/do?what=bag_arrived&a=<bag>` makes it unopened. `/api/library` `bags[]` carries `unopened`,
`onTheWay`, `expectedDate` and `subscriptionId`. An unopened
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
