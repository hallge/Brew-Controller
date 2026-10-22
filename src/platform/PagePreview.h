#pragma once
// Reads a shop's page for a subscription card (core/Subscriptions.h parsePageHead): its title,
// picture, icon, colour, and which subscription system it uses.
//
// On the board this is PagePreview.cpp: HTTPS checked against Mozilla's root certificates
// (certs/x509_crt_bundle.bin, built by certs/gen_cert_bundle.py), redirects followed, at most the
// first 192 KB read, up to the end of <head>. The PC emulator uses the PC's curl
// (emu/src/emu_preview.cpp). Nothing is sent but the request for the page itself.
//
// Blocks for a few seconds at most. Called only from the loop task while the controller is idle;
// the control task, which stops shots, never waits on it.

#include <stddef.h>

#include "core/Subscriptions.h"

namespace bbw {

// Fills `out` from the page at `url`. Returns the final HTTP status (200 when the page was read), or
// below 0 when it could not be reached; `problem` says what went wrong, in words.
int fetchPagePreview(const char *url, PageMeta &out, char *problem, size_t problemSize);

}  // namespace bbw
