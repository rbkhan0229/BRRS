#!/usr/bin/env python3
"""Compare experiment-1 accumCount distributions and annotate PER."""

from __future__ import annotations

import argparse
import csv
import re
import sys
from dataclasses import dataclass
from pathlib import Path

from brrs_exp1_log_to_csv_plot import Exp1Run, parse_log, plt, save_figure


FILENAME_RE = re.compile(r"lead(?P<lead>\d+)(?:-(?P<repeat>\d+))?_")


@dataclass(frozen=True)
class Dataset:
    label: str
    path: Path
    runs: list[Exp1Run]


def parse_dataset(
    label: str, path: Path, excluded_leads: set[int]
) -> tuple[Dataset, list[str]]:
    if not path.is_dir():
        raise FileNotFoundError(path)

    runs: list[Exp1Run] = []
    warnings: list[str] = []
    seen_leads: dict[int, Path] = {}

    for log_path in sorted(path.glob("*.log")):
        filename_match = FILENAME_RE.search(log_path.name)
        if not filename_match:
            warnings.append(f"SKIP {log_path.name}: no lead value in filename")
            continue
        filename_lead = int(filename_match.group("lead"))
        if filename_lead in excluded_leads:
            warnings.append(f"SKIP {log_path.name}: excluded lead={filename_lead}")
            continue
        if filename_match.group("repeat") is not None:
            warnings.append(f"SKIP {log_path.name}: repeated run suffix")
            continue

        try:
            run = parse_log(log_path)
        except ValueError as error:
            warnings.append(f"SKIP {log_path.name}: {error}")
            continue

        assert run.lead_us is not None
        if run.lead_us in seen_leads:
            warnings.append(
                f"SKIP {log_path.name}: duplicate lead={run.lead_us} "
                f"(already {seen_leads[run.lead_us].name})"
            )
            continue
        seen_leads[run.lead_us] = log_path
        runs.append(run)

    if not runs:
        raise ValueError(f"{label}: no valid logs in {path}")

    runs.sort(key=lambda run: run.lead_us if run.lead_us is not None else -1)
    return Dataset(label=label, path=path, runs=runs), warnings


def format_per(value: float) -> str:
    if value == 0:
        return "0%"
    if value == 100:
        return "100%"
    return f"{value:.1f}%"


def per_color(value: float) -> str:
    if value <= 1:
        return "#2E7D32"
    if value <= 10:
        return "#B26A00"
    return "#C62828"


def plot_datasets(
    datasets: list[Dataset], output_dir: Path, prefix: str, notes: list[str]
) -> list[Path]:
    colors = ("#2878B5", "#E07A3F", "#4C956C", "#8C6BB1")
    figure, axes = plt.subplots(
        len(datasets),
        1,
        figsize=(12.2, 3.35 * len(datasets) + 1.5),
        sharex=True,
        sharey=True,
        squeeze=False,
    )

    for index, (dataset, axis) in enumerate(zip(datasets, axes[:, 0])):
        color = colors[index % len(colors)]
        mode_x: list[int] = []
        mode_y: list[int] = []

        for run in dataset.runs:
            assert run.lead_us is not None
            assert run.per_percent is not None

            axis.annotate(
                f"PER\n{format_per(run.per_percent)}",
                (run.lead_us, 20.2),
                ha="center",
                va="top",
                fontsize=7.5,
                fontweight="bold",
                color=per_color(run.per_percent),
            )

            if not run.accum_histogram:
                axis.scatter(
                    run.lead_us,
                    1,
                    marker="x",
                    s=45,
                    color="#7A7A7A",
                    linewidth=1.5,
                    zorder=3,
                )
                axis.annotate(
                    "N/A",
                    (run.lead_us, 1),
                    xytext=(0, 7),
                    textcoords="offset points",
                    ha="center",
                    fontsize=7,
                    color="#666666",
                )
                continue

            total = sum(run.accum_histogram.values())
            max_count = max(run.accum_histogram.values())
            modes = sorted(
                accum
                for accum, count in run.accum_histogram.items()
                if count == max_count
            )
            mode_x.append(run.lead_us)
            mode_y.append(modes[0])

            for accum, count in sorted(run.accum_histogram.items()):
                fraction = count / total
                axis.scatter(
                    run.lead_us,
                    accum,
                    s=55 + 300 * fraction,
                    color=color,
                    alpha=0.45 + 0.5 * fraction,
                    edgecolor="#253238",
                    linewidth=0.8,
                    zorder=4,
                )
                if len(run.accum_histogram) > 1:
                    axis.annotate(
                        f"n={count}",
                        (run.lead_us, accum),
                        xytext=(7, -2),
                        textcoords="offset points",
                        fontsize=7,
                        color="#4A4A4A",
                    )

        axis.plot(
            mode_x,
            mode_y,
            color=color,
            linewidth=1.8,
            marker="o",
            markersize=4,
            zorder=2,
            label="modal accumCount",
        )
        axis.set_title(dataset.label, loc="left", fontsize=11, fontweight="bold")
        axis.set_ylabel("Ipatov accumCount\n(symbols)")
        axis.set_ylim(0, 21)
        axis.set_yticks((0, 4, 8, 12, 16, 20))
        axis.grid(axis="both", color="#D9D9D9", linewidth=0.7)
        axis.spines["top"].set_visible(False)
        axis.spines["right"].set_visible(False)

    all_leads = sorted(
        {
            run.lead_us
            for dataset in datasets
            for run in dataset.runs
            if run.lead_us is not None
        }
    )
    axes[-1, 0].set_xlabel("Delayed-RX lead margin (us)")
    axes[-1, 0].set_xticks(all_leads)
    axes[-1, 0].set_xlim(min(all_leads) - 1, max(all_leads) + 1)
    figure.suptitle(
        "32-symbol preamble: Ipatov accumCount and PER by lead margin",
        fontsize=15,
        fontweight="bold",
        y=0.995,
    )

    caption = (
        "Bubble area = fraction among successfully received frames; "
        "line = modal accumCount; N/A = no successful RX."
    )
    if notes:
        caption += "\nExcluded: " + "; ".join(notes)
    figure.text(0.5, 0.015, caption, ha="center", va="bottom", fontsize=8)
    figure.tight_layout(rect=(0.02, 0.06, 0.99, 0.97))
    return save_figure(figure, output_dir, prefix)


def write_csv(datasets: list[Dataset], path: Path) -> None:
    with path.open("w", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(
            [
                "dataset",
                "log_file",
                "lead_us",
                "per_percent",
                "rx",
                "expected",
                "accum_mode",
                "accum_mean",
                "accum_histogram",
            ]
        )
        for dataset in datasets:
            for run in dataset.runs:
                histogram = "|".join(
                    f"{accum}:{count}"
                    for accum, count in sorted(run.accum_histogram.items())
                )
                writer.writerow(
                    [
                        dataset.label,
                        run.path.name,
                        run.lead_us,
                        f"{run.per_percent:.2f}",
                        run.rx,
                        run.expected,
                        run.accum_mode,
                        "" if run.accum_mean is None else f"{run.accum_mean:.3f}",
                        histogram,
                    ]
                )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compare accumCount and PER across experiment-1 datasets."
    )
    parser.add_argument(
        "--dataset",
        nargs=2,
        action="append",
        metavar=("LABEL", "DIRECTORY"),
        required=True,
        help="Dataset label and directory; repeat for each dataset",
    )
    parser.add_argument("-o", "--output-dir", type=Path, required=True)
    parser.add_argument("--prefix", default="exp1_32sym_accum_per_comparison")
    parser.add_argument(
        "--exclude-lead",
        type=int,
        action="append",
        default=[],
        help="Lead value to exclude from every dataset; may be repeated",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    datasets: list[Dataset] = []
    warnings: list[str] = []
    excluded_leads = set(args.exclude_lead)
    for label, path_text in args.dataset:
        dataset, dataset_warnings = parse_dataset(
            label, Path(path_text), excluded_leads
        )
        datasets.append(dataset)
        warnings.extend(dataset_warnings)

    notes: list[str] = []
    if excluded_leads:
        excluded_text = ", ".join(str(lead) for lead in sorted(excluded_leads))
        notes.append(f"lead={excluded_text} us in all datasets")
    if any("repeated run suffix" in warning for warning in warnings):
        notes.append("home files with '-' repeat suffix")
    if any("filename lead=20, log header lead=18" in warning for warning in warnings):
        notes.append("home lead20 file (log header reports LEAD=18 us)")
    csv_path = args.output_dir / f"{args.prefix}.csv"
    write_csv(datasets, csv_path)
    outputs = [csv_path]
    outputs.extend(plot_datasets(datasets, args.output_dir, args.prefix, notes))

    for warning in warnings:
        print(f"WARNING: {warning}", file=sys.stderr)
    for dataset in datasets:
        leads = ",".join(str(run.lead_us) for run in dataset.runs)
        print(f"{dataset.label}: validated {len(dataset.runs)} logs, lead={leads} us")
    for output in outputs:
        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
