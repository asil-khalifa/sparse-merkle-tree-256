// 256-level Sparse Merkle Tree over content-addressed storage.
//
// Logical structure: a complete binary tree of depth 256; key K (a 256-bit
// path) lives at the leaf reached by following K's bits (0 = left, 1 = right).
// Two optimisations keep every operation O(log N) in the number of stored
// keys N instead of O(256):
//   1. An empty subtree of any height hashes to a fixed placeholder and is
//      never materialised.
//   2. A subtree that contains exactly one key is represented by that leaf
//      alone (hash = leaf hash), instead of a 256-deep chain of nodes.
// The resulting structure is canonical: the root depends only on the set of
// (key, value) pairs, not on the insertion order.
#pragma once
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

#include "smt/hash.hpp"
#include "smt/node_store.hpp"
#include "smt/proof.hpp"

namespace smt {

struct TreeStats {
  std::size_t leaves = 0;
  std::size_t internal_nodes = 0;
  unsigned max_depth = 0;
  double avg_depth = 0.0;
};

class SparseMerkleTree {
 public:
  // keep_history=true keeps nodes of previous versions in the store, so that
  // older roots stay provable (versioned state DB). keep_history=false frees
  // nodes that are no longer referenced by the current root after each update.
  explicit SparseMerkleTree(NodeStore& store, bool keep_history = false);
  SparseMerkleTree(NodeStore& store, const Hash256& root, bool keep_history = false);

  const Hash256& root() const { return root_; }
  void set_root(const Hash256& root) { root_ = root; }  // checkout a version
  bool keep_history() const { return keep_history_; }
  NodeStore& store() { return store_; }
  const NodeStore& store() const { return store_; }

  // Insert or overwrite. Returns the new root.
  const Hash256& put(const Hash256& key, std::vector<uint8_t> value);
  // Delete (no-op if absent). Returns the new root.
  const Hash256& remove(const Hash256& key);

  bool get(const Hash256& key, std::vector<uint8_t>* value = nullptr, Hash256* value_hash = nullptr) const;
  bool contains(const Hash256& key) const { return get(key); }

  Proof prove(const Hash256& key) const { return prove_at(store_, root_, key); }

  // Static versions working on any (historical) root present in the store.
  static bool get_at(const NodeStore& store, const Hash256& root, const Hash256& key,
                     std::vector<uint8_t>* value = nullptr, Hash256* value_hash = nullptr);
  static Proof prove_at(const NodeStore& store, const Hash256& root, const Hash256& key);

  // Garbage-collect: delete every node not reachable from one of `keep_roots`.
  // Returns the number of nodes removed.
  std::size_t prune(const std::vector<Hash256>& keep_roots);

  TreeStats stats() const;
  void for_each_leaf(const std::function<void(const Hash256& key, const Hash256& value_hash,
                                              const std::vector<uint8_t>& value, unsigned depth)>& fn) const;

 private:
  struct Ref {
    Hash256 hash;
    NodeKind kind;
  };
  struct UpdateCtx {
    std::vector<Hash256> old_nodes;  // nodes replaced along the path
    std::vector<Hash256> new_nodes;  // nodes created during this update
  };

  static const Node& load(const NodeStore& store, const Hash256& h);
  Ref classify(const Hash256& h, bool hint_leaf) const;
  Ref make_internal(const Ref& left, const Ref& right, UpdateCtx& ctx);
  Ref update_rec(const Ref& node, unsigned depth, const Hash256& key, const Ref& new_leaf, UpdateCtx& ctx);
  void finish_update(const Ref& new_root, UpdateCtx& ctx);

  NodeStore& store_;
  Hash256 root_;
  bool keep_history_;
};

}  // namespace smt
