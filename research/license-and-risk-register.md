# License and risk register

Dated 2026-10-12. **Not legal advice.** Items flagged ⚖ need qualified counsel. Licenses were read from LICENSE files, README text, `package.json`/`pyproject.toml`, or the GitHub API as noted; a license read from GitHub's API without a LICENSE file is marked "(API)".

## 1. License matrix

| Component | License (evidence) | Reuse mode we'd use | Obligation that matters | Compatible with AGPL-3.0-or-later firmware? |
| --- | --- | --- | --- | --- |
| OBBW firmware + EspressoScaleBLE | AGPL-3.0-or-later (LICENSE; HA integration `pyproject`) | already vendored unmodified (`lib/EspressoScaleProtocol`), algorithm headers cited | AGPL §13 network clause if modified and offered over a network; source offer for distributed devices | Yes (same license) |
| AcaiaArduinoBLE | MIT (LICENSE via API) | via OBBW; notices preserved | keep copyright notice | Yes |
| Beanconqueror | GPL-3.0 (LICENSE) | **interop via URL-scheme only**; reading its docs | none triggered by sending data to it; copying its TS code would make our derivative GPL-3 (AGPL §13 / GPLv3 §13 permits combination) | n/a (no code reuse) |
| Visualizer.coffee code | MIT (LICENSE) | call API; optional upstream contribution | none for API use; MIT notice if code copied | Yes |
| Visualizer service/data | ToS **not read**; canonical data upstream **Unknown** | lookup only | ⚖ | — |
| Caliburr repo | **no license ⇒ all rights reserved** | none | copying code or seed lists risks infringement | **No (not usable)** |
| Caliburr community data | ToS: owner = contributor; license to Caliburr to store/display/distribute "to other users of the service"; bots prohibited | none | ⚖ ; **Blocked** | — |
| Gaggiuino | **CC BY-NC 4.0** (LICENSE) | none (code/schemas) | NonCommercial: incompatible with OSI-style licenses and with downstream commercial use | **No** |
| GaggiMate | **CC BY-NC-SA 4.0** (README; GitHub API: none) | none | NC + ShareAlike; no LICENSE file ⇒ also ambiguous | **No** |
| decaid | GPL-3.0 (LICENSE.txt; API says NOASSERTION) | interop only | — | via §13 if ever linked |
| pyDE1 / hass-gaggiuino / gaggiuino-local-profiler | GPL-3.0 (API) | none | — | — |
| gaggimate HA integration | none declared | none | all rights reserved | No |
| acaia-lunar-ble | MIT (API) | reference | notice | Yes |
| switchbotpy | MIT (API) | reference | notice | Yes |
| SwitchBot BLE docs | no LICENSE file in repo (API: none) | implement protocol independently from a public spec | ⚖ whether protocol facts are free to implement (generally facts/interfaces are not copyrightable; wording/figures are) | Yes if we write our own text |
| grinder-converter | MIT (LICENSE) code **and** data modules; measurements' **provenance not stated** | optional offline prior | MIT notice; ⚖ if upstream sieve data has its own terms ("finer.coffee") | Code yes; data redistribution **Unknown** |
| jldbc/coffee-quality-database | MIT repo, **CQI source terms unconfirmed** | none now | ⚖ | Unknown |
| Home Assistant core | Apache-2.0 (not re-read) | integration interface | — | Apache-2.0 is GPLv3/AGPLv3 compatible (one-way) |
| Apple/Google store apps (Act Four, Grindstep, Grindset…) | proprietary | none | ToS; no reverse engineering | — |

## 2. Licensing strategy recommendation (provisional)

The existing repository is **AGPL-3.0-or-later** because unmodified OBBW files are vendored — that fixes firmware and anything linked with it. Different components can still choose different licenses:

| Component | Recommendation | Why | Constraint |
| --- | --- | --- | --- |
| Firmware + vendored scale protocol | **AGPL-3.0-or-later** (keep) | forced by vendored files; consistent with upstream | Anyone shipping devices must offer source |
| Own driver *interfaces* (headers defining `Scale`, `ActuatorDriver`, `MachineProfile`) | AGPL (same binary) — or a **permissive (MIT/Apache-2.0) "SDK headers" repo** if third-party/commercial drivers are desired | Permissive interface headers lower adoption friction but cannot include AGPL OBBW code | Needs clean-room separation; ⚖ |
| Phone/PC companion + adapters | AGPL or Apache-2.0 | no copyleft code vendored; Apache-2.0 eases Beanconqueror/Visualizer interop contributions | do not copy GPL-3 Beanconqueror code into an Apache-2.0 repo |
| JSON Schemas / spec text | **CC0 or MIT** (+ CC BY 4.0 for prose) | maximize adoption by other apps | — |
| Public catalog (coffees, grinders, machines) | **CC0** for factual fields; contributor terms grant a license to the maintainers *and* re-license to the public | facts are largely uncopyrightable; ODbL/CC BY-SA add friction for apps | ⚖ database rights in EU/UK; contributor agreement (DCO or lightweight CLA) |
| Community shot data (user contributions) | **CC BY 4.0 or CC0**, opt-in, with explicit consent screen; telemetry opt-in separately | enables research/analytics re-use | ⚖ GDPR: user-linked data; pseudonymous key id is still personal data in some readings |
| Hardware plans | **CERN-OHL-S-2.0 or CC BY-SA 4.0** | share-alike for hardware | **Avoid NC licenses** (Gaggiuino/GaggiMate precedent hurts adoption and is not open source) |
| Documentation | CC BY 4.0 | — | — |
| Trademarks | Use "compatible with Acaia/SwitchBot/Breville" nominative statements; no logos; "Open Brew by Weight" is a third-party project name — avoid confusing similarity ⚖ | — | — |

Contributor agreement: required before accepting third-party catalog data and code (OBBW has a `CONTRIBUTING.md`; this project does not have a documented contributor license yet — **Unknown**, check repo).

## 3. Dataset reuse restrictions (what may *not* be done)

| Source | Restriction found | Action |
| --- | --- | --- |
| Caliburr | ToS: bots prohibited; license only to service users; no open licence | do not scrape, do not use the anon Supabase key, do not copy seed lists |
| Visualizer canonical | upstream (Loffee Labs) terms unknown; Visualizer API rate limits (50/min/IP) | lookups only, per-user cache; ask maintainer |
| Visualizer public shots | per-uploader data; public listing exists; ToS not read | do not mirror into catalog |
| CQI / Kaggle scrapes | unknown | defer |
| grinder-converter data | MIT file license; real-world measurement source unstated | confirm with author before shipping inside a CC0 catalog |
| Store-app data (Grindstep/Grindset/Act Four) | no API; ToS not reviewed | only user-owned exports |

## 4. Risk register

| ID | Risk | L | I | Mitigation | Residual / owner action |
| --- | --- | --- | --- | --- | --- |
| R-lic-1 | Accidentally incorporating Caliburr code/data (no license) | M | H | policy: never import Caliburr artefacts; this research read it for *interface facts* only | ⚖ confirm reading public repo for schema design is acceptable; avoid copying structure verbatim (enum names `stepped/micro_stepped/stepless` are descriptive but note provenance) |
| R-lic-2 | NC licenses (Gaggiuino, GaggiMate) contaminate project | L | H | no code/schema reuse | keep import code written from public file examples by the user, not from their schema files |
| R-lic-3 | AGPL deters device makers/commercial integrators | M | M | keep drivers' *interface* layer separable; document how to ship compliant devices | ⚖ decision on SDK-headers repo |
| R-lic-4 | SwitchBot doc reuse, trademark | L | M | write own spec text; nominative use | ⚖ |
| R-dat-1 | Grinder-distribution data provenance | M | M | store with `source_ref`; keep out of public catalog until clarified | ask author |
| R-dat-2 | Catalog gets contaminated by user-submitted text with third-party copyright | M | M | contributor terms; moderation | ⚖ |
| R-sec-1 | OBBW-style unauthenticated LAN API copied for write paths | M | H | require auth for any actuating route; keep remote start disabled by default | design rule |
| R-sec-2 | GitHub tokens stored on controllers; `C:\tools\github keys.txt` exists next to this project (**not opened**) | M | H | fine-grained, single-repo tokens; rotate if file holds live ones; relay in Phase 2 | **owner action: check that file** |
| R-sec-3 | Signed records authenticate origin, not honesty; poisoning of shared fits | M | M | PR review for identity records; robust statistics (median/IQR as Caliburr plans); minimum-n thresholds | needs validation experiment |
| R-sec-4 | Privacy: raw timestamps + weights + device id link to a person | M | M | random per-device id (not MAC) already in design; opt-in; coarse dates | ⚖ GDPR |
| R-saf-1 | Button pusher cannot stop a latched/toggle machine on BLE loss | M | H | pulse-only semantics, safe-state docs, alert; never claim physical confirmation from BLE ack | hardware validation |
| R-saf-2 | Hardware plans published without a safety statement | M | H | adopt OBBW-style charter language ("not a certified appliance"), manual test plan | before publish |
| R-dep-1 | Beanconqueror import contract changes (`v:1`) | M | L | version-pin tests, adapter isolated | monitor releases |
| R-dep-2 | Visualizer single-maintainer hosted service | M | L | optional sink only | — |
| R-dep-3 | OBBW refactors protocol table / drops Arduino support (5.0.0 already did) | H | M | vendor a pinned snapshot, contribute fixes upstream | track tags |
| R-dep-4 | Act Four/Grindstep/Grindset are closed; formats can change without notice | H | L | adapters only from user-supplied samples | — |
| R-lock-1 | Lock-in to GitHub (account friction, rate limits, ToS) | M | M | index contract decoupled; Phase 3 service emits same files | — |
| R-qual-1 | Dial-in advice from sparse, confounded data | H | M | publish n and spread; require repeated settled dial-ins; correlation ≠ causation messaging | validation experiments in roadmap |

## 5. Unresolved legal questions (for counsel)
1. Is reading a public no-license repo to learn its *schema design* acceptable if no code/data/strings are copied?
2. Can a CC0 catalog include grinder range/step facts derived from manufacturer manuals and from third-party measurement sets?
3. Does AGPL §13 apply to a community library/relay if it executes AGPL-derived code server-side? (relay will not run OBBW code; confirm.)
4. Contributor license for opt-in shot data (CC0 vs CC BY) and GDPR roles (controller/processor) for a GitHub-hosted store.
5. Hardware license choice and liability disclaimers for devices that actuate a mains-powered appliance.
6. Use of vendor names (Breville, Acaia, SwitchBot) and "Open Brew by Weight" in project naming.
