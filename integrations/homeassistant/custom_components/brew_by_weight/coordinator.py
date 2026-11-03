"""Polls the controller every 30 s."""
from __future__ import annotations

import logging
from typing import Any

import aiohttp
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import build_snapshot, error_message
from .const import DOMAIN, REQUEST_TIMEOUT_S, SCAN_INTERVAL

_LOGGER = logging.getLogger(__name__)

SHOT_KEYS = ("last_shot_time_s", "last_shot_weight_g", "last_shot_grind", "ambient_rh_pct", "ambient_temp_c")
GRIND_KEYS = ("advised_grind", "advised_action", "shot_noise_s", "drift_s_per_day")


class BrewCoordinator(DataUpdateCoordinator[dict[str, Any]]):
    def __init__(self, hass: HomeAssistant, host: str) -> None:
        super().__init__(hass, _LOGGER, name=DOMAIN, update_interval=SCAN_INTERVAL)
        self.host = host
        self._session = async_get_clientsession(hass)

    async def _get(self, path: str, optional: bool = False) -> Any:
        timeout = aiohttp.ClientTimeout(total=REQUEST_TIMEOUT_S)
        try:
            async with self._session.get(self.host + path, timeout=timeout) as resp:
                if resp.status != 200:
                    if optional:
                        return None  # e.g. /api/shots answers 409 while a shot runs
                    raise UpdateFailed(f"{path} answered {resp.status}")
                return await resp.json(content_type=None)
        except (aiohttp.ClientError, TimeoutError, ValueError) as err:
            if optional:
                return None
            raise UpdateFailed(f"{path}: {err}") from err

    async def _async_update_data(self) -> dict[str, Any]:
        status = await self._get("/api/status")
        shots = await self._get("/api/shots?limit=1", optional=True)
        grind = await self._get("/api/grind", optional=True)
        data = build_snapshot(status, shots, grind)
        # A failed optional call must not blank the last known values.
        if self.data:
            if shots is None:
                data.update({k: self.data.get(k) for k in SHOT_KEYS})
            if grind is None:
                data.update({k: self.data.get(k) for k in GRIND_KEYS})
        return data

    async def async_start_shot(self) -> None:
        timeout = aiohttp.ClientTimeout(total=REQUEST_TIMEOUT_S)
        try:
            async with self._session.post(
                self.host + "/api/cmd", params={"c": "start"}, timeout=timeout
            ) as resp:
                try:
                    body = await resp.json(content_type=None)
                except ValueError:
                    body = None
                if resp.status != 200:
                    raise HomeAssistantError(
                        error_message(body, f"The controller answered {resp.status}")
                    )
        except (aiohttp.ClientError, TimeoutError) as err:
            raise HomeAssistantError(f"Could not reach the controller: {err}") from err
        await self.async_request_refresh()
