#!/usr/bin/env bash
# Prints the UDID of an iPhone simulator on the newest iOS runtime (a Pro model if there is
# one). Output: the UDID only.
set -euo pipefail
python3 - <<'PY'
import json, subprocess, sys

def simctl(*args):
    return subprocess.run(["xcrun", "simctl", *args], check=True, capture_output=True, text=True).stdout

devices = json.loads(simctl("list", "devices", "available", "-j"))["devices"]
phones = [(rt, d) for rt, ds in devices.items() if ".iOS-" in rt for d in ds if d["name"].startswith("iPhone")]
if not phones:
    sys.exit("no iPhone simulator available")
def rank(item):
    rt, d = item
    ver = [int(p) for p in rt.rsplit("iOS-", 1)[-1].split("-") if p.isdigit()]
    return (ver, "Pro" in d["name"] and "Max" not in d["name"])
print(max(phones, key=rank)[1]["udid"])
PY
