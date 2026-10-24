#!/usr/bin/env python3
"""Decodes crash reports against each release's firmware.elf.

    python crashes.py <folder> [--since YYYY-MM-DD] [--repo OWNER/NAME] [--addr2line PATH]

Crashes are grouped as in fleet.py (dump.pc plus the first 3 backtrace
addresses). For each group and firmware version, the release's firmware.elf is
fetched once with `gh release download <fw> -p firmware.elf` and cached under
.elf/<fw>/ next to this script. pc and the newest crash's backtrace are decoded
with xtensa-esp32s3-elf-addr2line -pfiaC, then the newest crash's log tail is
printed.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fleet  # noqa: E402

TOOL = "xtensa-esp32s3-elf-addr2line"


def find_addr2line(given=None):
    """Returns the addr2line command as a list, or None."""
    if given:
        return [sys.executable, given] if given.endswith(".py") else [given]
    bindir = os.path.join(os.path.expanduser("~"), ".platformio", "packages",
                          "toolchain-xtensa-esp32s3", "bin")
    for name in (TOOL + ".exe", TOOL):
        p = os.path.join(bindir, name)
        if os.path.isfile(p):
            return [p]
    p = shutil.which(TOOL)
    return [p] if p else None


def fetch_elf(fw, cache, repo=None):
    """Returns the path to <cache>/<fw>/firmware.elf, downloading it if needed,
    or (None, reason)."""
    dest = os.path.join(cache, fw, "firmware.elf")
    if os.path.isfile(dest):
        return dest, None
    if not shutil.which("gh"):
        return None, "gh is not installed"
    tmp = tempfile.mkdtemp(prefix="elf-")
    try:
        cmd = ["gh", "release", "download", fw, "-p", "firmware.elf", "-D", tmp]
        if repo:
            cmd += ["-R", repo]
        r = subprocess.run(cmd, capture_output=True, text=True)
        got = os.path.join(tmp, "firmware.elf")
        if r.returncode != 0 or not os.path.isfile(got):
            return None, (r.stderr or r.stdout).strip() or "no firmware.elf in that release"
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.move(got, dest)
        return dest, None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def decode(addr2line, elf, addrs):
    """Runs addr2line -pfiaC on addrs. Returns its output lines."""
    r = subprocess.run(addr2line + ["-pfiaC", "-e", elf] + list(addrs), capture_output=True, text=True)
    if r.returncode != 0:
        return [f"addr2line failed: {(r.stderr or r.stdout).strip()}"]
    return [l for l in r.stdout.splitlines() if l.strip()]


def report(records, addr2line, cache, repo=None, tail=20, out=sys.stdout):
    w = lambda s="": print(s, file=out)  # noqa: E731
    groups = fleet.crash_groups(records)
    if not groups:
        w("No crash reports.")
        return 0
    if addr2line is None:
        w(f"({TOOL} not found: pass --addr2line; addresses are shown undecoded)")
    for i, g in enumerate(groups, 1):
        w(f"=== Crash group {i}: {g['count']} x {' '.join(g['key'])}")
        w(f"    units {', '.join(g['units'])}; versions {', '.join(g['versions'])}")
        for fw in g["versions"]:
            mine = [r for r in g["crashes"] if (r["body"].get("fw") or "?") == fw]
            newest = max(mine, key=lambda r: r["body"].get("at") or 0)
            dump = newest["body"].get("dump")
            if not dump:
                continue
            addrs = [dump.get("pc")] + list(dump.get("bt") or [])
            addrs = [a for a in addrs if a]
            w(f"  {fw}: {dump.get('cause') or '?'} in {dump.get('task') or '?'}"
              + ("  (backtrace corrupted)" if dump.get("corrupted") else ""))
            elf, why = fetch_elf(fw, cache, repo) if addr2line else (None, None)
            if addr2line and not elf:
                w(f"    (no firmware.elf for {fw}: {why})")
            if elf:
                for line in decode(addr2line, elf, addrs):
                    w("    " + line)
            else:
                w("    " + " ".join(addrs))
        newest = g["newest"]
        log = newest["body"].get("log") or []
        w(f"  Log tail of the newest ({fleet.day(newest['body'].get('at'))}, {newest['kid']}, "
          f"{newest['body'].get('fw')}, reset {newest['body'].get('reset')}):")
        for line in log[-tail:]:
            w("    " + line)
        w()
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folder", help="clone of the reports repo, or a folder of envelopes")
    ap.add_argument("--since", help="only crashes on or after this UTC date (YYYY-MM-DD)")
    ap.add_argument("--emulator", action="store_true", help="include reports under emulator/")
    ap.add_argument("--addr2line", help=f"path to {TOOL} (a .py stub is run with Python)")
    ap.add_argument("--repo", help="GitHub repo holding the releases (default: gh's current repo)")
    ap.add_argument("--elf-cache", default=os.path.join(HERE, ".elf"), help="where ELFs are cached")
    ap.add_argument("--tail", type=int, default=20, help="log lines to show (default 20)")
    args = ap.parse_args(argv)

    since = fleet.parse_date(args.since) if args.since else None
    records, problems = fleet.load(args.folder, since=since, emulator=args.emulator)
    for path, why in problems:
        print(f"skipped {path}: {why}", file=sys.stderr)
    return report(records, find_addr2line(args.addr2line), args.elf_cache, args.repo, args.tail)


if __name__ == "__main__":
    sys.exit(main())
