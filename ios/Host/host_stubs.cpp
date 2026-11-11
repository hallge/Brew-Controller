// What the Windows-only emulator files provide to the firmware, for the iPhone build: Home Assistant
// REST, diagnostics upload, page preview and the add-on network fetch, with the board's behaviour
// (src/platform/HomeAssistantLink.cpp, TelemetryLink.cpp, PagePreview.cpp, AddonNet.cpp).
//
// Everything that leaves the process goes through hostnet::httpRequest (net_portable.cpp), whose
// transport is URLSession on iOS (ios_net.mm). The library link, the device key and MQTT are in
// net_portable.cpp. See NET.md.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "BrewHost.h"
#include "addons/Addon.h"
#include "addons/HttpWire.h"
#include "core/HomeAssistant.h"
#include "net_portable.h"
#include "platform/HomeAssistantLink.h"
#include "platform/PagePreview.h"
#include "platform/TelemetryLink.h"

namespace {

void say(char *problem, size_t size, const char *text) {
  if (problem && size) {
    strncpy(problem, text, size - 1);
    problem[size - 1] = '\0';
  }
}

bool hasProblem(const char *problem) { return problem && problem[0]; }

}  // namespace

// ----------------------------------------------------------------- the C seam (BrewHost.h) --
int hostHttp(const char *method, const char *url, const char *headers, const void *body, size_t bodyLen,
             char *response, size_t responseCap, size_t *responseLen, int timeoutMs, char *problem,
             size_t problemSize) {
  bbw::hostnet::HttpRequest r;
  r.method = method ? method : "GET";
  r.url = url;
  r.headers = headers;
  r.body = body;
  r.bodyLen = bodyLen;
  r.timeoutMs = timeoutMs;
  bbw::hostnet::HttpResult res;
  const int code = bbw::hostnet::httpRequest(r, response, responseCap, res, problem, problemSize);
  if (responseLen) *responseLen = res.length;
  return code;
}

namespace bbw {

// ---------------------------------------------------------------------- Home Assistant --
int haRequest(const char *url, const char *token, const char *body, char *out, size_t outSize,
              char *problem, size_t problemSize, uint16_t timeoutMs) {
  say(problem, problemSize, "");
  if (out && outSize) out[0] = '\0';
  if (!url || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)) {
    say(problem, problemSize, "the address is not an http:// or https:// link");
    return -2;
  }
  std::string headers;
  if (token && token[0]) headers += std::string("Authorization: Bearer ") + token + "\r\n";
  if (body) headers += "Content-Type: application/json\r\n";
  hostnet::HttpRequest r;
  r.method = body ? "POST" : "GET";
  r.url = url;
  r.headers = headers.c_str();
  r.body = body;
  r.bodyLen = body ? strlen(body) : 0;
  r.timeoutMs = timeoutMs;
  hostnet::HttpResult res;
  char why[64] = {};
  const int code = hostnet::httpRequest(r, out, outSize, res, why, sizeof(why));
  if (code < 0) {
    say(problem, problemSize, why[0] ? why : "Home Assistant could not be reached");
  } else if (code == 401) say(problem, problemSize, "Home Assistant refused the token");
  else if (code == 404) say(problem, problemSize, "Home Assistant has no such entity or webhook");
  else if (code >= 400) say(problem, problemSize, "Home Assistant answered with an error");
  return code;
}

// ------------------------------------------------------------------------- diagnostics --
int diagPost(const char *url, const char *envelope, char *problem, size_t problemSize) {
  say(problem, problemSize, "");
  if (!url || strncmp(url, "https://", 8) != 0) {
    say(problem, problemSize, "the relay's address is not an https link");
    return -2;
  }
  hostnet::HttpRequest r;
  r.method = "POST";
  r.url = url;
  r.headers = "Content-Type: application/json\r\nUser-Agent: brew-by-weight-controller\r\n";
  r.body = envelope;
  r.bodyLen = envelope ? strlen(envelope) : 0;
  r.timeoutMs = 16000;
  r.followRedirects = false;
  char answer[512];
  hostnet::HttpResult res;
  char why[96] = {};
  const int code = hostnet::httpRequest(r, answer, sizeof(answer), res, why, sizeof(why));
  if (code < 0) {
    char text[160];
    snprintf(text, sizeof(text), "the relay could not be reached%s%s", why[0] ? ": " : "", why);
    say(problem, problemSize, text);
  } else if (code == 429) {
    say(problem, problemSize, "the relay has had enough from this controller today");
  } else if (code >= 400) {
    // The relay says why in {"ok":false,"error":"..."}: kept short.
    char text[160];
    const char *at = strstr(answer, "\"error\":\"");
    if (at) {
      const char *start = at + 9;
      const char *end = strchr(start, '"');
      const int n = end ? static_cast<int>(end - start) : static_cast<int>(strlen(start));
      snprintf(text, sizeof(text), "the relay refused it (%d): %.*s", code, n, start);
    } else {
      snprintf(text, sizeof(text), "the relay refused it (%d)", code);
    }
    say(problem, problemSize, text);
  }
  return code;
}

// -------------------------------------------------------------------------- page preview --
namespace {
constexpr size_t PAGE_MAX = 192 * 1024;  // some shops put their tags far into a long <head>
}

int fetchPagePreview(const char *url, PageMeta &out, char *problem, size_t problemSize) {
  out = PageMeta{};
  say(problem, problemSize, "");
  if (!url || (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0)) {
    say(problem, problemSize, "not a web link");
    return -2;
  }
  static std::string page(PAGE_MAX + 1, '\0');
  hostnet::HttpRequest r;
  r.url = url;
  // A browser-style agent that still says what it is: shops behind bot filters refuse unknown ones.
  r.headers =
      "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) BrewController/1.0\r\n"
      "Accept: text/html,application/xhtml+xml\r\nAccept-Language: en\r\n";
  r.timeoutMs = 16000;
  r.stopAfter = "</head";  // enough once the head has ended
  hostnet::HttpResult res;
  char why[96] = {};
  int code = -1;
  for (int tries = 0; tries < 2; ++tries) {  // a connection that fails is tried once more
    code = hostnet::httpRequest(r, &page[0], page.size(), res, why, sizeof(why));
    if (code >= 0) break;
  }
  if (code == 200) {
    parsePageHead(page.c_str(), res.length, url, out);
    return 200;
  }
  if (code < 0 && why[0]) snprintf(problem, problemSize, "no secure connection: %s", why);
  else if (code < 0) say(problem, problemSize, "the shop's site could not be reached");
  else if (code == 403 || code == 429 || code == 503) say(problem, problemSize, "the shop's site turned the controller away");
  else if (code >= 300 && code < 400) say(problem, problemSize, "too many redirects");
  else say(problem, problemSize, "the shop's site answered with an error");
  return code;
}

// ---------------------------------------------------------------------- add-on network --
namespace addon {

// The board checks an https server's certificate by its SHA-256 before sending a byte (a grinder's
// own certificate is self-signed and names no host); URLSession does the same in ios_net.mm.
void fetchHere(FetchJob &job) {
  job.problem[0] = '\0';
  job.truncated = false;
  job.setCookie[0] = '\0';
  UrlParts u;
  if (!splitUrl(job.url, u)) {
    job.status = -2;
    say(job.problem, sizeof(job.problem), "not an http(s) address");
    return;
  }
  std::string headers;
  if (job.cookie[0]) headers += std::string("Cookie: ") + job.cookie + "\r\n";
  if (job.post) {
    headers += std::string("Content-Type: ") + (job.contentType ? job.contentType : "application/json") + "\r\n";
  }
  hostnet::HttpRequest r;
  r.method = job.post ? "POST" : "GET";
  r.url = job.url;
  r.headers = headers.c_str();
  r.body = job.body;
  r.bodyLen = job.body ? strlen(job.body) : 0;
  r.timeoutMs = job.totalTimeoutMs;
  r.followRedirects = false;  // the board does not follow them
  if (u.tls) r.pinSha256 = job.certSha256;
  hostnet::HttpResult res;
  job.status = hostnet::httpRequest(r, reinterpret_cast<char *>(job.out), job.outCap, res, job.problem,
                                    sizeof(job.problem));
  job.outLen = res.length;
  job.truncated = res.truncated;
  strncpy(job.setCookie, res.setCookie, sizeof(job.setCookie) - 1);
  job.setCookie[sizeof(job.setCookie) - 1] = '\0';
  if (job.status < 0 && !hasProblem(job.problem)) say(job.problem, sizeof(job.problem), "could not connect");
}

}  // namespace addon
}  // namespace bbw
