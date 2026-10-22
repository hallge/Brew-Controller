#!/usr/bin/env python3
"""Build (and optionally run) the PC emulator.

    python emu/build.py            build .pio/emu/bbw_emu.exe
    python emu/build.py --run      build, then start it and open the page
    python emu/build.py --clean    remove the build directory first

Needs the libraries PlatformIO already fetched (.pio/libdeps, from `pio run`
or `pio pkg install`) and a C++17 compiler. By default the compiler is zig
(`pip install ziglang`, already in this project's .venv); set CXX and CC to
use another clang/gcc that targets Windows.
"""
import concurrent.futures
import os
import shlex
import shutil
import subprocess
import sys
import webbrowser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EMU = ROOT / "emu"
OUT = ROOT / ".pio" / "emu"
LIBDEPS = ROOT / ".pio" / "libdeps" / "t-display-s3-amoled-plus"
LVGL = LIBDEPS / "lvgl"
ARDUINOJSON = LIBDEPS / "ArduinoJson" / "src"

FIRMWARE = [
    "src/main.cpp",
    "src/Log.cpp",
    "src/platform/FirmwareUpdate.cpp",
    "src/ble/Ble.cpp",
    "src/core/BrewController.cpp",
    "src/net/Web.cpp",
    "src/storage/Store.cpp",
    "src/ui/Ui.cpp",
    "lib/EspressoScaleProtocol/src/Acaia.cpp",
    "lib/EspressoScaleProtocol/src/Felicita.cpp",
    "lib/EspressoScaleProtocol/src/Decent.cpp",
    "lib/EspressoScaleProtocol/src/Difluid.cpp",
    "lib/EspressoScaleProtocol/src/Eclair.cpp",
    "lib/EspressoScaleProtocol/src/Myscale.cpp",
    "lib/EspressoScaleProtocol/src/WeighMyBru.cpp",
    "lib/EspressoScaleProtocol/src/Varia.cpp",
    "lib/EspressoScaleProtocol/src/Eureka.cpp",
    "lib/EspressoScaleProtocol/src/Registry.cpp",
    "lib/EspressoScaleProtocol/src/ScaleProtocolCommon.cpp",
]

INCLUDES = [EMU / "shims", EMU / "src", ROOT / "include", ROOT / "src", ROOT / ".pio" / "gen",
            ROOT / "lib" / "EspressoScaleProtocol" / "src", LVGL, ARDUINOJSON]
DEFINES = ["BBW_EMULATOR", "LV_CONF_INCLUDE_SIMPLE", "CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3",
           "ARDUINOJSON_ENABLE_ARDUINO_STRING=1", "ARDUINOJSON_ENABLE_ARDUINO_PRINT=1"]
COMMON = ["-O1", "-g0"] + [f"-I{p}" for p in INCLUDES] + [f"-D{d}" for d in DEFINES]


def tool(env, zig):
    return shlex.split(os.environ[env]) if env in os.environ else [sys.executable, "-m", "ziglang", zig]


def newest(paths):
    return max((p.stat().st_mtime for p in paths), default=0.0)


def main():
    # The helper extension the controller serves (/extension.zip), packed as the firmware build does.
    subprocess.check_call([sys.executable, str(ROOT / "browser-extension" / "make_zip.py")])
    if "--clean" in sys.argv and OUT.exists():
        shutil.rmtree(OUT)
    if not LVGL.exists() or not ARDUINOJSON.exists():
        sys.exit(f"libraries not found under {LIBDEPS}\nrun `pio pkg install` (or `pio run`) once first")
    cc, cxx = tool("CC", "cc"), tool("CXX", "c++")
    (OUT / "obj").mkdir(parents=True, exist_ok=True)

    # Any header change rebuilds the firmware and emulator sources; LVGL only
    # depends on its configuration.
    headers = [p for d in (ROOT / "src", ROOT / "lib", EMU / "shims", EMU / "src")
               for p in d.rglob("*.h")]
    lv_conf = ROOT / "include" / "lv_conf.h"
    own_stamp = max(newest(headers), lv_conf.stat().st_mtime, Path(__file__).stat().st_mtime)
    lv_stamp = max(lv_conf.stat().st_mtime, (EMU / "shims" / "Arduino.h").stat().st_mtime)

    jobs = []
    for src in sorted((LVGL / "src").rglob("*.c")):
        obj = OUT / "obj" / ("lv_" + "_".join(src.relative_to(LVGL / "src").with_suffix(".o").parts))
        jobs.append((src, obj, cc + ["-std=gnu11", "-w"], lv_stamp))
    own = [ROOT / s for s in FIRMWARE] + sorted((EMU / "src").glob("*.cpp"))
    for src in own:
        obj = OUT / "obj" / ("_".join(src.relative_to(ROOT).with_suffix(".o").parts))
        jobs.append((src, obj, cxx + ["-std=gnu++17", "-Wall", "-Wextra", "-Wno-c++11-narrowing"], own_stamp))

    def compile_one(job):
        src, obj, compiler, stamp = job
        if obj.exists() and obj.stat().st_mtime > max(src.stat().st_mtime, stamp):
            return None
        result = subprocess.run(compiler + COMMON + ["-c", str(src), "-o", str(obj)],
                                capture_output=True, text=True)
        return src, result

    todo = [j for j in jobs if not (j[1].exists() and j[1].stat().st_mtime > max(j[0].stat().st_mtime, j[3]))]
    if todo:
        print(f"compiling {len(todo)} of {len(jobs)} files ...", flush=True)
    failed = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for done in pool.map(compile_one, todo):
            if done is None:
                continue
            src, result = done
            if result.stderr.strip():
                print(f"--- {src.relative_to(ROOT)}\n{result.stderr.strip()}", flush=True)
            failed = failed or result.returncode != 0
    if failed:
        sys.exit("build failed")

    exe = OUT / "bbw_emu.exe"
    objs = [j[1] for j in jobs]
    if todo or not exe.exists():
        rsp = OUT / "link.rsp"
        rsp.write_text("\n".join('"' + str(o).replace("\\", "/") + '"' for o in objs))
        result = subprocess.run(cxx + [f"@{rsp}", "-o", str(exe), "-lws2_32"],
                                capture_output=True, text=True)
        if result.returncode != 0:
            # The emulator is probably still running and holds the file open.
            sys.exit(result.stderr.strip() + "\nlink failed (is the emulator still running?)")
        print(f"built {exe.relative_to(ROOT)}", flush=True)
    else:
        print("up to date", flush=True)

    if "--run" in sys.argv:
        args = [str(exe), f"--data={EMU / 'data'}", f"--ui={EMU / 'ui'}"]
        args += [a for a in sys.argv[1:] if a.startswith("--web-port=") or a.startswith("--emu-port=")]
        port = next((a.split("=")[1] for a in args if a.startswith("--emu-port=")), "8090")
        if "--no-browser" not in sys.argv:
            webbrowser.open(f"http://localhost:{port}")
        sys.exit(subprocess.call(args))


if __name__ == "__main__":
    main()
