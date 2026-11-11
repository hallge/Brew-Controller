#pragma once
// Portable network logic for the iPhone build: everything that is not the transport. Compiles on
// Windows (the headless runner and test/hostapp/net_test.cpp) and on iOS. Transport is one function,
// hostHttpNative(): URLSession in ios_net.mm; a "no network" stand-in elsewhere.
#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "mqtt_client.h"

namespace bbw {
namespace hostnet {

struct HttpRequest {
  const char *method = "GET";
  const char *url = nullptr;
  const char *headers = nullptr;     // "Name: value\r\n..."; may be null
  const void *body = nullptr;
  size_t bodyLen = 0;
  int timeoutMs = 8000;              // for the whole request
  const char *pinSha256 = nullptr;   // https: accept only the server certificate with this SHA-256
                                     // (64 hex digits, colons allowed); no host name is checked
  bool followRedirects = true;
  const char *stopAfter = nullptr;   // stop reading once this text has arrived (e.g. "</head")
};

struct HttpResult {
  int status = -1;           // HTTP status, or < 0 with the reason in `problem`
  size_t length = 0;         // bytes put in the response buffer
  bool truncated = false;    // more came than the buffer holds
  char setCookie[128] = {};  // the first Set-Cookie, name=value only
  char location[256] = {};   // Location of a redirect that was not followed
};

// Blocking. Never on the main thread. Fills response (NUL-terminated, cap bytes with the NUL).
int httpRequest(const HttpRequest &request, char *response, size_t cap, HttpResult &result,
                char *problem, size_t problemSize);

// The transport, provided by ios_net.mm (iOS) or by net_portable.cpp's stand-in (everywhere else).
int hostHttpNative(const HttpRequest &request, char *response, size_t cap, HttpResult &result,
                   char *problem, size_t problemSize);

// Tests: replaces the transport for the whole process. nullptr restores it.
using HttpHandler = int (*)(const HttpRequest &, char *response, size_t cap, HttpResult &, char *problem,
                            size_t problemSize);
void setHttpHandler(HttpHandler handler);

// ---- device key --------------------------------------------------------------------------------
// ECDSA DER signature (SEQUENCE { INTEGER r, INTEGER s }) to the library's raw r|s: 32 + 32 bytes,
// big endian, left padded. false when the DER is not a P-256 signature.
bool derToRaw(const uint8_t *der, size_t length, uint8_t out[64]);

// The P-256 primitives, provided by ios_devicekey.mm (Security.framework). The private key is the
// 97 bytes of the X9.63 form: 04 | X | Y | K.
constexpr size_t KEY_BLOB_LEN = 97;
bool hostP256Generate(uint8_t blob[KEY_BLOB_LEN]);
// Signs a SHA-256 digest; DER out (at most 72 bytes).
bool hostP256Sign(const uint8_t blob[KEY_BLOB_LEN], const uint8_t hash[32], uint8_t *der, size_t *derLength);
// True when the two above really sign (false in the stand-in).
bool hostP256Available();

// ---- test hooks --------------------------------------------------------------------------------
// The transport MQTT uses (default: POSIX sockets). Called each time a session is made.
using MqttTransportMaker = std::unique_ptr<mqtt::Transport> (*)();
void setMqttTransportMaker(MqttTransportMaker maker);
// The clock of the library's backoff and of MQTT's retry timer.
void setLibraryClockForTests(int64_t (*nowMs)());

}  // namespace hostnet
}  // namespace bbw
