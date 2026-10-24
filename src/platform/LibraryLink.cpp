// The shared library over HTTPS: GitHub's contents API with the owner's token, or
// the Brew Controller relay with none. Not built for the emulator (see LibraryLink.h).
#ifndef BBW_EMULATOR

#include "LibraryLink.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "SafeTls.h"
#include <time.h>

#include "BigMem.h"
#include "GithubRoots.h"
// Mozilla's root certificates in ESP-IDF's bundle format, for the relay (another host than GitHub).
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_certs_x509_crt_bundle_bin_start");
#include "Log.h"
#include "core/Share.h"

namespace bbw {

namespace {

constexpr const char *API = "https://api.github.com/repos/";
constexpr uint16_t CONNECT_TIMEOUT_MS = 6000;
constexpr uint16_t ANSWER_TIMEOUT_MS = 6000;
// After a failure: wait this long, doubling each time, before trying again.
constexpr uint32_t BACKOFF_FIRST_MS = 60000;
constexpr uint32_t BACKOFF_MAX_MS = 3600000;
// A record, base64 encoded, inside a small JSON object.
constexpr size_t BODY_SIZE = SHARE_ENVELOPE_MAX * 4 / 3 + 128;

char g_repo[64];
char g_token[128];
char g_relayBase[96];        // "https://host"; empty: no relay
bool g_viaRelay = false;
bool g_keyMissing = false;
SafeTlsClient *g_client;  // one connection, kept open between requests
SafeTlsClient *g_relayClient;
char *g_relayBody;           // RELAY_BODY_SIZE, in PSRAM
// {"path":"...","record":"<the envelope, escaped>"}: every character escaped, at worst.
constexpr size_t RELAY_BODY_SIZE = 2 * SHARE_ENVELOPE_MAX + 256;
char *g_body;                // BODY_SIZE, in PSRAM
LibraryLinkStatus g_status;
uint32_t g_backoffMs = 0;
uint32_t g_retryAtMs = 0;    // 0 = not backing off

bool clockSet() { return time(nullptr) > 1700000000; }

void fail(int code, const char *problem) {
  g_status.lastHttp = code;
  strlcpy(g_status.problem, problem, sizeof(g_status.problem));
  g_backoffMs = g_backoffMs == 0 ? BACKOFF_FIRST_MS
                                 : (g_backoffMs * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : g_backoffMs * 2);
  g_retryAtMs = millis() + g_backoffMs;
  if (g_retryAtMs == 0) g_retryAtMs = 1;
  if (g_client) g_client->stop();  // start clean next time
  LOGW("library: %s (answer %d), next try in %u s", problem, code,
       static_cast<unsigned>(g_backoffMs / 1000));
}

void succeeded(int code) {
  g_status.lastHttp = code;
  g_status.problem[0] = '\0';
  g_status.lastOkEpoch = static_cast<uint32_t>(time(nullptr));
  g_backoffMs = 0;
  g_retryAtMs = 0;
}

// The relay's answers, in words.
const char *explainRelay(int code) {
  if (code < 0) return "the relay could not be reached";
  if (code == 403) return "the relay refused this controller";
  if (code == 429) return "the relay's daily limit is reached";
  if (code == 502) return "the relay could not reach the library";
  return "the relay answered with an error";
}

// What an answer that is not a success means, in words.
const char *explain(int code) {
  if (g_viaRelay) return explainRelay(code);
  if (code < 0) return "GitHub could not be reached";
  if (code == 401) return "GitHub refused the token";
  if (code == 403) return "the token may not do that, or the hourly limit is reached";
  if (code == 404) return "repository, branch or permission not found";
  if (code == 409) return "GitHub was busy with the branch";
  return "GitHub answered with an error";
}

void begin(HTTPClient &http, const char *url, const char *accept) {
  if (g_viaRelay) {
    if (!g_relayClient) {
      g_relayClient = new SafeTlsClient();
      g_relayClient->setCACertBundle(rootca_crt_bundle_start);
      g_relayClient->setHandshakeTimeout(15);  // the relay's chain is checked in software too
    }
    http.setReuse(true);
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(ANSWER_TIMEOUT_MS);
    http.begin(*g_relayClient, url);
    http.addHeader("Accept", accept);
    http.setUserAgent("brew-by-weight-controller");
    return;
  }
  if (!g_client) {
    g_client = new SafeTlsClient();
    g_client->setCACert(GITHUB_ROOTS_PEM);  // nothing else is trusted
    g_client->setHandshakeTimeout(CONNECT_TIMEOUT_MS / 1000);
  }
  http.setReuse(true);
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(ANSWER_TIMEOUT_MS);
  http.begin(*g_client, url);
  char authorization[160];
  snprintf(authorization, sizeof(authorization), "Bearer %s", g_token);
  http.addHeader("Authorization", authorization);
  http.addHeader("Accept", accept);
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");
  http.setUserAgent("brew-by-weight-controller");
}

}  // namespace

void libraryLinkSetRelay(const char *base) { strlcpy(g_relayBase, base ? base : "", sizeof(g_relayBase)); }

bool libraryTakeKeyMissing() {
  const bool missing = g_keyMissing;
  g_keyMissing = false;
  return missing;
}

void libraryLinkConfigure(const char *repo, const char *token) {
  strlcpy(g_repo, repo ? repo : "", sizeof(g_repo));
  strlcpy(g_token, token ? token : "", sizeof(g_token));
  const bool own = g_repo[0] != '\0' && g_token[0] != '\0';
  g_viaRelay = !own && g_relayBase[0] != '\0';
  g_keyMissing = false;
  strlcpy(g_status.repository, g_viaRelay ? g_relayBase + (strncmp(g_relayBase, "https://", 8) == 0 ? 8 : 0) : g_repo,
          sizeof(g_status.repository));
  g_status.configured = own || g_viaRelay;
  // New details deserve a fresh try.
  g_backoffMs = 0;
  g_retryAtMs = 0;
  g_status.problem[0] = '\0';
  if (g_client) g_client->stop();
}

void libraryLinkRelease() {
  if (g_client) g_client->stop();
  if (g_relayClient) g_relayClient->stop();
}

bool libraryLinkReady() {
  if (!g_status.configured || WiFi.status() != WL_CONNECTED || !clockSet()) return false;
  return g_retryAtMs == 0 || static_cast<int32_t>(millis() - g_retryAtMs) >= 0;
}

// Through the relay: POST <relay>/library/put {"path","record"}.
bool relayPut(const char *path, const char *text) {
  if (!g_relayBody) g_relayBody = bigArray<char>(RELAY_BODY_SIZE);
  JsonOut j(g_relayBody, RELAY_BODY_SIZE);
  j.open();
  j.text("path", path);
  j.text("record", text);
  j.close();
  if (!j.ok()) return false;  // would not fit: not a record
  char url[160];
  snprintf(url, sizeof(url), "%s/library/put", g_relayBase);
  HTTPClient http;
  begin(http, url, "application/json");
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(reinterpret_cast<uint8_t *>(g_relayBody), j.length());
  http.end();
  // 201: in the library's inbox. 422 there means it arrived on an earlier try.
  if (code == 201 || code == 200 || code == 422) {
    succeeded(code);
    return true;
  }
  if (code == 409) {
    // The relay has no key for this controller yet (a library that started over, or a key that went
    // out by token before): the key goes now, then this record again. Not a failure to back off from.
    g_keyMissing = true;
    g_status.lastHttp = code;
    strlcpy(g_status.problem, "the relay asked for this controller's key first", sizeof(g_status.problem));
    return false;
  }
  fail(code, explain(code));
  return false;
}

bool libraryPut(const char *path, const char *text) {
  if (!libraryLinkReady() || !path || !text) return false;
  if (g_viaRelay) return relayPut(path, text);
  if (!g_body) g_body = bigArray<char>(BODY_SIZE);
  // {"message":"...","branch":"inbox","content":"<base64 of the record>"}
  int at = snprintf(g_body, BODY_SIZE, "{\"message\":\"Record from a controller\",\"branch\":\"inbox\",\"content\":\"");
  const size_t encoded = base64Encode(reinterpret_cast<const uint8_t *>(text), strlen(text),
                                      g_body + at, BODY_SIZE - at - 3);
  if (encoded == 0) return false;  // would not fit: not a record
  strlcpy(g_body + at + encoded, "\"}", BODY_SIZE - at - encoded);

  char url[224];
  snprintf(url, sizeof(url), "%s%s/contents/inbox/%s", API, g_repo, path);
  HTTPClient http;
  begin(http, url, "application/vnd.github+json");
  http.addHeader("Content-Type", "application/json");
  const int code = http.sendRequest("PUT", reinterpret_cast<uint8_t *>(g_body), strlen(g_body));
  http.end();
  // 201: created. 422: a file of that name is there already, which for a
  // record means it arrived on an earlier try whose answer was lost.
  if (code == 201 || code == 200 || code == 422) {
    succeeded(code);
    return true;
  }
  fail(code, explain(code));
  return false;
}

bool libraryGet(const char *path, char *out, size_t size) {
  if (!libraryLinkReady() || !path || !out || size < 2) return false;
  out[0] = '\0';
  char url[224];
  if (g_viaRelay) snprintf(url, sizeof(url), "%s/library/get?path=%s", g_relayBase, path);
  else snprintf(url, sizeof(url), "%s%s/contents/%s?ref=main", API, g_repo, path);
  HTTPClient http;
  begin(http, url, "application/vnd.github.raw+json");  // the file itself, not a description of it
  const int code = http.GET();
  if (code == 404) {  // not there: an answer, not a failure
    http.end();
    succeeded(code);
    return false;
  }
  if (code != 200) {
    http.end();
    fail(code, explain(code));
    return false;
  }
  const int length = http.getSize();  // -1 when the server does not say
  if (length >= static_cast<int>(size)) {
    http.end();
    succeeded(code);
    LOGW("library: %s is too big to read here (%d bytes)", path, length);
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  size_t got = 0;
  const uint32_t startedMs = millis();
  while (http.connected() && (length < 0 || got < static_cast<size_t>(length)) &&
         millis() - startedMs < ANSWER_TIMEOUT_MS) {
    const int ready = stream->available();
    if (ready <= 0) {
      delay(2);
      continue;
    }
    if (got + 1 >= size) {  // more than fits
      got = 0;
      break;
    }
    const size_t room = size - 1 - got;
    got += stream->readBytes(out + got, static_cast<size_t>(ready) < room ? ready : room);
  }
  http.end();
  out[got] = '\0';
  const bool whole = got > 0 && (length < 0 || got == static_cast<size_t>(length));
  if (!whole) {
    out[0] = '\0';
    fail(code, "an answer from GitHub was cut short");
    return false;
  }
  succeeded(code);
  return true;
}

LibraryLinkStatus libraryLinkStatus() {
  LibraryLinkStatus status = g_status;
  status.ready = libraryLinkReady();
  strlcpy(status.via, g_viaRelay ? "the Brew Controller relay, over HTTPS (no GitHub token needed)"
                                  : "GitHub, over HTTPS, with your token", sizeof(status.via));
  status.viaRelay = g_viaRelay;
  if (status.configured && !status.problem[0]) {
    if (WiFi.status() != WL_CONNECTED) {
      strlcpy(status.problem, "not on a Wi-Fi network", sizeof(status.problem));
    } else if (!clockSet()) {
      strlcpy(status.problem, "the clock is not set yet", sizeof(status.problem));
    }
  }
  return status;
}

}  // namespace bbw

#endif  // BBW_EMULATOR
