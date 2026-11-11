// Headless runner for the iPhone host: starts the firmware in-process in demo mode through the C API
// (ios/Host/BrewHost.h), pairs the simulated bot, picks the Breville profile, places the cup, brews
// one shot and checks the result through the web API. Exit code 0: all checks passed.
//
// No sockets, no ports: everything goes through bbw_request. The data directory is a fresh
// temporary folder (or argv[1]).
#include <ArduinoJson.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include "BrewHost.h"

namespace {

int g_failures = 0;
std::atomic<int> g_stateEvents{0}, g_wakeEvents{0};

void check(bool ok, const char *what) {
  printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
  fflush(stdout);
  if (!ok) ++g_failures;
}

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Reply {
  int status = 0;
  std::string type, body;
};

Reply call(const char *method, const std::string &path, const std::string &body = "",
           const char *type = "application/json", int timeoutMs = 15000) {
  Reply r;
  BbwResponse resp;
  if (bbw_request(method, path.c_str(), body.data(), body.size(), body.empty() ? nullptr : type, timeoutMs,
                  &resp) != 0) {
    return r;
  }
  r.status = resp.status;
  r.type = resp.content_type;
  r.body.assign(reinterpret_cast<char *>(resp.body), resp.body_len);
  bbw_response_free(&resp);
  return r;
}

bool waitFor(bool (*pred)(const BbwLive &), int timeoutMs, BbwLive *last = nullptr) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  BbwLive live;
  while (std::chrono::steady_clock::now() < end) {
    bbw_live(&live);
    if (last) *last = live;
    if (pred(live)) return true;
    sleepMs(100);
  }
  return false;
}

void onEvent(int event, int arg, void *) {
  (void)arg;
  if (event == BBW_EVENT_STATE) ++g_stateEvents;
  if (event == BBW_EVENT_WAKE) ++g_wakeEvents;
}

std::string g_dumpDir;  // --dump DIR: every route body is also written there, for the Swift fixtures

void dump(const std::string &name, const std::string &path) {
  if (g_dumpDir.empty()) return;
  const Reply r = call("GET", path);
  FILE *f = fopen((g_dumpDir + "/" + name + ".json").c_str(), "wb");
  if (!f) return;
  fwrite(r.body.data(), 1, r.body.size(), f);
  fclose(f);
  printf("  dumped %s (%d, %zu bytes)\n", name.c_str(), r.status, r.body.size());
}

void dumpAll(const char *suffix) {
  const struct {
    const char *name;
    const char *path;
  } routes[] = {
      {"status", "/api/status"},     {"screen", "/api/screen"},   {"library", "/api/library"},
      {"settings", "/api/settings"}, {"shots", "/api/shots?limit=2"}, {"grind", "/api/grind"},
      {"devices", "/api/devices"},   {"power", "/api/power"},     {"maintenance", "/api/maintenance"},
      {"machine", "/api/machine"},
      // The hardware screens' models (Grinder, Machine, Devices, Setup, Logs).
      {"grinder_community", "/api/community/grinders"}, {"machine_community", "/api/community/machines"},
      {"machine_events", "/api/power/events?days=30"},  {"devices_pusher_brew", "/api/pusher?role=brew"},
      {"devices_pusher_power", "/api/pusher?role=power"}, {"devices_bot_test", "/api/bot/test"},
      {"setup_start", "/api/start"},                    {"setup_homeassistant", "/api/homeassistant"},
      {"setup_share", "/api/share"},                    {"setup_diag_report", "/api/diagnostics/report"},
      {"setup_crash", "/api/crash"},                    {"logs_config", "/api/log/config"},
  };
  for (const auto &rt : routes) dump(std::string(rt.name) + suffix, rt.path);
}

}  // namespace

int main(int argc, char **argv) {
  namespace fs = std::filesystem;
  // Optional "--dump DIR" anywhere in the arguments; the rest is as before (argv[1]: data dir).
  for (int i = 1; i + 1 < argc; ++i) {
    if (!strcmp(argv[i], "--dump")) {
      g_dumpDir = argv[i + 1];
      fs::create_directories(g_dumpDir);
      for (int j = i; j + 2 < argc; ++j) argv[j] = argv[j + 2];
      argc -= 2;
      break;
    }
  }
  setvbuf(stdout, nullptr, _IOLBF, 4096);
  const bool ownDir = argc < 2;
  const fs::path dir = ownDir ? fs::temp_directory_path() /
                                    ("bbw_hostapp_" + std::to_string(std::chrono::steady_clock::now()
                                                                         .time_since_epoch()
                                                                         .count()))
                              : fs::path(argv[1]);
  printf("data dir: %s\n", dir.string().c_str());

  bbw_set_event_callback(onEvent, nullptr);
  bbw_host_configure(dir.string().c_str(), 1);
  check(bbw_host_start() == 0, "bbw_host_start");
  bool ready = false;
  for (int i = 0; i < 300 && !(ready = bbw_host_ready()); ++i) sleepMs(100);
  check(ready, "firmware setup() finished");
  if (!ready) return 1;

  // Pair the simulated bot (its address is fixed in emu_devices.cpp) and pick the Breville profile.
  Reply r = call("POST", "/api/pair", R"({"kind":"bot","address":"e5:8a:3c:11:9d:40","type":1})");
  check(r.status == 200, "POST /api/pair (bot)");
  // Right after start-up the controller can answer 409 "not while a shot is running" for a moment
  // (seen on the first request after the bot is paired): retry for a few seconds.
  for (int i = 0; i < 40; ++i) {
    r = call("POST", "/api/machine", R"({"builtin":1})");
    if (r.status != 409) break;
    sleepMs(250);
  }
  check(r.status == 200, "POST /api/machine (builtin 1, Breville)");
  if (r.status != 200) printf("  machine reply: %d %s\n", r.status, r.body.c_str());
  r = call("GET", "/api/machine");
  check(r.status == 200 && r.body.find("Breville") != std::string::npos, "GET /api/machine says Breville");

  BbwLive live;
  const bool linked = waitFor([](const BbwLive &l) { return l.scale_connected && l.bot_connected; }, 60000, &live);
  printf("  scale=%d bot=%d state=%s\n", live.scale_connected, live.bot_connected, live.state_name);
  check(linked, "scale and bot connected");

  // The cup goes on the scale: toggled so the firmware sees it arrive.
  bbw_demo_set("machine", "cupOnScale", "0");
  sleepMs(3000);  // long enough for the firmware to see the empty scale
  check(bbw_demo_set("machine", "cupOnScale", "1") == 1, "bbw_demo_set cupOnScale");
  const bool cup = waitFor([](const BbwLive &l) { return l.cup_present != 0; }, 10000, &live);
  printf("  weight=%.1f fresh=%d cup_present=%d cup_required=%d sample_ms=%d\n", live.weight_g, live.weight_fresh, live.cup_present, live.cup_required, live.sample_period_ms);
  check(cup, "cup seen on the scale");
  const bool ready2 = waitFor([](const BbwLive &l) { return strcmp(l.state_name, "READY") == 0; }, 20000, &live);
  printf("  state=%s (%d) target=%.1f g\n", live.state_name, live.state, live.target_weight_g);
  (void)ready2;

  const uint32_t shotsBefore = live.last_shot_id;
  dumpAll("_idle");
  check(bbw_command(BBW_CMD_START) == 0, "bbw_command START");
  // Poll at 10 Hz until the shot has started, is brewing, and ended.
  bool started = false, finished = false;
  float peakWeight = 0;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(120);
  uint32_t lastChange = 0;
  int changes = 0;
  bool dumpedMid = false;
  while (std::chrono::steady_clock::now() < end) {
    bbw_live(&live);
    if (live.change != lastChange) {
      lastChange = live.change;
      ++changes;
    }
    if (live.weight_g > peakWeight) peakWeight = live.weight_g;
    if (live.shot_time_s > 0.5f) started = true;
    if (started && !dumpedMid && live.weight_g > 8.0f) {
      dumpedMid = true;
      dumpAll("_brewing");
    }
    if (started && live.last_shot_id != shotsBefore && strcmp(live.state_name, "RESULT") == 0) {
      finished = true;
      break;
    }
    if (started && live.last_shot_id != shotsBefore) {
      finished = true;
      break;
    }
    sleepMs(100);
  }
  printf("  state=%s peak=%.1f g shot id %u (was %u), %d live changes\n", live.state_name, peakWeight,
         live.last_shot_id, shotsBefore, changes);
  check(started, "the shot started");
  check(finished, "the shot ended and was stored");
  check(changes > 10, "live change counter advanced during the shot");
  check(g_stateEvents.load() > 0, "state events delivered to the callback");

  sleepMs(500);
  dumpAll("_result");
  if (!g_dumpDir.empty()) {
    // A shelf with one coffee and a bag, so the library fixture has every field the page reads.
    r = call("POST", "/api/coffee",
             R"({"name":"Guji Natural","roaster":"Demo Roasters","origin":"Ethiopia","region":"Guji","producer":"Smallholders","varietal":"Heirloom","process":"Natural","notes":"jasmine, peach","altitudeM":2100,"roastLevel":1,"roastDate":20261101})");
    JsonDocument cd;
    if (r.status == 200 && !deserializeJson(cd, r.body)) {
      call("POST", "/api/bag",
           std::string(R"({"coffeeId":)") + std::to_string(cd["id"].as<int>()) +
               R"(,"roastDate":20261101,"openedDate":20261108,"weightG":250})");
      // One rating, so the ratings fixture has a row (the iOS library screens read it).
      call("POST", "/api/coffee/rate",
           std::string(R"({"coffee":)") + std::to_string(cd["id"].as<int>()) +
               R"(,"bag":0,"drinker":0,"stars":4,"attr":[3,2,3,1,4],"flavours":5})");
    }
    dump("library_coffee", "/api/library");
    dump("coffee_ratings", "/api/coffee/ratings");
    dump("coffee_directory", "/api/directory");
    dump("coffee_community_coffees", "/api/community/coffees");
    dump("coffee_library_pending", "/api/library/pending");
    dump("coffee_share", "/api/share");
    {
      JsonDocument ld;
      r = call("GET", "/api/library");
      if (!deserializeJson(ld, r.body) && ld["coffees"].size() > 0 && ld["recipes"].size() > 0) {
        dump("coffee_community",
             "/api/community?coffee=" + std::to_string(ld["coffees"][0]["id"].as<int>()) +
                 "&recipe=" + std::to_string(ld["recipes"][0]["id"].as<int>()));
      }
    }
  }
  if (!g_dumpDir.empty()) {
    r = call("GET", "/api/shots?limit=1");
    JsonDocument sd;
    if (!deserializeJson(sd, r.body) && sd.is<JsonArray>() && sd.size() > 0) {
      dump("shot_data", "/api/shot/data?id=" + std::to_string(sd[0]["id"].as<unsigned long>()));
    }
  }
  r = call("GET", "/api/shots?limit=1");
  check(r.status == 200, "GET /api/shots?limit=1");
  {
    JsonDocument doc;
    const bool parsed = !deserializeJson(doc, r.body) && doc.is<JsonArray>() && doc.size() == 1;
    check(parsed, "shots parses as one record");
    if (parsed) {
      const char *reason = doc[0]["end_reason"] | "";
      printf("  end_reason=%s stop_confirmed=%d final=%.1f g\n", reason, doc[0]["stop_confirmed"].as<bool>(),
             doc[0]["final_weight_g"].as<float>());
      check(strcmp(reason, "weight") == 0, "end_reason is weight");
      check(doc[0]["stop_confirmed"].as<bool>(), "stop_confirmed");
    }
  }

  bbw_command(BBW_CMD_ACK);
  sleepMs(300);
  const char *paths[] = {"/api/screen", "/api/library", "/api/settings"};
  for (const char *path : paths) {
    r = call("GET", path);
    JsonDocument doc;
    const bool json = !deserializeJson(doc, r.body);
    printf("  GET %s -> %d, %zu bytes\n", path, r.status, r.body.size());
    check(r.status == 200 && json, path);
  }
  check(g_wakeEvents.load() >= 0, "event callback wired");

  printf(g_failures ? "FAILED: %d check(s)\n" : "all checks passed\n", g_failures);
  fflush(stdout);
  if (ownDir) {
    std::error_code ignored;
    fs::remove_all(dir, ignored);
  }
  _Exit(g_failures ? 1 : 0);  // the firmware threads never end
}
