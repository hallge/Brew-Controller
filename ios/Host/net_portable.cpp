// Portable network logic for the iPhone build (see net_portable.h and NET.md). What the firmware
// calls on the board's LibraryLink.cpp, DeviceKey.cpp and HomeAssistantLink.cpp (MQTT half) is here,
// with the board's behaviour; only the transport differs.
#include "net_portable.h"

#include <Preferences.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "core/HomeAssistant.h"
#include "core/Share.h"
#include "platform/DeviceKey.h"
#include "platform/HomeAssistantLink.h"
#include "platform/LibraryLink.h"

namespace bbw {
namespace hostnet {

namespace {

HttpHandler g_handler = nullptr;
int64_t (*g_clock)() = nullptr;

int64_t nowMs() {
  if (g_clock) return g_clock();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void say(char *problem, size_t size, const char *text) {
  if (problem && size) {
    strncpy(problem, text, size - 1);
    problem[size - 1] = '\0';
  }
}

}  // namespace

void setHttpHandler(HttpHandler handler) { g_handler = handler; }
void setLibraryClockForTests(int64_t (*now)()) {
  g_clock = now;
  mqtt::setClockForTests(now);
}

int httpRequest(const HttpRequest &request, char *response, size_t cap, HttpResult &result, char *problem,
                size_t problemSize) {
  result = HttpResult{};
  if (response && cap) response[0] = '\0';
  say(problem, problemSize, "");
  if (!request.url || (strncmp(request.url, "http://", 7) != 0 && strncmp(request.url, "https://", 8) != 0)) {
    result.status = -2;
    say(problem, problemSize, "not an http(s) address");
    return -2;
  }
  const int code = g_handler ? g_handler(request, response, cap, result, problem, problemSize)
                             : hostHttpNative(request, response, cap, result, problem, problemSize);
  result.status = code;
  return code;
}

#if !defined(__APPLE__)
// No network off the iPhone: the Windows runner never reaches a real service.
int hostHttpNative(const HttpRequest &, char *response, size_t cap, HttpResult &, char *problem,
                   size_t problemSize) {
  if (response && cap) response[0] = '\0';
  say(problem, problemSize, "no network in this build");
  return -1;
}
#endif

// ---------------------------------------------------------------------------- device key --

bool derToRaw(const uint8_t *der, size_t length, uint8_t out[64]) {
  if (!der || length < 8 || der[0] != 0x30) return false;
  size_t at = 1;
  size_t seq = der[at++];
  if (seq & 0x80) {  // long form (a 72-byte signature never needs it, but be lenient)
    const size_t n = seq & 0x7F;
    if (n == 0 || n > 2 || at + n > length) return false;
    seq = 0;
    for (size_t i = 0; i < n; ++i) seq = (seq << 8) | der[at++];
  }
  if (at + seq != length) return false;
  memset(out, 0, 64);
  for (int part = 0; part < 2; ++part) {
    if (at + 2 > length || der[at] != 0x02) return false;
    size_t n = der[at + 1];
    at += 2;
    if (n == 0 || n > 33 || at + n > length) return false;
    const uint8_t *v = der + at;
    at += n;
    while (n > 32 && v[0] == 0) {  // the sign byte
      ++v;
      --n;
    }
    if (n > 32) return false;
    memcpy(out + part * 32 + (32 - n), v, n);
  }
  return at == length;
}

#if !defined(__APPLE__)
bool hostP256Generate(uint8_t blob[KEY_BLOB_LEN]) {
  (void)blob;
  return false;
}
bool hostP256Sign(const uint8_t[KEY_BLOB_LEN], const uint8_t[32], uint8_t *, size_t *) { return false; }
bool hostP256Available() { return false; }
#endif

namespace {

constexpr uint16_t KEY_VERSION = 1;
// The board's own record (DeviceKey.cpp StoredKey), so a backup moves a key either way.
#pragma pack(push, 1)
struct StoredKey {
  uint16_t version;
  uint8_t privateKey[32];
  uint8_t publicKey[PUBLIC_KEY_LEN];
};
#pragma pack(pop)
static_assert(sizeof(StoredKey) == 99, "the board's key record");

class HostSigner : public ISigner {
 public:
  bool begin() {
    if (!hostP256Available()) return beginUnsigned();
    StoredKey stored = {};
    Preferences prefs;
    prefs.begin("bbw", false);
    const bool have = prefs.getBytesLength("devkey") == sizeof(stored) &&
                      prefs.getBytes("devkey", &stored, sizeof(stored)) == sizeof(stored) &&
                      stored.version == KEY_VERSION && stored.publicKey[0] == 0x04;
    if (!have) {
      uint8_t blob[KEY_BLOB_LEN];
      if (!hostP256Generate(blob)) return false;
      stored = StoredKey{};
      stored.version = KEY_VERSION;
      memcpy(stored.publicKey, blob, PUBLIC_KEY_LEN);
      memcpy(stored.privateKey, blob + PUBLIC_KEY_LEN, 32);
      memset(blob, 0, sizeof(blob));
      if (prefs.putBytes("devkey", &stored, sizeof(stored)) != sizeof(stored)) return false;
    }
    memcpy(publicKey_, stored.publicKey, PUBLIC_KEY_LEN);
    memcpy(blob_, stored.publicKey, PUBLIC_KEY_LEN);
    memcpy(blob_ + PUBLIC_KEY_LEN, stored.privateKey, 32);
    memset(&stored, 0, sizeof(stored));
    real_ = true;
    ready_ = true;
    return true;
  }
  bool ready() const override { return ready_; }
  const uint8_t *publicKey() const override { return publicKey_; }
  const char *algorithm() const override { return real_ ? "ES256" : "HOST-UNSIGNED"; }
  bool sign(const uint8_t hash[32], uint8_t signature[SIGNATURE_LEN]) override {
    if (!ready_) return false;
    if (!real_) return signUnsigned(hash, signature);
    uint8_t der[80];
    size_t derLength = sizeof(der);
    return hostP256Sign(blob_, hash, der, &derLength) && derToRaw(der, derLength, signature);
  }

 private:
  // Off the iPhone (tests): not a signature, and says so in its algorithm name, so the library
  // refuses it.
  bool beginUnsigned() {
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
  bool signUnsigned(const uint8_t hash[32], uint8_t signature[SIGNATURE_LEN]) {
    Sha256 a, b;
    a.update(secret_, sizeof(secret_));
    a.update(hash, 32);
    a.finish(signature);
    b.update(hash, 32);
    b.update(secret_, sizeof(secret_));
    b.finish(signature + 32);
    return true;
  }
  void mix(const char *tag, uint8_t out[32]) {
    Sha256 h;
    h.update(secret_, sizeof(secret_));
    h.update(reinterpret_cast<const uint8_t *>(tag), 1);
    h.finish(out);
  }
  uint8_t secret_[32] = {};
  uint8_t blob_[KEY_BLOB_LEN] = {};
  uint8_t publicKey_[PUBLIC_KEY_LEN] = {};
  bool ready_ = false, real_ = false;
};

HostSigner g_signer;

}  // namespace
}  // namespace hostnet

bool beginDeviceKey() { return hostnet::g_signer.begin(); }
ISigner &deviceSigner() { return hostnet::g_signer; }

// ---------------------------------------------------------------------------- the library --
namespace hostnet {
namespace {

constexpr const char *API = "https://api.github.com/repos/";
constexpr int ANSWER_TIMEOUT_MS = 12000;
constexpr uint32_t BACKOFF_FIRST_MS = 60000;
constexpr uint32_t BACKOFF_MAX_MS = 3600000;
constexpr size_t RELAY_BODY_SIZE = 2 * SHARE_ENVELOPE_MAX + 256;
constexpr size_t BODY_SIZE = SHARE_ENVELOPE_MAX * 4 / 3 + 128;

std::mutex g_lock;  // the status is read from the web thread; no request is made holding it
char g_repo[64], g_token[128], g_relayBase[96];
bool g_viaRelay = false, g_keyMissing = false, g_refused = false;
LibraryLinkStatus g_status;
uint32_t g_backoffMs = 0;
int64_t g_retryAtMs = 0;  // 0: not backing off

bool clockSet() { return time(nullptr) > 1700000000; }

void copy(char *to, size_t size, const char *from) {
  strncpy(to, from ? from : "", size - 1);
  to[size - 1] = '\0';
}

void fail(int code, const char *problem) {
  std::lock_guard<std::mutex> hold(g_lock);
  g_status.lastHttp = code;
  copy(g_status.problem, sizeof(g_status.problem), problem);
  g_backoffMs = g_backoffMs == 0 ? BACKOFF_FIRST_MS
                                 : (g_backoffMs * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : g_backoffMs * 2);
  g_retryAtMs = nowMs() + g_backoffMs;
  if (g_retryAtMs == 0) g_retryAtMs = 1;
}

void succeeded(int code) {
  std::lock_guard<std::mutex> hold(g_lock);
  g_status.lastHttp = code;
  g_status.problem[0] = '\0';
  g_status.lastOkEpoch = static_cast<uint32_t>(time(nullptr));
  g_backoffMs = 0;
  g_retryAtMs = 0;
}

const char *explainRelay(int code) {
  if (code < 0) return "the relay could not be reached";
  if (code == 403) return "the relay refused this controller";
  if (code == 429) return "the relay's daily limit is reached";
  if (code == 502) return "the relay could not reach the library";
  return "the relay answered with an error";
}

const char *explain(int code, bool relay) {
  if (relay) return explainRelay(code);
  if (code < 0) return "GitHub could not be reached";
  if (code == 401) return "GitHub refused the token";
  if (code == 403) return "the token may not do that, or the hourly limit is reached";
  if (code == 404) return "repository, branch or permission not found";
  if (code == 409) return "GitHub was busy with the branch";
  return "GitHub answered with an error";
}

struct Config {
  std::string repo, token, relay;
  bool viaRelay;
};
Config config() {
  std::lock_guard<std::mutex> hold(g_lock);
  return Config{g_repo, g_token, g_relayBase, g_viaRelay};
}

bool ready() {
  std::lock_guard<std::mutex> hold(g_lock);
  if (!g_status.configured || !clockSet()) return false;
  return g_retryAtMs == 0 || nowMs() >= g_retryAtMs;
}

// The headers GitHub's contents API wants, or the relay's.
std::string headersFor(const Config &c, const char *accept, bool json) {
  std::string h = std::string("Accept: ") + accept + "\r\nUser-Agent: brew-by-weight-controller\r\n";
  if (!c.viaRelay) {
    h += "Authorization: Bearer " + c.token + "\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
  }
  if (json) h += "Content-Type: application/json\r\n";
  return h;
}

bool relayPut(const Config &c, const char *path, const char *text) {
  std::vector<char> body(RELAY_BODY_SIZE);
  JsonOut j(body.data(), body.size());
  j.open();
  j.text("path", path);
  j.text("record", text);
  j.close();
  if (!j.ok()) return false;  // would not fit: not a record
  const std::string url = c.relay + "/library/put";
  const std::string headers = headersFor(c, "application/json", true);
  HttpRequest r;
  r.method = "POST";
  r.url = url.c_str();
  r.headers = headers.c_str();
  r.body = body.data();
  r.bodyLen = j.length();
  r.timeoutMs = ANSWER_TIMEOUT_MS;
  char answer[128], problem[64];
  HttpResult res;
  const int code = httpRequest(r, answer, sizeof(answer), res, problem, sizeof(problem));
  if (code == 201 || code == 200 || code == 422) {
    succeeded(code);
    return true;
  }
  if (code == 409) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_keyMissing = true;
    g_status.lastHttp = code;
    copy(g_status.problem, sizeof(g_status.problem), "the relay asked for this controller's key first");
    return false;
  }
  if (code == 400 || code == 401 || code == 403 || code == 413) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_refused = true;
    g_status.lastHttp = code;
    copy(g_status.problem, sizeof(g_status.problem), "the relay refused a record; it was dropped");
    return false;
  }
  fail(code, explain(code, true));
  return false;
}

bool putHere(const char *path, const char *text) {
  if (!ready() || !path || !text) return false;
  const Config c = config();
  if (c.viaRelay) return relayPut(c, path, text);
  std::vector<char> body(BODY_SIZE);
  int at = snprintf(body.data(), body.size(),
                    "{\"message\":\"Record from a controller\",\"branch\":\"inbox\",\"content\":\"");
  const size_t encoded = base64Encode(reinterpret_cast<const uint8_t *>(text), strlen(text), body.data() + at,
                                      body.size() - at - 3);
  if (encoded == 0) return false;
  strcpy(body.data() + at + encoded, "\"}");
  const std::string url = std::string(API) + c.repo + "/contents/inbox/" + path;
  const std::string headers = headersFor(c, "application/vnd.github+json", true);
  HttpRequest r;
  r.method = "PUT";
  r.url = url.c_str();
  r.headers = headers.c_str();
  r.body = body.data();
  r.bodyLen = strlen(body.data());
  r.timeoutMs = ANSWER_TIMEOUT_MS;
  char answer[128], problem[64];
  HttpResult res;
  const int code = httpRequest(r, answer, sizeof(answer), res, problem, sizeof(problem));
  if (code == 201 || code == 200 || code == 422) {  // 422: it arrived on an earlier try
    succeeded(code);
    return true;
  }
  fail(code, explain(code, false));
  return false;
}

bool getHere(const char *path, char *out, size_t size) {
  if (!ready() || !path || !out || size < 2) return false;
  out[0] = '\0';
  const Config c = config();
  const std::string url = c.viaRelay ? c.relay + "/library/get?path=" + path
                                     : std::string(API) + c.repo + "/contents/" + path + "?ref=main";
  const std::string headers = headersFor(c, "application/vnd.github.raw+json", false);
  HttpRequest r;
  r.url = url.c_str();
  r.headers = headers.c_str();
  r.timeoutMs = ANSWER_TIMEOUT_MS;
  char problem[64];
  HttpResult res;
  const int code = httpRequest(r, out, size, res, problem, sizeof(problem));
  if (code == 404) {  // not there: an answer, not a failure
    out[0] = '\0';
    succeeded(code);
    return false;
  }
  if (code != 200) {
    out[0] = '\0';
    fail(code, explain(code, c.viaRelay));
    return false;
  }
  if (res.truncated) {  // more than fits
    out[0] = '\0';
    succeeded(code);
    return false;
  }
  if (res.length == 0) {
    out[0] = '\0';
    fail(code, "an answer from GitHub was cut short");
    return false;
  }
  succeeded(code);
  return true;
}

}  // namespace
}  // namespace hostnet

void libraryLinkSetRelay(const char *base) {
  std::lock_guard<std::mutex> hold(hostnet::g_lock);
  hostnet::copy(hostnet::g_relayBase, sizeof(hostnet::g_relayBase), base);
}

bool libraryTakeRefused() {
  std::lock_guard<std::mutex> hold(hostnet::g_lock);
  const bool v = hostnet::g_refused;
  hostnet::g_refused = false;
  return v;
}

bool libraryTakeKeyMissing() {
  std::lock_guard<std::mutex> hold(hostnet::g_lock);
  const bool v = hostnet::g_keyMissing;
  hostnet::g_keyMissing = false;
  return v;
}

void libraryLinkConfigure(const char *repo, const char *token) {
  using namespace hostnet;
  std::lock_guard<std::mutex> hold(g_lock);
  copy(g_repo, sizeof(g_repo), repo);
  copy(g_token, sizeof(g_token), token);
  const bool own = g_repo[0] != '\0' && g_token[0] != '\0';
  g_viaRelay = !own && g_relayBase[0] != '\0';
  g_keyMissing = false;
  g_refused = false;
  copy(g_status.repository, sizeof(g_status.repository),
       g_viaRelay ? g_relayBase + (strncmp(g_relayBase, "https://", 8) == 0 ? 8 : 0) : g_repo);
  g_status.configured = own || g_viaRelay;
  g_backoffMs = 0;
  g_retryAtMs = 0;
  g_status.problem[0] = '\0';
}

void libraryLinkRelease() {}  // URLSession keeps nothing the firmware needs to let go of

bool libraryLinkReady() { return hostnet::ready(); }
bool libraryPut(const char *path, const char *text) { return hostnet::putHere(path, text); }
bool libraryGet(const char *path, char *out, size_t size) { return hostnet::getHere(path, out, size); }

LibraryLinkStatus libraryLinkStatus() {
  using namespace hostnet;
  LibraryLinkStatus status;
  bool via;
  {
    std::lock_guard<std::mutex> hold(g_lock);
    status = g_status;
    via = g_viaRelay;
  }
  status.ready = ready();
  copy(status.via, sizeof(status.via),
       via ? "the Brew Controller relay, over HTTPS (no GitHub token needed)" : "GitHub, over HTTPS, with your token");
  status.viaRelay = via;
  if (status.configured && !status.problem[0] && !clockSet()) {
    copy(status.problem, sizeof(status.problem), "the clock is not set yet");
  }
  return status;
}

// ------------------------------------------------------------------------------------ MQTT --
namespace hostnet {
namespace {

MqttTransportMaker g_maker = nullptr;
std::unique_ptr<mqtt::Session> g_session;
char g_host[64] = {};
uint16_t g_port = 1883;
char g_user[MQTT_USER_LEN] = {}, g_password[MQTT_PASSWORD_LEN] = {};
char g_node[24] = {}, g_firmware[24] = {}, g_problem[64] = {};
bool g_on = false, g_announced = false;
int64_t g_triedMs = 0;
void (*g_start)() = nullptr;
char g_startTopic[64] = {};

void onMessage(const std::string &topic, const uint8_t *payload, size_t length) {
  if (topic != g_startTopic) return;
  if (length == 5 && memcmp(payload, "PRESS", 5) == 0 && g_start) g_start();
}

std::unique_ptr<mqtt::Session> newSession() {
  std::unique_ptr<mqtt::Transport> t = g_maker ? g_maker() : mqtt::makeSocketTransport();
  if (!t) return nullptr;
  std::unique_ptr<mqtt::Session> s(new mqtt::Session(std::move(t)));
  s->setKeepAlive(30);
  s->setOnMessage(onMessage);
  return s;
}

// The discovery configs, retained: Home Assistant makes the device from them, also after it restarts.
void announce() {
  std::vector<char> topic(128), payload(900);
  for (size_t i = 0; i < MQTT_ENTITY_COUNT; ++i) {
    if (!mqttDiscoveryTopic(MQTT_ENTITIES[i], g_node, topic.data(), topic.size())) continue;
    if (!mqttDiscoveryConfig(MQTT_ENTITIES[i], g_node, g_firmware, payload.data(), payload.size())) continue;
    g_session->publish(topic.data(), payload.data(), true);
  }
  g_announced = true;
}

}  // namespace

void setMqttTransportMaker(MqttTransportMaker maker) {
  g_maker = maker;
  g_session.reset();
}

}  // namespace hostnet

void mqttConfigure(const char *url, const char *user, const char *password, const char *node,
                   const char *firmware) {
  using namespace hostnet;
  if (g_session && g_session->connected()) g_session->disconnect();
  g_session.reset();
  g_on = url && url[0] && mqttBrokerFromUrl(url, g_host, sizeof(g_host), g_port);
  copy(g_user, sizeof(g_user), user);
  copy(g_password, sizeof(g_password), password);
  copy(g_node, sizeof(g_node), node && node[0] ? node : "bbw");
  copy(g_firmware, sizeof(g_firmware), firmware);
  mqttTopic(g_node, "start", g_startTopic, sizeof(g_startTopic));
  g_announced = false;
  g_triedMs = 0;
  g_problem[0] = '\0';
  if (url && url[0] && !g_on) copy(g_problem, sizeof(g_problem), "the broker address is not mqtt://host[:port]");
}

bool mqttService(bool idle, void (*onStart)()) {
  using namespace hostnet;
  g_start = onStart;
  if (!g_on) return false;
  if (g_session && g_session->connected()) {
    if (g_session->service()) return true;
    copy(g_problem, sizeof(g_problem), "the connection to the broker was lost");
    g_triedMs = nowMs() ? nowMs() : 1;  // wait a minute before the next try, as for a failed connect
    return false;
  }
  if (!idle) return false;
  const int64_t now = nowMs();
  if (g_triedMs && now - g_triedMs < 60000) return false;
  g_triedMs = now ? now : 1;
  if (!g_session) g_session = newSession();
  if (!g_session) {
    copy(g_problem, sizeof(g_problem), "MQTT is not available in this build");
    return false;
  }
  char id[40], avail[64];
  snprintf(id, sizeof(id), "bbw-%s", g_node);
  mqttTopic(g_node, "availability", avail, sizeof(avail));
  int rc = 0;
  std::string why;
  if (!g_session->connect(g_host, g_port, id, g_user, g_user[0] ? g_password : nullptr, avail, "offline", rc, why)) {
    snprintf(g_problem, sizeof(g_problem), "the broker did not accept the connection (%d)", rc);
    return false;
  }
  g_problem[0] = '\0';
  g_session->publish(avail, "online", true);
  g_session->subscribe(g_startTopic);
  announce();
  return true;
}

bool mqttConnected() { return hostnet::g_on && hostnet::g_session && hostnet::g_session->connected(); }

bool mqttPublish(const char *topic, const char *payload, bool retain) {
  if (!mqttConnected()) return false;
  return hostnet::g_session->publish(topic, payload, retain);
}

const char *mqttProblem() { return hostnet::g_problem; }

}  // namespace bbw
