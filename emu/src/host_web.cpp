// The firmware's WebServer: route table and the queue of pending requests, shared by the Windows
// emulator (requests arrive from emu_http.cpp's sockets) and the iPhone app (requests arrive from
// webEnqueue() in the same process). Handlers always run on the loop task inside handleClient().

#include <WebServer.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

#include "emu.h"
#include "http_parse.h"

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

namespace emu {

HttpResponse webDispatch(const HttpRequest &request, int timeoutMs) {
  auto pending = std::make_shared<Pending>();
  pending->request = request;
  {
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    g_pending.push_back(pending);
  }
  HttpResponse response;
  std::unique_lock<std::mutex> lock(pending->mutex);
  if (pending->ready.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                              [&] { return pending->done; })) {
    response = pending->response;
  } else {
    response.code = 503;
    response.body = "controller did not answer";
  }
  return response;
}

WebResponse webEnqueue(const std::string &method, const std::string &uri, const std::string &body,
                       const std::string &contentType, int timeoutMs) {
  HttpRequest request;
  request.method = method;
  const size_t q = uri.find('?');
  request.path = urlDecode(uri.substr(0, q));
  if (q != std::string::npos) parseArgs(uri.substr(q + 1), request.query);
  request.body = body;
  if (contentType.find("application/x-www-form-urlencoded") != std::string::npos) {
    parseArgs(request.body, request.query);
  }
  const HttpResponse response = webDispatch(request, timeoutMs);
  WebResponse out;
  out.status = response.code;
  out.contentType = response.type;
  out.headers = response.headers;
  out.body = response.body;
  return out;
}

}  // namespace emu

void WebServer::on(const char *path, HTTPMethod method, Handler handler) {
  routes_.push_back({path, method, handler});
}

void WebServer::begin() {
  if (!emu::g_hooks.webListen) return;  // in-process host: requests come from emu::webEnqueue
  if (!emu::g_hooks.webListen(emu::g_options.webPort)) {
    emu::log("emu", "device web UI: port %d is in use", emu::g_options.webPort);
  }
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
    uri_ = request.path;
    method_ = method;
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
