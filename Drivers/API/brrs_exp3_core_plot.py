#!/usr/bin/env python3
"""Create the one-page core figure for BRRS Experiment 3."""

from __future__ import annotations

import csv
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RESULT_DIR = ROOT / "result3" / "exp3_core"
SUMMARY_CSV = RESULT_DIR / "exp3_core_summary.csv"
DIFF_CSV = RESULT_DIR / "exp3_core_differential.csv"


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def main() -> int:
    RESULT_DIR.mkdir(parents=True, exist_ok=True)
    mpl_config = RESULT_DIR / ".matplotlib"
    cache_home = RESULT_DIR / ".cache"
    mpl_config.mkdir(exist_ok=True)
    cache_home.mkdir(exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(mpl_config))
    os.environ.setdefault("XDG_CACHE_HOME", str(cache_home))

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    summary = read_csv(SUMMARY_CSV)
    differential = read_csv(DIFF_CSV)

    variants = [row["variant"] for row in summary]
    theory = [float(row["theory_us"]) for row in summary]
    measured = [float(row["measured_mean_us"]) for row in summary]
    minimum = [float(row["measured_min_us"]) for row in summary]
    maximum = [float(row["measured_max_us"]) for row in summary]
    per = [float(row["per_percent"]) for row in summary]

    plt.rcParams.update(
        {
            "font.size": 10.5,
            "axes.titlesize": 13,
            "axes.labelsize": 10.5,
            "axes.edgecolor": "#444444",
            "axes.linewidth": 0.8,
            "xtick.color": "#333333",
            "ytick.color": "#333333",
        }
    )

    fig = plt.figure(figsize=(14, 5.8), facecolor="white")
    grid = fig.add_gridspec(
        1, 3, width_ratios=[1.35, 1.1, 0.85],
        left=0.055, right=0.975, top=0.80, bottom=0.20, wspace=0.34,
    )

    # Panel 1: paired theory/measurement points make the sub-us agreement visible.
    ax1 = fig.add_subplot(grid[0, 0])
    y = list(range(len(variants)))
    for index in y:
        ax1.plot(
            [theory[index], measured[index]],
            [index, index],
            color="#A7ADB5",
            linewidth=2.2,
            zorder=1,
        )
    ax1.scatter(theory, y, color="#737B86", s=75, label="Analytical", zorder=3)
    xerr_left = [measured[i] - minimum[i] for i in y]
    xerr_right = [maximum[i] - measured[i] for i in y]
    ax1.errorbar(
        measured,
        y,
        xerr=[xerr_left, xerr_right],
        fmt="o",
        color="#1769AA",
        ecolor="#1769AA",
        elinewidth=1.4,
        capsize=4,
        markersize=7.5,
        label="Measured mean (min-max)",
        zorder=4,
    )
    ax1.set_yticks(y, [f"{v}  " for v in variants])
    ax1.invert_yaxis()
    ax1.set_xlim(194, 229)
    ax1.set_xlabel("TX airtime (us)")
    ax1.set_title("A. Absolute airtime")
    ax1.grid(axis="x", alpha=0.22)
    ax1.legend(frameon=False, loc="lower right", fontsize=9)
    for index, value in enumerate(measured):
        ax1.text(
            value + 0.65,
            index,
            f"{value:.3f}",
            color="#1769AA",
            fontsize=9,
            va="center",
        )

    # Panel 2: the differential result is the defensible primary claim.
    ax2 = fig.add_subplot(grid[0, 1])
    diff_labels = ["B-A\nSFD +8 sym", "A-C\nSTD-DTA PHR"]
    diff_theory = [float(row["theory_us"]) for row in differential]
    diff_measured = [float(row["measured_us"]) for row in differential]
    x = [0, 1]
    width = 0.34
    theory_bars = ax2.bar(
        [value - width / 2 for value in x],
        diff_theory,
        width,
        color="#A7ADB5",
        label="Analytical",
    )
    measured_bars = ax2.bar(
        [value + width / 2 for value in x],
        diff_measured,
        width,
        color=["#D8722C", "#2A8C72"],
        label="Measured",
    )
    ax2.bar_label(theory_bars, fmt="%.3f", padding=3, fontsize=9)
    ax2.bar_label(measured_bars, fmt="%.3f", padding=3, fontsize=9)
    ax2.set_xticks(x, diff_labels)
    ax2.set_ylim(0, 22)
    ax2.set_ylabel("Difference (us)")
    ax2.set_title("B. Differential validation")
    ax2.grid(axis="y", alpha=0.22)
    ax2.legend(frameon=False, loc="upper left", fontsize=9)

    # Panel 3: PER is supporting link evidence, not the airtime claim.
    ax3 = fig.add_subplot(grid[0, 2])
    per_bars = ax3.bar(
        variants,
        per,
        color=["#D8722C", "#2A8C72", "#E2B03F"],
        width=0.62,
    )
    ax3.bar_label(per_bars, labels=[f"{value:.2f}%" for value in per], padding=4)
    ax3.set_ylim(0, 0.55)
    ax3.set_ylabel("PER (%)")
    ax3.set_title("C. RX link result")
    ax3.grid(axis="y", alpha=0.22)

    fig.suptitle(
        "Experiment 3 | SFD and PHR airtime validation",
        x=0.055,
        y=0.955,
        ha="left",
        fontsize=20,
        fontweight="bold",
        color="#20252B",
    )
    fig.text(
        0.055,
        0.865,
        "EXTTXE hardware capture: 1000/1000 valid samples in A, B and C",
        ha="left",
        fontsize=11,
        color="#4F5964",
    )
    fig.text(
        0.515,
        0.07,
        "Inferred SFD 8 + STD PHR: measured 29.572 us | analytical 29.681 us | error -0.37%",
        ha="center",
        fontsize=10.5,
        fontweight="bold",
        color="#20252B",
    )

    png_path = RESULT_DIR / "exp3_core_result.png"
    svg_path = RESULT_DIR / "exp3_core_result.svg"
    fig.savefig(png_path, dpi=220, facecolor="white")
    fig.savefig(svg_path, facecolor="white")
    plt.close(fig)

    print(png_path)
    print(svg_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
