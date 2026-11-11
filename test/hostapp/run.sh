#!/usr/bin/env bash
# Headless runner for the iPhone host (ios/Host/BrewHost.h): compiles the firmware the way the app
# will (BBW_EMULATOR + BBW_HOST_IOS + BBW_CLEAN, no LVGL, no Ui.cpp, no OBBW code), starts it in demo
# mode and brews one shot through the C API.
#
#   test/hostapp/run.sh              build and run (the native target)
#   test/hostapp/run.sh syntax       only the macOS syntax check (-target aarch64-macos, compiled to a throwaway object: zig 0.x cannot do -fsyntax-only for C++)
#
# Needs a C++17 compiler; by default zig (python -m ziglang c++). Set CXX to use another. ArduinoJson
# is read from .pio/libdeps (a junction to the main checkout's is fine).
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
OUT=.pio/hostapp
mkdir -p "$OUT/obj"

if [ -n "${CXX:-}" ]; then
  # shellcheck disable=SC2206
  CXXCMD=($CXX)
else
  PY=.venv/Scripts/python.exe
  [ -x "$PY" ] || PY=${PYTHON:-python}
  CXXCMD=("$PY" -m ziglang c++)
fi

ARDUINOJSON=${ARDUINOJSON:-.pio/libdeps/t-display-s3-amoled-plus/ArduinoJson/src}
[ -d "$ARDUINOJSON" ] || { echo "ArduinoJson not found at $ARDUINOJSON"; exit 2; }

FIRMWARE=(
  src/main.cpp src/Log.cpp src/platform/FirmwareUpdate.cpp src/ble/Ble.cpp src/core/BrewController.cpp
  src/net/Web.cpp src/net/ScreenApi.cpp src/addons/AddonHost.cpp src/storage/Store.cpp src/storage/Backup.cpp
)
SCALEWIRE=(lib/ScaleWire/src/*.cpp)
EMU=(emu/src/host_runtime.cpp emu/src/host_web.cpp emu/src/ble_facade.cpp emu/src/virtual_radio.cpp emu/src/emu_devices.cpp)
HOST=(ios/Host/BrewHost.cpp ios/Host/host_stubs.cpp ios/Host/ui_stub.cpp)
RUNNER=(test/hostapp/hostapp_main.cpp)
SOURCES=("${FIRMWARE[@]}" "${SCALEWIRE[@]}" "${EMU[@]}" "${HOST[@]}" "${RUNNER[@]}")

# No OBBW code: not as a source, not as an include path, and (below) not as an included header.
for s in "${SOURCES[@]}"; do
  case "$s" in *EspressoScaleProtocol*) echo "OBBW source in the build: $s"; exit 1;; esac
done

FLAGS=(-std=gnu++17 -O1 -g0 -Wall -Wextra -Wno-c++11-narrowing
  -DBBW_EMULATOR -DBBW_HOST_IOS -DBBW_CLEAN -DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=3
  -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=1
  -Iios/Host -Iemu/shims -Iemu/src -Isrc -Iios/Generated -Ilib/ScaleWire/src "-I$ARDUINOJSON")

if [ "${1:-}" = "syntax" ]; then
  fail=0
  for s in "${SOURCES[@]}"; do
    if ! "${CXXCMD[@]}" -target aarch64-macos "${FLAGS[@]}" -c "$s" -o "$OUT/syntax.o" 2>"$OUT/syntax.err"; then
      echo "--- $s"; head -30 "$OUT/syntax.err"; fail=1
    fi
  done
  [ $fail = 0 ] && echo "macOS syntax check clean (${#SOURCES[@]} files)"
  exit $fail
fi

export OUT
export CXXCMD_STR="${CXXCMD[*]}"
# xargs cannot pass arrays: rebuild them inside the shell.
FLAGS_STR="${FLAGS[*]}"
export FLAGS_STR
printf '%s\n' "${SOURCES[@]}" | xargs -P "${JOBS:-8}" -I{} bash -c '
  src={}; obj=$OUT/obj/$(echo "$src" | tr "/" "_" | sed "s/\.cpp$/.o/")
  if [ -f "$obj" ] && [ -z "$(find "$src" src emu ios lib -newer "$obj" \( -name "*.h" -o -name "*.cpp" \) 2>/dev/null | head -1)" ]; then exit 0; fi
  $CXXCMD_STR $FLAGS_STR -MMD -MF "${obj%.o}.d" -c "$src" -o "$obj" 2>"${obj%.o}.err" \
    || { echo "--- $src"; cat "${obj%.o}.err"; exit 1; }
  [ -s "${obj%.o}.err" ] && { echo "--- $src (warnings)"; head -20 "${obj%.o}.err"; }
  exit 0' || { echo "build failed"; exit 1; }

if grep -l "EspressoScaleProtocol" "$OUT"/obj/*.d >/dev/null 2>&1; then
  echo "an OBBW header (EspressoScaleProtocol) was included by:"
  grep -l "EspressoScaleProtocol" "$OUT"/obj/*.d
  exit 1
fi

EXE=$OUT/hostapp.exe
"${CXXCMD[@]}" "$OUT"/obj/*.o -o "$EXE"
echo "built $EXE"
"$EXE" "$@"
