#pragma once
// The controller's connection to the shared library: a GitHub repository,
// reached over HTTPS with a token that is good for that repository alone.
//
//   put   a signed record goes to  inbox/<path>  on the repository's `inbox`
//         branch. Nothing there is trusted; the library's own Intake checks
//         it (signature, types, ranges) before anything is filed.
//   get   status/ and index/ are read from `main`.
//
// On the board this is LibraryLink.cpp (WiFiClientSecure, GitHub's contents
// API). The PC emulator has no TLS: emu/src/emu_devicekey.cpp stands in for
// it with a folder beside the emulator's data, laid out as the repository is.
//
// Every call blocks until GitHub has answered or the time-out has passed, a
// second or two when all is well. They are only made from the loop task while
// the controller is idle; the control task, which stops shots, never waits on
// them.

#include <stddef.h>
#include <stdint.h>

namespace bbw {

struct LibraryLinkStatus {
  bool configured = false;   // a repository and a token have been given
  bool ready = false;        // ...and the network, the clock and GitHub are all there
  int lastHttp = 0;          // the last answer's status code; below 0: no answer
  uint32_t lastOkEpoch = 0;  // when a request last succeeded, 0 = never
  char repository[64] = {};
  char problem[64] = {};     // in words, empty when there is none
  char via[80] = {};         // how records travel, in words
  // false when what is "sent" does not reach the library at all (the
  // emulator writing to its folder).
  bool toLibrary = true;
};

// repo: "owner/name". token: a fine-grained token with Contents read and write
// on that repository only. Either may be empty: nothing is sent or read then.
void libraryLinkConfigure(const char *repo, const char *token);

// False while there is no point in trying: not set up, no Wi-Fi, clock not
// set (certificates cannot be checked), or backing off after a failure.
bool libraryLinkReady();

// true once GitHub has the record (or had it already).
bool libraryPut(const char *path, const char *text);

// Closes the connection kept open between requests (one TLS connection at a time: a subscription
// card's page fetch lets this one go first). The next request opens it again.
void libraryLinkRelease();

// Reads a file of the library into `out`, terminated. false if it is not
// there, does not fit, or the library could not be reached.
bool libraryGet(const char *path, char *out, size_t size);

LibraryLinkStatus libraryLinkStatus();

}  // namespace bbw
