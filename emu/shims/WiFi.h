#pragma once
// Wi-Fi stand-in. There is no radio: the firmware's web server listens on
// localhost instead (see WebServer.h).
#include <Arduino.h>

enum { WIFI_AP = 2, WIFI_AP_STA = 3 };
enum { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };

class IPAddress {
 public:
  String toString() const { return String("127.0.0.1"); }
};

class WiFiClass {
 public:
  void macAddress(uint8_t *mac);
  void mode(int) {}
  bool softAP(const char *ssid, const char *password);
  void setAutoReconnect(bool) {}
  void begin(const char *ssid, const char *password);
  int status() { return WL_DISCONNECTED; }
  IPAddress localIP() { return IPAddress(); }
};
extern WiFiClass WiFi;
