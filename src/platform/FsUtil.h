#pragma once
// Whether a file exists on the LittleFS partition, without noise.
//
// On this Arduino core LittleFS.exists() works by opening the file, and
// opening a file that is not there prints "[E][vfs_api.cpp] ... does not
// exist" on the serial port: every boot of a new board, and every check for
// an optional file. stat() asks the same question quietly.

#include <LittleFS.h>

#ifndef BBW_EMULATOR
#include <stdio.h>
#include <sys/stat.h>
#endif

namespace bbw {

inline bool fileExists(const char *path) {
#ifdef BBW_EMULATOR
  return LittleFS.exists(path);
#else
  char full[96];
  snprintf(full, sizeof(full), "/littlefs%s", path);  // where Store::begin() mounts it
  struct stat info;
  return stat(full, &info) == 0;
#endif
}

}  // namespace bbw
