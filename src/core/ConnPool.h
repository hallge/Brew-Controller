#pragma once
// A small pool of accepted connections that have not sent their request yet (12 Nov 2026).
//
// The stock Arduino WebServer accepts one connection and then waits up to 5 s for its request before
// it serves anyone else. Browsers open spare connections that send nothing, so one of those made the
// next real request wait about 5 s, and a few of them filled the listen backlog (the next connect
// timed out and retried after 1, 3, 7, 15 s). The pool accepts every waiting connection and serves
// whichever one has its request in, first come first served. Idle ones are dropped after a limit.
//
// `C` is the client type (WiFiClient on the board, a fake in the host tests): copyable, default
// constructible, with `bool connected()` and `int available()`.
// Pure: no Arduino.

#include <stddef.h>
#include <stdint.h>

namespace bbw {

template <class C, size_t N>
class ConnPool {
 public:
  size_t size() const { return n_; }
  bool full() const { return n_ >= N; }

  // Takes a newly accepted connection. false: the pool is full (leave it in the backlog).
  bool add(const C &c, uint32_t nowMs) {
    if (n_ >= N) return false;
    slot_[n_].c = c;
    slot_[n_].sinceMs = nowMs;
    ++n_;
    return true;
  }

  // Drops the connections that went away or sat idle longer than maxIdleMs, then hands out the
  // oldest one whose request has begun to arrive (removed from the pool). false: none is ready.
  bool takeReady(uint32_t nowMs, uint32_t maxIdleMs, C &out) {
    size_t i = 0;
    while (i < n_) {
      C &c = slot_[i].c;
      if (c.available() > 0) {   // a request in: serve it even if the peer has since closed
        out = c;
        removeAt(i);
        return true;
      }
      if (!c.connected() || (uint32_t)(nowMs - slot_[i].sinceMs) > maxIdleMs) {
        removeAt(i);   // the next one moved into slot i
        continue;
      }
      ++i;
    }
    return false;
  }

 private:
  struct Slot {
    C c;
    uint32_t sinceMs = 0;
  };
  void removeAt(size_t i) {
    for (size_t j = i + 1; j < n_; ++j) slot_[j - 1] = slot_[j];
    slot_[n_ - 1].c = C();
    --n_;
  }
  Slot slot_[N];
  size_t n_ = 0;
};

}  // namespace bbw
