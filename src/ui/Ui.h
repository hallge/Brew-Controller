#pragma once
// Touch UI for the 536x240 AMOLED. One LVGL screen with five panels that
// follow the brew state: home, brew, result, fault, settings.

namespace ui {

bool begin();          // display + touch + LVGL; false if the panel is missing
void loop();           // call often from the Arduino loop task
void wake();           // restore brightness (button press, state change)
bool isOff();          // display switched off because the scale is off
void bootShortPress(); // BOOT button: cancel, confirm-then-stop, acknowledge
const char *wifiLine();
void setWifiLine(const char *text);  // shown on the settings panel

}  // namespace ui
