"""The one place that talks HTTP to the controller (coordinator, websocket, downloads, services)."""
from __future__ import annotations

import json
from typing import Any

import aiohttp

from .api import UNREACHABLE

TIMEOUT_S = 15


class ControllerClient:
    def __init__(self, session: aiohttp.ClientSession, host: str) -> None:
        self.session = session
        self.host = host.rstrip("/")

    async def raw(self, method: str, path: str, query: dict[str, Any] | None = None,
                  body: Any = None) -> tuple[int, str, bytes, dict[str, str]]:
        """(status, content type, bytes, headers); 502 with a JSON error when unreachable."""
        kwargs: dict[str, Any] = {"timeout": aiohttp.ClientTimeout(total=TIMEOUT_S)}
        if query:
            kwargs["params"] = {k: str(v) for k, v in query.items() if v is not None}
        if method.upper() == "POST":
            kwargs["data"] = json.dumps(body) if body is not None else ""
            kwargs["headers"] = {"Content-Type": "application/json"}
        try:
            async with self.session.request(method.upper(), self.host + path, **kwargs) as resp:
                data = await resp.read()
                return resp.status, resp.headers.get("Content-Type", ""), data, dict(resp.headers)
        except (aiohttp.ClientError, TimeoutError):
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
