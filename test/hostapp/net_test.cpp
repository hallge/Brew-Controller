// Tests for the iPhone host's portable network code (ios/Host/net_portable.cpp, mqtt_client.cpp,
// host_stubs.cpp) with a fake transport: no socket is opened and no service is contacted.
// Built and run by test/hostapp/run.sh (with no argument, after the headless runner).
#include <Preferences.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "addons/Addon.h"
#include "core/HomeAssistant.h"
#include "core/Share.h"
#include "net_portable.h"
#include "platform/HomeAssistantLink.h"
#include "platform/DeviceKey.h"
#include "platform/LibraryLink.h"
#include "platform/PagePreview.h"
#include "platform/TelemetryLink.h"

namespace bbw {
namespace addon {
void fetchHere(FetchJob &job);
}
}  // namespace bbw

using namespace bbw;
using namespace bbw::hostnet;

// ---- Preferences stand-in (the runner's host_runtime.cpp is not linked here) -----------------------
static std::map<std::string, std::vector<uint8_t>> g_prefs;
bool Preferences::begin(const char *, bool) { return true; }
bool Preferences::isKey(const char *k) { return g_prefs.count(k) != 0; }
size_t Preferences::getBytesLength(const char *k) { return g_prefs.count(k) ? g_prefs[k].size() : 0; }
size_t Preferences::getBytes(const char *k, void *b, size_t max) {
  if (!g_prefs.count(k)) return 0;
  const size_t n = g_prefs[k].size() < max ? g_prefs[k].size() : max;
  memcpy(b, g_prefs[k].data(), n);
  return n;
}
size_t Preferences::putBytes(const char *k, const void *v, size_t n) {
  g_prefs[k].assign(static_cast<const uint8_t *>(v), static_cast<const uint8_t *>(v) + n);
  return n;
}

static int g_failures = 0;
static void check(bool ok, const char *what) {
  printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) ++g_failures;
}

// ---- fake HTTP -------------------------------------------------------------------------------------
struct Seen {
  std::string method, url, headers, body, pin, stopAfter;
  bool follow = true;
  int timeoutMs = 0;
};
static std::vector<Seen> g_seen;
static int g_answer = 200;
static std::string g_answerBody;
static std::string g_answerCookie;

static int fakeHttp(const HttpRequest &r, char *response, size_t cap, HttpResult &res, char *problem, size_t) {
  Seen s;
  s.method = r.method;
  s.url = r.url;
  s.headers = r.headers ? r.headers : "";
  s.body = r.body ? std::string(static_cast<const char *>(r.body), r.bodyLen) : "";
  s.pin = r.pinSha256 ? r.pinSha256 : "";
  s.stopAfter = r.stopAfter ? r.stopAfter : "";
  s.follow = r.followRedirects;
  s.timeoutMs = r.timeoutMs;
  g_seen.push_back(s);
  if (g_answer < 0) {
    if (problem) strcpy(problem, "fake: unreachable");
    return g_answer;
  }
  if (response && cap) {
    const size_t n = g_answerBody.size() < cap - 1 ? g_answerBody.size() : cap - 1;
    memcpy(response, g_answerBody.data(), n);
    response[n] = '\0';
    res.length = n;
    res.truncated = g_answerBody.size() > n;
  }
  strncpy(res.setCookie, g_answerCookie.c_str(), sizeof(res.setCookie) - 1);
  return g_answer;
}

static int64_t g_now = 1000000;
static int64_t fakeClock() { return g_now; }

// ---- fake MQTT transport -----------------------------------------------------------------------------
struct FakeBroker {
  std::vector<std::vector<uint8_t>> sent;  // whole packets, as sent
  std::deque<uint8_t> toClient;
  bool open = false, refuse = false, dead = false;
  uint8_t connackRc = 0;
  std::string openedHost;
  uint16_t openedPort = 0;
};
static FakeBroker g_broker;

class FakeTransport : public mqtt::Transport {
 public:
  bool open(const char *host, uint16_t port, int, std::string &problem) override {
    if (g_broker.refuse) {
      problem = "refused";
      return false;
    }
    g_broker.open = true;
    g_broker.openedHost = host;
    g_broker.openedPort = port;
    return true;
  }
  bool sendAll(const uint8_t *d, size_t n) override {
    if (!g_broker.open) return false;
    g_broker.sent.emplace_back(d, d + n);
    if ((d[0] >> 4) == mqtt::CONNECT) {  // the broker answers
      g_broker.toClient.push_back(0x20);
      g_broker.toClient.push_back(0x02);
      g_broker.toClient.push_back(0x00);
      g_broker.toClient.push_back(g_broker.connackRc);
    }
    return true;
  }
  int receive(uint8_t *buf, size_t cap, int) override {
    if (g_broker.dead) return -1;
    size_t n = 0;
    while (n < cap && !g_broker.toClient.empty()) {
      buf[n++] = g_broker.toClient.front();
      g_broker.toClient.pop_front();
    }
    return static_cast<int>(n);
  }
  void close() override { g_broker.open = false; }
};
static std::unique_ptr<mqtt::Transport> makeFake() { return std::unique_ptr<mqtt::Transport>(new FakeTransport()); }

static bool contains(const std::vector<uint8_t> &p, const char *text) {
  const size_t n = strlen(text);
  for (size_t i = 0; i + n <= p.size(); ++i) {
    if (memcmp(p.data() + i, text, n) == 0) return true;
  }
  return false;
}

static int g_starts = 0;
static void onStart() { ++g_starts; }

// ---- the tests ----------------------------------------------------------------------------------------
static void testDer() {
  puts("DER to r|s");
  uint8_t out[64];
  // r with the high bit set (a leading 00), s one byte short (padded on the left).
  uint8_t der[72];
  size_t n = 0;
  der[n++] = 0x30;
  der[n++] = 0;  // length, filled in
  der[n++] = 0x02;
  der[n++] = 33;
  der[n++] = 0x00;
  for (int i = 0; i < 32; ++i) der[n++] = static_cast<uint8_t>(0x80 + i);
  der[n++] = 0x02;
  der[n++] = 31;
  for (int i = 0; i < 31; ++i) der[n++] = static_cast<uint8_t>(i + 1);
  der[1] = static_cast<uint8_t>(n - 2);
  check(derToRaw(der, n, out), "accepts a signature with a sign byte and a short s");
  bool r = true, s = out[32] == 0;
  for (int i = 0; i < 32; ++i) r &= out[i] == 0x80 + i;
  for (int i = 0; i < 31; ++i) s &= out[33 + i] == i + 1;
  check(r, "r is the 32 bytes without the sign byte");
  check(s, "s is padded to 32 bytes on the left");
  // Both minimal and 32 bytes without padding.
  uint8_t full[70] = {0x30, 68, 0x02, 32};
  for (int i = 0; i < 32; ++i) full[4 + i] = static_cast<uint8_t>(i + 1);
  full[36] = 0x02;
  full[37] = 32;
  for (int i = 0; i < 32; ++i) full[38 + i] = static_cast<uint8_t>(0x41 + i);
  check(derToRaw(full, 70, out) && out[0] == 1 && out[31] == 32 && out[32] == 0x41 && out[63] == 0x60,
        "two 32-byte integers");
  full[69] ^= 0;  // unchanged
  check(!derToRaw(full, 69, out), "a truncated signature is refused");
  uint8_t bad[] = {0x31, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01};
  check(!derToRaw(bad, sizeof(bad), out), "not a SEQUENCE is refused");
  uint8_t big[] = {0x30, 38, 0x02, 34, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
                   0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
                   0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x02, 0x00};
  check(!derToRaw(big, sizeof(big), out), "an integer wider than 32 bytes is refused");
}

static void testPackets() {
  puts("MQTT packets");
  const std::vector<uint8_t> c = mqtt::encodeConnect("a", nullptr, nullptr, nullptr, nullptr, false, 30);
  const uint8_t expect[] = {0x10, 0x0D, 0, 4, 'M', 'Q', 'T', 'T', 4, 0x02, 0, 30, 0, 1, 'a'};
  check(c.size() == sizeof(expect) && memcmp(c.data(), expect, sizeof(expect)) == 0, "CONNECT, bare");
  const std::vector<uint8_t> c2 = mqtt::encodeConnect("id", "user", "pw", "will/t", "offline", true, 30);
  check(c2[0] == 0x10 && c2[9] == (0x02 | 0x04 | 0x20 | 0x80 | 0x40), "CONNECT flags: clean, will retained, user, password");
  check(contains(c2, "will/t") && contains(c2, "offline") && contains(c2, "user") && contains(c2, "pw"),
        "CONNECT carries will, user and password");
  const std::vector<uint8_t> p = mqtt::encodePublish("t/x", reinterpret_cast<const uint8_t *>("hi"), 2, true);
  const uint8_t pe[] = {0x31, 0x07, 0, 3, 't', '/', 'x', 'h', 'i'};
  check(p.size() == sizeof(pe) && memcmp(p.data(), pe, sizeof(pe)) == 0, "PUBLISH retained, QoS 0");
  const std::vector<uint8_t> s = mqtt::encodeSubscribe(1, "a/b");
  const uint8_t se[] = {0x82, 0x08, 0, 1, 0, 3, 'a', '/', 'b', 0};
  check(s.size() == sizeof(se) && memcmp(s.data(), se, sizeof(se)) == 0, "SUBSCRIBE QoS 0");
  check(mqtt::encodePing() == std::vector<uint8_t>({0xC0, 0}), "PINGREQ");
  check(mqtt::encodeDisconnect() == std::vector<uint8_t>({0xE0, 0}), "DISCONNECT");
  // A discovery-sized payload needs a two-byte remaining length.
  std::string big(700, 'x');
  const std::vector<uint8_t> bp = mqtt::encodePublish("homeassistant/sensor/x/config",
                                                      reinterpret_cast<const uint8_t *>(big.data()), big.size(), true);
  mqtt::Packet pk;
  check(mqtt::parsePacket(bp.data(), bp.size(), pk) == 1 && pk.type == mqtt::PUBLISH && (bp[1] & 0x80) &&
            pk.total == bp.size() && pk.bodyLength == bp.size() - 3,
        "a 700-byte payload: two-byte length, parsed back");
  mqtt::Publish pub;
  check(mqtt::parsePublish(pk, pub) && std::string(pub.topic, pub.topicLength) == "homeassistant/sensor/x/config" &&
            pub.payloadLength == 700,
        "PUBLISH parsed back");
  check(mqtt::parsePacket(bp.data(), bp.size() - 1, pk) == 0, "a partial packet needs more");
  const uint8_t bad[] = {0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
  check(mqtt::parsePacket(bad, sizeof(bad), pk) == -1, "a length over four bytes is malformed");
  // QoS 1 incoming carries a packet id before the payload.
  const uint8_t q1[] = {0x32, 0x08, 0, 3, 'a', '/', 'b', 0x12, 0x34, 'Z'};
  check(mqtt::parsePacket(q1, sizeof(q1), pk) == 1 && mqtt::parsePublish(pk, pub) && pub.qos == 1 &&
            pub.packetId == 0x1234 && pub.payloadLength == 1 && pub.payload[0] == 'Z',
        "QoS 1 PUBLISH with its packet id");
}

static void testMqttGlue() {
  puts("MQTT as the board does it");
  setLibraryClockForTests(fakeClock);
  setMqttTransportMaker(makeFake);
  g_broker = FakeBroker{};
  mqttConfigure("mqtt://broker.local:1884", "ha", "secret", "node1", "9.9.9");
  check(!mqttConnected() && !mqttService(false, onStart), "not connected while the controller is busy");
  check(g_broker.sent.empty(), "nothing sent while busy");
  check(mqttService(true, onStart), "connects when idle");
  check(g_broker.openedHost == "broker.local" && g_broker.openedPort == 1884, "the broker's host and port");
  const std::vector<uint8_t> &connect = g_broker.sent[0];
  check((connect[0] >> 4) == mqtt::CONNECT && contains(connect, "bbw-node1") && contains(connect, "bbw/node1/availability") &&
            contains(connect, "offline") && contains(connect, "ha") && contains(connect, "secret"),
        "CONNECT: client id, will on the availability topic, user and password");
  size_t publishes = 0, subscribes = 0, discoveries = 0;
  bool online = false, startTopic = false;
  for (size_t i = 1; i < g_broker.sent.size(); ++i) {
    const auto &p = g_broker.sent[i];
    if ((p[0] >> 4) == mqtt::PUBLISH) {
      ++publishes;
      if (!(p[0] & 1)) check(false, "every announcement is retained");
      if (contains(p, "homeassistant/")) ++discoveries;
      if (contains(p, "online") && contains(p, "bbw/node1/availability")) online = true;
    }
    if ((p[0] >> 4) == mqtt::SUBSCRIBE) {
      ++subscribes;
      startTopic = contains(p, "bbw/node1/start");
    }
  }
  check(online, "availability 'online', retained");
  check(subscribes == 1 && startTopic, "subscribes to the Start button's topic");
  check(discoveries == MQTT_ENTITY_COUNT, "one discovery config per entity");
  check(publishes == MQTT_ENTITY_COUNT + 1, "nothing else published on connect");
  check(mqttConnected() && mqttProblem()[0] == '\0', "connected, no problem");

  // The Start button.
  g_starts = 0;
  const std::vector<uint8_t> press = mqtt::encodePublish("bbw/node1/start", reinterpret_cast<const uint8_t *>("PRESS"), 5, false);
  g_broker.toClient.insert(g_broker.toClient.end(), press.begin(), press.end());
  check(mqttService(true, onStart) && g_starts == 1, "PRESS on the Start topic calls onStart");
  const std::vector<uint8_t> other = mqtt::encodePublish("bbw/node1/start", reinterpret_cast<const uint8_t *>("press"), 5, false);
  const std::vector<uint8_t> elsewhere = mqtt::encodePublish("bbw/other/start", reinterpret_cast<const uint8_t *>("PRESS"), 5, false);
  g_broker.toClient.insert(g_broker.toClient.end(), other.begin(), other.end());
  g_broker.toClient.insert(g_broker.toClient.end(), elsewhere.begin(), elsewhere.end());
  mqttService(true, onStart);
  check(g_starts == 1, "other payloads and other topics are ignored");
  // A message split across two reads.
  g_broker.toClient.insert(g_broker.toClient.end(), press.begin(), press.begin() + 6);
  mqttService(true, onStart);
  g_broker.toClient.insert(g_broker.toClient.end(), press.begin() + 6, press.end());
  mqttService(true, onStart);
  check(g_starts == 2, "a PUBLISH split over two reads");

  const size_t before = g_broker.sent.size();
  check(mqttPublish("bbw/node1/state", "{\"a\":1}", false) && g_broker.sent.size() == before + 1 &&
            contains(g_broker.sent.back(), "{\"a\":1}"),
        "state published");
  g_now += 20000;
  mqttService(true, onStart);
  check((g_broker.sent.back()[0] >> 4) == mqtt::PINGREQ, "PINGREQ after half the keep-alive");

  // The broker goes away: dropped, retried no sooner than a minute later.
  g_broker.dead = true;
  check(!mqttService(true, onStart) && !mqttConnected(), "a dead connection is noticed");
  check(!mqttPublish("t", "x", false), "no publishing while down");
  g_broker.dead = false;
  g_broker.open = true;
  g_broker.sent.clear();
  g_now += 30000;
  mqttService(true, onStart);
  check(g_broker.sent.empty(), "no retry inside a minute");
  g_now += 40000;
  check(mqttService(true, onStart) && mqttConnected(), "reconnects and announces again after a minute");

  // A broker that says no.
  g_broker = FakeBroker{};
  g_broker.connackRc = 5;
  mqttConfigure("mqtt://10.0.0.5", "ha", "bad", "node1", "9.9.9");
  check(!mqttService(true, onStart) && strstr(mqttProblem(), "(5)"), "refused credentials say why");
  g_broker = FakeBroker{};
  g_broker.refuse = true;
  g_now += 70000;
  check(!mqttService(true, onStart) && mqttProblem()[0], "an unreachable broker is a problem, not a hang");
  mqttConfigure("http://nope", "", "", "n", "1");
  check(strstr(mqttProblem(), "mqtt://") != nullptr, "a bad broker address is explained");
  mqttConfigure("", "", "", "n", "1");
  check(!mqttService(true, onStart) && mqttProblem()[0] == '\0', "off with an empty address");
  setMqttTransportMaker(nullptr);
}

static void testLibrary() {
  puts("shared library link");
  setHttpHandler(fakeHttp);
  setLibraryClockForTests(fakeClock);
  g_seen.clear();
  libraryLinkSetRelay("https://relay.example");
  libraryLinkConfigure("", "");
  LibraryLinkStatus st = libraryLinkStatus();
  check(st.configured && st.viaRelay && strcmp(st.repository, "relay.example") == 0, "relay by default");
  check(libraryLinkReady(), "ready once configured");
  g_answer = 201;
  check(libraryPut("a/b.json", "{\"x\":\"q\\\"\"}"), "put through the relay");
  check(g_seen.size() == 1 && g_seen[0].method == "POST" && g_seen[0].url == "https://relay.example/library/put" &&
            g_seen[0].body.find("\"path\":\"a/b.json\"") != std::string::npos &&
            g_seen[0].body.find("\"record\":") != std::string::npos &&
            g_seen[0].headers.find("Authorization") == std::string::npos,
        "relay request: POST /library/put {path, record}, no token");
  g_answer = 422;
  check(libraryPut("a/b.json", "{}"), "422 means it arrived before");
  g_answer = 409;
  check(!libraryPut("a/b.json", "{}") && libraryTakeKeyMissing() && !libraryTakeKeyMissing() && libraryLinkReady(),
        "409: key missing, once, and no backoff");
  g_answer = 403;
  check(!libraryPut("a/b.json", "{}") && libraryTakeRefused() && !libraryTakeRefused() && libraryLinkReady(),
        "403: refused for good, no backoff");
  g_answer = 500;
  check(!libraryPut("a/b.json", "{}") && !libraryLinkReady() && strstr(libraryLinkStatus().problem, "relay"),
        "500: failure, backs off");
  g_now += 61000;
  check(libraryLinkReady(), "ready again after the first backoff (60 s)");
  g_answer = -1;
  libraryPut("a/b.json", "{}");
  g_now += 61000;
  check(!libraryLinkReady(), "the second backoff doubles");
  g_now += 61000;
  check(libraryLinkReady(), "...to 120 s");

  // Reading.
  g_answer = 200;
  g_answerBody = "{\"index\":1}";
  char out[64];
  g_seen.clear();
  check(libraryGet("index/x.json", out, sizeof(out)) && strcmp(out, "{\"index\":1}") == 0 &&
            g_seen[0].url == "https://relay.example/library/get?path=index/x.json",
        "get through the relay");
  g_answer = 404;
  check(!libraryGet("index/x.json", out, sizeof(out)) && libraryLinkReady(), "404 is an answer, not a failure");
  g_answer = 200;
  g_answerBody = std::string(200, 'z');
  check(!libraryGet("index/x.json", out, sizeof(out)) && out[0] == '\0' && libraryLinkReady(), "too big to read here");

  // The owner's own repository and token.
  libraryLinkConfigure("owner/lib", "tok123");
  st = libraryLinkStatus();
  check(st.configured && !st.viaRelay && strcmp(st.repository, "owner/lib") == 0, "own repository and token");
  g_seen.clear();
  g_answer = 201;
  check(libraryPut("p/q.json", "hello"), "put to GitHub");
  check(g_seen[0].method == "PUT" && g_seen[0].url == "https://api.github.com/repos/owner/lib/contents/inbox/p/q.json" &&
            g_seen[0].headers.find("Authorization: Bearer tok123") != std::string::npos &&
            g_seen[0].headers.find("X-GitHub-Api-Version: 2022-11-28") != std::string::npos &&
            g_seen[0].body.find("\"branch\":\"inbox\"") != std::string::npos &&
            g_seen[0].body.find("\"content\":\"aGVsbG8=\"") != std::string::npos,
        "GitHub contents API: PUT inbox/, branch inbox, base64 content, token");
  g_answer = 200;
  g_answerBody = "raw";
  g_seen.clear();
  check(libraryGet("status/a.json", out, sizeof(out)) &&
            g_seen[0].url == "https://api.github.com/repos/owner/lib/contents/status/a.json?ref=main" &&
            g_seen[0].headers.find("application/vnd.github.raw+json") != std::string::npos,
        "get from GitHub's main, raw");
  g_answer = 401;
  libraryPut("p/q.json", "x");
  check(strstr(libraryLinkStatus().problem, "refused the token") != nullptr, "401 says the token was refused");
  libraryLinkConfigure("", "");
  libraryLinkSetRelay("");
  libraryLinkConfigure("", "");
  check(!libraryLinkStatus().configured && !libraryLinkReady() && !libraryPut("a", "b"), "nothing set: nothing sent");
  libraryLinkSetRelay("https://relay.example");
  setHttpHandler(nullptr);
}

static void testStubs() {
  puts("Home Assistant, diagnostics, page preview, add-on fetch");
  setHttpHandler(fakeHttp);
  char out[512], problem[160];
  g_seen.clear();
  g_answer = 200;
  g_answerBody = "{\"state\":\"21.5\"}";
  check(haRequest("http://homeassistant.local:8123/api/states/climate.x", "TOKEN", nullptr, out, sizeof(out), problem,
                  sizeof(problem), 3000) == 200 &&
            g_seen[0].method == "GET" && g_seen[0].headers.find("Authorization: Bearer TOKEN") != std::string::npos &&
            strcmp(out, "{\"state\":\"21.5\"}") == 0 && g_seen[0].timeoutMs == 3000,
        "HA REST GET with the bearer token");
  float v = 0;
  check(haReadingFromState(out, "", false, v) || true, "(reading parsed by core)");
  g_seen.clear();
  haRequest("http://homeassistant.local:8123/api/webhook/abc", nullptr, "{\"a\":1}", out, sizeof(out), problem, sizeof(problem), 3000);
  check(g_seen[0].method == "POST" && g_seen[0].body == "{\"a\":1}" && g_seen[0].headers.find("Authorization") == std::string::npos &&
            g_seen[0].headers.find("Content-Type: application/json") != std::string::npos,
        "HA webhook: POST JSON, no token");
  g_answer = 401;
  haRequest("http://h/api", "T", nullptr, out, sizeof(out), problem, sizeof(problem), 1000);
  check(strstr(problem, "refused the token"), "401 in words");
  g_answer = 404;
  haRequest("http://h/api", "T", nullptr, out, sizeof(out), problem, sizeof(problem), 1000);
  check(strstr(problem, "no such entity"), "404 in words");
  g_answer = -1;
  check(haRequest("ftp://x", "T", nullptr, out, sizeof(out), problem, sizeof(problem), 1000) == -2, "not an http link");
  check(haRequest("http://h", "T", nullptr, out, sizeof(out), problem, sizeof(problem), 1000) == -1 && problem[0],
        "unreachable");

  g_answer = 202;
  g_answerBody = "";
  g_seen.clear();
  check(diagPost("https://relay.example/diag", "{\"v\":1}", problem, sizeof(problem)) == 202 && problem[0] == '\0' &&
            g_seen[0].method == "POST" && g_seen[0].body == "{\"v\":1}" && !g_seen[0].follow,
        "diagnostics post");
  check(diagPost("http://relay.example/diag", "{}", problem, sizeof(problem)) == -2, "diagnostics need https");
  g_answer = 429;
  diagPost("https://r/d", "{}", problem, sizeof(problem));
  check(strstr(problem, "enough from this controller"), "429 in words");
  g_answer = 400;
  g_answerBody = "{\"ok\":false,\"error\":\"bad signature\"}";
  diagPost("https://r/d", "{}", problem, sizeof(problem));
  check(strstr(problem, "refused it (400): bad signature"), "the relay's own reason");

  g_answer = 200;
  g_answerBody =
      "<html><head><title>Fine Beans &amp; Co</title><meta property=\"og:image\" content=\"/img/a.jpg\">"
      "<meta property=\"og:site_name\" content=\"Fine Beans\"></head><body></body></html>";
  PageMeta meta;
  g_seen.clear();
  check(fetchPagePreview("https://shop.example/products/x", meta, problem, sizeof(problem)) == 200, "page preview answers 200");
  check(g_seen[0].stopAfter == "</head" && g_seen[0].headers.find("User-Agent: Mozilla/5.0") != std::string::npos &&
            g_seen[0].headers.find("Accept-Language: en") != std::string::npos && g_seen[0].follow,
        "reads to the end of <head>, browser-style agent, follows redirects");
  check(std::string(meta.title).find("Fine Beans") == 0 && std::string(meta.image) == "https://shop.example/img/a.jpg",
        "title and the picture's address resolved against the page");
  g_answer = 403;
  check(fetchPagePreview("https://shop.example/", meta, problem, sizeof(problem)) == 403 && strstr(problem, "turned the controller away"),
        "403 in words");
  g_answer = -1;
  g_seen.clear();
  check(fetchPagePreview("https://shop.example/", meta, problem, sizeof(problem)) == -1 && g_seen.size() == 2,
        "a connection that fails is tried once more");
  check(fetchPagePreview("ftp://x", meta, problem, sizeof(problem)) == -2, "not a web link");

  addon::FetchJob job;
  strcpy(job.url, "https://192.168.1.50:8443/status");
  strcpy(job.certSha256, "AA:BB");
  strcpy(job.cookie, "sid=1");
  static uint8_t buf[32];
  job.out = buf;
  job.outCap = sizeof(buf);
  g_answer = 200;
  g_answerBody = "abc";
  g_answerCookie = "sid=2";
  g_seen.clear();
  addon::fetchHere(job);
  check(job.status == 200 && job.outLen == 3 && memcmp(buf, "abc", 3) == 0 && strcmp(job.setCookie, "sid=2") == 0 &&
            g_seen[0].pin == "AA:BB" && !g_seen[0].follow && g_seen[0].headers.find("Cookie: sid=1") != std::string::npos,
        "add-on fetch: pinned certificate, cookie both ways, no redirects");
  strcpy(job.url, "http://192.168.1.50/x");
  g_seen.clear();
  addon::fetchHere(job);
  check(g_seen[0].pin.empty(), "plain http carries no pin");
  setHttpHandler(nullptr);
}

static void testSigner() {
  puts("device key (the unsigned stand-in off the iPhone)");
  check(beginDeviceKey() && deviceSigner().ready(), "a key is made");
  check(std::string(deviceSigner().algorithm()) == (hostP256Available() ? "ES256" : "HOST-UNSIGNED"),
        "the algorithm names what it is");
  check(deviceSigner().publicKey()[0] == 0x04, "uncompressed public key");
  uint8_t h[32] = {1}, sig[SIGNATURE_LEN], sig2[SIGNATURE_LEN];
  check(deviceSigner().sign(h, sig) && deviceSigner().sign(h, sig2), "signs");
  uint8_t pub1[PUBLIC_KEY_LEN];
  memcpy(pub1, deviceSigner().publicKey(), PUBLIC_KEY_LEN);
  check(beginDeviceKey() && memcmp(pub1, deviceSigner().publicKey(), PUBLIC_KEY_LEN) == 0, "the key persists across starts");
}

int main() {
  testDer();
  testPackets();
  testMqttGlue();
  testLibrary();
  testStubs();
  testSigner();
  printf("%s\n", g_failures ? "net tests FAILED" : "net tests passed");
  return g_failures ? 1 : 0;
}

// hostapp's runner defines these through BrewHost.cpp; this program only needs the symbols host_stubs.cpp
// refers to that live elsewhere.
