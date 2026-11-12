#pragma once
// How long each pass of the Arduino loop takes, and which of its jobs took the time (11 Nov 2026).
//
// The web server answers one connection per pass of the loop, so a slow pass makes every request
// slow: on the board even answers from RAM took 1-2.5 s and connections waited up to 15 s, with no
// figure saying what filled a pass. The loop marks each job with a LoopSpan; a pass ends with
// loopPassEnd. What /api/diagnostics shows is the last full minute (passes, average, slowest pass
// and the job that took most of it, the slowest run of each job) and the slowest pass since boot.
//
// Pure: times come in as arguments, so the host tests drive it. Loop task only.

#include <stddef.h>
#include <stdint.h>

namespace bbw {

// The loop's jobs. The names match platform/RamWatch.h where the job is the same.
enum LoopJob : uint8_t {
  LOOP_UI = 0,
  LOOP_WEB,
  LOOP_ADDON,
  LOOP_SHOT_SAVE,  // the shot just made, and the files written after it
  LOOP_LOG,
  LOOP_UPDATE,
  LOOP_SHARE,
  LOOP_DIAG,
  LOOP_SUBS,
  LOOP_HA,
  LOOP_OTHER,      // whatever the marked jobs leave of the pass
  LOOP_JOB_COUNT
};

constexpr const char *LOOP_JOB_NAMES[LOOP_JOB_COUNT] = {"ui",   "web",   "addon",   "shot save",
                                                        "log",  "update", "sharing", "diag",
                                                        "subs", "ha",    "other"};

struct LoopWindow {
  uint32_t passes = 0;
  uint64_t totalUs = 0;
  uint32_t maxPassUs = 0;
  uint8_t maxPassJob = LOOP_OTHER;  // the job that took most of the slowest pass
  uint32_t maxPassJobUs = 0;
  uint32_t jobMaxUs[LOOP_JOB_COUNT] = {};
};

struct LoopTiming {
  static constexpr uint32_t WINDOW_MS = 60000;

  LoopWindow current;
  LoopWindow last;       // the last full minute; passes == 0 until one has gone by
  uint32_t windowStartMs = 0;
  bool started = false;
  // This pass so far.
  uint32_t passStartUs = 0;
  uint32_t passJobUs[LOOP_JOB_COUNT] = {};
  // Since boot.
  uint32_t bootMaxPassUs = 0;
  uint8_t bootMaxJob = LOOP_OTHER;
  uint32_t bootMaxJobUs = 0;
  uint32_t bootMaxAtS = 0;
};

inline void loopPassBegin(LoopTiming &t, uint32_t nowUs, uint32_t nowMs) {
  if (!t.started) {
    t.started = true;
    t.windowStartMs = nowMs;
  }
  t.passStartUs = nowUs;
  for (uint32_t &us : t.passJobUs) us = 0;
}

inline void loopSpanAdd(LoopTiming &t, LoopJob job, uint32_t us) {
  if (job >= LOOP_JOB_COUNT) return;
  t.passJobUs[job] += us;
}

// Ends the pass begun last. Returns the pass's length in microseconds.
inline uint32_t loopPassEnd(LoopTiming &t, uint32_t nowUs, uint32_t nowMs) {
  const uint32_t passUs = nowUs - t.passStartUs;
  uint32_t marked = 0;
  for (size_t j = 0; j < LOOP_OTHER; ++j) marked += t.passJobUs[j];
  t.passJobUs[LOOP_OTHER] += passUs > marked ? passUs - marked : 0;
  uint8_t top = LOOP_OTHER;
  for (size_t j = 0; j < LOOP_JOB_COUNT; ++j) {
    if (t.passJobUs[j] > t.passJobUs[top]) top = static_cast<uint8_t>(j);
  }
  LoopWindow &w = t.current;
  ++w.passes;
  w.totalUs += passUs;
  for (size_t j = 0; j < LOOP_JOB_COUNT; ++j) {
    if (t.passJobUs[j] > w.jobMaxUs[j]) w.jobMaxUs[j] = t.passJobUs[j];
  }
  if (passUs > w.maxPassUs) {
    w.maxPassUs = passUs;
    w.maxPassJob = top;
    w.maxPassJobUs = t.passJobUs[top];
  }
  if (passUs > t.bootMaxPassUs) {
    t.bootMaxPassUs = passUs;
    t.bootMaxJob = top;
    t.bootMaxJobUs = t.passJobUs[top];
    t.bootMaxAtS = nowMs / 1000u;
  }
  if (nowMs - t.windowStartMs >= LoopTiming::WINDOW_MS) {
    t.last = t.current;
    t.current = LoopWindow();
    t.windowStartMs = nowMs;
  }
  return passUs;
}

// The window to report: the last full minute, or the one under way before a minute has gone by.
inline const LoopWindow &loopReportWindow(const LoopTiming &t) {
  return t.last.passes ? t.last : t.current;
}

// Marks one job of the loop for as long as it is in scope. `clock` gives microseconds.
template <uint32_t (*clock)()>
class LoopSpanT {
 public:
  LoopSpanT(LoopTiming &t, LoopJob job) : t_(t), job_(job), startUs_(clock()) {}
  ~LoopSpanT() { loopSpanAdd(t_, job_, clock() - startUs_); }
  LoopSpanT(const LoopSpanT &) = delete;
  LoopSpanT &operator=(const LoopSpanT &) = delete;

 private:
  LoopTiming &t_;
  LoopJob job_;
  uint32_t startUs_;
};

}  // namespace bbw
