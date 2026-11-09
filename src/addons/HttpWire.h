#pragma once
// An add-on's HTTP request on the wire, and the answer read back (Host::fetchStart). HTTP/1.0 with
// Connection: close, so the answer is never chunked and ends when the server closes. The board
// moves the bytes over a socket it has checked (platform/AddonNet.cpp), the emulator through curl.
//
// Portable; host-tested.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addons/Addon.h"

namespace bbw {
namespace addon {

struct UrlParts {
  bool tls = false;
  char host[64] = {};
  uint16_t port = 80;
  char path[128] = {};
};

inline bool splitUrl(const char *url, UrlParts &out) {
  out = UrlParts();
  const char *p;
  if (strncmp(url, "https://", 8) == 0) {
    out.tls = true;
    out.port = 443;
    p = url + 8;
  } else if (strncmp(url, "http://", 7) == 0) {
    p = url + 7;
  } else {
    return false;
  }
  size_t h = 0;
  while (*p && *p != '/' && *p != ':' && *p != '?') {
    if (h + 1 >= sizeof(out.host) || *p == '@') return false;
    out.host[h++] = *p++;
  }
  if (h == 0) return false;
  if (*p == ':') {
    const long port = strtol(p + 1, const_cast<char **>(&p), 10);
    if (port <= 0 || port > 65535) return false;
    out.port = static_cast<uint16_t>(port);
  }
  if (*p == '\0') {
    strcpy(out.path, "/");
  } else {
    if (*p != '/' && *p != '?') return false;
    if (strlen(p) >= sizeof(out.path)) return false;
    if (*p == '?') {
      out.path[0] = '/';
      strcpy(out.path + 1, p);
    } else {
      strcpy(out.path, p);
    }
  }
  for (const char *c = out.path; *c; ++c) {
    if (*c == '\r' || *c == '\n' || *c == ' ') return false;  // nothing smuggled into the request
  }
  return true;
}

// The request's head (and body). Returns its length, 0 if it does not fit.
inline size_t buildRequest(const FetchJob &job, const UrlParts &u, char *out, size_t size) {
  for (const char *c = job.cookie; *c; ++c) {
    if (*c == '\r' || *c == '\n') return 0;
  }
  const size_t bodyLen = job.post && job.body ? strlen(job.body) : 0;
  int n = snprintf(out, size,
                   "%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: brew-controller-addon\r\nAccept: */*\r\n"
                   "Connection: close\r\n",
                   job.post ? "POST" : "GET", u.path, u.host);
  if (n < 0 || static_cast<size_t>(n) >= size) return 0;
  size_t len = static_cast<size_t>(n);
  if (job.cookie[0]) {
    n = snprintf(out + len, size - len, "Cookie: %s\r\n", job.cookie);
    if (n < 0 || static_cast<size_t>(n) >= size - len) return 0;
    len += static_cast<size_t>(n);
  }
  if (job.post) {
    n = snprintf(out + len, size - len, "Content-Type: %s\r\nContent-Length: %u\r\n",
                 job.contentType ? job.contentType : "application/json", static_cast<unsigned>(bodyLen));
    if (n < 0 || static_cast<size_t>(n) >= size - len) return 0;
    len += static_cast<size_t>(n);
  }
  if (len + 2 + bodyLen >= size) return 0;
  memcpy(out + len, "\r\n", 2);
  len += 2;
  if (bodyLen) {
    memcpy(out + len, job.body, bodyLen);
    len += bodyLen;
  }
  out[len] = '\0';
  return len;
}

// Reads the answer as it arrives: status line, headers (the first Set-Cookie's name=value), then the
// body into the job's buffer.
class ResponseReader {
 public:
  explicit ResponseReader(FetchJob &job) : job_(job) {}
  void feed(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      if (inBody_) {
        if (job_.outLen < job_.outCap) job_.out[job_.outLen++] = data[i];
        else job_.truncated = true;
        continue;
      }
      const char c = static_cast<char>(data[i]);
      if (c == '\n') {
        line_[lineLen_] = '\0';
        if (lineLen_ && line_[lineLen_ - 1] == '\r') line_[--lineLen_] = '\0';
        headerLine();
        lineLen_ = 0;
      } else if (lineLen_ + 1 < sizeof(line_)) {
        line_[lineLen_++] = c;
      }
    }
  }
  bool headComplete() const { return inBody_; }
  long contentLength() const { return contentLength_; }

 private:
  void headerLine() {
    if (!sawStatus_) {
      sawStatus_ = true;
      const char *sp = strchr(line_, ' ');
      job_.status = (strncmp(line_, "HTTP/", 5) == 0 && sp) ? atoi(sp + 1) : -1;
      return;
    }
    if (lineLen_ == 0) {
      inBody_ = true;
      return;
    }
    if (strncasecmpPortable(line_, "set-cookie:", 11) == 0 && !job_.setCookie[0]) {
      const char *v = line_ + 11;
      while (*v == ' ') ++v;
      size_t n = 0;
      while (v[n] && v[n] != ';' && n + 1 < sizeof(job_.setCookie)) ++n;
      memcpy(job_.setCookie, v, n);
      job_.setCookie[n] = '\0';
    } else if (strncasecmpPortable(line_, "content-length:", 15) == 0) {
      contentLength_ = atol(line_ + 15);
    }
  }
  static int strncasecmpPortable(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      const int x = tolower(static_cast<unsigned char>(a[i])), y = tolower(static_cast<unsigned char>(b[i]));
      if (x != y || !x) return x - y;
    }
    return 0;
  }

  FetchJob &job_;
  char line_[256] = {};
  size_t lineLen_ = 0;
  bool sawStatus_ = false;
  bool inBody_ = false;
  long contentLength_ = -1;
};

}  // namespace addon
}  // namespace bbw
