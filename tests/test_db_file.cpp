#include <cstdio>
#include <random>

#include "smt/db_file.hpp"
#include "smt/smt.hpp"
#include "test_framework.hpp"

using namespace smt;

TEST(database_save_and_load) {
  const std::string path = "test_smt_db.tmp";
  Database db;
  db.keep_history = true;
  db.roots.push_back(placeholder());
  {
    SparseMerkleTree t(db.store, db.current_root(), true);
    std::mt19937_64 rng(11);
    for (int i = 0; i < 200; ++i) {
      Hash256 k;
      for (auto& b : k) b = uint8_t(rng());
      t.put(k, std::vector<uint8_t>{uint8_t(i), 0x00, 0xff, uint8_t(i * 7)});
      db.record_root(t.root());
    }
  }
  std::string err;
  CHECK_MSG(db.save(path, &err), err);
  Database loaded;
  CHECK_MSG(Database::load(path, loaded, &err), err);
  CHECK_EQ(loaded.keep_history, true);
  CHECK(loaded.roots == db.roots);
  CHECK_EQ(loaded.store.size(), db.store.size());
  // Every key of every version is still provable from the reloaded store.
  SparseMerkleTree a(db.store, db.current_root(), true), b(loaded.store, loaded.current_root(), true);
  a.for_each_leaf([&](const Hash256& k, const Hash256&, const std::vector<uint8_t>& v, unsigned) {
    std::vector<uint8_t> v2;
    CHECK(b.get(k, &v2) && v2 == v);
    CHECK(verify_membership(b.prove(k), b.root(), k, v));
  });
  for (std::size_t i = 0; i < loaded.roots.size(); i += 37) {
    Hash256 k;
    for (auto& x : k) x = uint8_t(i);
    const Proof p = SparseMerkleTree::prove_at(loaded.store, loaded.roots[i], k);
    CHECK(verify_proof(p, loaded.roots[i], k));
  }
  std::remove(path.c_str());
  Database missing;
  CHECK(!Database::load("does-not-exist.tmp", missing, &err));
}
