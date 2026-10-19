# Open questions

Dated 2026-10-12.

| # | Question | Why it matters | Evidence needed | Next action |
| --- | --- | --- | --- | --- |
| Q1 | What are Visualizer's ToS for public shots and canonical data, and Loffee Labs' terms? | decides whether any canonical coffee data can be mirrored | written terms or maintainer reply | read `visualizer.coffee` terms page (not fetched); owner decides whether to contact `miha@visualizer.coffee` (address from OpenAPI `info.contact`) |
| Q2 | Will Caliburr's operator grant a data license/API? | only route to its community data | operator reply | owner decision; nothing sent |
| Q3 | Provenance of `grinder-converter` sieve data | prior for unmarked grinders; redistribution | author statement / source link | **Checked:** repo `tools/` has only `clean.mjs`/`verify-release-tag.mjs`, README credits finer.coffee and says MIT, and no measurement source is cited → still **Unknown**; read finer.coffee, then ask author |
| Q4 | What do Grindstep and Grindset exports look like? | needed to design import adapters | sample exports from owner's accounts | owner exports a sample |
| Q5 | Does `ADD_BREW` work reliably from mobile browsers / PWAs, and what does espresso `waterIn` display as? | the main verified hand-off path | device test | Phase 2 validation 1 |
| Q6 | Does the Visualizer parser accept a hand-built Beanconqueror-shaped JSON without side effects; is the shape stable? | upload path relies on another app's parser | test upload | Phase 2 validation 2 |
| Q7 | Can OBBW's firmware export a per-shot time series through a *public* route? **Partly answered:** `docs/INTEGRATION_API.md` (lines ~323, 471, 544) shows Stats/History rows embed curve arrays (`wCg` weight in centigrams paired with `wAtMs`) over the **Web UI WebSocket**, which needs the exclusive `X-WebUI-Client` claim — i.e. not the public integration contract. | would let OBBW users contribute telemetry | confirm whether a CSV export carries curves; read `ShotStopperShotCurve*.h` | code read |
| Q8 | Real Lunar sample-time jitter and reconnect behaviour; SwitchBot press-to-flow latency on the Breville | stop accuracy and safety claims | traces on hardware | owner hardware session |
| Q9 | GitHub limits/CDN behaviour for controller polling and for contributor writes at scale | Phase 1 viability | measurement | script a load test from the emulator |
| Q10 | Is the signed-key contribution model acceptable to non-technical contributors (GitHub account friction)? | adoption | user trials | pilot with a few owners |
| Q11 | Are there open grinder-setting references I missed (manufacturer PDFs, forum tables, 1Zpresso/Comandante charts) and their licenses? | seeding the grinder registry legally | survey | Phase 3 task |
| Q12 | Gaggiuino/GaggiMate/Meticulous: are their shot schemas stable and can we *read user files* without legal issue? | legacy import | counsel + sample files | ⚖ |
| Q13 | Which sensory vocabulary (SCA/WCR) can be used as enums and under what license? | taste normalisation | license check | read WCR/SCA terms |
| Q14 | Does this project have a contributor agreement? | catalog licensing | repo check | grep CONTRIBUTING (none seen) |
| Q15 | Unmaintained/unchecked: Xenia, Sanremo YOU, Move2 integrations in Beanconqueror; Meticulous API; commercial BBW devices | completeness | code read | Defer unless needed |
| Q16 | Is a roast-level → grind prior justified by real data? (project memory: unverified) | recommendation for undialled coffees | library data analysis | Phase 6 experiment |
| Q17 | Can preparation problems (channeling) be distinguished from grind effects with flow + time + rating data alone? | recommendation correctness | labelled shots | data collection design |

## Added / changed by the roadmap review (2026-10-12)
| # | Item | Status |
| --- | --- | --- |
| Q5 | `ADD_BREW` contract | **Partly resolved:** decoder accepts a controller-shaped envelope; phone/browser behaviour (CompressionStream + custom scheme from an http page on the controller AP) still untested |
| Q6 | Visualizer parser accepts hand-built file | **Reframed:** the file must mimic Beanconqueror's internal export; decision needed (route B vs upstream parser) |
| Q18 | Board bring-up: mbedTLS signature verifies in `verify_record.py`? free heap with Wi-Fi+BLE+TLS? | open — critical path |
| Q19 | What does the "845 presses: 298 ok, 186 not sent, 354 unconfirmed" host-test line measure? | open — read `test_main.cpp` pusher-link stress section |
| Q20 | Do fractional/non-numeric grinder dials need support (current: integer 1–2000 in unit)? | open — product decision |
| Q21 | Make `Community-Coffee` / the firmware repo public? Contributor/data terms and safety text first | open — owner |
| Q22 | Does Beanconqueror render imported flow traces and the controller's `metrics[]` custom trace as expected? | open — phone test |

## Added 13 Oct 2026 (second round)
| # | Item | Status |
| --- | --- | --- |
| Q23 | Primary text of Wang & Lim (2014) for the CO2 activation energy, and a traceable source for "70 days at -25 C = 2 days at 35 C" | open: the freezer fraction 0.10 rests on a bracket (0.03 to 0.33) built from these |
| Q24 | Does refining towards liked shots improve cups? | open: needs owners using the setting on and off (plan E4) |
| Q25 | Size of the roast, altitude and process effects on a grinder setting | open: only the owner's own coffees can show it now; the library could measure it from pairs (plan E2) |
| Q26 | Does Beanconqueror display the controller's flow trace, and does a phone browser let the controller's http page open the link? | open: decoder accepts it; phone untested |
| Q27 | Should a published dial-in carry the liked time (A3)? | owner decision |
| Q28 | Letter-marked dials | not supported; would need a ring-label model |
| Q29 | Bags in the shared records (opened date, weight) | not done |

## Added 14 Oct 2026 (expanded review)
| # | Item | Status |
| --- | --- | --- |
| Q30 | What is "Grind Finder"? | open: owner to give link |
| Q31 | Does the owner use Roastbook? | open |
| Q32 | Show G1-G3 hints on the controller or only the page? | owner decision |
| Q33 | Is an LLM explainer wanted, and which provider? | owner decision |
| Q34 | Grindstep CSV/JSON sample export | open: unblocks import |

## Added 15 Oct 2026 (app landscape)
| # | Item | Status |
| --- | --- | --- |
| Q30 | "Grind Finder" | resolved: Grind Finer (grindfiner.eu) |
| Q35 | Does advice account for the first shot after a grind change (retention)? | open: check before F6 |
| Q36 | Which Beanary / SingleDose was meant; contact Grindhog, Grind Finer, Brewster developers about export/API? | owner |
| Q37 | Read licence files of coffee-by-weight and MeticAI before any reuse | open |
| Q38 | May imported typed times influence the base sensitivity? (recommended no) | owner decision |
| Q39 | F6: cross-coffee prior for sensitivity or hint only? | owner decision (touches a core input) |
| Q40 | Sample exports from Grindstep, Puck Yeah, Dial In Espresso | owner |

## Added 16 Oct 2026 (starting grind)
| # | Item | Status |
| --- | --- | --- |
| Q40 | Sample exports from other apps | withdrawn: owner has none; importers are roadmap only |
| Q41 | Is shot-time elasticity to setting about 0.8-0.95 across coffees and grinders (so sensitivity and step cap should scale with the setting)? | open: needs library pairs (S3) |
| Q42 | Visualizer public-shot data: terms of use, units of the grinder setting | open: read before any seeding |
| Q43 | Manufacturer-published espresso ranges per grinder (last-resort tier S4) | open: curation with sources |
| Q44 | Is any of the owner's 50 stored shots real (not emulator)? | resolved 16 Oct: all simulated; real shots needed (`dial-in-program.md` section 7) |

## Added 16 Oct 2026 (dial-in program)
| # | Item | Status |
| --- | --- | --- |
| Q45 | Real shot-to-shot noise (sd of time at one setting with careful prep) | open: session A |
| Q46 | Real start error by tier and share of coffees done by shot 2 | open: session D (needs S2 logging) |
| Q47 | Do cup-side flow detectors P1-P5 separate sabotaged from normal shots, at <= 5% false alarms? | open: session C |
| Q48 | Minimum sample rate and resolution of a scale for the flow detectors (capability gate) | open: real traces from each supported scale |
| Q49 | Approve S1+S2 (show tier and range; log suggested versus settled setting) and later the switchable C1/C2 | owner |

## Validation experiments (summary)
E1 Beanconqueror deep-link import; E2 Visualizer test upload; E3 Lunar+Bot hardware latency; E4 hold-out fit evaluation; E5 GitHub load/rate test; E6 sample export collection from closed apps.
