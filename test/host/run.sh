#!/usr/bin/env bash
# Host tests for the portable brew logic. Needs any C++17 compiler:
#   CXX="g++" test/host/run.sh
# Without one installed:  pip install ziglang  &&  CXX="python -m ziglang c++" test/host/run.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
CXX=${CXX:-g++}
mkdir -p .pio/host
$CXX -std=c++17 -O1 -Wall -Wextra -Isrc -Ilib/EspressoScaleProtocol/src \
  test/host/test_main.cpp src/core/BrewController.cpp \
  lib/EspressoScaleProtocol/src/Acaia.cpp \
  lib/EspressoScaleProtocol/src/ScaleProtocolCommon.cpp \
  lib/EspressoScaleProtocol/src/Felicita.cpp \
  lib/EspressoScaleProtocol/src/Decent.cpp \
  lib/EspressoScaleProtocol/src/Difluid.cpp \
  lib/EspressoScaleProtocol/src/Eclair.cpp \
  lib/EspressoScaleProtocol/src/Myscale.cpp \
  lib/EspressoScaleProtocol/src/WeighMyBru.cpp \
  lib/EspressoScaleProtocol/src/Varia.cpp \
  lib/EspressoScaleProtocol/src/Eureka.cpp \
  lib/EspressoScaleProtocol/src/Registry.cpp \
  -o .pio/host/bbw_tests
.pio/host/bbw_tests
# The flash storage (Store.cpp, Backup.cpp) on the emulator's shims: a program of its own, because the
# shims and the portable core tests cannot share a translation unit.
$CXX -std=c++17 -O1 -w -DBBW_EMULATOR -Iemu/shims -Isrc -Ilib/EspressoScaleProtocol/src \
  test/host/test_store_main.cpp src/storage/Store.cpp src/storage/Backup.cpp \
  -o .pio/host/bbw_store_tests
.pio/host/bbw_store_tests
