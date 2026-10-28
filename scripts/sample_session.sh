#!/usr/bin/env bash
# Demonstrates the CLI end-to-end. Run from the project root after building:
#   bash scripts/sample_session.sh
set -u
SMT=${SMT:-./build/smt}
DB=${DB:-/tmp/smt_demo.db}
OUT=${OUT:-evaluation/sample_proofs}
mkdir -p "$OUT"
rm -f "$DB"

run() { echo; echo "\$ smt --db demo.db $*"; "$SMT" --db "$DB" "$@"; echo "[exit code $?]"; }

run init
run put alice 100
run put bob 250
run put carol 7
run put dave 4200
run batch scripts/sample_batch.txt
run root
run get bob
run get mallory
run stats
run dump
run prove bob --out "$OUT/proof_bob_membership.json"
run verify "$OUT/proof_bob_membership.json" --value 250
run verify "$OUT/proof_bob_membership.json" --value 251
run prove mallory --out "$OUT/proof_mallory_nonmembership.json"
run verify "$OUT/proof_mallory_nonmembership.json"
run prove zed --out "$OUT/proof_zed_nonmembership.json"
run verify "$OUT/proof_zed_nonmembership.json"
echo; echo "--- contents of $OUT/proof_mallory_nonmembership.json ---"; cat "$OUT/proof_mallory_nonmembership.json"
echo; echo "--- tampering with the proof (flip one hex digit of a sibling) ---"
python3 - "$OUT/proof_mallory_nonmembership.json" "$OUT/proof_tampered.json" <<'PYEOF'
import json, sys
p = json.load(open(sys.argv[1]))
s = p["siblings"][0]
p["siblings"][0] = ("0" if s[0] != "0" else "1") + s[1:]   # flip the first hex digit of a sibling
json.dump(p, open(sys.argv[2], "w"), indent=2)
PYEOF
run verify "$OUT/proof_tampered.json"
run del bob
run get bob
run prove bob --out "$OUT/proof_bob_after_delete.json"
run verify "$OUT/proof_bob_after_delete.json"
run history
OLD=$("$SMT" --db "$DB" history | sed -n 's/^v5 *\([0-9a-f]\{64\}\)$/\1/p')
echo; echo "Proving against the historical root v5 = $OLD (bob still present there):"
run prove bob --root "$OLD" --out "$OUT/proof_bob_at_v5.json"
run verify "$OUT/proof_bob_at_v5.json" --root "$OLD" --value 250
run stats
run prune
run stats
echo; echo "Historical root no longer provable after prune:"
run prove bob --root "$OLD"
