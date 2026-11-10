// Written from SPEC.md (clean-room) for Brew Controller, 2026-11-10.
// DiFluid Microbalance.

#include "ScaleProtocol.h"

namespace {

const char *const kPrefixes[] = {"Microbalance"};

const uint8_t kUnitGram[] = {0xdf, 0xdf, 0x01, 0x04, 0x01, 0x00, 0xc4};
const uint8_t kAutoNotify[] = {0xdf, 0xdf, 0x01, 0x00, 0x01, 0x01, 0xc1};

const ScalePayload kInitWrites[] = {
    {kUnitGram, static_cast<int>(sizeof(kUnitGram)), "unit_gram"},
    {kAutoNotify, static_cast<int>(sizeof(kAutoNotify)), "auto_notify"},
};

const uint8_t kTare[] = {0xdf, 0xdf, 0x03, 0x02, 0x01, 0x01, 0xc5};
const uint8_t kHeartbeat[] = {0xdf, 0xdf, 0x03, 0x05, 0x00, 0xc6};

bool encode(ScaleOp op, uint8_t, uint8_t *out, int *length) {
    switch (op) {
        case ScaleOp::Tare: return scaleCopyPayload(kTare, sizeof(kTare), out, length);
        case ScaleOp::Heartbeat: return scaleCopyPayload(kHeartbeat, sizeof(kHeartbeat), out, length);
        default: return false;
    }
}

constexpr uint32_t kFlags = ScaleFeatureWeight | ScaleFeatureTare | ScaleFeatureHeartbeat;

bool supportedLength(int length) { return length == 19 || length == 20; }

bool parseWeight(const uint8_t *d, int length, float *weight) {
    if (!d || !weight || length < 19) return false;
    if (d[0] != 0xdf || d[1] != 0xdf) return false;
    uint32_t sum = 0;
    for (int i = 0; i < length - 1; ++i) sum += d[i];
    if (static_cast<uint8_t>(sum & 0xff) != d[length - 1]) return false;
    if (d[2] != 0x03 || d[3] != 0x00 || d[4] < 13 || length < 6 + d[4]) return false;
    const uint32_t raw = (static_cast<uint32_t>(d[5]) << 24) | (static_cast<uint32_t>(d[6]) << 16) |
                         (static_cast<uint32_t>(d[7]) << 8) | static_cast<uint32_t>(d[8]);
    const float w = static_cast<float>(static_cast<int32_t>(raw)) / 10.0f;
    if (!scaleValidWeight(w)) return false;
    *weight = w;
    return true;
}

}  // namespace

const ScaleProtocol kScaleProtocolDifluid = {
    "difluid",
    "DiFluid Microbalance detected",
    "aa01",
    "aa01",
    kPrefixes,
    sizeof(kPrefixes) / sizeof(kPrefixes[0]),
    {kFlags, 0, 0, 2000, 5000, 0},
    supportedLength,
    parseWeight,
    nullptr,
    encode,
    kInitWrites,
    sizeof(kInitWrites) / sizeof(kInitWrites[0]),
    false,
};
