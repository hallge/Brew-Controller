#!/usr/bin/env python3
"""Builds an add-on as a loadable module: `<id>.bbwmod` (docs/ADDONS.md, "Add-ons as modules").

    python scripts/build_module.py <add-on folder> [--out DIR] [--min-firmware X.Y.Z] [--sums FILE]
    python scripts/build_module.py --check FILE.bbwmod

The folder is an add-on's, as scripts/addons.py reads it (addon.json: id, name, version, api, header,
factory, routes; optional "sources": [...], default every *.cpp beside it). The compiler is the
PlatformIO ESP32-S3 toolchain the firmware is built with. The result is a 128-byte header
(src/addons/ModuleFile.h) followed by the ELF, which the firmware's loader (lib/elf_loader) places in PSRAM.

--check verifies a file: header layout, size, and the ELF's SHA-256.
--sums appends the file's line ("<sha256>  <id>.bbwmod") to a SHA256SUMS.txt, as a release carries.
"""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import addons  # noqa: E402  (scripts/addons.py)

HEADER = struct.Struct("<4sHHII32s16s32s32s")
HEADER_SIZE = 128
MAX_ELF = 256 * 1024
MAGIC = b"BBWM"

# The flags the ESP-IDF's own ELF-application build uses (components/elf_loader/elf_loader.cmake), plus
# what a module without a C++ runtime needs.
CXXFLAGS = ["-std=gnu++17", "-Os", "-mlongcalls", "-fPIC", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
            "-ffunction-sections", "-fdata-sections", "-fvisibility=hidden", "-DBBW_MODULE_BUILD", "-Wall", "-Wextra"]
LDFLAGS = ["-nostartfiles", "-nostdlib", "-fPIC", "-shared", "-e", "bbw_module_main", "-Wl,--gc-sections",
           "-fvisibility=hidden"]
STRIP = ["--strip-unneeded", "--remove-section=.comment", "--remove-section=.dynamic", "--remove-section=.xt.lit",
         "--remove-section=.xt.prop", "--remove-section=.xtensa.info"]


def abi_version(firmware):
    text = (firmware / "src" / "addons" / "AddonAbi.h").read_text(encoding="utf-8")
    m = re.search(r"#define\s+BBW_ADDON_ABI\s+(\d+)", text)
    if not m:
        raise SystemExit("BBW_ADDON_ABI not found in src/addons/AddonAbi.h")
    return int(m.group(1))


def pack_version(text):
    m = re.match(r"^v?(\d+)(?:\.(\d+))?(?:\.(\d+))?", text or "")
    if not m:
        return 0
    major, minor, patch = (min(int(g or 0), 255) for g in m.groups())
    return (major << 16) | (minor << 8) | patch


def find_toolchain(explicit):
    candidates = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("BBW_XTENSA_TOOLCHAIN"):
        candidates.append(Path(os.environ["BBW_XTENSA_TOOLCHAIN"]))
    home = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    candidates.append(home / "packages" / "toolchain-xtensa-esp32s3")
    for base in candidates:
        for exe in ("xtensa-esp32s3-elf-g++.exe", "xtensa-esp32s3-elf-g++"):
            if (base / "bin" / exe).exists():
                return base / "bin"
    raise SystemExit("the xtensa-esp32s3 toolchain was not found: build the firmware once with PlatformIO, or pass "
                     "--toolchain <folder> (or set BBW_XTENSA_TOOLCHAIN)")


def tool(bindir, name):
    for ext in (".exe", ""):
        p = bindir / f"xtensa-esp32s3-elf-{name}{ext}"
        if p.exists():
            return str(p)
    raise SystemExit(f"{name} not found in {bindir}")


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("failed: " + " ".join(str(c) for c in cmd[:3]) + " ...")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r


def entry_source(manifest):
    routes = manifest.get("routes", [])[:2]
    r = [json.dumps(x) for x in routes] + ["nullptr"] * (2 - len(routes))
    return ('// Made by scripts/build_module.py. Do not edit.\n'
            '#include "addons/AddonModule.h"\n#include "%s"\n'
            'BBW_ADDON_MODULE(&%s, %s, %s)\n' % (manifest["header"], manifest["factory"], r[0], r[1]))


def build(folder, out_dir, firmware, toolchain, min_firmware, sums):
    m = addons.manifest(folder.resolve())
    abi = abi_version(firmware)
    if int(m["api"]) != abi:
        raise SystemExit(f"{folder}: addon.json says api {m['api']}, this firmware's add-on interface is {abi}")
    bindir = find_toolchain(toolchain)
    cxx, strip = tool(bindir, "g++"), tool(bindir, "strip")
    folder = folder.resolve()
    sources = [folder / s for s in m["sources"]] if "sources" in m else sorted(folder.glob("*.cpp"))
    if not sources:
        raise SystemExit(f"{folder}: no .cpp files")
    with tempfile.TemporaryDirectory(prefix="bbwmod-") as tmp:
        tmp = Path(tmp)
        entry = tmp / "bbw_module_entry.cpp"
        entry.write_text(entry_source(m), encoding="utf-8")
        objects = []
        for src in [entry] + sources:
            obj = tmp / (src.stem + ".o")
            run([cxx, *CXXFLAGS, "-I", str(firmware / "src"), "-I", str(folder), "-c", str(src), "-o", str(obj)])
            objects.append(str(obj))
        # libgcc supplies the 64-bit and soft-float helpers a module needs, but it must be linked without its
        # .eh_frame. The cause of "BFD ... elf32-xtensa.c:3299 assertion fail": libgcc is not built with -fPIC,
        # and its larger members (_umoddi3, _udivmoddi4, ...) carry an .eh_frame whose R_XTENSA_32 entries
        # point into .text. In a -shared link that read-only section would need dynamic relocations, which this
        # binutils (2.35) asserts on instead of reporting. Nothing in a module unwinds (-fno-exceptions), so the
        # section is simply removed from a private copy of libgcc.a (objcopy accepts an archive).
        libgcc = run([cxx, "-print-libgcc-file-name"]).stdout.strip()
        (tmp / "gcc").mkdir()
        run([tool(bindir, "objcopy"), "--remove-section=.eh_frame", "--remove-section=.rela.eh_frame", libgcc,
             str(tmp / "gcc" / "libgcc.a")])
        elf = tmp / "module.elf"
        run([cxx, *LDFLAGS, *objects, "-L", str(tmp / "gcc"), "-lgcc", "-o", str(elf)])
        run([strip, *STRIP, str(elf)])
        data = elf.read_bytes()
    if len(data) > MAX_ELF:
        raise SystemExit(f"the module is {len(data)} bytes: over the 256 kB limit")
    version = str(m["version"])
    if len(version.encode()) > 15 or len(m["id"].encode()) > 31:
        raise SystemExit("id (31) or version (15) too long for the header")
    header = HEADER.pack(MAGIC, HEADER_SIZE, abi, len(data), pack_version(min_firmware or m.get("min_firmware", "")),
                         m["id"].encode(), version.encode(), hashlib.sha256(data).digest(), bytes(32))
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{m['id']}.bbwmod"
    out.write_bytes(header + data)
    print(f"{out}: {len(data)} bytes of ELF, {m['id']} {version}, interface {abi}, "
          f"sha256 of the ELF {hashlib.sha256(data).hexdigest()}")
    if sums:
        digest = hashlib.sha256(out.read_bytes()).hexdigest()
        with open(sums, "a", encoding="utf-8", newline="\n") as f:
            f.write(f"{digest}  {out.name}\n")
    return out


def check(path):
    data = Path(path).read_bytes()
    if len(data) < HEADER_SIZE:
        print("REFUSED: too short to be an add-on module")
        return 1
    magic, hsize, abi, elf_size, min_fw, mid, ver, sha, _reserved = HEADER.unpack(data[:HEADER_SIZE])
    mid_text = mid.rstrip(b"\0").decode("ascii", "replace")
    problems = []
    if magic != MAGIC:
        problems.append("magic is not BBWM")
    if hsize != HEADER_SIZE:
        problems.append(f"header size {hsize}")
    if elf_size == 0 or elf_size > MAX_ELF:
        problems.append(f"ELF size {elf_size} is not 1..{MAX_ELF}")
    if len(data) != HEADER_SIZE + elf_size:
        problems.append("file size does not match the header")
    elif hashlib.sha256(data[HEADER_SIZE:]).digest() != sha:
        problems.append("SHA-256 of the ELF does not match")
    if not re.match(r"^[a-z0-9][a-z0-9-]{1,30}$", mid_text):
        problems.append("id is not valid")
    if data[HEADER_SIZE:HEADER_SIZE + 4] != b"\x7fELF":
        problems.append("the payload is not an ELF")
    print(f"{path}: id {mid_text} version {ver.rstrip(bytes(1)).decode('ascii', 'replace')} interface {abi} "
          f"min firmware {min_fw >> 16}.{(min_fw >> 8) & 255}.{min_fw & 255} ELF {elf_size} bytes")
    if problems:
        print("REFUSED: " + "; ".join(problems))
        return 1
    print("ok: header and SHA-256 check out")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", nargs="?", help="the add-on's folder (addon.json)")
    ap.add_argument("--out", default="build/modules", help="where <id>.bbwmod goes")
    ap.add_argument("--firmware", default=str(ROOT), help="the firmware source tree (headers): default this one")
    ap.add_argument("--toolchain", help="the xtensa-esp32s3 toolchain folder")
    ap.add_argument("--min-firmware", help="the oldest firmware that may run it, e.g. 0.3.2")
    ap.add_argument("--sums", help="append '<sha256>  <file>' to this SHA256SUMS.txt")
    ap.add_argument("--check", metavar="FILE", help="verify a .bbwmod file instead of building")
    a = ap.parse_args()
    if a.check:
        sys.exit(check(a.check))
    if not a.folder:
        ap.error("give the add-on's folder, or --check FILE")
    build(Path(a.folder), Path(a.out), Path(a.firmware), a.toolchain, a.min_firmware, a.sums)


if __name__ == "__main__":
    main()
