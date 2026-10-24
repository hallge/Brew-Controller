// The emulator's side of sending a diagnostics report (src/platform/TelemetryLink.h): the PC's curl
// posts the envelope, written to a file first, to the relay (or to `wrangler dev` on this PC).
// The emulator's reports are unsigned; the relay files them apart from real ones.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "emu.h"
#include "platform/TelemetryLink.h"

namespace bbw {

int diagPost(const char *url, const char *envelope, char *problem, size_t problemSize) {
  auto say = [&](const char *text) {
    if (problem && problemSize) {
      strncpy(problem, text, problemSize - 1);
      problem[problemSize - 1] = '\0';
    }
  };
  say("");
  if (!url || (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://127.0.0.1", 16) != 0 &&
               strncmp(url, "http://localhost", 16) != 0)) {
    say("the relay's address is not an https link");
    return -2;
  }
  for (const char *p = url; *p; ++p) {
    if (*p == '"' || static_cast<unsigned char>(*p) < 0x21 || *p == 0x7F) {
      say("the relay's address could not be used");
      return -2;
    }
  }
  char path[512];
  const char *tmp = getenv("TEMP");
  snprintf(path, sizeof(path), "%s\\bbw_diag_%u.json", tmp ? tmp : ".", static_cast<unsigned>(rand()));
  FILE *f = fopen(path, "wb");
  if (!f) {
    say("the report could not be written for curl");
    return -1;
  }
  fwrite(envelope, 1, strlen(envelope), f);
  fclose(f);
  const std::string command = std::string("curl -s -m 20 -o NUL -w \"%{http_code}\" -H \"Content-Type: application/json\" ") +
                              "--data-binary \"@" + path + "\" \"" + url + "\"";
  const std::string wrapped = "\"" + command + "\"";
  FILE *pipe = _popen(wrapped.c_str(), "rb");
  int code = -1;
  if (pipe) {
    char answer[16] = {};
    fread(answer, 1, sizeof(answer) - 1, pipe);
    _pclose(pipe);
    code = atoi(answer);
    if (code == 0) code = -1;
  }
  remove(path);
  if (code < 0) say("the relay could not be reached");
  else if (code == 429) say("the relay has had enough from this controller today");
  else if (code >= 400) say("the relay refused it");
  emu::log("emu", "diagnostics: relay answered %d", code);
  return code;
}

}  // namespace bbw
