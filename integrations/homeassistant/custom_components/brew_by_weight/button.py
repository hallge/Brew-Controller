"""Buttons: Start shot (hardware, guarded by the controller), upkeep done, grind recommendation."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from homeassistant.components.button import ButtonEntity, ButtonEntityDescription
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .entity import BrewEntity


@dataclass(frozen=True, kw_only=True)
class WriteButtonDescription(ButtonEntityDescription):
    path: str
    query: dict[str, Any]
    available_key: str | None = None


WRITE_BUTTONS: tuple[WriteButtonDescription, ...] = (
    WriteButtonDescription(key="backflush_done", translation_key="backflush_done", icon="mdi:water-sync",
                           path="/api/maintenance", query={"job": "backflush", "action": "done"}),
    WriteButtonDescription(key="descale_done", translation_key="descale_done", icon="mdi:water-alert",
                           path="/api/maintenance", query={"job": "descale", "action": "done"}),
    WriteButtonDescription(key="grinder_clean_done", translation_key="grinder_clean_done", icon="mdi:broom",
                           path="/api/maintenance", query={"job": "grinder_clean", "action": "done"}),
    WriteButtonDescription(key="grind_recommendation_use", translation_key="grind_recommendation_use",
                           icon="mdi:check", path="/api/grind/recommendation", query={"use": 1},
                           available_key="grind_recommendation"),
    WriteButtonDescription(key="grind_recommendation_keep", translation_key="grind_recommendation_keep",
                           icon="mdi:close", path="/api/grind/recommendation", query={"use": 0},
                           available_key="grind_recommendation"),
)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry,
                            async_add_entities: AddEntitiesCallback) -> None:
    coordinator = entry.runtime_data
    entities: list[ButtonEntity] = [StartShotButton(coordinator, entry)]
    entities += [WriteButton(coordinator, entry, d) for d in WRITE_BUTTONS]
    async_add_entities(entities)


class StartShotButton(BrewEntity, ButtonEntity):
    _attr_translation_key = "start_shot"

    def __init__(self, coordinator, entry) -> None:
        super().__init__(coordinator, entry, "start_shot")

    async def async_press(self) -> None:
        # Raises HomeAssistantError with the controller's message when web start is off.
        await self.coordinator.async_start_shot()


class WriteButton(BrewEntity, ButtonEntity):
    entity_description: WriteButtonDescription

    def __init__(self, coordinator, entry, description: WriteButtonDescription) -> None:
        super().__init__(coordinator, entry, description.key)
        self.entity_description = description

    @property
    def available(self) -> bool:
        key = self.entity_description.available_key
        return super().available and (key is None or self.value(key) is not None)

    async def async_press(self) -> None:
        d = self.entity_description
        await self.coordinator.async_write(d.path, None, dict(d.query))
