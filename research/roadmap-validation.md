# Roadmap review against the current `bdb-bbw` implementation, and integration validation

Dated 2026-10-12. Implementation read at HEAD `5520bea` **plus the uncommitted working tree** (many files are modified; see `git status`). Evidence labels as elsewhere. "Reproduced" = I ran it in this review.

## 1. Headline

The original roadmap (`implementation-roadmap.md`, written before I read this code) **assumed phases that are mostly already built**. The implementation is ahead of it on contracts, storage, signing, intake and review; what is genuinely missing is the **board-side first run**, **publication/legal groundwork**, **external-app hand-off**, and a few **data-model gaps**. Several research recommendations were also **wrong or already superseded** (§4). Use the rebased plan in §3; the old file now carries a pointer to this one.

## 2. What I reproduced

| Check | Command / method | Result |
| --- | --- | --- |
| Host tests | `CXX=".venv/Scripts/python.exe -m ziglang c++" bash test/host/run.sh` (no `g++` on this machine; project's documented ziglang route) | **3553 checks, 0 failed** (reproduced; matches the project's own figure). Sections include scale drivers, pusher drivers, "stop press missed by the machine", "stop acknowledgement lost, press did happen", "bot link down at stop time", "hold for the shot, release, and an arm left down", share/signing, community suggestion. Output also reports a stress line "845 presses: 298 ok, 186 not sent, 354 unconfirmed" (meaning not investigated). |
| Library tooling | `python tools/{verify_record,intake,build_index,reviews}.py --self-test` in `library-template/` | all four **self-test passed** |
| Beanconqueror hand-off contract | ran Beanconqueror's real `brew-handoff.decoder.ts` (commit `bbf8de0`) under Node 24 with a stubbed zip fallback; harness in `research/validation/` | see §5 |
| SwitchBot action codes | re-read vendor `bot.md` | `0x3` push-stop (down and stay), `0x4` back; corrects my earlier statement (§4) |
| Visualizer's Beanconqueror parser input | read `app/models/parsers/beanconqueror.rb`, `base.rb`, test fixtures | needs Beanconqueror's **internal export shape**, not the hand-off envelope (§5) |

Not reproduced: anything on the real board (the project itself says the TLS/signing path "compiles; has not run"), BLE against a real Lunar/Bot, the emulator end to end, Ruby-side Visualizer parsing (no Ruby installed).

## 3. Rebased roadmap: original phase → actual state → remaining work

| Original phase | Actual state in `bdb-bbw` (evidence) | Remaining (estimate) |
| --- | --- | --- |
| **0 Decisions before publishing** | AGPL-3.0-or-later `LICENSE` + README §12 provenance notice **done**. **Not done:** no `CONTRIBUTING`/contributor terms, no data license or contributor consent text for shared records (`library-template/README.md` has none), no safety / non-appliance statement in README. Both `hallge/Brew-Controller` and `Community-Coffee` are **private** (GitHub API). `github keys.txt` still unchecked. | 1–3 d, all owner decisions + text. **Highest-leverage remaining item before going public.** Note private repo → no branch protection (project's own caveat, `COMMUNITY.md` §9). |
| **1 Schemas and adapter contracts** | **Largely done, in C++ not JSON Schema.** `ScaleDriver` + 9 capability bits (`src/core/ScaleDriver.h`), `PusherDriver` + 6 caps with PRESS/DOWN/UP, ack (`PusherDriver.h`), three pusher makes (SwitchBot, MicroBot, Tuya Fingerbot), `MachineProfile` with 5 styles (toggle, start-only, manual, start/stop buttons, hold) (`MachineProfile.h`), records with `t` + `v:1` and hand-written range checks in `verify_record.py`. **Gaps:** no machine-readable JSON Schema (validation lives only in `verify_record.py` + firmware), no `schema`/`rev` versioning beyond `v:1`, no conformance suite for third-party drivers beyond host tests, no `index_schema`. | 3–6 d (down from 5–10): generate JSON Schemas from the checks, add `schema` strings and an `index` version, document driver contracts for third parties. |
| **2 Interop with existing apps** | **Not started.** `grep` for ADD_BREW / beanconqueror / visualizer in `src/`, `docs/`, `emu/ui/` → no matches. Data needed is already available: `/api/shot/data` (time_s, weight_g, flow_g_s, scale_timer_s arrays), `/shot.csv`, `/shots.csv`. | 3–5 d for a Beanconqueror hand-off button; Visualizer needs more (§5). |
| **3 Minimum public catalog** | **Mechanism done and exercised on GitHub (12 Oct)**: private `Community-Coffee`, intake workflow, PR per coffee/grinder, status files, index build (project doc §9). **Content empty** ("the library is empty" after the live check). Bundled grinder list has **5 models** (`GrinderRegistry.h`). | curation work (grinders from manuals; owner decides count); public/private decision. |
| **4 Validation and review workflow** | **Mostly done**: envelope + ECDSA P-256 signature + type/range checks, similarity hints in PR text, merge=publish / close=decline with note, `status/<key>.json`. **Gaps vs. my proposal:** corrections create a new `record` number but there is **no shared grinder/coffee revision model**; no duplicate-alias handling beyond difflib similarity; no abuse/ban tooling beyond removing a key. | 3–5 d |
| **5 Local and synced storage** | **Done in the emulator, written for the board**: outbox of 300 files on LittleFS, 1/s send, `LibraryLink` GitHub contents API with pinned roots, backoff, NVS-stored fine-grained token. **Not run on hardware:** DeviceKey (mbedTLS) "compiles; has not run", signer not yet checked against the verifier, **free-memory budget with Wi-Fi+BLE+TLS unmeasured**. Live GitHub requests were sent *from a PC* with the exact request shapes. | **Board bring-up is the critical path**: first signed record end-to-end; memory measurement. Estimate 2–5 d with hardware. |
| **6 Analytics / recommendation** | **Built and ahead of the plan**: per-model scale + per-grinder offset fit, flow normalisation (KAPPA prior), near-recipe matching, bean-age allowance, `index/models.json`, start suggestion (`Community.h`). Validated only in self-tests and emulator with invented contributors (project's own statement). | Needs real data; held-out evaluation (E4). |
| **Hardware track** | The code anticipates the failure modes my risk register listed (stop missed, ack lost, link down at stop, arm left down) **and tests them in simulation** (§2). Real-hardware latency/jitter still unmeasured. | owner session |

### Revised priority order
1. Board bring-up of signing + GitHub push (unblocks everything shared).
2. Phase 0 publication package (licenses, contributor/data terms, safety text, repo visibility).
3. Hardware latency/jitter measurements (Lunar, Bot).
4. JSON Schema + versioning extraction (small, enables third-party tooling).
5. Beanconqueror hand-off (validated, §5); Visualizer only after a decision on §5.2.
6. Catalog content and data-model gaps (§6).
7. Held-out evaluation of the fit once real contributors exist.

## 4. Research claims I made that this review corrects

| Earlier claim (file) | Correction | Evidence |
| --- | --- | --- |
| ADR-002 proposed a `pulse(ms)` actuator primitive and said hold/release were not native (`architecture-decisions.md`, `integration-catalog.md` I7) | **Wrong in two ways.** (a) The vendor doc *does* define `0x3` push-and-stay and `0x4` back. (b) The project already implements `PRESS/DOWN/UP` with `PUSHER_CAP_DOWN_UP`, `PUSHER_CAP_HOLD_SECONDS`, `PUSHER_CAP_ACK`, and ack states ACCEPTED/UNSUPPORTED/REFUSED. ADR-002 is **superseded by the existing design**. | `PusherDriver.h`; `SwitchBotProtocol.h` `Action::DOWN=0x03, UP=0x04`; vendor `bot.md` |
| "Confirmation never inferred from BLE ack; needs a FeedbackSource" | Already the design: `SHOT_FLAG_STOP_CONFIRMED`, `STOP_RETRIED`, `stopLatencyMs` in `ShotRecord`; ack is distinguished from physical effect; tests for lost ack / missed stop. | `Types.h`; host test section names |
| ADR-001 / I8 "wrap OBBW table in a thin `Scale` interface" | Already exists (`ScaleDriver`, `ScaleLink`, `TableDriver`, `ScaleCap`). | `ScaleDriver.h` |
| ADR-003 "extend machine profile to button-centric" | Already button-centric in the sense that matters (5 styles incl. start/stop two-button and hold). **Remaining gap:** no multi-button *sequences* (e.g. "press A then B") or per-button labels beyond the single `control` string. | `MachineProfile.h` |
| `data-model-analysis.md` "record raw setting text" | Implementation stores the setting as an **integer in the grinder's unit** (`grindUm`, `grind` 1–2000; "a dial read in tenths is not supported") with `unit`/`um_per_unit` on the grinder. Raw text is not kept. Real gap for dials with fractional/non-numeric positions. | `Types.h`, `COMMUNITY.md` §8 step 4, `verify_record.py` |
| Proposed `grinder_revision` as new | A **per-owner calibration epoch already exists** (`Grinder::calibration`, copied into each shot as `grinderId`; advice compares only within it). Missing: a **shared model revision** (burr set/model refresh) and a way to say two owners' epochs are comparable. | `Types.h` |
| Integration catalog I2: "emit a Beanconqueror-shaped JSON" for Visualizer | Underspecified/misleading: Visualizer's parser reads Beanconqueror's **internal export** (`brew.config.unix_timestamp`, `brew.grind_size`, `mill.name`, `bean.*`, `preparation.name`, `brewFlow.*[].timestamp` as `HH:MM:SS.mmm`), **not** the `ADD_BREW` envelope. | `parsers/beanconqueror.rb`, `test/files/beanconqueror.json` |
| Roadmap Phase 1: "define Scale/Actuator/MachineProfile interfaces" | Done; only schema export/versioning remains. | above |

## 5. Integration steps validated

### 5.1 Beanconqueror `ADD_BREW` — **Verified against the real decoder (Reproduced)**
Setup: simulated 33 s curve (331 points, quantised to 0.01 s / 0.01 g like `CurvePoint`; weights had noise added so compression is realistic), mapped from `ShotRecord` fields as in `src/net/ShotFormat.h`/`/api/shot/data`.

| Case | Decoder verdict | What it means for the controller |
| --- | --- | --- |
| Controller-shaped shot with flow | **Accepted.** 858 base64url chars → 3 chunks (≤400 each), URL ≈ 926 chars; 331 samples decoded | Feasible within the contract; a ~40 s Lunar shot is small. |
| Fractional decigram deltas | Accepted; accumulated weight error vs source **0.0000 g** | The wire's "decigram" unit does not force 0.1 g quantisation; 0.01 g survives. |
| No `waterIn` | **Rejected** ("brew.waterIn must be an object") | Must always send; for espresso send yield (ml≈g) — a convention to document. |
| `rating` 4.5 | **Rejected** (integer only) | Controller rating is already an integer 1–5; fine. 0 = unrated: omit. |
| `beverageOut` unit `ml` | **Rejected** | Must be grams. |
| `grindSize` as number | **Rejected** (string only) | Format `grindUm` + unit as text, e.g. `"246"` / `"246 um"`. |
| Summary only (no `flow`) | Accepted | Fallback when no curve is stored. |
| >10,000 samples | **Rejected** | Not an issue at 10 Hz (~330/shot); would matter only with `fidelity` for long or high-rate data. |
| Date without zone | **Rejected** | `ShotRecord::epochS == 0` ("wall-clock time was unknown") **cannot be sent**; need a fallback (phone clock) in the companion page. |
| `v:2`, negative `brewTime` | Rejected | Contract is strict; pin and re-test on upstream releases. |
| Extra `metrics[]` (flow g/s trace) | Accepted | Can carry controller-derived flow as a custom trace. |

Still **untested**: the deep link on a real phone from the controller's web page; whether mobile browsers allow `CompressionStream` and custom-scheme navigation from an **insecure (http) page served by the controller** (Plausible, not verified); how Beanconqueror renders the imported trace; behaviour of the "create bean/grinder?" prompts; duplicates (no dedup per upstream doc).
Mapping from this project's data (design): `dose`←`doseG`, `beverageOut`←`finalWeightG`, `brewTime`←`shotTimeS` (note: start command → stop command, *not* including drip), `firstDripTime`←`firstDropS`, `bean.name/roaster/origin/region/varietal/process`←`Coffee` fields, `roastingDate`←`roastDate` (YYYYMMDD → ISO), `grinderName`←`Grinder::model`, `preparationMethod`←"Espresso" or machine maker/model.
**Disposition unchanged: Integrate. Effort:** 3–5 d incl. phone testing. **Blocker to check first:** the web page is served from the board (47 KB, no external assets) — verify JS gzip availability on target phones.

### 5.2 Visualizer upload — **Changed from "Integrate" to "Defer pending a decision"**
- Upload works only by presenting a file that Visualizer's **format detector** recognises (`mill`/`brewFlow` ⇒ Beanconqueror parser). That parser requires Beanconqueror's internal export fields listed in §4 and derives duration from `brewFlow` timestamps; a hand-built file would mimic another app's private format, with no stability guarantee.
- Options: (A) export a Beanconqueror-format file and upload via the user's own OAuth (fragile); (B) send to Beanconqueror (5.1) and let *its* Visualizer upload do the work (that app appears to support it: `visualizer_username/password` settings are blanked in `uiStorage.export()`, and Visualizer documents Beanconqueror uploads; **inferred, not verified**) — **no code needed here beyond 5.1**; (C) propose an upstream parser (owner decision).
- **Recommendation: B first.** Not tested end to end.

### 5.3 SwitchBot Bot — **Documented, implemented, simulated; hardware pending**
Vendor doc confirms `0x57 0x01 <action>` with action `0x0` press, `0x3` push-stop, `0x4` back, status `0x01` OK; the project's `SwitchBotProtocol.h` matches (Action PRESS/ON/OFF/DOWN/UP, Status codes 0x01–0x09) and cites the same doc plus pySwitchbot. Status 0x01 is documented as "Action executed" — the project treats ack ≠ physical effect (tests: lost ack, missed press). **Unverified:** press-to-first-flow latency on the Breville; hold behaviour timing; password-protected variant path (`57 11 <crc32>`, implemented; not on hardware).

### 5.4 Scale protocols — **Reproduced in simulation**
Host tests include "scale drivers: registry, capabilities, wire formats" and "a whole shot over each scale's wire format" using captured frames; Lunar 17-byte frame decoding checked against `pyacaia`/`aioacaia` per `docs/RESEARCH.md`. **Hardware not verified** (README flags the USB-C Lunar as "Hit or Miss" upstream).

### 5.5 GitHub library path — **Partly verified live by the project; board side unrun**
Intake/review/status/index mechanics: project reports a live throwaway-key test on 12 Oct (5 accepted, 3 refused for correct reasons, merge→published, close→declined). I re-ran the Python self-tests only. Request shapes (`PUT contents/inbox/...` 201/422, `GET ...?ref=main`) were exercised from a PC. **Open:** mbedTLS signature vs verifier on a real board; TLS memory; rate limits under many contributors; the private repo cannot protect `main`.

### 5.6 OBBW webhook interop — **Not applicable now**
This project's pusher is BLE, OBBW's is a relay; there is no code path that would emit OBBW webhooks. Interop is a "nice to have"; **Defer** (was "Integrate (interop)").

## 6. Data-model gaps that survive contact with the code

| Gap | Where it shows | Why it matters | Cost |
| --- | --- | --- | --- |
| No bag/batch entity: `Coffee::roastDate` is "the bag in use"; shots copy `roastDate`; "a new bag is not a new description" | `Types.h`, `COMMUNITY.md` step 6 | freshness-aware comparison works per shot (age is derivable), but bag identity/quantity/open date are lost | low–medium |
| Setting stored as integer in grinder unit; no raw text, no fractional | `Types.h` `grindUm`, `verify_record.py` `grind` 1–2000 | dials with letters/half-steps/dual rings cannot be represented | medium (format change; "fields only added at the end" rule helps) |
| Shared grinder **revision** absent (only owner-local calibration epoch) | `Grinder::calibration` | a corrected shared grinder definition cannot be versioned without re-fitting | medium |
| Published shot/dial-in records carry summary + flow shape (25/50/75 %), **no time series** | `Share.h` `buildShotRecord` | fine for the current fit; blocks telemetry research and OBBW/Beanconqueror-style analytics on shared data | low (opt-in curve sidecar) |
| Rating stored 1–5 with taste enum, no scale descriptor | `Types.h`, `/api/shot/rate` (0–5) | OK while the scale is fixed; add descriptor before supporting imports | low |
| `ShotRecord::epochS == 0` when clock unknown | `Types.h` | breaks any external import needing a date | low (use phone clock) |
| No `schema` string/`index_schema` | records have `t`,`v:1` only | forward compatibility of the index contract | low |

## 7. Corrections to other research files (applied)
`architecture-decisions.md` (ADR-001/002/003 status notes), `integration-catalog.md` (I2, I6, I7), `implementation-roadmap.md` (pointer), `open-questions.md` (resolved/new items), `README.md` (index). Original reasoning is kept where still valid; superseded items are marked, not deleted.
