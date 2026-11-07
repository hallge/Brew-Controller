"""Sensors. A value the controller did not send makes the entity unavailable."""
from __future__ import annotations

from typing import Any

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import PERCENTAGE, UnitOfMass, UnitOfTemperature, UnitOfTime
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .api import STATUS_OPTIONS, status_option
from .entity import BrewEntity

M = SensorStateClass.MEASUREMENT
TOTAL = SensorStateClass.TOTAL_INCREASING

SENSORS: tuple[SensorEntityDescription, ...] = (
    SensorEntityDescription(key="state", translation_key="state"),
    SensorEntityDescription(key="last_shot_time_s", translation_key="last_shot_time",
                            native_unit_of_measurement=UnitOfTime.SECONDS,
                            device_class=SensorDeviceClass.DURATION, state_class=M),
    SensorEntityDescription(key="last_shot_weight_g", translation_key="last_shot_weight",
                            native_unit_of_measurement=UnitOfMass.GRAMS,
                            device_class=SensorDeviceClass.WEIGHT, state_class=M),
    SensorEntityDescription(key="last_shot_grind", translation_key="last_shot_grind", state_class=M),
    SensorEntityDescription(key="advised_grind", translation_key="advised_grind", state_class=M),
    SensorEntityDescription(key="advised_action", translation_key="advised_action"),
    SensorEntityDescription(key="shot_noise_s", translation_key="shot_noise",
                            native_unit_of_measurement=UnitOfTime.SECONDS, state_class=M),
    SensorEntityDescription(key="drift_s_per_day", translation_key="drift", state_class=M,
                            native_unit_of_measurement="s/d"),
    SensorEntityDescription(key="ambient_rh_pct", translation_key="ambient_humidity",
                            native_unit_of_measurement=PERCENTAGE,
                            device_class=SensorDeviceClass.HUMIDITY, state_class=M),
    SensorEntityDescription(key="ambient_temp_c", translation_key="ambient_temperature",
                            native_unit_of_measurement=UnitOfTemperature.CELSIUS,
                            device_class=SensorDeviceClass.TEMPERATURE, state_class=M),
    SensorEntityDescription(key="bot_battery_pct", translation_key="bot_battery",
                            native_unit_of_measurement=PERCENTAGE,
                            device_class=SensorDeviceClass.BATTERY, state_class=M),
    SensorEntityDescription(key="maintenance_due", translation_key="maintenance_due", state_class=M),
    SensorEntityDescription(key="target_weight_g", translation_key="target_weight",
                            native_unit_of_measurement=UnitOfMass.GRAMS,
                            device_class=SensorDeviceClass.WEIGHT, state_class=M),
    SensorEntityDescription(key="recipe", translation_key="recipe"),
    # 0.3.0
    SensorEntityDescription(key="coffee_name", translation_key="coffee_in_use", icon="mdi:sack"),
    SensorEntityDescription(key="bag_age_days", translation_key="bag_age", icon="mdi:calendar-clock",
                            native_unit_of_measurement=UnitOfTime.DAYS, state_class=M),
    SensorEntityDescription(key="shots_total", translation_key="shots_total", icon="mdi:counter", state_class=TOTAL),
    SensorEntityDescription(key="grind_recommendation", translation_key="grind_recommendation", icon="mdi:grain"),
    SensorEntityDescription(key="subs_due_soon", translation_key="subs_due_soon", icon="mdi:truck-delivery"),
    SensorEntityDescription(key="backflush_since", translation_key="backflush_since", icon="mdi:water-sync", state_class=M),
    SensorEntityDescription(key="descale_since", translation_key="descale_since", icon="mdi:water-alert", state_class=M),
    SensorEntityDescription(key="grinder_clean_since", translation_key="grinder_clean_since", icon="mdi:broom", state_class=M),
)

ATTRIBUTES = {
    "coffee_name": lambda d: {"roaster": d.get("coffee_roaster"), "coffee_id": d.get("coffee_id")},
    "grind_recommendation": lambda d: {"source": d.get("grind_recommendation_source")},
    "subs_due_soon": lambda d: {"next": d.get("subs_next")},
    "backflush_since": lambda d: {"every_shots": d.get("backflush_every")},
    "descale_since": lambda d: {"every_shots": d.get("descale_every")},
    "grinder_clean_since": lambda d: {"every_shots": d.get("grinder_clean_every")},
}


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry,
                            async_add_entities: AddEntitiesCallback) -> None:
    coordinator = entry.runtime_data
    entities: list[SensorEntity] = [BrewSensor(coordinator, entry, d) for d in SENSORS]
    entities.append(MachineStatusSensor(coordinator, entry))
    async_add_entities(entities)


class BrewSensor(BrewEntity, SensorEntity):
    def __init__(self, coordinator, entry, description: SensorEntityDescription) -> None:
        super().__init__(coordinator, entry, description.key)
        self.entity_description = description

    @property
    def native_value(self):
        return self.value()

    @property
    def extra_state_attributes(self) -> dict[str, Any] | None:
        fn = ATTRIBUTES.get(self._key)
        return fn(self.coordinator.data or {}) if fn else None

    @property
    def available(self) -> bool:
        return super().available and self.native_value is not None


class MachineStatusSensor(BrewEntity, SensorEntity):
    """The machine state in words (an enum with translated states). The raw `state` sensor stays
    as it was, upper case, because automations compare against its values."""

    _attr_translation_key = "machine_status"
    _attr_device_class = SensorDeviceClass.ENUM
    _attr_options = list(STATUS_OPTIONS)
    _attr_icon = "mdi:coffee-maker"

    def __init__(self, coordinator, entry) -> None:
        super().__init__(coordinator, entry, "machine_status")

    @property
    def native_value(self):
        return status_option(self.value("state"))

    @property
    def available(self) -> bool:
        return super().available and self.native_value is not None
