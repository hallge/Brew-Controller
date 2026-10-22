// The emulator's page fetch for subscription cards (src/platform/PagePreview.h): the PC's curl
// (Windows 10 and later bring one) reads the page, redirects followed, at most the first 192 KB.
// The certificate check is curl's own.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "emu.h"
#include "platform/PagePreview.h"

namespace bbw {

int fetchPagePreview(const char *url, PageMeta &out, char *problem, size_t problemSize) {
  out = PageMeta{};
  auto say = [&](const char *text) {
    if (problem && problemSize) {
      strncpy(problem, text, problemSize - 1);
      problem[problemSize - 1] = '\0';
    }
  };
  say("");
  if (!url || (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0)) {
    say("not a web link");
    return -2;
  }
  // Only characters a URL may hold go on the command line.
  for (const char *p = url; *p; ++p) {
    if (*p == '"' || static_cast<unsigned char>(*p) < 0x21 || *p == 0x7F) {
      say("not a link that can be read");
      return -2;
    }
  }
  // -w prints the final address and status after the page. The agent matches the board's.
  const std::string command = std::string("curl -sL --max-redirs 4 -m 15 ") +
                              "-A \"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) BrewController/1.0\" -H \"Accept: text/html\" " +
                              "-w \"\\n@@@%{http_code} %{url_effective}\" \"" + url + "\"";
  const std::string wrapped = "\"" + command + "\"";
  FILE *pipe = _popen(wrapped.c_str(), "rb");
  if (!pipe) {
    say("curl could not be run");
    return -1;
  }
  // The first 192 KB are kept, and the end, where curl's status line is.
  std::string text, tail;
  char chunk[4096];
  size_t n;
  bool whole = true;
  while ((n = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
    if (text.size() < 196608) text.append(chunk, n);
    else whole = false;
    tail.append(chunk, n);
    if (tail.size() > 8192) tail.erase(0, tail.size() - 1024);
  }
  _pclose(pipe);
  if (!whole) {
    const size_t tailMark = tail.rfind("\n@@@");
    if (text.size() > 196608) text.resize(196608);
    if (tailMark != std::string::npos) text += tail.substr(tailMark);
  }
  const size_t mark = text.rfind("\n@@@");
  if (mark == std::string::npos) {
    say("the shop's site could not be reached");
    return -1;
  }
  const int code = atoi(text.c_str() + mark + 4);
  std::string finalUrl = text.substr(text.find(' ', mark) + 1);
  while (!finalUrl.empty() && (finalUrl.back() == '\r' || finalUrl.back() == '\n')) finalUrl.pop_back();
  text.resize(mark);
  if (text.size() > 196608) text.resize(196608);
  if (code != 200 && code != 206) {
    if (code == 0) say("the shop's site could not be reached");
    else if (code == 403 || code == 429 || code == 503) say("the shop's site turned the controller away");
    else say("the shop's site answered with an error");
    emu::log("emu", "subscriptions: page not read (answer %d)", code);
    return code == 0 ? -1 : code;
  }
  parsePageHead(text.data(), text.size(), finalUrl.c_str(), out);
  emu::log("emu", "subscriptions: read %u bytes of %s", static_cast<unsigned>(text.size()), finalUrl.c_str());
  return 200;
}

}  // namespace bbw
