#pragma once
// Home Assistant and the room's climate (docs/HOME_ASSISTANT.md).
//
// The controller can work with Home Assistant four ways, every one off until it is set up:
//   * it reads the room's humidity and temperature from two Home Assistant entities (REST API,
//     GET /api/states/<entity_id> with a long-lived access token), or takes them pushed to
//     POST /api/ambient by anything else; each new shot's context records the reading;
//   * it posts each finished shot to a Home Assistant webhook (an automation trigger);
//   * it publishes itself to an MQTT broker with Home Assistant's discovery, as a device with
//     sensors and a Start button (which obeys the web page's "start from the browser" switch);
//   * Home Assistant polls its JSON API (a `rest:` sensor, or the custom integration in
//     integrations/homeassistant).
// What is here is portable and host-tested: checking what the owner typed, reading a state, and
// building the payloads. The requests themselves are platform/HomeAssistantLink.
//
// Humidity is recorded, not used: no controlled study ties it to shot time, and published advice
// disagrees even on the direction (research/grind-logic-review.md section 8).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stddef.h>
#include <stdint.h>

namespace bbw {

constexpr size_t HA_URL_LEN = 96;
constexpr size_t HA_TOKEN_LEN = 256;
constexpr size_t HA_ENTITY_LEN = 64;
constexpr size_t HA_WEBHOOK_LEN = 64;
constexpr size_t MQTT_URL_LEN = 96;
constexpr size_t MQTT_USER_LEN = 48;
constexpr size_t MQTT_PASSWORD_LEN = 64;

// A reading is stamped on a shot while it is this fresh.
constexpr uint32_t AMBIENT_FRESH_S = 30u * 60u;
// How often the controller reads Home Assistant while idle.
constexpr uint32_t AMBIENT_PULL_EVERY_MS = 5u * 60u * 1000u;

enum class AmbientSource : uint8_t { NONE = 0, HOME_ASSISTANT = 1, PUSHED = 2 };

inline const char *ambientSourceName(AmbientSource s) {
  switch (s) {
    case AmbientSource::HOME_ASSISTANT: return "ha";
    case AmbientSource::PUSHED: return "push";
    case AmbientSource::NONE: break;
  }
  return "none";
}

// The room's latest reading. A value not known is NAN.
struct AmbientReading {
  float rhPct = NAN;
  float tempC = NAN;
  uint32_t epochS = 0;      // when it was read; 0 = never
  uint32_t atMs = 0;        // millis() then, for a reading taken before the clock was set
  AmbientSource source = AmbientSource::NONE;
};

inline bool ambientHasValue(const AmbientReading &r) {
  return r.source != AmbientSource::NONE && (!std::isnan(r.rhPct) || !std::isnan(r.tempC));
}

// Fresh enough to stamp on a shot made at nowMs.
inline bool ambientFresh(const AmbientReading &r, uint32_t nowMs) {
  return ambientHasValue(r) && nowMs - r.atMs <= AMBIENT_FRESH_S * 1000u;
}

// Tenths, for a record; INT16_MIN for a value not known.
constexpr int16_t AMBIENT_NONE_X10 = INT16_MIN;
inline int16_t ambientX10(float v) {
  if (std::isnan(v)) return AMBIENT_NONE_X10;
  const float t = v * 10.0f;
  if (t > 32000.0f || t < -32000.0f) return AMBIENT_NONE_X10;
  return static_cast<int16_t>(lroundf(t));
}

// The plausible range of each.
inline bool ambientRhOk(float rh) { return !std::isnan(rh) && rh >= 0.0f && rh <= 100.0f; }
inline bool ambientTempOk(float c) { return !std::isnan(c) && c >= -40.0f && c <= 70.0f; }

// The room's slow temperature (9 Nov 2026): what the beans and the grinder have taken on. A running
// average of the readings with a 12 h time constant. On the board's real shots (Oct-Nov 2026) the
// room's mean over the 8-24 h before a shot told its time better than the reading at the shot.
constexpr float ROOM_EFF_TAU_S = 12.0f * 3600.0f;
struct RoomEffective {
  float c = NAN;          // degrees C; NAN until the first reading
  uint32_t epochS = 0;    // of the last reading taken in
};
// One reading. Older than the last, or without a clock: left out. After a gap of more than two
// time constants the old figure says nothing any more and the reading starts it again.
inline void roomEffectiveUpdate(RoomEffective &r, float tempC, uint32_t epochS) {
  if (!ambientTempOk(tempC) || epochS == 0) return;
  if (std::isnan(r.c) || r.epochS == 0 || epochS - r.epochS > static_cast<uint32_t>(2.0f * ROOM_EFF_TAU_S)) {
    if (!std::isnan(r.c) && r.epochS != 0 && epochS <= r.epochS) return;
    r.c = tempC;
    r.epochS = epochS;
    return;
  }
  if (epochS <= r.epochS) return;
  const float w = 1.0f - expf(-static_cast<float>(epochS - r.epochS) / ROOM_EFF_TAU_S);
  r.c += w * (tempC - r.c);
  r.epochS = epochS;
}

// A Home Assistant entity id: domain.object_id, lower-case letters, digits and underscores.
inline bool haEntityIdOk(const char *id) {
  if (!id || !id[0] || strlen(id) >= HA_ENTITY_LEN) return false;
  int dots = 0;
  bool before = false, after = false;
  for (const char *p = id; *p; ++p) {
    const char c = *p;
    if (c == '.') {
      if (++dots > 1 || !before) return false;
      continue;
    }
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    (dots ? after : before) = true;
  }
  return dots == 1 && after;
}

// An http:// or https:// address with a host and nothing that would break a request line.
// Trailing slashes are dropped. false: not usable; out untouched.
inline bool haBaseUrl(const char *in, char *out, size_t size) {
  if (!in) return false;
  const bool plain = strncmp(in, "http://", 7) == 0, tls = strncmp(in, "https://", 8) == 0;
  if (!plain && !tls) return false;
  size_t n = strlen(in);
  while (n > 0 && in[n - 1] == '/') --n;
  const size_t scheme = tls ? 8 : 7;
  if (n <= scheme || n >= size) return false;
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    if (c < 0x21 || c == 0x7F || c == '"' || c == '\\' || c == '<' || c == '>') return false;
  }
  memcpy(out, in, n);
  out[n] = '\0';
  return true;
}

// The host of a scheme://[user@]host[:port][/...] address, lower case. false: no host.
inline bool urlHostOf(const char *url, char *out, size_t size) {
  if (!url || size == 0) return false;
  const char *h = strstr(url, "://");
  if (!h) return false;
  h += 3;
  const char *end = h + strcspn(h, "/?#");
  const char *at = static_cast<const char *>(memchr(h, '@', static_cast<size_t>(end - h)));
  if (at) h = at + 1;
  const char *colon = static_cast<const char *>(memchr(h, ':', static_cast<size_t>(end - h)));
  if (colon) end = colon;
  const size_t n = static_cast<size_t>(end - h);
  if (n == 0 || n >= size) return false;
  for (size_t i = 0; i < n; ++i) out[i] = (h[i] >= 'A' && h[i] <= 'Z') ? static_cast<char>(h[i] + 32) : h[i];
  out[n] = '\0';
  return true;
}

// A stored token or password may stay only while its address keeps the same host: one that moves
// to another host (or is switched off) must be entered again, so a changed address never receives
// the old credential (audit G2, 10 Nov 2026).
inline bool credentialFollowsUrl(const char *oldUrl, const char *newUrl) {
  char a[96], b[96];
  return urlHostOf(oldUrl, a, sizeof(a)) && urlHostOf(newUrl, b, sizeof(b)) && strcmp(a, b) == 0;
}

// <base>/api/states/<entity>. false: either is not usable.
inline bool haStateUrl(const char *base, const char *entity, char *out, size_t size) {
  char b[HA_URL_LEN];
  if (!haBaseUrl(base, b, sizeof(b)) || !haEntityIdOk(entity)) return false;
  const int n = snprintf(out, size, "%s/api/states/%s", b, entity);
  return n > 0 && static_cast<size_t>(n) < size;
}

// A webhook id as Home Assistant makes them: letters, digits, '-' and '_'.
inline bool haWebhookIdOk(const char *id) {
  if (!id || !id[0] || strlen(id) >= HA_WEBHOOK_LEN) return false;
  for (const char *p = id; *p; ++p) {
    const char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
      return false;
    }
  }
  return true;
}

inline bool haWebhookUrl(const char *base, const char *id, char *out, size_t size) {
  char b[HA_URL_LEN];
  if (!haBaseUrl(base, b, sizeof(b)) || !haWebhookIdOk(id)) return false;
  const int n = snprintf(out, size, "%s/api/webhook/%s", b, id);
  return n > 0 && static_cast<size_t>(n) < size;
}

// A Home Assistant state, as the REST API returns it ("state" and the attribute
// "unit_of_measurement"), as a humidity in percent or a temperature in degrees C.
// false: unavailable, unknown, not a number, another unit, or out of range.
inline bool haReadingFromState(const char *state, const char *unit, bool humidity, float &value) {
  if (!state || !state[0] || strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0) return false;
  char *end = nullptr;
  const float v = strtof(state, &end);
  if (end == state || (end && *end != '\0') || std::isnan(v) || std::isinf(v)) return false;
  const char *u = unit ? unit : "";
  if (humidity) {
    if (u[0] && strcmp(u, "%") != 0) return false;
    if (!ambientRhOk(v)) return false;
    value = v;
    return true;
  }
  float c = v;
  // "\xC2\xB0" is the degree sign in UTF-8.
  if (strcmp(u, "\xC2\xB0" "F") == 0 || strcmp(u, "F") == 0) {
    c = (v - 32.0f) * 5.0f / 9.0f;
  } else if (strcmp(u, "K") == 0) {
    c = v - 273.15f;
  } else if (u[0] && strcmp(u, "\xC2\xB0" "C") != 0 && strcmp(u, "C") != 0) {
    return false;
  }
  if (!ambientTempOk(c)) return false;
  value = c;
  return true;
}

// ------------------------------------------------------------------ MQTT --
// mqtt://host[:port]: the broker. false: not usable. Port 1883 when not given.
inline bool mqttBrokerFromUrl(const char *url, char *host, size_t hostSize, uint16_t &port) {
  if (!url || strncmp(url, "mqtt://", 7) != 0) return false;
  const char *h = url + 7;
  const char *colon = strchr(h, ':');
  const char *slash = strchr(h, '/');
  const char *end = colon ? colon : (slash ? slash : h + strlen(h));
  const size_t n = static_cast<size_t>(end - h);
  if (n == 0 || n >= hostSize) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = h[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) {
      return false;
    }
  }
  memcpy(host, h, n);
  host[n] = '\0';
  port = 1883;
  if (colon) {
    char *stop = nullptr;
    const long p = strtol(colon + 1, &stop, 10);
    if (stop == colon + 1 || (*stop != '\0' && *stop != '/') || p < 1 || p > 65535) return false;
    port = static_cast<uint16_t>(p);
  } else if (slash && slash[1] != '\0') {
    return false;
  }
  return true;
}

constexpr const char *MQTT_DISCOVERY_PREFIX = "homeassistant";

// The controller's topics: bbw/<node>/<leaf>. node: a short id unique to this controller.
inline bool mqttTopic(const char *node, const char *leaf, char *out, size_t size) {
  const int n = snprintf(out, size, "bbw/%s/%s", node, leaf);
  return n > 0 && static_cast<size_t>(n) < size;
}

// One entity the controller publishes. The state of every sensor is one JSON object on
// bbw/<node>/state; each takes its field with a value template.
struct MqttEntity {
  const char *component;   // "sensor" or "button"
  const char *objectId;
  const char *name;
  const char *field;       // in the state JSON (sensors)
  const char *unit;        // nullptr: none
  const char *deviceClass; // nullptr: none
  const char *icon;
};

constexpr MqttEntity MQTT_ENTITIES[] = {
    {"sensor", "state", "Machine state", "state", nullptr, nullptr, "mdi:coffee-maker"},
    {"sensor", "last_shot_time", "Last shot time", "last_shot_time_s", "s", "duration", "mdi:timer-outline"},
    {"sensor", "last_shot_weight", "Last shot weight", "last_shot_weight_g", "g", "weight", "mdi:scale"},
    {"sensor", "last_shot_grind", "Last shot grind", "last_shot_grind", nullptr, nullptr, "mdi:cog"},
    {"sensor", "advice_action", "Grind advice", "advice_action", nullptr, nullptr, "mdi:tune-variant"},
    {"sensor", "advice_grind", "Advised grind", "advice_grind", nullptr, nullptr, "mdi:cog-transfer"},
    {"sensor", "shots_today", "Shots today", "shots_today", nullptr, nullptr, "mdi:counter"},
    {"button", "start", "Start shot", nullptr, nullptr, nullptr, "mdi:play"},
};
constexpr size_t MQTT_ENTITY_COUNT = sizeof(MQTT_ENTITIES) / sizeof(MQTT_ENTITIES[0]);

// homeassistant/<component>/<node>/<object>/config
inline bool mqttDiscoveryTopic(const MqttEntity &e, const char *node, char *out, size_t size) {
  const int n = snprintf(out, size, "%s/%s/%s/%s/config", MQTT_DISCOVERY_PREFIX, e.component, node, e.objectId);
  return n > 0 && static_cast<size_t>(n) < size;
}

// The retained discovery config of one entity. Every one names the device (one per controller),
// the availability topic (bbw/<node>/availability, "online" / "offline"), and either the state
// topic with a template (sensors) or the command topic (the button). firmware: shown on the device.
inline size_t mqttDiscoveryConfig(const MqttEntity &e, const char *node, const char *firmware, char *out,
                                  size_t size) {
  char avail[64], state[64], command[64];
  mqttTopic(node, "availability", avail, sizeof(avail));
  mqttTopic(node, "state", state, sizeof(state));
  mqttTopic(node, "start", command, sizeof(command));
  int n = snprintf(out, size,
                   "{\"name\":\"%s\",\"unique_id\":\"bbw_%s_%s\",\"object_id\":\"brew_controller_%s\","
                   "\"availability_topic\":\"%s\",\"icon\":\"%s\",",
                   e.name, node, e.objectId, e.objectId, avail, e.icon);
  if (n <= 0 || static_cast<size_t>(n) >= size) return 0;
  size_t used = static_cast<size_t>(n);
  auto add = [&](const char *fmt, const char *a) {
    if (used >= size) return;
    const int k = snprintf(out + used, size - used, fmt, a);
    used = k > 0 ? used + static_cast<size_t>(k) : size;
  };
  if (strcmp(e.component, "button") == 0) {
    add("\"command_topic\":\"%s\",\"payload_press\":\"PRESS\",", command);
  } else {
    add("\"state_topic\":\"%s\",", state);
    add("\"value_template\":\"{{ value_json.%s }}\",", e.field);
    if (e.unit) add("\"unit_of_measurement\":\"%s\",", e.unit);
    if (e.deviceClass) add("\"device_class\":\"%s\",", e.deviceClass);
    if (e.unit) add("\"state_class\":\"%s\",", "measurement");
  }
  add("\"device\":{\"identifiers\":[\"bbw_%s\"],", node);
  add("\"name\":\"%s\",\"manufacturer\":\"Brew Controller\",\"model\":\"Brew by weight\",", "Brew Controller");
  add("\"sw_version\":\"%s\"}}", firmware ? firmware : "");
  if (used >= size) return 0;
  return used;
}

// The state JSON the sensors read. A value not known is null.
struct MqttState {
  const char *state = "idle";
  float lastShotTimeS = NAN;
  float lastShotWeightG = NAN;
  const char *lastShotGrind = nullptr;   // as the dial reads, e.g. "7.6"
  const char *adviceAction = nullptr;
  const char *adviceGrind = nullptr;
  int shotsToday = -1;
};

inline size_t mqttStateJson(const MqttState &s, char *out, size_t size) {
  char t[16], w[16], st[24], g[40], aa[24], ag[40], today[12];
  auto num = [](float v, char *buf, size_t n) {
    if (std::isnan(v)) snprintf(buf, n, "null");
    else snprintf(buf, n, "%.1f", static_cast<double>(v));
  };
  auto str = [](const char *v, char *buf, size_t n) {
    if (!v) { snprintf(buf, n, "null"); return; }
    size_t k = 0;
    buf[k++] = '"';
    for (const char *p = v; *p && k + 2 < n; ++p) {
      if (*p == '"' || *p == '\\' || static_cast<unsigned char>(*p) < 0x20) continue;
      buf[k++] = *p;
    }
    buf[k++] = '"';
    buf[k] = '\0';
  };
  num(s.lastShotTimeS, t, sizeof(t));
  num(s.lastShotWeightG, w, sizeof(w));
  str(s.state, st, sizeof(st));
  str(s.lastShotGrind, g, sizeof(g));
  str(s.adviceAction, aa, sizeof(aa));
  str(s.adviceGrind, ag, sizeof(ag));
  if (s.shotsToday < 0) snprintf(today, sizeof(today), "null");
  else snprintf(today, sizeof(today), "%d", s.shotsToday);
  const int n = snprintf(out, size,
                         "{\"state\":%s,\"last_shot_time_s\":%s,\"last_shot_weight_g\":%s,\"last_shot_grind\":%s,"
                         "\"advice_action\":%s,\"advice_grind\":%s,\"shots_today\":%s}",
                         st, t, w, g, aa, ag, today);
  return n > 0 && static_cast<size_t>(n) < size ? static_cast<size_t>(n) : 0;
}

}  // namespace bbw
