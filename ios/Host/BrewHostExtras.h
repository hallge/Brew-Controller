#pragma once
// Plain-C helpers the Swift side needs and BrewHost.h does not carry. Add this header to the app's
// bridging header (ios/BrewControllerApp/BrewHost-Bridging-Header.h) next to BrewHost.h.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Number of colour schemes (bbw::UI_THEME_COUNT): 6.
int bbw_palette_count(void);
// The colours of scheme `theme` as 0xRRGGBB, in the order bg, text, dim, ok, bad, sleep, accent,
// panel, track, select (src/ui/Palette.h). An unknown scheme gives Dark, as the board's screen does.
void bbw_palette(int theme, uint32_t out[10]);
// The scheme's name ("Dark", "Light", "Nord", ...): a static string, never NULL.
const char *bbw_palette_name(int theme);

#ifdef __cplusplus
}
#endif
