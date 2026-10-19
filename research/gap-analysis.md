# Feature-gap, logic-learning and data-import analysis

> **Update 13 Oct 2026:** most of what this lists as a gap is now built; see `implementation-plan.md` (status block at the top). Two statements here were wrong and are corrected: **curves are stored for every shot** (G5 / B4 need no new storage), and ratings now **refine** the time advice instead of replacing the target. The 3% similar-coffee step proposed in G2 was unsupported by any source and has been removed (`assumptions-validation.md`).

Dated 2026-10-12. Baseline = `bdb-bbw` at `5520bea` + working tree. Competitors were read as in `source-code-findings.md` (commit pins there). Evidence labels as before; **closed apps are store-page claims only**.
Licence guard rail: Gaggiuino (CC BY-NC) and GaggiMate (CC BY-NC-SA) were read for *ideas and file formats only*; nothing below proposes copying their code, schemas or text. Visualizer is MIT, OBBW is AGPL (already vendored), Beanconqueror GPL-3, Caliburr all-rights-reserved (ideas only).

How to read: **§1** feature matrix; **§2** gaps (they have, we don't); **§3** like-for-like features where another implementation shows something worth learning; **§4** where we are ahead (do not trade away); **§5** importing and reusing existing data and integrations; **§6** ranked recommendations.

---

## 1. Feature matrix (control, dial-in, data)

● = implemented (read in source/docs), ◐ = partial, ○ = absent, ? = not determinable. Closed apps: marked *(claim)*.

| Capability | bdb-bbw | OBBW | Beanconqueror | Visualizer | Caliburr | GaggiMate / Gaggiuino | Act Four / Grindstep / Grindset *(claims)* |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **Non-invasive actuator** (BLE pusher) | ● 3 makes (SwitchBot, MicroBot, Tuya) | ○ relay (wired) | ○ | ○ | ○ | ○ (internal control) | Act Four: SwitchBot (IAP) |
| Machine styles (toggle/start-only/two-button/hold/manual) | ● 5 | ◐ latch/paddle/momentary/reed | ○ | ○ | ○ | n/a | Act Four: "most button machines" |
| Multi-scale support | ● table-driven, ~10 families | ● 10 families | ● ~40 devices (scales, thermometers, pressure) | ○ | ○ | ◐ BLE scales | Act Four: Acaia, Skale2, Felicita |
| Target-weight stop | ● 2-sample + fit of last 10 readings | ● same + guards | ◐ brew-by-weight code paths exist in barista mode and a graph "cut" tool (not inspected in depth) | ○ | ○ | ● | ● |
| Stop confirmation + retry | ● (`stop_confirmed`, `retried`, latency) | ● | ○ | ○ | ○ | n/a | ? |
| Offset learning (EWMA) + adaptive gain | ● (ported selector) | ● | ○ | ○ | ○ | ? | ? |
| **Fast-extraction guard** (extend a too-fast shot) | ○ | ● | ○ | ○ | ○ | ○ | ○ |
| **Slow-extraction guard** (cut a too-slow shot) | ◐ time limit only | ● | ○ | ○ | ○ | ◐ phase timeouts | ○ |
| **Scale-lost handling** | ◐ stops the shot (`SCALE_LOST`) | ● deadline from trend of last 5 shots, keeps reconnecting | n/a | n/a | n/a | ? | ? |
| Accidental-bump rejection / sustained-weight backup | ◐ slew limiter (100 g/s +20 g) | ● dedicated | n/a | n/a | n/a | ? | ? |
| Start-of-shot protection window; cup presence rules | ◐ cup modes, `requireCup`, cup-removed stop | ● configurable window, retare | n/a | n/a | n/a | ? | ? |
| Pre-infusion hold / bloom pause | ● (hold button; bloom pause 1–45 s) | ◐ | ○ | ○ | ○ | ● phase profiles | Act Four: preinfusion |
| Recipe model (coffee × recipe × grinder preset) | ● | ◐ presets | ◐ per brew | ◐ per shot | ◐ recipes | ● profiles | ◐ |
| **Dial-in advice** | ● time vs target, µm/s measured, outliers set aside | ○ | ○ | ◐ LLM, admin-only | ○ (community medians) | ○ | Grindstep/Grindset/Act Four ● *(claim)* |
| Taste-aware advice | ○ (rating/verdict stored, unused) | ○ | ○ | ◐ LLM uses enjoyment + notes | ○ | ○ | Act Four, Grindset *(claim)* |
| Best-rated reference shot | ○ | ○ | ◐ `best_brew` flag + reference graph | ● used in advice | ○ | ○ | ? |
| **Starting suggestion for an undialled coffee** | ◐ community median/fit; **none without peers** | ○ | ○ | ◐ "similar bag" LLM | ◐ median+IQR per grinder+method | ○ | Grindstep "First Shot Forecast" *(claim)* |
| Cross-grinder normalisation | ● fit (scale/offset/kappa) | ○ | ○ | ○ | ○ (explicitly not attempted) | ○ | Burrfect/GrindConverter *(claim)* |
| Bean ageing model | ● **measured** µm/day from pairs | ○ | ◐ dates, frozen, best date | ◐ dates, frozen/defrosted | ◐ roast date field | ○ | Grindset peak window *(claim)* |
| Frozen-bean handling | ○ | ○ | ● dates/ids/groups | ● frozen_date/defrosted_date | ○ | ○ | ? |
| Full-curve storage | ◐ recent shots only (`has_data`) | ◐ last curves | ● flow file per brew | ● | ○ | ● | ? |
| Curve overlay vs reference | ○ | ○ | ● reference graph | ● chart compare | ○ | ● shot analyzer | ? |
| Flow-shape features (25/50/75 %, accel., roughness) | ● | ○ | ○ | ○ | ○ | ◐ | ? |
| Shot charts: control chart / scatter | ◐ scatter (grind vs time) | ◐ stats | ● many | ● | ○ | ● | Grindstep "Brew Control Chart" |
| TDS / extraction yield | ○ | ○ | ● (+ refractometer devices) | ● fields | ○ | ○ | Grindstep logs TDS |
| Sensory scoring | ○ (1–5 + sour/balanced/bitter) | ○ | ● cupping/flavour wheel | ● 8 axes | ◐ tags | ◐ balance | Grindset tasting notes *(claim)* |
| Maintenance counters (backflush/descale/burrs) | ○ | ◐ activation history, automatic backflush | ○ | ○ | ○ | ◐ | ? |
| Machine telemetry (pressure/temp) | ○ (hardware cannot) | ○ | ◐ via devices | ● | ○ | ● | ○ |
| Public community library, reviewed | ● PR review, signed records | ○ | ◐ QR/NFC share | ● public shots | ● verification (5 confirmations) | ◐ profile sharing | Grindset shared recipes |
| Signed/attributable contributions | ● ECDSA P-256 | ○ | ○ | ○ (accounts) | ○ (accounts) | ○ | ○ |
| Versioned external API / webhook / HA | ○ (`/api/status` unversioned) | ● `apiVersion`, capabilities, webhook, HA | ◐ deep-link import | ● OpenAPI + OAuth | ○ | ◐ HA (3rd party) | ○ |
| Import from other apps | ○ | ○ | ● machine shots (Gaggiuino/GaggiMate/Meticulous), file import | ● 7 formats | ○ | ○ | Grindstep imports "compatible logs" *(claim)* |
| Export | ● CSV + JSON | ● CSV | ● JSON; deep link ADD_BREW | ● download | ○ | ● JSON | JSON/CSV (Grindstep) |
| OTA, audit log, crash archive, watchdog | ◐ OTA + watchdog present; crash/audit not assessed | ● all, with image identity | n/a | n/a | n/a | ? | n/a |
| Offline / local-first | ● | ● | ● | ○ (hosted) | ○ (hosted) | ● | Grindstep on-device |

---

## 2. Gaps: what others have and we do not

Ranked by value for *this* product (non-invasive BBW + dial-in + community), with the conflict risk against existing design called out.

### G1. Rating/taste is stored but never acts — **High value, low–medium effort**
- **Others:** Visualizer's advice picks a **reference shot** — the best-rated earlier shot of this coffee on this profile, otherwise the median of ≥3 typical earlier shots — and labels the new shot relative to it (time/ratio *much shorter/shorter/about the same/longer/much longer* at ±10 % / ±25 %); the LLM scores seven levels finer…coarser and the code only shows a direction when probability ≥ 0.6 (`app/models/shot/grind_suggestable.rb`, `MIN_DIRECTION_PROBABILITY`, `REFERENCE_LIMIT = 20`). Act Four and Grindset claim taste-driven adjustment.
- **Us:** `ShotRecord::rating`/`taste` exist; `/api/shot/rate` accepts them; your own `FEATURES.md` #13 calls this "the most valuable step on this list"; advisor targets a fixed `targetTimeS`.
- **Learn:** keep the deterministic core, but make the *target* data-driven: per coffee+recipe, target time/ratio = median of the shots rated ≥ the user's best band (min n, e.g. 3 rated shots, tolerance bands like Visualizer's). Use `taste` (sour → finer/longer; bitter → coarser/shorter) as a tie-breaker when time is in tolerance. Show "why" as you already do. No LLM needed.
- **Risk:** small samples; mark as advisory, never override the time rule until n is reached.

### G2. Prior for a coffee nobody has dialled — **High value, medium effort (your own open item)**
- **Others:** Visualizer's *similar bag* logic: when you have no shot of a new bag, it takes your best-rated dial-in of the most similar earlier bag **on the same grinder and profile** and applies an ordinal relative adjustment (much finer / slightly finer / same / slightly coarser / much coarser) judged on `roast_level, country, region, variety, elevation, processing, tasting_notes`. The rubric (`RELATIVE_CRITERIA`) is explicit: lighter roast or denser, higher-grown washed → finer; darker, softer, lower-grown, natural/anaerobic → coarser. Caliburr: median + IQR for grinder × brew method, outliers at mean ± 2 SD.
- **Us:** `Community.h` needs peers on the same coffee; "an unmarked dial with no fit gets no suggestion"; project notes list "no prior for a coffee nobody dialled; roast-level relation unverified".
- **Learn:** a **second-tier suggestion**: nearest *attribute neighbour* among the owner's own dialled coffees (distance on roast level, process, altitude band, origin), shifted by a small per-step amount, with the range widened and the label "from a similar coffee — a guess". Treat Visualizer's direction rubric as *hypotheses to test*, not facts: your library can estimate each attribute's effect from real pairs the same way it estimates bean-age rate (§8b), and only publish an effect once n ≥ threshold. Caliburr's mean ± 2 SD is weaker than your medians; do not adopt it.
- **Risk:** the roast-level→grind direction is plausible but **unvalidated** here; ship as "unverified prior" until data supports it.

### G3. Frozen-bean handling — **Medium value, low effort, correctness issue**
- **Others:** Beanconqueror (`frozenDate, unfrozenDate, frozenGroupId, frozenStorageType`) and Visualizer (`frozen_date`, `defrosted_date`).
- **Us:** the age model counts from the roast date only; no frozen concept (grep: none). A coffee frozen for 6 weeks would be treated as 6 weeks stale and shifted ~finer by the measured rate.
- **Learn:** add `frozen_days` (or frozen/thawed dates) to the bag/coffee and compute *effective age = days since roast − frozen days × factor* (factor to be set conservatively, e.g. 0 = frozen time does not age, until measured). Without it, the age allowance can push the start suggestion the wrong way.

### G4. Extraction yield (TDS) — **Medium value, low effort, interoperability bonus**
- **Others:** Beanconqueror stores `tds` and EY; Visualizer stores `drink_tds`, `drink_ey`; Grindstep logs TDS; Beanconqueror supports DiFluid refractometers.
- **Us:** no TDS field. Time + weight is a proxy; EY (TDS × yield / dose) is the objective outcome that separates "fast but fine" from "fast and under-extracted".
- **Learn:** optional per-shot typed TDS (%), compute EY, show next to rating. Record it in the share record only if opted in. It also makes imports from Beanconqueror/Visualizer lossless on this axis.

### G5. Control chart and reference overlay — **Medium value, low–medium effort**
- **Others:** Grindstep "Brew Control Chart" *(claim)*; Beanconqueror reference graph; Visualizer chart compare.
- **Us:** grind-vs-time scatter exists (`gchart`); no control chart; curves only for recent shots.
- **Learn:** (a) control chart = shot time per shot against target ± tolerance bands, with setting changes marked — all data already in history; (b) overlay the current shot on the best-rated one. For (b) storage must grow: a curve is 8 B/point; **a downsampled curve (e.g. 2 Hz ≈ 70 points ≈ 560 B)** for every shot would fit 500 shots in ≈ 280 kB — an estimate to verify against the flash budget.

### G6. Maintenance counters — **Medium value, low effort**
- **Others:** OBBW keeps activation history (`shot`, `rinse`, `backflush`…) and even automatic backflush on supported machines; `FEATURES.md` #14 already plans counters.
- **Learn:** shots since backflush/descale/burr clean with "done" buttons; Bot battery from `PusherInfo`. No inference required. Counters also give the "slow drift" detector (#15) something to subtract.

### G7. Extraction guards — **Medium value, medium effort, *design conflict***
- **Others:** OBBW Fast guard (target reached before min brew time ⇒ keep extending up to a max recovery weight) and Slow guard (target not reached by decision time ⇒ cut at a recovery weight). On by default there.
- **Us:** none; `TimeLimit` only.
- **Conflict:** your dial-in advisor interprets shot time as signal. A guard that extends/cuts a shot makes the time **censored** (time-to-target no longer observed). If adopted: flag such shots (`extended_min_time`, `slow_max_time` analogues), exclude them from the µm/s fit and treat them as "direction known, magnitude unknown". Alternatively skip guards: the product's purpose is dial-in, and the guards hide what the advisor needs to see.
- **Recommendation:** **defer**; revisit only if users ask for "rescue" behaviour.

### G8. Scale-loss deadline instead of immediate stop — **Low–medium value**
- **Others:** OBBW's A→M guard keeps trying to reconnect and only stops at a deadline derived from the last five shot durations; resumes weight control on reconnect (three coherent samples).
- **Us:** `SCALE_LOST` stops the shot at once (safe, but a single BLE hiccup ends the shot early).
- **Learn:** a short reconnect grace tied to the recipe's typical duration; resume control if the scale returns. Keep the immediate stop as the safe default until hardware tests show how often drops happen (Lunar USB-C is "Hit or Miss" upstream).

### G9. Versioned external surface — **Medium value for ecosystem, low effort**
- **Others:** OBBW's `GET /api/v1/integration` returns `apiVersion`, `minimumClientApiVersion`, stable `deviceId`, `capabilities[]`, `lastShot`, `stats`; durable mutations return `202` + `requestId` and are polled until `PERSISTED`; single webhook (`start/firstDrop/end`) with `schemaVersion`.
- **Us:** `/api/status` (no version/capabilities), `/api/shots`, `/api/shot/data`; no webhook, no HA integration.
- **Learn:** add `apiVersion` + `capabilities` to `/api/status` now (cheap, prevents later breakage), add a stable `/api/last` contract (exists), optionally a single webhook. OBBW's *unauthenticated LAN* stance is a documented choice; yours should at least require a Setup toggle.

### G10. Smaller items
- **Sensory axes** (BC cupping/flavour wheel, Visualizer 8 axes): *Defer* — not needed by the dial-in loop; the verdict enum already captures the actionable part.
- **Best-brew flag** (BC `best_brew`, `favourite`): trivial, and a useful input to G1.
- **Peak-window display** (Grindset claim): you already have the measured ageing rate; showing "peak window" would need data you lack.
- **Photos, cost, CO₂e, EAN**: out of scope.
- **QR/NFC share of a coffee** (BC `internal_share_code`): maps naturally to your `library_id`; *Defer*.

---

## 3. Like-for-like features: what to learn from them

| Area | Ours | Theirs | Lesson |
| --- | --- | --- | --- |
| **Stop logic** | 2 fresh samples ≥ target − offset, or linear fit over last 10 | OBBW: same + protection window (default 12 s), sustained-weight backup (1 s), bump rejection | Your slew-limit guard handles spikes, but OBBW's **start-window** idea (no weight stop for the first N s after the window opens, while tare/cup settle) addresses a different failure: stops caused by the cup being placed. `minShotTimeMs = 3000` is shorter than their 12 s; check against the "cup placed after tare" test path on real hardware. |
| **Offset learning** | EWMA, adaptive gain selector (ported) | OBBW original; learning keyed by **stable preset id**, RAM-bounded; evidence window 20 | Equivalent. Verify yours is also keyed by id, not list index (OBBW comments call this out); `Preset` has coffee/recipe ids, probably fine — confirm in `BbwLearningBank`-equivalent code. |
| **Grind advice core** | Mahlkönig-style: moving average of time vs target, tolerance, µm/s measured, one-off exclusion, "inconsistent" state | Visualizer: LLM over last 5 shots × {same coffee, same profile, same grind}, reference = best-rated; direction only if p ≥ 0.6 | Your **explicit "INCONSISTENT" and one-off rules** are better than Visualizer's probabilistic labelling for a controller. Borrow: the *three history slices* (same coffee / same recipe / same setting) as the evidence set, the reference-by-rating idea (G1), and the confidence threshold pattern (say "not sure" below it). |
| **Community aggregation** | Per-model scale, per-grinder offset, kappa for flow, age rate, min contributors | Caliburr: median + IQR per grinder × method, 5-user verification of grinder definitions | Yours is stronger. Borrow Caliburr's **auto-promotion by independent confirmation** for *grinder definitions* (currently every grinder needs a human PR): e.g. ≥5 distinct keys submitting compatible definitions ⇒ auto-merge candidate, reviewer still can veto. Also its **anonymise on delete** (rows kept, user link removed) is a clean privacy rule for your signed records (`key id` removal path is not specified in `COMMUNITY.md`). |
| **Flow analysis** | cup-flow 25/50/75 %, acceleration, roughness | BC: EMA smoothing (0.3 new / 0.7 old) of weight and computed flow; Decent/Gaggia: pressure/flow profiles | Different sensor set. Keep your window-based slope; BC's EMA is simpler but lags — don't swap. GaggiMate records a flag for samples taken *after the pump stopped while weight settles* (`extendedRecording`); you already observe post-stop drip, so mark those samples in stored curves (cheap) to keep analyses clean. |
| **History retention** | summaries 500; curves recent | GaggiMate/BC: whole curves with phase/exit-reason metadata | Add *why the shot ended* per shot (you have `EndReason`) — already in CSV; good. Consider storing downsampled curves (G5). |
| **Recipe model** | coffee × recipe × preset; bloom; hold pre-infusion | GaggiMate: multi-phase profiles with typed exit reasons (volumetric, pressure, flow, duration, manual, ratio…) | Not applicable to your hardware (no pump control); the **typed exit-reason vocabulary** is a good model for your `EndReason`, which already has the machine-stopped/bot-lost cases others lack. |
| **Coffee identity** | `Coffee` has roast date of the bag in use | Visualizer: canonical coffee vs your bag; BC bean = bag | Your split is half-way: no bag entity (`roast_date` on Coffee). Adding `Bag` (roast date, opened, frozen days, weight) fixes G3 and makes imports cleaner (BC beans *are* bags). |
| **Test discipline** | 3,553 host checks incl. failure paths | OBBW: hardware-in-loop plans, schedulability docs; Visualizer: parser fixtures | Add **fixture-based import tests** from upstream sample files (see §5.2) the same way Visualizer tests parsers with real files. |

---

## 4. Where we are ahead (protect these)

1. **Cross-grinder, cross-contributor dial-in** with per-model scale, per-owner offset, flow normalisation and a measured ageing rate — no other project read here does any of it (Caliburr explicitly refuses normalisation; Visualizer's version is an LLM over one user's history).
2. **Non-invasive actuation with confirmation semantics** (ack vs physical effect, retry, bot-lost handling, five machine styles, three pusher makes). Act Four is the only comparable product and is closed and iPhone-only, with no release listed on its store page since 2024-08 (maintenance status otherwise unknown).
3. **Signed, attributable contributions with PR review** — Caliburr and Visualizer rely on accounts.
4. **Explicit statistical honesty**: one-offs excluded, INCONSISTENT state, minimum-contributor thresholds, "unverified" labelling.
5. **Safety posture documented and simulated** (missed stop, lost ack, link down, arm left down).

---

## 5. Making the most of existing offerings and data

### 5.1 What is reusable, and how

| Source | What it gives | Route | Value | Constraint |
| --- | --- | --- | --- | --- |
| **Your own Beanconqueror export** (`BEANS`, `BREWS`, `MILL`, `PREPARATION`, brew→bean/mill by uuid) | Coffee catalogue (roaster, name, origin, region, farmer, variety, process, altitude, roast, roast date) and a **personal dial-in history** (grind_size, time, dose, yield, rating, grinder) | One-shot import on the controller's web page (file picker → map → `POST /api/coffee`/records), or a PC script | Fast onboarding; seeds the owner's own offset fit; seeds coffee entries for library search | Free-text grind (`"4"`), brew_beverage_quantity often 0, non-espresso brews to filter, user-specific rating scale; field limits (23-char names). See check below. |
| **Your own Visualizer account** (`GET /shots`, `/coffee_bags`, `read` scope) | Same, from Decent/Gaggia/Meticulous users' shots: `grinder_model`, `grinder_setting`, `duration`, `bean_weight`, `drink_weight`, `espresso_enjoyment`, `roast_date`, `profile_title`; canonical coffee match ids | OAuth on a phone/PC tool; map as above | For users migrating from another machine, their coffee+grinder knowledge carries over | Different machine/profile ⇒ times not comparable; use for coffee identity, grinder setting *hints* and roast date, not as dial-in records. Rate limits 50/min per IP. |
| **Visualizer canonical lookup** (`/canonical_coffee_bags?q=`, no auth) | Autocomplete + structured attributes (country, region, elevation, farmer, harvest, processing, variety, roast level, tasting notes) | Companion/web page calls it (not the firmware) with caching | Faster, cleaner coffee entry; fills G2 attributes for similarity | Upstream data terms Unknown (Loffee Labs) — lookup only, do not mirror into the public catalog; your name limit is 23 chars (truncation). |
| **Grindstep / Grindset exports** (JSON/CSV claimed) | Dial-in histories from closed apps | Needs a sample file first | Migration path for those users | Format unknown — get a sample (owner action). |
| **`grinder-converter`** (MIT) | 74 grinder model names + sieve distributions | Seed *model names/burr types* into the registry candidate list; optionally use distributions as a prior | Grows `GRINDER_MODELS` beyond 5 | Distribution provenance unknown; it holds **no µm-per-click**, so `um_per_unit` still needs manufacturer data. |
| **Manufacturer manuals** | µm per step where published | Manual curation via PR | The only legitimate source for `um_per_unit` | Per-grinder verification; quote source URL in the grinder PR. |
| **Beanconqueror `ADD_BREW`** (out) | Pushes finished shots into the largest open logbook, and from there to Visualizer | Web-page button (validated against the real decoder) | Gives users graphs, TDS, photos, cupping and Visualizer sync **without us building them** — answers most of G5/G4/G10 | Contract strictness (`waterIn` required, integer rating, date needed); no dedup. |
| **OBBW ecosystem** (HA integration, webhooks) | Home Assistant users | emit/consume only if wanted | Low for BLE-pusher users | Deferred. |
| **Caliburr** | — | — | — | Blocked (§ licence register). |

**Strategic reading:** do not rebuild the logbook. Treat Beanconqueror (and through it Visualizer) as the **long-tail analytics/graph/cupping layer**, and keep the controller's unique value — actuation, stop, dial-in, community fit — focused. Gaps G4/G5/G10 are therefore *"export well"* problems, not feature builds.

### 5.2 Draft import mapping, checked against a real upstream fixture (Reproduced)
`research/validation/import-mapping-check.mjs` maps one real Beanconqueror brew export (Visualizer's test fixture `test/files/beanconqueror_real.json`, a Chemex) to this controller's Coffee + dial-in fields:
- **Maps cleanly:** bean name, roaster, country→`origin`, region, farmer→`producer`, variety→`varietal`, processing→`process`, aromatics→`notes`, roast date (ISO → date), roast enum → controller level (14 → 5 is a *design judgement*; `AMERICAN_ROAST`→`medium-light` here), `grind_size "4"`→integer 4, `mill.name`→grinder, timestamp.
- **Correctly rejected/flagged:** non-espresso preparation (Chemex) skipped; dose 75 g and time 340 s outside controller ranges (5–30 g, 5–90 s); `brew_beverage_quantity` 0 ⇒ yield unknown ("water in is not yield"); `tds` has no field (G4).
- **Not tested:** a full-storage export (`BREWS`/`BEANS`/`MILL` arrays with uuid joins), an espresso brew, BC's configurable rating scale; the roast mapping's grind relevance.

### 5.3 Import design principles (to avoid polluting the fit)
1. **Import as the owner's private history, never as community records.** Signed dial-ins require a settled window of shots (`result.shots`) the importer cannot attest; do not let imports synthesise them.
2. Filter by preparation style (espresso only) and sanity ranges before anything is stored.
3. Preserve the original line (`imported{source, uuid, raw grind_size}`) so a corrected mapping can be re-run; never overwrite typed fields.
4. Treat imported grinder settings as **unit-less hints** until the owner confirms the grinder's unit/`um_per_unit` (BC mills have no unit).
5. Mark imported shots `machine = unknown` so the advisor never mixes them with this machine's time.
6. Idempotency: use BC `config.uuid` (brew) as the dedup key.

---

## 6. Ranked recommendations

| # | Action | Type | Effort (est.) | Why now |
| --- | --- | --- | --- | --- |
| 1 | Use ratings: reference target from best-rated shots + taste tie-break (G1) | learn from Visualizer | 3–5 d | Data already collected; biggest dial-in quality gain |
| 2 | Beanconqueror hand-off button (validated) | integrate | 3–5 d | Delivers charts, TDS, cupping, Visualizer sync for free |
| 3 | Bag entity + frozen days; effective age (G3) | fix + learn | 3–4 d | Prevents wrong age shift; unlocks cleaner imports |
| 4 | Attribute-neighbour prior for undialled coffees, flagged unverified (G2) | learn from Visualizer rubric | 4–6 d | Closes your listed open item; collect data to validate |
| 5 | `apiVersion` + `capabilities` on `/api/status` (G9) | learn from OBBW | 0.5 d | Cheap future-proofing |
| 6 | Maintenance counters (G6) | learn from OBBW | 1–2 d | User-visible, no inference |
| 7 | Control chart + downsampled curve per shot + reference overlay (G5) | learn from Grindstep/BC | 3–6 d | Needs flash-budget check |
| 8 | Beanconqueror export importer (private history, §5.3) | integrate | 4–6 d | Onboarding; add fixture tests from upstream files |
| 9 | Optional TDS → EY (G4) | learn from BC | 1–2 d | Better outcome metric; interop |
| 10 | Auto-promote grinder definitions on independent confirmation (Caliburr pattern) | learn | 2–3 d | Reduces reviewer load; needs contributor volume first |
| 11 | Scale-loss grace (G8), guards (G7) | learn from OBBW | — | **Defer**; guards conflict with advisor unless censored |
| — | LLM-based advice, sensory wheels, photos, QR/NFC | — | — | Not recommended now |

**Unverified premises to test before building:** that rating-derived targets beat the fixed target in practice (needs rated shots per coffee — FEATURES.md estimates ~10); that Visualizer's roast/process rubric holds for your grinder and machine; flash headroom for curves; mobile support for the hand-off link; whether BC users' exports contain enough espresso brews with yield recorded (the fixture shows beverage quantity 0).
