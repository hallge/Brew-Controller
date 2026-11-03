#pragma once
// The requests to Home Assistant and the MQTT broker (core/HomeAssistant.h says what each is for).
// The board's side is HomeAssistantLink.cpp; the emulator's is emu/src/emu_homeassistant.cpp.
// Every call is made on the loop task between shots, with short timeouts: none of them may hold up
// a shot, and every one is off until the owner sets it up.

#include <stddef.h>
#include <stdint.h>

namespace bbw {

// One HTTP request: GET when body is null, else POST with that JSON body. token: a bearer token, or
// null for none (a webhook). The answer's body goes to out (cut to outSize - 1). Returns the HTTP
// status, or a negative number when there was no answer, with why in problem.
int haRequest(const char *url, const char *token, const char *body, char *out, size_t outSize, char *problem,
              size_t problemSize, uint16_t timeoutMs);

// The MQTT link. Configure with an empty url to switch it off. node: a short id unique to this
// controller, in its topics; firmware: shown on the Home Assistant device.
void mqttConfigure(const char *url, const char *user, const char *password, const char *node,
                   const char *firmware);
// Called from loop(): keeps the connection, (re)connects at most once a minute and only when idle,
// and hands a press of the Start button to onStart. true while connected.
bool mqttService(bool idle, void (*onStart)());
bool mqttConnected();
bool mqttPublish(const char *topic, const char *payload, bool retain);
// Why the last connection failed ("" when connected or never tried).
const char *mqttProblem();

}  // namespace bbw
