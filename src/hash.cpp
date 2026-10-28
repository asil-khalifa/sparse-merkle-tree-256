#include "smt/hash.hpp"

#include <cstring>

namespace smt {

static uint64_t g_hash_invocations = 0;
uint64_t hash_invocations() { return g_hash_invocations; }
void reset_hash_invocations() { g_hash_invocations = 0; }

Hash256 sha256(const uint8_t* data, std::size_t len) { return Sha256::digest(data, len); }
Hash256 sha256(std::string_view s) {
  return Sha256::digest(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}
Hash256 sha256(const std::vector<uint8_t>& v) { return Sha256::digest(v.data(), v.size()); }

Hash256 hash_leaf(const Hash256& key, const Hash256& value_hash) {
  uint8_t buf[1 + 2 * HASH_SIZE];
  ++g_hash_invocations;
  buf[0] = LEAF_PREFIX;
  std::memcpy(buf + 1, key.data(), HASH_SIZE);
  std::memcpy(buf + 1 + HASH_SIZE, value_hash.data(), HASH_SIZE);
  return Sha256::digest(buf, sizeof(buf));
}

Hash256 hash_node(const Hash256& left, const Hash256& right) {
  uint8_t buf[1 + 2 * HASH_SIZE];
  ++g_hash_invocations;
  buf[0] = NODE_PREFIX;
  std::memcpy(buf + 1, left.data(), HASH_SIZE);
  std::memcpy(buf + 1 + HASH_SIZE, right.data(), HASH_SIZE);
  return Sha256::digest(buf, sizeof(buf));
}

Hash256 hash_key(std::string_view user_key) { return sha256(user_key); }
Hash256 hash_value(const std::vector<uint8_t>& value) { return sha256(value); }

unsigned common_prefix_len(const Hash256& a, const Hash256& b) {
  for (unsigned i = 0; i < HASH_SIZE; ++i) {
    const uint8_t x = uint8_t(a[i] ^ b[i]);
    if (x != 0) return i * 8 + unsigned(__builtin_clz(uint32_t(x)) - 24);
  }
  return KEY_BITS;
}

std::string to_hex(const uint8_t* data, std::size_t len) {
  static const char* digits = "0123456789abcdef";
  std::string s;
  s.resize(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    s[2 * i] = digits[data[i] >> 4];
    s[2 * i + 1] = digits[data[i] & 0xf];
  }
  return s;
}
std::string to_hex(const Hash256& h) { return to_hex(h.data(), h.size()); }
std::string to_hex(const std::vector<uint8_t>& v) { return to_hex(v.data(), v.size()); }

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool from_hex_bytes(std::string_view hex, std::vector<uint8_t>& out) {
  if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex.remove_prefix(2);
  if (hex.size() % 2 != 0) return false;
  out.clear();
  out.reserve(hex.size() / 2);
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    const int hi = hex_val(hex[i]), lo = hex_val(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out.push_back(uint8_t(hi << 4 | lo));
  }
  return true;
}

bool from_hex(std::string_view hex, Hash256& out) {
  std::vector<uint8_t> bytes;
  if (!from_hex_bytes(hex, bytes) || bytes.size() != HASH_SIZE) return false;
  std::memcpy(out.data(), bytes.data(), HASH_SIZE);
  return true;
}

}  // namespace smt
