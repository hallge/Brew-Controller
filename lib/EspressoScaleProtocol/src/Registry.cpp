// This project's replacement for Open Brew by Weight's protocols/Registry.cpp:
// only the protocols that have a driver in src/core/ScaleLink.h are listed.
// The other files in this directory are unmodified copies from
// Advanced-Shot-Stopper/libraries/EspressoScaleBLE/src (AGPL-3.0-or-later).
// To add a scale: copy its protocols/<Name>.cpp here, list it below, and give
// it a driver in ScaleLink.h.
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
