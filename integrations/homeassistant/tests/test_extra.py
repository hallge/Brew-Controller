"""0.3.0 mappings: machine states, slower reads, services, panel answer shapes. No Home Assistant needed."""
import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
_PKG = Path(__file__).resolve().parents[1] / "custom_components" / "brew_by_weight"
_spec = importlib.util.spec_from_file_location("bbw_api_extra", _PKG / "api.py")
api = importlib.util.module_from_spec(_spec)
sys.modules["bbw_api_extra"] = api
_spec.loader.exec_module(api)

TODAY = (2026, 11, 7)
LIBRARY = {
    "activePreset": 1,
    "coffees": [
        {"id": 3, "name": "Honduras", "roaster": "Onyx", "roastDate": 20261028, "bagId": 9},
        {"id": 4, "name": "Kenya", "roastDate": 20261020, "bagId": 0},
    ],
    "recipes": [{"id": 1, "name": "Triple"}, {"id": 2, "name": "Double"}],
    "bags": [{"id": 9, "coffeeId": 3, "roastDate": 20261101}],
    "presets": [
        {"coffeeId": 4, "recipeId": 2, "hidden": False},
        {"coffeeId": 3, "recipeId": 1, "hidden": False},
        {"coffeeId": 4, "recipeId": 1, "hidden": True},
    ],
}
MAINT = {"shots_total": 27, "jobs": [
    {"id": "backflush", "since_shots": 27, "every_shots": 200, "due": False},
    {"id": "descale", "since_shots": 27, "every_shots": 0, "due": False},
    {"id": "grinder_clean", "since_shots": 510, "every_shots": 500, "due": True},
]}
SUBS = {"subscriptions": [
    {"id": 1, "name": "Onyx", "status": "active", "days_until": 5, "next_date": 20261112, "due_soon": False},
    {"id": 2, "name": "Filters", "status": "active", "days_until": 1, "next_date": 20261108, "due_soon": True},
    {"id": 3, "name": "Old", "status": "cancelled", "days_until": 0, "due_soon": True},
]}
GRIND = {"recommendation": {"setting": 52, "source": "community"}, "reminder_due": True}


def test_states_match_firmware():
    text = (ROOT / "src" / "core" / "Types.h").read_text(encoding="utf-8")
    firmware = re.findall(r'case State::(\w+): return "\1";', text)
    assert firmware and tuple(firmware) == api.STATES


def test_status_option():
    assert api.status_option("READY") == "ready"
    assert api.status_option("CONNECTING_SCALE") == "connecting_scale"
    assert api.status_option("NEW_STATE") is None and api.status_option(None) is None


def test_presets_and_coffee_in_use():
    x = api.build_extra(LIBRARY, MAINT, SUBS, GRIND, TODAY)
    assert x["preset_options"] == ["Kenya — Double", "Honduras — Triple"]
    assert x["preset_current"] == "Honduras — Triple"
    assert x["preset_index_by_label"] == {"Kenya — Double": 0, "Honduras — Triple": 1}
    assert (x["coffee_name"], x["coffee_roaster"], x["coffee_id"]) == ("Honduras", "Onyx", 3)
    assert x["bag_age_days"] == 6  # the bag's roast date wins over the coffee's


def test_maintenance_grind_subscriptions():
    x = api.build_extra(LIBRARY, MAINT, SUBS, GRIND, TODAY)
    assert (x["shots_total"], x["backflush_since"], x["backflush_every"], x["backflush_due"]) == (27, 27, 200, False)
    assert x["grinder_clean_due"] is True
    assert (x["grind_recommendation"], x["grind_recommendation_source"], x["grind_reminder_due"]) == (52, "community", True)
    assert x["subs_due_soon"] == 1
    assert x["subs_next"] == {"name": "Filters", "date": 20261108, "days_until": 1}


def test_garbage_is_none_not_an_error():
    for bad in (None, [], "x", 5):
        x = api.build_extra(bad, bad, bad, bad, TODAY)
        assert x["preset_options"] == [] and x["coffee_name"] is None and x["bag_age_days"] is None
        assert x["shots_total"] is None and x["subs_due_soon"] is None and x["subs_next"] is None


def test_duplicate_preset_labels_are_made_unique():
    lib = dict(LIBRARY, presets=[{"coffeeId": 3, "recipeId": 1}, {"coffeeId": 3, "recipeId": 1}])
    assert api.visible_presets(lib) == [(0, "Honduras — Triple"), (1, "Honduras — Triple (2)")]


def test_ymd_days():
    assert api.ymd_days(20261101, TODAY) == 6
    assert api.ymd_days(0, TODAY) is None and api.ymd_days(20261399, TODAY) is None


def test_shot_summary():
    s = api.shot_summary({"id": 34, "coffee": "Honduras", "recipe": "Triple", "final_weight_g": 50.4,
                          "target_weight_g": 48, "shot_time_s": 17.6, "target_time_s": 29, "grind_um": 74})
    assert s["id"] == 34 and s["grind"] == 74 and s["final_weight_g"] == 50.4
    # Older firmware has no stop_target_g: the shot stopped for the recipe's target.
    assert s["stop_target_g"] == 48
    # A shot with a target of its own (a grinder add-on's measured dose): kept apart from the recipe's.
    own = api.shot_summary({"id": 35, "final_weight_g": 49.0, "target_weight_g": 48, "stop_target_g": 49.0})
    assert own["target_weight_g"] == 48 and own["stop_target_g"] == 49.0


def test_shape_answer():
    assert api.shape_answer(200, "application/json", b'{"ok":true}', {}) == {"status": 200, "json": {"ok": True}}
    t = api.shape_answer(200, "text/plain", b"line\n", {"X-Log-Next": "42"})
    assert t == {"status": 200, "text": "line\n", "headers": {"x-log-next": "42"}}
    assert api.shape_answer(404, "", b"[1,2]", None)["json"] == [1, 2]


def test_service_requests():
    snap = {"status": {"last_shot_id": 34}, "coffee_id": 3}
    assert api.service_request("set_grind", {"setting": 54}, snap) == ("/api/grind/set", {"um": 54}, None)
    assert api.service_request("rate_shot", {"rating": 4, "taste": 2}, snap) == (
        "/api/shot/rate", {"id": 34, "rating": 4, "taste": 2}, None)
    assert api.service_request("set_shot_tds", {"tds": 9.1, "shot_id": 30}, snap) == (
        "/api/shot/tds", {"id": 30, "tds": 9.1}, None)
    assert api.service_request("bag_finished", {}, snap) == ("/api/library/do", {"what": "coffee_finish", "a": 3}, None)
    assert api.service_request("subscription_arrived", {"subscription_id": 2}, snap) == (
        "/api/subscriptions/arrived", None, {"id": 2})
    assert api.service_request("maintenance_done", {"job": "descale"}, snap) == (
        "/api/maintenance", {"job": "descale", "action": "done"}, None)


def test_service_requests_pass_the_proxy():
    spec = importlib.util.spec_from_file_location("bbw_proxy_extra", _PKG / "proxy.py")
    proxy = importlib.util.module_from_spec(spec)
    sys.modules["bbw_proxy_extra"] = proxy
    spec.loader.exec_module(proxy)
    snap = {"status": {"last_shot_id": 34}, "coffee_id": 3}
    for name, data in [("set_grind", {"setting": 54}), ("rate_shot", {"rating": 4}), ("set_shot_tds", {"tds": 9}),
                       ("bag_finished", {}), ("subscription_arrived", {"subscription_id": 1}),
                       ("subscription_skip", {"subscription_id": 1}), ("maintenance_done", {"job": "backflush"})]:
        path, query, body = api.service_request(name, data, snap)
        assert proxy.check("POST", path, query, body).allowed, name


def test_service_request_needs_a_shot():
    try:
        api.service_request("rate_shot", {"rating": 3}, {})
    except ValueError:
        return
    raise AssertionError("expected ValueError")
