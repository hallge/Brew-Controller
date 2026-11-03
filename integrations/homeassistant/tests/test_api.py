"""Tests of the plain-Python helpers; no Home Assistant needed.

api.py is loaded by path so importing the package's __init__ (which needs Home Assistant) is avoided.
"""
import importlib.util
import json
from pathlib import Path

_PKG = Path(__file__).resolve().parents[1] / "custom_components" / "brew_by_weight"
_spec = importlib.util.spec_from_file_location("bbw_api", _PKG / "api.py")
api = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(api)

STATUS = {"state": "IDLE", "web_start": True, "weight_g": 0.0}
SHOTS = [{"id": 7, "shot_time_s": 27.4, "final_weight_g": 36.1, "grind_um": 540}]
GRIND = {"action": "finer", "suggested_um": 520, "has_number": True, "shot_noise_s": 1.3}


def test_full_snapshot():
    s = api.build_snapshot(STATUS, SHOTS, GRIND)
    assert s["state"] == "IDLE"
    assert s["last_shot_time_s"] == 27.4
    assert s["last_shot_weight_g"] == 36.1
    assert s["last_shot_grind"] == 540
    assert s["advised_grind"] == 520
    assert s["advised_action"] == "finer"
    assert s["shot_noise_s"] == 1.3


def test_absent_optional_fields_are_none():
    s = api.build_snapshot(STATUS, SHOTS, GRIND)
    for key in ("drift_s_per_day", "ambient_rh_pct", "ambient_temp_c"):
        assert s[key] is None


def test_new_fields_when_present():
    shots = [dict(SHOTS[0], ambient_rh_pct=48.5, ambient_temp_c=21.0)]
    grind = dict(GRIND, drift_s_per_day=-0.2)
    s = api.build_snapshot(STATUS, shots, grind)
    assert (s["ambient_rh_pct"], s["ambient_temp_c"], s["drift_s_per_day"]) == (48.5, 21.0, -0.2)


def test_garbage_never_raises():
    for bad in (None, [], "x", 5, {}, [None], [[]]):
        s = api.build_snapshot(bad, bad, bad)
        assert set(s) >= {"state", "last_shot_time_s", "advised_grind"}
        assert all(v is None for v in s.values())


def test_empty_history_and_zero_means_unknown():
    s = api.build_snapshot(STATUS, [], dict(GRIND, suggested_um=0))
    assert s["last_shot_time_s"] is None
    assert s["advised_grind"] is None
    s = api.build_snapshot(STATUS, [dict(SHOTS[0], grind_um=0)], GRIND)
    assert s["last_shot_grind"] is None


def test_no_number_hides_advised_grind():
    assert api.build_snapshot(STATUS, SHOTS, dict(GRIND, has_number=False))["advised_grind"] is None


def test_bool_and_string_are_not_numbers():
    s = api.build_snapshot(STATUS, [{"shot_time_s": True, "final_weight_g": "36"}], GRIND)
    assert s["last_shot_time_s"] is None and s["last_shot_weight_g"] is None


def test_web_start_flag():
    assert api.build_snapshot({"web_start": False}, [], {})["web_start"] is False
    assert api.build_snapshot({}, [], {})["web_start"] is None


def test_normalize_host():
    assert api.normalize_host(" brewbyweight.local/ ") == "http://brewbyweight.local"
    assert api.normalize_host("http://brewbyweight.local") == "http://brewbyweight.local"
    assert api.normalize_host("") == ""


def test_error_message():
    body = {"ok": False, "error": "Start from the web page is disabled (Network section)"}
    assert api.error_message(body, "d") == body["error"]
    assert api.error_message(None, "d") == "d"
    assert api.error_message({"error": ""}, "d") == "d"


def test_manifest_and_translations():
    manifest = json.loads((_PKG / "manifest.json").read_text())
    assert manifest["domain"] == "brew_by_weight" and manifest["config_flow"] is True
    assert manifest["iot_class"] == "local_polling" and manifest["version"] == "0.1.0"
    assert json.loads((_PKG / "strings.json").read_text()) == json.loads(
        (_PKG / "translations" / "en.json").read_text())
