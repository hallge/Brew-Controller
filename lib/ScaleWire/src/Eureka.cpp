// Written from SPEC.md (clean-room) for Brew Controller, 2026-11-10.
// Eureka Precisa scale.

#include "ScaleProtocol.h"

namespace {

const char *const kPrefixes[] = {"CFS-9002", "LSJ-001"};

const uint8_t kTare[] = {0xaa, 0x02, 0x31, 0x31, 0x00, 0x00};
const uint8_t kStart[] = {0xaa, 0x02, 0x33, 0x33, 0x00, 0x00};
const uint8_t kStop[] = {0xaa, 0x02, 0x34, 0x34, 0x00, 0x00};
const uint8_t kReset[] = {0xaa, 0x02, 0x35, 0x35, 0x00, 0x00};

bool encode(ScaleOp op, uint8_t, uint8_t *out, int *length) {
    switch (op) {
        case ScaleOp::Tare: return scaleCopyPayload(kTare, sizeof(kTare), out, length);
        case ScaleOp::StartTimer: return scaleCopyPayload(kStart, sizeof(kStart), out, length);
        case ScaleOp::StopTimer: return scaleCopyPayload(kStop, sizeof(kStop), out, length);
        case ScaleOp::ResetTimer: return scaleCopyPayload(kReset, sizeof(kReset), out, length);
        default: return false;
    }
}

bool supportedLength(int length) { return length == 11; }

bool parseWeight(const uint8_t *d, int length, float *weight) {
    if (!d || !weight || length != 11) return false;
    if (d[0] != 0xaa || d[1] != 0x09 || d[2] != 0x41) return false;
    if (d[6] != 0 && d[6] != 1) return false;
    const uint32_t raw = static_cast<uint32_t>(d[7]) | (static_cast<uint32_t>(d[8]) << 8);
    float w = static_cast<float>(raw) * 0.1f;
    if (d[6] == 1) w = -w;
    if (!scaleValidWeight(w)) return false;
    *weight = w;
    return true;
}

}  // namespace

const ScaleProtocol kScaleProtocolEureka = {
    "eureka",
    "Eureka Precisa detected",
    "fff1",
    "fff2",
    kPrefixes,
    sizeof(kPrefixes) / sizeof(kPrefixes[0]),
    {SCALE_CORE_FEATURES, 0, 0, 0, 5000, 0},
    supportedLength,
    parseWeight,
    nullptr,
    encode,
    nullptr,
    0,
    true,
};
