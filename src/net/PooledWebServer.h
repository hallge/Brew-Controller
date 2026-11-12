#pragma once
// WebServer that does not let an idle connection block the others (12 Nov 2026). See core/ConnPool.h.
// Relies on WebServer's protected members: _server, _currentClient, _currentStatus, _statusChange,
// _nullDelay, _contentLength, _currentUpload, _currentRaw, _parseRequest(), _handleRequest().
#ifndef BBW_EMULATOR
#include <WebServer.h>
#include <WiFiClient.h>
#include "core/ConnPool.h"
#include "platform/NetWorker.h"

class PooledWebServer : public WebServer {
 public:
  using WebServer::WebServer;

  void handleClient() override {
    // Accept everything waiting; a full pool leaves the rest in the listen backlog.
    while (!_pool.full()) {
      WiFiClient c = _server.available();
      if (!c) break;
      _pool.add(c, millis());
    }
    WiFiClient ready;
    if (!_pool.takeReady(millis(), HTTP_MAX_DATA_WAIT, ready)) {
      if (_nullDelay) delay(1);
      return;
    }
    _currentClient = ready;
    _currentStatus = HC_WAIT_READ;
    _statusChange = millis();
    if (_parseRequest(_currentClient)) {
      _currentClient.setTimeout(HTTP_MAX_SEND_WAIT / 1000);
      _contentLength = CONTENT_LENGTH_NOT_SET;
      bbw::netWebRequest(true);   // a network job asked for in this request may wait for a background one
      _handleRequest();
      bbw::netWebRequest(false);
    }
    _currentClient = WiFiClient();
    _currentStatus = HC_NONE;
    _currentUpload.reset();
    _currentRaw.reset();
  }

 private:
  bbw::ConnPool<WiFiClient, 6> _pool;
};
#endif
