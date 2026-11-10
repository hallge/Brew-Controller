#pragma once
// The module's side of the boundary (docs/ADDONS.md, "Add-ons as modules"). Header only. An add-on keeps
// being the C++ it is: it implements Addon (and GrinderDriver) against Host. This file
//   - wraps the `bbw_host_api` the firmware hands the module back into a Host (ModuleHost), and
//   - builds the `bbw_addon_api` table from an Addon* (ModuleShim).
// Neither side sees the other's C++ objects: only the tables cross (AddonAbi.h).
//
// A module needs one line in one .cpp:
//     BBW_ADDON_MODULE(makeMyAddon, "/api/my/", nullptr)
// (scripts/build_module.py writes it from addon.json). It defines the ELF entry point and, for a build
// with BBW_MODULE_BUILD, the little runtime a module has to bring itself:
//   - no malloc: `operator new` takes from Host::allocate (zeroed, PSRAM), `delete` does nothing;
//   - `__cxa_pure_virtual`, `__dso_handle`, `__cxa_atexit` (nothing runs at exit);
//   - build with -fno-exceptions -fno-rtti -fno-threadsafe-statics (build_module.py does).
// A static constructor runs when the module is first requested, before the entry: it must not allocate.
//
// The host tests link this file with ModuleAddon.h and test every call through the C table.

#include <math.h>
#include <string.h>

#include <new>

#include "addons/Addon.h"
#include "addons/AddonAbi.h"

namespace bbw {
namespace addon {

namespace modulelocal {
inline void text(char *dst, size_t size, const char *src) {
  if (!dst || size == 0) return;
  size_t i = 0;
  if (src) {
    for (; i + 1 < size && src[i]; ++i) dst[i] = src[i];
  }
  dst[i] = '\0';
}
}  // namespace modulelocal

// The firmware's Host, as the add-on sees it.
class ModuleHost : public Host {
 public:
  static constexpr size_t MAX_JOBS = 4;

  explicit ModuleHost(const bbw_host_api *api) : api_(api) {}

  uint32_t nowMs() const override { return api_->now_ms(api_->ctx); }
  uint32_t epochS() const override { return api_->epoch_s(api_->ctx); }
  const char *config(const char *key) const override { return api_->config(api_->ctx, key); }
  void log(const char *text) override { api_->log(api_->ctx, text); }
  MachineState machine() const override {
    bbw_machine_state s;
    api_->machine(api_->ctx, &s);
    MachineState m;
    m.power = s.power == 2 ? MachineState::ON : s.power == 1 ? MachineState::OFF : MachineState::UNKNOWN;
    m.shotPhase = static_cast<MachineState::ShotPhase>(s.shot_phase <= 3 ? s.shot_phase : 0);
    m.shotElapsedMs = s.shot_elapsed_ms;
    m.warming = s.warming != 0;
    m.brewing = s.brewing != 0;
    m.scaleConnected = s.scale_connected != 0;
    m.lastShotId = s.last_shot_id;
    m.lastShotTimeS = s.last_shot_time_s;
    m.lastShotYieldG = s.last_shot_yield_g;
    m.lastShotAgeS = s.last_shot_age_s;
    return m;
  }
  GrinderInfo grinder() const override {
    bbw_grinder_info s;
    api_->grinder(api_->ctx, &s);
    GrinderInfo g;
    g.modelId = s.model_id;
    s.unit[sizeof(s.unit) - 1] = '\0';
    modulelocal::text(g.unit, sizeof(g.unit), s.unit);
    g.divisor = s.divisor;
    g.subSteps = s.sub_steps;
    g.minStep = s.min_step;
    g.maxStep = s.max_step;
    g.rangeKnown = s.range_known != 0;
    return g;
  }
  void *allocate(size_t size) override { return api_->allocate(api_->ctx, size); }

  bool fetchStart(FetchJob &job) override {
    job.problem[0] = '\0';
    Slot *slot = nullptr;
    for (Slot &s : slots_) {
      if (s.job == &job) {
        slot = &s;
        break;
      }
    }
    if (slot && job.running()) return false;
    if (!slot) {
      for (Slot &s : slots_) {
        if (!s.job) {
          slot = &s;
          break;
        }
      }
    }
    if (!slot) {
      modulelocal::text(job.problem, sizeof(job.problem), "too many requests at once");
      return false;
    }
    bbw_fetch_job &m = slot->wire;
    memset(&m, 0, sizeof(m));
    modulelocal::text(m.url, sizeof(m.url), job.url);
    m.post = job.post ? 1 : 0;
    m.body = job.body;
    m.content_type = job.contentType;
    modulelocal::text(m.cookie, sizeof(m.cookie), job.cookie);
    modulelocal::text(m.cert_sha256, sizeof(m.cert_sha256), job.certSha256);
    m.connect_timeout_ms = job.connectTimeoutMs;
    m.total_timeout_ms = job.totalTimeoutMs;
    m.out = job.out;
    m.out_cap = job.outCap;
    if (!api_->fetch_start(api_->ctx, &m)) {
      modulelocal::text(job.problem, sizeof(job.problem), m.problem[0] ? m.problem : "not started");
      return false;
    }
    job.outLen = 0;
    job.truncated = false;
    job.status = 0;
    job.setCookie[0] = '\0';
    slot->job = &job;
    job.state.store(FetchJob::RUNNING);
    return true;
  }
  // Every tick: a job the firmware finished is given back to the add-on.
  void pump() {
    for (Slot &s : slots_) {
      if (!s.job || s.wire.state != 2) continue;
      FetchJob &job = *s.job;
      job.outLen = s.wire.out_len;
      job.truncated = s.wire.truncated != 0;
      job.status = s.wire.status;
      s.wire.set_cookie[sizeof(s.wire.set_cookie) - 1] = '\0';
      s.wire.problem[sizeof(s.wire.problem) - 1] = '\0';
      modulelocal::text(job.setCookie, sizeof(job.setCookie), s.wire.set_cookie);
      modulelocal::text(job.problem, sizeof(job.problem), s.wire.problem);
      s.job = nullptr;
      job.state.store(FetchJob::DONE);
    }
  }

  void notice(const char *text) override { api_->notice(api_->ctx, text); }
  uint32_t chipFree() const override { return api_->chip_free(api_->ctx); }
  uint16_t recipeForGrinderRecipe(uint8_t grinderRecipe) const override {
    return api_->recipe_for_grinder_recipe(api_->ctx, grinderRecipe);
  }
  const char *requestShotStart(uint16_t recipeId) override { return api_->request_shot_start(api_->ctx, recipeId); }
  const char *appendFile(const char *name, const char *data, size_t len) override {
    return api_->append_file(api_->ctx, name, data, len);
  }
  const char *writeFile(const char *name, const char *data, size_t len) override {
    return api_->write_file(api_->ctx, name, data, len);
  }
  int32_t fileSize(const char *name) const override { return api_->file_size(api_->ctx, name); }
  int32_t readFile(const char *name, uint32_t offset, uint8_t *out, size_t cap) const override {
    return api_->read_file(api_->ctx, name, offset, out, cap);
  }

 private:
  struct Slot {
    FetchJob *job = nullptr;
    bbw_fetch_job wire;
  };
  const bbw_host_api *api_;
  Slot slots_[MAX_JOBS];
};

// The add-on, as a `bbw_addon_api`. Made once, in the module's own memory (Host::allocate).
class ModuleShim {
 public:
  ModuleShim(Addon *addon, const bbw_host_api *hostApi, const char *route0, const char *route1)
      : host_(hostApi), addon_(addon) {
    memset(&api_, 0, sizeof(api_));
    api_.abi = BBW_ADDON_ABI;
    api_.size = sizeof(bbw_addon_api);
    api_.self = this;
    api_.info = [](void *s) -> const bbw_addon_info * { return &me(s).info_; };
    api_.begin = [](void *s) { me(s).addon_->begin(me(s).host_); };
    api_.end = [](void *s) { me(s).addon_->end(); };
    api_.tick = [](void *s, uint32_t now) {
      me(s).addon_->tick(now);
      me(s).host_.pump();
    };
    api_.route = [](void *s, const bbw_request *in, bbw_response *out) -> int32_t {
      Request request;
      request.post = in->post != 0;
      request.path = in->path ? in->path : "";
      request.body = in->body ? in->body : "";
      Response response;
      if (!me(s).addon_->handleRoute(request, response)) return 0;
      out->status = response.status;
      modulelocal::text(out->type, sizeof(out->type), response.type);
      modulelocal::text(out->body, sizeof(out->body), response.body);
      return 1;
    };
    api_.command = [](void *s, const char *cmd, const char *arg, char *reply, size_t size) -> int32_t {
      return me(s).addon_->command(cmd, arg, reply, size) ? 1 : 0;
    };
    api_.status = [](void *s, char *out, size_t size) { me(s).addon_->statusText(out, size); };
    api_.card_json = [](void *s, char *out, size_t size) -> int32_t { return me(s).addon_->cardJson(out, size) ? 1 : 0; };
    api_.caps = [](void *s) -> uint32_t {
      GrinderDriver *g = me(s).addon_->grinder();
      return g ? g->caps() : 0;
    };
    api_.link = [](void *s) -> uint8_t {
      GrinderDriver *g = me(s).addon_->grinder();
      if (!g) return 0;
      const Link l = g->link();
      return l == Link::WAITING ? 1 : l == Link::CONNECTED ? 2 : l == Link::FAILED ? 3 : 0;
    };
    api_.take_grind = [](void *s, bbw_grind_event *out) -> int32_t {
      GrinderDriver *g = me(s).addon_->grinder();
      GrindEvent e;
      if (!g || !g->takeGrind(e)) return 0;
      out->seq = e.seq;
      out->at_ms = e.atMs;
      out->dose_actual_g = e.doseActualG;
      out->dose_target_g = e.doseTargetG;
      out->gap_um = e.gapUm;
      out->beverage_target_g = e.beverageTargetG;
      out->recipe_time_s = e.recipeTimeS;
      out->grinder_recipe = e.grinderRecipe;
      out->filter = e.filter;
      out->recipe_id = e.recipeId;
      return 1;
    };
    api_.wants_shot_reports = [](void *s) -> uint8_t {
      GrinderDriver *g = me(s).addon_->grinder();
      return g && g->wantsShotReports() ? 1 : 0;
    };
    api_.shot_complete = [](void *s, const bbw_shot_report *in) {
      GrinderDriver *g = me(s).addon_->grinder();
      if (!g) return;
      ShotReport r;
      r.shotId = in->shot_id;
      r.totalTimeS = in->total_time_s;
      r.yieldG = in->yield_g;
      r.weighed = in->weighed != 0;
      r.aborted = in->aborted != 0;
      r.recipeId = in->recipe_id;
      r.targetTimeS = in->target_time_s;
      r.timeBasis = in->time_basis;
      r.pourTimeS = in->pour_time_s;
      r.firstDropS = in->first_drop_s;
      r.grindSeq = in->grind_seq;
      r.coffeeId = in->coffee_id;
      r.bagId = in->bag_id;
      r.roomTempC = in->room_temp_c;
      char coffee[sizeof(in->coffee)];
      memcpy(coffee, in->coffee, sizeof(coffee));
      coffee[sizeof(coffee) - 1] = '\0';
      modulelocal::text(r.coffee, sizeof(r.coffee), coffee);
      const bbw_advice &a = in->advice;
      r.advice.valid = a.valid != 0;
      r.advice.action = static_cast<AdviceSnapshot::Action>(a.action <= 3 ? a.action : 0);
      r.advice.hasNumber = a.has_number != 0;
      r.advice.stepLimited = a.step_limited != 0;
      r.advice.counted = a.counted != 0;
      r.advice.currentSteps = a.current_steps;
      r.advice.suggestedSteps = a.suggested_steps;
      r.advice.umPerStep = a.um_per_step;
      r.advice.umPerS = a.um_per_s;
      g->onShotComplete(r);
    };
    buildInfo(route0, route1);
  }

  const bbw_addon_api *api() const { return &api_; }

 private:
  static ModuleShim &me(void *s) { return *static_cast<ModuleShim *>(s); }

  void buildInfo(const char *route0, const char *route1) {
    const AddonInfo &in = addon_->info();
    memset(&info_, 0, sizeof(info_));
    info_.id = in.id;
    info_.name = in.name;
    info_.version = in.version;
    info_.api = in.api;
    info_.kind = 1;
    uint8_t n = in.fieldCount > MAX_FIELDS ? static_cast<uint8_t>(MAX_FIELDS) : in.fieldCount;
    for (uint8_t i = 0; i < n; ++i) {
      fields_[i].key = in.fields[i].key;
      fields_[i].label = in.fields[i].label;
      fields_[i].type = static_cast<uint8_t>(in.fields[i].type);
      fields_[i].def = in.fields[i].def;
      fields_[i].help = in.fields[i].help;
    }
    info_.field_count = n;
    info_.fields = fields_;
    info_.routes[0] = route0;
    info_.routes[1] = route1;
  }

  bbw_addon_api api_;
  bbw_addon_info info_;
  bbw_config_field fields_[MAX_FIELDS];
  ModuleHost host_;
  Addon *addon_;
};

// What a module's entry does: make the add-on and its shim in the module's own memory.
// `make` is the add-on's factory (addon.json "factory"). nullptr: the memory was refused.
inline const bbw_addon_api *moduleEntry(const bbw_host_api *host, Addon *(*make)(), const char *route0,
                                        const char *route1) {
  if (!host || host->abi != BBW_ADDON_ABI || host->size != sizeof(bbw_host_api)) return nullptr;
  Addon *addon = make();
  if (!addon) return nullptr;
  void *memory = host->allocate(host->ctx, sizeof(ModuleShim));
  if (!memory) return nullptr;
  return (new (memory) ModuleShim(addon, host, route0, route1))->api();
}

}  // namespace addon
}  // namespace bbw

#ifdef BBW_MODULE_BUILD
// ---- what a module brings itself: no libstdc++, no malloc ----

namespace bbw_module_runtime {
inline const bbw_host_api *&hostApi() {
  static const bbw_host_api *host = nullptr;
  return host;
}
}  // namespace bbw_module_runtime

void *operator new(size_t size) {
  const bbw_host_api *h = bbw_module_runtime::hostApi();
  return h ? h->allocate(h->ctx, size ? size : 1) : nullptr;
}
void *operator new[](size_t size) { return operator new(size); }
void *operator new(size_t size, const std::nothrow_t &) noexcept { return operator new(size); }
void operator delete(void *) noexcept {}
void operator delete[](void *) noexcept {}
void operator delete(void *, size_t) noexcept {}
void operator delete[](void *, size_t) noexcept {}
extern "C" {
void __cxa_pure_virtual() {
  for (;;) {
  }
}
void *__dso_handle = nullptr;
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
}

// The ELF entry point (build_module.py links with -e bbw_module_main). The loader calls it with
// argv[0] = the bbw_host_api and argv[1] = where to put the bbw_addon_api (a `const bbw_addon_api *`).
#define BBW_ADDON_MODULE(factory, route0, route1)                                                      \
  extern "C" int bbw_module_main(int argc, char **argv) {                                              \
    if (argc < 2) return -1;                                                                           \
    const bbw_host_api *host = reinterpret_cast<const bbw_host_api *>(argv[0]);                        \
    bbw_module_runtime::hostApi() = host;                                                              \
    *reinterpret_cast<const bbw_addon_api **>(argv[1]) =                                               \
        bbw::addon::moduleEntry(host, factory, route0, route1);                                        \
    return 0;                                                                                          \
  }
#endif
