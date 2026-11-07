# Home Assistant

The controller can work with [Home Assistant](https://www.home-assistant.io/) in five ways. Each one is off until you set it up, and none of them can hold up a shot. Settings are on the **Setup** page, in the **Home Assistant** card.

| Way | What you get | Set up on the controller |
| --- | --- | --- |
| 1. Room climate, read from Home Assistant | The room's humidity and temperature are recorded with each shot | Address, token, two entity ids |
| 2. Room climate, pushed | The same, from anything that can send a web request | Nothing |
| 3. Webhook | Each finished shot triggers an automation (a notification, a log) | Webhook id |
| 4. MQTT discovery | A Brew Controller device with sensors and a Start button | Broker address, user, password |
| 5. Polling the JSON API | Sensors from the controller's own API: a `rest:` sensor, or the custom integration | Nothing |

Use the controller's address on your network. `http://brewbyweight.local` works where mDNS works.

## Why record the room at all

No controlled study ties room humidity to espresso shot time. Published advice even disagrees on which way it pushes the grind. So the controller records the reading with each shot (in the shot's context: `ambient_rh_pct`, `ambient_temp_c`) but the grind advice does not use it yet. Once 30 or more shots cover a 15-point spread of humidity, the reading can be tested against the drift the advice already learns (`research/grind-logic-review.md`, section 8).

## 1. Read the room from Home Assistant

1. In Home Assistant, open your profile, then **Security**, and create a **long-lived access token**. Copy it; Home Assistant shows it only once.
2. Find the entity ids of a humidity sensor and a temperature sensor (**Settings → Devices & services → Entities**), for example `sensor.kitchen_humidity` and `sensor.kitchen_temperature`. Either one may be left empty.
3. On the controller, in the Home Assistant card, enter:
   - the address, with its port, for example `http://homeassistant.local:8123` (the port depends on how Home Assistant is installed);
   - the token;
   - the two entity ids.

   Then **Save**, and **Test the reading**.

The controller reads both entities every 5 minutes while it is not brewing (`GET /api/states/<entity_id>`, with the token as a bearer header). A shot records the latest reading if it is less than 30 minutes old. Temperatures in °F or K are converted to °C. A sensor that is `unavailable` or `unknown`, out of range, or in another unit is not recorded, and the card says why.

- **The token never leaves the controller.** It is not sent back to the web page or the API, and goes into a backup only when you tick "include the passwords" when you download it. Use a Home Assistant user with as few rights as you like: reading states needs no admin.
- **`.local` names** are looked up over mDNS. If that does not work on your network, use Home Assistant's IP address.
- **HTTPS** works when Home Assistant has a certificate from a public authority (for example through Nabu Casa). A self-signed certificate is refused: on your own network, use `http://`.

## 2. Push the room from anything

```
POST /api/ambient
{"rh_pct": 48.5, "temp_c": 21.2}
```

Either value may be left out. `GET /api/ambient` returns the latest reading, its age and its source (`ha` or `push`). A pushed reading is used until a newer one arrives from either source. From Home Assistant, a `rest_command` and an automation do it:

```yaml
rest_command:
  brew_controller_room:
    url: http://brewbyweight.local/api/ambient
    method: POST
    content_type: application/json
    payload: >
      {"rh_pct": {{ states('sensor.kitchen_humidity') }},
       "temp_c": {{ states('sensor.kitchen_temperature') }}}

automation:
  - alias: Brew Controller room climate
    triggers:
      - trigger: time_pattern
        minutes: "/5"
    actions:
      - action: rest_command.brew_controller_room
```

The controller's API has no password, like the rest of its web page: keep it on your home network.

## 3. A webhook after each shot

1. In Home Assistant, create an automation with a **Webhook** trigger, and copy its webhook id. Leave "only accessible from the local network" on.
2. On the controller, enter the Home Assistant address and the webhook id, then **Save**.

After each shot the controller posts:

```json
{"event": "shot",
 "shot": {"id": 127, "timestamp": "...", "shot_time_s": 31.5, "final_weight_g": 48.0, "grind_um": 64, "...": "..."},
 "advice": {"action": "hold", "suggested": 64},
 "ambient": {"rh_pct": 48.5, "temp_c": 21.5}}
```

The `shot` object is the same as one entry of `/api/shots`. In the automation, the shot is `trigger.json.shot`, for example:

```yaml
triggers:
  - trigger: webhook
    webhook_id: !secret brew_controller_webhook
    allowed_methods: [POST]
    local_only: true
actions:
  - action: notify.mobile_app_phone
    data:
      message: >
        Shot {{ trigger.json.shot.id }}: {{ trigger.json.shot.final_weight_g }} g
        in {{ trigger.json.shot.shot_time_s }} s, grind: {{ trigger.json.advice.action }}
```

The webhook id is kept like the token: not shown again, and in a backup only with the passwords.

## 4. MQTT discovery

You need an MQTT broker (the Mosquitto add-on) and Home Assistant's MQTT integration.

On the controller, enter the broker as `mqtt://homeassistant.local:1883`, plus a user and password if the broker asks for them, and **Save**. The controller then:

- connects while it is not brewing, retrying once a minute;
- announces a device, **Brew Controller**, with these entities (retained `homeassistant/.../config` messages):
  - machine state;
  - last shot time, weight and grind;
  - grind advice, and the advised grind;
  - shots today;
  - a **Start shot** button;
- publishes their values as one JSON object on `bbw/<id>/state`: on every change of state, after each shot, and every 5 minutes;
- marks itself `online` / `offline` on `bbw/<id>/availability` (offline through the broker's last will).

`<id>` is six hex digits, the same on every start.

**The Start button obeys the same switch as the web page's Start**: "Allow Start from this page" in the Network card. With it off, a press is logged and ignored. Starting a shot needs a cup on the scale and the machine ready, as always.

## 5. Polling the controller's JSON API

With nothing set up on the controller, Home Assistant can read its API. A `rest:` sensor:

```yaml
rest:
  - resource: http://brewbyweight.local/api/status
    scan_interval: 30
    sensor:
      - name: Brew Controller state
        value_template: "{{ value_json.state }}"
      - name: Brew Controller weight
        value_template: "{{ value_json.weight_g }}"
        unit_of_measurement: g
  - resource: http://brewbyweight.local/api/grind
    scan_interval: 120
    sensor:
      - name: Brew Controller grind advice
        value_template: "{{ value_json.action }}"
        json_attributes: [suggested_um, error_s, drift_s_per_day, shot_noise_s]
```

There is also a custom integration in `integrations/homeassistant` (copy `custom_components/brew_by_weight` into Home Assistant's `config/custom_components`, restart, and add **Brew by Weight**). See its README.

## 6. The Brew Controller panel, and the controller's own page

The custom integration (from 0.3.0) adds a **Brew Controller** panel to Home Assistant's sidebar: the
controller's interface rebuilt in Home Assistant's style, with every feature that does not operate the
hardware (shots, insights, coffees, recipes, grinder, upkeep, subscriptions, discover, community,
settings, logs). It works in the Home Assistant app and away from home. What it may ask the controller
is an allowlist; hardware actions are refused and stay in the controller's own page. See
`integrations/homeassistant/README.md`.

### The controller's own page as a full-screen panel

A small Home Assistant add-on (`integrations/homeassistant/addon/brew_controller`) shows the
controller's own page in the sidebar through Home Assistant's ingress, the way Music Assistant
appears. Home Assistant's sidebar folds into a slide-over drawer (the round button at the bottom left
opens it), so the page is not shown with two sidebars side by side. Nothing is rebuilt: it is the
controller's page, so a firmware update changes it with no change in the add-on. Because Home
Assistant proxies it, it also works away from home and in the Home Assistant app over HTTPS, with
Home Assistant's login in front of the open API. Install and options: its `README.md`
(copy to `/addons/brew_controller`, set **controller** to the controller's address, use an IP address).

Next to it, a native dashboard (a Home Assistant `sections` view of the custom integration's entities)
covers the daily view and works anywhere Home Assistant does: machine state, scale, cup and brew bot,
recipe and target, maintenance due, shots today, the last shot in words, the grind advice, 7-day graphs
of shot time, weight and grind setting, and a tile that opens the full panel.

## The API

| Endpoint | Method | What |
| --- | --- | --- |
| `/api/homeassistant` | GET | The settings without the secrets (`token_set`, `webhook_set`, `mqtt_password_set`), `mqtt_connected`, the last `problem`, `last_webhook_code`, and the latest `ambient` reading |
| `/api/homeassistant` | POST | `url`, `token`, `rh_entity`, `temp_entity`, `webhook_id`, `mqtt_url`, `mqtt_user`, `mqtt_password`. Empty secrets keep the stored ones; `clear_token`, `clear_webhook` and `clear_mqtt_password` remove them. No restart |
| `/api/ambient` | GET / POST | The latest room reading / push one |
| `/api/ambient/test` | GET | One read from Home Assistant now: `ok`, `problem`, `reading` |
