// Written from SPEC.md (clean-room) for Brew Controller, 2026-11-10.
// Decent Scale (and EspressiScale).

#include "ScaleProtocol.h"

namespace {

const char *const kPrefixes[] = {"Decent Scale", "EspressiScale"};

const uint8_t kTare[] = {0x03, 0x0f, 0x00, 0x00, 0x00, 0x01, 0x0d};
const uint8_t kHeartbeat[] = {0x03, 0x0a, 0x03, 0xff, 0xff, 0x00, 0x0a};

bool encode(ScaleOp op, uint8_t, uint8_t *out, int *length) {
    switch (op) {
        case ScaleOp::Tare: return scaleCopyPayload(kTare, sizeof(kTare), out, length);
        case ScaleOp::Heartbeat: return scaleCopyPayload(kHeartbeat, sizeof(kHeartbeat), out, length);
        default: return false;
    }
}

constexpr uint32_t kFlags = ScaleFeatureWeight | ScaleFeatureTare | ScaleFeatureHeartbeat;

bool supportedLength(int length) { return length == 7 || length == 10; }

bool parseWeight(const uint8_t *d, int length, float *weight) {
    if (!d || !weight || !supportedLength(length)) return false;
    if (d[0] != 0x03 || (d[1] != 0xca && d[1] != 0xce)) return false;
    const uint8_t check = d[length - 1];
    if (check != 0 && scaleXorBytes(d, length - 1) != check) return false;
    const int16_t tenths = static_cast<int16_t>((d[2] << 8) | d[3]);
    const float w = static_cast<float>(tenths) / 10.0f;
    if (!scaleValidWeight(w)) return false;
    *weight = w;
    return true;
}

}  // namespace

const ScaleProtocol kScaleProtocolDecent = {
    "decent",
    "Decent Scale detected",
    "fff4",
    "36f5",
    kPrefixes,
    sizeof(kPrefixes) / sizeof(kPrefixes[0]),
    {kFlags, 0, 0, 5000, 5000, 0},
    supportedLength,
    parseWeight,
    nullptr,
    encode,
    nullptr,
    0,
    false,
};
