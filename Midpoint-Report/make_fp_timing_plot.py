#!/usr/bin/env python3
"""
Regenerates Midpoint-Report/images/fp_timing_data.png.

Grouped bar chart of normal vs. subnormal floating-point timing (ns/op) for
floating-point add and multiply, one subplot per processor, each with its own
y-axis. This reproduces the original figure (built in a spreadsheet) so the plot
can be regenerated whenever a processor is added.

All per-processor numbers below are taken directly from the measurements
reported in Section 4 (Evaluation). Each (normal, subnormal) tuple is in ns/op.

Usage:
    python make_fp_timing_plot.py
    python make_fp_timing_plot.py --out images/fp_timing_data.png

Requires: matplotlib.
"""

import argparse
import os

import matplotlib
matplotlib.use("Agg")  # no display needed; write straight to a file
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

# --- Color palette (matches the original figure) ---------------------------
NORMAL_ADD    = "#2E5496"  # dark blue
SUBNORMAL_ADD = "#A6401E"  # rust / brick red
NORMAL_MUL    = "#9DC3E6"  # light blue
SUBNORMAL_MUL = "#F4B183"  # light orange

GRID_COLOR = "#D9D9D9"
ISA_COLOR  = "#808080"

# --- Data: (normal, subnormal) in ns/op, in the original display order ------
# To add a processor, append another dict here; the grid grows automatically.
PROCESSORS = [
    {"name": "Apple M5",                "isa": "ARM",                          "add": (0.50, 0.50), "mul": (0.76, 0.77)},
    {"name": "Intel Xeon 5218",         "isa": "x86", "arch": "ECE LRC",        "add": (1.30, 1.30), "mul": (1.30, 37.57)},
    {"name": "Apple M1",                "isa": "ARM",                          "add": (1.13, 0.95), "mul": (1.35, 1.43)},
    {"name": "AMD EPYC Milan",          "isa": "x86", "arch": "Zen 3",          "add": (0.87, 0.85), "mul": (0.90, 1.14)},
    {"name": "Intel Core i7-13700H",    "isa": "x86", "arch": "Raptor Lake",    "add": (1.51, 1.57), "mul": (3.41, 112.47)},
    {"name": "Intel Xeon Platinum 8160","isa": "x86", "arch": "Skylake",        "add": (1.44, 1.43), "mul": (1.45, 49.31)},
    {"name": "Intel Xeon Max 9480",     "isa": "x86", "arch": "Sapphire Rapids", "add": (0.77, 0.77), "mul": (1.54, 46.90)},
    {"name": "Intel Xeon E5-2603 v3",   "isa": "x86", "arch": "Haswell",        "add": (2.02, 1.88), "mul": (3.25, 81.80)},
    {"name": "Intel Core i5-14600",     "isa": "x86", "arch": "Raptor Lake",    "add": (0.53, 0.42), "mul": (0.88, 24.46)},
    {"name": "NVIDIA Grace",            "isa": "ARM", "arch": "Neoverse V2",     "add": (0.61, 0.61), "mul": (0.92, 0.92)},
    {"name": "AMD Ryzen 7 7800X3D",     "isa": "x86", "arch": "Zen 4",           "add": (0.71, 0.69), "mul": (0.63, 0.90)},
]

NCOLS = 3
BAR_W = 0.38          # width of each bar
GROUP_X = [0.0, 1.0]  # x center of the Add group and the Mul group


def draw_subplot(ax, proc):
    n_add, s_add = proc["add"]
    n_mul, s_mul = proc["mul"]

    # (x position, height, color) for the four bars
    bars = [
        (GROUP_X[0] - BAR_W / 2, n_add, NORMAL_ADD),
        (GROUP_X[0] + BAR_W / 2, s_add, SUBNORMAL_ADD),
        (GROUP_X[1] - BAR_W / 2, n_mul, NORMAL_MUL),
        (GROUP_X[1] + BAR_W / 2, s_mul, SUBNORMAL_MUL),
    ]

    ymax = max(n_add, s_add, n_mul, s_mul)
    top = ymax * 1.30 if ymax > 0 else 1.0

    for x, h, color in bars:
        ax.bar(x, h, width=BAR_W, color=color, zorder=3)
        ax.text(x, h + top * 0.015, f"{h:.2f}", ha="center", va="bottom",
                fontsize=8.5, color="#333333")

    # Axes cosmetics to match the original look.
    ax.set_ylim(0, top)
    ax.set_xlim(-0.6, 1.6)
    ax.set_xticks(GROUP_X)
    ax.set_xticklabels(["Add", "Mul"], fontsize=10)
    ax.set_ylabel("ns/op", fontsize=9)
    ax.grid(axis="y", color=GRID_COLOR, linewidth=0.8, zorder=0)
    ax.set_axisbelow(True)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    ax.spines["left"].set_color(GRID_COLOR)
    ax.spines["bottom"].set_color(GRID_COLOR)
    ax.tick_params(length=0)

    # Bold title + gray ISA/microarchitecture subtitle, left-aligned above the axes.
    subtitle = proc["isa"] + (f"  ·  {proc['arch']}" if proc.get("arch") else "")
    ax.text(0, 1.12, proc["name"], transform=ax.transAxes,
            fontsize=10, fontweight="bold", ha="left", va="bottom")
    ax.text(0, 1.02, subtitle, transform=ax.transAxes,
            fontsize=8.5, color=ISA_COLOR, ha="left", va="bottom")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_out = os.path.join(here, "images", "fp_timing_data.png")

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default=default_out,
                        help="output PNG path (default: images/fp_timing_data.png)")
    parser.add_argument("--dpi", type=int, default=150)
    args = parser.parse_args()

    n = len(PROCESSORS)
    nrows = (n + NCOLS - 1) // NCOLS

    fig, axes = plt.subplots(nrows, NCOLS, figsize=(7.5, 1.7 * nrows + 0.9))
    axes = axes.flatten()

    for ax, proc in zip(axes, PROCESSORS):
        draw_subplot(ax, proc)

    # Hide any leftover empty cells (e.g. the odd slot with 9 processors).
    for ax in axes[n:]:
        ax.set_visible(False)

    # Shared legend across the top.
    handles = [
        Patch(color=NORMAL_ADD,    label="Normal add"),
        Patch(color=SUBNORMAL_ADD, label="Subnormal add"),
        Patch(color=NORMAL_MUL,    label="Normal mul"),
        Patch(color=SUBNORMAL_MUL, label="Subnormal mul"),
    ]
    legend = fig.legend(handles=handles, loc="upper left",
                        bbox_to_anchor=(0.06, 0.99), ncol=4, frameon=False,
                        fontsize=9, handlelength=1.2, columnspacing=1.2)
    fig.text(0.98, 0.985, "All values in ns/op", fontsize=9,
             color=ISA_COLOR, va="top", ha="right")

    fig.subplots_adjust(left=0.08, right=0.98, top=0.92, bottom=0.05,
                        hspace=0.80, wspace=0.30)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    fig.savefig(args.out, dpi=args.dpi, facecolor="white")
    print(f"Wrote {args.out}")


if __name__ == "__main__":
    main()
