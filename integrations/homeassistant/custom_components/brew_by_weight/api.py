"""Plain-Python helpers for the controller's JSON API: no Home Assistant imports, so they test alone."""
from __future__ import annotations

from typing import Any


def normalize_host(host: str) -> str:
    """Turn what the user typed into a base URL without a trailing slash."""
    host = (host or "").strip().rstrip("/")
    if not host:
        return ""
    if "://" not in host:
        host = "http://" + host
    return host


def number(value: Any) -> float | int | None:
    """A JSON number, else None (bool, strings, null and missing are not numbers)."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return value


def positive(value: Any) -> float | int | None:
    """A number above zero; the controller sends 0 for 'not known'."""
    n = number(value)
    return n if n is not None and n > 0 else None


def text(value: Any) -> str | None:
    return value if isinstance(value, str) and value else None


def flag(value: Any) -> bool | None:
    """A JSON true/false, else None (0, 1 and strings are not flags)."""
    return value if isinstance(value, bool) else None


def newest_shot(shots: Any) -> dict[str, Any]:
    """The first shot of an /api/shots list (newest first), or an empty dict."""
    if isinstance(shots, list) and shots and isinstance(shots[0], dict):
        return shots[0]
    return {}


def build_snapshot(status: Any, shots: Any, grind: Any) -> dict[str, Any]:
    """Map the three responses to the sensor values; anything absent is None."""
    status = status if isinstance(status, dict) else {}
    grind = grind if isinstance(grind, dict) else {}
    shot = newest_shot(shots)
    has_number = grind.get("has_number") is not False
    return {
        "state": text(status.get("state")),
        "web_start": flag(status.get("web_start")),
        "scale_connected": flag(status.get("scale_connected")),
        "cup_present": flag(status.get("cup_present")),
        "bot_connected": flag(status.get("bot_connected")),
        "bot_battery_pct": number(status.get("bot_battery")),
        "maintenance_due": number(status.get("maintenance_due")),
        "target_weight_g": positive(status.get("target_weight_g")),
        "recipe": text(status.get("recipe")),
        "last_shot_time_s": number(shot.get("shot_time_s")),
        "last_shot_weight_g": number(shot.get("final_weight_g")),
        "last_shot_grind": positive(shot.get("grind_um")),
        "advised_grind": positive(grind.get("suggested_um")) if has_number else None,
        "advised_action": text(grind.get("action")),
        "shot_noise_s": number(grind.get("shot_noise_s")),
        "drift_s_per_day": number(grind.get("drift_s_per_day")),
        "ambient_rh_pct": number(shot.get("ambient_rh_pct")),
        "ambient_temp_c": number(shot.get("ambient_temp_c")),
    }


def error_message(body: Any, default: str) -> str:
    """The controller's {"ok": false, "error": "..."} message, else a default."""
    if isinstance(body, dict) and isinstance(body.get("error"), str) and body["error"]:
        return body["error"]
    return default
