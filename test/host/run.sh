#!/usr/bin/env bash
# Host tests for the portable brew logic. Needs any C++17 compiler:
#   CXX="g++" test/host/run.sh
# Without one installed:  pip install ziglang  &&  CXX="python -m ziglang c++" test/host/run.sh
#
# BBW_CLEAN=1 test/host/run.sh builds with the clean replacements for Open Brew
# by Weight's code (core/StopMath.h, lib/ScaleWire; research/obbw-provenance.md).
# Each scale family comes from lib/ScaleWire/src once its file is there, and
# from lib/EspressoScaleProtocol/src until then.
set -euo pipefail
cd "$(dirname "$0")/../.."
CXX=${CXX:-g++}
mkdir -p .pio/host
OBBW=lib/EspressoScaleProtocol/src
CLEAN=lib/ScaleWire/src
NAMES="Felicita Decent Difluid Eclair Myscale WeighMyBru Varia Eureka Registry"
if [ "${BBW_CLEAN:-0}" = "1" ]; then
  FLAGS="-DBBW_CLEAN -I$CLEAN"
  PROTOCOLS="$CLEAN/Acaia.cpp $CLEAN/Common.cpp"
  for n in $NAMES; do
    if [ -f "$CLEAN/$n.cpp" ]; then PROTOCOLS="$PROTOCOLS $CLEAN/$n.cpp"; else PROTOCOLS="$PROTOCOLS $OBBW/$n.cpp"; fi
  done
  OUT=.pio/host/bbw_tests_clean
else
  FLAGS="-I$OBBW"
  PROTOCOLS="$OBBW/Acaia.cpp $OBBW/ScaleProtocolCommon.cpp"
  for n in $NAMES; do PROTOCOLS="$PROTOCOLS $OBBW/$n.cpp"; done
  OUT=.pio/host/bbw_tests
fi
echo "scale tables: $PROTOCOLS"
# shellcheck disable=SC2086
$CXX -std=c++17 -O1 -Wall -Wextra -Isrc $FLAGS \
  test/host/test_main.cpp src/core/BrewController.cpp $PROTOCOLS \
  -o $OUT
$OUT
# The flash storage (Store.cpp, Backup.cpp) on the emulator's shims: a program of its own, because the
# shims and the portable core tests cannot share a translation unit.
# shellcheck disable=SC2086
$CXX -std=c++17 -O1 -w -DBBW_EMULATOR -Iemu/shims -Isrc $FLAGS \
  test/host/test_store_main.cpp src/storage/Store.cpp src/storage/Backup.cpp \
  -o ${OUT}_store
${OUT}_store
