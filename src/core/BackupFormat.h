#pragma once
// One file holding a controller's settings and data: the NVS records (settings, Wi-Fi, pairing,
// machine, the signing key...) and the files on the file system (library, shots...). Used for the
// copy of NVS kept on the file system (storage/Backup.cpp, /nvs.bin) and for Setup's backup.
//
//   BackupHeader
//   entries: type (1 byte), name length (1), name, data length (4), data
//   trailer: BACKUP_END, CRC-32 of everything before the trailer
//
// Each record is stored as it is on the controller: records only ever grow at the end and say
// how long they are (Store.cpp loadBlob, Library.h decodeLibrary), so a backup restores onto the
// same firmware or a newer one.
// Portable; no flash or network access in here.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace bbw {

constexpr uint32_t BACKUP_MAGIC = 0x42574242;  // "BBWB"
constexpr uint32_t BACKUP_END = 0x444E4542;    // "BEND"
constexpr uint16_t BACKUP_FORMAT = 1;
enum BackupFlags : uint16_t {
  BACKUP_SECRETS = 1u << 0,  // passwords, the library token and the signing key are in it
  BACKUP_HISTORY = 1u << 1,  // the shot history is in it
};
enum BackupEntryType : uint8_t { BACKUP_ENTRY_NVS = 1, BACKUP_ENTRY_FILE = 2 };
constexpr size_t BACKUP_NAME_MAX = 48;
constexpr size_t BACKUP_ENTRY_HEADER_MAX = 2 + BACKUP_NAME_MAX + 4;

struct BackupHeader {
  uint32_t magic = BACKUP_MAGIC;
  uint16_t format = BACKUP_FORMAT;
  uint16_t flags = 0;
  uint32_t createdEpochS = 0;
  char firmware[24] = {};
  char keyId[24] = {};  // the signing key's id (20 hex digits), as /sharekey.txt has it
};
static_assert(sizeof(BackupHeader) == 60, "BackupHeader is written as it is");

struct Crc32 {
  uint32_t state = 0xFFFFFFFFu;
  void add(const void *data, size_t length) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < length; ++i) {
      state ^= p[i];
      for (int bit = 0; bit < 8; ++bit) state = (state >> 1) ^ (0xEDB88320u & (0u - (state & 1u)));
    }
  }
  uint32_t value() const { return ~state; }
};

// A name this format may carry: an NVS key (a-z, 0-9, _; at most 15) or a file at the top of the
// file system ("/" then A-Z, a-z, 0-9, ., _, -; no "..").
inline bool backupNameOk(uint8_t type, const char *name) {
  const size_t n = name ? strlen(name) : 0;
  if (type == BACKUP_ENTRY_NVS) {
    if (n == 0 || n > 15) return false;
    for (size_t i = 0; i < n; ++i) {
      const char c = name[i];
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
  }
  if (type != BACKUP_ENTRY_FILE || n < 2 || n > BACKUP_NAME_MAX || name[0] != '/') return false;
  for (size_t i = 1; i < n; ++i) {
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return strstr(name, "..") == nullptr;
}

// Writes the format to `out(data, length)`, keeping the CRC.
template <typename Out>
struct BackupWriter {
  Out &out;
  Crc32 crc;
  size_t written = 0;
  explicit BackupWriter(Out &o) : out(o) {}
  void raw(const void *data, size_t length) {
    crc.add(data, length);
    out(static_cast<const uint8_t *>(data), length);
    written += length;
  }
  void header(const BackupHeader &h) { raw(&h, sizeof(h)); }
  // false (nothing written) for a name the format may not carry.
  bool beginEntry(uint8_t type, const char *name, uint32_t length) {
    if (!backupNameOk(type, name)) return false;
    const uint8_t head[2] = {type, static_cast<uint8_t>(strlen(name))};
    raw(head, 2);
    raw(name, head[1]);
    raw(&length, sizeof(length));
    return true;
  }
  bool entry(uint8_t type, const char *name, const void *data, uint32_t length) {
    if (!beginEntry(type, name, length)) return false;
    raw(data, length);
    return true;
  }
  void finish() {
    const uint32_t trailer[2] = {BACKUP_END, crc.value()};
    out(reinterpret_cast<const uint8_t *>(trailer), sizeof(trailer));
    written += sizeof(trailer);
  }
};

// Reading: `Source` has size_t read(uint8_t *, size_t) and bool seek(size_t).
// Checks the header, the trailer and the CRC over the whole file. nullptr when good, else why not.
template <typename Source>
const char *backupCheck(Source &src, size_t total, BackupHeader &header) {
  if (total < sizeof(BackupHeader) + 8) return "not a Brew Controller backup";
  if (!src.seek(0) || src.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header) ||
      header.magic != BACKUP_MAGIC) {
    return "not a Brew Controller backup";
  }
  if (header.format > BACKUP_FORMAT) return "made by a newer firmware: update this controller first";
  header.firmware[sizeof(header.firmware) - 1] = header.keyId[sizeof(header.keyId) - 1] = '\0';
  Crc32 crc;
  crc.add(&header, sizeof(header));
  uint8_t buffer[256];
  size_t at = sizeof(header);
  const size_t body = total - 8;
  while (at < body) {
    const size_t want = body - at < sizeof(buffer) ? body - at : sizeof(buffer);
    if (src.read(buffer, want) != want) return "the file is cut short";
    crc.add(buffer, want);
    at += want;
  }
  uint32_t trailer[2] = {0, 0};
  if (src.read(reinterpret_cast<uint8_t *>(trailer), sizeof(trailer)) != sizeof(trailer) ||
      trailer[0] != BACKUP_END) {
    return "the file is cut short";
  }
  if (trailer[1] != crc.value()) return "the file is damaged (its check does not match)";
  return nullptr;
}

// Calls visit(type, name, length, src) for each entry, after backupCheck. The visitor reads
// exactly `length` bytes from src (or none: they are skipped). false if an entry is malformed or
// the visitor says stop.
template <typename Source, typename Visit>
bool backupEntries(Source &src, size_t total, Visit visit) {
  size_t at = sizeof(BackupHeader);
  const size_t body = total - 8;
  while (at < body) {
    uint8_t head[2];
    char name[BACKUP_NAME_MAX + 1];
    uint32_t length = 0;
    if (!src.seek(at) || src.read(head, 2) != 2 || head[1] == 0 || head[1] > BACKUP_NAME_MAX ||
        src.read(reinterpret_cast<uint8_t *>(name), head[1]) != head[1]) {
      return false;
    }
    name[head[1]] = '\0';
    if (src.read(reinterpret_cast<uint8_t *>(&length), sizeof(length)) != sizeof(length)) return false;
    const size_t dataAt = at + 2 + head[1] + sizeof(length);
    if (dataAt > body || length > body - dataAt || !backupNameOk(head[0], name)) return false;
    if (!visit(head[0], static_cast<const char *>(name), length, src)) return false;
    at = dataAt + length;
  }
  return at == body;
}

// An in-memory Source, for the NVS copy and the tests.
struct BackupMemory {
  const uint8_t *data;
  size_t size;
  size_t at = 0;
  BackupMemory(const uint8_t *d, size_t s) : data(d), size(s) {}
  bool seek(size_t to) {
    if (to > size) return false;
    at = to;
    return true;
  }
  size_t read(uint8_t *out, size_t length) {
    const size_t n = size - at < length ? size - at : length;
    memcpy(out, data + at, n);
    at += n;
    return n;
  }
};

}  // namespace bbw
