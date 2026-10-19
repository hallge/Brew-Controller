#pragma once
// LittleFS stand-in: files live under <data dir>/fs/.
#include <Arduino.h>

class File {
 public:
  File() {}
  explicit File(FILE *handle) : handle_(handle) {}
  explicit operator bool() const { return handle_ != nullptr; }
  size_t read(uint8_t *buffer, size_t length) { return handle_ ? fread(buffer, 1, length, handle_) : 0; }
  size_t write(const uint8_t *buffer, size_t length) {
    return handle_ ? fwrite(buffer, 1, length, handle_) : 0;
  }
  bool seek(size_t position) { return handle_ && fseek(handle_, static_cast<long>(position), SEEK_SET) == 0; }
  size_t size() {
    if (!handle_) return 0;
    const long at = ftell(handle_);
    fseek(handle_, 0, SEEK_END);
    const long end = ftell(handle_);
    fseek(handle_, at, SEEK_SET);
    return end < 0 ? 0 : static_cast<size_t>(end);
  }
  void close() {
    if (handle_) fclose(handle_);
    handle_ = nullptr;
  }

 private:
  FILE *handle_ = nullptr;
};

class LittleFSClass {
 public:
  bool begin(bool formatOnFail = false, const char *basePath = "/littlefs", uint8_t maxOpenFiles = 10,
             const char *partitionLabel = "spiffs");
  File open(const char *path, const char *mode = "r");
  bool exists(const char *path);
  bool remove(const char *path);
  bool rename(const char *from, const char *to);  // replaces `to`, as LittleFS does
  size_t totalBytes() { return 3900u * 1024u; }  // the board's partition
  size_t usedBytes();
};
extern LittleFSClass LittleFS;
