#pragma once
// The add-ons compiled into this image, as the rest of the firmware sees them (docs/ADDONS.md).
// With none compiled in (the public firmware) every call here does nothing and answers "none".
// Loop task only.

#include <ArduinoJson.h>

#include "core/Types.h"

namespace addons {

void begin();  // setup(), after the library and settings are loaded
void loop();   // every loop() pass
size_t compiledCount();

// A request no route of the controller's own answered. true: an add-on answered it.
bool route(bool post, const char *path, const char *body, int &status, const char *&type, const char *&text);

// /api/addons and its changes. false: no such add-on, or the change is refused (problem says why).
void listJson(JsonObject out);
void namesJson(JsonArray out);  // the ids compiled in (for /api/update)
bool enable(const char *id, bool on, const char *&problem);
bool configure(const char *id, JsonObjectConst values, JsonObjectConst options, const char *&problem);
bool map(const char *id, uint8_t grinderRecipe, uint16_t recipeId, const char *&problem);
bool command(const char *id, const char *cmd, const char *arg, char *reply, size_t size);
bool forget(const char *id);
void diagnosticsJson(JsonObject out);  // chip RAM per add-on

// The newest grind from a grinder add-on, for /api/grind. false: none.
bool lastGrind(float &doseG, float &gapUm, uint32_t &ageS);

// The shot save (main.cpp): the dose of the grind this shot followed (tenths of a gram, 0: none),
// put into ShotContextInputs; then, once everything is saved, shotSaved().
uint16_t doseForShot(const bbw::ShotRecord &shot);
void shotSaved(const bbw::ShotRecord &shot);

}  // namespace addons
