#pragma once
// Local time from a POSIX TZ rule, such as "EST5EDT,M3.2.0,M11.1.0" or "<-05>5<-04>,M3.2.0,M11.1.0".
//
// The power schedule is kept in local time. A fixed offset from UTC goes wrong twice a year (seen on
// the board: 8:00 fired at 7:00 for a week after the clocks went back on 1 Nov 2026), so the web page
// sends its time zone as a rule and the controller works out the offset for each moment itself.
//
// Only the "M" form of the change dates is read (month, week, weekday), which is what every zone in
// use today needs and what the web page writes. Pure C++, no libc time zone calls: the board, the
// emulator and the host tests give the same answers.

#include <initializer_list>
#include <stddef.h>
#include <stdint.h>

namespace bbw {

constexpr size_t TZ_RULE_LEN = 48;

struct TzChange {
  uint8_t month = 0;     // 1..12
  uint8_t week = 0;      // 1..5; 5 is the last one in the month
  uint8_t weekday = 0;   // 0 Sunday .. 6 Saturday
  int32_t timeS = 7200;  // local time of day it happens (may be negative or past 24 h)
};

struct TzRule {
  bool valid = false;
  int16_t stdOffsetMin = 0;  // local time minus UTC, standard time
  bool hasDst = false;
  int16_t dstOffsetMin = 0;  // local time minus UTC, daylight saving time
  TzChange start;            // into daylight saving time, in standard local time
  TzChange end;              // back to standard time, in daylight saving local time
};

namespace tzdetail {

inline bool digit(char c) { return c >= '0' && c <= '9'; }

// A zone name: three or more letters, or anything between < and >.
inline bool name(const char *&p) {
  if (*p == '<') {
    const char *q = p + 1;
    while (*q && *q != '>') ++q;
    if (*q != '>' || q - p < 2) return false;
    p = q + 1;
    return true;
  }
  const char *q = p;
  while ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z')) ++q;
  if (q - p < 3) return false;
  p = q;
  return true;
}

// [+-]hh[:mm[:ss]] as seconds; hours up to maxHours.
inline bool hms(const char *&p, int32_t &out, bool sign, int32_t maxHours) {
  int32_t s = 1;
  if (sign && (*p == '+' || *p == '-')) {
    s = *p == '-' ? -1 : 1;
    ++p;
  }
  if (!digit(*p)) return false;
  int32_t h = 0;
  while (digit(*p)) {
    h = h * 10 + (*p - '0');
    if (h > maxHours) return false;
    ++p;
  }
  int32_t m = 0, sec = 0;
  for (int32_t *part : {&m, &sec}) {
    if (*p != ':') break;
    ++p;
    if (!digit(p[0]) || !digit(p[1])) return false;
    *part = (p[0] - '0') * 10 + (p[1] - '0');
    if (*part > 59) return false;
    p += 2;
  }
  out = s * (h * 3600 + m * 60 + sec);
  return true;
}

inline bool number(const char *&p, int lo, int hi, uint8_t &out) {
  if (!digit(*p)) return false;
  int v = 0;
  while (digit(*p)) {
    v = v * 10 + (*p - '0');
    if (v > hi) return false;
    ++p;
  }
  if (v < lo) return false;
  out = static_cast<uint8_t>(v);
  return true;
}

inline bool change(const char *&p, TzChange &c) {
  if (*p != 'M') return false;
  ++p;
  if (!number(p, 1, 12, c.month) || *p++ != '.') return false;
  if (!number(p, 1, 5, c.week) || *p++ != '.') return false;
  if (!number(p, 0, 6, c.weekday)) return false;
  c.timeS = 7200;
  if (*p == '/') {
    ++p;
    if (!hms(p, c.timeS, true, 167)) return false;
  }
  return true;
}

// Days since 1970-01-01 of a civil date (H. Hinnant's algorithm).
inline int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline int64_t yearOfDays(int64_t days) {
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned month = mp < 10 ? mp + 3 : mp - 9;
  return static_cast<int64_t>(yoe) + era * 400 + (month <= 2 ? 1 : 0);
}

inline unsigned daysInMonth(int64_t y, unsigned m) {
  static const uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : DAYS[m - 1];
}

// The UTC second a change happens in year y; offsetMin is the local offset in force just before it.
inline int64_t changeUtc(const TzChange &c, int64_t y, int16_t offsetMin) {
  const int64_t first = daysFromCivil(y, c.month, 1);
  const unsigned firstWeekday = static_cast<unsigned>(((first % 7) + 7 + 4) % 7);  // 1970-01-01: Thursday
  unsigned day = 1 + (c.weekday + 7 - firstWeekday) % 7 + (c.week - 1) * 7u;
  while (day > daysInMonth(y, c.month)) day -= 7;
  return (first + day - 1) * 86400 + c.timeS - static_cast<int64_t>(offsetMin) * 60;
}

}  // namespace tzdetail

// false (and rule.valid false) for anything it cannot read in full.
inline bool parseTzRule(const char *text, TzRule &rule) {
  using namespace tzdetail;
  rule = TzRule();
  if (!text) return false;
  const char *p = text;
  int32_t s = 0;
  if (!name(p) || !hms(p, s, true, 24)) return false;
  TzRule r;
  r.stdOffsetMin = static_cast<int16_t>(-s / 60);  // POSIX counts west as positive
  if (*p == '\0') {
    r.valid = true;
    rule = r;
    return true;
  }
  if (!name(p)) return false;
  r.hasDst = true;
  r.dstOffsetMin = static_cast<int16_t>(r.stdOffsetMin + 60);
  if (*p != ',' && *p != '\0') {
    if (!hms(p, s, true, 24)) return false;
    r.dstOffsetMin = static_cast<int16_t>(-s / 60);
  }
  if (*p != ',') return false;  // a rule without its dates: not one this reads
  ++p;
  if (!change(p, r.start) || *p++ != ',' || !change(p, r.end) || *p != '\0') return false;
  if (r.stdOffsetMin < -14 * 60 || r.stdOffsetMin > 14 * 60 || r.dstOffsetMin < -14 * 60 ||
      r.dstOffsetMin > 14 * 60) {
    return false;
  }
  r.valid = true;
  rule = r;
  return true;
}

inline bool tzIsDst(const TzRule &r, uint32_t epochS) {
  if (!r.valid || !r.hasDst) return false;
  using namespace tzdetail;
  const int64_t t = epochS;
  const int64_t y = yearOfDays((t + r.stdOffsetMin * 60) / 86400);
  const int64_t start = changeUtc(r.start, y, r.stdOffsetMin);
  const int64_t end = changeUtc(r.end, y, r.dstOffsetMin);
  // Northern zones change in, then out, within the year; southern ones the other way round.
  return start < end ? (t >= start && t < end) : (t < end || t >= start);
}

// Local time minus UTC at that moment, in minutes. fallbackMin when the rule is not valid.
inline int16_t tzOffsetMinAt(const TzRule &r, uint32_t epochS, int16_t fallbackMin = 0) {
  if (!r.valid) return fallbackMin;
  return tzIsDst(r, epochS) ? r.dstOffsetMin : r.stdOffsetMin;
}

}  // namespace bbw
