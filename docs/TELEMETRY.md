# Diagnostics from controllers we cannot reach

Controllers in other homes, with other scales, pushers and machines, send a small technical report
once a day, and one after a crash. The reports go through a relay to a private repository that only
the maintainer reads. Problems found there are fixed here and shipped as a GitHub release, which the
owners install from Setup › Firmware.

- **On by default.** Setup › Diagnostics says what is sent, shows the exact next report, and turns
  it off. Off means nothing is built or sent.
- **No GitHub account or token on the controller.** The relay holds the only credential, a GitHub
  App allowed to write to the reports repository and nothing else.

## What is sent, and what never is

**Sent:** firmware version and resets, which scale, pusher and machine type is in use, how their
Bluetooth links behave, how shots and power presses end, memory and Wi-Fi strength, and warnings
from the log with names and addresses taken out.

**Never sent:**
- Bluetooth addresses (replaced by the role: `<scale>`, `<brew bot>`, `<power bot>`,
  `<stop bot>`, or `<mac>`);
- IP addresses, the Wi-Fi name, passwords, tokens;
- owner, drinker, coffee or roaster names, notes, subscriptions;
- location.

The controller is known only by its key id: the first 80 bits of the SHA-256 of a key made on the
board, not its MAC. It is the same key that signs library records.

## The envelope

One JSON object per report, sent as `POST <relay>/report` with `Content-Type: application/json`, at
most 32 KB:

```
{"v":1,"kid":"<20 hex>","alg":"ES256","pub":"<base64 of 65 bytes>","sig":"<base64 of 64 bytes>",
 "body":"<the report, a JSON object as a string>"}
```

| Field | What it is |
| --- | --- |
| `pub` | The P-256 public key, uncompressed (`04 \| X \| Y`). |
| `kid` | The lowercase hex of the first 10 bytes of SHA-256(`pub` bytes). |
| `sig` | ECDSA P-256 over SHA-256 of the `body` string's UTF-8 bytes, as `r \| s`, 32 bytes each, big endian (WebCrypto's `ECDSA` / `SHA-256` format). |
| `alg` | `ES256`. An emulator on a PC without OpenSSL sends `EMULATOR-UNSIGNED`, with an empty `sig`. |

## The report bodies

Every body has:
- `t`: the kind, `diag_daily` or `diag_crash`;
- `v`: 1;
- `emu`: 1 when the PC emulator made it (its shots are simulated), absent from a board;
- `fw`: the firmware version, e.g. `v0.1.20`;
- `at`: UTC seconds when built.

Unknown fields are kept; readers ignore what they do not know.

### `diag_daily`

```
{"t":"diag_daily","v":1,"fw":"v0.1.20","at":1792806690,"since":1792720290,
 "boot":{"count":58,"uptime_s":27418,"slot":"app1","resets":["restart requested","CRASH (exception)"]},
 "hw":{"scale":{"driver":"acaia","name":"LUNAR"},
       "bots":[{"role":"brew","kind":"SwitchBot Bot"},{"role":"power","kind":"SwitchBot Bot"}],
       "machine":{"key":"breville-dual-boiler","style":"toggle_button"}},
 "links":{"scale":{"attempts":3,"failures":1,"connects":2,"longest_wait_ms":4100},
          "brew":{...},"power":{...},"stop":{...},
          "scale_frames":{"frames":1793,"bad":0,"dropped":0}},
 "shots":{"count":2,"end":{"weight":2},"faults":{},"stop_latency_ms":{"median":3500,"max":3691},
          "overshoot_g":{"median":1.8,"max":1.8},"stop_retried":0},
 "power":{"done":3,"bot_unreachable":0,"machine_busy":1,"failed":0},
 "health":{"heap_lowest":42908,"stack_left":{"loop":8068,"control":5364,"ble":5548},
           "rssi":{"min":-68,"max":-61}},
 "settings":{"scale_rest_min":5,"require_cup":true,"power_with_scale":false,"learning_factor":0.3},
 "warnings":[{"n":7,"p":"<power bot>: connect failed after # ms: timed out (code #); last heard # s before, # dBm"}],
 "recent":["[65179.623] W <brew bot>: no supported pusher service/characteristics usable"]}
```

Notes on the fields:
- `since` is when the period covered began: the previous report, or the first start.
- `links` holds the counts since the last report.
- `warnings` holds each distinct `W` or `E` line once, with its numbers turned into `#` and its
  count. There are at most 32, the most frequent first.
- `recent` holds the last 30 such lines, cleaned the same way and cut at 160 characters.

### `diag_crash`

Built at the first start after a panic or watchdog reset.

```
{"t":"diag_crash","v":1,"fw":"v0.1.19","at":1792806700,"reset":"CRASH (exception)","boot_count":58,
 "dump":{"task":"loopTask","pc":"0x42012345","cause":"LoadProhibited (28)","vaddr":"0x00000000",
         "elf":"1a2b3c4d5e6f7081","bt":["0x42012345","0x4200abcd"],
         "corrupted":false},
 "log":["[  48.768] I update: up to date: v0.1.19 is the latest release", "..."]}
```

- `elf` is the start of the crashed build's ELF SHA-256 (the core dump's `app_elf_sha256`).
- `dump` is the core dump's summary (`esp_core_dump_get_summary`). It is absent when there is none.
- Addresses are decoded against that version's `firmware.elf`, which every release carries.
- `log` holds the last 60 lines written before the reset, cleaned as above.
- A core dump still in flash at a normal start (a crash the start after it did not report, such as
  one under an older firmware followed by an update) is reported too: `reset` then says "an
  earlier crash, found at start-up", `fw` is the firmware that ran before this start (or
  "unknown"), and there is no `log`. Its `elf` names the build exactly.

## The relay

`telemetry-relay/` is a Cloudflare Worker, `POST /report`. It:
1. refuses more than 32 KB, a body that is not JSON, or an envelope or `t` it does not know;
2. checks that `kid` is the hash of `pub` and that `sig` verifies, and refuses a body still holding
   a MAC (`xx:xx:xx:xx:xx:xx`) or an IPv4 address;
3. counts per key per UTC day: at most 4 `diag_daily` and 10 `diag_crash`;
4. commits it to the private repository, at
   `reports/<yyyy>-<mm>/<kid>/<yyyymmddThhmmssZ>-<t>.json`, as the GitHub App, through the
   contents API. A report whose body has `"emu":1`, or whose `alg` is `EMULATOR-UNSIGNED` (no
   signature to check), goes to `emulator/<yyyy>-<mm>/...` instead, and nowhere else;
5. answers `201 {"ok":true}`, or `4xx {"ok":false,"error":"..."}`. A `429` means try tomorrow.

## The controller's side

- Reports are built when due and kept in `/tm/` on the controller's flash: at most 20, the oldest
  dropped first.
- One is sent at a time, only when the controller is idle on the home Wi-Fi, has its clock set, and
  no shot is running or ended in the last 30 seconds.
- After a failure it waits 1 minute, doubling up to an hour.
- `GET /api/diagnostics/preview` shows the next report.
- `POST /api/diagnostics/send` builds and sends one now.
- `GET /api/crash` shows the last crash summary.
