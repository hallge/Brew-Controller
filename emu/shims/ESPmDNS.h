#pragma once
struct MDNSClass {
  bool begin(const char *) { return true; }
  bool addService(const char *, const char *, int) { return true; }
};
extern MDNSClass MDNS;
