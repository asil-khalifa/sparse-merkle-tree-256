#!/usr/bin/env python3
"""Draw Figure 1 of the report: the naive complete tree versus the collapsed tree for
4-bit keys A=0010, B=0011, C=1100, and the compact non-membership proof for the
absent key 0001. Usage: python3 report/draw_structure.py [out.png]"""
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyBboxPatch

BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"
SURFACE, INK, INK2, MUTED, AXIS, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#c3c2b7", "#e1e0d9"
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "fig_structure.png")

fig = plt.figure(figsize=(7.2, 7.0), facecolor=SURFACE)
ax1 = fig.add_axes([0.01, 0.52, 0.98, 0.46])
ax2 = fig.add_axes([0.01, 0.00, 0.98, 0.50])
for ax in (ax1, ax2):
    ax.set_facecolor(SURFACE)
    ax.set_aspect("equal")
    ax.axis("off")

R = 0.24


def node(ax, x, y, kind, label="", color=BLUE):
    if kind == "empty":
        ax.add_patch(Circle((x, y), R * 0.72, facecolor=SURFACE, edgecolor=MUTED, linestyle=(0, (2, 2)), linewidth=0.9))
        ax.text(x, y, "∅", ha="center", va="center", fontsize=6.5, color=MUTED)
    elif kind == "leaf":
        ax.add_patch(FancyBboxPatch((x - R, y - R), 2 * R, 2 * R, boxstyle="round,pad=0.02,rounding_size=0.06",
                                    facecolor=color, edgecolor=SURFACE, linewidth=1))
        ax.text(x, y, label, ha="center", va="center", fontsize=8, color="white", fontweight="bold")
    else:
        ax.add_patch(Circle((x, y), R, facecolor="#ffffff", edgecolor=INK2, linewidth=1.1))
        if label:
            ax.text(x, y, label, ha="center", va="center", fontsize=7, color=INK)


def edge(ax, a, b, color=AXIS, lw=0.9):
    ax.plot([a[0], b[0]], [a[1], b[1]], color=color, linewidth=lw, zorder=0, solid_capstyle="round")


# ---------------- top: naive complete tree of depth 4 ----------------
keys = {0b0010: "A", 0b0011: "B", 0b1100: "C"}
DX, DY = 0.62, 1.05
pos = {}
for d in range(5):
    n = 2 ** d
    span = 16 * DX / n
    for j in range(n):
        pos[(d, j)] = (j * span + span / 2, -d * DY)


def nonempty(d, j):
    lo, hi = j * 2 ** (4 - d), (j + 1) * 2 ** (4 - d)
    return any(lo <= k < hi for k in keys)


for d in range(4):
    for j in range(2 ** d):
        for c in (2 * j, 2 * j + 1):
            edge(ax1, pos[(d, j)], pos[(d + 1, c)], color=AXIS if nonempty(d + 1, c) else GRID)
for d in range(4):
    for j in range(2 ** d):
        x, y = pos[(d, j)]
        node(ax1, x, y, "internal" if nonempty(d, j) else "empty")
for j in range(16):
    x, y = pos[(4, j)]
    if j in keys:
        node(ax1, x, y, "leaf", keys[j])
    else:
        node(ax1, x, y, "empty")
for d in range(5):
    ax1.text(16 * DX + 0.25, -d * DY, f"depth {d}", fontsize=7, color=MUTED, va="center")
ax1.text(0, 0.75, "(a) Naive 256-level SMT, drawn for 4-bit keys A = 0010, B = 0011, C = 1100",
         fontsize=9, fontweight="bold", color=INK, va="bottom")
ax1.text(0, -4 * DY - 0.55, "16 leaf slots and 15 internal nodes exist; 13 slots and 8 internal nodes are empty (∅).\n"
         "Every proof carries one sibling per level: 4 here, 256 in the real tree (8,259 bytes).",
         fontsize=7.6, color=INK2, va="top")
ax1.set_xlim(-0.3, 16 * DX + 1.4)
ax1.set_ylim(-4 * DY - 1.5, 1.1)

# ---------------- bottom: collapsed tree + compact proof for 0001 ----------------
P = {"root": (3.6, 0), "n1": (2.0, -DY), "C": (5.2, -DY), "e1": (3.2, -2 * DY), "n2": (0.8, -2 * DY),
     "e2": (0.0, -3 * DY), "n3": (1.6, -3 * DY), "A": (0.8, -4 * DY), "B": (2.4, -4 * DY)}
edges = [("root", "n1"), ("root", "C"), ("n1", "n2"), ("n1", "e1"), ("n2", "e2"), ("n2", "n3"), ("n3", "A"), ("n3", "B")]
path = {("root", "n1"), ("n1", "n2"), ("n2", "n3"), ("n3", "B")}
for a, b in edges:
    on = (a, b) in path
    edge(ax2, P[a], P[b], color=ORANGE if on else AXIS, lw=2.2 if on else 0.9)
for n in ("root", "n1", "n2", "n3"):
    node(ax2, *P[n], "internal", "H")
node(ax2, *P["e1"], "empty")
node(ax2, *P["e2"], "empty")
node(ax2, *P["A"], "leaf", "A")
node(ax2, *P["B"], "leaf", "B")
node(ax2, *P["C"], "leaf", "C")
for name in ("C", "A", "e1", "e2"):
    sent = name in ("C", "A")
    ax2.add_patch(Circle(P[name], R * 1.6, facecolor="none", edgecolor=AQUA if sent else MUTED, linewidth=1.5,
                         linestyle="-" if sent else (0, (2, 2))))
ax2.text(P["C"][0], P["C"][1] - 0.62, "sibling, depth 0 (sent)", fontsize=7.4, color=AQUA, va="top", ha="center")
ax2.text(P["e1"][0] + 0.15, P["e1"][1] - 0.5, "\u2205 sibling, depth 1\n(omitted)", fontsize=7.4, color=MUTED, va="top", ha="center")
ax2.text(P["e2"][0] - 0.5, P["e2"][1], "\u2205 sibling, depth 2\n(omitted)", fontsize=7.4, color=MUTED, va="center", ha="right")
ax2.text(P["A"][0] - 0.5, P["A"][1], "sibling, depth 3\n(sent)", fontsize=7.4, color=AQUA, va="center", ha="right")
ax2.text(P["B"][0] + 0.45, P["B"][1], "terminal leaf B: key 0011 \u2260 0001", fontsize=7.4, color=ORANGE, va="center")
ax2.text(-2.8, 0.75, "(b) Collapsed tree (stored) and the compact non-membership proof for key 0001",
         fontsize=9, fontweight="bold", color=INK, va="bottom")
ax2.text(7.0, 0.3,
         "C is the only key starting with bit 1, so\n"
         "its leaf floats up to depth 1. A and B share\n"
         "3 bits and diverge at bit 3, so they hang at\n"
         "depth 4 under a chain of nodes that each\n"
         "have one empty child.\n\n"
         "Proof for 0001: the walk (orange) follows\n"
         "bits 0,0,0,1 and ends at leaf B, a different\n"
         "key: kind = non-membership-leaf, depth = 4,\n"
         "bitmask = 1001. Only 2 of 4 siblings are\n"
         "sent; the verifier substitutes \u2205 where the\n"
         "bit is clear and re-hashes up to the root.",
         fontsize=7.6, color=INK2, va="top", ha="left")
ax2.set_xlim(-2.8, 14.6)
ax2.set_ylim(-4 * DY - 0.9, 1.1)

fig.savefig(out, dpi=180, facecolor=SURFACE)
print("wrote", out)
