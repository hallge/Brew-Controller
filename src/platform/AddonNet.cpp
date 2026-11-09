// An add-on's HTTP(S) request on the board (addons/Addon.h FetchJob), on the network task
// (NetWorker.h netSubmit). Written by hand rather than with HTTPClient, so that for https the
// server's certificate is checked by its SHA-256 fingerprint before a single byte is sent: a
// device's own certificate (a grinder's) is self-signed and names no host, so no name is checked.

#ifndef BBW_EMULATOR

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "addons/Addon.h"
#include "addons/HttpWire.h"
#include "platform/BigMem.h"

namespace bbw {
namespace addon {

namespace {
void say(FetchJob &job, int status, const char *text) {
  job.status = status;
  strlcpy(job.problem, text, sizeof(job.problem));
}
}  // namespace

void fetchHere(FetchJob &job) {
  job.problem[0] = '\0';
  UrlParts u;
  if (!splitUrl(job.url, u)) return say(job, -2, "not an http(s) address");
  if (WiFi.status() != WL_CONNECTED) return say(job, -1, "not on a Wi-Fi network");
  static char *request = bigArray<char>(1024);  // PSRAM, once
  const size_t requestLen = buildRequest(job, u, request, 1024);
  if (!requestLen) return say(job, -2, "the request does not fit");

  const uint32_t started = millis();
  WiFiClient plain;
  WiFiClientSecure secure;
  Client *client = &plain;
  if (u.tls) {
    secure.setInsecure();  // the chain is not checked: the fingerprint below is
    secure.setHandshakeTimeout(job.connectTimeoutMs / 1000 + 1);
    if (!secure.connect(u.host, u.port, job.connectTimeoutMs)) return say(job, -1, "could not connect");
    if (!secure.verify(job.certSha256, nullptr)) {
      secure.stop();
      return say(job, -3, "the certificate is not the one expected");
    }
    client = &secure;
  } else {
    if (!plain.connect(u.host, u.port, job.connectTimeoutMs)) return say(job, -1, "could not connect");
  }
  if (client->write(reinterpret_cast<const uint8_t *>(request), requestLen) != requestLen) {
    client->stop();
    return say(job, -1, "the request could not be sent");
  }
  ResponseReader reader(job);
  uint8_t chunk[512];
  for (;;) {
    if (millis() - started > job.totalTimeoutMs) {
      client->stop();
      return say(job, -1, "timed out");
    }
    const int avail = client->available();
    if (avail > 0) {
      const int n = client->read(chunk, avail < static_cast<int>(sizeof(chunk)) ? avail : sizeof(chunk));
      if (n > 0) reader.feed(chunk, static_cast<size_t>(n));
      continue;
    }
    if (!client->connected()) break;
    delay(5);
  }
  client->stop();
  if (!reader.headComplete()) say(job, -1, "the answer was cut short");
}

}  // namespace addon
}  // namespace bbw

#endif  // BBW_EMULATOR
