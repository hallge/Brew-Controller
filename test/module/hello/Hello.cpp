#include "Hello.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace bbw {
namespace addon {
namespace {

int g_ctorRan = 0;
struct CtorProbe {
  CtorProbe() { g_ctorRan = 0x5A; }
} g_probe;  // a static constructor: must have run before the entry

struct Shape {
  virtual ~Shape() = default;
  virtual int sides() const = 0;
};
struct Triangle : Shape {
  int sides() const override { return 3; }
};
struct Square : Shape {
  int sides() const override { return 4; }
};

// Initialised data and read-only data of some size, so relocations of both are exercised.
const uint16_t RODATA[64] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                             22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
                             41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                             60, 61, 62, 63, 64};  // sum 2080
uint16_t g_data[8] = {10, 20, 30, 40, 50, 60, 70, 80};  // sum 360
const char *const NAMES[] = {"alpha", "beta", "gamma"};   // pointers in read-only data: relocated

const ConfigField FIELDS[] = {
    {"greeting", "Greeting", FieldType::TEXT, "hello", "Shown in the status line"},
};

class Hello : public Addon, public GrinderDriver {
 public:
  const AddonInfo &info() const override {
    static const AddonInfo i = {"hello", "Hello (loader spike)", "0.0.1", ADDON_API_VERSION, Kind::GRINDER, FIELDS, 1};
    return i;
  }
  void begin(Host &host) override {
    host_ = host_ ? host_ : &host;
    host_ = &host;
    buffer_ = static_cast<uint8_t *>(host.allocate(1024));
    if (buffer_) {
      for (int i = 0; i < 1024; ++i) buffer_[i] = static_cast<uint8_t>(i * 7 + 3);
    }
    ticks_ = 0;
    maxGapMs_ = 0;
    lastMs_ = 0;
    fault_[0] = '\0';
    host.log("hello: begin");
  }
  void end() override { host_ = nullptr; }
  void tick(uint32_t nowMs) override {
    ++ticks_;
    if (lastMs_) {
      const uint32_t gap = nowMs - lastMs_;
      if (gap > maxGapMs_) maxGapMs_ = gap;
    }
    lastMs_ = nowMs;
    if (fault_[0]) return;
    if (g_ctorRan != 0x5A) return fail("static constructor did not run");
    Triangle t;
    Square q;
    const Shape *shapes[2] = {&t, &q};
    int sides = 0;
    for (const Shape *s : shapes) sides += s->sides();
    if (sides != 7) return fail("virtual call");
    uint32_t sum = 0;
    for (uint16_t v : RODATA) sum += v;
    if (sum != 2080) return fail("read-only data");
    sum = 0;
    for (uint16_t v : g_data) sum += v;
    if (sum != 360) return fail("initialised data");
    if (strcmp(NAMES[1], "beta") != 0) return fail("relocated pointer");
    if (buffer_) {
      const int i = static_cast<int>(ticks_ % 1024);
      if (buffer_[i] != static_cast<uint8_t>(i * 7 + 3)) return fail("allocated memory");
    }
    if (ticks_ % 1000 == 0) {
      char text[32];
      snprintf(text, sizeof(text), "%.2f", 3.14159);
      if (strtod(text, nullptr) < 3.1 || strtod(text, nullptr) > 3.2) return fail("snprintf or strtod");
    }
  }
  GrinderDriver *grinder() override { return this; }
  bool handleRoute(const Request &request, Response &response) override {
    snprintf(response.body, sizeof(response.body), "{\"hello\":true,\"path\":\"%s\",\"ticks\":%lu}", request.path,
             static_cast<unsigned long>(ticks_));
    return true;
  }
  void statusText(char *out, size_t size) const override {
    const char *greeting = host_ ? host_->config("greeting") : "";
    if (fault_[0]) snprintf(out, size, "FAULT: %s (after %lu ticks)", fault_, static_cast<unsigned long>(ticks_));
    else snprintf(out, size, "ok: %s, %lu ticks, longest gap %lu ms", greeting, static_cast<unsigned long>(ticks_),
                  static_cast<unsigned long>(maxGapMs_));
  }
  // GrinderDriver: nothing to do.
  uint32_t caps() const override { return 0; }
  Link link() const override { return Link::OFF; }
  bool takeGrind(GrindEvent &) override { return false; }

 private:
  void fail(const char *what) { snprintf(fault_, sizeof(fault_), "%s", what); }
  Host *host_ = nullptr;
  uint8_t *buffer_ = nullptr;
  uint32_t ticks_ = 0, maxGapMs_ = 0, lastMs_ = 0;
  char fault_[40] = {};
};

}  // namespace

Addon *makeHello() { return new Hello(); }

}  // namespace addon
}  // namespace bbw
