#pragma once
// The add-on interface (docs/ADDONS.md): what an optional driver compiled into a controller image
// may implement, and the one thing it is handed, Host.
//
// An add-on is not in the public firmware. It lives in its own folder (a private repository, a local
// folder), is listed at build time (scripts/addons.py), and is switched on by the owner on the web
// page. Nothing here runs unless it is switched on, and nothing here is on the path that stops a
// shot: add-ons are ticked by the loop task, never by the control task.
//
// Portable: no Arduino, JSON or file access, so add-ons and their policy build in the host tests.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <atomic>

namespace bbw {
namespace addon {

// Raised when this interface changes in a way an add-on built against an older one would get wrong.
// An add-on built for another version is listed but not started.
// 2: ShotReport and MachineState grew; Host gained notice, chipFree, requestShotStart and the file calls;
// Addon gained cardJson. The same number is ADDON_ABI in AddonAbi.h.
constexpr uint16_t ADDON_API_VERSION = 2;

enum class Kind : uint8_t { GRINDER = 1 };  // machines: a later round (docs/ADDONS.md)

// ------------------------------------------------------------------ settings --

enum class FieldType : uint8_t {
  TEXT = 0,
  NUMBER = 1,
  BOOL = 2,    // "1" / "0"
  URL = 3,     // the only addresses the add-on may fetch from (Host::fetchStart)
  SECRET = 4,  // write-only: never returned to the web page, kept out of backups
};

struct ConfigField {
  const char *key;    // [a-z0-9_], at most 23 characters
  const char *label;  // as the Add-ons card shows it
  FieldType type;
  const char *def;    // default value, "" for none
  const char *help;   // one line under the field, "" for none
};

constexpr size_t MAX_FIELDS = 10;
constexpr size_t VALUE_LEN = 128;
constexpr size_t MAX_ROUTES = 2;

struct AddonInfo {
  const char *id;       // [a-z0-9-], as in addon.json
  const char *name;
  const char *version;
  uint16_t api;         // ADDON_API_VERSION it was built for
  Kind kind;
  const ConfigField *fields;
  uint8_t fieldCount;
};

// --------------------------------------------------------------- what it sees --

// The machine as the controller believes it to be. Read only.
struct MachineState {
  enum Power : uint8_t { UNKNOWN = 0, OFF = 1, ON = 2 };
  // Where a shot is: STARTING (the start press, nothing poured yet), POURING (pre-infusion, brew,
  // bloom, the stop), DRIPPING (stopped, the cup still settling). `brewing` is true in all three.
  enum ShotPhase : uint8_t { PHASE_NONE = 0, PHASE_STARTING = 1, PHASE_POURING = 2, PHASE_DRIPPING = 3 };
  Power power = UNKNOWN;
  ShotPhase shotPhase = PHASE_NONE;
  uint32_t shotElapsedMs = 0;   // since the start press, while shotPhase is not NONE
  bool warming = false;      // switched on less than half an hour ago
  bool brewing = false;      // a shot is being set up, run or dripped
  bool scaleConnected = false;
  uint32_t lastShotId = 0;   // 0: none since start-up
  float lastShotTimeS = 0.0f;   // from the start press to the stop, pre-infusion included
  float lastShotYieldG = 0.0f;
  uint32_t lastShotAgeS = 0;
};

// The grinder the owner has set up (Settings / library): a setting is in stored steps of it.
struct GrinderInfo {
  uint16_t modelId = 0;     // core/GrinderRegistry.h, 0: none chosen
  char unit[8] = {};        // "um", "dial", ...
  uint8_t divisor = 1;
  uint8_t subSteps = 0;
  uint16_t minStep = 1, maxStep = 2000;
  bool rangeKnown = false;
};

// ------------------------------------------------------------------- network --

// One HTTP(S) request, run on the network task (platform/NetWorker.h netSubmit) so the loop task,
// the screen and the web page keep running. Owned by the add-on (its buffers from Host::allocate),
// handed to Host::fetchStart, polled until done.
struct FetchJob {
  enum State : uint8_t { IDLE = 0, RUNNING = 1, DONE = 2 };
  // The request.
  char url[160] = {};
  bool post = false;
  const char *body = nullptr;      // POST body, the add-on's; nullptr: none
  const char *contentType = "application/json";
  char cookie[128] = {};           // sent as Cookie: when not empty
  // https only: the server certificate's SHA-256, 64 hex digits (colons allowed). Checked before
  // anything is sent; no name is checked (a device's certificate often has none). Required for https.
  char certSha256[100] = {};
  uint16_t connectTimeoutMs = 3000;
  uint16_t totalTimeoutMs = 8000;  // the whole job, at most
  // The answer.
  uint8_t *out = nullptr;          // the add-on's buffer
  size_t outCap = 0;
  size_t outLen = 0;
  bool truncated = false;          // more came than outCap
  int status = 0;                  // HTTP status; <0: failed before an answer (problem says why)
  char setCookie[128] = {};        // every Set-Cookie's name=value, joined by "; " (ready to send as cookie)
  char problem[64] = {};
  std::atomic<uint8_t> state{IDLE};

  bool done() const { return state.load() == DONE; }
  bool running() const { return state.load() == RUNNING; }
};

// --------------------------------------------------------------- web routes --

// A request under a prefix the add-on declared in addon.json (Host refuses any other), answered by
// the controller's own web server: a device that calls the controller reaches it on port 80.
struct Request {
  bool post = false;
  const char *path = "";   // the whole path, e.g. "/api/dev/status"
  const char *body = "";   // POST body as sent
};
struct Response {
  int status = 200;
  const char *type = "application/json";
  char body[512] = {};
};

// ------------------------------------------------------------------- grinder --

// One grind, as the grinder reported it. Any value it could not give is NAN / 0.
struct GrindEvent {
  uint32_t seq = 0;              // grows by one per new grind (the driver's own count)
  uint32_t atMs = 0;             // Host::nowMs() when the grind finished (not when it was read)
  float doseActualG = NAN;       // ground into the portafilter
  float doseTargetG = NAN;       // the grinder recipe's dose
  float gapUm = NAN;             // burr gap at the grind
  float beverageTargetG = NAN;   // the grinder recipe's yield, when it has one
  float recipeTimeS = NAN;       // the grinder recipe's brew time
  uint8_t grinderRecipe = 0;     // the grinder's own recipe number, 1..MAX_GRINDER_RECIPES; 0: none
  uint8_t filter = 0;            // the grinder's basket type, 0: not said
  // Set by the manager, not the driver (interface 2): the controller recipe grinderRecipe maps to when
  // the grind was dealt with, 0: not mapped. Host::recipeForGrinderRecipe() answers the same earlier.
  uint16_t recipeId = 0;
};

// The controller's grind advice for the shot's recipe, worked out with the shot included. Settings are
// in stored steps of the grinder set up here (GrinderInfo, Host::grinder()).
struct AdviceSnapshot {
  enum Action : uint8_t { NONE = 0, HOLD = 1, FINER = 2, COARSER = 3 };
  bool valid = false;          // false: the advice is not for this shot's recipe, so nothing below is set
  Action action = NONE;        // NONE: no verdict (no setting entered, no data, inconsistent shots)
  bool hasNumber = false;      // suggestedSteps is a number to act on
  uint16_t currentSteps = 0;   // the setting the advice starts from
  uint16_t suggestedSteps = 0;
  float umPerStep = 0.0f;      // microns per stored step; 0: the grinder is not read in microns
  float umPerS = 0.0f;         // the advice's sensitivity: microns of setting per second of brew time
  bool stepLimited = false;    // the full correction exceeded the step limit
  bool counted = false;        // this shot counted towards the advice
};

constexpr size_t SHOT_COFFEE_LEN = 32;

// One shot, for grind-by-sync style feedback. Sent when the owner turned shot feedback on, or when the
// add-on asks for reports itself (GrinderDriver::wantsShotReports(), e.g. a recipe is in shadow mode).
struct ShotReport {
  uint32_t shotId = 0;
  float totalTimeS = 0.0f;   // start press to stop, pre-infusion included
  float yieldG = 0.0f;
  bool weighed = false;      // the final weight settled
  bool aborted = false;      // stopped by hand, a fault, or the time limit
  // Added in interface 2.
  uint16_t recipeId = 0;
  float targetTimeS = 0.0f;  // the recipe's, 0: none
  uint8_t timeBasis = 0;     // the basis targetTimeS is on: 0 total, 1 pour (GrindTimeBasis)
  float pourTimeS = 0.0f;    // first drop to stop, 0: not known
  float firstDropS = 0.0f;   // when coffee first reached the cup, 0: not known
  uint32_t grindSeq = 0;     // GrindEvent::seq of the grind this shot followed, 0: none
  AdviceSnapshot advice;
  // Added later in interface 2 (appended).
  uint16_t coffeeId = 0;     // 0: none or no longer known
  uint16_t bagId = 0;        // 0: none (made before bags)
  float roomTempC = NAN;     // the room's slow 12 h temperature at the shot (its context record), NaN: not known
  char coffee[SHOT_COFFEE_LEN] = {};   // the coffee's name as the shot recorded it (cut to fit)
};

enum class Link : uint8_t { OFF = 0, WAITING = 1, CONNECTED = 2, FAILED = 3 };

// Capabilities.
constexpr uint32_t GRIND_EVENTS = 1u << 0;    // takeGrind() delivers grinds
constexpr uint32_t SHOT_FEEDBACK = 1u << 1;   // onShotComplete() does something
constexpr uint32_t WRITE_SETTING = 1u << 2;   // reserved: no driver writes a setting yet

constexpr uint8_t MAX_GRINDER_RECIPES = 8;

class GrinderDriver {
 public:
  virtual ~GrinderDriver() = default;
  virtual uint32_t caps() const = 0;
  virtual Link link() const = 0;
  // The newest grind not yet taken. false: none since the last call.
  virtual bool takeGrind(GrindEvent &out) = 0;
  // After a shot is saved, only when the owner turned shot feedback on for this add-on.
  virtual void onShotComplete(const ShotReport &) {}
  // Interface 2: true while the add-on wants every shot report even though the owner left shot
  // feedback off (shadow mode only watches; it writes nothing to the grinder or the controller).
  virtual bool wantsShotReports() const { return false; }
  // Reserved (WRITE_SETTING): no driver implements it yet.
  virtual bool requestSetting(uint16_t /*steps*/) { return false; }
};

// -------------------------------------------------------------------- host --

class Host {
 public:
  virtual ~Host() = default;
  virtual uint32_t nowMs() const = 0;
  virtual uint32_t epochS() const = 0;            // 0: the clock is not set
  // This add-on's own setting (its default when not set). Never another add-on's.
  virtual const char *config(const char *key) const = 0;
  virtual void log(const char *text) = 0;
  virtual MachineState machine() const = 0;
  virtual GrinderInfo grinder() const = 0;
  // Zeroed memory that is never freed (PSRAM on the board). Take it in begin(), once.
  virtual void *allocate(size_t size) = 0;
  // Starts the job on the network task and returns at once. false: not started (problem says why:
  // the URL is not one of this add-on's URL settings, https without a certificate fingerprint, or
  // the network task is busy: try again on a later tick).
  virtual bool fetchStart(FetchJob &job) = 0;

  // ---- added in interface 2 ----
  // One line for the Home and result screens, "" takes it away. It also goes by itself after
  // NOTICE_TTL_MS. Only the newest add-on notice is shown.
  virtual void notice(const char *text) = 0;
  // Chip (internal) RAM free now, bytes; 0: not known. For work that should wait while it is short.
  virtual uint32_t chipFree() const = 0;
  // Starts a shot on this recipe through the controller's own start (the same path as the web page's
  // Start button, so every check there applies). Returns nullptr when asked, else why it was refused.
  // Refused unless: "Start from the web page" is on, the controller is ready with the scale connected,
  // the active recipe is this one, no drip or result screen is up, and the cup (if the recipe needs
  // one) is in place. It never stops a shot and never presses a bot otherwise. The add-on limits
  // itself to one start per grind.
  virtual const char *requestShotStart(uint16_t recipeId) = 0;
  // Files, only under /addons/<this add-on's id>/, name [a-z0-9._-], at most FILE_NAME_MAX characters.
  // ADDON_FILE_LIMIT bytes in all, and never when less than ADDON_FILE_RESERVE would be free on the
  // file system afterwards (the owner's shot curves come first). All three return what went wrong:
  // nullptr (or a size >= 0) when it worked. They run on the loop task and are small: do not call
  // them with more than a few kB at a time.
  virtual const char *appendFile(const char *name, const char *data, size_t len) = 0;
  // Replaces the file (a roll-over: write the shortened content). The limit counts the new size.
  virtual const char *writeFile(const char *name, const char *data, size_t len) = 0;
  // Size of the file, -1 when it is not there or the name is refused.
  virtual int32_t fileSize(const char *name) const = 0;
  // Reads up to cap bytes from offset. Bytes read (0 at the end), -1 when it is not there or refused.
  virtual int32_t readFile(const char *name, uint32_t offset, uint8_t *out, size_t cap) const = 0;
  // The controller recipe this add-on's grinder recipe (1..MAX_GRINDER_RECIPES) is linked to, by the
  // automatic match or by the owner's mapping (the Add-ons card). 0: not mapped, or its recipe is gone.
  // Lets an add-on ask for the right recipe before any shot exists.
  virtual uint16_t recipeForGrinderRecipe(uint8_t grinderRecipe) const = 0;
};

constexpr uint32_t NOTICE_TTL_MS = 15UL * 60UL * 1000UL;
constexpr size_t NOTICE_LEN = 96;
constexpr size_t FILE_NAME_MAX = 20;
constexpr size_t FILE_PATH_MAX = 47;   // "/addons/<id>/<name>": fits a backup entry name (core/BackupFormat.h)
constexpr uint32_t ADDON_FILE_LIMIT = 1024UL * 1024UL;    // per add-on, all its files
constexpr uint32_t ADDON_FILE_RESERVE = 512UL * 1024UL;   // free on the file system after a write
constexpr size_t CARD_JSON_MAX = 3072;

class Addon {
 public:
  virtual ~Addon() = default;
  virtual const AddonInfo &info() const = 0;
  // Switched on (at start-up, or by the owner). The host outlives the add-on.
  virtual void begin(Host &host) = 0;
  // Switched off: stop answering and fetching. A fetch already running still finishes on its own.
  virtual void end() {}
  // From the loop task, about every 10 ms while switched on. Must not block.
  virtual void tick(uint32_t nowMs) = 0;
  virtual GrinderDriver *grinder() { return nullptr; }
  // A request under one of its declared prefixes. false: not answered (404).
  virtual bool handleRoute(const Request &, Response &) { return false; }
  // A button on its card (POST /api/addons/do). false: no such command.
  virtual bool command(const char * /*cmd*/, const char * /*arg*/, char *reply, size_t size) {
    if (reply && size) reply[0] = '\0';
    return false;
  }
  // One line for its card: what it is doing.
  virtual void statusText(char *out, size_t size) const {
    if (out && size) out[0] = '\0';
  }
  // What the Add-ons card shows beyond its settings, as a JSON object (at most CARD_JSON_MAX bytes,
  // schema in docs/ADDONS.md "The card"). The add-on supplies every label and value; the page draws
  // them. Edits come back through command("set", "<field>:<key>:<value>"). false: nothing extra.
  virtual bool cardJson(char * /*out*/, size_t /*size*/) const { return false; }
};

// What the generated table (scripts/addons.py, .pio/gen/AddonList.h) lists for each add-on compiled in.
struct AddonEntry {
  const char *id;
  Addon *(*make)();             // makes the add-on; it lives as long as the controller runs
  const char *routes[MAX_ROUTES];  // prefixes it may answer, nullptr: unused
};

}  // namespace addon
}  // namespace bbw
