# Brew by Weight for Home Assistant

A custom integration for the Brew Controller. It shows the machine, the last shot, the bean shelf,
subscriptions and upkeep as entities, adds services for automations, and puts a **Brew Controller**
summary app in Home Assistant's sidebar. **Nothing is set up in Home Assistant**: coffees, recipes,
the grinder, settings, sharing, pairing and the power schedule stay in the controller's own page,
which the panel links to.

## Install

1. Copy `custom_components/brew_by_weight` into your Home Assistant `config/custom_components/` folder
   (the `frontend/dev` folder is only for development and can be left out).
2. Restart Home Assistant.
3. Settings > Devices & services > Add integration > "Brew by Weight". The controller is usually found
   by itself (mDNS name `brewbyweight`); otherwise enter its address, for example
   `http://brewbyweight.local`.

## The Brew Controller panel

**Summary** is the home screen. Its cards:
- **Now**: state, weight, time and flow, alerts.
- **Last shot**: weight against the shot's own stop target, time against the target, the curve, and
  taste, rating and TDS.
- **Today**: shots, how many ran on time (within the grind tolerance), and the average time.
- **Grind advice**: the setting and the advice. The **room** correction (`/api/grind` `room`), the
  **grind reminder**, the new-bag recommendation (Use / Keep), and a grinder add-on's last dose.
- **Machine power**: what the controller believes, the last press, the next scheduled switch-on and
  -off, and the schedule's entries. All read-only.
- **Links**: scale, brew bot, power bot, add-ons.
- **Bean shelf**: the bag in use (age, freezer), unopened bags, bags on their way.
- **Subscriptions**: what comes next, supplies left, monthly spend.
- **Maintenance**: each job against its interval.
- **Coffee ratings**: the newest three, liked best, buy again.
- **Getting started**: while setup is incomplete.
- **Controller**: firmware, uptime, Wi-Fi, the room reading, a crash notice.

The other tabs are read-only drill-ins: **Shots**, **Insights**, **Coffees** (the shelf) and
**Subscriptions**. Insights and Subscriptions can be hidden per Home Assistant user (the tune button
in the toolbar). Bookmarks to the 0.3 tabs open the Summary.

**The only writes the panel makes** are the everyday actions:
- rate a shot, its TDS, preparation and vote;
- "I set the grinder to ..." and Use / Keep a recommendation;
- bag finished, open a bag, a bag arrived;
- a delivery arrived or skipped;
- an upkeep job done.

The allowlist in `proxy.py` refuses everything else, and anything that operates the hardware is
refused as before. The entities and services may also choose the active preset (the select entity).
Changes need a Home Assistant administrator.

It works in the Home Assistant app and away from home, because Home Assistant passes each request on
to the controller (websocket commands `brew_by_weight/request`, `brew_by_weight/subscribe_status`).
The interface between the panel and the integration is described in `frontend/CONTRACT.md`. The panel
is plain JavaScript modules (no build step). `frontend/dev/serve.py` runs it against an emulator copy
with a fake Home Assistant for development.

## Entities

| Entity | From |
|---|---|
| Machine state (raw, as automations compare it), Machine status (the same in words) | `/api/status` `state` |
| Scale connected, Cup present, Brew bot connected (binary sensors) | `/api/status` |
| Brew bot battery, Maintenance due, Target weight, Recipe | `/api/status` |
| Last shot time, weight, grind setting, humidity, ambient temperature | newest shot of `/api/shots` |
| Advised grind setting, Grind advice, Shot noise, Grind drift | `/api/grind` |
| Grind recommendation (with source), Grind reminder (binary sensor) | `/api/grind` |
| Active preset (select: coffee and recipe the controller brews) | `/api/library`, writes `preset_select` |
| Coffee in use, Bag age | `/api/library` |
| Shots total, Shots since backflush / descale / grinder clean, Backflush / Descale / Grinder clean due | `/api/maintenance` |
| Subscriptions due soon (attribute `next`) | `/api/subscriptions` |
| Backflush done, Descale done, Grinder cleaned, Use grind recommendation, Keep current grind (buttons) | `POST /api/maintenance`, `/api/grind/recommendation` |
| Shots today (attributes: average time, on time), Last shot stop target, Last shot rating | `/api/shots` |
| Room effect on brew time (attributes: s per °C, room now, learned, shots) | `/api/grind` `room` |
| Machine power, Power bot paired (binary sensors; attributes: last press, next on, next off) | `/api/power` |
| Scale battery, Scale resting (binary) | `/api/status` |
| Unopened bags, Bags on the way, Bag in the freezer (binary) | `/api/library` `bags` |
| Subscriptions per month (Home Assistant's currency), Supplies left (attribute per supply), Next delivery (date) | `/api/subscriptions` |
| Last coffee rating (attributes: coffee, drinker) | `/api/coffee/ratings` |
| Firmware version (diagnostic, read hourly) | `/api/update` |
| Start shot (button) | `POST /api/cmd?c=start` (only when "Allow Start from this page" is on in the controller) |

`/api/status` is read every 5 seconds; the rest every 60 seconds, and at once after a new shot. Each
new shot also fires the event `brew_by_weight_shot` (id, coffee, recipe, final and target weight, time,
target time, grind, end reason).

## Services

`brew_by_weight.set_grind`, `rate_shot`, `set_shot_tds`, `bag_finished`, `subscription_arrived`,
`subscription_skip`, `maintenance_done` (fields in `services.yaml`; a shot or coffee left out means the
newest shot or the coffee in use). They go through the same allowlist as the panel.

## API gaps (controller v0.1.30)

What the summary would like from the controller's API and works around today:
1. **No day summary.** Shots today are counted from `/api/shots` (50 for the entities, 500 in the
   panel). A `?since=` or a `today` block in `/api/status` would be cheaper.
2. **No next power event.** The schedule maths (days bitmask, time zone) is repeated in `api.py`
   `next_power_events` and `frontend/lib/schedule.js`. `/api/power` could send `next: {on, t}`.
3. **No firmware version in `/api/status`.** It comes from `/api/update`, or `/api/diagnostics` in
   the panel.
4. **`/api/coffee/ratings` has no `?limit=`.** The summary needs only the newest few.
5. **The grind `room` block has no words.** The panel words it itself, which may drift from the
   controller screen.
6. **No partial subscription update.** `POST /api/subscriptions` replaces the record. This matters
   only if Home Assistant ever updates subscriptions itself.

A value the firmware does not send (older firmware, or nothing known yet) makes that entity
unavailable. While a shot runs the controller refuses `/api/shots`; the last known shot values are kept.

## Tests

```
.venv\Scripts\python.exe -m pytest integrations/homeassistant/tests
```

The tests cover the pure modules (JSON mapping, the proxy's allowlist, services, machine states against
`src/core/Types.h`) and need no Home Assistant installation.
