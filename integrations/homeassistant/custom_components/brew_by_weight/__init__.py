"""Brew by Weight: Home Assistant integration for the brew-by-weight controller."""
from __future__ import annotations

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant
from homeassistant.helpers import config_validation as cv

from .const import CONF_HOST, DOMAIN, loaded_entries
from .coordinator import BrewCoordinator

PLATFORMS = [Platform.SENSOR, Platform.BINARY_SENSOR, Platform.BUTTON, Platform.SELECT]
CONFIG_SCHEMA = cv.config_entry_only_config_schema(DOMAIN)


async def async_setup(hass: HomeAssistant, config: dict) -> bool:
    """Once per start: websocket commands, downloads, services, the panel's static files."""
    from . import panel, services, websocket
    from .download import BrewDownloadView
    websocket.async_register(hass)
    hass.http.register_view(BrewDownloadView(hass))
    services.async_register(hass)
    await panel.async_register_static(hass)
    return True


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    from . import panel
    coordinator = BrewCoordinator(hass, entry.data[CONF_HOST], entry)
    await coordinator.async_config_entry_first_refresh()
    entry.runtime_data = coordinator
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    await panel.async_register_panel(hass)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    from . import panel, websocket
    websocket.stop_pollers(hass, entry.entry_id)
    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unloaded and not [e for e in loaded_entries(hass) if e.entry_id != entry.entry_id]:
        panel.async_remove_panel(hass)
    return unloaded
