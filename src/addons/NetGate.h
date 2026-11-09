#pragma once
// The rule between an add-on's network job and every other one (platform/NetWorker.h), portable so
// the host tests check it and the emulator uses it. The board's FirmwareUpdate.cpp keeps the same
// rule with its own task and semaphores.
//
// - submit(): an add-on's job is handed to a runner (the network task) and not waited for. One at
//   a time: false while another is running.
// - run(): any other job. While an add-on's job is running it is not run at all (false): the caller
//   tries again on a later turn. It is never run where it is called instead, because the loop task
//   has only about 2.6 kB of stack to spare, and these jobs include TLS handshakes.

#include <atomic>

namespace bbw {

class NetGate {
 public:
  using Job = void (*)(void *);
  // How a submitted job is started: on the board the network task, in the emulator a thread, in
  // the tests by hand. It must call finished() when the job has run.
  using Runner = void (*)(NetGate &gate, Job fn, void *context);

  explicit NetGate(Runner runner) : runner_(runner) {}

  bool submit(Job fn, void *context) {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) return false;
    runner_(*this, fn, context);
    return true;
  }
  // false: not run (an add-on's job holds the network).
  bool run(Job fn, void *context) {
    if (busy_.load()) return false;
    fn(context);
    return true;
  }
  void finished() { busy_.store(false); }
  bool busy() const { return busy_.load(); }

 private:
  Runner runner_;
  std::atomic<bool> busy_{false};
};

}  // namespace bbw
