#pragma once
// Written from research/obbw-clean-spec.md (clean-room), not from Open Brew by Weight's code.
// The table that describes one scale family, and the helpers its parsers share.

#include "ScaleFeatures.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SCALE_MAX_PACKET_LENGTH 20
#define SCALE_MAX_COMMAND_LENGTH 20
#define SCALE_MAX_WEIGHT_GRAMS 10000.0f

#define SCALE_CORE_FEATURES                                                                 \
    (ScaleFeatureWeight | ScaleFeatureTare | ScaleFeatureStartTimer | ScaleFeatureStopTimer | \
     ScaleFeatureResetTimer)

// One fixed byte string written to the scale after connecting.
struct ScalePayload {
    const uint8_t *data;
    int length;
    const char *label;
};

struct ScaleProtocol {
    const char *id;
    const char *debugLabel;
    const char *readUuid;
    const char *writeUuid;
    const char *const *namePrefixes;
    size_t namePrefixCount;
    ScaleFeatureSet features;
    bool (*supportedPacketLength)(int length);
    bool (*parseWeight)(const uint8_t *data, int length, float *weight);
    bool (*parseTimer)(const uint8_t *data, int length, uint32_t *timerMs);
    bool (*encodeCommand)(ScaleOp op, uint8_t arg, uint8_t *out, int *length);
    const ScalePayload *initWrites;
    size_t initWriteCount;
    bool requireAdvertisedName;
};

// Registry.cpp
const ScaleProtocol *scaleProtocolAt(size_t index);
size_t scaleProtocolCount();
bool scaleNameIsCompatible(const char *name);

// Common.cpp
bool scaleNameMatchesProtocol(const char *name, const ScaleProtocol *p);
bool scaleParseUuid16(const char *uuid, uint16_t *out);
bool scaleUuid16AllowsNamelessConnect(uint16_t uuid);
bool scaleValidWeight(float w);
bool scaleCopyPayload(const uint8_t *cmd, int n, uint8_t *out, int *length);
uint8_t scaleXorBytes(const uint8_t *d, int n);
uint32_t scaleReadUint32LittleEndian(const uint8_t *d);
float scaleDecimalDivisor(uint8_t e);
bool scaleLooksLikeAcaiaTimer(uint8_t m, uint8_t s, uint8_t t);
uint32_t scaleAcaiaTimerToMs(uint8_t m, uint8_t s, uint8_t t);
bool scaleValidAcaiaChecksum(const uint8_t *d, int n);
bool scaleFelicitaAsciiTimer(const uint8_t *d, uint32_t *ms);

extern const ScaleProtocol kScaleProtocolAcaiaLegacy;
extern const ScaleProtocol kScaleProtocolAcaia;
extern const ScaleProtocol kScaleProtocolFelicita;
extern const ScaleProtocol kScaleProtocolEclair;
extern const ScaleProtocol kScaleProtocolDecent;
extern const ScaleProtocol kScaleProtocolDifluid;
extern const ScaleProtocol kScaleProtocolMyscale;
extern const ScaleProtocol kScaleProtocolWeighMyBru;
extern const ScaleProtocol kScaleProtocolVaria;
extern const ScaleProtocol kScaleProtocolEureka;
