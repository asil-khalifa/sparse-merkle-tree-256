// Simple single-file persistence for the CLI "state database":
// header, version history (list of roots) and the content-addressed node set.
#pragma once
#include <string>
#include <vector>

#include "smt/node_store.hpp"

namespace smt {

struct Database {
  bool keep_history = true;
  std::vector<Hash256> roots;  // version history; roots.back() is the current root
  MemoryNodeStore store;

  const Hash256& current_root() const { return roots.empty() ? placeholder() : roots.back(); }
  void record_root(const Hash256& r) {
    if (roots.empty() || roots.back() != r) roots.push_back(r);
  }

  static bool load(const std::string& path, Database& db, std::string* err = nullptr);
  bool save(const std::string& path, std::string* err = nullptr) const;
};

}  // namespace smt
