"""The Start shot button."""
from __future__ import annotations

from homeassistant.components.button import ButtonEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import BrewCoordinator


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry,
                            async_add_entities: AddEntitiesCallback) -> None:
    async_add_entities([StartShotButton(hass.data[DOMAIN][entry.entry_id], entry)])


class StartShotButton(CoordinatorEntity[BrewCoordinator], ButtonEntity):
    _attr_has_entity_name = True
    _attr_translation_key = "start_shot"

    def __init__(self, coordinator: BrewCoordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator)
        self._attr_unique_id = f"{entry.entry_id}_start_shot"
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, entry.entry_id)})

    async def async_press(self) -> None:
        # Raises HomeAssistantError with the controller's message when web start is off.
        await self.coordinator.async_start_shot()
