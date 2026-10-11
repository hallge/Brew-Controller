#pragma once
// The full data set of one shot: every reading the scale sent from the start
// command until the weight settled, as it is kept on flash, plus what can be
// worked out from it afterwards. Pure C++ so the host tests cover it.
//
// One point is 8 bytes:
//   time since the start command   0.01 s   (local receive time of the frame)
//   weight                         0.01 g
//   the scale's own timer          0.1 s    (0xFFFF: the frame carried none)
//   the scale's weight flag byte   raw
// A 40 s shot at the Lunar's 10 readings a second, with its drip time, is
// about 500 points: 4 kB.

#include <cmath>
#include <stddef.h>
#include <stdint.h>

#include "FlowFeatures.h"

namespace bbw {

constexpr uint16_t CURVE_NO_TIMER = 0xFFFF;

struct CurvePoint {
  uint16_t timeCs = 0;
  int16_t weightCg = 0;
  uint16_t timerDs = CURVE_NO_TIMER;
  uint8_t flags = 0;
  uint8_t reserved = 0;
};
static_assert(sizeof(CurvePoint) == 8, "CurvePoint is an on-flash layout");

inline uint16_t packTimerDs(int32_t timerMs) {
  if (timerMs < 0) return CURVE_NO_TIMER;
  const int32_t ds = (timerMs + 50) / 100;
  return static_cast<uint16_t>(ds > 0xFFFE ? 0xFFFE : ds);
}

inline CurvePoint packPoint(float timeS, float weightG, uint16_t timerDs, uint8_t flags) {
  CurvePoint p;
  const float cs = roundf(timeS * 100.0f);
  p.timeCs = static_cast<uint16_t>(cs < 0.0f ? 0.0f : (cs > 65535.0f ? 65535.0f : cs));
  const float cg = roundf(weightG * 100.0f);
  p.weightCg = static_cast<int16_t>(cg < -32768.0f ? -32768.0f : (cg > 32767.0f ? 32767.0f : cg));
  p.timerDs = timerDs;
  p.flags = flags;
  return p;
}

inline float pointTimeS(const CurvePoint &p) { return p.timeCs / 100.0f; }
inline float pointWeightG(const CurvePoint &p) { return p.weightCg / 100.0f; }
inline bool pointHasTimer(const CurvePoint &p) { return p.timerDs != CURVE_NO_TIMER; }
inline float pointTimerS(const CurvePoint &p) { return p.timerDs / 10.0f; }

// How good the data itself is: says whether the numbers derived from it can
// be trusted, and whether the scale and this controller agree about time.
struct ShotDataStats {
  size_t samples = 0;
  float durationS = 0.0f;       // first to last reading
  float meanPeriodMs = 0.0f;    // average spacing of the readings
  float maxGapMs = 0.0f;        // longest wait for a reading
  size_t gapsOver500Ms = 0;
  size_t timerSamples = 0;      // readings that carried the scale's timer
  // Over the stretch where the scale's timer was counting:
  float timerSpanS = 0.0f;      // what the scale's timer counted
  float clockSpanS = 0.0f;      // what this controller's clock counted
  float timerLagS = 0.0f;       // shot time minus scale timer, average
};

inline ShotDataStats shotDataStats(const CurvePoint *points, size_t count) {
  ShotDataStats s;
  s.samples = count;
  if (!points || count < 2) return s;
  s.durationS = pointTimeS(points[count - 1]) - pointTimeS(points[0]);
  s.meanPeriodMs = s.durationS * 1000.0f / static_cast<float>(count - 1);
  for (size_t i = 1; i < count; ++i) {
    const float gap = (pointTimeS(points[i]) - pointTimeS(points[i - 1])) * 1000.0f;
    if (gap > s.maxGapMs) s.maxGapMs = gap;
    if (gap > 500.0f) ++s.gapsOver500Ms;
  }
  // The scale's timer, over the stretch where it was counting: from the
  // reading before its first advance to the last reading where it advanced
  // (it stands still once the shot is stopped). A step backwards is the reset
  // at the start of the shot arriving; counting begins again after it.
  size_t from = count, lastMoving = count, previous = count;
  double lag = 0.0;
  size_t lagCount = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!pointHasTimer(points[i])) continue;
    ++s.timerSamples;
    if (previous != count) {
      if (points[i].timerDs < points[previous].timerDs) {
        from = lastMoving = count;
        lag = 0.0;
        lagCount = 0;
      } else if (points[i].timerDs > points[previous].timerDs) {
        if (from == count) from = previous;
        lastMoving = i;
        lag += pointTimeS(points[i]) - pointTimerS(points[i]);
        ++lagCount;
      }
    }
    previous = i;
  }
  if (from != count && lastMoving != count) {
    s.timerSpanS = pointTimerS(points[lastMoving]) - pointTimerS(points[from]);
    s.clockSpanS = pointTimeS(points[lastMoving]) - pointTimeS(points[from]);
    s.timerLagS = static_cast<float>(lag / lagCount);
  }
  return s;
}

// Unpacks into the float arrays the flow code works on. Returns the count.
inline size_t unpackCurve(const CurvePoint *points, size_t count, float *timeS, float *weightG,
                          size_t max) {
  const size_t n = count < max ? count : max;
  for (size_t i = 0; i < n; ++i) {
    timeS[i] = pointTimeS(points[i]);
    weightG[i] = pointWeightG(points[i]);
  }
  return n;
}

// Flow at one reading, g/s: slope of the weight over about a second centred
// on it (one-sided at the two ends). NAN with too little data around it.
inline float curveFlowAt(const float *timeS, const float *weightG, size_t count, size_t index) {
  float slope = 0.0f;
  if (!flowdetail::slopeAt(timeS, weightG, count, index, slope)) return NAN;
  return slope;
}

}  // namespace bbw
