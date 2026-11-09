// The emulator's side of an add-on's HTTP request (addons/Addon.h FetchJob). Only to this PC
// (localhost / 127.0.0.1), over plain http, through the PC's curl: an add-on run in the emulator is
// tested against a stand-in on this machine, never against a real device on the network.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "addons/Addon.h"
#include "addons/HttpWire.h"
#include "emu.h"

namespace bbw {
namespace addon {

namespace {
void say(FetchJob &job, int status, const char *text) {
  job.status = status;
  strncpy(job.problem, text, sizeof(job.problem) - 1);
  job.problem[sizeof(job.problem) - 1] = '\0';
}

std::string tempPath(const char *what) {
  const char *tmp = getenv("TEMP");
  char path[512];
  snprintf(path, sizeof(path), "%s\\bbw_addon_%s_%u.bin", tmp ? tmp : ".", what, static_cast<unsigned>(rand()));
  return path;
}

bool safe(const char *s) {
  for (const char *p = s; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '"' || c == '%' || c == '^' || c == '&' || c == '|' || c == '<' || c == '>' || c < 0x21 || c == 0x7F)
      return false;
  }
  return true;
}
}  // namespace

void fetchHere(FetchJob &job) {
  job.problem[0] = '\0';
  UrlParts u;
  if (!splitUrl(job.url, u)) return say(job, -2, "not an http(s) address");
  if (u.tls) return say(job, -2, "the emulator fetches http from this PC only");
  if (strcmp(u.host, "127.0.0.1") != 0 && strcmp(u.host, "localhost") != 0) {
    return say(job, -2, "the emulator fetches from this PC only (127.0.0.1)");
  }
  if (!safe(job.url) || !safe(job.cookie)) return say(job, -2, "the address could not be used");
  const std::string answer = tempPath("a"), data = tempPath("d");
  std::string command = "curl -s -i --http1.0 --max-time " + std::to_string(job.totalTimeoutMs / 1000 + 1) +
                        " --connect-timeout " + std::to_string(job.connectTimeoutMs / 1000 + 1) + " -o \"" + answer + "\" ";
  if (job.cookie[0]) command += std::string("-H \"Cookie: ") + job.cookie + "\" ";
  if (job.post) {
    FILE *d = fopen(data.c_str(), "wb");
    if (d) {
      if (job.body) fwrite(job.body, 1, strlen(job.body), d);
      fclose(d);
    }
    command += std::string("-H \"Content-Type: ") + (job.contentType ? job.contentType : "application/json") +
               "\" --data-binary \"@" + data + "\" ";
  }
  command += "\"" + std::string(job.url) + "\"";
  const int rc = system(("\"" + command + "\"").c_str());
  FILE *a = fopen(answer.c_str(), "rb");
  ResponseReader reader(job);
  if (a) {
    uint8_t chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), a)) > 0) reader.feed(chunk, n);
    fclose(a);
  }
  remove(answer.c_str());
  remove(data.c_str());
  if (!reader.headComplete()) say(job, -1, rc ? "could not connect" : "the answer was cut short");
  emu::log("emu", "addon fetch %s %s: %d", job.post ? "POST" : "GET", u.path, job.status);
}

}  // namespace addon
}  // namespace bbw
