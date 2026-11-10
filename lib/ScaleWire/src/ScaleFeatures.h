#pragma once
// Written from research/obbw-clean-spec.md (clean-room), not from Open Brew by Weight's code.
// What a scale family can do, and the result of asking it to do something.

#include <stdint.h>

// One bit per capability. A family ORs the ones it supports into ScaleFeatureSet::flags.
enum ScaleFeature {
    ScaleFeatureWeight = 1u << 0,
    ScaleFeatureTare = 1u << 1,
    ScaleFeatureStartTimer = 1u << 2,
    ScaleFeatureStopTimer = 1u << 3,
    ScaleFeatureResetTimer = 1u << 4,
    ScaleFeatureCombinedTareStart = 1u << 5,
    ScaleFeatureIndependentBeep = 1u << 6,
    ScaleFeatureVolume = 1u << 7,
    ScaleFeatureCommandAudibleFeedback = 1u << 8,
    ScaleFeatureHeartbeat = 1u << 9,
    ScaleFeaturePowerOff = 1u << 10,
    // No reading yet after the init writes: send them again before giving up.
    ScaleFeatureReinitWhileSilent = 1u << 11,
};

struct ScaleFeatureSet {
    uint32_t flags;
    uint8_t volumeMin;
    uint8_t volumeMax;
    uint16_t heartbeatPeriodMs;
    uint16_t maxPacketSilenceMs;
    uint16_t minimumCommandIntervalMs;

    bool has(ScaleFeature f) const { return (flags & static_cast<uint32_t>(f)) != 0; }
};

inline ScaleFeatureSet scaleFeatureSetNone() { return ScaleFeatureSet{0, 0, 0, 0, 0, 0}; }

enum class ScaleCommandResult : uint8_t {
    Ok,
    Unsupported,
    NotConnected,
    InvalidArgument,
    WriteFailed,
    Deferred
};

// Lets the caller approve a command before it is written; the callback may set a capture boundary.
struct ScaleCommandAdmission {
    bool (*approve)(void *context, uint32_t &captureBoundary) = nullptr;
    void *context = nullptr;
};

inline bool scaleCommandOk(ScaleCommandResult r) { return r == ScaleCommandResult::Ok; }

enum class ScaleOp : uint8_t {
    Tare,
    StartTimer,
    StopTimer,
    ResetTimer,
    CombinedTareStart,
    Heartbeat,
    SetVolume,
    PowerOff
};

enum class ScaleModel : uint8_t { Unknown, BookooMini, BookooUltra };
