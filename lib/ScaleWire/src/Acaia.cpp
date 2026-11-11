// Written from research/obbw-clean-spec.md (clean-room), not from Open Brew by Weight's code.
// Acaia scales: the old (2a80) and the new (serial service) protocol.

#include "ScaleProtocol.h"

namespace {

const char *const kPrefixes[] = {"CINCO", "ACAIA", "PYXIS", "LUNAR", "PEARL", "PROCH"};

const uint8_t kIdentify[] = {0xef, 0xdd, 0x0b, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
                             0x37, 0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x9a, 0x6d};
const uint8_t kNotifyRequest[] = {0xef, 0xdd, 0x0c, 0x09, 0x00, 0x01, 0x01,
                                  0x02, 0x02, 0x05, 0x03, 0x04, 0x15, 0x06};

const ScalePayload kInitWrites[] = {
    {kIdentify, static_cast<int>(sizeof(kIdentify)), "identify"},
    {kNotifyRequest, static_cast<int>(sizeof(kNotifyRequest)), "notification_request"},
};

const uint8_t kTare[] = {0xef, 0xdd, 0x04, 0x00, 0x00, 0x00};
const uint8_t kStart[] = {0xef, 0xdd, 0x0d, 0x00, 0x00, 0x00, 0x00};
const uint8_t kStop[] = {0xef, 0xdd, 0x0d, 0x00, 0x02, 0x00, 0x02};
const uint8_t kReset[] = {0xef, 0xdd, 0x0d, 0x00, 0x01, 0x00, 0x01};
const uint8_t kHeartbeat[] = {0xef, 0xdd, 0x00, 0x02, 0x00, 0x02, 0x00};

bool encode(ScaleOp op, uint8_t, uint8_t *out, int *length) {
    switch (op) {
        case ScaleOp::Tare: return scaleCopyPayload(kTare, sizeof(kTare), out, length);
        case ScaleOp::StartTimer: return scaleCopyPayload(kStart, sizeof(kStart), out, length);
        case ScaleOp::StopTimer: return scaleCopyPayload(kStop, sizeof(kStop), out, length);
        case ScaleOp::ResetTimer: return scaleCopyPayload(kReset, sizeof(kReset), out, length);
        case ScaleOp::Heartbeat: return scaleCopyPayload(kHeartbeat, sizeof(kHeartbeat), out, length);
        default: return false;
    }
}

constexpr uint32_t kFlags = SCALE_CORE_FEATURES | ScaleFeatureHeartbeat |
                            ScaleFeatureCommandAudibleFeedback | ScaleFeatureReinitWhileSilent;

// ---- old protocol ----

bool legacyLength(int length) { return length == 10 || length == 14; }

bool legacyWeight(const uint8_t *d, int length, float *weight) {
    if (!d || !weight || !legacyLength(length)) return false;
    if (d[6] < 1 || d[6] > 4) return false;
    if (length == 14 && !scaleValidAcaiaChecksum(d, length)) return false;
    uint16_t raw = static_cast<uint16_t>(d[2] | (d[3] << 8));
    float w = static_cast<float>(raw) / scaleDecimalDivisor(d[6]);
    if (d[7] & 0x02) w = -w;
    if (!scaleValidWeight(w)) return false;
    *weight = w;
    return true;
}

bool legacyTimer(const uint8_t *, int, uint32_t *) { return false; }

// ---- new protocol ----

bool currentLength(int length) { return length == 10 || length == 13 || length == 17; }

bool headerOk(const uint8_t *d, int length) {
    return d[0] == 0xef && d[1] == 0xdd && d[2] == 0x0c && d[3] + 5 == length &&
           scaleValidAcaiaChecksum(d, length);
}

bool currentWeight(const uint8_t *d, int length, float *weight) {
    if (!d || !weight || (length != 13 && length != 17)) return false;
    if (!headerOk(d, length) || d[4] != 0x05) return false;
    if (d[9] < 1 || d[9] > 4) return false;
    uint16_t raw = static_cast<uint16_t>(d[5] | (d[6] << 8));
    float w = static_cast<float>(raw) / scaleDecimalDivisor(d[9]);
    if (d[10] & 0x02) w = -w;
    if (!scaleValidWeight(w)) return false;
    *weight = w;
    return true;
}

bool currentTimer(const uint8_t *d, int length, uint32_t *ms) {
    if (!d || !ms || !currentLength(length) || !headerOk(d, length)) return false;
    if (d[4] == 0x07 && scaleLooksLikeAcaiaTimer(d[5], d[6], d[7])) {
        *ms = scaleAcaiaTimerToMs(d[5], d[6], d[7]);
        return true;
    }
    if (d[4] == 0x05 && length == 17 && scaleLooksLikeAcaiaTimer(d[11], d[12], d[13])) {
        *ms = scaleAcaiaTimerToMs(d[11], d[12], d[13]);
        return true;
    }
    return false;
}

}  // namespace

const ScaleProtocol kScaleProtocolAcaiaLegacy = {
    "acaia_legacy",
    "Old version Acaia detected",
    "2a80",
    "2a80",
    kPrefixes,
    sizeof(kPrefixes) / sizeof(kPrefixes[0]),
    {kFlags, 0, 0, 2750, 5000, 0},
    legacyLength,
    legacyWeight,
    legacyTimer,
    encode,
    kInitWrites,
    sizeof(kInitWrites) / sizeof(kInitWrites[0]),
    false,
};

const ScaleProtocol kScaleProtocolAcaia = {
    "acaia",
    "New version Acaia detected",
    "49535343-1e4d-4bd9-ba61-23c647249616",
    "49535343-8841-43f4-a8d4-ecbe34729bb3",
    kPrefixes,
    sizeof(kPrefixes) / sizeof(kPrefixes[0]),
    {kFlags, 0, 0, 2750, 5000, 0},
    currentLength,
    currentWeight,
    currentTimer,
    encode,
    kInitWrites,
    sizeof(kInitWrites) / sizeof(kInitWrites[0]),
    false,
};
