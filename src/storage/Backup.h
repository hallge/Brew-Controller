#pragma once
// Settings that survive a reflash, and Setup's backup and restore (core/BackupFormat.h).
//
// NVS holds the settings, Wi-Fi, pairing, machine and the signing key. A whole-image flash or a
// fault can wipe it while the file system keeps everything else, so a copy of NVS is kept on the
// file system (/nvs.bin) and put back at start-up when NVS comes up empty.

#include <stddef.h>
#include <stdint.h>

namespace bbw {

// The file system is mounted (Store::begin); nothing here works before.
void backupFsReady(bool ok);
// Rewrites /nvs.bin from NVS when it differs (loop task: after every NVS save, and at start-up).
void mirrorNvs();
// At start-up, before anything reads NVS: NVS with no settings and a good copy on the file system
// gets the copy back. true if it did.
bool restoreNvsFromMirror();

struct BackupOptions {
  bool history = true;   // shots, their curves and contexts
  bool secrets = false;  // passwords, the library token, the signing key
  uint32_t epochS = 0;
};
// Streams a backup to out(data, length, context). Returns the bytes written.
size_t writeBackup(const BackupOptions &options, void (*out)(const uint8_t *, size_t, void *), void *context);

// The restore file being uploaded, then checked (POST /api/restore).
constexpr const char *RESTORE_PATH = "/restore.tmp";
struct RestoreSummary {
  bool ok = false;
  const char *problem = nullptr;  // why not, when !ok
  uint32_t createdEpochS = 0;
  char firmware[24] = {};
  char keyId[24] = {};
  bool history = false, secrets = false;
  uint16_t records = 0, files = 0;
  uint32_t bytes = 0;
};
RestoreSummary checkRestore();
// Writes everything in the checked file back (the caller restarts straight after). nullptr when
// done, else why not. NVS records a backup without secrets lacks keep this controller's own.
const char *applyRestore();
void discardRestore();

}  // namespace bbw
