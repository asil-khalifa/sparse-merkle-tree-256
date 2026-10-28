// Hashing conventions for the Sparse Merkle Tree.
//
//   leaf  hash  = SHA256( 0x00 || key(32) || value_hash(32) )
//   node  hash  = SHA256( 0x01 || left(32) || right(32) )
//   empty hash  = 0x00..00  (32 zero bytes, a fixed placeholder)
//   key   path  = SHA256( user_key )    (uniform 256-bit path, DoS resistant)
//   value hash  = SHA256( value )
//
// The 0x00 / 0x01 prefixes give domain separation between leaves and internal
// nodes (RFC 6962 style), which prevents second-preimage attacks where a leaf
// is re-interpreted as an internal node or vice-versa.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "smt/sha256.hpp"

namespace smt {

constexpr std::size_t HASH_SIZE = 32;
constexpr unsigned KEY_BITS = 256;  // depth of the (logical) tree

constexpr uint8_t LEAF_PREFIX = 0x00;
constexpr uint8_t NODE_PREFIX = 0x01;

// The hash used for every empty subtree, regardless of its height.
inline const Hash256& placeholder() {
  static const Hash256 zero{};
  return zero;
}
inline bool is_placeholder(const Hash256& h) { return h == placeholder(); }

Hash256 sha256(const uint8_t* data, std::size_t len);
Hash256 sha256(std::string_view s);
Hash256 sha256(const std::vector<uint8_t>& v);

Hash256 hash_leaf(const Hash256& key, const Hash256& value_hash);
Hash256 hash_node(const Hash256& left, const Hash256& right);
Hash256 hash_key(std::string_view user_key);
Hash256 hash_value(const std::vector<uint8_t>& value);

// Number of leaf/node hash evaluations performed so far (benchmark metric).
uint64_t hash_invocations();
void reset_hash_invocations();

// Bit i of a 256-bit path, most-significant bit first (bit 0 = MSB of byte 0).
inline bool get_bit(const Hash256& k, unsigned i) {
  return (k[i >> 3] >> (7 - (i & 7))) & 1u;
}
inline void set_bit(Hash256& k, unsigned i) { k[i >> 3] |= uint8_t(1u << (7 - (i & 7))); }

// Length (in bits) of the longest common prefix of two paths (0..256).
unsigned common_prefix_len(const Hash256& a, const Hash256& b);

std::string to_hex(const Hash256& h);
std::string to_hex(const uint8_t* data, std::size_t len);
std::string to_hex(const std::vector<uint8_t>& v);
bool from_hex(std::string_view hex, Hash256& out);
bool from_hex_bytes(std::string_view hex, std::vector<uint8_t>& out);

// Hasher for unordered containers keyed by Hash256. Node hashes are SHA-256
// outputs, so their first 8 bytes are already uniformly distributed.
struct Hash256Hasher {
  std::size_t operator()(const Hash256& h) const noexcept {
    std::size_t s;
    std::memcpy(&s, h.data(), sizeof(s));
    return s;
  }
};

}  // namespace smt
