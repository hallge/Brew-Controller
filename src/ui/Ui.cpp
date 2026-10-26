#include "Ui.h"

#include <Arduino.h>
#include <LV_Helper.h>
#include <LilyGo_AMOLED.h>
#include <lvgl.h>

#include "App.h"
#include "core/SettingFormat.h"
#include "core/SettingsUtil.h"
#include "Log.h"
#include "Version.h"

using bbw::GrindAction;
using bbw::State;

// Screen language borrowed from the Yield Target Pro guide (status line, one
// dominant value, detail line), with its knob replaced by touch keys down the
// right-hand side. The brew screen adds a progress ring around the weight.

namespace ui {

namespace {

LilyGo_Class amoled;

// The colour schemes (bbw::UiTheme, chosen on the web page): the colours each role on the screens
// takes. The dark schemes keep a true black background, so AMOLED pixels stay off. Within a palette
// every colour differs, so a change of scheme can tell each object's colour's role from its value.
struct Palette {
  uint32_t bg, text, dim, ok, bad, sleep, accent, panel, track, select;
};
constexpr Palette PALETTES[bbw::UI_THEME_COUNT] = {
    // bg        text      dim       ok        bad       sleep     accent    panel     track     select
    {0x000000, 0xFFFFFF, 0x8A8A8A, 0x2ECC71, 0xE74C3C, 0x777777, 0xE67E22, 0x1C1C1C, 0x2A2A2A, 0x3A2A12},  // Dark
    {0xF2F1EE, 0x141414, 0x5E5E5E, 0x1E8E4E, 0xC0392B, 0x8C8C8C, 0xC2560C, 0xFFFFFF, 0xD9D7D2, 0xF5DCC2},  // Light
    {0x000000, 0xECEFF4, 0xA3ABB9, 0xA3BE8C, 0xBF616A, 0x7B8394, 0xD08770, 0x3B4252, 0x4C566A, 0x5A4A45},  // Nord
    {0x000000, 0xEEE8D5, 0x93A1A1, 0x859900, 0xDC322F, 0x657B83, 0xCB4B16, 0x073642, 0x0F4654, 0x3D3A1E},  // Solarized Dark
    {0x000000, 0xF8F8F2, 0xA9ADC8, 0x50FA7B, 0xFF5555, 0x6272A4, 0xFFB86C, 0x282A36, 0x44475A, 0x4D3F3A},  // Dracula
    {0x000000, 0xFFFFFF, 0xD0D0D0, 0x00E676, 0xFF3B30, 0xA0A0A0, 0xFFB000, 0x1A1A1A, 0x3C3C3C, 0x4A3500},  // High contrast
};
// The scheme in use: set before the screens are built, and by applyTheme.
uint32_t COLOR_BG = PALETTES[0].bg;
uint32_t COLOR_TEXT = PALETTES[0].text;
uint32_t COLOR_DIM = PALETTES[0].dim;
uint32_t COLOR_OK = PALETTES[0].ok;
uint32_t COLOR_BAD = PALETTES[0].bad;
uint32_t COLOR_SLEEP = PALETTES[0].sleep;
uint32_t COLOR_ACCENT = PALETTES[0].accent;
uint32_t COLOR_PANEL = PALETTES[0].panel;
uint32_t COLOR_TRACK = PALETTES[0].track;
uint32_t COLOR_SELECT = PALETTES[0].select;  // the selected row of a list
uint8_t themeShown = 0;

void setPalette(uint8_t theme) {
  const Palette &p = PALETTES[theme < bbw::UI_THEME_COUNT ? theme : 0];
  COLOR_BG = p.bg;
  COLOR_TEXT = p.text;
  COLOR_DIM = p.dim;
  COLOR_OK = p.ok;
  COLOR_BAD = p.bad;
  COLOR_SLEEP = p.sleep;
  COLOR_ACCENT = p.accent;
  COLOR_PANEL = p.panel;
  COLOR_TRACK = p.track;
  COLOR_SELECT = p.select;
  themeShown = theme < bbw::UI_THEME_COUNT ? theme : 0;
}

// Landscape with the USB-C connector on the left. On the 1.91" panel
// rotations 0 and 2 are landscape; 0 puts the connector, and the touch key
// printed beside it, on the right.
constexpr uint8_t ROTATION_USB_LEFT = 2;
// Half a turn: the connector and the touch key on the right. The panel
// library turns the touch coordinates with it, and finds the touch key from
// the chip's raw reading, so both keep working.
constexpr uint8_t ROTATION_USB_RIGHT = 0;

constexpr uint8_t BRIGHT_ACTIVE = 220;
constexpr uint8_t BRIGHT_IDLE = 40;
constexpr uint32_t IDLE_DIM_MS = 180000;
// Standby follows the scale: this long after the scale switches off, the
// panel goes dark and the CPU slows down. Bluetooth keeps listening and the
// web page stays up; the scale's power button brings everything back.
constexpr uint32_t SCALE_OFF_SLEEP_MS = 30000;
constexpr uint32_t CPU_ACTIVE_MHZ = 240;
constexpr uint32_t CPU_STANDBY_MHZ = 80;  // lowest clock the radios accept

// The smallest text on the home, recipe list, edit and settings screens is
// Montserrat 20, the size of the status line and the hint under the card.

// Home screen recipe card: full-width lines and two columns beneath them.
constexpr int CARD_BOX_W = 404;
constexpr int CARD_PAD = 10;
constexpr int CARD_W = CARD_BOX_W - 2 * CARD_PAD;
constexpr int COL_GAP = 8;
constexpr int COL_W = (CARD_W - COL_GAP) / 2;

// Left 2/3 of the screen carries the text, the right 1/3 the keys.
constexpr int LEFT_W = 330;
constexpr int KEY_X = -12;
constexpr int KEY_W = 180;
constexpr int WIFI_W = 536 - 28;  // the Wi-Fi lines under the settings keys

enum class Panel : uint8_t { HOME, BREW, RESULT, FAULT, SETTINGS, RECIPES, EDIT, COFFEE };

lv_obj_t *panels[8];
constexpr int EDIT_FIELDS = 5;
Panel shown = Panel::HOME;
bool settingsOpen = false;
bool recipesOpen = false;
bool editOpen = false;
bool coffeeOpen = false;
constexpr int EDIT_FIELD_GRIND = 2;  // index in the field list on the edit screen
int editField = EDIT_FIELD_GRIND;
bbw::Recipe editRecipe;         // working copy on the edit panel
// BOOT asks before it stops a running shot: the question stays up this long.
constexpr uint32_t STOP_CONFIRM_MS = 5000;
uint32_t stopConfirmUntilMs = 0;
uint32_t recipesOpenedAtMs = 0;
// The capacitive key on the glass. The touch chip reports it on every poll
// while a finger is on it; a press is the first report after a quiet gap.
volatile uint32_t touchKeySeenMs = 0;
volatile bool touchKeyPressed = false;
bool dimmed = false;
bool screenOff = false;
uint32_t ownActivityMs = 0;  // when wake() last told LVGL of activity (not a touch)
bool started = false;
uint32_t scaleLostAtMs = 0;
char wifiText[128] = "Wi-Fi starting";

// Home
lv_obj_t *homeStatus, *homeScaleBattery, *homeBotBattery, *homeScaleDot, *homeBotDot, *homeCard, *homeHint, *homeEditLabel;
lv_obj_t *homeName, *homeYield, *homeGrind, *homeAdvice, *homePre;
lv_obj_t *homeFoot;
// The hint under the card is also a key: it starts the shot while it says so.
lv_obj_t *homeStartKey;
bool homeCanStart = false;
// The coffee screen: its name, and everything else about it in a scrolling column.
lv_obj_t *coffeeTitle, *coffeeSub, *coffeeList;
uint32_t coffeeOpenedAtMs = 0;
// BAG FINISHED under the coffee card: the first tap asks, a second within FINISH_CONFIRM_MS finishes.
lv_obj_t *coffeeFinishKey, *coffeeFinishLabel;
uint32_t finishConfirmUntilMs = 0;  // 0: not asking
constexpr uint32_t FINISH_CONFIRM_MS = 5000;
constexpr int COFFEE_LIST_Y = 72, COFFEE_LIST_H = 240 - COFFEE_LIST_Y - 6;
// BAG FINISHED floats over the bottom corner of the coffee card; while it shows, the card keeps
// this much room under its last row so the rows scroll up clear of it.
constexpr int FINISH_KEY_W = 180, FINISH_KEY_H = 52, FINISH_KEY_BOTTOM = 8;
constexpr int FINISH_ROOM = COFFEE_LIST_Y + COFFEE_LIST_H - (240 - FINISH_KEY_BOTTOM - FINISH_KEY_H) + 6;
lv_obj_t *recipeBar;
lv_obj_t *editFieldBtn[EDIT_FIELDS], *editTitle, *editValue, *editHint;
lv_obj_t *editSetAdviceBtn, *editSetAdviceLabel;
lv_obj_t *recipeList;
// Brew
lv_obj_t *brewArc, *brewWeight, *brewUnit, *brewTime, *brewTarget, *brewFlow, *brewPhase;
lv_obj_t *brewStopLabel;
// Result
lv_obj_t *resTop, *resWeight, *resLine1, *resLine2, *resGrind, *resGrindSub;
lv_obj_t *resReview, *resReadyBtn, *resIgnoreBtn, *resCountBtn;
// When the grind should change: READY and GRINDER share the key column.
lv_obj_t *resReadyHalfBtn, *resGrinderBtn;
bool editPending = false;  // GRINDER tapped: open the edit screen once the controller is READY
uint32_t resShotId = 0;  // the shot the two ruling keys act on
// Fault
lv_obj_t *faultTitle, *faultText, *faultPressBtn;
// Settings
lv_obj_t *setLearnLabel, *setBotLabel, *setScaleLabel, *setWifiLabel;

void setText(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

void setTextF(lv_obj_t *label, const char *fmt, ...) {
  char buffer[96];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  setText(label, buffer);
}

void setColor(lv_obj_t *label, uint32_t color) {
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                    lv_align_t align, int x, int y, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_label_set_text(label, text);
  lv_obj_align(label, align, x, y);
  return label;
}

// A centred line of fixed width at (x, y).
lv_obj_t *makeLine(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int y,
                   int x = 8, int width = LEFT_W) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, width);
  lv_label_set_text(label, "");
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);
  return label;
}

lv_obj_t *makeButton(lv_obj_t *parent, const char *text, const lv_font_t *font,
                     uint32_t color, int w, int h, lv_align_t align, int x, int y,
                     lv_event_cb_t cb, lv_obj_t **labelOut = nullptr) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_size(button, w, h);
  lv_obj_align(button, align, x, y);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
  lv_obj_set_style_radius(button, 14, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *label = lv_label_create(button);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  if (labelOut) *labelOut = label;
  return button;
}

// A key that acts on a tap and repeats while held. CLICKED also fires when a
// long press is released, so these keys listen to the two events below only.
lv_obj_t *makeRepeatKey(lv_obj_t *parent, const char *text, int w, int h, lv_align_t align,
                        int x, int y, lv_event_cb_t cb) {
  lv_obj_t *key = makeButton(parent, text, &lv_font_montserrat_24, COLOR_PANEL, w, h, align, x, y,
                             [](lv_event_t *) {});
  lv_obj_add_event_cb(key, cb, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_add_event_cb(key, cb, LV_EVENT_LONG_PRESSED_REPEAT, nullptr);
  return key;
}

// A left-aligned line of fixed width (a column entry).
lv_obj_t *makeLeft(lv_obj_t *parent, uint32_t color, int x, int y, int width) {
  lv_obj_t *label = makeLine(parent, &lv_font_montserrat_20, color, y, x, width);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
  return label;
}

// Sets the text in the largest of the given fonts that fits the width, so
// nothing on the card is ever cut off with "...".
void fitText(lv_obj_t *label, const char *text, int width, int count, ...) {
  va_list fonts;
  va_start(fonts, count);
  const lv_font_t *chosen = nullptr;
  for (int i = 0; i < count; ++i) {
    chosen = va_arg(fonts, const lv_font_t *);
    if (lv_txt_get_width(text, strlen(text), chosen, 0, LV_TEXT_FLAG_NONE) <= width) break;
  }
  va_end(fonts);
  if (lv_obj_get_style_text_font(label, LV_PART_MAIN) != chosen) {
    lv_obj_set_style_text_font(label, chosen, 0);
  }
  setText(label, text);
}

void scrollIfLong(lv_obj_t *label, const char *text, int width);

// A full-width line on the result screen: 22 pt if it fits, 20 if not.
void fitLine(lv_obj_t *label, const char *text) {
  fitText(label, text, LEFT_W, 2, &lv_font_montserrat_22, &lv_font_montserrat_20);
}

// The same for the result screen's detail lines.
void fitDetail(lv_obj_t *label, const char *fmt, ...) {
  char buffer[96];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  fitText(label, buffer, LEFT_W, 1, &lv_font_montserrat_20);
  scrollIfLong(label, buffer, LEFT_W);
}

// Text that is still too wide in the smallest font (a long coffee name typed
// on the web page) scrolls round instead of being cut off with "...".
void scrollIfLong(lv_obj_t *label, const char *text, int width) {
  const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  const lv_label_long_mode_t mode =
      lv_txt_get_width(text, strlen(text), font, 0, LV_TEXT_FLAG_NONE) > width
          ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT;
  if (lv_label_get_long_mode(label) != mode) lv_label_set_long_mode(label, mode);
}

bool fitsColumn(const char *text, const lv_font_t *font) {
  return lv_txt_get_width(text, strlen(text), font, 0, LV_TEXT_FLAG_NONE) <= COL_W;
}

// The home card's column entries, all in one size: 22 pt when every one fits,
// 20 otherwise. Each has a shorter wording for when the full one does not fit
// at 20; a text still too wide scrolls.
struct CardEntry {
  lv_obj_t *label;
  const char *text, *shorter;
};
void fitCardEntries(const CardEntry *entries, int count) {
  bool large = true;
  for (int i = 0; i < count; ++i) large = large && fitsColumn(entries[i].text, &lv_font_montserrat_22);
  const lv_font_t *font = large ? &lv_font_montserrat_22 : &lv_font_montserrat_20;
  for (int i = 0; i < count; ++i) {
    const CardEntry &e = entries[i];
    const char *text = large || fitsColumn(e.text, font) ? e.text : e.shorter;
    fitText(e.label, text, COL_W, 1, font);
    scrollIfLong(e.label, text, COL_W);
  }
}

// A status lamp.
lv_obj_t *makeDot(lv_obj_t *parent, int x, int y) {
  lv_obj_t *dot = lv_obj_create(parent);
  lv_obj_set_size(dot, 12, 12);
  lv_obj_align(dot, LV_ALIGN_TOP_LEFT, x, y);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(COLOR_BAD), 0);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return dot;
}

void setDot(lv_obj_t *dot, uint32_t color) {
  lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
}

lv_obj_t *makePanel() {
  lv_obj_t *panel = lv_obj_create(lv_scr_act());
  lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_radius(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
  return panel;
}

void show(Panel panel) {
  if (panel == shown && !lv_obj_has_flag(panels[static_cast<int>(panel)], LV_OBJ_FLAG_HIDDEN)) {
    return;
  }
  for (lv_obj_t *p : panels) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(panels[static_cast<int>(panel)], LV_OBJ_FLAG_HIDDEN);
  shown = panel;
  wake();
}

bool repeating(lv_event_t *e) { return lv_event_get_code(e) == LV_EVENT_LONG_PRESSED_REPEAT; }

// ------------------------------------------------------------- callbacks --

void onStop(lv_event_t *) {
  stopConfirmUntilMs = 0;
  app::stop();
}
void onTare(lv_event_t *) { app::tare(); }
void onAck(lv_event_t *) { app::acknowledge(); }
void onManualPress(lv_event_t *) { app::manualPress(); }
// Recipe quick edit. Works on a copy; SAVE writes it back.
// From the result screen: the shot is done with, and the grind is next.
void onGrinder(lv_event_t *) {
  app::acknowledge();
  editRecipe = app::activeRecipe();
  editField = EDIT_FIELD_GRIND;
  editPending = true;
}
void onOpenEdit(lv_event_t *) {
  editRecipe = app::activeRecipe();
  // Opens on the grind: it is what gets changed from one shot to the next.
  editField = EDIT_FIELD_GRIND;
  editOpen = true;
}
void onEditField(lv_event_t *e) {
  editField = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target(e))));
}
void editStep(int direction, bool held) {
  bbw::Recipe &r = editRecipe;
  switch (editField) {
    case 0:
      if (r.useRatio) r.ratio += direction * 0.1f;
      else r.targetWeightG += direction * (held ? 0.1f : 0.5f);
      break;
    case 1: r.doseG += direction * (held ? 0.5f : 0.1f); break;
    case 2: {
      // Somewhere to start from on first use: the middle of the grinder's range, else 250 (microns).
      const bbw::StepRange range = bbw::stepRange(app::grinder().modelId);
      int um = r.grindUm ? r.grindUm : (range.known ? (range.lo + range.hi) / 2 : 250);
      um += direction * (held ? 10 : 1);
      r.grindUm = bbw::clampToRange(um, range);
      break;
    }
    case 3: r.targetTimeS += direction * (held ? 1.0f : 0.5f); break;
    default:
      // Minus at the shortest hold goes back to the machine's own pre-infusion.
      if (r.preinfusion != bbw::Preinfusion::HOLD) {
        if (direction > 0) {
          r.preinfusion = bbw::Preinfusion::HOLD;
          r.preinfusionHoldMs = 500;
        }
      } else {
        const int ms = static_cast<int>(r.preinfusionHoldMs) + direction * (held ? 500 : 100);
        if (ms < 300) r.preinfusion = bbw::Preinfusion::MACHINE;
        else r.preinfusionHoldMs = static_cast<uint16_t>(ms > bbw::MAX_PREINFUSION_HOLD_MS
                                                              ? bbw::MAX_PREINFUSION_HOLD_MS : ms);
      }
      break;
  }
  bbw::sanitizeRecipe(r);
}
void onEditMinus(lv_event_t *e) { editStep(-1, repeating(e)); }
void onEditPlus(lv_event_t *e) { editStep(+1, repeating(e)); }
// The setting the grind advice names, when it names one (finer or coarser, with a number).
bool adviceSetting(uint16_t &um) {
  const bbw::GrindAdvice g = app::grindAdvice();
  if (g.action != GrindAction::FINER && g.action != GrindAction::COARSER) return false;
  if (!bbw::adviceHasNumber(g) || !g.suggestedUm) return false;
  um = g.suggestedUm;
  return true;
}
// SET under the advice: the advised setting in one tap. Kept only on SAVE, like - and +.
void onEditSetAdvice(lv_event_t *) {
  uint16_t um = 0;
  if (editField != EDIT_FIELD_GRIND || !adviceSetting(um)) return;
  editRecipe.grindUm = um;
  bbw::sanitizeRecipe(editRecipe);
}
void onEditSave(lv_event_t *) {
  app::applyPresetEdit(app::activePreset(), editRecipe);
  editOpen = false;
}
void onEditCancel(lv_event_t *) { editOpen = false; }
void onOpenRecipes(lv_event_t *) {
  recipesOpenedAtMs = millis();
  recipesOpen = true;
}
void onCloseRecipes(lv_event_t *) { recipesOpen = false; }
// The bar and the list move each other. The bar's top is scroll position 0.
void onRecipeBarMoved(lv_event_t *) {
  lv_obj_scroll_to_y(recipeList, lv_slider_get_max_value(recipeBar) - lv_slider_get_value(recipeBar),
                     LV_ANIM_OFF);
}
void onRecipeListScrolled(lv_event_t *) {
  if (lv_obj_has_state(recipeBar, LV_STATE_PRESSED)) return;  // the finger is on the bar
  int y = lv_obj_get_scroll_y(recipeList);
  const int max = lv_slider_get_max_value(recipeBar);
  y = y < 0 ? 0 : (y > max ? max : y);
  lv_slider_set_value(recipeBar, max - y, LV_ANIM_OFF);
}
void onPickRecipe(lv_event_t *e) {
  app::selectRecipe(static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
  recipesOpen = false;
}
void onTouchKey(void *) {
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - touchKeySeenMs) > 400) touchKeyPressed = true;
  touchKeySeenMs = now ? now : 1;
}
// The user's ruling on an unusual shot.
void onVoteIgnore(lv_event_t *) {
  app::setShotGrindVote(resShotId, static_cast<uint8_t>(bbw::GrindVote::IGNORE));
}
void onVoteCount(lv_event_t *) {
  app::setShotGrindVote(resShotId, static_cast<uint8_t>(bbw::GrindVote::COUNT));
}
void onOpenCoffee(lv_event_t *) {
  coffeeOpenedAtMs = millis() ? millis() : 1;
  coffeeOpen = true;
  finishConfirmUntilMs = 0;
}
bool finishConfirming() {
  return finishConfirmUntilMs != 0 && static_cast<int32_t>(millis() - finishConfirmUntilMs) < 0;
}
// The bag of the coffee in use is finished: its entries leave the list until a new bag is started on
// the web page, and the home screen shows the entry picked next (app::finishActiveCoffee).
void onBagFinished(lv_event_t *) {
  if (!finishConfirming()) {
    finishConfirmUntilMs = millis() + FINISH_CONFIRM_MS;
    if (finishConfirmUntilMs == 0) finishConfirmUntilMs = 1;
    return;
  }
  finishConfirmUntilMs = 0;
  if (app::finishActiveCoffee()) coffeeOpen = false;
}
void onCloseCoffee(lv_event_t *) { coffeeOpen = false; }
// The hint under the card, pressed while it says TOUCH KEY TO BREW: the same
// start as the touch key. A tap on a dimmed screen only brightens it.
void onHomeStart(lv_event_t *) {
  if (dimmed || !homeCanStart) return;
  const State state = app::status().state;
  if (state == State::READY || (state == State::CONNECTING_SCALE && app::scaleNearby())) app::start();
}
void onOpenSettings(lv_event_t *) { settingsOpen = true; }
void onCloseSettings(lv_event_t *) { settingsOpen = false; }
void onPairBot(lv_event_t *) {
  setText(setBotLabel, app::pairStrongestBot() ? "Bot: pairing..." : "Bot: none found nearby");
}
void onForgetScale(lv_event_t *) { app::pairScale("", 0); }
// The screen the right way up for how the controller is mounted.
bool flippedShown = false;

// Turned half a turn, the touch key is at the other end of the panel, so a screen in MIRRORED is
// laid out mirror-wise: the keys move to the far end and the rest to the key's end. Each object on
// the screen keeps its own content; its offset from one side becomes the same offset from the
// other. Objects marked keepPlace (the home status line, which reads left to right) stay put.
constexpr Panel MIRRORED[] = {Panel::HOME};
struct Placed {
  lv_obj_t *obj;
  lv_align_t align;
  lv_coord_t x, y;
};
Placed placed[96];
uint8_t placedCount = 0;

void keepPlace(lv_obj_t *obj) { lv_obj_add_flag(obj, LV_OBJ_FLAG_USER_2); }

lv_align_t mirrorAlign(lv_align_t align) {
  switch (align) {
    case LV_ALIGN_DEFAULT:
    case LV_ALIGN_TOP_LEFT: return LV_ALIGN_TOP_RIGHT;
    case LV_ALIGN_TOP_RIGHT: return LV_ALIGN_TOP_LEFT;
    case LV_ALIGN_LEFT_MID: return LV_ALIGN_RIGHT_MID;
    case LV_ALIGN_RIGHT_MID: return LV_ALIGN_LEFT_MID;
    case LV_ALIGN_BOTTOM_LEFT: return LV_ALIGN_BOTTOM_RIGHT;
    case LV_ALIGN_BOTTOM_RIGHT: return LV_ALIGN_BOTTOM_LEFT;
    default: return align;  // the middle ones: only x changes sign
  }
}

// Where everything on the mirrored screens was put, as built (the unturned layout).
void recordLayout() {
  for (Panel panel : MIRRORED) {
    lv_obj_t *p = panels[static_cast<int>(panel)];
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(p); ++i) {
      lv_obj_t *obj = lv_obj_get_child(p, i);
      if (lv_obj_has_flag(obj, LV_OBJ_FLAG_USER_2)) continue;
      if (placedCount == sizeof(placed) / sizeof(placed[0])) {
        LOGW("ui: too many objects to mirror");
        return;
      }
      placed[placedCount++] = {obj, static_cast<lv_align_t>(lv_obj_get_style_align(obj, LV_PART_MAIN)),
                               lv_obj_get_style_x(obj, LV_PART_MAIN), lv_obj_get_style_y(obj, LV_PART_MAIN)};
    }
  }
}

void placeLayout(bool flipped) {
  for (uint8_t i = 0; i < placedCount; ++i) {
    const Placed &q = placed[i];
    lv_obj_align(q.obj, flipped ? mirrorAlign(q.align) : q.align, flipped ? -q.x : q.x, q.y);
  }
}

// The hint that names the touch key points at it, at whichever end it is.
const char *touchHint() {
  return flippedShown ? "TOUCH KEY TO BREW  " LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT "  TOUCH KEY TO BREW";
}

void applyRotation(bool flipped) {
  flippedShown = flipped;
  amoled.setRotation(flipped ? ROTATION_USB_RIGHT : ROTATION_USB_LEFT);
  placeLayout(flipped);
  lv_obj_invalidate(lv_scr_act());  // redraw everything the new way up
}

// Every colour an object was given takes its role's colour in the new scheme: the object and all
// below it, each of its own styles (any part or state). Colours set later by the refresh code read
// the new scheme by themselves.
void recolour(lv_obj_t *obj, const Palette &from, const Palette &to) {
  const uint32_t *a = &from.bg, *b = &to.bg;
  constexpr int ROLES = sizeof(Palette) / sizeof(uint32_t);
  static const lv_style_prop_t PROPS[] = {LV_STYLE_BG_COLOR, LV_STYLE_TEXT_COLOR, LV_STYLE_BORDER_COLOR,
                                          LV_STYLE_ARC_COLOR, LV_STYLE_LINE_COLOR, LV_STYLE_OUTLINE_COLOR,
                                          LV_STYLE_SHADOW_COLOR};
  for (uint32_t i = 0; i < obj->style_cnt; ++i) {
    if (!obj->styles[i].is_local) continue;
    const lv_style_selector_t selector = obj->styles[i].selector;
    for (lv_style_prop_t prop : PROPS) {
      lv_style_value_t value;
      if (lv_style_get_prop(obj->styles[i].style, prop, &value) != LV_STYLE_RES_FOUND) continue;
      for (int r = 0; r < ROLES; ++r) {
        if (value.color.full != lv_color_hex(a[r]).full) continue;
        value.color = lv_color_hex(b[r]);
        lv_obj_set_local_style_prop(obj, prop, value, selector);
        break;
      }
    }
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); ++i) recolour(lv_obj_get_child(obj, i), from, to);
}

void applyTheme(uint8_t theme) {
  if (theme >= bbw::UI_THEME_COUNT) theme = 0;
  const Palette from = PALETTES[themeShown];
  setPalette(theme);
  recolour(lv_scr_act(), from, PALETTES[theme]);
  lv_obj_invalidate(lv_scr_act());
}

void onRotate(lv_event_t *) {
  bbw::Settings s = app::settings();
  s.displayFlipped = !s.displayFlipped;
  app::applySettings(s);
  applyRotation(s.displayFlipped);
}

void onToggleLearning(lv_event_t *) {
  bbw::Settings s = app::settings();
  s.learningEnabled = !s.learningEnabled;
  app::applySettings(s);
}

// ---------------------------------------------------------------- panels --

void buildHome() {
  lv_obj_t *p = panels[static_cast<int>(Panel::HOME)] = makePanel();
  // Status line across the whole width: state and live weight, then the two
  // links, each with its light and battery.
  homeStatus = makeLabel(p, &lv_font_montserrat_20, COLOR_TEXT, LV_ALIGN_TOP_LEFT, 16, 5, "");
  makeLabel(p, &lv_font_montserrat_20, COLOR_DIM, LV_ALIGN_TOP_LEFT, 250, 5, "SCALE");
  homeScaleDot = makeDot(p, 322, 10);
  homeScaleBattery = makeLabel(p, &lv_font_montserrat_20, COLOR_DIM, LV_ALIGN_TOP_LEFT, 340, 5, "");
  makeLabel(p, &lv_font_montserrat_20, COLOR_DIM, LV_ALIGN_TOP_LEFT, 404, 5, "BOT");
  homeBotDot = makeDot(p, 452, 10);
  homeBotBattery = makeLabel(p, &lv_font_montserrat_20, COLOR_DIM, LV_ALIGN_TOP_LEFT, 470, 5, "");
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(p); ++i) keepPlace(lv_obj_get_child(p, i));

  // The selected recipe, in full: what goes in, what comes out, the grind,
  // the pre-infusion and the coffee. Tapping it opens the recipe list.
  homeCard = lv_btn_create(p);
  lv_obj_set_size(homeCard, CARD_BOX_W, 162);
  lv_obj_align(homeCard, LV_ALIGN_TOP_LEFT, 12, 34);
  lv_obj_set_style_bg_color(homeCard, lv_color_hex(COLOR_PANEL), 0);
  lv_obj_set_style_radius(homeCard, 14, 0);
  lv_obj_set_style_shadow_width(homeCard, 0, 0);
  lv_obj_set_style_pad_all(homeCard, 0, 0);
  lv_obj_add_event_cb(homeCard, onOpenRecipes, LV_EVENT_CLICKED, nullptr);
  // The coffee across the top, then the recipe with what goes in and comes
  // out, then two columns: grind and advice beside pre-infusion and brew time,
  // The roast and where the coffee comes from are on the COFFEE key. The four
  // lines are spaced evenly down the card, with equal margins top and bottom.
  constexpr int NAME_Y = 12, YIELD_Y = 50, ROW0 = 88, ROW = 37;
  homeName = makeLine(homeCard, &lv_font_montserrat_26, COLOR_ACCENT, NAME_Y, CARD_PAD, CARD_W - 24);
  lv_obj_align(homeName, LV_ALIGN_TOP_LEFT, CARD_PAD + 12, NAME_Y);
  homeYield = makeLine(homeCard, &lv_font_montserrat_24, COLOR_TEXT, YIELD_Y, CARD_PAD, CARD_W);
  constexpr int RIGHT_X = CARD_PAD + COL_W + COL_GAP;
  homeGrind = makeLeft(homeCard, COLOR_TEXT, CARD_PAD, ROW0, COL_W);
  homeAdvice = makeLeft(homeCard, COLOR_ACCENT, CARD_PAD, ROW0 + ROW, COL_W);
  homePre = makeLeft(homeCard, COLOR_TEXT, RIGHT_X, ROW0, COL_W);
  homeFoot = makeLeft(homeCard, COLOR_TEXT, RIGHT_X, ROW0 + ROW, COL_W);
  makeLabel(homeCard, &lv_font_montserrat_20, COLOR_DIM, LV_ALIGN_TOP_RIGHT, -8, NAME_Y + 4,
            LV_SYMBOL_DOWN);

  // The touch key on the glass, at this end of the panel, is the start
  // button. The hint under the card names it, and while it does it is a key
  // that starts the shot too; otherwise it is only text.
  homeStartKey = makeButton(p, "", &lv_font_montserrat_20, COLOR_PANEL, CARD_BOX_W, 36,
                            LV_ALIGN_TOP_LEFT, 12, 200, onHomeStart, &homeHint);
  lv_obj_set_style_text_color(homeHint, lv_color_hex(COLOR_OK), 0);

  // The keys start under the status line, beside the card.
  makeButton(p, "EDIT\nRECIPE", &lv_font_montserrat_20, COLOR_PANEL, 100, 62,
             LV_ALIGN_TOP_RIGHT, KEY_X, 34, onOpenEdit, &homeEditLabel);
  lv_obj_set_style_text_align(homeEditLabel, LV_TEXT_ALIGN_CENTER, 0);
  makeButton(p, "COFFEE", &lv_font_montserrat_20, COLOR_PANEL, 100, 62, LV_ALIGN_TOP_RIGHT, KEY_X,
             34 + 62 + 7, onOpenCoffee);
  makeButton(p, LV_SYMBOL_SETTINGS, &lv_font_montserrat_28, COLOR_PANEL, 100, 62,
             LV_ALIGN_BOTTOM_RIGHT, KEY_X, -8, onOpenSettings);
}

// The coffee being brewed, in full: everything the shelf knows about it.
void buildCoffee() {
  lv_obj_t *p = panels[static_cast<int>(Panel::COFFEE)] = makePanel();
  // The name, and under it the roast: what changes the grind most.
  constexpr int HEAD_W = 536 - 16 - 12 - 104 - 12;
  coffeeTitle = makeLine(p, &lv_font_montserrat_26, COLOR_ACCENT, 6, 16, HEAD_W);
  lv_obj_set_style_text_align(coffeeTitle, LV_TEXT_ALIGN_LEFT, 0);
  coffeeSub = makeLine(p, &lv_font_montserrat_20, COLOR_TEXT, 38, 16, HEAD_W);
  lv_obj_set_style_text_align(coffeeSub, LV_TEXT_ALIGN_LEFT, 0);
  makeButton(p, LV_SYMBOL_LEFT " BACK", &lv_font_montserrat_20, COLOR_PANEL, 104, 52,
             LV_ALIGN_TOP_RIGHT, -12, 8, onCloseCoffee);
  // The rest on a card, a name and its value to a row; drag it when it is longer
  // than the screen.
  coffeeList = lv_obj_create(p);
  lv_obj_set_size(coffeeList, 536 - 24, COFFEE_LIST_H);
  lv_obj_align(coffeeList, LV_ALIGN_TOP_LEFT, 12, COFFEE_LIST_Y);
  lv_obj_set_style_bg_color(coffeeList, lv_color_hex(COLOR_PANEL), 0);
  lv_obj_set_style_radius(coffeeList, 14, 0);
  lv_obj_set_style_border_width(coffeeList, 0, 0);
  lv_obj_set_style_pad_hor(coffeeList, 14, 0);
  lv_obj_set_style_pad_ver(coffeeList, 8, 0);
  lv_obj_set_style_pad_row(coffeeList, 4, 0);
  lv_obj_set_flex_flow(coffeeList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(coffeeList, LV_DIR_VER);
  lv_obj_set_style_bg_color(coffeeList, lv_color_hex(COLOR_ACCENT), LV_PART_SCROLLBAR);
  // A narrow key over the card's bottom right corner, when the entry has a coffee (refreshCoffee),
  // clear of the scroll bar. The card stays full height and scrolls under it. Outlined so it stands
  // off the card. It stays on the right whichever way up the screen is.
  coffeeFinishKey = makeButton(p, "BAG FINISHED", &lv_font_montserrat_20, COLOR_TRACK, FINISH_KEY_W,
                               FINISH_KEY_H, LV_ALIGN_BOTTOM_RIGHT, -24, -FINISH_KEY_BOTTOM,
                               onBagFinished, &coffeeFinishLabel);
  keepPlace(coffeeFinishKey);
  lv_obj_set_style_border_width(coffeeFinishKey, 2, 0);
  lv_obj_set_style_border_color(coffeeFinishKey, lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_text_align(coffeeFinishLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(coffeeFinishKey, LV_OBJ_FLAG_HIDDEN);
}

// One row of the coffee card: the name dim on the left, the value beside it,
// wrapping onto more lines when it is long (the notes).
void addCoffeeRow(const char *name, const char *value) {
  if (!value || !value[0]) return;
  constexpr int NAME_W = 112;
  lv_obj_t *row = lv_obj_create(coffeeList);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t *label = makeLabel(row, &lv_font_montserrat_20, COLOR_DIM, LV_ALIGN_TOP_LEFT, 0, 0, name);
  lv_obj_set_width(label, NAME_W);
  lv_obj_t *text = makeLabel(row, &lv_font_montserrat_20, COLOR_TEXT, LV_ALIGN_TOP_LEFT, NAME_W, 0, value);
  lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(text, 536 - 24 - 28 - NAME_W - 8);
}

// Quick edits to the selected recipe. Everything else is on the web page.
void buildEdit() {
  lv_obj_t *p = panels[static_cast<int>(Panel::EDIT)] = makePanel();
  static const char *const names[EDIT_FIELDS] = {"TARGET", "DOSE", "GRIND", "BREW TIME", "PRE-INF"};
  for (int i = 0; i < EDIT_FIELDS; ++i) {
    editFieldBtn[i] = makeButton(p, names[i], &lv_font_montserrat_20, COLOR_PANEL, 132, 40,
                                 LV_ALIGN_TOP_LEFT, 8, 8 + 45 * i, onEditField);
    lv_obj_set_user_data(editFieldBtn[i], reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }
  editTitle = makeLine(p, &lv_font_montserrat_20, COLOR_DIM, 18, 146, 190);
  editValue = makeLine(p, &lv_font_montserrat_30, COLOR_TEXT, 54, 146, 190);
  // Up to four lines of 20 pt down to the bottom edge; three when SET is shown beneath.
  editHint = makeLine(p, &lv_font_montserrat_20, COLOR_DIM, 104, 146, 190);
  lv_label_set_long_mode(editHint, LV_LABEL_LONG_WRAP);
  // Under the grind advice, between PRE-INF and CANCEL: sets the advised number.
  editSetAdviceBtn = makeButton(p, "", &lv_font_montserrat_22, COLOR_ACCENT, 190, 52,
                                LV_ALIGN_BOTTOM_LEFT, 146, -8, onEditSetAdvice,
                                &editSetAdviceLabel);
  lv_obj_add_flag(editSetAdviceBtn, LV_OBJ_FLAG_HIDDEN);
  makeRepeatKey(p, LV_SYMBOL_MINUS, 86, 86, LV_ALIGN_TOP_RIGHT, KEY_X - 94, 8, onEditMinus);
  makeRepeatKey(p, LV_SYMBOL_PLUS, 86, 86, LV_ALIGN_TOP_RIGHT, KEY_X, 8, onEditPlus);
  makeButton(p, "SAVE", &lv_font_montserrat_24, COLOR_OK, KEY_W, 70, LV_ALIGN_TOP_RIGHT, KEY_X,
             102, onEditSave);
  makeButton(p, "CANCEL", &lv_font_montserrat_20, COLOR_PANEL, KEY_W, 52, LV_ALIGN_BOTTOM_RIGHT,
             KEY_X, -8, onEditCancel);
}

void buildRecipes() {
  lv_obj_t *p = panels[static_cast<int>(Panel::RECIPES)] = makePanel();
  // A scrolling list: drag the list or the bar beside it, tap to select.
  recipeList = lv_list_create(p);
  lv_obj_set_size(recipeList, 400, 224);
  lv_obj_align(recipeList, LV_ALIGN_TOP_LEFT, 8, 8);
  lv_obj_set_style_bg_color(recipeList, lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_border_width(recipeList, 0, 0);
  lv_obj_set_style_pad_all(recipeList, 0, 0);
  lv_obj_set_style_pad_row(recipeList, 6, 0);
  // LVGL's own scrollbar is a thin indicator that cannot be dragged. It is
  // replaced by a finger-sized bar that moves the list and follows it.
  lv_obj_set_scrollbar_mode(recipeList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(recipeList, onRecipeListScrolled, LV_EVENT_SCROLL, nullptr);

  recipeBar = lv_slider_create(p);
  lv_obj_set_size(recipeBar, 28, 154);
  lv_obj_align(recipeBar, LV_ALIGN_TOP_RIGHT, -12 - (104 - 28) / 2, 68);
  lv_obj_set_style_bg_color(recipeBar, lv_color_hex(COLOR_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(recipeBar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(recipeBar, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(recipeBar, lv_color_hex(COLOR_ACCENT), LV_PART_KNOB);
  lv_obj_set_style_radius(recipeBar, 8, LV_PART_KNOB);
  lv_obj_set_style_pad_hor(recipeBar, 24, LV_PART_KNOB);  // handle 76 wide ...
  lv_obj_set_style_pad_ver(recipeBar, -6, LV_PART_KNOB);  // ... and 16 tall
  lv_obj_set_ext_click_area(recipeBar, 36);               // the whole column grabs it
  lv_obj_add_event_cb(recipeBar, onRecipeBarMoved, LV_EVENT_VALUE_CHANGED, nullptr);

  makeButton(p, LV_SYMBOL_LEFT " BACK", &lv_font_montserrat_20, COLOR_PANEL, 104, 40,
             LV_ALIGN_TOP_RIGHT, -12, 6, onCloseRecipes);
}

void buildBrew() {
  lv_obj_t *p = panels[static_cast<int>(Panel::BREW)] = makePanel();

  // Progress ring: weight in the cup as a share of the target. Red while the
  // machine is running but nothing has reached the cup (pre-infusion), green
  // once coffee is flowing, closing the circle as the target is reached.
  constexpr int RING = 212;
  brewArc = lv_arc_create(p);
  lv_obj_set_size(brewArc, RING, RING);
  lv_obj_align(brewArc, LV_ALIGN_LEFT_MID, 10, 0);
  lv_arc_set_rotation(brewArc, 270);
  lv_arc_set_bg_angles(brewArc, 0, 360);
  lv_arc_set_range(brewArc, 0, 1000);
  lv_arc_set_value(brewArc, 0);
  lv_obj_remove_style(brewArc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(brewArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(brewArc, 16, LV_PART_MAIN);
  lv_obj_set_style_arc_width(brewArc, 16, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(brewArc, lv_color_hex(COLOR_TRACK), LV_PART_MAIN);
  lv_obj_set_style_arc_color(brewArc, lv_color_hex(COLOR_BAD), LV_PART_INDICATOR);

  // Inside the ring: the target until coffee arrives, then the live weight.
  brewWeight = makeLine(p, &lv_font_montserrat_48, COLOR_TEXT, 84, 10 + 20, RING - 40);
  brewUnit = makeLine(p, &lv_font_montserrat_18, COLOR_DIM, 142, 10 + 20, RING - 40);

  // Beside it: time, target, flow, and what is happening.
  constexpr int INFO_X = 232;
  constexpr int INFO_W = 150;
  brewPhase = makeLine(p, &lv_font_montserrat_16, COLOR_ACCENT, 16, INFO_X, INFO_W);
  brewTime = makeLine(p, &lv_font_montserrat_36, COLOR_TEXT, 48, INFO_X, INFO_W);
  brewTarget = makeLine(p, &lv_font_montserrat_22, COLOR_DIM, 100, INFO_X, INFO_W);
  brewFlow = makeLine(p, &lv_font_montserrat_22, COLOR_TEXT, 146, INFO_X, INFO_W);
  lv_label_set_long_mode(brewPhase, LV_LABEL_LONG_WRAP);

  makeButton(p, "STOP", &lv_font_montserrat_28, COLOR_BAD, 132, 216, LV_ALIGN_RIGHT_MID, KEY_X, 0,
             onStop, &brewStopLabel);
  lv_obj_set_style_text_align(brewStopLabel, LV_TEXT_ALIGN_CENTER, 0);
}

void buildResult() {
  lv_obj_t *p = panels[static_cast<int>(Panel::RESULT)] = makePanel();
  resTop = makeLine(p, &lv_font_montserrat_26, COLOR_OK, 6);          // "OVER +0.2 g"
  resWeight = makeLine(p, &lv_font_montserrat_48, COLOR_TEXT, 38);    // final weight
  // Nothing on this screen is smaller than 20 pt, as on the home screen.
  resLine1 = makeLine(p, &lv_font_montserrat_20, COLOR_TEXT, 96);     // times
  resLine2 = makeLine(p, &lv_font_montserrat_20, COLOR_DIM, 120);     // flow, drip, auto adjust
  resGrind = makeLine(p, &lv_font_montserrat_22, COLOR_ACCENT, 148);  // "BREW FAST  GRIND FINER"
  resGrindSub = makeLine(p, &lv_font_montserrat_20, COLOR_DIM, 176);  // amount or where to look
  lv_label_set_long_mode(resGrindSub, LV_LABEL_LONG_WRAP);  // two lines rather than "..."
  resReadyBtn = makeButton(p, "READY", &lv_font_montserrat_28, COLOR_PANEL, KEY_W, 216,
                           LV_ALIGN_RIGHT_MID, KEY_X, 0, onAck);

  // An unusual shot (a one-off, or one that ran out of time) is put to the
  // user instead of being quietly set aside: three lines of advice in place
  // of the grind verdict, and two keys in place of READY.
  resReview = makeLine(p, &lv_font_montserrat_20, COLOR_ACCENT, 148);
  lv_label_set_long_mode(resReview, LV_LABEL_LONG_WRAP);
  lv_obj_t *label = nullptr;
  resReadyHalfBtn = makeButton(p, "READY", &lv_font_montserrat_24, COLOR_PANEL, KEY_W, 104,
                               LV_ALIGN_TOP_RIGHT, KEY_X, 12, onAck);
  resGrinderBtn = makeButton(p, "GRINDER", &lv_font_montserrat_22, COLOR_ACCENT, KEY_W, 104,
                             LV_ALIGN_BOTTOM_RIGHT, KEY_X, -12, onGrinder);
  resIgnoreBtn = makeButton(p, "IGNORE\nSHOT", &lv_font_montserrat_20, COLOR_PANEL, KEY_W, 104,
                            LV_ALIGN_TOP_RIGHT, KEY_X, 12, onVoteIgnore, &label);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  resCountBtn = makeButton(p, "COUNT FOR\nGRIND", &lv_font_montserrat_20, COLOR_ACCENT, KEY_W,
                           104, LV_ALIGN_BOTTOM_RIGHT, KEY_X, -12, onVoteCount, &label);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

void buildFault() {
  lv_obj_t *p = panels[static_cast<int>(Panel::FAULT)] = makePanel();
  faultTitle = makeLabel(p, &lv_font_montserrat_24, COLOR_BAD, LV_ALIGN_TOP_LEFT, 16, 12, "");
  lv_label_set_long_mode(faultTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_width(faultTitle, LEFT_W - 16);
  faultText = makeLabel(p, &lv_font_montserrat_16, COLOR_TEXT, LV_ALIGN_TOP_LEFT, 16, 52, "");
  lv_label_set_long_mode(faultText, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(faultText, LEFT_W - 16);
  faultPressBtn = makeButton(p, "PRESS BUTTON", &lv_font_montserrat_16, COLOR_ACCENT, 170, 48,
                             LV_ALIGN_BOTTOM_LEFT, 16, -12, onManualPress);
  makeButton(p, "OK", &lv_font_montserrat_32, COLOR_PANEL, KEY_W, 216, LV_ALIGN_RIGHT_MID, KEY_X,
             0, onAck);
}

void buildSettings() {
  lv_obj_t *p = panels[static_cast<int>(Panel::SETTINGS)] = makePanel();
  makeLabel(p, &lv_font_montserrat_22, COLOR_ACCENT, LV_ALIGN_TOP_LEFT, 16, 12, "SETTINGS");
  makeLabel(p, &lv_font_montserrat_20, COLOR_DIM, LV_ALIGN_TOP_LEFT, 138, 14, BBW_VERSION);  // the firmware running
  // Beside the title, then three rows of keys: the first has the toggles,
  // the bot and the scale each have a row with their state beside the key.
  makeButton(p, "ROTATE SCREEN", &lv_font_montserrat_20, COLOR_PANEL, 190, 40, LV_ALIGN_TOP_RIGHT,
             -12 - 104 - 8, 6, onRotate);
  constexpr int ROW_Y[3] = {52, 100, 148}, ROW_H = 44, LEFT_KEY_W = 220;
  constexpr int INFO_X = 12 + LEFT_KEY_W + 8;
  makeButton(p, "", &lv_font_montserrat_20, COLOR_PANEL, LEFT_KEY_W, ROW_H, LV_ALIGN_TOP_LEFT, 12,
             ROW_Y[0], onToggleLearning, &setLearnLabel);
  makeButton(p, "TARE SCALE", &lv_font_montserrat_20, COLOR_PANEL, 536 - 12 - INFO_X, ROW_H,
             LV_ALIGN_TOP_LEFT, INFO_X, ROW_Y[0], onTare);
  makeButton(p, "PAIR NEAREST BOT", &lv_font_montserrat_20, COLOR_PANEL, LEFT_KEY_W, ROW_H,
             LV_ALIGN_TOP_LEFT, 12, ROW_Y[1], onPairBot);
  makeButton(p, "FORGET SCALE", &lv_font_montserrat_20, COLOR_PANEL, LEFT_KEY_W, ROW_H,
             LV_ALIGN_TOP_LEFT, 12, ROW_Y[2], onForgetScale);
  // One or two lines, centred on their key's row whatever their height.
  setBotLabel = makeLabel(p, &lv_font_montserrat_20, COLOR_TEXT, LV_ALIGN_LEFT_MID, INFO_X,
                          ROW_Y[1] + ROW_H / 2 - 120, "");
  setScaleLabel = makeLabel(p, &lv_font_montserrat_20, COLOR_TEXT, LV_ALIGN_LEFT_MID, INFO_X,
                            ROW_Y[2] + ROW_H / 2 - 120, "");
  lv_label_set_long_mode(setBotLabel, LV_LABEL_LONG_WRAP);
  lv_label_set_long_mode(setScaleLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(setBotLabel, 536 - 12 - INFO_X);
  lv_obj_set_width(setScaleLabel, 536 - 12 - INFO_X);
  // Two lines under the keys.
  setWifiLabel = makeLabel(p, &lv_font_montserrat_20, COLOR_DIM, LV_ALIGN_BOTTOM_LEFT, 16, -2, "");
  lv_label_set_long_mode(setWifiLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(setWifiLabel, WIFI_W);
  makeButton(p, LV_SYMBOL_LEFT " BACK", &lv_font_montserrat_20, COLOR_PANEL, 104, 40,
             LV_ALIGN_TOP_RIGHT, -12, 6, onCloseSettings);
}

// Short enough to fit beside the OK key; the exact state name is still what
// the web page, the log and the CSV use.
const char *faultTitle_(State state) {
  switch (state) {
    case State::SCALE_DISCONNECTED: return "SCALE DISCONNECTED";
    case State::SWITCHBOT_DISCONNECTED: return "BOT NOT CONNECTED";
    case State::SCALE_DATA_STALE: return "SCALE WENT SILENT";
    case State::START_COMMAND_FAILED: return "START NOT CONFIRMED";
    case State::STOP_COMMAND_FAILED: return "STOP NOT CONFIRMED";
    case State::TARE_FAILED: return "TARE FAILED";
    case State::SHOT_TIMEOUT: return "SHOT TIMED OUT";
    case State::INVALID_WEIGHT: return "WEIGHT NOT VALID";
    case State::BLE_ERROR: return "BLUETOOTH ERROR";
    default: return bbw::stateName(state);
  }
}

const char *faultMessage(State state, bool manualStop, bool stopStartedPump) {
  if (manualStop) return "STOP THE MACHINE BY HAND.\nThe stop press could not be delivered or confirmed.";
  if (state == State::START_COMMAND_FAILED && stopStartedPump) {
    return "The start press never reached the button, so STOP started the pump. It was stopped "
           "again when coffee appeared. Check the Bot's arm.";
  }
  switch (state) {
    case State::SCALE_DISCONNECTED: return "The scale is not connected. Switch it on.";
    case State::SWITCHBOT_DISCONNECTED: return "The brew bot did not answer. Check it is paired, in range and has battery.";
    case State::SCALE_DATA_STALE: return "The scale stopped sending weight. The shot was stopped.";
    case State::START_COMMAND_FAILED: return "The start press was not confirmed by coffee flow. Check the machine.";
    case State::STOP_COMMAND_FAILED: return "The stop press was not confirmed. Check the machine.";
    case State::TARE_FAILED: return "The scale did not settle at zero. Keep the cup still and start again.";
    case State::SHOT_TIMEOUT: return "Maximum shot time reached. The shot was stopped.";
    case State::INVALID_WEIGHT: return "Implausible weight (cup removed?). The shot was stopped.";
    case State::BLE_ERROR: return "Bluetooth failed to start. Restart the controller.";
    default: return "";
  }
}

void refreshSettings() {
  app::discover();  // keep the pairing list fresh while this panel is open
  const bbw::Settings s = app::settings();
  const app::Devices d = app::devices();
  setText(setLearnLabel, s.learningEnabled ? "OFFSET: LEARNING" : "OFFSET: FIXED");
  char battery[24] = "";
  if (d.bot.valid) snprintf(battery, sizeof(battery), "battery %u%%", d.bot.batteryPercent);
  if (d.pairing.botAddress[0] == '\0') {
    setText(setBotLabel, "Bot: not paired\nhold it close, tap PAIR");
  } else if (d.botConnected && d.bot.valid && d.bot.switchMode) {
    setTextF(setBotLabel, "Bot: connected, %u%%\nSWITCH MODE: use Press", d.bot.batteryPercent);
  } else if (d.botConnected) {
    setTextF(setBotLabel, "Bot: connected\n%s", battery);
  } else {
    setTextF(setBotLabel, "Bot: asleep until needed\n%s", battery);
  }
  char scaleBattery[24] = "";
  if (d.scaleConnected && d.scale.batteryPercent >= 0) {
    snprintf(scaleBattery, sizeof(scaleBattery), "battery %d%%", d.scale.batteryPercent);
  }
  setTextF(setScaleLabel, "Scale: %s\n%s",
           d.scaleConnected ? d.scaleName
                            : (d.pairing.scaleAddress[0] ? "searching" : "first scale found"),
           d.scaleConnected ? (scaleBattery[0] ? scaleBattery : d.scaleProtocol)
                            : d.pairing.scaleAddress);
  // The web server's two lines ("Wi-Fi <name>  password <pw>\n<address>").
  // When the first is too wide at this size, the line breaks at its gap
  // instead and the second part runs on with the address, so the name, the
  // password and the address each stay whole on two lines.
  char wifi[sizeof(wifiText) + 8];
  const char *newline = strchr(wifiText, '\n');
  const char *gap = strstr(wifiText, "  ");
  if (newline && gap && gap < newline &&
      lv_txt_get_width(wifiText, newline - wifiText, &lv_font_montserrat_20, 0,
                       LV_TEXT_FLAG_NONE) > WIFI_W) {
    snprintf(wifi, sizeof(wifi), "%.*s\n%.*s  %s", static_cast<int>(gap - wifiText), wifiText,
             static_cast<int>(newline - gap - 2), gap + 2, newline + 1);
  } else {
    strlcpy(wifi, wifiText, sizeof(wifi));
  }
  setText(setWifiLabel, wifi);
  show(Panel::SETTINGS);
}

// Days from 1970-01-01 to a YYYYMMDD date (proleptic Gregorian).
long daysFromCivil(uint32_t yyyymmdd) {
  long y = yyyymmdd / 10000;
  const unsigned m = (yyyymmdd / 100) % 100, d = yyyymmdd % 100;
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}

// "12 d off roast", or the date itself until the clock has been set. Brief,
// after the roast level: "12 d", or the date alone.
void describeRoastAge(const bbw::Recipe &r, bool brief, char *out, size_t size) {
  out[0] = '\0';
  if (r.roastDate == 0) return;
  const uint32_t now = app::epochNow();
  const long days = now ? static_cast<long>(now / 86400) - daysFromCivil(r.roastDate) : -1;
  if (days >= 0 && days < 1000) {
    snprintf(out, size, brief ? "%ld d" : "%ld d off roast", days);
  } else {
    snprintf(out, size, brief ? "%04u-%02u-%02u" : "roasted %04u-%02u-%02u",
             static_cast<unsigned>(r.roastDate / 10000),
             static_cast<unsigned>((r.roastDate / 100) % 100),
             static_cast<unsigned>(r.roastDate % 100));
  }
}

// What a grind setting is read in on this controller's grinder ("um", "steps"...).
const char *grindUnit() {
  static bbw::Grinder grinder;
  grinder = app::grinder();
  return grinder.unit;
}

// A setting as this grinder's dial reads (core/SettingFormat.h), in one of a few rotating buffers so
// that several can go into one line. A change of setting, and what to call it, likewise.
const char *gs(uint16_t value) {
  static char buffers[6][12];
  static uint8_t next;
  char *out = buffers[next++ % 6];
  bbw::formatSetting(value, app::grinder(), out, sizeof(buffers[0]));
  return out;
}
const char *gd(int delta) {
  static char buffers[3][12];
  static uint8_t next;
  char *out = buffers[next++ % 3];
  bbw::formatSettingDelta(delta, app::grinder(), out, sizeof(buffers[0]));
  return out;
}
const char *gdu() {
  static bbw::Grinder grinder;
  grinder = app::grinder();
  return bbw::settingDeltaUnit(grinder);
}

void describePreinfusion(const bbw::Recipe &r, char *out, size_t size) {
  if (r.preinfusion == bbw::Preinfusion::HOLD) {
    if (r.bloomPauseMs) {
      snprintf(out, size, "pre %.1f s, bloom %.0f s", r.preinfusionHoldMs / 1000.0f,
               r.bloomPauseMs / 1000.0f);
    } else {
      snprintf(out, size, "pre-inf hold %.1f s", r.preinfusionHoldMs / 1000.0f);
    }
  } else {
    snprintf(out, size, "pre-inf machine");
  }
}

void refreshRecipes() {
  static uint32_t builtFor = 0;
  if (builtFor != recipesOpenedAtMs) {  // build once per opening, not every refresh
    builtFor = recipesOpenedAtMs;
    const uint8_t count = app::presetCount(), activeIndex = app::activePreset();
    const char *unit = grindUnit();
    lv_obj_clean(recipeList);
    for (uint8_t i = 0; i < count; ++i) {
      // Entries of a finished coffee are left out (the active one stays, should every entry be hidden).
      if (i != activeIndex && !app::presetVisible(i)) continue;
      const bbw::Recipe r = app::preset(i);
      // Two lines: the coffee, then the recipe with what it makes and the
      // grind (a long name wraps onto a third). A recipe listed by itself has
      // its name on the first line. The roast details are on the home screen.
      char grind[24] = "", text[144];
      if (r.grindUm) snprintf(grind, sizeof(grind), "  %s %s", gs(r.grindUm), unit);
      if (r.coffee[0]) {
        snprintf(text, sizeof(text), "%s\n%s  %.1f -> %.1f g%s", r.coffee, r.name, r.doseG,
                 r.targetWeightG, grind);
      } else {
        snprintf(text, sizeof(text), "%s\n%.1f -> %.1f g%s", r.name, r.doseG, r.targetWeightG,
                 grind);
      }
      lv_obj_t *item = lv_list_add_btn(recipeList, nullptr, text);
      // The list's default is a scrolling marquee; show the text still.
      lv_label_set_long_mode(lv_obj_get_child(item, 0), LV_LABEL_LONG_WRAP);
      const bool active = i == activeIndex;
      lv_obj_set_style_bg_color(item, lv_color_hex(active ? COLOR_SELECT : COLOR_PANEL), 0);
      lv_obj_set_style_text_color(item, lv_color_hex(active ? COLOR_ACCENT : COLOR_TEXT), 0);
      lv_obj_set_style_text_font(item, &lv_font_montserrat_20, 0);
      lv_obj_set_style_radius(item, 10, 0);
      lv_obj_set_style_border_width(item, 0, 0);
      lv_obj_set_style_pad_ver(item, 10, 0);
      lv_obj_set_style_pad_hor(item, 12, 0);
      lv_obj_add_event_cb(item, onPickRecipe, LV_EVENT_CLICKED,
                          reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
      if (active) lv_obj_scroll_to_view(item, LV_ANIM_OFF);
    }
    // Size the bar to the list: its range is how far the list can scroll.
    lv_obj_update_layout(recipeList);
    const int range = lv_obj_get_scroll_y(recipeList) + lv_obj_get_scroll_bottom(recipeList);
    if (range > 0) {
      lv_slider_set_range(recipeBar, 0, range);
      lv_slider_set_value(recipeBar, range - lv_obj_get_scroll_y(recipeList), LV_ANIM_OFF);
      lv_obj_clear_flag(recipeBar, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(recipeBar, LV_OBJ_FLAG_HIDDEN);  // everything fits: nothing to scroll
    }
  }
  show(Panel::RECIPES);
}

void describeEditField(const bbw::Recipe &r, int field, char *title, char *value, char *hint,
                       size_t size) {
  switch (field) {
    case 0:
      if (r.useRatio) {
        snprintf(title, size, "RATIO");
        snprintf(value, size, "1:%.1f", r.ratio);
        snprintf(hint, size, "%.1f g in gives\n%.1f g out", r.doseG, r.doseG * r.ratio);
      } else {
        snprintf(title, size, "TARGET OUT");
        snprintf(value, size, "%.1f g", r.targetWeightG);
        snprintf(hint, size, "tap 0.5 g,\nhold for 0.1 g");
      }
      break;
    case 1:
      snprintf(title, size, "DOSE IN");
      snprintf(value, size, "%.1f g", r.doseG);
      snprintf(hint, size, "dry coffee in\nthe basket");
      break;
    case 2:
      snprintf(title, size, "GRIND");
      if (r.grindUm) snprintf(value, size, "%s %s", gs(r.grindUm), grindUnit());
      else snprintf(value, size, "not set");
      {
        const bbw::GrindAdvice g = app::grindAdvice();
        if (g.action == GrindAction::FINER || g.action == GrindAction::COARSER) {
          const char *way = g.action == GrindAction::FINER ? "finer" : "coarser";
          if (bbw::adviceHasNumber(g)) snprintf(hint, size, "advice: go %s,\nset %s %s", way, gs(g.suggestedUm), grindUnit());
          else snprintf(hint, size, "advice: go %s\nhow far: see the\nweb page", way);
        } else {
          snprintf(hint, size, "number on the\ngrinder: tap 1,\nhold for 10");
        }
      }
      break;
    case 3:
      snprintf(title, size, "TARGET TIME");
      snprintf(value, size, "%.1f s", r.targetTimeS);
      snprintf(hint, size, "brew time the\ngrind advice\naims for");
      break;
    default:
      snprintf(title, size, "PRE-INFUSION");
      if (r.preinfusion == bbw::Preinfusion::HOLD) {
        snprintf(value, size, "HOLD %.1f s", r.preinfusionHoldMs / 1000.0f);
        snprintf(hint, size, "pre-infusion lasts\nonly as long as\nthe hold");
      } else {
        snprintf(value, size, "MACHINE");
        snprintf(hint, size, "press and release:\nthe machine\npre-infuses");
      }
      break;
  }
}

void refreshEdit() {
  char title[48], value[48], hint[96];
  describeEditField(editRecipe, editField, title, value, hint, sizeof(title));
  fitText(editTitle, title, 190, 1, &lv_font_montserrat_20);
  fitText(editValue, value, 190, 3, &lv_font_montserrat_30, &lv_font_montserrat_26,
          &lv_font_montserrat_22);
  setText(editHint, hint);
  // SET while the grind is open and differs from the advised setting.
  uint16_t advised = 0;
  if (editField == EDIT_FIELD_GRIND && adviceSetting(advised) && editRecipe.grindUm != advised) {
    char label[32];
    snprintf(label, sizeof(label), "SET %s %s", gs(advised), grindUnit());
    fitText(editSetAdviceLabel, label, 170, 2, &lv_font_montserrat_22, &lv_font_montserrat_20);
    lv_obj_clear_flag(editSetAdviceBtn, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(editSetAdviceBtn, LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < EDIT_FIELDS; ++i) {
    lv_obj_set_style_bg_color(editFieldBtn[i],
                              lv_color_hex(i == editField ? COLOR_SELECT : COLOR_PANEL), 0);
  }
  show(Panel::EDIT);
}

// The coffee screen, one fact to a line; what was never entered is left out.
struct CoffeeLookup {
  uint16_t id;
  bbw::Coffee coffee;
  bool found;
};

void refreshCoffee() {
  const bbw::Recipe r = app::activeRecipe();
  CoffeeLookup look{r.coffeeId, {}, false};
  if (r.coffeeId) {
    app::readLibrary([](const bbw::Library &lib, void *context) {
      CoffeeLookup &l = *static_cast<CoffeeLookup *>(context);
      for (uint8_t i = 0; i < lib.coffeeCount; ++i) {
        if (lib.coffees[i].id == l.id) {
          l.coffee = lib.coffees[i];
          l.found = true;
          return;
        }
      }
    }, &look);
  }
  constexpr int HEAD_W = 536 - 16 - 12 - 104 - 12;
  const char *name = r.coffee[0] ? r.coffee : "No coffee";
  fitText(coffeeTitle, name, HEAD_W, 3, &lv_font_montserrat_26, &lv_font_montserrat_22,
          &lv_font_montserrat_20);
  scrollIfLong(coffeeTitle, name, HEAD_W);

  // Under the name: the roast level and how long ago, "medium-light, 118 d off roast".
  char age[32], sub[64];
  describeRoastAge(r, false, age, sizeof(age));
  snprintf(sub, sizeof(sub), "%s%s%s", r.roastLevel ? bbw::roastLevelName(r.roastLevel) : "",
           r.roastLevel && age[0] ? ", " : "", age);
  fitText(coffeeSub, sub, HEAD_W, 1, &lv_font_montserrat_20);
  scrollIfLong(coffeeSub, sub, HEAD_W);

  // BAG FINISHED, for a coffee on the shelf; asking for the second tap for a few seconds.
  const bool canFinish = r.coffeeId != 0 && look.found;
  const bool asking = canFinish && finishConfirming();
  if (!asking) finishConfirmUntilMs = 0;
  static int8_t keyShown = -1;  // 0 hidden, 1 BAG FINISHED, 2 asking; -1 not drawn yet
  const int8_t keyNow = !canFinish ? 0 : asking ? 2 : 1;
  if (keyNow != keyShown) {
    keyShown = keyNow;
    if (keyNow == 0) lv_obj_add_flag(coffeeFinishKey, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(coffeeFinishKey, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_bottom(coffeeList, keyNow == 0 ? 8 : FINISH_ROOM, 0);
    lv_label_set_text(coffeeFinishLabel, keyNow == 2 ? "TAP AGAIN\nTO CONFIRM" : "BAG FINISHED");
    lv_obj_set_style_bg_color(coffeeFinishKey, lv_color_hex(keyNow == 2 ? COLOR_BAD : COLOR_TRACK), 0);
  }

  // The rows are built once each time the screen is opened.
  static uint32_t builtFor = 0;
  if (builtFor != coffeeOpenedAtMs) {
    builtFor = coffeeOpenedAtMs;
    lv_obj_clean(coffeeList);
    lv_obj_scroll_to_y(coffeeList, 0, LV_ANIM_OFF);
    const bbw::Coffee &c = look.coffee;
    char line[48];
    if (look.found) {
      addCoffeeRow("Roaster", c.roaster);
      if (c.name[0] && strcmp(c.name, r.coffee) != 0) addCoffeeRow("Coffee", c.name);
      addCoffeeRow("Country", c.origin);
    }
    addCoffeeRow("Region", look.found ? c.region : r.region);
    if (look.found) addCoffeeRow("Producer", c.producer);
    addCoffeeRow("Varietal", look.found ? c.varietal : r.varietal);
    if (look.found) {
      addCoffeeRow("Process", c.process);
      if (c.altitudeM) {
        snprintf(line, sizeof(line), "%u m", static_cast<unsigned>(c.altitudeM));
        addCoffeeRow("Altitude", line);
      }
    }
    if (r.roastDate) {
      snprintf(line, sizeof(line), "%04u-%02u-%02u", static_cast<unsigned>(r.roastDate / 10000),
               static_cast<unsigned>((r.roastDate / 100) % 100), static_cast<unsigned>(r.roastDate % 100));
      addCoffeeRow("Roasted", line);
    }
    if (look.found) addCoffeeRow("Notes", c.notes);
    if (lv_obj_get_child_cnt(coffeeList) == 0) {
      lv_obj_t *none = makeLabel(coffeeList, &lv_font_montserrat_20, COLOR_DIM, LV_ALIGN_TOP_LEFT, 0, 0,
                                 r.coffee[0] || look.found
                                     ? "Nothing more is known about this coffee.\nAdd details on the web page, Coffees."
                                     : "No coffee is attached to this recipe.\nAdd one on the web page, Coffees.");
      lv_label_set_long_mode(none, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(none, lv_pct(100));
    }
  }
  show(Panel::COFFEE);
}

void refreshHome(const bbw::LiveStatus &st) {
  const bool ready = st.state == State::READY;
  if (!ready) {
    setText(homeStatus, app::scaleResting() ? "SCALE RESTING" : "NO SCALE");
  } else if (!st.botPaired && st.machineStyle != static_cast<uint8_t>(bbw::MachineStyle::MANUAL)) {
    setText(homeStatus, "NO BOT");
  } else if (st.cupRequired && !st.cupPresent) {
    setTextF(homeStatus, "NO CUP  %.1f g", st.weightG);
  } else {
    setTextF(homeStatus, "READY  %.1f g", st.weightG);
  }
  // Red: no scale. Amber: scale, but no cup on it yet. Green: ready to brew.
  const bool cupMissing = st.state == State::READY && st.cupRequired && !st.cupPresent;
  setDot(homeScaleDot, !st.scaleConnected ? COLOR_BAD : (cupMissing ? COLOR_ACCENT : COLOR_OK));
  if (st.scaleConnected && st.scaleBatteryPercent >= 0) {
    setTextF(homeScaleBattery, "%d%%", st.scaleBatteryPercent);
    setColor(homeScaleBattery, st.scaleBatteryPercent <= 15 ? COLOR_ACCENT : COLOR_DIM);
  } else {
    setText(homeScaleBattery, "");
  }
  // Grey: paired and asleep, which is the normal idle state.
  setDot(homeBotDot, st.botConnected ? COLOR_OK : (st.botPaired ? COLOR_SLEEP : COLOR_BAD));
  if (st.botBatteryPercent >= 0) {
    setTextF(homeBotBattery, "%d%%", st.botBatteryPercent);
    setColor(homeBotBattery, st.botBatteryPercent <= 15 ? COLOR_ACCENT : COLOR_DIM);
  } else {
    setText(homeBotBattery, "");
  }

  const bbw::Recipe r = app::activeRecipe();
  char text[64];
  // The coffee is what the card is headed by; a recipe listed by itself heads its own.
  fitText(homeName, r.coffee[0] ? r.coffee : r.name, CARD_W - 24, 3, &lv_font_montserrat_26, &lv_font_montserrat_22,
          &lv_font_montserrat_20);
  scrollIfLong(homeName, r.coffee[0] ? r.coffee : r.name, CARD_W - 24);
  // With the coffee as the heading, this line names the recipe: "Triple - 24.0 in -> 48.0 out",
  // with the ratio after it when there is room.
  char yield[64];
  if (r.coffee[0]) snprintf(yield, sizeof(yield), "%s - %.1f in -> %.1f out", r.name, r.doseG, r.targetWeightG);
  else snprintf(yield, sizeof(yield), "%.1f g in -> %.1f g out", r.doseG, r.targetWeightG);
  snprintf(text, sizeof(text), "%s  1:%.1f", yield, r.ratio);
  if (!r.useRatio || lv_txt_get_width(text, strlen(text), &lv_font_montserrat_20, 0,
                                      LV_TEXT_FLAG_NONE) > CARD_W) {
    snprintf(text, sizeof(text), "%s", yield);
  }
  fitText(homeYield, text, CARD_W, 3, &lv_font_montserrat_24, &lv_font_montserrat_22,
          &lv_font_montserrat_20);
  scrollIfLong(homeYield, text, CARD_W);

  // Left column: grind, the advisor's verdict, pre-infusion. Each has a
  // shorter wording for when the full one is too wide for the column.
  const bbw::GrindAdvice g = app::grindAdvice();
  const char *unit = grindUnit();
  char shorter[64], grindText[40];
  if (r.grindUm) snprintf(grindText, sizeof(grindText), "grind %s %s", gs(r.grindUm), unit);
  else snprintf(grindText, sizeof(grindText), "grind not set");
  // As on the result screen, the setting to go to is only shown once the
  // sensitivity has been measured from this grinder's own shots. The unit is
  // on the grind line above, so the short wording leaves it out.
  if (r.grindUm && (g.action == GrindAction::FINER || g.action == GrindAction::COARSER)) {
    const char *way = g.action == GrindAction::FINER ? "finer" : "coarser";
    if (bbw::adviceHasNumber(g)) {
      const char *to = gs(g.suggestedUm);
      if (g.refined) snprintf(text, sizeof(text), "liked: %s %s %s", way, to, unit);
      else snprintf(text, sizeof(text), "go %s: %s %s", way, to, unit);
      snprintf(shorter, sizeof(shorter), "%s%s %s", g.refined ? "liked: " : "", way, to);
    } else {
      snprintf(text, sizeof(text), "go %s", way);
      snprintf(shorter, sizeof(shorter), "%s", way);
    }
  } else if (r.grindUm && g.action == GrindAction::INCONSISTENT) {
    snprintf(text, sizeof(text), "brew times vary");
    snprintf(shorter, sizeof(shorter), "times vary");
  } else if (r.grindUm && g.action == GrindAction::HOLD && g.betweenSteps) {
    snprintf(text, sizeof(text), "closest step to target");
    snprintf(shorter, sizeof(shorter), "closest to target");
  } else if (r.grindUm && g.action == GrindAction::HOLD) {
    snprintf(text, sizeof(text), "brew time on target");
    snprintf(shorter, sizeof(shorter), "time on target");
  } else if (!r.grindUm && app::communityStartForActive().setting != 0) {
    // No setting entered yet, and the shared library has somewhere to start.
    const char *start = gs(app::communityStartForActive().setting);
    snprintf(text, sizeof(text), "community: start %s %s", start, unit);
    snprintf(shorter, sizeof(shorter), "community %s %s", start, unit);
    if (lv_txt_get_width(shorter, strlen(shorter), &lv_font_montserrat_20, 0, LV_TEXT_FLAG_NONE) > COL_W) {
      snprintf(shorter, sizeof(shorter), "community %s", start);  // the unit is on the edit screen
    }
  } else if (!r.grindUm && r.coffeeId != 0 &&
             app::similarStart(r.coffeeId, r.recipeId).setting != 0) {
    // Nothing from the library either: the owner's own most similar coffee (a guess).
    const char *start = gs(app::similarStart(r.coffeeId, r.recipeId).setting);
    snprintf(text, sizeof(text), "similar: try %s %s", start, unit);
    snprintf(shorter, sizeof(shorter), "similar: %s %s", start, unit);
  } else {
    text[0] = shorter[0] = '\0';
  }
  setColor(homeAdvice, g.action == GrindAction::HOLD ? COLOR_OK : COLOR_ACCENT);
  char preText[48], preShort[48], footText[32];
  describePreinfusion(r, preText, sizeof(preText));
  snprintf(preShort, sizeof(preShort), "pre %.1f, bloom %.0f s", r.preinfusionHoldMs / 1000.0f,
           r.bloomPauseMs / 1000.0f);  // only ever needed with a bloom
  // The stop offset is learned and looks after itself; it is not shown here.
  snprintf(footText, sizeof(footText), "brew time %.0f s", r.targetTimeS);
  const CardEntry entries[] = {{homeGrind, grindText, grindText},
                               {homeAdvice, text, shorter},
                               {homePre, preText, preShort},
                               {homeFoot, footText, footText}};
  fitCardEntries(entries, 4);

  // The roast level and age are on the COFFEE screen.

  // An idle scale is let go so that it can switch itself off (core/ScaleRest.h). While it rests, or
  // is on its way back, Start takes it back and brews as soon as it is there.
  const bool startPending = !ready && app::scaleStartPending();
  const bool scaleNear = !ready && !startPending && app::scaleNearby();
  if (startPending) {
    setText(homeHint, "CONNECTING TO THE SCALE");
    setColor(homeHint, COLOR_DIM);
  } else if (scaleNear) {
    setText(homeHint, touchHint());
    setColor(homeHint, COLOR_OK);
  } else if (!ready) {
    setText(homeHint, "SWITCH ON THE SCALE");
    setColor(homeHint, COLOR_DIM);
  } else if (!st.botPaired && st.machineStyle != static_cast<uint8_t>(bbw::MachineStyle::MANUAL)) {
    setText(homeHint, "PAIR THE BOT IN SETTINGS");
    setColor(homeHint, COLOR_DIM);
  } else if (!st.weightFresh) {
    setText(homeHint, "WAITING FOR WEIGHT");
    setColor(homeHint, COLOR_DIM);
  } else if (st.cupRequired && !st.cupPresent) {
    setText(homeHint, "PLACE CUP ON SCALE");
    setColor(homeHint, COLOR_ACCENT);
  } else {
    setText(homeHint, touchHint());
    setColor(homeHint, COLOR_OK);
  }
  // A key only while it offers the start; plain text, as before, otherwise.
  const bool canStart = scaleNear ||
                        (ready && (st.botPaired || st.machineStyle == static_cast<uint8_t>(bbw::MachineStyle::MANUAL)) &&
                         st.weightFresh && !(st.cupRequired && !st.cupPresent));
  if (canStart != homeCanStart || !lv_obj_has_flag(homeStartKey, LV_OBJ_FLAG_USER_1)) {
    homeCanStart = canStart;
    lv_obj_add_flag(homeStartKey, LV_OBJ_FLAG_USER_1);  // styled at least once
    lv_obj_set_style_bg_opa(homeStartKey, canStart ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (canStart) lv_obj_add_flag(homeStartKey, LV_OBJ_FLAG_CLICKABLE);
    else lv_obj_clear_flag(homeStartKey, LV_OBJ_FLAG_CLICKABLE);
  }
  show(Panel::HOME);
}

void refreshBrew(const bbw::LiveStatus &st) {
  const char *stopText = "STOP";
  const char *phase = "";
  uint32_t phaseColor = COLOR_ACCENT;
  uint32_t ringColor = COLOR_TRACK;
  bool running = false;  // the machine has been started
  switch (st.state) {
    case State::TARING:
      phase = "AUTO TARE\nWAIT FOR ZERO";
      stopText = "CANCEL";
      break;
    case State::WAITING_FOR_CUP:
      phase = "PLACE CUP\nON THE SCALE";
      stopText = "CANCEL";
      break;
    case State::READY_TO_BREW:
      phase = st.botConnected || st.machineStyle == static_cast<uint8_t>(bbw::MachineStyle::MANUAL)
                  ? "TOUCH KEY\nTO BREW" : "WAKING BOT";
      stopText = "CANCEL";
      break;
    case State::STARTING_SHOT:
      phase = st.awaitingDrip ? "PULL THE SHOT\nWAITING FOR COFFEE"
              : st.holdingButton ? "PRE-INFUSION\nHOLDING BUTTON" : "STARTING";
      running = true;
      break;
    case State::BREWING:
      phase = st.targetCue ? "STOP NOW" : st.firstDropSeen ? "BREWING" : "PRE-INFUSION";
      running = true;
      break;
    case State::BLOOMING: {
      static char bloomText[24];
      snprintf(bloomText, sizeof(bloomText), "BLOOM\n%.0f s", st.bloomLeftS);
      phase = st.holdingButton ? "BLOOM DONE\nSTARTING" : bloomText;
      running = true;
      break;
    }
    case State::STOPPING_SHOT:
      phase = st.manualStopNeeded ? "STOP BY HAND!" : "STOPPING";
      if (st.manualStopNeeded) phaseColor = COLOR_BAD;
      running = true;
      break;
    default:  // POST_SHOT_DRIP
      // After a stop with no coffee seen, the pump is watched for a while:
      // one press toggles, so the stop may have been a start.
      phase = st.verifyingStop ? "CHECKING\nPUMP IS OFF" : "FINALIZING\nLEAVE CUP";
      running = true;
      break;
  }

  // Ring: empty and grey before the machine starts, a full red ring while it
  // runs with nothing in the cup, then green growing with the weight.
  int progress = 0;
  if (running && st.firstDropSeen && st.targetWeightG > 0.0f) {
    const float share = st.weightG / st.targetWeightG;
    progress = static_cast<int>((share < 0 ? 0 : (share > 1 ? 1 : share)) * 1000.0f);
    ringColor = COLOR_OK;
  }
  lv_obj_set_style_arc_color(brewArc, lv_color_hex(running ? COLOR_BAD : COLOR_TRACK),
                             LV_PART_MAIN);
  if (lv_arc_get_value(brewArc) != progress) lv_arc_set_value(brewArc, progress);
  lv_obj_set_style_arc_color(brewArc, lv_color_hex(ringColor), LV_PART_INDICATOR);

  // Centre: the target until coffee is detected, then the live weight.
  const bool showWeight = st.firstDropSeen || st.state == State::TARING ||
                          st.state == State::WAITING_FOR_CUP;
  if (showWeight) {
    setTextF(brewWeight, "%.1f", st.weightG);
    setColor(brewWeight, st.firstDropSeen ? COLOR_OK : COLOR_TEXT);
    setText(brewUnit, "g");
  } else {
    setTextF(brewWeight, "%.1f", st.targetWeightG);
    setColor(brewWeight, COLOR_DIM);
    setText(brewUnit, "g target");
  }

  // The time is the scale's own timer whenever the scale reports it, so the
  // two displays agree; otherwise the controller's clock.
  const float seconds =
      st.scaleTimerValid && st.state == State::BREWING ? st.scaleTimerS : st.shotTimeS;
  char clock[16];
  snprintf(clock, sizeof(clock), "%.1f s", running ? seconds : 0.0f);
  fitText(brewTime, clock, 150, 3, &lv_font_montserrat_36, &lv_font_montserrat_30,
          &lv_font_montserrat_26);
  setTextF(brewTarget, "/ %.1f g", st.targetWeightG);
  setTextF(brewFlow, "%.1f g/s", st.flowGS);
  // BOOT was pressed during the shot: ask before stopping.
  const bool stoppable = st.state == State::STARTING_SHOT || st.state == State::BREWING ||
                         st.state == State::BLOOMING;
  if (!stoppable) stopConfirmUntilMs = 0;
  if (stopConfirmUntilMs != 0 && static_cast<int32_t>(millis() - stopConfirmUntilMs) >= 0) {
    stopConfirmUntilMs = 0;
  }
  if (stopConfirmUntilMs != 0) {
    phase = "STOP SHOT?\nPRESS AGAIN";
    phaseColor = COLOR_BAD;
    stopText = "YES\nSTOP";
  }
  setText(brewPhase, phase);
  setColor(brewPhase, phaseColor);
  setText(brewStopLabel, stopText);
  show(Panel::BREW);
}

// The result of a shot. Also used for a shot that ran out of time: the same
// details, under a heading that says so.
bool resReviewPending = false;  // the result screen is showing IGNORE / COUNT

void refreshResult(State state) {
  bbw::ShotRecord shot;
  const bool have = app::lastShot(shot);
  const bbw::EndReason ended =
      have ? static_cast<bbw::EndReason>(shot.endReason) : bbw::EndReason::NONE;
  // Anything but a normal finish gets a red heading saying what ended it.
  const bool abnormal = state != State::SHOT_COMPLETE;
  // Of those, the ones that ran out of time are put to the user (below).
  const bool timedOut = abnormal && (ended == bbw::EndReason::TIMEOUT ||
                                     ended == bbw::EndReason::MACHINE_STOPPED);
  if (have) {
    const float error = shot.finalWeightG - shot.targetWeightG;
    char yield[32];
    if (fabsf(error) < 0.05f) snprintf(yield, sizeof(yield), "ON TARGET");
    else snprintf(yield, sizeof(yield), "%s %+.1f g", error > 0 ? "OVER" : "UNDER", error);

    if (abnormal) {
      const char *heading = "SHOT ENDED EARLY";
      switch (ended) {
        case bbw::EndReason::TIMEOUT: heading = "SHOT TIMED OUT"; break;
        // The pour stopped with nothing pressed here: the machine's own
        // time-out, or someone pressed its button. The scale cannot tell which.
        case bbw::EndReason::MACHINE_STOPPED: heading = "STOPPED AT THE MACHINE"; break;
        case bbw::EndReason::CUP_REMOVED: heading = "CUP LIFTED: STOPPED"; break;
        case bbw::EndReason::RECOVERED: heading = "STOPPED AFTER RESTART"; break;
        default: break;
      }
      fitText(resTop, heading, LEFT_W, 3, &lv_font_montserrat_26, &lv_font_montserrat_22,
              &lv_font_montserrat_20);
      setColor(resTop, COLOR_BAD);
    } else {
      // How far off the weight, and how far off the brew time.
      const bbw::Settings cfg = app::settings();
      const float brewS = bbw::grindShotTime(shot, cfg.grindTimeBasis);
      const float late = brewS - shot.targetTimeS;
      const bool timed = shot.targetTimeS > 0.0f && brewS > 0.0f;
      char top[64];
      if (!timed) snprintf(top, sizeof(top), "%s", yield);
      else if (fabsf(late) < 0.05f) snprintf(top, sizeof(top), "%s   ON TIME", yield);
      else snprintf(top, sizeof(top), "%s   %s %+.1f s", yield, late > 0 ? "SLOW" : "FAST", late);
      fitText(resTop, top, LEFT_W, 2, &lv_font_montserrat_26, &lv_font_montserrat_22,
              &lv_font_montserrat_20);
      const bool good = fabsf(error) <= 0.5f && (!timed || fabsf(late) <= cfg.grindToleranceS);
      setColor(resTop, good ? COLOR_OK : COLOR_ACCENT);
    }
    // An asterisk marks a weight that did not settle (cup lifted early).
    setTextF(resWeight, "%.1f g%s", shot.finalWeightG,
             (shot.flags & bbw::SHOT_FLAG_FINAL_VALID) ? "" : " *");
    if (abnormal && shot.pourTimeS > 0.0f) {
      fitDetail(resLine1, "%s   %.1f s   pour %.1f s", yield, shot.shotTimeS, shot.pourTimeS);
    } else if (abnormal) {
      fitDetail(resLine1, "%s   %.1f s", yield, shot.shotTimeS);
    } else if (shot.pourTimeS > 0.0f) {
      fitDetail(resLine1, "%.1f s total   pour %.1f s", shot.shotTimeS, shot.pourTimeS);
    } else {
      fitDetail(resLine1, "%.1f s", shot.shotTimeS);
    }
    const float flow = shot.pourTimeS > 0.0f ? shot.pourFlowGS : shot.averageFlowGS;
    const float change = shot.nextStopOffsetG - shot.stopOffsetG;
    if ((shot.flags & bbw::SHOT_FLAG_LEARNED) && fabsf(change) >= 0.005f) {
      // A larger offset means the next shot is stopped earlier.
      // The longest line on the screen: tighter wordings until one fits at 20 pt.
      const char *way = change > 0 ? "earlier" : "later";
      char line[64];
      snprintf(line, sizeof(line), "%.2f g/s  drip %.1f g  auto %s %.2f", flow, shot.dripWeightG,
               way, fabsf(change));
      if (lv_txt_get_width(line, strlen(line), &lv_font_montserrat_20, 0, LV_TEXT_FLAG_NONE) > LEFT_W) {
        snprintf(line, sizeof(line), "%.1f g/s  drip %.1f g  auto %s %.2f", flow, shot.dripWeightG,
                 way, fabsf(change));
      }
      if (lv_txt_get_width(line, strlen(line), &lv_font_montserrat_20, 0, LV_TEXT_FLAG_NONE) > LEFT_W) {
        snprintf(line, sizeof(line), "%.1f g/s drip %.1f g %s %.2f", flow, shot.dripWeightG, way,
                 fabsf(change));
      }
      fitDetail(resLine2, "%s", line);
    } else {
      fitDetail(resLine2, "%.2f g/s   drip %.1f g", flow, shot.dripWeightG);
    }
  }

  // Grinder verdict. Two different things are kept apart here: how THIS shot
  // ran, and what the recent shots at this setting say about the grind. A
  // single fast shot among consistent ones is reported as fast, with the
  // grind left alone until it happens again.
  const bbw::GrindAdvice g = app::grindAdvice();
  const bbw::Settings s = app::settings();
  const bbw::GrindUse use =
      have ? bbw::grindShotUse(shot, s.grindTimeBasis) : bbw::GrindUse::NO_TIME;
  const bool counted = use == bbw::GrindUse::COUNTED;
  const float thisTime = have ? bbw::grindShotTime(shot, s.grindTimeBasis) : 0.0f;

  // Ask rather than decide, when the shot was unusual and has not been ruled
  // on yet: a one-off among consistent shots, or one that ran out of time.
  const bool undecided = have && shot.grindVote == static_cast<uint8_t>(bbw::GrindVote::AUTO) &&
                         shot.grindUm != 0 && thisTime > 0.0f;
  const bool oneOff = undecided && counted && g.newestIgnored;
  // ...or one that reached its weight but so far off the target time that it
  // falls outside what is counted automatically.
  const bool offTime = undecided && (use == bbw::GrindUse::TOO_SHORT ||
                                     use == bbw::GrindUse::TOO_LONG);
  const bool review = oneOff || offTime || (undecided && timedOut);
  auto showIf = [](lv_obj_t *obj, bool visible) {
    if (visible) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  };
  resReviewPending = review;
  // A grind change recommended by this shot: offer the way to the grinder
  // setting beside READY.
  const bool grindChange = !review && have && counted && !g.newestIgnored &&
                           (g.action == GrindAction::FINER || g.action == GrindAction::COARSER);
  showIf(resReview, review);
  showIf(resIgnoreBtn, review);
  showIf(resCountBtn, review);
  showIf(resReadyBtn, !review && !grindChange);
  showIf(resReadyHalfBtn, grindChange);
  showIf(resGrinderBtn, grindChange);
  showIf(resGrind, !review);
  showIf(resGrindSub, !review);
  if (review) {
    resShotId = shot.id;
    // A shot that ran out of time was slow by definition.
    const bool fast = !timedOut && thisTime < shot.targetTimeS;
    setTextF(resReview, "Shot %s than expected.\nReview preparation routine.\nGrind %s if necessary.",
             fast ? "faster" : "slower", fast ? "finer" : "coarser");
    show(Panel::RESULT);
    return;
  }

  setColor(resGrind, COLOR_ACCENT);
  if (g.action == GrindAction::NO_GRIND_SET) {
    setText(resGrind, "");
    setText(resGrindSub, "set the grind in EDIT\nfor grind advice");
  } else if (!counted) {
    // Say why, in each case.
    const char *why = "this shot is not counted\nfor grind advice";
    switch (use) {
      case bbw::GrindUse::IGNORED_BY_USER: why = "ignored for grind advice,\nas you chose"; break;
      case bbw::GrindUse::NO_TIME: why = "no first drops detected:\nnot counted for grind advice"; break;
      case bbw::GrindUse::OTHER_BASIS: why = "time basis was changed:\nnot counted for grind advice"; break;
      case bbw::GrindUse::NOT_SETTLED: why = "cup lifted, weight unsettled:\nnot counted for grind advice"; break;
      case bbw::GrindUse::STOP_RETRIED: why = "stop had to be repeated:\nnot counted for grind advice"; break;
      case bbw::GrindUse::NOT_WEIGHT_STOP:
        switch (ended) {
          case bbw::EndReason::USER: why = "stopped with STOP:\nnot counted for grind advice"; break;
          case bbw::EndReason::CUP_REMOVED: why = "cup lifted during the shot:\nnot counted for grind advice"; break;
          case bbw::EndReason::RECOVERED: why = "restarted during the shot:\nnot counted for grind advice"; break;
          default: why = "did not stop on its weight:\nnot counted for grind advice"; break;
        }
        break;
      default: break;
    }
    setText(resGrind, "");
    setText(resGrindSub, why);
  } else if (g.newestIgnored) {
    fitLine(resGrind, thisTime < shot.targetTimeS ? "BREW FAST   ONE-OFF?"
                                                  : "BREW SLOW   ONE-OFF?");
    setTextF(resGrindSub, "%.1f s vs %.0f s: grind kept,\nadvice if it repeats",
             thisTime, shot.targetTimeS);
  } else if (g.action == GrindAction::FINER || g.action == GrindAction::COARSER) {
    const bool finer = g.action == GrindAction::FINER;
    fitLine(resGrind, finer ? "BREW FAST   GRIND FINER" : "BREW SLOW   GRIND COARSER");
    // The amount is only shown once it rests on a sensitivity measured from
    // this grinder's own history.
    if (bbw::adviceHasNumber(g)) {
      setTextF(resGrindSub, "%s %s %s  ->  set %s %s", gd(g.deltaUm),
               gdu(), finer ? "finer" : "coarser", gs(g.suggestedUm), grindUnit());
    } else {
      setText(resGrindSub, "see web page for details");
    }
  } else if (g.action == GrindAction::INCONSISTENT) {
    fitLine(resGrind, "BREW TIMES VARY   NO ADVICE");
    setTextF(resGrindSub, "%.0f to %.0f s at %s %s:\ncheck the preparation",
             g.minTimeS, g.maxTimeS, gs(g.currentUm), grindUnit());
  } else if (g.action == GrindAction::HOLD) {
    fitLine(resGrind, "BREW TIME OK   KEEP GRIND");
    setColor(resGrind, COLOR_OK);
    if (g.shotsUsed > 1) {
      setTextF(resGrindSub, "this shot %.1f s, last %u\naverage %.1f s, target %.0f s",
               thisTime, g.shotsUsed, g.averageTimeS, g.targetTimeS);
    } else {
      setTextF(resGrindSub, "%.1f s, target %.0f s", thisTime, g.targetTimeS);
    }
  } else {
    setText(resGrind, "");
    setText(resGrindSub, "");
  }
  show(Panel::RESULT);
}

void setStandby(bool on) {
  if (on == screenOff) return;
  screenOff = on;
  if (on) {
    amoled.setBrightness(0);  // AMOLED: every pixel off
    setCpuFrequencyMhz(CPU_STANDBY_MHZ);
    LOGI("ui: scale is off, standby");
  } else {
    setCpuFrequencyMhz(CPU_ACTIVE_MHZ);
    amoled.setBrightness(BRIGHT_ACTIVE);
    LOGI("ui: awake");
  }
}

void refresh() {
  const bbw::LiveStatus st = app::status();
  static State lastState = State::BOOT;
  if (st.state != lastState) {
    lastState = st.state;
    wake();
  }
  const bool idle = st.state == State::READY || st.state == State::CONNECTING_SCALE;

  // Standby with the scale. Only while idle with no scale; anything else
  // (scale back, a shot, a fault, a touch) brings the controller back. After
  // a touch it stays awake until it has been left alone again.
  const uint32_t now = millis();
  if (st.state == State::CONNECTING_SCALE && !st.scaleConnected) {
    if (scaleLostAtMs == 0) scaleLostAtMs = now ? now : 1;
    if (!screenOff && now - scaleLostAtMs >= SCALE_OFF_SLEEP_MS &&
        lv_disp_get_inactive_time(nullptr) >= SCALE_OFF_SLEEP_MS) {
      settingsOpen = recipesOpen = editOpen = coffeeOpen = false;
      setStandby(true);
    }
  } else {
    scaleLostAtMs = 0;
    if (screenOff) wake();
  }
  if (screenOff) {
    if (lv_disp_get_inactive_time(nullptr) < 1000) {
      // Touched while dark: wake only, the touch must not press a key.
      lv_indev_wait_release(lv_indev_get_next(nullptr));
      wake();
      app::noteActivity();  // the owner is here: a resting scale is taken back
    }
    return;
  }
  // A touch on the screen (not this code's own wake(), which also counts as activity to LVGL):
  // an idle scale is not let go, and one let go is taken back.
  {
    const uint32_t inactive = lv_disp_get_inactive_time(nullptr);
    if (inactive < 1000 && static_cast<int32_t>((millis() - inactive) - ownActivityMs) > 50) {
      app::noteActivity();
    }
  }

  if (editPending) {
    // GRINDER on the result screen: the edit screen opens once the
    // acknowledgement has taken the controller back to READY.
    if (idle) {
      editPending = false;
      editOpen = true;
    } else if (st.state != State::SHOT_COMPLETE) {
      editPending = false;  // something else happened meanwhile
    }
  }
  if (recipesOpen && idle) {
    refreshRecipes();
    return;
  }
  recipesOpen = false;
  if (editOpen && idle) {
    refreshEdit();
    return;
  }
  editOpen = false;
  if (settingsOpen && idle) {
    refreshSettings();
    return;
  }
  settingsOpen = false;
  if (coffeeOpen && idle) {
    refreshCoffee();
    return;
  }
  coffeeOpen = false;

  switch (st.state) {
    case State::BOOT:
    case State::INITIALIZING:
    case State::CONNECTING_SCALE:
    case State::READY:
      refreshHome(st);
      break;
    case State::TARING:
    case State::WAITING_FOR_CUP:
    case State::READY_TO_BREW:
    case State::STARTING_SHOT:
    case State::BREWING:
    case State::BLOOMING:
    case State::STOPPING_SHOT:
    case State::POST_SHOT_DRIP:
      refreshBrew(st);
      break;
    case State::SHOT_COMPLETE:
      refreshResult(st.state);
      // The used cup taken off and a fresh one put on: that is "ready for the
      // next one". Back to the home screen without a tap, unless the result
      // is still asking whether the shot counts for the grind.
      if (st.cupPresent && !resReviewPending) app::acknowledge();
      break;
    case State::SHOT_TIMEOUT:
    case State::INVALID_WEIGHT: {
      // A shot that ran out of time, or was stopped because the cup was
      // lifted, still gets its summary. If the stop could not be confirmed,
      // or the weight was simply implausible, the warning screen takes over.
      bbw::ShotRecord last;
      const bool cupLifted = app::lastShot(last) &&
                             last.endReason == static_cast<uint8_t>(bbw::EndReason::CUP_REMOVED);
      if (!st.manualStopNeeded && (st.state == State::SHOT_TIMEOUT || cupLifted)) {
        refreshResult(st.state);
        break;
      }
    }
      [[fallthrough]];
    default: {
      fitText(faultTitle, st.manualStopNeeded ? "STOP MACHINE" : faultTitle_(st.state),
              LEFT_W - 16, 3, &lv_font_montserrat_24, &lv_font_montserrat_22,
              &lv_font_montserrat_18);
      setText(faultText, faultMessage(st.state, st.manualStopNeeded, st.stopStartedPump));
      const bool offerPress = st.botConnected && (st.state == State::START_COMMAND_FAILED ||
                                                  st.state == State::STOP_COMMAND_FAILED);
      if (offerPress) lv_obj_clear_flag(faultPressBtn, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(faultPressBtn, LV_OBJ_FLAG_HIDDEN);
      show(Panel::FAULT);
      break;
    }
  }

  // Dim an idle home screen; never during a shot or a fault.
  if (idle && !dimmed && lv_disp_get_inactive_time(nullptr) > IDLE_DIM_MS) {
    amoled.setBrightness(BRIGHT_IDLE);
    dimmed = true;
  } else if (dimmed && lv_disp_get_inactive_time(nullptr) < 1000) {
    wake();
  }
}

}  // namespace

bool begin() {
  if (!amoled.begin()) {
    LOGW("ui: AMOLED panel not detected");
    return false;
  }
  LOGI("ui: board %s", amoled.getName());
  flippedShown = app::settings().displayFlipped;
  amoled.setRotation(flippedShown ? ROTATION_USB_RIGHT : ROTATION_USB_LEFT);
  amoled.setBrightness(BRIGHT_ACTIVE);
  setPalette(app::settings().uiTheme);
  beginLvglHelper(amoled);
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(COLOR_BG), 0);
  buildHome();
  buildBrew();
  buildResult();
  buildFault();
  buildSettings();
  buildRecipes();
  buildEdit();
  buildCoffee();
  recordLayout();
  placeLayout(flippedShown);
  // The CST816T reports the key on the glass as a touch at a fixed point off
  // the end of the panel; the library turns that into this callback.
  amoled.setHomeButtonCallback(onTouchKey, nullptr);
#ifndef BBW_EMULATOR
  // The CST816T touch chip sleeps by itself when left alone. On this board it
  // then stops reporting touches altogether once it has been touched and left
  // again (measured: the chip still answers, with zero fingers, while being
  // tapped), so a dark screen could not be woken a second time. Kept awake,
  // it costs a few milliamps.
  amoled.TouchDrvCSTXXX::disableAutoSleep();
#endif
  started = true;
  show(Panel::HOME);
  return true;
}

void wake() {
  if (!started) return;
  if (screenOff) {
    setStandby(false);
    // Stay up for a full idle period before considering standby again.
    if (scaleLostAtMs != 0) scaleLostAtMs = millis() ? millis() : 1;
  } else if (dimmed) {
    amoled.setBrightness(BRIGHT_ACTIVE);
  }
  dimmed = false;
  lv_disp_trig_activity(nullptr);
  ownActivityMs = millis();
}

bool isOff() { return screenOff; }

void setWifiLine(const char *text) { strlcpy(wifiText, text, sizeof(wifiText)); }
const char *wifiLine() { return wifiText; }

void loop() {
  if (!started) return;
  static uint32_t lastRefresh = 0;
  const uint32_t now = millis();
  if (now - lastRefresh >= 100) {
    lastRefresh = now;
    // Turned from the web page: follow it here.
    const bool flipped = app::settings().displayFlipped;
    if (flipped != flippedShown) applyRotation(flipped);
    const uint8_t theme = app::settings().uiTheme;
    if (theme != themeShown) applyTheme(theme);
    refresh();
  }
  lv_timer_handler();

  // The touch key starts a shot. It never stops one: it is too easy to
  // brush against. From a menu it goes back to the home screen (an unsaved
  // recipe edit is dropped), so starting always takes a press on that screen.
  if (touchKeyPressed) {
    touchKeyPressed = false;
    const bool wasOff = screenOff;
    LOGI("ui: touch key (%s%s)", bbw::stateName(app::status().state),
         wasOff ? ", screen off: only wakes it" : (settingsOpen || recipesOpen || editOpen || coffeeOpen)
                                                     ? ", a menu was open: back to home" : "");
    wake();
    if (wasOff) {
      // a press on a dark screen only wakes it (and takes a resting scale back)
      app::noteActivity();
    } else if (settingsOpen || recipesOpen || editOpen || coffeeOpen) {
      app::noteActivity();
      settingsOpen = recipesOpen = editOpen = coffeeOpen = false;
    } else {
      const State state = app::status().state;
      if (state == State::READY || state == State::READY_TO_BREW) app::start();
      // The scale rests (let go so that it can switch itself off) or is on its way back: it is taken
      // back and the shot starts once it is there.
      else if (state == State::CONNECTING_SCALE && app::scaleNearby()) app::start();
      else if (state == State::SHOT_COMPLETE || bbw::isFault(state)) app::acknowledge();
    }
  }
}

void bootShortPress() {
  if (!started) return;
  wake();
  const State state = app::status().state;
  if (state == State::STARTING_SHOT || state == State::BREWING || state == State::BLOOMING) {
    // Stopping a shot from the button takes two presses, with the question
    // shown in between. The STOP key on the screen stays a single touch.
    if (stopConfirmUntilMs != 0) {
      stopConfirmUntilMs = 0;
      app::stop();
    } else {
      stopConfirmUntilMs = millis() + STOP_CONFIRM_MS;
      if (stopConfirmUntilMs == 0) stopConfirmUntilMs = 1;
    }
  } else if (state == State::TARING || state == State::WAITING_FOR_CUP ||
             state == State::READY_TO_BREW) {
    app::stop();  // nothing is running yet: this only cancels
  } else if (state == State::SHOT_COMPLETE || bbw::isFault(state)) {
    app::acknowledge();
  } else if (state == State::READY) {
    if (settingsOpen || recipesOpen || editOpen || coffeeOpen) {
      settingsOpen = recipesOpen = editOpen = coffeeOpen = false;  // back to the home screen
    } else {
      app::start();  // spare start button, should the touch key ever fail
    }
  }
}

}  // namespace ui
