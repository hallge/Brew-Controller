#include "Backup.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <stddef.h>
#include <string.h>

#include "Log.h"
#include "Store.h"
#include "Version.h"
#include "core/BackupFormat.h"
#include "platform/BigMem.h"
#include "platform/FsUtil.h"

#ifndef BBW_EMULATOR
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace bbw {
namespace {

constexpr const char *NVS_NAMESPACE = "bbw";
// Every NVS record that is the owner's: settings, maintenance, pairing, update channel, cup
// memory, network, pushers, stop bot, machine, drinkers, the signing key, the log level. Not the
// firmware slot record ("slots") or the boot count: they belong to this board's flash.
constexpr const char *NVS_KEYS[] = {"cfg", "maint", "pairing", "otacfg", "cupmem", "net", "pushers",
                                    "stopbot", "machine", "drinkers", "devkey", "log"};
constexpr const char *MIRROR_PATH = "/nvs.bin";
constexpr const char *MIRROR_TMP_PATH = "/nvs.tmp";
constexpr size_t RECORD_MAX = 4096;     // the largest NVS record (Settings is about 1 kB)
constexpr size_t MIRROR_MAX = 16384;
constexpr size_t FILES_MAX = 900;       // a full history is 500 curves and a dozen other files

bool g_fsOk = false;
uint8_t *g_record;    // RECORD_MAX, PSRAM
uint8_t *g_mirror;    // MIRROR_MAX, the copy being built
uint8_t *g_mirrorOld; // MIRROR_MAX, the copy on the file system
char (*g_names)[BACKUP_NAME_MAX + 1];
uint32_t *g_sizes;

void allocate() {
  if (g_record) return;
  g_record = bigArray<uint8_t>(RECORD_MAX);
  g_mirror = bigArray<uint8_t>(MIRROR_MAX);
  g_mirrorOld = bigArray<uint8_t>(MIRROR_MAX);
  g_names = static_cast<char (*)[BACKUP_NAME_MAX + 1]>(bigAlloc(FILES_MAX * (BACKUP_NAME_MAX + 1)));
  g_sizes = bigArray<uint32_t>(FILES_MAX);
}

// The network record as NVS keeps it (Store.cpp saveBlob: a version, then the struct).
struct StoredNet {
  uint16_t version;
  NetConfig value;
};

// The passwords and the library token taken out of a network record.
void blankSecrets(uint8_t *record, size_t length) {
  if (length < sizeof(StoredNet)) {
    // An older, shorter record: only the fields it has.
    StoredNet full;
    memset(&full, 0, sizeof(full));
    memcpy(&full, record, length);
    blankSecrets(reinterpret_cast<uint8_t *>(&full), sizeof(full));
    memcpy(record, &full, length);
    return;
  }
  StoredNet &n = *reinterpret_cast<StoredNet *>(record);
  memset(n.value.staPassword, 0, sizeof(n.value.staPassword));
  memset(n.value.apPassword, 0, sizeof(n.value.apPassword));
  memset(n.value.botPassword, 0, sizeof(n.value.botPassword));
  memset(n.value.libraryToken, 0, sizeof(n.value.libraryToken));
}

// A backup without secrets restored: the passwords and token this controller has now are kept.
void keepOwnSecrets(uint8_t *record, size_t length) {
  StoredNet in, own;
  memset(&in, 0, sizeof(in));
  memset(&own, 0, sizeof(own));
  memcpy(&in, record, length < sizeof(in) ? length : sizeof(in));
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    const size_t have = prefs.isKey("net") ? prefs.getBytesLength("net") : 0;
    if (have > 0 && have <= sizeof(own)) prefs.getBytes("net", &own, have);
    prefs.end();
  }
  auto keep = [](char *field, const char *mine, size_t size) {
    if (field[0] == '\0' && mine[0] != '\0') memcpy(field, mine, size);
    field[size - 1] = '\0';
  };
  keep(in.value.staPassword, own.value.staPassword, sizeof(in.value.staPassword));
  keep(in.value.apPassword, own.value.apPassword, sizeof(in.value.apPassword));
  keep(in.value.botPassword, own.value.botPassword, sizeof(in.value.botPassword));
  keep(in.value.libraryToken, own.value.libraryToken, sizeof(in.value.libraryToken));
  if (in.value.apPassword[0] == '\0') strncpy(in.value.apPassword, NetConfig().apPassword, sizeof(in.value.apPassword) - 1);
  memcpy(record, &in, length < sizeof(in) ? length : sizeof(in));
}

// The NVS records, each read into g_record and handed to visit(key, length).
template <typename Visit>
void forEachNvsRecord(Visit visit) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) return;
  for (const char *key : NVS_KEYS) {
    const size_t length = prefs.isKey(key) ? prefs.getBytesLength(key) : 0;
    if (length == 0) continue;
    if (length > RECORD_MAX || prefs.getBytes(key, g_record, length) != length) {
      LOGW("backup: NVS record %s not read (%u bytes)", key, static_cast<unsigned>(length));
      continue;
    }
    visit(key, length);
  }
  prefs.end();
}

// What a backup leaves out: the log, files being written, the copy of NVS (its records are in the
// backup already) and what the shared library is fetched again from.
bool skippedFile(const char *name) {
  const size_t n = strlen(name);
  auto endsWith = [&](const char *tail) {
    const size_t t = strlen(tail);
    return n >= t && strcmp(name + n - t, tail) == 0;
  };
  if (strcmp(name, "/log.txt") == 0 || strcmp(name, "/log.old") == 0 || strcmp(name, MIRROR_PATH) == 0 ||
      strcmp(name, RESTORE_PATH) == 0 ||
      strcmp(name, "/diag.bin") == 0 || strcmp(name, "/diagcrash.json") == 0) {
    return true;
  }
  if (endsWith(".tmp") || endsWith(".rs")) return true;
  // The library's caches: /l<name>.json and /dir<x>.json, but not the owner's own files.
  if (strncmp(name, "/dir", 4) == 0 && endsWith(".json")) return true;
  if (name[1] == 'l' && endsWith(".json") && strcmp(name, "/links.json") != 0 &&
      strcmp(name, "/libreview.json") != 0) {
    return true;
  }
  return false;
}

// The shot history: shots, their curves and contexts, and the copies an older history format was
// kept in while it was converted (/shots.v8.bak...).
bool historyFile(const char *name) {
  if (strcmp(name, "/shots.bin") == 0 || strcmp(name, "/shots.csv") == 0 || strcmp(name, "/shot.csv") == 0 ||
      strcmp(name, "/contexts.bin") == 0 || strncmp(name, "/shots.", 7) == 0) {
    return true;
  }
  if (name[1] != 'c') return false;
  size_t i = 2;
  while (name[i] >= '0' && name[i] <= '9') ++i;
  return i > 2 && strcmp(name + i, ".bin") == 0;
}

// The files at the top of the file system, into g_names / g_sizes. Returns how many.
size_t listFiles() {
  size_t count = 0;
#ifdef BBW_EMULATOR
  LittleFS.list([](const char *path, size_t size, void *context) {
    size_t &n = *static_cast<size_t *>(context);
    if (n >= FILES_MAX || strlen(path) > BACKUP_NAME_MAX) return;
    strncpy(g_names[n], path, BACKUP_NAME_MAX);
    g_names[n][BACKUP_NAME_MAX] = '\0';
    g_sizes[n++] = static_cast<uint32_t>(size);
  }, &count);
#else
  DIR *dir = opendir("/littlefs");  // where Store::begin() mounts it
  if (!dir) return 0;
  char full[96];
  while (struct dirent *e = readdir(dir)) {
    if (count >= FILES_MAX || e->d_name[0] == '.' || strlen(e->d_name) + 1 > BACKUP_NAME_MAX) continue;
    snprintf(full, sizeof(full), "/littlefs/%s", e->d_name);
    struct stat info;
    if (stat(full, &info) != 0 || !S_ISREG(info.st_mode)) continue;
    snprintf(g_names[count], BACKUP_NAME_MAX + 1, "/%s", e->d_name);
    g_sizes[count++] = static_cast<uint32_t>(info.st_size);
  }
  closedir(dir);
#endif
  return count;
}

struct MemoryOut {
  uint8_t *data;
  size_t capacity, used = 0;
  bool overflow = false;
  void operator()(const uint8_t *p, size_t n) {
    if (used + n > capacity) {
      overflow = true;
      return;
    }
    memcpy(data + used, p, n);
    used += n;
  }
};

struct CallbackOut {
  void (*out)(const uint8_t *, size_t, void *);
  void *context;
  void operator()(const uint8_t *p, size_t n) { out(p, n, context); }
};

// A file as a backup Source.
struct FileSource {
  File file;
  size_t read(uint8_t *out, size_t length) { return file.read(out, length); }
  bool seek(size_t to) { return file.seek(to); }
};

void readKeyId(char *out, size_t size) {
  out[0] = '\0';
  File f = fileExists("/sharekey.txt") ? LittleFS.open("/sharekey.txt", "r") : File();
  if (!f) return;
  const size_t n = f.read(reinterpret_cast<uint8_t *>(out), size - 1);
  f.close();
  out[n] = '\0';
  for (size_t i = 0; i < n; ++i) {
    if (out[i] == '\n' || out[i] == '\r') out[i] = '\0';
  }
}

void fillHeader(BackupHeader &h, uint16_t flags, uint32_t epochS) {
  h = BackupHeader();
  h.flags = flags;
  h.createdEpochS = epochS;
  strncpy(h.firmware, BBW_VERSION, sizeof(h.firmware) - 1);
  readKeyId(h.keyId, sizeof(h.keyId));
}

}  // namespace

void backupFsReady(bool ok) {
  g_fsOk = ok;
  if (ok) allocate();
}

void mirrorNvs() {
  if (!g_fsOk) return;
  MemoryOut out{g_mirror, MIRROR_MAX};
  BackupWriter<MemoryOut> w(out);
  BackupHeader h;
  fillHeader(h, BACKUP_SECRETS, 0);  // no time: the same records give the same bytes
  w.header(h);
  size_t records = 0;
  forEachNvsRecord([&](const char *key, size_t length) {
    w.entry(BACKUP_ENTRY_NVS, key, g_record, static_cast<uint32_t>(length));
    ++records;
  });
  w.finish();
  if (out.overflow || records == 0) return;
  // Unchanged: nothing written (flash wear).
  File old = fileExists(MIRROR_PATH) ? LittleFS.open(MIRROR_PATH, "r") : File();
  if (old) {
    const size_t n = old.size() == out.used ? old.read(g_mirrorOld, out.used) : 0;
    old.close();
    if (n == out.used && memcmp(g_mirrorOld, g_mirror, out.used) == 0) return;
  }
  File f = LittleFS.open(MIRROR_TMP_PATH, "w");
  if (!f) return;
  const bool ok = f.write(g_mirror, out.used) == out.used;
  f.close();
  if (!ok || !LittleFS.rename(MIRROR_TMP_PATH, MIRROR_PATH)) {
    LittleFS.remove(MIRROR_TMP_PATH);
    LOGW("backup: copy of the settings not written");
  }
}

bool restoreNvsFromMirror() {
  if (!g_fsOk || !fileExists(MIRROR_PATH)) return false;
  {
    Preferences prefs;
    const bool has = prefs.begin(NVS_NAMESPACE, true) && prefs.isKey("cfg");
    prefs.end();
    if (has) return false;
  }
  File f = LittleFS.open(MIRROR_PATH, "r");
  if (!f) return false;
  const size_t size = f.size();
  const size_t n = size <= MIRROR_MAX ? f.read(g_mirrorOld, size) : 0;
  f.close();
  BackupMemory src(g_mirrorOld, n);
  BackupHeader h;
  if (n != size || backupCheck(src, n, h) != nullptr) {
    LOGW("backup: the copy of the settings is damaged, not used");
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) return false;
  unsigned restored = 0;
  backupEntries(src, n, [&](uint8_t type, const char *name, uint32_t length, BackupMemory &s) {
    if (type != BACKUP_ENTRY_NVS || length > RECORD_MAX || s.read(g_record, length) != length) return true;
    if (prefs.putBytes(name, g_record, length) == length) ++restored;
    return true;
  });
  prefs.end();
  LOGW("backup: NVS was empty; %u settings records restored from the file system copy", restored);
  return restored != 0;
}

size_t writeBackup(const BackupOptions &options, void (*out)(const uint8_t *, size_t, void *), void *context) {
  if (!g_fsOk) return 0;
  CallbackOut sink{out, context};
  BackupWriter<CallbackOut> w(sink);
  BackupHeader h;
  fillHeader(h, static_cast<uint16_t>((options.secrets ? BACKUP_SECRETS : 0) | (options.history ? BACKUP_HISTORY : 0)),
             options.epochS);
  w.header(h);
  forEachNvsRecord([&](const char *key, size_t length) {
    if (!options.secrets && strcmp(key, "devkey") == 0) return;
    if (!options.secrets && strcmp(key, "net") == 0) blankSecrets(g_record, length);
    w.entry(BACKUP_ENTRY_NVS, key, g_record, static_cast<uint32_t>(length));
  });
  const size_t count = listFiles();
  for (size_t i = 0; i < count; ++i) {
    const char *name = g_names[i];
    if (skippedFile(name) || (!options.history && historyFile(name))) continue;
    File f = LittleFS.open(name, "r");
    if (!f) continue;
    const uint32_t length = static_cast<uint32_t>(f.size());
    if (!w.beginEntry(BACKUP_ENTRY_FILE, name, length)) {
      f.close();
      continue;
    }
    uint32_t left = length;
    while (left > 0) {
      const size_t want = left < RECORD_MAX ? left : RECORD_MAX;
      size_t got = f.read(g_record, want);
      // A file that shrank meanwhile: padded, so the entry keeps the length it was given.
      if (got < want) memset(g_record + got, 0, want - got), got = want;
      w.raw(g_record, got);
      left -= static_cast<uint32_t>(got);
    }
    f.close();
    esp_task_wdt_reset();
  }
  w.finish();
  return w.written;
}

RestoreSummary checkRestore() {
  RestoreSummary s;
  s.problem = "no backup file was received";
  if (!g_fsOk || !fileExists(RESTORE_PATH)) return s;
  FileSource src{LittleFS.open(RESTORE_PATH, "r")};
  if (!src.file) return s;
  const size_t total = src.file.size();
  BackupHeader h;
  s.problem = backupCheck(src, total, h);
  if (!s.problem) {
    s.bytes = static_cast<uint32_t>(total);
    s.createdEpochS = h.createdEpochS;
    memcpy(s.firmware, h.firmware, sizeof(s.firmware));
    memcpy(s.keyId, h.keyId, sizeof(s.keyId));
    s.secrets = h.flags & BACKUP_SECRETS;
    s.history = h.flags & BACKUP_HISTORY;
    const bool ok = backupEntries(src, total, [&](uint8_t type, const char *, uint32_t, FileSource &) {
      if (type == BACKUP_ENTRY_NVS) ++s.records;
      else ++s.files;
      return true;
    });
    if (!ok) s.problem = "the file is damaged (an entry does not read)";
    esp_task_wdt_reset();
  }
  src.file.close();
  s.ok = s.problem == nullptr;
  return s;
}

const char *applyRestore() {
  const RestoreSummary s = checkRestore();
  if (!s.ok) return s.problem;
  FileSource src{LittleFS.open(RESTORE_PATH, "r")};
  if (!src.file) return "no backup file was received";
  const size_t total = src.file.size();
  // A backup with the history replaces it: the curves of shots it does not have go.
  if (s.history) {
    const size_t count = listFiles();
    for (size_t i = 0; i < count; ++i) {
      if (historyFile(g_names[i])) LittleFS.remove(g_names[i]);
    }
  }
  Preferences prefs;
  const bool nvsOk = prefs.begin(NVS_NAMESPACE, false);
  unsigned records = 0, files = 0, failed = 0;
  backupEntries(src, total, [&](uint8_t type, const char *name, uint32_t length, FileSource &in) {
    if (type == BACKUP_ENTRY_NVS) {
      if (!nvsOk || length > RECORD_MAX || in.read(g_record, length) != length) {
        ++failed;
        return true;
      }
      if (!s.secrets && strcmp(name, "net") == 0) keepOwnSecrets(g_record, length);
      if (prefs.putBytes(name, g_record, length) == length) ++records;
      else ++failed;
      return true;
    }
    // A file: written beside the one there and swapped in.
    char tmp[BACKUP_NAME_MAX + 4];
    snprintf(tmp, sizeof(tmp), "%s.rs", name);
    File f = LittleFS.open(tmp, "w");
    bool ok = static_cast<bool>(f);
    uint32_t left = length;
    while (ok && left > 0) {
      const size_t want = left < RECORD_MAX ? left : RECORD_MAX;
      ok = in.read(g_record, want) == want && f.write(g_record, want) == want;
      left -= static_cast<uint32_t>(want);
    }
    if (f) f.close();
    if (ok && LittleFS.rename(tmp, name)) {
      ++files;
    } else {
      LittleFS.remove(tmp);
      ++failed;
    }
    esp_task_wdt_reset();
    return true;
  });
  if (nvsOk) prefs.end();
  src.file.close();
  LittleFS.remove(RESTORE_PATH);
  LOGW("backup: restored %u settings records and %u files (%u failed), from %s", records, files, failed,
       s.firmware);
  return failed ? "restored, but some parts could not be written (see the log)" : nullptr;
}

void discardRestore() {
  if (g_fsOk) LittleFS.remove(RESTORE_PATH);
}

}  // namespace bbw
