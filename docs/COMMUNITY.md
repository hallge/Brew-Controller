# Brew by Community: design

How the controller grows from brew-by-weight into a shared dial-in library:
what changes in the data model, the web page, the controller screen and the
back end. Written 2026-10-10. Step 1 of section 7 (the data model split and
the migration) is built; section 2 says how it turned out. The rest is not.

Screen mock-ups of the web page: https://claude.ai/artifact/U4qVf3B3bSBHPqSUt5g6Zi

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

### Polish pass (13 Oct 2026)

The page had grown to seven sections and about 122 KB, and Setup alone was
nine cards, 110 inputs and four and a half screens of scrolling. Nothing
said that a button had been pressed. What changed, with no feature removed:

- **Every button answers.** It dims and sinks when pressed, shows a spinner
  while the controller works, then flashes green or shakes red. The message
  at the bottom is green for done and red for refused or not filled in.
- **Unsaved changes show.** Typing in a form puts a dot on that card's Save
  button until it is saved.
- **Each page says what it is for** in one line under its title.
- **Long pages have a contents menu.** Home, Grinder and Setup get an "On
  this page" button at the right of the title. It opens a list of the
  page's cards; picking one opens that card and scrolls to it. Scrolled past
  the title, the button shrinks to a round icon in the margin, clear of the
  cards. (It began as a row of chips, which took too much room.)
- **Home has no contents menu**: a page needs five cards to get one. Its
  pusher is called "Brew bot", since it need not be a SwitchBot.
- **The coffee form is in three groups**: the coffee (name, roaster, roast
  level, notes), the bag in use (roast date, freezer dates), and where it is
  from (optional). Delete sits apart from Save. "Add coffees from elsewhere"
  starts folded.
- **Forgetting a paired device asks first**, and those buttons, like "Clear
  stored log", are drawn as the destructive ones they are.
- **Long notes show their first line.** Explanations of three sentences or
  more are a one-line summary with "More"; the rest opens as a short list.
  Nothing was cut.
- **Pick lists hold only real profiles.** The machine list is the machine
  in use, the Breville the firmware was built on, and the shared library's;
  the five "Any ..." outlines are gone from it ("How a shot is run" covers
  them). It also read as one entry repeated: the reply pointed at text in
  temporaries (fixed in `handleGetMachine`). The grinder list is the models
  whose microns per step are known (E64 WS) and the shared
  library's; DF64, Mignon and Niche were names with nothing behind them.
  A grinder already set to one of those keeps it.
- **A listed profile can be changed and saved.** Every field is open after
  picking. Saving a changed machine or grinder keeps it as the owner's own
  version (and it can then be sent for review); picking the listed one again
  and saving goes back. Changing only the microns per step does not start
  grind advice afresh.
- **The machine has its own page.** "Espresso machine" sits in the menu
  between Grinder and Setup and holds Upkeep, the make and model (the card
  that was called "Espresso machine") and Machine power, all open. Setup
  keeps the scale and pushers, pusher keys, brew, network, firmware and
  sharing.
- **Rarely used cards are folded.** Setup opens with Devices;
  the rest, and the lower three cards on Grinder (the two charts stay open), open with a click on their
  title or chip and stay as the user left them (kept in the browser).
- **State is always in view.** The controller's state sits in the menu on
  every page, and a banner says so when the controller cannot be reached.
- **Phones.** Under 760 px the menu becomes a row across the top that
  stays put, and nothing scrolls sideways.
- Keyboard focus is visible, and motion is dropped for those who ask their
  system for less of it.
- **Shots is a list by day, one shot open at a time.** The ten-column table
  is gone. A row is the time, coffee and recipe, weight out, brew time and
  grind; clicking it opens that shot in place with its curve, taste, rating,
  TDS, grind setting ("change") and whether it counts for grind advice.
  Shots that did not stop on weight, or were ruled counted or ignored, carry
  a small tag. Four figures above the list sum up what is shown (shots, share
  on time, typical miss from target, rating). Twenty shots show at first,
  with "Show 20 more"; the filter gained "Brew time off target" and "Not
  rated yet". Pour time, pour flow and the end reason moved from the row to
  the opened shot; nothing was dropped.

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
3. **Built (Oct 2026): the relay.** `telemetry-relay/`, a Cloudflare Worker
   holding a GitHub App key, takes a controller's signed record, checks it and
   commits it to the library's inbox; it also serves `status/` and `index/`.
   Controllers hold no GitHub credential and owners need no account. It is the
   default; option 1 (the owner's own repository and token) stays, under
   Setup, "Use your own library repository".
   - A controller's key record goes first. The relay keeps the key and checks
     every later record's signature against it; a record whose key it does
     not have is answered 409, and the controller sends its key then.
   - A record is only taken at an inbox path carrying its own key id, so no
     controller can file under another's.
   - Limits per key per day, a block list for a bad contributor, and the
     library's own Intake and review behind it, as before.
   - Details and the setup: `telemetry-relay/README.md`, docs/TELEMETRY.md.

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
## 8a-2. Espresso machines: the shared list

A machine profile (README, "Espresso machines") travels like a grinder model.
The owner's own profile is sent as a signed `machine` record to
`machines/<key id>/m1-<record>.json` in the inbox:

    {"t":"machine","v":1,"at":...,"record":N,"local_id":1,
     "maker":"Breville","model":"Dual Boiler","control":"2 CUP button",
     "style":"toggle_button" | "start_button" | "manual" | "start_stop_buttons" | "hold_button",
     "manual_start":"tap" | "first_drip",          (manual only)
     "hold_preinfusion":0|1,"tap_preinfusion":0|1,"power_toggle":0|1,
     "self_stop_s":N}                              (if known)

`intake.py` checks it and opens a review for `machines/<maker>-<model>-
<control>.json`; `build_index.py` lists the merged entries in
`index/machines.json`, which the controller reads with the other lists and
offers on its web page. One entry is one machine by one control: the same
machine by another button is another entry.

The outcome of the review reaches the owner as it does for a coffee or a
grinder: `intake.py` writes "review" under `machines` in
`status/<key id>.json`, `reviews.py` writes "published" (with the entry's
name) or "declined" (with the reviewer's note, from a comment starting
`declined:`) when the request is closed, and the controller reads that file
while idle and shows the state on its web page.

A shared shot now also carries the machine it was made on and its ratio as
brewed:

    "machine":{"style":"manual","clock_from":"first_drip","maker":...,"model":...,"control":...},
    "result":{"yield_g":37.8,"ratio":2.1,"time_s":29.0,...}

`clock_from: first_drip` marks times that are without pre-infusion. The index
does not use these two yet: shots from different machine styles are still
fitted together.

## 8k. Subscription services (22 Oct 2026)

A subscription service travels like a machine profile: a signed `service` record in
`services/<key id>/s<local id>-<record>.json`, checked by `verify_record.py` and put up for review by
`intake.py` as `services/<slug>.json`. The reviewer checks its links and sourcing claims against the
shop's site. Its outcome is written under `services` in the contributor's status file. `build_index.py`
lists merged services in `index/services.json`, with reviews from two or more contributors.

Reviews (`service_review`, filed under `svcreviews/`) and the `service` field of coffee ratings feed
the recommendations. `taste.json` gains `services` (each service's coffees' mean taste) and `svc:<id>`
entries in the anonymous drinkers' rows. Design: docs/SUBSCRIPTIONS.md.

## 8b. Bean age

Roasted coffee goes on changing: as a bag ages, a shot runs faster at the
same grind, so its owner grinds finer. A dial-in shared from a ten-day-old
bag is the wrong number for a seven-week-old one. Three things follow.

- **The rate is measured, not assumed.** A contributor who settles the same
  coffee and recipe on the same grinder at two ages at least five days apart
  gives one measurement: the grinder's offset and the coffee's own fineness
  are the same both times, so the difference is the ageing. The library's
  index builder takes the median over all such pairs and publishes it as
  `index/age.json` (`um_per_day`, negative: finer with age), once there are
  at least three. Until then there is no rate and nobody is offered a number.
- **Community figures are for a stated age.** With a rate, each dial-in is
  moved to the age its coffee was typically settled at before the fit, and
  the entry carries that age (`age_days`). A dial-in made on an old bag no
  longer widens the spread or drags the figure.
- **A suggestion allows for this bag's age.** The starting grind is shifted
  by rate times (this bag's age less theirs), converted to the grinder's
  unit, and the panel says so: "includes 35 um finer because your bag is 53
  days off roast and theirs were about 10". Its range widens by half the
  shift. Ages beyond 90 days are treated as 90, and the shift is never more
  than 60 microns: the rate was measured on younger coffee. No roast date,
  no allowance.
- **Coming back to a coffee.** The Grinder page says how old the bag is and
  how long since a shot of it last counted. After three days or more it
  says the grind will usually need to go finer, with a number if the rate is
  known and the grinder's unit can be converted. Under five days off roast
  it warns that the coffee is still degassing. These are a guide beside the
  advice, not part of it: the brew-time advice still follows from the next
  shot.

Bean age is counted from the roast date of the bag in use. "New bag..." on
the Coffees page asks for the date printed on the bag (it used to assume the
bag was roasted today, which it hardly ever is).

Tried against the example library in the emulator: the rate came back as
-0.81 against the invented -0.8; a bag 53 days off roast was offered 214
where a bag of the contributors' age was offered 249; a bag younger than
theirs, 251; no roast date, 249. The "days since last brewed" note could
only be seen with a one-day gap, which is below where it speaks.

## 8c. The emulator and the real library

The emulator used to "send" to a folder and call that sent. It now says
"not sent" when what it wrote went no further than this PC, and it can
reach the real library:

- With OpenSSL on the PC (Git for Windows has it) the emulator has a real
  ES256 key and signs through `openssl`. Its records pass the library's
  checks like a board's.
- With a repository set on the Setup page and the GitHub CLI logged in on
  the PC, records go to that repository's inbox through `gh`, and status and
  index are read back from it. No token is typed into or stored by the
  emulator.
- It never sends measurements: its shots come from a simulated machine.
  Dial-ins and shots stay in the folder. Coffees and grinders, which its
  owner typed, go to review like anyone's.
- A controller whose key has changed forgets what it sent before, since the
  library knows nothing under the new key. "Send everything again" on the
  Setup page does the same on request and queues every coffee and grinder.

Tried on a copy: a coffee saved in the emulator reached the repository, the
Intake verified its signature and opened the pull request, the emulator
showed "in review", and after the merge "published" with its library id.

## 8d. The fit, revised (12 Oct 2026)

A review of step 8 found three things, now changed in `build_index.py`:

- **It needed a micron-marked grinder.** Fineness was seeded only from grinders
  whose microns per unit is known, so a coffee dialled in on Niche, DF64 and
  Eureka dials alone produced nothing, not even the same-model medians. Now any
  group of linked grinders is fitted. With a micron-marked one in it the axis is
  microns, as before. Without, the axis is relative (the unit of the best-covered
  model) and carries an `axis` id in the `dialin` and `fit` files; the controller
  uses a fit only on the axis of the coffee's fineness, never reads a relative
  number as microns, and makes no ageing allowance on one (the rate is in
  microns). A coffee nothing can place still gets `models` medians.
- **Scale was fitted per owner.** Now per model, from all its owners, with only
  the zero per grinder (hierarchy: model, then grinder). A new owner of a known
  model needs one coffee in common, not three; a grinder zeroed again keeps its
  scale. `index/models.json` lists each model's learned scale (and `um_per_unit`
  where the axis is microns) for reviewers to check against the maker's.
- **Per-component pinning.** Each group of linked grinders pins its own axis;
  before, one global shift was applied across unrelated groups.

**Same-model range, measured.** Each model's entry in a coffee's `models` carries a
`spread` (owners' zeros, in the model's unit: from that coffee's owners if three or
more, else from everything the model's owners have shared). The controller's range
is 1.5 times it. Without one: 15 microns on a grinder marked in them (as before);
on an unmarked dial a sixth of the setting, no longer the old claim of one unit.

**Flow normalisation.** Each dial-in carries yield and time, so its flow is yield over
time on the basis the owner steers by. After the age step the index measures a
sensitivity KAPPA (grind axis units per unit of ln flow), then moves every dial-in to
the flow its coffee was typically settled at, and refits. The coffee's entry publishes
`flow_gs`, `flow_basis` and `kappa`; the controller shifts the start by
`kappa x ln(my target flow / their flow)` (my flow: recipe yield over target time),
only on the same time basis, limited to a flow ratio of 1.65, and says so on the
page ("includes N finer because your recipe aims for a slower flow").
- KAPPA is measured from within-owner variation only (two-way within estimator: each
  coffee's level and each grinder's level removed). An owner's habitual flow cannot
  be told from where their zero sits, so it needs owners whose flow varies from
  coffee to coffee. With fewer than 8 points it is the prior of 84 microns per unit of
  ln flow (the advisor's 3 microns per second at about 28 s) on the microns axis,
  and absent on a relative axis. A measured figure outside a quarter to four times
  the prior is treated as noise. `index/flow.json` lists what each axis is using.
- Habitual flow is thus removed from the zero only through the prior or a measured
  KAPPA; the prior is a guess, flagged as such in the index.

**Nearest recipe.** The controller no longer needs the same recipe class (dose within
0.5 g, yield within 2 g). It takes the exact class if the library has it, else the
nearest within reach: 2 g of dose and a yield ratio of about 1.35 (`recipeDistance`,
0 to 2). A different yield mostly changes how long a shot runs, so the flow shift
above already corrects for it; dose is not corrected. A farther recipe is trusted
less: the range widens by 3% of the setting per step of distance, and the page says
which recipe the figures come from. The widening is an assumption, not a measurement.
Caveat: on the total-time basis the pre-infusion is a fixed overhead, so yield over
total time is not strictly proportional across yields; the pour basis is exact.

Still open: the machine is not in the fit; a coffee nobody has dialled gets nothing.
A prior from roast level or process would need enough placed coffees on the microns
axis to show a real relation, and whether one exists is not known: it should be
looked at in the library's own data once it has some, not assumed.

**Flow or microns as the common axis.** Considered and kept as microns for the
latent axis. Flow is an outcome, not a property of a grinder setting: the same
setting gives different flow for another coffee, bag age, dose, basket, puck
preparation or machine, so a setting-to-flow map would need a curve for every
grinder and coffee, where `scale x fineness + offset` needs one number each.
Flow is non-linear in grind (and clogs near the fine end), while the latent axis
is linear in the range that matters. What flow does give is a better *observation*:
pour flow (yield over time since the first drop, `pour_flow_gs`) removes
pre-infusion and, unlike time, is comparable across yields. Next step: normalise
each dial-in to a reference flow with a measured sensitivity, and match the
user's own target flow when suggesting a start.

## 8e. Ratings refine the time advice (13 Oct 2026)

The base is the Grind-by-Sync idea of section 8d's predecessor (`core/GrindAdvisor.h`): steer the grind
by how far the moving average of measured brew times is from the recipe's target time, inside a
tolerance. It is computed first, from the shots only, and it stays the forward guidance and the
default. Ratings add to it; they never replace it.

- **What a rating can do.** When the base says *keep*, and the owner has at least three shots of
  this coffee, bag, recipe and grinder calibration rated 4 or 5 stars, the median time of those is
  the time they liked. If it is inside the base's tolerance, the advice may move the grind a
  little (same sensitivity, same largest step, stopping at the liked time) to bring the average
  onto it: a tighter target range inside the base one. The state is `refining`; the answer is
  finer or coarser with `refined: true`; `base_action` still says *keep*. On the liked time already:
  `on_liked`, keep.
- **What it cannot do.** It cannot widen the range: a liked time outside the tolerance is ignored
  (`outside_band`). It cannot override the base: whenever the base says finer or coarser the advice
  is exactly the base's (`base_moving`; host test compares every field with `adviseGrind`). With
  fewer than three liked shots, or the setting off, the answer is the base's, field for field.
  The target time shown and compared stays the recipe's.
- **The tighter range** is not a constant: it is the standard error of the median of the liked
  times (1.2533 sd / sqrt n), at least 0.5 s and at most the base tolerance.
- **Settled** (what lets a dial-in go to the library) is the base verdict (`baseAction == HOLD`):
  a personal refinement does not hold a dial-in back.
- **Taste:** two or more of the last three shots at the current setting that all say sour (or all
  bitter), with none balanced, add a note: sour suggests finer, bitter coarser. It never changes
  the action.
- **Setting:** "Ratings" on the Settings page (`grindRatedTarget`, on by default); off is
  exactly the base advice. Rating a shot now refreshes the advice at once (it did not before).
- **Shown:** `/api/grind` has `base_action`, `refined`, `rated_state`
  (`off`, `too_few`, `outside_band`, `base_moving`, `refining`, `on_liked`), `rated_shots`,
  `rated_time_s`, `rated_band_s`, `aim_time_s`, `taste_hint`, `taste_shots`; the Grinder page says
  what happened and why.
- **What is and is not confirmed** (`research/assumptions-validation.md`): no source gives a
  liking threshold or a test-retest figure for coffee star ratings; sensory work shows individual
  ratings are noisy, which is why the median of several shots is used and why the base always
  leads. 4 stars (a convention) and 3 shots (the smallest sample whose median ignores one odd shot)
  are the only fixed numbers left. Mahlkoenig publishes none of its own tolerance, average length
  or step; the base's numbers are this project's settings.

## 8f. Frozen coffee (13 Oct 2026)

A bag kept in the freezer goes stale far more slowly than one on the shelf, and the ageing model
(8b) counts days off roast. Beanconqueror (`frozenDate`, `unfrozenDate`) and Visualizer
(`frozen_date`, `defrosted_date`) both keep these dates; so does this now.

- `Coffee::frozenDate` and `thawDate` (YYYYMMDD), added at the end of the record, so libraries
  saved before them read as "never frozen" (host test rebuilds an old file). Dates must be in
  order and after the roast; a thaw date with no freeze date is dropped. A thaw date of 0 with a
  freeze date means it is in the freezer now.
- **Age** (`core/Community.h`): `effectiveAgeDays` = days off roast less `(1 - FROZEN_AGING_FRACTION)`
  times the days frozen. **`FROZEN_AGING_FRACTION` is 0.10.** It is bracketed, not measured: low
  temperature slows ageing (verified), CO2 release follows an Arrhenius law (Ea about 17-18 kJ/mol
  as reported for Wang and Lim 2014, primary text not read), which makes a freezer day about a
  third of a shelf day at most (0.33); a trade-quoted "70 days at -25 C = 2 days at 35 C" (not
  traced to a primary paper) would give 0.05. 0.10 is the geometric mean of 0.03 and 0.33. The
  first version of this feature assumed 0, which the research contradicts.
- **Not modelled:** grinding straight from the freezer gives a finer grind than the same beans warm
  (Uman et al., Sci Rep 2016), so a bag ground frozen needs a coarser setting than its age alone says.
- **A new bag** (a new roast date) clears the dates, in the page and the library.
- **Shared records** carry `frozen_days` (optional, 0 to 3650) on dial-ins and shots; the index
  builder applies the same fraction before it measures the ageing rate. When there are enough
  contributors the library can measure the fraction itself (not built).
- Not covered: several freezing periods in one bag (only the last pair is kept).

## 8g. A start from the owner's own coffees (13 Oct 2026)

The community start (section 6) needs the library to know the coffee. Visualizer's "similar bag"
logic (`CoffeeBag::GrindSuggestable`) covers the coffee nobody else has dialled: it takes the user's
own dial-in of the most similar earlier coffee on the same grinder and profile and asks a language
model how the new one should be ground relative to it. `core/SimilarCoffee.h` does it with rules
and with the owner's own data, offline.

- **Neighbour:** among the owner's other coffees with a setting for a close recipe
  (`recipeDistance`, as in 8d), the one with the smallest distance on roast level, process,
  altitude and origin; at least two of those must be known on both sides; a settled setting and a
  nearer recipe break ties.
- **The number starts at the neighbour's setting and is moved only by the owner's own data.** No
  source gives the size of any factor in grinder units (`research/assumptions-validation.md`): the
  earlier fixed step of 3% of the setting was a guess and is gone. When the owner has at least
  two more dialled coffees than the factors they differ in (roast level, altitude, process; each
  must vary), a ridge regression of ln(setting) on those factors, shrunk towards "no effect",
  gives how far to move. A factor of the unexpected sign (darker coarser, higher-grown finer, natural
  coarser) is not applied.
- **Hints, not shifts.** Where the data do not speak, the page says which way the usual advice points
  ("darker than it: usually ground coarser") without moving the number; for process it says
  advice differs and nothing measured supports it (Uman et al. 2016: particle size distribution
  independent of processing method).
- **Range:** the scatter of the owner's own settings (standard deviation of ln setting, with a
  small-sample correction); 10% when there is a single coffee to go on, stated as wide on purpose;
  never under two units (10 microns on a grinder marked in them); wider for a nearer-recipe match.
- **Where it shows:** the Coffees page and the Home panel ("A guess from your own coffees", with
  the button "Use ... as my starting grind"), and on the controller's home card as `similar: try N`
  when there is no setting and no community figure. `/api/community` has a `similar` object
  (`basis` nearest or fitted, `samples`, `applied`, hints, always `verified: false`).
- **Not done:** the library measuring each factor's effect from many owners' pairs (the way it
  measures the ageing rate); the neighbour's age against this bag's; the machine.

## 8h. Bags (13 Oct 2026)

A coffee keeps every bag it has had. Until now the bag in use was only a roast date on the coffee, so "a new bag"
meant overwriting it and the earlier bag was lost.

- `Bag` (`core/Types.h`, `Library::bags`, up to 48): id, coffee, roast date, opened date, frozen and thawed
  dates, weight. `Coffee::bagId` is the one in use, and the coffee's own `roastDate`, `frozenDate` and
  `thawDate` **mirror** it, so everything that only needs "the coffee" (the advice, the community start,
  the shared records, the controller's list) reads them where it always did.
- **Editing the dates of a coffee corrects the bag in use**; a new bag is `POST /api/bag {coffeeId, roastDate,
  openedDate?}` (the page's "New bag..."). The earlier bag keeps its dates, including its time in the freezer, and can
  be put back in use (`bag_use`) or forgotten (`bag_delete`, not the one in use). When the list is full the oldest bag
  nobody has in use is dropped.
- **A shot remembers its bag** (`ShotRecord::bagId`). Grind advice compares only shots of the same bag
  (`sameBag`): by id, or, for a shot made before bags existed, by the roast date it copied. Two bags of the same roast
  date are now told apart for new shots.
- **Storage.** The library file is version 3: the bags follow the grinders. A version 1 or 2 file reads as before and
  its coffees are given bags from their dates (host test). The shot history is version 10 (the bag and a measured TDS
  added at the end of each shot): `Store::convertHistoryV9` rewrites a version 9 history shot for shot and keeps the old
  file as `/shots.v9.bak`; a reset between the two renames is recovered at the next start. Run against a copy of the real
  54-shot history, every shot read back with identical values.
- **Not done:** bags in the shared records (a record still carries the roast date and, since 8f, `frozen_days`); bag
  weight left, consumption.

## 8i. Dials that are not whole numbers (13 Oct 2026)

A setting was a whole number from 1 to 2000 in the grinder's unit; "a dial read in tenths is not supported" was
a stated limit. Now a grinder says how its dial is read (`core/SettingFormat.h`):

| reading | `divisor` | `subSteps` | stored 35 shows as |
| --- | --- | --- | --- |
| whole steps (microns, clicks, notches) | 1 | 0 | 35 |
| halves, quarters, fifths, tenths ... hundredths | 2, 4, 5, 10, 20, 25, 50, 100 | 0 | 3.5 (tenths) |
| two rings (outer position and inner step, as on a Baratza Sette) | 1 | N >= 2 | 4:8 (9 inner steps) |

- **What is stored does not change**: a whole number of the smallest step, 1 to 2000. The advice, the fit, the
  community start and the shared library all keep working in those steps; only what is shown and typed changes (the web
  page, the controller's screen, the CSV stays in stored steps). **Microns per unit is per stored step.**
- A divisor must divide 100 (so a decimal is always exact); rings and a divisor do not mix. Changing how a grinder is
  read starts a new calibration and clears the settings entered, as a change of unit does: 35 would otherwise silently
  turn from 35 into 3.5.
- Typed settings are parsed back (`3.5`, `3,5`, `12:3`, `12-3`, `12/3`) and the round trip is exact for all 2000 values in
  every format (host test and a node test of the page's own functions against the same vectors).
- Shared records carry `divisor` / `sub_steps` when they are not whole numbers; the index keeps grinders read differently
  apart (`unit_key`: `dial/d10`, `sette/r9`) so one owner's 35 is never averaged with another's 3.5.
- **Letters** (a dial marked A to K) are not supported: a ring is numbered.

## 8j. Working with other apps, TDS and upkeep (13 Oct 2026)

- **Beanconqueror hand-off.** "Send to Beanconqueror" on a shot builds Beanconqueror's own import link
  (`beanconqueror://ADD_BREW`, JSON, gzip, base64url, 400-character chunks) from the shot and its curve in the browser.
  Verified: a real shot's link (323 readings, 931 characters in 3 chunks) is accepted by Beanconqueror's real decoder and
  its flow read back (`test/host/page_interop_check.mjs` with `BC_DECODER`). Not verified on a phone: the app's display of
  the trace, and whether a mobile browser lets the controller's plain-HTTP page open the link. A shot with no date (the
  controller's clock was not set) is sent dated now, after asking.
- **Beanconqueror import** of coffees: a backup file's beans become coffees with their bags (roast, opened, weight,
  freezer dates); finished beans are skipped unless asked for; duplicates are skipped. Shots are not imported:
  another app's brews are not made with this machine and would confuse the advice.
- **visualizer.coffee lookup**: a search that fills the coffee form from its public canonical list
  (`/api/canonical_coffee_bags`, CORS open, no sign-in). Needs a phone that is online. Its names are retailers' product
  titles, so they are cut to 23 characters and need tidying; the data is community-contributed with its own terms and
  is not copied into the library.
- **TDS and extraction yield**: a refractometer reading per shot (`POST /api/shot/tds`), extraction = TDS x yield / dose,
  shown on the page, in `/api/shots` and appended to the CSV (`tds_pct`, `extraction_pct`). Not shared.
- **Upkeep counters**: shots (12 s or longer, 2 g or more) since backflush, descale and grinder clean, with optional
  reminders at an interval the owner sets (starting at backflush 200, descale off, grinder clean 500; nothing in the cup says when). `/api/maintenance`; the status
  carries `maintenance_due`.
- **Charts**: a control chart of each shot's time against the target band (with the liked time when refining), and the
  best-rated shot's curve drawn dashed behind the one being looked at.
- **API**: `/api/status` has `api_version` and `capabilities`; endpoints are listed in `docs/API.md`.

## 9. The library repository, and what is left

The library is https://github.com/hallge/Community-Coffee (private for now).
`library-template/` in this project is the copy its files were made from.

**In place and tried on GitHub itself (12 Oct 2026):**

- **Intake.** Controllers push signed records to `inbox/` on the `inbox`
  branch. A small workflow there asks `main`'s Intake workflow to run (it
  also runs daily). Intake checks every record (envelope, ECDSA P-256
  signature against a key on file, types and ranges), files keys, dial-ins
  and shots, and for each coffee or grinder opens a pull request. What fails
  is left out and listed with its reason. Records are signed, not encrypted:
  there is nothing to decrypt.
- **Review outcome.** Merging such a pull request publishes; closing it
  declines (a last comment starting `Declined:` is passed on). Either way
  the contributor's `status/<key id>.json` is updated and the index rebuilt.
- The one repository setting it needs is on: Actions may open pull requests.
  The default permission of workflow tokens was left read-only; each
  workflow asks for what it uses.
- The live check, with a throwaway key: five good records filed or put up
  for review, three bad ones refused (tampered, out of range, emulator-made)
  each for the right reason; a coffee merged came back "published" under its
  catalog id, a grinder closed with a comment came back "declined" with the
  note; review branches removed. The check's records were then taken out
  again: the library is empty.
- Branch protection is not available on a private repository on the free
  plan, so nothing stops a direct push to `main` by someone with write
  access. Made public, or on a paid plan, `main` should require a pull
  request.

**The board's side** (`platform/LibraryLink.cpp`; compiles, not yet run):

- Records go out with `PUT .../contents/inbox/<path>` on the `inbox` branch;
  `status/` and `index/` are read with `GET .../contents/<path>?ref=main`.
  Those exact requests were sent from a PC and GitHub answered as the code
  expects: 201 for a new record, 422 for one already there (counted as
  sent), the file's own text for a read, 404 for a file that is not there.
- Only GitHub is trusted: six root certificates (`platform/GithubRoots.h`,
  taken from the Mozilla bundle), the one GitHub's API uses today, its
  authority's others, and the one GitHub used before. Checked: those six
  alone verify `api.github.com`; an unrelated one alone does not. If GitHub
  ever moves to another authority the controller refuses the connection and
  records wait until a firmware with the new root is installed.
- The repository (`owner/name`) and the token are entered on the Setup page
  and kept in NVS. No page or API hands the token back. The token should be
  a fine-grained one for this repository only, Contents read and write: it
  cannot change workflow files, so it cannot change what the checks do. It
  is stored unencrypted in the board's flash; anyone holding the board
  could read it out, which is why it must be good for nothing else.
- Requests are made only by the loop task while idle; each takes a second
  or two, during which the screen does not update. The control task, which
  stops shots, never waits on them. After a failure the next try is a
  minute later, doubling up to an hour. Certificates cannot be checked
  before the clock is set, so nothing is tried until it is.
- The status file is asked for every ten minutes, the index once an hour.

**Still to do, all on the board:**

- First real run end to end with the board's own signatures. Nothing from
  the emulator can be used for it: its records are refused by design.
- The mbedTLS signer checked against the verifier.
- Free memory with Wi-Fi, Bluetooth and TLS all up (see below).

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

- GitHub REST API: repository contents; fine-grained personal access tokens
