#pragma once
// A minimal MQTT 3.1.1 client for the iPhone build's Home Assistant link (the board uses PubSubClient;
// see NET.md for why this is not vendored). QoS 0 publishing and subscribing, retained messages, a
// will, user/password, keep-alive. Packet encoding and the session logic are portable and tested on
// Windows with a fake transport; the socket transport is POSIX only.
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

namespace bbw {
namespace mqtt {

// ---- packets (pure) ----------------------------------------------------------------------------
enum PacketType : uint8_t {
  CONNECT = 1, CONNACK = 2, PUBLISH = 3, PUBACK = 4, SUBSCRIBE = 8, SUBACK = 9, PINGREQ = 12,
  PINGRESP = 13, DISCONNECT = 14
};

std::vector<uint8_t> encodeConnect(const char *clientId, const char *user, const char *password,
                                   const char *willTopic, const char *willPayload, bool willRetain,
                                   uint16_t keepAliveS);
std::vector<uint8_t> encodePublish(const char *topic, const uint8_t *payload, size_t length, bool retain);
std::vector<uint8_t> encodeSubscribe(uint16_t packetId, const char *topic);  // QoS 0
std::vector<uint8_t> encodePuback(uint16_t packetId);
std::vector<uint8_t> encodePing();
std::vector<uint8_t> encodeDisconnect();

struct Packet {
  uint8_t type = 0;
  uint8_t flags = 0;
  const uint8_t *body = nullptr;
  size_t bodyLength = 0;
  size_t total = 0;  // bytes the whole packet took in the buffer
};
// 1: a whole packet is at the front of buf (filled in); 0: more bytes needed; -1: malformed.
int parsePacket(const uint8_t *buf, size_t length, Packet &out);

struct Publish {
  const char *topic = nullptr;  // not terminated: topicLength
  size_t topicLength = 0;
  const uint8_t *payload = nullptr;
  size_t payloadLength = 0;
  uint8_t qos = 0;
  uint16_t packetId = 0;
};
bool parsePublish(const Packet &packet, Publish &out);

// Tests: the clock the session's keep-alive runs on (milliseconds); nullptr: the steady clock.
void setClockForTests(int64_t (*nowMs)());

// ---- transport ---------------------------------------------------------------------------------
class Transport {
 public:
  virtual ~Transport() = default;
  virtual bool open(const char *host, uint16_t port, int timeoutMs, std::string &problem) = 0;
  virtual bool sendAll(const uint8_t *data, size_t length) = 0;
  // >0: bytes read; 0: nothing within timeoutMs; <0: the connection is gone.
  virtual int receive(uint8_t *buf, size_t cap, int timeoutMs) = 0;
  virtual void close() = 0;
};
// The POSIX socket transport; null where there are no POSIX sockets (the Windows test runner).
std::unique_ptr<Transport> makeSocketTransport();

// ---- session -----------------------------------------------------------------------------------
class Session {
 public:
  using OnMessage = void (*)(const std::string &topic, const uint8_t *payload, size_t length);
  explicit Session(std::unique_ptr<Transport> transport) : transport_(std::move(transport)) {}

  // true once the broker accepted. rc: CONNACK's return code, or -1 no answer, -2 no connection.
  bool connect(const char *host, uint16_t port, const char *clientId, const char *user,
               const char *password, const char *willTopic, const char *willPayload, int &rc,
               std::string &problem, int timeoutMs = 2000);
  bool connected() const { return connected_; }
  bool publish(const char *topic, const char *payload, bool retain);
  bool subscribe(const char *topic);
  void setOnMessage(OnMessage f) { onMessage_ = f; }
  // Reads what has arrived (never waits), answers pings, sends PINGREQ when due. false: dropped.
  bool service();
  void disconnect();
  void setKeepAlive(uint16_t seconds) { keepAliveS_ = seconds; }

 private:
  bool send(const std::vector<uint8_t> &packet);
  void drop();
  bool handle(const Packet &p);
  std::unique_ptr<Transport> transport_;
  OnMessage onMessage_ = nullptr;
  bool connected_ = false;
  uint16_t keepAliveS_ = 30;
  uint16_t nextId_ = 1;
  int64_t lastOutMs_ = 0, lastInMs_ = 0;
  bool pingOut_ = false;
  std::vector<uint8_t> in_;
};

}  // namespace mqtt
}  // namespace bbw
