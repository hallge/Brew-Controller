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

#include "core/CoffeeTaste.h"
#include "core/MachineProfile.h"
#include "core/PowerControl.h"
#include "ble/Ble.h"
#include "core/Library.h"
#include "core/ShotData.h"
#include "platform/FirmwareUpdate.h"
#include "core/Maintenance.h"
#include "core/ShotContext.h"
#include "core/Types.h"

namespace bbw {

struct NetConfig {
  char staSsid[33] = {};
  char staPassword[65] = {};
  char apPassword[33] = "brewbyweight";  // WPA2, change it in the web UI
  bool webStartEnabled = false;          // allow starting a shot from the browser
  char botPassword[24] = {};             // only if one was set in the SwitchBot app
  // The shared library (platform/LibraryLink.h): the repository, and a token
  // good for that repository alone. Added at the end: a blob saved before
  // they existed still loads.
  char libraryRepo[64] = {};             // "owner/name"
  char libraryToken[128] = {};           // never sent back to the web page
  // Where diagnostics reports go, for testing (a relay run on a PC); empty: the built-in relay.
  char diagRelay[96] = {};
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
  UpdateConfig loadUpdateConfig();
  void saveUpdateConfig(const UpdateConfig &config);
  CupMemory loadCupMemory();
  void saveCupMemory(const CupMemory &memory);
  Maintenance loadMaintenance();
  void saveMaintenance(const Maintenance &m);
  NetConfig loadNet();
  void saveNet(const NetConfig &net);
  // Power bot, pusher keys and the power schedule (core/PowerControl.h).
  PusherConfig loadPushers();
  void savePushers(const PusherConfig &config);
  // The espresso machine's profile (core/MachineProfile.h). The Breville
  // Dual Boiler's MANUAL button if none was ever stored.
  StopBotConfig loadStopBot();
  void saveStopBot(const StopBotConfig &config);
  MachineProfile loadMachine();
  void saveMachine(const MachineProfile &machine);

  bool appendShot(const ShotRecord &record);
  uint32_t shotCount() const { return header_.count; }
  bool readShot(uint32_t newestIndex, ShotRecord &out);  // 0 = most recent
  // Newest first, one file open for the whole pass. The callback returns
  // false to stop early.
  void scanShots(uint32_t maxShots, bool (*visit)(const ShotRecord &, void *), void *context);
  uint32_t nextShotId() const { return header_.nextId; }
  bool setShotGrind(uint32_t id, uint16_t grindUm);  // correct a shot after the fact
  bool setShotRating(uint32_t id, uint8_t rating, uint8_t taste);
  // The strength the owner measured with a refractometer, in hundredths of a percent TDS; 0 clears it.
  bool setShotTds(uint32_t id, uint16_t tdsX100);
  bool setShotGrindVote(uint32_t id, uint8_t vote);
  void clearShots();
  // The shot's full data set. Kept for as long as the shot is in the history.
  bool saveCurve(uint32_t id, const CurvePoint *points, size_t count);
  size_t loadCurve(uint32_t id, CurvePoint *out, size_t max);  // 0: none stored
  bool hasCurve(uint32_t id);
  // The circumstances of the shot (core/ShotContext.h): one fixed slot per shot in a ring file
  // beside the history (/contexts.bin, 500 x 64 bytes). Only recorded, never read by the advice.
  bool saveContext(const ShotContext &context);
  bool loadContext(uint32_t id, ShotContext &out);  // false: none stored, or another version
  // The owner's entries for a shot still in the history (core/ShotContext.h applyShotPrep). A shot
  // from before contexts were kept gets a context holding only these. false: no such shot, or a dose
  // outside 5..40 g.
  bool setShotPrep(uint32_t id, uint16_t doseDg, bool purged, bool uneven);
  bool historyOk() const { return fsOk_; }

  // The power presses the controller made (core/PowerControl.h PowerEvent), in a ring file
  // (/power.bin, 500 x 8 bytes): the oldest goes when it is full.
  static constexpr uint32_t POWER_EVENT_CAPACITY = 500;

  // Coffee ratings (core/CoffeeTaste.h), in a ring file (/ratings.bin, 500 x 32 bytes). saveRating
  // replaces the drinker's rating of the same bag (or the one with r.id) and gives a new one an id;
  // false: not kept (nothing to keep, or flash trouble).
  static constexpr uint32_t RATING_CAPACITY = 500;
  bool saveRating(CoffeeRating &r);
  bool deleteRating(uint32_t id);
  bool setRatingShare(uint32_t id, uint8_t shareState, uint32_t shareSeq);
  // Newest first; the callback returns false to stop early.
  void scanRatings(bool (*visit)(const CoffeeRating &, void *), void *context);
  Drinkers loadDrinkers();
  void saveDrinkers(const Drinkers &drinkers);
  bool appendPowerEvent(const PowerEvent &event);
  // Newest first. The callback returns false to stop early.
  void scanPowerEvents(bool (*visit)(const PowerEvent &, void *), void *context);

  // Small text files: what was last read from the shared library, kept for
  // when it cannot be reached.
  size_t readText(const char *path, char *out, size_t size);  // 0: not there
  bool writeTextIfChanged(const char *path, const char *text);
  // The file holds `text` afterwards: written, or the same already. false only when it could not be
  // written (for files the controller owns, where "unchanged" is not news).
  bool saveText(const char *path, const char *text);

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
  void outboxForgetKey();  // the public key is to be sent again, ahead of the next record

 private:
  int writeText(const char *path, const char *text);  // 1 written, 0 already so, -1 failed
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
  bool convertHistoryV9();
  void removeCurve(uint32_t id);
  bool prepareContexts();
  bool preparePowerLog();
  bool prepareRatings();
  bool editRatingSlot(uint32_t id, const CoffeeRating *with);
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
