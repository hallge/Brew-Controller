#pragma once
// PC emulator stand-in for LilyGo-AMOLED-Series: a 1.91" RM67162 panel
// rendered into a frame buffer that the emulator page shows. Touch comes
// from the mouse. Rotations are numbered as in the library: 0 and 2 are
// landscape (536x240), 1 and 3 portrait; 0 has the USB-C end, with the touch
// key printed on the glass, on the right.

#include <Arduino.h>

class LilyGo_Display {
 public:
  virtual ~LilyGo_Display() = default;
  virtual uint16_t width() = 0;
  virtual uint16_t height() = 0;
};

class LilyGo_Class : public LilyGo_Display {
 public:
  bool begin();
  const char *getName() { return "T-Display AMOLED Plus 1.91\" (emulated)"; }
  void setRotation(uint8_t rotation);
  uint8_t getRotation() const { return rotation_; }
  void setBrightness(uint8_t level);
  // SensorLib's TouchDrvCSTXXX API: with a callback installed, the touch key
  // is delivered through it and is no longer reported as a point.
  typedef void (*home_button_callback_t)(void *user_data);
  void setHomeButtonCallback(home_button_callback_t callback, void *user_data = nullptr);
  uint16_t width() override { return (rotation_ & 1) ? 240 : 536; }
  uint16_t height() override { return (rotation_ & 1) ? 536 : 240; }

 private:
  uint8_t rotation_ = 0;
};
