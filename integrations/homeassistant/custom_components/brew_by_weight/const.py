"""Constants for the Brew by Weight integration."""
from datetime import timedelta

DOMAIN = "brew_by_weight"
CONF_HOST = "host"
DEFAULT_HOST = "http://brewbyweight.local"
SCAN_INTERVAL = timedelta(seconds=30)
REQUEST_TIMEOUT_S = 10
