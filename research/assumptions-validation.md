# Assumptions and guesses: what research could and could not confirm

Dated 2026-10-13. Question asked: confirm the 3%-of-setting step in the similar-coffee shift, the ratings thresholds and the freezer assumption, and any other guess. **Short answer: none of the three could be confirmed as stated.** For each, this page gives what was searched, what was actually found (with how well it was checked), and what the code does now. Where research found nothing usable, the code no longer pretends to a number: it measures from the owner's own data, or shows a hint without moving anything.

Method: web search (standard and extended), then fetching primary pages and open indexes (Europe PMC, Crossref) for the specific claims. Paywalled or login-gated pages (Nature, Springer) could not be fetched; abstracts were read from Europe PMC instead. "Verified" below means I read the cited text or abstract myself; "secondary" means it only appeared in a search summary or a trade article.

## 1. The Grind-by-Sync base (what the "old advice" is modelled on)
| Item | Finding | Evidence |
| --- | --- | --- |
| Tolerance around the target time, length of the moving average, step size, which shots are ignored, purge after big changes | **Mahlkönig does not publish any of them.** The FAQ says the grinder compares "the moving average of the actual brew time to the recipe target time" and reacts when a shot is outside the recipe, nothing more | Verified: fetched `mahlkoenig.com/blogs/news/the-sync-system-technical-faq` and asked for each item; all "not stated". (Prima Coffee's review, already cited in `GrindAdvisor.h`, is where the 15 s and "target + 15 s" valid-shot limits come from) |
| Therefore | The base's numbers (±2 s, window 3, 20 µm max step, 3 µm/s starting guess) are **this project's own settings**, not Mahlkönig's. They are user-adjustable and the µm/s figure is replaced by a measurement from the owner's shots as soon as two settings exist | code unchanged; documented |
| Starting guess of 3 µm per second of error | No controlled study of seconds per grinder step exists. Forum reports differ by a factor of ~3: one Lagom P64 owner reported one unit (≈10 µm) ≈ 10 s (≈1 µm/s); another reported 7.5 µm ≈ "several seconds" (≈2 µm/s) | Secondary (Home-Barista threads). 3 µm/s sits just above that range; it only matters until the owner's own history has two settings |

## 2. Ratings thresholds (4 stars liked, ≥3 shots, 8 s spread, 20% bound, etc.)
| Item | Finding | Evidence |
| --- | --- | --- |
| A "liked" threshold for a 5-star coffee rating | **No source found.** Nothing in coffee sensory literature defines a liking threshold for single-rater star ratings. 9-point hedonic scales are the norm in consumer work; a 2025 study (136 consumers) found 5-point and 9-point scales give significantly different results, i.e. the scale length itself matters | Secondary (search summaries of SLACAN 2025 / UC Davis studies); not read in full |
| How reliable one person's repeated rating is | General sensory work: replicate-sample correlations on 9-point scales ranged **0.32–0.75** for the same juice in one session (Massey University study); a one-week retest of food liking (Deakin) gave ICCs from **0.40–0.59 (moderate) to 0.75–1.00 (excellent)**. Not coffee, not 5-star | Secondary (search summaries of the two papers); the figures are what those summaries state |
| What that supports | Individual ratings are **noisy**, so decisions should rest on several shots and a robust statistic (median), and should never override a measured quantity. It does **not** support any particular count or threshold | reasoning from the above |
| Shot-to-shot variation of brew time at one setting | **No published standard deviation.** One forum thread of ~130 shots with the same beans and setting reported 20–30 s; Clive Coffee, Kaffeemacher and Barista Hustle all name grinder retention, coffee age and dose drift as causes | Secondary |

**Result:** the 4-star line is a convention, the 3-shot minimum is "the smallest sample whose median ignores one odd shot", and the 8 s spread and 20% bounds were my inventions. **They are removed.** The redesign (see §6) makes ratings a *refinement* of the time advice: only inside its own tolerance, and with a band that follows the owner's own liked-shot scatter (standard error of the median, floored at 0.5 s because below that shot-to-shot noise makes a difference meaningless; capped at the base tolerance). The two judgement constants that remain (4 stars; 3 shots) are labelled as conventions in code and docs.

## 3. The freezer assumption (frozen days do not age the coffee)
| Item | Finding | Evidence |
| --- | --- | --- |
| Does low temperature slow staling? | Yes. Storage at 5 °C vs 20 °C for a month slowed changes in the volatile profile and kept sensory quality better (Gantner et al., *Foods* 2024) | **Verified** (abstract via Europe PMC) |
| Rate of CO₂ degassing vs temperature | Smrke et al. (*J. Agric. Food Chem.* 2017) built the gravimetric method but measured at **35 °C only** (their ovens), so it gives no sub-zero data | **Verified** (read the paper text) |
| Arrhenius dependence | A search summary reports **Ea ≈ 17–18 kJ/mol** for CO₂ release (attributed to Wang & Lim, *Food Res. Int.* 2014, which exists: DOI 10.1016/j.foodres.2014.01.027). I could not read the primary text. On that Ea, a −18 °C day is ≈ 0.33 of a 22 °C day | Secondary; calculation mine |
| "70 days at −25 °C ≈ 2 days at 35 °C" | Appears in trade write-ups (attributed to SCA / a Royal Coffee post); **I could not trace it to a primary paper.** Taken at face value it is ≈ 0.05 of a 22 °C day | Unverified |
| Staling vs degassing | Degassing is only a proxy; staling also involves oxidation and loss of volatiles. The 2012 SCA literature review (as quoted in search results) notes none of the reviewed studies measured effects of freezing or of the temperature cycling that goes with it. Even sealed at <1% O₂ in a freezer, aroma volatiles were still lost over a year | Secondary |
| Does the freezer change the grind itself? | **Yes, and it is not modelled:** grinding cold beans gives a narrower particle size distribution and a smaller mean particle size (Uman et al., *Sci Rep* 2016) | **Verified** (abstract via Europe PMC) |

**Result:** the assumption "freezer days do not age the coffee" (fraction 0) is **contradicted** as stated: freezing slows ageing, it does not stop it. The supportable range is **0.03–0.33**. `FROZEN_AGING_FRACTION` is now **0.10** (geometric mean of the bounds), documented as bracketed-not-measured, the same constant in firmware and in the index builder, and the index will measure it from `frozen_days` once contributors have it. The cold-grind effect is called out in the code comment and the page, not modelled.

## 4. The 3%-of-setting similar-coffee step, and the factors behind it
| Factor | Direction used | Finding | Evidence |
| --- | --- | --- | --- |
| **Step size** (3% of the setting per step; half-steps per process class and per 500 m) | n/a | **No source gives any factor's size in grinder units.** The numbers were my guess; there is nothing to confirm them against | searched: roast, altitude, process vs grind setting; none quantitative |
| **Roast level** | darker → coarser | One uncontrolled EK43 experiment: at a fixed setting the median particle size fell from ~305 µm just past first crack to ~207 (filter), ~160/153 (espresso) and ~120 µm past second crack; darker roasts' shots ran slower at the same grind. Direction supported; the author himself notes no systematic scientific work exists | Secondary (blog-published; not peer-reviewed; one grinder, one setting). Uman 2016 reports no roast result in its abstract |
| **Altitude / density** | higher → finer | Altitude does raise green-bean density (Nepal 2021 study as summarised by Roast Magazine; Mozambique, *Agronomy* 2022, DOI 10.3390/agronomy12102540 via Crossref). **No study ties density to the espresso grind setting.** Uman 2016 found particle size distribution independent of bean origin | density: verified existence of both papers; grind link: practitioner claim only |
| **Process (natural vs washed)** | natural → coarser | Uman 2016: particle size distribution **independent of processing method** (verified abstract). Advice in trade sources contradicts itself (some say naturals extract fastest, others that they are denser in sugars). One logged home dataset of 993 brews found **no extraction difference** by process | verified (Uman); secondary (others) |

**Result:** the fixed step is **removed**. The similar-coffee suggestion now: (a) starts from the nearest of the owner's own dialled coffees and does **not** shift the number on convention; (b) shows the conventional direction as a hint in words, and for process says plainly that nothing supports it; (c) moves the number only when the owner's own coffees show the effect: a ridge regression of ln(setting) on roast, altitude and process over their other dialled coffees (≥ factors + 2 coffees, each factor must vary, shrunk to zero, a factor of the unexpected sign is ignored); (d) sets the range from the scatter of the owner's own settings with a small-sample correction (10% only when there is a single coffee to go on, stated as "wide on purpose"). Tests build coffees whose settings follow a known 5%-per-level law and check the fit recovers about 4.5% (ridge-shrunk) and moves the number finer for a lighter roast; reversed data and too few coffees move nothing.

## 5. Other guesses found while checking
| Guess | Status |
| --- | --- |
| Recipe-distance reach (2 g dose, ln-yield 0.3) and 3% range per recipe step in `Community.h` | pre-existing project design, not researched here; unchanged |
| 5 µm minimum step on a grinder marked in microns | an engineering floor (the finest a person can set), not a measured figure |
| Taste note thresholds (2 of the last 3 shots agree, none balanced) | a rule of thumb; it only adds a note and never moves the grind |
| `SIMILAR_NO_DATA_FRACTION` = 10% | the explicit "no data" width; it is the one remaining guess in the similar-coffee path, and it errs wide |

## 6. Consequences for the design (implemented)
1. **Ratings refine, they do not replace.** The Grind-by-Sync-style verdict is computed first and stays the default. Ratings act only when it says "keep", only if the liked time is inside its tolerance, and only to bring the average onto the liked time. Detail in `docs/COMMUNITY.md` 8e.
2. **Similar coffee is measured, not assumed** (8g).
3. **Freezer fraction is 0.10, bracketed** (8f).

## 7. Still unconfirmed (and what would confirm it)
| Open item | What would settle it |
| --- | --- |
| Whether refining toward liked shots improves cups | owners using the setting on and off, with later ratings as the outcome (plan E4) |
| The true freezer fraction | contributors' repeat dial-ins on bags with `frozen_days`; or a degassing/volatile study at −18 °C vs 22 °C read from its primary text |
| Roast/altitude/process effects in grinder units | pairs of the owner's coffees on one grinder, accumulated in the library (plan E2); or a controlled grinder + extraction study |
| Primary text of Wang & Lim (2014) and the SCA staling review | access to the papers |
