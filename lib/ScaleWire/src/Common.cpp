// Written from research/obbw-clean-spec.md (clean-room), not from Open Brew by Weight's code.
// Helpers shared by the scale families.

#include "ScaleProtocol.h"

#include <math.h>

bool scaleNameMatchesProtocol(const char *name, const ScaleProtocol *p) {
    if (!name || !name[0] || !p || !p->namePrefixes) return false;
    for (size_t i = 0; i < p->namePrefixCount; i++) {
        const char *prefix = p->namePrefixes[i];
        if (!prefix || !prefix[0]) continue;
        if (strncmp(name, prefix, strlen(prefix)) == 0) return true;
    }
    return false;
}

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool scaleParseUuid16(const char *uuid, uint16_t *out) {
    if (!uuid || !out) return false;
    uint16_t v = 0;
    for (int i = 0; i < 4; i++) {
        int d = hexDigit(uuid[i]);  // the terminator is not a hex digit, so a short string stops here
        if (d < 0) return false;
        v = static_cast<uint16_t>((v << 4) | d);
    }
    if (uuid[4] != '\0') return false;
    *out = v;
    return true;
}

bool scaleUuid16AllowsNamelessConnect(uint16_t uuid) {
    if (uuid == 0xFFF1 || uuid == 0xFFF2) return false;
    size_t n = scaleProtocolCount();
    for (size_t i = 0; i < n; i++) {
        const ScaleProtocol *p = scaleProtocolAt(i);
        if (!p || p->requireAdvertisedName) continue;
        uint16_t u = 0;
        if (scaleParseUuid16(p->readUuid, &u) && u == uuid) return true;
        if (scaleParseUuid16(p->writeUuid, &u) && u == uuid) return true;
    }
    return false;
}

bool scaleValidWeight(float w) { return isfinite(w) && fabsf(w) <= SCALE_MAX_WEIGHT_GRAMS; }

bool scaleCopyPayload(const uint8_t *cmd, int n, uint8_t *out, int *length) {
    if (!cmd || !out || !length || n <= 0 || n > SCALE_MAX_COMMAND_LENGTH) return false;
    memcpy(out, cmd, static_cast<size_t>(n));
    *length = n;
    return true;
}

uint8_t scaleXorBytes(const uint8_t *d, int n) {
    uint8_t x = 0;
    if (!d) return 0;
    for (int i = 0; i < n; i++) x ^= d[i];
    return x;
}

uint32_t scaleReadUint32LittleEndian(const uint8_t *d) {
    return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8) |
           (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
}

float scaleDecimalDivisor(uint8_t e) {
    switch (e) {
        case 0: return 1.0f;
        case 1: return 10.0f;
        case 2: return 100.0f;
        case 3: return 1000.0f;
        case 4: return 10000.0f;
        default: return 1.0f;
    }
}

bool scaleLooksLikeAcaiaTimer(uint8_t m, uint8_t s, uint8_t t) { return m <= 99 && s <= 59 && t <= 9; }

uint32_t scaleAcaiaTimerToMs(uint8_t m, uint8_t s, uint8_t t) {
    return (static_cast<uint32_t>(m) * 60u + s) * 1000u + static_cast<uint32_t>(t) * 100u;
}

bool scaleValidAcaiaChecksum(const uint8_t *d, int n) {
    if (!d || n < 6) return false;
    uint8_t even = 0, odd = 0;
    for (int i = 3; i <= n - 3; i++) {
        if (((i - 3) & 1) == 0) even = static_cast<uint8_t>(even + d[i]);
        else odd = static_cast<uint8_t>(odd + d[i]);
    }
    return even == d[n - 2] && odd == d[n - 1];
}

bool scaleFelicitaAsciiTimer(const uint8_t *d, uint32_t *ms) {
    if (!d || !ms) return false;
    for (int i = 9; i <= 13; i++) {
        if (d[i] < '0' || d[i] > '9') return false;
    }
    uint8_t m = static_cast<uint8_t>((d[9] - '0') * 10 + (d[10] - '0'));
    uint8_t s = static_cast<uint8_t>((d[11] - '0') * 10 + (d[12] - '0'));
    uint8_t t = static_cast<uint8_t>(d[13] - '0');
    if (!scaleLooksLikeAcaiaTimer(m, s, t)) return false;
    *ms = scaleAcaiaTimerToMs(m, s, t);
    return true;
}
