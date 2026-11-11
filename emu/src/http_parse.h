#pragma once
// URL and query-string parsing shared by the socket server and the in-process
// web entry point. No socket or firmware headers, so any host can use it.

#include <ctype.h>
#include <stdlib.h>

#include <map>
#include <string>

namespace emu {

inline std::string urlDecode(const std::string &in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '+') {
      out += ' ';
    } else if (in[i] == '%' && i + 2 < in.size() && isxdigit(static_cast<unsigned char>(in[i + 1])) &&
               isxdigit(static_cast<unsigned char>(in[i + 2]))) {
      out += static_cast<char>(strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += in[i];
    }
  }
  return out;
}

inline void parseArgs(const std::string &text, std::map<std::string, std::string> &out) {
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('&', start);
    if (end == std::string::npos) end = text.size();
    const std::string pair = text.substr(start, end - start);
    const size_t eq = pair.find('=');
    if (!pair.empty()) {
      out[urlDecode(pair.substr(0, eq))] = eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
    }
    start = end + 1;
  }
}

}  // namespace emu
