#pragma once
// A tiny add-on to prove the module path (docs/ADDONS.md, "The first spike"): it is built as a module
// (scripts/build_module.py test/module/hello) and runs the things a real add-on needs from the loader:
//   - a static constructor ran (g_ctorRan), and virtual calls inside the module work,
//   - memory from Host::allocate works and keeps what was written,
//   - read-only data and initialised data were relocated correctly (a checksum over both),
//   - snprintf with floats and strtod/strlen/memcpy reach the firmware through the allow-list,
//   - every tick calls through the C table from the loop task, and the gap between ticks is measured.
// Its status line says "ok" or names the first fault. It does nothing else: no network, no grinder.
// The host tests build the same code as a compiled-in add-on and through the C table.
#include "addons/Addon.h"

namespace bbw {
namespace addon {
Addon *makeHello();
}  // namespace addon
}  // namespace bbw
