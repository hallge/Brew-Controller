// The registry of scale tables: the project's own code, moved here unchanged from
// lib/EspressoScaleProtocol/src/Registry.cpp on 2026-11-10, so that a clean
// build needs nothing from that directory (research/obbw-clean-spec.md §6.11).
// Only the protocols that have a driver in src/core/ScaleLink.h are listed.
// To add a scale: write its table here, list it below, and give it a driver in
// ScaleLink.h.
#include "ScaleProtocol.h"

static const ScaleProtocol *const kProtocols[] = {
    &kScaleProtocolAcaiaLegacy,
    &kScaleProtocolAcaia,
    &kScaleProtocolFelicita,
    &kScaleProtocolEclair,
    &kScaleProtocolDecent,
    &kScaleProtocolDifluid,
    &kScaleProtocolMyscale,
    &kScaleProtocolWeighMyBru,
    &kScaleProtocolVaria,
    &kScaleProtocolEureka,
};

const ScaleProtocol *scaleProtocolAt(size_t index) {
    if (index >= scaleProtocolCount()) {
        return nullptr;
    }
    return kProtocols[index];
}

size_t scaleProtocolCount() {
    return sizeof(kProtocols) / sizeof(kProtocols[0]);
}

bool scaleNameIsCompatible(const char *name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < scaleProtocolCount(); ++i) {
        if (scaleNameMatchesProtocol(name, kProtocols[i])) {
            return true;
        }
    }
    return false;
}
