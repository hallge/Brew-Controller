// Clean replacements against the Open Brew by Weight code they replace
// (research/obbw-clean-spec.md §5). Included from test_main.cpp, before main().
//
// - Default build: core/StopMath.h is compared with BbwAlgorithms.h's
//   adaptation, both compiled in this binary, over random and edge-case input.
// - Both builds: the Acaia tables and the shared helpers are run over a fixed
//   pseudo-random input set and every output is folded into a digest. The
//   digests below were recorded from the default build (Open Brew's files);
//   BBW_CLEAN=1 test/host/run.sh must give the same ones.

namespace cleantest {

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ull) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return static_cast<uint32_t>(s >> 32);
  }
  uint32_t below(uint32_t n) { return n ? next() % n : 0; }
  float uniform(float a, float b) { return a + (b - a) * (next() / 4294967296.0f); }
  bool chance(uint32_t percent) { return below(100) < percent; }
};

struct Digest {
  uint64_t h = 1469598103934665603ull;
  void byte(uint8_t b) { h = (h ^ b) * 1099511628211ull; }
  void bytes(const uint8_t *p, size_t n) { for (size_t i = 0; i < n; ++i) byte(p[i]); }
  void u32(uint32_t v) { for (int i = 0; i < 4; ++i) byte(static_cast<uint8_t>(v >> (8 * i))); }
  void f32(float v) { uint32_t u; memcpy(&u, &v, 4); u32(u); }
  void flag(bool v) { byte(v ? 1 : 0); }
  void text(const char *s) {
    if (!s) { byte(0xff); return; }
    while (*s) byte(static_cast<uint8_t>(*s++));
    byte(0);
  }
};

inline void fixAcaiaChecksum(uint8_t *d, int n) {
  if (n < 6) return;
  uint8_t even = 0, odd = 0;
  for (int i = 3, k = 0; i < n - 2; ++i, ++k) {
    if ((k & 1) == 0) even = static_cast<uint8_t>(even + d[i]);
    else odd = static_cast<uint8_t>(odd + d[i]);
  }
  d[n - 2] = even;
  d[n - 1] = odd;
}

// A frame that is mostly shaped like an Acaia one, sometimes valid, sometimes damaged.
inline int acaiaFrame(Rng &r, uint8_t *d, bool legacy) {
  static const int lengths[] = {10, 13, 14, 17};
  int n = r.chance(85) ? lengths[r.below(4)] : static_cast<int>(r.below(21));
  for (int i = 0; i < 24; ++i) d[i] = static_cast<uint8_t>(r.next());
  if (r.chance(75)) {
    if (legacy) {
      d[6] = static_cast<uint8_t>(r.below(6));
      d[7] = static_cast<uint8_t>(r.chance(30) ? 0x02 : r.below(256));
    } else {
      d[0] = 0xef; d[1] = 0xdd; d[2] = 0x0c;
      d[3] = static_cast<uint8_t>(r.chance(90) ? n - 5 : r.below(20));
      const uint32_t tag = r.below(10);
      d[4] = tag < 5 ? 0x05 : tag < 8 ? 0x07 : static_cast<uint8_t>(r.below(256));
      d[9] = static_cast<uint8_t>(r.below(6));
      d[10] = static_cast<uint8_t>(r.chance(30) ? 0x02 : r.below(4));
      if (r.chance(80)) {  // a plausible timer at both places
        d[5 + (d[4] == 0x07 ? 0 : 6)] = static_cast<uint8_t>(r.below(r.chance(90) ? 3 : 120));
        d[6 + (d[4] == 0x07 ? 0 : 6)] = static_cast<uint8_t>(r.below(r.chance(90) ? 60 : 70));
        d[7 + (d[4] == 0x07 ? 0 : 6)] = static_cast<uint8_t>(r.below(r.chance(90) ? 10 : 15));
      }
    }
    if (r.chance(85)) fixAcaiaChecksum(d, n);
  }
  return n;
}

inline uint64_t acaiaTableDigest(const ScaleProtocol &p, uint64_t seed) {
  Digest d;
  d.text(p.id); d.text(p.debugLabel); d.text(p.readUuid); d.text(p.writeUuid);
  d.u32(static_cast<uint32_t>(p.namePrefixCount));
  for (size_t i = 0; i < p.namePrefixCount; ++i) d.text(p.namePrefixes[i]);
  d.u32(p.features.flags); d.byte(p.features.volumeMin); d.byte(p.features.volumeMax);
  d.u32(p.features.heartbeatPeriodMs); d.u32(p.features.maxPacketSilenceMs);
  d.u32(p.features.minimumCommandIntervalMs);
  d.flag(p.requireAdvertisedName);
  d.u32(static_cast<uint32_t>(p.initWriteCount));
  for (size_t i = 0; i < p.initWriteCount; ++i) {
    d.u32(static_cast<uint32_t>(p.initWrites[i].length));
    d.bytes(p.initWrites[i].data, static_cast<size_t>(p.initWrites[i].length));
    d.text(p.initWrites[i].label);
  }
  for (uint8_t op = 0; op <= static_cast<uint8_t>(ScaleOp::PowerOff); ++op) {
    for (uint8_t arg : {0, 1, 7}) {
      uint8_t out[SCALE_MAX_COMMAND_LENGTH + 4] = {};
      int length = -1;
      const bool ok = p.encodeCommand(static_cast<ScaleOp>(op), arg, out, &length);
      d.flag(ok);
      if (ok) { d.u32(static_cast<uint32_t>(length)); d.bytes(out, static_cast<size_t>(length)); }
    }
  }
  for (int n = -1; n <= 24; ++n) d.flag(p.supportedPacketLength(n));
  Rng r(seed);
  const bool legacy = strcmp(p.id, "acaia_legacy") == 0;
  int weights = 0, timers = 0;
  for (int i = 0; i < 30000; ++i) {
    uint8_t frame[24];
    const int n = acaiaFrame(r, frame, legacy);
    float w = 0.0f;
    const bool hasWeight = p.parseWeight(frame, n, &w);
    d.flag(hasWeight);
    if (hasWeight) { d.f32(w); ++weights; }
    uint32_t t = 0;
    const bool hasTimer = p.parseTimer(frame, n, &t);
    d.flag(hasTimer);
    if (hasTimer) { d.u32(t); ++timers; }
  }
  // The input set has to reach the interesting paths, or the digest proves little.
  CHECK(weights > 1500);
  CHECK(legacy ? timers == 0 : timers > 1500);
  return d.h;
}

inline uint64_t helperDigest() {
  Digest d;
  const float weights[] = {0.0f, 1.0f, -1.0f, 9999.9f, 10000.0f, 10000.01f, -10000.0f,
                           -10000.5f, NAN, INFINITY, -INFINITY, 36.25f};
  for (float w : weights) d.flag(scaleValidWeight(w));
  const uint8_t source[24] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                              13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
  for (int n = -1; n <= 22; ++n) {
    uint8_t out[24] = {};
    int length = -7;
    const bool ok = scaleCopyPayload(source, n, out, &length);
    d.flag(ok); d.u32(static_cast<uint32_t>(length)); d.bytes(out, sizeof out);
  }
  int length = 0;
  d.flag(scaleCopyPayload(source, 4, nullptr, &length));
  uint8_t sink[4];
  d.flag(scaleCopyPayload(source, 4, sink, nullptr));
  const char *names[] = {nullptr, "", "LUNAR-123456", "lunar", "PYXIS", "ACAIA", "PROCH",
                         "PROCHx", "XLUNAR", "PEARLS", "CINCO 1", "FELICITA", "Decent Scale",
                         "AKU MINI", "VARIA AKU", "CFS-9002", "LSJ-001", "MY_SCALE", "BOOKOO_SC 1"};
  for (const char *name : names) {
    d.flag(scaleNameMatchesProtocol(name, &kScaleProtocolAcaia));
    d.flag(scaleNameMatchesProtocol(name, &kScaleProtocolAcaiaLegacy));
    d.flag(scaleNameMatchesProtocol(name, nullptr));
    d.flag(scaleNameIsCompatible(name));
  }
  const char *uuids[] = {"2a80", "2A80", "fff1", "FFF2", "2a8", "2a800", "zz00", "", "0000",
                         "ffff", "49535343-1e4d-4bd9-ba61-23c647249616", "2a8g"};
  for (const char *u : uuids) {
    uint16_t value = 0xabcd;
    d.flag(scaleParseUuid16(u, &value));
    d.u32(value);
  }
  uint16_t ignored = 0;
  d.flag(scaleParseUuid16(nullptr, &ignored));
  d.flag(scaleParseUuid16("2a80", nullptr));
  for (uint32_t u = 0; u <= 0xffff; u += 0x0f) d.flag(scaleUuid16AllowsNamelessConnect(static_cast<uint16_t>(u)));
  for (uint16_t u : {0x2a80, 0xfff0, 0xfff1, 0xfff2, 0xfff4, 0xffe0, 0xffe1, 0x0ffe})
    d.flag(scaleUuid16AllowsNamelessConnect(u));
  Rng r(0xc1ea4u);
  for (int i = 0; i < 4000; ++i) {
    uint8_t buf[24];
    for (uint8_t &b : buf) b = static_cast<uint8_t>(r.next());
    const int n = static_cast<int>(r.below(21));
    d.byte(scaleXorBytes(buf, n));
    d.u32(scaleReadUint32LittleEndian(buf));
    if (r.chance(50)) fixAcaiaChecksum(buf, n);
    d.flag(scaleValidAcaiaChecksum(buf, n));
    if (r.chance(60)) for (int k = 9; k <= 13; ++k) buf[k] = static_cast<uint8_t>('0' + r.below(r.chance(95) ? 10 : 12));
    uint32_t ms = 0x55555555u;
    d.flag(scaleFelicitaAsciiTimer(buf, &ms));
    d.u32(ms);
  }
  for (int e = 0; e <= 7; ++e) d.f32(scaleDecimalDivisor(static_cast<uint8_t>(e)));
  for (int m = 97; m <= 101; ++m)
    for (int s = 57; s <= 61; ++s)
      for (int t = 8; t <= 11; ++t) {
        d.flag(scaleLooksLikeAcaiaTimer(static_cast<uint8_t>(m), static_cast<uint8_t>(s), static_cast<uint8_t>(t)));
        d.u32(scaleAcaiaTimerToMs(static_cast<uint8_t>(m), static_cast<uint8_t>(s), static_cast<uint8_t>(t)));
      }
  d.u32(scaleAcaiaTimerToMs(255, 255, 255));
  return d.h;
}

#ifndef BBW_CLEAN
// The adaptation and the clean code, side by side.

inline void stopMathPrediction() {
  Rng r(0x5eed01u);
  int compared = 0, predicted = 0, mismatches = 0;
  for (int i = 0; i < 20000; ++i) {
    float t[48], w[48];
    const size_t n = r.chance(90) ? 10 + r.below(38) : r.below(12);
    float time = r.uniform(0.0f, r.chance(20) ? 500.0f : 30.0f);
    float weight = r.uniform(-2.0f, 25.0f);
    const float flow = r.chance(10) ? r.uniform(-1.0f, 0.05f) : r.uniform(0.3f, 4.0f);
    const float noise = r.chance(30) ? 0.0f : r.uniform(0.0f, 0.4f);
    for (size_t k = 0; k < n; ++k) {
      const float dt = r.chance(2) ? (r.chance(50) ? 0.0f : -0.05f)
                                   : r.uniform(0.04f, r.chance(5) ? 0.00002f + 0.04f : 0.15f);
      time += k == 0 ? 0.0f : dt;
      weight += flow * dt + r.uniform(-noise, noise);
      t[k] = time;
      w[k] = r.chance(1) ? NAN : weight;
    }
    if (r.chance(3) && n >= 10) for (size_t k = n - 10; k < n; ++k) t[k] = time;  // flat time
    const float target = r.chance(2) ? NAN : r.uniform(5.0f, 80.0f);
    const float fallback = r.chance(2) ? INFINITY : 99.0f;
    const float a = predictStopTimeS(t, w, n, target, fallback);
    const float b = predictCrossingS(t, w, n, target, fallback);
    ++compared;
    const bool aFallback = a == fallback || (std::isnan(a) && std::isnan(fallback));
    const bool bFallback = b == fallback || (std::isnan(b) && std::isnan(fallback));
    bool same = aFallback == bFallback;
    if (same && !aFallback) {
      ++predicted;
      same = fabsf(a - b) <= fmaxf(1e-3f, 1e-5f * fabsf(a));
    }
    if (!same) {
      if (mismatches < 5) printf("  prediction differs: n=%u target=%g old=%.6f new=%.6f\n",
                                 static_cast<unsigned>(n), target, a, b);
      ++mismatches;
    }
  }
  CHECK(mismatches == 0);
  CHECK(predicted > 5000);  // most series reach a prediction
  printf("  StopMath prediction: %d series, %d predicted, %d differ\n", compared, predicted, mismatches);
  CHECK(predictCrossingS(nullptr, nullptr, 20, 30.0f, 7.0f) == 7.0f);
}

inline void stopMathOffset() {
  Rng r(0x5eed02u);
  int mismatches = 0, learned = 0;
  for (int i = 0; i < 50000; ++i) {
    const float offset = r.chance(2) ? NAN : r.uniform(-1.0f, 11.0f);
    const float target = r.uniform(18.0f, 60.0f);
    const float final = target + (r.chance(2) ? INFINITY : r.uniform(-12.0f, 12.0f));
    const float alphas[] = {0.0f, 0.005f, 0.01f, 0.1f, 0.3f, 0.5f, 1.0f, 1.0001f, NAN, r.uniform(0.0f, 1.2f)};
    const float alpha = alphas[r.below(10)];
    float a = -42.0f, b = -42.0f;
    const bool okA = learnStopOffset(offset, final, target, alpha, a);
    const bool okB = nextOffset(offset, final, target, alpha, b);
    uint32_t ua, ub;
    memcpy(&ua, &a, 4);
    memcpy(&ub, &b, 4);
    if (okA != okB || ua != ub) ++mismatches;
    if (okA) ++learned;
  }
  CHECK(mismatches == 0);
  CHECK(learned > 10000);
  for (float v : {-1.0f, 0.0f, 4.2f, 10.0f, 12.0f}) CHECK(clampOffset(v) == limitOffset(v));
  for (int a = 0; a <= 255; ++a) CHECK(validAlpha(static_cast<uint8_t>(a)) == gainValid(static_cast<uint8_t>(a)));
  for (int i = 0; i < 4; ++i) CHECK(ALPHA_CANDIDATES[i] == kGainChoices[i]);
  printf("  StopMath offset: 50000 cases, %d learned, %d differ\n", learned, mismatches);
}

inline void stopMathSelector() {
  Rng r(0x5eed03u);
  int mismatches = 0, switches = 0, sequences = 0;
  const uint8_t starts[] = {10, 30, 50, 100, 25, 70, 1, 0, 101};
  for (int s = 0; s < 3000; ++s) {
    AlphaEvidence old;
    AlphaSelector clean;
    uint8_t alphaOld = starts[r.below(9)];
    uint8_t alphaNew = alphaOld;
    float level = r.uniform(0.0f, 6.0f);
    const float noise = r.uniform(0.0f, 2.5f);
    const float drift = r.chance(30) ? r.uniform(-0.05f, 0.05f) : 0.0f;
    const int length = 20 + static_cast<int>(r.below(300));
    bool differs = false;
    for (int k = 0; k < length; ++k) {
      level += drift;
      if (r.chance(2)) level = r.uniform(0.0f, 6.0f);  // a new coffee
      float observation = level + r.uniform(-noise, noise);
      if (r.chance(1)) observation = r.chance(50) ? NAN : 11.0f;
      if (r.chance(1)) observation = static_cast<float>(r.below(5));  // exact values, for ties
      const float seed = r.chance(1) ? INFINITY : r.uniform(0.0f, 5.0f);
      const uint8_t nextOld = old.observe(observation, seed, alphaOld);
      const uint8_t nextNew = clean.observe(observation, seed, alphaNew);
      if (nextOld != alphaOld) ++switches;
      if (nextOld != nextNew) differs = true;
      alphaOld = nextOld;
      alphaNew = nextNew;
    }
    ++sequences;
    if (differs) {
      if (mismatches < 5) printf("  selector differs in sequence %d\n", s);
      ++mismatches;
    }
  }
  CHECK(mismatches == 0);
  CHECK(switches > 100);  // the switching path is exercised
  printf("  StopMath selector: %d sequences, %d switches, %d differ\n", sequences, switches, mismatches);
}
#endif  // !BBW_CLEAN

}  // namespace cleantest

static void testCleanReplacements() {
  printf("clean replacements%s\n",
#ifdef BBW_CLEAN
         " (BBW_CLEAN build)"
#else
         ""
#endif
  );
#ifndef BBW_CLEAN
  cleantest::stopMathPrediction();
  cleantest::stopMathOffset();
  cleantest::stopMathSelector();
#endif
  // Recorded from the default build (Open Brew by Weight's tables and helpers).
  constexpr uint64_t LEGACY_DIGEST = 0xac347864c27dd038ull;
  constexpr uint64_t CURRENT_DIGEST = 0x9d0890037b3f3319ull;
  constexpr uint64_t HELPER_DIGEST = 0x7a53d7978c9bafc7ull;
  const uint64_t legacy = cleantest::acaiaTableDigest(kScaleProtocolAcaiaLegacy, 0xacaa1u);
  const uint64_t current = cleantest::acaiaTableDigest(kScaleProtocolAcaia, 0xacaa2u);
  const uint64_t helpers = cleantest::helperDigest();
  printf("  digests: legacy %016llx current %016llx helpers %016llx\n",
         static_cast<unsigned long long>(legacy), static_cast<unsigned long long>(current),
         static_cast<unsigned long long>(helpers));
  CHECK(legacy == LEGACY_DIGEST);
  CHECK(current == CURRENT_DIGEST);
  CHECK(helpers == HELPER_DIGEST);
}
