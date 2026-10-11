#pragma once
// Brew-by-weight maths adapted from Open Brew by Weight
// (Advanced-Shot-Stopper src/ShotStopperBbwEwma.h, AGPL-3.0-or-later):
//   * predictStopTimeS  == bbwEwma::predict  (centred least squares, 10 samples)
//   * learnStopOffset   == bbwEwma::learn    (EWMA between shots; alpha = 1.0
//                          reproduces the original shotStopper full correction)
//   * AlphaEvidence     == bbwEwma::Evidence (adaptive learning-factor selector)

#include <cmath>
#include <stddef.h>

#include "Types.h"

namespace bbw {

constexpr size_t TREND_POINT_COUNT = 10;
constexpr float TREND_MIN_LAST_SAMPLE_G = 10.0f;
constexpr float TREND_MIN_HORIZON_S = 0.25f;

inline float clampOffset(float value) {
  return fmaxf(0.0f, fminf(MAX_OFFSET_G, value));
}

// Time (seconds since shot start) at which the weight trend reaches targetG.
// Returns fallbackS when no trustworthy prediction exists.
inline float predictStopTimeS(const float *timeS, const float *weightG,
                              size_t count, float targetG, float fallbackS) {
  if (!timeS || !weightG || count < TREND_POINT_COUNT ||
      !std::isfinite(targetG) || !std::isfinite(fallbackS) ||
      weightG[count - 1] < TREND_MIN_LAST_SAMPLE_G) {
    return fallbackS;
  }
  const size_t first = count - TREND_POINT_COUNT;
  const float reference = timeS[count - 1];
  float meanX = 0.0f, meanY = 0.0f;
  for (size_t i = first; i < count; ++i) {
    if (!std::isfinite(timeS[i]) || !std::isfinite(weightG[i]) ||
        (i > first && timeS[i] <= timeS[i - 1])) {
      return fallbackS;
    }
    meanX += timeS[i] - reference;
    meanY += weightG[i];
  }
  meanX /= TREND_POINT_COUNT;
  meanY /= TREND_POINT_COUNT;
  float covariance = 0.0f, variance = 0.0f;
  for (size_t i = first; i < count; ++i) {
    const float dx = (timeS[i] - reference) - meanX;
    covariance += dx * (weightG[i] - meanY);
    variance += dx * dx;
  }
  if (!std::isfinite(variance) || variance < 0.000001f / TREND_POINT_COUNT) {
    return fallbackS;
  }
  const float slope = covariance / variance;
  if (!std::isfinite(slope) || slope <= 0.0f) return fallbackS;
  const float predicted = reference + meanX + (targetG - meanY) / slope;
  return std::isfinite(predicted) && predicted >= reference + TREND_MIN_HORIZON_S
             ? predicted
             : fallbackS;
}

// offset' = clamp(offset + alpha * (final - target), 0, MAX_OFFSET_G).
// Rejected (returns false, offset untouched) when the shot implies a
// compensation outside 0..MAX_OFFSET_G: that is a brewing error, not drip.
inline bool learnStopOffset(float offsetG, float finalWeightG, float targetG,
                            float alpha, float &updatedOffsetG) {
  const float observation = offsetG + (finalWeightG - targetG);
  if (!(alpha >= 0.01f && alpha <= 1.0f) || !std::isfinite(offsetG) ||
      !std::isfinite(observation) || fabsf(observation) > MAX_OFFSET_G) {
    return false;
  }
  updatedOffsetG = clampOffset(offsetG + alpha * (finalWeightG - targetG));
  return true;
}

constexpr uint8_t ALPHA_CANDIDATES[] = {10, 30, 50, 100};  // hundredths

inline bool validAlpha(uint8_t alpha) { return alpha >= 1 && alpha <= 100; }

// Open Brew's adaptive learning-factor selector, unchanged in behaviour.
// It keeps the last 20 compensation observations (offset + final - target) and
// replays them through four candidate gains plus the gain it started with.
// Every fifth observation, once 20 are held, a candidate that beats the
// current gain by at least 10 % squared error in two evaluations running, and
// at least ten observations after the previous switch, becomes the new gain.
// The earliest possible switch is therefore observation 25. Selection changes
// future learning speed only; it never jumps the offset itself.
struct AlphaEvidence {
  static constexpr uint8_t WINDOW = 20;
  float anchors[5] = {};
  float observations[WINDOW] = {};
  uint8_t initialAlpha = 0;  // seeded by the first eligible observation
  uint8_t count = 0;
  uint8_t position = 0;
  uint8_t cadence = 0;
  uint8_t sinceSwitch = 0;
  uint8_t challenger = 0;
  uint8_t wins = 0;

  // Returns the learning factor to use from now on.
  uint8_t observe(float observation, float seed, uint8_t alpha) {
    if (!std::isfinite(observation) || fabsf(observation) > MAX_OFFSET_G ||
        !std::isfinite(seed) || !validAlpha(alpha)) return alpha;
    if (count == 0) {
      for (float &anchor : anchors) anchor = seed;
      initialAlpha = alpha;
    }
    if (count == WINDOW) {
      for (uint8_t i = 0; i < 5; ++i) {
        const uint8_t gain = i < 4 ? ALPHA_CANDIDATES[i] : initialAlpha;
        anchors[i] = clampOffset(anchors[i] +
            (gain / 100.0f) * (observations[position] - anchors[i]));
      }
    }
    observations[position] = observation;
    position = (position + 1) % WINDOW;
    if (count < WINDOW) ++count;
    if (sinceSwitch < 10) ++sinceSwitch;
    cadence = (cadence + 1) % 5;
    if (count < WINDOW || cadence != 0) return alpha;
    float scores[5] = {};
    uint8_t incumbent = 4;
    for (uint8_t i = 0; i < 5; ++i) {
      const uint8_t gain = i < 4 ? ALPHA_CANDIDATES[i] : initialAlpha;
      float prediction = anchors[i];
      for (uint8_t j = 0; j < WINDOW; ++j) {
        const float error = observations[(position + j) % WINDOW] - prediction;
        scores[i] += error * error;  // score before updating
        prediction = clampOffset(prediction + (gain / 100.0f) * error);
      }
      if (!std::isfinite(scores[i])) { wins = 0; return alpha; }
      if (i < 4 && gain == alpha) incumbent = i;
    }
    uint8_t best = incumbent;
    bool tied = false;
    for (uint8_t i = 0; i < 4; ++i) {
      if (scores[i] < scores[best]) { best = i; tied = false; }
      else if (i != best && scores[i] == scores[best]) tied = true;
    }
    if (tied || best == incumbent || sinceSwitch < 10 ||
        !(scores[best] <= scores[incumbent] * 0.90f)) {
      wins = 0;
      return alpha;
    }
    wins = challenger == best ? wins + 1 : 1;
    challenger = best;
    if (wins < 2) return alpha;
    wins = 0;
    sinceSwitch = 0;
    return ALPHA_CANDIDATES[best];
  }
};

// Flow rate from real sample timestamps: least-squares slope over the samples
// received in the trailing window. No sample-rate assumption anywhere.
class FlowEstimator {
 public:
  static constexpr size_t CAPACITY = 32;
  static constexpr uint32_t WINDOW_MS = 1000;
  static constexpr uint32_t MIN_SPAN_MS = 200;

  void reset() { count_ = 0; head_ = 0; flow_ = 0.0f; }

  void add(float weightG, uint32_t atMs) {
    t_[head_] = atMs;
    w_[head_] = weightG;
    head_ = (head_ + 1) % CAPACITY;
    if (count_ < CAPACITY) ++count_;
    compute(atMs);
  }

  float flowGS() const { return flow_; }

 private:
  void compute(uint32_t nowMs) {
    // Walk back from the newest sample while inside the window.
    size_t n = 0;
    double sumX = 0, sumY = 0;
    uint32_t oldest = nowMs;
    for (size_t i = 0; i < count_; ++i) {
      const size_t idx = (head_ + CAPACITY - 1 - i) % CAPACITY;
      if (static_cast<uint32_t>(nowMs - t_[idx]) > WINDOW_MS) break;
      sumX += -static_cast<double>(nowMs - t_[idx]) / 1000.0;
      sumY += w_[idx];
      oldest = t_[idx];
      ++n;
    }
    if (n < 3 || static_cast<uint32_t>(nowMs - oldest) < MIN_SPAN_MS) {
      flow_ = 0.0f;
      return;
    }
    const double meanX = sumX / n, meanY = sumY / n;
    double cov = 0, var = 0;
    for (size_t i = 0; i < n; ++i) {
      const size_t idx = (head_ + CAPACITY - 1 - i) % CAPACITY;
      const double dx = -static_cast<double>(nowMs - t_[idx]) / 1000.0 - meanX;
      cov += dx * (w_[idx] - meanY);
      var += dx * dx;
    }
    flow_ = var > 1e-9 ? static_cast<float>(cov / var) : 0.0f;
  }

  uint32_t t_[CAPACITY] = {};
  float w_[CAPACITY] = {};
  size_t count_ = 0;
  size_t head_ = 0;
  float flow_ = 0.0f;
};

}  // namespace bbw
