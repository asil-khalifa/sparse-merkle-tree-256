#include <random>

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

std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

struct Fixture {
  MemoryNodeStore store;
  SparseMerkleTree tree{store};
  std::vector<Hash256> keys;
  Hash256 absent_empty{}, absent_leaf{};
  Fixture() {
    std::mt19937_64 rng(2024);
    for (int i = 0; i < 500; ++i) {
      keys.push_back(random_key(rng));
      tree.put(keys.back(), bytes("value-" + std::to_string(i)));
    }
    bool have_empty = false, have_leaf = false;
    while (!(have_empty && have_leaf)) {
      const Hash256 k = random_key(rng);
      const Proof p = tree.prove(k);
      if (p.kind == ProofKind::NonMembershipEmpty && !have_empty) { absent_empty = k; have_empty = true; }
      if (p.kind == ProofKind::NonMembershipLeaf && !have_leaf) { absent_leaf = k; have_leaf = true; }
    }
  }
};

bool proofs_equal(const Proof& a, const Proof& b) {
  return a.kind == b.kind && a.key == b.key && a.value_hash == b.value_hash && a.other_key == b.other_key &&
         a.depth == b.depth && a.bitmask == b.bitmask && a.siblings == b.siblings;
}

}  // namespace

TEST(proof_binary_roundtrip) {
  Fixture f;
  for (const Hash256& k : {f.keys[0], f.keys[123], f.absent_empty, f.absent_leaf}) {
    const Proof p = f.tree.prove(k);
    const std::vector<uint8_t> wire = p.serialize();
    CHECK_EQ(wire.size(), p.encoded_size());
    Proof q;
    std::string err;
    CHECK_MSG(Proof::deserialize(wire, q, &err), err);
    CHECK(proofs_equal(p, q));
    CHECK(verify_proof(q, f.tree.root(), k));
    // Truncation / trailing data are rejected.
    std::vector<uint8_t> t(wire.begin(), wire.end() - 1);
    CHECK(!Proof::deserialize(t, q));
    t = wire;
    t.push_back(0);
    CHECK(!Proof::deserialize(t, q));
  }
}

TEST(proof_json_roundtrip) {
  Fixture f;
  for (const Hash256& k : {f.keys[7], f.absent_empty, f.absent_leaf}) {
    const Proof p = f.tree.prove(k);
    const std::string json = p.to_json("some \"user\" key");
    Proof q;
    std::string err;
    CHECK_MSG(Proof::from_json(json, q, &err), err);
    CHECK(proofs_equal(p, q));
    CHECK(q.root == f.tree.root());
    CHECK(verify_proof(q, f.tree.root(), k));
  }
  Proof q;
  CHECK(!Proof::from_json("{\"type\": \"membership\"}", q));
  CHECK(!Proof::from_json("not json", q));
}

TEST(tampered_proofs_are_rejected) {
  Fixture f;
  const Hash256 root = f.tree.root();
  const Hash256 k = f.keys[42];
  const Proof good = f.tree.prove(k);
  CHECK(good.kind == ProofKind::Membership && !good.siblings.empty());
  CHECK(verify_membership(good, root, k, bytes("value-42")));

  Proof p = good;  // flip one bit of one sibling
  p.siblings[p.siblings.size() / 2][3] ^= 0x01;
  CHECK(!verify_proof(p, root, k));

  p = good;  // drop a sibling (count mismatch with bitmask)
  p.siblings.pop_back();
  CHECK(!verify_proof(p, root, k));

  p = good;  // claim one less level
  p.depth = uint16_t(p.depth - 1);
  CHECK(!verify_proof(p, root, k));

  p = good;  // wrong value hash
  p.value_hash[0] ^= 0xff;
  CHECK(!verify_proof(p, root, k));
  CHECK(!verify_membership(good, root, k, bytes("value-43")));

  p = good;  // relabel as non-membership: forging absence of a present key
  p.kind = ProofKind::NonMembershipEmpty;
  CHECK(!verify_non_membership(p, root, k));
  p.kind = ProofKind::NonMembershipLeaf;
  p.other_key = k;
  CHECK(!verify_non_membership(p, root, k));  // other_key == key is rejected
  p.other_key = f.keys[43];
  CHECK(!verify_non_membership(p, root, k));  // other leaf not on the path / wrong hash

  p = good;  // wrong root / wrong key
  CHECK(!verify_proof(p, sha256("wrong"), k));
  CHECK(!verify_proof(p, root, f.keys[43]));

  p = good;  // bitmask bit beyond depth
  set_bit(p.bitmask, 255);
  CHECK(!verify_proof(p, root, k));

  // Forging membership of an absent key from its non-membership proof.
  const Proof nm = f.tree.prove(f.absent_leaf);
  p = nm;
  p.kind = ProofKind::Membership;
  CHECK(!verify_proof(p, root, f.absent_leaf));
  p = f.tree.prove(f.absent_empty);
  p.kind = ProofKind::Membership;
  p.value_hash = hash_value(bytes("x"));
  CHECK(!verify_proof(p, root, f.absent_empty));
}

TEST(proof_size_bounds) {
  Fixture f;
  // Uncompressed 256-level proof: kind + depth + key + value hash + 256 siblings.
  const std::size_t naive = 1 + 2 + 32 + 32 + 256 * 32;
  for (const Hash256& k : f.keys) {
    const Proof p = f.tree.prove(k);
    CHECK(p.encoded_size() <= 8323);
    CHECK(p.encoded_size() * 6 < naive);  // >6x smaller than the naive proof for N=500
    CHECK(p.siblings.size() <= p.depth);
  }
}

TEST(proofs_against_historical_roots) {
  MemoryNodeStore store;
  SparseMerkleTree t(store, true);
  std::vector<Hash256> roots;
  std::mt19937_64 rng(5);
  std::vector<Hash256> keys;
  for (int i = 0; i < 50; ++i) {
    keys.push_back(random_key(rng));
    t.put(keys.back(), bytes(std::to_string(i)));
    roots.push_back(t.root());
  }
  // In version i, keys[0..i] are present and keys[i+1..] are absent.
  for (std::size_t i = 0; i < roots.size(); ++i) {
    for (std::size_t j = 0; j < keys.size(); ++j) {
      const Proof p = SparseMerkleTree::prove_at(store, roots[i], keys[j]);
      if (j <= i) CHECK(verify_membership(p, roots[i], keys[j], bytes(std::to_string(j))));
      else CHECK(verify_non_membership(p, roots[i], keys[j]));
    }
  }
}
