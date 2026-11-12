#pragma once
// The rule between the network jobs (platform/NetWorker.h), portable so the host tests check it and
// the emulator uses it. The board's FirmwareUpdate.cpp keeps the same rule with its own task and
// semaphores.
//
// - submit(): an add-on's job is handed to a runner (the network task) and not waited for. One at
//   a time: false while another add-on job or a background job is running.
// - begin(): a background job of the loop's own (a library download, a share sent), handed over
//   and not waited for, so the loop keeps serving the web page and the grinder meanwhile. false
//   while an add-on job or another background job is running: the loop tries again later.
// - run(): any other job, run where it is called. While an add-on's job is running it is not run at
//   all (false): the caller tries again on a later turn. While a background job is running, a caller
//   that may wait (a web request) waits for it to finish first; any other caller is refused (false)
//   and tries again later, as after any network failure. It is never run where it is called while
//   another job holds the network: the loop task has only about 2.6 kB of stack to spare, and these
//   jobs include TLS handshakes.

#include <atomic>

namespace bbw {

class NetGate {
 public:
  using Job = void (*)(void *);
  // How a handed-over job is started: on the board the network task, in the emulator a thread, in
  // the tests by hand. It must call finished() when the job has run.
  using Runner = void (*)(NetGate &gate, Job fn, void *context);
  // How a caller that may wait waits for a background job: until backgroundBusy() is false or it
  // gives up (false). Without one, a caller is never kept waiting (refused instead).
  using Waiter = bool (*)(NetGate &gate);

  explicit NetGate(Runner runner, Waiter waiter = nullptr) : runner_(runner), waiter_(waiter) {}

  bool submit(Job fn, void *context) { return handOver(Kind::ADDON, fn, context); }
  bool begin(Job fn, void *context) { return handOver(Kind::BACKGROUND, fn, context); }

  // false: not run (another job holds the network).
  bool run(Job fn, void *context, bool mayWait) {
    const Kind k = kind_.load();
    if (k == Kind::ADDON) return false;
    if (k == Kind::BACKGROUND) {
      if (!mayWait || !waiter_ || !waiter_(*this) || kind_.load() != Kind::NONE) return false;
    }
    fn(context);
    return true;
  }
  void finished() { kind_.store(Kind::NONE); }
  bool busy() const { return kind_.load() == Kind::ADDON; }             // an add-on's job
  bool backgroundBusy() const { return kind_.load() == Kind::BACKGROUND; }

 private:
  enum class Kind { NONE, ADDON, BACKGROUND };
  bool handOver(Kind kind, Job fn, void *context) {
    Kind expected = Kind::NONE;
    if (!kind_.compare_exchange_strong(expected, kind)) return false;
    runner_(*this, fn, context);
    return true;
  }
  Runner runner_;
  Waiter waiter_;
  std::atomic<Kind> kind_{Kind::NONE};
};

}  // namespace bbw
