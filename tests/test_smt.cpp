#include <algorithm>
#include <map>
#include <random>
#include <set>

#include "reference_smt.hpp"
#include "smt/smt.hpp"
#include "test_framework.hpp"

using namespace smt;

namespace {

Hash256 random_key(std::mt19937_64& rng) {
  Hash256 k;
  for (std::size_t i = 0; i < 32; i += 8) {
    const uint64_t v = rng();
    for (int j = 0; j < 8; ++j) k[i + j] = uint8_t(v >> (8 * j));
  }
  return k;
}

std::vector<uint8_t> random_value(std::mt19937_64& rng) {
  std::vector<uint8_t> v(1 + rng() % 40);
  for (auto& b : v) b = uint8_t(rng());
  return v;
}

std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

}  // namespace

TEST(empty_tree) {
  MemoryNodeStore store;
  SparseMerkleTree t(store);
  CHECK(is_placeholder(t.root()));
  CHECK(!t.contains(sha256("nothing")));
  const Proof p = t.prove(sha256("nothing"));
  CHECK(p.kind == ProofKind::NonMembershipEmpty);
  CHECK_EQ(p.depth, 0);
  CHECK(p.siblings.empty());
  CHECK(verify_non_membership(p, t.root(), sha256("nothing")));
  // Deleting from an empty tree is a no-op.
  t.remove(sha256("nothing"));
  CHECK(is_placeholder(t.root()));
  CHECK_EQ(store.size(), 0u);
}

TEST(single_key) {
  MemoryNodeStore store;
  SparseMerkleTree t(store);
  const Hash256 k = hash_key("alice");
  t.put(k, bytes("100"));
  CHECK(t.root() == hash_leaf(k, hash_value(bytes("100"))));
  std::vector<uint8_t> v;
  CHECK(t.get(k, &v) && v == bytes("100"));
  const Proof p = t.prove(k);
  CHECK(p.kind == ProofKind::Membership);
  CHECK_EQ(p.depth, 0);
  CHECK(verify_membership(p, t.root(), k, bytes("100")));
  CHECK(!verify_membership(p, t.root(), k, bytes("101")));
  // Any other key: non-membership proven by the single (unrelated) leaf.
  const Hash256 other = hash_key("bob");
  const Proof q = t.prove(other);
  CHECK(q.kind == ProofKind::NonMembershipLeaf);
  CHECK(q.other_key == k);
  CHECK_EQ(q.depth, 0);
  CHECK(verify_non_membership(q, t.root(), other));
}

TEST(two_keys_match_reference) {
  MemoryNodeStore store;
  SparseMerkleTree t(store);
  std::map<Hash256, std::vector<uint8_t>> kv;
  kv[hash_key("alice")] = bytes("100");
  kv[hash_key("bob")] = bytes("250");
  for (auto& e : kv) t.put(e.first, e.second);
  CHECK(t.root() == reference_root(kv));
  for (auto& e : kv) {
    const Proof p = t.prove(e.first);
    CHECK(p.kind == ProofKind::Membership);
    CHECK(verify_membership(p, t.root(), e.first, e.second));
  }
}

TEST(regression_root_vector) {
  // Frozen root for a fixed data set; detects any accidental change of the
  // hashing / structure conventions.
  MemoryNodeStore store;
  SparseMerkleTree t(store);
  t.put(hash_key("alice"), bytes("100"));
  t.put(hash_key("bob"), bytes("250"));
  t.put(hash_key("carol"), bytes("7"));
  CHECK_EQ(to_hex(t.root()), "973afc726375a2f326e36025b339ef8933b8bb43da8e2a46bd49dc8960ad8f4c");
}

TEST(random_sets_match_reference_oracle) {
  std::mt19937_64 rng(12345);
  for (std::size_t n : {2u, 3u, 5u, 10u, 64u, 300u, 1000u}) {
    MemoryNodeStore store;
    SparseMerkleTree t(store);
    std::map<Hash256, std::vector<uint8_t>> kv;
    for (std::size_t i = 0; i < n; ++i) {
      const Hash256 k = random_key(rng);
      kv[k] = random_value(rng);
      t.put(k, kv[k]);
    }
    CHECK_MSG(t.root() == reference_root(kv), "insert n=" << n);
    // Overwrite a third of the values.
    std::size_t i = 0;
    for (auto& e : kv) {
      if (i++ % 3 == 0) {
        e.second = random_value(rng);
        t.put(e.first, e.second);
      }
    }
    CHECK_MSG(t.root() == reference_root(kv), "update n=" << n);
    // Delete half of the keys.
    std::vector<Hash256> keys;
    for (auto& e : kv) keys.push_back(e.first);
    for (std::size_t j = 0; j < keys.size(); j += 2) {
      kv.erase(keys[j]);
      t.remove(keys[j]);
    }
    CHECK_MSG(t.root() == reference_root(kv), "delete n=" << n);
    // Storage holds exactly the live nodes (no garbage) when history is off.
    const TreeStats s = t.stats();
    CHECK_EQ(s.leaves, kv.size());
    CHECK_EQ(store.size(), s.leaves + s.internal_nodes);
    // Delete the rest: the tree must return to the empty root with no nodes.
    for (auto& e : kv) t.remove(e.first);
    CHECK(is_placeholder(t.root()));
    CHECK_EQ(store.size(), 0u);
  }
}

TEST(root_is_independent_of_insertion_order) {
  std::mt19937_64 rng(7);
  std::vector<std::pair<Hash256, std::vector<uint8_t>>> items;
  for (int i = 0; i < 500; ++i) items.emplace_back(random_key(rng), random_value(rng));
  Hash256 first_root;
  for (int round = 0; round < 5; ++round) {
    std::shuffle(items.begin(), items.end(), rng);
    MemoryNodeStore store;
    SparseMerkleTree t(store);
    for (auto& it : items) t.put(it.first, it.second);
    if (round == 0) first_root = t.root();
    CHECK(t.root() == first_root);
  }
  // Same set reached through inserts + deletes of extra keys gives the same root.
  MemoryNodeStore store;
  SparseMerkleTree t(store);
  std::vector<Hash256> extra;
  for (int i = 0; i < 200; ++i) {
    extra.push_back(random_key(rng));
    t.put(extra.back(), bytes("tmp"));
  }
  for (auto& it : items) t.put(it.first, it.second);
  for (auto& k : extra) t.remove(k);
  CHECK(t.root() == first_root);
}

TEST(membership_and_non_membership_proofs_random) {
  std::mt19937_64 rng(99);
  MemoryNodeStore store;
  SparseMerkleTree t(store);
  std::map<Hash256, std::vector<uint8_t>> kv;
  for (int i = 0; i < 2000; ++i) {
    const Hash256 k = random_key(rng);
    kv[k] = random_value(rng);
    t.put(k, kv[k]);
  }
  const Hash256 root = t.root();
  std::size_t max_size = 0;
  for (auto& e : kv) {
    std::vector<uint8_t> v;
    CHECK(t.get(e.first, &v) && v == e.second);
    const Proof p = t.prove(e.first);
    CHECK(p.kind == ProofKind::Membership);
    CHECK(p.depth <= 256 && p.siblings.size() <= p.depth);
    CHECK(verify_membership(p, root, e.first, e.second));
    max_size = std::max(max_size, p.encoded_size());
  }
  CHECK(max_size < 1200);  // ~ (log2(2000)+few) siblings, far below the 8323-byte cap
  int empty_kind = 0, leaf_kind = 0;
  for (int i = 0; i < 2000; ++i) {
    const Hash256 k = random_key(rng);
    if (kv.count(k)) continue;
    CHECK(!t.contains(k));
    const Proof p = t.prove(k);
    CHECK(p.kind != ProofKind::Membership);
    if (p.kind == ProofKind::NonMembershipEmpty) ++empty_kind; else ++leaf_kind;
    std::string err;
    CHECK_MSG(verify_non_membership(p, root, k, &err), err);
    // The same proof must not verify for a different root or key.
    CHECK(!verify_non_membership(p, sha256("other root"), k));
    CHECK(!verify_proof(p, root, random_key(rng)));
  }
  CHECK(empty_kind > 0 && leaf_kind > 0);  // both non-membership shapes exercised
}

TEST(adversarial_keys_with_long_common_prefix) {
  // Keys sharing a 250-bit prefix force the maximum depth; everything must
  // still work and proofs must stay within the constant upper bound.
  MemoryNodeStore store;
  SparseMerkleTree t(store);
  std::map<Hash256, std::vector<uint8_t>> kv;
  for (uint8_t tail = 0; tail < 64; ++tail) {
    Hash256 k = sha256("shared-prefix");
    k[31] = uint8_t((k[31] & 0xC0) | tail);  // only the last 6 bits differ
    kv[k] = bytes("v" + std::to_string(tail));
    t.put(k, kv[k]);
  }
  CHECK(t.root() == reference_root(kv));
  const TreeStats s = t.stats();
  CHECK_EQ(s.max_depth, 256u);
  CHECK(s.avg_depth > 250.0);
  const Hash256 root = t.root();
  for (auto& e : kv) {
    const Proof p = t.prove(e.first);
    CHECK(p.depth >= 250 && p.depth <= 256);
    CHECK(p.encoded_size() <= 8323);  // constant upper bound
    CHECK(verify_membership(p, root, e.first, e.second));
  }
  // A key that shares the prefix but is absent: non-membership at depth ~250+.
  Hash256 absent = sha256("shared-prefix");
  absent[31] ^= 0x80;  // differs in bit 248 only
  const Proof q = t.prove(absent);
  CHECK(q.kind != ProofKind::Membership);
  CHECK(q.depth >= 248);
  CHECK(verify_non_membership(q, root, absent));
  // A totally unrelated key: non-membership at depth 0 against the single
  // deep chain (the whole chain hangs below one root-level internal node).
  const Hash256 far = sha256("unrelated");
  const Proof f = t.prove(far);
  CHECK(verify_non_membership(f, root, far));
  CHECK(f.encoded_size() < 200);
}

TEST(history_and_prune) {
  MemoryNodeStore store;
  SparseMerkleTree t(store, /*keep_history=*/true);
  const Hash256 k1 = hash_key("k1"), k2 = hash_key("k2"), k3 = hash_key("k3");
  t.put(k1, bytes("a"));
  const Hash256 r1 = t.root();
  t.put(k2, bytes("b"));
  const Hash256 r2 = t.root();
  t.put(k3, bytes("c"));
  t.put(k1, bytes("a2"));
  const Hash256 r3 = t.root();
  // Old versions remain fully provable.
  Proof p = SparseMerkleTree::prove_at(store, r1, k2);
  CHECK(p.kind != ProofKind::Membership && verify_non_membership(p, r1, k2));
  p = SparseMerkleTree::prove_at(store, r2, k2);
  CHECK(verify_membership(p, r2, k2, bytes("b")));
  p = SparseMerkleTree::prove_at(store, r1, k1);
  CHECK(verify_membership(p, r1, k1, bytes("a")));
  p = SparseMerkleTree::prove_at(store, r3, k1);
  CHECK(verify_membership(p, r3, k1, bytes("a2")));
  std::vector<uint8_t> v;
  CHECK(SparseMerkleTree::get_at(store, r1, k1, &v) && v == bytes("a"));
  // Prune to the current root only: old versions disappear, current is intact.
  const std::size_t before = store.size();
  const std::size_t removed = t.prune({r3});
  CHECK(removed > 0 && store.size() == before - removed);
  const TreeStats s = t.stats();
  CHECK_EQ(store.size(), s.leaves + s.internal_nodes);
  CHECK_THROWS(SparseMerkleTree::prove_at(store, r1, k1));
  CHECK(verify_membership(t.prove(k1), r3, k1, bytes("a2")));
  CHECK(verify_membership(t.prove(k3), r3, k3, bytes("c")));
}

TEST(no_history_frees_old_nodes) {
  std::mt19937_64 rng(3);
  MemoryNodeStore store;
  SparseMerkleTree t(store, /*keep_history=*/false);
  std::vector<Hash256> keys;
  for (int i = 0; i < 300; ++i) {
    keys.push_back(random_key(rng));
    t.put(keys.back(), random_value(rng));
    const TreeStats s = t.stats();
    CHECK_EQ(store.size(), s.leaves + s.internal_nodes);
  }
  for (int i = 0; i < 1000; ++i) {
    t.put(keys[rng() % keys.size()], random_value(rng));
    if (i % 100 == 0) {
      const TreeStats s = t.stats();
      CHECK_EQ(store.size(), s.leaves + s.internal_nodes);
    }
  }
  // Identical re-insert is a no-op that keeps the store consistent.
  std::vector<uint8_t> v;
  CHECK(t.get(keys[0], &v));
  const Hash256 r = t.root();
  t.put(keys[0], v);
  CHECK(t.root() == r);
  const TreeStats s = t.stats();
  CHECK_EQ(store.size(), s.leaves + s.internal_nodes);
}

TEST(stats_and_leaf_iteration) {
  std::mt19937_64 rng(42);
  MemoryNodeStore store;
  SparseMerkleTree t(store);
  std::set<Hash256> keys;
  for (int i = 0; i < 1000; ++i) {
    const Hash256 k = random_key(rng);
    keys.insert(k);
    t.put(k, bytes("x"));
  }
  const TreeStats s = t.stats();
  CHECK_EQ(s.leaves, keys.size());
  CHECK(s.internal_nodes >= keys.size() - 1);
  CHECK(s.avg_depth > 8.0 && s.avg_depth < 14.0);  // ~ log2(1000) + small constant
  CHECK(s.max_depth < 40);
  std::set<Hash256> seen;
  t.for_each_leaf([&](const Hash256& k, const Hash256& vh, const std::vector<uint8_t>& v, unsigned depth) {
    CHECK(vh == hash_value(v));
    CHECK(depth <= s.max_depth);
    seen.insert(k);
  });
  CHECK(seen == keys);
}
