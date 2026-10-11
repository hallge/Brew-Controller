#include "emu_http.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdio>
#include <thread>

namespace emu {

namespace {

std::string urlDecode(const std::string &in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '+') {
      out += ' ';
    } else if (in[i] == '%' && i + 2 < in.size() && isxdigit(static_cast<unsigned char>(in[i + 1])) &&
               isxdigit(static_cast<unsigned char>(in[i + 2]))) {
      out += static_cast<char>(strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += in[i];
    }
  }
  return out;
}

void parseArgs(const std::string &text, std::map<std::string, std::string> &out) {
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('&', start);
    if (end == std::string::npos) end = text.size();
    const std::string pair = text.substr(start, end - start);
    const size_t eq = pair.find('=');
    if (!pair.empty()) {
      out[urlDecode(pair.substr(0, eq))] = eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
    }
    start = end + 1;
  }
}

const char *reason(int code) {
  switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 409: return "Conflict";
    default: return code < 400 ? "OK" : "Error";
  }
}

bool sendAll(SOCKET s, const char *data, size_t length) {
  while (length > 0) {
    const int n = send(s, data, static_cast<int>(length > 65536 ? 65536 : length), 0);
    if (n <= 0) return false;
    data += n;
    length -= static_cast<size_t>(n);
  }
  return true;
}

void serveConnection(SOCKET s, std::function<void(const HttpRequest &, HttpResponse &)> handler) {
  std::string buffer;
  char chunk[8192];
  for (;;) {
    // Read one request: headers, then Content-Length bytes of body.
    size_t headerEnd;
    while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
      const int n = recv(s, chunk, sizeof(chunk), 0);
      if (n <= 0 || buffer.size() > (1u << 20)) { closesocket(s); return; }
      buffer.append(chunk, static_cast<size_t>(n));
    }
    const std::string head = buffer.substr(0, headerEnd);
    size_t contentLength = 0;
    {
      std::string lower = head;
      for (char &c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
      const size_t at = lower.find("content-length:");
      if (at != std::string::npos) contentLength = strtoul(lower.c_str() + at + 15, nullptr, 10);
    }
    if (contentLength > (4u << 20)) { closesocket(s); return; }
    while (buffer.size() < headerEnd + 4 + contentLength) {
      const int n = recv(s, chunk, sizeof(chunk), 0);
      if (n <= 0) { closesocket(s); return; }
      buffer.append(chunk, static_cast<size_t>(n));
    }

    HttpRequest request;
    const size_t lineEnd = head.find("\r\n");
    const std::string line = head.substr(0, lineEnd);
    const size_t sp1 = line.find(' ');
    const size_t sp2 = line.rfind(' ');
    if (sp1 == std::string::npos || sp2 <= sp1) { closesocket(s); return; }
    request.method = line.substr(0, sp1);
    std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const size_t q = target.find('?');
    request.path = urlDecode(target.substr(0, q));
    if (q != std::string::npos) parseArgs(target.substr(q + 1), request.query);
    request.body = buffer.substr(headerEnd + 4, contentLength);
    buffer.erase(0, headerEnd + 4 + contentLength);
    if (head.find("application/x-www-form-urlencoded") != std::string::npos) {
      parseArgs(request.body, request.query);
    }

    HttpResponse response;
    handler(request, response);

    char status[256];
    snprintf(status, sizeof(status),
             "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
             "Cache-Control: no-store\r\nConnection: keep-alive\r\n",
             response.code, reason(response.code), response.type.c_str(), response.body.size());
    std::string out = status;
    for (const auto &h : response.headers) out += h.first + ": " + h.second + "\r\n";
    out += "\r\n";
    if (!sendAll(s, out.data(), out.size()) ||
        !sendAll(s, response.body.data(), response.body.size())) {
      closesocket(s);
      return;
    }
  }
}

}  // namespace

bool httpServe(int port, std::function<void(const HttpRequest &, HttpResponse &)> handler) {
  static bool started = false;
  if (!started) {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    started = true;
  }
  SOCKET listener = INVALID_SOCKET;
  // After an emulated restart the previous process may still be closing.
  for (int attempt = 0; attempt < 50; ++attempt) {
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // this machine only
    if (bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0 &&
        listen(listener, 16) == 0) {
      break;
    }
    closesocket(listener);
    listener = INVALID_SOCKET;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (listener == INVALID_SOCKET) return false;
  std::thread([listener, handler]() {
    for (;;) {
      const SOCKET s = accept(listener, nullptr, nullptr);
      if (s == INVALID_SOCKET) continue;
      int one = 1;
      setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof(one));
      std::thread(serveConnection, s, handler).detach();
    }
  }).detach();
  return true;
}

}  // namespace emu
