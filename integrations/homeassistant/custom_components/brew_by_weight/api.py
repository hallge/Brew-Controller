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
        "scale_battery_pct": number(status.get("scale_battery")),
        "scale_resting": flag(status.get("scale_resting")),
        "maintenance_due": number(status.get("maintenance_due")),
        "target_weight_g": positive(status.get("target_weight_g")),
        "recipe": text(status.get("recipe")),
        "last_shot_time_s": number(shot.get("shot_time_s")),
        "last_shot_weight_g": number(shot.get("final_weight_g")),
        "last_shot_grind": positive(shot.get("grind_um")),
        # What the shot stopped for; older firmware has only the recipe's target.
        "last_shot_stop_target_g": positive(shot.get("stop_target_g", shot.get("target_weight_g"))),
        "last_shot_rating": positive(shot.get("rating")),
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


# --- 0.4.0: the summary's figures as entities ---------------------------------------------------

def shot_local_date(shot: Any, tzinfo) -> tuple[int, int, int] | None:
    """The shot's date in `tzinfo` (its `timestamp` is ISO UTC, "2026-11-07T08:12:00Z"), or None."""
    from datetime import datetime, timezone
    stamp = text(shot.get("timestamp")) if isinstance(shot, dict) else None
    if not stamp:
        return None
    try:
        t = datetime.fromisoformat(stamp.replace("Z", "+00:00"))
    except ValueError:
        return None
    if t.tzinfo is None:
        t = t.replace(tzinfo=timezone.utc)
    d = t.astimezone(tzinfo).date() if tzinfo else t.date()
    return d.year, d.month, d.day


ON_TIME_S = 3  # the grind advice's default tolerance, when /api/grind gives none


def shots_today(shots: Any, today: tuple[int, int, int], tzinfo=None, tolerance_s: Any = None) -> dict[str, Any]:
    """{count, average_time_s, on_time} of today's shots (local date); None without a shot list.
    On time: within the grind tolerance of the recipe's target time (lib/charts.js dialIn)."""
    tol = number(tolerance_s) or ON_TIME_S
    if not isinstance(shots, list):
        return {"count": None, "average_time_s": None, "on_time": None}
    todays = [s for s in shots if isinstance(s, dict) and shot_local_date(s, tzinfo) == today]
    times = [t for t in (number(s.get("shot_time_s")) for s in todays) if t is not None]
    on_time = sum(1 for s in todays
                  if number(s.get("target_time_s")) and number(s.get("shot_time_s")) is not None
                  and abs(s["shot_time_s"] - s["target_time_s"]) <= tol)
    return {"count": len(todays), "average_time_s": round(sum(times) / len(times), 1) if times else None,
            "on_time": on_time}


def room_effect(grind: Any) -> dict[str, Any]:
    """/api/grind `room` (v0.1.30): seconds the room moved the newest shot's time (0 when not
    applied), with its figures; None when the firmware has no room block or it is switched off."""
    room = grind.get("room") if isinstance(grind, dict) and isinstance(grind.get("room"), dict) else None
    if not room or room.get("on") is False:
        return {"room_shift_s": None, "room_attrs": None}
    shift = number(room.get("shift_s")) if room.get("applied") else 0
    return {
        "room_shift_s": shift,
        "room_attrs": {"learned": flag(room.get("learned")), "applied": flag(room.get("applied")),
                       "s_per_c": number(room.get("s_per_c")), "now_c": number(room.get("now_c")),
                       "shots": number(room.get("shots")), "span_c": number(room.get("span_c"))},
    }


def next_power_events(power: Any, now_epoch: int) -> dict[str, int | None]:
    """Epoch seconds of the next scheduled switch-on and switch-off, or None.

    `/api/power` `schedule[]` is {enabled, on, minute (local, 0..1439), days (bit 0 Sunday)}; the
    controller's local time is UTC + `utc_offset_min_now` (src/core/PowerControl.h). The offset of
    now is used for the week ahead, so a daylight-saving change in between moves it by an hour.
    The panel's lib/schedule.js is the same rule.
    """
    out: dict[str, int | None] = {"next_on": None, "next_off": None}
    if not isinstance(power, dict) or not isinstance(power.get("schedule"), list) or power.get("clock_set") is False:
        return out
    offset_s = int(number(power.get("utc_offset_min_now", power.get("utc_offset_min"))) or 0) * 60
    local_now = int(now_epoch) + offset_s
    day_start = local_now - local_now % 86400
    for e in power["schedule"]:
        if not isinstance(e, dict) or not e.get("enabled"):
            continue
        minute, days = number(e.get("minute")), number(e.get("days"))
        if minute is None or days is None:
            continue
        key = "next_on" if e.get("on", True) else "next_off"
        for k in range(8):
            start = day_start + k * 86400
            weekday_sun0 = (start // 86400 + 4) % 7   # 1 Jan 1970 was a Thursday
            t = start + int(minute) * 60
            if t > local_now and int(days) & (1 << weekday_sun0):
                at = t - offset_s
                if out[key] is None or at < out[key]:
                    out[key] = at
                break
    return out


def _ymd_date(ymd: Any):
    from datetime import date
    n = number(ymd)
    if not n or n < 19000101:
        return None
    n = int(n)
    try:
        return date(n // 10000, (n // 100) % 100, n % 100)
    except ValueError:
        return None


def drinker_name(ratings: dict[str, Any], drinker: Any) -> str:
    d = int(number(drinker) or 0)
    names = ratings.get("drinkers") or []
    if d == 0:
        return "household"
    if ratings.get("named") and 0 < d <= len(names) and names[d - 1]:
        return str(names[d - 1])
    return f"drinker {d}"


def build_summary(library: Any, subs: Any, power: Any, ratings: Any, update: Any, shots: Any,
                  today: tuple[int, int, int], now_epoch: int, tzinfo=None, grind: Any = None) -> dict[str, Any]:
    """Values for the 0.4.0 entities; anything absent is None."""
    library = library if isinstance(library, dict) else {}
    subs = subs if isinstance(subs, dict) else {}
    out: dict[str, Any] = {}

    day = shots_today(shots, today, tzinfo, grind.get("tolerance_s") if isinstance(grind, dict) else None)
    out["shots_today"] = day["count"]
    out["shots_today_attrs"] = {"average_time_s": day["average_time_s"], "on_time": day["on_time"]}

    if isinstance(power, dict) and power:
        belief = power.get("belief")
        out["machine_power"] = True if belief == "on" else False if belief == "off" else None
        out["power_bot_paired"] = flag(power.get("paired"))
        out["machine_power_attrs"] = {"last": text(power.get("last")), **next_power_events(power, now_epoch)}
    else:
        out["machine_power"] = out["power_bot_paired"] = out["machine_power_attrs"] = None

    has_bags = isinstance(library.get("bags"), list)
    bags = [b for b in library.get("bags") or [] if isinstance(b, dict)]
    out["bags_unopened"] = sum(1 for b in bags if b.get("unopened")) if has_bags else None
    out["bags_on_the_way"] = sum(1 for b in bags if b.get("onTheWay")) if has_bags else None
    src = bag_in_use(library) or coffee_in_use(library)
    out["bag_frozen"] = bool(src.get("frozenDate") and not src.get("thawDate")) if src else None

    if isinstance(subs.get("subscriptions"), list):
        cents = number(subs.get("monthly_cents"))
        out["subs_monthly_spend"] = round(cents / 100, 2) if cents is not None else None
        active = [x for x in subs["subscriptions"] if isinstance(x, dict) and x.get("status", "active") == "active"]
        supplies = [x for x in active if x.get("kind") == "supplies" and number(x.get("stock")) is not None]
        out["supplies_left"] = min((x["stock"] for x in supplies), default=None)
        out["supplies_attrs"] = {"supplies": [
            {"name": text(x.get("name")), "item": text(x.get("item")), "stock": x.get("stock"),
             "days_left": number(x.get("stock_days_left")), "short": bool(x.get("stock_short"))} for x in supplies]}
        nxt = sorted((x for x in active if number(x.get("days_until")) is not None), key=lambda x: x["days_until"])
        out["next_delivery"] = _ymd_date(nxt[0].get("next_date")) if nxt else None
        out["next_delivery_attrs"] = ({"name": text(nxt[0].get("name")), "days_until": nxt[0]["days_until"],
                                       "due_soon": bool(nxt[0].get("due_soon"))} if nxt else None)
    else:
        out["subs_monthly_spend"] = out["supplies_left"] = out["next_delivery"] = None
        out["supplies_attrs"] = out["next_delivery_attrs"] = None

    rows = [r for r in ratings.get("ratings") or [] if isinstance(r, dict)] if isinstance(ratings, dict) else []
    newest = max(rows, key=lambda r: number(r.get("t")) or 0) if rows else None
    if newest:
        coffee = next((c for c in library.get("coffees") or []
                       if isinstance(c, dict) and c.get("id") == newest.get("coffee")), {})
        out["last_coffee_rating"] = number(newest.get("stars"))
        out["last_coffee_rating_attrs"] = {"coffee": text(coffee.get("name")),
                                           "drinker": drinker_name(ratings, newest.get("drinker")),
                                           "t": number(newest.get("t"))}
    else:
        out["last_coffee_rating"] = out["last_coffee_rating_attrs"] = None

    out["firmware_version"] = text(update.get("version")) if isinstance(update, dict) else None
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
        # What the shot stopped for (its own target when a grinder add-on scaled it); older
        # firmware has no such field: the recipe's target.
        "stop_target_g": number(shot.get("stop_target_g", shot.get("target_weight_g"))),
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
