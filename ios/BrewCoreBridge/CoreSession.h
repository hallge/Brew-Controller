#pragma once
// The one class Swift sees of the core. A CoreSession owns a bbw::BrewController, its settings and
// library, and either a simulated machine or the transport state for real devices.
//
// Threading: none inside. Every call, from Swift or from a test, is made on one serial queue (the
// same queue CoreBluetooth is given), as the core is single-threaded on the board too.
//
// The core's headers stay behind CoreSessionImpl, so Swift's C++ import sees this file only.
//
// Swift cannot subclass the core's virtual interfaces (IScale, IActuator, PusherWriter), so those are
// implemented in CoreSession.cpp: they forward to the simulator, or queue bytes as BridgeCommands for
// Swift to write.

#include <stddef.h>
#include <stdint.h>

#include "BridgeTypes.h"

#define BB_SWIFT_REF(retain, release)                \
  __attribute__((swift_attr("import_reference")))   \
  __attribute__((swift_attr("retain:" #retain)))    \
  __attribute__((swift_attr("release:" #release)))
#define BB_RETURNS_RETAINED __attribute__((swift_attr("returns_retained")))

struct CoreSessionImpl;
class CoreSession;
void bbCoreSessionRetain(CoreSession *session);
void bbCoreSessionRelease(CoreSession *session);

class BB_SWIFT_REF(bbCoreSessionRetain, bbCoreSessionRelease) CoreSession {
 public:
  // A new session on the simulated machine. Swift owns it (+1).
  static CoreSession *_Nonnull create() BB_RETURNS_RETAINED;
  // Milliseconds from a monotonic clock that does not run in sleep (CLOCK_UPTIME_RAW).
  static uint32_t uptimeMs();
  static const char *_Nonnull endReasonText(uint8_t endReason);
  static const char *_Nonnull stateText(uint8_t state);

  // The core is driven by this. In the app, from a 10 ms timer and after every notification; a
  // test passes its own clock.
  void tick(uint32_t nowMs);

  // The owner's intents (core: BrewController::request*).
  void requestStart();
  void requestStop();
  void requestTare();
  void acknowledge();

  LiveView live(uint32_t nowMs) const;
  ShotSummary lastShot() const;
  // The active recipe's target and dose. Takes effect with the next shot.
  void setRecipe(float targetG, float doseG);
  float recipeTargetG() const;
  float recipeDoseG() const;

  // ---- simulator ----
  // true (the default): a simulated Lunar, Bot and Breville. false: real devices, through the
  // transport functions below.
  void useSimulator(bool simulated);
  bool simulated() const;
  void setFault(uint8_t fault, bool on);
  bool faultActive(uint8_t fault) const;
  // Imitates the app being suspended: for `forMs` the world goes on (the machine runs, the cup fills)
  // but the core gets no ticks and no readings, then it is ticked with the time that passed.
  void pauseTicks(uint32_t forMs, uint32_t nowMs);

  // ---- probe log ----
  // Entries are numbered from 1. probeTotal() is the number of the newest one.
  uint32_t probeTotal() const;
  ProbeEntry probeAt(uint32_t seq) const;

  // ---- real devices: the transport's side ----
  uint8_t pendingCommands() const;
  BridgeCommand popCommand();
  uint32_t droppedCommands() const;
  // The scale's link is up and subscribed (`driverId` is the protocol id, "acaia" for a Lunar), or
  // gone. Up: the scale's init writes are queued.
  void scaleLink(bool up, const char *_Nonnull driverId, uint32_t nowMs);
  void onScaleBytes(const uint8_t *_Nonnull data, size_t length, uint32_t nowMs);
  // The Bot's link is up and subscribed (a SwitchBot Bot), or gone.
  void botLink(bool up);
  void onBotBytes(const uint8_t *_Nonnull data, size_t length, uint32_t nowMs);

 private:
  CoreSession();
  ~CoreSession();
  CoreSession(const CoreSession &) = delete;
  CoreSession &operator=(const CoreSession &) = delete;
  friend void bbCoreSessionRetain(CoreSession *);
  friend void bbCoreSessionRelease(CoreSession *);

  CoreSessionImpl *impl_;
  int refs_;
};
