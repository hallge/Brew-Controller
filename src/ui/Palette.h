#pragma once
// The six colour schemes (bbw::UiTheme, chosen on the web page and in the app): the colour each
// role on the screens takes. Plain data with no LVGL types, so the board's screen (Ui.cpp) and the
// iPhone app draw from the same table. The dark schemes keep a true black background, so AMOLED
// pixels stay off. Within a palette every colour differs, so a change of scheme can tell each
// object's colour's role from its value.
#include <stdint.h>

#include "core/Types.h"

namespace bbw {

struct Palette {
  uint32_t bg, text, dim, ok, bad, sleep, accent, panel, track, select;  // 0xRRGGBB
};

// In the order of the fields above, for a host that wants them by name.
constexpr const char *PALETTE_ROLE_NAMES[10] = {"bg",     "text",  "dim",   "ok",    "bad",
                                                "sleep",  "accent", "panel", "track", "select"};
constexpr const char *PALETTE_THEME_NAMES[UI_THEME_COUNT] = {"Dark",    "Light",   "Nord",
                                                             "Solarized Dark", "Dracula", "High contrast"};

constexpr Palette PALETTES[UI_THEME_COUNT] = {
    // bg        text      dim       ok        bad       sleep     accent    panel     track     select
    {0x000000, 0xFFFFFF, 0x8A8A8A, 0x2ECC71, 0xE74C3C, 0x777777, 0xE67E22, 0x1C1C1C, 0x2A2A2A, 0x3A2A12},  // Dark
    {0xF2F1EE, 0x141414, 0x5E5E5E, 0x1E8E4E, 0xC0392B, 0x8C8C8C, 0xC2560C, 0xFFFFFF, 0xD9D7D2, 0xF5DCC2},  // Light
    {0x000000, 0xECEFF4, 0xA3ABB9, 0xA3BE8C, 0xBF616A, 0x7B8394, 0xD08770, 0x3B4252, 0x4C566A, 0x5A4A45},  // Nord
    {0x000000, 0xEEE8D5, 0x93A1A1, 0x859900, 0xDC322F, 0x657B83, 0xCB4B16, 0x073642, 0x0F4654, 0x3D3A1E},  // Solarized Dark
    {0x000000, 0xF8F8F2, 0xA9ADC8, 0x50FA7B, 0xFF5555, 0x6272A4, 0xFFB86C, 0x282A36, 0x44475A, 0x4D3F3A},  // Dracula
    {0x000000, 0xFFFFFF, 0xD0D0D0, 0x00E676, 0xFF3B30, 0xA0A0A0, 0xFFB000, 0x1A1A1A, 0x3C3C3C, 0x4A3500},  // High contrast
};

// The palette of a scheme number; an unknown number gives Dark, as the screen does.
inline const Palette &paletteFor(uint8_t theme) { return PALETTES[theme < UI_THEME_COUNT ? theme : 0]; }

}  // namespace bbw
