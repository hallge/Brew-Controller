// Written from SPEC.md (clean-room) for Brew Controller, 2026-11-10.
// AtomHeart Eclair scale.

#include "ScaleProtocol.h"

namespace {

const char *const kPrefixes[] = {"ECLAI"};

const uint8_t kTare[] = {0x54, 0x01, 0x01};
const uint8_t kStart[] = {0x53, 0x01, 0x01};
const uint8_t kStop[] = {0x45, 0x01, 0x01};
const uint8_t kReset[] = {0x52, 0x01, 0x01};

bool encode(ScaleOp op, uint8_t, uint8_t *out, int *length) {
    switch (op) {
        case ScaleOp::Tare: return scaleCopyPayload(kTare, sizeof(kTare), out, length);
        case ScaleOp::StartTimer: return scaleCopyPayload(kStart, sizeof(kStart), out, length);
        case ScaleOp::StopTimer: return scaleCopyPayload(kStop, sizeof(kStop), out, length);
        case ScaleOp::ResetTimer: return scaleCopyPayload(kReset, sizeof(kReset), out, length);
        default: return false;
    }
}

bool supportedLength(int length) { return length == 10; }

bool parseWeight(const uint8_t *d, int length, float *weight) {
    if (!d || !weight || length != 10) return false;
    if (d[0] != 'W' || scaleXorBytes(d + 1, 8) != d[9]) return false;
    const int32_t milligrams = static_cast<int32_t>(scaleReadUint32LittleEndian(d + 1));
    const float w = static_cast<float>(milligrams) / 1000.0f;
    if (!scaleValidWeight(w)) return false;
    *weight = w;
    return true;
}

bool parseTimer(const uint8_t *d, int length, uint32_t *ms) {
    float w;
    if (!ms || !parseWeight(d, length, &w)) return false;
    *ms = scaleReadUint32LittleEndian(d + 5);
    return true;
}

}  // namespace

const ScaleProtocol kScaleProtocolEclair = {
    "atomheart_eclair",
    "AtomHeart Eclair detected",
    "AD736C5F-BBC9-1F96-D304-CB5D5F41E160",
    "4F9A45BA-8E1B-4E07-E157-0814D393B968",
    kPrefixes,
    sizeof(kPrefixes) / sizeof(kPrefixes[0]),
    {SCALE_CORE_FEATURES, 0, 0, 0, 5000, 0},
    supportedLength,
    parseWeight,
    parseTimer,
    encode,
    nullptr,
    0,
    false,
};
