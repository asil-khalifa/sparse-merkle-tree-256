// Compact (non-)membership proofs.
//
// A proof for key K against root R is the list of sibling hashes along K's
// path, from the root down to the node where the walk stops:
//   * a leaf whose key is K            -> membership proof
//   * an empty subtree (placeholder)   -> non-membership proof (empty)
//   * a leaf whose key is NOT K        -> non-membership proof (other leaf)
//
// Only the non-empty siblings are transmitted. A bitmask records which depths
// had a real sibling, every other sibling is the well-known placeholder. The
// verifier re-hashes the path with the key's bits and compares with R.
//
// Size: 1 + 2 + 32 + {0,32,64} + ceil(depth/8) + 32 * (#non-empty siblings)
// bytes, which is O(log N) for N random keys and never more than 8,323 bytes
// (the depth is bounded by 256 by construction).
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "smt/hash.hpp"

namespace smt {

enum class ProofKind : uint8_t {
  Membership = 0,          // key is present with value_hash
  NonMembershipEmpty = 1,  // key's path ends in an empty subtree
  NonMembershipLeaf = 2    // key's path ends at a leaf holding a different key
};

struct Proof {
  ProofKind kind = ProofKind::NonMembershipEmpty;
  Hash256 root{};        // root this proof was generated for (informational)
  Hash256 key{};         // queried key path
  Hash256 value_hash{};  // Membership: value hash of key; NonMembershipLeaf: value hash of other leaf
  Hash256 other_key{};   // NonMembershipLeaf: key path of the leaf found on the path
  uint16_t depth = 0;    // number of levels walked (0..256)
  Hash256 bitmask{};     // bit i set <=> sibling at depth i is non-empty and present in `siblings`
  std::vector<Hash256> siblings;  // non-empty siblings only, root side first

  bool is_membership() const { return kind == ProofKind::Membership; }
  std::size_t num_siblings() const { return siblings.size(); }

  // Compact binary wire size in bytes.
  std::size_t encoded_size() const;
  std::vector<uint8_t> serialize() const;
  static bool deserialize(const std::vector<uint8_t>& bytes, Proof& out, std::string* err = nullptr);

  std::string to_json(std::string_view user_key = {}) const;
  static bool from_json(std::string_view json, Proof& out, std::string* err = nullptr);
};

const char* proof_kind_name(ProofKind k);

// Core verifier. Recomputes the root from the proof and compares with `root`.
// Returns true iff the proof is well-formed and authenticates the claim
// "key is present with value_hash" (Membership) or "key is absent".
bool verify_proof(const Proof& proof, const Hash256& root, const Hash256& key, std::string* err = nullptr);

// Convenience wrappers.
bool verify_membership(const Proof& proof, const Hash256& root, const Hash256& key,
                       const std::vector<uint8_t>& value, std::string* err = nullptr);
bool verify_non_membership(const Proof& proof, const Hash256& root, const Hash256& key,
                           std::string* err = nullptr);

}  // namespace smt
