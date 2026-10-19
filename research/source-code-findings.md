# Source-code findings

Retrieval date for everything below: **2026-10-12**. Upstream clones were shallow (`--depth 1`) into a scratch
directory; nothing upstream was modified, no issues/PRs were opened, no network service of any project was
queried other than reading public GitHub/store pages. **No tests were run and no hardware was touched**, so
nothing here is "reproduced"; "Verified" means *read in authoritative source*.

Pinned revisions:

| Project | Commit read | Commit date |
| --- | --- | --- |
| Open Brew by Weight / Advanced-Shot-Stopper (OBBW) | `840900a12fabce5c3fa030dac9f1c3a94c9ded84` (VERSION 0.0.8) | 2026-10-12 |
| Beanconqueror | `bbf8de0895a415021c1e1a47ee37716d301fd494` (package.json 2.0.0) | 2026-10-11 |
| Caliburr (public repo `vincentvella/Caliburr`) | `ead41f91d7959b3b37a28a05677fbc70d0d6d7b3` | 2026-08-20 |
| Visualizer.coffee (`miharekar/visualizer`) | `0627baaf126e9c67531bb9fbcf5e2d922c78ced7` (OpenAPI v1.18.1) | 2026-10-12 |
| grinder-converter (`daniilr/grinder-converter`) | `f49effab558397126370b66afc8f191436bde897` | 2026-08-29 |
| This project (`bdb-bbw`) | `5520bea` (+ uncommitted work) | 2026-10-12 |

Link form: `<repo>/blob/<commit>/<path>`; e.g. `https://github.com/Cheerpipe/Advanced-Shot-Stopper/blob/840900a/docs/INTEGRATION_API.md`.
(Repository owner for OBBW taken from its LICENSE/library.properties: Felipe Urzúa; the GitHub org path `Cheerpipe/` is what the clone used.)

---

## 1. Open Brew by Weight (OBBW) — AGPL-3.0-or-later

**Identity.** `LICENSE` header: "Open Brew by Weight, Copyright (C) 2024-2026 Felipe Urzúa and contributors", AGPL-3.0; states that
portions derive from `tatemazer/AcaiaArduinoBLE` (MIT) with notices preserved.
**Scope** (`PROJECT_CHARTER.md`): ESP32-S3 n8r4/n16r8 with PSRAM; latch/paddle, momentary, momentary+reed/hall machines; **a relay is
the only connection to the machine's activation circuit** — i.e. it is *wired* (invasive), not a button pusher. Remote shot start/rinse
are disabled in default firmware; "MQTT, persistent remote-control integrations ... are not project goals".

### 1.1 Scale driver contract (best reusable abstraction found)
`libraries/EspressoScaleBLE/src/ScaleProtocol.h` — a **data-driven protocol descriptor**:

```c
struct ScaleProtocol {
  const char *id, *debugLabel, *readUuid, *writeUuid;
  const char *const *namePrefixes; size_t namePrefixCount;
  ScaleFeatureSet features;
  bool (*supportedPacketLength)(int);
  bool (*parseWeight)(const uint8_t*, int, float*);
  bool (*parseTimer)(const uint8_t*, int, uint32_t*);
  bool (*encodeCommand)(ScaleOp, uint8_t, uint8_t*, int*);
  const ScalePayload *initWrites; size_t initWriteCount;
  bool requireAdvertisedName;
};
```
- Capability bitset `ScaleFeatures.h`: Weight, Tare, StartTimer, StopTimer, ResetTimer, CombinedTareStart, IndependentBeep, Volume, CommandAudibleFeedback (…).
- Protocol implementations in `libraries/EspressoScaleBLE/src/protocols/`: Acaia, Decent, Difluid, Eclair, Eureka, Felicita, GenericFf11, Myscale, Varia, WeighMyBru (+ Registry).
- README compatibility table (maintainer observations, not requalified per build): Acaia Lunar (2 hardware variants, "Great"/"Hit or Miss"), Pearl S, Pyxis, Bookoo Themis Mini/Ultra, Felicita Arc, Eclair, Decent, DiFluid, MyScale, Varia, Eureka, WeighMyBru. Weight always grams.
- Contract differences vs. what this project needs: capabilities are **per-protocol, not per-device-instance** except Bookoo model identification; no stability flag, no battery in the descriptor; sampling timestamps are not part of `parseWeight`'s signature (timing is handled by the scale worker). Release 5.0.0 supports **only ESP-IDF 6.1 NimBLE on ESP32-S3+PSRAM** ("standalone Arduino Library Manager ... no longer supported").
- This project already vendors these files unmodified: `bdb-bbw/lib/EspressoScaleProtocol` (documented in `bdb-bbw/README.md` §12, `docs/RESEARCH.md` §1).

### 1.2 Machine profiles (JSON, versioned)
`config/machines/rancilio-silvia-pro-x.json` (also `la-marzocco-linea-micra.json`, `...-reed.json`), `config/hardware/esp32-s3-relay-x1-speaker*.json`.
Fields read: `schema_version:1`, `id`, `display_name`, `compatibility_revision`, `hardware[]`, `brand`, `model`, `integration_revision`, `integration`,
`interface{control: "momentary"|…, feedback}`, `factory_defaults{operational_wall_ms, momentary{start_edge, stop_pulse_ms, max_single_press_ms, assume_idle_on_scale_connect, shot_reaction_timeout_s}, quick_rinse{…}}`.
→ This is the closest existing thing to a "machine profile declaring button semantics (toggle vs momentary, pulse widths, caps)". It is *relay-oriented* (edge/pulse timing); it has no concept of "N physical buttons, each with a label and a press-sequence".
Safety model (`PROJECT_CHARTER.md`): relay open at startup/reset/power loss/fault; 60 s cap on full-firmware activations; compatibility mode mirrors paddle without cap.

### 1.3 Stop logic and learning (`src/ShotStopperBbwEwma.h`, `ShotStopperBbwLearning.h`)
- `bbwEwma::predict(timeS[], weightG[], count, targetG, fallbackS)`: ordinary least squares over the last `WEIGHT_TREND_POINT_COUNT` samples; returns `reference + meanX + (target − meanY)/slope`; falls back when non-finite, slope ≤ 0, too few points, last sample below `WEIGHT_TREND_MIN_LAST_SAMPLE_G`, timestamps non-increasing, or predicted horizon < `WEIGHT_TREND_MIN_HORIZON_S`.
- `bbwEwma::learn(offsetG, finalWeightG, goalG, alpha, fresh, &updated)`: `offset += (alpha/100)·(final − goal)`, clamped to `[0, MAX_OFFSET_G]`; rejects stale/non-finite observations or |observation| > MAX_OFFSET_G.
- `Evidence` struct (`WINDOW = 20`, 5 anchor trajectories, challenger/incumbent switching) = bounded replay of observations to choose among alpha values; state keyed by **stable preset ID** (`BbwLearningBank`, ≤3000 B RAM, `static_assert`).
- This project's `src/core/BbwAlgorithms.h` already cites this file in a header comment (AGPL provenance noted).
- Not found: any grind recommendation. OBBW learns the *stop offset*, not the grind.

### 1.4 Shot persistence (`src/ShotStopperShotLogTypes.h`)
`ShotLogRecord` is a **fixed-size summary**, not a time-series: `id, bootId, endedAtMs, endedAtUnixSec, endedAtLocalSec, timezoneOffsetMinutesAtCommit, durationDs (0.1 s), goalWeightG, actualWeightCg (0.01 g), errorCg, offsetUsedCg, firstDropDs, avgFlowCgS, shotType/cutType (bit-packed with preset ID), stopDetail, actualWeightSource, maxRecoveryWeightCg, minBbwBrewTimeDs, targetReachedEarlyDs, tareAtDs, …`; `ShotLogHeader{magic, schemaVersion, recordSize, generation, …, checksum}`. A separate `ShotStopperShotCurve*` exists for curve arrays shown in the Web UI (not traced in depth — **Unknown** whether curves are exportable via the public API).
Time convention: durations in deciseconds; wall time as UTC unix seconds + local seconds frozen at completion (documented in `INTEGRATION_API.md`).

### 1.5 Integration API (`docs/INTEGRATION_API.md`, 682 lines)
- LAN-only, **deliberately unauthenticated**: `GET /api/v1/integration` (apiVersion 2, `deviceId`, `capabilities[]` e.g. `webhook_v1, preset_select_v1, quick_settings_v1, restart_v1, stored_shots_v1`, `shotState`, `lastShot{cycleId, durationMs, targetWeightG, presetId, presetName, shotType, stopDetail, firstDropMs, weightG, averageFlowGps, rating, endedAtUnixSec…}`, `stats{…}`); `GET/PUT /integration/webhook` (single destination, plain `http://`); `GET /integration/presets`; `PUT /integration/presets/active`; `PUT /integration/quick-settings`; `POST /integration/restart`; async mutations return 202 + `requestId`, poll `/integration/request` until `PERSISTED`.
- It **cannot start/stop/rinse** the machine by design.
- Webhook example (verbatim from `docs/features/webhooks.md` line ~80, `schemaVersion:1, event:"end"`): `{"schemaVersion":1,"event":"end","deviceId":"AA:BB:CC:DD:EE:FF","bootId":123,"cycleId":42,"uptimeMs":923400,"timestamp":1767225611,"sentAtUptimeMs":923480,"durationMs":27800,"targetWeightG":36.0,"presetId":2,"presetName":"Double","shotType":"auto","stopDetail":"normal_target","firstDropMs":4480,"weightG":36.72,"averageFlowGps":1.57}`.
- Home Assistant integration: `integrations/OpenBrewByWeight/` (`custom_components/open_brew_by_weight`, pyproject `license = "AGPL-3.0-or-later"`, coverage gate 95 %).
- Diagnostic scale-profile capture (`/api/v1/diagnostic/scale-profile*`): 32-byte records, 8,192 slots, text download — a raw scale trace exists but is a diagnostic tool, not a shot export.
- **Unknown:** no per-shot time-series export endpoint was found in the public integration contract (absence in docs ≠ absence in code).

---

## 2. Beanconqueror — GPL-3.0

Ionic/Angular/Capacitor app (`package.json` 2.0.0), local storage, optional Visualizer upload and "server" component. `LICENSE` = GPLv3.

### 2.1 Entities (interfaces in `src/interfaces/`)
- **`IBrew`** (`brew/iBrew.ts`, 169 lines): `grind_size: string`, `grind_weight: number` (dose), `method_of_preparation`, `mill` (UUID string), `mill_speed`, `mill_timer`, `pressure_profile`, `bean` (UUID), `brew_temperature`, `brew_time` + `brew_time_milliseconds`, `brew_quantity` + `brew_quantity_type` (water in), `brew_beverage_quantity` + type (out), `note`, `rating`, `coffee_type`, `coffee_first_drip_time(+_milliseconds)`, `coffee_blooming_time(+_ms)`, `tds`, `config` (incl. `unix_timestamp`), `cupping`, `cupped_flavor`, `flow_profile` (filename of flow file), `reference_flow_profile`, `preparationDeviceBrew`, `customInformation`, `bean_weight_in`, `vessel_*`, `favourite`, `best_brew`.
- **`IBean`** (`bean/iBean.ts`): `name, roaster, roastingDate, buyDate, openDate, bestDate, beanMix (blend/single), roast (enum) + roast_range + roast_custom, bean_information[] (country, region, farm, farmer, elevation, processing, variety …), bean_roast_information, cupping_points, decaffeinated, ean_article_number, frozen* (date, id, groupId, storage type), weight, cost, co2e_kg, rating`. **A Bean is a bag-like record** (carries roast date, weight, frozen state) — product identity and bag are *not* separated.
- **`IMill`** (`mill/iMill.ts`): `name, note, finished, config, attachments, has_adjustable_speed, has_timer`. **No manufacturer/model identity, no setting unit, direction, range or step.** Grind setting is a free string on the brew.
- Flow data: `classes/brew/brewFlow.ts` (`BrewFlow`), stored as a separate file referenced by `brew.flow_profile`.

### 2.2 Persistence/export
`services/uiStorage.ts`: `export()` dumps every key/value as one JSON object and blanks `visualizer_username/password` ("#520"). Import is the inverse. Not versioned beyond a `VERSION` store (`services/uiVersionStorage.ts`; migration logic not traced). **Unknown:** exact backwards-compat guarantees of the export file.

### 2.3 Device layer
`classes/devices/`: `BluetoothScale` base (`bluetoothDevice.ts`; `Weight`, `WeightChangeEvent`, `FlowChangeEvent`, `TimerEvent`) with ~40 device classes — Acaia (`lunarScale.ts`, `acaia/`), Bookoo, Decent, Felicita, Skale, Eureka, Varia, WeighMyBru, DiFluid, Timemore, thermometers, pressure sensors, refractometer. `classes/preparationDevice/`: **Gaggiuino, GaggiMate, Meticulous, Xenia, Sanremo YOU, Move2** (machine integrations that read shots). `grep -i switchbot` over `src/` → **no matches**: no button-pusher support found.

### 2.4 Import API (`docs/brew-import-api.md`, 675 lines, last touched in `bbf8de0`)
`beanconqueror://ADD_BREW?len=<n>&shareBrew0=<chunk>…` and `ADD_BREWS` (≤50). Payload = JSON → gzip → unpadded base64url; chunks ≤400 chars, ≤1,024 chunks; inflate cap 512 KiB (single) / 4 MiB (batch). Routed in `services/intentHandler/intent-handler.service.ts`; decoder `brew-handoff.decoder.ts`; mapper `services/brewImport/brew-import.service.ts`.
Envelope `v:1` → `app{name,version?}`, `brew{date*, doseIn{g}, waterIn*{ml}, beverageOut*{g}, brewTime*(s), temperature(°C), ratio, grindSize(string), grinderRpm, grinderName, preparationMethod*, preparationType, bloomTime, firstDripTime, rating(0–10 sender scale, clamped not rescaled), note}`, `bean{name, roaster, roastingDate, origin, region, farm, farmer, elevation, process, variety, aromatics, note, beanMix, decaffeinated, imageUrl}`, `flow{fidelity: full|downsampled, t[] (delta ms), waterDispensed[] & weight[] (deci-grams, delta), temperature[]?}` (≤10,000 samples), `metrics[]` (custom traces, `kind: measured|target`), `imported{source*, sourceName*, sourceUrl?, device?, schema*, params?}`.
Behaviour: bean/grinder/preparation matched **by name after NFC + lowercase**, user is *asked* before creating a bean/grinder; `waterIn` is mandatory (ml) — espresso senders must synthesise it; `brew.ratio` and `app` are validated and dropped; `bean.imageUrl` dropped; **the shot is always created as a new Brew (no upsert/dedup)** — duplicates are the sender's problem; import is only possible with the app foreground and onboarding complete.
Evidence: **Verified (documented in-repo by the maintainers, matches decoder module names)**; **not exercised**.

---

## 3. Visualizer.coffee — MIT (`miharekar/visualizer`, Rails)

### 3.1 API (`openapi.yaml`, OpenAPI 3.1, v1.18.1; server `https://visualizer.coffee/api`)
Auth: OAuth2 authorization-code (apps register at `/oauth/applications`; token 1 week, refresh) or HTTP Basic. Scopes `read`, `upload`, `write` (non-inclusive). Public/unauthenticated: public shots, share codes, **canonical search**.
Paths: `/me`, `/shots`, `/shots/upload`, `/shots/shared`, `/shots/{id}[/download|/profile]`, `/roasters[/{id}]`, `/coffee_bags[/{id}]`, `/canonical_roasters`, `/canonical_coffee_bags`.
`POST /shots/upload` detects **Beanconqueror** (JSON with `mill` or `brewFlow`), **Gaggiuino** (`datapoints`), **GaggiMate** (`samples`), **Meticulous** (`profile_name` + `data`), Decent JSON, Decent `.shot` TCL, Smart Espresso Profiler CSV. Re-upload updates the existing shot (Beanconqueror matched by `visualizerId`/`meta.visualizer.shot_id`, others by telemetry).
Limits (documented): 50 req/min and 200 req/10 min per IP; 200/10 min per user; non-premium: shot lists limited to last month, **30 uploads / rolling 24 h**; premium needed to create/update roasters, coffee bags, tags, tasting scores, private notes, metadata.
Parsers: `app/models/parsers/{beanconqueror,decent_json,decent_tcl,gaggimate,gaggiuino,meticulous,sep_csv}.rb`.

### 3.2 Schema (`db/schema.rb`, 412 lines)
- `canonical_roasters(name, website, country, address, loffee_labs_id)`; `canonical_coffee_bags(canonical_roaster_id, name, url, country, region, elevation, farmer, harvest_time, processing, roast_level, variety, tasting_notes, loffee_labs_id unique)`.
- User-owned `roasters(user_id, name, website, canonical_roaster_id)`; `coffee_bags(roaster_id, name, roast_date, roast_level, country, region, farm, farmer, variety, elevation, processing, harvest_time, quality_score, canonical_coffee_bag_id, frozen_date, defrosted_date, metadata jsonb, grind_suggestion jsonb, …)`.
- `shots(start_time, profile_title, user_id, drink_tds, drink_ey, espresso_enjoyment int, bean_weight, drink_weight (strings), grinder_model, grinder_setting (strings), bean_brand, bean_type, roast_date (string), roast_level, duration float, metadata jsonb, sha, public bool, coffee_bag_id, canonical_coffee_bag_id, fragrance/aroma/flavor/aftertaste/acidity/sweetness/mouthfeel/bitterness int, grind_suggestion jsonb)`.
→ **Only existing system found that separates canonical (shared) coffee from user bag.** Grinder is *just two strings* on the shot — no grinder entity.
- Canonical data provenance: `app/jobs/loffee_labs_importer_job.rb` pulls `https://beta.loffeelabs.com/api/v2/roasters` and `/beans/bulk` with a **private bearer key in Rails credentials**, maps fields, excludes some domains, keeps beans newer than 3 months. **Redistribution terms of Loffee Labs data: Unknown** (no terms found).

### 3.3 Grind suggestion (`app/models/shot/grind_suggestable.rb`)
Not a statistical model: builds a JSON "state" (this shot + up to 5 prior shots of same coffee / same profile / same grind, and a comparison with a reference: best-rated earlier shot or median of ≥3 earlier shots; relative labels at ±10 %/±25 %), sends it to an LLM service (`SystemOne#evaluate`) asking for a 7-level score (Much finer … Much coarser) and a "puck issue" yes/no; keeps direction only if summed probability ≥ 0.6 else "Not sure". Gated to `user&.admin?` in the model — **not generally available**. Takeaway: the *prompt structure and thresholds* are inspectable; the core is a hosted LLM, i.e. not reusable offline.

---

## 4. Caliburr — public repo, **no LICENSE file**

`vincentvella/Caliburr` (TypeScript, Expo Router + NativeWind + **Supabase**); bundle id `coffee.caliburr.app` confirmed in `app.config.ts` lines 41/53, which ties this repo to the store app. Homepage `caliburr.coffee` meta: "Crowdsourced dial-ins for every grinder, bean, and brew method."
- Tables (`lib/database.types.ts`): `grinders(brand, model, burr_type, adjustment_type: stepped|stepless|micro_stepped, steps_per_unit, range_min, range_max, image_url, verified, created_by)`, `beans(name, roaster, origin, process, roast_level, tasting_notes[])`, `brew_machines`, `recipes(grinder_id, bean_id?, brew_machine_id?, brew_method, grind_setting text, dose_g, yield_g, brew_time_s, water_temp_c, ratio, roast_date, roast_level, notes, user_id nullable, upvotes)`, `recipe_history`, `grinder_edits`, `grinder_verifications`, `user_grinders`, …
- Governance model: 5 distinct non-creator user confirmations flip `verified=true`; verified entries read-only; edits via `*_edits` tables with admin review; account deletion **anonymises** (user_id→NULL) rather than deleting.
- Product decisions (`CLAUDE.md`): grind setting stored as **free text**; "normalization across grinder models is not attempted"; aggregation = median + IQR, outliers at 2 SD.
- RLS (`supabase/migrations/20260330193506_initial_schema.sql`): `grinders_select`, `beans_select`, `recipes_select` are `for select using (true)` — the data is technically world-readable to anyone holding the project's public anon key.
- Terms (`app/terms.tsx` lines ~101–113, 126, 148): contributors retain ownership; grant Caliburr "a worldwide, royalty-free licence to store, display, and distribute it **to other users of the service**"; "Use automated scripts or bots to interact with the service" is listed as prohibited; contributed data "shared under the licence described in the User Content section" (i.e. not an open licence).
- **Conclusion (Blocked):** no documented public API, no data export, no open licence, no third-party redistribution right, bots prohibited. I deliberately did **not** query the live backend. Code in the repo is all-rights-reserved by default (no LICENSE). Design ideas (governance, adjustment-type enum) can inform an independent design; code and the seed grinder/bean lists should not be copied.

---

## 5. Commercial / closed apps (store-page evidence only)

Source code unavailable for all three; claims below are **published claims, not verified behaviour**.
- **Act Four** (App Store id6473959855, AIV, LLC, v1.6.0 dated 2024-08-01 on the page; page copyright says 2026): iPhone, Acaia (full from 1.4.1), Skale2, Felicita Arc; **SwitchBot brew-by-weight as a $19.99 IAP** (start with pre-infusion, stop at target weight); recipe/flow tracking; analytics (random session id); no export mentioned. Maintenance signal: last release listed >2 years ago.
- **Grindstep** (App Store id6789557773, Huihui Gao; v1.0 2026-07-16 → v1.2 2026-09-20): iPhone, on-device, no account, "does not collect any data" (Apple-unverified label), **JSON archive export + CSV export, import "compatible coffee logs"**, Pro: First Shot Forecast (starting grind from previously dialled-in bags, shows math/confidence); one grinder/machine free.
- **Grindset** (grindset.cafe, one-person project, web sign-up; free/$5 mo): shot log, "Dial-In Coach" (natural-language, AI), shared recipes with ratings, leaderboards, export of full shot history; no API, no Terms/Privacy links visible on the fetched page; no open-source claim.
- **Others seen only as search snippets (Plausible, not inspected):** Burrfect (crowd-sourced start recipes), Coffee Notes: Espresso Journal / Beano (paid community dial-ins), GrindConverter (200+ grinders, converter), Dialed In (local only), Shot Dialer (web beta).

---

## 6. Other projects (license facts from GitHub API / LICENSE files read 2026-10-12)

| Project | License read | Notes |
| --- | --- | --- |
| Zer0-bit/gaggiuino | **CC BY-NC 4.0** (LICENSE text read) | Not OSI-open, non-commercial. 2.7k★, pushed 2026-10-08. |
| jniebuhr/gaggimate | **CC BY-NC-SA 4.0** (README §License; no LICENSE file; GitHub API says none) | Has `schema/{profile,shot_history,shot_notes}.json`. 964★, pushed 2026-10-10. |
| decentespresso/decaid | GPL-3.0 (LICENSE.txt header; GitHub shows NOASSERTION) | Flutter gateway for Decent. |
| jeffsf/pyDE1 | GPL-3.0 | last push 2023-08-15 (stale). |
| ALERTua/hass-gaggiuino | GPL-3.0 | HA HACS. |
| gaggimate/ha-integration | none declared | HA integration for GaggiMate. |
| mxkissnr/gaggiuino-local-profiler | GPL-3.0 | local shot recorder for Gaggiuino+GaggiMate. |
| tatemazer/AcaiaArduinoBLE | MIT | upstream of OBBW scale code. |
| beat843796/acaia-lunar-ble | MIT | Python Lunar library. |
| RoButton/switchbotpy | MIT | last push 2024-07-28. |
| OpenWonderLabs/SwitchBotAPI-BLE | no license file | Official BLE docs; Bot: `0x57 0x01` execute action; `0x57 0x0f 0x08 <s>` set long-press duration; response `0x01` = action executed (command acknowledged, not physical effect). |
| daniilr/grinder-converter | MIT | 74 grinder modules; per-setting **sieve-bucket mass distributions** (>900, 900–600, 600–300, <300 µm); `measured` vs interpolated flag; quality flag. **Data provenance ("source sieve measurements") not stated in the files read** → treat redistribution as Unknown. |
| jldbc/coffee-quality-database | MIT repo (underlying CQI data terms unconfirmed) | 2018 scrape, no roast date. |
| Meticulous | no official public API docs found; community OpenAPI repo `ohheyitsdave/Meticulous-OpenAPI-Documentation` has no license | Visualizer parser exists for Meticulous JSON. **Unknown**/Plausible. |

---

## 7. This project's existing design (baseline for all comparisons)

`docs/COMMUNITY.md` (799 lines) already specifies: Coffee/Bag/Recipe/Grinder split (§2); a **GitHub-hosted library** with `coffees/`, `grinders/`, `index/`, contributor repos, **signed records** pushed to an `inbox` branch, intake workflow (envelope, signature, range checks), PR-based review for coffees and grinders (§5, §7, `library-template/tools/{intake,build_index,reviews,verify_record}.py`), latent-axis grind fit with per-model scale and per-grinder offset, flow-based normalisation (§8d), bean-age allowances (§8b), machine shared list (§8a-2). Open items listed in project memory: machine not in the fit; no prior for undialled coffees; KAPPA prior unvalidated; nothing run end-to-end with a near-recipe library.
Existing license: **AGPL-3.0-or-later** (`LICENSE`, README §12) because unmodified OBBW AGPL files are vendored.
