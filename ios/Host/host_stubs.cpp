// Portable stand-ins for what the Windows-only emulator files provide to the firmware: the device
// key, the shared-library link, Home Assistant, MQTT, diagnostics upload, page preview and the add-on
// network fetch (emu_devicekey, emu_homeassistant, emu_telemetry, emu_preview, emu_addonnet).
//
// They answer "no network" / "not available" until W6 replaces this file's contents with URLSession
// (hostHttp) and Security.framework (the key). Everything that leaves the process goes through
// hostHttp() so that W6 swaps one function.
#include <Preferences.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "BrewHost.h"
#include "addons/Addon.h"
#include "addons/HttpWire.h"
#include "core/HomeAssistant.h"
#include "core/Share.h"
#include "emu.h"
#include "platform/DeviceKey.h"
#include "platform/HomeAssistantLink.h"
#include "platform/LibraryLink.h"
#include "platform/PagePreview.h"
#include "platform/TelemetryLink.h"

namespace {

void say(char *problem, size_t size, const char *text) {
  if (problem && size) {
    strncpy(problem, text, size - 1);
    problem[size - 1] = '\0';
  }
}

}  // namespace

// ------------------------------------------------------------------ the one network seam --
int hostHttp(const char *method, const char *url, const char *headers, const void *body,
             size_t bodyLen, char *response, size_t responseCap, size_t *responseLen,
             int timeoutMs, char *problem, size_t problemSize) {
  (void)method;
  (void)url;
  (void)headers;
  (void)body;
  (void)bodyLen;
  (void)timeoutMs;
  if (response && responseCap) response[0] = '\0';
  if (responseLen) *responseLen = 0;
  say(problem, problemSize, "no network in this build yet");
  return -1;
}

namespace bbw {

// ------------------------------------------------------------------------- device key --
namespace {

// Not a signature, and says so in its algorithm name: the library refuses it. W6: P-256 through
// Security.framework.
class StandInSigner : public ISigner {
 public:
  bool begin() {
    Preferences prefs;
    prefs.begin("bbw", false);
    if (prefs.getBytesLength("devkey_host") != sizeof(secret_) ||
        prefs.getBytes("devkey_host", secret_, sizeof(secret_)) != sizeof(secret_)) {
      std::random_device source;
      for (uint8_t &b : secret_) b = static_cast<uint8_t>(source());
      prefs.putBytes("devkey_host", secret_, sizeof(secret_));
    }
    publicKey_[0] = 0x04;
    mix("x", publicKey_ + 1);
    mix("y", publicKey_ + 33);
    ready_ = true;
    return true;
  }
  bool ready() const override { return ready_; }
  const uint8_t *publicKey() const override { return publicKey_; }
  const char *algorithm() const override { return "HOST-UNSIGNED"; }
  bool sign(const uint8_t hash[32], uint8_t signature[SIGNATURE_LEN]) override {
    Sha256 a, b;
    a.update(secret_, sizeof(secret_));
    a.update(hash, 32);
    a.finish(signature);
    b.update(hash, 32);
    b.update(secret_, sizeof(secret_));
    b.finish(signature + 32);
    return true;
  }

 private:
  void mix(const char *tag, uint8_t out[32]) {
    Sha256 h;
    h.update(secret_, sizeof(secret_));
    h.update(reinterpret_cast<const uint8_t *>(tag), 1);
    h.finish(out);
  }
  uint8_t secret_[32] = {};
  uint8_t publicKey_[PUBLIC_KEY_LEN] = {};
  bool ready_ = false;
};

StandInSigner g_signer;

LibraryLinkStatus g_libraryStatus;

}  // namespace

bool beginDeviceKey() { return g_signer.begin(); }
ISigner &deviceSigner() { return g_signer; }

// -------------------------------------------------------------------- shared library --
void libraryLinkSetRelay(const char *) {}
bool libraryTakeRefused() { return false; }
bool libraryTakeKeyMissing() { return false; }
void libraryLinkConfigure(const char *repo, const char *) {
  g_libraryStatus = LibraryLinkStatus{};
  g_libraryStatus.configured = repo && repo[0];
  if (repo) strncpy(g_libraryStatus.repository, repo, sizeof(g_libraryStatus.repository) - 1);
}
bool libraryLinkReady() { return false; }
bool libraryPut(const char *, const char *) { return false; }
void libraryLinkRelease() {}
bool libraryGet(const char *, char *out, size_t size) {
  if (out && size) out[0] = '\0';
  return false;
}
LibraryLinkStatus libraryLinkStatus() {
  LibraryLinkStatus status = g_libraryStatus;
  status.ready = false;
  status.toLibrary = false;
  say(status.problem, sizeof(status.problem), "the library is not reachable in this build yet");
  say(status.via, sizeof(status.via), "no network in this build yet");
  return status;
}

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
  size_t length = 0;
  const int code = hostHttp(body ? "POST" : "GET", url, headers.c_str(), body, body ? strlen(body) : 0,
                            out, outSize, &length, timeoutMs, problem, problemSize);
  if (code < 0) {
    if (!problem || !problem[0]) say(problem, problemSize, "Home Assistant could not be reached");
  } else if (code == 401) say(problem, problemSize, "Home Assistant refused the token");
  else if (code == 404) say(problem, problemSize, "Home Assistant has no such entity or webhook");
  else if (code >= 400) say(problem, problemSize, "Home Assistant answered with an error");
  return code;
}

namespace {
char g_mqttProblem[64] = {};
}

void mqttConfigure(const char *url, const char *, const char *, const char *, const char *) {
  g_mqttProblem[0] = '\0';
  if (url && url[0]) say(g_mqttProblem, sizeof(g_mqttProblem), "MQTT is not available in this build yet");
}
bool mqttService(bool, void (*)()) { return false; }
bool mqttConnected() { return false; }
bool mqttPublish(const char *, const char *, bool) { return false; }
const char *mqttProblem() { return g_mqttProblem; }

// ------------------------------------------------------------------------- diagnostics --
int diagPost(const char *url, const char *envelope, char *problem, size_t problemSize) {
  say(problem, problemSize, "");
  if (!url || strncmp(url, "https://", 8) != 0) {
    say(problem, problemSize, "the relay's address is not an https link");
    return -2;
  }
  char answer[64];
  size_t length = 0;
  const int code = hostHttp("POST", url, "Content-Type: application/json\r\n", envelope,
                            envelope ? strlen(envelope) : 0, answer, sizeof(answer), &length, 20000,
                            problem, problemSize);
  if (code < 0) say(problem, problemSize, "the relay could not be reached");
  else if (code == 429) say(problem, problemSize, "the relay has had enough from this controller today");
  else if (code >= 400) say(problem, problemSize, "the relay refused it");
  return code;
}

// -------------------------------------------------------------------------- page preview --
int fetchPagePreview(const char *url, PageMeta &out, char *problem, size_t problemSize) {
  out = PageMeta{};
  say(problem, problemSize, "");
  if (!url || (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0)) {
    say(problem, problemSize, "not a web link");
    return -2;
  }
  static char page[196608];
  size_t length = 0;
  const int code = hostHttp("GET", url, "Accept: text/html\r\n", nullptr, 0, page, sizeof(page),
                            &length, 15000, problem, problemSize);
  if (code < 0) {
    say(problem, problemSize, "the shop's site could not be reached");
    return -1;
  }
  if (code != 200 && code != 206) {
    say(problem, problemSize, "the shop's site answered with an error");
    return code;
  }
  parsePageHead(page, length, url, out);
  return 200;
}

// ---------------------------------------------------------------------- add-on network --
namespace addon {

void fetchHere(FetchJob &job) {
  job.problem[0] = '\0';
  UrlParts u;
  if (!splitUrl(job.url, u)) {
    job.status = -2;
    say(job.problem, sizeof(job.problem), "not an http(s) address");
    return;
  }
  std::string headers;
  if (job.cookie[0]) headers += std::string("Cookie: ") + job.cookie + "\r\n";
  if (job.post) headers += std::string("Content-Type: ") + (job.contentType ? job.contentType : "application/json") + "\r\n";
  size_t length = 0;
  job.status = hostHttp(job.post ? "POST" : "GET", job.url, headers.c_str(), job.body,
                        job.body ? strlen(job.body) : 0, reinterpret_cast<char *>(job.out), job.outCap,
                        &length, job.totalTimeoutMs, job.problem, sizeof(job.problem));
  job.outLen = length;
}

}  // namespace addon
}  // namespace bbw
