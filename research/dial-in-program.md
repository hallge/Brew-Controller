# Dial-in program: first shot, continuous dial-in, and shot preparation

Written 16 Oct 2026, after the owner's steer: **all shot data so far is simulated**; collect real shots before spending cycles on integrations; focus on **dial-in (cup zero and the correction after cup one) and on detecting flow problems that are not about grind**. Integration research is kept and reserved (`roadmap-integrations.md`).

Nothing here changes the core method. `adviseGrind` stays as it is: the scale's real time against the recipe's target, a tolerance, a learned sensitivity, one-off screening, ratings refining inside the tolerance. Every change below happens **around** it (in what is fed to it, what is shown, or behind a switch that defaults to off), and each can be switched off and tested for "bit-identical when off".

## 1. The goal, stated so it can be measured

| Term | Meaning here |
| --- | --- |
| **Cup zero** | the setting suggested before any shot of a coffee is pulled |
| **Cup one** | the first shot, at the suggested setting; its time gives the correction |
| **Cup two** | the shot at the corrected setting |
| **Done** | a shot inside the recipe tolerance (+-2 s by default) and the advice says keep |
| **Wasted shots** | shots pulled before the first one that is "done" |

Owner's targets: a good suggestion at cup zero; a correction after cup one that makes cup two "done" **75-80% of the time** (so two shots, one wasted at most); a third shot the rest of the time; three shots is "pushing it"; one shot is the ideal. Today's expectation (simulation, section 3) is about three.

## 2. Honest status of the evidence

* Every stored shot is simulated (owner, 16 Oct). They exercise the code; they say nothing about real coffee, grinders or channeling.
* The simulations below use the project's **real advisor** against a **synthetic grinder** with stated assumptions. They answer "what would the controller need to reach 75-80%?", not "does it reach it?".
* Real numbers needed: shot-to-shot noise, start error, how time responds to the setting, how often prep goes wrong, what channeling looks like in the curve. Section 7 says how to get them in about 40 shots.

## 3. What the simulation says it takes

Files: `research/validation/settle_sim_*.cpp` and `.csv` (compile line at the top of each). Rows are 4,000-6,000 trials. Synthetic grinder: time = target x (setting/ideal)^(-0.9 x variation) + noise; start error and noise as stated; target 28 s; tolerance 2 s.

**Noise sets a ceiling no grind logic can pass.** Probability that cup two is done (best rule, start already at 5% error): shot-to-shot noise 1.0 s: **96%**; 1.5 s: **92%**; 2.0 s: **84%**. A single noisy cup one is partly copied into the correction (the controller cannot tell noise from grind), which is why the ceiling is below 100%.

**Start error needed for 75-80% at cup two** (setting about 70 um, noise 1.5 s, damped first correction): start error (1 standard deviation) of **10-15% of the setting** gives 83% / 77%. At noise 2.0 s it needs about 5-10%. At noise 1.0 s, up to about 20%. For reference, the community start's default ranges (+-15 um at a 70 um setting) are about 21% (read as one standard deviation), which gives about 66% with today's rule and 70% damped at 1.5 s noise.

| Start error | 5% | 10% | 15% | 25% | 40% |
| --- | --- | --- | --- | --- | --- |
| P(cup two done), current rule, noise 1.5 s, 70 um | 0.82 | 0.78 | 0.73 | 0.62 | 0.48 |
| P(cup two done), damped first correction | **0.92** | **0.83** | **0.77** | 0.64 | 0.49 |
| mean shots, current / damped | 1.62 / 1.47 | 1.89 / 1.78 | 2.10 / 2.03 | 2.46 / 2.40 | 3.02 / 2.97 |

**What each lever is worth** (ranked by size in these runs):

1. **Start accuracy** is by far the biggest: 25% -> 10% error adds about 15-19 points of P(two shots).
2. **Shot-to-shot noise** is next: going from 2.0 s to 1.0 s is worth 10-20 points. Part of the noise is preparation, which is why preparation is a pillar (section 6).
3. **Damped first correction**: when the start is good, the first correction should not fully chase one noisy shot. Scaling it by k = sp^2 / (sp^2 + noise^2) (sp: how wrong the start probably is, in seconds) adds **+3 to +11 points** when the start is within 5-10%, and nothing when it is poor. It stayed no worse than the current rule even when the controller believed its start was twice as good as it was.
4. **Step cap that grows with the setting** (10% of it, at least 20 um): matters at coarse settings (230 um, 25% start error: mean shots 3.24 -> 2.78; 400 um: 4.3 -> 3.05); no effect at fine settings.
5. **Sensitivity proportional to the setting** instead of a fixed 3 um/s: worth 0.1-0.4 shots at coarse settings, almost nothing at fine ones. Low priority.
6. **Flow-problem detection** for the shot count: see section 6; modest and conditional.

## 4. Cup zero: an error budget instead of a hunch

The start error is the sum of independent parts. Each part has a different cure, and each needs a measurement.

| Part of the error | Cause | Cure we have | What to measure |
| --- | --- | --- | --- |
| Coffee to coffee, same grinder | roast, density, age, process | own nearest coffee + ridge on own coffees (needs 2+ more coffees than factors); library pairs | within-owner spread of settled settings across coffees on one grinder, same recipe class |
| Grinder to grinder | zero point and burr wear differ even in one model | fitted scale and offset from coffees in common; same-model median | offset spread across owners of one model; how many coffees pin it down |
| Recipe, basket, machine | different resistance | flow shift (kappa x ln flow ratio), recipe distance | settled setting versus flow ratio; basket/machine as match keys |
| Bag age and freezing | degassing and staling | library age rate, frozen fraction | settled setting versus days since roast on the same coffee |
| Day to day | humidity, retention, ambient | continuous advice | drift of the settled setting for one coffee |

Rules of thumb that follow:

* Until real shots exist the **tier that produced the number must be shown** (S1 in `starting-grind-and-integration-impact.md`) and **every suggestion must be logged with where the coffee settled** (S2). Without S2 the 75-80% goal cannot be tracked.
* The range shown should be calibrated so that about two thirds of settled settings fall inside it. Today's ranges are conventions.
* A new owner with no history and an unmarked dial gets the widest range; the goal there is "drinkable, informative first shot", not 75%.

## 5. Cup one to cup two: improvements around the unchanged advisor

| # | Change | Where | Switch | Gate to enable |
| --- | --- | --- | --- | --- |
| C1 | **Damped first correction.** The caller multiplies the sensitivity guess by k for the first correction of a run when the sensitivity has not been learned. `adviseGrind` itself is not edited. k comes from the tier's range and the owner's measured noise (or 1.5 s until measured) | the code that builds `GrindConfig` for the advisor | off by default | real shots show noise and start error in the range where it helps (section 3) |
| C2 | **Step cap that grows with the setting** | same place | off | real shots show a coarse-regime owner |
| C3 | **Time basis check.** Offer pour time (excludes pre-infusion) as the basis if it scatters less than total time in the owner's repeats. Already a recipe option; only the advice to use it is new | page | n/a | real repeats under both bases (section 7, session A) |
| C4 | **Flagged shot leaves the average** (opt-in). A shot flagged as uneven can be set aside with the existing "ignore this shot" ruling (`GrindVote::IGNORE`), pre-filled by the flag | result screen | off | detector precision proven (section 6) |
| C5 | **Prior sensitivity** from the owner's other coffees on the same grinder, only when the current coffee has fewer than 3 shots | config builder | off | real data (S3 in the earlier plan) |

All five default to "advisor behaves exactly as today". Host tests assert equality with the switches off.

## 6. Shot preparation: detecting flow problems that are not about grind

### 6.1 What the scale can and cannot see

| Signal | Available? | Notes |
| --- | --- | --- |
| Weight over time (0.1 g, about 10 Hz on the Acaia Lunar) | yes | stored for every shot |
| Flow (derivative of weight) | yes, cup-side and smoothed | arrives after the puck, delayed and smeared by the cup |
| Time to first drop | yes (`firstDropS`) | stored |
| Flow shape numbers (flow at 25/50/75% of the yield, acceleration, roughness) | yes (`FlowFeatures`) | stored, **currently unused by any advice** |
| Pressure, temperature | **no** | the Dual Boiler's gauge is not read |
| Pump pulsation | no | far above the sample rate |

So the detectors are **cup-side flow detectors**. Anything that depends on pressure (the methods most apps use) cannot be copied.

### 6.2 How others do it

| Source | Method | Needs | Validated? |
| --- | --- | --- | --- |
| MeticAI (`shotFacts.ts`) | channeling = pressure drop >= 1.5 bar together with flow rise >= 1.5 ml/s; stall = weight gain < 0.5 g | pressure sensor | author's thresholds, no source |
| Scott Rao, extraction-curve analysis | volatile pressure in pre-infusion = channels forming; steep pressure fall late = channels as the puck erodes; flow collapse after pre-infusion = grind too fine | pressure | practitioner article |
| gaggimate-mcp decision trees (adapted from gaggimate-barista) | sour + erratic flow -> channeling, fix puck prep, **do not grind finer**; first drip time varying > 3 s between shots -> prep inconsistency | flow, first-drip time | practitioner; text is CC BY-NC-SA, not copied |
| Practitioner flow guides (Story of Coffee, Manta) | healthy: smooth climb in 5-10 s to a plateau about 1.3-2 g/s; channeling: sudden mid-shot flow jump; choke: below 0.5 g/s after 15 s; flow accelerating steeply = too coarse | scale flow | rules of thumb; no thresholds tested |
| Acaia Lunar flow indicator | LED showing peak flow on a fixed 0.1-3.4 g/s scale | scale | a display, not a detector |
| Espresso Analysis | neural net labels shots under / balanced / over from the weight and flow curve | 15+ labelled shots | no results published; not a channeling detector |

**No source gives a tested detector, thresholds, or error rates.** Every number above is a rule of thumb. Whatever we build must therefore **calibrate itself on the owner's own shots and be validated on labelled shots**, not copy a threshold.

### 6.3 Detector design (all cup-side, self-calibrating, display-first)

Computed per shot from the stored curve and features, compared with **the owner's own previous shots of the same coffee, recipe and bag** (robust median and MAD; silent until at least 5 comparable shots).

| Id | Flag | Signal | Practitioner basis |
| --- | --- | --- | --- |
| P1 | Mid-shot flow jump | after the ramp (first 5 s past first drop), a rise in the 1 s flow estimate that is large against the shot's own smooth trend (z-score of the residual) | "sudden jump in flow mid-shot" |
| P2 | Rough flow | `flowRoughnessGS` high against own history | stuttering / unevenness |
| P3 | Odd first drop | `firstDropS` far from own median (in the guide: > 3 s between shots) | prep inconsistency |
| P4 | Steep acceleration | `flowAcceleration` high against own history | too coarse / soft puck / eroding channel |
| P5 | Choke | flow after 15 s far below the recipe's own average flow | choking |

Output: one of *normal / possible uneven flow / possible choke*, with the reason. **It never changes the grind number by itself.** It adds a caution line ("this shot's flow looks uneven; check distribution and tamp before changing the grind"), lowers the advice confidence, and offers a one-tap "ignore this shot" (the existing ruling).

**Capability gate.** The detector is switched on only when the connected scale delivers enough: median sample interval, smallest weight step, and noise of the idle weight before the shot are measured per shot, and the flag shows "not available with this scale" below the gate. Slow or coarse scales (and any with fewer samples per second than the ramp phase needs) are therefore left out automatically. Gate thresholds are set from real traces (section 7), not guessed now.

### 6.4 What the simulation says detection is worth for the shot count

Setup: 10% or 20% of shots are "bad prep" (run 3-8 s fast whatever the grind; **assumed**), noise 1.5 s, start error 10-25%. `settle_sim_channeling_value.cpp`.

| Bad-prep rate | no detector | perfect detector | recall 0.8, false alarm 5% | recall 0.8, false alarm 15% |
| --- | --- | --- | --- | --- |
| 0% | 0.83 | 0.83 | 0.80 | 0.74 |
| 10% | 0.74 | 0.76 | 0.72 | 0.66 |
| 20% | 0.66 | 0.69 | 0.65 | 0.59 |

(P(cup two done), start error 10%, 70 um.)

Findings, plainly:

* **Even a perfect detector adds only 2-4 points** for the two-shot goal, because setting a shot aside still costs a shot. Mean shots fall by 0.1-0.2 at 10-20% bad prep.
* **A detector with 5% false alarms is not better than none for the shot count, and one with 15% is worse.** False alarms must be rare, which is why the flag is a caution and not an automatic exclusion, and why it stays off until measured.
* **Preparation consistency itself is worth as much as start accuracy:** removing 20% bad prep (0.66 -> 0.83) is about the same gain as taking the start error from 25% to 10% (0.65 -> 0.83). The value of this pillar is **coaching the owner to fix prep**, which raises the two-shot rate and the cup, not suppressing advice.

### 6.5 How to validate detectors honestly

Create labelled positives on purpose (mark them with the shot page's "flow looked uneven"; no separate flag is kept for this, by the owner's choice): at one setting and recipe pull 5 normal shots and 5 sabotaged ones (for example: no distribution so clumps remain; a tilted, uneven tamp; a hole poked in the puck), and record which is which. Measure for each detector the separation (does any threshold put the normal shots on one side?). Adopt a detector only if it separates them with at most 5% false alarms on the normal shots, and publish the thresholds with the numbers behind them.

## 7. Real-data plan (about 40 shots, then every new coffee)

Use one grinder, one machine and basket, one scale, and record the prep consistently. Roughly 18 g per shot, so about 700 g in total.

| Session | Shots | What it gives |
| --- | --- | --- |
| **A. Noise baseline** | 10 shots, same coffee, same setting, same careful prep (flush and purge between) | shot-to-shot noise sd (total and pour time); scatter of first drop and flow features; the ceiling in section 3 |
| **B. Response** | 5 settings spaced about 5% apart around the right one, 2 shots each | how steeply time falls with the setting (um per second and as a percentage), whether it is linear enough; sensitivity scaling (C2, C5) |
| **C. Induced faults** | 5 normal and 5 sabotaged (section 6.5) at one setting | whether P1-P5 separate faults from normal; thresholds and false-alarm rate |
| **D. New coffees** | every new coffee from the first bag on, with S2 logging on | start error by tier, shots to settle, share settled by shot 2; the real answer to the 75-80% goal |

Procedure notes: change one thing at a time; note the grinder setting in the grinder's own units and the zero (re-zeroing after burr change); do not rate during A-C unless asked; keep the data on the controller (it never leaves unless sharing is on).

**Analysis:** `research/validation/analyze_dialin_shots.py` reads the controller's `GET /api/shots` output and prints noise, response strength, flow-feature scatter, wasted shots per coffee, and the share of dial-ins settled by shot 1, 2 and 3. It has been run on the simulated history (label printed on the output) as a smoke test; its numbers on simulated data mean nothing.

**Decision rules** the data will support:

| If real data shows | Then |
| --- | --- |
| noise sd above about 2 s | 75% at two shots is out of reach for grind logic; put effort into preparation and the time basis first |
| median start error above about 20% | invest in the start (library, own-coffee fit, corrections), not in the correction rule |
| start error below about 12% and noise under about 1.5 s | enable C1 (damping); expect +3-11 points |
| owner settings above about 200 um, or a long first move | enable C2 |
| detectors separate sabotaged shots with at most 5% false alarms | ship the flags as cautions; consider C4 |
| response strength differs a lot between coffees | learn it per coffee earlier (C5); otherwise leave the 3 um/s default |

## 8. Program, in order

| Step | What | Firmware? | Gate |
| --- | --- | --- | --- |
| P0 | This document; reserve the integration research | no | done |
| P1 | **S1 + S2**: show tier, range in um and %, expected shots; log suggested versus settled setting with the dial-in (history/library field) | yes | owner OK |
| P2 | Collect sessions A-D with the analysis script | no | real shots |
| P3 | Re-run the simulations with measured noise, response and start error; set the targets and switches from section 7 | no | P2 |
| P4 | C1, C2 behind switches (default off), host tests for bit-identical-when-off | yes | P3 and owner OK (core inputs) |
| P5 | Detectors P1-P5 + capability gate (display only, default off) + the tap to label "was this uneven?" | yes | P2 session C |
| P6 | Calibrate the unsourced constants in the community start (ranges, 3% per recipe step, age cap) from the S2 data | yes (constants) | enough S2 data |
| P7 | Only then: reserved integrations in the order in `roadmap-integrations.md` | later | targets met |

## 9. What this does not establish

* Whether real shot-to-shot noise lets 75-80% be reached at all. If real noise is about 2 s, the grind logic cannot get there (84% is the ceiling at a 5% start error, and the start will not be that good).
* Any detector threshold; no tested one exists anywhere I could find.
* The assumed rate and size of bad-prep shots (10-20%, 3-8 s) are placeholders.
* The synthetic grinder is a smooth power law; real grinders have retention, zero drift and discrete steps.
* Nothing has been run on a real board with real coffee.
