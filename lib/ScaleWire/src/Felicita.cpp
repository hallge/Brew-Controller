// Written from SPEC.md (clean-room) for Brew Controller, 2026-11-10.
// Felicita Arc scale.

#include "ScaleProtocol.h"

namespace {

const char *const kPrefixes[] = {"FELIC"};

const uint8_t kWeightTimerMode[] = {0x32};
const ScalePayload kInitWrites[] = {
    {kWeightTimerMode, static_cast<int>(sizeof(kWeightTimerMode)), "weight_timer_mode"},
};

const uint8_t kTare[] = {0x54};
const uint8_t kStart[] = {0x52};
const uint8_t kStop[] = {0x53};
const uint8_t kReset[] = {0x43};

bool encode(ScaleOp op, uint8_t, uint8_t *out, int *length) {
    switch (op) {
        case ScaleOp::Tare: return scaleCopyPayload(kTare, sizeof(kTare), out, length);
        case ScaleOp::StartTimer: return scaleCopyPayload(kStart, sizeof(kStart), out, length);
        case ScaleOp::StopTimer: return scaleCopyPayload(kStop, sizeof(kStop), out, length);
        case ScaleOp::ResetTimer: return scaleCopyPayload(kReset, sizeof(kReset), out, length);
        default: return false;
    }
}

constexpr uint32_t kFlags = SCALE_CORE_FEATURES | ScaleFeatureCommandAudibleFeedback;

bool supportedLength(int length) { return length == 18; }

bool isDigit(uint8_t c) { return c >= '0' && c <= '9'; }

bool parseWeight(const uint8_t *d, int length, float *weight) {
    if (!d || !weight || length != 18) return false;
    const uint8_t sign = d[2];
    if (sign != '-' && sign != '+' && sign != ' ' && sign != 0x00) return false;
    uint32_t raw = 0;
    for (int i = 3; i <= 8; ++i) {
        if (!isDigit(d[i])) return false;
        raw = raw * 10 + static_cast<uint32_t>(d[i] - '0');
    }
    float w = static_cast<float>(raw) / 100.0f;
    if (sign == '-') w = -w;
    if (!scaleValidWeight(w)) return false;
    *weight = w;
    return true;
}

bool parseTimer(const uint8_t *d, int length, uint32_t *ms) {
    float w;
    if (!ms || !parseWeight(d, length, &w)) return false;
    if (scaleFelicitaAsciiTimer(d, ms)) return true;
    if (scaleLooksLikeAcaiaTimer(d[9], d[10], d[11])) {
        *ms = scaleAcaiaTimerToMs(d[9], d[10], d[11]);
        return true;
    }
    return false;
}

}  // namespace

const ScaleProtocol kScaleProtocolFelicita = {
    "felicita",
    "Felicita Arc detected",
    "ffe1",
    "ffe1",
    kPrefixes,
    sizeof(kPrefixes) / sizeof(kPrefixes[0]),
    {kFlags, 0, 0, 0, 5000, 0},
    supportedLength,
    parseWeight,
    parseTimer,
    encode,
    kInitWrites,
    sizeof(kInitWrites) / sizeof(kInitWrites[0]),
    false,
};
