// Content-addressed node storage. Every node is stored under its own hash,
// exactly as a blockchain state database (e.g. a LevelDB/RocksDB column) would
// do. Because nodes are immutable and addressed by hash, several tree versions
// (roots) can share nodes, which gives cheap snapshots / history for free.
#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "smt/hash.hpp"

namespace smt {

enum class NodeKind : uint8_t { Leaf = 0, Internal = 1, Empty = 2 };

struct Node {
  NodeKind kind = NodeKind::Empty;
  Hash256 a{};  // Leaf: key path        | Internal: left child hash
  Hash256 b{};  // Leaf: value hash      | Internal: right child hash
  // Storage hints (not part of the hash): whether each child of an internal
  // node is a leaf. Saves one store lookup per level during updates.
  bool left_is_leaf = false;
  bool right_is_leaf = false;
  std::vector<uint8_t> value;  // Leaf only: raw value bytes

  static Node leaf(const Hash256& key, const Hash256& value_hash, std::vector<uint8_t> value) {
    Node n;
    n.kind = NodeKind::Leaf;
    n.a = key;
    n.b = value_hash;
    n.value = std::move(value);
    return n;
  }
  static Node internal(const Hash256& left, bool left_leaf, const Hash256& right, bool right_leaf) {
    Node n;
    n.kind = NodeKind::Internal;
    n.a = left;
    n.b = right;
    n.left_is_leaf = left_leaf;
    n.right_is_leaf = right_leaf;
    return n;
  }
  Hash256 hash() const {
    return kind == NodeKind::Leaf ? hash_leaf(a, b) : hash_node(a, b);
  }
};

class NodeStore {
 public:
  virtual ~NodeStore() = default;
  virtual const Node* find(const Hash256& h) const = 0;
  virtual void put(const Hash256& h, Node node) = 0;
  virtual bool erase(const Hash256& h) = 0;
  virtual std::size_t size() const = 0;
  virtual void clear() = 0;
  virtual void for_each(const std::function<void(const Hash256&, const Node&)>& fn) const = 0;
};

// In-memory hash map backend. A disk-backed KV store can implement the same
// interface without changing the tree logic.
class MemoryNodeStore final : public NodeStore {
 public:
  const Node* find(const Hash256& h) const override {
    auto it = map_.find(h);
    return it == map_.end() ? nullptr : &it->second;
  }
  void put(const Hash256& h, Node node) override { map_[h] = std::move(node); }
  bool erase(const Hash256& h) override { return map_.erase(h) > 0; }
  std::size_t size() const override { return map_.size(); }
  void clear() override { map_.clear(); }
  void for_each(const std::function<void(const Hash256&, const Node&)>& fn) const override {
    for (const auto& kv : map_) fn(kv.first, kv.second);
  }
  void reserve(std::size_t n) { map_.reserve(n); }

 private:
  std::unordered_map<Hash256, Node, Hash256Hasher> map_;
};

}  // namespace smt
