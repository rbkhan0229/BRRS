#!/usr/bin/env python3
"""Create the core comparison figure for 32- and 64-symbol preambles."""

from __future__ import annotations

import csv
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RESULT_DIR = ROOT / "result3" / "preamble_32_64_compare"
SUMMARY_CSV = RESULT_DIR / "preamble_32_64_summary.csv"


def main() -> int:
    with SUMMARY_CSV.open(newline="") as handle:
        rows = list(csv.DictReader(handle))

    mpl_config = RESULT_DIR / ".matplotlib"
    cache_home = RESULT_DIR / ".cache"
    mpl_config.mkdir(exist_ok=True)
    cache_home.mkdir(exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(mpl_config))
    os.environ.setdefault("XDG_CACHE_HOME", str(cache_home))

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    labels = [f"{row['preamble_symbols']} sym" for row in rows]
    per = [float(row["per_percent"]) for row in rows]
    sfdto = [int(row["sfdto"]) for row in rows]
    phe = [int(row["phe"]) for row in rows]
    accum_avg = [float(row["accum_avg"]) for row in rows]
    plen = [int(row["preamble_symbols"]) for row in rows]
    accum_percent = [
        100.0 * accum_avg[index] / plen[index] for index in range(len(rows))
    ]

    colors = ["#D76F30", "#2A8C72"]
    plt.rcParams.update(
        {
            "font.size": 10.5,
            "axes.titlesize": 13,
            "axes.labelsize": 10.5,
            "axes.edgecolor": "#444444",
            "axes.linewidth": 0.8,
        }
    )

    fig = plt.figure(figsize=(13.2, 5.7), facecolor="white")
    grid = fig.add_gridspec(
        1, 3, left=0.06, right=0.975, top=0.78, bottom=0.21, wspace=0.36
    )

    ax1 = fig.add_subplot(grid[0, 0])
    bars = ax1.bar(labels, per, color=colors, width=0.62)
    ax1.bar_label(bars, labels=[f"{value:.2f}%" for value in per], padding=4)
    ax1.set_ylim(0, 27)
    ax1.set_ylabel("PER (%)")
    ax1.set_title("A. Packet error rate")
    ax1.grid(axis="y", alpha=0.22)

    ax2 = fig.add_subplot(grid[0, 1])
    x = [0, 1]
    sfd_bars = ax2.bar(x, sfdto, color="#C94E45", width=0.62, label="SFD timeout")
    phe_bars = ax2.bar(
        x, phe, bottom=sfdto, color="#E6B54A", width=0.62, label="PHR error"
    )
    totals = [sfdto[index] + phe[index] for index in x]
    for index, total in enumerate(totals):
        ax2.text(index, total + 5, str(total), ha="center", fontweight="bold")
    ax2.set_xticks(x, labels)
    ax2.set_ylim(0, 265)
    ax2.set_ylabel("Failed frames")
    ax2.set_title("B. Failure breakdown")
    ax2.grid(axis="y", alpha=0.22)
    ax2.legend(frameon=False, loc="upper right", fontsize=9)

    ax3 = fig.add_subplot(grid[0, 2])
    accum_bars = ax3.bar(labels, accum_percent, color=colors, width=0.62)
    ax3.bar_label(
        accum_bars,
        labels=[
            f"{accum_percent[0]:.1f}%\n({accum_avg[0]:.1f}/{plen[0]})",
            f"{accum_percent[1]:.1f}%\n({accum_avg[1]:.1f}/{plen[1]})",
        ],
        padding=4,
    )
    ax3.set_ylim(0, 85)
    ax3.set_ylabel("Mean accumCount / PLEN (%)")
    ax3.set_title("C. Successful-frame accumulation")
    ax3.grid(axis="y", alpha=0.22)

    fig.suptitle(
        "Preamble length comparison | 32 vs 64 symbols",
        x=0.06,
        y=0.95,
        ha="left",
        fontsize=20,
        fontweight="bold",
        color="#20252B",
    )
    fig.text(
        0.06,
        0.855,
        "TX success: 1000/1000 in both conditions | delayed-TX late: 0",
        ha="left",
        fontsize=11,
        color="#4F5964",
    )
    fig.text(
        0.5,
        0.08,
        "64 sym reduced PER by 23.0 percentage points (97.5% relative); "
        "accumCount includes successful frames only.",
        ha="center",
        fontsize=10.5,
        fontweight="bold",
        color="#20252B",
    )

    png_path = RESULT_DIR / "preamble_32_64_comparison.png"
    svg_path = RESULT_DIR / "preamble_32_64_comparison.svg"
    fig.savefig(png_path, dpi=220, facecolor="white")
    fig.savefig(svg_path, facecolor="white")
    plt.close(fig)

    print(png_path)
    print(svg_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

