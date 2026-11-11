"""Binary sensors: what the controller can see right now, and what upkeep is due."""
from __future__ import annotations

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
    BinarySensorEntityDescription,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory
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
    # 0.4.0
    BinarySensorEntityDescription(key="machine_power", translation_key="machine_power",
                                  device_class=BinarySensorDeviceClass.POWER),
    BinarySensorEntityDescription(key="power_bot_paired", translation_key="power_bot_paired",
                                  icon="mdi:robot-industrial", entity_category=EntityCategory.DIAGNOSTIC),
    BinarySensorEntityDescription(key="scale_resting", translation_key="scale_resting", icon="mdi:scale"),
    BinarySensorEntityDescription(key="bag_frozen", translation_key="bag_frozen", icon="mdi:snowflake"),
)

ATTRIBUTES = {
    "machine_power": lambda d: _power_attrs(d.get("machine_power_attrs")),
}


def _power_attrs(a):
    """Next scheduled switch-on / -off as ISO times (Home Assistant's own time zone)."""
    if not a:
        return None
    from homeassistant.util import dt as dt_util
    iso = lambda t: dt_util.as_local(dt_util.utc_from_timestamp(t)).isoformat() if t else None  # noqa: E731
    return {"last": a.get("last"), "next_on": iso(a.get("next_on")), "next_off": iso(a.get("next_off"))}


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

    @property
    def extra_state_attributes(self):
        fn = ATTRIBUTES.get(self._key)
        return fn(self.coordinator.data or {}) if fn else None
