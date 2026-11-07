"""The active preset (coffee and recipe) the controller brews with. Choosing one presses nothing."""
from __future__ import annotations

from homeassistant.components.select import SelectEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .entity import BrewEntity


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry,
                            async_add_entities: AddEntitiesCallback) -> None:
    async_add_entities([ActivePresetSelect(entry.runtime_data, entry)])


class ActivePresetSelect(BrewEntity, SelectEntity):
    _attr_translation_key = "active_preset"
    _attr_icon = "mdi:cup-water"

    def __init__(self, coordinator, entry) -> None:
        super().__init__(coordinator, entry, "active_preset")

    @property
    def options(self) -> list[str]:
        return list(self.value("preset_options") or [])

    @property
    def current_option(self) -> str | None:
        return self.value("preset_current")

    @property
    def available(self) -> bool:
        return super().available and bool(self.options)

    async def async_select_option(self, option: str) -> None:
        index = (self.value("preset_index_by_label") or {}).get(option)
        if index is None:
            raise HomeAssistantError(f"Unknown preset: {option}")
        await self.coordinator.async_write("/api/library/do", None, {"what": "preset_select", "a": index})
