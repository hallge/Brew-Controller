#pragma once
// Sends a diagnostics report to the relay (docs/TELEMETRY.md): one HTTPS POST of the signed
// envelope. On the board this is TelemetryLink.cpp, checked against Mozilla's root certificates
// (certs/x509_crt_bundle.bin, as PagePreview.cpp); the PC emulator uses the PC's curl
// (emu/src/emu_telemetry.cpp).
//
// Blocks for a few seconds at most. Called only from the loop task while the controller is idle.

#include <stddef.h>

namespace bbw {

// The relay every controller reports to unless Setup names another (for testing).
constexpr const char *DIAG_RELAY_DEFAULT = "https://bbw-telemetry-relay.bbw-diagnostics.workers.dev/report";

// Returns the HTTP status (201 when the relay took it), or below 0 when it could not be reached;
// `problem` says what went wrong, in words.
int diagPost(const char *url, const char *envelope, char *problem, size_t problemSize);

}  // namespace bbw
