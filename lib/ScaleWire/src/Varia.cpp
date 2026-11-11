// Written from SPEC.md (clean-room) for Brew Controller, 2026-11-10.
// Varia AKU scale.

#include "ScaleProtocol.h"

namespace {

const char *const kPrefixes[] = {"AKU MINI SCALE", "VARIA AKU", "Varia AKU", "AKU SCALE"};

const uint8_t kTare[] = {0xfa, 0x82, 0x01, 0x01, 0x82};
const uint8_t kStart[] = {0xfa, 0x88, 0x01, 0x01, 0x88};
const uint8_t kStop[] = {0xfa, 0x89, 0x01, 0x02, 0x8a};
const uint8_t kReset[] = {0xfa, 0x8a, 0x01, 0x03, 0x88};

bool encode(ScaleOp op, uint8_t, uint8_t *out, int *length) {
    switch (op) {
        case ScaleOp::Tare: return scaleCopyPayload(kTare, sizeof(kTare), out, length);
        case ScaleOp::StartTimer: return scaleCopyPayload(kStart, sizeof(kStart), out, length);
        case ScaleOp::StopTimer: return scaleCopyPayload(kStop, sizeof(kStop), out, length);
        case ScaleOp::ResetTimer: return scaleCopyPayload(kReset, sizeof(kReset), out, length);
        default: return false;
    }
}

bool supportedLength(int length) { return length == 6 || length == 7; }

// XOR of bytes 1..length-2 equals the last byte.
bool checkOk(const uint8_t *d, int length) {
    return length >= 3 && scaleXorBytes(d + 1, length - 2) == d[length - 1];
}

bool parseWeight(const uint8_t *d, int length, float *weight) {
    if (!d || !weight || length != 7) return false;
    if (d[0] != 0xfa || d[1] != 0x01 || !checkOk(d, length)) return false;
    const int32_t magnitude = static_cast<int32_t>((static_cast<uint32_t>(d[3] & 0x0f) << 16) |
                                                   (static_cast<uint32_t>(d[4]) << 8) | d[5]);
    const int32_t hundredths = (d[3] & 0x10) ? -magnitude : magnitude;
    const float w = static_cast<float>(hundredths) * 0.01f;
    if (!scaleValidWeight(w)) return false;
    *weight = w;
    return true;
}

bool parseTimer(const uint8_t *d, int length, uint32_t *ms) {
    if (!d || !ms || length != 6) return false;
    if (d[0] != 0xfa || d[1] != 0x87 || !checkOk(d, length)) return false;
    const uint32_t seconds = (static_cast<uint32_t>(d[3]) << 8) | d[4];
    *ms = seconds * 1000;
    return true;
}

}  // namespace

const ScaleProtocol kScaleProtocolVaria = {
    "varia",
    "Varia AKU detected",
    "fff1",
    "fff2",
    kPrefixes,
    sizeof(kPrefixes) / sizeof(kPrefixes[0]),
    {SCALE_CORE_FEATURES, 0, 0, 0, 5000, 0},
    supportedLength,
    parseWeight,
    parseTimer,
    encode,
    nullptr,
    0,
    true,
};
