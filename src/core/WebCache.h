#pragma once
// Small helpers for serving the page and the shot list cheaply (11 Nov 2026).
//
// On the board a 21 KB shot list took about 6 s to send: it went out as about six small writes per
// shot, and each small write costs a round trip. ChunkBuffer gathers the text into one buffer and
// sends it in large pieces. The tags let a browser ask "still the same?" and get a 304 without the
// controller reading or sending anything: the page's tag is the firmware build, the shot list's is
// the history's revision (storage/Store.h historyRevision) under a number chosen at boot.
//
// Pure: no Arduino, so the host tests use it as it is.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace bbw {

// The shot list's tag: one boot, one revision of the history, one limit. false: `out` too small.
inline bool shotsEtag(uint32_t bootNonce, uint32_t revision, uint32_t limit, char *out, size_t size) {
  const int n = snprintf(out, size, "\"s-%08x-%u-%u\"", static_cast<unsigned>(bootNonce),
                         static_cast<unsigned>(revision), static_cast<unsigned>(limit));
  if (n < 0 || static_cast<size_t>(n) >= size) {
    if (size) out[0] = '\0';
    return false;
  }
  return true;
}

// Whether an If-None-Match header names `tag` (quoted, as sent in ETag): one of a comma-separated
// list, weak (W/) or not, or "*".
inline bool etagMatches(const char *header, const char *tag) {
  if (!header || !tag || !tag[0]) return false;
  const size_t tagLen = strlen(tag);
  const char *p = header;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == ',') ++p;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != ',') ++p;
    const char *end = p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
    if (end - start >= 2 && (start[0] == 'W' || start[0] == 'w') && start[1] == '/') start += 2;
    const size_t len = static_cast<size_t>(end - start);
    if (len == 1 && start[0] == '*') return true;
    if (len == tagLen && memcmp(start, tag, len) == 0) return true;
  }
  return false;
}

// Whether an Accept-Encoding header takes gzip ("gzip" or "*", not refused with q=0).
inline bool acceptsGzip(const char *header) {
  if (!header) return false;
  const char *p = header;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == ',') ++p;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != ',' && *p != ';' && *p != ' ' && *p != '\t') ++p;
    const size_t len = static_cast<size_t>(p - start);
    bool named = false;
    if (len == 1 && start[0] == '*') named = true;
    if (len == 4) {
      named = true;
      const char *g = "gzip";
      for (size_t i = 0; i < 4; ++i) {
        char c = start[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != g[i]) named = false;
      }
    }
    // Its parameters, up to the next comma: q=0 (0, 0.0, 0.00...) refuses it.
    bool refused = false;
    while (*p && *p != ',') {
      if (*p == 'q' && p[1] == '=') {
        const char *v = p + 2;
        bool zero = *v == '0';
        if (zero) {
          ++v;
          if (*v == '.') {
            ++v;
            while (*v == '0') ++v;
          }
          if (*v >= '1' && *v <= '9') zero = false;
        }
        refused = zero;
      }
      ++p;
    }
    if (named && !refused) return true;
  }
  return false;
}

// Gathers text and sends it in pieces of up to the buffer's size. Text larger than the buffer goes
// out on its own, after what was gathered.
class ChunkBuffer {
 public:
  using Send = void (*)(const char *data, size_t length, void *context);
  ChunkBuffer(char *buffer, size_t size, Send send, void *context)
      : buffer_(buffer), size_(size), send_(send), context_(context) {}
  ChunkBuffer(const ChunkBuffer &) = delete;
  ChunkBuffer &operator=(const ChunkBuffer &) = delete;

  void add(const char *text, size_t length) {
    if (length == 0) return;
    if (used_ + length > size_) flush();
    if (length > size_) {
      send_(text, length, context_);
      return;
    }
    memcpy(buffer_ + used_, text, length);
    used_ += length;
  }
  void add(const char *text) { add(text, strlen(text)); }
  void flush() {
    if (used_ == 0) return;
    send_(buffer_, used_, context_);
    used_ = 0;
  }

 private:
  char *buffer_;
  size_t size_;
  size_t used_ = 0;
  Send send_;
  void *context_;
};

}  // namespace bbw
