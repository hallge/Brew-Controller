#pragma once
/* The add-on boundary as a plain C function table (docs/ADDONS.md, "Add-ons as modules").
 *
 * Not used yet: there is no loader. This header only *declares* the tables, with fixed layouts, so that
 * a module built later and the firmware that loads it agree, and so that a change to Addon.h that is
 * not reflected here fails the host tests (they compare the sizes below). C++ objects, virtual calls
 * and the standard library never cross this boundary.
 *
 * The version is the same number as bbw::addon::ADDON_API_VERSION (Addon.h): BBW_ADDON_ABI. A module
 * built for another number is listed but not started.
 *
 * A module exports one symbol:
 *     const bbw_addon_api *bbw_addon_entry(const bbw_host_api *host);
 *
 * Everything with a layout is plain data: fixed-width integers, floats, NUL-terminated char arrays, and
 * pointers only where a string or a buffer is handed over. Every struct starts with its own `abi` and
 * `size` where it is a table, so a loader can refuse a table that is not the size it expects.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#define BBW_ABI_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define BBW_ABI_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#define BBW_ADDON_ABI 2

/* ------------------------------------------------------------------ data (no pointers) -- */

typedef struct bbw_machine_state {
  uint8_t power;          /* 0 unknown, 1 off, 2 on */
  uint8_t shot_phase;     /* 0 none, 1 starting, 2 pouring, 3 dripping */
  uint8_t warming;
  uint8_t brewing;
  uint8_t scale_connected;
  uint8_t reserved[3];
  uint32_t shot_elapsed_ms;
  uint32_t last_shot_id;
  float last_shot_time_s;
  float last_shot_yield_g;
  uint32_t last_shot_age_s;
} bbw_machine_state;
BBW_ABI_ASSERT(sizeof(bbw_machine_state) == 28, "bbw_machine_state layout");

typedef struct bbw_grinder_info {
  uint16_t model_id;
  uint16_t min_step;
  uint16_t max_step;
  uint8_t divisor;
  uint8_t sub_steps;
  uint8_t range_known;
  uint8_t reserved[3];
  char unit[8];
} bbw_grinder_info;
BBW_ABI_ASSERT(sizeof(bbw_grinder_info) == 20, "bbw_grinder_info layout");

typedef struct bbw_grind_event {
  uint32_t seq;
  uint32_t at_ms;
  float dose_actual_g;       /* NaN when not given */
  float dose_target_g;
  float gap_um;
  float beverage_target_g;
  float recipe_time_s;
  uint8_t grinder_recipe;
  uint8_t filter;
  uint8_t reserved[2];
} bbw_grind_event;
BBW_ABI_ASSERT(sizeof(bbw_grind_event) == 32, "bbw_grind_event layout");

typedef struct bbw_advice {
  uint8_t valid;
  uint8_t action;            /* 0 none, 1 hold, 2 finer, 3 coarser */
  uint8_t has_number;
  uint8_t step_limited;
  uint8_t counted;
  uint8_t reserved[3];
  uint16_t current_steps;
  uint16_t suggested_steps;
  float um_per_step;
  float um_per_s;
} bbw_advice;
BBW_ABI_ASSERT(sizeof(bbw_advice) == 20, "bbw_advice layout");

typedef struct bbw_shot_report {
  uint32_t shot_id;
  float total_time_s;
  float yield_g;
  uint8_t weighed;
  uint8_t aborted;
  uint16_t recipe_id;
  float target_time_s;
  float pour_time_s;
  float first_drop_s;
  uint32_t grind_seq;
  uint8_t time_basis;
  uint8_t reserved[3];
  bbw_advice advice;
} bbw_shot_report;
BBW_ABI_ASSERT(sizeof(bbw_shot_report) == 56, "bbw_shot_report layout");

typedef struct bbw_response {
  int32_t status;
  char type[32];             /* media type, "application/json" by default */
  char body[512];
} bbw_response;
BBW_ABI_ASSERT(sizeof(bbw_response) == 548, "bbw_response layout");

/* One HTTP(S) request on the network task. The add-on owns the struct and its buffers (from allocate),
 * hands it to fetch_start and polls `state` (0 idle, 1 running, 2 done). Mirrors bbw::addon::FetchJob;
 * the firmware's adapter copies between them. */
typedef struct bbw_fetch_job {
  char url[160];
  uint8_t post;
  uint8_t reserved0[3];
  const char *body;
  const char *content_type;
  char cookie[128];
  char cert_sha256[100];
  uint16_t connect_timeout_ms;
  uint16_t total_timeout_ms;
  uint8_t *out;
  size_t out_cap;
  size_t out_len;
  uint8_t truncated;
  uint8_t reserved1[3];
  int32_t status;
  char set_cookie[128];
  char problem[64];
  volatile uint8_t state;
  uint8_t reserved2[3];
} bbw_fetch_job;

typedef struct bbw_request {
  int32_t post;
  const char *path;
  const char *body;
} bbw_request;

typedef struct bbw_config_field {
  const char *key;
  const char *label;
  uint8_t type;              /* 0 text, 1 number, 2 bool, 3 url, 4 secret */
  uint8_t reserved[3];
  const char *def;
  const char *help;
} bbw_config_field;

typedef struct bbw_addon_info {
  const char *id;
  const char *name;
  const char *version;
  uint16_t api;              /* BBW_ADDON_ABI it was built for */
  uint8_t kind;              /* 1 grinder */
  uint8_t field_count;
  const bbw_config_field *fields;
  const char *routes[2];     /* web prefixes it may answer (addon.json "routes" for a compiled-in add-on), NULL: unused */
} bbw_addon_info;

/* ------------------------------------------------------------------------ the tables -- */

/* What the firmware hands the module. `ctx` is passed back as the first argument of every call.
 * Called from the loop task only. File calls return NULL (or a size >= 0) on success and a reason
 * otherwise; see Host in Addon.h for what each one refuses. */
typedef struct bbw_host_api {
  uint32_t abi;              /* BBW_ADDON_ABI */
  uint32_t size;             /* sizeof(bbw_host_api) */
  void *ctx;
  uint32_t (*now_ms)(void *ctx);
  uint32_t (*epoch_s)(void *ctx);
  const char *(*config)(void *ctx, const char *key);
  void (*log)(void *ctx, const char *text);
  void (*machine)(void *ctx, bbw_machine_state *out);
  void (*grinder)(void *ctx, bbw_grinder_info *out);
  void *(*allocate)(void *ctx, size_t size);
  int32_t (*fetch_start)(void *ctx, bbw_fetch_job *job);                 /* 1 started, 0 not */
  void (*notice)(void *ctx, const char *text);
  uint32_t (*chip_free)(void *ctx);
  const char *(*request_shot_start)(void *ctx, uint16_t recipe_id);      /* NULL: asked, else why not */
  const char *(*append_file)(void *ctx, const char *name, const char *data, size_t len);
  const char *(*write_file)(void *ctx, const char *name, const char *data, size_t len);
  int32_t (*file_size)(void *ctx, const char *name);                     /* -1: none or refused */
  int32_t (*read_file)(void *ctx, const char *name, uint32_t offset, uint8_t *out, size_t cap);
} bbw_host_api;
#define BBW_HOST_API_SLOTS 15
BBW_ABI_ASSERT(sizeof(bbw_host_api) == 2 * sizeof(uint32_t) + (1 + BBW_HOST_API_SLOTS) * sizeof(void *),
               "bbw_host_api layout");

/* What the module hands the firmware. `self` is the module's own object, passed back as the first
 * argument. */
typedef struct bbw_addon_api {
  uint32_t abi;
  uint32_t size;
  void *self;
  const bbw_addon_info *(*info)(void *self);
  void (*begin)(void *self);
  void (*end)(void *self);
  void (*tick)(void *self, uint32_t now_ms);                             /* about every 10 ms; must not block */
  int32_t (*route)(void *self, const bbw_request *request, bbw_response *response);  /* 1 answered */
  int32_t (*command)(void *self, const char *cmd, const char *arg, char *reply, size_t size);  /* 1 ok */
  void (*status)(void *self, char *out, size_t size);
  int32_t (*card_json)(void *self, char *out, size_t size);              /* 1: wrote a JSON object */
  uint32_t (*caps)(void *self);
  uint8_t (*link)(void *self);                                           /* 0 off, 1 waiting, 2 connected, 3 failed */
  int32_t (*take_grind)(void *self, bbw_grind_event *out);               /* 1: a grind was written */
  void (*shot_complete)(void *self, const bbw_shot_report *report);
} bbw_addon_api;
#define BBW_ADDON_API_SLOTS 12
BBW_ABI_ASSERT(sizeof(bbw_addon_api) == 2 * sizeof(uint32_t) + (1 + BBW_ADDON_API_SLOTS) * sizeof(void *),
               "bbw_addon_api layout");

/* The one symbol a module exports. */
typedef const bbw_addon_api *(*bbw_addon_entry_fn)(const bbw_host_api *host);

#ifdef __cplusplus
}  /* extern "C" */
#endif
