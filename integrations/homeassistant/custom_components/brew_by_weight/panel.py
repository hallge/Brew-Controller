"""Registers the Brew panel (a custom panel in Home Assistant's sidebar) and its static files."""
from __future__ import annotations

import json
from pathlib import Path

from homeassistant.core import HomeAssistant

from .const import DOMAIN

FRONTEND_DIR = Path(__file__).parent / "frontend"
STATIC_URL = "/brew_by_weight_static"
PANEL_URL_PATH = "brew-panel"
PANEL_TITLE = "Brew Controller"
PANEL_ICON = "mdi:coffee-maker"


def _version() -> str:
    try:
        return json.loads((Path(__file__).parent / "manifest.json").read_text())["version"]
    except (OSError, ValueError, KeyError):
        return "0"


def _static_url() -> str:
    """The frontend is served under the integration's version, so a browser never mixes a new panel
    with cached modules of an older one (the sections import each other by relative address)."""
    return f"{STATIC_URL}/{_version()}"


async def async_register_static(hass: HomeAssistant) -> None:
    """Once per Home Assistant start (static routes cannot be removed)."""
    data = hass.data.setdefault(DOMAIN, {})
    if data.get("static"):
        return
    from homeassistant.components.http import StaticPathConfig
    await hass.http.async_register_static_paths([StaticPathConfig(_static_url(), str(FRONTEND_DIR), True)])
    data["static"] = True


async def async_register_panel(hass: HomeAssistant) -> None:
    data = hass.data.setdefault(DOMAIN, {})
    if data.get("panel"):
        return
    from homeassistant.components import panel_custom
    await panel_custom.async_register_panel(
        hass,
        frontend_url_path=PANEL_URL_PATH,
        webcomponent_name="brew-panel",
        sidebar_title=PANEL_TITLE,
        sidebar_icon=PANEL_ICON,
        module_url=f"{_static_url()}/brew-panel.js",
        embed_iframe=False,
        require_admin=False,
    )
    data["panel"] = True


def async_remove_panel(hass: HomeAssistant) -> None:
    data = hass.data.get(DOMAIN, {})
    if not data.get("panel"):
        return
    from homeassistant.components.frontend import async_remove_panel as remove
    remove(hass, PANEL_URL_PATH)
    data["panel"] = False
