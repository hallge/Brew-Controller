// The board's side of HomeAssistantLink.h. Not built for the emulator.
#ifndef BBW_EMULATOR

#include "HomeAssistantLink.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include <memory>

#include "Log.h"
#include "NetWorker.h"
#include "SafeTls.h"
#include "BigMem.h"
#include "core/HomeAssistant.h"

// Mozilla's root certificates in ESP-IDF's bundle format, linked in by board_build.embed_files.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_certs_x509_crt_bundle_bin_start");

namespace bbw {

namespace {

void say(char *problem, size_t size, const char *text) {
  if (problem && size) strlcpy(problem, text, size);
}

// http://name.local[:port]/...: the board's DNS does not answer .local names, so the name is asked
// for over mDNS and the address put in its place. A name found is kept for ten minutes.
bool resolveLocal(const char *url, char *out, size_t size) {
  const char *host = strstr(url, "://");
  if (!host) return false;
  host += 3;
  const char *end = host;
  while (*end && *end != ':' && *end != '/') ++end;
  const size_t n = static_cast<size_t>(end - host);
  if (n < 7 || n > 63 || strncmp(end - 6, ".local", 6) != 0) return false;
  static char cachedName[64];
  static IPAddress cachedIp;
  static uint32_t cachedAtMs = 0;
  char name[64];
  memcpy(name, host, n - 6);
  name[n - 6] = '\0';
  IPAddress ip;
  if (cachedAtMs && strcmp(cachedName, name) == 0 && millis() - cachedAtMs < 600000u) {
    ip = cachedIp;
  } else {
    ip = MDNS.queryHost(name, 2000);
    if (ip == IPAddress(0, 0, 0, 0)) return false;
    strlcpy(cachedName, name, sizeof(cachedName));
    cachedIp = ip;
    cachedAtMs = millis() ? millis() : 1;
  }
  const int k = snprintf(out, size, "%.*s%s%s", static_cast<int>(host - url), url, ip.toString().c_str(), end);
  return k > 0 && static_cast<size_t>(k) < size;
}

}  // namespace

int haRequest(const char *url, const char *token, const char *body, char *out, size_t outSize, char *problem,
              size_t problemSize, uint16_t timeoutMs) {
  say(problem, problemSize, "");
  if (out && outSize) out[0] = '\0';
  if (!url || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)) {
    say(problem, problemSize, "the address is not an http:// or https:// link");
    return -2;
  }
  if (WiFi.status() != WL_CONNECTED) {
    say(problem, problemSize, "not on a Wi-Fi network");
    return -1;
  }
  // An add-on's network job is running (NetWorker.h): skip this turn rather than open a second
  // connection beside it.
  if (netAddonBusy()) {
    say(problem, problemSize, "the network is busy; trying again shortly");
    return -1;
  }
  const bool tls = strncmp(url, "https://", 8) == 0;
  // The buffers here are taken from PSRAM the first time they are needed, so Home Assistant
  // costs no memory inside the chip while it is not set up.
  constexpr size_t RESOLVED_SIZE = 256;
  static char *resolved = bigArray<char>(RESOLVED_SIZE);
  const char *target = resolveLocal(url, resolved, RESOLVED_SIZE) ? resolved : url;
  std::unique_ptr<WiFiClient> plain;
  std::unique_ptr<SafeTlsClient> secure;
  HTTPClient http;
  http.setConnectTimeout(timeoutMs);
  http.setTimeout(timeoutMs);
  http.setReuse(false);
  bool begun;
  if (tls) {
    if (time(nullptr) < 1700000000) {
      say(problem, problemSize, "the clock is not set yet (certificates cannot be checked)");
      return -1;
    }
    secure.reset(new SafeTlsClient());
    secure->setCACertBundle(rootca_crt_bundle_start);
    begun = http.begin(*secure, target);
  } else {
    plain.reset(new WiFiClient());
    begun = http.begin(*plain, target);
  }
  if (!begun) {
    say(problem, problemSize, "the address could not be used");
    return -2;
  }
  if (token && token[0]) {
    constexpr size_t AUTH_SIZE = HA_TOKEN_LEN + 8;
    static char *auth = bigArray<char>(AUTH_SIZE);
    snprintf(auth, AUTH_SIZE, "Bearer %s", token);
    http.addHeader("Authorization", auth);
  }
  int code;
  if (body) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(body)), strlen(body));
  } else {
    code = http.GET();
  }
  if (code <= 0) {
    say(problem, problemSize, "Home Assistant could not be reached");
    http.end();
    return -1;
  }
  if (out && outSize > 1) {
    // Never more than asked for: the state of one entity is a few hundred bytes.
    WiFiClient *stream = http.getStreamPtr();
    size_t used = 0;
    const uint32_t until = millis() + timeoutMs;
    int left = http.getSize();
    while (stream && used + 1 < outSize && (left < 0 || left > 0) && millis() < until) {
      const int avail = stream->available();
      if (avail <= 0) {
        if (!http.connected()) break;
        delay(1);
        continue;
      }
      size_t want = outSize - 1 - used;
      if (static_cast<size_t>(avail) < want) want = static_cast<size_t>(avail);
      if (left > 0 && static_cast<size_t>(left) < want) want = static_cast<size_t>(left);
      const int got = stream->readBytes(out + used, want);
      if (got <= 0) break;
      used += static_cast<size_t>(got);
      if (left > 0) left -= got;
    }
    out[used] = '\0';
  }
  http.end();
  if (code == 401) say(problem, problemSize, "Home Assistant refused the token");
  else if (code == 404) say(problem, problemSize, "Home Assistant has no such entity or webhook");
  else if (code >= 400) say(problem, problemSize, "Home Assistant answered with an error");
  return code;
}

// ------------------------------------------------------------------ MQTT --
namespace {

WiFiClient g_mqttNet;
PubSubClient g_mqtt(g_mqttNet);
char g_mqttHost[64] = {};
uint16_t g_mqttPort = 1883;
char g_mqttUser[MQTT_USER_LEN] = {};
char g_mqttPassword[MQTT_PASSWORD_LEN] = {};
char g_mqttNode[24] = {};
char g_mqttFirmware[24] = {};
char g_mqttProblem[64] = {};
bool g_mqttOn = false;
bool g_mqttAnnounced = false;
uint32_t g_mqttTriedMs = 0;
void (*g_mqttStart)() = nullptr;
char g_startTopic[64] = {};

void onMessage(char *topic, uint8_t *payload, unsigned int length) {
  if (strcmp(topic, g_startTopic) != 0) return;
  if (length == 5 && memcmp(payload, "PRESS", 5) == 0 && g_mqttStart) g_mqttStart();
}

// The discovery configs, retained: Home Assistant makes the device from them, also after it restarts.
void announce() {
  constexpr size_t TOPIC_SIZE = 128, PAYLOAD_SIZE = 900;
  static char *topic = bigArray<char>(TOPIC_SIZE);
  static char *payload = bigArray<char>(PAYLOAD_SIZE);
  for (size_t i = 0; i < MQTT_ENTITY_COUNT; ++i) {
    if (!mqttDiscoveryTopic(MQTT_ENTITIES[i], g_mqttNode, topic, TOPIC_SIZE)) continue;
    if (!mqttDiscoveryConfig(MQTT_ENTITIES[i], g_mqttNode, g_mqttFirmware, payload, PAYLOAD_SIZE)) continue;
    g_mqtt.publish(topic, payload, true);
  }
  g_mqttAnnounced = true;
}

}  // namespace

void mqttConfigure(const char *url, const char *user, const char *password, const char *node,
                   const char *firmware) {
  if (g_mqtt.connected()) g_mqtt.disconnect();
  g_mqttOn = url && url[0] && mqttBrokerFromUrl(url, g_mqttHost, sizeof(g_mqttHost), g_mqttPort);
  strlcpy(g_mqttUser, user ? user : "", sizeof(g_mqttUser));
  strlcpy(g_mqttPassword, password ? password : "", sizeof(g_mqttPassword));
  strlcpy(g_mqttNode, node ? node : "bbw", sizeof(g_mqttNode));
  strlcpy(g_mqttFirmware, firmware ? firmware : "", sizeof(g_mqttFirmware));
  mqttTopic(g_mqttNode, "start", g_startTopic, sizeof(g_startTopic));
  g_mqttAnnounced = false;
  g_mqttTriedMs = 0;
  g_mqttProblem[0] = '\0';
  if (url && url[0] && !g_mqttOn) strlcpy(g_mqttProblem, "the broker address is not mqtt://host[:port]", sizeof(g_mqttProblem));
  g_mqtt.setServer(g_mqttHost, g_mqttPort);
  // A discovery config is about 600 bytes. The buffer is taken only when a broker is set.
  if (g_mqttOn) g_mqtt.setBufferSize(1024);
  g_mqtt.setSocketTimeout(2);    // seconds: a broker that is gone must not hold up the loop
  g_mqtt.setKeepAlive(30);
  g_mqtt.setCallback(onMessage);
}

bool mqttService(bool idle, void (*onStart)()) {
  g_mqttStart = onStart;
  if (!g_mqttOn) return false;
  if (g_mqtt.connected()) {
    g_mqtt.loop();
    return true;
  }
  if (!idle || WiFi.status() != WL_CONNECTED || netAddonBusy()) return false;
  const uint32_t now = millis();
  if (g_mqttTriedMs && now - g_mqttTriedMs < 60000u) return false;
  g_mqttTriedMs = now ? now : 1;
  char id[40], avail[64];
  snprintf(id, sizeof(id), "bbw-%s", g_mqttNode);
  mqttTopic(g_mqttNode, "availability", avail, sizeof(avail));
  g_mqttNet.setTimeout(2);
  const bool ok = g_mqttUser[0] ? g_mqtt.connect(id, g_mqttUser, g_mqttPassword, avail, 0, true, "offline")
                                : g_mqtt.connect(id, nullptr, nullptr, avail, 0, true, "offline");
  if (!ok) {
    snprintf(g_mqttProblem, sizeof(g_mqttProblem), "the broker did not accept the connection (%d)", g_mqtt.state());
    return false;
  }
  g_mqttProblem[0] = '\0';
  g_mqtt.publish(avail, "online", true);
  g_mqtt.subscribe(g_startTopic);
  announce();
  LOGI("mqtt: connected to the broker, %u entities announced", static_cast<unsigned>(MQTT_ENTITY_COUNT));
  return true;
}

bool mqttConnected() { return g_mqttOn && g_mqtt.connected(); }

bool mqttPublish(const char *topic, const char *payload, bool retain) {
  if (!mqttConnected()) return false;
  return g_mqtt.publish(topic, payload, retain);
}

const char *mqttProblem() { return g_mqttProblem; }

}  // namespace bbw

#endif  // BBW_EMULATOR
