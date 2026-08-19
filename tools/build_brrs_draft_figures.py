#!/usr/bin/env python3
"""Generate publication-style figures for the BRRS working manuscript."""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


NAVY = "#18324A"
BLUE = "#4C78A8"
ORANGE = "#F28E2B"
GREEN = "#4E9F6D"
RED = "#C84C4C"
GRAY = "#8A939B"
LIGHT = "#E8EDF1"


def style() -> None:
    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 9,
            "axes.titlesize": 10,
            "axes.labelsize": 9,
            "axes.edgecolor": "#52606B",
            "axes.linewidth": 0.8,
            "xtick.color": "#303A42",
            "ytick.color": "#303A42",
            "text.color": "#1F2930",
            "axes.titleweight": "bold",
            "figure.facecolor": "white",
            "axes.facecolor": "white",
            "savefig.facecolor": "white",
            "savefig.bbox": "tight",
        }
    )


def save(fig: plt.Figure, path: Path) -> None:
    fig.savefig(path, dpi=300, bbox_inches="tight", pad_inches=0.08)
    plt.close(fig)


def fig_system(path: Path) -> None:
    fig = plt.figure(figsize=(7.15, 5.25))
    gs = fig.add_gridspec(2, 1, height_ratios=[1.05, 1.0], hspace=0.42)

    ax = fig.add_subplot(gs[0])
    ax.set_xlim(-3.45, 4.95)
    ax.set_ylim(-0.9, 1.0)
    ax.axis("off")
    ax.axvspan(-2.7, 0, color="#F4EDE7")
    ax.axvspan(0, 4.2, color="#EAF0F4")
    ax.axvline(0, color="#5B6166", lw=5, alpha=0.75)
    ax.text(-1.35, 0.78, "Outside Room 519", ha="center", weight="bold")
    ax.text(2.1, 0.78, "Room 519 interior", ha="center", weight="bold")
    ax.text(0, -0.72, "closed iron door", ha="center", color="#555B61")
    ax.scatter([-2.7], [0], s=240, marker="D", color=ORANGE, edgecolor="white", linewidth=1.5, zorder=4)
    ax.scatter([4.2], [0], s=290, marker="o", color=BLUE, edgecolor="white", linewidth=1.5, zorder=4)
    ax.text(-2.65, 0.34, "sensor TX", ha="center", weight="bold")
    ax.text(4.0, 0.34, "beacon + delayed RX", ha="center", weight="bold")
    ax.annotate("", xy=(4.05, 0), xytext=(-2.55, 0), arrowprops=dict(arrowstyle="->", lw=1.8, ls="--", color=NAVY))
    ax.text(0.75, 0.12, "6.9 m NLOS UWB link", ha="center", color=NAVY)
    ax.annotate("", xy=(0, -0.42), xytext=(-2.7, -0.42), arrowprops=dict(arrowstyle="|-|", color=GRAY, lw=1.2))
    ax.annotate("", xy=(4.2, -0.42), xytext=(0, -0.42), arrowprops=dict(arrowstyle="|-|", color=GRAY, lw=1.2))
    ax.text(-1.35, -0.60, "2.7 m", ha="center")
    ax.text(2.1, -0.60, "4.2 m", ha="center")
    ax.text(4.55, -0.2, "height: 1.5 m", rotation=90, va="center", color=GRAY)
    ax.set_title("(a) Controlled iron-door NLOS evaluation geometry", loc="left", pad=4)

    ax = fig.add_subplot(gs[1])
    ax.set_xlim(0, 10_000)
    ax.set_ylim(0, 1)
    ax.set_yticks([])
    ax.set_xlabel("Time from beacon TX RMARKER (us)")
    for spine in ("left", "right", "top"):
        ax.spines[spine].set_visible(False)
    ax.axvspan(0, 337, color=BLUE, alpha=0.85)
    ax.annotate("256-symbol beacon", xy=(168, 0.7), xytext=(650, 0.88),
                arrowprops=dict(arrowstyle="->", color=BLUE), color=BLUE, fontsize=8, weight="bold")
    ax.axvspan(337, 3000, color=LIGHT)
    ax.text(1650, 0.63, "beacon-referenced\nschedule", ha="center", va="center", color="#5A6470")
    for x in [3000, 3297, 3594]:
        ax.axvspan(x, x + 97, color=ORANGE, alpha=0.9)
        ax.axvspan(x + 97, x + 297, color="#F5D7B3", alpha=0.75)
    ax.annotate("three example data slots\n(97-us frame + 200-us guard)", xy=(3450, 0.34), xytext=(4700, 0.24),
                arrowprops=dict(arrowstyle="->", color=ORANGE), color=ORANGE, fontsize=8)
    ax.axvline(7500, color=RED, lw=1.2, ls="--")
    ax.text(7500, 0.82, "PHY/sync preparation", ha="center", color=RED, fontsize=8)
    ax.axvspan(7500, 10_000, color="#F6EAEA", alpha=0.8)
    ax.text(8750, 0.52, "reserved coordinator\nservice interval", ha="center", color="#7C4545")
    ax.set_xticks([0, 3000, 5000, 7500, 10_000])
    ax.set_title("(b) Implemented 10-ms beacon-scheduled superframe (example: M=32)", loc="left", pad=4)

    save(fig, path)


def fig_exp12(path: Path) -> None:
    m = np.array([32, 64, 128, 256])
    per = np.array([0.0, 0.01, 0.0, 0.0])
    upper = np.array([0.02995, 0.0474, 0.02995, 0.02995])
    ratio = np.array([(46962 + 46892) / 2, (239018 + 236837) / 2,
                      (542884 + 536967) / 2, (1052369 + 1107616) / 2]) / 1000.0
    snr = 10 * np.log10(ratio)
    full_model = snr[0] + 10 * np.log10(m / 32.0)
    effective_model = snr[0] + 10 * np.log10((m - 23) / (32 - 23))

    fig, axes = plt.subplots(1, 2, figsize=(7.15, 3.05), gridspec_kw={"wspace": 0.34})
    ax = axes[0]
    bars = ax.bar(m.astype(str), per, color=[BLUE, ORANGE, GREEN, GRAY], width=0.62)
    ax.scatter(range(4), upper, marker="_", s=130, color=RED, zorder=3, label="one-sided 95% upper bound")
    for bar, p, u in zip(bars, per, upper):
        ax.text(bar.get_x() + bar.get_width()/2, max(p, 0.001) + 0.003, f"{p:.2f}%", ha="center", fontsize=8)
        ax.vlines(bar.get_x() + bar.get_width()/2, p, u, color=RED, lw=0.9, alpha=0.75)
    ax.set_ylim(0, 0.055)
    ax.set_ylabel("Packet error rate (%)")
    ax.set_xlabel("Preamble length M (symbols)")
    ax.set_title("(a) Exp1: 5 x 2,000 frames")
    ax.grid(axis="y", color=LIGHT, lw=0.8)
    ax.legend(frameon=False, fontsize=7, loc="upper left")

    ax = axes[1]
    ax.plot(m, snr, "o-", color=NAVY, lw=2, label="measured FP-SNR")
    ax.plot(m, full_model, "s--", color=GRAY, lw=1.4, label="10 log10(M), anchored")
    ax.plot(m, effective_model, "^--", color=ORANGE, lw=1.5, label="10 log10(M-23), anchored")
    for x, y in zip(m, snr):
        ax.text(x, y + 0.55, f"{y:.2f}", ha="center", fontsize=7.5)
    ax.set_xscale("log", base=2)
    ax.set_xticks(m, labels=[str(x) for x in m])
    ax.set_xlabel("Preamble length M (symbols, log2 axis)")
    ax.set_ylabel("First-path SNR (dB)")
    ax.set_ylim(14, 32.5)
    ax.set_title("(b) Exp2: CIR-derived first-path SNR")
    ax.grid(color=LIGHT, lw=0.8)
    ax.legend(frameon=False, fontsize=7, loc="lower right")
    fig.suptitle("Short-preamble reliability and effective accumulation", fontsize=11, weight="bold", color=NAVY, y=1.02)
    save(fig, path)


def fig_exp3(path: Path) -> None:
    labels = ["A\nSFD8 + STD", "B\nSFD16 + STD", "C\nSFD8 + DTA"]
    measured = np.array([95.6935, 103.9020, 76.8005])
    model = np.array([95.068, 103.209, 76.222])
    x = np.arange(3)
    width = 0.34
    fig = plt.figure(figsize=(7.15, 3.35))
    grid = fig.add_gridspec(1, 2, width_ratios=[1.72, 1.0], wspace=0.08)
    ax = fig.add_subplot(grid[0, 0])
    summary = fig.add_subplot(grid[0, 1])
    b1 = ax.bar(x - width/2, measured, width, color=NAVY, label="EXTTXE measured")
    b2 = ax.bar(x + width/2, model, width, color=ORANGE, label="airtime model")
    for bars in (b1, b2):
        for bar in bars:
            ax.text(
                bar.get_x() + bar.get_width()/2,
                bar.get_height() + 1.2,
                f"{bar.get_height():.3f}",
                ha="center",
                fontsize=7.2,
            )
    ax.set_xticks(x, labels)
    ax.set_ylim(0, 114)
    ax.set_ylabel("TX airtime (us)")
    ax.grid(axis="y", color=LIGHT, lw=0.8)
    fig.legend(
        handles=[b1, b2],
        labels=["EXTTXE measured", "airtime model"],
        frameon=False,
        ncol=2,
        loc="upper center",
        bbox_to_anchor=(0.34, 0.91),
        fontsize=7.5,
    )

    summary.axis("off")
    summary.text(0.02, 0.96, "Differential results", color=NAVY, weight="bold", fontsize=9.5, va="top")
    summary.text(0.02, 0.82, "SFD cost", color=BLUE, weight="bold", fontsize=8.5, va="top")
    summary.text(0.02, 0.73, "B - A = 8.209 us", fontsize=8.5, va="top")
    summary.text(0.02, 0.65, "Model: 8.141 us  (0.83% diff.)", color=GRAY, fontsize=7.5, va="top")

    summary.text(0.02, 0.51, "PHR-rate difference", color=GREEN, weight="bold", fontsize=8.5, va="top")
    summary.text(0.02, 0.42, "A - C = 18.893 us", fontsize=8.5, va="top")
    summary.text(0.02, 0.34, "Model: 18.846 us  (0.25% diff.)", color=GRAY, fontsize=7.5, va="top")

    summary.text(
        0.02,
        0.19,
        "SFD8 + STD PHR",
        color=NAVY,
        weight="bold",
        fontsize=8.5,
        va="top",
    )
    summary.text(
        0.02,
        0.145,
        "8.209 + (18.893 + 2.693)",
        fontsize=7.4,
        va="top",
    )
    summary.text(
        0.005,
        0.02,
        "= 29.795 us",
        color=NAVY,
        weight="bold",
        fontsize=11,
        va="bottom",
        bbox=dict(boxstyle="round,pad=0.3", fc="#EEF3F6", ec="none"),
    )
    fig.suptitle(
        "Experiment 3: differential EXTTXE airtime measurement",
        color=NAVY,
        weight="bold",
        fontsize=10.5,
        y=0.99,
    )
    fig.subplots_adjust(top=0.84, bottom=0.16, left=0.09, right=0.98)
    save(fig, path)


def fig_exp4(path: Path) -> None:
    labels = ["M=32", "M=256"]
    frame = np.array([97, 325])
    slot = np.array([297, 525])
    capacity = np.array([15, 9])
    s2_per = np.array([1.30, 1.175])
    fig, axes = plt.subplots(1, 2, figsize=(7.15, 3.2), gridspec_kw={"wspace": 0.38})
    ax = axes[0]
    x = np.arange(2)
    ax.bar(x, slot, color=LIGHT, edgecolor=NAVY, width=0.62, label="slot = frame + 200-us guard")
    ax.bar(x, frame, color=[ORANGE, BLUE], width=0.62, label="on-air frame")
    for i in range(2):
        ax.text(i, frame[i]/2, f"frame\n{frame[i]} us", ha="center", va="center", color="white", weight="bold")
        ax.text(i, slot[i] - 100, "guard\n200 us", ha="center", va="center", color="#56616A", fontsize=8)
        ax.text(i, slot[i] + 13, f"{slot[i]} us", ha="center", weight="bold")
    ax.set_xticks(x, labels)
    ax.set_ylim(0, 590)
    ax.set_ylabel("Duration (us)")
    ax.set_title("(a) Implemented data-slot duration")
    ax.grid(axis="y", color=LIGHT, lw=0.8)

    ax = axes[1]
    bars = ax.bar(x, capacity, color=[ORANGE, BLUE], width=0.58)
    for bar, n in zip(bars, capacity):
        ax.text(bar.get_x() + bar.get_width()/2, n + 0.4, str(n), ha="center", weight="bold", fontsize=10)
    ax.set_xticks(x, labels)
    ax.set_ylim(0, 17.5)
    ax.set_ylabel("Maximum scheduled data slots\n(current 10-ms implementation)")
    ax.set_title("(b) Analytical capacity of frozen schedule")
    ax.grid(axis="y", color=LIGHT, lw=0.8)
    ax.text(0.5, 2.4, "Observed S2 PER\n1.30% vs 1.175%\n(no scheduling errors)", ha="center", fontsize=8,
            bbox=dict(boxstyle="round,pad=0.4", fc="#F6F7F8", ec="#C4CBD1"))
    fig.suptitle("Experiment 4: preamble reduction shortens the slot, but saturation remains to be measured",
                 fontsize=10.5, weight="bold", color=NAVY, y=1.02)
    save(fig, path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    style()
    fig_system(args.output_dir / "fig1_system_and_superframe.png")
    fig_exp12(args.output_dir / "fig2_exp1_exp2.png")
    fig_exp3(args.output_dir / "fig3_exp3_airtime.png")
    fig_exp4(args.output_dir / "fig4_exp4_capacity.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
