"""Authenticated downloads for the Brew panel: shot CSVs, the log, a backup."""
from __future__ import annotations

from aiohttp import web
from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant

from .const import loaded_entries

# kind -> (controller path, admin only, file name)
DOWNLOADS = {
    "shots_csv": ("/shots.csv", False, "brew-shots.csv"),
    "shot_csv": ("/shot.csv", False, "brew-shot-{id}.csv"),
    "log": ("/log.txt", False, "brew-controller-log.txt"),
    "backup": ("/api/backup", True, "brew-controller.bbwbak"),
}
PASS_PARAMS = ("id", "history", "secrets")


class BrewDownloadView(HomeAssistantView):
    url = "/api/brew_by_weight/download/{entry_id}"
    name = "api:brew_by_weight:download"
    requires_auth = True

    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass

    async def get(self, request: web.Request, entry_id: str) -> web.StreamResponse:
        kind = request.query.get("kind", "")
        if kind not in DOWNLOADS:
            return web.Response(status=404, text="unknown kind")
        path, admin_only, filename = DOWNLOADS[kind]
        user = request.get("hass_user")
        is_admin = bool(user and user.is_admin)
        if (admin_only or request.query.get("secrets") == "1") and not is_admin:
            return web.Response(status=403, text="Only an administrator can download this")
        entry = next((e for e in loaded_entries(self.hass) if e.entry_id == entry_id), None)
        if entry is None:
            return web.Response(status=404, text="No such Brew by Weight entry")
        params = {k: request.query[k] for k in PASS_PARAMS if k in request.query}
        status, ctype, raw, _headers = await entry.runtime_data.client.raw("GET", path, params)
        if status != 200:
            return web.Response(status=status, body=raw, content_type="text/plain")
        name = filename.format(id=params.get("id", ""))
        return web.Response(body=raw, content_type=(ctype or "application/octet-stream").split(";")[0],
                            headers={"Content-Disposition": f'attachment; filename="{name}"'})
