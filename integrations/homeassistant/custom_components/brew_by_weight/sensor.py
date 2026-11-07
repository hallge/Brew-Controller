"""Sensors. A value the controller did not send makes the entity unavailable."""
from __future__ import annotations

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import PERCENTAGE, UnitOfMass, UnitOfTemperature, UnitOfTime
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import BrewCoordinator

M = SensorStateClass.MEASUREMENT

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
)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry,
                            async_add_entities: AddEntitiesCallback) -> None:
    coordinator: BrewCoordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities(BrewSensor(coordinator, entry, d) for d in SENSORS)


class BrewSensor(CoordinatorEntity[BrewCoordinator], SensorEntity):
    _attr_has_entity_name = True

    def __init__(self, coordinator: BrewCoordinator, entry: ConfigEntry,
                 description: SensorEntityDescription) -> None:
        super().__init__(coordinator)
        self.entity_description = description
        self._attr_unique_id = f"{entry.entry_id}_{description.key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.entry_id)}, name="Brew by Weight",
            manufacturer="Brew by Weight", configuration_url=coordinator.host)

    @property
    def native_value(self):
        return (self.coordinator.data or {}).get(self.entity_description.key)

    @property
    def available(self) -> bool:
        return super().available and self.native_value is not None
