"""Binary sensors: what the controller can see right now, and what upkeep is due."""
from __future__ import annotations

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
    BinarySensorEntityDescription,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .entity import BrewEntity

BINARY_SENSORS: tuple[BinarySensorEntityDescription, ...] = (
    BinarySensorEntityDescription(key="scale_connected", translation_key="scale_connected",
                                  device_class=BinarySensorDeviceClass.CONNECTIVITY),
    BinarySensorEntityDescription(key="cup_present", translation_key="cup_present",
                                  device_class=BinarySensorDeviceClass.OCCUPANCY),
    BinarySensorEntityDescription(key="bot_connected", translation_key="bot_connected",
                                  device_class=BinarySensorDeviceClass.CONNECTIVITY),
    # 0.3.0
    BinarySensorEntityDescription(key="backflush_due", translation_key="backflush_due",
                                  device_class=BinarySensorDeviceClass.PROBLEM, icon="mdi:water-sync"),
    BinarySensorEntityDescription(key="descale_due", translation_key="descale_due",
                                  device_class=BinarySensorDeviceClass.PROBLEM, icon="mdi:water-alert"),
    BinarySensorEntityDescription(key="grinder_clean_due", translation_key="grinder_clean_due",
                                  device_class=BinarySensorDeviceClass.PROBLEM, icon="mdi:broom"),
    BinarySensorEntityDescription(key="grind_reminder_due", translation_key="grind_reminder_due",
                                  icon="mdi:grain"),
)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry,
                            async_add_entities: AddEntitiesCallback) -> None:
    coordinator = entry.runtime_data
    async_add_entities(BrewBinarySensor(coordinator, entry, d) for d in BINARY_SENSORS)


class BrewBinarySensor(BrewEntity, BinarySensorEntity):
    def __init__(self, coordinator, entry, description: BinarySensorEntityDescription) -> None:
        super().__init__(coordinator, entry, description.key)
        self.entity_description = description

    @property
    def is_on(self) -> bool | None:
        return self.value()

    @property
    def available(self) -> bool:
        return super().available and self.is_on is not None
