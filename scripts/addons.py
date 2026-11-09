"""The add-ons compiled into this image (docs/ADDONS.md).

Add-on folders are listed, one path per line or separated by ';', in the environment variable
BBW_ADDONS or in the file addons.local.txt at the project root (gitignored). Nothing listed: the
public firmware, with an empty table.

Each folder holds addon.json:
    {"id": "my-grinder", "name": "...", "version": "1.0.0", "api": 1, "kind": "grinder",
     "header": "MyGrinder.h", "factory": "makeMyGrinder", "routes": ["/api/mygrinder/"]}
and its sources: the header declares `bbw::addon::Addon *<factory>();`, the .cpp files beside it
are compiled with the firmware.

Used two ways:
- PlatformIO pre-script (platformio.ini extra_scripts): writes .pio/gen/AddonList.h and adds the
  add-ons' sources to the build.
- From emu/build.py: write_table() and sources(), with the folders it was given.
"""
import json
import os
import re
from pathlib import Path

ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]{1,31}$")
FACTORY_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")


def folders(project, extra=None):
    """The add-on folders to build: `extra` if given, else BBW_ADDONS, else addons.local.txt."""
    text = extra
    if text is None:
        text = os.environ.get("BBW_ADDONS")
    if text is None:
        local = Path(project) / "addons.local.txt"
        text = local.read_text(encoding="utf-8") if local.exists() else ""
    out = []
    for part in re.split(r"[;\n]", text):
        part = part.strip()
        if part and not part.startswith("#"):
            path = Path(part)
            if not path.is_absolute():
                path = Path(project) / path
            out.append(path.resolve())
    return out


def manifest(folder):
    data = json.loads((folder / "addon.json").read_text(encoding="utf-8"))
    for key in ("id", "name", "version", "api", "header", "factory"):
        if key not in data:
            raise SystemExit(f"{folder / 'addon.json'}: '{key}' missing")
    if not ID_RE.match(data["id"]):
        raise SystemExit(f"{folder}: id '{data['id']}' is not [a-z0-9-]")
    if not FACTORY_RE.match(data["factory"]):
        raise SystemExit(f"{folder}: factory '{data['factory']}' is not a C++ name")
    routes = data.get("routes", [])
    if len(routes) > 2 or any(not re.match(r"^/api/[a-z0-9/_-]+/$", r) or r.startswith("/api/addons") for r in routes):
        raise SystemExit(f"{folder}: routes must be at most two prefixes like /api/mygrinder/ (not /api/addons)")
    if not (folder / data["header"]).exists():
        raise SystemExit(f"{folder}: header {data['header']} not found")
    data["folder"] = folder
    return data


def write_table(project, extra=None):
    """Writes .pio/gen/AddonList.h. Returns the manifests."""
    found = [manifest(f) for f in folders(project, extra)]
    ids = [m["id"] for m in found]
    if len(set(ids)) != len(ids):
        raise SystemExit(f"add-ons listed twice: {ids}")
    lines = ["#pragma once",
             "// Made by scripts/addons.py: the add-ons compiled into this image. Do not edit.",
             '#include "addons/Addon.h"']
    for m in found:
        lines.append('#include "' + (m["folder"] / m["header"]).as_posix() + '"')
    lines += [f"#define BBW_ADDON_COUNT {len(found)}", "namespace bbw {", "namespace addon {"]
    if found:
        lines.append("inline const AddonEntry *addonTable() {")
        lines.append("  static const AddonEntry table[] = {")
        for m in found:
            routes = [f'"{r}"' for r in m.get("routes", [])] + ["nullptr"] * (2 - len(m.get("routes", [])))
            lines.append(f'      {{"{m["id"]}", &{m["factory"]}, {{{", ".join(routes)}}}}},')
        lines += ["  };", "  return table;", "}"]
    else:
        lines.append("inline const AddonEntry *addonTable() { return nullptr; }")
    lines += ["}  // namespace addon", "}  // namespace bbw", ""]
    out = Path(project) / ".pio" / "gen" / "AddonList.h"
    out.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines)
    if not out.exists() or out.read_text(encoding="utf-8") != text:
        out.write_text(text, encoding="utf-8", newline="\n")
    for m in found:
        print(f"add-on: {m['id']} {m['version']} from {m['folder']}")
    return found


def sources(manifests):
    """The .cpp files of these add-ons."""
    return [p for m in manifests for p in sorted(m["folder"].glob("*.cpp"))]


try:  # as a PlatformIO pre-script, SCons provides Import()
    Import("env")  # noqa: F821
except NameError:
    env = None

if env is not None:
    project = env["PROJECT_DIR"]  # noqa: F821
    found = write_table(project)
    for m in found:
        env.BuildSources(os.path.join("$BUILD_DIR", "addon_" + m["id"]), str(m["folder"]),  # noqa: F821
                         src_filter="+<*.cpp>")
        env.Append(CPPPATH=[str(m["folder"])])  # noqa: F821
if env is None and __name__ == "__main__":
    import sys
    write_table(Path(__file__).resolve().parent.parent, sys.argv[1] if len(sys.argv) > 1 else None)
