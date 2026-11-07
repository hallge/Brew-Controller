"""Services for automations. Each one becomes a controller request checked by the proxy rules."""
from __future__ import annotations

from typing import Any

import voluptuous as vol
from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import config_validation as cv

from .api import service_request
from .const import DOMAIN, loaded_entries

ENTRY = vol.Optional("entry_id")


SCHEMAS = {
    "set_grind": vol.Schema({ENTRY: cv.string, vol.Required("setting"): vol.All(vol.Coerce(int), vol.Range(1, 2000))}),
    "rate_shot": vol.Schema({ENTRY: cv.string, vol.Optional("shot_id"): vol.Coerce(int),
                             vol.Required("rating"): vol.All(vol.Coerce(int), vol.Range(0, 5)),
                             vol.Optional("taste", default=0): vol.All(vol.Coerce(int), vol.Range(0, 3))}),
    "set_shot_tds": vol.Schema({ENTRY: cv.string, vol.Optional("shot_id"): vol.Coerce(int),
                                vol.Required("tds"): vol.All(vol.Coerce(float), vol.Any(0, vol.Range(0.5, 25)))}),
    "bag_finished": vol.Schema({ENTRY: cv.string, vol.Optional("coffee_id"): vol.Coerce(int)}),
    "subscription_arrived": vol.Schema({ENTRY: cv.string, vol.Required("subscription_id"): vol.Coerce(int)}),
    "subscription_skip": vol.Schema({ENTRY: cv.string, vol.Required("subscription_id"): vol.Coerce(int)}),
    "maintenance_done": vol.Schema({ENTRY: cv.string,
                                    vol.Required("job"): vol.In(["backflush", "descale", "grinder_clean"])}),
}


def async_register(hass: HomeAssistant) -> None:
    async def handle(call: ServiceCall) -> None:
        entries = loaded_entries(hass)
        if call.data.get("entry_id"):
            entries = [e for e in entries if e.entry_id == call.data["entry_id"]]
        if not entries:
            raise HomeAssistantError("No Brew by Weight controller is set up")
        coordinator = entries[0].runtime_data
        try:
            path, query, body = service_request(call.service, dict(call.data), coordinator.data or {})
        except ValueError as err:
            raise HomeAssistantError(str(err)) from err
        await coordinator.async_write(path, body, query)

    for name, schema in SCHEMAS.items():
        if not hass.services.has_service(DOMAIN, name):
            hass.services.async_register(DOMAIN, name, handle, schema=schema)
