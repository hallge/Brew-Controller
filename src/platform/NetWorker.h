#pragma once
// HTTPS requests and signing on the network task's stack, not the caller's.
//
// A TLS handshake checked against the certificate bundle, and an ECDSA signature, each take several
// kilobytes of stack at their deepest. Done on the loop task they left it 3 KB of its 16 KB (measured
// on the board, v0.1.20). The update task (FirmwareUpdate.cpp) has a 12 KB stack of its own and is
// idle almost always, so it runs them: netRun() hands it the job and waits, as long as the job would
// have taken where it was called. The caller's code and its flow do not change.
//
// When the update task is busy (checking for or installing a firmware), when it does not exist yet,
// when called from that task itself, and in the PC emulator, the job runs where it is called, as
// before.

namespace bbw {

// Runs fn(context) on the network task and returns when it has finished.
void netRun(void (*fn)(void *), void *context);

}  // namespace bbw
