# Add-ons: optional drivers built into a controller image

An add-on is a driver for a device the public firmware does not support, typically a grinder or a
machine with a network interface of its own. It is not part of this repository and not part of the
public releases. It lives in its own folder (a private repository, a folder on your PC), is compiled into
a firmware image built for you, and stays off until you switch it on.

The public firmware has no add-ons: its add-on table is empty, nothing is allocated for them, and it
behaves exactly as before. Grinders and machines keep working without any add-on, set up as today.

This round has **grinder** add-ons. Machine add-ons are described at the end; nothing is built for them yet.

## 1. How an add-on gets onto a controller

The ESP32-S3 cannot load native code at run time, so add-ons are compiled in. Three ways were weighed:

| | Compiled in (chosen) | Script runtime (Berry, Lua, WASM) on the file system | Off-board bridge (Home Assistant, a PC) |
| --- | --- | --- | --- |
| Flash | a few kB of core, plus the driver | a VM of 250-400 kB, of the ~800 kB left in a 3.5 MB slot | none |
| Chip RAM | objects and buffers in PSRAM | a VM heap and bindings | none |
| What a driver needs (sockets, TLS, binary formats) | native | every binding written and kept up | fine, but needs a box that is always on |
| Security | trust = whoever built the image | code uploaded over an unauthenticated LAN API could press the machine's buttons | a separate box; the same API |
| Private code stays private | yes | yes | yes |

An off-board bridge can still be written as one more add-on, using the same interface and the controller's API.

### Building an image with add-ons

List the add-on folders in either of these:
- the environment variable `BBW_ADDONS` (`;`-separated paths);
- a file `addons.local.txt` at the project root, one path per line. It is gitignored.

Then build as usual:

```
pio run                                           # firmware
python emu/build.py --addons=C:/path/to/my-addon  # emulator (or BBW_ADDONS / addons.local.txt)
```

`scripts/addons.py` reads each folder's `addon.json`, writes `.pio/gen/AddonList.h` (the table the firmware
starts the add-ons from), and adds the folder's `.cpp` files to the build. With nothing listed, the table
is empty.

```json
{"id": "my-grinder", "name": "My grinder", "version": "1.0.0", "api": 1, "kind": "grinder",
 "header": "MyGrinder.h", "factory": "bbw::addon::makeMyGrinder", "routes": ["/api/mygrinder/"]}
```

- `id`: `[a-z0-9-]`, permanent. The add-on's stored settings are kept under it.
- `api`: the `ADDON_API_VERSION` it was built for (`src/addons/Addon.h`). An add-on built for another version is listed but cannot be switched on.
- `header` declares `bbw::addon::Addon *<factory>();`.
- `routes`: at most two path prefixes the add-on may answer (see §3).

### Updates over the air

The controller installs updates from a GitHub repository's latest release (`firmware.bin`, checked against
`SHA256SUMS.txt`), and the repository is a setting (Setup, Firmware: owner/name and a token for a private one).
To keep add-ons across updates, point it at a private repository whose releases are built with them:

1. A private repository holds the add-on folders and a release workflow (below).
2. The workflow checks out this repository at a release tag, builds with `BBW_ADDONS` set to its folders,
   and publishes `firmware.bin` and `SHA256SUMS.txt` under its own tag, for example `v0.1.30+addons.1`.
   The tag differs from the public one, so the controller shows it as an update.
3. On the controller: Setup, Firmware, "Releases from" = the private repository, plus a token that can read it.

The existing checks all apply: the SHA-256 of the download, the image check, the trial start with rollback.
`/api/update` lists the add-ons compiled into the running image (`addons`). The Firmware card warns when a
release from the public repository would remove them. An add-on's settings survive any update, including
one without it, and come back with it.

A workflow sketch for the private repository:

```yaml
on:
  workflow_dispatch: {inputs: {tag: {required: true}}}   # the public release to build on
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with: {repository: hallge/Brew-Controller, ref: "${{ inputs.tag }}", path: fw}
      - uses: actions/checkout@v4
        with: {path: addons}
      - run: pip install platformio
      - run: cd fw && CXX=g++ bash test/host/run.sh
      - run: cd fw && pio run
        env:
          BBW_ADDONS: ${{ github.workspace }}/addons/my-grinder
          PLATFORMIO_BUILD_FLAGS: -DBBW_VERSION='"${{ inputs.tag }}+addons.${{ github.run_number }}"'
      - run: |
          mkdir out && cp fw/.pio/build/*/firmware.bin out/
          (cd out && sha256sum firmware.bin > SHA256SUMS.txt)
          gh release create "${{ inputs.tag }}+addons.${{ github.run_number }}" out/* --title "..." --notes "..."
        env: {GH_TOKEN: "${{ github.token }}"}
```

### Installing, switching on, listing, removing

| | How |
| --- | --- |
| Install | Install an image that has it (over the air as above, or the file upload in Setup, Firmware). |
| Switch on / off | Setup, Add-ons (`POST /api/addons/enable?id=&on=1`). Off by default. No restart. |
| List | Setup, Add-ons (`GET /api/addons`). |
| Configure | Its settings on the card (`POST /api/addons/config`). Secrets are write-only. |
| Remove | Switch it off, or install an image without it. "Forget its settings" (`POST /api/addons/forget`) clears what was stored. |

What is stored: `/addons.json` on the file system (switched on, settings, options, recipe map; included in
backups), and the add-ons' secrets in their own NVS record (`bbwaddon`), which backups leave out.

## 2. The grinder interface (`src/addons/Addon.h`)

An add-on implements `Addon`. A grinder add-on also implements `GrinderDriver`:

| | |
| --- | --- |
| `caps()` | `GRIND_EVENTS` (delivers grinds), `SHOT_FEEDBACK` (wants each shot), `WRITE_SETTING` (reserved: no driver writes a setting yet) |
| `link()` | off, waiting, connected, failed |
| `takeGrind(GrindEvent&)` | the newest grind not yet taken |
| `onShotComplete(ShotReport)` | after a shot is saved, only when the owner turned shot feedback on |
| `requestSetting(steps)` | reserved |

One grind is one event, because a grinder knows all of it at once:

| `GrindEvent` | |
| --- | --- |
| `seq` | grows per grind |
| `atMs` | when the grind finished (not when it was read) |
| `doseActualG`, `doseTargetG` | ground, and the grinder recipe's dose |
| `gapUm` | burr gap at the grind |
| `beverageTargetG`, `recipeTimeS` | the grinder recipe's yield and brew time, when it has them |
| `grinderRecipe` | the grinder's own recipe number (1-8), 0 if not known |
| `filter` | the grinder's basket type |

`ShotReport`: shot id, total time (start press to stop, pre-infusion included), yield, settled, aborted.

### What an add-on is handed: `Host`

| | |
| --- | --- |
| `nowMs`, `epochS`, `log` | |
| `config(key)` | its own settings only |
| `machine()` | read only: power belief (on, off, unknown), warming up, a shot in hand, scale connected, the last shot's id, time, yield and age |
| `grinder()` | the grinder set up here: model, unit, how a setting is read, its range |
| `allocate(size)` | PSRAM, never freed; take it in `begin()` |
| `fetchStart(FetchJob&)` | one HTTP(S) request on the network task (§4) |

### What the controller does with a grind

The add-on manager (`src/addons/AddonManager.h`) does it, never the add-on:

- **Which recipe.** The grinder's recipe number is mapped to a recipe here: by you on the Add-ons card, or
  automatically when exactly one recipe matches the grind's target dose (±0.5 g) and yield (±2 g). A grind
  for an unmapped or ambiguous recipe changes nothing. A recipe is the preset of that recipe with the coffee in use.
- **The setting.** The burr gap becomes that recipe's setting, never another recipe's, so a single's grind
  does not overwrite the double's. It is taken only by a grinder read in microns (unit `um`), and is clamped
  to the grinder's range. The grind reminder sees the grinder moved.
- **The dose never goes into the recipe.** Grind advice compares only shots whose dose and target match the
  recipe within 0.05 g, so a dose that differs a little at every grind would put each shot in its own group.
  Instead:
  - The next shot on that recipe, started within 15 minutes, stops at `recipe target × actual dose / recipe dose`.
    That shot only (`BrewController::setNextShotTarget`). Only when the measured dose is within 2 g of the
    recipe's; option "the measured dose sets the next shot's own target", on by default.
  - The shot record keeps the recipe's dose and target, and the target it stopped for beside them
    (`ShotRecord::stopTargetCg`, in what was padding, flagged by `SHOT_FLAG_OWN_TARGET`; `stop_target_g` in
    `/api/shots` and the CSV). Every over/under figure (result screen, web page, diagnostics, Home Assistant)
    compares against the target the shot stopped for.
  - The measured dose is saved with the shot's context when the shot is saved (`ShotContextInputs::doseActualDg`,
    flag `CTX_PREP_DOSE_GRINDER`; `dose_weighed_g`). An owner's own entry later replaces it.
- **Option "a grind selects its recipe here"**, off by default.
- **Nothing happens during a shot.** A grind that arrives then waits until the shot has finished.

## 3. Web routes

A device that calls the controller reaches its web server on port 80. An add-on switched on may answer
paths under the prefixes it declared in `addon.json`, and only those:
- under `/api/`, at least two segments (`/api/mygrinder/`), never `/api/addons...`;
- only paths no route of the controller's own answers (the add-on is asked from the not-found handler).

Switched off, its paths answer 404.

## 4. Limits

- **Chip RAM** (the low was about 20 kB). The manager, the add-ons' objects and their buffers come from PSRAM.
  Each add-on's work (begin, tick, routes, network jobs) is marked `RAM_ADDON` in the chip-RAM watch
  (`platform/RamWatch.h`). `/api/addons` and `/api/diagnostics` show per add-on the chip RAM its start took
  and the least free since.
- **Flash.** The core is a few kB in the public image; the inflate code (`core/Inflate.h`) is only linked
  when an add-on uses it.
- **The network never blocks the loop.** `fetchStart` hands the request to the network task
  (`platform/NetWorker.h` `netSubmit`) and returns at once; the add-on collects the answer on a later tick. While it runs, other
  network jobs (library, diagnostics, page previews, signing) are not run (`netRun` returns false) and try
  again on their next turn, and Home Assistant requests skip a turn. Nothing runs on the loop task's stack
  instead. A request has hard time-outs (3 s to connect, 8 s in all).
- **HTTPS** to a device: its certificate is checked by its SHA-256 fingerprint only, before anything is sent
  (a device's own certificate is self-signed and names no host). An https request without a fingerprint is refused.
- **Emulator.** Add-ons run in the emulator (`python emu/build.py --addons=...`). There a request may only go
  to this PC (`127.0.0.1`, plain http, through curl), so an add-on is tested against a stand-in, never a real device.

## 5. What an installed add-on can touch

An add-on is native code in the image: it can do anything the firmware can. Trust is whoever built the
image, and the OTA path's SHA-256 check (releases are not signed). The interface is what an add-on is
meant to use, and the controller keeps to these rules for it:
- settings: its own; secrets never returned to the page and kept out of backups;
- network: only to the origins of its own URL settings;
- web: only under its declared prefixes, only when switched on;
- the controller: through the manager, which bounds every value (dose 5-40 g, settings within the grinder's
  range), changes nothing while a shot runs, and leaves the recipe alone;
- shot feedback: only after the owner turns it on.

The web API is unauthenticated, add-on routes included: use it on a network you trust (`docs/API.md`).

## 6. Machine add-ons (not built)

A machine add-on would offer `START_STOP`, `POWER`, `READ_STATE` (heating, ready, brewing), temperature and
pressure, and possibly a `MachineProfile` of its own. Starting and stopping touches the stop path, so the
rules would be: ticked on the control task, never blocking, a fallback to the button pusher, and the three
guard tests (`testScaleLinkKeepsLunarData`, `testPusherLinkKeepsSwitchBot`, `testMachineProfiles`) unchanged.
None is planned yet.

## 7. Files

- `src/addons/Addon.h`: the interface.
- `src/addons/AddonManager.h`: the registry and the grind policy (host-tested).
- `src/addons/AddonHost.cpp`: the firmware's glue (Host, storage, `/api/addons`).
- `src/addons/HttpWire.h`, `src/addons/NetGate.h`: the request on the wire, the rule between network jobs.
- `src/platform/AddonNet.cpp` (board) and `emu/src/emu_addonnet.cpp` (emulator): the request itself.
- `src/core/Inflate.h`: DEFLATE and ZIP entries.
- `scripts/addons.py`: the build step.
- Tests: `test/host/test_main.cpp` (`testAddonManager` with a test-only grinder, `testInflate`, `testNetGate`,
  `testShotOwnTarget`, `testShotRecordGolden`, `testGrindsKeepOneHistory`, `testContextGrinderDose`),
  `test/host/page_addons_check.mjs`.
