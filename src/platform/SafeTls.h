#pragma once
// WiFiClientSecure with a framework bug worked around. Board only.
//
// In arduino-esp32 2.0.17, stop_ssl_socket() (WiFiClientSecure's stop) closes the socket, sets it
// to -1, and then clears the whole TLS context with memset, which leaves the socket number at 0.
// Every later stop() of the same client (HTTPClient's end() and then its destructor, and
// WiFiClientSecure's own destructor) then sees a "socket" 0 and calls close(0). File descriptor 0 is
// the console's at first; once that is closed, the next file opened anywhere can be given 0, and the
// next TLS client torn down closes that file under its owner. Seen on the board (v0.1.19, decoded
// from its core dump): the update task's client closed a LittleFS file the loop task had open, and
// LittleFS stopped the controller with an assertion.
//
// Use SafeTlsClient wherever a WiFiClientSecure would be: it puts the socket back to -1 after every
// stop, so a stopped client never closes anything again.

#ifndef BBW_EMULATOR

#include <WiFiClientSecure.h>

namespace bbw {

class SafeTlsClient : public WiFiClientSecure {
 public:
  void stop() override {
    WiFiClientSecure::stop();
    sslclient->socket = -1;
  }
  // The base destructor stops the client again, without this class's stop: it must find -1.
  ~SafeTlsClient() override { stop(); }
};

}  // namespace bbw

#endif  // BBW_EMULATOR
