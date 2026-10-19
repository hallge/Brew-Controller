# Safety statement

> **DRAFT, 13 Oct 2026**, for the project owner and a lawyer to review before anything is published. It is modelled on the
> boundary Open Brew by Weight states in its `PROJECT_CHARTER.md`. It is not legal advice.

## What this is

Firmware and a web page for an ESP32-S3 board that watches a Bluetooth scale and has a Bluetooth button pusher press a button on an espresso
machine to stop a shot at a target weight. **It is a hobby project, not a certified or plug-and-play appliance.** It has not been
tested by any safety body, it comes without warranty (see `LICENSE`), and **it has not yet been run on real hardware**
(`README.md`, "Status").

## How it is built to fail safe

* **It only presses the machine's own buttons.** Nothing is wired into the machine, its mains circuit or its firmware, and the machine's own
  time-out still applies (`TimeLimit::MACHINE`). Taking the pusher off restores the machine as it was.
* **A lost scale or a lost pusher stops the controller acting**, it does not leave it guessing: the shot is stopped (or, if the pusher is the part that is
  gone, the owner is told the shot must be stopped by hand: `manual_stop_needed`).
* **Delivery of a command is not the same as the machine doing it.** The pusher's reply says the arm moved; the controller then watches
  the scale for the flow to fall and presses again if it did not (`stop_confirmed`, `stop_retried`). These paths are simulated in the host tests (missed stop,
  lost acknowledgement, link down at the stop, an arm left down); they have not been measured on a machine.
* **Remote start is off by default**, and the web API has no authentication (`docs/API.md`): use it on a network you trust.

## What you must still do

* Be present while a shot runs. The controller is an aid to stopping at a weight, not a safety device. A toggle-style machine that was started and
  cannot be stopped (a flat pusher battery, a lost link) keeps running until you press its button.
* Keep the scale, pusher and machine dry and the machine's descaling and cleaning up to date. The upkeep counters count; they do not know.
* Check the first shots by hand (`README.md` section 7 lists what to look for) before leaving it to itself.

## Before publishing

The owner decides, with advice: this statement's wording and where it appears (the README's first screen, the web page's Setup), a hardware licence for
any published plans (the research suggests CERN-OHL-S-2.0 or CC BY-SA 4.0 rather than any non-commercial licence), and the liability disclaimer for a
device that presses a button on a mains-powered appliance.
