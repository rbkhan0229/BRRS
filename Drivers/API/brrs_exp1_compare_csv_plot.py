#!/usr/bin/env python3
"""Compare BRRS experiment-1 summary CSV files."""

from __future__ import annotations

import argparse
import csv
import os
import tempfile
from pathlib import Path


MATPLOTLIB_CACHE = Path(tempfile.gettempdir()) / "brrs-matplotlib"
MATPLOTLIB_CACHE.mkdir(parents=True, exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(MATPLOTLIB_CACHE))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


SERIES_STYLES = [
    ("#3A6EA5", "o", "-"),
    ("#E45756", "s", "--"),
    ("#59A14F", "^", "-."),
]


def read_summary(path: Path) -> list[dict[str, float]]:
    rows: list[dict[str, float]] = []
    with path.open(newline="") as source:
        for raw in csv.DictReader(source):
            rows.append(
                {
                    "lead_us": float(raw["lead_us"]),
                    "per_percent": float(raw["per_percent"]),
                    "accum_mean": float(raw["accum_mean"]) if raw["accum_mean"] else float("nan"),
                }
            )
    if not rows:
        raise ValueError(f"{path}: no rows")
    return sorted(rows, key=lambda row: row["lead_us"])


def parse_series(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("series must use LABEL=/path/to/summary.csv")
    label, path_text = value.split("=", 1)
    path = Path(path_text)
    if not label.strip() or not path.is_file():
        raise argparse.ArgumentTypeError(f"invalid series: {value}")
    return label.strip(), path


def style_axis(axis: plt.Axes) -> None:
    axis.grid(axis="y", color="#D9D9D9", linewidth=0.8)
    axis.spines["top"].set_visible(False)
    axis.spines["right"].set_visible(False)


def save_figure(figure: plt.Figure, output_dir: Path, stem: str) -> None:
    for suffix in (".png", ".svg"):
        figure.savefig(output_dir / f"{stem}{suffix}", dpi=220, bbox_inches="tight")
    plt.close(figure)


def plot_per(series: list[tuple[str, list[dict[str, float]]]], output_dir: Path, prefix: str) -> None:
    figure, axes = plt.subplots(
        2,
        1,
        figsize=(9.4, 6.6),
        sharex=True,
        gridspec_kw={"height_ratios": [2.2, 1.4]},
    )
    main_axis, zoom_axis = axes

    for index, (label, rows) in enumerate(series):
        color, marker, line_style = SERIES_STYLES[index % len(SERIES_STYLES)]
        leads = [row["lead_us"] for row in rows]
        per_values = [row["per_percent"] for row in rows]
        main_axis.plot(
            leads,
            per_values,
            color=color,
            marker=marker,
            linestyle=line_style,
            linewidth=2,
            label=label,
        )
        zoom_rows = [row for row in rows if row["lead_us"] >= 6]
        zoom_axis.plot(
            [row["lead_us"] for row in zoom_rows],
            [row["per_percent"] for row in zoom_rows],
            color=color,
            marker=marker,
            linestyle=line_style,
            linewidth=2,
            label=label,
        )

    all_leads = sorted({row["lead_us"] for _, rows in series for row in rows})
    main_axis.axvspan(4, 6, color="#F2CF5B", alpha=0.22)
    main_axis.text(5, 82, "Transition\n4-6 us", ha="center", va="center")
    main_axis.set_title("Experiment 1: PER comparison across environments")
    main_axis.set_ylabel("PER (%)")
    main_axis.set_ylim(-3, 105)
    main_axis.legend(frameon=False)
    style_axis(main_axis)

    max_zoom = max(
        row["per_percent"]
        for _, rows in series
        for row in rows
        if row["lead_us"] >= 6
    )
    zoom_axis.set_title("Low-PER region (lead >= 6 us)", fontsize=10)
    zoom_axis.set_ylabel("PER (%)")
    zoom_axis.set_xlabel("Lead margin (us)")
    zoom_axis.set_ylim(-0.05, max(1.0, max_zoom + 0.15))
    zoom_axis.set_xticks(all_leads)
    style_axis(zoom_axis)

    figure.tight_layout()
    save_figure(figure, output_dir, f"{prefix}_per_comparison")


def plot_accum(series: list[tuple[str, list[dict[str, float]]]], output_dir: Path, prefix: str) -> None:
    figure, axis = plt.subplots(figsize=(9.4, 4.9))
    all_leads: set[float] = set()

    for index, (label, rows) in enumerate(series):
        color, marker, line_style = SERIES_STYLES[index % len(SERIES_STYLES)]
        valid = [row for row in rows if row["accum_mean"] == row["accum_mean"]]
        leads = [row["lead_us"] for row in valid]
        accum = [row["accum_mean"] for row in valid]
        all_leads.update(leads)
        axis.plot(
            leads,
            accum,
            color=color,
            marker=marker,
            linestyle=line_style,
            linewidth=2,
            label=label,
        )

    axis.set_title("Experiment 1: accumCount comparison across environments")
    axis.set_xlabel("Lead margin (us)")
    axis.set_ylabel("Mean accumCount (symbols)")
    axis.set_xticks(sorted(all_leads))
    axis.set_ylim(0, 20)
    axis.legend(frameon=False)
    style_axis(axis)
    figure.tight_layout()
    save_figure(figure, output_dir, f"{prefix}_accum_comparison")


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare experiment-1 summary CSV files")
    parser.add_argument("--series", action="append", type=parse_series, required=True)
    parser.add_argument("-o", "--output-dir", type=Path, required=True)
    parser.add_argument("--prefix", default="exp1_environment")
    args = parser.parse_args()

    if len(args.series) < 2:
        parser.error("at least two --series values are required")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    series = [(label, read_summary(path)) for label, path in args.series]
    lead_sets = [{row["lead_us"] for row in rows} for _, rows in series]
    if any(leads != lead_sets[0] for leads in lead_sets[1:]):
        raise ValueError("all summaries must contain the same lead settings")

    plot_per(series, args.output_dir, args.prefix)
    plot_accum(series, args.output_dir, args.prefix)
    print(f"Compared {len(series)} series: {', '.join(label for label, _ in series)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
