# Dial-in engine: strategy review, the formula, and what to capture

Written 16 Oct 2026. Owner's brief: no hardware yet, so build the infrastructure now. Capture the right data so a **learning engine** can be built: an inference engine for the grinder, not a language model. It must be **formulaic**. It must learn each grinder and how its owner reads the steps, and recommend step changes. It should take in shelf time, freezer time, ratio, taste and the other factors, layering in complexity only where that narrows the window. It must build on the existing foundation, not replace it. The owner then asked for a review of the goals, the strategy and the efficiency of the changes. Section 1 is that review.

Companion documents: `dial-in-program.md` (goals, simulations, real-data plan), `starting-grind-and-integration-impact.md` (the start suggestion), `roadmap-integrations.md` (reserved).

## 1. Review of goals, strategy and the changes made

### 1.1 The goals, checked for consistency

| Goal | Measurable as | Verdict |
| --- | --- | --- |
| Keep the core method (time against target, tolerance, learned sensitivity) | the advisor gives bit-identical answers with every new option off (host tests) | **sound**: holds in everything built so far |
| Good suggestion at cup zero | start error (suggested vs settled setting, in % of setting); share of coffees whose settled setting falls inside the shown range | **sound**, now measurable: the start on offer is recorded with every shot (section 5) |
| Cup two done 75-80% of the time | share of new coffees whose second shot is inside the tolerance and the advice says keep | **sound, but only reachable if shot-to-shot noise is about 1.5 s or less and the start is within about 10-15%** (simulation). Real noise is unknown |
| "Narrow the window" | see below | **conflicts with the two-shot goal if it means a tighter tolerance** |
| Formulaic, no LLM | every recommendation is a closed-form expression of stored numbers, printable as working | **sound**; the design in section 3 keeps it |

**The conflict, in numbers.** Assume a shot aimed perfectly at the target, with normal shot-to-shot noise sd. The chance it lands within plus or minus w seconds is:

| noise sd | w = 1 s | w = 1.5 s | w = 2 s |
| --- | --- | --- | --- |
| 1.0 s | 68% | 87% | 95% |
| 1.5 s | 50% | 68% | 82% |
| 2.0 s | 38% | 55% | 68% |

With a tolerance of plus or minus 1 s, even a perfect engine is "done" on half the shots at 1.5 s noise, so two shots become impossible. **Recommendation:** keep the acceptance tolerance at 2 s ("done" means the grind is right; the rest is noise). Make the narrowing happen in two other places:

* **the precision of the recommendation**: the engine's prediction interval for the right setting, shown to the owner, shrinking shot by shot;
* **the aim**: the ratings refinement already moves the aim inside the tolerance towards the time the owner likes.

That is "a much more narrow window" without making the target unreachable.

### 1.2 The strategy, validated

| Element | Assessment |
| --- | --- |
| Measure before building (real-data plan, analysis script) | **right**. Every simulation result depends on two numbers we do not have: real noise and real start error |
| Simulation with the real advisor and a synthetic grinder | **useful for ranking levers and finding ceilings; not evidence about coffee**. Each conclusion in the program is gated on real data |
| Learning on one owner's shots | **right for the per-coffee layer, slow for the rest.** One owner opens perhaps 2-4 bags a month. The per-coffee setting and response are learned within shots. Effects of roast, process, altitude and age across coffees need dozens of coffees, which is months to years for one owner. **Pillar B therefore depends on the shared library**, which pools owners. That has a consequence for capture: the new context fields should later be shareable with consent |
| Proving the 75-80% claim | **not possible from one owner quickly.** To pin a rate near 78% to within plus or minus 10 points (95% confidence) takes about 66 dial-ins; within 15 points, about 30. The plan has to rely on measured components (noise, start error, response) fed into the simulation ("plug-in" estimate), plus library-wide rates later |
| Flow-problem detection | **right to keep it display-first.** Simulation: even a perfect detector adds 2-4 points to the two-shot rate; false alarms above about 5% make things worse. Its value is coaching better prep, which raises the rate indirectly |
| The "done" signal | **weak point:** one shot inside the tolerance makes the advisor say keep, and a lucky shot counts. Track a second metric, **confirmed** (the next shot at the same setting is also inside), so luck is not mistaken for accuracy |
| Integrations reserved | **right**; none moves the dial-in metrics |

### 1.3 The changes made in this round, and their efficiency

| Change | Cost | Assessment |
| --- | --- | --- |
| `core/ShotContext.h`: a 64-byte record per shot (bean age at the shot, time since last shot, upkeep count, start on offer, advice given, curve quality, advisor settings, reserved owner fields) | none at brew time; built once after the shot is stored | portable, host-tested (20 checks), never read by the advice |
| Storage | **first version wrote one file per shot (`/x<id>.bin`); changed to one ring file** `/contexts.bin`: 12-byte header + 500 x 64 bytes = **32,012 bytes**, created once, one seek per write | The first version would have put about 1,000 small files in the root directory next to the 500 curves. That slows every lookup in littlefs and adds metadata wear. The ring costs 32 KB of the 3.9 MB partition, writes 64 bytes per shot, and is pruned implicitly: a slot is reused when its shot leaves the history, and is only believed if its shot id matches |
| Work after each shot | one history scan for the advice (the same scan the screen triggers anyway, then cached), one library read for the bag dates, the memoised community start, the own-coffee start | done once per shot in the loop task after the shot is saved, while the machine is idle; no effect on stop timing |
| API `GET /api/contexts?limit=` and capability `shot_context_v1` | streamed, one file open per record | the analysis script joins it to `/api/shots` by shot id |
| Not captured yet, though the fields exist: machine on-time, dose actually weighed, prep flags | zero until an input exists | listed in section 5 |

Verified: host suite **3,849 checks, 0 failed** (3,829 before). Emulator built; on a throwaway data copy two simulated shots were each given a context record (first with the per-file version, then again with the ring file: `contexts.bin` 32,012 bytes, record read back through the API with the right shot id, time since the previous shot, sample interval 110 ms, weight step 0.1 g). The owner's own emulator data was not touched. The board was not used.

## 2. Principles for the engine

1. **The existing advisor is layer 0 and the fallback.** The engine must reproduce it exactly when the other layers are off. It may replace an output only when, on held-out shots, it predicts better.
2. **One equation, in logarithms**, so that every effect is a percentage. Grinder steps differ in size between grinders; percentages carry over.
3. **Model flow, not time.** For a fixed yield, time is yield divided by flow, plus the delay to first drops. A change of yield then needs no separate rule.
4. **Every layer must earn its place.** It joins only if it lowers the error of the *next* shot's prediction on held-out data. Otherwise its weight stays at zero.
5. **Closed form, small, on the device.** Fewer than about 10 coefficients per grinder, solved by ridge or Bayesian linear regression (a 10 x 10 system). The heavy analysis is done on a PC with the same formula; the device only evaluates it.
6. **Every recommendation prints its working**: which layers moved it, by how much, and the interval.

## 3. The formula

For a valid shot i of bag b on grinder g:

```
ln Q_i = a_b                               base: this bag on this grinder (the "where is it" term)
       + beta_g * ln(x_eff,i / x_ref)      grind: x is the burr gap or setting, in the finest unit
       + gamma_g * ln(D_i / D_ref)         dose (recipe, or weighed if entered)
       + delta * ln(1 + A_i / 7)           bean age: A = effective days off roast (frozen days x 0.10)
       + rho_g * roast level + pi * process + ...   coffee attributes (shared through the library)
       + u_day                             today's offset (shared by all shots today; carries to the next coffee)
       + e_i                               noise, sd sigma_g (grinder and owner)

Q_i = yield_i / pour time_i                (grams per second; total time = first-drop delay + yield / Q)
x_eff,i = (1 - r_g) * x_i + r_g * x_(i-1)  retention: part of the previous setting's grounds still in the burrs
```

What each layer is in plain terms, and what already exists:

| Layer | Term | Exists today as |
| --- | --- | --- |
| 0 | `a_b`, `beta_g` with the recipe's target | `adviseGrind`: moving average and learned um per second. A fixed um/s is the same as a fixed `beta` at a fixed setting |
| 1 | prior on `a_b` from the start suggestion, with its range as the prior's spread | the community and own-coffee starts; the **damped first correction** in `dial-in-program.md` is exactly this Bayesian update |
| 2 | `delta` (age, freezer) | `FROZEN_AGING_FRACTION`, the library's age rate |
| 3 | `rho`, `pi`, altitude | the ridge in `SimilarCoffee.h` (own coffees) and the library pairs |
| 4 | `gamma`, yield through Q | the flow shift in `Community.h` (`kappa` x ln flow ratio) |
| 5 | `u_day` | nothing yet |
| 6 | `r_g` retention | nothing yet |
| 7 | taste: the preferred flow `Q*_b` | the ratings refinement (liked time) |

**Recommendation.** Solve the equation for the setting that gives the aimed flow:

```
ln x_new = ln x_ref + (ln Q*_b - a_b - gamma ln(D/D_ref) - delta ln(1+A/7) - ... - u_day) / beta_g
```

* Its uncertainty comes from the coefficients' covariance plus sigma, giving an interval in microns. That interval is mapped to the owner's grinder units (`SettingFormat.h`: divisor, sub-steps, rings) and rounded to steps the dial can actually take.
* If the interval contains the current setting, the answer is **keep**. That is the engine's version of the tolerance and it shrinks as data accrue: the narrow window.
* **Stepped grinders:** if one step moves the time by more than the tolerance, no setting can be inside it. The engine then says which of the two neighbouring steps is closer to the aim. A dose change of a tenth or two of a gram is an optional fine trim, but only once `gamma` is measured.

**Learning.**
* After each shot the coefficients are updated (recursive least squares with a slow forgetting factor, so burr wear and seasons are followed).
* `beta_g`, `gamma_g`, `r_g` and `sigma_g` are pooled across all the owner's coffees on the grinder, which is how the engine "learns the grinder". `a_b` is per bag, shrunk towards the coffee and then towards the start suggestion.
* Cross-coffee terms (`delta`, `rho`, `pi`) start at zero or at the library's value, and move only as the owner's own coffees and the library show them.

**How the owner sees steps.**
* The engine works in the finest unit. It talks in the owner's: "two clicks finer", "from 3.5 to 3.3", "inner ring B, outer 4".
* It learns the time change per displayed step (`beta_g` x step size / setting) and shows it ("on this grinder one step is about 1.8 s at this setting").
* It records the direction of the last move. If hysteresis turns out to matter (H11), the formula adds a term for it.

## 4. Hypotheses: what each layer assumes, and how it is tested

Evidence: P = plausible (physics or practice, untested here), D = documented by others, U = unknown. "Keep if" is the admission rule from principle 4.

| # | Hypothesis | Evidence now | Test (real data) | Keep if |
| --- | --- | --- | --- | --- |
| H1 | Locally, ln(flow) is linear in ln(setting) | P (porous bed; library already uses ln flow) | session B: 5 settings, residuals versus fitted line | residuals show no curvature beyond noise |
| H2 | `beta` is similar across coffees on one grinder (so it can be pooled) | U | `beta` per coffee as coffees accrue | pooled prior beats per-coffee estimate on next-shot error |
| H3 | Older beans flow faster at a fixed setting; log-shaped in days | P (degassing; practitioner) | same bag at intervals; library pairs | `delta` significant and lowers next-shot error |
| H4 | A frozen day counts about 0.10 of a shelf day | partial (Gantner 2024; bracket 0.03-0.33) | bags frozen and thawed; library | as H3 |
| H5 | Darker roast flows faster (needs coarser) | weak (one informal test; practitioner) | across coffees, own and library | sign holds and error falls |
| H6 | Process and origin do not change grind beyond what roast and age explain | supported for particle size (Uman 2016) | as H5 | the term earns nothing: drop it |
| H7 | A bigger dose flows slower; `gamma` is stable per grinder and basket | P | dose changed by 0.5 g at a fixed setting | measurable, then usable as the fine trim |
| H8 | Yield changes time through Q (constant flow) | P (approximately) | yield changed at a fixed setting | Q stays within noise |
| H9 | Part of the shot-to-shot noise is explained by routine (time since last shot, first shot of the day, machine warm-up) | P (practitioner) | context fields; residual against them | sigma falls by a useful amount |
| H10 | A day offset exists and carries from one coffee to the next on the same day | U | residual correlation within a day | `u_day` lowers the error of the first shot of the next coffee |
| H11 | Retention: the first shot after a change reflects part of the old setting | P (single-dose vs hopper practice) | after each change, first shot versus second at the same setting | `r_g` > 0 with the right sign; the correction then accounts for it |
| H12 | The owner's preferred flow per coffee lies inside the tolerance and is stable | partly built (liked time) | ratings | already admitted when 3+ liked shots exist |

## 5. What to capture: coverage

| Needed by | Field | Captured? |
| --- | --- | --- |
| Q, time, layers 0 and 4 | yield, total and pour time, first drop, flows, whole curve | **yes** (shot record and curve, since before this round) |
| x | setting in the finest unit, grinder calibration, unit format | **yes** |
| x_prev, retention | setting of the previous shot on the grinder | **derivable** from history order |
| D | recipe dose | **yes**; weighed dose: **yes** (entered on the shot's page) |
| A, freezer | effective, calendar and frozen days **at the shot** | **yes, new** (context) |
| roast, process, altitude | coffee entry | **yes** (via coffee id) |
| u_day, routine | time since the previous shot | **yes, new**; machine on-time: field reserved, **not filled** (needs the power bot's state) |
| upkeep | shots since grinder clean | **yes, new** |
| prior (layer 1) | community start and own-coffee start on offer, with ranges and basis | **yes, new** |
| advice scoring | the advice after the shot: action, setting, sensitivity used, learned or guessed, step-limited, refined | **yes, new** |
| replay | advisor settings in force; engine version | **yes, new** |
| detector gate | median sample interval, weight resolution, number of points | **yes, new** |
| taste | rating, sour/balanced/bitter, TDS | **yes**; strength and "uneven": **not yet** |
| prep | purge, owner saw uneven flow | **yes** (shot's page); WDT, screen, paper reserved, no input |
| basket, machine | basket identity | **no** |
| ambient | temperature, humidity | **no** (no sensor; low priority until H9/H10 say otherwise) |

The gaps that matter most for the program:
* a **quick input after a shot** for dose weighed, purge, and "uneven" (built 16 Oct; session C labels its deliberately badly prepared shots with "uneven");
* **basket**;
* machine on-time.

All three fit the reserved fields or need one small UI change, without a history migration.

## 6. Plan (revised after this review)

| Step | What | Gate | State |
| --- | --- | --- | --- |
| E0 | Capture layer (`ShotContext`, ring file, `/api/contexts`), analysis script joins it | none | **done** |
| E1 | Quick post-shot input for weighed dose, purge, uneven (fills reserved fields) | owner OK | **done 16 Oct** on the shot's page (`/api/shot/prep`); "made badly on purpose" dropped at the owner's request |
| E2 | Engine workbench on a PC: the formula in section 3, fitted to `/api/shots` + `/api/contexts`, leave-one-dial-in-out replay against layer 0, admission test per layer; tested first on synthetic data with known coefficients, to prove it recovers them | none | **partly done 16 Oct**: the grinder layer (pooled elasticity, weighed sensitivity) is built into the firmware behind "Learn the grinder", recovers a known elasticity in host tests, and is simulated with the real code (`grind-logic-review.md`); fitting real `/api` data waits for shots |
| E3 | Real sessions A-D (`dial-in-program.md` section 7) | hardware | waiting |
| E4 | Measured noise and start error into the simulation: a plug-in estimate of the two-shot rate, with its uncertainty | E3 | |
| E5 | Port the admitted layers to `core/` as a portable header, behind a switch, layer 0 bit-identical when off; recommendation interval shown with its working | E2, E3, owner OK | |
| E6 | Context fields offered to the shared library (opt-in), so cross-coffee layers can pool owners | data terms decided | |

## 7. Limits

* Nothing in sections 3 and 4 has been fitted to real coffee; the equation is a hypothesis with a test plan.
* Simulated shots exercise the capture and the API; they say nothing about the coefficients.
* Sample sizes needed to *prove* the two-shot rate are large (section 1.2); the plan relies on components plus the library.
