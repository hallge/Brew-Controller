# App landscape review and per-feature implementation plan

> **Steer of 16 Oct 2026:** the owner has no access to the apps' data and does not want integrations added casually. Importers (F14), sample exports (Q40) and the other connection items are **roadmap only**, ranked by an impact analysis in `starting-grind-and-integration-impact.md` section 7. The pillar that most needs work is the **first-shot starting grind**; see that document (S1-S7) before choosing features from this list.

Written 15 Oct 2026. Owner's list (15 names): Grindhog, Beanconqueror, Grind Finer, Espresso Analysis, SingleDose, Grindstep, Beanary, Brewster, Open Brew by Weight, Coffee by Weight, roastbook, gaggimate-mcp, Puck Yeah!, Dial In Espresso / Dial-In, MeticAI.

Rules from the owner: **keep our method** (Grind-by-Sync-style time advice is the base, ratings refine it inside its tolerance); learn from everything that exists; be as compatible and "seamless" as possible; find non-grind improvements (channeling and similar); get to a good recommendation sooner and make the first recommendation better. Every feature below is therefore an **augmentation beside** `adviseGrind` / `adviseGrindRated`. None replaces them, and each can be switched off.

Evidence labels: **V** verified (source code or primary text read), **D** documented-untested (store listing / README, app not run), **P** plausible, **U** unknown. Where an app is closed source I could read only its store page or website; no app was installed or run. Dispositions: Adopt, Integrate, Learn, Defer, Reject.

Supersedes the earlier `expanded-ecosystem-review.md` where they differ (that file guessed "Grind Finder"; it is **Grind Finer**, below).

---

## 1. Name resolution

| Owner's name | Resolved to | Confidence |
| --- | --- | --- |
| Grind Finer | grindfiner.eu: iOS/Android app (plus legacy web app) that predicts a grinder setting from your successful past shots for the same setup | D |
| Grindhog | iOS app by Johannes Hermann Hoffart, darkcoff.ee/grindhog | D |
| Grindstep | iOS app, "Espresso Dial-In" (id 6789557773) | D |
| Espresso Analysis | github.com/ArvickC/Espresso-Analysis (Python; Varia AKU Mini scale; 1D CNN + Gaussian Process) | V (code) |
| Beanary | **two candidates**: iOS app by Maximilian Zeus (App Store id 6760232039, closed) and a Swiss student project `mitallemscharf/beanary` (SvelteKit + MongoDB, German docs) | D / V |
| Brewster | iOS "Brewster - Espresso Companion" (id 6761865274), closed, offline | D |
| Open Brew by Weight | github.com/Cheerpipe/Advanced-Shot-Stopper (AGPL-3.0-or-later) | V (earlier rounds) |
| Coffee by Weight | github.com/george-ags/coffee-by-weight (Raspberry Pi, GPL-3.0) | V (README) |
| MeticAI ("Metic") | github.com/hessius/MeticAI (Docker server + web + Android app for Meticulous machines) | V (docs, key source) |
| Puck Yeah! | iOS "PUCK YEAH! Espresso Tracker" (Alex Stewart), closed | D |
| Dial In Espresso | iOS app (Le Huang), closed; several same-named apps exist | D |
| roastbook | github.com/michidk/roastbook (MIT) | V |
| gaggimate-mcp | MCP server for GaggiMate (MIT), plus gaggiuino/meticulous MCP siblings | V |
| Beanconqueror | github.com/graphefruit/Beanconqueror (GPL-3.0) | V (earlier rounds) |
| **SingleDose** | **not found** under that name in two searches (App Store / GitHub / web) | U: please send a link |

---

## 2. App-by-app findings

Format: what it does, what it exposes, what we learn, disposition.

### 2.1 Grindhog (closed iOS, free)
* **Does (D):** "knowledge graph" over beans, equipment, techniques; recommends dose, yield, time and grind from similar beans you logged; claims 1-2 shots to dial in; opt-in **community** suggestions "from strong shots by other users with the same beans and equipment" (tagged TESTING); release notes say it **detects when a recipe that worked may stop working as beans age**, weights recent extraction times and rating changes when judging confidence, and gets more cautious about old recipes when newer shots disagree; baskets are first-class.
* **Exposes:** no export, import, API or integration listed. Privacy: usage data collected, not linked.
* **Learn:** (a) **age-aware confidence**: a good old recipe loses trust as the bag ages and newer shots drift; (b) recent-shot weighting; (c) basket as a matching key; (d) community match key = same bean + same equipment (we already share dial-ins keyed by coffee and grinder).
* **Disposition:** Learn. No integration possible today; the developer is reachable via their site (see 6, Q36).

### 2.2 Grind Finer (closed, iOS/Android + web)
* **Does (D):** logs shots; when you start a new one with the same setup it predicts a setting from past successful extractions; one-tap accept; equipment presets (Niche Zero, DF64 named). Says consistent naming of grinder/machine/basket improves it.
* **Exposes:** nothing on export/import/API.
* **Learn:** "same setup" matching is the whole product: bean + grinder + machine + basket. Our history match is by coffee/bag/grinder calibration; **basket is missing** (see F12). One-tap accept of the suggestion is a UX we have in part (advice shown on the result screen).
* **Disposition:** Learn.

### 2.3 Grindstep (closed iOS; free tier + Pro $14.99 lifetime / $4.99 yr)
* **Does (D):** learns **seconds per grinder step from your own consecutive shots** (regression), gives the move in the grinder's own units, shows the arithmetic, says results are directional until enough data. Free: logging, timer, directional advice, 2 bags, 1 grinder/machine, basic stats, **import and export (JSON full archive, CSV shots)**. Pro: quantitative adjustments with visible math, **First Shot Forecast**, **Brew Control Chart and roast-age drift**, unlimited bags, cost analytics, share cards, optional water/accessory fields. Marks shots "dialed-in" and lets you choose which sets a bag's current setting. Logs TDS.
* **Exposes:** JSON + CSV export; "imports compatible coffee logs" (which formats: **U**). No API.
* **Learn:** this is the closest design to ours and its feature list is a checklist. Where we already match: learned step size, control chart, TDS, bags, First-Shot-Forecast equivalent (similar coffee). **Where we do not:** the **dialed-in marker per bag** (a user-chosen "this is my current setting"), **roast-age drift** as an explicit forecast, **cost per bag**, and the **share card**. **Visible math** is the part to copy exactly: show "average 24.1 s vs 28 s target, sensitivity 2.4 µm per second, so 9.4 µm = 2 steps finer".
* **Disposition:** Learn (features F4, F6, F8); Integrate import/export once a sample file exists (F14).

### 2.4 Espresso Analysis (ArvickC; Python; **no LICENSE file** despite a "MIT" badge)
* **Does (V):** logs weight curves from a Varia AKU Mini scale to per-shot CSV (`elapsed_s`, `weight_g`) and a `manifest.csv` (`grind_setting`, `shot_time_s`, `label` under/balanced/over, `bean_name`, `curve_file`). A **1D CNN** (3 conv blocks, 16/32/64 channels) classifies shot quality from the weight and flow curve (resampled to 35 s at 10 Hz, Savitzky-Golay smoothing, flow clipped ±15 g/s), needing **15 labelled shots (5 per class)** before training. A **Gaussian Process** (RBF + white noise) maps grind setting -> shot time, per bean, needs 4 shots, and picks the next grind by "distance to the 25-30 s band minus 0.4 x predictive standard deviation" (an exploration bonus).
* **Learn:** (a) the **GP's useful output is its uncertainty**: how much the next shot would teach you; (b) the preprocessing recipe (resample, smooth, flow from derivative) is the same shape as our `FlowFeatures.h`; (c) their CNN has no validation beyond a random 80/20 split of a handful of own shots: **not evidence of accuracy** (V by reading; no results published).
* **Exposes:** plain CSVs. We can **export our shots in their layout** so anyone can run their code on our data (F13). Their code has no stated licence: ideas only, no copying.
* **Disposition:** Learn (uncertainty -> confidence F4; "next most informative shot" F6); Integrate as export-only; **Reject** the CNN for the firmware (data hunger, unverified).

### 2.5 SingleDose
* Unidentified. Single-dose workflow matters to us in one way: retention changes the first shot after a grind change. Whether our advice accounts for the first shot after a grind change is unchecked (Q35). **U.**

### 2.6 Beanary (two candidates)
* **App Store app (D):** bean library (roast date, origin, process, level), shot log (dose, yield, time, taste), "grind finer/coarser or adjust the ratio" guidance, saved dialed recipes, export/backup, share a recipe as an image. Recommendation logic and export format not stated.
* **GitHub student project (V):** SvelteKit + MongoDB; ratio recommendation = aggregate of best-rated shots per bean ("sweet spot parameters"), freshness computed from roast date, CSV/PDF export, QR code per bean, XP/leaderboard. AI-assisted code (declared). Licence not stated.
* **Learn:** only the "recipe as an image / QR for a bean" sharing idea. Nothing in the logic is new.
* **Disposition:** Learn (share card F15). Ask the owner which one was meant (Q36).

### 2.7 Brewster (closed iOS, free, offline)
* **Does (D):** logs grind, dose, yield, time, temperature, pressure; **five taste dimensions** (acidity, bitterness, body, sweetness, overall); "smart correction" suggesting grind finer / dose / time with an explanation of why; **100+ grinder profiles** with calibration; bean age. Export or delete; no import.
* **Learn:** five-dimension taste is richer than our one-axis verdict: it separates *extraction* (acidity vs bitterness) from *strength* (body). Reasons-with-advice again confirms "visible math".
* **Disposition:** Learn (F10 two-axis taste).

### 2.8 Open Brew by Weight (OBBW; AGPL-3.0-or-later; ESP32-S3)
* **V (earlier rounds, `source-code-findings.md`):** BLE scale protocol table, machine JSON profiles, LAN/HA integration API. Same licence family as us; same problem (stop at weight), no grind advice.
* **Integrate:** protocol table (done as plan), Home Assistant integration pattern (F16). **Contribute upstream** is open: any scale-protocol fixes we make.
* **Disposition:** Integrate/Contribute (no change to this round's plan).

### 2.9 Coffee by Weight (george-ags; Python on Raspberry Pi; GPL-3.0 per GitHub)
* **D (README read; code not read):** LM-BBW (La Marzocco Micra brew-by-weight) with **adaptive overshoot learning**, memory banks, shot-history web gallery; GRIND-BW (grind-by-weight skeleton); shared **scale drivers for Acaia, BooKoo, Timemore** and a `doc/BT_Scales/` folder with **Bluetooth protocol specs**. README also asks commercial users to contact the author, which sits oddly with GPL: treat as a request, not a licence term (Q37).
* **Learn:** the BooKoo and Timemore protocol notes are the cheapest route to more scales for us (we do Acaia Lunar and what OBBW's table gives); overshoot learning is the same idea as our `stopOffset` learning (compare constants, do not change ours).
* **Licence note:** GPL-3.0 code may be combined into an AGPL-3.0 work (GPLv3 section 13), so reuse is legally open in principle. **Do not copy until the licence file is read** (only the GitHub licence tag and README were seen).
* **Disposition:** Learn/Integrate (scale protocol docs; F17).

### 2.10 roastbook (MIT)
* **V:** LLM recommender with strong guardrails (one primary change; modest steps unless repeated small steps did nothing; no invented precision; read grinder scale direction; sour+bitter => uneven extraction => puck prep; "already good" is valid; confidence from amount/recency/consistency/comparability; exact-setup filtering, last 50 shots; evidence includes gear subtype details such as basket, WDT, puck screen, paper filter position, tamp force). **One bipolar sour<->bitter value (1-5)**, with the author stating it cannot express sour AND bitter. Shot-timer **HTTP API**: `POST /api/shottimer/shots {"seconds":27.4}` (bearer token, 204) and `GET /api/shottimer/target` -> `{"targetSeconds":28}`.
* **Disposition:** Integrate (client, F11); Learn (guards F2, F3, F4).

### 2.11 gaggimate-mcp (MIT) and siblings
* **V:** MCP server exposing machine, profiles, shots and settings to an LLM; a **diagnostic decision-tree knowledge base** and agent skills (channeling, sour, bitter, flow problems). Siblings: gaggiuino-mcp, meticulous-mcp (MIT). GaggiMate firmware docs are CC BY-NC-SA, so **the diagnostic text must not be copied**.
* **Learn:** the pattern: deterministic facts first, LLM second; tools are read-mostly; knowledge as decision trees.
* **Disposition:** Integrate (read-only MCP server, F16); Learn (trees, rewritten from first principles).

### 2.12 Puck Yeah! (closed iOS; free; Pro $0.99 / $5.99 yr)
* **D:** shot journal with dose, yield, ratio, timer, grind, temperature, basket, optional pre-infusion, pressure, TDS, flow control; recipes; **flow rate per shot**; **extraction yield from TDS**; bean freshness; **partial freezing splits a bag into two linked halves, the frozen half's clock pauses**; **per-grinder step size, auto-detected from a database**; maintenance reminders by time or shot count; **"Jarvis" on-device advisor** (grind advice that knows numbering direction, dose to basket, target flow by grinder-mark moves, weekly look-back); opt-in **community library** of beans, recipes, gear; CSV/JSON export, CSV import with rollback on failure, monthly backup reminder; Apple Health caffeine (out of scope).
* **Exposes:** CSV/JSON export and CSV import; no API.
* **Learn:** partial-freeze split (we hold frozen days per bag; no split), basket entity, import with all-or-nothing rollback (we should mirror that discipline), numbering-direction awareness (we handle direction in the grinder entity; keep).
* **Disposition:** Learn (F9 bag split, F12 basket); Integrate (CSV import F14); Reject gamification, caffeine, milk drinks.

### 2.13 Dial In Espresso / Dial-In (closed iOS; several same-named apps)
* **D:** plain journal (grind, dose, yield, time, temperature, rating, pre-infusion, photos, gear, AI autofill for bean details, "insights" summaries). CSV/PDF export, no import, no guidance logic described.
* **Disposition:** Integrate (our CSV export columns could follow common names so their exports/ours round-trip, F14); no logic to learn.

### 2.14 MeticAI / "Metic" (hessius; Docker, Bun server, web, Android; licence not detected by GitHub)
* **V (docs + `compass.ts` + `shotFacts.ts` thresholds):** a **deterministic, no-LLM shot analysis runs before any AI sees the shot**: effective control mode, targeted vs failsafe exit, **stall detection** (weight gain < 0.5 g), **channeling detection** (pressure drop >= 1.5 bar together with flow rise >= 1.5 ml/s), curve adherence. The "Espresso Compass" is a rule table over two taste axes: X sour(-1) to bitter(+1), Y weak(-1) to strong(+1), deadband 0.25, mapping to grind, **temperature**, ratio and dose moves. LLM (Gemini) is used for profile creation from a bag photo and for shot coaching. REST API, iOS Shortcuts, Home Assistant, Tailscale.
* **Learn:** (a) analysis facts first, language model last; (b) the **two-axis compass** separates extraction from strength so a weak shot is not "fixed" with grind; (c) channeling/stall are **rules on measured signals**. **Caveat:** Metic's channeling rule needs the machine's **pressure**; we have none, so the rule does not transfer; only the *idea* of a flow-shape rule does (F1). Its deadband (0.25), thresholds (1.5/1.5) and "ratio_up/ratio_down" wording (inconsistent in the code's reason text) are the author's choices with no cited source: **do not copy constants**.
* **Disposition:** Learn; Integrate only by pattern.

### 2.15 Beanconqueror (GPL-3.0)
* **V (earlier rounds):** the reference open journal; JSON export/import; Visualizer upload; BLE scales; free-text/decimal grind setting; beans, mills, brews, preparations. Our hand-off link and coffee/bag import are built and verified against its decoder (phone untested).
* **Disposition:** Integrate (done; add the phone test, Q26).

---

## 3. Cross-cutting findings

1. **Nobody else measures the shot.** Every grind advisor in the list works from typed times or taste. Ours reads the scale. That is the moat, and the reason not to change the method.
2. **Three techniques recur and we have two of them:** learned step size (Grindstep, ours), similar-setup lookup (Grind Finer, Grindhog, ours), and an LLM over history with guardrails (Roastbook, Metic, MCP servers). The third is optional for us and belongs outside the firmware.
3. **What others have that we lack:** roast-age drift forecast, dialed-in marker, basket entity, bag split on partial freeze, two-axis taste, uneven-extraction handling, advice->outcome history, confidence label, CSV/JSON import, share card, cost.
4. **No grind model to download.** The two ML pieces found (CNN classifier, Gaussian process) are small per-user fits; neither is validated.
5. **Channeling cannot be detected as Metic does (no pressure).** A cup-side proxy is possible from what we already store (flow roughness, acceleration, first drop) but **its thresholds must be learned from the owner's own shots**, not copied.

---

## 4. Per-feature implementation plan

Conventions. "Core" = `adviseGrind`/`adviseGrindRated` internals. Every item below says **Core touched: none** unless stated. Tests follow the project's usual stack: host C++ suite (`test/host/run.sh` with ziglang), Python self-tests, node page checks, emulator on throwaway data.

### F1. Flow-shape anomaly flag (channeling proxy) — **highest value, non-grind**
* **Source:** Metic `detectChanneling`/`detectStall` (concept only); Espresso Analysis curve preprocessing; our own `FlowFeatures` (already stored per shot and **currently unused by any advice**).
* **What:** after a shot, compare its `flowRoughnessGS`, `flowAcceleration`, `firstDropS` and `flow25/50/75` with the owner's own previous valid shots **at the same bag and setting (fall back to coffee)**, using robust spread (median and MAD). If two or more are unusual in the *channeling direction* (rougher, flow rising early then collapsing, very early first drop), show "uneven flow: check puck prep before changing grind". A stall flag: weight gain below a minimum for a stretch of the shot (blocked/clogged).
* **Why not copy thresholds:** Metic's need pressure. **P**: roughness and acceleration likely rise with channeling; **no source confirms the direction or size** for cup-side data.
* **Design:** new header `core/FlowAnomaly.h` (pure function, host-testable); new `uint8_t anomalyFlags` appended to `ShotRecord` (history v11 with converter, as v10 did) *or* computed on demand from stored features (preferred: no schema change; compute in API/page and on result screen). Needs min 5 comparable shots, else no flag. Display only; **never changes advice**, but G-guard (F2) uses it to say "do not chase grind on this shot".
* **Validation (do first, before any build):** pull the owner's 54-shot history (copy), tabulate roughness/acceleration against ratings and taste; see whether low-rated or sour+bitter shots separate. If they do not, the feature is **not shipped** (Reject on evidence).
* **Tests:** synthetic curves (smooth, surging, stalling); host test of the MAD logic; no-data and few-data cases; emulator shot.
* **Effort:** S (flag) after the M validation. **Risk:** false alarms erode trust: default off until validated.

### F2. Uneven-extraction guard (sour + bitter)
* **Source:** Roastbook, gaggimate-mcp trees, crema (all state the same practitioner rule; **not independently verified** as a rule, **P**).
* **What:** add a taste value `UNEVEN` (sour and bitter) next to SOUR/BALANCED/BITTER (`Taste` enum is `uint8_t`, append value 4: backward compatible). When a shot or the recent liked/rated shots carry UNEVEN, **taste-driven refinement is skipped** (the base time advice still runs unchanged) and the hint reads "check distribution, tamp level, puck prep".
* **Where:** `grindTasteHint`, `adviseGrindRated` input filter; UI and page taste picker; API enum; shared records (`taste` field values).
* **Core touched:** none in `adviseGrind`; one filter line on which shots feed the *rated* refinement.
* **Tests:** extend `testRatedTarget`; page taste check; converter not needed (enum append).
* **Effort:** S.

### F3. Time-versus-taste contradiction flag
* **Source:** gaggimate-mcp trees; Roastbook rules.
* **What:** fast shot and bitter, or slow shot and sour, contradict the extraction model: show "time and taste disagree: check dose, distribution, temperature before touching grind". Never alters the number.
* **Design:** pure function over (verdict vs target, taste) in `GrindAdvisor.h` (new function, base untouched).
* **Effort:** S. **Risk:** low.

### F4. Confidence label and "visible math"
* **Source:** Roastbook (confidence), Grindhog (recency/trend aware confidence), Grindstep (shows the arithmetic), Espresso Analysis (GP uncertainty).
* **What:** label each advice line low/medium/high from: shots behind the sensitivity (`learnShots`), spread, recency, same bag, sensitivity spread; and print the working: "avg 24.1 s vs 28 s, 2.4 µm/s, 9.4 µm = 2 steps finer".
* **Design:** `adviceConfidence()` reads fields `GrindAdvice` already carries; no new storage. Thresholds start as plain counts and are **stated as conventions** (like the 4-star and 3-shot ones; no source exists).
* **Core touched:** none (reads the result).
* **Effort:** S. Page + controller line + API field `confidence`.

### F5. Roast-age drift (forecast how the setting must move as the bag ages)
* **Source:** Grindstep Pro "roast-age drift", Grindhog "recipe may stop working as beans age", our own frozen-aging model.
* **What:** for a bag with enough shots across several days, fit time-at-setting-adjusted-by-sensitivity against days since roast (frozen days weighted by `FROZEN_AGING_FRACTION`). Show "this bag is running about X s faster per week; expect to go ~N steps finer in 5 days". Only a forecast; the base advice still decides what to do now.
* **Design:** `core/AgeDrift.h`: simple least squares on (agingDays, time - sensitivity*setting) per bag; minimum 6 shots over at least 6 days, else silent. No effect on the base verdict.
* **Evidence:** the direction (staling changes flow) is widely reported (**P**; earlier freezer research found only partial support: see `assumptions-validation.md`). The size is learned from the owner's own bag, so no external constant is needed.
* **Effort:** M. **Risk:** over-reading noise: require a minimum slope significance.

### F6. Faster first recommendations ("fewer shots to dial in")
* **Source:** Grindhog (1-2 shots), Grindstep First Shot Forecast, Espresso Analysis (GP exploration), our similar-coffee ridge.
* **What (two parts):**
  1. **Confidence on the start suggestion**: say how many similar coffees it rests on and show "no data" honestly (we already refuse to invent a number).
  2. **Prior for the sensitivity**: when a coffee has only 2-3 shots, borrow the owner's average µm/s from the same grinder across coffees as a *prior*, blended by shot count (a simple shrinkage estimate). The base `learnGrindSensitivity` keeps its own rules; the prior is used **only when it would otherwise fall back to a constant**.
* **Core touched:** the *fallback value* only (an input the base already has), behind a switch; **needs owner approval** because it is the one item touching core inputs. Alternative: leave core and show the prior as a hint.
* **Evidence:** shrinkage toward a pooled mean is standard statistics (P for this application). Validate by leave-one-out on the owner's history: does the prior predict the real next move better than the constant?
* **Effort:** M.

### F7. "Next most informative shot" hint
* **Source:** Espresso Analysis GP acquisition (distance to band minus kappa x sigma).
* **What:** when the shots so far sit at one setting, say "a shot two steps finer would tell us the most". Low priority; shares F4's uncertainty.
* **Effort:** S after F4. Defer until F4 is in.

### F8. Dialed-in marker per bag (user-chosen current setting)
* **Source:** Grindstep ("mark dialed-in shots; choose which one sets a bag's current setting").
* **What:** owner can pin a shot as "this is my dialed-in shot" for a bag; the start-of-bag suggestion and shared dial-in use the pinned setting before the advisor's settled setting. Reflects human judgement (taste) as the final say without touching the time advice.
* **Design:** `Bag::dialedShotId` (library file v4, append at the tail like earlier fields) and a shot flag; page button.
* **Effort:** S-M. Core touched: none.

### F9. Partial freeze / bag split
* **Source:** Puck Yeah (split into two linked bags; frozen half's clock pauses).
* **What:** "freeze part of this bag" creates a second bag with the frozen share of the weight and `frozenSince`; both keep the same roast date. We already track frozen days per bag, so this is mostly a UI/API action over existing fields.
* **Effort:** S. Depends on nothing.

### F10. Two-axis taste (extraction and strength) with non-grind hints
* **Source:** Metic Compass (X sour-bitter, Y weak-strong), Brewster (five dimensions), Roastbook (single axis, with the author's caveat).
* **What:** keep our verdict (sour/balanced/bitter/uneven) and add an optional **strength** value (thin/ok/strong). Strength drives **yield/dose** hints only ("thin: try a shorter yield or +0.5 g dose"), never the grind. Temperature hints only on machines where the controller knows the temperature (not the Breville here).
* **Evidence:** the separation of extraction from strength is textbook (P); magnitudes are not sourced. Hints state a direction, not a number, until an owner measures (TDS is stored).
* **Effort:** S-M. Core touched: none.

### F11. Roastbook shot-timer client
* **Source:** Roastbook `docs/shottimer.md` (V).
* **What:** setting for base URL + bearer token; after each valid shot `POST` the seconds; optionally `GET` the target and offer it as the recipe's target time (owner must confirm; default off). Offline-safe queue (drop after N failures). Token stored with the other secrets (never in exports).
* **Tests:** a local stub server; token never in logs; failure does not delay the next shot (async, short timeout).
* **Effort:** S-M. **Risk:** external dependency: isolate in its own module with a kill switch.

### F12. Basket (and puck-prep) as matching keys
* **Source:** Grind Finer, Grindhog, Puck Yeah, Roastbook (exact-gear filtering).
* **What:** optional basket label/size on the recipe and shot (and optional prep tags: WDT, screen, paper). History matching for the *rated* and similar-coffee features can then separate shots that are not comparable; **base advice keeps its current matching** unless the owner opts in. Shared dial-ins gain an optional `basket`.
* **Effort:** M (schema: history v11 + library v4 alongside F8). Do together with F1/F8 to share one migration.

### F13. Export in other tools' shapes
* **What:** (a) Espresso Analysis layout (manifest + per-shot curve CSV, columns seen in their code); (b) a "common journal CSV" whose column names follow the exports above once samples exist; (c) Beanconqueror hand-off (done).
* **Effort:** S per format. Page-side or `/api/export?...`; no firmware logic.

### F14. Import (CSV universal mapper, Grindstep JSON, Puck Yeah CSV)
* **What:** page-side wizard: upload CSV/JSON, map columns (auto-guess common names), preview, then POST in one transaction with **all-or-nothing rollback** (Puck Yeah's good practice). Imported shots are marked `imported` and have no curve.
* **Policy decision (owner):** imported typed times are **not** allowed to move the base sensitivity (measured vs typed timing differ by clock definition: see `GrindTimeBasis`); they may seed the similar-coffee fit and history. **Needs the owner's call.**
* **Blocked on sample files** from Grindstep, Puck Yeah, Dial In Espresso (headers unknown). Build the mapper now, presets later.
* **Effort:** M.

### F15. Share card (image of a dialed-in recipe)
* **Source:** Beanary, Grindstep Pro. Page-side canvas to PNG with coffee, recipe, grinder setting, time. No data leaves the device. Effort S; nice-to-have.

### F16. Read-only MCP server and Home Assistant integration
* **Source:** gaggimate-mcp, meticulous-mcp, Metic (REST + HA), OBBW (HA integration pattern).
* **What:** a small separate process (Python or Node, runs on a PC/Pi, **not on the ESP32**) exposing tools: `list_coffees`, `list_shots`, `get_advice` (with the arithmetic and confidence), `get_flow_facts`, `compare_shots`. **Read-only first.** Pairs with Roastbook-style guardrails in its tool descriptions. Optional later: HA custom component following OBBW.
* **Licence:** our own code (AGPL); the MCP libraries are MIT.
* **Effort:** M. **Risk:** an LLM client may reveal data to a third-party model: document in `docs/SAFETY.md` / `DATA-TERMS.md`.

### F17. More scales (BooKoo, Timemore) from protocol notes
* **Source:** coffee-by-weight `doc/BT_Scales/`, OBBW protocol table, BooKoo's published specs.
* **What:** driver module per scale behind our `ScaleDriver` interface; feasibility depends on the licence reading and hardware to test. **Blocked on a scale to test with.** Effort M each.

### F18. Cost per bag
* **Source:** Puck Yeah, Grindstep Pro. Bag price + weight -> cost per shot. Page only, S. Low value; do if trivial.

### F19. Advice -> outcome record ("adjustment")
* **Source:** crema experiment record (see `expanded-ecosystem-review.md`).
* **What:** store {advised move, move made, next shot's time and rating}. Gives a measured "advice worked / did not" rate and the data later learning needs. Shares the v11 migration with F8/F12.
* **Effort:** M.

### Not adopted
Gamification (XP, streaks, achievements, wrapped cards), caffeine/Apple Health, milk-drink logging, a CNN shot classifier, an LLM inside the firmware, copying GaggiMate/Gaggiuino diagnostic text or crema's dataset (non-commercial licences), copying grinder tables from GrindConverter/Bee & Coffee (proprietary). **Reasons:** off-mission, unvalidated, or licence-incompatible.

---

## 5. Integration matrix (who can talk to whom)

| Counterparty | They offer | We offer / do | Status |
| --- | --- | --- | --- |
| Beanconqueror | JSON import/export, Visualizer upload | hand-off link, coffee/bag import | done; phone test open |
| Visualizer | public coffee lookup | lookup in page | done |
| Roastbook | bearer-token shot-timer API | client (F11) | proposed |
| Grindstep | JSON + CSV export | import (F14) | blocked on sample |
| Puck Yeah | CSV/JSON export, CSV import | import (F14); our CSV export fits their import? **U** | blocked on sample |
| Dial In Espresso | CSV/PDF export | import (F14) | blocked on sample |
| Espresso Analysis | plain CSV manifest + curves | export (F13) | proposed |
| MCP clients (Claude, others) | MCP protocol | read-only server (F16) | proposed |
| Home Assistant | OBBW pattern | integration (F16) | later |
| Grindhog, Grind Finer, Brewster, Beanary (App Store) | nothing public | nothing possible | contact the developers (Q36) |
| OBBW / coffee-by-weight | scale protocols, HA | reuse notes; upstream fixes | ongoing |

---

## 6. Recommended order

1. **Validation first (no code shipped):** F1 on the owner's real history (does flow shape separate bad shots?), F6 prior by leave-one-out. Results decide whether F1/F6 are built.
2. **Small guards and display:** F4 (confidence + visible math), F2, F3.
3. **One migration, three features:** history v11 + library v4 carrying F8 (dialed-in marker), F12 (basket/prep), F19 (adjustment record), F9 (bag split).
4. **Connections:** F11 (Roastbook), F13 (exports), F14 (import mapper, presets when samples arrive).
5. **Insight features:** F5 (age drift) once bags have enough data; F10 (strength axis).
6. **Outside the firmware:** F16 (MCP, then HA).
7. **Hardware-bound:** F17.

Every step keeps `adviseGrind` bit-identical except the single optional input in F6, which is switchable and needs the owner's approval.

## 7. Open questions (new)

* **Q35** Does the controller handle the first shot after a grind change (retention / purge) in advice? Check `GrindAdvisor` and docs before F6.
* **Q36** Which "Beanary" and "SingleDose" were meant? Should we contact Grindhog / Grind Finer / Brewster developers about an export or API?
* **Q37** coffee-by-weight and MeticAI licence files: read before any reuse (no SPDX detected for MeticAI; coffee-by-weight asks commercial users to contact the author).
* **Q38** F14 policy: may imported typed times influence the base sensitivity? (recommended: no)
* **Q39** F6: allow a cross-coffee prior for sensitivity, or keep it a hint only? (touches a core input)
* **Q40** Samples wanted: Grindstep JSON and CSV, Puck Yeah CSV, Dial In Espresso CSV.

## 8. What was not checked

No closed-source app was installed or run; all of their details come from store pages or the developers' sites and are **D**. Grindstep's regression method, minimum shot count and file formats are **not published**. Source for Espresso Analysis, Metic (key files only), roastbook and gaggimate-mcp was read, not executed. coffee-by-weight and OBBW code was not re-read this round. No claim here has been tried on the real board.
