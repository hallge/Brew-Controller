"""Shared base for the integration's entities: one device, available while the value is known."""
from __future__ import annotations

from typing import Any

from homeassistant.config_entries import ConfigEntry
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import BrewCoordinator


class BrewEntity(CoordinatorEntity[BrewCoordinator]):
    _attr_has_entity_name = True

    def __init__(self, coordinator: BrewCoordinator, entry: ConfigEntry, key: str) -> None:
        super().__init__(coordinator)
        self._key = key
        self._attr_unique_id = f"{entry.entry_id}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.entry_id)}, name="Brew by Weight",
            manufacturer="Brew by Weight", configuration_url=coordinator.host)

    def value(self, key: str | None = None) -> Any:
        return (self.coordinator.data or {}).get(key or self._key)
