#pragma once
// The Community Hub's links (docs/COMMUNITY.md): videos, posts and guides about espresso that owners
// suggest and a reviewer lists for everyone. A suggestion is kept on this controller with where it
// stands, and goes to the library as a signed `link` record (core/Share.h buildLinkRecord); what
// reviewers listed comes back in index/links.json.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "Types.h"

namespace bbw {

constexpr const char *LINK_KINDS[] = {"video", "playlist", "post", "guide", "article", "podcast"};
constexpr size_t LINK_KIND_COUNT = sizeof(LINK_KINDS) / sizeof(LINK_KINDS[0]);
constexpr const char *LINK_TOPICS[] = {"preparation", "dialing_in", "brewing", "beans",
                                       "gear",        "milk",       "maintenance", "science"};
constexpr size_t LINK_TOPIC_COUNT = sizeof(LINK_TOPICS) / sizeof(LINK_TOPICS[0]);
constexpr uint8_t LINK_TOPICS_MAX = 3;  // topics one link may have
constexpr size_t MAX_LINK_SUGGESTIONS = 24;
constexpr size_t LINK_URL_LEN = 200, LINK_TITLE_LEN = 80, LINK_WHY_LEN = 161, LINK_IMAGE_LEN = 256;

struct LinkSuggestion {
  uint16_t id = 0;
  uint32_t epochS = 0;
  char url[LINK_URL_LEN] = {};
  char title[LINK_TITLE_LEN] = {};
  uint8_t kind = 0;      // LINK_KINDS
  uint8_t topics = 0;    // bits of LINK_TOPICS
  char why[LINK_WHY_LEN] = {};
  char image[LINK_IMAGE_LEN] = {};
  // Where it stands with the library, as for a coffee (core/Share.h ShareState).
  uint8_t shareState = 0;
  uint32_t shareSeq = 0;
  char reviewNote[NOTES_LEN] = {};
  char libraryId[LIBRARY_ID_LEN] = {};
};

inline int linkKindIndex(const char *name) {
  for (size_t i = 0; name && i < LINK_KIND_COUNT; ++i) {
    if (strcmp(name, LINK_KINDS[i]) == 0) return static_cast<int>(i);
  }
  return -1;
}

inline int linkTopicIndex(const char *name) {
  for (size_t i = 0; name && i < LINK_TOPIC_COUNT; ++i) {
    if (strcmp(name, LINK_TOPICS[i]) == 0) return static_cast<int>(i);
  }
  return -1;
}

inline uint8_t topicCount(uint8_t mask) {
  uint8_t n = 0;
  for (; mask; mask &= static_cast<uint8_t>(mask - 1)) ++n;
  return n;
}

// A link the library would take: https (an http link is made https), printable, no spaces, short
// enough, and no query word that looks like a credential. Writes the link as it would be sent.
inline bool cleanLinkUrl(const char *in, char *out, size_t size) {
  if (!in || !out || size < 16) return false;
  const char *rest = nullptr;
  if (strncmp(in, "https://", 8) == 0) rest = in + 8;
  else if (strncmp(in, "http://", 7) == 0) rest = in + 7;
  if (!rest || !*rest) return false;
  const size_t n = strlen(rest);
  if (8 + n >= size || 8 + n > LINK_URL_LEN - 1) return false;
  for (size_t i = 0; i < n; ++i) {
    if (rest[i] <= 0x20 || rest[i] > 0x7e) return false;
  }
  const char *q = strchr(rest, '?');
  if (q) {
    static const char *words[] = {"token", "key=", "sig", "auth", "session", "password"};
    for (const char *w : words) {
      const size_t wl = strlen(w);
      for (const char *p = q; *p; ++p) {
        size_t k = 0;
        while (k < wl && p[k] && (p[k] | 0x20) == w[k]) ++k;
        if (k == wl) return false;
      }
    }
  }
  memcpy(out, "https://", 8);
  memcpy(out + 8, rest, n + 1);
  return true;
}

}  // namespace bbw
