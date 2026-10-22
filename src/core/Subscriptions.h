#pragma once
// Subscriptions: coffee and supplies that come by post (a roaster's coffee every few weeks, paper
// filters). The shops' own subscription systems cannot be reached by a customer's app (Recharge,
// Ordergroove and the like take only the shop's private key), so the controller keeps its own record
// of each: how often it comes, when next, what it costs, how much stock is left. The shop's page is
// where it is changed; the card links there.
//
// Also here: reading a shop page's <head> for a card (its name, picture, icon and colour) and
// guessing from the page which subscription system the shop uses.
//
// Portable and host-tested; no network or flash access in here.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Community.h"

namespace bbw {

constexpr size_t MAX_SUBSCRIPTIONS = 12;

enum class SubKind : uint8_t { COFFEE, SUPPLIES, OTHER };
enum class SubStatus : uint8_t { ACTIVE, PAUSED, CANCELLED };

struct Subscription {
  uint16_t id = 0;
  SubKind kind = SubKind::COFFEE;
  SubStatus status = SubStatus::ACTIVE;
  char name[48] = {};
  char url[200] = {};          // the shop's page, as pasted
  char manageUrl[200] = {};    // where the subscription is changed (suggested, editable)
  uint16_t everyDays = 0;      // 0: not regular
  uint32_t nextDate = 0;       // YYYYMMDD of the next charge or delivery, 0 = not known
  uint32_t priceCents = 0;     // each delivery
  uint32_t lastArrived = 0;    // YYYYMMDD
  char note[96] = {};
  // What the shop's page said (PageMeta below).
  char title[96] = {};
  char site[48] = {};
  char description[200] = {};
  char image[256] = {};
  char icon[256] = {};
  char color[8] = {};          // #rrggbb
  char platform[16] = {};
  // Supplies: what it is, how many are left, how many a shot uses and a delivery brings.
  char item[40] = {};
  int32_t stock = 0;
  uint8_t perShot = 0;         // 0: not used up by shots
  uint16_t perDelivery = 0;
};

inline const char *subKindName(SubKind k) {
  return k == SubKind::SUPPLIES ? "supplies" : k == SubKind::OTHER ? "other" : "coffee";
}
inline SubKind subKindFrom(const char *s) {
  if (s && strcmp(s, "supplies") == 0) return SubKind::SUPPLIES;
  if (s && strcmp(s, "other") == 0) return SubKind::OTHER;
  return SubKind::COFFEE;
}
inline const char *subStatusName(SubStatus s) {
  return s == SubStatus::PAUSED ? "paused" : s == SubStatus::CANCELLED ? "cancelled" : "active";
}
inline SubStatus subStatusFrom(const char *s) {
  if (s && strcmp(s, "paused") == 0) return SubStatus::PAUSED;
  if (s && strcmp(s, "cancelled") == 0) return SubStatus::CANCELLED;
  return SubStatus::ACTIVE;
}

inline void subCopy(char *to, const char *from, size_t size) {
  if (!from) from = "";
  strncpy(to, from, size - 1);
  to[size - 1] = '\0';
}

// ------------------------------------------------------------------ dates --
// YYYYMMDD <-> days since 1970-01-01 (H. Hinnant's civil-date algorithms; daysFromDate is in
// core/Community.h).
inline uint32_t dateFromDays(int32_t z) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int32_t doe = z - era * 146097;
  const int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int32_t mp = (5 * doy + 2) / 153;
  const int32_t d = doy - (153 * mp + 2) / 5 + 1;
  const int32_t m = mp < 10 ? mp + 3 : mp - 9;
  const int32_t y = yoe + era * 400 + (m <= 2);
  return static_cast<uint32_t>(y * 10000 + m * 100 + d);
}
inline bool validSubDate(uint32_t ymd) {
  const uint32_t y = ymd / 10000, m = ymd / 100 % 100, d = ymd % 100;
  return y >= 2000 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31 &&
         dateFromDays(daysFromDate(ymd)) == ymd;
}
inline uint32_t addDays(uint32_t ymd, int32_t n) { return dateFromDays(daysFromDate(ymd) + n); }
// b - a in days.
inline int32_t daysBetween(uint32_t a, uint32_t b) { return daysFromDate(b) - daysFromDate(a); }

// The next date after a delivery came, or one was skipped: one period on from the date it was due
// (or from today, if no date was known), and never today or before. 0 if it is not regular.
inline uint32_t nextDateAfter(const Subscription &s, uint32_t today) {
  if (s.everyDays == 0 || !validSubDate(today)) return validSubDate(s.nextDate) ? s.nextDate : 0;
  uint32_t n = validSubDate(s.nextDate) ? s.nextDate : today;
  n = addDays(n, s.everyDays);
  for (int guard = 0; daysBetween(today, n) <= 0 && guard < 400; ++guard) n = addDays(n, s.everyDays);
  return n;
}

// A delivery arrived: the next date moves on, and supplies are added to the stock.
inline void subscriptionArrived(Subscription &s, uint32_t today) {
  s.nextDate = nextDateAfter(s, today);
  if (validSubDate(today)) s.lastArrived = today;
  if (s.kind == SubKind::SUPPLIES) s.stock += s.perDelivery;
}

// What the active subscriptions cost in an average month (30.44 days), in cents.
inline uint32_t monthlyCents(const Subscription *subs, size_t count) {
  double total = 0;
  for (size_t i = 0; i < count; ++i) {
    if (subs[i].status != SubStatus::ACTIVE || subs[i].everyDays == 0) continue;
    total += static_cast<double>(subs[i].priceCents) * 30.4375 / subs[i].everyDays;
  }
  return static_cast<uint32_t>(total + 0.5);
}

// Days until the next date (negative: overdue); INT32_MIN if not known.
inline int32_t daysUntilNext(const Subscription &s, uint32_t today) {
  if (!validSubDate(s.nextDate) || !validSubDate(today)) return INT32_MIN;
  return daysBetween(today, s.nextDate);
}

// A reminder is shown this many days before an active subscription's next date.
constexpr int32_t SUB_REMIND_DAYS = 3;
inline bool subscriptionDueSoon(const Subscription &s, uint32_t today) {
  const int32_t d = daysUntilNext(s, today);
  return s.status == SubStatus::ACTIVE && d != INT32_MIN && d <= SUB_REMIND_DAYS;
}

// Supplies used up by shots: one shot saved takes perShot off the stock (never below 0).
inline bool subscriptionShotTaken(Subscription &s) {
  if (s.kind != SubKind::SUPPLIES || s.perShot == 0 || s.stock <= 0) return false;
  s.stock = s.stock > s.perShot ? s.stock - s.perShot : 0;
  return true;
}

// Days the stock lasts at `shotsPerDay`; -1 if it is not used up (no shots, or not per shot).
inline int32_t stockDaysLeft(const Subscription &s, float shotsPerDay) {
  if (s.kind != SubKind::SUPPLIES || s.perShot == 0 || !(shotsPerDay > 0.0f)) return -1;
  return static_cast<int32_t>(static_cast<float>(s.stock) / (shotsPerDay * s.perShot));
}

// The stock runs out before the next delivery comes.
inline bool stockShort(const Subscription &s, float shotsPerDay, uint32_t today) {
  const int32_t left = stockDaysLeft(s, shotsPerDay);
  if (left < 0) return false;
  const int32_t next = daysUntilNext(s, today);
  if (s.status != SubStatus::ACTIVE || next == INT32_MIN) return left <= 7;
  return left < next;
}

// Shots a day over the last `days` days, from their times (seconds since 1970).
inline float shotsPerDay(const uint32_t *epochs, size_t count, uint32_t nowS, uint32_t days = 14) {
  if (days == 0 || nowS == 0) return 0.0f;
  const uint32_t since = nowS > days * 86400u ? nowS - days * 86400u : 0;
  size_t n = 0;
  for (size_t i = 0; i < count; ++i) n += epochs[i] >= since && epochs[i] <= nowS;
  return static_cast<float>(n) / static_cast<float>(days);
}

inline void sanitizeSubscription(Subscription &s, uint32_t today = 0) {
  (void)today;
  s.name[sizeof(s.name) - 1] = s.url[sizeof(s.url) - 1] = s.manageUrl[sizeof(s.manageUrl) - 1] = '\0';
  s.note[sizeof(s.note) - 1] = s.title[sizeof(s.title) - 1] = s.site[sizeof(s.site) - 1] = '\0';
  s.description[sizeof(s.description) - 1] = s.image[sizeof(s.image) - 1] = '\0';
  s.icon[sizeof(s.icon) - 1] = s.color[sizeof(s.color) - 1] = s.platform[sizeof(s.platform) - 1] = '\0';
  s.item[sizeof(s.item) - 1] = '\0';
  if (s.everyDays > 366) s.everyDays = 366;
  if (s.nextDate && !validSubDate(s.nextDate)) s.nextDate = 0;
  if (s.lastArrived && !validSubDate(s.lastArrived)) s.lastArrived = 0;
  if (s.stock < 0) s.stock = 0;
  if (s.stock > 100000) s.stock = 100000;
  if (s.perShot > 10) s.perShot = 10;
  if (s.priceCents > 10000000) s.priceCents = 10000000;
  if (s.kind > SubKind::OTHER) s.kind = SubKind::OTHER;
  if (s.status > SubStatus::CANCELLED) s.status = SubStatus::ACTIVE;
  // Only web links: a card never opens anything else.
  const auto web = [](const char *u) {
    return strncmp(u, "https://", 8) == 0 || strncmp(u, "http://", 7) == 0;
  };
  if (s.url[0] && !web(s.url)) s.url[0] = '\0';
  if (s.manageUrl[0] && !web(s.manageUrl)) s.manageUrl[0] = '\0';
  if (s.image[0] && !web(s.image)) s.image[0] = '\0';
  if (s.icon[0] && !web(s.icon)) s.icon[0] = '\0';
  bool hex = s.color[0] == '#' && strlen(s.color) == 7;
  for (int i = 1; hex && i < 7; ++i) {
    const char c = s.color[i];
    hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  }
  if (!hex) s.color[0] = '\0';
}

// ---------------------------------------------------------- page preview --
// What a shop's page says about itself, from its <head>: Open Graph tags, the icon, the theme colour,
// and which subscription system the page loads.
struct PageMeta {
  char title[96] = {};
  char site[48] = {};
  char description[200] = {};
  char image[256] = {};
  char icon[256] = {};
  char color[8] = {};
  char platform[16] = {};    // recharge, ordergroove, skio, loop, appstle, bold, shopify; "" = not known
  char manageUrl[200] = {};  // the suggested place to change the subscription
};

// The scheme and host of a URL ("https://shop.example"); "" if it is not a web link.
inline void urlOrigin(const char *url, char *out, size_t size) {
  out[0] = '\0';
  const char *p = strstr(url, "://");
  if (!p || (strncmp(url, "https", 5) != 0 && strncmp(url, "http", 4) != 0)) return;
  p += 3;
  const char *end = p;
  while (*end && *end != '/' && *end != '?' && *end != '#') ++end;
  const size_t n = static_cast<size_t>(end - url);
  if (n + 1 > size) return;
  memcpy(out, url, n);
  out[n] = '\0';
}

// `ref` as found in the page, made absolute against the page's URL.
inline void resolveUrl(const char *base, const char *ref, char *out, size_t size) {
  out[0] = '\0';
  if (!ref || !ref[0]) return;
  if (strncmp(ref, "https://", 8) == 0 || strncmp(ref, "http://", 7) == 0) {
    subCopy(out, ref, size);
  } else if (strcspn(ref, ":/?#") < strlen(ref) && ref[strcspn(ref, ":/?#")] == ':') {
    return;  // another scheme (javascript:, data:, mailto:): not a web link
  } else if (ref[0] == '/' && ref[1] == '/') {
    snprintf(out, size, "%s%s", strncmp(base, "http://", 7) == 0 ? "http:" : "https:", ref);
  } else {
    char origin[160];
    urlOrigin(base, origin, sizeof(origin));
    if (!origin[0]) return;
    if (ref[0] == '/') {
      snprintf(out, size, "%s%s", origin, ref);
    } else {
      // Relative to the page's folder.
      const char *path = base + strlen(origin);
      const char *slash = strrchr(path, '/');
      const size_t keep = slash ? static_cast<size_t>(slash - path) + 1 : 0;
      snprintf(out, size, "%s%.*s%s%s", origin, static_cast<int>(keep), path, keep ? "" : "/", ref);
    }
  }
  if (strlen(out) + 1 >= size) out[0] = '\0';  // cut short: not a usable link
}

namespace detail {

inline char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// Case-insensitive search in [p, end).
inline const char *findNoCase(const char *p, const char *end, const char *what) {
  const size_t n = strlen(what);
  for (; p + n <= end; ++p) {
    size_t i = 0;
    while (i < n && lower(p[i]) == lower(what[i])) ++i;
    if (i == n) return p;
  }
  return nullptr;
}

// Text with the common entities decoded and runs of white space made one space.
inline void decodeText(const char *p, const char *end, char *out, size_t size) {
  size_t n = 0;
  bool space = true;  // leading space dropped
  while (p < end && n + 1 < size) {
    char c = *p++;
    if (c == '&') {
      const char *semi = p;
      while (semi < end && semi - p < 8 && *semi != ';') ++semi;
      if (semi < end && *semi == ';') {
        const size_t len = static_cast<size_t>(semi - p);
        long code = -1;
        if (len >= 2 && p[0] == '#') {
          code = (p[1] == 'x' || p[1] == 'X') ? strtol(p + 2, nullptr, 16) : strtol(p + 1, nullptr, 10);
        } else if (len == 3 && strncmp(p, "amp", 3) == 0) code = '&';
        else if (len == 4 && strncmp(p, "quot", 4) == 0) code = '"';
        else if (len == 4 && strncmp(p, "apos", 4) == 0) code = '\'';
        else if (len == 2 && strncmp(p, "lt", 2) == 0) code = '<';
        else if (len == 2 && strncmp(p, "gt", 2) == 0) code = '>';
        else if (len == 4 && strncmp(p, "nbsp", 4) == 0) code = ' ';
        if (code > 0 && code < 128) {
          c = static_cast<char>(code);
          p = semi + 1;
        } else if (code >= 128 && code < 0x10000 && n + 4 < size) {
          // UTF-8, two or three bytes.
          if (code < 0x800) {
            out[n++] = static_cast<char>(0xC0 | (code >> 6));
            out[n++] = static_cast<char>(0x80 | (code & 0x3F));
          } else {
            out[n++] = static_cast<char>(0xE0 | (code >> 12));
            out[n++] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out[n++] = static_cast<char>(0x80 | (code & 0x3F));
          }
          p = semi + 1;
          space = false;
          continue;
        }
      }
    }
    const bool ws = c == ' ' || c == '\n' || c == '\r' || c == '\t';
    if (ws) {
      if (!space) out[n++] = ' ';
      space = true;
    } else {
      out[n++] = c;
      space = false;
    }
  }
  while (n > 0 && out[n - 1] == ' ') --n;
  // Never end inside a UTF-8 character.
  size_t k = n;
  while (k > 0 && (static_cast<unsigned char>(out[k - 1]) & 0xC0) == 0x80) --k;
  if (k > 0 && (static_cast<unsigned char>(out[k - 1]) & 0x80)) {
    const unsigned char lead = static_cast<unsigned char>(out[k - 1]);
    const size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
    if (n - (k - 1) < want) n = k - 1;
  }
  out[n] = '\0';
}

// The value of attribute `name` in the tag [tag, end); false if it has none.
inline bool attr(const char *tag, const char *end, const char *name, const char *&from, const char *&to) {
  const size_t n = strlen(name);
  for (const char *p = tag + 1; p + n < end; ++p) {
    if (!(p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r')) continue;
    size_t i = 0;
    while (i < n && lower(p[i]) == lower(name[i])) ++i;
    if (i < n) continue;
    const char *q = p + n;
    while (q < end && *q == ' ') ++q;
    if (q >= end || *q != '=') continue;
    ++q;
    while (q < end && *q == ' ') ++q;
    if (q < end && (*q == '"' || *q == '\'')) {
      const char quote = *q++;
      const char *e = q;
      while (e < end && *e != quote) ++e;
      from = q;
      to = e;
    } else {
      const char *e = q;
      while (e < end && *e != ' ' && *e != '>' && *e != '/') ++e;
      from = q;
      to = e;
    }
    return true;
  }
  return false;
}

inline bool attrIs(const char *tag, const char *end, const char *name, const char *value) {
  const char *f, *t;
  if (!attr(tag, end, name, f, t)) return false;
  const size_t n = strlen(value);
  if (static_cast<size_t>(t - f) != n) return false;
  for (size_t i = 0; i < n; ++i) if (lower(f[i]) != lower(value[i])) return false;
  return true;
}

inline bool attrHasWord(const char *tag, const char *end, const char *name, const char *word) {
  const char *f, *t;
  if (!attr(tag, end, name, f, t)) return false;
  const char *hit = findNoCase(f, t, word);
  if (!hit) return false;
  const char *after = hit + strlen(word);
  return (hit == f || hit[-1] == ' ') && (after == t || *after == ' ');
}

}  // namespace detail

// Which subscription system a shop's page loads, and where it says a customer manages theirs.
inline void guessPlatform(const char *html, size_t length, const char *pageUrl, PageMeta &m) {
  using detail::findNoCase;
  const char *end = html + length;
  struct Mark { const char *marker; const char *platform; const char *path; };
  // Recharge's customer portal answers on every shop at this path; the others live in the shop's
  // account pages.
  static const Mark marks[] = {
      {"rechargecdn", "recharge", "/tools/recurring/login"},
      {"rechargeapps", "recharge", "/tools/recurring/login"},
      {"ordergroove", "ordergroove", "/account"},
      {"skio", "skio", "/account"},
      {"loopsubscriptions", "loop", "/account"},
      {"appstle", "appstle", "/account"},
      {"boldapps", "bold", "/account"},
      {"cdn.shopify", "shopify", "/account"},
      {"Shopify.shop", "shopify", "/account"},
  };
  m.platform[0] = '\0';
  for (const Mark &k : marks) {
    if (!findNoCase(html, end, k.marker)) continue;
    subCopy(m.platform, k.platform, sizeof(m.platform));
    char origin[160];
    urlOrigin(pageUrl, origin, sizeof(origin));
    if (origin[0]) snprintf(m.manageUrl, sizeof(m.manageUrl), "%s%s", origin, k.path);
    return;
  }
}

// Reads the page's <head> (or as much of the page as was fetched) for a card. Anything not found
// stays "". The page is untrusted text: everything is copied as text, links only as web links.
inline void parsePageHead(const char *html, size_t length, const char *pageUrl, PageMeta &m) {
  using namespace detail;
  m = PageMeta{};
  const char *end = html + length;
  const char *headEnd = findNoCase(html, end, "</head");
  const char *stop = headEnd ? headEnd : end;
  char raw[300];
  char ogTitle[96] = {}, plainTitle[96] = {}, appleIcon[256] = {}, plainIcon[256] = {};
  for (const char *p = html; p < stop;) {
    const char *lt = static_cast<const char *>(memchr(p, '<', static_cast<size_t>(stop - p)));
    if (!lt) break;
    const char *gt = static_cast<const char *>(memchr(lt, '>', static_cast<size_t>(stop - lt)));
    if (!gt) break;
    const char *f, *t;
    if (findNoCase(lt, lt + 6 < gt ? lt + 6 : gt, "<meta ") == lt) {
      const bool hasContent = attr(lt, gt, "content", f, t);
      if (hasContent) {
        decodeText(f, t, raw, sizeof(raw));
        if (attrIs(lt, gt, "property", "og:title") || attrIs(lt, gt, "name", "og:title")) {
          subCopy(ogTitle, raw, sizeof(ogTitle));
        } else if (attrIs(lt, gt, "property", "og:site_name")) {
          subCopy(m.site, raw, sizeof(m.site));
        } else if (attrIs(lt, gt, "property", "og:description") ||
                   (!m.description[0] && attrIs(lt, gt, "name", "description"))) {
          subCopy(m.description, raw, sizeof(m.description));
        } else if (attrIs(lt, gt, "property", "og:image") ||
                   attrIs(lt, gt, "property", "og:image:secure_url") ||
                   (!m.image[0] && attrIs(lt, gt, "name", "twitter:image"))) {
          if (!m.image[0] || attrIs(lt, gt, "property", "og:image:secure_url")) {
            resolveUrl(pageUrl, raw, m.image, sizeof(m.image));
          }
        } else if (attrIs(lt, gt, "name", "theme-color")) {
          if (raw[0] == '#' && strlen(raw) == 7) subCopy(m.color, raw, sizeof(m.color));
        }
      }
    } else if (findNoCase(lt, lt + 6 < gt ? lt + 6 : gt, "<link ") == lt && attr(lt, gt, "href", f, t)) {
      decodeText(f, t, raw, sizeof(raw));
      if (attrHasWord(lt, gt, "rel", "apple-touch-icon")) {
        if (!appleIcon[0]) resolveUrl(pageUrl, raw, appleIcon, sizeof(appleIcon));
      } else if (attrHasWord(lt, gt, "rel", "icon")) {
        if (!plainIcon[0]) resolveUrl(pageUrl, raw, plainIcon, sizeof(plainIcon));
      }
    } else if (findNoCase(lt, lt + 6 < gt ? lt + 6 : gt, "<title") == lt && !plainTitle[0]) {
      const char *close = findNoCase(gt, stop, "</title");
      if (close) decodeText(gt + 1, close, plainTitle, sizeof(plainTitle));
      p = close ? close : gt + 1;
      continue;
    }
    p = gt + 1;
  }
  subCopy(m.title, ogTitle[0] ? ogTitle : plainTitle, sizeof(m.title));
  subCopy(m.icon, appleIcon[0] ? appleIcon : plainIcon, sizeof(m.icon));
  if (!m.site[0]) {
    // The host, without www.
    char origin[160];
    urlOrigin(pageUrl, origin, sizeof(origin));
    const char *host = strstr(origin, "://");
    if (host) {
      host += 3;
      if (strncmp(host, "www.", 4) == 0) host += 4;
      subCopy(m.site, host, sizeof(m.site));
    }
  }
  guessPlatform(html, length, pageUrl, m);
}

}  // namespace bbw
