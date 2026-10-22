// The emulator page's control API (port --emu-port) and the Arduino
// WebServer stand-in that carries the firmware's own web interface
// (port --web-port).

#include <ArduinoJson.h>
#include <WebServer.h>

#include <condition_variable>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>

#include "App.h"
#include "emu.h"
#include "emu_http.h"

// ============================================================== WebServer ==

namespace {

struct Pending {
  emu::HttpRequest request;
  emu::HttpResponse response;
  bool done = false;
  std::mutex mutex;
  std::condition_variable ready;
};

std::mutex g_pendingMutex;
std::deque<std::shared_ptr<Pending>> g_pending;

}  // namespace

void WebServer::on(const char *path, HTTPMethod method, Handler handler) {
  routes_.push_back({path, method, handler});
}

void WebServer::begin() {
  const bool ok = emu::httpServe(
      emu::g_options.webPort, [](const emu::HttpRequest &request, emu::HttpResponse &response) {
        auto pending = std::make_shared<Pending>();
        pending->request = request;
        {
          std::lock_guard<std::mutex> lock(g_pendingMutex);
          g_pending.push_back(pending);
        }
        std::unique_lock<std::mutex> lock(pending->mutex);
        if (pending->ready.wait_for(lock, std::chrono::seconds(10),
                                    [&] { return pending->done; })) {
          response = pending->response;
        } else {
          response.code = 503;
          response.body = "controller did not answer";
        }
      });
  if (!ok) emu::log("emu", "device web UI: port %d is in use", emu::g_options.webPort);
}

void WebServer::handleClient() {
  for (;;) {
    std::shared_ptr<Pending> pending;
    {
      std::lock_guard<std::mutex> lock(g_pendingMutex);
      if (g_pending.empty()) return;
      pending = g_pending.front();
      g_pending.pop_front();
    }
    const emu::HttpRequest &request = pending->request;
    args_ = request.query;
    args_["plain"] = request.body;
    code_ = 200;
    type_ = "text/plain";
    body_.clear();
    headers_.clear();
    const HTTPMethod method = request.method == "POST" ? HTTP_POST : HTTP_GET;
    const Handler *handler = &notFound_;
    for (const Route &route : routes_) {
      if (route.path == request.path && route.method == method) handler = &route.handler;
    }
    if (*handler) (*handler)();
    std::lock_guard<std::mutex> lock(pending->mutex);
    pending->response.code = code_;
    pending->response.type = type_;
    pending->response.body = body_;
    pending->response.headers = headers_;
    pending->done = true;
    pending->ready.notify_all();
  }
}

void WebServer::sendHeader(const char *name, const char *value) {
  headers_.emplace_back(name, value);
}

void WebServer::send(int code, const char *type, const char *body) {
  code_ = code;
  type_ = type;
  body_ = body;
}

void WebServer::send_P(int code, const char *type, const char *body, size_t length) {
  code_ = code;
  type_ = type;
  body_.assign(body, length);
}

String WebServer::arg(const char *name) {
  const auto it = args_.find(name);
  return it == args_.end() ? String() : String(it->second);
}

bool WebServer::hasArg(const char *name) { return args_.count(name) != 0; }

// ============================================================ emulator API ==

namespace emu {

namespace {

std::string readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream out;
  out << in.rdbuf();
  return out.str();
}

std::string arg(const HttpRequest &request, const char *name) {
  const auto it = request.query.find(name);
  return it == request.query.end() ? "" : it->second;
}

void handleState(const HttpRequest &request, HttpResponse &response) {
  JsonDocument doc;
  deserializeJson(doc, worldStateJson());
  JsonObject board = doc["board"].to<JsonObject>();
  board["brightness"] = g_board.brightness.load();
  board["cpuMhz"] = g_board.cpuMhz.load();
  board["rotation"] = g_board.rotation.load();
  board["uptimeS"] = millis() / 1000.0;
  board["resetReason"] = g_board.resetReason;
  board["apSsid"] = g_board.apSsid;
  board["webPort"] = g_options.webPort;
  board["logPeriodic"] = g_logPeriodic.load();
  // The page polls this from the moment the process starts. Until setup()
  // has finished, the firmware's locks and links do not exist yet: asking it
  // for its status then crashed the emulator now and again at start-up.
  const bbw::LiveStatus st = g_firmwareReady.load() ? app::status() : bbw::LiveStatus();
  JsonObject fw = doc["firmware"].to<JsonObject>();
  fw["state"] = bbw::stateName(st.state);
  fw["scaleConnected"] = st.scaleConnected;
  fw["botConnected"] = st.botConnected;
  fw["weightG"] = st.weightG;
  fw["targetG"] = st.targetWeightG;
  fw["shotTimeS"] = st.shotTimeS;
  JsonArray lines = doc["log"].to<JsonArray>();
  uint64_t last = strtoull(arg(request, "log").c_str(), nullptr, 10);
  for (const LogLine &line : logSince(last, 400)) {
    JsonArray entry = lines.add<JsonArray>();
    entry.add(line.ms);
    entry.add(line.category);
    entry.add(line.text);
    last = line.seq;
  }
  doc["logSeq"] = last;
  response.type = "application/json";
  serializeJson(doc, response.body);
}

void handle(const HttpRequest &request, HttpResponse &response) {
  const std::string &path = request.path;
  if (request.method == "GET") {
    if (path == "/" || path == "/index.html") {
      response.type = "text/html; charset=utf-8";
      response.body = readFile(g_options.uiDir + "/index.html");
      if (response.body.empty()) {
        response.code = 404;
        response.body = "emulator page not found in " + g_options.uiDir;
      }
    } else if (path == "/frame") {
      std::vector<uint8_t> rgba;
      uint64_t counter = 0;
      int width = 0, height = 0;
      const uint64_t since = strtoull(arg(request, "since").c_str(), nullptr, 10);
      if (frameCopy(since, rgba, counter, width, height)) {
        response.type = "application/octet-stream";
        response.body.assign(rgba.begin(), rgba.end());
      } else {
        response.code = 204;
      }
      response.headers.emplace_back("X-Frame", std::to_string(counter));
      response.headers.emplace_back("X-Size",
                                    std::to_string(width) + "x" + std::to_string(height));
    } else if (path == "/state") {
      handleState(request, response);
    } else {
      response.code = 404;
    }
    return;
  }
  // Everything below changes something: POST with the body the page sends.
  if (request.method != "POST" || request.body != "{}") {
    response.code = 403;
    return;
  }
  response.type = "application/json";
  response.body = "{\"ok\":true}";
  if (path == "/touch") {
    g_board.touchX = atoi(arg(request, "x").c_str());
    g_board.touchY = atoi(arg(request, "y").c_str());
    const bool down = arg(request, "down") == "1";
    if (down && !g_board.touchDown) g_board.touchDownAtMs = millis();
    g_board.touchDown = down;
  } else if (path == "/touchkey") {
    touchKey(arg(request, "down") == "1");
  } else if (path == "/button") {
    const bool down = arg(request, "down") == "1";
    if (down && !g_board.bootButtonDown) g_board.bootDownAtMs = millis();
    g_board.bootButtonDown = down;
  } else if (path == "/serial") {
    log("serial", "> %s", arg(request, "line").c_str());
    serialInject(arg(request, "line"));
  } else if (path == "/set") {
    const std::string device = arg(request, "device"), key = arg(request, "key");
    if (device == "emu" && key == "logPeriodic") {
      g_logPeriodic = arg(request, "value") == "1";
    } else if (!worldSet(device, key, arg(request, "value"))) {
      response.code = 400;
      response.body = "{\"ok\":false}";
    }
  } else if (path == "/reset") {
    const std::string kind = arg(request, "kind");
    if (kind == "crash") {
      log("emu", "controller crash injected");
      restart(Reset::PANIC);
    } else if (kind == "erase") {
      eraseFlash();
      restart(Reset::POWER_ON);
    } else {
      restart(Reset::POWER_ON);
    }
  } else {
    response.code = 404;
  }
}

}  // namespace

void apiBegin() {
  if (!httpServe(g_options.emuPort, handle)) {
    log("emu", "emulator page: port %d is in use", g_options.emuPort);
  }
}

}  // namespace emu
