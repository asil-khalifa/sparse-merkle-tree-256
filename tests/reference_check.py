#!/usr/bin/env python3
"""Independent cross-check of the hashing conventions, written from the spec
in include/smt/hash.hpp with nothing but hashlib. The root printed here must
equal the regression vector in tests/test_smt.cpp and the output of:
    smt init; smt put alice 100; smt put bob 250; smt put carol 7; smt root
"""
import hashlib

H = lambda b: hashlib.sha256(b).digest()
PLACEHOLDER = bytes(32)


def bit(k, i):
    return (k[i >> 3] >> (7 - (i & 7))) & 1


def reference_root(leaves, depth=0):
    """Canonical definition of the collapsed 256-level SMT."""
    if not leaves:
        return PLACEHOLDER
    if len(leaves) == 1:
        k, vh = leaves[0]
        return H(b"\x00" + k + vh)                 # leaf = H(0x00 || key || value_hash)
    left = [x for x in leaves if not bit(x[0], depth)]
    right = [x for x in leaves if bit(x[0], depth)]
    return H(b"\x01" + reference_root(left, depth + 1) + reference_root(right, depth + 1))


if __name__ == "__main__":
    data = [("alice", "100"), ("bob", "250"), ("carol", "7")]
    leaves = [(H(k.encode()), H(v.encode())) for k, v in data]   # key path = SHA256(key)
    root = reference_root(leaves).hex()
    expected = "973afc726375a2f326e36025b339ef8933b8bb43da8e2a46bd49dc8960ad8f4c"
    print("python reference root :", root)
    print("expected (C++)        :", expected)
    print("MATCH" if root == expected else "MISMATCH")
    raise SystemExit(0 if root == expected else 1)
