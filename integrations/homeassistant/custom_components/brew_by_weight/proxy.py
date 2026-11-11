"""Which controller requests Home Assistant may pass on for the Brew panel.

Plain Python (no Home Assistant imports) so it tests alone and the panel's dev harness
(frontend/dev/serve.py) enforces exactly the same rules.

The panel is a summary: it reads, and it offers the few everyday actions (rate a shot, set the
advised grind, bag finished or opened, a delivery arrived or skipped, upkeep done). Nothing that
sets the controller up passes (coffees, recipes, grinder, settings, sharing, the Home Assistant
link), and nothing that operates the hardware (presses, pairing, network, firmware, restore, the
machine profile, the power schedule): those stay in the controller's own page.

Rules are allowlists: a request not listed is refused.
"""
from __future__ import annotations

import re
from dataclasses import dataclass
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
    r"/api/update",             # read only: the firmware version
    r"/api/addons",             # read only: which add-ons run
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
# Only the everyday actions (integration 0.4.0): all setup (coffees, recipes, grinder, settings,
# sharing, the Home Assistant link, diagnostics, logs) is done in the controller's own page.

POST_ALLOWED: tuple[str, ...] = (
    r"/api/coffee/rate",        # a coffee rating
    r"/api/library/do",         # what= is checked against LIBRARY_DO_ALLOWED
    r"/api/grind/set",          # "I set the grinder to ...", from the advice
    r"/api/grind/recommendation",
    r"/api/shot/rate",
    r"/api/shot/tds",
    r"/api/shot/vote",
    r"/api/shot/prep",
    r"/api/maintenance",        # action=done only (MAINTENANCE_ACTIONS_ALLOWED)
    r"/api/subscriptions/arrived",
    r"/api/subscriptions/skip",
)

LIBRARY_DO_ALLOWED: frozenset[str] = frozenset({"coffee_finish", "bag_open", "bag_arrived"})

# The entities may also choose what the controller brews (the Active preset select): a daily
# choice, not setup, and nothing is pressed. The panel cannot.
ENTITY_LIBRARY_DO_ALLOWED: frozenset[str] = LIBRARY_DO_ALLOWED | {"preset_select"}

MAINTENANCE_ACTIONS_ALLOWED: frozenset[str] = frozenset({"done"})

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


def check(method: str, path: str, query: dict[str, Any] | None = None, body: Any = None,
          scope: str = "panel") -> Decision:
    """Decide whether a request may go to the controller, and with which body.

    `scope` is "panel" (the Brew panel's websocket requests) or "entity" (entities and services,
    which may also choose the active preset)."""
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
        allowed = ENTITY_LIBRARY_DO_ALLOWED if scope == "entity" else LIBRARY_DO_ALLOWED
        if what not in allowed:
            return Decision(False, f"library action '{what}' is done in the controller page")

    if path == "/api/maintenance":
        if str(query.get("action", "")) not in MAINTENANCE_ACTIONS_ALLOWED:
            return Decision(False, "reminder intervals are set up in the controller page")

    return Decision(True, body=body)
