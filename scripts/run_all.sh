#!/usr/bin/env bash
# Builds everything, runs the unit tests, the benchmark, the CLI demo, and
# regenerates every artifact under evaluation/.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)"
mkdir -p evaluation
echo "== unit tests =="
./build/smt_tests | tee evaluation/test_output.txt
echo "== benchmark =="
./build/smt_bench --csv evaluation/bench_results.csv "$@" | tee evaluation/bench_output.txt
echo "== CLI demo =="
bash scripts/sample_session.sh | tee evaluation/sample_cli_session.txt
if python3 -c "import matplotlib" 2>/dev/null; then
  python3 bench/plot.py evaluation/bench_results.csv evaluation
else
  echo "matplotlib not installed; skipping plots (pip install matplotlib)"
fi
echo "done."
