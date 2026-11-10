# What in this repository comes from Open Brew by Weight

Dated 2026-11-10. **Not legal advice.** This list is for the project owner and, before any App Store
release, a lawyer (⚖). It lists every place where code, constants or design in this repository come
from Open Brew by Weight (OBBW), by Felipe Urzúa and contributors (AGPL-3.0-or-later). OBBW is
`Advanced-Shot-Stopper`, with its `libraries/EspressoScaleBLE`. The list was made by reading the
files and their history (`git log`). It was not made by comparing against OBBW's sources line by
line.

Why it matters: an App Store build can't carry third-party AGPL code (`research/ios-app-feasibility.md`
§4.1). Each item below is either covered by a permission from OBBW's authors
(`research/obbw-exception-request.md`), or replaced by the clean code in `src/core/StopMath.h` and
`lib/ScaleWire/`, or is only an idea or a number.

## How each item is graded

- **Verbatim**: OBBW's files, copied.
- **Adapted**: OBBW's code rewritten here, with the same maths, structure and constants.
- **Idea or number**: a rule, a threshold or a design taken from OBBW, written here in this
  project's own code. Facts, numbers and ideas are generally not protected by copyright, but how far
  that goes is for a lawyer.
- **Own**: this project's code. It may mention OBBW, but nothing of OBBW's is in it.

## 1. Verbatim: `lib/EspressoScaleProtocol/src`

| File | Lines | Grade | Notes |
| --- | --- | --- | --- |
| `ScaleProtocol.h` | 152 | Verbatim | The protocol-table interface (`ScaleProtocol`, `ScalePayload`, helper declarations, Bookoo helpers) |
| `ScaleFeatures.h` | 74 | Verbatim, **modified** | `ScaleFeatureReinitWhileSilent` added in commit 72f7ee9 (2026-11-02) |
| `ScaleProtocolCommon.cpp` | 152 | Verbatim | Helpers: weight check, payload copy, name match, UUID parse, XOR, LE read, divisors, Acaia timer and checksum, Felicita ASCII timer |
| `Acaia.cpp` | 167 | Verbatim, **modified** | Legacy (2a80) and current Acaia tables. `ScaleFeatureReinitWhileSilent` added to the features in 72f7ee9 |
| `Felicita.cpp`, `Eclair.cpp`, `Decent.cpp`, `Difluid.cpp`, `Myscale.cpp`, `WeighMyBru.cpp`, `Varia.cpp`, `Eureka.cpp` | 71–110 each | Verbatim | One table per scale family |
| `Registry.cpp` | 43 | **Own** | Its header says it replaces OBBW's `protocols/Registry.cpp` and lists only the families this project drives |

**Action, whatever the route:**
- README §12 and `RESEARCH.md` §7 call these files "unmodified", but two of them were changed in
  72f7ee9.
- AGPL §5(a) asks that a modified file carry a notice saying so, with a date.
- Add that notice to `ScaleFeatures.h` and `Acaia.cpp`, and correct the two documents.

## 2. Adapted: `src/core/BbwAlgorithms.h`

| Item | Lines | Grade | OBBW source (as the file states) |
| --- | --- | --- | --- |
| `predictStopTimeS` and the constants `TREND_POINT_COUNT` 10, `TREND_MIN_LAST_SAMPLE_G` 10, `TREND_MIN_HORIZON_S` 0.25 | 16–61 | Adapted | `ShotStopperBbwEwma.h`, `bbwEwma::predict` |
| `learnStopOffset`, `clampOffset` | 20–22, 63–75 | Adapted | `bbwEwma::learn` (alpha 1.0 = shotStopper's full correction) |
| `ALPHA_CANDIDATES`, `validAlpha`, `AlphaEvidence` | 77–153 | Adapted | `bbwEwma::Evidence` |
| `FlowEstimator` | 155–209 | Own | Not in the file's list of adapted items |

**Clean replacement:** `src/core/StopMath.h` (`predictCrossingS`, `nextOffset`, `AlphaSelector`),
selected with `-DBBW_CLEAN`. It was written from the behaviour specified in
`research/obbw-clean-spec.md` §2, not from this file.

## 3. Ideas and numbers in the project's own code

| Where | What came from OBBW | Grade |
| --- | --- | --- |
| `src/core/Types.h:31-50` | Limits: `MAX_OFFSET_G` (raised from 5 to 10 here), target, dose and ratio limits, `HARD_MAX_SHOT_MS`, `MAX_AUTOMATION_WEIGHT_G`, slew limit and allowance, `DIRECT_STOP_CONFIRMATION_SAMPLES` 2, `TARE_SETTLE_BAND_G`, `FIRST_DROP_THRESHOLD_G`, `FLOW_ON_G_S` 0.6 / `FLOW_OFF_G_S` 0.2 / `FLOW_HOLD_MS` 500, `MAX_SETTLED_WEIGHT_DROP_G` 2 | Numbers |
| `src/core/BrewController.cpp:15` | Cup-placement stability: OBBW's re-tare defaults | Numbers |
| `BrewController.cpp:371` | Sample plausibility gates (range and slew) | Idea and numbers |
| `BrewController.cpp:901` | Never pulse an unconfirmed start (from OBBW's momentary-machine inference) | Idea |
| `BrewController.cpp:1275` | Lifted-cup check on the settled weight (`plausibleSettledBrewWeight`, 2 g) | Idea and number |
| `BrewController.cpp:1322` | Only a normal weight-target cut trains the offset (`ShotStopperBbwFinalize.inc`) | Idea |
| `BrewController.cpp` (stop at target − offset, drip delay) | `ShotStopperBbwCutoff.h`, `DEFAULT_DRIP_DELAY_MS` | Idea and numbers |
| `src/core/BrewController.h:360` | Adaptive evidence kept in RAM, as OBBW does | Idea |
| `src/core/ScaleLink.h:205` | `FIRST_PACKET_TIMEOUT_MS` 5000 | Number |
| `src/core/Maintenance.h:9, 71` | A history of activations; what counts as a shot (not a rinse or a flush) | Idea |
| `Recipe` presets (up to 16) | `ShotStopperPresets.h` | Idea |
| `src/ble/Ble.cpp:101` | The order in which characteristic pairs are tried (from AcaiaArduinoBLE and OBBW) | Idea; the iOS app rewrites this in Swift anyway |
| `src/ble/Ble.cpp:680` | Scale reconnect back-off (500 ms steps up to 5 s) | Idea and numbers; rewritten in Swift |
| `test/host/test_main.cpp:4236, 4783` | A worked example from OBBW's `brew-by-weight.md`; tests of the selector's earliest switch | Numbers in tests |
| `emu/src/emu_devices.cpp:6` | The Lunar model written from `docs/RESEARCH.md`, which cites AcaiaArduinoBLE and OBBW | Idea |

These need no rewriting, unless a lawyer advises that the constants and rules, taken together, are
protected expression. If the authors grant the permission in the request, they are covered by it
anyway.

## 4. Not from OBBW

- `src/core/AcaiaStream.h` and `AcaiaDriver.h`: this project's own decoder for the Lunar 2021
  stream. It was written from the MIT-licensed `zweckj/aioacaia` capture fixtures because OBBW's
  parser mishandles split frames and the tagged timer (the file's header comment says so). It still
  uses the vendored `kScaleProtocolAcaia` table for UUIDs, name prefixes, init writes and commands.
- `FlowEstimator`, everything else in `src/core`, `src/ble` (apart from the two ideas above),
  `src/ui`, `src/net`, `src/storage`, `src/platform`, `src/addons`, `emu/`, `telemetry-relay/`.
- AcaiaArduinoBLE (Tate Mazer, MIT) is OBBW's own upstream. Its Acaia command bytes are protocol
  facts, also published in MIT form by `pyacaia` and `aioacaia`.

## 5. What the App Store build needs, by route

| Route | Scale protocol layer | Stop maths | Ideas and numbers |
| --- | --- | --- | --- |
| A: the authors grant an app-store permission | Ship as is, with notices | Ship as is | Covered |
| B: no permission | `lib/ScaleWire` (Acaia today; the other families to follow) | `StopMath.h` | Keep, on the lawyer's advice |

Under route B, the eight other scale families are still OBBW's files. Until each has a clean
replacement, an App Store build supports the Acaia scales only (Lunar 2021+, Pyxis, legacy).
That is the MVP's scale anyway.
