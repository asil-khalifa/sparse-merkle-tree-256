// smt -- command line front-end: a tiny versioned state database backed by the
// 256-level Sparse Merkle Tree, with proof generation and offline verification.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "smt/db_file.hpp"
#include "smt/smt.hpp"

using namespace smt;

namespace {

struct Options {
  std::string db = "smt.db";
  bool raw_key = false;    // key argument is a 64-hex-char path instead of a string
  bool hex_value = false;  // value argument is hex instead of a string
  std::string command;
  std::vector<std::string> args;
  std::string root_hex;    // --root
  std::string out_file;    // --out
  std::string key_opt;     // --key
  std::string value_opt;   // --value
  bool no_history = false;
  bool has_value_opt = false;
};

void usage() {
  std::cout <<
      "smt - 256-level Sparse Merkle Tree state database (roll no. 106123015)\n\n"
      "Usage: smt [global options] <command> [arguments]\n\n"
      "Global options:\n"
      "  --db PATH        database file (default: smt.db)\n"
      "  --raw-key        treat keys as 64-hex-char 256-bit paths (default: path = SHA-256(key))\n"
      "  --hex-value      treat values as hex bytes (default: UTF-8 string)\n\n"
      "Commands:\n"
      "  init [--no-history]                 create an empty database\n"
      "  put <key> <value>                   insert or update a key\n"
      "  del <key>                           delete a key\n"
      "  get <key>                           look up a key\n"
      "  root                                print the current state root\n"
      "  history                             list all committed roots (versions)\n"
      "  prove <key> [--root HEX] [--out F]  generate a membership / non-membership proof (JSON)\n"
      "  verify <proof.json> [--root HEX] [--key K] [--value V]\n"
      "                                      verify a proof offline (no database needed with --root)\n"
      "  batch <file>                        apply lines: 'put <key> <value>' or 'del <key>'\n"
      "  dump                                list leaves (key path, depth, value) of the current version\n"
      "  stats                               tree and storage statistics\n"
      "  prune                               drop nodes unreachable from the current root\n"
      "  hash <key>                          print the 256-bit path of a key\n";
}

bool parse_args(int argc, char** argv, Options& o) {
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](std::string& dst) {
      if (i + 1 >= argc) { std::cerr << "error: " << a << " needs an argument\n"; return false; }
      dst = argv[++i];
      return true;
    };
    if (a == "--db") { if (!next(o.db)) return false; }
    else if (a == "--raw-key") o.raw_key = true;
    else if (a == "--hex-value") o.hex_value = true;
    else if (a == "--root") { if (!next(o.root_hex)) return false; }
    else if (a == "--out" || a == "-o") { if (!next(o.out_file)) return false; }
    else if (a == "--key") { if (!next(o.key_opt)) return false; }
    else if (a == "--value") { if (!next(o.value_opt)) return false; o.has_value_opt = true; }
    else if (a == "--no-history") o.no_history = true;
    else if (a == "-h" || a == "--help") { usage(); std::exit(0); }
    else if (!a.empty() && a[0] == '-') { std::cerr << "error: unknown option " << a << "\n"; return false; }
    else positional.push_back(a);
  }
  if (positional.empty()) { usage(); return false; }
  o.command = positional[0];
  o.args.assign(positional.begin() + 1, positional.end());
  return true;
}

bool parse_key(const Options& o, const std::string& s, Hash256& out) {
  if (o.raw_key) {
    if (!from_hex(s, out)) { std::cerr << "error: --raw-key expects 64 hex characters\n"; return false; }
    return true;
  }
  out = hash_key(s);
  return true;
}

bool parse_value(const Options& o, const std::string& s, std::vector<uint8_t>& out) {
  if (o.hex_value) {
    if (!from_hex_bytes(s, out)) { std::cerr << "error: --hex-value expects hex bytes\n"; return false; }
    return true;
  }
  out.assign(s.begin(), s.end());
  return true;
}

std::string show_value(const std::vector<uint8_t>& v) {
  bool printable = std::all_of(v.begin(), v.end(), [](uint8_t c) { return c >= 0x20 && c < 0x7f; });
  if (printable) return std::string(v.begin(), v.end());
  return "0x" + to_hex(v);
}

bool load_db(const Options& o, Database& db) {
  std::string err;
  if (!Database::load(o.db, db, &err)) {
    std::cerr << "error: " << err << " (run 'smt --db " << o.db << " init' first)\n";
    return false;
  }
  return true;
}

bool save_db(const Options& o, const Database& db) {
  std::string err;
  if (!db.save(o.db, &err)) { std::cerr << "error: " << err << "\n"; return false; }
  return true;
}

int cmd_init(const Options& o) {
  Database db;
  db.keep_history = !o.no_history;
  db.roots.push_back(placeholder());
  if (!save_db(o, db)) return 1;
  std::cout << "created " << o.db << " (history " << (db.keep_history ? "on" : "off") << ")\n"
            << "root: " << to_hex(db.current_root()) << "\n";
  return 0;
}

int apply_mutations(const Options& o, const std::vector<std::pair<bool, std::pair<std::string, std::string>>>& ops) {
  Database db;
  if (!load_db(o, db)) return 1;
  SparseMerkleTree tree(db.store, db.current_root(), db.keep_history);
  const Hash256 before = tree.root();
  for (const auto& op : ops) {
    Hash256 k;
    if (!parse_key(o, op.second.first, k)) return 1;
    if (op.first) {
      std::vector<uint8_t> v;
      if (!parse_value(o, op.second.second, v)) return 1;
      tree.put(k, std::move(v));
    } else {
      tree.remove(k);
    }
  }
  db.record_root(tree.root());
  if (!save_db(o, db)) return 1;
  std::cout << "applied " << ops.size() << " operation(s)\n"
            << "old root: " << to_hex(before) << "\n"
            << "new root: " << to_hex(tree.root()) << "\n";
  return 0;
}

int cmd_put(const Options& o) {
  if (o.args.size() != 2) { std::cerr << "usage: smt put <key> <value>\n"; return 1; }
  return apply_mutations(o, {{true, {o.args[0], o.args[1]}}});
}

int cmd_del(const Options& o) {
  if (o.args.size() != 1) { std::cerr << "usage: smt del <key>\n"; return 1; }
  return apply_mutations(o, {{false, {o.args[0], ""}}});
}

int cmd_batch(const Options& o) {
  if (o.args.size() != 1) { std::cerr << "usage: smt batch <file>\n"; return 1; }
  std::ifstream f(o.args[0]);
  if (!f) { std::cerr << "error: cannot open " << o.args[0] << "\n"; return 1; }
  std::vector<std::pair<bool, std::pair<std::string, std::string>>> ops;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string verb, key, value;
    ls >> verb >> key;
    std::getline(ls >> std::ws, value);
    if (verb == "put") ops.push_back({true, {key, value}});
    else if (verb == "del") ops.push_back({false, {key, ""}});
    else { std::cerr << "error: bad line: " << line << "\n"; return 1; }
  }
  return apply_mutations(o, ops);
}

int cmd_get(const Options& o) {
  if (o.args.size() != 1) { std::cerr << "usage: smt get <key>\n"; return 1; }
  Database db;
  if (!load_db(o, db)) return 1;
  Hash256 k;
  if (!parse_key(o, o.args[0], k)) return 1;
  Hash256 root = db.current_root();
  if (!o.root_hex.empty() && !from_hex(o.root_hex, root)) { std::cerr << "error: bad --root\n"; return 1; }
  std::vector<uint8_t> v;
  Hash256 vh;
  if (!SparseMerkleTree::get_at(db.store, root, k, &v, &vh)) {
    std::cout << "key not found\n";
    return 2;
  }
  std::cout << "value: " << show_value(v) << "\nvalue_hash: " << to_hex(vh) << "\n";
  return 0;
}

int cmd_root(const Options& o) {
  Database db;
  if (!load_db(o, db)) return 1;
  std::cout << to_hex(db.current_root()) << "\n";
  return 0;
}

int cmd_history(const Options& o) {
  Database db;
  if (!load_db(o, db)) return 1;
  std::cout << "history: " << (db.keep_history ? "kept (all versions provable)" : "not kept (only current version provable)") << "\n";
  for (std::size_t i = 0; i < db.roots.size(); ++i)
    std::cout << "v" << i << (i + 1 == db.roots.size() ? " (current) " : "           ") << to_hex(db.roots[i]) << "\n";
  return 0;
}

int cmd_prove(const Options& o) {
  if (o.args.size() != 1) { std::cerr << "usage: smt prove <key> [--root HEX] [--out FILE]\n"; return 1; }
  Database db;
  if (!load_db(o, db)) return 1;
  Hash256 k;
  if (!parse_key(o, o.args[0], k)) return 1;
  Hash256 root = db.current_root();
  if (!o.root_hex.empty() && !from_hex(o.root_hex, root)) { std::cerr << "error: bad --root\n"; return 1; }
  Proof p;
  try {
    p = SparseMerkleTree::prove_at(db.store, root, k);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  const std::string json = p.to_json(o.raw_key ? std::string_view{} : std::string_view(o.args[0]));
  if (o.out_file.empty()) {
    std::cout << json;
  } else {
    std::ofstream f(o.out_file);
    f << json;
    std::cout << "proof type: " << proof_kind_name(p.kind) << "\n"
              << "depth: " << p.depth << ", non-empty siblings: " << p.siblings.size()
              << ", size: " << p.encoded_size() << " bytes (uncompressed 256-level proof would be "
              << (3 + 32 + 32 + 256 * 32) << " bytes)\n"
              << "written to " << o.out_file << "\n";
  }
  return 0;
}

int cmd_verify(const Options& o) {
  if (o.args.size() != 1) { std::cerr << "usage: smt verify <proof.json> [--root HEX] [--key K] [--value V]\n"; return 1; }
  std::ifstream f(o.args[0]);
  if (!f) { std::cerr << "error: cannot open " << o.args[0] << "\n"; return 1; }
  std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  Proof p;
  std::string err;
  if (!Proof::from_json(text, p, &err)) { std::cerr << "error: " << err << "\n"; return 1; }

  // The trusted root comes from --root, else from the local database, else
  // (untrusted!) from the proof file itself.
  Hash256 root;
  std::string root_source;
  if (!o.root_hex.empty()) {
    if (!from_hex(o.root_hex, root)) { std::cerr << "error: bad --root\n"; return 1; }
    root_source = "--root";
  } else {
    Database db;
    if (Database::load(o.db, db)) { root = db.current_root(); root_source = "database " + o.db; }
    else { root = p.root; root_source = "proof file (UNTRUSTED)"; }
  }
  Hash256 key = p.key;
  if (!o.key_opt.empty() && !parse_key(o, o.key_opt, key)) return 1;

  bool ok;
  if (o.has_value_opt) {
    std::vector<uint8_t> v;
    if (!parse_value(o, o.value_opt, v)) return 1;
    ok = verify_membership(p, root, key, v, &err);
  } else {
    ok = verify_proof(p, root, key, &err);
  }
  std::cout << "proof type : " << proof_kind_name(p.kind) << "\n"
            << "key        : " << to_hex(key) << "\n"
            << "root       : " << to_hex(root) << " (from " << root_source << ")\n"
            << "depth      : " << p.depth << ", siblings: " << p.siblings.size() << ", size: " << p.encoded_size() << " bytes\n";
  if (ok) {
    std::cout << "result     : VALID -- " << (p.is_membership() ? "key is present with the stated value" : "key is NOT present in the tree") << "\n";
    return 0;
  }
  std::cout << "result     : INVALID -- " << err << "\n";
  return 3;
}

int cmd_dump(const Options& o) {
  Database db;
  if (!load_db(o, db)) return 1;
  SparseMerkleTree tree(db.store, db.current_root(), db.keep_history);
  std::size_t n = 0;
  tree.for_each_leaf([&](const Hash256& k, const Hash256&, const std::vector<uint8_t>& v, unsigned depth) {
    std::cout << to_hex(k) << "  depth=" << depth << "  value=" << show_value(v) << "\n";
    ++n;
  });
  std::cout << n << " leaf/leaves\n";
  return 0;
}

int cmd_stats(const Options& o) {
  Database db;
  if (!load_db(o, db)) return 1;
  SparseMerkleTree tree(db.store, db.current_root(), db.keep_history);
  const TreeStats s = tree.stats();
  std::cout << "root            : " << to_hex(tree.root()) << "\n"
            << "versions        : " << db.roots.size() << "\n"
            << "leaves (keys)   : " << s.leaves << "\n"
            << "internal nodes  : " << s.internal_nodes << "\n"
            << "avg leaf depth  : " << s.avg_depth << "\n"
            << "max leaf depth  : " << s.max_depth << " (logical tree depth is 256)\n"
            << "stored nodes    : " << db.store.size() << " (all versions)\n";
  return 0;
}

int cmd_prune(const Options& o) {
  Database db;
  if (!load_db(o, db)) return 1;
  SparseMerkleTree tree(db.store, db.current_root(), db.keep_history);
  const std::size_t removed = tree.prune({tree.root()});
  const Hash256 cur = tree.root();
  db.roots.clear();
  db.roots.push_back(cur);
  if (!save_db(o, db)) return 1;
  std::cout << "removed " << removed << " unreachable node(s); " << db.store.size() << " remain; history cleared\n";
  return 0;
}

int cmd_hash(const Options& o) {
  if (o.args.size() != 1) { std::cerr << "usage: smt hash <key>\n"; return 1; }
  Hash256 k;
  if (!parse_key(o, o.args[0], k)) return 1;
  std::cout << to_hex(k) << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  if (!parse_args(argc, argv, o)) return 1;
  try {
    if (o.command == "init") return cmd_init(o);
    if (o.command == "put") return cmd_put(o);
    if (o.command == "del") return cmd_del(o);
    if (o.command == "get") return cmd_get(o);
    if (o.command == "root") return cmd_root(o);
    if (o.command == "history") return cmd_history(o);
    if (o.command == "prove") return cmd_prove(o);
    if (o.command == "verify") return cmd_verify(o);
    if (o.command == "batch") return cmd_batch(o);
    if (o.command == "dump") return cmd_dump(o);
    if (o.command == "stats") return cmd_stats(o);
    if (o.command == "prune") return cmd_prune(o);
    if (o.command == "hash") return cmd_hash(o);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  std::cerr << "error: unknown command '" << o.command << "'\n";
  usage();
  return 1;
}
