# Brew by Community: design

How the controller grows from brew-by-weight into a shared dial-in library:
what changes in the data model, the web page, the controller screen and the
back end. Written 2026-10-10. Step 1 of section 7 (the data model split and
the migration) is built; section 2 says how it turned out. The rest is not.

Screen mock-ups of the web page: https://claude.ai/artifact/U4qVf3B3bSBHPqSUt5g6Zi

## 1. What LUCCA TrueGrind does, and what to take from it

From Clive Coffee's own pages, the Daily Coffee News launch article and the
app's store listing:

- **Sensor.** The LUCCA Atom 75 (Eureka Atom 75 underneath) has an active,
  laser-calibrated position sensor that reports the burr gap at all times.
  One step on its display is 5 microns. The dial is still turned by hand; the
  grinder only tells you where to turn it to.
- **Prediction.** A cloud service returns a grind position for a coffee, a
  recipe (dose, yield, time) and a machine. Its stated inputs: the coffee's
  colour score and roast date, and the machine's brew temperature, pressure
  and basket size.
- **Library.** Curated, not crowd-sourced: 100 coffees at launch in 2021
  (mostly from Mistobox, which Clive part-owns), "over 500" in the app
  listing. Roasters can submit recipes. Up to six recipes per coffee.
- **Feedback.** The product page mentions a dial-in assistant that adjusts
  after a shot. How it works is not published.
- **State.** The store listing was last updated in May 2024. Reviews on
  third-party listing pages report sync failures, and one asks for exactly
  what this project is about: tuning the library from users' real shots.
  I could not confirm any of that first-hand.

What that means here:

| TrueGrind | This controller |
| --- | --- |
| Knows the burr gap in microns | Same: the E64 WS shows it, and it is typed in |
| Predicts from coffee and machine facts | Has no colour score, and cannot read the machine |
| Never sees the shot | Measures every shot: time, first drops, pour flow, flow shape |
| Curated library, closed | Open library built from measured shots |

So the strength to build on is the closed loop. TrueGrind guesses a setting
and stops. This controller can share **what a good shot of this coffee looked
like** (time, pour flow) along with the setting, and the grind advisor already
knows how to walk a grinder to a target time. A shared setting only has to be
close enough to start from.

## 2. Data model: split coffee, recipe and the pairing

Today `Recipe` holds everything: coffee facts, dose and yield, grind setting,
learned stop offset. That is why the same "Double" has to be re-entered for
each coffee. Split it into three records plus the grinder:

```
Coffee      what it is       roaster, name, origin, region, producer, varietal,
                             process, roast level, altitude, notes
  Bag       which bag        roast date, opened date            (1 coffee : n bags)
Recipe      how to pull it   dose, yield or ratio, target time, pre-infusion,
                             time limit, starting stop offset
Grinder     what grinds it   model (from the registry), burrs, zero date
Preset      coffee + recipe  grind setting, learned stop offset, learned
                             alpha, learned microns-per-second
```

- A **preset** is one coffee paired with one recipe on the current grinder.
  It owns everything that is learned, because all of it depends on both.
- The controller's list shows presets. Tapping one still selects coffee and
  recipe together, exactly as now. `MAX_RECIPES = 16` becomes 16 presets.
- A preset with no coffee is allowed ("Ristretto", "Guest"), so the current
  coffee-less recipes carry over unchanged.
- `ShotRecord` keeps its copied coffee and recipe fields (history must stay
  true when records are edited) and gains `coffeeId`, `bagId`, `recipeId`,
  `grinderId`. The grind advisor's grouping key, today "recipe name + coffee
  string + roast date + target + dose", becomes "preset + bag".
- A new bag keeps the preset, seeds its grind from the last bag, and starts a
  fresh run for advice. That is what the roast-date comparison does today.

### As built (step 1)

Code: `core/Types.h` (`Coffee`, `RecipeDef`, `Preset`, `Library`),
`core/Library.h` (everything that reads or changes a library, the saved
format, and the readers for what earlier builds stored), `core/Legacy.h`
(the old layouts, frozen), `storage/Store.cpp`. Host tests: `testLibrary`
and `testMigration`.

- **Where it is stored.** `/library.bin` on LittleFS: a header and the three
  tables as fixed records, written beside the old file and renamed over it.
  Not JSON as first planned: the records are small, the format is covered by
  the host tests without a JSON library, and fields can still be added at the
  end of any record (the header carries each record's length). `Settings`
  keeps only real settings, under a new NVS key (`cfg`).
- **The flat `Recipe` stays** as the form a preset is handed around in:
  `resolvePreset()` fills one in from the three tables. The brew controller,
  the display and the grind advisor did not have to learn the new model.
- **Bags.** A coffee has one roast date, the bag in use. A new bag is a new
  date on the same coffee; shots carry the date they were made with, so
  advice still starts fresh. A separate bag table can come later if bags need
  more than a date.
- **Shot history is version 9**: the same record plus `coffeeId`, `recipeId`
  and a reserved `grinderId`. Grind advice groups by those ids, so renaming a
  coffee or a recipe no longer cuts it off from its history.
- **Sharing.** Editing a preset's dose, yield or time (web page, the
  controller's quick edit, its + and - buttons) changes the recipe, so every
  coffee on that recipe. Editing a coffee's details changes it for every
  recipe. A new entry made on the current web page always gets a recipe of
  its own, so nothing is shared until the new page offers it.
- **Limits**: 24 coffees, 16 recipes, 16 presets.

Migration, on the first start of the new build:

- Settings version 7 are read from NVS and left there. Each of its recipes
  becomes one recipe and one preset of the same name, in the same order.
  Recipes are **not** merged, even when dose and yield match: the names are
  what the history shows, and merging would lose some. Recipes naming the
  same coffee (same name, origin and roast date) share one coffee. Grind
  setting, learned stop offset and learning factor move to the preset.
- History version 8 is rewritten shot for shot into version 9, with the ids
  looked up by the names each shot carries. The old file is kept as
  `/shots.v8.bak`. A reset part-way leaves the old history to convert again.
  A history file this build cannot read at all is renamed `/shots.unread`,
  never deleted.
- Checked on a copy of the emulator's real data: 7 recipes became 7 presets
  and 3 coffees, all 23 shots came through with every exported value
  unchanged, and grind advice read the same before and after.
- An older build must not be run on converted data: it would not recognise
  the history and would start a new one.

One thing changed in behaviour: a preset now keeps its learned stop offset
when its target weight is edited (before, the web page restarted it from the
baseline, because name and target were the only way to tell which recipe was
which). "Reset learned offset" is still there.

For the new page, the current names want a look: the old form's "Coffee"
field became the coffee's name and its "Name" the recipe's, so the first
preset reads as coffee "Portrait", recipe "DR Congo Idjwi Island".

## 3. Web page

One self-contained page as now (it must work on the access point with no
internet, so no web fonts or CDN files), but with six sections instead of one
scroll. Section switching is client-side; the existing `/api/*` handlers stay
and new ones are added beside them.

| Section | Holds | Replaces |
| --- | --- | --- |
| Home | Live readings, what is being brewed, grind advice in one line, last shot with its curve and taste buttons, community note | Live, top of Grinder, Most recent shot |
| Shots | Selected shot in detail, consistency figures, history table cut from 19 columns to 9 with filters | Shot history |
| Coffees | Search, my shelf and the shared library, coffee details, bags, "brew it with" recipe ticks, community dial-in | Coffee fields inside Recipes |
| Recipes | Recipe list and form in three groups, and which coffees use it | Recipes |
| Grinder | Setting now, advice with its reasoning, brew time against grind chart, my grinder, advice settings, shared registry | Grinder |
| Setup | Devices, Brew, Network, Shared library | Brew settings, SwitchBot, Devices, Network |

Decisions worth knowing about:

- **Where coffee meets recipe.** On the coffee's page, under "Brew it with":
  tick the recipes you use for it. Each tick is one preset and one line on
  the controller. No separate mapping screen.
- **Own data is orange, shared data is blue.** Anything that came from other
  people is in a blue panel, so a community number is never mistaken for a
  measurement of yours.
- **Rarely changed settings are folded away** (scale timeout, no-flow
  timeout, press timing). They are still there.
- **Rating moves to Home.** Feature 13 in FEATURES.md calls ratings the most
  valuable missing input. Three buttons under the last shot get used; a
  column in a table does not.
- The header shows save and sync state at all times: saved on the controller,
  records waiting to send, last library refresh.

The page grows from 28 KB to an estimated 60 to 80 KB. Serve it gzipped from
PROGMEM (`Content-Encoding: gzip`); the emulator's HTTP shim needs the same
header.

## 4. Controller screen

Unchanged in behaviour. The recipe list becomes the preset list: coffee name
on the first line, recipe and grind on the second. One addition on the result
screen, only when the library has data for the coffee and the preset has no
grind yet: `COMMUNITY START 250 um`. Rating stays off the device, as decided
in FEATURES.md.

## 5. Back end on GitHub

### Where the note's plan is changed

| The note | Here | Why |
| --- | --- | --- |
| Browser local storage is the working copy | The controller is | Shots happen with no browser open; the page is a view of the controller |
| Background push from the client | The controller pushes, when idle | Same reason; and never during a shot, while BLE timing matters |
| One read-write key shared by authorised devices | No shared key; each contributor writes to their own repository | See Security |
| Append rows to shared files | One new file per record | Creating a file needs no knowledge of the current state, so two devices can never collide |

### Layout

```
library repository (public, read by everyone without a key)
  coffees/<roaster>/<coffee>.json     one coffee, canonical facts
  grinders/<brand>-<model>.json       model, burrs, unit, microns per unit if known
  contributors.json                   list of contributor repositories
  index/                              built by a GitHub Action, never by hand
    coffees.json                      id, name, roaster, origin: for search
    dialin/<coffee-id>.json           aggregated dial-in per coffee and recipe class
    grinders.json                     per-model offsets and scale

contributor repository (one per controller owner, from a template)
  coffees/<id>.json                   coffees this owner added or corrected
  dialins/<yyyy>/<mm>/<id>.json       one settled dial-in each
  shots/<yyyy>/<mm>/<id>.json         optional: one counted shot each
```

A scheduled Action in the library repository reads every repository listed in
`contributors.json`, validates each record against a JSON schema (types,
ranges from `Types.h`: dose 5 to 30 g, time 5 to 90 s, and so on), drops what
fails, and rebuilds `index/`. Controllers read only `index/`, through the
raw-file or Pages address, cache it in `/lib/` on LittleFS and use the cache
offline. `coffees.json` is fetched whole; a dial-in file only for coffees on
the shelf.

### What a dial-in record holds

Published when a preset has settled: the advisor says HOLD over the full
averaging window.

```json
{ "v": 1, "id": "01J9...", "device": "random-uuid", "at": "2026-10-10",
  "coffee": "portrait/dr-congo-idjwi-island", "roast_date": "2026-03-03",
  "recipe": { "dose_g": 18, "yield_g": 36, "preinfusion": "machine" },
  "grinder": { "model": "mahlkonig-e64-ws", "burrs": "stock", "zeroed": "..." },
  "grind": { "value": 246, "unit": "um" },
  "result": { "time_s": 25.6, "basis": "total", "first_drop_s": 8.4,
              "pour_flow_gs": 2.1, "shots": 3, "spread_s": 0.6 },
  "machine": "breville-bes920", "taste": "balanced", "rating": 4 }
```

No name, no address, no network detail. `device` is a random ID made on first
boot, not the MAC. Taste and rating are sent only if ticked in Setup.

### Security

A read-write key shared by every device does not hold up. It sits in flash on
hardware other people own, anyone who extracts one can delete or rewrite the
whole library, and it cannot be revoked for one person without breaking
everyone. A fine-grained GitHub token with Contents write also allows
deleting files and pushing to any unprotected branch.

Instead:

1. **Now, one controller.** A fine-grained personal token limited to one
   repository, Contents read and write, nothing else. Entered once in Setup
   over the controller's own access point, stored in NVS, never returned by
   any `/api` call. Worst case if it leaks: one repository of your own shot
   data. Personal fine-grained tokens can be made without an expiry, though
   GitHub recommends one; a yearly reminder on the Setup page is cheap.
2. **A community.** Every contributor does the same with their own
   repository. The library only ever pulls. Removing a bad contributor is one
   deleted line in `contributors.json`, and no secret is shared at all.
3. **Later, if GitHub accounts are a barrier.** A small relay (a serverless
   function holding a GitHub App key) that accepts a record, validates it and
   commits it. Controllers then hold no GitHub credential. Not needed to
   start.

There is a plain-text file `C:\tools\github keys.txt` next to this project. I
did not open it. If it holds live tokens, they should be rotated and kept in
a password manager before any of this goes on GitHub.

### On the controller

- HTTPS with `WiFiClientSecure` and the root certificate bundle, only in
  station mode, only in `READY` with no shot in the last 30 s.
- Outbox on LittleFS: a record is written to `/out/<id>.json` when it is
  made, sent with `PUT /repos/{owner}/{repo}/contents/{path}`, and deleted on
  201. A 422 (already exists) also counts as sent. Anything else stays for
  the next try, with back-off.
- A record is about 0.6 KB; the request about 1.5 KB with base64. GitHub
  allows 5000 authenticated requests an hour per token; a controller makes a
  handful a day.
- The emulator needs a `WiFiClientSecure` shim; point it at a local folder so
  the whole flow can be tested with no network.

## 6. Making settings travel between grinders

A setting on one grinder means nothing on another, even between two grinders
that both read in microns: burr geometry changes what a gap produces. The
registry gives each model a unit and, where known, microns per unit. The rest
is learned:

```
setting(owner, coffee) = fineness(coffee, recipe class, days since roast)
                         x scale(grinder model) + offset(owner's grinder)
```

- `fineness` is a per-coffee number on a common scale, fitted by the Action
  from all dial-ins of that coffee.
- `offset` is per grinder instance (zero points differ), fitted from the
  coffees that owner has in common with others. With none in common it is
  zero and the suggestion carries a wide range.
- Medians and a minimum count before anything is published, so one odd entry
  cannot move a suggestion.

The honest limits, in the spirit of FEATURES.md:

- The first coffee on a new controller gets a suggestion no better than the
  model default. It improves with each coffee in common.
- The Breville's temperature and pressure cannot be read, so machine
  differences end up inside the owner's offset. That is acceptable as long as
  the owner does not change them.
- A suggestion is a start. The shared target time and pour flow matter more:
  they tell the local advisor where to steer, and that part works on every
  grinder.
- Coffee identity is the weak point of every crowd library. Search-before-add
  and a canonical ID (`roaster/coffee`) reduce duplicates; merging the rest
  is a maintainer job on the library repository.

## 7. Signed contributions and review

The library is meant to work like a wiki: anyone's controller can propose a
coffee or a correction, a reviewer approves it, and only then is it
published. Two things make that workable.

### Each controller signs what it sends

- On first start the firmware makes a key pair (ECDSA P-256, from the ESP32's
  hardware random number generator; mbedTLS is already in the build). The
  private key stays in NVS and is never shown or sent. The public key is the
  controller's identity; its short fingerprint replaces the random `device`
  id in section 5.
- Every record sent (a coffee, a correction, a dial-in, a shot) carries the
  public key's fingerprint and a signature over the record's canonical bytes
  (keys sorted, no spaces, the `sig` field left out).
- The first thing a controller publishes is its public key
  (`keys/<fingerprint>.json` in its contributor repository). The library
  records the key the first time it sees it.

What a signature proves, and what it does not:

- It proves a record comes from the **same controller** as that key's earlier
  records, and that nobody changed it on the way. That gives every
  contributor a history, lets reviewers trust a key that has been right
  before, and lets one key be blocked without touching anyone else.
- It does **not** prove the sender is a real controller. The firmware is
  open, so anyone can make a key and sign invented data. Nothing a
  controller can hold in its flash would change that. A new key is therefore
  just a stranger: its coffees wait for a reviewer, and its dial-ins count
  for nothing until several of its shots agree with other contributors'
  (the medians and minimum counts of section 6 already limit what one key
  can do).

### Review before publishing

GitHub's pull requests are the review queue; nothing has to be built for
that part.

```
controller   signs a record, pushes it to its own repository          (step 5)
library      scheduled Action: pulls new records from every listed
             contributor, checks schema and signature
               coffee added or changed  ->  opens a pull request:
                                            "Add Portrait / Idjwi Island"
                                            with the fields, the key's
                                            fingerprint and its record so far
               dial-in or shot          ->  no review: merged into the
                                            contributor's data, used by the
                                            aggregates once the key is trusted
reviewer     reads the pull request, edits if needed, approves and merges
library      Action on merge: rebuilds index/, coffee now in everyone's search
```

- **What needs review:** coffee entries and edits to them, grinder models,
  and a contributor's first appearance. These are the wiki's pages.
- **What does not:** measurements. A dial-in is a fact about one controller;
  it is checked by signature, schema and range, and weighed statistically.
- **Reviewers** are people with write access to the library repository,
  named in `CODEOWNERS`. Branch protection requires one approval. A reviewer
  cannot approve their own controller's submission.
- **Edits** to a published coffee are a pull request that changes its file,
  so the diff, the discussion and the history are all there, as on a wiki
  page's history tab.
- **On the controller,** a coffee shows one of: local only, sent, in review,
  published, declined (with the reviewer's note). A declined or edited entry
  never overwrites what the owner typed; the published version is offered.
- **Duplicates** are the reviewer's main job. The Action helps by listing
  existing coffees with a similar name and roaster in the pull request.

## 8. Order of work

1. **Data model split and migration.** Done (section 2).
2. **Web page rebuild.** Done: six sections (`#home`, `#shots`, `#coffees`,
   `#recipes`, `#grinder`, `#setup`) in `net/WebPage.h`, with
   `POST /api/coffee`, `POST /api/recipe` and `POST /api/library/do` behind
   them. Coffee meets recipe under "Brew it with". Not done from the list in
   section 3: serving the page gzipped (it is 47 KB and loads fine as it is), and
   the controller's list still shows the recipe name first.
   Also done here, though not a web change: the **bloom pause** in recipes
   (README section 1).
3. **Controller list.** Done: the coffee heads each line and the home card,
   with the recipe beneath; a recipe listed by itself heads its own.
4. **Grinder record and registry.** Done. `Grinder` in the library (model,
   burrs, the unit a setting is read in, microns per unit, last zeroed) and
   a bundled list of five models in `core/GrinderRegistry.h`; "My grinder"
   on the Grinder page. The unit shows wherever a setting does, on the page
   and the controller. `Grinder::calibration` goes up when the grinder is
   zeroed again, gets other burrs or is replaced; shots carry it
   (`ShotRecord::grinderId`) and grind advice only compares shots made
   under the current one. Naming the grinder for the first time is not a
   change. A change of unit clears the settings entered so far. Limits:
   settings are whole numbers (a dial read in tenths is not supported), and
   the sensitivity settings are still in units per second, whatever the
   unit.
5. **Device key and signed outbox.** Done on the controller's side, to a
   local folder; not connected to GitHub.
   - `core/Share.h` builds the records (key, coffee, dial-in, shot) and the
     signed envelope; `core/Sha256.h` is the hash. Host-tested, with the
     standard SHA-256 vectors.
   - The key: `platform/DeviceKey.cpp` makes an ECDSA P-256 pair with the
     board's mbedTLS on first start and keeps it in NVS. **It compiles; it
     has not run**, and will not until there is a board. The emulator's
     stand-in (`emu/src/emu_devicekey.cpp`) signs as `EMULATOR-UNSIGNED`,
     which the library refuses.
   - The outbox: one small file per record on LittleFS (`Store::outbox*`),
     room for 300, kept across restarts. Sent one a second while idle.
   - Where it sends: in the emulator, the folder `published/` beside its
     data, laid out as the repository will be. On the board, nowhere yet:
     records wait in the outbox (`publishRecord()` in `main.cpp` is the one
     function to fill in with the GitHub push).
   - What is queued, once "Share from this controller" is on in Setup (it
     is off until then): the key, once; a coffee when it is saved, or all of
     them on request; each shot that counts for grind advice, if shots are
     shared; a dial-in the first time a coffee and recipe settle at a grind
     setting (advice says keep, over the full window).
   - The library's side: `library-template/tools/verify_record.py` checks
     envelope, key, signature and ranges, and passes its self-test (RFC 6979
     vector, tampering, wrong key, emulator records). The pull-request check
     that runs it is written but has never run. The collector that opens
     pull requests for coffees is not written: it needs the repository.
   - Still to connect: the HTTPS push, the repository's address and token
     on the Setup page.6. **Coffee status.** Done. Each coffee on the Coffees page shows where it
   stands: on this controller only, waiting to send, sent, in review,
   published, declined (with the reviewer's note). "Send to the shared
   library" queues one coffee; an edit after sending shows as "changed
   since" and can be sent as a new version. A new bag (roast date) is not a
   new description.
   - The first three states the controller knows by itself, from the
     outbox. The last three come from the library, as a small file it reads
     every 15 s while idle: `status/<key id>.json`,
     `{"v":1,"coffees":[{"local_id":1,"record":2,"state":"review" |
     "published" | "declined","note":"...","library_id":"..."}]}`.
     `record` is the number in the coffee record's file name
     (`c<local id>-<record>.json`): a verdict only counts for the version
     last sent, so one about an older record cannot undo an edit.
   - A verdict never changes what the owner typed.
   - In the emulator that file is read from `published/status/`; nothing
     writes it, so the review states were tried by writing it by hand. On
     the board there is nowhere to read it from yet. Writing it is the
     library's job (the collector, with step 5's push).
   - Shown on the web page only, not on the controller's screen.7. **Library read and cache.** Done, against a local folder.
   - While idle the controller reads the library's index a file every two
     seconds and keeps each on flash, written only when it has changed:
     the list of published coffees (`index/coffees.json`), what was settled
     on for each coffee on the shelf that is in the library
     (`index/dialin/<coffee>.json`), and how its own grinder sits against
     the common scale (`index/fit/<key id>.json`). A full pass once a
     minute in the emulator, once an hour on the board; straight away when
     a coffee is added from the library.
   - Coffees page: coffees in the shared library show under the search, with
     Add. One added that way is already published, and its dial-ins name
     the library's entry.
   - Community panels (Coffees and Home): where to start the grinder and how
     that was arrived at, what a good shot looked like, how many people it
     rests on, and "Use ... as my starting grind".
   - The starting setting (`core/Community.h`), best basis first: this
     grinder's own fitted scale and offset; else the median of others with
     the same grinder model; else the common scale read as microns. Each
     comes with a range, never tighter than 10 microns with one coffee in
     common or 5 with more. An unmarked dial with no fit gets no suggestion.
   - Controller screen: with no grind entered for the selected coffee and a
     suggestion to hand, the home card says `community: start 235 um`.
   - The coffee list is read whole, into a 16 KB buffer: about a hundred
     coffees. A larger library needs searching on its side.
   - On the board there is nowhere to read from yet (`fetchLibraryFile()`
     in `main.cpp`).
8. **Cross-grinder fit.** Done, as the library's index builder:
   `library-template/tools/build_index.py`. It takes the verified dial-ins,
   fits a fineness per coffee and recipe and a scale and offset per
   contributor's grinder (section 6), and writes the three kinds of index
   file above. Medians throughout; a coffee's figures appear once two
   different contributors have settled it (`--min-contributors`).
   - Its self-test recovers invented scales and offsets exactly, predicts
     settings nobody reported, is not moved by one wild entry, refuses to
     guess an unmarked dial from two coffees, and runs end to end on signed
     example records.
   - Tried with the emulator: an example library of four invented
     contributors; a coffee added from it started from the same-model
     median; after three shots the controller's own dial-in went out, the
     index was rebuilt, and the next coffee's suggestion came through the
     controller's own fitted offset.
   - Not modelled: the coffee ageing since roast, and the machine. Both end
     up inside a contributor's offset or the spread.
   - The common scale's zero is pinned to the middle contributor with a
     grinder marked in microns, so "fineness" is a relative number: it
     moves a little as contributors join.

## 8a. Grinders: the owner's own, and the shared list

Added after step 8. A grinder is treated as a coffee is.

- **One in use, one more at most.** A controller keeps the grinder in use
  and, if its owner has a second, that one beside it ("Add ... as my second
  grinder", "Use it instead" on the Grinder page). Each has its own setting
  for every coffee and recipe, its own calibration and so its own shots.
  Switching swaps the settings in and out and loses nothing: advice for
  either grinder picks up where it was.
- **Kept here, or sent.** A grinder that is one of the firmware's five known
  models has nothing to send. One that was typed in is kept on the
  controller, and can be sent to the library ("Send to the shared
  library"): a signed `grinder` record, checked by the intake, then a pull
  request that would add it to `grinders/`. Its standing shows as for a
  coffee: on this controller only, waiting, sent, in review, published,
  declined.
- **Why a shared list is worth having.** The grinder is part of the
  estimate: a suggestion for a new coffee uses, in order, this grinder's own
  fitted scale and offset, what other owners of the same model set, and the
  common scale read through the model's microns per unit. The last two need
  everyone with a given grinder to mean the same thing by its name and its
  unit, which a typed-in name cannot guarantee and a reviewed entry can.
- The library's list (`index/grinders.json`) appears in the model list on
  the Grinder page, after the firmware's own. A grinder picked from it is
  published already, and its dial-ins name the entry.
- Still not handled: a dial read in tenths (settings are whole numbers), and
  the coffee ageing since roast, which matters as much as the grinder for
  how far a shared setting can be trusted.
## 9. The library repository, and what is left

The library is https://github.com/hallge/Community-Coffee (private for now).
`library-template/` in this project is the copy its files were made from.

In place there, as a pull request (#1) and an `inbox` branch:

- **Intake.** Controllers push signed records to the `inbox` branch. A small
  workflow there asks `main`'s Intake workflow to run (it also runs daily).
  Intake checks every record (envelope, ECDSA P-256 signature against a key
  on file, types and ranges), files keys, dial-ins and shots, and for each
  coffee opens a pull request that would add it to `catalog/`. What fails
  is left out and listed with its reason. Nothing is decrypted: records are
  signed, not encrypted.
- **Review outcome.** Merging such a pull request publishes the coffee;
  closing it declines it (a last comment starting `Declined:` is passed to
  the contributor). Either way the contributor's `status/<key id>.json` is
  updated and the index rebuilt.
- The checking always runs from `main`. A controller's token (Contents read
  and write on this one repository) cannot change workflow files, so it
  cannot change what the checks do.
- A coffee record now carries its version number (`record`) inside the
  signed body, so the verdict can name the version it is about.
- Each tool has a self-test and all pass. **The workflows have not run.**

Still to do:

- Merge the pull request, and in the repository's settings let Actions write
  and create pull requests (README there, "Setting up").
- On the board: the HTTPS push to `inbox/` on the `inbox` branch and the
  reads of `status/` and `index/` (`publishRecord()`, `fetchStatus()`,
  `fetchLibraryFile()` in `main.cpp`), with the repository's address and
  the controller's token on the Setup page. While the repository is
  private, reading needs the token too.
- First real run end to end. Nothing from the emulator can be used for it:
  its records are refused by design. It needs the board's real signatures.
- On the real board: the mbedTLS signer checked against the verifier, and
  free memory with Wi-Fi, Bluetooth and TLS all up (see below).

### Memory on the board

The ESP32-S3R8 has about 340 kB of RAM inside the chip for data (the rest of
its 512 kB holds code that must run from RAM) and 8 MB of PSRAM beside it.
Everything the linker places as a global goes inside the chip, and by step 8
that was 221 kB, three quarters of it this project's own buffers, leaving
roughly 120 kB for Wi-Fi, Bluetooth, TLS and the task stacks: not enough.

Now the large buffers that only the loop task uses are taken from PSRAM at
start-up (`platform/BigMem.h`): a shot's readings on their way to flash and
to the browser, the text read from the library, the working copies of the
library, the signing buffers. Fixed use inside the chip is 99 kB, leaving
about 245 kB. LVGL's memory and the display's frame buffer were already in
PSRAM.

Left inside the chip on purpose: the brew controller with the shot in
progress (25 kB) and the library it brews from (10 kB). Stopping a shot
does not depend on the external memory. Without PSRAM the buffers fall back
to the chip's own memory and the firmware still runs, with a warning in the
log.

What cannot be known without the board is how much the radios and TLS take.
The Setup page's Devices card and the start-up log show the figure that
answers it: the least free memory inside the chip since start-up. Below
about 40 kB is tight.
## Sources

- Clive Coffee, "LUCCA Atom 75 Espresso Grinder Overview" and "LUCCA Espresso
  Launches Smart Coffee Grinder"; LUCCA Atom quick start guide
- Daily Coffee News, launch article on the LUCCA Atom 75 (Sept 2021)
- TrueGrind AI, App Store listing and third-party listing pages for reviews
- GitHub REST API: repository contents; fine-grained personal access tokens
