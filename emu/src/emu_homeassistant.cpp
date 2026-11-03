// The emulator's side of src/platform/HomeAssistantLink.h. HTTP goes through the PC's curl (the
// token in a header file, never on the command line). MQTT is not connected in the emulator:
// what the board would publish is written to the log instead, so the payloads can be checked.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/HomeAssistant.h"
#include "emu.h"
#include "platform/HomeAssistantLink.h"

namespace bbw {

namespace {

void say(char *problem, size_t size, const char *text) {
  if (problem && size) {
    strncpy(problem, text, size - 1);
    problem[size - 1] = '\0';
  }
}

bool safeForShell(const char *s) {
  for (const char *p = s; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '"' || c == '%' || c == '^' || c == '&' || c == '|' || c < 0x21 || c == 0x7F) return false;
  }
  return true;
}

std::string tempPath(const char *what) {
  const char *tmp = getenv("TEMP");
  char path[512];
  snprintf(path, sizeof(path), "%s\\bbw_ha_%s_%u.txt", tmp ? tmp : ".", what, static_cast<unsigned>(rand()));
  return path;
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
  if (!safeForShell(url)) {
    say(problem, problemSize, "the address could not be used");
    return -2;
  }
  emu::log("emu", "home assistant: %s sent", body ? "POST" : "GET");
  const std::string headers = tempPath("h"), answer = tempPath("a"), data = tempPath("d");
  FILE *h = fopen(headers.c_str(), "wb");
  if (!h) {
    say(problem, problemSize, "the request could not be written for curl");
    return -1;
  }
  if (token && token[0]) fprintf(h, "Authorization: Bearer %s\r\n", token);
  if (body) fprintf(h, "Content-Type: application/json\r\n");
  fclose(h);
  if (body) {
    FILE *d = fopen(data.c_str(), "wb");
    if (d) {
      fwrite(body, 1, strlen(body), d);
      fclose(d);
    }
  }
  char seconds[16];
  snprintf(seconds, sizeof(seconds), "%.1f", timeoutMs / 1000.0);
  std::string command = std::string("curl -s -m ") + seconds + " -o \"" + answer + "\" -w \"%{http_code}\" -H \"@" +
                        headers + "\" ";
  if (body) command += "--data-binary \"@" + data + "\" ";
  command += "\"" + std::string(url) + "\"";
  const std::string wrapped = "\"" + command + "\"";
  FILE *pipe = _popen(wrapped.c_str(), "rb");
  int code = -1;
  if (pipe) {
    char status[16] = {};
    fread(status, 1, sizeof(status) - 1, pipe);
    _pclose(pipe);
    code = atoi(status);
    if (code == 0) code = -1;
  }
  if (out && outSize > 1) {
    FILE *a = fopen(answer.c_str(), "rb");
    if (a) {
      const size_t n = fread(out, 1, outSize - 1, a);
      out[n] = '\0';
      fclose(a);
    }
  }
  remove(headers.c_str());
  remove(answer.c_str());
  remove(data.c_str());
  if (code < 0) say(problem, problemSize, "Home Assistant could not be reached");
  else if (code == 401) say(problem, problemSize, "Home Assistant refused the token");
  else if (code == 404) say(problem, problemSize, "Home Assistant has no such entity or webhook");
  else if (code >= 400) say(problem, problemSize, "Home Assistant answered with an error");
  emu::log("emu", "home assistant: %s answered %d", body ? "POST" : "GET", code);
  return code;
}

// ------------------------------------------------------------------ MQTT --
namespace {
bool g_on = false, g_announced = false;
char g_node[24] = {};
char g_firmware[24] = {};
char g_problem[64] = {};
}  // namespace

void mqttConfigure(const char *url, const char *, const char *, const char *node, const char *firmware) {
  char host[64];
  uint16_t port = 0;
  g_on = url && url[0] && mqttBrokerFromUrl(url, host, sizeof(host), port);
  strncpy(g_node, node ? node : "bbw", sizeof(g_node) - 1);
  strncpy(g_firmware, firmware ? firmware : "", sizeof(g_firmware) - 1);
  g_announced = false;
  g_problem[0] = '\0';
  if (url && url[0] && !g_on) strncpy(g_problem, "the broker address is not mqtt://host[:port]", sizeof(g_problem) - 1);
  else if (g_on) strncpy(g_problem, "not connected in the emulator (payloads are logged)", sizeof(g_problem) - 1);
}

bool mqttService(bool idle, void (*)()) {
  if (!g_on || !idle || g_announced) return false;
  // What the board would announce once connected.
  char topic[128], payload[900];
  for (size_t i = 0; i < MQTT_ENTITY_COUNT; ++i) {
    if (!mqttDiscoveryTopic(MQTT_ENTITIES[i], g_node, topic, sizeof(topic))) continue;
    if (!mqttDiscoveryConfig(MQTT_ENTITIES[i], g_node, g_firmware, payload, sizeof(payload))) continue;
    emu::log("emu", "mqtt (not sent) %s %s", topic, payload);
  }
  g_announced = true;
  return false;
}

bool mqttConnected() { return false; }

bool mqttPublish(const char *topic, const char *payload, bool) {
  if (g_on) emu::log("emu", "mqtt (not sent) %s %s", topic, payload);
  return false;
}

const char *mqttProblem() { return g_problem; }

}  // namespace bbw
