#pragma once
// PC emulator stand-in for the Arduino-ESP32 core. Only what this firmware
// uses. The C part is included by LVGL (lv_conf.h takes its tick from here).

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);
void delay(uint32_t ms);
size_t strlcpy(char *dst, const char *src, size_t size);
#ifdef __cplusplus
}

#include <sys/time.h>

#include <random>
#include <string>

#define PROGMEM
#define INPUT 1
#define INPUT_PULLUP 5
#define LOW 0
#define HIGH 1
// Variables marked this way survive an emulated software reset / crash, as
// RTC memory does on the chip. See emu_core.cpp.
#define RTC_NOINIT_ATTR __attribute__((section(".rtcni$m")))

void setup();
void loop();

void pinMode(int pin, int mode);
int digitalRead(int pin);
bool setCpuFrequencyMhz(uint32_t mhz);
void configTime(long gmtOffset, int daylightOffset, const char *server);
int settimeofday(const struct timeval *tv, const void *tz);
// newlib has it, the Windows C runtime spells it differently.
inline struct tm *gmtime_r(const time_t *t, struct tm *out) {
  return gmtime_s(out, t) == 0 ? out : nullptr;
}

typedef enum {
  ESP_RST_UNKNOWN,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
} esp_reset_reason_t;
esp_reset_reason_t esp_reset_reason();

struct EspClass {
  [[noreturn]] void restart();
};
extern EspClass ESP;

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  const char *c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  long toInt() const { return atol(s_.c_str()); }
  bool reserve(size_t n) { s_.reserve(n); return true; }
  bool concat(const char *s) { if (s) s_ += s; return true; }
  bool concat(const String &s) { s_ += s.s_; return true; }
  String &operator+=(const char *s) { concat(s); return *this; }
  String &operator+=(const String &s) { concat(s); return *this; }
  String &operator+=(char c) { s_ += c; return *this; }
  bool operator==(const char *s) const { return s_ == (s ? s : ""); }
  bool operator==(const String &s) const { return s_ == s.s_; }
  bool operator!=(const char *s) const { return !(*this == s); }
  char operator[](size_t i) const { return s_[i]; }
  const std::string &std() const { return s_; }

 private:
  std::string s_;
};

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i) write(data[i]);
    return length;
  }
  size_t print(const char *s) { return write(reinterpret_cast<const uint8_t *>(s), strlen(s)); }
  size_t print(const String &s) { return print(s.c_str()); }
  size_t println() { return print("\n"); }
  size_t println(const char *s) { return print(s) + println(); }
  size_t println(const String &s) { return println(s.c_str()); }
  size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

class HardwareSerial : public Print {
 public:
  using Print::write;
  void begin(unsigned long) {}
  void setTxTimeoutMs(uint32_t) {}
  int available();
  int read();
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *data, size_t length) override;
};
extern HardwareSerial Serial;

// ---------------------------------------------------------------- FreeRTOS --
// Tasks are std::threads, mutexes and queues are small host objects.

typedef uint32_t TickType_t;
typedef struct EmuSemaphore *SemaphoreHandle_t;
typedef struct EmuQueue *QueueHandle_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))

SemaphoreHandle_t xSemaphoreCreateMutex();
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
QueueHandle_t xQueueCreate(size_t length, size_t itemSize);
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);
int xQueueReset(QueueHandle_t queue);
TickType_t xTaskGetTickCount();
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t *previousWake, TickType_t increment);
int xTaskCreatePinnedToCore(void (*task)(void *), const char *name, uint32_t stack, void *arg,
                            int priority, void *handle, int core);

#endif  // __cplusplus

#ifdef __cplusplus
// Hardware random number, as on the ESP32.
inline uint32_t esp_random() {
  static std::random_device device;
  return device();
}
#endif
