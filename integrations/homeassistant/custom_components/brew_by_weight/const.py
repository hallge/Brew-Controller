"""Constants for the Brew by Weight integration."""
from datetime import timedelta

DOMAIN = "brew_by_weight"
CONF_HOST = "host"
DEFAULT_HOST = "http://brewbyweight.local"
SCAN_INTERVAL = timedelta(seconds=30)
REQUEST_TIMEOUT_S = 10


def loaded_entries(hass):
    """The Brew by Weight entries that are set up (loaded, coordinator in runtime_data)."""
    from homeassistant.config_entries import ConfigEntryState
    return [e for e in hass.config_entries.async_entries(DOMAIN)
            if e.state is ConfigEntryState.LOADED and getattr(e, "runtime_data", None) is not None]
