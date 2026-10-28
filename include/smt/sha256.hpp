// SHA-256 (FIPS 180-4) -- self-contained, dependency-free implementation.
// Used as the collision-resistant hash for leaves, internal nodes and keys.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace smt {

using Hash256 = std::array<uint8_t, 32>;

class Sha256 {
 public:
  Sha256();
  void reset();
  void update(const uint8_t* data, std::size_t len);
  void finish(uint8_t out[32]);
  static Hash256 digest(const uint8_t* data, std::size_t len);

 private:
  void compress(const uint8_t block[64]);
  uint32_t h_[8];
  uint8_t buf_[64];
  std::size_t buf_len_;
  uint64_t total_len_;
};

}  // namespace smt
