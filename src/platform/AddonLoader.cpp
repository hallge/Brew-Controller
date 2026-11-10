// Add-on modules on the board (platform/AddonLoader.h, docs/ADDONS.md "Add-ons as modules"). Board only.

#ifndef BBW_EMULATOR

#include "platform/AddonLoader.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <dirent.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

extern "C" {
#include "esp_elf.h"
}

#include "Log.h"
#include "addons/AddonAbi.h"
#include "addons/ModuleAddon.h"
#include "addons/ModuleFile.h"
#include "core/Sha256.h"
#include "platform/BigMem.h"
#include "platform/FirmwareUpdate.h"
#include "platform/FsUtil.h"
#include "platform/NetWorker.h"
#include "platform/RamWatch.h"
#include "Version.h"

namespace bbw {
namespace addonloader {

using namespace bbw::addon;
namespace mod = bbw::addon::module;

namespace {

constexpr const char *PREFS_NS = "bbwaddon";  // NVS, not in backups (storage/Backup.cpp NVS_KEYS)
constexpr const char *UPLOAD_TMP = "/addons/upload.tmp";
constexpr const char *DOWNLOAD_TMP = "/addons/download.tmp";
constexpr size_t FILE_MAX = mod::HEADER_SIZE + mod::MODULE_MAX_ELF;
constexpr size_t SUMS_MAX = 4096;

size_t g_count = 0;

// What a job on the network task is asked to do.
struct Job {
  enum Kind : uint8_t { CHECK, INSTALL } kind;
  char id[32];
  Source source;
};

// Everything that is not tiny lives in PSRAM, taken the first time it is needed (no module files and
// nobody asking: nothing is taken): the public firmware's static chip RAM stays what it was.
struct Heap {
  Module modules[MAX_MODULES];
  Delivery delivery;   // what the page sees; guarded by g_mutex
  Job job;
  char upError[96];
  char unresolved[48];
};
Heap *g_heap = nullptr;
Heap &heap() {
  if (!g_heap) g_heap = bigNew<Heap>();
  return *g_heap;
}

// ---------------------------------------------------------------- crash guard store --

class NvsGuardStore : public mod::GuardStore {
 public:
  void get(const char *key, char *out, size_t size) override {
    out[0] = '\0';
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, true)) return;
    prefs.getString(key, out, size);
    prefs.end();
  }
  void put(const char *key, const char *value) override {
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) return;
    if (value && value[0]) prefs.putString(key, value);
    else prefs.remove(key);
    prefs.end();
  }
};
NvsGuardStore g_store;
mod::Guard g_guard(g_store);

bool crashReset() {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return true;
    default:
      return false;
  }
}

// ---------------------------------------------------------------- what a module may call --

// The short allow-list: the module is linked with -nostdlib and gets only these from the firmware.
// Anything else it needs makes it fail to load, with the name shown.
char *(*const STRCHR)(const char *, int) = strchr;
char *(*const STRSTR)(const char *, const char *) = strstr;

struct Export {
  const char *name;
  const void *address;
};
const Export EXPORTS[] = {
    {"snprintf", reinterpret_cast<const void *>(&snprintf)},
    {"vsnprintf", reinterpret_cast<const void *>(&vsnprintf)},
    {"strtod", reinterpret_cast<const void *>(&strtod)},
    {"atof", reinterpret_cast<const void *>(&atof)},
    {"strlen", reinterpret_cast<const void *>(&strlen)},
    {"strcmp", reinterpret_cast<const void *>(&strcmp)},
    {"strncmp", reinterpret_cast<const void *>(&strncmp)},
    {"strstr", reinterpret_cast<const void *>(STRSTR)},
    {"strchr", reinterpret_cast<const void *>(STRCHR)},
    {"memcpy", reinterpret_cast<const void *>(&memcpy)},
    {"memmove", reinterpret_cast<const void *>(&memmove)},
    {"memset", reinterpret_cast<const void *>(&memset)},
    {"memcmp", reinterpret_cast<const void *>(&memcmp)},
    {"strcpy", reinterpret_cast<const void *>(&strcpy)},
    {"strncpy", reinterpret_cast<const void *>(&strncpy)},
    {"floorf", reinterpret_cast<const void *>(static_cast<float (*)(float)>(&floorf))},
    {"fabsf", reinterpret_cast<const void *>(static_cast<float (*)(float)>(&fabsf))},
    {"sqrtf", reinterpret_cast<const void *>(static_cast<float (*)(float)>(&sqrtf))},
};


uintptr_t resolve(const char *name) {
  if (name) {
    for (const Export &e : EXPORTS) {
      if (strcmp(e.name, name) == 0) return reinterpret_cast<uintptr_t>(e.address);
    }
    if (!heap().unresolved[0]) strlcpy(heap().unresolved, name, sizeof(heap().unresolved));
  }
  return 0;
}

// ---------------------------------------------------------------- loading --

struct Loaded {
  esp_elf_t elf;
};

// Calls the module's ELF entry: argv[0] is the host table, argv[1] where the module puts its own.
const bbw_addon_api *callEntry(void *context, const bbw_host_api *host) {
  Loaded *loaded = static_cast<Loaded *>(context);
  const bbw_addon_api *table = nullptr;
  char *argv[2] = {reinterpret_cast<char *>(const_cast<bbw_host_api *>(host)), reinterpret_cast<char *>(&table)};
  if (esp_elf_request(&loaded->elf, 0, 2, argv) != 0) return nullptr;
  return table;
}

void *moduleAlloc(size_t size) { return bigAlloc(size); }

void setProblem(Module &m, State state, const char *text) {
  m.state = state;
  strlcpy(m.problem, text, sizeof(m.problem));
}

void makeStub(Module &m, const char *name) {
  void *memory = bigAlloc(sizeof(ModuleStub));
  m.addon = new (memory) ModuleStub(m.id, name, m.version);
}

bool readWhole(const char *path, uint8_t *buffer, size_t size) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  const size_t n = f.read(buffer, size);
  f.close();
  return n == size;
}

// Reads, verifies and loads one module. On every failure the module is listed as refused.
void load(Module &m, const char *path) {
  const uint32_t startMs = millis();
  const uint32_t chipBefore = memoryReport().known ? memoryReport().internalFree : 0;
  uint8_t *file = static_cast<uint8_t *>(heap_caps_malloc(m.fileBytes + 16, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!file) {
    setProblem(m, State::REFUSED, "no memory to read the module");
    makeStub(m, "");
    return;
  }
  mod::Header header;
  if (!readWhole(path, file, m.fileBytes)) {
    setProblem(m, State::REFUSED, "the module file could not be read");
  } else if (const char *why = mod::verifyFile(file, m.fileBytes, header)) {
    setProblem(m, State::REFUSED, why);
  } else {
    g_guard.loading(m.id, millis());
    Loaded *loaded = static_cast<Loaded *>(bigAlloc(sizeof(Loaded)));
    heap().unresolved[0] = '\0';
    elf_set_symbol_resolver(resolve);
    esp_elf_init(&loaded->elf);
    const int rc = esp_elf_relocate(&loaded->elf, file + mod::HEADER_SIZE, header.elfSize);
    if (rc != 0) {
      char text[sizeof(m.problem)];
      if (heap().unresolved[0]) snprintf(text, sizeof(text), "it needs %s, which a module may not use", heap().unresolved);
      else snprintf(text, sizeof(text), "the loader refused it (%d)", rc);
      setProblem(m, State::REFUSED, text);
      esp_elf_deinit(&loaded->elf);
    } else {
      ModuleAddon *addon = new (bigAlloc(sizeof(ModuleAddon))) ModuleAddon(callEntry, loaded, moduleAlloc);
      if (!addon->ok()) {
        setProblem(m, State::REFUSED, "the module gave no usable add-on");
      } else if (strcmp(addon->info().id, m.id) != 0) {
        setProblem(m, State::REFUSED, "the add-on's id is not the file's");
      } else {
        m.addon = addon;
        m.state = State::LOADED;
        m.problem[0] = '\0';
      }
    }
  }
  heap_caps_free(file);
  if (!m.addon) makeStub(m, "");
  m.loadMs = millis() - startMs;
  const uint32_t chipAfter = memoryReport().known ? memoryReport().internalFree : 0;
  m.chipUsed = chipBefore > chipAfter ? chipBefore - chipAfter : 0;
  LOGI("addon module %s %s: %s (%u ms, chip RAM %u B)", m.id, m.version, m.state == State::LOADED ? "loaded" : m.problem,
       static_cast<unsigned>(m.loadMs), static_cast<unsigned>(m.chipUsed));
}

// A file in /addons that may be a module: the name "<id>.bbwmod".
void listFiles() {
  DIR *d = opendir("/littlefs/addons");
  if (!d) return;
  while (struct dirent *e = readdir(d)) {
    if (g_count >= MAX_MODULES) break;
    char id[32];
    if (!mod::idFromAssetName(e->d_name, id, sizeof(id))) continue;
    Module &m = heap().modules[g_count++];
    m = Module();
    strlcpy(m.id, id, sizeof(m.id));
    char path[64];
    snprintf(path, sizeof(path), "/addons/%s", e->d_name);
    File f = LittleFS.open(path, "r");
    if (!f) {
      setProblem(m, State::REFUSED, "the module file could not be opened");
      makeStub(m, "");
      continue;
    }
    m.fileBytes = static_cast<uint32_t>(f.size());
    uint8_t head[mod::HEADER_SIZE];
    const size_t n = f.read(head, sizeof(head));
    f.close();
    mod::Header h;
    if (const char *why = mod::parseHeader(head, n, m.fileBytes, h)) {
      setProblem(m, State::REFUSED, why);
      makeStub(m, "");
      continue;
    }
    strlcpy(m.version, h.version, sizeof(m.version));
    if (strcmp(h.id, m.id) != 0) {
      setProblem(m, State::REFUSED, "the file's name is not the add-on's id");
      makeStub(m, "");
      continue;
    }
    switch (mod::fits(h, BBW_ADDON_ABI, mod::packVersion(BBW_VERSION))) {
      case mod::Fit::NEEDS_REBUILD: {
        char text[sizeof(m.problem)];
        snprintf(text, sizeof(text), "built for add-on interface %u, this firmware has %u: needs a rebuild",
                 static_cast<unsigned>(h.abi), static_cast<unsigned>(BBW_ADDON_ABI));
        setProblem(m, State::NEEDS_REBUILD, text);
        makeStub(m, "");
        continue;
      }
      case mod::Fit::NEEDS_FIRMWARE:
        setProblem(m, State::NEEDS_FIRMWARE, "needs a newer firmware");
        makeStub(m, "");
        continue;
      default:
        break;
    }
    if (g_guard.blocked(m.id)) {
      setProblem(m, State::CRASHED, "stopped after a crash");
      makeStub(m, "");
      continue;
    }
    load(m, path);
  }
  closedir(d);
}

// ---------------------------------------------------------------- storing a file --

uint32_t fsFree() {
  const size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  return total > used ? static_cast<uint32_t>(total - used) : 0;
}

// A complete file at `tmp` (the upload or a download): check it, then swap it in as /addons/<id>.bbwmod.
bool finalize(const char *tmp, char *idOut, size_t idSize, const char *&problem) {
  File f = LittleFS.open(tmp, "r");
  if (!f) {
    problem = "the file could not be read back";
    return false;
  }
  const size_t size = f.size();
  uint8_t *buffer = size >= mod::HEADER_SIZE && size <= FILE_MAX
                        ? static_cast<uint8_t *>(heap_caps_malloc(size + 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
                        : nullptr;
  bool ok = false;
  mod::Header header;
  if (!buffer) {
    problem = size > FILE_MAX ? "the module is over 256 kB" : size < mod::HEADER_SIZE ? "not an add-on module file" : "no memory";
  } else if (f.read(buffer, size) != size) {
    problem = "the file could not be read back";
  } else if (const char *why = mod::verifyFile(buffer, size, header)) {
    problem = why;
  } else if (mod::fits(header, BBW_ADDON_ABI, mod::packVersion(BBW_VERSION)) == mod::Fit::NEEDS_REBUILD) {
    problem = "built for another add-on interface: needs a rebuild";
  } else if (mod::fits(header, BBW_ADDON_ABI, mod::packVersion(BBW_VERSION)) == mod::Fit::NEEDS_FIRMWARE) {
    problem = "needs a newer firmware";
  } else {
    ok = true;
  }
  f.close();
  if (buffer) heap_caps_free(buffer);
  if (!ok) {
    LittleFS.remove(tmp);
    return false;
  }
  char path[64];
  snprintf(path, sizeof(path), "/addons/%s.bbwmod", header.id);
  LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path)) {
    LittleFS.remove(tmp);
    problem = "the module could not be stored";
    return false;
  }
  strlcpy(idOut, header.id, idSize);
  return true;
}

// ---------------------------------------------------------------- the upload --

File g_upFile;
bool g_upOpen = false;
bool g_upStored = false;
size_t g_upSize = 0;

// ---------------------------------------------------------------- delivery --

SemaphoreHandle_t g_mutex = nullptr;
ReleaseAssets *g_assets;   // PSRAM, from the last check
char *g_sums;              // SUMS_MAX, PSRAM
volatile bool g_jobBusy = false;

void lock() {
  if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
  xSemaphoreTake(g_mutex, portMAX_DELAY);
}
void unlock() { xSemaphoreGive(g_mutex); }

void setStatus(Phase phase, const char *message) {
  lock();
  heap().delivery.phase = phase;
  if (message) strlcpy(heap().delivery.message, message, sizeof(heap().delivery.message));
  unlock();
}

Source loadSource() {
  Source s;
  Preferences prefs;
  if (prefs.begin(PREFS_NS, true)) {
    prefs.getString("srcrepo", s.repo, sizeof(s.repo));
    prefs.getString("srctoken", s.token, sizeof(s.token));
    prefs.end();
  }
  return s;
}

// The SHA-256 of an installed module file; false: not there.
bool fileSha(const char *path, uint8_t out[32]) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  Sha256 sha;
  uint8_t chunk[256];
  for (;;) {
    const size_t n = f.read(chunk, sizeof(chunk));
    if (n == 0) break;
    sha.update(chunk, n);
  }
  f.close();
  sha.finish(out);
  return true;
}

struct SumsSink {
  size_t length = 0;
};
bool sumsSink(const uint8_t *data, size_t n, void *context) {
  SumsSink &s = *static_cast<SumsSink *>(context);
  if (s.length + n >= SUMS_MAX) return false;
  memcpy(g_sums + s.length, data, n);
  s.length += n;
  g_sums[s.length] = '\0';
  return true;
}

struct FileSink {
  File file;
  Sha256 sha;
  size_t written = 0;
};
bool fileSink(const uint8_t *data, size_t n, void *context) {
  FileSink &s = *static_cast<FileSink *>(context);
  if (s.written + n > FILE_MAX || s.file.write(data, n) != n) return false;
  s.sha.update(data, n);
  s.written += n;
  return true;
}

const ReleaseAssetInfo *findAsset(const char *name) {
  if (!g_assets) return nullptr;
  for (size_t i = 0; i < g_assets->count; ++i) {
    if (strcmp(g_assets->asset[i].name, name) == 0) return &g_assets->asset[i];
  }
  return nullptr;
}

void runCheck(const Source &src) {
  setStatus(Phase::CHECKING, "asking GitHub for the newest release");
  char why[96];
  if (!releaseListLatest(src.repo, src.token, *g_assets, why, sizeof(why))) {
    char message[112];
    snprintf(message, sizeof(message), "check failed: %s", why);
    setStatus(Phase::FAILED, message);
    return;
  }
  const ReleaseAssetInfo *sumsAsset = findAsset("SHA256SUMS.txt");
  SumsSink sink;
  g_sums[0] = '\0';
  if (!sumsAsset || !releaseStreamAsset(src.repo, src.token, sumsAsset->id, SUMS_MAX - 1, sumsSink, &sink, why, sizeof(why))) {
    setStatus(Phase::FAILED, sumsAsset ? "the release's SHA256SUMS.txt could not be read" : "the release has no SHA256SUMS.txt");
    return;
  }
  Delivery out;
  out.checkedEpoch = static_cast<uint32_t>(time(nullptr));
  for (size_t i = 0; i < g_assets->count && out.offerCount < MAX_MODULES; ++i) {
    const ReleaseAssetInfo &a = g_assets->asset[i];
    Offer &o = out.offers[out.offerCount];
    if (!mod::idFromAssetName(a.name, o.id, sizeof(o.id))) continue;
    uint8_t listed[32];
    if (!mod::sumsLookup(g_sums, a.name, listed)) continue;  // no checksum: not offered
    strlcpy(o.tag, g_assets->tag, sizeof(o.tag));
    o.bytes = a.bytes;
    char path[64];
    snprintf(path, sizeof(path), "/addons/%s", a.name);
    uint8_t have[32];
    o.installed = fileSha(path, have);
    o.differs = o.installed && memcmp(have, listed, 32) != 0;
    ++out.offerCount;
  }
  lock();
  heap().delivery.checkedEpoch = out.checkedEpoch;
  heap().delivery.offerCount = out.offerCount;
  for (size_t i = 0; i < out.offerCount; ++i) heap().delivery.offers[i] = out.offers[i];
  unlock();
  char message[112];
  if (out.offerCount == 0) snprintf(message, sizeof(message), "%s has no add-on modules", g_assets->tag);
  else snprintf(message, sizeof(message), "%s: %u add-on module(s) available", g_assets->tag,
                static_cast<unsigned>(out.offerCount));
  setStatus(Phase::DONE, message);
  LOGI("addon modules: %s", message);
}

void runInstall(const Source &src, const char *id) {
  char name[48];
  snprintf(name, sizeof(name), "%s.bbwmod", id);
  const ReleaseAssetInfo *asset = findAsset(name);
  uint8_t listed[32];
  if (!asset || !mod::sumsLookup(g_sums, name, listed)) {
    setStatus(Phase::FAILED, "that module is not in the last check: check again");
    return;
  }
  if (asset->bytes < mod::HEADER_SIZE || asset->bytes > FILE_MAX) {
    setStatus(Phase::FAILED, "the release's module has an impossible size");
    return;
  }
  if (fsFree() < asset->bytes + ADDON_FILE_RESERVE) {
    setStatus(Phase::FAILED, "not enough free storage: the owner's shot data comes first");
    return;
  }
  char message[112];
  snprintf(message, sizeof(message), "downloading %s", name);
  setStatus(Phase::DOWNLOADING, message);
  LittleFS.mkdir("/addons");
  LittleFS.remove(DOWNLOAD_TMP);
  FileSink sink;
  sink.file = LittleFS.open(DOWNLOAD_TMP, "w");
  if (!sink.file) {
    setStatus(Phase::FAILED, "the file could not be opened for writing");
    return;
  }
  char why[96];
  const bool got = releaseStreamAsset(src.repo, src.token, asset->id, FILE_MAX, fileSink, &sink, why, sizeof(why));
  sink.file.close();
  if (!got) {
    LittleFS.remove(DOWNLOAD_TMP);
    snprintf(message, sizeof(message), "download failed: %s", why);
    setStatus(Phase::FAILED, message);
    return;
  }
  uint8_t actual[32];
  sink.sha.finish(actual);
  if (memcmp(actual, listed, 32) != 0) {
    LittleFS.remove(DOWNLOAD_TMP);
    setStatus(Phase::FAILED, "the download does not match the release's SHA-256; not installed");
    return;
  }
  char stored[32];
  const char *problem = "";
  if (!finalize(DOWNLOAD_TMP, stored, sizeof(stored), problem)) {
    snprintf(message, sizeof(message), "refused: %s", problem);
    setStatus(Phase::FAILED, message);
    return;
  }
  lock();
  heap().delivery.restartNeeded = true;
  for (size_t i = 0; i < heap().delivery.offerCount; ++i) {
    if (strcmp(heap().delivery.offers[i].id, stored) == 0) {
      heap().delivery.offers[i].installed = true;
      heap().delivery.offers[i].differs = false;
    }
  }
  unlock();
  snprintf(message, sizeof(message), "%s stored; it loads at the next restart", name);
  setStatus(Phase::DONE, message);
  LOGI("addon modules: %s", message);
}

void jobMain(void *) {
  RamJobScope ram(RAM_ADDON);
  if (heap().job.kind == Job::CHECK) runCheck(heap().job.source);
  else runInstall(heap().job.source, heap().job.id);
  memset(heap().job.source.token, 0, sizeof(heap().job.source.token));
  g_jobBusy = false;
}

bool submit(Job::Kind kind, const char *id, const char *&problem) {
  if (g_jobBusy || netAddonBusy()) {
    problem = "the network is busy: try again in a moment";
    return false;
  }
  const Source src = loadSource();
  if (!src.repo[0]) {
    problem = "no repository set under Add-ons from";
    return false;
  }
  if (!g_assets) {
    g_assets = bigNew<ReleaseAssets>();
    g_sums = bigArray<char>(SUMS_MAX);
  }
  heap().job.kind = kind;
  strlcpy(heap().job.id, id ? id : "", sizeof(heap().job.id));
  heap().job.source = src;
  g_jobBusy = true;
  if (!netSubmit(jobMain, nullptr)) {
    g_jobBusy = false;
    problem = "the network is busy: try again in a moment";
    return false;
  }
  return true;
}

}  // namespace

const char *stateName(State s) {
  switch (s) {
    case State::LOADED: return "loaded";
    case State::CRASHED: return "stopped after a crash";
    case State::NEEDS_REBUILD: return "needs a rebuild";
    case State::NEEDS_FIRMWARE: return "needs a newer firmware";
    default: return "refused";
  }
}

size_t scan(uint32_t nowMs) {
  (void)nowMs;
  g_guard.boot(crashReset());
  g_count = 0;
  listFiles();
  return g_count;
}
size_t count() { return g_count; }
const Module &module(size_t i) { return heap().modules[i < g_count ? i : 0]; }
int find(const char *id) {
  for (size_t i = 0; i < g_count; ++i) {
    if (id && strcmp(heap().modules[i].id, id) == 0) return static_cast<int>(i);
  }
  return -1;
}

void service(uint32_t nowMs) { g_guard.service(nowMs); }

bool retry(const char *id, const char *&problem) {
  if (!id || !g_guard.blocked(id)) {
    problem = "that module is not stopped";
    return false;
  }
  g_guard.retry(id);
  lock();
  heap().delivery.restartNeeded = true;
  unlock();
  return true;
}

bool remove(const char *id, const char *&problem) {
  if (!mod::validId(id)) {
    problem = "no such module";
    return false;
  }
  char path[64];
  snprintf(path, sizeof(path), "/addons/%s.bbwmod", id);
  if (!fileExists(path)) {
    problem = "no such module";
    return false;
  }
  LittleFS.remove(path);
  g_guard.retry(id);
  lock();
  heap().delivery.restartNeeded = true;
  unlock();
  return true;
}

Source source() {
  Source s = loadSource();
  s.token[0] = s.token[0] ? '*' : '\0';  // the page learns only whether there is one
  s.token[1] = '\0';
  return s;
}

bool setSource(const char *repo, const char *token, const char *&problem) {
  if (repo && repo[0] && !mod::validRepo(repo)) {
    problem = "the repository is written owner/name";
    return false;
  }
  if (token && strlen(token) >= sizeof(Source::token)) {
    problem = "the token is too long";
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(PREFS_NS, false)) {
    problem = "the setting could not be stored";
    return false;
  }
  if (repo) {
    if (repo[0]) prefs.putString("srcrepo", repo);
    else prefs.remove("srcrepo");
  }
  if (token && token[0]) {
    if (strcmp(token, "-") == 0) prefs.remove("srctoken");
    else prefs.putString("srctoken", token);
  }
  prefs.end();
  return true;
}

Delivery delivery() {
  lock();
  Delivery d = heap().delivery;
  unlock();
  return d;
}

bool check(const char *&problem) { return submit(Job::CHECK, "", problem); }

bool install(const char *id, bool idle, const char *&problem) {
  if (!idle) {
    problem = "not while a shot is in hand";
    return false;
  }
  if (!mod::validId(id)) {
    problem = "no such module";
    return false;
  }
  return submit(Job::INSTALL, id, problem);
}

void uploadBegin(bool idle) {
  heap().upError[0] = '\0';
  g_upStored = false;
  g_upSize = 0;
  g_upOpen = false;
  if (!idle) {
    strlcpy(heap().upError, "not while a shot is in hand", sizeof(heap().upError));
    return;
  }
  if (g_jobBusy) {
    strlcpy(heap().upError, "a download is under way: try again when it has finished", sizeof(heap().upError));
    return;
  }
  LittleFS.mkdir("/addons");
  LittleFS.remove(UPLOAD_TMP);
  g_upFile = LittleFS.open(UPLOAD_TMP, "w");
  if (!g_upFile) {
    strlcpy(heap().upError, "the file could not be opened for writing", sizeof(heap().upError));
    return;
  }
  g_upOpen = true;
}

void uploadWrite(const uint8_t *data, size_t size) {
  if (!g_upOpen || heap().upError[0]) return;
  if (g_upSize + size > FILE_MAX) {
    strlcpy(heap().upError, "the module is over 256 kB", sizeof(heap().upError));
    return;
  }
  if (g_upFile.write(data, size) != size) {
    strlcpy(heap().upError, "the file could not be written", sizeof(heap().upError));
    return;
  }
  g_upSize += size;
}

bool uploadEnd(char *id, size_t idSize, const char *&problem) {
  if (g_upOpen) g_upFile.close();
  const bool wasOpen = g_upOpen;
  g_upOpen = false;
  if (heap().upError[0]) {
    if (wasOpen) LittleFS.remove(UPLOAD_TMP);
    problem = heap().upError;
    return false;
  }
  bool ok = true;
  if (fsFree() < g_upSize + ADDON_FILE_RESERVE) {
    LittleFS.remove(UPLOAD_TMP);
    problem = "not enough free storage: the owner's shot data comes first";
    ok = false;
  } else {
    ok = finalize(UPLOAD_TMP, id, idSize, problem);
  }
  if (!ok) {
    strlcpy(heap().upError, problem, sizeof(heap().upError));  // for the request's answer
    return false;
  }
  g_upStored = true;
  lock();
  heap().delivery.restartNeeded = true;
  unlock();
  return true;
}

void uploadAbort() {
  if (g_upOpen) g_upFile.close();
  if (g_upOpen) LittleFS.remove(UPLOAD_TMP);
  g_upOpen = false;
  strlcpy(heap().upError, "the upload was interrupted", sizeof(heap().upError));
}

bool uploadStored() { return g_upStored; }
const char *uploadError() { return heap().upError; }

}  // namespace addonloader
}  // namespace bbw

#endif  // BBW_EMULATOR
