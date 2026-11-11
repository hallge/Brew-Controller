#include "mqtt_client.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#ifndef _WIN32
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace bbw {
namespace mqtt {

namespace {

void putU16(std::vector<uint8_t> &v, size_t n) {
  v.push_back(static_cast<uint8_t>(n >> 8));
  v.push_back(static_cast<uint8_t>(n & 0xFF));
}
void putString(std::vector<uint8_t> &v, const char *s, size_t n) {
  putU16(v, n);
  v.insert(v.end(), s, s + n);
}
void putString(std::vector<uint8_t> &v, const char *s) { putString(v, s, strlen(s)); }

// Fixed header + remaining length (1 to 4 bytes) + body.
std::vector<uint8_t> frame(uint8_t first, const std::vector<uint8_t> &body) {
  std::vector<uint8_t> out;
  out.push_back(first);
  size_t n = body.size();
  do {
    uint8_t digit = static_cast<uint8_t>(n % 128);
    n /= 128;
    if (n) digit |= 0x80;
    out.push_back(digit);
  } while (n);
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

int64_t (*g_clock)() = nullptr;

int64_t nowMs() {
  if (g_clock) return g_clock();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

void setClockForTests(int64_t (*now)()) { g_clock = now; }

std::vector<uint8_t> encodeConnect(const char *clientId, const char *user, const char *password,
                                   const char *willTopic, const char *willPayload, bool willRetain,
                                   uint16_t keepAliveS) {
  std::vector<uint8_t> b;
  putString(b, "MQTT");
  b.push_back(4);  // 3.1.1
  uint8_t flags = 0x02;  // clean session
  const bool will = willTopic && willTopic[0];
  const bool hasUser = user && user[0];
  if (will) {
    flags |= 0x04;  // will, QoS 0
    if (willRetain) flags |= 0x20;
  }
  if (hasUser) {
    flags |= 0x80;
    if (password) flags |= 0x40;
  }
  b.push_back(flags);
  putU16(b, keepAliveS);
  putString(b, clientId ? clientId : "");
  if (will) {
    putString(b, willTopic);
    putString(b, willPayload ? willPayload : "");
  }
  if (hasUser) {
    putString(b, user);
    if (password) putString(b, password);
  }
  return frame(0x10, b);
}

std::vector<uint8_t> encodePublish(const char *topic, const uint8_t *payload, size_t length, bool retain) {
  std::vector<uint8_t> b;
  putString(b, topic);
  if (length) b.insert(b.end(), payload, payload + length);
  return frame(static_cast<uint8_t>(0x30 | (retain ? 1 : 0)), b);
}

std::vector<uint8_t> encodeSubscribe(uint16_t packetId, const char *topic) {
  std::vector<uint8_t> b;
  putU16(b, packetId);
  putString(b, topic);
  b.push_back(0);  // QoS 0
  return frame(0x82, b);
}

std::vector<uint8_t> encodePuback(uint16_t packetId) {
  std::vector<uint8_t> b;
  putU16(b, packetId);
  return frame(0x40, b);
}

std::vector<uint8_t> encodePing() { return {0xC0, 0x00}; }
std::vector<uint8_t> encodeDisconnect() { return {0xE0, 0x00}; }

int parsePacket(const uint8_t *buf, size_t length, Packet &out) {
  if (length < 2) return 0;
  size_t remaining = 0, multiplier = 1, i = 1;
  for (;; ++i) {
    if (i >= length) return 0;
    if (i > 4) return -1;
    remaining += (buf[i] & 0x7F) * multiplier;
    multiplier *= 128;
    if (!(buf[i] & 0x80)) break;
  }
  ++i;
  if (length < i + remaining) return 0;
  out.type = buf[0] >> 4;
  out.flags = buf[0] & 0x0F;
  out.body = buf + i;
  out.bodyLength = remaining;
  out.total = i + remaining;
  return 1;
}

bool parsePublish(const Packet &p, Publish &out) {
  if (p.type != PUBLISH || p.bodyLength < 2) return false;
  const size_t topicLen = (static_cast<size_t>(p.body[0]) << 8) | p.body[1];
  size_t at = 2 + topicLen;
  if (at > p.bodyLength) return false;
  out.topic = reinterpret_cast<const char *>(p.body + 2);
  out.topicLength = topicLen;
  out.qos = (p.flags >> 1) & 3;
  out.packetId = 0;
  if (out.qos > 0) {
    if (at + 2 > p.bodyLength) return false;
    out.packetId = static_cast<uint16_t>((p.body[at] << 8) | p.body[at + 1]);
    at += 2;
  }
  out.payload = p.body + at;
  out.payloadLength = p.bodyLength - at;
  return true;
}

// ---- Session -----------------------------------------------------------------------------------

bool Session::send(const std::vector<uint8_t> &packet) {
  if (!transport_ || !transport_->sendAll(packet.data(), packet.size())) {
    drop();
    return false;
  }
  lastOutMs_ = nowMs();
  return true;
}

void Session::drop() {
  connected_ = false;
  pingOut_ = false;
  in_.clear();
  if (transport_) transport_->close();
}

bool Session::connect(const char *host, uint16_t port, const char *clientId, const char *user,
                      const char *password, const char *willTopic, const char *willPayload, int &rc,
                      std::string &problem, int timeoutMs) {
  drop();
  rc = -2;
  if (!transport_ || !transport_->open(host, port, timeoutMs, problem)) return false;
  rc = -1;
  if (!send(encodeConnect(clientId, user, password, willTopic, willPayload, true, keepAliveS_))) return false;
  const int64_t until = nowMs() + timeoutMs;
  uint8_t chunk[256];
  while (nowMs() < until) {
    const int n = transport_->receive(chunk, sizeof(chunk), 50);
    if (n < 0) {
      drop();
      return false;
    }
    in_.insert(in_.end(), chunk, chunk + n);
    Packet p;
    const int got = parsePacket(in_.data(), in_.size(), p);
    if (got < 0) break;
    if (got == 0) continue;
    if (p.type != CONNACK || p.bodyLength != 2) break;
    rc = p.body[1];
    in_.erase(in_.begin(), in_.begin() + p.total);
    if (rc != 0) {
      drop();
      return false;
    }
    connected_ = true;
    lastInMs_ = lastOutMs_ = nowMs();
    return true;
  }
  drop();
  return false;
}

bool Session::publish(const char *topic, const char *payload, bool retain) {
  if (!connected_) return false;
  return send(encodePublish(topic, reinterpret_cast<const uint8_t *>(payload), payload ? strlen(payload) : 0, retain));
}

bool Session::subscribe(const char *topic) {
  if (!connected_) return false;
  if (nextId_ == 0) nextId_ = 1;
  return send(encodeSubscribe(nextId_++, topic));
}

void Session::disconnect() {
  if (connected_) send(encodeDisconnect());
  drop();
}

bool Session::handle(const Packet &p) {
  switch (p.type) {
    case PINGRESP: pingOut_ = false; break;
    case PUBLISH: {
      Publish pub;
      if (!parsePublish(p, pub)) return false;
      if (pub.qos == 1 && !send(encodePuback(pub.packetId))) return false;
      if (pub.qos < 2 && onMessage_) {
        onMessage_(std::string(pub.topic, pub.topicLength), pub.payload, pub.payloadLength);
      }
      break;
    }
    default: break;  // SUBACK, PUBACK: nothing to do at QoS 0
  }
  return true;
}

bool Session::service() {
  if (!connected_) return false;
  uint8_t chunk[512];
  for (int rounds = 0; rounds < 8; ++rounds) {
    const int n = transport_->receive(chunk, sizeof(chunk), 0);
    if (n < 0) {
      drop();
      return false;
    }
    if (n == 0) break;
    lastInMs_ = nowMs();
    in_.insert(in_.end(), chunk, chunk + n);
    if (in_.size() > 8192) {  // nothing here is that big
      drop();
      return false;
    }
    for (;;) {
      Packet p;
      const int got = parsePacket(in_.data(), in_.size(), p);
      if (got < 0) {
        drop();
        return false;
      }
      if (got == 0) break;
      const bool ok = handle(p);
      if (!connected_) return false;  // handle() sent and failed
      in_.erase(in_.begin(), in_.begin() + p.total);
      if (!ok) {
        drop();
        return false;
      }
    }
  }
  const int64_t now = nowMs();
  if (pingOut_ && now - lastOutMs_ > keepAliveS_ * 1500LL && now - lastInMs_ > keepAliveS_ * 1500LL) {
    drop();  // the broker has gone quiet
    return false;
  }
  if (!pingOut_ && now - lastOutMs_ >= keepAliveS_ * 500LL) {
    if (!send(encodePing())) return false;
    pingOut_ = true;
  }
  return connected_;
}

// ---- POSIX socket transport --------------------------------------------------------------------
#ifndef _WIN32
namespace {

class SocketTransport : public Transport {
 public:
  ~SocketTransport() override { close(); }

  bool open(const char *host, uint16_t port, int timeoutMs, std::string &problem) override {
    close();
    addrinfo hints = {};
    hints.ai_socktype = SOCK_STREAM;
    char service[8];
    snprintf(service, sizeof(service), "%u", static_cast<unsigned>(port));
    addrinfo *list = nullptr;
    if (getaddrinfo(host, service, &hints, &list) != 0 || !list) {
      problem = "the broker's name could not be found";
      return false;
    }
    for (addrinfo *a = list; a; a = a->ai_next) {
      const int s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (s < 0) continue;
      const int flags = fcntl(s, F_GETFL, 0);
      fcntl(s, F_SETFL, flags | O_NONBLOCK);
      int r = ::connect(s, a->ai_addr, a->ai_addrlen);
      if (r < 0 && errno == EINPROGRESS) {
        pollfd p = {s, POLLOUT, 0};
        if (poll(&p, 1, timeoutMs) == 1) {
          int err = 0;
          socklen_t len = sizeof(err);
          getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
          r = err == 0 ? 0 : -1;
        }
      }
      if (r == 0) {
        fcntl(s, F_SETFL, flags);
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
        setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        timeval tv = {2, 0};  // a stalled send must not hold up the loop for long
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        fd_ = s;
        break;
      }
      ::close(s);
    }
    freeaddrinfo(list);
    if (fd_ < 0) problem = "the broker could not be reached";
    return fd_ >= 0;
  }

  bool sendAll(const uint8_t *data, size_t length) override {
    if (fd_ < 0) return false;
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    size_t sent = 0;
    while (sent < length) {
      const ssize_t n = ::send(fd_, data + sent, length - sent, flags);
      if (n <= 0) {
        if (n < 0 && errno == EINTR) continue;
        return false;
      }
      sent += static_cast<size_t>(n);
    }
    return true;
  }

  int receive(uint8_t *buf, size_t cap, int timeoutMs) override {
    if (fd_ < 0) return -1;
    pollfd p = {fd_, POLLIN, 0};
    const int r = poll(&p, 1, timeoutMs);
    if (r < 0) return errno == EINTR ? 0 : -1;
    if (r == 0) return 0;
    const ssize_t n = ::recv(fd_, buf, cap, 0);
    if (n > 0) return static_cast<int>(n);
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
    return -1;  // 0: the broker closed
  }

  void close() override {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

 private:
  int fd_ = -1;
};

}  // namespace

std::unique_ptr<Transport> makeSocketTransport() { return std::unique_ptr<Transport>(new SocketTransport()); }
#else
std::unique_ptr<Transport> makeSocketTransport() { return nullptr; }
#endif

}  // namespace mqtt
}  // namespace bbw
