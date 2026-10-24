// The board's page fetch for subscription cards (PagePreview.h). Not built for the emulator.
#ifndef BBW_EMULATOR

#include "PagePreview.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "SafeTls.h"
#include <esp_heap_caps.h>

#include <memory>

#include "BigMem.h"
#include "Log.h"
#include "LibraryLink.h"

// Mozilla's root certificates in ESP-IDF's bundle format, linked in by board_build.embed_files.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_certs_x509_crt_bundle_bin_start");

namespace bbw {

namespace {

// Some shops put their tags far into a long <head> (one at 100 KB): read up to this much, in PSRAM.
constexpr size_t PAGE_MAX = 192 * 1024;
constexpr uint16_t CONNECT_TIMEOUT_MS = 8000;
constexpr uint16_t ANSWER_TIMEOUT_MS = 8000;
// A shop's chain can be four certificates of P-384 keys, each checked in software: more time than
// GitHub's short chain needs.
constexpr uint16_t HANDSHAKE_TIMEOUT_S = 15;
constexpr int MAX_REDIRECTS = 4;
constexpr int TRIES = 2;   // a connection that fails is tried once more

char *g_page;  // PAGE_MAX + 1, in PSRAM

void say(char *problem, size_t size, const char *text) {
  if (problem && size) strlcpy(problem, text, size);
}

// Reads the answer until the end of <head>, PAGE_MAX bytes, or the time-out. Returns the length.
size_t readHead(HTTPClient &http) {
  WiFiClient *stream = http.getStreamPtr();
  const int length = http.getSize();
  size_t got = 0;
  const uint32_t startedMs = millis();
  while ((http.connected() || stream->available()) && got < PAGE_MAX &&
         (length < 0 || got < static_cast<size_t>(length)) && millis() - startedMs < ANSWER_TIMEOUT_MS) {
    const int ready = stream->available();
    if (ready <= 0) {
      delay(2);
      continue;
    }
    const size_t room = PAGE_MAX - got;
    got += stream->readBytes(g_page + got, static_cast<size_t>(ready) < room ? ready : room);
    g_page[got] = '\0';
    // Enough once the head has ended (looked for in the part just read, and a little before it).
    const size_t from = got > static_cast<size_t>(ready) + 8 ? got - ready - 8 : 0;
    if (strstr(g_page + from, "</head") || strstr(g_page + from, "</HEAD")) break;
  }
  g_page[got] = '\0';
  return got;
}

}  // namespace

int fetchPagePreview(const char *url, PageMeta &out, char *problem, size_t problemSize) {
  out = PageMeta{};
  say(problem, problemSize, "");
  if (!url || (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0)) {
    say(problem, problemSize, "not a web link");
    return -2;
  }
  if (WiFi.status() != WL_CONNECTED) {
    say(problem, problemSize, "not on a Wi-Fi network");
    return -1;
  }
  if (time(nullptr) < 1700000000) {
    say(problem, problemSize, "the clock is not set yet (certificates cannot be checked)");
    return -1;
  }
  if (!g_page) g_page = bigArray<char>(PAGE_MAX + 1);
  if (!g_page) {
    say(problem, problemSize, "no memory for the page");
    return -1;
  }
  // One TLS connection at a time: the library's is let go first.
  libraryLinkRelease();
  char current[256];
  strlcpy(current, url, sizeof(current));
  int code = -1;
  char tlsError[96] = {};
  int tries = 0;
  for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    const bool secure = strncmp(current, "https://", 8) == 0;
    // On the heap, not the loop task's stack: a TLS client carries the whole mbedTLS context.
    std::unique_ptr<SafeTlsClient> tls(new SafeTlsClient());
    std::unique_ptr<WiFiClient> plain(new WiFiClient());
    if (secure) {
      tls->setCACertBundle(rootca_crt_bundle_start);
      tls->setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
    }
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(ANSWER_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);  // followed here, across http and https
    if (!(secure ? http.begin(*tls, current) : http.begin(*plain, current))) {
      say(problem, problemSize, "not a link that can be read");
      return -2;
    }
    // A browser-style agent that still says what it is: shops behind bot filters (Shopify) refuse
    // agents they do not recognise.
    http.setUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) BrewController/1.0");
    http.addHeader("Accept", "text/html,application/xhtml+xml");
    http.addHeader("Accept-Language", "en");
    static const char *keep[] = {"Location"};
    http.collectHeaders(keep, 1);
    const size_t freeBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largestBefore = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t startedMs = millis();
    code = http.GET();
    if (code < 0) {
      // Why the connection failed: mbedTLS's own words when it was the secure part, with the memory
      // there was for it.
      tlsError[0] = '\0';
      if (secure) {
        const int err = tls->lastError(tlsError, sizeof(tlsError));
        if (err == 0) tlsError[0] = '\0';
      }
      LOGW("subscriptions: %s: no connection (answer %d) after %lu ms%s%s; chip RAM %u free, largest %u",
           current, code, static_cast<unsigned long>(millis() - startedMs), tlsError[0] ? ": " : "",
           tlsError, static_cast<unsigned>(freeBefore), static_cast<unsigned>(largestBefore));
      http.end();
      if (++tries < TRIES) {
        delay(500);
        --hop;   // the same address again
        continue;
      }
      break;
    }
    if (code >= 300 && code < 400 && http.hasHeader("Location")) {
      char next[256];
      resolveUrl(current, http.header("Location").c_str(), next, sizeof(next));
      http.end();
      if (!next[0]) break;
      strlcpy(current, next, sizeof(current));
      continue;
    }
    if (code != 200) {
      http.end();
      break;
    }
    const size_t got = readHead(http);
    http.end();
    parsePageHead(g_page, got, current, out);
    LOGI("subscriptions: read %u bytes of %s", static_cast<unsigned>(got), current);
    return 200;
  }
  if (code < 0 && tlsError[0] && problem && problemSize) {
    snprintf(problem, problemSize, "no secure connection: %s", tlsError);
  } else if (code < 0) say(problem, problemSize, "the shop's site could not be reached");
  else if (code == 403 || code == 429 || code == 503) say(problem, problemSize, "the shop's site turned the controller away");
  else if (code >= 300 && code < 400) say(problem, problemSize, "too many redirects");
  else say(problem, problemSize, "the shop's site answered with an error");
  LOGW("subscriptions: page not read (answer %d): %s", code, problem ? problem : "");
  return code;
}

}  // namespace bbw

#endif  // BBW_EMULATOR
