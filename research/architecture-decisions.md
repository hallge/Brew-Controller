# Architecture decision records (provisional)

> **Review note 2026-10-12 (see `roadmap-validation.md` §4):** ADR-001, ADR-002 and ADR-003 were written before the `bdb-bbw` source was read. The project already implements them: `ScaleDriver`/`ScaleCap`, `PusherDriver` with PRESS/DOWN/UP + ack + hold-seconds caps, and a 5-style `MachineProfile`. **ADR-002's `pulse(ms)` primitive is superseded** (the vendor doc also defines `0x3` push-and-stay / `0x4` back, which I had missed). Remaining open parts: ADR-003's multi-button sequences, and ADR-004's bag/raw-setting/shared-revision gaps.

Dated 2026-10-12. Status values: **Proposed** (evidence supports, validation pending), **Open** (insufficient evidence — not decided). Nothing here is final.

---
## ADR-001 — Scale driver contract: wrap OBBW's `ScaleProtocol` table
- **Context:** Need multi-vendor scale support without rewriting the recipe engine.
- **Problem:** Which wire-level protocol set to reuse, and what interface the application sees.
- **Options:** (a) own protocol implementations; (b) vendor OBBW `EspressoScaleBLE` protocol table (AGPL, 10 protocol families); (c) port Beanconqueror's TypeScript devices; (d) depend on OBBW transport as-is.
- **Evidence:** `ScaleProtocol.h` is data-driven with feature bitset; OBBW transport is ESP-IDF-native NimBLE for S3+PSRAM only (library.properties, README) while this project uses NimBLE-Arduino → transport not reusable; this project already vendors the protocol files unmodified and cross-checked Acaia against 3 decoders (`docs/RESEARCH.md` §1). Beanconqueror device classes are GPL TypeScript.
- **Decision (Proposed):** (b) keep vendoring the protocol layer; define a thin project-owned `Scale` interface above it: `connect/disconnect events`, `onSample(weight_g, t_local_us, stable?)`, `capabilities()` (per instance), `tare()`, `timer*()` optional, `battery()` optional, `state()`.
- **Consequences:** AGPL stays; transport maintained locally; upstream protocol fixes need periodic re-sync.
- **Reversal:** OBBW drops/forks the table incompatibly, or a permissive alternative covers the same scales.
- **Validation outstanding:** Lunar on real hardware (both variants), sample-jitter measurement, reconnection behaviour.

## ADR-002 — Actuator abstraction: `pulse`-based, with explicit confirmation levels
- **Context:** SwitchBot Bot and future pushers (servos, solenoids, other BLE bots).
- **Evidence:** vendor BLE doc: action reply = command executed ack; hold duration is a device setting (`0x57 0x0f 0x08`), no separate release command (`bot.md`). OBBW's machine JSON models relay edge/pulse semantics (`stop_pulse_ms`, `max_single_press_ms`, `start_edge`).
- **Options:** (a) `press/hold/release` as primitives; (b) `pulse(ms)` primitive + optional `holdSupported`; (c) high-level `start()/stop()` only.
- **Decision (Proposed):** core = `pulse(button_id, duration_ms) -> {delivered: bool, physical_confirmed: tri-state}`; optional extension `hold(button_id)/release(button_id)` only for devices that actually have it. Confirmation is **never** inferred from BLE ack; `physical_confirmed` comes from a separate `FeedbackSource` (scale weight rise, reed/hall, LED) or stays `unknown`.
- **Consequences:** machine profiles describe *buttons and sequences*, e.g. `start: [pulse(brew_btn, 100)]`, `stop: [pulse(brew_btn, 100)]` (toggle) vs `stop: [pulse(stop_btn)]`. Safe behaviour on disconnect is machine-profile data (`on_actuator_loss: alert|abort|none`).
- **Reversal:** a target device offers native hold/release with low jitter and the pulse model costs accuracy.
- **Validation:** press-to-first-flow latency distribution on the Breville; toggle-state recovery after missed stop.

## ADR-003 — Machine profile format: extend OBBW-style JSON, but button-centric
- **Evidence:** OBBW `config/machines/*.json` has `schema_version`, `compatibility_revision`, `interface.control`, `factory_defaults.momentary{…}` — relay-centric, no button layout.
- **Decision (Proposed):** reuse naming and `schema_version/compatibility_revision` pattern; add `buttons[]`, `sequences{start,stop,rinse?}`, `state_feedback`, `constraints{max_duration_s, min_interval_ms}`. Keep a converter to/from OBBW profiles where semantics overlap (momentary/latch only).
- **Open:** whether to propose the extension upstream (**Contribute upstream** vs private schema) — decision for the owner; no contact made.

## ADR-004 — Canonical identity model: coffee ≠ bag ≠ batch; grinder revision-versioned; observations immutable
- **Evidence:** Visualizer is the only system with canonical-vs-user coffee records; Beanconqueror's `Bean` mixes product and bag; Caliburr lacks bags; none models batch or grinder revisions (`data-model-analysis.md`).
- **Decision (Proposed):** adopt the model in `data-model-analysis.md` §2; record `grinder_revision_ref` and **raw setting text** on every shot; corrections create new revisions.
- **Reversal:** if contributors cannot supply revision/batch data, collapse optional levels without breaking readers (fields optional by design).
- **Validation:** round-trip tests against existing dial-in record format; import adapter fixtures.

## ADR-005 — Grinder settings: latent axis fit stays; no asserted equivalence; flow is the observation
- **Evidence:** project memory/`COMMUNITY.md` §8d (scale per model, offset per grinder, KAPPA prior); `grinder-converter` has measured distributions for 74 grinders but no dial↔flow mapping; Caliburr deliberately does not normalise.
- **Decision (Proposed):** keep current approach; treat `grinder-converter` buckets as an *optional prior* for models lacking `um_per_unit`; never publish "equivalent setting" claims without `n` and spread.
- **Reversal:** fit quality (held-out prediction error) worse than a simple per-model median baseline.
- **Validation (outstanding):** hold-out evaluation on real library data; KAPPA prior still a guess (project memory).

## ADR-006 — Backend: GitHub for reviewed catalog, device/user for private history, service deferred
- **Evidence:** `backend-assessment.md`.
- **Decision (Proposed):** Phase 0–1 as in that document; define `index/` as the firmware contract.
- **Reversal triggers:** index size/build time thresholds; token friction; abuse; need for private-shareable data.
- **Open:** GitHub rate-limit and CDN assumptions unmeasured.

## ADR-007 — External integrations are adapters outside firmware
- **Evidence:** Beanconqueror contract needs gzip/base64url/URL-scheme on a phone; Visualizer needs OAuth with weekly-expiring tokens and parser-shaped JSON.
- **Decision (Proposed):** controllers speak only (a) scale BLE, (b) actuator BLE, (c) the library's static files + signed submission; everything else lives in a companion web page / desktop tool / CI job.
- **Reversal:** none expected; revisit if the controller gains a robust TLS + OAuth stack (not planned).

## ADR-008 — Recommendation engine: statistics first, LLM never in the control path
- **Evidence:** Visualizer's grind suggestion = hosted LLM with thresholded probability and admin-only gating (`grind_suggestable.rb`); Grindstep/Grindset algorithms unpublished; OBBW learns stop offset (EWMA), not grind.
- **Decision (Proposed):** keep deterministic, inspectable logic on-device (time-to-target direction + flow normalisation); community priors as static data; any LLM feature (taste-note translation) is optional, off-device, and not authoritative. Report confidence from `n` and spread; do not label correlations as causes.
- **Open:** how to separate preparation problems from grind effects with the available data (no channeling sensor). Candidate signals (flow shape, first-drop delay, rating text) are *Plausible* only.

## ADR-009 — Licensing: AGPL firmware, permissive schemas, CC0 factual catalog, no NC
- **Evidence:** `license-and-risk-register.md`.
- **Decision (Proposed):** as tabulated there. **Open:** SDK-headers permissive split; contributor agreement; catalog license choice pending counsel.

## ADR-010 — Competing vs integrating with closed apps
- **Decision (Proposed):** Act Four (SwitchBot BBW) = compete; Grindstep/Grindset = compete on dial-in transparency, support import only from user-owned samples; Caliburr = compete via open governance; Beanconqueror/Visualizer = complement.
