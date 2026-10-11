#pragma once
#include <Arduino.h>

// Serial log lines: "[  12.345] I scale: ..." - one line per event.
#define BBW_LOG(level, fmt, ...) \
  Serial.printf("[%8.3f] " level " " fmt "\n", millis() / 1000.0, ##__VA_ARGS__)
#define LOGI(fmt, ...) BBW_LOG("I", fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) BBW_LOG("W", fmt, ##__VA_ARGS__)
