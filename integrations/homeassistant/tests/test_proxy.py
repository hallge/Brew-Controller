"""The Brew panel's proxy must never pass on a request that operates the hardware."""
import importlib.util
import sys
from pathlib import Path

_PKG = Path(__file__).resolve().parents[1] / "custom_components" / "brew_by_weight"
_spec = importlib.util.spec_from_file_location("bbw_proxy_test", _PKG / "proxy.py")
proxy = importlib.util.module_from_spec(_spec)
sys.modules["bbw_proxy_test"] = proxy
_spec.loader.exec_module(proxy)

HARDWARE_CALLS = [
    ("POST", "/api/cmd", {"c": "start"}, None),
    ("POST", "/api/cmd", {"c": "stop"}, None),
    ("POST", "/api/cmd", {"c": "tare"}, None),
    ("POST", "/api/power", None, {"action": "press"}),
    ("POST", "/api/power", None, {"action": "on"}),
    ("POST", "/api/power", None, {"action": "off"}),
    ("POST", "/api/power", None, {"action": "schedule", "schedule": []}),
    ("POST", "/api/pair", None, {"kind": "scale", "address": ""}),
    ("POST", "/api/bot/test", {"role": "brew"}, None),
    ("GET", "/api/bot/test", None, None),
    ("POST", "/api/pusher", None, {}),
    ("GET", "/api/pusher", {"role": "brew"}, None),
    ("POST", "/api/net", None, {"staSsid": "x"}),
    ("GET", "/api/net", None, None),
    ("POST", "/api/update/install", None, None),
    ("POST", "/api/update/boot", {"slot": "factory"}, None),
    ("POST", "/api/update", None, None),
    ("POST", "/api/update/config", None, {}),
    ("POST", "/api/restore", None, None),
    ("POST", "/api/restore/apply", None, None),
    ("POST", "/api/time", {"epoch": 1}, None),
    ("POST", "/api/machine", None, {"maker": "x"}),
    ("POST", "/api/share/machine", None, None),
    ("GET", "/api/devices", None, None),
]


def test_every_hardware_call_is_refused():
    for method, path, query, body in HARDWARE_CALLS:
        assert not proxy.check(method, path, query, body).allowed, (method, path, body)


SETUP_CALLS = [
    ("POST", "/api/settings", None, {"grindToleranceS": 2}),
    ("POST", "/api/homeassistant", None, {"url": "http://ha"}),
    ("POST", "/api/coffee", None, {"name": "x"}),
    ("POST", "/api/bag", None, {"coffeeId": 3}),
    ("POST", "/api/recipe", None, {"name": "x"}),
    ("POST", "/api/grinder", None, {"model": "x"}),
    ("POST", "/api/drinkers", None, {"names": []}),
    ("POST", "/api/subscriptions", None, {"name": "x"}),
    ("POST", "/api/subscriptions/delete", None, {"id": 1}),
    ("POST", "/api/share/coffee", None, None),
    ("POST", "/api/links/suggest", None, {"url": "https://x"}),
    ("POST", "/api/diagnostics/send", None, None),
    ("POST", "/api/log/config", None, {}),
    ("POST", "/api/history/clear", None, None),
    ("POST", "/api/shot/grind", {"id": 1, "um": 40}, None),
    ("POST", "/api/power", None, {"action": "believe_on"}),
    ("POST", "/api/maintenance", {"job": "descale", "action": "every", "every": 200}, None),
    ("POST", "/api/library/do", {"what": "preset_select", "a": 1}, None),
    ("POST", "/api/library/do", {"what": "coffee_delete", "a": 1}, None),
    ("POST", "/api/library/do", {"what": "preset_grind", "a": 1, "b": 40}, None),
]


def test_setup_is_refused_in_the_panel():
    for method, path, query, body in SETUP_CALLS:
        assert not proxy.check(method, path, query, body).allowed, (method, path, query)


def test_everyday_actions_pass():
    for method, path, query, body in [
        ("GET", "/api/status", None, None),
        ("GET", "/api/shots", {"limit": 500}, None),
        ("GET", "/api/addons", None, None),
        ("GET", "/api/power", None, None),
        ("POST", "/api/shot/rate", {"id": 3, "rating": 4, "taste": 2}, None),
        ("POST", "/api/shot/tds", {"id": 3, "tds": 9.1}, None),
        ("POST", "/api/coffee/rate", None, {"coffee": 3, "bag": 9, "drinker": 0, "stars": 4}),
        ("POST", "/api/grind/set", {"um": 52}, None),
        ("POST", "/api/grind/recommendation", {"use": 1}, None),
        ("POST", "/api/library/do", {"what": "coffee_finish", "a": 3}, None),
        ("POST", "/api/library/do", {"what": "bag_open", "a": 9}, None),
        ("POST", "/api/library/do", {"what": "bag_arrived", "a": 9}, None),
        ("POST", "/api/subscriptions/arrived", None, {"id": 1}),
        ("POST", "/api/subscriptions/skip", None, {"id": 1}),
        ("POST", "/api/maintenance", {"job": "backflush", "action": "done"}, None),
    ]:
        assert proxy.check(method, path, query, body).allowed, (method, path, query)


def test_entities_may_choose_the_preset_but_not_more():
    assert proxy.check("POST", "/api/library/do", {"what": "preset_select", "a": 1}, None, scope="entity").allowed
    assert not proxy.check("POST", "/api/library/do", {"what": "coffee_delete", "a": 1}, None, scope="entity").allowed
    assert not proxy.check("POST", "/api/settings", None, {"grindToleranceS": 2}, scope="entity").allowed
    for method, path, query, body in HARDWARE_CALLS:
        assert not proxy.check(method, path, query, body, scope="entity").allowed, (method, path)


def test_unknown_library_action_and_paths_are_refused():
    assert not proxy.check("POST", "/api/library/do", {"what": "factory_reset"}, None).allowed
    assert not proxy.check("GET", "/api/../api/cmd", None, None).allowed
    assert not proxy.check("GET", "/api/whatever", None, None).allowed
    assert not proxy.check("DELETE", "/api/coffee", None, None).allowed


def test_backup_is_admin_only():
    d = proxy.check("GET", "/api/backup", {"secrets": 1}, None)
    assert d.allowed and d.admin_only
