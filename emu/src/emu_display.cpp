// The AMOLED panel and its touch controller: LVGL draws into a frame buffer
// that the emulator page fetches; the mouse is the finger.

#include <LV_Helper.h>

#include <mutex>
#include <vector>

#include "emu.h"

namespace {

constexpr int MAX_W = 536, MAX_H = 536;

std::mutex g_frameMutex;
std::vector<uint8_t> g_frame;  // RGBA
int g_width = 536, g_height = 240;
uint64_t g_frameCounter = 0;

lv_disp_draw_buf_t g_drawBuffer;
lv_disp_drv_t g_displayDriver;
lv_indev_drv_t g_inputDriver;

void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
  {
    std::lock_guard<std::mutex> lock(g_frameMutex);
    for (int y = area->y1; y <= area->y2; ++y) {
      uint8_t *out = &g_frame[(static_cast<size_t>(y) * g_width + area->x1) * 4];
      for (int x = area->x1; x <= area->x2; ++x, ++pixels, out += 4) {
        const uint32_t c = lv_color_to32(*pixels);  // 0xAARRGGBB
        out[0] = (c >> 16) & 0xff;
        out[1] = (c >> 8) & 0xff;
        out[2] = c & 0xff;
        out[3] = 0xff;
      }
    }
    if (lv_disp_flush_is_last(driver)) ++g_frameCounter;
  }
  lv_disp_flush_ready(driver);
}

// Where the CST816T reports the touch key: (600, 120) on the panel's native
// axes, turned with the display like any other point.
void keyPoint(int &x, int &y) {
  x = 600;
  y = 120;
  switch (emu::g_board.rotation.load()) {
    case 1: x = 240 - 120; y = 600; break;
    case 2: x = 536 - 600; y = 240 - 120; break;
    case 3: x = 120; y = 536 - 600; break;
    default: break;
  }
}

LilyGo_Class::home_button_callback_t g_homeCallback = nullptr;
void *g_homeUser = nullptr;

void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
  const bool down = emu::pressed(emu::g_board.touchDown, emu::g_board.touchDownAtMs);
  // As in TouchClassCST816::getPoint: a touch at the key's coordinate goes to
  // the home-button callback, on every poll while held, and yields no point.
  const int tx = emu::g_board.touchX.load(), ty = emu::g_board.touchY.load();
  int keyX = 0, keyY = 0;
  keyPoint(keyX, keyY);
  // TouchClassCST816::getPoint compares with the key's exact coordinate. A
  // finger dragged past the edge of the panel is somewhere else, not the key.
  const bool isKey = tx == keyX && ty == keyY;
  if (down && isKey && g_homeCallback) {
    g_homeCallback(g_homeUser);
    data->state = LV_INDEV_STATE_REL;
    return;
  }
  data->point.x = static_cast<lv_coord_t>(emu::g_board.touchX.load());
  data->point.y = static_cast<lv_coord_t>(emu::g_board.touchY.load());
  data->state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

}  // namespace

bool LilyGo_Class::begin() {
  setRotation(0);
  return true;
}

void LilyGo_Class::setRotation(uint8_t rotation) {
  rotation_ = rotation % 4;
  emu::g_board.rotation = rotation_;
}

void LilyGo_Class::setBrightness(uint8_t level) { emu::g_board.brightness = level; }

void LilyGo_Class::setHomeButtonCallback(home_button_callback_t callback, void *user_data) {
  g_homeCallback = callback;
  g_homeUser = user_data;
}

void beginLvglHelper(LilyGo_Display &board, bool) {
  lv_init();
  g_width = board.width();
  g_height = board.height();
  {
    std::lock_guard<std::mutex> lock(g_frameMutex);
    g_frame.assign(static_cast<size_t>(g_width) * g_height * 4, 0);
    for (size_t i = 3; i < g_frame.size(); i += 4) g_frame[i] = 0xff;
  }
  static std::vector<lv_color_t> buffer(static_cast<size_t>(MAX_W) * MAX_H);
  lv_disp_draw_buf_init(&g_drawBuffer, buffer.data(), nullptr,
                        static_cast<uint32_t>(g_width) * g_height);
  lv_disp_drv_init(&g_displayDriver);
  g_displayDriver.hor_res = static_cast<lv_coord_t>(g_width);
  g_displayDriver.ver_res = static_cast<lv_coord_t>(g_height);
  g_displayDriver.flush_cb = flush;
  g_displayDriver.draw_buf = &g_drawBuffer;
  lv_disp_drv_register(&g_displayDriver);

  lv_indev_drv_init(&g_inputDriver);
  g_inputDriver.type = LV_INDEV_TYPE_POINTER;
  g_inputDriver.read_cb = readTouch;
  lv_indev_drv_register(&g_inputDriver);
}

namespace emu {

// The CST816T reports the key as a touch at (600, 120), past the end of the
// 536-pixel axis, and the library then applies the rotation's swap / mirror
// like for any other point. Nothing is filtered unless the firmware installs
// a home-button callback, so LVGL gets an off-screen press.
void touchKey(bool down) {
  int x = 0, y = 0;
  keyPoint(x, y);
  g_board.touchX = x;
  g_board.touchY = y;
  if (down && !g_board.touchDown) g_board.touchDownAtMs = millis();
  g_board.touchDown = down;
}

bool frameCopy(uint64_t since, std::vector<uint8_t> &rgba, uint64_t &counter, int &width,
               int &height) {
  std::lock_guard<std::mutex> lock(g_frameMutex);
  counter = g_frameCounter;
  width = g_width;
  height = g_height;
  if (g_frame.empty() || counter == since) return false;
  rgba = g_frame;
  return true;
}

}  // namespace emu
