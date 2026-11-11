// Plain-C helpers for Swift (BrewHostExtras.h). Reads the same palette table the board's screen uses.
#include "BrewHostExtras.h"

#include "ui/Palette.h"

extern "C" {

int bbw_palette_count(void) { return static_cast<int>(bbw::UI_THEME_COUNT); }

void bbw_palette(int theme, uint32_t out[10]) {
  if (!out) return;
  const bbw::Palette &p = bbw::paletteFor(theme < 0 || theme > 255 ? 0 : static_cast<uint8_t>(theme));
  out[0] = p.bg;
  out[1] = p.text;
  out[2] = p.dim;
  out[3] = p.ok;
  out[4] = p.bad;
  out[5] = p.sleep;
  out[6] = p.accent;
  out[7] = p.panel;
  out[8] = p.track;
  out[9] = p.select;
}

const char *bbw_palette_name(int theme) {
  return bbw::PALETTE_THEME_NAMES[theme < 0 || theme >= static_cast<int>(bbw::UI_THEME_COUNT) ? 0 : theme];
}

}  // extern "C"
