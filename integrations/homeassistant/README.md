# Brew by Weight for Home Assistant

A custom integration that reads the Brew Controller's web API and shows the machine state, the last
shot and the grind advice as sensors, plus a "Start shot" button. It polls every 30 seconds.

## Install

1. Copy `custom_components/brew_by_weight` into your Home Assistant `config/custom_components/` folder.
2. Restart Home Assistant.
3. Settings > Devices & services > Add integration > "Brew by Weight". The controller is usually found
   by itself (mDNS name `brewbyweight`); otherwise enter its address, for example
   `http://brewbyweight.local`.

## Entities

| Entity | From |
|---|---|
| Machine state | `/api/status` `state` |
| Last shot time, weight, grind setting | newest shot of `/api/shots?limit=1` (`shot_time_s`, `final_weight_g`, `grind_um`) |
| Advised grind setting, Grind advice | `/api/grind` (`suggested_um`, `action`) |
| Shot noise, Grind drift | `/api/grind` (`shot_noise_s`, `drift_s_per_day`) |
| Last shot humidity, ambient temperature | newest shot (`ambient_rh_pct`, `ambient_temp_c`) |
| Start shot (button) | `POST /api/cmd?c=start` |

A value the firmware does not send (older firmware, or nothing known yet) makes that sensor
unavailable. The grind sensors show a setting number only when the controller has one.

Start shot works only when "Start from the web page" is switched on in the controller's Network
section; otherwise pressing the button shows the controller's own message as an error.

While a shot runs the controller refuses `/api/shots`; the last known shot values are kept until the
next poll.

## Tests

```
.venv\Scripts\python.exe -m pytest integrations/homeassistant/tests
```

The tests cover the JSON mapping only and need no Home Assistant installation.
