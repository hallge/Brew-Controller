#!/usr/bin/env python3
"""Fleet view of Brew Controller diagnostic reports.

    python fleet.py <folder> [--since YYYY-MM-DD] [--json] [--emulator]

<folder> is a local clone of the private reports repo, or any folder holding
envelopes (for example fixtures/). Every *.json file under reports/ is read;
emulator/ is left out unless --emulator is given. The report format is in
docs/TELEMETRY.md.
"""
import argparse
import calendar
import datetime as dt
import json
import os
import statistics
import sys
import time
from collections import Counter, defaultdict

KINDS = ("diag_daily", "diag_crash")
BOT_ROLES = ("brew", "power", "stop")


# ---------------------------------------------------------------- loading

def parse_date(s):
    """YYYY-MM-DD -> UTC seconds at the start of that day."""
    return calendar.timegm(time.strptime(s, "%Y-%m-%d"))


def day(at):
    return time.strftime("%Y-%m-%d", time.gmtime(at)) if at else "?"


def load(folder, since=None, emulator=False):
    """Reads every envelope under folder. Returns (records, problems).

    A record is {"kid", "path", "alg", "body"}; problems lists files that
    could not be read, as (path, reason)."""
    records, problems = [], []
    for root, dirs, files in os.walk(folder):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        rel = os.path.relpath(root, folder).replace("\\", "/")
        if not emulator and (rel == "emulator" or rel.startswith("emulator/")):
            dirs[:] = []
            continue
        for name in sorted(files):
            if not name.endswith(".json"):
                continue
            path = os.path.join(root, name)
            try:
                with open(path, encoding="utf-8") as f:
                    env = json.load(f)
                body = env["body"]
                body = json.loads(body) if isinstance(body, str) else body
                if body.get("t") not in KINDS:
                    raise ValueError(f"unknown kind {body.get('t')!r}")
            except (OSError, ValueError, KeyError, TypeError, AttributeError) as e:
                problems.append((path, str(e)))
                continue
            kid = env.get("kid") or os.path.basename(root)
            at = body.get("at") or 0
            if since is not None and at < since:
                continue
            records.append({"kid": kid, "path": path, "alg": env.get("alg"), "body": body})
    records.sort(key=lambda r: (r["body"].get("at") or 0, r["path"]))
    return records, problems


# ---------------------------------------------------------------- analysis

def _bump(counter, d):
    for k, v in (d or {}).items():
        if isinstance(v, (int, float)):
            counter[k] += v


def _spread(medians, maxes):
    if not medians and not maxes:
        return None
    out = {"reports": len(medians)}
    if medians:
        out["median"] = statistics.median(medians)
        out["lowest_median"] = min(medians)
        out["highest_median"] = max(medians)
    if maxes:
        out["max"] = max(maxes)
    return out


def crash_key(body):
    """Groups crashes by dump.pc plus the first 3 backtrace addresses."""
    dump = body.get("dump")
    if not dump:
        return ("no dump", body.get("reset") or "?")
    return (dump.get("pc") or "?",) + tuple((dump.get("bt") or [])[:3])


def crash_groups(records):
    groups = {}
    for r in records:
        b = r["body"]
        if b.get("t") != "diag_crash":
            continue
        key = crash_key(b)
        g = groups.setdefault(key, {"key": list(key), "count": 0, "units": set(), "versions": set(),
                                    "causes": Counter(), "tasks": Counter(), "crashes": []})
        g["count"] += 1
        g["units"].add(r["kid"])
        g["versions"].add(b.get("fw") or "?")
        dump = b.get("dump") or {}
        g["causes"][dump.get("cause") or b.get("reset") or "?"] += 1
        if dump.get("task"):
            g["tasks"][dump["task"]] += 1
        g["crashes"].append(r)
    out = []
    for g in groups.values():
        newest = max(g["crashes"], key=lambda r: r["body"].get("at") or 0)
        out.append({"key": g["key"], "count": g["count"], "units": sorted(g["units"]),
                    "versions": sorted(g["versions"]), "causes": dict(g["causes"]),
                    "tasks": dict(g["tasks"]), "newest": newest, "crashes": g["crashes"]})
    out.sort(key=lambda g: (-g["count"], -len(g["units"]), g["key"]))
    return out


def analyse(records, now=None, silent_days=3):
    now = time.time() if now is None else now
    units = {}
    hw_scale, hw_bots, hw_machines = defaultdict(set), defaultdict(set), defaultdict(set)
    links = defaultdict(lambda: {"attempts": 0, "failures": 0, "connects": 0,
                                 "longest_wait_ms": 0, "units": set(), "reports": 0})
    frames = defaultdict(lambda: {"frames": 0, "bad": 0, "dropped": 0, "units": set()})
    shots = defaultdict(lambda: {"units": set(), "count": 0, "end": Counter(), "faults": Counter(),
                                 "stop_retried": 0, "_lat_med": [], "_lat_max": [],
                                 "_over_med": [], "_over_max": []})
    power_total = Counter()
    power_by_machine = defaultdict(Counter)
    warnings = defaultdict(lambda: {"n": 0, "units": set()})
    counts = Counter()

    for r in records:
        b, kid = r["body"], r["kid"]
        at = b.get("at") or 0
        counts[b["t"]] += 1
        u = units.setdefault(kid, {"kid": kid, "fw": None, "last_at": 0, "daily": 0, "crash": 0})
        u["daily" if b["t"] == "diag_daily" else "crash"] += 1
        if at >= u["last_at"]:
            u["last_at"], u["fw"] = at, b.get("fw") or "?"
        if b["t"] != "diag_daily":
            continue

        hw = b.get("hw") or {}
        scale = hw.get("scale") or {}
        scale_driver = scale.get("driver") or "?"
        hw_scale[f"{scale_driver} / {scale.get('name') or '?'}"].add(kid)
        bot_kind = {}
        for bot in hw.get("bots") or []:
            role, kind = bot.get("role") or "?", bot.get("kind") or "?"
            bot_kind[role] = kind
            hw_bots[(role, kind)].add(kid)
        m = hw.get("machine") or {}
        machine = m.get("key") or "?"
        hw_machines[f"{machine} ({m.get('style') or '?'})"].add(kid)

        for role, l in (b.get("links") or {}).items():
            if role == "scale_frames":
                f = frames[scale_driver]
                for k in ("frames", "bad", "dropped"):
                    f[k] += l.get(k) or 0
                f["units"].add(kid)
                continue
            if not isinstance(l, dict):
                continue
            device = scale_driver if role == "scale" else bot_kind.get(role, "?")
            e = links[(role, device)]
            for k in ("attempts", "failures", "connects"):
                e[k] += l.get(k) or 0
            e["longest_wait_ms"] = max(e["longest_wait_ms"], l.get("longest_wait_ms") or 0)
            e["units"].add(kid)
            e["reports"] += 1

        s = b.get("shots") or {}
        if s:
            e = shots[machine]
            e["units"].add(kid)
            e["count"] += s.get("count") or 0
            _bump(e["end"], s.get("end"))
            _bump(e["faults"], s.get("faults"))
            e["stop_retried"] += s.get("stop_retried") or 0
            if s.get("count"):
                lat, over = s.get("stop_latency_ms") or {}, s.get("overshoot_g") or {}
                for src, key, dst in ((lat, "median", "_lat_med"), (lat, "max", "_lat_max"),
                                      (over, "median", "_over_med"), (over, "max", "_over_max")):
                    if isinstance(src.get(key), (int, float)):
                        e[dst].append(src[key])

        _bump(power_total, b.get("power"))
        _bump(power_by_machine[machine], b.get("power"))

        for w in b.get("warnings") or []:
            p = w.get("p")
            if p:
                warnings[p]["n"] += w.get("n") or 1
                warnings[p]["units"].add(kid)

    silent_after = now - silent_days * 86400
    unit_list = sorted(units.values(), key=lambda u: (-u["last_at"], u["kid"]))
    for u in unit_list:
        u["last"] = day(u["last_at"])
        u["silent"] = u["last_at"] < silent_after
        u["days_silent"] = round((now - u["last_at"]) / 86400, 1)

    by_version = Counter(u["fw"] for u in unit_list)
    groups = crash_groups(records)

    return {
        "now": day(now),
        "silent_days": silent_days,
        "reports": dict(counts),
        "units": [{k: u[k] for k in ("kid", "fw", "last", "daily", "crash", "silent", "days_silent")}
                  for u in unit_list],
        "versions": dict(sorted(by_version.items())),
        "silent": [u["kid"] for u in unit_list if u["silent"]],
        "hardware": {
            "scales": {k: len(v) for k, v in sorted(hw_scale.items())},
            "bots": [{"role": r, "kind": k, "units": len(v)} for (r, k), v in sorted(hw_bots.items())],
            "machines": {k: len(v) for k, v in sorted(hw_machines.items())},
        },
        "links": [{"role": r, "device": d, "attempts": e["attempts"], "failures": e["failures"],
                   "connects": e["connects"],
                   "failure_rate": round(e["failures"] / e["attempts"], 3) if e["attempts"] else None,
                   "longest_wait_ms": e["longest_wait_ms"], "units": len(e["units"]),
                   "reports": e["reports"]}
                  for (r, d), e in sorted(links.items())],
        "scale_frames": {d: {"frames": f["frames"], "bad": f["bad"], "dropped": f["dropped"],
                             "units": len(f["units"])} for d, f in sorted(frames.items())},
        "shots": {m: {"units": len(e["units"]), "count": e["count"],
                      "end": dict(e["end"].most_common()), "faults": dict(e["faults"].most_common()),
                      "stop_retried": e["stop_retried"],
                      "stop_latency_ms": _spread(e["_lat_med"], e["_lat_max"]),
                      "overshoot_g": _spread(e["_over_med"], e["_over_max"])}
                  for m, e in sorted(shots.items())},
        "power": {"total": dict(power_total),
                  "by_machine": {m: dict(c) for m, c in sorted(power_by_machine.items())}},
        "warnings": [{"p": p, "n": w["n"], "units": len(w["units"])}
                     for p, w in sorted(warnings.items(), key=lambda kv: (-kv[1]["n"], -len(kv[1]["units"]), kv[0]))],
        "crashes": [{"key": g["key"], "count": g["count"], "units": g["units"],
                     "versions": g["versions"], "causes": g["causes"], "tasks": g["tasks"],
                     "newest": day(g["newest"]["body"].get("at"))} for g in groups],
    }


# ---------------------------------------------------------------- text output

def _counts(d):
    return ", ".join(f"{k} {v}" for k, v in d.items()) or "-"


def render(a, problems=()):
    out = []
    w = out.append
    rep = a["reports"]
    w(f"Fleet as of {a['now']}: {len(a['units'])} units, "
      f"{rep.get('diag_daily', 0)} daily and {rep.get('diag_crash', 0)} crash reports")
    if problems:
        w(f"  ({len(problems)} files could not be read; see stderr)")

    w("\nUnits")
    for u in a["units"]:
        flag = f"  SILENT {u['days_silent']} days" if u["silent"] else ""
        w(f"  {u['kid']}  {u['fw']:<12} last {u['last']}  daily {u['daily']}, crash {u['crash']}{flag}")
    w("  By version: " + _counts(a["versions"]))
    w(f"  Silent for more than {a['silent_days']} days: {len(a['silent'])}"
      + (" (" + ", ".join(a["silent"]) + ")" if a["silent"] else ""))

    hw = a["hardware"]
    w("\nHardware (units)")
    w("  Scales:   " + _counts(hw["scales"]))
    w("  Bots:     " + (", ".join(f"{b['role']}: {b['kind']} {b['units']}" for b in hw["bots"]) or "-"))
    w("  Machines: " + _counts(hw["machines"]))

    w("\nLinks (role, device: failures/attempts, longest wait)")
    for l in a["links"]:
        rate = f"{l['failure_rate'] * 100:.0f}%" if l["failure_rate"] is not None else "-"
        w(f"  {l['role']:<6} {l['device']:<16} {l['failures']}/{l['attempts']} = {rate:<5}"
          f" longest {l['longest_wait_ms'] / 1000:.1f} s  ({l['units']} units)")
    for d, f in a["scale_frames"].items():
        w(f"  frames {d:<16} {f['frames']} frames, {f['bad']} bad, {f['dropped']} dropped")

    w("\nShots by machine")
    for m, s in a["shots"].items():
        w(f"  {m}: {s['count']} shots on {s['units']} units, stop retried {s['stop_retried']}")
        w(f"    end:    {_counts(s['end'])}")
        w(f"    faults: {_counts(s['faults'])}")
        for label, key, unit in (("stop latency", "stop_latency_ms", "ms"), ("overshoot", "overshoot_g", "g")):
            sp = s[key]
            if sp and "median" in sp:
                w(f"    {label}: median {sp['median']:g} {unit} (per-report medians "
                  f"{sp['lowest_median']:g} to {sp['highest_median']:g}), worst {sp.get('max', 0):g} {unit}")

    w("\nPower presses")
    w("  All: " + _counts(a["power"]["total"]))
    for m, c in a["power"]["by_machine"].items():
        w(f"  {m}: " + _counts(c))

    w("\nWarnings (count, units)")
    for x in a["warnings"]:
        w(f"  {x['n']:>5}  {x['units']:>3}  {x['p']}")
    if not a["warnings"]:
        w("  -")

    w("\nCrashes (pc + first 3 backtrace addresses)")
    for g in a["crashes"]:
        w(f"  {g['count']} x {' '.join(g['key'])}")
        w(f"     cause {_counts(g['causes'])}; task {_counts(g['tasks'])}; newest {g['newest']}")
        w(f"     versions {', '.join(g['versions'])}; units {', '.join(g['units'])}")
    if not a["crashes"]:
        w("  -")
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folder", help="clone of the reports repo, or a folder of envelopes")
    ap.add_argument("--since", help="only reports built on or after this UTC date (YYYY-MM-DD)")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--emulator", action="store_true", help="include reports under emulator/")
    ap.add_argument("--silent-days", type=float, default=3, help="days without a report (default 3)")
    ap.add_argument("--now", help="treat this UTC date as today (YYYY-MM-DD), for silence checks")
    args = ap.parse_args(argv)

    since = parse_date(args.since) if args.since else None
    now = parse_date(args.now) if args.now else None
    records, problems = load(args.folder, since=since, emulator=args.emulator)
    for path, why in problems:
        print(f"skipped {path}: {why}", file=sys.stderr)
    a = analyse(records, now=now, silent_days=args.silent_days)
    if args.json:
        a["unreadable"] = [p for p, _ in problems]
        json.dump(a, sys.stdout, indent=2)
        print()
    else:
        print(render(a, problems))
    return 0


if __name__ == "__main__":
    sys.exit(main())
