# Optimized 256-level Sparse Merkle Tree with Compact Non-Membership Proofs

**CSPE71 Advanced Cryptography — Mini Project**
National Institute of Technology Tiruchirappalli, Department of CSE
VII Semester B.Tech (CSE), Academic Year 2026–27

| | |
|---|---|
| **Roll No.** | 106123015 |
| **Name** | ASIL KHALIFA |
| **Problem statement** | Program an optimized 256-level Sparse Merkle Tree in C++/Rust generating constant-size O(log N) cryptographic proofs for key non-existence in state databases. |
| **Language** | C++17 (no external dependencies; SHA-256 implemented in-tree) |

## What is in this archive

```
106123015_MiniProject/
├── README.md                    <- this file (setup + usage)
├── CMakeLists.txt / Makefile    <- build (CMake primary, plain make fallback)
├── include/smt/                 <- public headers (library API)
│   ├── sha256.hpp               SHA-256 (FIPS 180-4)
│   ├── hash.hpp                 hashing conventions, bit helpers, hex utils
│   ├── node_store.hpp           content-addressed node storage interface + in-memory backend
│   ├── smt.hpp                  the SparseMerkleTree (put / remove / get / prove / prune)
│   ├── proof.hpp                compact proofs, verifier, JSON + binary encodings
│   ├── db_file.hpp              single-file persistence used by the CLI
│   └── json.hpp                 minimal JSON reader/writer
├── src/                         <- implementation (+ src/cli/main.cpp = the `smt` CLI tool)
├── tests/                       <- 23 unit tests, naive reference oracle, Python cross-check
├── bench/                       <- benchmark harness (bench.cpp) and plot script (plot.py)
├── scripts/                     <- run_all.sh (build+test+bench+demo), sample_session.sh (CLI demo)
├── evaluation/                  <- SAMPLE TEST EXECUTION OUTPUTS and evaluation artifacts
│   ├── test_output.txt          unit-test run
│   ├── sample_cli_session.txt   full CLI demo transcript (proofs, tampering, history, prune)
│   ├── sample_proofs/*.json     generated proof files
│   ├── bench_output.txt         benchmark console output
│   ├── bench_results.csv        benchmark numbers (1k … 1M keys + adversarial case)
│   └── *.png                    charts generated from the CSV
└── report/                      <- Report_106123015.pdf (and its Markdown source)
```

## Build

Requirements: a C++17 compiler (tested with GCC 13.3) and CMake ≥ 3.14 (or just GNU make).
No third-party libraries are needed.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces three binaries in `build/`:

| binary | purpose |
|---|---|
| `smt` | command-line state database: put/get/del, proofs, offline verification, history, prune |
| `smt_tests` | unit tests (run `./build/smt_tests`) |
| `smt_bench` | benchmark (run `./build/smt_bench --csv out.csv`) |

Fallback without CMake: `make` (binaries land in `build-make/`).

Optional: `python3 tests/reference_check.py` re-derives the regression root from the
written spec using only `hashlib`, independently of the C++ code.

## Reproduce every artifact in one go

```bash
bash scripts/run_all.sh            # builds, runs tests, benchmark, CLI demo, regenerates evaluation/*
```
(The benchmark inserts up to 1,000,000 keys and needs roughly 1 GB of RAM and a few minutes.
Use `bash scripts/run_all.sh --sizes 1000,10000,100000` for a quicker run.
Charts need `pip install matplotlib`; everything else is dependency-free.)

## Using the CLI

```bash
./build/smt --db state.db init                      # new empty state database
./build/smt --db state.db put alice 100             # insert / update
./build/smt --db state.db put bob 250
./build/smt --db state.db get bob
./build/smt --db state.db root                      # current state root (hex)
./build/smt --db state.db prove bob --out bob.json          # membership proof
./build/smt --db state.db prove mallory --out mallory.json  # NON-membership proof
./build/smt verify mallory.json --root <root-hex>           # offline verification, no DB needed
./build/smt --db state.db history                   # every committed root (versions)
./build/smt --db state.db prove bob --root <old-root>       # prove against a historical version
./build/smt --db state.db del bob
./build/smt --db state.db prune                     # garbage-collect old versions
./build/smt --db state.db stats
./build/smt --db state.db batch ops.txt             # lines: "put <key> <value>" / "del <key>"
```

Keys are mapped to 256-bit paths with `SHA-256(key)`; use `--raw-key` to pass a 64-hex-character
path directly and `--hex-value` to pass binary values. Exit codes: 0 = ok / proof VALID, 2 = key not
found, 3 = proof INVALID, 1 = usage or I/O error. See `evaluation/sample_cli_session.txt` for a full
transcript.

### Proof file format

```json
{
  "format": "smt-proof-v1",
  "type": "non-membership-leaf",          // or "membership" / "non-membership-empty"
  "root": "…64 hex…", "key": "…64 hex…",
  "value_hash": "…", "other_key": "…",   // depends on type
  "depth": 3,                             // levels walked from the root
  "bitmask": "…64 hex…",                 // bit i set <=> sibling at depth i is non-empty (included)
  "siblings": ["…", "…"],                // only the non-empty siblings
  "size_bytes": 164                       // compact binary size
}
```

## Library API (C++)

```cpp
#include "smt/smt.hpp"
smt::MemoryNodeStore store;
smt::SparseMerkleTree tree(store, /*keep_history=*/false);

auto key = smt::hash_key("alice");
tree.put(key, {'1','0','0'});                 // returns the new root
smt::Proof p = tree.prove(smt::hash_key("mallory"));   // non-membership
bool ok = smt::verify_non_membership(p, tree.root(), smt::hash_key("mallory"));
```

`NodeStore` is an interface; the in-memory `MemoryNodeStore` can be swapped for a
LevelDB/RocksDB-backed implementation without touching the tree logic.

## Design in one paragraph

Keys are 256-bit paths, so the logical tree has 2^256 leaves and depth 256. Two rules make it
practical: (1) every empty subtree hashes to a fixed 32-byte placeholder and is never stored, and
(2) a subtree holding exactly one key is represented by that leaf alone. The structure is therefore
canonical (the root depends only on the key/value set) and a leaf sits at depth = 1 + length of the
longest prefix it shares with another key, which is about log2 N for random keys. A proof is the list
of siblings down the key's path; empty siblings are omitted and flagged in a bitmask, giving proofs of
about 32·(log2 N + 2) bytes on average with a hard upper bound of 8,323 bytes (256 siblings). A
non-membership proof shows that the walk ends either in an empty subtree or at a leaf holding a
*different* key; the verifier recomputes the root from that terminal node with the queried key's bits.
Leaves and internal nodes are hashed with distinct domain-separation prefixes (0x00 / 0x01). Nodes
are stored content-addressed (hash → node) so that every historical root remains provable (versioned
state database) until explicitly pruned. Full details, security analysis and measurements are in
`report/Report_106123015.pdf`.
