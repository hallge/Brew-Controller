"""Which controller requests Home Assistant may pass on for the Brew panel.

Plain Python (no Home Assistant imports) so it tests alone and the panel's dev harness
(frontend/dev/serve.py) enforces exactly the same rules.

The panel carries every feature of the controller's page that does not operate the hardware.
Anything that presses a button, pairs or tests a device, changes the network, installs or boots
firmware, restores a backup, or changes how the machine is driven (machine profile, brew and press
timing, power schedule, power-bot settings) is refused here: those stay in the controller's own
page (the Brew Controller add-on panel).

Rules are allowlists: a request not listed is refused.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any

# --- reads -------------------------------------------------------------------------------------

GET_ALLOWED: tuple[str, ...] = (
    r"/api/status",
    r"/api/start",
    r"/api/settings",
    r"/api/library",
    r"/api/library/pending",
    r"/api/grind",
    r"/api/grind/accuracy",
    r"/api/shots",
    r"/api/contexts",
    r"/api/shot/data",
    r"/api/last",
    r"/api/maintenance",
    r"/api/machine",            # read only: make and model shown, never changed here
    r"/api/power",              # read only: the machine's believed state
    r"/api/power/events",
    r"/api/coffee/ratings",
    r"/api/taste/profile",
    r"/api/taste/recommend",
    r"/api/community",
    r"/api/community/coffees",
    r"/api/community/recipes",
    r"/api/community/grinders",
    r"/api/community/machines",
    r"/api/directory",
    r"/api/trending",
    r"/api/links",
    r"/api/share",
    r"/api/subscriptions",
    r"/api/subscriptions/preview",
    r"/api/subscriptions/library",
    r"/api/subscriptions/reviews",
    r"/api/subscriptions/recommend",
    r"/api/homeassistant",
    r"/api/ambient",
    r"/api/ambient/test",
    r"/api/diagnostics",
    r"/api/diagnostics/report",
    r"/api/diagnostics/preview",
    r"/api/crash",
    r"/api/update",             # read only: firmware version shown in Settings
    r"/api/log",
    r"/api/log/config",
    r"/shots\.csv",
    r"/shot\.csv",
    r"/log\.txt",
)

# Downloads that need an admin and may carry secrets: only through the download view.
GET_ADMIN_ONLY: tuple[str, ...] = (
    r"/api/backup",
)

# --- writes ------------------------------------------------------------------------------------

POST_ALLOWED: tuple[str, ...] = (
    r"/api/coffee",
    r"/api/coffee/adopt",
    r"/api/coffee/rate",
    r"/api/coffee/rate/delete",
    r"/api/bag",
    r"/api/recipe",
    r"/api/grinder",
    r"/api/drinkers",
    r"/api/library/do",         # what= is checked against LIBRARY_DO_ALLOWED
    r"/api/grind/set",
    r"/api/grind/recommendation",
    r"/api/shot/rate",
    r"/api/shot/tds",
    r"/api/shot/vote",
    r"/api/shot/grind",
    r"/api/shot/prep",
    r"/api/history/clear",
    r"/api/offset/reset",
    r"/api/maintenance",
    r"/api/power",              # only the belief corrections in POWER_ACTIONS_ALLOWED: nothing is pressed
    r"/api/settings",           # body keys are filtered to SETTINGS_ALLOWED
    r"/api/homeassistant",
    r"/api/share/link",
    r"/api/share/coffee",
    r"/api/share/coffees",
    r"/api/share/again",
    r"/api/share/grinder",
    r"/api/recipes/share",
    r"/api/library/review",
    r"/api/subscriptions",
    r"/api/subscriptions/delete",
    r"/api/subscriptions/arrived",
    r"/api/subscriptions/skip",
    r"/api/subscriptions/preview",
    r"/api/subscriptions/share",
    r"/api/subscriptions/review",
    r"/api/subscriptions/review/delete",
    r"/api/subscriptions/prefs",
    r"/api/links/suggest",
    r"/api/links/delete",
    r"/api/diagnostics/send",
    r"/api/diagnostics/bug",
    r"/api/diagnostics/relay",
    r"/api/log/config",
    r"/api/log/clear",
)

LIBRARY_DO_ALLOWED: frozenset[str] = frozenset({
    "preset_select", "preset_add", "preset_delete", "preset_grind",
    "recipe_delete", "coffee_delete", "coffee_finish",
    "bag_use", "bag_delete", "bag_open", "bag_arrived",
    "grinder_switch", "grinder_remove_other",
})

POWER_ACTIONS_ALLOWED: frozenset[str] = frozenset({"believe_on", "believe_off"})

# Settings the panel may change: grind advice, sharing, screen, drinkers, diagnostics, panels.
# Everything else in /api/settings (stop-offset learning, scale and shot timing, cup workflow,
# press style and timing, power-bot and scale-sleep behaviour) drives the hardware and is refused.
SETTINGS_ALLOWED: frozenset[str] = frozenset({
    "grindToleranceS", "grindWindow", "grindUmPerS", "grindMaxStepUm", "grindTimeBasis",
    "grindRatedTarget", "grindConfidence",
    "shareEnabled", "shareCoffees", "shareDialIns", "shareShots", "shareRatings",
    "displayFlipped", "namedDrinkers", "diagnosticsOn", "webPanels", "uiTheme",
})

# Never, whatever the method (documentation of intent; anything unlisted is refused anyway).
HARDWARE: tuple[str, ...] = (
    r"/api/cmd", r"/api/pair", r"/api/bot/test", r"/api/pusher", r"/api/net", r"/api/devices",
    r"/api/update/install", r"/api/update/boot", r"/api/update/config", r"/api/update/check",
    r"/api/restore", r"/api/restore/apply", r"/api/time", r"/api/share/machine",
)

_SAFE_PATH = re.compile(r"^/[A-Za-z0-9_./-]*$")


def _match(path: str, patterns: tuple[str, ...]) -> bool:
    return any(re.fullmatch(p, path) for p in patterns)


@dataclass
class Decision:
    allowed: bool
    reason: str = ""
    body: Any = None
    admin_only: bool = False
    dropped_keys: list[str] = field(default_factory=list)


def check(method: str, path: str, query: dict[str, Any] | None = None, body: Any = None) -> Decision:
    """Decide whether a request may go to the controller, and with which body."""
    method = (method or "").upper()
    query = query or {}
    if not isinstance(path, str) or not _SAFE_PATH.match(path) or ".." in path or "//" in path:
        return Decision(False, "bad path")
    if _match(path, HARDWARE):
        return Decision(False, "operates the hardware: use the controller page")

    if method == "GET":
        if _match(path, GET_ALLOWED):
            return Decision(True, body=None)
        if _match(path, GET_ADMIN_ONLY):
            return Decision(True, body=None, admin_only=True)
        return Decision(False, "not available in the panel")

    if method != "POST":
        return Decision(False, "method not allowed")
    if not _match(path, POST_ALLOWED):
        return Decision(False, "not available in the panel")

    if path == "/api/library/do":
        what = str(query.get("what", ""))
        if what not in LIBRARY_DO_ALLOWED:
            return Decision(False, f"library action '{what}' not available in the panel")

    if path == "/api/power":
        action = body.get("action") if isinstance(body, dict) else None
        if action not in POWER_ACTIONS_ALLOWED:
            return Decision(False, "operates the hardware: use the controller page")

    if path == "/api/settings":
        if not isinstance(body, dict):
            return Decision(False, "settings need an object")
        kept = {k: v for k, v in body.items() if k in SETTINGS_ALLOWED}
        dropped = sorted(k for k in body if k not in SETTINGS_ALLOWED)
        if not kept:
            return Decision(False, "these settings drive the hardware: use the controller page",
                            dropped_keys=dropped)
        return Decision(True, body=kept, dropped_keys=dropped)

    return Decision(True, body=body)
