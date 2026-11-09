#pragma once
// DEFLATE (RFC 1951) and ZIP entries, for add-ons that read a device's own exports (docs/ADDONS.md).
// Written for this project from the RFC; output goes to a caller's buffer, nothing is allocated, and
// the decoder needs about 2.5 kB of stack. Header-only: it costs no flash unless an add-on uses it.
//
// Portable; host-tested.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace bbw {

enum class InflateResult : uint8_t { OK = 0, BAD_DATA = 1, OUT_FULL = 2, TRUNCATED = 3 };

namespace inflate_detail {

struct Bits {
  const uint8_t *in;
  size_t inLen, inPos = 0;
  uint32_t buf = 0;
  uint8_t count = 0;
  bool overrun = false;
  int need(uint8_t n) {
    while (count < n) {
      if (inPos >= inLen) {
        overrun = true;
        return -1;
      }
      buf |= static_cast<uint32_t>(in[inPos++]) << count;
      count += 8;
    }
    const int v = static_cast<int>(buf & ((1u << n) - 1u));
    buf >>= n;
    count -= n;
    return v;
  }
};

// Canonical Huffman code: counts per length and symbols in code order.
struct Huffman {
  uint16_t count[16];
  uint16_t symbol[320];
};

// false: the lengths do not make a usable code.
inline bool build(Huffman &h, const uint8_t *lengths, int n) {
  memset(h.count, 0, sizeof(h.count));
  for (int i = 0; i < n; ++i) h.count[lengths[i]]++;
  if (h.count[0] == n) return true;  // no codes (allowed for an unused distance table)
  int left = 1;
  for (int len = 1; len < 16; ++len) {
    left <<= 1;
    left -= h.count[len];
    if (left < 0) return false;  // over-subscribed
  }
  uint16_t offs[16];
  offs[1] = 0;
  for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<uint16_t>(offs[len] + h.count[len]);
  for (int i = 0; i < n; ++i) {
    if (lengths[i]) h.symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);
  }
  return true;
}

inline int decode(Bits &b, const Huffman &h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; ++len) {
    const int bit = b.need(1);
    if (bit < 0) return -1;
    code |= bit;
    const int count = h.count[len];
    if (code - count < first) return h.symbol[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -2;  // no such code
}

constexpr uint16_t LEN_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                   3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t DIST_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                    6145, 8193, 12289, 16385, 24577};
constexpr uint8_t DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

inline InflateResult codes(Bits &b, const Huffman &lit, const Huffman &dist, uint8_t *out,
                           size_t outCap, size_t &outPos) {
  for (;;) {
    int sym = decode(b, lit);
    if (sym < 0) return b.overrun ? InflateResult::TRUNCATED : InflateResult::BAD_DATA;
    if (sym < 256) {
      if (outPos >= outCap) return InflateResult::OUT_FULL;
      out[outPos++] = static_cast<uint8_t>(sym);
    } else if (sym == 256) {
      return InflateResult::OK;
    } else {
      sym -= 257;
      if (sym >= 29) return InflateResult::BAD_DATA;
      const int extra = b.need(LEN_EXTRA[sym]);
      if (extra < 0) return InflateResult::TRUNCATED;
      const size_t len = LEN_BASE[sym] + static_cast<size_t>(extra);
      const int dsym = decode(b, dist);
      if (dsym < 0) return b.overrun ? InflateResult::TRUNCATED : InflateResult::BAD_DATA;
      if (dsym >= 30) return InflateResult::BAD_DATA;
      const int dextra = b.need(DIST_EXTRA[dsym]);
      if (dextra < 0) return InflateResult::TRUNCATED;
      const size_t d = DIST_BASE[dsym] + static_cast<size_t>(dextra);
      if (d > outPos) return InflateResult::BAD_DATA;  // before the start
      if (outPos + len > outCap) return InflateResult::OUT_FULL;
      for (size_t i = 0; i < len; ++i, ++outPos) out[outPos] = out[outPos - d];
    }
  }
}

}  // namespace inflate_detail

// Raw DEFLATE (no zlib or gzip header) into out. outLen: bytes written.
inline InflateResult inflateRaw(const uint8_t *in, size_t inLen, uint8_t *out, size_t outCap, size_t &outLen) {
  using namespace inflate_detail;
  Bits b{in, inLen};
  outLen = 0;
  static constexpr uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  for (;;) {
    const int last = b.need(1);
    const int type = b.need(2);
    if (last < 0 || type < 0) return InflateResult::TRUNCATED;
    if (type == 0) {  // stored
      b.buf = 0;
      b.count = 0;
      if (b.inPos + 4 > inLen) return InflateResult::TRUNCATED;
      const size_t len = in[b.inPos] | (in[b.inPos + 1] << 8);
      const size_t nlen = in[b.inPos + 2] | (in[b.inPos + 3] << 8);
      b.inPos += 4;
      if (len != (~nlen & 0xFFFFu)) return InflateResult::BAD_DATA;
      if (b.inPos + len > inLen) return InflateResult::TRUNCATED;
      if (outLen + len > outCap) return InflateResult::OUT_FULL;
      memcpy(out + outLen, in + b.inPos, len);
      b.inPos += len;
      outLen += len;
    } else if (type == 1 || type == 2) {
      Huffman lit, dist;
      uint8_t lengths[320];
      if (type == 1) {  // fixed codes
        int i = 0;
        for (; i < 144; ++i) lengths[i] = 8;
        for (; i < 256; ++i) lengths[i] = 9;
        for (; i < 280; ++i) lengths[i] = 7;
        for (; i < 288; ++i) lengths[i] = 8;
        build(lit, lengths, 288);
        for (i = 0; i < 30; ++i) lengths[i] = 5;
        build(dist, lengths, 30);
      } else {  // dynamic codes
        const int nlen = b.need(5), ndist = b.need(5), ncode = b.need(4);
        if (nlen < 0 || ndist < 0 || ncode < 0) return InflateResult::TRUNCATED;
        const int nl = nlen + 257, nd = ndist + 1, nc = ncode + 4;
        if (nl > 286 || nd > 30) return InflateResult::BAD_DATA;
        memset(lengths, 0, 19);
        for (int i = 0; i < nc; ++i) {
          const int v = b.need(3);
          if (v < 0) return InflateResult::TRUNCATED;
          lengths[ORDER[i]] = static_cast<uint8_t>(v);
        }
        Huffman lencode;
        if (!build(lencode, lengths, 19)) return InflateResult::BAD_DATA;
        int idx = 0;
        while (idx < nl + nd) {
          int sym = decode(b, lencode);
          if (sym < 0) return b.overrun ? InflateResult::TRUNCATED : InflateResult::BAD_DATA;
          if (sym < 16) {
            lengths[idx++] = static_cast<uint8_t>(sym);
            continue;
          }
          uint8_t value = 0;
          int repeat;
          if (sym == 16) {
            if (idx == 0) return InflateResult::BAD_DATA;
            value = lengths[idx - 1];
            repeat = b.need(2);
            if (repeat < 0) return InflateResult::TRUNCATED;
            repeat += 3;
          } else if (sym == 17) {
            repeat = b.need(3);
            if (repeat < 0) return InflateResult::TRUNCATED;
            repeat += 3;
          } else {
            repeat = b.need(7);
            if (repeat < 0) return InflateResult::TRUNCATED;
            repeat += 11;
          }
          if (idx + repeat > nl + nd) return InflateResult::BAD_DATA;
          while (repeat--) lengths[idx++] = value;
        }
        if (lengths[256] == 0) return InflateResult::BAD_DATA;  // no end-of-block code
        if (!build(lit, lengths, nl)) return InflateResult::BAD_DATA;
        if (!build(dist, lengths + nl, nd)) return InflateResult::BAD_DATA;
      }
      const InflateResult r = codes(b, lit, dist, out, outCap, outLen);
      if (r != InflateResult::OK) return r;
    } else {
      return InflateResult::BAD_DATA;
    }
    if (last) return InflateResult::OK;
  }
}

// One file inside a ZIP archive, found by the end of its name (e.g. "GrindLogs" plus ".csv"), read
// by its local header. Stored and deflated entries only. An entry whose sizes are only in a trailing
// data descriptor is found through the central directory.
struct ZipEntry {
  const uint8_t *data = nullptr;  // its compressed bytes inside the archive
  size_t compressedSize = 0;
  size_t size = 0;
  uint16_t method = 0;            // 0 stored, 8 deflated
  char name[96] = {};
};

namespace zip_detail {
inline uint16_t u16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t u32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline bool matches(const uint8_t *name, size_t n, const char *contains, const char *suffix) {
  const size_t sl = suffix ? strlen(suffix) : 0;
  if (sl > n || (sl && memcmp(name + n - sl, suffix, sl) != 0)) return false;
  if (!contains || !contains[0]) return true;
  const size_t cl = strlen(contains);
  for (size_t i = 0; i + cl <= n; ++i) {
    if (memcmp(name + i, contains, cl) == 0) return true;
  }
  return false;
}
}  // namespace zip_detail

// The first entry whose name contains `contains` and ends with `suffix` (either may be empty).
inline bool zipFind(const uint8_t *zip, size_t len, const char *contains, const char *suffix, ZipEntry &out) {
  using namespace zip_detail;
  // The central directory (end record within the last 64 kB + 22 bytes).
  if (len < 22) return false;
  size_t eocd = 0;
  bool found = false;
  for (size_t i = len - 22 + 1; i-- > 0;) {
    if (u32(zip + i) == 0x06054b50u) {
      eocd = i;
      found = true;
      break;
    }
    if (len - i > 65557) break;
  }
  if (!found) return false;
  const uint16_t entries = u16(zip + eocd + 10);
  size_t pos = u32(zip + eocd + 16);
  for (uint16_t k = 0; k < entries; ++k) {
    if (pos + 46 > len || u32(zip + pos) != 0x02014b50u) return false;
    const uint16_t method = u16(zip + pos + 10);
    const uint32_t csize = u32(zip + pos + 20), usize = u32(zip + pos + 24);
    const uint16_t nlen = u16(zip + pos + 28), elen = u16(zip + pos + 30), clen = u16(zip + pos + 32);
    const uint32_t local = u32(zip + pos + 42);
    if (pos + 46 + nlen > len) return false;
    const uint8_t *name = zip + pos + 46;
    if (matches(name, nlen, contains, suffix)) {
      if (local + 30 > len || u32(zip + local) != 0x04034b50u) return false;
      const size_t start = local + 30 + u16(zip + local + 26) + u16(zip + local + 28);
      if (start + csize > len) return false;
      out.data = zip + start;
      out.compressedSize = csize;
      out.size = usize;
      out.method = method;
      const size_t keep = nlen < sizeof(out.name) - 1 ? nlen : sizeof(out.name) - 1;
      memcpy(out.name, name, keep);
      out.name[keep] = '\0';
      return method == 0 || method == 8;
    }
    pos += 46 + nlen + elen + clen;
  }
  return false;
}

// The entry's contents into out (not NUL-terminated). outLen: bytes written.
inline InflateResult zipExtract(const ZipEntry &e, uint8_t *out, size_t outCap, size_t &outLen) {
  outLen = 0;
  if (e.method == 0) {
    if (e.compressedSize > outCap) return InflateResult::OUT_FULL;
    memcpy(out, e.data, e.compressedSize);
    outLen = e.compressedSize;
    return InflateResult::OK;
  }
  if (e.method != 8) return InflateResult::BAD_DATA;
  return inflateRaw(e.data, e.compressedSize, out, outCap, outLen);
}

}  // namespace bbw
