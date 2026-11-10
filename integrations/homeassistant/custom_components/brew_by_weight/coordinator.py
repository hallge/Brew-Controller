"""Polls the controller: /api/status every 5 s, the rest every 60 s or at once after a new shot."""
from __future__ import annotations

import asyncio
import logging
import time
from datetime import date, timedelta
from typing import Any

from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from . import proxy
from .api import (build_extra, build_snapshot, build_summary, error_message, normalize_host,
                  room_effect, shot_summary)
from .client import ControllerClient
from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)

STATUS_INTERVAL = timedelta(seconds=5)
SLOW_INTERVAL_S = 60
UPDATE_INTERVAL_S = 3600   # the firmware version changes only with an update
SHOT_KEYS = ("last_shot_time_s", "last_shot_weight_g", "last_shot_grind", "ambient_rh_pct", "ambient_temp_c")
GRIND_KEYS = ("advised_grind", "advised_action", "shot_noise_s", "drift_s_per_day", "room_shift_s", "room_attrs")
EVENT_SHOT = f"{DOMAIN}_shot"


class BrewCoordinator(DataUpdateCoordinator[dict[str, Any]]):
    def __init__(self, hass: HomeAssistant, host: str, entry=None) -> None:
        super().__init__(hass, _LOGGER, name=DOMAIN, update_interval=STATUS_INTERVAL, config_entry=entry)
        self.host = normalize_host(host)
        self.client = ControllerClient(async_get_clientsession(hass), self.host)
        self.write_lock = asyncio.Lock()
        self.raw: dict[str, Any] = {}       # last good answer per slow endpoint
        self._slow_at = 0.0
        self._update_at = -UPDATE_INTERVAL_S
        self._force_slow = True
        self._last_shot_id: Any = None

    async def _get(self, path: str, query: dict[str, Any] | None = None) -> Any:
        status, body = await self.client.get_json(path, query)
        return body if status == 200 else None

    async def _async_update_data(self) -> dict[str, Any]:
        status_code, status = await self.client.get_json("/api/status")
        if status_code != 200 or not isinstance(status, dict):
            raise UpdateFailed(f"/api/status answered {status_code}")

        shot_id = status.get("last_shot_id")
        new_shot = self._last_shot_id is not None and shot_id != self._last_shot_id
        if new_shot or self._force_slow or time.monotonic() - self._slow_at >= SLOW_INTERVAL_S:
            await self._refresh_slow()

        data = build_snapshot(status, self.raw.get("shots"), self.raw.get("grind"))
        data.update(build_extra(self.raw.get("library"), self.raw.get("maintenance"),
                                self.raw.get("subs"), self.raw.get("grind"), _today()))
        data.update(room_effect(self.raw.get("grind")))
        data.update(build_summary(self.raw.get("library"), self.raw.get("subs"), self.raw.get("power"),
                                  self.raw.get("ratings"), self.raw.get("update"), self.raw.get("shots"),
                                  _today(), int(time.time()), _tz(), self.raw.get("grind")))
        # A failed optional read must not blank the last known values.
        if self.data:
            if self.raw.get("shots") is None:
                data.update({k: self.data.get(k) for k in SHOT_KEYS})
            if self.raw.get("grind") is None:
                data.update({k: self.data.get(k) for k in GRIND_KEYS})
        data["status"] = status

        if new_shot and isinstance(self.raw.get("shots"), list) and self.raw["shots"]:
            self.hass.bus.async_fire(EVENT_SHOT, shot_summary(self.raw["shots"][0]))
        self._last_shot_id = shot_id
        return data

    async def _refresh_slow(self) -> None:
        reads = {
            "shots": ("/api/shots", {"limit": 50}),   # enough for a day's count
            "grind": ("/api/grind", None),
            "library": ("/api/library", None),
            "maintenance": ("/api/maintenance", None),
            "subs": ("/api/subscriptions", None),
            "power": ("/api/power", None),
            "ratings": ("/api/coffee/ratings", None),
        }
        if time.monotonic() - self._update_at >= UPDATE_INTERVAL_S:
            reads["update"] = ("/api/update", None)
        results = await asyncio.gather(*(self._get(p, q) for p, q in reads.values()))
        for key, value in zip(reads, results):
            if value is not None:
                self.raw[key] = value
            elif key == "shots":
                # /api/shots answers 409 while a shot runs: keep the last list
                pass
        self._slow_at = time.monotonic()
        if "update" in reads:
            self._update_at = self._slow_at
        self._force_slow = False

    async def async_write(self, path: str, body: Any = None, query: dict[str, Any] | None = None) -> Any:
        """A write from an entity or service: checked by the proxy rules, then a refresh."""
        decision = proxy.check("POST", path, query, body, scope="entity")
        if not decision.allowed:
            raise HomeAssistantError(decision.reason)
        async with self.write_lock:
            status, answer = await self.client.post_json(path, decision.body, query)
        if status != 200 or (isinstance(answer, dict) and answer.get("ok") is False):
            raise HomeAssistantError(error_message(answer, f"The controller answered {status}"))
        self._force_slow = True
        await self.async_request_refresh()
        return answer

    async def async_start_shot(self) -> None:
        """The existing Start shot button (the controller itself refuses unless web start is on)."""
        async with self.write_lock:
            status, answer = await self.client.post_json("/api/cmd", None, {"c": "start"})
        if status != 200:
            raise HomeAssistantError(error_message(answer, f"The controller answered {status}"))
        await self.async_request_refresh()


def _tz():
    from homeassistant.util import dt as dt_util
    return dt_util.get_default_time_zone()


def _today() -> tuple[int, int, int]:
    from homeassistant.util import dt as dt_util
    d: date = dt_util.now().date()  # Home Assistant's time zone, not the container's
    return d.year, d.month, d.day
