"""Config flow: a host URL typed in, or found by zeroconf; checked with GET /api/status."""
from __future__ import annotations

from typing import Any

import aiohttp
import voluptuous as vol
from homeassistant.config_entries import ConfigFlow, ConfigFlowResult
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo

from .api import normalize_host
from .const import CONF_HOST, DEFAULT_HOST, DOMAIN, REQUEST_TIMEOUT_S


async def _validate(hass, host: str) -> bool:
    session = async_get_clientsession(hass)
    try:
        async with session.get(
            host + "/api/status", timeout=aiohttp.ClientTimeout(total=REQUEST_TIMEOUT_S)
        ) as resp:
            body = await resp.json(content_type=None)
            status = resp.status
    except (aiohttp.ClientError, TimeoutError, ValueError):
        return False
    return status == 200 and isinstance(body, dict) and "state" in body


class BrewConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._host = ""

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            host = normalize_host(user_input[CONF_HOST])
            if not host:
                errors["base"] = "invalid_host"
            elif not await _validate(self.hass, host):
                errors["base"] = "cannot_connect"
            else:
                await self.async_set_unique_id(host)
                self._abort_if_unique_id_configured()
                return self.async_create_entry(title="Brew by Weight", data={CONF_HOST: host})
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({vol.Required(CONF_HOST, default=DEFAULT_HOST): str}),
            errors=errors,
        )

    async def async_step_zeroconf(self, discovery_info: ZeroconfServiceInfo) -> ConfigFlowResult:
        host = normalize_host(discovery_info.host)
        await self.async_set_unique_id(host)
        self._abort_if_unique_id_configured()
        if not await _validate(self.hass, host):
            return self.async_abort(reason="cannot_connect")
        self._host = host
        return await self.async_step_confirm()

    async def async_step_confirm(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        if user_input is not None:
            return self.async_create_entry(title="Brew by Weight", data={CONF_HOST: self._host})
        self._set_confirm_only()
        return self.async_show_form(step_id="confirm", description_placeholders={"host": self._host})
