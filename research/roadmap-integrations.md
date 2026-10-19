# Integration roadmap (reserved)

Decided 16 Oct 2026: **integrations are on the roadmap, not in the current build cycles.** Reason: all shot data so far is simulated; the product's value is how fast and how reliably it gets a coffee dialled in (`dial-in-program.md`), and no integration was found that improves that. The research is kept in full and is the starting point when the gate below is met.

## Gate to start

All of: (1) real shots collected (sessions A-D in `dial-in-program.md` section 7); (2) the measured two-shot rate and start error reviewed against the 75-80% goal; (3) the owner decides to proceed. Until then, only maintenance of what is already built (Beanconqueror hand-off and coffee import, Visualizer lookup) is done.

## Where the reserved research lives

| Topic | Document |
| --- | --- |
| Ecosystem, repositories, licences | `ecosystem-inventory.md`, `license-and-risk-register.md`, `integration-catalog.md` |
| Source-code findings (Visualizer, Beanconqueror, Caliburr, OBBW, ...) | `source-code-findings.md` |
| Per-app review (15 apps) and feature list F1-F19 | `app-landscape-and-feature-plan.md` |
| Impact of each integration against the pillars | `starting-grind-and-integration-impact.md` section 7 |
| Data model and backend options | `data-model-analysis.md`, `backend-assessment.md` |
| Phases and decisions | `implementation-roadmap.md`, `implementation-plan.md`, `architecture-decisions.md` |

## Order when the gate opens (by value to dial-in, then cost)

1. Visualizer public shots as a seed prior for the starting grind: **read its terms first**; check the units of its grinder settings.
2. Roastbook shot-timer client (push shot times, read a target).
3. Read-only MCP server (lets an assistant read and explain the owner's data).
4. Export in other tools' layouts (Espresso Analysis style curves; common journal CSV).
5. Importers (Grindstep, Puck Yeah, Dial In Espresso, CSV mapper), when the owner has sample files.
6. More scales (BooKoo, Timemore) from published protocol notes, when a scale is available to test.
7. Home Assistant, LLM explainer.

Things not to do at any stage: copy GaggiMate or Gaggiuino text (non-commercial share-alike), ingest the crema dataset (non-commercial), copy grinder conversion tables from closed apps, put a language model inside the firmware.
