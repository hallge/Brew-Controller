# Add-ons: optional drivers built into a controller image

An add-on is a driver for a device the public firmware does not support, typically a grinder or a
machine with a network interface of its own. It is not part of this repository and not part of the
public releases. It lives in its own folder (a private repository, a folder on your PC), is compiled into
a firmware image built for you, and stays off until you switch it on.

The public firmware has no add-ons: its add-on table is empty, nothing is allocated for them, and it
behaves exactly as before. Grinders and machines keep working without any add-on, set up as today.

This round has **grinder** add-ons. Machine add-ons are described at the end; nothing is built for them yet.

An add-on reaches a controller in one of two ways: compiled into an image (§1, the developer and emulator
route), or as a **module**, a file loaded at start-up (§8). A module is how an add-on that must stay private
is delivered without ever being in a public release.

## 1. How an add-on gets onto a controller

The first route compiles the add-on in (the second, §8, loads it as a module from the file system). Three
ways were weighed:

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
{"id": "my-grinder", "name": "My grinder", "version": "1.0.0", "api": 2, "kind": "grinder",
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
`/api/update` lists the add-ons compiled into the running image (`addons`; modules are not in it). The Firmware card warns when a
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
| `onShotComplete(ShotReport)` | after a shot is saved, when the owner turned shot feedback on, or while `wantsShotReports()` is true |
| `wantsShotReports()` | interface 2. True while the add-on wants every report with the owner's switch off, e.g. any recipe is in shadow mode. Shadow mode only watches, so this adds no control over the grinder or the controller. The add-on needs no card switch for it: the report simply arrives |
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
| `recipeId` | interface 2, set by the controller: the recipe `grinderRecipe` maps to when the grind was dealt with, 0 if unmapped |

`ShotReport`: shot id, total time (start press to stop, pre-infusion included), yield, settled, aborted, and
(interface 2) the recipe, its target time and time basis, the pour time and first drop, `grindSeq` (the
`GrindEvent::seq` of the grind the shot followed, 0 if none) and `advice`, a snapshot of the controller's
grind advice for the shot's recipe worked out with this shot included: its verdict (none, hold, finer,
coarser), the current and suggested setting in stored steps, microns per step, the sensitivity, whether the
step was limited and whether the shot counted. `advice.valid` is false when the active recipe is not the
shot's. Also in interface 2 (appended): `coffeeId`, `bagId` (0: none), `roomTempC` (the room's slow 12 h
temperature saved in the shot's context, NaN when not known) and `coffee`, the coffee's name as the shot recorded
it (32 characters, NUL-ended).

### What an add-on is handed: `Host`

| | |
| --- | --- |
| `nowMs`, `epochS`, `log` | |
| `config(key)` | its own settings only |
| `machine()` | read only: power belief (on, off, unknown), warming up, a shot in hand, its phase (none, starting, pouring, dripping) and elapsed time, scale connected, the last shot's id, time, yield and age |
| `grinder()` | the grinder set up here: model, unit, how a setting is read, its range |
| `allocate(size)` | PSRAM, never freed; take it in `begin()` |
| `fetchStart(FetchJob&)` | one HTTP(S) request on the network task (§4) |
| `notice(text)` | one line on the Home and result screens; `""` takes it away, and it goes by itself after 15 minutes |
| `chipFree()` | chip RAM free now, for work that should wait while it is short |
| `requestShotStart(recipeId)` | asks the controller to start a shot (§5). Returns why it was refused, or null |
| `appendFile`, `writeFile`, `fileSize`, `readFile` | its own files (§5) |
| `recipeForGrinderRecipe(n)` | the controller recipe grinder recipe `n` (1-8) is linked to, by the automatic match or the owner's mapping; 0 if unmapped or the recipe is gone. Lets the add-on ask for the right recipe before any shot exists |

### The card

An add-on may describe extra content for its Add-ons card with `cardJson()`: a JSON object of at most 3 kB,
with every label and value supplied by the add-on. The page draws it as given, and sends edits back through
`command("set", "<field>:<key>:<value>")`.

```json
{"steer": {"title": "...", "note": "...",
           "modes": ["Off", "Shadow", "Grinder", "Controller"],
           "capMode": 2, "capLabel": "Cap (s)",
           "recipes": [{"key": "2", "label": "Triple", "mode": 1, "cap": 3, "note": ""}],
           "finest":   {"label": "Finest allowed gap (um)", "value": "60"},
           "coarsest": {"label": "Coarsest allowed gap (um)", "value": ""},
           "knob": {"label": "Knob press", "value": 0, "options": ["Arms the grind", "Starts the shot"]}},
 "compare": {"title": "...", "file": "compare.csv", "columns": ["Method", "Shots", "Mean error"],
             "groups": [{"name": "Coffee", "rows": [["Controller", "7", "1.2 s"]]}]},
 "charts": [{"title": "Ground and advised", "file": "grinds.csv", "x": "shot_id", "xLabel": "Shot", "yLabel": "um",
             "last": 60, "series": [{"col": "gap_um", "label": "Ground"}, {"col": "advice_um", "label": "Advice"}]}]}
```

- `steer`: a mode switch for each recipe row (`mode` is an index into `modes`; the cap field is enabled only
  in `capMode`), optional `finest` and `coarsest` number fields and a `knob` choice. Edits come back as
  `mode:<recipe key>:<index>`, `cap:<recipe key>:<number>`, `finest::<number>`, `coarsest::<number>` and
  `knob::<index>`.
- `compare`: a table per group (a coffee, say) and a download of the file `file` from the add-on's own
  folder (`GET /api/addons/file?id=&name=`, only that folder; text files). `"files": ["compare.csv", "grinds.csv"]`
  gives one download link per file (up to 4) instead of the single `file`.
- `charts`: up to 2 line charts under the comparison, each with up to 4 series (extras are ignored). `file` is a
  CSV in the add-on's own folder with a header line; `x` and every series `col` are looked up by header name,
  commas inside double quotes are kept, `\r\n` is fine. `x` is numeric (a shot id, say) and the points are drawn
  in file order, the newest `last` rows only (default 60, at most 500). An empty or non-numeric cell is a gap
  in that line; a column the file does not have leaves that series out. `title`, `xLabel`, `yLabel` and the
  series `label` are the add-on's own text. Hovering a point shows its x and the series values. The page
  fetches each file itself through the file route above, so the chart is not drawn (it says "Chart not
  available now") while a shot is running, when the file is missing or when the controller does not answer.
  Keep the file small (the add-on's files may total 1 MB): write a rolling file rather than an ever-growing one.
- The card shows nothing it was not given, and the whole card is hidden when no add-on is compiled in.

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
  - Grind advice counts such a shot with the recipe's other shots: it is the recipe's ratio for the dose
    in the basket. A target of its own more than 10 % from the recipe's is another yield and is left out
    (`GRIND_OWN_TARGET_SAME_FRAC`). Before 11 Nov 2026 every shot with a target of its own was left out,
    so each grinder-dosed shot read "No advice: the shot did not count".
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
- shot feedback: only after the owner turns it on;
- starting a shot: `requestShotStart(recipeId)` goes through the controller's own start (the same path as
  `/api/start`), and only while "Start from the web page" is on (off by default). It is also refused unless the
  controller is ready with the scale connected, the active recipe is that one (it is never switched), no drip
  or result screen is up, the cup check (if the recipe uses one) is met, and no firmware update is running.
  The add-on keeps to one start per grind. An add-on can never stop a shot or press a bot otherwise. Because
  the web API is unauthenticated, anything on the network that can reach an add-on's route could ask for a
  start exactly as it could call `/api/start`: it gets no more than that and nothing without the switch;
- files: only under `/addons/<its id>/`, names `[a-z0-9._-]` of at most 20 characters (no path, no `..`, no
  other add-on's folder), 1 MB for all of an add-on's files, and never when less than 512 kB of the file
  system would be free afterwards (the owner's shot curves come first). `fileSize` lets an add-on roll a file
  over; `writeFile` replaces it. The files survive firmware updates and go into `/api/backup`.

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
- `src/addons/AddonRules.h`: the file-path, file-space and shot-start rules (pure, host-tested).
- `src/addons/AddonAbi.h`: the plain C tables (`bbw_host_api`, `bbw_addon_api`), the module boundary (§8).
- `src/addons/ModuleAddon.h`, `src/addons/AddonModule.h`: the two ends of that boundary (firmware, module).
- `src/addons/ModuleFile.h`: the `.bbwmod` file, the crash guard and the release-checksum parser (pure, host-tested).
- `src/platform/AddonLoader.h/.cpp` and `lib/elf_loader/`: reading, loading, storing and delivering modules (board).
- `scripts/build_module.py`, `test/module/hello/`: building a module, and the loader's first-spike test module.
- `src/addons/AddonHost.cpp`: the firmware's glue (Host, storage, `/api/addons`).
- `src/addons/HttpWire.h`, `src/addons/NetGate.h`: the request on the wire, the rule between network jobs.
- `src/platform/AddonNet.cpp` (board) and `emu/src/emu_addonnet.cpp` (emulator): the request itself.
- `src/core/Inflate.h`: DEFLATE and ZIP entries.
- `scripts/addons.py`: the build step.
- Tests: `test/host/test_main.cpp` (`testAddonModules`, `testAddonManager` with a test-only grinder, `testInflate`, `testNetGate`,
  `testShotOwnTarget`, `testShotRecordGolden`, `testGrindsKeepOneHistory`, `testContextGrinderDose`),
  `test/host/page_addons_check.mjs`.

## 8. Add-ons as modules

A module is an add-on built separately from the firmware and loaded from the file system at start-up. It lets
a private add-on be delivered from a repository of the owner's own (a private GitHub repository) without it
ever being in the public firmware or its releases, and it survives firmware updates (OTA does not touch
LittleFS). Nothing in the public repository names a private add-on, a repository or a token.

### The boundary: a C table, not C++ objects

C++ virtual calls on host objects are not safe across a loaded ELF, so only plain C crosses (`src/addons/AddonAbi.h`):
- the firmware hands the module a `bbw_host_api` (`now_ms`, `config`, `log`, `machine`, `grinder`, `allocate`,
  `fetch_start`, `notice`, `chip_free`, `request_shot_start`, the file calls);
- the module exports one entry, `bbw_module_main`, which returns a `bbw_addon_api` (`info`, `begin`, `end`,
  `tick`, `route`, `command`, `status`, `card_json`, `caps`, `link`, `take_grind`, `shot_complete`).

Every struct has a fixed layout, `static_assert`ed sizes, and starts with its own `abi` and `size`. The ABI number
is `BBW_ADDON_ABI`, the same number as `ADDON_API_VERSION` (2). Two header-only adapters keep add-on code the C++
it is: `AddonModule.h` (in the module) wraps the host table back into a `Host` and builds the `bbw_addon_api` from
an `Addon*`; `ModuleAddon.h` (in the firmware) makes the module's table an ordinary `Addon` and `GrinderDriver`,
so the manager, the card and the grind policy treat a module exactly like a compiled-in add-on. The host tests link
an add-on, `AddonModule.h` and `ModuleAddon.h` together, so every call crosses the table as on the board.

A module has no C++ runtime and no `malloc`: `operator new` takes zeroed PSRAM from `Host::allocate`, `delete`
does nothing, nothing runs at exit, and a static constructor must not allocate (it runs before the entry).
Build flags are `-fno-exceptions -fno-rtti -fno-threadsafe-statics`. **A module may call only this allow-list
from the firmware:** `snprintf`, `vsnprintf`, `strtod`, `atof`, `atoi`, `atol` (formatting and parsing numbers);
`strlen`, `strcmp`, `strncmp`, `strncasecmp`, `strstr`, `strchr`, `strtok`, `memcpy`, `memmove`, `memset`,
`memcmp`, `memchr`, `strcpy`, `strncpy` (text and memory); `floorf`, `fabsf`, `sqrtf`, `fminf` (single-precision
maths). Anything else makes the module fail to load, and the card names the symbol ("it needs X, which a module
may not use"). Code that needs 64-bit arithmetic or double-precision maths gets it from `libgcc`, which
`scripts/build_module.py` links into the module itself (without its `.eh_frame`, which a shared link cannot take),
so those helpers (`__udivmoddi4`, `__divdf3`, `__muldf3`, ...) are not imports. The list is `src/platform/AddonImports.h`;
a host test keeps it and this paragraph equal. `strtok` keeps its state in the C library, which is fine because
module code only ever runs in the loop task. The loader supports the relocation types a module build produces:
`R_XTENSA_RELATIVE`, `R_XTENSA_JMP_SLOT` and `R_XTENSA_RTLD` (and `R_XTENSA_GLOB_DAT`); any other type refuses the module.

Module code runs from PSRAM. While flash is written PSRAM cannot be reached, so module code is only ever called
from the loop task (never from an interrupt, never from the control task).

### The file and where it lives

`<id>.bbwmod` is a 128-byte header followed by the module's ELF (at most 256 kB): magic `BBWM`, header size, ABI, ELF
size, minimum firmware, the add-on's id and version, and the ELF's SHA-256 (layout in `src/addons/ModuleFile.h`).
It is stored at `/addons/<id>.bbwmod` (not in the add-on's own folder `/addons/<id>/`, which holds its data files).
`python scripts/build_module.py <add-on folder>` makes one from the folder's `addon.json` with the PlatformIO
ESP32-S3 toolchain; `--check FILE` verifies one; `--sums FILE` appends the line for a release's `SHA256SUMS.txt`.

At start-up, before `addons::begin`, the loader reads each file, checks the header and SHA-256, loads the module
into PSRAM and makes its add-on. Each module becomes one more table entry beside any compiled-in ones, so the
manager is unchanged. With no module files, nothing is allocated. A module that cannot start is still listed,
with the reason, and never started:
- **built for another interface** (ABI differs): "needs a rebuild";
- **needs a newer firmware** (its minimum is above this firmware's);
- **refused**: not a module file, wrong size or checksum, an import outside the allow-list, or no usable add-on;
- **stopped after a crash** (below).

A firmware update with a newer `ADDON_ABI` therefore lists the module as "needs a rebuild" until a matching one is
installed; the Add-ons card says so for that module. Modules are files and stay through any update, so the
Firmware card says nothing about them (`/api/update` lists them apart, in `modules`).

### The crash guard

With no USB there is no way to recover from a module that crashes the board on every start. NVS
`bbwaddon/loading` names the modules being loaded and begun, set before loading and cleared after 120 s of
normal running. If the board starts after a panic or a watchdog reset with the flag still set, those modules
are put on `bbwaddon/blocked` and not loaded; the card says "stopped after a crash", and "Try it again" takes the
block away for the next restart. A restart by the owner, an update or a power cycle with the flag set is not
treated as a crash. If two modules were being loaded, both are held back (the flag cannot tell which). The flag is
not a promise about the firmware itself: a new firmware still has its own trial start.

### Delivery

Delivery is separate from the firmware, and the firmware's own update is unchanged.
- **Add-ons from** (Add-ons card): `owner/name` and an optional token, stored in NVS (the token is never sent
  back to the page and is not in backups). There is no default repository. "Check for add-on updates" asks the
  repository's newest release for `<id>.bbwmod` files with a line in `SHA256SUMS.txt`, and says for each whether it
  is installed and whether the installed file is the same one. "Install" or "Update" downloads it on the network
  task into a temporary file, checks the SHA-256 against `SHA256SUMS.txt`, then the header and the file's own
  checksum, and swaps it in. The module loads at the next restart (the card offers it).
  It reuses the firmware update's GitHub release fetch (`src/platform/FirmwareUpdate.h`, `releaseListLatest`
  and `releaseStreamAsset`): the same trusted roots, the token only sent to the API.
- **Upload**: `POST /api/addons/module` (multipart, one file), for a local build. Checked the same way.
- Both are refused while a shot is in hand, or while another network job runs, and need free storage beyond the
  shot-curve reserve (512 kB). A file that does not fit this firmware (another interface, a newer minimum) is
  refused rather than stored.
- `POST /api/addons/module/retry?id=`, `/remove?id=`, `/restart`, `/check`, `/install?id=` and `POST
  /api/addons/source {repo, token}` are what the card calls. `GET /api/addons` lists each module's state and the
  delivery state (`module_source`).

Trust is what it is for the firmware upload today: the LAN API is unauthenticated (`docs/API.md`), so anything on
the network that can reach the controller can store a module file, and a module is native code. A release's
checksum file proves only that the download matches the release, not who made it; keep the repository private and
its token narrow (Contents, read only). Do not use an untrusted network.

### The emulator and the first spike

The emulator keeps the compiled-in path (`python emu/build.py --addons=...`); it has no loader. The boundary is
tested on the PC by linking the add-on, `AddonModule.h` and `ModuleAddon.h` in the host tests.

The loader rests on one assumption the PC cannot test: that the ELF loader runs code from PSRAM on this board's
Arduino core. `test/module/hello/` is a tiny module for that spike, with its steps in the report of the change
that added it: it checks a static constructor, virtual calls, allocation, data and read-only data, and the
allow-listed imports every tick, and reports `ok` or the first fault on its status line. If the spike fails, modules
are dropped and the compiled-in route (§1) stays the only one.
