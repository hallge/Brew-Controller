"""Binary sensors: what the controller can see right now. A value it did not send makes the entity unavailable."""
from __future__ import annotations

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
    BinarySensorEntityDescription,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import BrewCoordinator

BINARY_SENSORS: tuple[BinarySensorEntityDescription, ...] = (
    BinarySensorEntityDescription(key="scale_connected", translation_key="scale_connected",
                                  device_class=BinarySensorDeviceClass.CONNECTIVITY),
    BinarySensorEntityDescription(key="cup_present", translation_key="cup_present",
                                  device_class=BinarySensorDeviceClass.OCCUPANCY),
    BinarySensorEntityDescription(key="bot_connected", translation_key="bot_connected",
                                  device_class=BinarySensorDeviceClass.CONNECTIVITY),
)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry,
                            async_add_entities: AddEntitiesCallback) -> None:
    coordinator: BrewCoordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities(BrewBinarySensor(coordinator, entry, d) for d in BINARY_SENSORS)


class BrewBinarySensor(CoordinatorEntity[BrewCoordinator], BinarySensorEntity):
    _attr_has_entity_name = True

    def __init__(self, coordinator: BrewCoordinator, entry: ConfigEntry,
                 description: BinarySensorEntityDescription) -> None:
        super().__init__(coordinator)
        self.entity_description = description
        self._attr_unique_id = f"{entry.entry_id}_{description.key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.entry_id)}, name="Brew by Weight",
            manufacturer="Brew by Weight", configuration_url=coordinator.host)

    @property
    def is_on(self) -> bool | None:
        return (self.coordinator.data or {}).get(self.entity_description.key)

    @property
    def available(self) -> bool:
        return super().available and self.is_on is not None
