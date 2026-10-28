#include "smt/smt.hpp"

#include <algorithm>
#include <unordered_set>

namespace smt {

SparseMerkleTree::SparseMerkleTree(NodeStore& store, bool keep_history)
    : store_(store), root_(placeholder()), keep_history_(keep_history) {}

SparseMerkleTree::SparseMerkleTree(NodeStore& store, const Hash256& root, bool keep_history)
    : store_(store), root_(root), keep_history_(keep_history) {}

const Node& SparseMerkleTree::load(const NodeStore& store, const Hash256& h) {
  const Node* n = store.find(h);
  if (!n) throw std::runtime_error("SMT: missing node " + to_hex(h) + " (pruned or corrupt store)");
  return *n;
}

// Resolve the kind of a child from the parent's storage hint: no lookup needed.
SparseMerkleTree::Ref SparseMerkleTree::classify(const Hash256& h, bool hint_leaf) const {
  if (is_placeholder(h)) return Ref{h, NodeKind::Empty};
  return Ref{h, hint_leaf ? NodeKind::Leaf : NodeKind::Internal};
}

// Build the parent of (left, right) applying the two collapse rules.
SparseMerkleTree::Ref SparseMerkleTree::make_internal(const Ref& l, const Ref& r, UpdateCtx& ctx) {
  if (l.kind == NodeKind::Empty && r.kind == NodeKind::Empty) return Ref{placeholder(), NodeKind::Empty};
  if (l.kind == NodeKind::Empty && r.kind == NodeKind::Leaf) return r;  // single leaf: collapse
  if (r.kind == NodeKind::Empty && l.kind == NodeKind::Leaf) return l;  // single leaf: collapse
  const Hash256 h = hash_node(l.hash, r.hash);
  store_.put(h, Node::internal(l.hash, l.kind == NodeKind::Leaf, r.hash, r.kind == NodeKind::Leaf));
  ctx.new_nodes.push_back(h);
  return Ref{h, NodeKind::Internal};
}

// Recursively rebuild the path of `key` below `node` (which sits at `depth`),
// replacing the leaf for `key` by `new_leaf` (kind Empty => deletion).
SparseMerkleTree::Ref SparseMerkleTree::update_rec(const Ref& node, unsigned depth, const Hash256& key,
                                                   const Ref& new_leaf, UpdateCtx& ctx) {
  if (depth > KEY_BITS) throw std::runtime_error("SMT: corrupt tree (depth > 256)");
  switch (node.kind) {
    case NodeKind::Empty:
      // Empty subtree: the new leaf (or nothing, on delete) takes its place.
      return new_leaf;

    case NodeKind::Leaf: {
      const Hash256 leaf_key = load(store_, node.hash).a;
      if (leaf_key == key) {  // overwrite or delete this exact key
        ctx.old_nodes.push_back(node.hash);
        return new_leaf;
      }
      if (new_leaf.kind == NodeKind::Empty) return node;  // deleting an absent key: no change
      // Two different keys below this position: push both down to the first
      // bit where they diverge and hang them under a chain of internal nodes.
      const unsigned d = common_prefix_len(key, leaf_key);  // depth <= d <= 255
      if (d < depth) throw std::runtime_error("SMT: corrupt tree (leaf off its path)");
      Ref cur = get_bit(key, d) ? make_internal(node, new_leaf, ctx) : make_internal(new_leaf, node, ctx);
      const Ref empty{placeholder(), NodeKind::Empty};
      for (unsigned i = d; i-- > depth;)
        cur = get_bit(key, i) ? make_internal(empty, cur, ctx) : make_internal(cur, empty, ctx);
      return cur;
    }

    case NodeKind::Internal: {
      const Node& n = load(store_, node.hash);
      const Hash256 lh = n.a, rh = n.b;  // copy before the store is mutated
      const bool ll = n.left_is_leaf, rl = n.right_is_leaf;
      const bool bit = get_bit(key, depth);
      const Ref child = classify(bit ? rh : lh, bit ? rl : ll);
      const Ref other = classify(bit ? lh : rh, bit ? ll : rl);
      const Ref new_child = update_rec(child, depth + 1, key, new_leaf, ctx);
      if (new_child.hash == child.hash) return node;  // nothing changed below
      ctx.old_nodes.push_back(node.hash);
      return bit ? make_internal(other, new_child, ctx) : make_internal(new_child, other, ctx);
    }
  }
  return node;
}

void SparseMerkleTree::finish_update(const Ref& new_root, UpdateCtx& ctx) {
  root_ = new_root.hash;
  if (keep_history_) return;
  // Free nodes of the previous version that the new version no longer uses.
  for (const Hash256& h : ctx.old_nodes)
    if (std::find(ctx.new_nodes.begin(), ctx.new_nodes.end(), h) == ctx.new_nodes.end()) store_.erase(h);
}

const Hash256& SparseMerkleTree::put(const Hash256& key, std::vector<uint8_t> value) {
  const Hash256 vh = hash_value(value);
  const Hash256 lh = hash_leaf(key, vh);
  UpdateCtx ctx;
  if (!store_.find(lh)) store_.put(lh, Node::leaf(key, vh, std::move(value)));
  ctx.new_nodes.push_back(lh);
  const Ref root_ref = is_placeholder(root_) ? Ref{root_, NodeKind::Empty} : Ref{root_, load(store_, root_).kind};
  const Ref r = update_rec(root_ref, 0, key, Ref{lh, NodeKind::Leaf}, ctx);
  finish_update(r, ctx);
  return root_;
}

const Hash256& SparseMerkleTree::remove(const Hash256& key) {
  UpdateCtx ctx;
  const Ref root_ref = is_placeholder(root_) ? Ref{root_, NodeKind::Empty} : Ref{root_, load(store_, root_).kind};
  const Ref r = update_rec(root_ref, 0, key, Ref{placeholder(), NodeKind::Empty}, ctx);
  finish_update(r, ctx);
  return root_;
}

bool SparseMerkleTree::get(const Hash256& key, std::vector<uint8_t>* value, Hash256* value_hash) const {
  return get_at(store_, root_, key, value, value_hash);
}

bool SparseMerkleTree::get_at(const NodeStore& store, const Hash256& root, const Hash256& key,
                              std::vector<uint8_t>* value, Hash256* value_hash) {
  Hash256 cur = root;
  unsigned depth = 0;
  while (!is_placeholder(cur)) {
    const Node& n = load(store, cur);
    if (n.kind == NodeKind::Leaf) {
      if (n.a != key) return false;
      if (value) *value = n.value;
      if (value_hash) *value_hash = n.b;
      return true;
    }
    if (depth >= KEY_BITS) throw std::runtime_error("SMT: corrupt tree (depth > 256)");
    cur = get_bit(key, depth) ? n.b : n.a;
    ++depth;
  }
  return false;
}

Proof SparseMerkleTree::prove_at(const NodeStore& store, const Hash256& root, const Hash256& key) {
  Proof p;
  p.root = root;
  p.key = key;
  Hash256 cur = root;
  unsigned depth = 0;
  while (true) {
    if (is_placeholder(cur)) {
      p.kind = ProofKind::NonMembershipEmpty;
      break;
    }
    const Node& n = load(store, cur);
    if (n.kind == NodeKind::Leaf) {
      if (n.a == key) {
        p.kind = ProofKind::Membership;
        p.value_hash = n.b;
      } else {
        p.kind = ProofKind::NonMembershipLeaf;
        p.other_key = n.a;
        p.value_hash = n.b;
      }
      break;
    }
    if (depth >= KEY_BITS) throw std::runtime_error("SMT: corrupt tree (depth > 256)");
    const bool bit = get_bit(key, depth);
    const Hash256& sib = bit ? n.a : n.b;
    if (!is_placeholder(sib)) {  // compaction: omit empty siblings
      set_bit(p.bitmask, depth);
      p.siblings.push_back(sib);
    }
    cur = bit ? n.b : n.a;
    ++depth;
  }
  p.depth = uint16_t(depth);
  return p;
}

std::size_t SparseMerkleTree::prune(const std::vector<Hash256>& keep_roots) {
  std::unordered_set<Hash256, Hash256Hasher> live;
  std::vector<Hash256> stack(keep_roots.begin(), keep_roots.end());
  while (!stack.empty()) {
    const Hash256 h = stack.back();
    stack.pop_back();
    if (is_placeholder(h) || live.count(h)) continue;
    const Node* n = store_.find(h);
    if (!n) continue;
    live.insert(h);
    if (n->kind == NodeKind::Internal) {
      stack.push_back(n->a);
      stack.push_back(n->b);
    }
  }
  std::vector<Hash256> dead;
  store_.for_each([&](const Hash256& h, const Node&) {
    if (!live.count(h)) dead.push_back(h);
  });
  for (const Hash256& h : dead) store_.erase(h);
  return dead.size();
}

void SparseMerkleTree::for_each_leaf(
    const std::function<void(const Hash256&, const Hash256&, const std::vector<uint8_t>&, unsigned)>& fn) const {
  struct Item {
    Hash256 h;
    unsigned depth;
  };
  std::vector<Item> stack;
  if (!is_placeholder(root_)) stack.push_back({root_, 0});
  while (!stack.empty()) {
    const Item it = stack.back();
    stack.pop_back();
    const Node& n = load(store_, it.h);
    if (n.kind == NodeKind::Leaf) {
      fn(n.a, n.b, n.value, it.depth);
      continue;
    }
    if (!is_placeholder(n.b)) stack.push_back({n.b, it.depth + 1});
    if (!is_placeholder(n.a)) stack.push_back({n.a, it.depth + 1});
  }
}

TreeStats SparseMerkleTree::stats() const {
  TreeStats s;
  double depth_sum = 0;
  struct Item {
    Hash256 h;
    unsigned depth;
  };
  std::vector<Item> stack;
  if (!is_placeholder(root_)) stack.push_back({root_, 0});
  while (!stack.empty()) {
    const Item it = stack.back();
    stack.pop_back();
    const Node& n = load(store_, it.h);
    if (n.kind == NodeKind::Leaf) {
      ++s.leaves;
      depth_sum += it.depth;
      s.max_depth = std::max(s.max_depth, it.depth);
      continue;
    }
    ++s.internal_nodes;
    if (!is_placeholder(n.b)) stack.push_back({n.b, it.depth + 1});
    if (!is_placeholder(n.a)) stack.push_back({n.a, it.depth + 1});
  }
  s.avg_depth = s.leaves ? depth_sum / double(s.leaves) : 0.0;
  return s;
}

}  // namespace smt
