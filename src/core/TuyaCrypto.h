#pragma once
// MD5 (RFC 1321), AES-128 in CBC mode (FIPS 197, SP 800-38A) and the CRC-16
// Tuya's BLE protocol uses. Small, portable, and only for that protocol: the
// host tests check them against the published test vectors.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace bbw {
namespace tuya {

// MD5 of up to 119 bytes (two blocks), which is all the key derivation needs.
inline bool md5(const uint8_t *data, size_t length, uint8_t out[16]) {
  static const uint32_t K[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
      0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  static const uint8_t S[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
  if (length > 119) return false;
  uint8_t message[128] = {};
  memcpy(message, data, length);
  message[length] = 0x80;
  const size_t total = length < 56 ? 64 : 128;
  const uint64_t bits = static_cast<uint64_t>(length) * 8;
  for (int i = 0; i < 8; ++i) message[total - 8 + i] = static_cast<uint8_t>(bits >> (8 * i));
  uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  for (size_t block = 0; block < total; block += 64) {
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
      const uint8_t *p = message + block + 4 * i;
      m[i] = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
             (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; ++i) {
      uint32_t f;
      int g;
      if (i < 16) { f = (b & c) | (~b & d); g = i; }
      else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
      else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
      else { f = c ^ (b | ~d); g = (7 * i) % 16; }
      const uint32_t x = a + f + K[i] + m[g];
      const int s = S[(i / 16) * 4 + (i % 4)];
      a = d;
      d = c;
      c = b;
      b = b + ((x << s) | (x >> (32 - s)));
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
  }
  for (int i = 0; i < 16; ++i) out[i] = static_cast<uint8_t>(h[i / 4] >> (8 * (i % 4)));
  return true;
}

class Aes128 {
 public:
  explicit Aes128(const uint8_t key[16]) {
    tables();
    memcpy(round_, key, 16);
    uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
      uint8_t t[4] = {round_[i - 4], round_[i - 3], round_[i - 2], round_[i - 1]};
      if (i % 16 == 0) {
        const uint8_t first = t[0];
        t[0] = static_cast<uint8_t>(sbox()[t[1]] ^ rcon);
        t[1] = sbox()[t[2]];
        t[2] = sbox()[t[3]];
        t[3] = sbox()[first];
        rcon = xtime(rcon);
      }
      for (int k = 0; k < 4; ++k) round_[i + k] = static_cast<uint8_t>(round_[i - 16 + k] ^ t[k]);
    }
  }

  void encryptBlock(uint8_t s[16]) const {
    addKey(s, 0);
    for (int round = 1; round <= 10; ++round) {
      for (int i = 0; i < 16; ++i) s[i] = sbox()[s[i]];
      shiftRows(s, false);
      if (round < 10) mixColumns(s, false);
      addKey(s, round);
    }
  }

  void decryptBlock(uint8_t s[16]) const {
    addKey(s, 10);
    for (int round = 9; round >= 0; --round) {
      shiftRows(s, true);
      for (int i = 0; i < 16; ++i) s[i] = inverse()[s[i]];
      addKey(s, round);
      if (round > 0) mixColumns(s, true);
    }
  }

  // length must be a multiple of 16. In place.
  void cbcEncrypt(const uint8_t iv[16], uint8_t *data, size_t length) const {
    uint8_t chain[16];
    memcpy(chain, iv, 16);
    for (size_t at = 0; at + 16 <= length; at += 16) {
      for (int i = 0; i < 16; ++i) data[at + i] ^= chain[i];
      encryptBlock(data + at);
      memcpy(chain, data + at, 16);
    }
  }

  void cbcDecrypt(const uint8_t iv[16], uint8_t *data, size_t length) const {
    uint8_t chain[16], next[16];
    memcpy(chain, iv, 16);
    for (size_t at = 0; at + 16 <= length; at += 16) {
      memcpy(next, data + at, 16);
      decryptBlock(data + at);
      for (int i = 0; i < 16; ++i) data[at + i] ^= chain[i];
      memcpy(chain, next, 16);
    }
  }

 private:
  static uint8_t xtime(uint8_t x) { return static_cast<uint8_t>((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
  static uint8_t multiply(uint8_t x, uint8_t by) {
    uint8_t result = 0;
    for (; by; by >>= 1, x = xtime(x)) {
      if (by & 1) result ^= x;
    }
    return result;
  }
  static uint8_t *sbox() { static uint8_t table[256]; return table; }
  static uint8_t *inverse() { static uint8_t table[256]; return table; }
  // The S-box from its definition: the inverse in GF(2^8), then the affine map.
  static void tables() {
    static bool made = false;
    if (made) return;
    uint8_t p = 1, q = 1;
    do {
      p = static_cast<uint8_t>(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));  // p * 3
      q ^= static_cast<uint8_t>(q << 1);                                // q / 3
      q ^= static_cast<uint8_t>(q << 2);
      q ^= static_cast<uint8_t>(q << 4);
      if (q & 0x80) q ^= 0x09;
      const auto rotl = [](uint8_t v, int n) { return static_cast<uint8_t>((v << n) | (v >> (8 - n))); };
      const uint8_t x = static_cast<uint8_t>(q ^ rotl(q, 1) ^ rotl(q, 2) ^ rotl(q, 3) ^ rotl(q, 4) ^ 0x63);
      sbox()[p] = x;
      inverse()[x] = p;
    } while (p != 1);
    sbox()[0] = 0x63;
    inverse()[0x63] = 0;
    made = true;
  }
  void addKey(uint8_t s[16], int round) const {
    for (int i = 0; i < 16; ++i) s[i] ^= round_[16 * round + i];
  }
  // The state is column-major: byte i is row i % 4 of column i / 4.
  static void shiftRows(uint8_t s[16], bool back) {
    uint8_t t[16];
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        const int from = back ? (column - row + 4) % 4 : (column + row) % 4;
        t[4 * column + row] = s[4 * from + row];
      }
    }
    memcpy(s, t, 16);
  }
  static void mixColumns(uint8_t s[16], bool back) {
    static const uint8_t forward[4] = {2, 3, 1, 1}, backward[4] = {14, 11, 13, 9};
    const uint8_t *m = back ? backward : forward;
    for (int column = 0; column < 4; ++column) {
      uint8_t *c = s + 4 * column;
      const uint8_t in[4] = {c[0], c[1], c[2], c[3]};
      for (int row = 0; row < 4; ++row) {
        c[row] = static_cast<uint8_t>(multiply(in[0], m[(4 - row) % 4]) ^ multiply(in[1], m[(5 - row) % 4]) ^
                                      multiply(in[2], m[(6 - row) % 4]) ^ multiply(in[3], m[(7 - row) % 4]));
      }
    }
  }

  uint8_t round_[176];
};

// CRC-16 as Tuya's BLE packets carry it: start 0xFFFF, polynomial 0xA001 reflected.
inline uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001) : (crc >> 1);
  }
  return crc;
}

}  // namespace tuya
}  // namespace bbw
