#include "smt/proof.hpp"

#include <cstring>
#include <sstream>

#include "smt/json.hpp"

namespace smt {

const char* proof_kind_name(ProofKind k) {
  switch (k) {
    case ProofKind::Membership: return "membership";
    case ProofKind::NonMembershipEmpty: return "non-membership-empty";
    case ProofKind::NonMembershipLeaf: return "non-membership-leaf";
  }
  return "unknown";
}

static bool kind_from_name(std::string_view s, ProofKind& k) {
  if (s == "membership") { k = ProofKind::Membership; return true; }
  if (s == "non-membership-empty") { k = ProofKind::NonMembershipEmpty; return true; }
  if (s == "non-membership-leaf") { k = ProofKind::NonMembershipLeaf; return true; }
  return false;
}

static bool fail(std::string* err, const char* msg) {
  if (err) *err = msg;
  return false;
}

std::size_t Proof::encoded_size() const {
  std::size_t s = 1 + 2 + HASH_SIZE;  // kind, depth, key
  if (kind == ProofKind::Membership) s += HASH_SIZE;
  if (kind == ProofKind::NonMembershipLeaf) s += 2 * HASH_SIZE;
  s += (depth + 7) / 8;             // bitmask
  s += siblings.size() * HASH_SIZE;  // non-empty siblings
  return s;
}

std::vector<uint8_t> Proof::serialize() const {
  std::vector<uint8_t> out;
  out.reserve(encoded_size());
  out.push_back(uint8_t(kind));
  out.push_back(uint8_t(depth >> 8));
  out.push_back(uint8_t(depth & 0xff));
  out.insert(out.end(), key.begin(), key.end());
  if (kind == ProofKind::Membership) out.insert(out.end(), value_hash.begin(), value_hash.end());
  if (kind == ProofKind::NonMembershipLeaf) {
    out.insert(out.end(), other_key.begin(), other_key.end());
    out.insert(out.end(), value_hash.begin(), value_hash.end());
  }
  out.insert(out.end(), bitmask.begin(), bitmask.begin() + (depth + 7) / 8);
  for (const Hash256& s : siblings) out.insert(out.end(), s.begin(), s.end());
  return out;
}

bool Proof::deserialize(const std::vector<uint8_t>& b, Proof& p, std::string* err) {
  std::size_t pos = 0;
  auto need = [&](std::size_t n) { return pos + n <= b.size(); };
  auto take_hash = [&](Hash256& h) {
    std::memcpy(h.data(), b.data() + pos, HASH_SIZE);
    pos += HASH_SIZE;
  };
  if (!need(3 + HASH_SIZE)) return fail(err, "truncated proof");
  p = Proof{};
  const uint8_t k = b[pos++];
  if (k > 2) return fail(err, "bad proof kind");
  p.kind = ProofKind(k);
  p.depth = uint16_t((b[pos] << 8) | b[pos + 1]);
  pos += 2;
  if (p.depth > KEY_BITS) return fail(err, "depth exceeds 256");
  take_hash(p.key);
  if (p.kind == ProofKind::Membership) {
    if (!need(HASH_SIZE)) return fail(err, "truncated proof");
    take_hash(p.value_hash);
  } else if (p.kind == ProofKind::NonMembershipLeaf) {
    if (!need(2 * HASH_SIZE)) return fail(err, "truncated proof");
    take_hash(p.other_key);
    take_hash(p.value_hash);
  }
  const std::size_t mask_bytes = (p.depth + 7) / 8;
  if (!need(mask_bytes)) return fail(err, "truncated proof");
  std::memcpy(p.bitmask.data(), b.data() + pos, mask_bytes);
  pos += mask_bytes;
  std::size_t count = 0;
  for (unsigned i = 0; i < p.depth; ++i) count += get_bit(p.bitmask, i);
  if (!need(count * HASH_SIZE)) return fail(err, "truncated proof");
  p.siblings.resize(count);
  for (std::size_t i = 0; i < count; ++i) take_hash(p.siblings[i]);
  if (pos != b.size()) return fail(err, "trailing bytes");
  return true;
}

std::string Proof::to_json(std::string_view user_key) const {
  std::ostringstream o;
  o << "{\n";
  o << "  \"format\": \"smt-proof-v1\",\n";
  o << "  \"type\": \"" << proof_kind_name(kind) << "\",\n";
  o << "  \"root\": \"" << to_hex(root) << "\",\n";
  if (!user_key.empty()) o << "  \"user_key\": \"" << json::escape(user_key) << "\",\n";
  o << "  \"key\": \"" << to_hex(key) << "\",\n";
  if (kind == ProofKind::Membership || kind == ProofKind::NonMembershipLeaf)
    o << "  \"value_hash\": \"" << to_hex(value_hash) << "\",\n";
  if (kind == ProofKind::NonMembershipLeaf) o << "  \"other_key\": \"" << to_hex(other_key) << "\",\n";
  o << "  \"depth\": " << depth << ",\n";
  o << "  \"bitmask\": \"" << to_hex(bitmask) << "\",\n";
  o << "  \"siblings\": [";
  for (std::size_t i = 0; i < siblings.size(); ++i) {
    o << (i ? ",\n    \"" : "\n    \"") << to_hex(siblings[i]) << "\"";
  }
  o << (siblings.empty() ? "],\n" : "\n  ],\n");
  o << "  \"size_bytes\": " << encoded_size() << "\n";
  o << "}\n";
  return o.str();
}

bool Proof::from_json(std::string_view text, Proof& p, std::string* err) {
  json::Value v;
  if (!json::parse(text, v, err)) return false;
  if (!v.is_object()) return fail(err, "proof JSON must be an object");
  auto str = [&](const char* name, Hash256& out) {
    const json::Value* f = v.get(name);
    return f && f->is_string() && from_hex(f->str, out);
  };
  p = Proof{};
  const json::Value* type = v.get("type");
  if (!type || !type->is_string() || !kind_from_name(type->str, p.kind)) return fail(err, "missing/invalid type");
  if (!str("root", p.root)) return fail(err, "missing/invalid root");
  if (!str("key", p.key)) return fail(err, "missing/invalid key");
  if (p.kind == ProofKind::Membership || p.kind == ProofKind::NonMembershipLeaf)
    if (!str("value_hash", p.value_hash)) return fail(err, "missing/invalid value_hash");
  if (p.kind == ProofKind::NonMembershipLeaf)
    if (!str("other_key", p.other_key)) return fail(err, "missing/invalid other_key");
  const json::Value* d = v.get("depth");
  if (!d || !d->is_number() || d->num < 0 || d->num > KEY_BITS) return fail(err, "missing/invalid depth");
  p.depth = uint16_t(d->num);
  if (!str("bitmask", p.bitmask)) return fail(err, "missing/invalid bitmask");
  const json::Value* s = v.get("siblings");
  if (!s || !s->is_array()) return fail(err, "missing/invalid siblings");
  for (const json::Value& e : s->arr) {
    Hash256 h;
    if (!e.is_string() || !from_hex(e.str, h)) return fail(err, "invalid sibling hash");
    p.siblings.push_back(h);
  }
  return true;
}

bool verify_proof(const Proof& p, const Hash256& root, const Hash256& key, std::string* err) {
  if (p.depth > KEY_BITS) return fail(err, "depth exceeds 256");
  if (p.key != key) return fail(err, "proof key does not match the queried key");
  std::size_t count = 0;
  for (unsigned i = 0; i < KEY_BITS; ++i) {
    if (!get_bit(p.bitmask, i)) continue;
    if (i >= p.depth) return fail(err, "bitmask has bits set beyond the proof depth");
    ++count;
  }
  if (count != p.siblings.size()) return fail(err, "sibling count does not match bitmask");

  // Hash of the node where the walk down the key's path ended.
  Hash256 cur;
  switch (p.kind) {
    case ProofKind::Membership:
      cur = hash_leaf(key, p.value_hash);
      break;
    case ProofKind::NonMembershipEmpty:
      cur = placeholder();
      break;
    case ProofKind::NonMembershipLeaf:
      if (p.other_key == key) return fail(err, "non-membership proof names a leaf with the queried key");
      if (common_prefix_len(p.other_key, key) < p.depth) return fail(err, "unrelated leaf is not on the key's path");
      cur = hash_leaf(p.other_key, p.value_hash);
      break;
    default:
      return fail(err, "unknown proof kind");
  }
  // Walk back up to the root; empty siblings are the placeholder.
  std::size_t idx = p.siblings.size();
  for (unsigned i = p.depth; i-- > 0;) {
    const Hash256& sib = get_bit(p.bitmask, i) ? p.siblings[--idx] : placeholder();
    cur = get_bit(key, i) ? hash_node(sib, cur) : hash_node(cur, sib);
  }
  if (cur != root) return fail(err, "computed root does not match the expected root");
  return true;
}

bool verify_membership(const Proof& p, const Hash256& root, const Hash256& key, const std::vector<uint8_t>& value,
                       std::string* err) {
  if (p.kind != ProofKind::Membership) return fail(err, "not a membership proof");
  if (hash_value(value) != p.value_hash) return fail(err, "value does not match the proof's value hash");
  return verify_proof(p, root, key, err);
}

bool verify_non_membership(const Proof& p, const Hash256& root, const Hash256& key, std::string* err) {
  if (p.kind == ProofKind::Membership) return fail(err, "not a non-membership proof");
  return verify_proof(p, root, key, err);
}

}  // namespace smt
