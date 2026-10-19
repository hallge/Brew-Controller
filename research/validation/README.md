# Validation harness: Beanconqueror `ADD_BREW` decoder

Purpose: run Beanconqueror's *own* decoder (`src/services/intentHandler/brew-handoff.decoder.ts`, commit `bbf8de0`)
against an envelope shaped like this controller's shot export, without installing the app.

Preparation (done once, in a scratch directory outside this repo, because the decoder is GPL-3.0):
1. `git clone --depth 1 https://github.com/graphefruit/Beanconqueror`
2. Make a folder with `services/intentHandler/brew-handoff.decoder.ts` and `enums/preparations/preparationTypes.ts` copied from it.
3. In the copied decoder change the extensionless import to `preparationTypes.ts`.
4. Stub the zip fallback: `node_modules/@zip.js/zip.js/index.js` containing `export class BlobReader{}; export class ZipReader{}` (+ a package.json with `"type":"module"`); the fallback path is never used on Node 24 because `DecompressionStream` exists.
5. `package.json` with `{"type":"module"}`.
Run with Node 24: `BC_DECODER=file:///.../services/intentHandler/brew-handoff.decoder.ts node --experimental-transform-types --no-warnings beanconqueror-handoff-check.mjs`

Limits: the curve is **simulated** (not a recorded shot); this proves the wire contract, not that the app displays the shot correctly.
Results are in `../roadmap-validation.md`.

## Settling simulations (16 Oct 2026)

`settle_sim_start_error.cpp` and `settle_sim_sensitivity_and_cap.cpp` run the project's real `adviseGrind` (src/core/GrindAdvisor.h, default settings) against a SYNTHETIC grinder (time falls as the burrs open, plus noise) and count the shots until the advice says "keep". They measure how the controller behaves, not how real coffee behaves: the response model, noise and elasticity are assumptions stated in each file. CSV outputs are included. Interpretation: `research/starting-grind-and-integration-impact.md`.
