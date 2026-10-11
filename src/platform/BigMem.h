#pragma once
// Where the large buffers live.
//
// The board has two kinds of RAM: about 400 kB inside the chip, which Wi-Fi,
// Bluetooth, TLS and every task's stack also need, and 8 MB of PSRAM beside
// it. Anything large that only the loop task works with (text read from the
// library, a shot's readings on their way to flash or the browser, scratch
// copies of the library) is taken from PSRAM at start-up with bigAlloc(),
// instead of being a global array, which the linker would place inside the
// chip for good.
//
// What stays inside the chip on purpose: the brew controller and the library
// it brews from. Stopping a shot must not depend on the external memory.
//
// PSRAM is not ready while global constructors run: call these from setup()
// or later. If the PSRAM is missing or full the memory comes from inside the
// chip instead, so the firmware still runs on a board without it.

#include <stddef.h>
#include <stdint.h>

#include <new>

#ifdef BBW_EMULATOR
#include <stdlib.h>
#else
#include <Arduino.h>
#include <esp_heap_caps.h>
#endif

namespace bbw {

// Zeroed memory that is never freed. Does not return if there is none at
// all: nothing could run without these buffers.
inline void *bigAlloc(size_t size) {
#ifdef BBW_EMULATOR
  void *memory = calloc(1, size);
#else
  void *memory = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = heap_caps_calloc(1, size, MALLOC_CAP_8BIT);
#endif
  if (!memory) abort();
  return memory;
}

template <typename T>
inline T *bigArray(size_t count) {
  return static_cast<T *>(bigAlloc(count * sizeof(T)));
}

template <typename T>
inline T *bigNew() {
  return new (bigAlloc(sizeof(T))) T();
}

// For the Setup page and the serial log: is there room to spare?
struct MemoryReport {
  bool known = false;            // false in the emulator
  uint32_t internalFree = 0;     // inside the chip, now
  uint32_t internalLowest = 0;   // the least it has been since start-up
  uint32_t internalLargest = 0;  // the biggest single piece that could be had
  uint32_t psramTotal = 0;
  uint32_t psramFree = 0;
};

inline MemoryReport memoryReport() {
  MemoryReport report;
#ifndef BBW_EMULATOR
  report.known = true;
  report.internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  report.internalLowest = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  report.internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  report.psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  report.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif
  return report;
}

}  // namespace bbw
