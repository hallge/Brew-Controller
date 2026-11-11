#pragma once
// Written from research/obbw-clean-spec.md (clean-room), not from Open Brew by Weight's code.
// Stop-time prediction, offset learning and the per-recipe learning-factor selector.
// Portable: no Arduino, no STL containers, no exceptions.

#include "Types.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace bbw {

constexpr size_t TREND_POINT_COUNT = 10;
constexpr float TREND_MIN_LAST_SAMPLE_G = 10.0f;
constexpr float TREND_MIN_HORIZON_S = 0.25f;

inline float limitOffset(float g) {
    if (g < 0.0f) return 0.0f;
    if (g > MAX_OFFSET_G) return MAX_OFFSET_G;
    return g;
}

// Time (s since the shot started) at which the cup weight reaches targetG, from a straight line
// through the last TREND_POINT_COUNT samples. fallbackS when the data can't support a prediction.
inline float predictCrossingS(const float *t, const float *w, size_t n, float targetG,
                              float fallbackS) {
    if (!t || !w || n < TREND_POINT_COUNT) return fallbackS;
    if (!isfinite(targetG) || !isfinite(fallbackS)) return fallbackS;
    const size_t first = n - TREND_POINT_COUNT;
    const float tLast = t[n - 1];
    if (!(w[n - 1] >= TREND_MIN_LAST_SAMPLE_G)) return fallbackS;

    float prev = 0.0f;
    for (size_t i = 0; i < TREND_POINT_COUNT; i++) {
        const float ti = t[first + i];
        const float wi = w[first + i];
        if (!isfinite(ti) || !isfinite(wi)) return fallbackS;
        if (i > 0 && !(ti > prev)) return fallbackS;
        prev = ti;
    }

    // Times relative to the newest sample keep the arithmetic small.
    const float count = static_cast<float>(TREND_POINT_COUNT);
    float sumT = 0.0f, sumW = 0.0f;
    for (size_t i = 0; i < TREND_POINT_COUNT; i++) {
        sumT += t[first + i] - tLast;
        sumW += w[first + i];
    }
    const float meanT = sumT / count;
    const float meanW = sumW / count;
    float sxx = 0.0f, sxy = 0.0f;
    for (size_t i = 0; i < TREND_POINT_COUNT; i++) {
        const float dt = (t[first + i] - tLast) - meanT;
        sxx += dt * dt;
        sxy += dt * (w[first + i] - meanW);
    }
    if (!isfinite(sxx) || sxx < 1e-7f) return fallbackS;
    const float slope = sxy / sxx;
    if (!isfinite(slope) || slope <= 0.0f) return fallbackS;

    // Relative time at which the fitted line equals the target.
    const float rel = meanT + (targetG - meanW) / slope;
    const float crossing = tLast + rel;
    if (!isfinite(crossing) || crossing < tLast + TREND_MIN_HORIZON_S) return fallbackS;
    return crossing;
}

// Learns the stop offset between shots. False (out untouched) when the inputs can't be trusted.
inline bool nextOffset(float offsetG, float finalG, float targetG, float alpha, float &out) {
    if (!(alpha >= 0.01f && alpha <= 1.0f)) return false;
    const float observed = offsetG + (finalG - targetG);
    if (!isfinite(offsetG) || !isfinite(observed)) return false;
    if (fabsf(observed) > MAX_OFFSET_G) return false;
    out = limitOffset(offsetG + alpha * (finalG - targetG));
    return true;
}

// Learning factors a recipe can use, in hundredths.
constexpr uint8_t kGainChoices[4] = {10, 30, 50, 100};

inline bool gainValid(uint8_t g) { return g >= 1 && g <= 100; }

// Replays recent offsets under each candidate gain and switches to a clearly better one.
struct AlphaSelector {
    static constexpr int TRACKS = 5;  // four choices, plus the starting gain
    static constexpr int HISTORY = 20;
    static constexpr uint8_t SWITCH_GAP = 10;

    float point[TRACKS] = {0, 0, 0, 0, 0};  // where each track's replay begins
    float ring[HISTORY] = {};               // last accepted observations
    uint8_t head = 0;                       // next slot to write (the oldest once full)
    uint8_t held = 0;
    uint8_t cadence = 0;
    uint8_t sinceSwitch = 0;
    uint8_t challenger = 255;  // 255: none yet
    uint8_t wins = 0;
    uint8_t startGain = 0;
    bool seeded = false;

    uint8_t gainOf(int track) const { return track < 4 ? kGainChoices[track] : startGain; }

    uint8_t observe(float observation, float seed, uint8_t alpha) {
        if (!isfinite(observation) || fabsf(observation) > MAX_OFFSET_G) return alpha;
        if (!isfinite(seed)) return alpha;
        if (!gainValid(alpha)) return alpha;

        if (!seeded) {
            seeded = true;
            startGain = alpha;
            for (int k = 0; k < TRACKS; k++) point[k] = seed;
        }
        if (held == HISTORY) {
            const float oldest = ring[head];
            for (int k = 0; k < TRACKS; k++) {
                point[k] = limitOffset(point[k] + (gainOf(k) / 100.0f) * (oldest - point[k]));
            }
        }
        ring[head] = observation;
        head = static_cast<uint8_t>((head + 1) % HISTORY);
        if (held < HISTORY) held++;
        if (sinceSwitch < SWITCH_GAP) sinceSwitch++;
        cadence = static_cast<uint8_t>((cadence + 1) % 5);
        if (held < HISTORY || cadence != 0) return alpha;

        // After a full ring head points at the oldest entry.
        float score[TRACKS];
        for (int k = 0; k < TRACKS; k++) {
            float p = point[k];
            const float g = gainOf(k) / 100.0f;
            float s = 0.0f;
            for (int i = 0; i < HISTORY; i++) {
                const float obs = ring[(head + i) % HISTORY];
                const float e = obs - p;
                s += e * e;
                p = limitOffset(p + g * e);
            }
            score[k] = s;
        }
        for (int k = 0; k < TRACKS; k++) {
            if (!isfinite(score[k])) {
                wins = 0;
                return alpha;
            }
        }

        int incumbent = 4;
        for (int k = 0; k < 4; k++) {
            if (kGainChoices[k] == alpha) {
                incumbent = k;
                break;
            }
        }
        int best = incumbent;
        float bestScore = score[incumbent];
        for (int k = 0; k < 4; k++) {
            if (score[k] < bestScore) {
                best = k;
                bestScore = score[k];
            }
        }
        bool tied = false;
        for (int k = 0; k < 4; k++) {
            if (k != best && score[k] == score[best]) tied = true;
        }

        if (tied || best == incumbent || sinceSwitch < SWITCH_GAP ||
            !(score[best] <= score[incumbent] * 0.90f)) {
            wins = 0;
            return alpha;
        }
        if (challenger == best) {
            wins++;
        } else {
            wins = 1;
            challenger = static_cast<uint8_t>(best);
        }
        if (wins < 2) return alpha;
        wins = 0;
        sinceSwitch = 0;
        return kGainChoices[best];
    }
};

}  // namespace bbw
