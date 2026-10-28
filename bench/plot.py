#!/usr/bin/env python3
"""Render the benchmark CSV (from smt_bench --csv) into PNG charts.
Usage: python3 bench/plot.py evaluation/bench_results.csv evaluation/
Requires matplotlib (pip install matplotlib).
"""
import csv
import math
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# Palette (validated categorical slots + chrome)
BLUE, ORANGE, AQUA, YELLOW = "#2a78d6", "#eb6834", "#1baf7a", "#eda100"
SURFACE, INK, INK2, MUTED, GRID, AXIS = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"

src = sys.argv[1] if len(sys.argv) > 1 else "evaluation/bench_results.csv"
out = sys.argv[2] if len(sys.argv) > 2 else "evaluation"
os.makedirs(out, exist_ok=True)

rows = list(csv.DictReader(open(src)))
rnd = [r for r in rows if r["mode"] == "random"]
adv = [r for r in rows if r["mode"].startswith("adversarial")]
N = [int(r["n"]) for r in rnd]
f = lambda key, rs=rnd: [float(r[key]) for r in rs]

plt.rcParams.update({
    "font.family": "sans-serif", "font.size": 10, "axes.edgecolor": AXIS, "axes.labelcolor": INK2,
    "xtick.color": INK2, "ytick.color": INK2, "axes.titlecolor": INK, "axes.titleweight": "bold",
    "axes.spines.top": False, "axes.spines.right": False, "figure.facecolor": SURFACE, "axes.facecolor": SURFACE,
    "grid.color": GRID, "grid.linewidth": 0.6, "legend.frameon": False, "legend.fontsize": 9,
})


def style(ax, xlabel, ylabel, log_x=True, log_y=False):
    ax.grid(True, axis="y")
    ax.set_axisbelow(True)
    if log_x:
        ax.set_xscale("log")
        ax.set_xticks(N)
        ax.set_xticklabels(["1k" if n == 1000 else "10k" if n == 10000 else "100k" if n == 100000 else "1M" for n in N])
        ax.minorticks_off()
    if log_y:
        ax.set_yscale("log")
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.tick_params(length=0)


def line(ax, x, y, color, label, end_label=True, dy=0):
    ax.plot(x, y, color=color, linewidth=2, marker="o", markersize=6, markeredgecolor=SURFACE, markeredgewidth=1.2, label=label)
    if end_label:
        ax.annotate(label, (x[-1], y[-1]), xytext=(7, dy), textcoords="offset points", va="center", fontsize=9, color=INK2)


# ---- Figure 1: proof size and sibling count vs N ------------------------------
fig, (a1, a2) = plt.subplots(1, 2, figsize=(10.5, 4.2))
a1.fill_between(N, f("mem_bytes_min"), f("mem_bytes_max"), color=BLUE, alpha=0.10, linewidth=0)
a1.fill_between(N, f("non_bytes_min"), f("non_bytes_max"), color=ORANGE, alpha=0.10, linewidth=0)
line(a1, N, f("mem_bytes_avg"), BLUE, "membership", dy=7)
line(a1, N, f("non_bytes_avg"), ORANGE, "non-membership", dy=-7)
style(a1, "number of keys N", "proof size (bytes)")
a1.set_ylim(0, 1000)
a1.set_xlim(N[0] / 1.5, N[-1] * 4)
a1.set_title("Compact proof size (avg, band = min..max)")
a1.text(N[0] / 1.3, 950, "naive 256-sibling proof: 8,259 bytes (off scale, 11-21x larger)", fontsize=8.5, color=INK2, va="top")
a1.legend(loc="lower right")

line(a2, N, f("mem_sibs_avg"), BLUE, "siblings (membership)", dy=4)
line(a2, N, f("non_sibs_avg"), ORANGE, "siblings (non-membership)", dy=-8)
line(a2, N, f("mem_depth_avg"), AQUA, "leaf depth (membership)", dy=6)
a2.plot(N, [math.log2(n) for n in N], color=MUTED, linewidth=1.4, linestyle="--", label="log2 N")
a2.annotate("log2 N", (3000, math.log2(3000)), xytext=(10, -12), textcoords="offset points", fontsize=9, color=MUTED)
style(a2, "number of keys N", "count")
a2.set_xlim(N[0] / 1.5, N[-1] * 12)
a2.set_ylim(0, 26)
a2.set_title("Hashes in a proof vs. log2 N")
a2.legend(loc="lower right")
fig.tight_layout()
fig.savefig(os.path.join(out, "fig_proof_size.png"), dpi=160)
plt.close(fig)

# ---- Figure 2: latency per operation vs N ------------------------------------
fig, ax = plt.subplots(figsize=(7.2, 4.2))
line(ax, N, f("insert_us"), BLUE, "insert")
line(ax, N, f("prove_non_us"), ORANGE, "prove non-membership")
line(ax, N, f("verify_non_us"), AQUA, "verify non-membership", dy=7)
line(ax, N, f("get_us"), YELLOW, "get", dy=-7)
style(ax, "number of keys N", "microseconds per operation", log_y=True)
ax.set_xlim(N[0] / 1.5, N[-1] * 14)
ax.set_title("Per-operation latency (single thread, in-memory store)")
ax.legend(loc="upper left")
fig.tight_layout()
fig.savefig(os.path.join(out, "fig_latency.png"), dpi=160)
plt.close(fig)

# ---- Figure 3: adversarial keys vs random keys (N = 1000) --------------------
if adv:
    r1 = [r for r in rnd if int(r["n"]) == int(adv[0]["n"])] or rnd[:1]
    cats = ["random keys", "200-bit shared prefix"]
    depth = [float(r1[0]["mem_depth_avg"]), float(adv[0]["mem_depth_avg"])]
    sibs = [float(r1[0]["mem_sibs_avg"]), float(adv[0]["mem_sibs_avg"])]
    size = [float(r1[0]["mem_bytes_avg"]), float(adv[0]["mem_bytes_avg"])]
    fig, ax = plt.subplots(figsize=(7.2, 4.0))
    x = [0, 1]
    w = 0.34
    b1 = ax.bar([i - w / 2 - 0.01 for i in x], depth, width=w, color=BLUE, label="avg leaf depth (levels hashed by verifier)")
    b2 = ax.bar([i + w / 2 + 0.01 for i in x], sibs, width=w, color=ORANGE, label="avg non-empty siblings sent")
    for b, v in zip(list(b1) + list(b2), depth + sibs):
        ax.annotate(f"{v:.1f}", (b.get_x() + b.get_width() / 2, v), xytext=(0, 3), textcoords="offset points", ha="center", fontsize=9, color=INK)
    for i, s in enumerate(size):
        ax.annotate(f"avg proof {s:.0f} bytes", (i, max(depth[i], sibs[i]) + 22), ha="center", fontsize=9, color=INK2)
    ax.set_xticks(x)
    ax.set_xticklabels(cats)
    style(ax, "", "count", log_x=False)
    ax.set_ylim(0, 260)
    ax.set_title(f"Adversarial keys (N = {adv[0]['n']}): depth grows, proof size does not")
    ax.legend(loc="upper left")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fig_adversarial.png"), dpi=160)
    plt.close(fig)

print("charts written to", out)
