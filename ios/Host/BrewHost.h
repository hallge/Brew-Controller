#pragma once
// The plain-C API through which the iPhone app drives the in-process controller (the board's own
// firmware compiled into the app). Usable from Swift with no C++ interop. Design: ios-parity/design.md
// section 3.
//
// Threading: every function may be called from any thread except where noted. bbw_request blocks
// until the controller answers (never call it on the main thread). Callbacks run on host threads.
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- lifecycle --------------------------------------------------------------------------------

// Call once, before bbw_host_start(). dataDir: the folder for the controller's flash (nvs/ and fs/
// below it). demo != 0: the simulated Lunar, SwitchBot and Breville on the virtual radio.
void bbw_host_configure(const char *dataDir, int demo);
// Real (non-demo) mode only: a BleBackend* (emu/src/ble_backend.h) such as the CoreBluetooth one.
// Call before bbw_host_start().
void bbw_host_set_ble_backend(void *backend);
// Runs the firmware's setup() and then loop() on a thread of its own. Returns 0 at once (the thread
// is up), nonzero when called twice or before configure.
int bbw_host_start(void);
// 1 once setup() has finished. Requests made earlier wait or time out.
int bbw_host_ready(void);
// 1 once the controller asked to restart (see BBW_EVENT_RESTART): the loop is parked for good.
int bbw_host_restart_requested(void);
// The app is in the foreground (1) or not (0). Background: the loop sleeps 25 ms per turn.
void bbw_set_foreground(int foreground);

// ---- web API, in process ---------------------------------------------------------------------

typedef struct BbwResponse {
  int status;              // HTTP status; 503 when the controller did not answer in time
  char *content_type;      // NUL-terminated, never NULL
  char *headers;           // extra headers, one "Name: value" per line, "" when none; never NULL
  uint8_t *body;           // body_len bytes plus a NUL; never NULL
  size_t body_len;
} BbwResponse;

// method "GET"/"POST"/...; path_with_query "/api/shots?limit=1"; body may be NULL (len 0); contentType
// may be NULL. Blocks up to timeoutMs. Returns 0 when `out` is filled (check out->status), nonzero
// when the host is not running. Free with bbw_response_free.
int bbw_request(const char *method, const char *path_with_query, const void *body, size_t len,
                const char *contentType, int timeoutMs, BbwResponse *out);
void bbw_response_free(BbwResponse *response);

// ---- live status (fast path, no web queue) ---------------------------------------------------

typedef struct BbwLive {
  uint32_t change;          // grows by one whenever any other field changed since the last call
  int32_t state;            // bbw::State
  char state_name[24];      // the same, as the web API names it
  uint8_t scale_connected, bot_connected, weight_fresh;
  float weight_g, flow_gs, shot_time_s, target_weight_g, stop_offset_g;
  uint16_t sample_period_ms;
  uint8_t manual_stop_needed, verifying_stop, holding_button, stop_started_pump;
  uint8_t machine_style, target_cue, awaiting_drip, cup_present, cup_required;
  uint8_t active_recipe;
  char recipe[32];
  uint8_t use_ratio;
  float dose_g, ratio;
  uint8_t first_drop_seen, bot_paired;
  uint8_t scale_timer_valid;
  float scale_timer_s;
  int8_t scale_battery_percent;   // -1: not reported
  int8_t bot_battery_percent;     // -1: never read
  float bloom_left_s;
  // The /api/screen live flags that are not in LiveStatus.
  uint8_t scale_resting, scale_start_pending, scale_nearby;
  uint32_t last_shot_id;          // 0: none yet
  uint8_t theme;                  // bbw::UiTheme
  uint8_t restart_requested;
  uint8_t demo;
} BbwLive;

// Fills `out` from the controller's own locked accessors. Safe to call at 10 Hz from any thread. All
// zero (state BOOT) before the controller is ready.
void bbw_live(BbwLive *out);

// ---- commands ---------------------------------------------------------------------------------

enum {
  BBW_CMD_START = 1,        // a device start: not gated by "web start"
  BBW_CMD_STOP = 2,
  BBW_CMD_TARE = 3,
  BBW_CMD_ACK = 4,          // acknowledge the result / fault
  BBW_CMD_PRESS = 5,        // one manual press of the brew bot
  BBW_CMD_ACTIVITY = 6,     // the owner did something (keeps an idle scale awake)
  BBW_CMD_NEXT_RECIPE = 7
};
// Queued to a worker thread and run in order; returns at once. 0 queued, nonzero unknown command.
int bbw_command(int cmd);

// ---- events -----------------------------------------------------------------------------------

enum {
  BBW_EVENT_STATE = 1,      // arg: the new bbw::State
  BBW_EVENT_RESTART = 2,    // the controller wants to restart (restore, mode switch): show "tap to restart"
  BBW_EVENT_WAKE = 3        // the screen would wake (a button, a state change worth waking for)
};
typedef void (*BbwEventCallback)(int event, int arg, void *ctx);
// Runs from a small watcher thread (about every 20 ms). Pass NULL to stop. May be set before start.
void bbw_set_event_callback(BbwEventCallback callback, void *ctx);

// ---- demo mode --------------------------------------------------------------------------------

// Sets a value in the simulated world (emu::worldSet): device "scale", "bot", "machine", ... and a
// key and value as emu/src/emu_devices.cpp defines them. Returns 1 when accepted. Demo mode only.
int bbw_demo_set(const char *device, const char *key, const char *value);

// ---- the network seam (host_stubs.cpp; W6 replaces the implementation) ------------------------

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// C++ only: this is for the firmware-side stand-ins, not for Swift.
// One blocking HTTP request, used for the shared library, diagnostics, page previews, Home
// Assistant and add-on fetches. method "GET"/"POST"; headers "Name: value\r\n..." or NULL. Fills
// `response` up to responseCap-1 bytes (NUL-terminated). Returns the HTTP status, or a negative
// number when there was no answer, with a reason in problem. The stub answers -1 "no network".
int hostHttp(const char *method, const char *url, const char *headers, const void *body,
             size_t bodyLen, char *response, size_t responseCap, size_t *responseLen,
             int timeoutMs, char *problem, size_t problemSize);
#endif
