// The board's side of sending a diagnostics report (TelemetryLink.h). Not built for the emulator.
#ifndef BBW_EMULATOR

#include "TelemetryLink.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "SafeTls.h"

#include <memory>

#include "LibraryLink.h"
#include "Log.h"

// Mozilla's root certificates in ESP-IDF's bundle format, linked in by board_build.embed_files.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_certs_x509_crt_bundle_bin_start");

namespace bbw {

namespace {

constexpr uint16_t CONNECT_TIMEOUT_MS = 8000;
constexpr uint16_t ANSWER_TIMEOUT_MS = 8000;
constexpr uint16_t HANDSHAKE_TIMEOUT_S = 15;

void say(char *problem, size_t size, const char *text) {
  if (problem && size) strlcpy(problem, text, size);
}

}  // namespace

int diagPost(const char *url, const char *envelope, char *problem, size_t problemSize) {
  say(problem, problemSize, "");
  if (!url || strncmp(url, "https://", 8) != 0) {
    say(problem, problemSize, "the relay's address is not an https link");
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
  // One TLS connection at a time: the library's is let go first.
  libraryLinkRelease();
  // On the heap, not the loop task's stack: a TLS client carries the whole mbedTLS context.
  std::unique_ptr<SafeTlsClient> tls(new SafeTlsClient());
  tls->setCACertBundle(rootca_crt_bundle_start);
  tls->setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(ANSWER_TIMEOUT_MS);
  if (!http.begin(*tls, url)) {
    say(problem, problemSize, "the relay's address could not be used");
    return -2;
  }
  http.setUserAgent("brew-by-weight-controller");
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(envelope)), strlen(envelope));
  if (code < 0) {
    char tlsError[96] = {};
    if (tls->lastError(tlsError, sizeof(tlsError)) == 0) tlsError[0] = '\0';
    char text[160];
    snprintf(text, sizeof(text), "the relay could not be reached%s%s", tlsError[0] ? ": " : "", tlsError);
    say(problem, problemSize, text);
  } else if (code == 429) {
    say(problem, problemSize, "the relay has had enough from this controller today");
  } else if (code >= 400) {
    // The relay says why in {"ok":false,"error":"..."}: kept short.
    String answer = http.getString();
    const int at = answer.indexOf("\"error\":\"");
    char text[160];
    if (at >= 0) {
      String why = answer.substring(at + 9);
      const int end = why.indexOf('"');
      if (end >= 0) why = why.substring(0, end);
      snprintf(text, sizeof(text), "the relay refused it (%d): %s", code, why.c_str());
    } else {
      snprintf(text, sizeof(text), "the relay refused it (%d)", code);
    }
    say(problem, problemSize, text);
  }
  http.end();
  return code;
}

}  // namespace bbw

#endif  // BBW_EMULATOR
