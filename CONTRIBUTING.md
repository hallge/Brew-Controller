# Contributing

> **DRAFT, 13 Oct 2026.** Written to be reviewed by the project owner and, before the project goes public, by a lawyer
> (see `research/license-and-risk-register.md`, "Unresolved legal questions"). It is not legal advice and nothing here binds
> anyone until the owner adopts it.

## What this project is licensed under

The firmware, the web page, the drivers and the library tools are **AGPL-3.0-or-later** (`LICENSE`). They include unmodified files
from Open Brew by Weight (AGPL-3.0) and, through it, Tate Mazer's AcaiaArduinoBLE (MIT); see `README.md` section 12. A contribution to
the code is made under the same licence.

## Sign-off (Developer Certificate of Origin)

Every commit carries a `Signed-off-by: Your Name <you@example.com>` line (`git commit -s`). It says, as the
[DCO 1.1](https://developercertificate.org/) puts it, that you wrote the change or have the right to submit it under this
project's licence. No copyright is assigned: you keep yours.

## What you must not contribute

* Code, schemas, text or data copied from a project whose licence does not allow it here. In particular: **Gaggiuino** (CC BY-NC 4.0) and
  **GaggiMate** (CC BY-NC-SA 4.0) are non-commercial and are not compatible; **Caliburr** has no licence at all.
* Anything taken from an app or site in breach of its terms (scraping, or reading a backend with a key found in an app).
* Credentials, tokens, device keys, or anyone's personal data.
* Measurements of unknown origin. A grinder's setting-to-microns figure needs a source (the maker's manual, with its URL).

## Data you send to the shared library

Shared records (coffees, grinders, machines, settled dial-ins, optionally shots) are described in `docs/DATA-TERMS.md` (also a draft).
Nothing is sent until the owner switches sharing on in Setup, and each kind has its own switch.

## Tests

* `test/host/run.sh` must pass (it compiles the portable logic and runs it; no board needed);
* the four `library-template/tools/*.py --self-test` must pass;
* `node test/host/page_settings_check.mjs` and `page_interop_check.mjs` for changes to the web page;
* a change that touches the Breville MANUAL-button path, the Lunar path or the SwitchBot path must keep their existing host tests unchanged
  (`testPusherLinkKeepsSwitchBot`, `testScaleLinkKeepsLunarData`, `testMachineProfiles`).

## Safety

Read `docs/SAFETY.md` before changing anything that presses a button or reads a scale.
