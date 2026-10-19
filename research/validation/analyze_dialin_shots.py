#!/usr/bin/env python3
"""Measure the numbers the dial-in plan depends on, from a controller's shot list.

    python analyze_dialin_shots.py shots.json [--contexts contexts.json] [--tolerance 2.0] [--label REAL|SIMULATED]

`shots.json` is the output of the controller's  GET /api/shots?limit=500 ; `contexts.json` (optional) of
GET /api/contexts?limit=500 (core/ShotContext.h), which adds the start suggestion that was on offer, so the
start error can be scored against where each coffee settled.
Nothing is sent anywhere. The script prints, per coffee and bag:

  * shots, settings used, target time;
  * wasted shots before the first shot inside the tolerance ("shots to settle");
  * shot-to-shot noise: scatter of times among shots at the SAME setting (needs >= 3 such shots);
  * response strength: how steeply time falls as the setting opens, as the slope of ln(time) on
    ln(setting) (an "elasticity"; the simulations assume about 0.9) and as um per second;
  * first-drop and flow-shape scatter at the same setting (inputs to the flow-anomaly flag).

And overall: the share of dial-ins settled by shot 1 / 2 / 3, which is the owner's goal (>= 75-80 % by shot 2).

It reads only fields the controller already exports. Read the caveats it prints: small samples give
wide, unreliable numbers, and simulated shots measure the simulator.
"""
import argparse
import collections
import json
import math
import statistics as st
import sys


def valid(s):
    return (s.get("end_reason") == "weight" and s.get("final_valid") and s.get("grind_um")
            and s.get("shot_time_s", 0) > 15 and s.get("target_time_s", 0) > 0)


def slope(xs, ys):
    mx, my = st.mean(xs), st.mean(ys)
    var = sum((x - mx) ** 2 for x in xs)
    if var <= 0:
        return None
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / var


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shots")
    ap.add_argument("--tolerance", type=float, default=2.0)
    ap.add_argument("--label", default="UNLABELLED")
    ap.add_argument("--contexts")
    a = ap.parse_args()
    shots = json.load(open(a.shots, encoding="utf-8"))
    shots = sorted(shots, key=lambda s: s["id"])
    ctx = {}
    if a.contexts:
        ctx = {c["shot_id"]: c for c in json.load(open(a.contexts, encoding="utf-8"))}
    print(f"DATA LABEL: {a.label}   shots in file: {len(shots)}")
    groups = collections.OrderedDict()
    for s in shots:
        if not valid(s):
            continue
        key = (s.get("coffee"), s.get("roast_date"), s.get("target_time_s"), s.get("dose_g"))
        groups.setdefault(key, []).append(s)

    settled_by = collections.Counter()
    dialins = 0
    noise_all, elast_all, start_errors = [], [], []
    for key, v in groups.items():
        coffee, roast, target, dose = key
        times = [s["shot_time_s"] for s in v]
        sets = [s["grind_um"] for s in v]
        first_ok = next((i + 1 for i, t in enumerate(times) if abs(t - target) <= a.tolerance), None)
        print(f"\n{coffee} (roast {roast}, target {target} s, dose {dose} g): {len(v)} valid shots, "
              f"settings {min(sets)}..{max(sets)} um")
        print(f"  first shot inside +-{a.tolerance:g} s: " + (f"shot {first_ok} (so {first_ok - 1} wasted)" if first_ok else "never"))
        dialins += 1
        if first_ok and ctx:
            settled = sets[first_ok - 1]
            c0 = ctx.get(v[0]["id"])
            for name, key, rng in (("community start", "start_setting", "start_range"),
                                   ("own-coffee start", "similar_setting", "similar_range")):
                if c0 and c0.get(key):
                    err = 100.0 * (c0[key] - settled) / settled
                    inside = abs(c0[key] - settled) <= c0.get(rng, 0)
                    print(f"  {name}: offered {c0[key]} +-{c0.get(rng, 0)}, settled {settled}: error {err:+.0f} %"
                          f" ({'inside' if inside else 'outside'} its range)")
                    start_errors.append((name, err, inside))
        if first_ok:
            for n in (1, 2, 3):
                if first_ok <= n:
                    settled_by[n] += 1
        # noise among repeats at the same setting (deviation from that setting's median)
        by_set = collections.defaultdict(list)
        for s in v:
            by_set[s["grind_um"]].append(s)
        devs = []
        for u, ss in by_set.items():
            if len(ss) >= 3:
                m = st.median(x["shot_time_s"] for x in ss)
                devs += [x["shot_time_s"] - m for x in ss]
        if len(devs) >= 6:
            sd = st.pstdev(devs) * math.sqrt(len(devs) / max(1, len(devs) - len(by_set)))
            print(f"  shot-to-shot noise at one setting: sd about {sd:.2f} s from {len(devs)} shots")
            noise_all.append(sd)
        else:
            print("  shot-to-shot noise: not enough repeats at one setting (need 3+ shots at the same setting)")
        # response strength
        if max(sets) - min(sets) >= 4 and len(set(sets)) >= 3:
            e = slope([math.log(x) for x in sets], [math.log(t) for t in times])
            b = slope(sets, times)
            if e is not None and e < 0 and b is not None and b < 0:
                print(f"  response: elasticity {-e:.2f} (ln time per ln setting); {-1 / b:.2f} um per second")
                elast_all.append(-e)
        # flow shape scatter
        for field in ("first_drop_s", "flow_roughness_g_s", "flow_acceleration"):
            vals = [s[field] for s in v if s.get(field)]
            if len(vals) >= 5:
                med = st.median(vals)
                mad = 1.4826 * st.median(abs(x - med) for x in vals)
                print(f"  {field}: median {med:.2f}, robust spread {mad:.2f} ({len(vals)} shots)")

    print("\n== overall ==")
    if dialins:
        for n in (1, 2, 3):
            print(f"  coffees settled by shot {n}: {settled_by[n]} of {dialins} ({100 * settled_by[n] / dialins:.0f} %)")
    for name in ("community start", "own-coffee start"):
        e = [abs(x) for n, x, _ in start_errors if n == name]
        if e:
            inside = sum(1 for n, _, i in start_errors if n == name and i)
            print(f"  {name}: median start error {st.median(e):.0f} % over {len(e)} coffees; inside its range {inside} of {len(e)}")
    if noise_all:
        print(f"  shot-to-shot noise: median {st.median(noise_all):.2f} s over {len(noise_all)} coffees")
    if elast_all:
        print(f"  elasticity: median {st.median(elast_all):.2f} over {len(elast_all)} coffees")
    print("\nCaveats: dial-ins here are 'coffees in this file', not controlled trials; a coffee already dialled before the file starts "
          "counts as settled by shot 1. Fewer than ~10 dial-ins make the percentages meaningless. Simulated shots measure the simulator.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
