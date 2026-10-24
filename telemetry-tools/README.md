# Telemetry tools

Tools for reading the diagnostic reports that controllers send (see `docs/TELEMETRY.md`):

- `fleet.py` gives an overview of every unit's reports;
- `crashes.py` decodes crash addresses against each release's `firmware.elf`.

They need Python 3.9 or later and nothing outside the standard library. `crashes.py` also needs the
GitHub CLI (`gh`) and the ESP32-S3 `addr2line`.

## 1. Get the reports

The reports are in a private repository that only the maintainer can read. The relay writes them
there; nothing in this public repo names it. Clone it once, next to this repo or anywhere outside
it, and pull before each look:

```bash
gh repo clone <owner>/<reports-repo> ../bbw-reports
```

```bash
git -C ../bbw-reports pull --ff-only
```

Never copy reports into this repo: it is public. The files in `fixtures/` are made up.

Layout: `reports/<yyyy>-<mm>/<kid>/<yyyymmddThhmmssZ>-<t>.json`. Each file is an envelope whose
`body` is the report as a JSON string. Reports from emulators are under `emulator/` and are left
out unless you pass `--emulator`.

## 2. Fleet view

```bash
python telemetry-tools/fleet.py ../bbw-reports
```

It prints:
- units: key id, firmware version and date of the newest report, and those silent for more than
  3 days (`--silent-days N` to change);
- hardware seen: scales (driver / name), bots per role, machines, each with a unit count;
- links per role and device (scale driver or bot kind): failures / attempts and the longest wait,
  plus scale frame counts;
- shots per machine: end reasons, faults, stop retries, stop latency and overshoot (the median and
  range of the per-report medians, and the worst single value);
- power press outcomes, overall and per machine;
- warning patterns, summed across units, most frequent first, with how many units show each;
- crashes, grouped by `dump.pc` plus the first 3 backtrace addresses, with count, units and
  versions. Crashes with no core dump are grouped by their reset reason.

Options:
- `--since YYYY-MM-DD`: only reports built on or after that UTC date;
- `--json`: the same data as JSON (crash groups without their log lines);
- `--now YYYY-MM-DD`: treat that date as today when deciding who is silent.

Files that cannot be read are listed on stderr and skipped.

## 3. Decode crashes

`crashes.py` needs each crashing version's `firmware.elf`. Releases attach it from the release
workflow onward; older releases do not have one, and their addresses are printed undecoded.

```bash
python telemetry-tools/crashes.py ../bbw-reports --repo hallge/Brew-Controller
```

For each crash group and version it:
1. downloads that release's ELF once, with `gh release download <fw> -p firmware.elf`, into
   `telemetry-tools/.elf/<fw>/` (ignored by git);
2. runs `xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf` on `pc` and the newest crash's
   backtrace, printing the function and file:line for each address (inlined callers are shown as
   `(inlined by)`);
3. prints the last 20 log lines of the group's newest crash (`--tail N` to change).

`addr2line` is looked for in `~/.platformio/packages/toolchain-xtensa-esp32s3/bin`, then on `PATH`.
Pass `--addr2line <path>` to use another one. `--since` and `--emulator` work as in `fleet.py`.
Without `--repo`, `gh` uses the repo of the folder you run it from.

## Tests

```bash
cd telemetry-tools && python -m unittest -v
```

The tests run both tools on `fixtures/` (two made-up units on different hardware, one of them
silent, with two crashes sharing a signature and one watchdog reset without a dump) and decode
addresses with a fake `addr2line` script.
