# Expanded ecosystem review: Beanconqueror, Grindstep, "Grind Finder", GaggiMate / Gaggia MCP servers, Roastbook, and AI dial-in models

> **Update 15 Oct 2026:** the owner's list resolved "Grind Finder" to **Grind Finer** (grindfiner.eu) and added Grindhog, Espresso Analysis, Beanary, Brewster, Puck Yeah!, Dial In Espresso, Coffee by Weight, MeticAI. See `app-landscape-and-feature-plan.md`, which supersedes this file's feature numbering (G1-G3 are now F2-F4, G4 is F19).

Written 14 Oct 2026. Question from the owner: what do these projects share with Brew by Weight, what can we learn, what can we reuse or connect to, and is there an AI model we can use or emulate to improve grind recommendations, **while the core Grind-by-Sync-style advice stays as it is and anything new only augments it**.

Evidence labels: **Verified** (source code or primary text read this round), **Documented-untested** (vendor/store listing or README only), **Plausible**, **Unknown**. Dispositions: Adopt, Integrate, Learn, Defer, Reject.

> **Not resolved: "Grind Finder".** No product with that exact name was found. Closest matches: GrindConverter (iOS, 200+ grinders, converts settings between grinders and microns, subscription), Bee & Coffee grinder converter (web/app, micron target to setting), Mahlkönig Home app, and the Bettercoffeelab explorer. Everything below about "Grind Finder" is therefore about the *category* (setting <-> micron conversion). **Please send a link or screenshot if you meant a specific product.**

## 1. Summary of findings

| # | Finding | Evidence | Disposition |
| --- | --- | --- | --- |
| 1 | **Grindstep's core method is the same as ours**: it learns "seconds per grinder step" from the owner's own consecutive shots (a regression), returns the change in the grinder's own units, shows the arithmetic, and says results are directional until there is enough data. Our `adviseGrind` µm/s sensitivity is the same idea, and ours uses real scale flow rather than typed times. | Documented-untested (store listing; app is closed source) | **Learn / validate**: confirms the base design; nothing to change. Borrow the "show the arithmetic" presentation |
| 2 | Grindstep offers CSV and **versioned JSON import/export** (free tier). No sample file seen. | Documented-untested | **Defer** import until the owner exports a sample (plan D5 stays blocked) |
| 3 | **Roastbook's AI recommender is an LLM over the owner's own filtered history** (OpenAI via TanStack AI, structured output, 45 s timeout, request log). Its rules are a good checklist: one primary controlled change; modest steps unless repeated small steps did nothing; never invent a precise number the grinder scale does not support; never assume bigger number = finer; read the grinder's setting format; sour+bitter points to uneven extraction (puck prep) not grind; "already good, change nothing" is a valid answer; confidence low/med/high from amount, recency, consistency, comparability of evidence; exact-setup filtering (same bean, method, machine, grinder, basket, accessories) and the last 50 shots. | **Verified** (`shot-recommendation.ts`, `shot-recommendations.ts`, `ai-brew-recommendation.server.ts`) | **Learn**: adopt the rules as *guards* on our own advice (section 4). Do not embed an LLM in the controller |
| 4 | Roastbook has a **shot-timer HTTP API** the controller could talk to: `POST /api/shottimer/shots {"seconds": 27.4}` (bearer token, 204) and `GET /api/shottimer/target` -> `{"targetSeconds": 28}`. | **Verified** (`docs/shottimer.md`) | **Integrate (optional client)**: push the shot time after a shot; read the target. See section 5 |
| 5 | Roastbook's taste axis is **one bipolar sour-to-bitter value (1-5)** and states plainly that it cannot express sour AND bitter, which "belongs in the detailed factors". | **Verified** (`extraction-balance.ts`) | **Learn**: our verdict set should keep a distinct "uneven" (sour+bitter) value that never moves the grind |
| 6 | **crema** (MIT code, CC BY-NC dataset) separates *execution score* (did the shot follow the recipe) from *taste*, changes one variable at a time, and stores each piece of advice with its outcome as an **experiment**. | **Verified** (README, SCOPE.md, `scoring.py`, `dataset.py`) | **Learn**: add an advice -> outcome record; keep execution separate from taste. Dataset licence (NC) means **do not ingest the data**; code ideas only |
| 7 | **gaggimate-mcp** (MIT) exposes a machine's profiles, shots and settings to an LLM via Model Context Protocol, with a **diagnostic decision tree** knowledge base and agent skills. Also `gaggiuino-mcp`, `meticulous-mcp` (MIT). GaggiMate firmware docs are CC BY-NC-SA, so **do not copy their text**. | **Verified** (README, CLAUDE.md, DIAGNOSTIC_TREES.md) | **Integrate (optional, later)**: an MCP server in front of the controller's `/api`; use the *shape* of the decision trees, written fresh |
| 8 | **Beanconqueror** already has a grind setting field (free text/decimal), a grind-size note, shot time, and imports/exports JSON; our hand-off link and coffee/bag import are built and verified against its decoder. | Verified earlier (source) | **Integrate** (done); phone test still open |
| 9 | **No trained grind model exists that we could download and use.** Every "AI dial-in" found is either an LLM prompt over your own history (Roastbook, crema, bean-whisperer, gaggimate-mcp) or a per-grinder regression (Grindstep, ours). The only data-driven model with a licence we could use is **optpresso** (GPL-3.0, image/shot-based), which is a different problem (predicting from puck images) and a licence that conflicts with embedding in the firmware. | Verified (repos) / Documented-untested (apps) | **Do not adopt a model.** Emulate the *method*: regression on the owner's own shots (already done) plus an optional LLM explainer outside the firmware |

## 2. Feature comparison (what each has that we lack, and the reverse)

| Capability | Brew by Weight | Beanconqueror | Grindstep | GaggiMate / Gaggia MCP | Roastbook |
| --- | --- | --- | --- | --- | --- |
| Grind advice from measured shot time | **Yes, from scale flow, automatic** | No (journal) | Yes, from typed times, per-grinder learned step | LLM over shot history | LLM over history |
| Advice in grinder's own units | Yes (divisor/sub-steps) | n/a | Yes | LLM, free text | LLM; told not to invent numbers |
| Taste ratings refine advice | Yes (inside tolerance only) | Rating stored, no advice | Unknown | LLM reads notes | Taste axis + tags into LLM |
| Learns from your shots | Yes (µm/s) | No | Yes (regression) | No (stateless LLM) | No (stateless LLM) |
| Similar-coffee starting point | Yes (ridge on owner's coffees) | No | Unknown | No | LLM only if same bean+gear |
| Bags / roast date / frozen | Yes | Yes (bean + roast date) | Unknown | No | Bean purchases |
| Shared community library | Yes (signed records) | No | No | Profiles (machine) | No |
| Machine control | Yes (BDB, SwitchBot, scale) | No | No | Yes | No |
| Import/export | JSON/CSV, Beanconqueror hand-off | JSON, Visualizer | CSV + versioned JSON | profile/shot export | shot-timer API |
| Open API for outside tools | `/api` (versioned) | No | No | MCP servers | HTTP API |
| Advice -> outcome record | **No** (gap) | No | Unknown | No | crema only |
| "No change needed" explicit state | Yes (keep) | n/a | Unknown | Yes in rules | Yes in rules |

## 3. Reusable logic, by licence

| Item | Source licence | Reuse as | Notes |
| --- | --- | --- | --- |
| Roastbook prompt rules (section 1 #3) | MIT | **Ideas and wording may be reused with attribution**; we reimplement as code guards | Keep a NOTICE entry if any text is copied |
| Roastbook shot-timer API contract | MIT | Implement a client from the documented contract | No code needed from them |
| crema scoring / experiment record | MIT (code) | Reimplement the *concept* | Dataset is CC BY-NC 4.0: never import |
| gaggimate-mcp structure | MIT | Our own MCP server can follow the same tool layout | Check each file's licence before copying |
| GaggiMate/Gaggiuino diagnostic text | CC BY-NC-SA / CC BY-NC | **Do not copy**; read, then write our own from first principles | Share-alike and NC are incompatible with our AGPL library |
| Beanconqueror code | GPL-3.0 | Interop only (links/JSON), no copying | Already how we use it |
| optpresso | GPL-3.0 | No | Different problem; licence conflict |
| Grindstep | closed | Interop only if the owner provides a sample export | |

## 4. Augmentations that leave the core logic untouched

All of these sit **beside** `adviseGrind` / `adviseGrindRated`. None changes the base verdict, the sensitivity learning, or the tolerance band. Each can be switched off.

| # | Augmentation | What it does | Touches core? | Learned from |
| --- | --- | --- | --- | --- |
| G1 | **Uneven-extraction flag** | If a shot is rated sour *and* bitter (or the rating tags say channeling/astringent), show "check puck prep" and **suppress any taste-driven grind refinement** for that shot. Base advice still runs. | No (a guard in `grindTasteHint`) | Roastbook, gaggimate-mcp trees, crema |
| G2 | **Taste vs time consistency check** | Fast + bitter, or slow + sour, is contradictory for the base logic: flag "time and taste disagree: check dose, distribution, temperature before changing grind". Never changes the number. | No | gaggimate-mcp trees, Roastbook rules |
| G3 | **Confidence label** (low/medium/high) on every advice line | Computed from shot count, recency, spread, bag match; reuses values `adviseGrind` already has | No (display only) | Roastbook |
| G4 | **Advice -> outcome record ("adjustment")** | Store the advised change, the change actually made, and the next shot's time/rating. Gives a measured check that advice worked, and the data for E-series learning. | No (new store, read-only on the advisor) | crema experiment record |
| G5 | **Exact-setup filter made visible** | Say which grinder, basket, bag the advice was drawn from. Already how our history match works; show it | No | Roastbook |
| G6 | **Execution vs taste** | Keep "shot matched the recipe" (time inside tolerance, weight on target) separate from "shot tasted good". Our verdicts already hold both; make the page show two numbers | No | crema |
| G7 | **Step-size rule** | Move only whole steps; if repeated small changes produced no change, allow a larger step (already what sensitivity does) | No | Roastbook rule matches ours |

Rejected: letting an LLM change the grind number in the firmware. Reasons: needs internet and an API key on the board, non-deterministic, would break the "SwitchBot/Lunar paths stay bit-identical" rule, and the data we already have makes a regression better than a language model at the numeric part.

## 5. Connections (compatibility opportunities)

| Connection | Direction | Effort | Status |
| --- | --- | --- | --- |
| Beanconqueror hand-off link + coffee/bag import | out / in | done | verified vs decoder; phone untested |
| Visualizer lookup | in | done | |
| **Roastbook shot-timer client** | out (shot seconds), in (target seconds) | small: one HTTP POST after a shot, one GET before | proposed; needs a bearer token setting; would let the target time come from Roastbook if the owner uses it. The controller's own target stays the default |
| **MCP server wrapping `/api`** | out | medium; runs on a PC/Raspberry Pi, not the ESP32 | proposed; read-only tools first (shots, advice, coffees), no write tools; lets any LLM client read our data and explain it |
| Grindstep import | in | blocked on sample file | owner can export from the app |
| Grind converters (GrindConverter, Bee & Coffee) | in (data) | licence unclear for their grinder tables | **Reject copying tables**; our library already stores microns per step per grinder with provenance |

## 6. AI model assessment

| Option | Fit | Verdict |
| --- | --- | --- |
| Download a trained grind-recommendation model | None found | not available |
| LLM over history with Roastbook-style rules | good for explaining and for sparse history | **Optional, outside the firmware** (MCP or page-side), read-only, never writes settings |
| Per-grinder regression of time vs step (Grindstep, ours) | what we already do, with better inputs | **keep as core** |
| Ridge regression across coffees (ours) | cross-coffee starting point | keep |
| Gradient-boosted / neural model on shared library | needs thousands of shots from several owners; library has none yet | **Defer** (plan E-series) |
| Image-based models (optpresso) | different problem, GPL | Reject |

Assessment: for one owner with one grinder, the data-efficient method is the regression we already run. A learned model only helps once the shared library holds many owners' shots; until then the useful "AI" is an explainer, not a predictor.

## 7. Proposed work order (nothing here has been built)

1. G1, G2, G3 (small, display and guards, host-testable).
2. G4 adjustment record (store version bump, API, page).
3. Roastbook shot-timer client (setting, POST/GET, test against a local stub).
4. Read-only MCP server (separate folder, outside the firmware).
5. Grindstep/Grind-converter work waits for the owner's sample file and product link.

## 8. Open questions

* Q30: What is "Grind Finder"? (link or screenshot)
* Q31: Does the owner use Roastbook, or only want compatibility with it?
* Q32: Should G1-G3 hints appear on the controller screen or only on the web page?
* Q33: Is an LLM explainer wanted at all, and with which provider?
* Q34: A Grindstep export (CSV and JSON) from the owner's phone.

## 9. What was not checked

Grindstep and the converter apps were read only through store listings and search results; none was installed or run. No licence text of each file inside gaggimate-mcp was audited. The Roastbook shot-timer API was read, not called. Nothing here was run on the board.
