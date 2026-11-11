#pragma once
// The add-on module file and the rules around it (docs/ADDONS.md, "Add-ons as modules"): portable, so the
// host tests run every refusal. The loader that uses it is platform/AddonLoader.cpp.
//
// A module file, `<id>.bbwmod`, is a 128-byte header followed by the ELF the module was linked to
// (scripts/build_module.py makes it):
//
//   0   char[4]  magic "BBWM"
//   4   u16      header size, 128
//   6   u16      ABI: BBW_ADDON_ABI the module was built for
//   8   u32      ELF size in bytes (at most MODULE_MAX_ELF)
//   12  u32      minimum firmware, packed (major << 16 | minor << 8 | patch), 0: any
//   16  char[32] add-on id
//   48  char[16] add-on version
//   64  u8[32]   SHA-256 of the ELF
//   96  u8[32]   reserved, zero
//
// Little endian (the board and every host that runs the tests).

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/Sha256.h"

namespace bbw {
namespace addon {
namespace module {

constexpr size_t HEADER_SIZE = 128;
constexpr size_t MODULE_MAX_ELF = 256UL * 1024UL;
constexpr size_t ID_MAX = 32;
constexpr size_t VERSION_MAX = 16;
constexpr char MAGIC[4] = {'B', 'B', 'W', 'M'};

struct Header {
  char magic[4];
  uint16_t headerSize;
  uint16_t abi;
  uint32_t elfSize;
  uint32_t minFirmware;
  char id[ID_MAX];
  char version[VERSION_MAX];
  uint8_t sha256[32];
  uint8_t reserved[32];
};
static_assert(sizeof(Header) == HEADER_SIZE, "the .bbwmod header is 128 bytes");

// "0.3.2", "v0.3.2" or "0.3.2-rc1" as major << 16 | minor << 8 | patch. 0: not a version ("dev", "").
inline uint32_t packVersion(const char *text) {
  if (!text) return 0;
  if (*text == 'v' || *text == 'V') ++text;
  unsigned part[3] = {0, 0, 0};
  int n = 0;
  while (n < 3) {
    if (*text < '0' || *text > '9') break;
    unsigned v = 0;
    while (*text >= '0' && *text <= '9') {
      v = v * 10 + static_cast<unsigned>(*text - '0');
      if (v > 255) v = 255;
      ++text;
    }
    part[n++] = v;
    if (*text != '.') break;
    ++text;
  }
  if (n == 0) return 0;
  return (part[0] << 16) | (part[1] << 8) | part[2];
}

// [a-z0-9-], 2 to 31 characters, starting with a letter or digit (the same rule as addon.json).
inline bool validId(const char *id) {
  if (!id) return false;
  const size_t n = strlen(id);
  if (n < 2 || n >= ID_MAX) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = id[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-' && i > 0);
    if (!ok) return false;
  }
  return true;
}

// A GitHub repository as "owner/name" (letters, digits, '.', '_', '-'), the only form the release fetch uses.
inline bool validRepo(const char *repo) {
  if (!repo) return false;
  const size_t n = strlen(repo);
  if (n < 3 || n > 63) return false;
  int slashes = 0;
  size_t partLen = 0;
  for (size_t i = 0; i < n; ++i) {
    const char c = repo[i];
    if (c == '/') {
      if (partLen == 0 || ++slashes > 1) return false;
      partLen = 0;
      continue;
    }
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                    c == '_' || c == '-';
    if (!ok) return false;
    ++partLen;
  }
  return slashes == 1 && partLen > 0 && strstr(repo, "..") == nullptr;
}

// "<id>.bbwmod" -> id. false: not such a name.
inline bool idFromAssetName(const char *name, char *id, size_t size) {
  if (!name || !id || size < ID_MAX) return false;
  const size_t n = strlen(name);
  static const char ext[] = ".bbwmod";
  if (n <= sizeof(ext) - 1 || strcmp(name + n - (sizeof(ext) - 1), ext) != 0) return false;
  const size_t idLen = n - (sizeof(ext) - 1);
  if (idLen >= ID_MAX) return false;
  memcpy(id, name, idLen);
  id[idLen] = '\0';
  return validId(id);
}

// The header of a file's first bytes, checked for what does not depend on this firmware: magic, sizes,
// the id. nullptr: it is a module file (and `out` is set), else why not. `fileSize` is the whole file's.
inline const char *parseHeader(const uint8_t *data, size_t length, size_t fileSize, Header &out) {
  if (!data || length < HEADER_SIZE) return "too short to be an add-on module";
  memcpy(&out, data, HEADER_SIZE);
  if (memcmp(out.magic, MAGIC, 4) != 0) return "not an add-on module file";
  if (out.headerSize != HEADER_SIZE) return "an add-on module header of another layout";
  out.id[ID_MAX - 1] = '\0';
  out.version[VERSION_MAX - 1] = '\0';
  if (!validId(out.id)) return "the module's id is not valid";
  if (out.elfSize == 0 || out.elfSize > MODULE_MAX_ELF) return "the module is empty or over 256 kB";
  if (fileSize != HEADER_SIZE + static_cast<size_t>(out.elfSize)) return "the module's size does not match its header";
  return nullptr;
}

// A whole file in memory: the header and the SHA-256 of the ELF behind it.
inline const char *verifyFile(const uint8_t *file, size_t size, Header &out) {
  if (const char *why = parseHeader(file, size, size, out)) return why;
  Sha256 sha;
  sha.update(file + HEADER_SIZE, out.elfSize);
  uint8_t digest[32];
  sha.finish(digest);
  if (memcmp(digest, out.sha256, 32) != 0) return "the module's checksum does not match";
  return nullptr;
}

enum class Fit : uint8_t { OK = 0, NEEDS_REBUILD = 1, NEEDS_FIRMWARE = 2 };

// Can this firmware start the module? `abi`: BBW_ADDON_ABI of this firmware. `firmware`: packVersion of
// the running one (0, a build by hand: every minimum is met).
inline Fit fits(const Header &h, uint16_t abi, uint32_t firmware) {
  if (h.abi != abi) return Fit::NEEDS_REBUILD;
  if (firmware != 0 && h.minFirmware > firmware) return Fit::NEEDS_FIRMWARE;
  return Fit::OK;
}

inline bool hexToBytes32(const char *hex, uint8_t out[32]) {
  for (int i = 0; i < 32; ++i) {
    unsigned v = 0;
    for (int k = 0; k < 2; ++k) {
      const char c = hex[2 * i + k];
      unsigned d;
      if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
      else return false;
      v = v * 16 + d;
    }
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

// The SHA-256 a release's SHA256SUMS.txt lists for `name` ("<64 hex>  name", as sha256sum writes it).
inline bool sumsLookup(const char *text, const char *name, uint8_t out[32]) {
  if (!text || !name || !name[0]) return false;
  const size_t nameLen = strlen(name);
  const char *p = text;
  while (*p) {
    const char *end = strchr(p, '\n');
    const size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
    if (len >= 64 + 2 + nameLen) {
      const char *file = p + 64;
      while (file < p + len && (*file == ' ' || *file == '*' || *file == '\t')) ++file;
      size_t fileLen = static_cast<size_t>(p + len - file);
      while (fileLen > 0 && (file[fileLen - 1] == '\r' || file[fileLen - 1] == ' ')) --fileLen;
      if (fileLen == nameLen && memcmp(file, name, nameLen) == 0 && hexToBytes32(p, out)) return true;
    }
    if (!end) break;
    p = end + 1;
  }
  return false;
}

// ------------------------------------------------------------------- crash guard --

// What the guard keeps across restarts (NVS "bbwaddon" on the board): two short lists of ids.
class GuardStore {
 public:
  virtual ~GuardStore() = default;
  virtual void get(const char *key, char *out, size_t size) = 0;   // "" when not set
  virtual void put(const char *key, const char *value) = 0;
};

// With no USB, a module that crashes the board on start must not do it again at every start.
//   "loading": the modules being loaded and begun, set before that and cleared after CLEAR_AFTER_MS of
//              normal running.
//   "blocked": modules that were in "loading" when the board came back after a panic or a watchdog
//              reset. They are not loaded again until the owner asks (retry()).
class Guard {
 public:
  static constexpr uint32_t CLEAR_AFTER_MS = 120UL * 1000UL;
  static constexpr size_t LIST_MAX = 128;

  explicit Guard(GuardStore &store) : store_(store) {}

  // Once at start-up, before anything is loaded. `crashReset`: the board came back after a panic or a
  // watchdog (not a power-on, a restart by the owner or an update).
  void boot(bool crashReset) {
    char loading[LIST_MAX];
    store_.get(KEY_LOADING, loading, sizeof(loading));
    if (!loading[0]) return;
    if (crashReset) {
      char blocked[LIST_MAX];
      store_.get(KEY_BLOCKED, blocked, sizeof(blocked));
      char id[ID_MAX];
      const char *p = loading;
      while (nextId(p, id)) add(blocked, sizeof(blocked), id);
      store_.put(KEY_BLOCKED, blocked);
    }
    store_.put(KEY_LOADING, "");
  }

  bool blocked(const char *id) const {
    char list[LIST_MAX];
    store_.get(KEY_BLOCKED, list, sizeof(list));
    return has(list, id);
  }

  // The owner asked to try the module again.
  void retry(const char *id) {
    char list[LIST_MAX];
    store_.get(KEY_BLOCKED, list, sizeof(list));
    remove(list, id);
    store_.put(KEY_BLOCKED, list);
  }

  // Before loading and beginning this module.
  void loading(const char *id, uint32_t nowMs) {
    char list[LIST_MAX];
    store_.get(KEY_LOADING, list, sizeof(list));
    add(list, sizeof(list), id);
    store_.put(KEY_LOADING, list);
    armed_ = true;
    sinceMs_ = nowMs;
  }

  // Every loop pass.
  void service(uint32_t nowMs) {
    if (!armed_ || static_cast<uint32_t>(nowMs - sinceMs_) < CLEAR_AFTER_MS) return;
    armed_ = false;
    store_.put(KEY_LOADING, "");
  }

  bool armed() const { return armed_; }

 private:
  static constexpr const char *KEY_LOADING = "loading";
  static constexpr const char *KEY_BLOCKED = "blocked";

  static bool nextId(const char *&p, char id[ID_MAX]) {
    while (*p == ',') ++p;
    if (!*p) return false;
    size_t n = 0;
    while (*p && *p != ',') {
      if (n < ID_MAX - 1) id[n++] = *p;
      ++p;
    }
    id[n] = '\0';
    return n > 0;
  }
  static bool has(const char *list, const char *id) {
    char item[ID_MAX];
    const char *p = list;
    while (nextId(p, item)) {
      if (strcmp(item, id) == 0) return true;
    }
    return false;
  }
  static void add(char *list, size_t size, const char *id) {
    if (!id || !id[0] || has(list, id)) return;
    const size_t n = strlen(list), k = strlen(id);
    if (n + k + 2 > size) return;
    if (n) list[n] = ',';
    memcpy(list + n + (n ? 1 : 0), id, k + 1);
  }
  static void remove(char *list, const char *id) {
    char out[LIST_MAX] = {};
    char item[ID_MAX];
    const char *p = list;
    while (nextId(p, item)) {
      if (strcmp(item, id) != 0) add(out, sizeof(out), item);
    }
    memcpy(list, out, sizeof(out));
  }

  GuardStore &store_;
  bool armed_ = false;
  uint32_t sinceMs_ = 0;
};

}  // namespace module
}  // namespace addon
}  // namespace bbw
