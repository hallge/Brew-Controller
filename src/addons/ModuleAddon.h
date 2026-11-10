#pragma once
// Firmware side of the module boundary (docs/ADDONS.md, "Add-ons as modules"): a loaded module is a
// `bbw_addon_api` (AddonAbi.h, plain C). ModuleAddon makes it an ordinary Addon + GrinderDriver, so the
// manager, the web page and the grind policy treat it like a compiled-in add-on. The module's side of the
// same table is AddonModule.h.
//
// What crosses the boundary is plain data and function pointers only. This file turns the C++ Host the
// firmware gives every add-on into a `bbw_host_api` (HostBridge), and the module's table into an Addon
// (ModuleAddon). A fetch is copied between the module's `bbw_fetch_job` and a FetchJob of the firmware's
// (the module's buffers are used directly, nothing is copied twice).
//
// Portable: the host tests link the add-on, AddonModule.h and this file together, so every call is made
// through the C table, as it is on the board.

#include <math.h>
#include <string.h>

#include "addons/Addon.h"
#include "addons/AddonAbi.h"

namespace bbw {
namespace addon {

namespace modulecopy {
inline void text(char *dst, size_t size, const char *src) {
  if (!dst || size == 0) return;
  size_t i = 0;
  if (src) {
    for (; i + 1 < size && src[i]; ++i) dst[i] = src[i];
  }
  dst[i] = '\0';
}
}  // namespace modulecopy

// What the module is given. One per module; `host` is set when the add-on is switched on (begin) and a
// call before that finds nothing to talk to and answers with a harmless default.
class HostBridge {
 public:
  static constexpr size_t MAX_JOBS = 2;

  HostBridge() {
    memset(&api_, 0, sizeof(api_));
    api_.abi = BBW_ADDON_ABI;
    api_.size = sizeof(bbw_host_api);
    api_.ctx = this;
    api_.now_ms = [](void *c) -> uint32_t { return self(c).host_ ? self(c).host_->nowMs() : 0; };
    api_.epoch_s = [](void *c) -> uint32_t { return self(c).host_ ? self(c).host_->epochS() : 0; };
    api_.config = [](void *c, const char *key) -> const char * {
      return self(c).host_ ? self(c).host_->config(key) : "";
    };
    api_.log = [](void *c, const char *text) {
      if (self(c).host_) self(c).host_->log(text ? text : "");
    };
    api_.machine = [](void *c, bbw_machine_state *out) {
      if (!out) return;
      memset(out, 0, sizeof(*out));
      if (!self(c).host_) return;
      const MachineState m = self(c).host_->machine();
      out->power = static_cast<uint8_t>(m.power);
      out->shot_phase = static_cast<uint8_t>(m.shotPhase);
      out->warming = m.warming;
      out->brewing = m.brewing;
      out->scale_connected = m.scaleConnected;
      out->shot_elapsed_ms = m.shotElapsedMs;
      out->last_shot_id = m.lastShotId;
      out->last_shot_time_s = m.lastShotTimeS;
      out->last_shot_yield_g = m.lastShotYieldG;
      out->last_shot_age_s = m.lastShotAgeS;
    };
    api_.grinder = [](void *c, bbw_grinder_info *out) {
      if (!out) return;
      memset(out, 0, sizeof(*out));
      if (!self(c).host_) return;
      const GrinderInfo g = self(c).host_->grinder();
      out->model_id = g.modelId;
      out->min_step = g.minStep;
      out->max_step = g.maxStep;
      out->divisor = g.divisor;
      out->sub_steps = g.subSteps;
      out->range_known = g.rangeKnown;
      modulecopy::text(out->unit, sizeof(out->unit), g.unit);
    };
    // The module makes its add-on in its entry, before begin(): until then memory comes from `alloc`.
    api_.allocate = [](void *c, size_t size) -> void * {
      HostBridge &b = self(c);
      if (b.host_) return b.host_->allocate(size);
      return b.alloc_ ? b.alloc_(size) : nullptr;
    };
    api_.fetch_start = [](void *c, bbw_fetch_job *job) -> int32_t { return self(c).fetchStart(job) ? 1 : 0; };
    api_.notice = [](void *c, const char *text) {
      if (self(c).host_) self(c).host_->notice(text ? text : "");
    };
    api_.chip_free = [](void *c) -> uint32_t { return self(c).host_ ? self(c).host_->chipFree() : 0; };
    api_.request_shot_start = [](void *c, uint16_t recipe) -> const char * {
      return self(c).host_ ? self(c).host_->requestShotStart(recipe) : "the add-on is not switched on";
    };
    api_.append_file = [](void *c, const char *name, const char *data, size_t len) -> const char * {
      return self(c).host_ ? self(c).host_->appendFile(name, data, len) : "the add-on is not switched on";
    };
    api_.write_file = [](void *c, const char *name, const char *data, size_t len) -> const char * {
      return self(c).host_ ? self(c).host_->writeFile(name, data, len) : "the add-on is not switched on";
    };
    api_.file_size = [](void *c, const char *name) -> int32_t {
      return self(c).host_ ? self(c).host_->fileSize(name) : -1;
    };
    api_.read_file = [](void *c, const char *name, uint32_t offset, uint8_t *out, size_t cap) -> int32_t {
      return self(c).host_ ? self(c).host_->readFile(name, offset, out, cap) : -1;
    };
  }

  const bbw_host_api *api() const { return &api_; }
  void useAllocator(void *(*alloc)(size_t)) { alloc_ = alloc; }
  void attach(Host *host) { host_ = host; }
  Host *host() const { return host_; }

  // The network task finished a job: hand the result to the module. Every tick.
  void pump() {
    for (Job &j : jobs_) {
      if (!j.job || !j.fetch.done()) continue;
      bbw_fetch_job *m = j.job;
      m->out_len = j.fetch.outLen;
      m->truncated = j.fetch.truncated;
      m->status = j.fetch.status;
      modulecopy::text(m->set_cookie, sizeof(m->set_cookie), j.fetch.setCookie);
      modulecopy::text(m->problem, sizeof(m->problem), j.fetch.problem);
      j.job = nullptr;
      j.fetch.state.store(FetchJob::IDLE);
      m->state = 2;  // last: the module polls it
    }
  }

  // The module is being switched off: its jobs are still running on the network task, which owns the
  // module's buffers until it is done; they finish on their own (Addon::end).
  bool busy() const {
    for (const Job &j : jobs_) {
      if (j.job) return true;
    }
    return false;
  }

 private:
  struct Job {
    bbw_fetch_job *job = nullptr;
    FetchJob fetch;
  };

  static HostBridge &self(void *ctx) { return *static_cast<HostBridge *>(ctx); }

  bool fetchStart(bbw_fetch_job *m) {
    if (!m) return false;
    if (!host_) {
      modulecopy::text(m->problem, sizeof(m->problem), "the add-on is not switched on");
      return false;
    }
    m->problem[0] = '\0';
    Job *slot = nullptr;
    for (Job &j : jobs_) {
      if (j.job == m) return false;  // already running
    }
    for (Job &j : jobs_) {
      if (!j.job && !j.fetch.running()) {
        slot = &j;
        break;
      }
    }
    if (!slot) {
      modulecopy::text(m->problem, sizeof(m->problem), "too many requests at once");
      return false;
    }
    FetchJob &f = slot->fetch;
    f.state.store(FetchJob::IDLE);
    modulecopy::text(f.url, sizeof(f.url), m->url);
    f.post = m->post != 0;
    f.body = m->body;
    f.contentType = m->content_type ? m->content_type : "application/json";
    modulecopy::text(f.cookie, sizeof(f.cookie), m->cookie);
    modulecopy::text(f.certSha256, sizeof(f.certSha256), m->cert_sha256);
    f.connectTimeoutMs = m->connect_timeout_ms;
    f.totalTimeoutMs = m->total_timeout_ms;
    f.out = m->out;
    f.outCap = m->out_cap;
    f.outLen = 0;
    f.truncated = false;
    f.status = 0;
    f.setCookie[0] = '\0';
    f.problem[0] = '\0';
    if (!host_->fetchStart(f)) {
      modulecopy::text(m->problem, sizeof(m->problem), f.problem[0] ? f.problem : "not started");
      return false;
    }
    m->out_len = 0;
    m->truncated = 0;
    m->status = 0;
    m->set_cookie[0] = '\0';
    slot->job = m;
    m->state = 1;
    return true;
  }

  bbw_host_api api_;
  Host *host_ = nullptr;
  void *(*alloc_)(size_t) = nullptr;
  Job jobs_[MAX_JOBS];
};

// A module's add-on, as the firmware's manager sees every add-on. Made with the entry function the
// loader found (a plain call in the host tests; a call into the loaded ELF on the board).
class ModuleAddon : public Addon, public GrinderDriver {
 public:
  // The entry: `load(ctx, host_api)` calls the module's bbw_addon_entry and returns its table.
  using EntryFn = const bbw_addon_api *(*)(void *ctx, const bbw_host_api *host);

  // `alloc`: zeroed memory that is never freed, for what the module allocates before it is switched on.
  ModuleAddon(EntryFn entry, void *ctx, void *(*alloc)(size_t) = nullptr) {
    bridge_.useAllocator(alloc);
    api_ = entry ? entry(ctx, bridge_.api()) : nullptr;
    if (api_ && api_->abi == BBW_ADDON_ABI && api_->size == sizeof(bbw_addon_api) && api_->info) {
      convertInfo();
    } else {
      api_ = nullptr;
    }
  }

  // false: the module's table was refused (wrong layout, or it gave none): not usable.
  bool ok() const { return api_ != nullptr; }
  const bbw_addon_api *table() const { return api_; }

  // ---- Addon ----
  const AddonInfo &info() const override { return info_; }
  void begin(Host &host) override {
    bridge_.attach(&host);
    if (api_ && api_->begin) api_->begin(api_->self);
  }
  void end() override {
    if (api_ && api_->end) api_->end(api_->self);
  }
  void tick(uint32_t nowMs) override {
    if (api_ && api_->tick) api_->tick(api_->self, nowMs);
    bridge_.pump();
  }
  GrinderDriver *grinder() override { return api_ && api_->take_grind ? this : nullptr; }
  bool handleRoute(const Request &request, Response &response) override {
    if (!api_ || !api_->route) return false;
    bbw_request in;
    in.post = request.post ? 1 : 0;
    in.path = request.path;
    in.body = request.body;
    bbw_response out;
    memset(&out, 0, sizeof(out));
    out.status = 200;
    if (!api_->route(api_->self, &in, &out)) return false;
    response.status = out.status;
    out.type[sizeof(out.type) - 1] = '\0';
    modulecopy::text(routeType_, sizeof(routeType_), out.type[0] ? out.type : "application/json");
    response.type = routeType_;
    out.body[sizeof(out.body) - 1] = '\0';
    modulecopy::text(response.body, sizeof(response.body), out.body);
    return true;
  }
  bool command(const char *cmd, const char *arg, char *reply, size_t size) override {
    if (reply && size) reply[0] = '\0';
    if (!api_ || !api_->command) return false;
    return api_->command(api_->self, cmd ? cmd : "", arg ? arg : "", reply, size) != 0;
  }
  void statusText(char *out, size_t size) const override {
    if (out && size) out[0] = '\0';
    if (api_ && api_->status && out && size) api_->status(api_->self, out, size);
  }
  bool cardJson(char *out, size_t size) const override {
    if (!api_ || !api_->card_json || !out || size == 0) return false;
    out[0] = '\0';
    return api_->card_json(api_->self, out, size) != 0;
  }

  // ---- GrinderDriver ----
  uint32_t caps() const override { return api_ && api_->caps ? api_->caps(api_->self) : 0; }
  Link link() const override {
    const uint8_t l = api_ && api_->link ? api_->link(api_->self) : 0;
    return l == 1 ? Link::WAITING : l == 2 ? Link::CONNECTED : l == 3 ? Link::FAILED : Link::OFF;
  }
  bool takeGrind(GrindEvent &out) override {
    if (!api_ || !api_->take_grind) return false;
    bbw_grind_event e;
    memset(&e, 0, sizeof(e));
    if (!api_->take_grind(api_->self, &e)) return false;
    out.seq = e.seq;
    out.atMs = e.at_ms;
    out.doseActualG = e.dose_actual_g;
    out.doseTargetG = e.dose_target_g;
    out.gapUm = e.gap_um;
    out.beverageTargetG = e.beverage_target_g;
    out.recipeTimeS = e.recipe_time_s;
    out.grinderRecipe = e.grinder_recipe;
    out.filter = e.filter;
    return true;
  }
  void onShotComplete(const ShotReport &r) override {
    if (!api_ || !api_->shot_complete) return;
    bbw_shot_report s;
    memset(&s, 0, sizeof(s));
    s.shot_id = r.shotId;
    s.total_time_s = r.totalTimeS;
    s.yield_g = r.yieldG;
    s.weighed = r.weighed;
    s.aborted = r.aborted;
    s.recipe_id = r.recipeId;
    s.target_time_s = r.targetTimeS;
    s.pour_time_s = r.pourTimeS;
    s.first_drop_s = r.firstDropS;
    s.grind_seq = r.grindSeq;
    s.time_basis = r.timeBasis;
    s.advice.valid = r.advice.valid;
    s.advice.action = static_cast<uint8_t>(r.advice.action);
    s.advice.has_number = r.advice.hasNumber;
    s.advice.step_limited = r.advice.stepLimited;
    s.advice.counted = r.advice.counted;
    s.advice.current_steps = r.advice.currentSteps;
    s.advice.suggested_steps = r.advice.suggestedSteps;
    s.advice.um_per_step = r.advice.umPerStep;
    s.advice.um_per_s = r.advice.umPerS;
    api_->shot_complete(api_->self, &s);
  }

  // The prefixes the module declared (for the AddonEntry).
  const char *route(size_t i) const { return i < MAX_ROUTES && routes_[i][0] ? routes_[i] : nullptr; }
  bool jobsRunning() const { return bridge_.busy(); }

 private:
  void convertInfo() {
    const bbw_addon_info *in = api_->info(api_->self);
    memset(&info_, 0, sizeof(info_));
    if (!in || !in->id || !in->name || !in->version) {
      api_ = nullptr;
      return;
    }
    modulecopy::text(id_, sizeof(id_), in->id);
    modulecopy::text(name_, sizeof(name_), in->name);
    modulecopy::text(version_, sizeof(version_), in->version);
    info_.id = id_;
    info_.name = name_;
    info_.version = version_;
    info_.api = in->api;
    info_.kind = Kind::GRINDER;
    uint8_t n = in->field_count;
    if (n > MAX_FIELDS || (n && !in->fields)) n = MAX_FIELDS + 1;  // refused by the manager (apiOk)
    info_.fieldCount = n;
    const uint8_t keep = n > MAX_FIELDS ? static_cast<uint8_t>(0) : n;
    for (uint8_t i = 0; i < keep; ++i) {
      const bbw_config_field &f = in->fields[i];
      // The strings stay where the module has them (its read-only data lives as long as it is loaded).
      fields_[i].key = f.key ? f.key : "";
      fields_[i].label = f.label ? f.label : "";
      fields_[i].type = static_cast<FieldType>(f.type <= 4 ? f.type : 0);
      fields_[i].def = f.def ? f.def : "";
      fields_[i].help = f.help ? f.help : "";
    }
    info_.fields = fields_;
    for (size_t i = 0; i < MAX_ROUTES; ++i) modulecopy::text(routes_[i], sizeof(routes_[i]), in->routes[i]);
  }

  HostBridge bridge_;
  const bbw_addon_api *api_ = nullptr;
  AddonInfo info_ = {};
  ConfigField fields_[MAX_FIELDS] = {};
  char id_[32] = {}, name_[48] = {}, version_[16] = {};
  char routes_[MAX_ROUTES][48] = {};
  char routeType_[32] = {};
};

// A module the firmware lists but does not start: built for another interface, needing a newer firmware,
// stopped after a crash, or refused. `api` is 0, so the manager marks it "built for another add-on
// interface: not started"; the card shows the module's own reason (AddonHost.cpp).
class ModuleStub : public Addon {
 public:
  ModuleStub(const char *id, const char *name, const char *version) {
    modulecopy::text(id_, sizeof(id_), id);
    modulecopy::text(name_, sizeof(name_), name && name[0] ? name : id);
    modulecopy::text(version_, sizeof(version_), version);
    info_.id = id_;
    info_.name = name_;
    info_.version = version_;
    info_.api = 0;
    info_.kind = Kind::GRINDER;
    info_.fields = nullptr;
    info_.fieldCount = 0;
  }
  const AddonInfo &info() const override { return info_; }
  void begin(Host &) override {}
  void tick(uint32_t) override {}

 private:
  AddonInfo info_ = {};
  char id_[32] = {}, name_[48] = {}, version_[16] = {};
};

}  // namespace addon
}  // namespace bbw
