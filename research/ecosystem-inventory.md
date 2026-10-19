# Ecosystem inventory

Verified 2026-10-12. Evidence column: **V** = Verified (source/official doc read), **D** = Documented but untested (store page / maintainer claim), **P** = Plausible, **U** = Unknown, **B** = Blocked. Detail and commit pins are in `source-code-findings.md`.
Disposition vocabulary: Adopt / Integrate / Contribute upstream / Fork / Support as legacy / Compete / Defer / Reject.

## A. Controllers and control hardware

| Project | URL | License | Stack / model | Maintenance (observed) | Relevant interface | Overlap | Evidence | Disposition |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Open Brew by Weight (OBBW)** | github.com/Cheerpipe/Advanced-Shot-Stopper | AGPL-3.0-or-later | ESP32-S3+PSRAM, ESP-IDF 6.1 NimBLE, relay (wired) | v0.0.8, commit 2026-10-12, very active | `ScaleProtocol` table; machine JSON profiles; LAN integration API; webhook; HA integration | **High** (BBW stop, scales, presets) but wired relay, no recipe/dial-in/community | V | **Adopt** scale protocol table (already vendored); **Integrate** webhook/HA; **Contribute upstream** scale fixes; machine-profile schema: Support compatible |
| **This project (BDB BBW)** | hallge/Brew-Controller (local `bdb-bbw`) | AGPL-3.0-or-later (derived files) | ESP32-S3 AMOLED, NimBLE-Arduino, SwitchBot Bot BLE | active | community library (GitHub), emulator | — | V (local) | baseline |
| Gaggiuino | github.com/Zer0-bit/gaggiuino | **CC BY-NC 4.0** | STM32+ESP32 mod for Gaggia Classic | active, 2.7k★ | shot JSON (`datapoints`), HA via 3rd party | Partial (machine-internal control) | V (license), P (design) | **Reject** code reuse; **Support as legacy** (import via Visualizer-format files) |
| GaggiMate | github.com/jniebuhr/gaggimate | **CC BY-NC-SA 4.0** (README) | ESP32 mod for Gaggia | active, 964★ | `schema/{profile,shot_history,shot_notes}.json`, HA integration (unlicensed) | Partial | V (license) | **Reject** code reuse; interoperate by reading user-exported files only |
| Decent DE1 / decaid / pyDE1 | github.com/decentespresso/decaid; jeffsf/pyDE1 | GPL-3.0 | Flutter gateway / Python | decaid active; pyDE1 stale since 2023 | `.shot` TCL + JSON; BLE | Low (different machine class) | V (license), P | **Support as legacy** (shot import); **Defer** controller |
| Meticulous | (vendor) | proprietary | local HTTP+socket.io (community-documented) | n/a | no official public docs found | Low | U | **Defer**; revisit if Visualizer/Beanconqueror parsers suffice |
| La Marzocco Linea Mini controller | github.com/Sc00bs110/linea-mini-controller | none declared | ESP32-S3, GICAR protocol, BLE scale | active | — | Low | D (search snippet) | **Defer** (inspect only if a GICAR driver is wanted) |
| Bezzera Duo protocol | github.com/larszu/bezzera-duo-protocol | GPL-2.0 | ESP32 bridge | active | reverse-engineered serial | Low | D | **Defer** |
| Act Four + SwitchBot | App Store id6473959855 | proprietary, IAP | iOS app | last release 2024-08 (page) | none documented | **Direct competitor** for SwitchBot BBW | D | **Compete** (validates concept; no integration surface) |
| Original ShotStopper / AcaiaArduinoBLE | github.com/tatemazer/AcaiaArduinoBLE | MIT | Arduino | active (2026-08) | Acaia BLE | Upstream of OBBW scale code | V | **Adopt** (via OBBW) / reference |

## B. Scales and actuators (libraries & protocol docs)

| Item | URL | License | Evidence | Disposition |
| --- | --- | --- | --- | --- |
| EspressoScaleBLE (OBBW) | in OBBW repo `libraries/EspressoScaleBLE` | AGPL-3.0 (repo) | V | **Adopt** (protocol table only — transport is ESP-IDF-native, this project uses NimBLE-Arduino) |
| Beanconqueror `classes/devices/*` | github.com/graphefruit/Beanconqueror | GPL-3.0 | V | **Reference only** (TypeScript, different runtime; GPL-3 text is AGPL-compatible via §13 but no code path to reuse) |
| aioacaia / pyacaia / acaia-lunar-ble | github.com/beat843796/acaia-lunar-ble (MIT); aioacaia (HA) | MIT / (check) | V (MIT for acaia-lunar-ble), P others | **Reference** for protocol cross-checks (already done in `docs/RESEARCH.md` §1) |
| SwitchBot BLE open API | github.com/OpenWonderLabs/SwitchBotAPI-BLE | no license file (official vendor docs) | V (command bytes read) | **Adopt** as the protocol spec; implement own client; ask vendor/counsel about doc reuse |
| switchbotpy | github.com/RoButton/switchbotpy | MIT | V | Reference; stale since 2024-07 |
| SwitchBot Cloud API | OpenWonderLabs/SwitchBotAPI | no license file | D | **Reject** for shot control (cloud round trip; latency/offline) |

## C. Logging, recipe and community platforms

| Project | URL | License | Model | Data surface | Evidence | Disposition |
| --- | --- | --- | --- | --- | --- | --- |
| **Beanconqueror** | github.com/graphefruit/Beanconqueror | GPL-3.0 | local-first app (Ionic/Angular), 2.0.0, active | **`beanconqueror://ADD_BREW` import contract** (documented, versioned `v:1`); full-JSON export; Visualizer upload; machine integrations | V | **Integrate** (export a finished shot to it); **Support as legacy** (import its export); not a data source for community records (user-local) |
| **Visualizer.coffee** | github.com/miharekar/visualizer | **MIT** | hosted Rails service, OpenAPI 3.1 | OAuth upload of shots in 7 formats; canonical roaster/coffee search (unauthenticated); public shot list | V | **Integrate** (upload target / import source via API with user's own OAuth); **Defer** bulk harvesting of canonical data (upstream terms Unknown); optionally **Contribute upstream** a grinder entity |
| **Caliburr** | github.com/vincentvella/Caliburr; caliburr.coffee | **none (all rights reserved)** | Expo + Supabase | no documented API/export; ToS bans bots; data licence only "to other users of the service" | V (repo, terms) | **Reject** as data source (**Blocked**); **Compete**; borrow governance *ideas* only |
| Grindstep | App Store id6789557773 | proprietary | iOS on-device | JSON/CSV export (page claim) | D | **Support as legacy** (import its JSON/CSV if a sample is obtained); **Compete** on algorithm (Pro forecast) |
| Grindset | grindset.cafe | proprietary | web SaaS, AI coach | export of history (page claim); no API | D | **Defer**; **Compete** |
| Burrfect / Coffee Notes (Beano) / GrindConverter / Dialed In | App Store | proprietary | — | none known | P (snippets) | **Defer** (survey only) |
| gaggiuino-local-profiler | github.com/mxkissnr/gaggiuino-local-profiler | GPL-3.0 | HA add-on, local shot store | — | D | **Defer** |
| Typica | typica.us | MIT (per search snippet) | roasting software | roast batch records | P | **Defer** (only relevant if roaster-side batch data is wanted) |

## D. Reference data

| Dataset / source | License / terms | Content | Evidence | Disposition |
| --- | --- | --- | --- | --- |
| Visualizer canonical roasters / coffee bags | service code MIT; **data terms Unknown**; upstream feed from Loffee Labs (private key) | roaster + bag attributes, last-3-months bias | V (schema/importer), U (terms) | **Defer** bulk import; **Integrate** read-only lookup/autocomplete via public endpoint, subject to rate limits (50/min/IP) |
| grinder-converter (MIT) | MIT on code+data files; **measurement provenance not stated** | 74 grinders × settings × 4-bucket particle mass | V (files), U (source terms) | **Integrate/Adopt as optional prior** after provenance check; **Defer** redistribution inside the public catalog until confirmed |
| Caliburr seed grinders (migration `20260418000002_seed_equipment.sql`) | all rights reserved repo; facts (brand/model/range) | ~50 grinders with range/adjustment type | V | **Reject** copying; re-derive from manufacturer manuals |
| CQI coffee quality database (jldbc scrape, 2018; Kaggle 2023) | repo MIT but underlying CQI terms unconfirmed | cupping scores by lot, no roast date | P | **Defer** |
| Open Food Facts / Coffee Review / retailer scrapes | not investigated | — | U | **Defer** (blind spot) |
| SCA/WCR flavor wheel / sensory lexicon | not investigated for licensing | vocabulary | U | **Defer**; verify licence before adopting as enum |

## E. Home Assistant and tooling
| Item | License | Evidence | Disposition |
| --- | --- | --- | --- |
| OBBW HA integration (`integrations/OpenBrewByWeight`) | AGPL-3.0-or-later | V | **Integrate** pattern for this project's own HA integration |
| hass-gaggiuino | GPL-3.0 | D | Defer |
| gaggimate/ha-integration | none | D | Defer |
| La Marzocco HA core integration | not inspected | U | Defer |
| MCP servers for Gaggiuino/GaggiMate/Meticulous (several MIT) | MIT (per GitHub API for 2) | D | Defer (LLM tooling, off-scope) |

## Search strategy and blind spots

Strategy: user-listed targets first; GitHub repository search (`gh api search/repositories`) for gaggiuino, decent, meticulous, acaia, switchbot, gaggimate, "grinder settings", "coffee dial-in"; web search for dial-in apps, Visualizer API, Meticulous API, coffee datasets, Loffee Labs; dependency/cross-reference discovery from source (Visualizer's parser list led to Gaggiuino/GaggiMate/Meticulous/SEP; its importer led to Loffee Labs; Beanconqueror's `preparationDevice/` led to Xenia/Sanremo/Move2).
Failed or thin attempts: Play Store page for Caliburr returned truncated content to the fetch tool (the repo and `caliburr.coffee` meta description were used instead); `caliburr.coffee` is a JS SPA (no static content); Loffee Labs had **no** search hits; GitHub topic-style searches for "espresso grinder settings json" and "coffee dial-in open source" returned 0 repos.
**Blind spots:** Xenia/Sanremo/Move2 internals; Meticulous API (no first-party docs); Typica/roaster-side data; commercial brew-by-weight devices (Brewista, Acaia Pearl BBW features, Bookoo Themis/Espresso Monitor firmware); `pyacaia`/`aioacaia` licenses; Pressensor; Smart Espresso Profiler; Coffee Review/Cropster/Roest data; Weighmybru project; Shot Dialer; non-English communities; the "Brew by Weight Pro/Mini" commercial units (only PDFs present locally); discovery passes were stopped when new passes mostly re-found the same set (Visualizer/Beanconqueror/OBBW cluster), not exhaustively.
