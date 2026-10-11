#pragma once
// Minimal localhost HTTP/1.1 server. Kept free of firmware and Arduino
// headers because it includes the Windows socket headers.

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace emu {

struct HttpRequest {
  std::string method, path, body;
  std::map<std::string, std::string> query;  // URL and form-encoded body arguments
};

struct HttpResponse {
  int code = 200;
  std::string type = "text/plain";
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
};

// Runs `handler` on a per-connection thread. Returns false if the port could
// not be bound.
bool httpServe(int port, std::function<void(const HttpRequest &, HttpResponse &)> handler);

}  // namespace emu
