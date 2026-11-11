// Written from SPEC.md (clean-room) for Brew Controller, 2026-11-10.
// WeighMyBru scale.

#include "ScaleProtocol.h"

namespace {

const char *const kPrefixes[] = {"WeighMyBru"};

const uint8_t kTare[] = {0x03, 0x0a, 0x01, 0x01, 0x00, 0x09};

bool encode(ScaleOp op, uint8_t, uint8_t *out, int *length) {
    switch (op) {
        case ScaleOp::Tare: return scaleCopyPayload(kTare, sizeof(kTare), out, length);
        default: return false;
    }
}

constexpr uint32_t kFlags = ScaleFeatureWeight | ScaleFeatureTare;

bool supportedLength(int length) { return length == 20; }

bool parseWeight(const uint8_t *d, int length, float *weight) {
    if (!d || !weight || length != 20) return false;
    if (d[0] != 0x03 || d[1] != 0x0b || scaleXorBytes(d, 19) != d[19]) return false;
    const uint32_t raw = (static_cast<uint32_t>(d[7]) << 16) | (static_cast<uint32_t>(d[8]) << 8) |
                         static_cast<uint32_t>(d[9]);
    float w = static_cast<float>(raw) / 100.0f;
    if (d[6] == '-') w = -w;
    if (!scaleValidWeight(w)) return false;
    *weight = w;
    return true;
}

}  // namespace

// The read and write UUIDs look swapped against Nordic UART naming; kept as specified.
const ScaleProtocol kScaleProtocolWeighMyBru = {
    "weighmybru",
    "WeighMyBru detected",
    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
    kPrefixes,
    sizeof(kPrefixes) / sizeof(kPrefixes[0]),
    {kFlags, 0, 0, 0, 8000, 0},
    supportedLength,
    parseWeight,
    nullptr,
    encode,
    nullptr,
    0,
    false,
};
