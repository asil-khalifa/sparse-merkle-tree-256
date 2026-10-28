#include "smt/sha256.hpp"

#include <cstring>

namespace smt {
namespace {

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t load_be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline void store_be32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24);
  p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);
  p[3] = uint8_t(v);
}

}  // namespace

Sha256::Sha256() { reset(); }

void Sha256::reset() {
  h_[0] = 0x6a09e667; h_[1] = 0xbb67ae85; h_[2] = 0x3c6ef372; h_[3] = 0xa54ff53a;
  h_[4] = 0x510e527f; h_[5] = 0x9b05688c; h_[6] = 0x1f83d9ab; h_[7] = 0x5be0cd19;
  buf_len_ = 0;
  total_len_ = 0;
}

void Sha256::compress(const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) w[i] = load_be32(block + 4 * i);
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t t1 = h + S1 + ch + K[i] + w[i];
    const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = S0 + maj;
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::update(const uint8_t* data, std::size_t len) {
  total_len_ += len;
  if (buf_len_ > 0) {
    const std::size_t take = (64 - buf_len_ < len) ? 64 - buf_len_ : len;
    std::memcpy(buf_ + buf_len_, data, take);
    buf_len_ += take;
    data += take;
    len -= take;
    if (buf_len_ == 64) {
      compress(buf_);
      buf_len_ = 0;
    }
  }
  while (len >= 64) {
    compress(data);
    data += 64;
    len -= 64;
  }
  if (len > 0) {
    std::memcpy(buf_ + buf_len_, data, len);
    buf_len_ += len;
  }
}

void Sha256::finish(uint8_t out[32]) {
  const uint64_t bit_len = total_len_ * 8;
  buf_[buf_len_++] = 0x80;
  if (buf_len_ > 56) {
    std::memset(buf_ + buf_len_, 0, 64 - buf_len_);
    compress(buf_);
    buf_len_ = 0;
  }
  std::memset(buf_ + buf_len_, 0, 56 - buf_len_);
  for (int i = 0; i < 8; ++i) buf_[56 + i] = uint8_t(bit_len >> (56 - 8 * i));
  compress(buf_);
  for (int i = 0; i < 8; ++i) store_be32(out + 4 * i, h_[i]);
  reset();
}

Hash256 Sha256::digest(const uint8_t* data, std::size_t len) {
  Sha256 ctx;
  ctx.update(data, len);
  Hash256 out;
  ctx.finish(out.data());
  return out;
}

}  // namespace smt
