"""The one place that talks HTTP to the controller (coordinator, websocket, downloads, services).

The controller is a small chip with only a few sockets: many requests at once make it drop
connections. So requests are queued (at most two at a time) and a read that fails to connect is
tried once more after a short pause.
"""
from __future__ import annotations

import asyncio
import json
from typing import Any

import aiohttp

from .api import UNREACHABLE

TIMEOUT_S = 15
MAX_PARALLEL = 2
RETRY_PAUSE_S = 0.4


class ControllerClient:
    def __init__(self, session: aiohttp.ClientSession, host: str) -> None:
        self.session = session
        self.host = host.rstrip("/")
        self._slots = asyncio.Semaphore(MAX_PARALLEL)

    async def _once(self, method: str, path: str, kwargs: dict[str, Any]) -> tuple[int, str, bytes, dict[str, str]]:
        async with self._slots:
            async with self.session.request(method, self.host + path, **kwargs) as resp:
                data = await resp.read()
                return resp.status, resp.headers.get("Content-Type", ""), data, dict(resp.headers)

    async def raw(self, method: str, path: str, query: dict[str, Any] | None = None,
                  body: Any = None) -> tuple[int, str, bytes, dict[str, str]]:
        """(status, content type, bytes, headers); 502 with a JSON error when unreachable."""
        method = method.upper()
        kwargs: dict[str, Any] = {"timeout": aiohttp.ClientTimeout(total=TIMEOUT_S)}
        if query:
            kwargs["params"] = {k: str(v) for k, v in query.items() if v is not None}
        if method == "POST":
            kwargs["data"] = json.dumps(body) if body is not None else ""
            kwargs["headers"] = {"Content-Type": "application/json"}
        tries = 2 if method == "GET" else 1  # a write is never sent twice
        for attempt in range(tries):
            try:
                return await self._once(method, path, kwargs)
            except (aiohttp.ClientError, TimeoutError):
                if attempt + 1 < tries:
                    await asyncio.sleep(RETRY_PAUSE_S)
        return 502, "application/json", json.dumps(UNREACHABLE).encode(), {}

    async def get_json(self, path: str, query: dict[str, Any] | None = None) -> tuple[int, Any]:
        status, _ctype, data, _h = await self.raw("GET", path, query)
        try:
            return status, json.loads(data) if data else None
        except ValueError:
            return status, None

    async def post_json(self, path: str, body: Any = None, query: dict[str, Any] | None = None) -> tuple[int, Any]:
        status, _ctype, data, _h = await self.raw("POST", path, query, body)
        try:
            return status, json.loads(data) if data else None
        except ValueError:
            return status, None
