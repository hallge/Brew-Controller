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
* **Never:** a step past a setting already seen on the other side; bouncing between two steps; silence after a choked or gushing shot.

## 6. Next steps, in order

1. **Done:** learning the grinder is built into the advice (no setting).
2. **Done:** the after-shot input on the shot's page: dose weighed, purged first, flow looked uneven ("made badly on purpose" dropped at the owner's request; detector tests can use "uneven").
3. Calibrate the start ranges, then F1 and F5, from the first real dial-ins.
