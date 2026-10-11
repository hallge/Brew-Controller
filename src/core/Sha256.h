#pragma once
// SHA-256 (FIPS 180-4). The same code on the board, in the emulator and in
// the host tests, so a record's hash is the same wherever it is made.
//
// Portable; no flash or UI access in here.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace bbw {

class Sha256 {
 public:
  Sha256() { reset(); }

  void reset() {
    static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(state_, init, sizeof(state_));
    length_ = 0;
    fill_ = 0;
  }

  void update(const void *data, size_t size) {
    const uint8_t *in = static_cast<const uint8_t *>(data);
    length_ += size;
    while (size > 0) {
      const size_t take = size < 64 - fill_ ? size : 64 - fill_;
      memcpy(block_ + fill_, in, take);
      fill_ += take;
      in += take;
      size -= take;
      if (fill_ == 64) {
        compress();
        fill_ = 0;
      }
    }
  }

  void finish(uint8_t out[32]) {
    const uint64_t bits = length_ * 8;
    const uint8_t one = 0x80, zero = 0;
    update(&one, 1);
    while (fill_ != 56) update(&zero, 1);
    uint8_t tail[8];
    for (int i = 0; i < 8; ++i) tail[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    update(tail, 8);
    for (int i = 0; i < 8; ++i) {
      out[4 * i] = static_cast<uint8_t>(state_[i] >> 24);
      out[4 * i + 1] = static_cast<uint8_t>(state_[i] >> 16);
      out[4 * i + 2] = static_cast<uint8_t>(state_[i] >> 8);
      out[4 * i + 3] = static_cast<uint8_t>(state_[i]);
    }
  }

 private:
  static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

  void compress() {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = static_cast<uint32_t>(block_[4 * i]) << 24 |
             static_cast<uint32_t>(block_[4 * i + 1]) << 16 |
             static_cast<uint32_t>(block_[4 * i + 2]) << 8 | block_[4 * i + 3];
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) +
                          k[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      h = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
  }

  uint32_t state_[8];
  uint64_t length_ = 0;
  uint8_t block_[64];
  size_t fill_ = 0;
};

inline void sha256(const void *data, size_t size, uint8_t out[32]) {
  Sha256 hash;
  hash.update(data, size);
  hash.finish(out);
}

}  // namespace bbw
