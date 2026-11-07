"""Websocket commands for the Brew panel (frontend/CONTRACT.md)."""
from __future__ import annotations

import asyncio
import logging
from typing import Any

import voluptuous as vol
from homeassistant.components import websocket_api
from homeassistant.core import HomeAssistant, callback

from . import proxy
from .api import shape_answer
from .const import DOMAIN, loaded_entries

_LOGGER = logging.getLogger(__name__)
STATUS_PERIOD_S = 1.0
ADMIN_ONLY = {"ok": False, "error": "Only an administrator can change this"}


def _coordinators(hass: HomeAssistant) -> dict[str, Any]:
    return {e.entry_id: e.runtime_data for e in loaded_entries(hass)}


def _coordinator(hass: HomeAssistant, entry_id: str):
    return _coordinators(hass).get(entry_id)


async def handle_request(coordinator, method: str, path: str, query: dict | None, body: Any,
                         is_admin: bool) -> dict[str, Any]:
    """One panel request: proxy rules, admin check, forward, shape (pure enough to test with a fake)."""
    decision = proxy.check(method, path, query, body)
    if not decision.allowed:
        return {"status": 403, "json": {"ok": False, "error": decision.reason}}
    if (method.upper() == "POST" or decision.admin_only) and not is_admin:
        return {"status": 403, "json": dict(ADMIN_ONLY)}
    if method.upper() == "POST":
        async with coordinator.write_lock:
            status, ctype, raw, headers = await coordinator.client.raw("POST", path, query, decision.body)
        coordinator._force_slow = True  # entities catch up at the next status poll
        coordinator.hass.async_create_task(coordinator.async_request_refresh())
    else:
        status, ctype, raw, headers = await coordinator.client.raw("GET", path, query)
    return shape_answer(status, ctype, raw, headers)


@callback
def async_register(hass: HomeAssistant) -> None:
    websocket_api.async_register_command(hass, ws_entries)
    websocket_api.async_register_command(hass, ws_request)
    websocket_api.async_register_command(hass, ws_subscribe_status)


@websocket_api.websocket_command({vol.Required("type"): f"{DOMAIN}/entries"})
@callback
def ws_entries(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    out = [{"entry_id": e.entry_id, "title": e.title, "host": e.runtime_data.host}
           for e in loaded_entries(hass)]
    connection.send_result(msg["id"], out)


@websocket_api.websocket_command({
    vol.Required("type"): f"{DOMAIN}/request",
    vol.Required("entry_id"): str,
    vol.Required("method"): vol.In(["GET", "POST", "get", "post"]),
    vol.Required("path"): str,
    vol.Optional("query"): dict,
    vol.Optional("body"): object,
})
@websocket_api.async_response
async def ws_request(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    coordinator = _coordinator(hass, msg["entry_id"])
    if coordinator is None:
        connection.send_error(msg["id"], "not_found", "No such Brew by Weight entry")
        return
    result = await handle_request(coordinator, msg["method"], msg["path"], msg.get("query"),
                                  msg.get("body"), bool(connection.user and connection.user.is_admin))
    connection.send_result(msg["id"], result)


class _StatusPoller:
    """One /api/status loop per entry, shared by every open panel, running only while one is open."""

    def __init__(self, hass: HomeAssistant, coordinator) -> None:
        self.hass = hass
        self.coordinator = coordinator
        self.listeners: set = set()
        self.task: asyncio.Task | None = None

    def add(self, fn) -> None:
        self.listeners.add(fn)
        if self.task is None or self.task.done():
            self.task = self.hass.async_create_background_task(self._run(), f"{DOMAIN} panel status")

    def remove(self, fn) -> None:
        self.listeners.discard(fn)
        if not self.listeners and self.task:
            self.task.cancel()
            self.task = None

    async def _run(self) -> None:
        while self.listeners:
            status, body = await self.coordinator.client.get_json("/api/status")
            payload = {"status": body} if status == 200 and isinstance(body, dict) else {
                "error": (body or {}).get("error") if isinstance(body, dict) else f"answered {status}"}
            for fn in list(self.listeners):
                try:
                    fn(payload)
                except Exception:  # noqa: BLE001 - a closed connection must not stop the others
                    _LOGGER.debug("status listener failed", exc_info=True)
            await asyncio.sleep(STATUS_PERIOD_S)


def _poller(hass: HomeAssistant, entry_id: str, coordinator) -> _StatusPoller:
    pollers = hass.data.setdefault(DOMAIN, {}).setdefault("pollers", {})
    if entry_id not in pollers:
        pollers[entry_id] = _StatusPoller(hass, coordinator)
    return pollers[entry_id]


def stop_pollers(hass: HomeAssistant, entry_id: str) -> None:
    p = hass.data.get(DOMAIN, {}).get("pollers", {}).pop(entry_id, None)
    if p:
        p.listeners.clear()
        if p.task:
            p.task.cancel()


@websocket_api.websocket_command({
    vol.Required("type"): f"{DOMAIN}/subscribe_status",
    vol.Required("entry_id"): str,
})
@callback
def ws_subscribe_status(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    coordinator = _coordinator(hass, msg["entry_id"])
    if coordinator is None:
        connection.send_error(msg["id"], "not_found", "No such Brew by Weight entry")
        return
    poller = _poller(hass, msg["entry_id"], coordinator)

    @callback
    def forward(payload: dict) -> None:
        connection.send_message(websocket_api.event_message(msg["id"], payload))

    @callback
    def unsubscribe() -> None:
        poller.remove(forward)

    connection.subscriptions[msg["id"]] = unsubscribe
    poller.add(forward)
    connection.send_result(msg["id"])
