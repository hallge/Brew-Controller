#include "FirmwareUpdate.h"

#include <string.h>

namespace bbw {

const char *updatePhaseName(UpdatePhase phase) {
  switch (phase) {
    case UpdatePhase::CHECKING: return "checking";
    case UpdatePhase::DOWNLOADING: return "downloading";
    case UpdatePhase::INSTALLED: return "installed";
    case UpdatePhase::FAILED: return "failed";
    default: return "idle";
  }
}

}  // namespace bbw

#ifdef BBW_EMULATOR
// ---------------------------------------------------------------- emulator --
#include <Arduino.h>

namespace bbw {

namespace {
UpdateConfig g_config;
}

void tlsUseExternalMemory() {}
void updateBegin(const UpdateConfig &config) { g_config = config; }
void updateConfigure(const UpdateConfig &config) { g_config = config; }
UpdateConfig updateConfig() { return g_config; }
bool updateCheck() { return false; }
bool updateInstall() { return false; }
bool updateBusy() { return false; }
UpdateStatus updateStatus() {
  UpdateStatus s;
  strlcpy(s.message, "the PC emulator is updated by rebuilding it", sizeof(s.message));
  return s;
}
void updateService(bool, bool) {}
size_t updateSlots(SlotInfo *, size_t) { return 0; }
bool updateBootSlot(const char *, char *why, size_t whySize) {
  strlcpy(why, "not in the emulator", whySize);
  return false;
}

}  // namespace bbw

#else
// ------------------------------------------------------------------- board --
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/platform.h>
#include <time.h>

#include "BigMem.h"
#include "GithubRoots.h"
#include "Log.h"
#include "ReleaseRoots.h"
#include "Version.h"
#include "core/Sha256.h"

namespace bbw {

namespace {

void *tlsCalloc(size_t count, size_t size) {
  void *memory = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return memory;
}

void tlsFree(void *memory) { heap_caps_free(memory); }

}  // namespace

void tlsUseExternalMemory() { mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree); }

namespace {

constexpr uint16_t CONNECT_TIMEOUT_MS = 8000;
constexpr uint16_t ANSWER_TIMEOUT_MS = 15000;
constexpr uint32_t STALL_TIMEOUT_MS = 20000;            // no bytes for this long: give up
constexpr uint32_t CHECK_PERIOD_S = 24UL * 3600UL;      // daily
constexpr uint32_t FIRST_CHECK_AFTER_MS = 120000;       // after start-up
constexpr size_t CHUNK = 4096;
constexpr size_t SLOT_COUNT = 3;

enum : uint8_t { WANT_NONE = 0, WANT_CHECK = 1, WANT_INSTALL = 2 };

UpdateConfig g_config;
UpdateStatus g_status;
SemaphoreHandle_t g_mutex;
TaskHandle_t g_task;
volatile uint8_t g_want = WANT_NONE;
uint8_t *g_chunk;  // CHUNK bytes, PSRAM

struct SlotVersions {
  char label[SLOT_COUNT][17];
  char version[SLOT_COUNT][32];
};

// ---- status, shared between the update task and the web page ----
void setStatus(UpdatePhase phase, uint8_t percent, const char *message) {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_status.phase = phase;
  g_status.percent = percent;
  if (message) strlcpy(g_status.message, message, sizeof(g_status.message));
  xSemaphoreGive(g_mutex);
}

void failWith(const char *message) {
  setStatus(UpdatePhase::FAILED, 0, message);
  LOGW("update: %s", message);
}

// ---- which version is in which slot ----
SlotVersions loadSlotVersions() {
  SlotVersions v;
  memset(&v, 0, sizeof(v));
  Preferences prefs;
  if (prefs.begin("bbw", true)) {
    if (prefs.isKey("slots") && prefs.getBytesLength("slots") == sizeof(v)) {
      prefs.getBytes("slots", &v, sizeof(v));
    }
    prefs.end();
  }
  return v;
}

void recordSlotVersion(const char *label, const char *version) {
  SlotVersions v = loadSlotVersions();
  int slot = -1;
  for (size_t i = 0; i < SLOT_COUNT; ++i) {
    if (strcmp(v.label[i], label) == 0) slot = static_cast<int>(i);
  }
  for (size_t i = 0; slot < 0 && i < SLOT_COUNT; ++i) {
    if (v.label[i][0] == '\0') slot = static_cast<int>(i);
  }
  if (slot < 0) slot = 0;
  if (strcmp(v.label[slot], label) == 0 && strcmp(v.version[slot], version) == 0) return;
  strlcpy(v.label[slot], label, sizeof(v.label[slot]));
  strlcpy(v.version[slot], version, sizeof(v.version[slot]));
  Preferences prefs;
  if (prefs.begin("bbw", false)) {
    prefs.putBytes("slots", &v, sizeof(v));
    prefs.end();
  }
}

const char *slotVersion(const SlotVersions &v, const char *label) {
  for (size_t i = 0; i < SLOT_COUNT; ++i) {
    if (strcmp(v.label[i], label) == 0) return v.version[i];
  }
  return "";
}

// ---- GitHub ----
void apiRequest(HTTPClient &http, WiFiClientSecure &client, const char *url, const char *accept) {
  client.setCACert(GITHUB_ROOTS_PEM);
  client.setHandshakeTimeout(CONNECT_TIMEOUT_MS / 1000);
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(ANSWER_TIMEOUT_MS);
  http.useHTTP10(true);  // no chunked answer: the body can be read as a stream
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.begin(client, url);
  http.setUserAgent("brew-by-weight-controller/" BBW_VERSION);
  http.addHeader("Accept", accept);
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");
  if (g_config.token[0]) {
    char authorization[160];
    snprintf(authorization, sizeof(authorization), "Bearer %s", g_config.token);
    http.addHeader("Authorization", authorization);
  }
}

const char *explain(int code) {
  if (code < 0) return "GitHub could not be reached";
  if (code == 401) return "GitHub refused the token";
  if (code == 403) return "not allowed, or the hourly limit is reached";
  if (code == 404) return "no release found (or a private repository without a token)";
  return "GitHub answered with an error";
}

bool readyToTalk(char *why, size_t size) {
  if (WiFi.status() != WL_CONNECTED) {
    strlcpy(why, "not on a network with internet: join one under Network", size);
    return false;
  }
  if (time(nullptr) < 1700000000) {
    strlcpy(why, "the clock is not set yet, so certificates cannot be checked", size);
    return false;
  }
  if (g_config.repo[0] == '\0') {
    strlcpy(why, "no repository set", size);
    return false;
  }
  return true;
}

bool doCheck() {
  char why[96];
  if (!readyToTalk(why, sizeof(why))) {
    failWith(why);
    return false;
  }
  setStatus(UpdatePhase::CHECKING, 0, "asking GitHub for the latest release");
  char url[160];
  snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", g_config.repo);
  WiFiClientSecure client;
  HTTPClient http;
  apiRequest(http, client, url, "application/vnd.github+json");
  const int code = http.GET();
  if (code != 200) {
    char message[112];
    snprintf(message, sizeof(message), "check failed: %s (%d)", explain(code), code);
    http.end();
    failWith(message);
    return false;
  }
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["name"] = true;
  filter["published_at"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["id"] = true;
  filter["assets"][0]["size"] = true;
  JsonDocument doc;
  const DeserializationError error =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (error) {
    failWith("the release information could not be read");
    return false;
  }
  ReleaseInfo info;
  strlcpy(info.tag, doc["tag_name"] | "", sizeof(info.tag));
  strlcpy(info.name, doc["name"] | "", sizeof(info.name));
  strlcpy(info.published, doc["published_at"] | "", sizeof(info.published));
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    const char *name = asset["name"] | "";
    if (strcmp(name, "firmware.bin") == 0) {
      info.firmwareId = asset["id"] | 0u;
      info.firmwareBytes = asset["size"] | 0u;
    } else if (strcmp(name, "SHA256SUMS.txt") == 0) {
      info.sumsId = asset["id"] | 0u;
    }
  }
  info.valid = info.tag[0] && info.firmwareId && info.sumsId;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_status.latest = info;
  g_status.checkedEpoch = static_cast<uint32_t>(time(nullptr));
  g_status.available = info.valid && strcmp(info.tag, BBW_VERSION) != 0;
  xSemaphoreGive(g_mutex);
  if (!info.valid) {
    failWith("the latest release has no firmware.bin and SHA256SUMS.txt");
    return false;
  }
  char message[112];
  if (strcmp(info.tag, BBW_VERSION) == 0) {
    snprintf(message, sizeof(message), "up to date: %s is the latest release", info.tag);
  } else {
    snprintf(message, sizeof(message), "%s is available (running %s)", info.tag, BBW_VERSION);
  }
  setStatus(UpdatePhase::IDLE, 0, message);
  LOGI("update: %s", message);
  return true;
}

// Opens a release file for reading: the API answers with a redirect to the
// download host, which is asked without the token. `http` and `client` are
// left reading the file's body. Returns its length, or -1.
int openAsset(uint32_t assetId, HTTPClient &http, WiFiClientSecure &client) {
  char url[160];
  snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/assets/%u", g_config.repo,
           static_cast<unsigned>(assetId));
  String location;
  {
    WiFiClientSecure apiClient;
    HTTPClient api;
    apiRequest(api, apiClient, url, "application/octet-stream");
    const char *keep[] = {"Location"};
    api.collectHeaders(keep, 1);
    const int code = api.GET();
    if (code != 302 && code != 301 && code != 307) {
      LOGW("update: asset %u: answer %d", static_cast<unsigned>(assetId), code);
      api.end();
      return -1;
    }
    location = api.header("Location");
    api.end();
  }
  if (!location.startsWith("https://")) return -1;
  client.setCACert(RELEASE_ROOTS_PEM);
  client.setHandshakeTimeout(CONNECT_TIMEOUT_MS / 1000);
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(ANSWER_TIMEOUT_MS);
  http.useHTTP10(true);
  http.begin(client, location);
  http.setUserAgent("brew-by-weight-controller/" BBW_VERSION);
  const int code = http.GET();
  if (code != 200) {
    LOGW("update: download answered %d", code);
    http.end();
    return -1;
  }
  return http.getSize();
}

bool hexToBytes(const char *hex, uint8_t out[32]) {
  for (int i = 0; i < 32; ++i) {
    unsigned value = 0;
    if (sscanf(hex + 2 * i, "%2x", &value) != 1) return false;
    out[i] = static_cast<uint8_t>(value);
  }
  return true;
}

bool fetchExpectedSha(uint32_t sumsId, uint8_t expected[32]) {
  WiFiClientSecure client;
  HTTPClient http;
  const int length = openAsset(sumsId, http, client);
  if (length <= 0 || length > 2048) {
    http.end();
    return false;
  }
  char text[2049];
  const size_t got = http.getStream().readBytes(text, length);
  http.end();
  text[got] = '\0';
  // sha256sum's format: "<64 hex>  firmware.bin" per line.
  for (char *line = strtok(text, "\n"); line; line = strtok(nullptr, "\n")) {
    if (strstr(line, "firmware.bin") && strlen(line) >= 64) return hexToBytes(line, expected);
  }
  return false;
}

bool doInstall() {
  if (!doCheck()) return false;
  ReleaseInfo info;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  info = g_status.latest;
  xSemaphoreGive(g_mutex);
  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  if (!target) {
    failWith("no update slot: the partition table has none");
    return false;
  }
  if (info.firmwareBytes == 0 || info.firmwareBytes > target->size) {
    failWith("the release's firmware does not fit the update slot");
    return false;
  }
  uint8_t expected[32];
  if (!fetchExpectedSha(info.sumsId, expected)) {
    failWith("the release's SHA256SUMS.txt could not be read");
    return false;
  }
  char message[112];
  snprintf(message, sizeof(message), "downloading %s into slot %s", info.tag, target->label);
  setStatus(UpdatePhase::DOWNLOADING, 0, message);
  LOGI("update: %s", message);

  WiFiClientSecure client;
  HTTPClient http;
  const int length = openAsset(info.firmwareId, http, client);
  if (length != static_cast<int>(info.firmwareBytes)) {
    http.end();
    failWith("the firmware download could not be started");
    return false;
  }
  if (!Update.begin(info.firmwareBytes, U_FLASH)) {
    http.end();
    snprintf(message, sizeof(message), "could not prepare the update slot: %s", Update.errorString());
    failWith(message);
    return false;
  }
  if (!g_chunk) g_chunk = bigArray<uint8_t>(CHUNK);
  Sha256 sha;
  WiFiClient &stream = http.getStream();
  size_t received = 0;
  uint32_t lastDataMs = millis();
  bool ok = true;
  while (received < info.firmwareBytes) {
    const size_t available = stream.available();
    if (available == 0) {
      if (!http.connected() || millis() - lastDataMs > STALL_TIMEOUT_MS) {
        ok = false;
        break;
      }
      delay(5);
      continue;
    }
    const size_t want = available < CHUNK ? available : CHUNK;
    const size_t n = stream.readBytes(g_chunk, want);
    if (n == 0) continue;
    lastDataMs = millis();
    sha.update(g_chunk, n);
    if (Update.write(g_chunk, n) != n) {
      ok = false;
      break;
    }
    received += n;
    setStatus(UpdatePhase::DOWNLOADING, static_cast<uint8_t>(received * 100 / info.firmwareBytes), nullptr);
  }
  http.end();
  if (!ok || received != info.firmwareBytes) {
    Update.abort();
    failWith("the download broke off; the running firmware is unchanged");
    return false;
  }
  uint8_t actual[32];
  sha.finish(actual);
  if (memcmp(actual, expected, 32) != 0) {
    Update.abort();
    failWith("the download does not match the release's SHA-256; not installed");
    return false;
  }
  // Checks the image itself and makes the slot the one to start from.
  if (!Update.end(true)) {
    snprintf(message, sizeof(message), "the image was refused: %s", Update.errorString());
    failWith(message);
    return false;
  }
  recordSlotVersion(target->label, info.tag);
  snprintf(message, sizeof(message), "%s installed in slot %s; restarting into it", info.tag, target->label);
  setStatus(UpdatePhase::INSTALLED, 100, message);
  LOGI("update: %s", message);
  return true;
}

void updateTask(void *) {
  for (;;) {
    const uint8_t want = g_want;
    if (want == WANT_NONE) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    g_want = WANT_NONE;
    if (want == WANT_INSTALL) {
      if (doInstall()) {
        delay(1500);  // the page gets to see "installed"
        logFlush();
        ESP.restart();
      }
    } else {
      doCheck();
    }
  }
}

bool request(uint8_t want) {
  if (!g_task || g_want != WANT_NONE) return false;
  const UpdatePhase phase = updateStatus().phase;
  if (phase == UpdatePhase::CHECKING || phase == UpdatePhase::DOWNLOADING) return false;
  g_want = want;
  xTaskNotifyGive(g_task);
  return true;
}

}  // namespace

void updateBegin(const UpdateConfig &config) {
  g_mutex = xSemaphoreCreateMutex();
  g_config = config;
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running) recordSlotVersion(running->label, BBW_VERSION);
  // Started from a slot other than the one the update data names (a fresh USB
  // flash names an empty ota_0 and the bootloader falls back to factory):
  // name the running one, so the page and the next restart agree.
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  esp_app_desc_t desc;
  if (running && boot && boot->address != running->address &&
      esp_ota_get_partition_description(boot, &desc) != ESP_OK) {
    esp_ota_set_boot_partition(running);
  }
  // TLS and the JSON of a release need a few kilobytes of stack: a task of
  // its own, waiting until it is asked for something. On the loop task's
  // core: a TLS handshake takes seconds of processor time, and on core 0 it
  // kept the idle task (which the watchdog watches) from running.
  xTaskCreatePinnedToCore(updateTask, "update", 12288, nullptr, 1, &g_task, 1);
}

void updateConfigure(const UpdateConfig &config) { g_config = config; }
UpdateConfig updateConfig() { return g_config; }

bool updateCheck() { return request(WANT_CHECK); }
bool updateInstall() { return request(WANT_INSTALL); }

bool updateBusy() {
  const UpdatePhase phase = updateStatus().phase;
  return g_want != WANT_NONE || phase == UpdatePhase::CHECKING || phase == UpdatePhase::DOWNLOADING ||
         phase == UpdatePhase::INSTALLED;
}

UpdateStatus updateStatus() {
  UpdateStatus copy;
  if (!g_mutex) return copy;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  copy = g_status;
  xSemaphoreGive(g_mutex);
  return copy;
}

void updateService(bool idle, bool online) {
  if (!g_config.autoCheck || !idle || !online || millis() < FIRST_CHECK_AFTER_MS) return;
  const time_t now = time(nullptr);
  if (now < 1700000000) return;
  const uint32_t checked = updateStatus().checkedEpoch;
  static uint32_t lastTryMs = 0;
  // Once a day after a check; after a failed one, try again an hour later.
  if (checked != 0 && static_cast<uint32_t>(now) - checked < CHECK_PERIOD_S) return;
  if (lastTryMs != 0 && millis() - lastTryMs < 3600000UL) return;
  lastTryMs = millis() ? millis() : 1;
  updateCheck();
}

size_t updateSlots(SlotInfo *out, size_t max) {
  const SlotVersions versions = loadSlotVersions();
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  size_t n = 0;
  esp_partition_iterator_t it =
      esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  for (; it && n < max; it = esp_partition_next(it)) {
    const esp_partition_t *p = esp_partition_get(it);
    SlotInfo &s = out[n++];
    strlcpy(s.label, p->label, sizeof(s.label));
    strlcpy(s.version, slotVersion(versions, p->label), sizeof(s.version));
    s.bytes = p->size;
    s.running = running && running->address == p->address;
    s.bootNext = boot && boot->address == p->address;
    esp_app_desc_t desc;
    const bool image = esp_ota_get_partition_description(p, &desc) == ESP_OK;
    if (p->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
      s.state = image ? "factory" : "unused";
    } else {
      esp_ota_img_states_t state;
      if (!image) {
        s.state = "unused";
      } else if (esp_ota_get_state_partition(p, &state) != ESP_OK) {
        s.state = "valid";  // written over USB, never through the update process
      } else if (state == ESP_OTA_IMG_PENDING_VERIFY || state == ESP_OTA_IMG_NEW) {
        s.state = "on trial";
      } else if (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED) {
        s.state = "failed";
      } else {
        s.state = "valid";
      }
    }
  }
  if (it) esp_partition_iterator_release(it);
  return n;
}

bool updateBootSlot(const char *which, char *why, size_t whySize) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *target = nullptr;
  if (strcmp(which, "factory") == 0) {
    target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    if (!target) {
      strlcpy(why, "this board has no factory slot (old partition table)", whySize);
      return false;
    }
  } else if (strcmp(which, "previous") == 0) {
    // The update slot that is not running, if it holds a firmware that ran.
    for (int sub = ESP_PARTITION_SUBTYPE_APP_OTA_0; sub <= ESP_PARTITION_SUBTYPE_APP_OTA_1; ++sub) {
      const esp_partition_t *p = esp_partition_find_first(
          ESP_PARTITION_TYPE_APP, static_cast<esp_partition_subtype_t>(sub), nullptr);
      if (!p || (running && p->address == running->address)) continue;
      esp_app_desc_t desc;
      esp_ota_img_states_t state;
      const bool image = esp_ota_get_partition_description(p, &desc) == ESP_OK;
      const bool failed = esp_ota_get_state_partition(p, &state) == ESP_OK &&
                          (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED);
      if (image && !failed) target = p;
    }
    if (!target) {
      strlcpy(why, "there is no earlier version in the other update slot", whySize);
      return false;
    }
  } else {
    strlcpy(why, "say factory or previous", whySize);
    return false;
  }
  if (running && target->address == running->address) {
    strlcpy(why, "that is the firmware running now", whySize);
    return false;
  }
  if (esp_ota_set_boot_partition(target) != ESP_OK) {
    strlcpy(why, "the bootloader would not take that slot", whySize);
    return false;
  }
  LOGI("update: next start from slot %s (%s)", target->label, which);
  snprintf(why, whySize, "the controller will start from slot %s", target->label);
  return true;
}

}  // namespace bbw

#endif
