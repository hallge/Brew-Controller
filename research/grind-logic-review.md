# Grind-adjustment logic review: is it sound, reliable and consistent?

Written 16 Oct 2026, at the owner's request. The grind adjustment is the foundation of the product, so it has to be sound and reliable: advice the owner can depend on, and that gets more dependable as it learns the grinder. "Consistent" here means **consistently reliable**, not consistent at the expense of quality.

What was reviewed: every step of the per-shot analysis and the adjustment in `src/core/GrindAdvisor.h` and its caller (`grindAdvice()` in `src/main.cpp`), and the first-shot suggestion (`Community.h`, `SimilarCoffee.h`). Each was checked against the research already gathered (`assumptions-validation.md`, `dial-in-program.md`) and against simulations that run the real code.

**Outcome:**
* The method (Grind-by-Sync style: average brew time against the recipe's target, a tolerance, a proportional move) is sound and stays.
* **Six defects** were found in the numbers it runs on and in edge cases. All six are fixed and are now part of the core advice ("learning the grinder"); for one day they sat behind a setting, which the owner removed on 17 Oct 2026.
* One page bug was found and fixed without a switch: the Ratings setting was never loaded or saved.
* Simulations show the fixes never left a bag undialled, against up to 81% of bags with the current logic. They cut overshooting moves by a third to nine tenths and are never materially worse (the one small cost: at coarse settings overshoots rise from about 1% to 3% of moves, because the larger step is allowed).
* **Learning the grinder is part of the core logic** (owner's decision, 17 Oct 2026): it was a setting, default on, for one day; there is no longer any way to turn it off. A default-constructed `GrindConfig` still gives the original rule, used only by tests and the simulation for comparison.

## 1. The per-shot analysis, step by step

| Step | What it does | Assessment | Change |
| --- | --- | --- | --- |
| Which shots count (`grindShotUse`) | ended on the weight, settled, not retried, 15 s < total, time < target + 15 s, same time basis, the owner's ruling first | **sound** (Mahlkönig's valid brew event plus interference rules); keeps bad shots out of averages and learning | none |
| Grouping (caller) | same recipe, coffee, bag, dose, yield, grinder calibration | **sound**: compares like with like | none |
| Moving average of the current setting's run (window 3) | a change of setting starts a new run | **sound**; matches Grind-by-Sync | none |
| One-off screening (needs 3+ shots, median +-max(2 x tolerance, 4 s)) | leaves out a shot far from the others | **sound**; cannot act before shot 3, which is acceptable | none |
| INCONSISTENT (one clearly fast and one clearly slow at the same setting) | no advice | **sound** | none |
| Keep when the average is inside +-tolerance | Grind-by-Sync | **sound**; keep the tolerance at 2 s (`dial-in-engine.md` 1.1: tighter makes two shots impossible) | none |
| Proportional move: error x sensitivity, capped, at least one step | Grind-by-Sync | **sound in form; its numbers were not** | D1, D2, D3, D4 |
| Learned sensitivity (least squares of time on setting, 3+ shots, span 4) | Grind-by-Sync does this privately | **unreliable as built** | D2, D3 |
| Ratings refinement (inside tolerance only) | owner's rule | **sound**; one edge case | D5 |
| A shot too long or too short to count | advice says "no data" | **gap** | D6 |
| "Settled" for sharing (keep, with a full window of shots) | | **sound**: needs three agreeing shots, so luck is not shared | none |

## 2. Defects found, and what was done

| # | Defect | Effect | Fix (now part of the core logic) |
| --- | --- | --- | --- |
| **D1** | **Units.** Settings are stored in the grinder's own smallest step. The starting sensitivity (3 per second), the largest step (20), the minimum spread for learning (4) and the plausible range of a learned sensitivity (0.5-20 per second) were written for the E64, where a step is one micron. On any other grinder they were the same numbers in the wrong unit: 3 *steps* per second on a dial read in whole numbers is several times too much. The plausible range then **rejected the true value**, so the advisor could never learn its way out | numbered dials: in simulation 44-81% of bags never dialled in; 65-96% of moves overshot | Limits converted with the grinder's microns per step. Where that is unknown, they are relative to the setting, which needs no unit: a percentage of the setting per second |
| **D2** | **Learned sensitivity trusted outright.** Three shots a few steps apart, with normal noise, gave a slope that was accepted as soon as it fell inside the plausible range. A host test has three such shots from which it took **8 per second**, where weighing them against the guess gives 3.2: a figure that size means overshooting | overshoots; erratic advice early in a bag | The measured slope is **weighed against the guess by how much each is worth** (a Bayesian update of one number): little spread, little weight. It is called "measured" once the data carry half the weight |
| **D3** | **Nothing learned about the grinder survives a new bag.** Sensitivity was learned per bag and started from the guess every time. The first correction of every new coffee therefore showed only a direction, never a number | every new coffee starts blind; the step that matters for the two-shot goal had no number | The response is **pooled over all the owner's coffees on the grinder**, as the slope of ln(time) on ln(setting) within each coffee (a percentage per percentage, so coffees at different settings pool correctly). A new coffee starts from it, and its first correction **shows a number** |
| **D4** | **Stepping past what is already known, and endless bouncing.** A proportional step could go past a setting that had already run clearly on the other side of the target. On a grinder whose single step moves the time by more than the tolerance, it bounced between two steps forever | overshoots; on coarse-step grinders, no end | If an earlier setting of this run was clearly on the other side, the step **stops between the two**, at the interpolated setting (the "regula falsi" step of root finding: never leave a known bracket). If no setting lies between them, the **closer one is kept** and the owner is told so |
| **D5** | Ratings refinement could also bounce between two steps around the liked time | small moves back and forth | No refinement smaller than half a step; no refinement when the base kept the closer of two steps |
| **D6** | **A shot far too long or far too short gave no advice.** It is rightly not counted, but the advisor then said "no data" at exactly the moment a big change was needed, and with a poor start the owner could be stuck | E64, 35% start error: 5-8% of bags never dialled in | When that shot is the newest at the current setting and nothing valid exists there, the advice names **the largest single step in the obvious direction**. The next shot gets the usual advice |
| D7 | Largest step fixed at 20 microns | coarse settings took extra shots (230 um: about 0.7 shots more) | The largest step is never less than a tenth of the setting |
| page | The web page's Ratings selector was missing from the list of settings it loads and saves (a stray attribute held its name); it always showed "on" and never saved | owner could not turn ratings off from the page | **Fixed** (no switch); a page check now fails if any grind setting with an input is missing from that list |

What stays exactly as it was: the valid-shot rules, the moving average, outlier screening, INCONSISTENT, the tolerance, ratings inside tolerance, and "settled".

## 3. Evidence

**Host tests:** 3,883 checks, 0 failed (34 new: units, pooling recovers a known elasticity of 1.2 to within 0.02, weighing, bracketing, closer-of-two-steps, out-of-range, and "switch off = unchanged"). Page checks: 59 (8 new). Interop page check 36, library self-tests pass. Emulator built and run on a throwaway copy: with the switch on and an unmarked dial at 15, the starting sensitivity became 0.6 steps per second (was 3); advice, API and the shot context all carry the new fields.

**Simulation** (`research/validation/settle_sim_learn_grinder.cpp`, `.csv`). The real advisor and caller rules (including "not counted"), 300 owners x 8 bags x 28 shots. The grinder is synthetic: a power law, coffee-to-coffee variation, ageing drift, noise. It measures the logic, not real coffee.

| Grinder | Start error | Noise | Mean shots to done (current -> learn) | Bags never dialled in | Overshooting moves |
| --- | --- | --- | --- | --- | --- |
| E64 at 70 um | 15% | 1.5 s | 2.08 -> 2.02 | 0 -> 0 | 19% -> 13% |
| E64 at 70 um | 35% | 1.5 s | 4.60 -> 2.67 | **5.9% -> 0** | 25% -> 14% |
| E64 at 230 um | 35% | 1.5 s | 5.90 -> 3.50 | **6.7% -> 0** | 1% -> 3% |
| dial at 15, no microns | 15% | 1.5 s | 14.7 -> 2.19 | **45% -> 0** | 67% -> 7% |
| dial at 8 (one step > tolerance) | 15% | 1.5 s | 17.7 -> 2.13 | **62% -> 0** | 86% -> 20% |
| dial in tenths (150) | 15% | 1.5 s | 2.10 -> 2.13 | 0 -> 0 | 7% -> 4% |
| E64, grinder responds 1.7x more than the guess | 15% | 1.5 s | 3.40 -> 2.40 | 1% -> 0 | 37% -> 20% |
| E64, grinder responds 0.6x | 15% | 1.5 s | 1.64 -> 1.66 | 0 -> 0 | 7% -> 8% |

The fixes are never materially worse: the worst case is +0.03 shots, within simulation noise; at coarse E64 settings overshoots rise from about 1% to 3% of moves. They are much better wherever the old numbers did not fit the grinder, and they remove every case of being stuck.

## 4. The first-shot suggestion: the theory, checked

**The theory, stated.**
* The right setting for a coffee on a grinder is unknown. The suggestion is an estimate from the most similar evidence available, in a strict order: the owner's own most similar coffee on this grinder; the library mapped onto this grinder; others with the same model; the library's micron figure.
* It is corrected for the known differences: bag age, recipe flow, recipe distance. Each correction applies only where its size is measured, never by an unsourced constant.
* The estimate carries a range. The first shot then measures where the coffee actually is, and the advisor corrects from there.
* Every product reviewed works the same way ("same or similar coffee on the same equipment"). None publishes accuracy (`starting-grind-and-integration-impact.md`).

**Assessment.**
* **Sound in structure.** Choosing the nearest evidence, then correcting it by measured effects only, is the right shape, and it avoids the unsupported constants the research rejected.
* **Its accuracy is unknown.** No real dial-in has been recorded. The ranges are conventions. The context record now stores every suggestion on offer with the shot, so start error can be measured from the first real coffees (`analyze_dialin_shots.py --contexts`).
* **The start sets the ceiling.** In every simulation, start accuracy decides the two-shot rate more than anything in the correction: at a 35% start error even the improved logic needs about 2.7 shots on the E64, and at 15% about 2.0. The correction logic is now as good as the simulations can show. **Further gains have to come from the start.**

**Weaknesses found, not fixed now (each needs real data or an owner decision):**

| # | Weakness | Why not fixed now | Plan |
| --- | --- | --- | --- |
| F1 | When both an own-coffee and a library suggestion exist, only one is shown (library first) instead of being combined | The right combination weights each by how reliable it is (inverse of its error squared). The ranges are not calibrated, and wrong weights can make the blend worse than the better source | Calibrate the ranges from recorded start errors, then combine |
| F2 | The own-coffee suggestion makes no allowance for the bag being older or younger than the nearest coffee was when it settled; the library one does | Needs the age at which the nearest coffee settled, which is not stored | Record it with the settled setting; apply the same age rate as the library, only when known |
| F3 | A grinder figure from other coffees is now learned (D3), but the starting *setting* does not use the owner's recent drift on this grinder (burr wear, season) | A drift term needs real data to show it exists (H10 in `dial-in-engine.md`) | Test with real shots |
| F4 | Practitioner advice is to start a little coarser when unsure: a fast first shot is informative, a choked one is wasted | With D6 a choked shot now gets a full step, which weakens the argument; still a hypothesis | Test on real dial-ins (new hypothesis H13) |
| F5 | The damped first correction (`dial-in-program.md`, C1) is not built | Its benefit depends on knowing how good the start is (calibrated ranges) | After start errors are measured |

## 5. How dependable the advice is now, and how it gets more so

* **First coffee on a new grinder:** a direction always. A number only once the coffee's own shots carry weight, or after a shot far outside the range (one largest step).
* **Second and later coffees on the same grinder:** a number from the first correction, from what the grinder has shown. The page says where it came from: "measured from your shots", "from what this grinder has shown on N coffees", or "starting guess".
* **As shots accrue:** each coffee's own measurement takes over in proportion to how much it is worth. A noisy early fit can no longer swing the advice.
* **Never:** a step past a setting seen on the other side in the last three days; bouncing between two steps; silence after a choked or gushing shot; a full step from a shot whose first drops came far from the usual (section 7).

## 6. Next steps, in order

1. **Done:** learning the grinder is built into the advice (no setting).
2. **Done:** the after-shot input on the shot's page: dose weighed, purged first, flow looked uneven ("made badly on purpose" dropped at the owner's request; detector tests can use "uneven").
3. Calibrate the start ranges, then F1 and F5, from the first real dial-ins.
4. **Done (2 Nov 2026):** the fixes of section 7, from the board's real shots.

## 7. Real shots, 2 Nov 2026

The board's first real history: shots 10 to 30, 19 Oct to 2 Nov 2026. Three coffees (Costa Rica 3 shots, Rwanda 13, Honduras 5), all the Triple recipe (24 g, 29 s +-2 s), beans 155 to 202 days from roast, no ratings, doses or prep notes entered. Shot 28's setting was wiped by the grinder-save bug of v0.1.25; it was pulled at 76 (it matches shot 29 there to 0.0 s). The replay is `testGrindRealHistory` in `test/host/test_main.cpp`: it rebuilds the controller's input for every shot and gives the advice before and after the fixes. Before the fixes it reproduces what the board advised for shots 16 to 23, 29 and 30; earlier shots ran older firmware.

**How the advice did.**

* Direction: every move from outside the tolerance moved the time the right way (9 of 9).
* Size: the time after a change was predicted to within 3.0 s rms. "The same as last time" misses by 4.1 s. Repeats scatter by about 1.4 s (learned, below), so the model is not far off the noise floor.
* Landing: 2 of 7 followed changes landed inside +-2 s; 6 of 21 shots were inside it.
* Noise sets a ceiling. At 1.4 s scatter a perfect setting lands inside +-2 s about 85% of the time; at 2 s, 68%. The 75-80% goal at cup two needs scatter under about 1.6 s, so the owner's preparation matters as much as the advice.
* Drift. The Rwanda ran about 0.8 s a day slower at one setting (2.6% a day; 64 um: 27.6, 30.1, 35.7, 34.1 s over three days). Nothing in the data says why (bean age, humidity, the grinder).
* First drop is steady (Rwanda median 13.0 s, robust spread 0.24 s) and says when a puck was bad: shot 27's came at 8.5 s and the shot ran 6.8 s fast, its pour only 2.2 s fast.

**What was changed** (`src/core/GrindAdvisor.h`; each switched on by `grindConfigForGrinder`, so a plain `GrindConfig` is the original rule):

| # | Change | On the real shots |
| --- | --- | --- |
| R1 | **First-drop screen.** A shot whose first drop is 3 s or more from the usual one on the recipe (median of the newest 10, at least 5), the same way as its error (early and fast, late and slow), and outside the tolerance is a one-off: left out of the average and of the sensitivity learning. The result screen asks whether to count it ("First drops 5 s early. Review preparation routine. Grind kept for now.") | Shot 27: grind kept at 96 instead of 20 um finer. Shots 10 and 11 (early but slow) and 18 (inside the tolerance) are not touched |
| R2 | **Learned noise.** The shot-to-shot scatter assumed in the sensitivity blend (1.5 s) is learned from the owner's repeats, blended with 1.5 s as if that were four repeats; pairs with a bad puck are left out | 1.32 s after shot 30, from 6 repeats (16-17, 19-20, 22-23, 23-24, 24-25, 28-29; the pairs with shot 18 or 27 are bad pucks) |
| R3 | **How much coffees differ.** The prior's width was the standard error of the grinder's average (floor 0.25), as if every coffee responded alike. It now adds the spread of the coffees' own slopes beyond their noise (method of moments), blended with 0.5 as if that were three coffees | 2 coffees with own slopes so far: still near the 0.5 start |
| R4 | **Step sized by the uncertainty.** The step (and its cap) is multiplied by 1/(1+cv^2), cv the sensitivity's remaining relative uncertainty after the blend: about 0.75 for a coffee with no slope of its own, near 1 once its shots pin it down. Errors over two tolerances keep the full step | Shot 29: 63 instead of 59 (the board's 59 ran 32.8 s; interpolating the Honduras's own shots, 63 would have run about 31 s). Shots 14 and 15: 4 to 6 um smaller, a likely extra shot on 14. Shot 10 (42 s): unchanged |
| R5 | **Contradicting shots count for nothing.** When the coffee's own shots say finer runs faster, the blend pulled the slope towards zero and its inverse to the 20 um/s ceiling. Now the grinder's figure stands, with its uncertainty | Honduras at shot 29 (95 um 27.6 s, 76 um 25.6 s): 6.9 um/s instead of 20 |
| R6 | **Brackets from recent shots only.** The other side of a bracket only from shots within 3 days of the newest | Shots 20, 21, 23 were held to +9, +3, +1 um by shot 15 (78 um, 22 Oct) while the coffee ran 32-35 s and slowing; now +16, +15, +16 |

**Tried and dropped.** Treating settings closer than a quarter tolerance (95 and 96 um) as one setting: on the replay it pooled the shot from before a deliberate change into the new setting's average (shot 22 would have moved off a shot inside the tolerance), and the longer run let the one-off screen throw out a real slow shot of a drifting coffee (shot 20). R1 already covers the case that suggested it.

**What the data cannot tell yet.** Whether R4 and R6 raise the two-shot rate: a replay compares each piece of advice, not the shots the owner would then have pulled. Three coffees and old beans; no ratings or prep notes; drift unexplained. Re-run `research/validation/analyze_dialin_shots.py` and the replay on the next ten coffees.

## 8. The adjustment, drift and resets, 3 Nov 2026

Four questions from the owner after section 7, and three more after the first plan. Each answer is switched on by `grindConfigForGrinder`, so a plain `GrindConfig` is still the original rule. The replay (`testGrindRealHistory`) now shows three versions: v0.1.26, round 1 (section 7) and this round.

| # | Question | What the code does now | On the real shots |
| --- | --- | --- | --- |
| A | Can a few coffees that happen to agree collapse the coffee-to-coffee spread, and make the step factor erratic? | The measured spread counts by its degrees of freedom (n - 1 coffees against the prior's 3), and never falls below 0.25 of the pooled figure (`GRIND_COFFEE_SPREAD_MIN`). Thirty agreeing coffees give exactly the floor. The prior's own floor (`GRIND_PRIOR_CV_MIN`) already kept the step factor at 0.94 or more | No change: two coffees with own slopes |
| B | Backlash: is 76 the same from 50 and from 100? | Each shot gets the direction the dial last turned before it (any coffee, `grindApproachDirections`). The burrs sit at `setting - dir * b / 2`. The dead zone b is learned from consecutive pairs of one coffee across which the dial turned back (`grindBacklash`, b ~ N(0, 4 um), 0 to 20 um), and the sensitivity, the bracket and the step all work where the burrs were. A move that turns the dial back adds b to the number, and the page and screen say so | Four reversals, two usable, all 1-2 um moves: b stays at 0, nothing changes |
| C | Drift: correct old shots instead of dropping them | The coffee's drift rate is learned from its own runs at one setting (time over days within each run, pooled, d ~ N(0, 0.5 s/day), at most 2 s/day, older shots forgotten with a 3-day half-life). Older shots are brought forward by `d * age` (at most two tolerances each), the sensitivity is learned again from those, and brackets reach back 7 days instead of 3 | The Rwanda: 0.4 to 0.56 s/day. Its sensitivity, no longer hiding the drift, is learned as about 9 um/s instead of 6.5-7 |
| C2 | Degassing is not linear | The forgetting makes d the coffee's current rate on whatever curve it is on, falling as the curve flattens. On a bag under 21 days from roast (where the curve bends most) shots are brought forward 3 days at most and brackets keep 3 days | Not tested: the beans were 155 to 202 days old, so their drift is not degassing |
| D | A cleaning or a re-zero must not leave the advice anchored to the old grinder | Zeroing (and new burrs, a new grinder) already started everything again: shots carry `Grinder::calibration` and are only compared within it. A cleaning (`grinder_clean` upkeep) now counts the repeats and reversal pairs from before it at half weight and pairs across it not at all. Settings stay comparable across a cleaning, since it does not move the zero | A replayed cleaning on 28 Oct moves the noise figure; the advice keeps its direction |
| E | The first shot after a reversal may still be off (carrier settling, old grounds in the chute) | It counts at half weight in the average and in the sensitivity (`GRIND_REVERSAL_FIRST_WEIGHT`). Once five reversals are known, the weight is learned from how far such shots scatter beyond the noise (`grindReversalWeight`, 0.25 to 1) | Fewer than five reversals: 0.5 |

**Tried and changed.** Drift learned from every consecutive pair, after the sensitivity, came out at -0.1 s/day on the Rwanda: a dial-in turns one way while the coffee drifts, so across changes of setting the two cannot be told apart. It is now learned at one setting only. "Go past and come back" was planned for a reversal. It was dropped: it leaves the threads turned the old way, which the controller cannot see. The dead zone is put in the number instead.

**Next-shot prediction** (11 changes of setting): rms 3.09 s for v0.1.26, 3.01 s for round 1, 3.02 s now. Drift and the steeper sensitivity do not improve the prediction yet on these shots. The check is that they make it no worse.

**Sources.** Degassing is fast for about the first 20 days after roasting and then slows sharply (SCA, "Coffee Decoded: What is fresh coffee?"). Smrke et al. (J. Agric. Food Chem. 2018, ZHAW) fit it with a Weibull curve rather than a straight line, and the published constants vary with roast and storage, so none is fixed here. Retention of grounds from the old setting is the usual explanation for an odd first shot after a change (Five Senses, "Grinder purge quantity"; Clive Coffee, "How to fix a grinder that drifts off its setting"). Burr-carrier settling is reported in forums only. No controlled study of room humidity and shot time was found, and the advice that is published disagrees on its direction: humidity is recorded with each shot (docs/HOME_ASSISTANT.md), not yet modelled.

**What resets what.**

| Owner action | What the advice starts again |
| --- | --- |
| Zeroed today, new burrs, another grinder | Everything: settings, runs, brackets, sensitivity, noise, dead zone, drift, the usual first drop |
| Cleaned the grinder | Noise and dead zone: older repeats and reversals at half weight, none across the cleaning; drift runs end at it |
| A new bag of the same coffee | That coffee's runs, brackets, own sensitivity and drift |
