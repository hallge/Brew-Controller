"""0.4.0 summary mappings: shots today, room effect, the power schedule, shelf, subscriptions, ratings."""
import importlib.util
import sys
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

_PKG = Path(__file__).resolve().parents[1] / "custom_components" / "brew_by_weight"
_spec = importlib.util.spec_from_file_location("bbw_api_summary", _PKG / "api.py")
api = importlib.util.module_from_spec(_spec)
sys.modules["bbw_api_summary"] = api
_spec.loader.exec_module(api)

TODAY = (2026, 11, 9)
NY = timezone(timedelta(hours=-5))


def epoch(y, mo, d, h=0, mi=0):
    return int(datetime(y, mo, d, h, mi, tzinfo=timezone.utc).timestamp())


def test_shots_today_counts_by_local_date():
    shots = [
        {"timestamp": "2026-11-09T14:00:00Z", "shot_time_s": 30.0, "target_time_s": 30},
        {"timestamp": "2026-11-09T13:00:00Z", "shot_time_s": 36.0, "target_time_s": 30},
        # 03:00 UTC on the 9th is still the 8th in New York
        {"timestamp": "2026-11-09T03:00:00Z", "shot_time_s": 28.0, "target_time_s": 30},
        {"timestamp": "", "shot_time_s": 28.0},
    ]
    day = api.shots_today(shots, TODAY, NY)
    assert day == {"count": 2, "average_time_s": 33.0, "on_time": 1}
    assert api.shots_today(shots, TODAY, timezone.utc)["count"] == 3
    assert api.shots_today(None, TODAY)["count"] is None
    assert api.shots_today([], TODAY)["count"] == 0


def test_room_effect():
    assert api.room_effect({})["room_shift_s"] is None                      # older firmware
    assert api.room_effect({"room": {"on": False}})["room_shift_s"] is None
    r = api.room_effect({"room": {"on": True, "learned": True, "applied": True, "s_per_c": -0.6,
                                  "shift_s": 1.2, "now_c": 19.5, "shots": 24, "span_c": 6}})
    assert r["room_shift_s"] == 1.2 and r["room_attrs"]["now_c"] == 19.5 and r["room_attrs"]["learned"]
    r = api.room_effect({"room": {"on": True, "learned": False, "applied": False, "shift_s": 0.8}})
    assert r["room_shift_s"] == 0


def test_next_power_events_weekdays_and_offset():
    # Monday 9 Nov 2026, 12:00 UTC = 07:00 local at UTC-5.
    now = epoch(2026, 11, 9, 12, 0)
    power = {"clock_set": True, "utc_offset_min_now": -300, "schedule": [
        {"enabled": True, "on": True, "minute": 6 * 60 + 30, "days": 0b0111110},   # 06:30 Mon-Fri
        {"enabled": True, "on": False, "minute": 9 * 60, "days": 0b0111110},       # 09:00 Mon-Fri
        {"enabled": False, "on": True, "minute": 8 * 60, "days": 0x7f},
    ]}
    nxt = api.next_power_events(power, now)
    assert nxt["next_off"] == epoch(2026, 11, 9, 14, 0)     # 09:00 local today
    assert nxt["next_on"] == epoch(2026, 11, 10, 11, 30)    # 06:30 local tomorrow (Tuesday)
    # Friday after the switch-off: the next switch-on is Monday.
    fri = epoch(2026, 11, 13, 15, 0)
    assert api.next_power_events(power, fri)["next_on"] == epoch(2026, 11, 16, 11, 30)
    assert api.next_power_events({"clock_set": False, "schedule": power["schedule"]}, now) == {"next_on": None, "next_off": None}
    assert api.next_power_events(None, now) == {"next_on": None, "next_off": None}


LIBRARY = {
    "activePreset": 0,
    "coffees": [{"id": 3, "name": "Honduras", "bagId": 9}, {"id": 4, "name": "Kenya", "bagId": 0}],
    "recipes": [{"id": 1, "name": "Double"}],
    "presets": [{"coffeeId": 3, "recipeId": 1}],
    "bags": [
        {"id": 9, "coffeeId": 3, "frozenDate": 20261020, "thawDate": 0, "current": True},
        {"id": 10, "coffeeId": 4, "unopened": True},
        {"id": 11, "coffeeId": 4, "unopened": True},
        {"id": 12, "coffeeId": 3, "onTheWay": True, "expectedDate": 20261116},
    ],
}
SUBS = {"monthly_cents": 4299, "subscriptions": [
    {"id": 1, "name": "Onyx", "kind": "coffee", "status": "active", "days_until": 5, "next_date": 20261114},
    {"id": 2, "name": "Filters", "kind": "supplies", "status": "active", "item": "paper filter", "stock": 23,
     "stock_days_left": 9, "stock_short": True, "days_until": 20, "next_date": 20261129},
    {"id": 3, "name": "Old", "kind": "supplies", "status": "cancelled", "stock": 1, "days_until": 1},
]}
POWER = {"paired": True, "belief": "on", "last": "switched on", "clock_set": True, "utc_offset_min_now": 0, "schedule": []}
RATINGS = {"named": True, "drinkers": ["Sam", "", "", "", "", "", "", ""], "ratings": [
    {"id": 1, "coffee": 4, "drinker": 0, "t": 100, "stars": 3},
    {"id": 2, "coffee": 3, "drinker": 1, "t": 200, "stars": 5},
]}


def test_build_summary():
    out = api.build_summary(LIBRARY, SUBS, POWER, RATINGS, {"version": "0.1.30"}, [], TODAY, epoch(2026, 11, 9))
    assert out["machine_power"] is True and out["power_bot_paired"] is True
    assert out["machine_power_attrs"]["last"] == "switched on"
    assert out["bags_unopened"] == 2 and out["bags_on_the_way"] == 1 and out["bag_frozen"] is True
    assert out["subs_monthly_spend"] == 42.99
    assert out["supplies_left"] == 23                                   # the cancelled one is left out
    assert out["supplies_attrs"]["supplies"][0]["short"] is True
    assert out["next_delivery"] == date(2026, 11, 14) and out["next_delivery_attrs"]["name"] == "Onyx"
    assert out["last_coffee_rating"] == 5
    assert out["last_coffee_rating_attrs"] == {"coffee": "Honduras", "drinker": "Sam", "t": 200}
    assert out["firmware_version"] == "0.1.30"
    assert out["shots_today"] == 0


def test_build_summary_with_nothing_known():
    out = api.build_summary(None, None, None, None, None, None, TODAY, 0)
    for key in ("machine_power", "bags_unopened", "bag_frozen", "subs_monthly_spend", "supplies_left",
                "next_delivery", "last_coffee_rating", "firmware_version", "shots_today"):
        assert out[key] is None, key
    assert api.build_summary(None, None, {"belief": "unknown"}, None, None, None, TODAY, 0)["machine_power"] is None


def test_snapshot_stop_target_and_rating():
    snap = api.build_snapshot({"scale_battery": 80, "scale_resting": True},
                              [{"target_weight_g": 36, "stop_target_g": 39.5, "rating": 4}], {})
    assert snap["last_shot_stop_target_g"] == 39.5 and snap["last_shot_rating"] == 4
    assert snap["scale_battery_pct"] == 80 and snap["scale_resting"] is True
    # older firmware: no stop_target_g, the recipe's target
    assert api.build_snapshot({}, [{"target_weight_g": 36}], {})["last_shot_stop_target_g"] == 36
