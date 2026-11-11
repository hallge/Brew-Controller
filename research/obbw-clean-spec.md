# Specification for the clean replacements

Dated 2026-11-10. This file is the only input the implementer of `src/core/StopMath.h` and
`lib/ScaleWire/` works from. It states behaviour, interfaces and protocol facts: byte values,
UUIDs, limits and formulas. It doesn't contain OBBW's code or wording.

How it was made (keep this record):

- **The spec writer** (the coordinating Claude session) read the OBBW-derived files and wrote down
  only what they do.
- **The implementer** (a separate agent) was told not to open `lib/EspressoScaleProtocol/` or
  `src/core/BbwAlgorithms.h`, and wrote the code from this file and the project's own code (the
  callers, the tests, `docs/RESEARCH.md`).
- **The tests** check that old and new behave the same.

This is a two-party clean-room process in the usual sense, but both parties are AI agents working
for the same owner. A lawyer decides whether that is enough (⚖). Who saw what, for every party, is
in `research/clean-room-record.md`.

The Acaia byte values are protocol facts. They are also published under MIT by AcaiaArduinoBLE
(Tate Mazer), `pyacaia` and `aioacaia`, and in `docs/RESEARCH.md` §1.

## 1. Build switch

- `-DBBW_CLEAN` selects the clean code. Without it, nothing changes.
- With it:
  - `src/core/BbwAlgorithms.h` takes its brew maths from `StopMath.h`, through aliases, so
    `BrewController` and the tests compile unchanged.
  - The include path holds `lib/ScaleWire/src` instead of `lib/EspressoScaleProtocol/src`.
  - `ScaleWire`'s `Common.cpp` and `Acaia.cpp` replace OBBW's `ScaleProtocolCommon.cpp` and
    `Acaia.cpp`.
- **The other eight families** (Felicita, Eclair, Decent, DiFluid, MyScale, WeighMyBru, Varia,
  Eureka) still compile from `lib/EspressoScaleProtocol/src` in this mode. They reach their own
  headers through their directory, and the helper functions defined in `ScaleWire/src/Common.cpp`
  through the linker. The struct layouts and function signatures below must therefore match
  exactly. This is transitional: `test/host/run.sh` takes each family from `lib/ScaleWire/src`
  as soon as its file exists there (§6), and from `lib/EspressoScaleProtocol/src` until then.

## 2. `src/core/StopMath.h` (namespace `bbw`, header-only, C++17, no STL containers)

It includes `Types.h` for `MAX_OFFSET_G` (10 g).

### 2.1 `float limitOffset(float g)`

Clamp to `[0, MAX_OFFSET_G]`.

### 2.2 `float predictCrossingS(const float *t, const float *w, size_t n, float targetG, float fallbackS)`

Predicts the time at which the cup weight reaches `targetG`, from a straight line fitted to the
latest samples.

- **Window:** the last 10 samples (`t[n-10..n-1]`, seconds since the shot started; `w`, grams).
- **Return `fallbackS` when any of these holds:**
  - either pointer is null, or `n < 10`;
  - `targetG` or `fallbackS` isn't finite;
  - the newest weight `w[n-1]` is below 10 g;
  - any time or weight in the window isn't finite, or the times in the window aren't strictly
    increasing;
  - the sum of squared time deviations from their mean is not finite or is below `1e-7` (s²);
  - the fitted slope (g/s) is not finite or is ≤ 0;
  - the predicted crossing time is not finite, or is earlier than `t[n-1] + 0.25` s.
- **Fit:** ordinary least squares of weight on time.
  - Compute in `float`, with times taken relative to the newest sample (`t[i] - t[n-1]`), so that
    large shot times don't lose precision.
  - The crossing is the time at which the fitted line equals `targetG`, converted back to absolute
    shot time.
- Results must agree with the existing implementation within 1e-3 s, or 1e-5 relative, whichever
  is larger. Where either returns the fallback, both must.

### 2.3 `bool nextOffset(float offsetG, float finalG, float targetG, float alpha, float &out)`

Learns the stop offset between shots.

- `observed = offsetG + (finalG - targetG)` is the offset that would have hit the target exactly.
- **Return false, leaving `out` alone**, when:
  - `alpha` is not within `[0.01, 1.0]` (a NaN alpha fails too);
  - `offsetG` or `observed` isn't finite;
  - `|observed| > MAX_OFFSET_G`.
- Otherwise `out = limitOffset(offsetG + alpha * (finalG - targetG))` and return true.
- It must be bit-identical to the existing function. It is one expression, so compute exactly
  `offsetG + alpha * (finalG - targetG)` in float.

### 2.4 Gains

`constexpr uint8_t kGainChoices[4] = {10, 30, 50, 100}` (hundredths), and
`bool gainValid(uint8_t g)` returns true for 1..100.

### 2.5 `struct AlphaSelector`

Picks the learning factor per recipe. It must be default-constructible, copyable, and resettable by
assigning `AlphaSelector{}`. It is a plain aggregate of fixed arrays and small integers.

Method: `uint8_t observe(float observation, float seed, uint8_t alpha)`. It returns the gain to use
from now on, in hundredths.

**1. Input check.** Ignore the call and return `alpha` when:
- `observation` isn't finite, or `|observation| > MAX_OFFSET_G`;
- `seed` isn't finite;
- `!gainValid(alpha)`.

**2. State.**
- **Five tracks:** the four gains of `kGainChoices`, and track 4 using the *starting gain*. The
  starting gain is the `alpha` of the first accepted call.
- **Starting points:** each track has one, all set to `seed` on the first accepted call.
- **History:** a ring of the last 20 accepted observations.
- **Counters:** a count of held observations (saturates at 20), a cadence counter (mod 5), a
  since-last-switch counter (saturates at 10), the current challenger index, and a win count.

**3. On each accepted call, in this order:**
1. If this is the first accepted call, set every starting point to `seed` and record the starting
   gain.
2. If the ring already holds 20, advance each track's starting point past the observation about to
   be overwritten (the oldest): `p = limitOffset(p + (gain/100.0f) * (oldest - p))`, with that
   track's gain.
3. Store the observation in the ring, replacing the oldest. Increment the held count (to at most
   20) and the since-switch count (to at most 10). Step the cadence (mod 5).
4. If fewer than 20 are held, or the cadence is not 0, return `alpha`.

**4. Scoring.** For each of the five tracks:
- Start at its starting point and replay the 20 observations, oldest to newest.
- For each one: `e = obs - p; score += e*e; p = limitOffset(p + (gain/100.0f) * e)`.
- This is all in `float`, in exactly this order, so scores are bit-identical.
- If any score is not finite, set wins to 0 and return `alpha`.

**5. Incumbent.** The first of the four choice tracks whose gain equals `alpha`. If none does,
track 4.

**6. Best.** The lowest score among the four choice tracks (0–3), starting the comparison from the
incumbent's score. A choice replaces the current best only if its score is strictly lower.
**Tied** means some choice track other than the best has a score exactly equal to the best's.

**7. Decision.**
- If tied, or the best is the incumbent, or since-switch < 10, or not
  `score[best] <= score[incumbent] * 0.90f`: set wins to 0 and return `alpha`.
- Otherwise, if the best is the same as the remembered challenger, add one win. If not, set wins to
  1 and remember it as the challenger.
- If wins < 2, return `alpha`.
- Otherwise set wins to 0 and since-switch to 0, and return `kGainChoices[best]`.

**Consequence:** the earliest switch is the 25th accepted observation. The returned gains must
equal the existing selector's on every sequence.

### 2.6 How `BbwAlgorithms.h` uses it under `BBW_CLEAN`

These existing names are kept for the callers:

```cpp
using AlphaEvidence = AlphaSelector;
constexpr const uint8_t (&ALPHA_CANDIDATES)[4] = kGainChoices;
inline bool validAlpha(uint8_t a) { return gainValid(a); }
inline float clampOffset(float v) { return limitOffset(v); }
inline float predictStopTimeS(...) { return predictCrossingS(...); }
inline bool learnStopOffset(...) { return nextOffset(...); }
```

`TREND_POINT_COUNT` (10), `TREND_MIN_LAST_SAMPLE_G` (10.0f) and `TREND_MIN_HORIZON_S` (0.25f)
are defined in `StopMath.h` under those names.

The coordinating session makes this edit, not the implementer. `FlowEstimator` stays where it is,
in both modes.

## 3. `lib/ScaleWire/src`: the protocol-table interface

The files `ScaleFeatures.h` and `ScaleProtocol.h` are drop-in headers, because the project's code
includes `"ScaleProtocol.h"`. The names, member order and types below are an interface the project's
own code (`src/core/ScaleDriver.h`, `ScaleLink.h`, `AcaiaDriver.h`, `src/ble/Ble.cpp`, the tests)
depends on, so they must match exactly. Write fresh comments in this project's style.

### 3.1 `ScaleFeatures.h`

`enum ScaleFeature` (unscoped, values are bits):

| Name | Bit |
| --- | --- |
| `ScaleFeatureWeight` | 0 |
| `ScaleFeatureTare` | 1 |
| `ScaleFeatureStartTimer` | 2 |
| `ScaleFeatureStopTimer` | 3 |
| `ScaleFeatureResetTimer` | 4 |
| `ScaleFeatureCombinedTareStart` | 5 |
| `ScaleFeatureIndependentBeep` | 6 |
| `ScaleFeatureVolume` | 7 |
| `ScaleFeatureCommandAudibleFeedback` | 8 |
| `ScaleFeatureHeartbeat` | 9 |
| `ScaleFeaturePowerOff` | 10 |
| `ScaleFeatureReinitWhileSilent` | 11 |

`ScaleFeatureReinitWhileSilent` means: no reading yet after the init writes, so send them again
before giving up.

`struct ScaleFeatureSet`, an aggregate with members in this order:
- `uint32_t flags`
- `uint8_t volumeMin`
- `uint8_t volumeMax`
- `uint16_t heartbeatPeriodMs`
- `uint16_t maxPacketSilenceMs`
- `uint16_t minimumCommandIntervalMs`
- `bool has(ScaleFeature f) const`, which tests the bit.

The other declarations in this header:
- `inline ScaleFeatureSet scaleFeatureSetNone()` returns all zeros.
- `enum class ScaleCommandResult : uint8_t { Ok, Unsupported, NotConnected, InvalidArgument,
  WriteFailed, Deferred }`.
- `struct ScaleCommandAdmission { bool (*approve)(void *context, uint32_t &captureBoundary) =
  nullptr; void *context = nullptr; }`.
- `inline bool scaleCommandOk(ScaleCommandResult r)` returns `r == Ok`.
- `enum class ScaleOp : uint8_t { Tare, StartTimer, StopTimer, ResetTimer, CombinedTareStart,
  Heartbeat, SetVolume, PowerOff }`, in that order.
- `enum class ScaleModel : uint8_t { Unknown, BookooMini, BookooUltra }`.

### 3.2 `ScaleProtocol.h`

It includes `ScaleFeatures.h`, `<stddef.h>`, `<stdint.h>` and `<string.h>`. It defines:

- **Limits:** `#define SCALE_MAX_PACKET_LENGTH 20`, `SCALE_MAX_COMMAND_LENGTH 20` and
  `SCALE_MAX_WEIGHT_GRAMS 10000.0f`.
- **`SCALE_CORE_FEATURES`:** Weight | Tare | StartTimer | StopTimer | ResetTimer.
- **`struct ScalePayload { const uint8_t *data; int length; const char *label; }`.**
- **`struct ScaleProtocol`**, members in this order:
  `const char *id; const char *debugLabel; const char *readUuid; const char *writeUuid;
  const char *const *namePrefixes; size_t namePrefixCount; ScaleFeatureSet features;
  bool (*supportedPacketLength)(int length);
  bool (*parseWeight)(const uint8_t *data, int length, float *weight);
  bool (*parseTimer)(const uint8_t *data, int length, uint32_t *timerMs);
  bool (*encodeCommand)(ScaleOp op, uint8_t arg, uint8_t *out, int *length);
  const ScalePayload *initWrites; size_t initWriteCount; bool requireAdvertisedName;`

**Function declarations.** These have external linkage, and the signatures must be exactly as given:

| Function | Defined in | Does |
| --- | --- | --- |
| `const ScaleProtocol *scaleProtocolAt(size_t index)` | `Registry.cpp` (the project's own, unchanged) | The registered table at an index |
| `size_t scaleProtocolCount()` | `Registry.cpp` | How many tables are registered |
| `bool scaleNameIsCompatible(const char *name)` | `Registry.cpp` | A registered table matches this name |
| `bool scaleNameMatchesProtocol(const char *name, const ScaleProtocol *p)` | `Common.cpp` | False for a null/empty name, null p, or null prefix list. True if the name starts with any non-null, non-empty prefix (case-sensitive) |
| `bool scaleParseUuid16(const char *uuid, uint16_t *out)` | `Common.cpp` | Exactly four hex digits (either case), then the end of the string. Otherwise false |
| `bool scaleUuid16AllowsNamelessConnect(uint16_t uuid)` | `Common.cpp` | False for 0xFFF1 and 0xFFF2. Otherwise true if some registered table that doesn't require an advertised name has this 16-bit UUID as its read or write UUID |
| `bool scaleValidWeight(float w)` | `Common.cpp` | Finite and `|w| <= SCALE_MAX_WEIGHT_GRAMS` |
| `bool scaleCopyPayload(const uint8_t *cmd, int n, uint8_t *out, int *length)` | `Common.cpp` | False if out/length null, n ≤ 0 or n > `SCALE_MAX_COMMAND_LENGTH`. Otherwise copies n bytes and sets `*length = n` |
| `uint8_t scaleXorBytes(const uint8_t *d, int n)` | `Common.cpp` | XOR of the n bytes; 0 for none |
| `uint32_t scaleReadUint32LittleEndian(const uint8_t *d)` | `Common.cpp` | Four bytes, little-endian |
| `float scaleDecimalDivisor(uint8_t e)` | `Common.cpp` | 10^e for e in 0..4, else 1.0f |
| `bool scaleLooksLikeAcaiaTimer(uint8_t m, uint8_t s, uint8_t t)` | `Common.cpp` | m ≤ 99, s ≤ 59, t ≤ 9 |
| `uint32_t scaleAcaiaTimerToMs(uint8_t m, uint8_t s, uint8_t t)` | `Common.cpp` | (m·60 + s)·1000 + t·100 |
| `bool scaleValidAcaiaChecksum(const uint8_t *d, int n)` | `Common.cpp` | See below |
| `bool scaleFelicitaAsciiTimer(const uint8_t *d, uint32_t *ms)` | `Common.cpp` | See below |

**`scaleValidAcaiaChecksum`:**
- False if n < 6.
- The payload is bytes 3 to n−3 inclusive.
- Sum, modulo 256, the payload bytes at even offsets from byte 3 (offsets 0, 2, 4…) and,
  separately, those at odd offsets.
- Valid when the even sum equals `d[n-2]` and the odd sum equals `d[n-1]`.

**`scaleFelicitaAsciiTimer`:**
- Bytes 9..13 must all be ASCII digits; otherwise false.
- Minutes are digits 9–10, seconds 11–12, tenths 13.
- They must pass `scaleLooksLikeAcaiaTimer`.
- `*ms = scaleAcaiaTimerToMs(...)`, return true.

**Table declarations:**
- `extern const ScaleProtocol` for `kScaleProtocolAcaiaLegacy`, `kScaleProtocolAcaia`,
  `kScaleProtocolGenericFf11`, `kScaleProtocolFelicita`, `kScaleProtocolEclair`,
  `kScaleProtocolDecent`, `kScaleProtocolDifluid`, `kScaleProtocolMyscale`,
  `kScaleProtocolWeighMyBru`, `kScaleProtocolVaria` and `kScaleProtocolEureka`.
- `kScaleProtocolGenericFf11` is declared but never defined or used. Leave it out unless something
  fails to compile.
- The Bookoo helpers (`scaleModelForAdvertisement`, `scaleDefaultFriendlyName`, `scaleModelName`,
  `scaleBookooOpcode`, `scaleBookooCommandAt`, `ScaleCommandInfo`, `scaleFeaturesForModel`) are not
  used by this project. Leave them out. (`bbw::scaleModelName` in `Telemetry.h` is the project's
  own, a different function.)

## 4. `lib/ScaleWire/src/Acaia.cpp`: Acaia facts

**Name prefixes** (shared by both tables): `CINCO`, `ACAIA`, `PYXIS`, `LUNAR`, `PEARL`, `PROCH`.

**Commands** (the same for both tables; `arg` is ignored):

| Op | Bytes |
| --- | --- |
| Tare | `ef dd 04 00 00 00` |
| StartTimer | `ef dd 0d 00 00 00 00` |
| StopTimer | `ef dd 0d 00 02 00 02` |
| ResetTimer | `ef dd 0d 00 01 00 01` |
| Heartbeat | `ef dd 00 02 00 02 00` |
| Anything else | not supported (return false) |

Copy them out with `scaleCopyPayload`.

**Init writes, in order** (labels as given; the project logs them):
1. `"identify"`: `ef dd 0b 30 31 32 33 34 35 36 37 38 39 30 31 32 33 34 9a 6d` (20 bytes).
2. `"notification_request"`: `ef dd 0c 09 00 01 01 02 02 05 03 04 15 06` (14 bytes).

**Features (both tables):**
- flags = `SCALE_CORE_FEATURES` | Heartbeat | CommandAudibleFeedback | ReinitWhileSilent;
- volumeMin 0, volumeMax 0;
- heartbeatPeriodMs 2750, maxPacketSilenceMs 5000, minimumCommandIntervalMs 0.

`requireAdvertisedName` is false for both.

**Legacy table** `kScaleProtocolAcaiaLegacy`:
- id `"acaia_legacy"`, debugLabel `"Old version Acaia detected"`;
- read and write UUID `"2a80"`.
- **Supported packet lengths:** 10 and 14.
- **Weight:**
  - The length must be 10 or 14, and the exponent byte `d[6]` must be 1..4.
  - For length 14 the Acaia checksum must be valid.
  - raw = `d[2] | d[3] << 8`, and weight = raw / `scaleDecimalDivisor(d[6])`.
  - The weight is negated when `d[7] & 0x02`.
  - Valid only if `scaleValidWeight`.
- **Timer:** never (always false).

**Current table** `kScaleProtocolAcaia`:
- id `"acaia"`, debugLabel `"New version Acaia detected"`;
- read UUID `"49535343-1e4d-4bd9-ba61-23c647249616"`, write UUID
  `"49535343-8841-43f4-a8d4-ecbe34729bb3"`.
- **Supported packet lengths:** 10, 13 and 17.
- **Header rule**, used by both parsers: `d[0]==0xef`, `d[1]==0xdd`, `d[2]==0x0c`,
  `d[3] + 5 == length`, and the Acaia checksum is valid.
- **Weight:**
  - The length must be 13 or 17, the header rule must hold, `d[4] == 0x05`, and the exponent
    `d[9]` must be 1..4.
  - raw = `d[5] | d[6] << 8`, and weight = raw / `scaleDecimalDivisor(d[9])`.
  - The weight is negated when `d[10] & 0x02`.
  - Valid only if `scaleValidWeight`.
- **Timer:**
  - The length must be 10, 13 or 17, and the header rule must hold.
  - If `d[4] == 0x07` and bytes 5, 6, 7 pass `scaleLooksLikeAcaiaTimer`, the time is those as
    minutes, seconds and tenths.
  - Otherwise, if `d[4] == 0x05`, the length is 17, and bytes 11, 12, 13 pass, it is those.
  - Otherwise false.
- On a false return, a parser need not leave its output untouched (callers only read it after
  true). Writing nothing is preferred.

Internal helpers go in an unnamed namespace. Only the two tables have external linkage.

## 5. Tests the coordinator adds (`test/host/test_clean.inc`)

- **StopMath against the existing maths**, both compiled in the same test binary. This only works
  in the default build, where `BbwAlgorithms.h` holds the original:
  - random and edge-case series through `predictCrossingS` and `predictStopTimeS`;
  - `nextOffset` and `learnStopOffset` across alphas and offsets;
  - `AlphaSelector` and the original `AlphaEvidence` over thousands of random sequences and
    starting gains (decisions identical).
- **The Acaia tables, by digest:**
  - Deterministic random frames go through both tables' `supportedPacketLength`, `parseWeight`
    and `parseTimer`, along with every `encodeCommand` op and the init writes, the UUIDs,
    prefixes and features. They are mostly valid frames, with checksums made correct, and
    damaged ones mixed in.
  - All outputs are folded into a 64-bit FNV-1a digest.
  - The digest is checked against a constant recorded from the default build, and the
    `BBW_CLEAN` build must produce the same constant.
- **The helpers, by digest:** every function in §3.2 over fixed and random inputs, in the same way.
- **The whole existing suite runs in both modes.**

## 6. The other eight families (added 2026-11-10)

One file per family in `lib/ScaleWire/src`: `Felicita.cpp`, `Eclair.cpp`, `Decent.cpp`,
`Difluid.cpp`, `Myscale.cpp`, `WeighMyBru.cpp`, `Varia.cpp`, `Eureka.cpp`. Each:

- includes `"ScaleProtocol.h"` and defines one `const ScaleProtocol` table with external linkage,
  named as in §3.2;
- keeps its helpers and byte arrays in an unnamed namespace;
- copies command bytes out with `scaleCopyPayload`, ignores `arg`, and returns false for any op not
  listed.

### 6.1 Rules shared by all eight

- **Field order** is the `ScaleProtocol` member order in §3.2. "No timer" means `parseTimer` is
  `nullptr`. "No init writes" means `initWrites` is `nullptr` and the count is 0.
- **Arithmetic.** Each weight is made exactly as stated: an integer value, converted to `float`,
  then divided or multiplied by a `float` constant (the spec says which). Dividing by `100.0f` and
  multiplying by `0.01f` give different bits, so follow the word used. A negative sign is applied
  after the scaling, by negating the float, unless the spec says the integer is signed.
- **Validity.** Every weight parser returns `scaleValidWeight(weight)` as its last step. As with
  Acaia, a parser need not leave its output alone on a false return.
- **XOR "over bytes a..b"** means `scaleXorBytes` over those bytes, both ends included.
- Parsers are called with the notification's bytes and length (at most 20). They must not read
  beyond `length` bytes, except at the fixed indices this spec gives for a fixed length.

### 6.2 Felicita (`kScaleProtocolFelicita`)

| Field | Value |
| --- | --- |
| id, debugLabel | `"felicita"`, `"Felicita Arc detected"` |
| read, write UUID | `"ffe1"`, `"ffe1"` |
| prefixes | `"FELIC"` |
| features | `SCALE_CORE_FEATURES` \| CommandAudibleFeedback; volume 0/0; heartbeat 0; silence 5000; interval 0 |
| init writes | one: label `"weight_timer_mode"`, bytes `32` |
| requireAdvertisedName | false |

- **Supported length:** 18 only.
- **Commands:** Tare `54`, StartTimer `52`, StopTimer `53`, ResetTimer `43` (one byte each).
- **Weight:**
  - The length must be 18. Byte 2 is the sign: `'-'`, `'+'`, `' '` or `0x00`; anything else fails.
  - Bytes 3..8 must all be ASCII digits. Read them as a six-digit decimal integer of hundredths.
  - weight = (that integer, as float) / `100.0f`, negated when byte 2 is `'-'`.
- **Timer:**
  - It fails unless the weight parse of the same frame succeeds (validity check included).
  - Then, if `scaleFelicitaAsciiTimer` succeeds, that is the time.
  - Otherwise, if bytes 9, 10, 11 pass `scaleLooksLikeAcaiaTimer`, the time is
    `scaleAcaiaTimerToMs` of them.
  - Otherwise false.

### 6.3 AtomHeart Eclair (`kScaleProtocolEclair`)

| Field | Value |
| --- | --- |
| id, debugLabel | `"atomheart_eclair"`, `"AtomHeart Eclair detected"` |
| read UUID | `"AD736C5F-BBC9-1F96-D304-CB5D5F41E160"` |
| write UUID | `"4F9A45BA-8E1B-4E07-E157-0814D393B968"` |
| prefixes | `"ECLAI"` |
| features | `SCALE_CORE_FEATURES`; volume 0/0; heartbeat 0; silence 5000; interval 0 |
| init writes | none |
| requireAdvertisedName | false |

- **Supported length:** 10 only.
- **Commands:** Tare `54 01 01`, StartTimer `53 01 01`, StopTimer `45 01 01`, ResetTimer `52 01 01`.
- **Weight:** the length must be 10, byte 0 must be `'W'` (0x57), and the XOR over bytes 1..8 must
  equal byte 9. Bytes 1..4 are a little-endian two's-complement 32-bit integer of milligrams.
  weight = (that integer, as float) / `1000.0f`.
- **Timer:** fails unless the weight parse succeeds. Then the time in ms is bytes 5..8 read
  little-endian, unsigned (`scaleReadUint32LittleEndian`), and the result is true.

### 6.4 Decent (`kScaleProtocolDecent`)

| Field | Value |
| --- | --- |
| id, debugLabel | `"decent"`, `"Decent Scale detected"` |
| read, write UUID | `"fff4"`, `"36f5"` |
| prefixes | `"Decent Scale"`, `"EspressiScale"` |
| features | Weight \| Tare \| Heartbeat; volume 0/0; heartbeat 5000; silence 5000; interval 0 |
| init writes | none |
| requireAdvertisedName | false |
| timer | none |

- **Supported length:** 7 or 10.
- **Commands:** Tare `03 0f 00 00 00 01 0d`, Heartbeat `03 0a 03 ff ff 00 0a`.
- **Weight:**
  - The length must be 7 or 10, byte 0 must be `03`, and byte 1 must be `ca` or `ce`.
  - The last byte is a check byte. When it is not zero, the XOR over bytes 0..length−2 must
    equal it. A zero check byte is accepted without checking.
  - Bytes 2, 3 are a big-endian two's-complement 16-bit integer of tenths.
    weight = (that integer, as float) / `10.0f`.

### 6.5 DiFluid Microbalance (`kScaleProtocolDifluid`)

| Field | Value |
| --- | --- |
| id, debugLabel | `"difluid"`, `"DiFluid Microbalance detected"` |
| read, write UUID | `"aa01"`, `"aa01"` |
| prefixes | `"Microbalance"` |
| features | Weight \| Tare \| Heartbeat; volume 0/0; heartbeat 2000; silence 5000; interval 0 |
| init writes | two, in order: `"unit_gram"` `df df 01 04 01 00 c4`; `"auto_notify"` `df df 01 00 01 01 c1` |
| requireAdvertisedName | false |
| timer | none |

- **Supported length:** 19 or 20.
- **Commands:** Tare `df df 03 02 01 01 c5`, Heartbeat `df df 03 05 00 c6`.
- **Weight** (checks in this order; any failure returns false):
  - the length is at least 19, and bytes 0 and 1 are both `df`;
  - the sum of bytes 0..length−2, modulo 256, equals the last byte;
  - byte 2 is `03`, byte 3 is `00`, byte 4 (a data length) is at least 13, and the frame length
    is at least 6 + byte 4;
  - bytes 5..8 are a big-endian two's-complement 32-bit integer of tenths.
    weight = (that integer, as float) / `10.0f`.

### 6.6 MyScale (`kScaleProtocolMyscale`)

| Field | Value |
| --- | --- |
| id, debugLabel | `"myscale"`, `"MyScale detected"` |
| read, write UUID | `"ffb2"`, `"ffb1"` |
| prefixes | `"blackcoffee"`, `"my_scale"`, `"MY_SCALE"` |
| features | Weight \| Tare; volume 0/0; heartbeat 0; silence 5000; interval 0 |
| init writes | none |
| requireAdvertisedName | false |
| timer | none |

- **Supported length:** 15 to 20 (`SCALE_MAX_PACKET_LENGTH`) inclusive.
- **Commands:** Tare only, 20 bytes: `ac 40`, then 16 × `00`, then `d2 d2`.
- **Weight:** the length must be at least 15. There is no header or check byte.
  - The sign is the high nibble of byte 2: negative when it is `8` or `c`.
  - The value is unsigned: the low nibble of byte 3, then bytes 4, 5, 6 (a 28-bit big-endian
    integer of milligrams).
  - weight = (value, as float) / `1000.0f`, negated when the sign says so.

### 6.7 WeighMyBru (`kScaleProtocolWeighMyBru`)

| Field | Value |
| --- | --- |
| id, debugLabel | `"weighmybru"`, `"WeighMyBru detected"` |
| read UUID | `"6E400002-B5A3-F393-E0A9-E50E24DCCA9E"` |
| write UUID | `"6E400003-B5A3-F393-E0A9-E50E24DCCA9E"` |
| prefixes | `"WeighMyBru"` |
| features | Weight \| Tare; volume 0/0; heartbeat 0; silence 8000; interval 0 |
| init writes | none |
| requireAdvertisedName | false |
| timer | none |

The read and write UUIDs are as given, though they look swapped against the usual Nordic UART
naming. Keep them.

- **Supported length:** 20 only.
- **Commands:** Tare `03 0a 01 01 00 09`.
- **Weight:** the length must be 20, byte 0 `03`, byte 1 `0b`, and the XOR over bytes 0..18 must
  equal byte 19. Bytes 7, 8, 9 are an unsigned 24-bit big-endian integer of hundredths.
  weight = (that integer, as float) / `100.0f`, negated when byte 6 is `'-'`.

### 6.8 Varia AKU (`kScaleProtocolVaria`)

| Field | Value |
| --- | --- |
| id, debugLabel | `"varia"`, `"Varia AKU detected"` |
| read, write UUID | `"fff1"`, `"fff2"` |
| prefixes | `"AKU MINI SCALE"`, `"VARIA AKU"`, `"Varia AKU"`, `"AKU SCALE"` |
| features | Weight \| Tare \| StartTimer \| StopTimer \| ResetTimer; volume 0/0; heartbeat 0; silence 5000; interval 0 |
| init writes | none |
| requireAdvertisedName | **true** |

- **Supported length:** 6 or 7.
- **Commands:** Tare `fa 82 01 01 82`, StartTimer `fa 88 01 01 88`, StopTimer `fa 89 01 02 8a`,
  ResetTimer `fa 8a 01 03 88`.
- **Check rule:** the length is at least 3, and the XOR over bytes 1..length−2 equals the last byte.
- **Weight:**
  - The length must be 7, byte 0 `fa`, byte 1 `01`, and the check rule must hold.
  - The sign is bit 4 (0x10) of byte 3: set means negative.
  - The value is the low nibble of byte 3, then bytes 4, 5 (a 20-bit big-endian integer of
    hundredths).
  - Make the signed integer first (value, or −value), convert it to float, and **multiply** by
    `0.01f`.
- **Timer:** the length must be 6, byte 0 `fa`, byte 1 `87`, and the check rule must hold.
  Bytes 3, 4 are a big-endian unsigned 16-bit count of seconds. ms = seconds × 1000. Return true.

### 6.9 Eureka Precisa (`kScaleProtocolEureka`)

| Field | Value |
| --- | --- |
| id, debugLabel | `"eureka"`, `"Eureka Precisa detected"` |
| read, write UUID | `"fff1"`, `"fff2"` |
| prefixes | `"CFS-9002"`, `"LSJ-001"` |
| features | Weight \| Tare \| StartTimer \| StopTimer \| ResetTimer; volume 0/0; heartbeat 0; silence 5000; interval 0 |
| init writes | none |
| requireAdvertisedName | **true** |
| timer | none |

- **Supported length:** 11 only.
- **Commands:** Tare `aa 02 31 31 00 00`, StartTimer `aa 02 33 33 00 00`, StopTimer
  `aa 02 34 34 00 00`, ResetTimer `aa 02 35 35 00 00`.
- **Weight:** the length must be 11, bytes 0..2 `aa 09 41`, and byte 6 (the sign) 0 or 1; any
  other sign byte fails. Bytes 7, 8 are an unsigned 16-bit **little-endian** integer of tenths.
  weight = (that integer, as float) × `0.1f` (multiply), negated when byte 6 is 1.

### 6.10 Worked examples (test vectors)

Bytes not shown are `00`. Each must give exactly the result shown.

| Family | Length | Bytes | Weight | Timer |
| --- | --- | --- | --- | --- |
| Felicita | 18 | `01 02 2b 30 30 33 36 32 35 30 30 32 35 31` | 36.25 | 25100 ms |
| Felicita | 18 | `01 02 41 30 30 33 36 32 35` | false (sign byte) | false |
| Eclair | 10 | `57 9a 8d 00 00 a8 61 00 00 de` | 36.25 | 25000 ms |
| Eclair | 10 | `57 9a 8d 00 00 a8 61 00 00 df` | false (XOR) | false |
| Decent | 7 | `03 ca 01 6b 00 00 a3` | 36.3 | none |
| Decent | 7 | `03 ce ff f6 00 00 00` | −1.0 (check byte 0, so not checked) | none |
| DiFluid | 19 | `df df 03 00 0d 00 00 01 6b`, then 9 × `00`, then `3a` | 36.3 | none |
| MyScale | 15 | `00 00 80 00 00 8d 9a` | −36.25 | none |
| WeighMyBru | 20 | `03 0b 00 00 00 00 2b 00 0e 29`, then 9 × `00`, then `04` | 36.25 | none |
| Varia | 7 | `fa 01 03 00 0e 29 25` | 36.25 (3625 × 0.01f) | false |
| Varia | 6 | `fa 87 02 00 19 9c` | false | 25000 ms |
| Eureka | 11 | `aa 09 41 00 00 00 00 a3 01 00 00` | 41.9 (419 × 0.1f) | none |
| Eureka | 11 | `aa 09 41 00 00 00 02 a3 01 00 00` | false (sign byte) | none |

### 6.11 Registry

`lib/ScaleWire/src/Registry.cpp` is the project's own `Registry.cpp`, moved by the coordinator
(not the implementer), so that a clean build uses nothing from `lib/EspressoScaleProtocol`. It
lists the ten tables in this order: AcaiaLegacy, Acaia, Felicita, Eclair, Decent, Difluid, Myscale,
WeighMyBru, Varia, Eureka.

### 6.12 Tests

`test/host/test_clean.inc` (`familyDigests`) runs each family table over 30,000 frames shaped like
that family's, mostly valid and some damaged. It folds every field, command and parser output into
a 64-bit FNV-1a digest, and checks it against the digest recorded from the default build.
`BBW_CLEAN=1 test/host/run.sh` must print the same digests. The whole suite must pass in both
builds, with `testShotRecordGolden` and `testScaleLinkKeepsLunarData` included.
