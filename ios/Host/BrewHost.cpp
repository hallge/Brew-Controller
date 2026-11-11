// The C API of the in-process controller (BrewHost.h). The firmware's setup() and loop() run on a
// thread of their own, exactly as emu_core.cpp runs them; requests go through emu::webEnqueue and the
// fast paths through app:: functions that take the firmware lock.
#include "BrewHost.h"

#include <Arduino.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "App.h"
#include "HostInternal.h"
#include "ble_backend.h"
#include "emu.h"
#include "emu_radio.h"
#include "platform/HostHooks.h"

// Defined by the firmware (src/main.cpp).
void setup();
void loop();

namespace {

std::atomic<bool> g_configured{false}, g_started{false}, g_demo{false};
std::atomic<bool> g_restart{false};
void *g_backend = nullptr;
std::string g_dataDir;

// ---- live status change counter -------------------------------------------------------------
std::mutex g_liveMutex;
BbwLive g_lastLive;
bool g_haveLast = false;
uint32_t g_change = 0;

// ---- commands ---------------------------------------------------------------------------------
std::mutex g_cmdMutex;
std::condition_variable g_cmdReady;
std::deque<int> g_cmds;
std::once_flag g_cmdOnce;

void runCommand(int cmd) {
  switch (cmd) {
    case BBW_CMD_START: app::start(); break;
    case BBW_CMD_STOP: app::stop(); break;
    case BBW_CMD_TARE: app::tare(); break;
    case BBW_CMD_ACK: app::acknowledge(); break;
    case BBW_CMD_PRESS: app::manualPress(); break;
    case BBW_CMD_ACTIVITY: app::noteActivity(); break;
    case BBW_CMD_NEXT_RECIPE: app::nextRecipe(); break;
    default: break;
  }
}

void commandWorker() {
  for (;;) {
    int cmd;
    {
      std::unique_lock<std::mutex> lock(g_cmdMutex);
      g_cmdReady.wait(lock, [] { return !g_cmds.empty(); });
      cmd = g_cmds.front();
      g_cmds.pop_front();
    }
    if (emu::g_firmwareReady) runCommand(cmd);
  }
}

// ---- events -----------------------------------------------------------------------------------
std::mutex g_eventMutex;
BbwEventCallback g_callback = nullptr;
void *g_callbackCtx = nullptr;
std::once_flag g_watcherOnce;

void fire(int event, int arg) {
  BbwEventCallback callback;
  void *ctx;
  {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    callback = g_callback;
    ctx = g_callbackCtx;
  }
  if (callback) callback(event, arg, ctx);
}

void watcher() {
  int lastState = -1;
  uint32_t lastWake = bbwhost::wakeCount();
  bool restartSent = false;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (emu::g_firmwareReady) {
      const int state = static_cast<int>(app::status().state);
      if (state != lastState) {
        lastState = state;
        fire(BBW_EVENT_STATE, state);
      }
    }
    const uint32_t wake = bbwhost::wakeCount();
    if (wake != lastWake) {
      lastWake = wake;
      fire(BBW_EVENT_WAKE, 0);
    }
    if (g_restart && !restartSent) {
      restartSent = true;
      fire(BBW_EVENT_RESTART, 0);
    }
  }
}

// ---- the controller thread ------------------------------------------------------------------
void controllerMain() {
  emu::g_options.dataDir = g_dataDir;
  std::error_code ignored;
  std::filesystem::create_directories(g_dataDir, ignored);
  // A host that keeps the RTC image across launches says when a launch is a restart mid-shot, so the
  // firmware's restart recovery runs (main.cpp resumedMidShot). Otherwise: power on.
  const emu::Reset reason = emu::g_hooks.bootReason ? emu::g_hooks.bootReason() : emu::Reset::POWER_ON;
  emu::g_resetReason = reason;
  emu::g_board.resetReason = reason == emu::Reset::POWER_ON ? "power on" : "restart";
  emu::rtcRestore(reason);
  emu::log("emu", "host controller boot (%s)", g_demo ? "demo" : "real devices");
  if (g_demo) {
    bleSetBackend(bleVirtualRadioBackend());
    emu::worldBegin(true);
    emu::radioBegin();
  } else if (g_backend) {
    bleSetBackend(static_cast<BleBackend *>(g_backend));
  }
  setup();
  emu::g_firmwareReady = true;
  while (!g_restart) loop();
  // Parked: the host shows "tap to restart". Nothing runs the firmware again in this process.
  for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
}

char *dupString(const std::string &s) {
  char *p = static_cast<char *>(malloc(s.size() + 1));
  memcpy(p, s.c_str(), s.size() + 1);
  return p;
}

}  // namespace

// Called by restartSoon() on the loop thread (src/platform/HostHooks.h).
void hostRequestRestart() {
  g_restart = true;
  bbwhost::noteWake();
}

extern "C" {

void bbw_host_configure(const char *dataDir, int demo) {
  if (g_started) return;
  g_dataDir = dataDir ? dataDir : "";
  g_demo = demo != 0;
  g_configured = true;
}

void bbw_host_set_ble_backend(void *backend) { g_backend = backend; }

int bbw_host_start(void) {
  if (!g_configured || g_started.exchange(true)) return 1;
  std::call_once(g_cmdOnce, [] { std::thread(commandWorker).detach(); });
  std::call_once(g_watcherOnce, [] { std::thread(watcher).detach(); });
  std::thread(controllerMain).detach();
  return 0;
}

int bbw_host_ready(void) { return emu::g_firmwareReady ? 1 : 0; }
int bbw_host_restart_requested(void) { return g_restart ? 1 : 0; }

int bbw_request(const char *method, const char *path, const void *body, size_t len,
                const char *contentType, int timeoutMs, BbwResponse *out) {
  if (!out) return 1;
  memset(out, 0, sizeof(*out));
  if (!g_started) return 1;
  // Wait for setup() to finish (the handlers' state does not exist before), within the timeout.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (!emu::g_firmwareReady && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  emu::WebResponse r;
  if (!emu::g_firmwareReady) {
    r.status = 503;
    r.body = "controller not ready";
  } else {
    const int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          deadline - std::chrono::steady_clock::now()).count());
    r = emu::webEnqueue(method ? method : "GET", path ? path : "/",
                        std::string(static_cast<const char *>(body ? body : ""), body ? len : 0),
                        contentType ? contentType : "", left > 50 ? left : 50);
  }
  std::string headers;
  for (const auto &h : r.headers) headers += h.first + ": " + h.second + "\n";
  out->status = r.status;
  out->content_type = dupString(r.contentType);
  out->headers = dupString(headers);
  out->body = reinterpret_cast<uint8_t *>(dupString(r.body));
  out->body_len = r.body.size();
  return 0;
}

void bbw_response_free(BbwResponse *response) {
  if (!response) return;
  free(response->content_type);
  free(response->headers);
  free(response->body);
  memset(response, 0, sizeof(*response));
}

void bbw_live(BbwLive *out) {
  if (!out) return;
  BbwLive v;
  memset(&v, 0, sizeof(v));
  v.demo = g_demo ? 1 : 0;
  v.restart_requested = g_restart ? 1 : 0;
  v.scale_battery_percent = -1;
  v.bot_battery_percent = -1;
  if (emu::g_firmwareReady) {
    const bbw::LiveStatus st = app::status();
    v.state = static_cast<int32_t>(st.state);
    strncpy(v.state_name, bbw::stateName(st.state), sizeof(v.state_name) - 1);
    v.scale_connected = st.scaleConnected;
    v.bot_connected = st.botConnected;
    v.weight_fresh = st.weightFresh;
    v.weight_g = st.weightG;
    v.flow_gs = st.flowGS;
    v.shot_time_s = st.shotTimeS;
    v.target_weight_g = st.targetWeightG;
    v.stop_offset_g = st.stopOffsetG;
    v.sample_period_ms = st.samplePeriodMs;
    v.manual_stop_needed = st.manualStopNeeded;
    v.verifying_stop = st.verifyingStop;
    v.holding_button = st.holdingButton;
    v.stop_started_pump = st.stopStartedPump;
    v.machine_style = st.machineStyle;
    v.target_cue = st.targetCue;
    v.awaiting_drip = st.awaitingDrip;
    v.cup_present = st.cupPresent;
    v.cup_required = st.cupRequired;
    v.active_recipe = st.activeRecipe;
    strncpy(v.recipe, st.recipe, sizeof(v.recipe) - 1);
    v.use_ratio = st.useRatio;
    v.dose_g = st.doseG;
    v.ratio = st.ratio;
    v.first_drop_seen = st.firstDropSeen;
    v.bot_paired = st.botPaired;
    v.scale_timer_valid = st.scaleTimerValid;
    v.scale_timer_s = st.scaleTimerS;
    v.scale_battery_percent = st.scaleBatteryPercent;
    v.bot_battery_percent = st.botBatteryPercent;
    v.bloom_left_s = st.bloomLeftS;
    v.scale_resting = app::scaleResting();
    v.scale_start_pending = app::scaleStartPending();
    v.scale_nearby = app::scaleNearby();
    bbw::ShotRecord shot;
    v.last_shot_id = app::lastShot(shot) ? shot.id : 0;
    v.theme = app::settings().uiTheme;
  }
  std::lock_guard<std::mutex> lock(g_liveMutex);
  v.change = 0;
  if (!g_haveLast || memcmp(&v, &g_lastLive, sizeof(v)) != 0) {
    ++g_change;
    g_lastLive = v;
    g_haveLast = true;
  }
  v.change = g_change;
  *out = v;
}

int bbw_command(int cmd) {
  if (cmd < BBW_CMD_START || cmd > BBW_CMD_NEXT_RECIPE) return 1;
  std::call_once(g_cmdOnce, [] { std::thread(commandWorker).detach(); });
  {
    std::lock_guard<std::mutex> lock(g_cmdMutex);
    g_cmds.push_back(cmd);
  }
  g_cmdReady.notify_one();
  return 0;
}

void bbw_set_event_callback(BbwEventCallback callback, void *ctx) {
  std::lock_guard<std::mutex> lock(g_eventMutex);
  g_callback = callback;
  g_callbackCtx = ctx;
  std::call_once(g_watcherOnce, [] { std::thread(watcher).detach(); });
}

int bbw_demo_set(const char *device, const char *key, const char *value) {
  if (!g_demo || !g_started || !device || !key) return 0;
  return emu::worldSet(device, key, value ? value : "") ? 1 : 0;
}

}  // extern "C"
