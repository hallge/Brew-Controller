#!/usr/bin/env bash
# Host tests for the portable brew logic. Needs any C++17 compiler:
#   CXX="g++" test/host/run.sh
# Without one installed:  pip install ziglang  &&  CXX="python -m ziglang c++" test/host/run.sh
#
# BBW_CLEAN=1 test/host/run.sh builds with the clean replacements for Open Brew
# by Weight's code (core/StopMath.h, lib/ScaleWire; research/obbw-provenance.md).
# The scale families not yet rewritten still come from lib/EspressoScaleProtocol.
set -euo pipefail
cd "$(dirname "$0")/../.."
CXX=${CXX:-g++}
mkdir -p .pio/host
OBBW=lib/EspressoScaleProtocol/src
FAMILIES="$OBBW/Felicita.cpp $OBBW/Decent.cpp $OBBW/Difluid.cpp $OBBW/Eclair.cpp
  $OBBW/Myscale.cpp $OBBW/WeighMyBru.cpp $OBBW/Varia.cpp $OBBW/Eureka.cpp $OBBW/Registry.cpp"
if [ "${BBW_CLEAN:-0}" = "1" ]; then
  FLAGS="-DBBW_CLEAN -Ilib/ScaleWire/src"
  PROTOCOLS="lib/ScaleWire/src/Acaia.cpp lib/ScaleWire/src/Common.cpp $FAMILIES"
  OUT=.pio/host/bbw_tests_clean
else
  FLAGS="-I$OBBW"
  PROTOCOLS="$OBBW/Acaia.cpp $OBBW/ScaleProtocolCommon.cpp $FAMILIES"
  OUT=.pio/host/bbw_tests
fi
# shellcheck disable=SC2086
$CXX -std=c++17 -O1 -Wall -Wextra -Isrc $FLAGS \
  test/host/test_main.cpp src/core/BrewController.cpp $PROTOCOLS \
  -o $OUT
$OUT
