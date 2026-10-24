#pragma once
// Diagnostics reports for controllers we cannot reach (docs/TELEMETRY.md): what goes into them,
// how log lines are cleaned of names and addresses first, and the signed envelope.
//
// Portable and host-tested: no Arduino, file or network access in here. The firmware fills a
// DiagInput from what it already keeps (link counters, the shot history, power events) and the
// warning tally is fed the log's W and E lines as they appear.

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/PowerControl.h"
#include "core/Share.h"
#include "core/Types.h"

namespace bbw {

constexpr size_t DIAG_BODY_MAX = 8192;
constexpr size_t DIAG_ENVELOPE_MAX = 2 * DIAG_BODY_MAX + 512;  // every character escaped, at worst
constexpr size_t DIAG_LINE_MAX = 160;     // a cleaned log line
constexpr size_t DIAG_PATTERN_MAX = 120;  // a warning's pattern
constexpr uint8_t DIAG_PATTERNS = 32;
constexpr uint8_t DIAG_RECENT = 30;
constexpr uint8_t DIAG_CRASH_LINES = 60;
constexpr uint32_t DIAG_PERIOD_S = 86400;

// ------------------------------------------------------------------ cleaning --

// What a line must not carry, and what takes its place: the paired devices' addresses (by role),
// and words such as the Wi-Fi name, the access point's name, coffee, roaster and drinker names.
struct ScrubList {
  struct Pair {
    char from[40];
    char to[16];
  };
  Pair pairs[96];  // 24 coffees and their roasters, the drinkers, the network, the devices
  uint8_t count = 0;

  void add(const char *from, const char *to) {
    if (!from || strlen(from) < 3 || count >= 96) return;
    for (uint8_t i = 0; i < count; ++i) {
      if (strcasecmp(pairs[i].from, from) == 0) return;
    }
    strncpy(pairs[count].from, from, sizeof(pairs[count].from) - 1);
    pairs[count].from[sizeof(pairs[count].from) - 1] = '\0';
    strncpy(pairs[count].to, to, sizeof(pairs[count].to) - 1);
    pairs[count].to[sizeof(pairs[count].to) - 1] = '\0';
    ++count;
  }
};

namespace diag_detail {

inline bool hexDigit(char c) { return isxdigit(static_cast<unsigned char>(c)) != 0; }

// xx:xx:xx:xx:xx:xx (or with '-') at s: its length (17), else 0.
inline size_t macAt(const char *s) {
  for (int g = 0; g < 6; ++g) {
    if (!hexDigit(s[g * 3]) || !hexDigit(s[g * 3 + 1])) return 0;
    if (g < 5 && s[g * 3 + 2] != ':' && s[g * 3 + 2] != '-') return 0;
  }
  return hexDigit(s[17]) ? 0 : 17;
}

// A dotted IPv4 address at s (four groups of 1 to 3 digits): its length, else 0.
inline size_t ipAt(const char *s) {
  size_t at = 0;
  for (int g = 0; g < 4; ++g) {
    size_t digits = 0;
    while (isdigit(static_cast<unsigned char>(s[at])) && digits < 4) ++at, ++digits;
    if (digits == 0 || digits > 3) return 0;
    if (g < 3) {
      if (s[at] != '.') return 0;
      ++at;
    }
  }
  return isdigit(static_cast<unsigned char>(s[at])) || (s[at] == '.' && isdigit(static_cast<unsigned char>(s[at + 1])))
             ? 0 : at;
}

// A long run of letters and digits with both in it (a token, a key, a serial): its length, else 0.
inline size_t tokenAt(const char *s) {
  size_t n = 0;
  bool letter = false, digit = false;
  while (isalnum(static_cast<unsigned char>(s[n])) || s[n] == '_' || s[n] == '-') {
    if (isdigit(static_cast<unsigned char>(s[n]))) digit = true;
    else if (isalpha(static_cast<unsigned char>(s[n]))) letter = true;
    ++n;
  }
  return n >= 24 && letter && digit ? n : 0;
}

inline bool wordChar(char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_'; }

}  // namespace diag_detail

// The line with every address, listed word and token replaced. Always terminated; cut at `size`.
inline size_t scrubLine(const char *in, char *out, size_t size, const ScrubList *list) {
  using namespace diag_detail;
  if (!out || size == 0) return 0;
  size_t n = 0;
  auto put = [&](const char *s, size_t len) {
    for (size_t i = 0; i < len && n + 1 < size; ++i) out[n++] = s[i];
  };
  for (size_t i = 0; in && in[i] && n + 1 < size;) {
    const bool atWordStart = i == 0 || !wordChar(in[i - 1]);
    size_t len;
    if (atWordStart && (len = macAt(in + i)) != 0) {
      const char *role = "<mac>";
      if (list) {
        for (uint8_t k = 0; k < list->count; ++k) {
          if (strncasecmp(in + i, list->pairs[k].from, len) == 0 && strlen(list->pairs[k].from) == len) {
            role = list->pairs[k].to;
            break;
          }
        }
      }
      put(role, strlen(role));
      i += len;
      continue;
    }
    if (atWordStart && (len = ipAt(in + i)) != 0) {
      put("<ip>", 4);
      i += len;
      continue;
    }
    bool replaced = false;
    if (list) {
      for (uint8_t k = 0; k < list->count && !replaced; ++k) {
        const size_t fl = strlen(list->pairs[k].from);
        if (strncasecmp(in + i, list->pairs[k].from, fl) == 0) {
          put(list->pairs[k].to, strlen(list->pairs[k].to));
          i += fl;
          replaced = true;
        }
      }
    }
    if (replaced) continue;
    if (atWordStart && (len = tokenAt(in + i)) != 0) {
      put("<token>", 7);
      i += len;
      continue;
    }
    out[n++] = in[i++];
  }
  out[n] = '\0';
  return n;
}

// A cleaned warning line as a pattern: without its time stamp and level, numbers as '#'.
inline void warningPattern(const char *cleaned, char *out, size_t size) {
  const char *s = cleaned;
  if (*s == '[') {
    const char *close = strchr(s, ']');
    if (close) s = close + 1;
  }
  while (*s == ' ') ++s;
  if ((s[0] == 'W' || s[0] == 'E') && s[1] == ' ') s += 2;
  size_t n = 0;
  while (*s && n + 1 < size) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && diag_detail::hexDigit(s[2])) {
      out[n++] = '#';
      s += 2;
      while (diag_detail::hexDigit(*s)) ++s;
    } else if (isdigit(static_cast<unsigned char>(*s)) || (*s == '-' && isdigit(static_cast<unsigned char>(s[1])) &&
                                                            (n == 0 || out[n - 1] == ' '))) {
      out[n++] = '#';
      ++s;
      while (isdigit(static_cast<unsigned char>(*s)) || *s == '.') ++s;
    } else {
      out[n++] = *s++;
    }
  }
  while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) --n;
  out[n] = '\0';
}

// ------------------------------------------------------------- the warnings --

// The W and E lines since the last report: each distinct pattern once with its count, and the
// last DIAG_RECENT lines as they were (cleaned). Plain data: saved and loaded as it is.
struct WarnTally {
  static constexpr uint32_t MAGIC = 0x57415231;  // "WAR1"
  struct Pattern {
    uint32_t hash;
    uint32_t count;
    char text[DIAG_PATTERN_MAX];
  };
  uint32_t magic = MAGIC;
  uint32_t other = 0;  // lines whose pattern found no room
  Pattern patterns[DIAG_PATTERNS];
  char recent[DIAG_RECENT][DIAG_LINE_MAX];
  uint8_t patternCount = 0;
  uint8_t recentNext = 0;
  uint8_t recentCount = 0;

  void clear() {
    other = 0;
    patternCount = recentNext = recentCount = 0;
  }
  bool valid() const {
    return magic == MAGIC && patternCount <= DIAG_PATTERNS && recentCount <= DIAG_RECENT && recentNext < DIAG_RECENT;
  }

  static uint32_t hashOf(const char *s) {
    uint32_t h = 2166136261u;  // FNV-1a
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
  }

  // One W or E line, already cleaned.
  void add(const char *cleaned) {
    strncpy(recent[recentNext], cleaned, DIAG_LINE_MAX - 1);
    recent[recentNext][DIAG_LINE_MAX - 1] = '\0';
    size_t len = strlen(recent[recentNext]);
    while (len > 0 && (recent[recentNext][len - 1] == '\n' || recent[recentNext][len - 1] == '\r')) {
      recent[recentNext][--len] = '\0';
    }
    recentNext = static_cast<uint8_t>((recentNext + 1) % DIAG_RECENT);
    if (recentCount < DIAG_RECENT) ++recentCount;

    char pattern[DIAG_PATTERN_MAX];
    warningPattern(cleaned, pattern, sizeof(pattern));
    const uint32_t h = hashOf(pattern);
    for (uint8_t i = 0; i < patternCount; ++i) {
      if (patterns[i].hash == h && strcmp(patterns[i].text, pattern) == 0) {
        ++patterns[i].count;
        return;
      }
    }
    if (patternCount >= DIAG_PATTERNS) {
      ++other;
      return;
    }
    Pattern &p = patterns[patternCount++];
    p.hash = h;
    p.count = 1;
    strncpy(p.text, pattern, sizeof(p.text) - 1);
    p.text[sizeof(p.text) - 1] = '\0';
  }

  // The i-th most recent line, 0 the oldest still held.
  const char *recentLine(uint8_t i) const {
    const uint8_t start = recentCount < DIAG_RECENT ? 0 : recentNext;
    return recent[(start + i) % DIAG_RECENT];
  }
};

// ---------------------------------------------------------------- the inputs --

struct DiagLink {
  bool used = false;     // a device is paired in this role
  uint32_t attempts = 0;
  uint32_t failures = 0;
  uint32_t connects = 0;
  uint32_t longestWaitMs = 0;
};

struct DiagInput {
  bool emulator = false;  // made by the PC emulator: its shots are simulated
  const char *firmware = "";
  uint32_t atS = 0;      // UTC seconds now
  uint32_t sinceS = 0;   // start of the period covered
  uint32_t bootCount = 0;
  uint32_t uptimeS = 0;
  const char *slot = "";
  const char *resets[8] = {};
  uint8_t resetCount = 0;

  const char *scaleDriver = "";  // "acaia", "felicita", ...; "" when none is paired
  char scaleName[24] = {};       // advertised name, its serial cut off: scaleModelName()
  struct Bot {
    const char *role;  // "brew", "power", "stop"
    const char *kind;  // "SwitchBot Bot", ...
  } bots[3] = {};
  uint8_t botCount = 0;
  const char *machineKey = "";
  const char *machineStyle = "";

  DiagLink scale, brew, power, stop;
  uint32_t frames = 0, badFrames = 0, droppedFrames = 0;

  // Shots in the period (ShotRecord, newest first, as the history hands them out).
  uint32_t shotCount = 0;
  uint16_t endCounts[16] = {};  // by EndReason
  uint32_t stopRetried = 0;
  uint16_t latencies[64] = {};  // stop latency, ms
  uint8_t latencyCount = 0;
  float overshoots[64] = {};    // final - target, g
  uint8_t overshootCount = 0;

  uint16_t powerOutcomes[9] = {};  // by PowerControl::Outcome

  uint32_t heapLowest = 0;
  uint32_t stackLoop = 0, stackControl = 0, stackBle = 0;
  int8_t rssiMin = 0, rssiMax = 0;  // 0, 0: not on the home network

  uint8_t scaleRestMin = 0;
  bool requireCup = false;
  bool powerWithScale = false;
  float learningFactor = 0.0f;

  // One shot of the period.
  void addShot(const ShotRecord &r) {
    ++shotCount;
    if (r.endReason < 16) ++endCounts[r.endReason];
    if (r.flags & SHOT_FLAG_STOP_RETRIED) ++stopRetried;
    if (r.stopLatencyMs > 0 && latencyCount < 64) latencies[latencyCount++] = r.stopLatencyMs;
    if ((r.flags & SHOT_FLAG_FINAL_VALID) && r.targetWeightG > 0.0f && overshootCount < 64) {
      overshoots[overshootCount++] = r.finalWeightG - r.targetWeightG;
    }
  }
  void addPower(const PowerEvent &e) {
    if (e.outcome < 9) ++powerOutcomes[e.outcome];
  }
  void addReset(const char *reason) {
    if (!reason || !reason[0]) return;
    for (uint8_t i = 0; i < resetCount; ++i) {
      if (strcmp(resets[i], reason) == 0) return;
    }
    if (resetCount < 8) resets[resetCount++] = reason;
  }
};

// A scale's advertised name without what tells one scale from another: "LUNAR-9E4079" -> "LUNAR".
inline void scaleModelName(const char *advertised, char *out, size_t size) {
  if (!out || size == 0) return;
  out[0] = '\0';
  if (!advertised) return;
  size_t n = 0;
  for (; advertised[n] && n + 1 < size; ++n) {
    const char c = advertised[n];
    if (c == '-' || c == '_' || c == ' ' || c == '#') break;
    out[n] = c;
  }
  out[n] = '\0';
  // A name that ends in a long run of hex digits (a serial with no separator): cut that too.
  size_t hex = 0;
  while (hex < n && diag_detail::hexDigit(out[n - 1 - hex]) && isdigit(static_cast<unsigned char>(out[n - 1 - hex]))) ++hex;
  if (hex >= 4 && hex < n) out[n - hex] = '\0';
}

namespace diag_detail {

template <typename T>
inline void sortSmall(T *v, uint8_t n) {
  for (uint8_t i = 1; i < n; ++i) {
    T x = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > x) {
      v[j + 1] = v[j];
      --j;
    }
    v[j + 1] = x;
  }
}

inline const char *powerOutcomeKey(uint8_t o) {
  static const char *const KEYS[] = {"none", "done", "already", "no_bot", "not_known", "busy",
                                     "bot_unreachable", "machine_busy", "press_failed"};
  return o < 9 ? KEYS[o] : "other";
}

inline void link(JsonOut &j, const char *key, const DiagLink &l) {
  if (!l.used) return;
  j.open(key);
  j.integer("attempts", l.attempts);
  j.integer("failures", l.failures);
  j.integer("connects", l.connects);
  j.integer("longest_wait_ms", l.longestWaitMs);
  j.close();
}

// "end":{"weight":2,...} with lower-case EndReason names.
inline void lowerName(const char *in, char *out, size_t size) {
  size_t n = 0;
  for (; in[n] && n + 1 < size; ++n) out[n] = static_cast<char>(tolower(static_cast<unsigned char>(in[n])));
  out[n] = '\0';
}

}  // namespace diag_detail

// --------------------------------------------------------------- the reports --

// The daily summary's body. Returns its length; 0 if it did not fit (the warnings are what is
// trimmed first: the caller may try again with fewer recent lines).
inline size_t buildDailyReport(const DiagInput &in, const WarnTally &w, char *out, size_t size,
                               uint8_t recentLines = DIAG_RECENT) {
  using namespace diag_detail;
  JsonOut j(out, size);
  j.open();
  j.text("t", "diag_daily");
  j.integer("v", 1);
  if (in.emulator) j.integer("emu", 1);
  j.text("fw", in.firmware);
  j.integer("at", static_cast<long>(in.atS));
  j.integer("since", static_cast<long>(in.sinceS));

  j.open("boot");
  j.integer("count", static_cast<long>(in.bootCount));
  j.integer("uptime_s", static_cast<long>(in.uptimeS));
  j.textIf("slot", in.slot);
  j.openArray("resets");
  for (uint8_t i = 0; i < in.resetCount; ++i) j.item(in.resets[i]);
  j.closeArray();
  j.close();

  j.open("hw");
  if (in.scaleDriver && in.scaleDriver[0]) {
    j.open("scale");
    j.text("driver", in.scaleDriver);
    j.textIf("name", in.scaleName);
    j.close();
  }
  j.openArray("bots");
  for (uint8_t i = 0; i < in.botCount; ++i) {
    j.open(nullptr);
    j.text("role", in.bots[i].role);
    j.text("kind", in.bots[i].kind);
    j.close();
  }
  j.closeArray();
  j.open("machine");
  j.textIf("key", in.machineKey);
  j.textIf("style", in.machineStyle);
  j.close();
  j.close();

  j.open("links");
  link(j, "scale", in.scale);
  link(j, "brew", in.brew);
  link(j, "power", in.power);
  link(j, "stop", in.stop);
  j.open("scale_frames");
  j.integer("frames", static_cast<long>(in.frames));
  j.integer("bad", static_cast<long>(in.badFrames));
  j.integer("dropped", static_cast<long>(in.droppedFrames));
  j.close();
  j.close();

  j.open("shots");
  j.integer("count", static_cast<long>(in.shotCount));
  j.open("end");
  for (uint8_t r = 0; r < 16; ++r) {
    if (!in.endCounts[r]) continue;
    char key[24];
    lowerName(endReasonName(static_cast<EndReason>(r)), key, sizeof(key));
    j.integer(key, in.endCounts[r]);
  }
  j.close();
  j.integer("stop_retried", static_cast<long>(in.stopRetried));
  if (in.latencyCount) {
    uint16_t v[64];
    memcpy(v, in.latencies, sizeof(v));
    sortSmall(v, in.latencyCount);
    j.open("stop_latency_ms");
    j.integer("median", v[in.latencyCount / 2]);
    j.integer("max", v[in.latencyCount - 1]);
    j.close();
  }
  if (in.overshootCount) {
    float v[64];
    memcpy(v, in.overshoots, sizeof(v));
    sortSmall(v, in.overshootCount);
    j.open("overshoot_g");
    j.number("median", v[in.overshootCount / 2], 1);
    j.number("max", v[in.overshootCount - 1], 1);
    j.close();
  }
  j.close();

  j.open("power");
  for (uint8_t o = 1; o < 9; ++o) {
    if (in.powerOutcomes[o]) j.integer(powerOutcomeKey(o), in.powerOutcomes[o]);
  }
  j.close();

  j.open("health");
  j.integer("heap_lowest", static_cast<long>(in.heapLowest));
  j.open("stack_left");
  j.integer("loop", static_cast<long>(in.stackLoop));
  j.integer("control", static_cast<long>(in.stackControl));
  j.integer("ble", static_cast<long>(in.stackBle));
  j.close();
  if (in.rssiMin || in.rssiMax) {
    j.open("rssi");
    j.integer("min", in.rssiMin);
    j.integer("max", in.rssiMax);
    j.close();
  }
  j.close();

  j.open("settings");
  j.integer("scale_rest_min", in.scaleRestMin);
  j.text("require_cup", in.requireCup ? "yes" : "no");
  j.text("power_with_scale", in.powerWithScale ? "yes" : "no");
  j.number("learning_factor", in.learningFactor, 2);
  j.close();

  // Most frequent first.
  uint8_t order[DIAG_PATTERNS];
  for (uint8_t i = 0; i < w.patternCount; ++i) order[i] = i;
  for (uint8_t i = 1; i < w.patternCount; ++i) {
    const uint8_t x = order[i];
    int k = i - 1;
    while (k >= 0 && w.patterns[order[k]].count < w.patterns[x].count) {
      order[k + 1] = order[k];
      --k;
    }
    order[k + 1] = x;
  }
  j.openArray("warnings");
  for (uint8_t i = 0; i < w.patternCount; ++i) {
    j.open(nullptr);
    j.integer("n", static_cast<long>(w.patterns[order[i]].count));
    j.text("p", w.patterns[order[i]].text);
    j.close();
  }
  j.closeArray();
  if (w.other) j.integer("warnings_other", static_cast<long>(w.other));
  j.openArray("recent");
  const uint8_t from = w.recentCount > recentLines ? w.recentCount - recentLines : 0;
  for (uint8_t i = from; i < w.recentCount; ++i) j.item(w.recentLine(i));
  j.closeArray();
  j.close();
  return j.ok() ? j.length() : 0;
}

struct CrashInput {
  bool emulator = false;
  const char *firmware = "";
  uint32_t atS = 0;
  const char *reset = "";
  uint32_t bootCount = 0;
  bool haveDump = false;
  char task[20] = {};
  uint32_t pc = 0;
  uint32_t vaddr = 0;    // the address the failing load or store used
  const char *cause = "";
  char elf[17] = {};     // the start of the crashed build's ELF SHA-256: which firmware.elf decodes it
  uint32_t backtrace[16] = {};
  uint8_t depth = 0;
  bool corrupted = false;
};

// The crash report's body; `lines` are the cleaned log lines before the reset, oldest first.
inline size_t buildCrashReport(const CrashInput &in, const char *const *lines, uint8_t lineCount, char *out,
                               size_t size) {
  JsonOut j(out, size);
  j.open();
  j.text("t", "diag_crash");
  j.integer("v", 1);
  if (in.emulator) j.integer("emu", 1);
  j.text("fw", in.firmware);
  j.integer("at", static_cast<long>(in.atS));
  j.text("reset", in.reset);
  j.integer("boot_count", static_cast<long>(in.bootCount));
  if (in.haveDump) {
    char hex[12];
    j.open("dump");
    j.text("task", in.task);
    snprintf(hex, sizeof(hex), "0x%08lx", static_cast<unsigned long>(in.pc));
    j.text("pc", hex);
    j.textIf("cause", in.cause);
    snprintf(hex, sizeof(hex), "0x%08lx", static_cast<unsigned long>(in.vaddr));
    j.text("vaddr", hex);
    j.textIf("elf", in.elf);
    j.openArray("bt");
    for (uint8_t i = 0; i < in.depth && i < 16; ++i) {
      snprintf(hex, sizeof(hex), "0x%08lx", static_cast<unsigned long>(in.backtrace[i]));
      j.item(hex);
    }
    j.closeArray();
    j.text("corrupted", in.corrupted ? "yes" : "no");
    j.close();
  }
  j.openArray("log");
  for (uint8_t i = 0; i < lineCount; ++i) j.item(lines[i]);
  j.closeArray();
  j.close();
  return j.ok() ? j.length() : 0;
}

// The envelope the relay takes: the library's, plus the public key, which the relay has no other
// way to know. Returns its length; 0 if the signer is not ready or it did not fit.
inline size_t sealReport(ISigner &signer, const char *body, char *out, size_t size) {
  if (!signer.ready() || !body || !body[0]) return 0;
  uint8_t hash[32], signature[SIGNATURE_LEN];
  sha256(body, strlen(body), hash);
  const bool emulator = strcmp(signer.algorithm(), "ES256") != 0;
  char sig[96] = "";
  if (!emulator) {
    if (!signer.sign(hash, signature)) return 0;
    base64Encode(signature, SIGNATURE_LEN, sig, sizeof(sig));
  }
  char kid[KEY_ID_LEN], pub[100];
  keyId(signer.publicKey(), kid);
  base64Encode(signer.publicKey(), PUBLIC_KEY_LEN, pub, sizeof(pub));
  JsonOut j(out, size);
  j.open();
  j.integer("v", 1);
  j.text("kid", kid);
  j.text("alg", signer.algorithm());
  j.text("pub", pub);
  j.text("sig", sig);
  j.text("body", body);
  j.close();
  return j.ok() ? j.length() : 0;
}

}  // namespace bbw
