# Espresso ecosystem research — navigation and synthesis

**Last verification date:** 2026-10-12 (all upstream facts were read on this date; commit pins in `source-code-findings.md`).
**Location:** `C:\tools\Brew_by_Weight\bdb-bbw\research\` (the project repo `bdb-bbw`; kept apart from `src/`; no existing file was modified; nothing committed or pushed).

## Scope and honesty statement
Objective: determine what an open espresso platform should reuse, integrate with, contribute to or build, backed by source-level evidence.
**What was actually done:** shallow-cloned and read OBBW (Advanced-Shot-Stopper), Beanconqueror, Caliburr (public repo), Visualizer.coffee, grinder-converter; read the SwitchBot BLE doc; checked licenses via LICENSE files / GitHub API for ~20 repos; fetched store/web pages for Grindset, Grindstep, Act Four; ran web searches for datasets, Meticulous, Loffee Labs; compared everything with this project's own `docs/COMMUNITY.md`.
**What was not done:** no code was executed, no tests run, no hardware or live service called (Caliburr's backend was deliberately *not* queried); Grindstep/Grindset/Act Four/Burrfect/etc. are closed-source so only store/marketing claims exist (labelled *Documented but untested*); Meticulous, Xenia, Sanremo, Move2, HA's La Marzocco integration, commercial BBW devices, Visualizer ToS, and data-licensing of CQI/SCA vocabularies were **not** investigated in depth (see blind spots in `ecosystem-inventory.md`). The Play Store page for Caliburr could not be fetched; the public repo was used instead and its identity confirmed via the bundle id.
Discovery was stopped when passes mostly re-found the same cluster, not proven exhaustive.

> **Update 2026-10-12:** after reading `bdb-bbw`'s source, several recommendations here were corrected or found already implemented — see `roadmap-validation.md` before relying on `implementation-roadmap.md`, ADR-001..003 or integration items I2, I6, I7.

## Documents
| File | Contents |
| --- | --- |
| `ecosystem-inventory.md` | project inventory, licenses, dispositions, search strategy, blind spots |
| `source-code-findings.md` | symbols/paths/commit pins, schemas, APIs, algorithms |
| `data-model-analysis.md` | schema comparison, canonical model, field mappings, identity/versioning |
| `backend-assessment.md` | storage roles × candidate stores, phased architecture |
| `integration-catalog.md` | each integration with status, flows, risks, validation |
| `license-and-risk-register.md` | license matrix, dataset restrictions, risks, counsel questions |
| `architecture-decisions.md` | ADR-001…010 (provisional) |
| `implementation-roadmap.md` | phases, effort estimates, acceptance criteria |
| `open-questions.md` | unanswered items, experiments |
| `roadmap-validation.md` | **review of the roadmap against the current `bdb-bbw` code, reproduced test runs, integration validation, corrections to earlier claims** |
| `gap-analysis.md` | **feature-gap matrix vs. OBBW/Beanconqueror/Visualizer/Caliburr/Gaggia projects/closed apps, logic lessons, import and reuse plan, ranked recommendations** |
| `grind-logic-review.md` | **review of the grind-adjustment logic (per-shot analysis, adjustment, learning, first-shot theory): six defects found and fixed behind "Learn the grinder", one page bug fixed, simulation evidence, what remains** |
| `dial-in-engine.md` | **strategy review (goals, conflicts, efficiency of the changes), the formulaic learning engine (one log-flow equation in layers), hypotheses H1-H12 with tests, data-capture coverage, revised plan E0-E6** |
| `dial-in-program.md` | **THE CURRENT FOCUS: first-shot and continuous dial-in toward a two-shot goal, shot-preparation (flow problem) detection, the real-data plan, and the step order** |
| `roadmap-integrations.md` | **reserved: integrations, with the gate and order for when real data exists** |
| `starting-grind-and-integration-impact.md` | **the first-shot starting grind (pillar B): how it works, how others do it, simulation of what costs shots, improvements S1-S7; impact analysis of every outside integration against the three pillars** |
| `app-landscape-and-feature-plan.md` | **15 apps reviewed (features, integration points, what to learn) and a per-feature implementation plan F1-F19 that augments, never replaces, the core advice** |
| `expanded-ecosystem-review.md` | **Beanconqueror, Grindstep, Grind Finder (unresolved), GaggiMate/Gaggia MCP, Roastbook, AI dial-in models: shared features, reusable logic, connections, augmentations that leave the core advice untouched** |
| `assumptions-validation.md` | **what research could and could not confirm: the Grind-by-Sync parameters, rating thresholds, the freezer fraction, the similar-coffee step; what the code does about each** |
| `implementation-plan.md` | **plan to close the gaps, reach parity and connect to existing apps; records what was built on 2026-10-13** |
| `validation/` | runnable harness for the Beanconqueror hand-off check |

## Major findings
1. **Visualizer.coffee is the most valuable external system found and was not on the starting list.** MIT code, OpenAPI 3.1, OAuth, uploads Beanconqueror/Gaggiuino/GaggiMate/Meticulous/Decent files, and the **only shared-vs-user coffee identity split** (`canonical_coffee_bags`). Caveats: its canonical data comes from Loffee Labs under unknown terms; its grind suggestion is an admin-only hosted-LLM call.
2. **Beanconqueror publishes a real, versioned inbound contract** (`beanconqueror://ADD_BREW`, JSON+gzip+base64url, `v:1`) with explicit validation limits. It is the verified path to the largest open-source logbook. It has no grinder model (just a name + free-text setting) and no SwitchBot/button-pusher support (`grep` found none).
3. **OBBW is the strongest reusable component** — scale protocol descriptor table (10 families), machine JSON profiles, a documented LAN/webhook API with HA integration — and it is **AGPL-3.0**, which this project already inherits by vendoring its files. OBBW is *wired relay*, forbids remote start by default, and has no dial-in or community layer.
4. **Caliburr's data is not reusable**: public repo without any license, ToS bans bots and licenses content only to its service's users, no API/export. Its *design* (verification threshold, edit tables, adjustment-type enum, median+IQR, anonymise-on-delete) is instructive.
5. **No existing system models grinder-setting calibration.** Caliburr stores range/adjustment type only and "does not attempt normalization"; Beanconqueror/Visualizer store free strings. `grinder-converter` (MIT) gives measured particle-size distributions for 74 grinders but with unstated provenance and no dial↔flow map.
6. **Non-commercial licenses block the Gaggia ecosystem** (Gaggiuino CC BY-NC 4.0; GaggiMate CC BY-NC-SA 4.0). Interoperate only through user-owned files.
7. **No existing system persists the full chain** *shot → grinder revision → recommendation → adjustment outcome*. The `adjustment` entity and grinder revisions in the proposed model are genuinely new.
8. The project's own `COMMUNITY.md` already contains a GitHub-hosted, signed-record, PR-reviewed library design; this research found nothing that should replace it, and several things (Visualizer canonical IDs, Beanconqueror/Visualizer export) that complement it.

## Key assumptions
Owner is the author of `hallge/Brew-Controller` / `bdb-bbw` and wants it to stay AGPL-compatible; hardware is Breville Dual Boiler + Acaia Lunar + SwitchBot Bot; "current project directory" = `bdb-bbw` (the session's working directory flipped between `C:\tools` and this repo; the research lives in the repo).

## Final synthesis — the 12 questions
1. **Strongest reusable component:** OBBW's scale protocol table (already vendored) for scales; OBBW machine-profile JSON as a naming/versioning model; for logging, Visualizer's upload API (not a controller). No existing project offers a button-pusher driver contract; the SwitchBot BLE doc is the spec.
2. **Beanconqueror:** complementary logbook and **import target** (verified contract); **legacy import source** (full-export JSON; format not traced in detail); *not* a reference-data source and not worth extending (GPL TypeScript/Ionic; no grinder model).
3. **Caliburr:** no reusable data (Blocked). Best viable alternative: independent registry seeded from manufacturer documentation + contributor submissions with PR review (this project's design), optional `grinder-converter` prior after provenance check, optionally ask the operator for a license.
4. **Most useful schema:** for coffee identity — Visualizer (canonical vs user); for grinder entity — Caliburr (adjustment type, range, step, verification), but neither captures calibration; for shot telemetry — Beanconqueror `flow` envelope; for machine control — OBBW profiles.
5. **Existing canonical model to adopt?** None. Extend a union (`data-model-analysis.md`); borrow Visualizer's canonical/user split, Caliburr's adjustment-type enum, OBBW's `schema_version/compatibility_revision` pattern.
6. **Existing hardware abstractions:** OBBW `ScaleProtocol` + features (scale), OBBW machine JSON (relay control), Beanconqueror `BluetoothScale` (TS). Differences: per-protocol (not per-instance) capabilities, no stability/battery/timestamp contract, relay semantics instead of button sequences, no actuator confirmation levels.
7. **Community/historical data holders:** Visualizer (public shots + canonical coffees; terms unread), Caliburr (Blocked). Individual-only: Beanconqueror, Grindstep, Act Four, OBBW devices. Grindset: shared recipes, closed.
8. **Gaps:** grinder calibration; batch-level coffee identity; shot→adjustment→outcome chain; button-pusher abstraction with confirmation; open governance of shared grinder data; telemetry portability from closed apps.
9. **Dispositions:** *Adopt* OBBW scale table, SwitchBot BLE spec; *Integrate* Beanconqueror ADD_BREW, Visualizer upload and canonical lookup; *Contribute upstream* scale fixes (OBBW), possibly a Visualizer parser/grinder entity (owner decision); *Support as legacy* Gaggiuino/GaggiMate/Beanconqueror/Grindstep imports from user files; *Compete* Act Four, Grindstep/Grindset dial-in, Caliburr; *Defer* HA integration, Meticulous, CQI datasets, bulk canonical import; *Reject* Caliburr data, NC-licensed code/schemas, SwitchBot cloud API for control.
10. **Minimum architecture for a useful public release:** controller + scale + actuator drivers with the contracts above; local storage + JSON export; companion page with Beanconqueror/Visualizer hand-off; GitHub library (grinders/machines/coffees via PR, schema CI, signed dial-ins, built `index/`); AGPL firmware, CC0 catalog, safety statement.
11. **Decisions needed before publishing:** licenses and contributor terms; catalog/data license and consent wording; safety/non-appliance statement; hardware license; whether to propose schemas upstream; token policy (`github keys.txt`); naming/trademark; privacy policy for shot data.
12. **Uncertainties that could invalidate the design:** (a) Bot press-to-flow latency/jitter too large for target accuracy; (b) Lunar reliability on the USB-C variant; (c) dial-in fits too sparse/confounded to beat per-model medians; (d) GitHub rate/CDN/token friction; (e) Visualizer or Beanconqueror contract drift; (f) AGPL scaring off hardware partners; (g) legal status of third-party grinder measurement data.

## Executive summary
The ecosystem is fragmented around three poles — controllers (OBBW, Gaggiuino, GaggiMate), personal loggers (Beanconqueror, Visualizer, closed iOS apps) and closed community dial-in apps (Caliburr, Grindset, Burrfect). Reusable, openly licensed building blocks are limited to OBBW's scale protocols and profile conventions, Beanconqueror's documented import contract, Visualizer's MIT API and canonical coffee model, and MIT grinder measurements of unknown provenance. The Gaggia-based projects are non-commercial licensed and Caliburr is all-rights-reserved, so the community grinder/coffee registry must be built independently. The existing GitHub-based, reviewed-record design in this project is consistent with the evidence; the largest unbuilt pieces are the grinder-revision/calibration model, the adjustment-outcome record, and an actuator abstraction with explicit confirmation semantics.

## Prioritised integration matrix
| Priority | Integration | Status | Disposition |
| --- | --- | --- | --- |
| 1 | OBBW scale table | Verified, vendored | Adopt |
| 1 | SwitchBot BLE spec | Verified (doc) | Adopt |
| 1 | Beanconqueror ADD_BREW | Verified (doc) | Integrate |
| 2 | Visualizer upload / lookup | Verified (spec) | Integrate (optional) |
| 2 | grinder-converter prior | Verified files, provenance Unknown | Integrate (private) / Defer (public) |
| 3 | Gaggiuino/GaggiMate/Grindstep/Grindset import | Plausible / documented-untested | Support as legacy |
| — | Caliburr data | Blocked | Reject |
| — | HA, Meticulous, CQI | Unknown | Defer |

## Proposed canonical architecture
Device (scale driver ← OBBW table; actuator driver ← SwitchBot spec; machine profile; recipe engine; local store) → signed records → GitHub library (catalog PRs, inbox, intake CI, built `index/`) → static index back to device; adapters on phone/PC for Beanconqueror/Visualizer/Grindstep files. See `backend-assessment.md` §4 diagram and `data-model-analysis.md` §2.

## Verified vs unverified
**Verified (read):** OBBW contracts and license; Beanconqueror import contract and license; Visualizer OpenAPI, schema, license, grind-suggestion code; Caliburr schema/terms/lack of license; SwitchBot Bot command bytes; Gaggiuino CC BY-NC; GaggiMate CC BY-NC-SA; grinder-converter MIT and data shape.
**Unverified:** every integration behaviour at runtime; store-page claims for Grindstep/Grindset/Act Four; Meticulous API; Loffee Labs/Visualizer data terms; GitHub limits; hardware performance.

## Highest risks
Technical: actuator confirmation/safe-state on toggle machines; Lunar USB-C reliability; sparse/confounded dial-in data. Licensing: Caliburr reuse temptation, NC contamination, third-party measurement data, AGPL adoption friction, contributor terms. Operational: GitHub token handling, single-maintainer external services.

## Primary sources (retrieved 2026-10-12)
- https://github.com/Cheerpipe/Advanced-Shot-Stopper (commit `840900a`)
- https://github.com/graphefruit/Beanconqueror (`bbf8de0`)
- https://github.com/vincentvella/Caliburr (`ead41f9`); https://caliburr.coffee
- https://github.com/miharekar/visualizer (`0627baa`); https://visualizer.coffee/api
- https://github.com/daniilr/grinder-converter (`f49effa`)
- https://github.com/OpenWonderLabs/SwitchBotAPI-BLE
- https://github.com/Zer0-bit/gaggiuino · https://github.com/jniebuhr/gaggimate · https://github.com/decentespresso/decaid
- https://grindset.cafe/ · https://apps.apple.com/us/app/grindstep-espresso-dial-in/id6789557773 · https://apps.apple.com/us/app/act-four/id6473959855
