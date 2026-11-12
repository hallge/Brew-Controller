#pragma once
// One background library request at a time, shared by the loop's jobs (12 Nov 2026). Pure: no
// Arduino, so the host tests drive it with a fake link.
//
// A job calls step() each turn with its own id and the request it wants (a path). The first call
// starts it and answers PENDING; later calls answer PENDING while it runs, then OK or FAIL once.
// Another job's request in flight: PENDING, without touching it, so each job only ever reads the
// answer to its own request. Not started (the network is busy): PENDING, started again next turn.
// The caller does nothing else while PENDING, and goes on with the answer, as it did when the
// request was waited for.

#include <stdint.h>
#include <string.h>

namespace bbw {

enum class Fetch : uint8_t { PENDING, OK, FAIL };

// What the link's poll answers (platform/LibraryLink.h LibraryJob), without its header.
enum class FetchState : uint8_t { IDLE, RUNNING, OK, FAILED };

class LibraryFetcher {
 public:
  using BeginFn = bool (*)(const char *key, void *context);   // start the request; false: not started
  using PollFn = FetchState (*)();

  explicit LibraryFetcher(PollFn poll) : poll_(poll) {}

  Fetch step(uint8_t owner, const char *key, BeginFn begin, void *context) {
    if (owner_ != 0 && owner_ != owner) return Fetch::PENDING;   // another job's request is under way
    if (owner_ == 0) {
      if (!begin(key, context)) return Fetch::PENDING;
      owner_ = owner;
      strncpy(key_, key, sizeof(key_) - 1);
      key_[sizeof(key_) - 1] = '\0';
      return Fetch::PENDING;
    }
    const FetchState s = poll_();
    if (s == FetchState::RUNNING) return Fetch::PENDING;
    owner_ = 0;
    // The answer to an earlier request of this job's (it has moved on): dropped, the new one starts next turn.
    if (strcmp(key_, key) != 0) return Fetch::PENDING;
    return s == FetchState::OK ? Fetch::OK : Fetch::FAIL;
  }

  // A request of this job's is under way (or of any job's, with owner 0).
  bool busy(uint8_t owner = 0) const { return owner == 0 ? owner_ != 0 : owner_ == owner; }

 private:
  PollFn poll_;
  uint8_t owner_ = 0;
  char key_[160] = {};
};

}  // namespace bbw
