#pragma once
// GET /api/screen and POST /api/screen/do: what the controller's own screen shows and does beyond
// /api/status, for a native copy of that screen (the iPhone app). Built on the board too, where
// it simply answers the same way. docs/API.md.

class WebServer;

namespace web {

// Called once from web::begin with the server the other routes are on.
void registerScreenApi(WebServer &server);

}  // namespace web
