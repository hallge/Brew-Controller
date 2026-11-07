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


def test_grey_zone_settings_are_stripped_or_refused():
    d = proxy.check("POST", "/api/settings", None, {"pressDurationMs": 300, "scaleRestMin": 5})
    assert not d.allowed
    d = proxy.check("POST", "/api/settings", None, {"grindToleranceS": 2, "actuationMode": 1})
    assert d.allowed and d.body == {"grindToleranceS": 2} and d.dropped_keys == ["actuationMode"]


def test_software_calls_pass():
    for method, path, query, body in [
        ("GET", "/api/status", None, None),
        ("GET", "/api/shots", {"limit": 500}, None),
        ("POST", "/api/shot/rate", {"id": 3, "rating": 4, "taste": 2}, None),
        ("POST", "/api/coffee", None, {"name": "x"}),
        ("POST", "/api/library/do", {"what": "preset_select", "a": 1}, None),
        ("POST", "/api/power", None, {"action": "believe_on"}),
        ("POST", "/api/maintenance", {"job": "backflush", "action": "done"}, None),
    ]:
        assert proxy.check(method, path, query, body).allowed, (method, path)


def test_unknown_library_action_and_paths_are_refused():
    assert not proxy.check("POST", "/api/library/do", {"what": "factory_reset"}, None).allowed
    assert not proxy.check("GET", "/api/../api/cmd", None, None).allowed
    assert not proxy.check("GET", "/api/whatever", None, None).allowed
    assert not proxy.check("DELETE", "/api/coffee", None, None).allowed


def test_backup_is_admin_only():
    d = proxy.check("GET", "/api/backup", {"secrets": 1}, None)
    assert d.allowed and d.admin_only
