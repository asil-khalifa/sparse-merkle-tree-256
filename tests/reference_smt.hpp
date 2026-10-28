// Naive reference implementation used as a test oracle.
//
// The canonical definition of the tree: a subtree containing a set S of
// (key, value_hash) pairs hashes to
//   placeholder                               if |S| = 0
//   hash_leaf(k, v)                           if S = {(k, v)}
//   hash_node(root(S_left), root(S_right))    otherwise, split on the bit at
//                                             the current depth.
// This builds the whole tree from scratch for every call, so it is only
// suitable for small sets, but it is obviously correct and independent of the
// incremental update logic in SparseMerkleTree.
#pragma once
#include <map>
#include <utility>
#include <vector>

#include "smt/hash.hpp"

inline smt::Hash256 reference_root(const std::vector<std::pair<smt::Hash256, smt::Hash256>>& leaves,
                                   unsigned depth = 0) {
  using namespace smt;
  if (leaves.empty()) return placeholder();
  if (leaves.size() == 1) return hash_leaf(leaves[0].first, leaves[0].second);
  std::vector<std::pair<Hash256, Hash256>> left, right;
  for (const auto& kv : leaves) (get_bit(kv.first, depth) ? right : left).push_back(kv);
  return hash_node(reference_root(left, depth + 1), reference_root(right, depth + 1));
}

inline smt::Hash256 reference_root(const std::map<smt::Hash256, std::vector<uint8_t>>& kv) {
  std::vector<std::pair<smt::Hash256, smt::Hash256>> leaves;
  for (const auto& e : kv) leaves.emplace_back(e.first, smt::hash_value(e.second));
  return reference_root(leaves);
}
