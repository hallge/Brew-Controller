# Brew panel contract

The Brew panel is a Home Assistant custom panel served by the `brew_by_weight` integration. It
re-implements every feature of the controller's own page (`src/net/WebPage.h`) that does not operate
the hardware, in Home Assistant's look, inside Home Assistant's normal sidebar. Hardware actions stay
in the controller page (the add-on panel at `/local_brew_controller`); the panel links there.

## What is in and out

- In: everything the proxy allows (`../proxy.py`): data, records, library, ratings, advice, history,
  exports, subscriptions, community, sharing, diagnostics, logs, grind-advice settings.
- Out: start/stop/tare/ack, power presses and schedule, pairing and device tests, pusher keys, Wi-Fi,
  firmware install/boot/upload/config, restore, machine make/model changes, brew and press timing
  settings, power-bot and scale-sleep settings. Where the page has such a control, the panel shows the
  state read-only (if useful) and an "Open the controller" link instead.
- Behaviour and maths are ported from `WebPage.h`, not invented: on-time rule, typical miss, taste
  lean, ratio maths, grind formatting, freezer ageing, chart scales. Cite the WebPage.h line in a
  comment where a formula comes from.

## Backend interface (implemented by the integration, faked by `dev/serve.py`)

Websocket commands (all need an authenticated user; POST requests need an admin):

| type | fields | result |
|---|---|---|
| `brew_by_weight/entries` | | `[{entry_id, title, host}]` |
| `brew_by_weight/request` | `entry_id`, `method` (`GET`/`POST`), `path` (`/api/...`), `query` (object, optional), `body` (any JSON, optional) | `{status, json}` for JSON answers, `{status, text, headers}` for text (`/api/log` sets `headers["x-log-next"]`) |
| `brew_by_weight/subscribe_status` | `entry_id` | subscription; each event is `{status: <the /api/status object>}` or `{error: "..."}`, about once a second |

A refused request (proxy) answers `{status: 403, json: {ok: false, error: "<why>"}}`.
A controller error passes through as `{status: 4xx/5xx, json: {ok: false, error: "..."}}`.
Unreachable controller: `{status: 502, json: {ok: false, error: "The controller could not be reached"}}`.

Downloads (CSV, log, backup): authenticated GET
`/api/brew_by_weight/download/<entry_id>?kind=shots_csv|shot_csv|log|backup[&id=N][&history=0|1][&secrets=0|1]`
(`backup` and `secrets=1` need an admin). Use `api.download(kind, params, filename)`.

## Frontend modules

- `brew-panel.js`: the shell (toolbar with `ha-menu-button`, tab bar, hash routing `#<tab>[/<arg>]`,
  per-user tab toggles, toast, "Open the controller" link). Owned by the coordinator.
- `lib/api.js`: `BrewApi` (get, post, text, subscribeStatus, download). Owned by the coordinator.
- `lib/format.js`: formatters ported from WebPage.h (grind setting display/parse, dates, ages,
  freezer ageing, money, ago). Owned by the coordinator; ask before changing signatures.
- `lib/styles.js`: shared CSS (`sharedStyles`) using Home Assistant's CSS variables only, and the
  `html` escaping helper. Owned by the coordinator.
- `lib/section.js`: `BrewSection` base class and the shared `store` (cached loads). Owned by the
  coordinator.
- `lib/charts.js`: canvas/SVG charts (shot curve, compare, radar, scatter, heat grid, calendar).
  Owned by thread T2. T3 may import it but not edit it.
- `lib/forms.js`: form fields, dialogs, confirm. Owned by thread T3. T2 may import it but not edit it.
- `sections/<id>.js`: one module per tab. Each module:
  - exports `meta = {id, title, icon, optional}` (`optional: true` for tabs the user can hide:
    insights, subscriptions, discover, community, logs);
  - defines a custom element named `brew-<id>` that extends `BrewSection`.

### `BrewSection` (lib/section.js)

Properties set by the shell before `connectedCallback`: `hass`, `api` (BrewApi), `store`, `panel`
(the shell: `panel.toast(text, ok)`, `panel.navigate(tab, arg)`, `panel.openController()`,
`panel.narrow`), `arg` (the part after `/` in the hash).

Override `async load()` (fetch what the tab needs, via `this.store` where shared) and `render()`
(set `this.shadowRoot.innerHTML`, then wire events). Call `this.refresh()` after a write.
`onStatus(status)` is called with each live status while the tab is shown (Home uses it).
`this.post(path, body, okText)` posts, shows a toast with the controller's answer, and returns the
answer (`{ok:false,error}` never throws).

### `store` keys (lib/section.js)

`settings`, `library`, `grind`, `shots` (500 newest), `maintenance`, `ratings`, `share`, `subs`,
`start`, `power`, `machine`. `await store.get(key)` loads once and caches; `store.invalidate(key...)`
after a write; the shell invalidates `shots`, `library`, `grind` when `status.last_shot_id` changes.

## Testing (threads)

Run an emulator on your own ports with a copy of the data, never the user's 8080/8090 emulator or
`emu/data` itself:

```
python emu/build.py                      # builds .pio/emu/bbw_emu.exe (only if you changed firmware; usually not)
copy the exe and emu/data to your own temp folder (so the user's running emulator and its data are untouched), then:
<temp>\bbw_emu.exe --data=<temp>\data --ui=emu\ui --web-port=81xx --emu-port=82xx
.venv\Scripts\python.exe integrations/homeassistant/custom_components/brew_by_weight/frontend/dev/serve.py --controller http://127.0.0.1:81xx --port 83xx
```

Ports: coordinator 8181/8281/8381, T1 8182/8282/8382, T2 8183/8283/8383, T3 8184/8284/8384. Then open `http://127.0.0.1:83xx/` in the
built-in browser: the harness loads the real panel with a fake `hass`, and `serve.py` enforces
`proxy.py`. Check light and dark (harness toggle) and a narrow width.
