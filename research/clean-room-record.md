# Clean-room record: who saw what

Started 2026-11-10. **Not legal advice.** This is the record a lawyer would ask for (⚖). It covers
every party that wrote, specified or tested the clean replacements for Open Brew by Weight's
(OBBW's) code. The owner's ruling (2026-11-10) is that every OBBW-derived part is redeveloped this
way:

- A **spec author**, who may read OBBW's code, writes only operating requirements and test vectors.
- An **implementer**, who is never given OBBW's code or its paths, builds from the spec.
- The record below says, for each party, what it read.

The parts and their status are in `research/obbw-provenance.md`. The spec is
`research/obbw-clean-spec.md`.

## Rules

1. An implementer gets the spec, the clean interface headers and the test vectors. Nothing else
   from OBBW, and no paths to OBBW's files.
2. An implementer is a fresh agent with no earlier conversation. Its whole tool log is kept, and
   the coordinator scans it afterwards for any read of OBBW's files, OBBW's names in paths, or web
   fetches. The result goes in the table below.
3. A party that has read OBBW's code never implements a clean part. That covers this session (the
   spec author), the Brew Controller coordinator session, the audit agents, and any session that
   has worked in `lib/EspressoScaleProtocol` or `src/core/BbwAlgorithms.h`.
4. Spec text states behaviour, byte values, UUIDs, limits, formulas and arithmetic order. It
   doesn't copy OBBW's code, comments or identifiers, apart from names that the project's own
   callers already use (the interface in spec §3).
5. Equivalence is shown by tests, never by an implementer reading the original: the same outputs
   bit for bit (FNV-1a digests over generated frames, the StopMath comparisons), with the whole
   host suite passing in both builds, `testShotRecordGolden` and `testScaleLinkKeepsLunarData`
   included.

## Parties

| Party | Role | Read OBBW's code? | What it read |
| --- | --- | --- | --- |
| iOS research session (Opus 5.5; branch `claude/ios-research-9a7ca9`; transcript `73e21acf…`) | Spec author, test author, coordinator of the clean room | **Yes** | All of `lib/EspressoScaleProtocol/src`, `src/core/BbwAlgorithms.h`, and their git history. It fetched no OBBW sources from the web (its transcript has no such fetch). It wrote `obbw-clean-spec.md` §1–6, `test/host/test_clean.inc`, the workspace vectors, `lib/ScaleWire/src/Registry.cpp` (the project's own file, moved) and the build wiring. It wrote no clean implementation code. |
| Implementer 1 (subagent of the session above, Opus 5.5, fresh context; 2026-11-10) | Wrote `src/core/StopMath.h` and `lib/ScaleWire/src/{ScaleFeatures.h, ScaleProtocol.h, Common.cpp, Acaia.cpp}`, `lib/ScaleWire/README.md` | **No** (log checked) | It read the spec (`research/obbw-clean-spec.md` §1–5), and of the project's own code `src/core/Types.h`, and grep output over `src`/`test/host/test_main.cpp` (with `BbwAlgorithms` filtered out). It also saw `test/host/run.sh`, which names OBBW's file paths but not their contents, and `ls lib`, which shows the `EspressoScaleProtocol` folder name. No reads of `lib/EspressoScaleProtocol` or `BbwAlgorithms.h`, and no web fetches. |
| Implementer 2 (subagent, Sonnet, fresh context; 2026-11-10) | The eight other families, `lib/ScaleWire/src/{Felicita, Eclair, Decent, Difluid, Myscale, WeighMyBru, Varia, Eureka}.cpp` | **No** (log checked) | It worked in an isolated folder outside the repository (this session's scratchpad), holding only a spec extract (§3 and §6.1–6.10, with no OBBW paths), the clean headers and helpers, `Acaia.cpp` as a style example, and the vectors (`test/vectors.cpp`). Its log shows 21 tool calls: reads and globs inside that folder only (`SPEC.md`, `src/Acaia.cpp`, `src/ScaleProtocol.h`, `test/vectors.cpp`), writes of its eight files there, and builds. The only thing outside the folder that it ran was the compiler (`.venv\Scripts\python.exe -m ziglang`), and it read nothing outside the folder. No web fetches. The coordinator copied the eight files into `lib/ScaleWire/src` unchanged. |
| Tests agents (Opus 5.5) | Ran the host tests and firmware builds | Compiled them, didn't author anything | They run commands only and never implement. |
| Brew Controller coordinator, audit agents, AppHost and iOS-spike sessions | Not part of the clean room | Unknown or yes | Not eligible as implementers (rule 3). |

## Two weaknesses, for the lawyer

- **Same model, same owner.** Both parties are Claude agents working for the same owner. They are
  kept apart by context (a fresh agent with only the brief), not by being different people.
  Implementer 1 also ran in the same repository checkout, so it could have opened OBBW's files.
  Its log shows it didn't. Implementer 2 works outside the repository.
- **Exact behaviour.** The spec gives the arithmetic order needed for bit-identical output
  (for example `nextOffset`, and divide-by-100 against multiply-by-0.01). That is behaviour, and
  it's needed to keep the golden tests identical, but it is close to the code's structure.

## Still OBBW-derived after this

- `src/core/BbwAlgorithms.h` keeps the OBBW adaptation in its default (`#else`) branch, which the
  firmware builds. A `BBW_CLEAN` build doesn't compile it. Once the owner switches the firmware
  to the clean code, the branch can be deleted.
- The ideas and numbers in the project's own code (`obbw-provenance.md` §3) aren't rewritten.
  Rules and constants are generally not protected, so the plan is to keep them unless a lawyer
  says otherwise. The owner can rule differently.
