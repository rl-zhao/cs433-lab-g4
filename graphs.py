# make_checkpoint_graphs_twopage.py
# Produces TWO separate pages in a single PDF:
#   Page 1: Weighted Speedup (normalized, Baseline = 1.0)
#   Page 2: Max Slowdown    (normalized, Baseline = 1.0)

import math
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages

# ----- 1) INPUT: cycles (from runs) -----
benches = ["gcc", "mcf", "milc", "omnetpp"]

# Single-program (alone) cycles
alone = {
    "gcc":     1307347,
    "mcf":     7834068,
    "milc":    6537524,
    "omnetpp": 1512470,
}

# Shared (4-program) cycles per configuration
shared = {
    "Baseline": {  # multiprogrammed baseline
        "gcc": 1372641,
        "mcf": 11199475,
        "milc": 9530815,
        "omnetpp": 1523027,
    },
    "WayPart": {
        "gcc": 1401877,
        "mcf": 12382621,
        "milc": 10024240,
        "omnetpp": 6231337,
    },
    "BLISS": {
        "gcc": 1597780,
        "mcf": 20433812,
        "milc": 14122042,
        "omnetpp": 2003754,
    },
}

# ----- 2) Metric helpers -----
def weighted_speedup(alone_cycles, shared_cycles):
    # WS = sum_i (cycles_alone / cycles_shared)
    return sum(alone_cycles[b] / shared_cycles[b] for b in benches)

def max_slowdown(alone_cycles, shared_cycles):
    # MS = max_i (cycles_shared / cycles_alone)
    return max(shared_cycles[b] / alone_cycles[b] for b in benches)

# Compute absolute metrics per configuration
metrics = {}
for cfg, sc in shared.items():
    ws = weighted_speedup(alone, sc)
    ms = max_slowdown(alone, sc)
    metrics[cfg] = {"WS": ws, "MS": ms}

# Normalize each metric to Baseline = 1.0
ws_base = metrics["Baseline"]["WS"]
ms_base = metrics["Baseline"]["MS"]
norm = {
    cfg: {
        "WS": metrics[cfg]["WS"] / ws_base,
        "MS": metrics[cfg]["MS"] / ms_base
    } for cfg in metrics
}

labels = ["Baseline", "WayPart", "BLISS"]

def add_value_labels(ax, fmt="{:.3f}"):
    for p in ax.patches:
        h = p.get_height()
        ax.annotate(fmt.format(h),
                    (p.get_x() + p.get_width()/2., h),
                    ha='center', va='bottom', fontsize=9,
                    xytext=(0, 3), textcoords='offset points')

plt.rcParams.update({
    "font.size": 11,
    "axes.spines.top": False,
    "axes.spines.right": False,
})

with PdfPages("checkpoint_graphs.pdf") as pdf:
    # ----- Page 1: Weighted Speedup (Normalized)
    fig1 = plt.figure(figsize=(7.2, 4.6))
    ax1 = fig1.add_subplot(111)
    vals_ws = [norm[c]["WS"] for c in labels]
    ax1.bar(labels, vals_ws)
    ax1.set_ylim(0, max(vals_ws)*1.15)
    ax1.set_title("Weighted Speedup (Normalized, Baseline = 1.0)")
    ax1.set_ylabel("Normalized WS")
    add_value_labels(ax1)
    fig1.tight_layout()
    pdf.savefig(fig1, bbox_inches="tight")
    plt.close(fig1)

    # ----- Page 2: Max Slowdown (Normalized)
    fig2 = plt.figure(figsize=(7.2, 4.6))
    ax2 = fig2.add_subplot(111)
    vals_ms = [norm[c]["MS"] for c in labels]
    ax2.bar(labels, vals_ms)
    ax2.set_ylim(0, max(vals_ms)*1.15)
    ax2.set_title("Max Slowdown (Normalized, Baseline = 1.0)")
    ax2.set_ylabel("Normalized Max Slowdown")
    add_value_labels(ax2)
    fig2.tight_layout()
    pdf.savefig(fig2, bbox_inches="tight")
    plt.close(fig2)

print("Wrote checkpoint_graphs.pdf")
print("\nAbsolute metrics:")
for cfg in labels:
    print(f"{cfg:10s}  WS={metrics[cfg]['WS']:.6f}   MaxSlowdown={metrics[cfg]['MS']:.6f}")