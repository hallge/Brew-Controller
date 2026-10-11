#pragma once
// Arduino WebServer stand-in on a localhost socket. Requests are accepted on
// a background thread and handled inside handleClient(), i.e. on the loop
// task, exactly where the real one runs them.
#include <Arduino.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

enum HTTPMethod { HTTP_GET, HTTP_POST };
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)

class WebServer {
 public:
  typedef std::function<void()> Handler;
  explicit WebServer(int port) { (void)port; }
  void on(const char *path, HTTPMethod method, Handler handler);
  void onNotFound(Handler handler) { notFound_ = handler; }
  void begin();
  void handleClient();
  void setContentLength(size_t) {}
  void sendHeader(const char *name, const char *value);
  void send(int code, const char *type, const char *body);
  void send(int code, const char *type, const String &body) { send(code, type, body.c_str()); }
  void send_P(int code, const char *type, const char *body) { send(code, type, body); }
  void sendContent(const char *chunk) { body_ += chunk; }
  void sendContent(const String &chunk) { body_ += chunk.std(); }
  String arg(const char *name);
  bool hasArg(const char *name);

 private:
  struct Route {
    std::string path;
    HTTPMethod method;
    Handler handler;
  };
  std::vector<Route> routes_;
  Handler notFound_;
  std::map<std::string, std::string> args_;
  int code_ = 200;
  std::string type_, body_;
  std::vector<std::pair<std::string, std::string>> headers_;
};
