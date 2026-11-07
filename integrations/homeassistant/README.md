# Brew by Weight for Home Assistant

A custom integration for the Brew Controller. It shows the machine and the last shot as entities,
adds services for automations, and puts a **Brew Controller** panel in Home Assistant's sidebar: the
controller's interface rebuilt in Home Assistant's own style, with every feature that does not operate
the hardware.

## Install

1. Copy `custom_components/brew_by_weight` into your Home Assistant `config/custom_components/` folder
   (the `frontend/dev` folder is only for development and can be left out).
2. Restart Home Assistant.
3. Settings > Devices & services > Add integration > "Brew by Weight". The controller is usually found
   by itself (mDNS name `brewbyweight`); otherwise enter its address, for example
   `http://brewbyweight.local`.

## The Brew Controller panel

Tabs: Home, Shots, Insights, Coffees, Recipes, Grinder, Machine, Subscriptions, Discover, Community,
Settings, Logs (the optional ones can be hidden per Home Assistant user in Settings). It works in the
Home Assistant app and away from home, because Home Assistant passes each request on to the
controller (websocket commands `brew_by_weight/request`, `brew_by_weight/subscribe_status`).

What the panel may ask the controller is an allowlist (`proxy.py`): records, library, ratings,
advice, history, exports, subscriptions, community, sharing, diagnostics, logs and the grind-advice
settings. Anything that operates the hardware is refused: start/stop/tare, power presses and the
schedule, pairing and test presses, pusher keys, Wi-Fi, firmware, restore, the machine profile, brew and
press timing. Those stay in the controller's own page (the "Controller Page" add-on, or
`http://brewbyweight.local`); the panel links there. Changes need a Home Assistant administrator.

The interface between the panel and the integration is described in `frontend/CONTRACT.md`. The
panel is plain JavaScript modules (no build step); `frontend/dev/serve.py` runs it against an emulator
copy with a fake Home Assistant for development.

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
| Start shot (button) | `POST /api/cmd?c=start` (only when "Allow Start from this page" is on in the controller) |

`/api/status` is read every 5 seconds; the rest every 60 seconds, and at once after a new shot. Each
new shot also fires the event `brew_by_weight_shot` (id, coffee, recipe, final and target weight, time,
target time, grind, end reason).

## Services

`brew_by_weight.set_grind`, `rate_shot`, `set_shot_tds`, `bag_finished`, `subscription_arrived`,
`subscription_skip`, `maintenance_done` (fields in `services.yaml`; a shot or coffee left out means the
newest shot or the coffee in use). They go through the same allowlist as the panel.

A value the firmware does not send (older firmware, or nothing known yet) makes that entity
unavailable. While a shot runs the controller refuses `/api/shots`; the last known shot values are kept.

## Tests

```
.venv\Scripts\python.exe -m pytest integrations/homeassistant/tests
```

The tests cover the pure modules (JSON mapping, the proxy's allowlist, services, machine states against
`src/core/Types.h`) and need no Home Assistant installation.
