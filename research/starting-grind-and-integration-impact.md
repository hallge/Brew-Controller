# The starting-grind suggestion, and what each outside integration is worth

Written 16 Oct 2026. This answers the owner's steer of 15-16 Oct: the product has three pillars; the first is settled and must not change; the second (the suggestion **before the first shot**) is where outside learning helps most; outside integrations go on a roadmap **after an impact analysis**, not casually.

**A word on "cold start".** That was my shorthand (crema, a project in the research, uses it for the same thing). In this document it means one thing: **the first-shot starting grind**, the setting suggested when a coffee is picked and no shot of it has been pulled yet. Nothing else.

## 1. The three pillars (the frame for everything below)

| Pillar | What it is | State | Rule |
| --- | --- | --- | --- |
| **A. Live advice** | After each shot, the scale's real time against the recipe's target time gives the next move (`adviseGrind`); ratings only refine it inside its tolerance | built, tested | **Do not change.** Complement only |
| **B. First-shot starting grind** | Before the first shot: where to set the grinder (`communityStart` from the library, `similarStart` from the owner's own coffees) | built, **constants unvalidated**, library empty today | **This is where to invest** |
| **C. Taste** | Ratings, sour/balanced/bitter, the hints that follow | built | small additions (uneven-extraction value, contradiction flag) |

Grinders, scales and machines are the shared reference data that A and B stand on (collaboration data), not a pillar of their own.

## 2. What B does today (so the improvements fit it)

A strict order, first match wins, each tier reporting its own range:

1. **Own coffees** (`SimilarCoffee.h`): the owner's nearest dialled coffee on this grinder and a close recipe; a ridge fit moves the number only if the owner's own coffees say roast, altitude or process matters.
2. **Library, fitted** (`Community.h`, `StartBasis::FITTED`): the library's common micron scale mapped onto this grinder with its own scale and offset from coffees in common.
3. **Library, same model**: the median of owners of the same grinder model.
4. **Library, microns**: the library's micron figure read straight off, wide range.
5. Nothing.

Corrections applied to tiers 2-4: bean age (library-measured rate), the recipe's flow against theirs (`kappa` times ln flow ratio), recipe distance. The index builder (`library-template/tools/build_index.py`) uses medians and robust spreads throughout, fits grinder offsets, and estimates `kappa` from within-coffee pairs.

**Status of the numbers in it** (all in code comments as unvalidated or conventions):

| Constant | Where | Source |
| --- | --- | --- |
| 15 um, 25 um default ranges; a sixth of the setting when the dial has no microns | `communityStart` | none: conventions |
| 3% of setting per step of recipe distance | `communityStart` | none (the same 3% that was removed from the similar-coffee shift; still here) |
| `KAPPA_PRIOR_UM = 84`, band prior/4 .. x4 | `build_index.py` | none stated |
| age cap 60 um, 90 days | `Community.h` | none stated |
| flow shift cap ln 0.5 | `Community.h` | none stated |
| recipe reach: 2 g dose, ln 0.3 yield | `Community.h` | none stated |

None is wrong on its face; all are unmeasured. The library is how they get measured (section 6, item S5).

## 3. How everyone else makes a first-shot suggestion

Sources: store pages, READMEs and source code read 14-16 Oct. Closed apps are **D** (documented, not run).

| Product | Method | Uses coffee properties? | Evidence of accuracy published? |
| --- | --- | --- | --- |
| **Grindhog** | "Knowledge graph" over beans, equipment, techniques; similar beans you logged; opt-in community from same bean and equipment; confidence weighs recent times and rating changes; detects recipes ageing out | graph of bean attributes (undisclosed) | no |
| **Grind Finer** | Your past successful extractions for the same setup (bean, grinder, machine, basket) | no | no |
| **Grindstep** | "First Shot Forecast" (Pro), learned seconds-per-step from your shots | undisclosed | no |
| **Visualizer.coffee** | Nearest earlier coffee on the same grinder and profile, then a language model judges roast, density, altitude, process (`grind_suggestable.rb`, read earlier) | LLM judgement | no |
| **crema** (GaggiMate helper) | Similar past shots by roast bucket plus origin/process word overlap, then an LLM proposes grind, dose, yield (`starting.py`, read) | roast bucket, words | no |
| **roastbook** | With no matching history: conservative guidance from bean, method and gear, marked "insufficient evidence", low confidence (read) | yes, through an LLM | no |
| **Puck Yeah!** | "Jarvis" advice from your shots; grinder step size from a database | undisclosed | no |
| **GrindConverter, Bee & Coffee** | Convert a setting between grinders or to microns; proprietary tables for 100-200+ grinders | grinder only | no |
| **Caliburr** | Community data of anonymised dial-ins (earlier rounds: unlicensed, blocked) | yes | no |

What this says:

* **Everyone matches on "same or similar coffee and same equipment".** That is our structure. Nobody has a better skeleton.
* **Nobody publishes accuracy.** There is no benchmark to beat or match. Our own measured error (section 6, S5) would be the first.
* LLMs are used to *judge* the roast/density question. The research found no source that makes that judgement reliable; for a controller, a measured fit is the sounder choice (already the design).

## 4. What actually moves the number of shots (simulation)

Method (files in `research/validation/`): the project's real `adviseGrind` with its default settings (2 s tolerance, 3-shot average, 3 um/s starting sensitivity, 20 um step cap) run against a **synthetic** grinder. Counts of shots until the advice says "keep". It measures the controller, not coffee; assumptions are stated in the source files. 3,000-4,000 trials per row.

**Start error is what costs shots.** Linear grinder, true sensitivity 3 um/s, 1 s of shot-to-shot noise (median shots until "keep"):

| Start error (um) | 0 | 5 | 10 | 20 | 30 | 40 | 60 | 80 | 120 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Shots | 1 | 1 | 2 | 2 | 3 | 3 | 4 | 5 | 7 |

About one extra shot per 20 um of error (the step cap). With 2 s of noise add roughly one shot at the 90th percentile.

**In relative terms** (power-law grinder, setting about 230 um, mean shots): start error 10% -> 1.9; 20% -> 3.3; 30% -> 4.3; 40% -> 5.4. **Getting the starting suggestion from about 40% off to about 10% off saves around 3.5 shots.** That is the size of the prize for pillar B.

**A wrong sensitivity guess costs less than I expected, except when it is too high.** With the true sensitivity 1.5 um/s and the controller assuming 3, even a 5 um start error took 3.4 shots (it overshoots and swings); assuming too low only slows the approach. Replacing the fixed 3 um/s by a guess proportional to the setting changed the average by only 0.1-0.4 shots (230 um regime). **What mattered more in coarse regimes was the 20 um step cap**: letting the cap grow with the setting (10% of it) cut a 400 um coffee from 4.3 to 3.05 shots on average, with no cost at 70 um.

**Tolerance in seconds is a narrow window in microns.** At 3 um/s the 2 s tolerance is +-6 um; at 70 um that is about 9% of the setting. So a first-shot suggestion is realistically "in the ballpark, then one nudge", exactly as the owner described. A range of +-15 um (21% at 70 um, 6% at 230 um) should be expected to need 2-3 shots at fine settings and about 2 at coarse ones.

**Correction (16 Oct 2026, after the owner confirmed it): every shot in the stored history is simulated.** An earlier version of this section reported a slope of about 2.7 um/s around 70 um and 11 um/s around 230 um and an "elasticity" near 0.8-0.95 as an observation from the owner's history. Those numbers describe **the emulator's model, not coffee**, and carry no evidence about real grinders. They are withdrawn as evidence; the idea that the sensitivity scales with the setting stays a hypothesis (supported only by the simulation of what the step cap does, and by the library's use of ln(flow)), to be tested on real shots (`dial-in-program.md`, section 7). No accuracy of pillar B can be measured until real shots exist.

## 5. Beanconqueror, plainly

* **Beanconqueror has no account system** (its README and code show none; the only credentials it stores are optional ones for Visualizer upload). It keeps data on the phone. You do not need an account to use it or to connect to it.
* **How the connection works.** The controller's web page builds a link of the form `beanconqueror://ADD_BREW?...` containing one shot (and its flow curve) as compressed text. Tapping it on a phone **with Beanconqueror installed** opens the app and offers to add the brew. Nothing is sent over the internet and nothing is linked to an identity. Without the app installed the link does nothing. A separate button reads a Beanconqueror backup file so its coffees and bags fill in ours.
* **What is proven and not.** The link was checked against Beanconqueror's own decoding code on a real shot from the history (accepted). It has **not** been tried on a phone.
* **What it is worth to the pillars.** A and B: close to nothing (it is a journal, not an advisor). It matters to people who already keep their logs there, as a convenience. So: keep it, no further investment, do not describe it as part of the grind advice.

## 6. Improvements to pillar B, inside the existing structure

All of these keep tiers 1-5 and leave pillar A untouched.

| # | Improvement | Why (evidence) | Cost | Touches pillar A? |
| --- | --- | --- | --- | --- |
| **S1** | **Say what to expect.** Show which tier produced the number (own coffee / fitted / same model / microns), the range in um **and as a percent of the setting**, and "about N shots" from the table in section 4. | Simulation: error in percent maps to shots; users trust a stated range | S, display only | no |
| **S2** | **Record the outcome of every suggestion**: suggested setting, tier, range, and the setting the coffee later settled at. Share it with the dial-in (fields `start_suggested`, `start_tier`). | Nobody publishes accuracy; without this the constants in section 2 stay guesses. This is the **measurement loop** for the whole pillar | M (history/library field, index builder statistic) | no |
| **S3** | **Test, then adopt, a setting-proportional sensitivity.** Replace the fixed `kappa` prior and (optionally) the fixed 20 um cap by values proportional to the setting, **only if** the library's pairs show it. Until then offer it as a switchable option, default off. | Simulation only (cap matters at coarse settings); no real-coffee evidence yet | S-M | **yes, as an input, behind a switch; needs the owner's approval** |
| **S4** | **Last-resort tier**: a manufacturer-published espresso range for the grinder, shown with a wide range and the label "typical for this grinder, not for this coffee". | The 5th tier is "nothing" today; every competitor suggests something | S-M, curation | no |
| **S5** | **Quantify the constants from the library** (the 15/25 um ranges, 3% per recipe step, age cap, reach) as soon as S2 data exists; replace each with the measured spread. | section 2 table | M, after data | no |
| **S6** | **Match on basket and machine class too** (optional fields), so shots that are not comparable are not mixed. | Grind Finer and Grindhog both make "same setup" include the basket; Roastbook filters by exact gear | M (with F12/F19 migration) | no |
| **S7** | **Age-aware confidence**: lower confidence on a library dial-in made on much younger or older coffee than this bag. | Grindhog says it does this; ours already has an age shift, so this only adds a flag | S | no |

Not recommended: using a language model for the roast/density judgement (no source shows it reliable; our fit is measured); copying any app's tables.

## 7. Impact analysis of outside integrations

Scored against the pillars. **A** live advice, **B** first-shot grind, **C** taste. Value: none / low / medium / high. "Account" says what the owner would need.

| Integration | A | B | C | What it adds | Account / cost | Effort | Risk | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Shared dial-in library** (ours) | none | **high** | low | the data pillar B lives on | GitHub; contributors needed | built | privacy terms (DATA-TERMS draft) | **core; needs contributors** |
| **Beanconqueror** link and import | none | none | none | convenience for its users | none (needs the phone app) | built | low | keep as is |
| **Visualizer** lookup | none | low | none | canonical coffee names; reading public data | none for lookup | built | name quality | keep as is |
| **Visualizer public shots as a seed prior** | none | medium **if** usable | none | thousands of grinder-setting records | terms of use not yet read; settings are in unknown units | M-L | licence, unit mapping | **roadmap; check terms first** |
| **Roastbook client** | none | none | none | shot times pushed to a journal; a target read back | self-hosted Roastbook and a token | S-M | low | roadmap, optional |
| **Read-only MCP server** | none | none | none | lets an assistant read and explain the owner's data | none | M | data leaves to a third-party model if the owner chooses | roadmap |
| **Importers** (Grindstep, Puck Yeah, Dial In Espresso, CSV) | none (typed times are not scale-measured) | low | low | history of owners switching over | none | M | formats unknown | **roadmap, not needed now** (no sample files available) |
| **Espresso Analysis export** | none | none | none | lets their code run on our curves | none | S | none | roadmap |
| **More scales** (BooKoo, Timemore) | none | none | none | wider hardware reach | the scale | M each | needs hardware | roadmap |
| **Home Assistant** (OBBW pattern) | none | none | none | automation | none | M | low | roadmap |
| **LLM explainer** | none | none | low | plain-language reading of advice | provider key | M | privacy, cost | roadmap, optional |

Reading the table: **only the library and its measurement loop (S2) move pillar B.** Everything else is convenience around the edges. That is why the owner is right not to add integrations casually: none of them, as far as I can show, improves the grind suggestions.

## 8. Recommended order

1. **S1** (say what to expect) and **S2** (record outcomes): small, in-structure, and they start producing the evidence every other number needs.
2. **S4** (last-resort tier) as curated data, with sources written down per grinder.
3. **S7**, then **S6** with the planned data migration.
4. When data exists: **S3** and **S5**, one constant at a time, each tested against the recorded outcomes.
5. Roadmap, in order of value: Visualizer seed (after reading its terms), Roastbook client, MCP, exports, importers, extra scales, Home Assistant, LLM explainer.

Pillar C additions (uneven-extraction taste value, time/taste contradiction flag) stay as in `app-landscape-and-feature-plan.md` F2-F3; the flow-shape anomaly flag (F1) still needs its check against ratings, which the current history cannot provide (no ratings).

## 9. What this does not establish

* The simulations use a synthetic grinder; they say how the controller behaves, not how coffee does. The elasticity, noise and regimes are assumptions.
* The elasticity figures in earlier drafts came from simulated shots and are withdrawn.
* No competitor's accuracy could be measured; none publishes any.
* Visualizer's data terms, the grinder manufacturers' published espresso ranges and Caliburr's licence were **not** checked for this document.
* Nothing here has been run on a real board.
