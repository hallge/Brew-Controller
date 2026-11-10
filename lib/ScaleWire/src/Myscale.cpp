// Written from SPEC.md (clean-room) for Brew Controller, 2026-11-10.
// MyScale.

#include "ScaleProtocol.h"

namespace {

const char *const kPrefixes[] = {"blackcoffee", "my_scale", "MY_SCALE"};

const uint8_t kTare[] = {0xac, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd2, 0xd2};

bool encode(ScaleOp op, uint8_t, uint8_t *out, int *length) {
    switch (op) {
        case ScaleOp::Tare: return scaleCopyPayload(kTare, sizeof(kTare), out, length);
        default: return false;
    }
}

constexpr uint32_t kFlags = ScaleFeatureWeight | ScaleFeatureTare;

bool supportedLength(int length) { return length >= 15 && length <= SCALE_MAX_PACKET_LENGTH; }

bool parseWeight(const uint8_t *d, int length, float *weight) {
    if (!d || !weight || length < 15) return false;
    const uint8_t signNibble = d[2] >> 4;
    const bool negative = signNibble == 0x8 || signNibble == 0xc;
    const uint32_t raw = (static_cast<uint32_t>(d[3] & 0x0f) << 24) | (static_cast<uint32_t>(d[4]) << 16) |
                         (static_cast<uint32_t>(d[5]) << 8) | static_cast<uint32_t>(d[6]);
    float w = static_cast<float>(raw) / 1000.0f;
    if (negative) w = -w;
    if (!scaleValidWeight(w)) return false;
    *weight = w;
    return true;
}

}  // namespace

const ScaleProtocol kScaleProtocolMyscale = {
    "myscale",
    "MyScale detected",
    "ffb2",
    "ffb1",
    kPrefixes,
    sizeof(kPrefixes) / sizeof(kPrefixes[0]),
    {kFlags, 0, 0, 0, 5000, 0},
    supportedLength,
    parseWeight,
    nullptr,
    encode,
    nullptr,
    0,
    false,
};
