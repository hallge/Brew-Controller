# Brew panel contract

The Brew panel is a Home Assistant custom panel served by the `brew_by_weight` integration. From
0.4.0 it is a **summary app**: the Summary tab, then read-only Shots, Insights, Coffees (the shelf)
and Subscriptions. Nothing is set up in Home Assistant. Setup and hardware stay in the controller's
own page (the add-on panel at `/local_brew_controller`, or the controller's address), and the panel
links there.

## What is in and out

- In:
  - reading everything the proxy allows (`../proxy.py` `GET_ALLOWED`);
  - the everyday actions: rate a shot, its TDS, preparation and vote; set the advised grind; Use /
    Keep a recommendation; bag finished, opened or arrived; a delivery arrived or skipped; an upkeep
    job done.
- Out (refused by the proxy):
  - every other write: coffees, bags, recipes, grinder, settings, drinkers, sharing, the Home
    Assistant link, diagnostics, logs, upkeep intervals, history, shot grind corrections, preset
    choice (entities only);
  - everything that operates the hardware.
- Behaviour and maths are ported from `WebPage.h`, not invented. Cite the WebPage.h line in a
  comment where a formula comes from. The schedule rule in `lib/schedule.js` mirrors `api.py`
  `next_power_events`, which is tested.

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

- `brew-panel.js`: the shell. Toolbar with `ha-menu-button`, the state pill, a per-user tabs menu and
  "Open the controller"; the tab bar; hash routing `#<tab>[/<arg>]` (0.3 tab names land on
  `#summary`); toast. A tab's module is `sections/<file or id>.js`, defining `brew-<file or id>`.
- `lib/api.js`: `BrewApi` (get, getOr, post, text, subscribeStatus, download).
- `lib/format.js`: formatters ported from WebPage.h (grind setting display, dates, ages, freezer
  ageing, money, ago).
- `lib/styles.js`: shared CSS (`sharedStyles`), Home Assistant's CSS variables only; `html` escaping.
- `lib/section.js`: the `BrewSection` base class and the shared `store` (cached loads).
- `lib/charts.js`, `lib/shotview.js`: charts, the shot curve and the rate row.
- `lib/advice.js`: grind advice in words. `lib/schedule.js`: next power events, day and time text.
- `lib/forms.js`: confirm and form dialogs. `lib/upkeep.js`: job names.
- `sections/summary.js` (Summary), `shots.js`, `insights.js`, `shelf.js` (tab `coffees`),
  `deliveries.js` (tab `subscriptions`). Each exports `meta` and defines its element.

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
