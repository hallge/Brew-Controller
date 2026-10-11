#pragma once
// Persistence: settings/pairing/network in NVS, the library (coffees, recipes,
// presets) and the shot history in LittleFS files: the history as a
// fixed-size ring of ShotRecord, and next to it one small file per shot with
// every reading of that shot (core/ShotData.h). No SD card involved.
//
// begin() also brings forward what an earlier build stored: settings version
// 7 becomes settings plus library, history version 8 is rewritten as version
// 9. What was there is kept: the old settings stay in NVS under their own
// key, the old history as /shots.v8.bak.

#include <Arduino.h>

#include "core/PowerControl.h"
#include "ble/Ble.h"
#include "core/Library.h"
#include "core/ShotData.h"
#include "core/Types.h"

namespace bbw {

struct NetConfig {
  char staSsid[33] = {};
  char staPassword[65] = {};
  char apPassword[33] = "brewbyweight";  // WPA2, change it in the web UI
  bool webStartEnabled = false;          // allow starting a shot from the browser
  char botPassword[24] = {};             // only if one was set in the SwitchBot app
};

class Store {
 public:
  static constexpr uint32_t HISTORY_CAPACITY = 500;

  bool begin();

  Settings loadSettings();
  void saveSettings(const Settings &settings);
  void loadLibrary(Library &out);  // defaults if nothing usable is stored
  bool saveLibrary(const Library &library);
  Pairing loadPairing();
  void savePairing(const Pairing &pairing);
  NetConfig loadNet();
  void saveNet(const NetConfig &net);
  // Power bot, pusher keys and the power schedule (core/PowerControl.h).
  PusherConfig loadPushers();
  void savePushers(const PusherConfig &config);

  bool appendShot(const ShotRecord &record);
  uint32_t shotCount() const { return header_.count; }
  bool readShot(uint32_t newestIndex, ShotRecord &out);  // 0 = most recent
  // Newest first, one file open for the whole pass. The callback returns
  // false to stop early.
  void scanShots(uint32_t maxShots, bool (*visit)(const ShotRecord &, void *), void *context);
  uint32_t nextShotId() const { return header_.nextId; }
  bool setShotGrind(uint32_t id, uint16_t grindUm);  // correct a shot after the fact
  bool setShotRating(uint32_t id, uint8_t rating, uint8_t taste);
  bool setShotGrindVote(uint32_t id, uint8_t vote);
  void clearShots();
  // The shot's full data set. Kept for as long as the shot is in the history.
  bool saveCurve(uint32_t id, const CurvePoint *points, size_t count);
  size_t loadCurve(uint32_t id, CurvePoint *out, size_t max);  // 0: none stored
  bool hasCurve(uint32_t id);
  bool historyOk() const { return fsOk_; }

  // Small text files: what was last read from the shared library, kept for
  // when it cannot be reached.
  size_t readText(const char *path, char *out, size_t size);  // 0: not there
  bool writeTextIfChanged(const char *path, const char *text);

  // Outbox: records waiting to go to the shared library, oldest first. Each
  // is a small file; this survives restarts and being offline.
  static constexpr uint32_t OUTBOX_CAPACITY = 300;
  bool outboxAdd(const char *path, const char *text);  // false: full, or flash trouble
  bool outboxPeek(char *path, size_t pathSize, char *text, size_t textSize);
  void outboxPop();  // the one just peeked has been sent
  uint32_t outboxWaiting() const { return outbox_.next - outbox_.sent; }
  uint32_t outboxSent() const { return outbox_.sent; }
  uint32_t outboxNext() const { return outbox_.next; }
  bool outboxKeyQueued() const { return outbox_.keyQueued != 0; }

 private:
  struct Header {
    uint32_t magic;
    uint16_t version;
    uint16_t recordSize;
    uint32_t head;   // slot the next record goes into
    uint32_t count;
    uint32_t nextId;
  };
  bool writeHeader();
  bool editShot(uint32_t id, void (*edit)(ShotRecord &, void *), void *context);
  void resetHistory();
  void prepareLibrary();
  bool convertHistoryV8();
  void removeCurve(uint32_t id);
  void makeRoomForCurve(size_t bytes);

  struct OutboxState {
    uint32_t magic;
    uint32_t next;       // number the next record gets
    uint32_t sent;       // records below this have gone
    uint32_t keyQueued;  // the public key has been put in the outbox
  };
  void saveOutboxState();
  OutboxState outbox_ = {};

  Header header_ = {};
  bool fsOk_ = false;
};

}  // namespace bbw
