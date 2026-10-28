// Benchmark harness: throughput, latency, proof size and depth statistics for
// growing key sets, plus an adversarial (long common prefix) experiment.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "smt/smt.hpp"

using namespace smt;
using Clock = std::chrono::steady_clock;

namespace {

double us_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

long rss_kb() {
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line))
    if (line.rfind("VmRSS:", 0) == 0) return std::atol(line.c_str() + 6);
  return -1;
}

Hash256 random_key(std::mt19937_64& rng) {
  Hash256 k;
  for (std::size_t i = 0; i < 32; i += 8) {
    const uint64_t v = rng();
    std::memcpy(k.data() + i, &v, 8);
  }
  return k;
}

std::vector<uint8_t> random_value(std::mt19937_64& rng) {
  std::vector<uint8_t> v(32);
  for (std::size_t i = 0; i < 32; i += 8) {
    const uint64_t x = rng();
    std::memcpy(v.data() + i, &x, 8);
  }
  return v;
}

struct Agg {
  double sum = 0;
  std::size_t min = ~std::size_t(0), max = 0, n = 0;
  void add(std::size_t v) {
    sum += double(v);
    if (v < min) min = v;
    if (v > max) max = v;
    ++n;
  }
  double avg() const { return n ? sum / double(n) : 0; }
};

struct Row {
  std::string mode;
  std::size_t n = 0;
  double insert_us = 0, hashes_per_insert = 0, get_us = 0, update_us = 0, delete_us = 0;
  double prove_mem_us = 0, verify_mem_us = 0, prove_non_us = 0, verify_non_us = 0;
  Agg mem_bytes, mem_sibs, mem_depth, non_bytes, non_sibs, non_depth;
  std::size_t non_empty_kind = 0, non_leaf_kind = 0;
  TreeStats stats;
  std::size_t store_nodes = 0;
  long rss_kb = 0;
};

constexpr std::size_t NAIVE_PROOF_BYTES = 1 + 2 + 32 + 32 + 256 * 32;  // 8259

Row run(const std::string& mode, std::size_t n, std::size_t queries, uint64_t seed, bool history,
        unsigned shared_prefix_bits) {
  Row r;
  r.mode = mode;
  r.n = n;
  std::mt19937_64 rng(seed);
  std::vector<Hash256> keys(n);
  std::vector<std::vector<uint8_t>> values(n);
  const Hash256 prefix = sha256("adversarial-shared-prefix");
  for (std::size_t i = 0; i < n; ++i) {
    keys[i] = random_key(rng);
    if (shared_prefix_bits > 0) {  // force the first `shared_prefix_bits` bits to be identical
      for (unsigned b = 0; b < shared_prefix_bits; ++b) {
        if (get_bit(prefix, b)) set_bit(keys[i], b);
        else keys[i][b >> 3] &= uint8_t(~(1u << (7 - (b & 7))));
      }
    }
    values[i] = random_value(rng);
  }

  MemoryNodeStore store;
  store.reserve(2 * n + 16);
  SparseMerkleTree tree(store, history);

  reset_hash_invocations();
  auto t0 = Clock::now();
  for (std::size_t i = 0; i < n; ++i) tree.put(keys[i], values[i]);
  r.insert_us = us_since(t0) / double(n);
  r.hashes_per_insert = double(hash_invocations()) / double(n);
  const Hash256 root = tree.root();

  const std::size_t q = std::min(queries, n);
  std::vector<std::size_t> idx(q);
  for (auto& i : idx) i = rng() % n;

  t0 = Clock::now();
  std::size_t found = 0;
  for (std::size_t i : idx) found += tree.get(keys[i]);
  r.get_us = us_since(t0) / double(q);
  if (found != q) std::cerr << "BUG: lookups failed\n";

  std::vector<Proof> proofs;
  proofs.reserve(q);
  t0 = Clock::now();
  for (std::size_t i : idx) proofs.push_back(tree.prove(keys[i]));
  r.prove_mem_us = us_since(t0) / double(q);
  for (const Proof& p : proofs) {
    r.mem_bytes.add(p.encoded_size());
    r.mem_sibs.add(p.siblings.size());
    r.mem_depth.add(p.depth);
  }
  t0 = Clock::now();
  std::size_t ok = 0;
  for (std::size_t j = 0; j < q; ++j) ok += verify_membership(proofs[j], root, keys[idx[j]], values[idx[j]]);
  r.verify_mem_us = us_since(t0) / double(q);
  if (ok != q) std::cerr << "BUG: membership verification failed\n";

  std::vector<Hash256> absent(q);
  for (auto& k : absent) {
    k = random_key(rng);
    if (shared_prefix_bits > 0)
      for (unsigned b = 0; b < shared_prefix_bits; ++b) {
        if (get_bit(prefix, b)) set_bit(k, b);
        else k[b >> 3] &= uint8_t(~(1u << (7 - (b & 7))));
      }
  }
  proofs.clear();
  t0 = Clock::now();
  for (const Hash256& k : absent) proofs.push_back(tree.prove(k));
  r.prove_non_us = us_since(t0) / double(q);
  for (const Proof& p : proofs) {
    r.non_bytes.add(p.encoded_size());
    r.non_sibs.add(p.siblings.size());
    r.non_depth.add(p.depth);
    if (p.kind == ProofKind::NonMembershipEmpty) ++r.non_empty_kind;
    else if (p.kind == ProofKind::NonMembershipLeaf) ++r.non_leaf_kind;
    else std::cerr << "BUG: random key reported present\n";
  }
  t0 = Clock::now();
  ok = 0;
  for (std::size_t j = 0; j < q; ++j) ok += verify_non_membership(proofs[j], root, absent[j]);
  r.verify_non_us = us_since(t0) / double(q);
  if (ok != q) std::cerr << "BUG: non-membership verification failed\n";

  r.stats = tree.stats();
  r.store_nodes = store.size();
  r.rss_kb = rss_kb();

  t0 = Clock::now();
  for (std::size_t i : idx) tree.put(keys[i], random_value(rng));
  r.update_us = us_since(t0) / double(q);

  t0 = Clock::now();
  for (std::size_t i : idx) tree.remove(keys[i]);
  r.delete_us = us_since(t0) / double(q);
  return r;
}

void print_row(const Row& r) {
  std::printf("\n=== mode=%s  N=%zu ===\n", r.mode.c_str(), r.n);
  std::printf("insert      : %9.2f us/op  (%.0f ops/s, %.1f hashes/op)\n", r.insert_us, 1e6 / r.insert_us, r.hashes_per_insert);
  std::printf("get         : %9.2f us/op\n", r.get_us);
  std::printf("update      : %9.2f us/op\n", r.update_us);
  std::printf("delete      : %9.2f us/op\n", r.delete_us);
  std::printf("prove (mem) : %9.2f us/op   verify: %7.2f us/op\n", r.prove_mem_us, r.verify_mem_us);
  std::printf("prove (non) : %9.2f us/op   verify: %7.2f us/op\n", r.prove_non_us, r.verify_non_us);
  std::printf("membership proof     : %.1f bytes avg (min %zu, max %zu) | siblings avg %.1f max %zu | depth avg %.1f max %zu\n",
              r.mem_bytes.avg(), r.mem_bytes.min, r.mem_bytes.max, r.mem_sibs.avg(), r.mem_sibs.max, r.mem_depth.avg(), r.mem_depth.max);
  std::printf("non-membership proof : %.1f bytes avg (min %zu, max %zu) | siblings avg %.1f max %zu | depth avg %.1f max %zu | kinds: empty=%zu leaf=%zu\n",
              r.non_bytes.avg(), r.non_bytes.min, r.non_bytes.max, r.non_sibs.avg(), r.non_sibs.max, r.non_depth.avg(), r.non_depth.max,
              r.non_empty_kind, r.non_leaf_kind);
  std::printf("naive 256-sibling proof: %zu bytes  -> compaction %.1fx (membership), %.1fx (non-membership)\n",
              NAIVE_PROOF_BYTES, double(NAIVE_PROOF_BYTES) / r.mem_bytes.avg(), double(NAIVE_PROOF_BYTES) / r.non_bytes.avg());
  std::printf("tree: leaves=%zu internal=%zu avg_depth=%.2f max_depth=%u | store nodes=%zu | RSS=%.1f MB\n",
              r.stats.leaves, r.stats.internal_nodes, r.stats.avg_depth, r.stats.max_depth, r.store_nodes, r.rss_kb / 1024.0);
}

void write_csv_header(std::ostream& o) {
  o << "mode,n,insert_us,hashes_per_insert,get_us,update_us,delete_us,prove_mem_us,verify_mem_us,prove_non_us,verify_non_us,"
       "mem_bytes_avg,mem_bytes_min,mem_bytes_max,mem_sibs_avg,mem_sibs_max,mem_depth_avg,mem_depth_max,"
       "non_bytes_avg,non_bytes_min,non_bytes_max,non_sibs_avg,non_sibs_max,non_depth_avg,non_depth_max,"
       "non_empty_kind,non_leaf_kind,leaves,internal_nodes,avg_depth,max_depth,store_nodes,rss_mb,naive_proof_bytes\n";
}

void write_csv_row(std::ostream& o, const Row& r) {
  o << r.mode << ',' << r.n << ',' << r.insert_us << ',' << r.hashes_per_insert << ',' << r.get_us << ',' << r.update_us << ','
    << r.delete_us << ',' << r.prove_mem_us << ',' << r.verify_mem_us << ',' << r.prove_non_us << ',' << r.verify_non_us << ','
    << r.mem_bytes.avg() << ',' << r.mem_bytes.min << ',' << r.mem_bytes.max << ',' << r.mem_sibs.avg() << ',' << r.mem_sibs.max << ','
    << r.mem_depth.avg() << ',' << r.mem_depth.max << ',' << r.non_bytes.avg() << ',' << r.non_bytes.min << ',' << r.non_bytes.max << ','
    << r.non_sibs.avg() << ',' << r.non_sibs.max << ',' << r.non_depth.avg() << ',' << r.non_depth.max << ',' << r.non_empty_kind << ','
    << r.non_leaf_kind << ',' << r.stats.leaves << ',' << r.stats.internal_nodes << ',' << r.stats.avg_depth << ',' << r.stats.max_depth
    << ',' << r.store_nodes << ',' << r.rss_kb / 1024.0 << ',' << NAIVE_PROOF_BYTES << '\n';
}

void hash_microbench() {
  const Hash256 a = sha256("a"), b = sha256("b");
  const std::size_t n = 2000000;
  Hash256 acc = a;
  const auto t0 = Clock::now();
  for (std::size_t i = 0; i < n; ++i) acc = hash_node(acc, b);
  const double us = us_since(t0);
  std::printf("SHA-256 node hash (65-byte input): %.3f us/hash, %.2f M hashes/s  [%02x..]\n", us / n, n / us, acc[0]);
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::size_t> sizes = {1000, 10000, 100000, 1000000};
  std::size_t queries = 10000;
  std::string csv;
  bool history = false, adversarial = true;
  uint64_t seed = 20261028;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--sizes" && i + 1 < argc) {
      sizes.clear();
      std::stringstream ss(argv[++i]);
      std::string tok;
      while (std::getline(ss, tok, ',')) sizes.push_back(std::stoull(tok));
    } else if (a == "--queries" && i + 1 < argc) queries = std::stoull(argv[++i]);
    else if (a == "--csv" && i + 1 < argc) csv = argv[++i];
    else if (a == "--history") history = true;
    else if (a == "--no-adversarial") adversarial = false;
    else if (a == "--seed" && i + 1 < argc) seed = std::stoull(argv[++i]);
    else {
      std::printf("usage: smt_bench [--sizes a,b,c] [--queries Q] [--csv FILE] [--history] [--no-adversarial] [--seed S]\n");
      return a == "-h" || a == "--help" ? 0 : 1;
    }
  }
  std::printf("Sparse Merkle Tree benchmark (history=%s, queries per size=%zu, seed=%llu)\n", history ? "on" : "off", queries,
              (unsigned long long)seed);
  hash_microbench();
  std::vector<Row> rows;
  for (std::size_t n : sizes) {
    rows.push_back(run("random", n, queries, seed, history, 0));
    print_row(rows.back());
    std::fflush(stdout);
  }
  if (adversarial) {
    // Worst case: all keys share a 200-bit prefix -> leaves sit at depth >= 200.
    rows.push_back(run("adversarial-200bit-prefix", 1000, std::min<std::size_t>(queries, 1000), seed, history, 200));
    print_row(rows.back());
  }
  if (!csv.empty()) {
    std::ofstream f(csv);
    write_csv_header(f);
    for (const Row& r : rows) write_csv_row(f, r);
    std::printf("\nCSV written to %s\n", csv.c_str());
  }
  return 0;
}
