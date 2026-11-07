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


# --- machine states (src/core/Types.h:84-105) ---------------------------------------------------

STATES: tuple[str, ...] = (
    "BOOT", "INITIALIZING", "CONNECTING_SCALE", "READY", "TARING", "WAITING_FOR_CUP", "READY_TO_BREW",
    "STARTING_SHOT", "BREWING", "BLOOMING", "STOPPING_SHOT", "POST_SHOT_DRIP", "SHOT_COMPLETE",
    "SCALE_DISCONNECTED", "SWITCHBOT_DISCONNECTED", "SCALE_DATA_STALE", "START_COMMAND_FAILED",
    "STOP_COMMAND_FAILED", "TARE_FAILED", "SHOT_TIMEOUT", "INVALID_WEIGHT", "BLE_ERROR",
)
STATUS_OPTIONS: tuple[str, ...] = tuple(s.lower() for s in STATES)


def status_option(state: Any) -> str | None:
    """The machine state as an enum option (lower case, translatable), or None if unknown."""
    if isinstance(state, str) and state.lower() in STATUS_OPTIONS:
        return state.lower()
    return None


# --- the slower data: library, maintenance, subscriptions, grind ----------------------------------

MAINTENANCE_JOBS: tuple[str, ...] = ("backflush", "descale", "grinder_clean")


def ymd_days(ymd: Any, today: tuple[int, int, int]) -> int | None:
    """Whole days from a YYYYMMDD integer to `today` (y, m, d), or None."""
    from datetime import date
    n = number(ymd)
    if not n or n < 19000101:
        return None
    n = int(n)
    try:
        d = date(n // 10000, (n // 100) % 100, n % 100)
        return (date(*today) - d).days
    except ValueError:
        return None


def visible_presets(library: Any) -> list[tuple[int, str]]:
    """(index, "Coffee — Recipe") for each preset that is not hidden, in library order."""
    if not isinstance(library, dict):
        return []
    coffees = {c.get("id"): c for c in library.get("coffees") or [] if isinstance(c, dict)}
    recipes = {r.get("id"): r for r in library.get("recipes") or [] if isinstance(r, dict)}
    out: list[tuple[int, str]] = []
    seen: set[str] = set()
    for i, p in enumerate(library.get("presets") or []):
        if not isinstance(p, dict) or p.get("hidden"):
            continue
        c, r = coffees.get(p.get("coffeeId")), recipes.get(p.get("recipeId"))
        label = ((c.get("name") + " — ") if c and c.get("name") else "") + ((r or {}).get("name") or "?")
        if label in seen:  # select options must be unique
            label = f"{label} ({i + 1})"
        seen.add(label)
        out.append((i, label))
    return out


def active_preset(library: Any) -> dict[str, Any]:
    if not isinstance(library, dict):
        return {}
    presets = library.get("presets") or []
    i = number(library.get("activePreset"))
    if i is None or not 0 <= int(i) < len(presets) or not isinstance(presets[int(i)], dict):
        return {}
    return presets[int(i)]


def coffee_in_use(library: Any) -> dict[str, Any]:
    if not isinstance(library, dict):
        return {}
    p = active_preset(library)
    for c in library.get("coffees") or []:
        if isinstance(c, dict) and p and c.get("id") == p.get("coffeeId"):
            return c
    return {}


def bag_in_use(library: Any) -> dict[str, Any]:
    if not isinstance(library, dict):
        return {}
    c = coffee_in_use(library)
    for b in library.get("bags") or []:
        if isinstance(b, dict) and c and b.get("id") == c.get("bagId"):
            return b
    return {}


def build_extra(library: Any, maintenance: Any, subs: Any, grind: Any,
                today: tuple[int, int, int]) -> dict[str, Any]:
    """Values for the entities that come from the slower reads; anything absent is None."""
    library = library if isinstance(library, dict) else {}
    maintenance = maintenance if isinstance(maintenance, dict) else {}
    subs = subs if isinstance(subs, dict) else {}
    grind = grind if isinstance(grind, dict) else {}
    out: dict[str, Any] = {}

    presets = visible_presets(library)
    out["preset_options"] = [label for _, label in presets]
    active_i = number(library.get("activePreset"))
    out["preset_current"] = next((label for i, label in presets if i == active_i), None)
    out["preset_index_by_label"] = {label: i for i, label in presets}

    coffee = coffee_in_use(library)
    bag = bag_in_use(library)
    out["coffee_name"] = text(coffee.get("name"))
    out["coffee_roaster"] = text(coffee.get("roaster"))
    out["coffee_id"] = number(coffee.get("id"))
    roast = bag.get("roastDate") or coffee.get("roastDate")
    out["bag_age_days"] = ymd_days(roast, today)

    out["shots_total"] = number(maintenance.get("shots_total"))
    for job in MAINTENANCE_JOBS:
        j = next((x for x in maintenance.get("jobs") or [] if isinstance(x, dict) and x.get("id") == job), {})
        out[f"{job}_since"] = number(j.get("since_shots"))
        out[f"{job}_every"] = number(j.get("every_shots"))
        out[f"{job}_due"] = flag(j.get("due")) if j else None

    rec = grind.get("recommendation") if isinstance(grind.get("recommendation"), dict) else {}
    out["grind_recommendation"] = positive(rec.get("setting"))
    out["grind_recommendation_source"] = text(rec.get("source"))
    out["grind_reminder_due"] = flag(grind.get("reminder_due"))

    active = [s for s in subs.get("subscriptions") or [] if isinstance(s, dict) and s.get("status", "active") == "active"]
    due = [s for s in active if s.get("due_soon") or s.get("stock_short")]
    out["subs_due_soon"] = len(due) if "subscriptions" in subs else None
    nxt = sorted((s for s in active if number(s.get("days_until")) is not None), key=lambda s: s["days_until"])
    out["subs_next"] = ({"name": text(nxt[0].get("name")), "date": nxt[0].get("next_date"),
                         "days_until": nxt[0].get("days_until")} if nxt else None)
    return out


def shot_summary(shot: Any) -> dict[str, Any]:
    """The small event payload for a new shot."""
    shot = shot if isinstance(shot, dict) else {}
    return {
        "id": number(shot.get("id")),
        "coffee": text(shot.get("coffee")),
        "recipe": text(shot.get("recipe")),
        "final_weight_g": number(shot.get("final_weight_g")),
        "target_weight_g": number(shot.get("target_weight_g")),
        "shot_time_s": number(shot.get("shot_time_s")),
        "target_time_s": number(shot.get("target_time_s")),
        "grind": positive(shot.get("grind_um")),
        "end_reason": text(shot.get("end_reason")),
    }


# --- shaping a controller answer for the panel (CONTRACT.md) -------------------------------------

UNREACHABLE = {"ok": False, "error": "The controller could not be reached"}


def shape_answer(status: int, content_type: str, raw: bytes, headers: dict[str, str] | None) -> dict[str, Any]:
    """{status, json} for JSON, else {status, text, headers} with lower-case header names."""
    import json
    body = (raw or b"").decode("utf-8", "replace")
    ctype = (content_type or "").lower()
    if "json" in ctype or body[:1] in ("{", "["):
        try:
            return {"status": status, "json": json.loads(body) if body else None}
        except ValueError:
            pass
    return {"status": status, "text": body,
            "headers": {str(k).lower(): str(v) for k, v in (headers or {}).items()}}


# --- services (services.py registers them; the mapping is here so it tests alone) ---------------

def service_request(service: str, data: dict[str, Any], snapshot: dict[str, Any]) -> tuple[str, dict | None, Any]:
    """(path, query, body) for a service call; pure so it tests alone. Raises ValueError when it
    cannot be built (for example no newest shot to rate)."""
    status = snapshot.get("status") or {}
    if service == "set_grind":
        return "/api/grind/set", {"um": int(data["setting"])}, None
    if service in ("rate_shot", "set_shot_tds"):
        shot_id = data.get("shot_id", status.get("last_shot_id"))
        if shot_id is None:
            raise ValueError("No shot to change yet")
        if service == "rate_shot":
            return "/api/shot/rate", {"id": int(shot_id), "rating": int(data["rating"]),
                                      "taste": int(data.get("taste", 0))}, None
        return "/api/shot/tds", {"id": int(shot_id), "tds": float(data["tds"])}, None
    if service == "bag_finished":
        coffee_id = data.get("coffee_id", snapshot.get("coffee_id"))
        if coffee_id is None:
            raise ValueError("No coffee in use")
        return "/api/library/do", {"what": "coffee_finish", "a": int(coffee_id)}, None
    if service == "subscription_arrived":
        return "/api/subscriptions/arrived", None, {"id": int(data["subscription_id"])}
    if service == "subscription_skip":
        return "/api/subscriptions/skip", None, {"id": int(data["subscription_id"])}
    if service == "maintenance_done":
        return "/api/maintenance", {"job": data["job"], "action": "done"}, None
    raise ValueError(f"unknown service {service}")
