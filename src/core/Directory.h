#pragma once
// The coffee directory (docs/COMMUNITY.md): the library's published coffees, kept on flash in pages
// (index/directory/) and searched here. What a search matches: every word of the query appears in
// one of the coffee's fields, whatever the case; a filter (origin, process, roast) must equal its field.

#include <ctype.h>
#include <stddef.h>
#include <string.h>

namespace bbw {

constexpr size_t DIRECTORY_RESULTS = 20;  // coffees in one answer

// `needle` (n characters) inside `hay`, whatever the case.
inline bool containsFolded(const char *hay, const char *needle, size_t n) {
  if (!n) return true;
  for (; hay && *hay; ++hay) {
    size_t k = 0;
    while (k < n && hay[k] && tolower(static_cast<unsigned char>(hay[k])) ==
                                  tolower(static_cast<unsigned char>(needle[k]))) {
      ++k;
    }
    if (k == n) return true;
  }
  return false;
}

// Every word of `query` (split on spaces) is in at least one of the fields.
inline bool directoryMatch(const char *query, const char *const *fields, size_t count) {
  const char *p = query ? query : "";
  while (*p) {
    while (*p == ' ') ++p;
    const char *end = p;
    while (*end && *end != ' ') ++end;
    const size_t n = static_cast<size_t>(end - p);
    if (n) {
      bool found = false;
      for (size_t i = 0; i < count && !found; ++i) found = containsFolded(fields[i], p, n);
      if (!found) return false;
    }
    p = end;
  }
  return true;
}

// A filter: empty takes everything; otherwise the field equals it, whatever the case.
inline bool directoryFilter(const char *wanted, const char *field) {
  if (!wanted || !wanted[0]) return true;
  if (!field) return false;
  for (; *wanted && *field; ++wanted, ++field) {
    if (tolower(static_cast<unsigned char>(*wanted)) != tolower(static_cast<unsigned char>(*field))) return false;
  }
  return !*wanted && !*field;
}

}  // namespace bbw
