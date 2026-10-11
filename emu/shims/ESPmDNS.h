#pragma once
struct MDNSClass {
  bool begin(const char *) { return true; }
};
extern MDNSClass MDNS;
