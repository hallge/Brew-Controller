#pragma once
// A few numbers that describe the shape of a shot's flow, computed once from
// the weight curve when the shot finishes and stored with it. They exist so
// that later analysis (consistency, abnormal-flow flags, drift) has history
// to work on without keeping every curve.
//
// All of it is cup-side: flow here is how fast weight arrives on the scale,
// taken over about a second, not what happens at the puck.

#include <cmath>
#include <stddef.h>

namespace bbw {

struct FlowFeatures {
  bool valid = false;
  float flow25GS = 0.0f;  // flow when 25 % of the yield was in the cup
  float flow50GS = 0.0f;
  float flow75GS = 0.0f;
  // flow75 / flow25. Above 1: the shot sped up as it went (normal as the puck
  // erodes). A jump from one shot to the next is what is worth noticing.
  float acceleration = 0.0f;
  // RMS distance of the flow curve from a straight line through it, g/s.
  // Smooth shots are low; surging or stuttering flow is high.
  float roughnessGS = 0.0f;
};

namespace flowdetail {

constexpr float HALF_WINDOW_S = 0.6f;

// Least-squares slope of weight against time over samples within
// +-HALF_WINDOW_S of timeS[centre]. Returns false with too little data.
inline bool slopeAt(const float *timeS, const float *weightG, size_t count, size_t centre,
                    float &slope) {
  const float t0 = timeS[centre];
  size_t first = centre, last = centre;
  while (first > 0 && t0 - timeS[first - 1] <= HALF_WINDOW_S) --first;
  while (last + 1 < count && timeS[last + 1] - t0 <= HALF_WINDOW_S) ++last;
  const size_t n = last - first + 1;
  if (n < 4 || timeS[last] - timeS[first] < HALF_WINDOW_S) return false;
  double meanT = 0, meanW = 0;
  for (size_t i = first; i <= last; ++i) { meanT += timeS[i]; meanW += weightG[i]; }
  meanT /= n;
  meanW /= n;
  double cov = 0, var = 0;
  for (size_t i = first; i <= last; ++i) {
    const double dt = timeS[i] - meanT;
    cov += dt * (weightG[i] - meanW);
    var += dt * dt;
  }
  if (var < 1e-9) return false;
  slope = static_cast<float>(cov / var);
  return true;
}

}  // namespace flowdetail

// timeS/weightG: the shot curve. fromS..toS: first drops to the stop command.
// yieldG: weight when the stop was commanded.
inline FlowFeatures computeFlowFeatures(const float *timeS, const float *weightG, size_t count,
                                        float fromS, float toS, float yieldG) {
  FlowFeatures f;
  if (!timeS || !weightG || count < 12 || !(toS - fromS >= 3.0f) || !(yieldG >= 5.0f)) return f;

  // Flow at the three quarter points of the yield.
  float *const out[3] = {&f.flow25GS, &f.flow50GS, &f.flow75GS};
  const float share[3] = {0.25f, 0.50f, 0.75f};
  for (int q = 0; q < 3; ++q) {
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
      if (timeS[i] < fromS || timeS[i] > toS) continue;
      if (weightG[i] >= share[q] * yieldG) {
        found = flowdetail::slopeAt(timeS, weightG, count, i, *out[q]);
        break;
      }
    }
    if (!found) return f;
  }

  // Roughness: flow at every sample of the pour, against a straight line.
  double sumT = 0, sumF = 0, sumTT = 0, sumTF = 0;
  size_t n = 0;
  for (size_t i = 0; i < count; ++i) {
    if (timeS[i] < fromS + flowdetail::HALF_WINDOW_S || timeS[i] > toS - flowdetail::HALF_WINDOW_S) continue;
    float flow = 0;
    if (!flowdetail::slopeAt(timeS, weightG, count, i, flow)) continue;
    sumT += timeS[i];
    sumF += flow;
    sumTT += static_cast<double>(timeS[i]) * timeS[i];
    sumTF += static_cast<double>(timeS[i]) * flow;
    ++n;
  }
  if (n < 8) return f;
  const double denom = n * sumTT - sumT * sumT;
  if (fabs(denom) < 1e-9) return f;
  const double b = (n * sumTF - sumT * sumF) / denom;
  const double a = (sumF - b * sumT) / n;
  double sq = 0;
  for (size_t i = 0; i < count; ++i) {
    if (timeS[i] < fromS + flowdetail::HALF_WINDOW_S || timeS[i] > toS - flowdetail::HALF_WINDOW_S) continue;
    float flow = 0;
    if (!flowdetail::slopeAt(timeS, weightG, count, i, flow)) continue;
    const double r = flow - (a + b * timeS[i]);
    sq += r * r;
  }
  f.roughnessGS = static_cast<float>(sqrt(sq / n));
  f.acceleration = f.flow25GS > 0.05f ? f.flow75GS / f.flow25GS : 0.0f;
  f.valid = std::isfinite(f.roughnessGS) && std::isfinite(f.acceleration);
  return f;
}

}  // namespace bbw
