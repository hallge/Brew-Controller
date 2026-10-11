#pragma once
// Local web interface: Wi-Fi access point (always) plus an optional home
// network, a single-page UI and a small JSON/CSV API. No cloud.

namespace web {

void begin();
void loop();  // call from the Arduino loop task

}  // namespace web
