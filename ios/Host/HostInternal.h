#pragma once
// Shared between BrewHost.cpp and ui_stub.cpp. Not part of the Swift-visible API.
#include <stdint.h>

namespace bbwhost {
void setForeground(bool foreground);
bool foreground();
void noteWake();  // ui::wake() was called
uint32_t wakeCount();
}  // namespace bbwhost
