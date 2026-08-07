#!/usr/bin/env python3
"""Parse BRRS experiment-1 lead-sweep logs and create CSV/plot outputs."""

from __future__ import annotations

import argparse
import csv
import os
import re
import tempfile
from dataclasses import dataclass, field
from pathlib import Path


MATPLOTLIB_CACHE = Path(tempfile.gettempdir()) / "brrs-matplotlib"
MATPLOTLIB_CACHE.mkdir(parents=True, exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(MATPLOTLIB_CACHE))
os.environ.setdefault("XDG_CACHE_HOME", str(MATPLOTLIB_CACHE))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


HEADER_RE = re.compile(
    r"^BRRS v[\d.]+:.*DATA_PLEN=(?P<plen>\d+)\((?P<symbols>\d+)sym\).*"
    r"RX_WIN=(?P<rx_window>\d+)us LEAD=(?P<lead>\d+)us "
    r"TAIL=(?P<tail>\d+)us"
)
STATS_RE = re.compile(
    r"^N2: rx=(?P<rx>\d+) expected=(?P<expected>\d+) "
    r"miss=(?P<miss>\d+) PER=(?P<per>[\d.]+)% err=(?P<err>\d+)"
)
ERROR_RE = re.compile(
    r"^RX timeouts=(?P<timeouts>\d+) "
    r"\(fwto=(?P<fwto>\d+) pto=(?P<pto>\d+)\)\s+"
    r"RX errors=(?P<errors>\d+) "
    r"\(sfdto=(?P<sfdto>\d+) phe=(?P<phe>\d+) "
    r"fce=(?P<fce>\d+) fsl=(?P<fsl>\d+)\)\s+"
    r"delayed(?: schedule)? late=(?P<delayed_late>\d+)"
)
ACCUM_RE = re.compile(r"^accum=(?P<accum>\d+): n=(?P<count>\d+)")
DONE_RE = re.compile(
    r"^EXP1_DONE,plen=(?P<symbols>\d+),lead_us=(?P<lead>\d+),"
    r"tail_us=(?P<tail>\d+),expected=(?P<expected>\d+),rx=(?P<rx>\d+),"
    r"collection=(?P<collection>PASS|FAIL),link=(?P<link>PASS|LOSS),"
    r"status=(?P<status>PASS|FAIL)"
)
FILENAME_LEAD_RE = re.compile(r"lead(?P<lead>\d+)")

FAILURE_FIELDS = ("fwto", "pto", "sfdto", "phe", "fce", "fsl")
FAILURE_LABELS = {
    "fwto": "FWTO",
    "pto": "PTO",
    "sfdto": "SFDTO",
    "phe": "PHE",
    "fce": "FCE",
    "fsl": "FSL",
}
FAILURE_COLORS = {
    "fwto": "#4C78A8",
    "pto": "#72B7B2",
    "sfdto": "#E45756",
    "phe": "#F2CF5B",
    "fce": "#B279A2",
    "fsl": "#FF9D4D",
}


@dataclass
class Exp1Run:
    path: Path
    lead_us: int | None = None
    tail_us: int | None = None
    rx_window_us: int | None = None
    plen: int | None = None
    preamble_symbols: int | None = None
    rx: int | None = None
    expected: int | None = None
    miss: int | None = None
    per_percent: float | None = None
    err: int | None = None
    timeouts: int | None = None
    errors: int | None = None
    delayed_late: int | None = None
    failure_counts: dict[str, int] = field(default_factory=dict)
    accum_histogram: dict[int, int] = field(default_factory=dict)
    cycle_lines: int = 0
    completion_marker: bool = False
    completion_status: str = ""

    @property
    def accum_mode(self) -> str:
        if not self.accum_histogram:
            return ""
        max_count = max(self.accum_histogram.values())
        modes = sorted(k for k, v in self.accum_histogram.items() if v == max_count)
        return "|".join(str(mode) for mode in modes)

    @property
    def accum_mean(self) -> float | None:
        total = sum(self.accum_histogram.values())
        if total == 0:
            return None
        return sum(value * count for value, count in self.accum_histogram.items()) / total


def clean_line(raw_line: str) -> str:
    line = raw_line.strip()
    match = re.match(r"^\d+>\s*(.*)$", line)
    return match.group(1).strip() if match else line


def parse_log(path: Path) -> Exp1Run:
    run = Exp1Run(path=path)

    with path.open("r", errors="replace") as log_file:
        for raw_line in log_file:
            line = clean_line(raw_line)
            if line.startswith("CYCLE "):
                run.cycle_lines += 1

            match = HEADER_RE.match(line)
            if match:
                values = {key: int(value) for key, value in match.groupdict().items()}
                run.plen = values["plen"]
                run.preamble_symbols = values["symbols"]
                run.rx_window_us = values["rx_window"]
                run.lead_us = values["lead"]
                run.tail_us = values["tail"]
                continue

            match = STATS_RE.match(line)
            if match:
                run.rx = int(match.group("rx"))
                run.expected = int(match.group("expected"))
                run.miss = int(match.group("miss"))
                run.per_percent = float(match.group("per"))
                run.err = int(match.group("err"))
                continue

            match = ERROR_RE.match(line)
            if match:
                values = {key: int(value) for key, value in match.groupdict().items()}
                run.timeouts = values.pop("timeouts")
                run.errors = values.pop("errors")
                run.delayed_late = values.pop("delayed_late")
                run.failure_counts = values
                continue

            match = ACCUM_RE.match(line)
            if match:
                run.accum_histogram[int(match.group("accum"))] = int(match.group("count"))
                continue

            match = DONE_RE.match(line)
            if match:
                values = match.groupdict()
                run.completion_marker = True
                run.completion_status = values["status"]
                if run.preamble_symbols != int(values["symbols"]):
                    raise ValueError(f"{path.name}: EXP1_DONE preamble mismatch")
                if run.lead_us != int(values["lead"]) or run.tail_us != int(values["tail"]):
                    raise ValueError(f"{path.name}: EXP1_DONE margin mismatch")
                if run.expected != int(values["expected"]) or run.rx != int(values["rx"]):
                    raise ValueError(f"{path.name}: EXP1_DONE counter mismatch")
                continue

            # RTT loggers can append the next reset/header after a completed run.
            # Keep the first complete result instead of letting that trailing header
            # overwrite the configuration associated with the FINAL STATS block.
            if line == "===== END STATS =====" and run.per_percent is not None:
                break

    validate_run(run)
    return run


def validate_run(run: Exp1Run) -> None:
    required = {
        "lead_us": run.lead_us,
        "tail_us": run.tail_us,
        "rx_window_us": run.rx_window_us,
        "preamble_symbols": run.preamble_symbols,
        "rx": run.rx,
        "expected": run.expected,
        "miss": run.miss,
        "per_percent": run.per_percent,
        "timeouts": run.timeouts,
        "errors": run.errors,
        "delayed_late": run.delayed_late,
    }
    missing_fields = [name for name, value in required.items() if value is None]
    if missing_fields:
        raise ValueError(f"{run.path.name}: missing fields: {', '.join(missing_fields)}")

    filename_match = FILENAME_LEAD_RE.search(run.path.name)
    if not filename_match:
        raise ValueError(f"{run.path.name}: filename has no lead value")
    filename_lead = int(filename_match.group("lead"))
    if filename_lead != run.lead_us:
        raise ValueError(
            f"{run.path.name}: filename lead={filename_lead}, log header lead={run.lead_us}"
        )

    assert run.rx is not None
    assert run.expected is not None
    assert run.miss is not None
    assert run.per_percent is not None
    assert run.timeouts is not None
    assert run.errors is not None

    if run.expected - run.rx != run.miss:
        raise ValueError(f"{run.path.name}: expected - rx does not equal miss")
    if run.timeouts + run.errors != run.miss:
        raise ValueError(f"{run.path.name}: timeout/error total does not equal miss")
    calculated_per = 100.0 * run.miss / run.expected
    if abs(calculated_per - run.per_percent) > 0.005:
        raise ValueError(f"{run.path.name}: logged PER does not match miss/expected")
    if run.completion_marker and run.completion_status != "PASS":
        raise ValueError(f"{run.path.name}: EXP1_DONE collection status is FAIL")
    if not run.completion_marker and run.cycle_lines < run.expected:
        raise ValueError(
            f"{run.path.name}: no EXP1_DONE marker and only {run.cycle_lines} "
            f"legacy cycle lines for {run.expected} frames"
        )
    if sum(run.accum_histogram.values()) != run.rx:
        raise ValueError(
            f"{run.path.name}: accum histogram count does not equal successful RX count"
        )


def collect_logs(inputs: list[Path]) -> list[Path]:
    paths: list[Path] = []
    for input_path in inputs:
        if input_path.is_dir():
            paths.extend(sorted(input_path.glob("*.log")))
        elif input_path.is_file():
            paths.append(input_path)
        else:
            raise FileNotFoundError(input_path)
    if not paths:
        raise ValueError("No .log files found")
    return paths


def parse_runs(paths: list[Path]) -> list[Exp1Run]:
    runs = sorted((parse_log(path) for path in paths), key=lambda run: run.lead_us or -1)
    seen: dict[int, Path] = {}
    for run in runs:
        assert run.lead_us is not None
        if run.lead_us in seen:
            raise ValueError(
                f"duplicate lead={run.lead_us}: {seen[run.lead_us].name}, {run.path.name}"
            )
        seen[run.lead_us] = run.path
    return runs


def write_summary_csv(runs: list[Exp1Run], path: Path) -> None:
    fields = [
        "log_file",
        "lead_us",
        "tail_us",
        "rx_window_us",
        "preamble_symbols",
        "expected",
        "rx",
        "miss",
        "per_percent",
        "timeouts",
        "errors",
        "fwto",
        "pto",
        "sfdto",
        "phe",
        "fce",
        "fsl",
        "delayed_late",
        "accum_mode",
        "accum_mean",
        "cycle_lines",
        "completion_marker",
        "completion_status",
    ]
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        for run in runs:
            writer.writerow(
                {
                    "log_file": run.path.name,
                    "lead_us": run.lead_us,
                    "tail_us": run.tail_us,
                    "rx_window_us": run.rx_window_us,
                    "preamble_symbols": run.preamble_symbols,
                    "expected": run.expected,
                    "rx": run.rx,
                    "miss": run.miss,
                    "per_percent": f"{run.per_percent:.2f}",
                    "timeouts": run.timeouts,
                    "errors": run.errors,
                    **{field: run.failure_counts.get(field, 0) for field in FAILURE_FIELDS},
                    "delayed_late": run.delayed_late,
                    "accum_mode": run.accum_mode,
                    "accum_mean": "" if run.accum_mean is None else f"{run.accum_mean:.3f}",
                    "cycle_lines": run.cycle_lines,
                    "completion_marker": run.completion_marker,
                    "completion_status": run.completion_status,
                }
            )


def write_accum_csv(runs: list[Exp1Run], path: Path) -> None:
    with path.open("w", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(["log_file", "lead_us", "accum_count", "frame_count", "fraction"])
        for run in runs:
            total = sum(run.accum_histogram.values())
            for accum, count in sorted(run.accum_histogram.items()):
                fraction = count / total if total else 0.0
                writer.writerow([run.path.name, run.lead_us, accum, count, f"{fraction:.6f}"])


def style_axis(axis: plt.Axes) -> None:
    axis.grid(axis="y", color="#D9D9D9", linewidth=0.8)
    axis.spines["top"].set_visible(False)
    axis.spines["right"].set_visible(False)


def save_figure(figure: plt.Figure, output_dir: Path, stem: str) -> list[Path]:
    outputs = []
    for suffix in (".png", ".svg"):
        path = output_dir / f"{stem}{suffix}"
        figure.savefig(path, dpi=220, bbox_inches="tight")
        outputs.append(path)
    plt.close(figure)
    return outputs


def plot_per(runs: list[Exp1Run], output_dir: Path, prefix: str) -> list[Path]:
    leads = [run.lead_us for run in runs]
    per_values = [run.per_percent for run in runs]

    figure, axes = plt.subplots(
        2,
        1,
        figsize=(9.2, 6.5),
        sharex=True,
        gridspec_kw={"height_ratios": [2.2, 1.4]},
    )
    main_axis, zoom_axis = axes

    main_axis.plot(leads, per_values, color="#3A6EA5", marker="o", linewidth=2)
    main_axis.axvspan(4, 6, color="#F2CF5B", alpha=0.25)
    main_axis.set_ylabel("PER (%)")
    main_axis.set_ylim(-3, 105)
    main_axis.set_title("Experiment 1: PER vs. delayed-RX lead margin")
    main_axis.text(5, 83, "Transition\n4-6 us", ha="center", va="center")
    style_axis(main_axis)

    for lead, per_value in zip(leads, per_values):
        main_axis.annotate(
            f"{per_value:.1f}%",
            (lead, per_value),
            xytext=(0, 8),
            textcoords="offset points",
            ha="center",
            fontsize=8,
        )

    zoom_leads = [run.lead_us for run in runs if (run.lead_us or 0) >= 6]
    zoom_values = [run.per_percent for run in runs if (run.lead_us or 0) >= 6]
    zoom_axis.plot(zoom_leads, zoom_values, color="#E45756", marker="o", linewidth=2)
    zoom_axis.set_ylabel("PER (%)")
    zoom_axis.set_xlabel("Lead margin (us)")
    zoom_axis.set_ylim(-0.05, max(1.0, max(zoom_values) + 0.15))
    zoom_axis.set_xticks(leads)
    zoom_axis.set_title("Low-PER region (lead >= 6 us)", fontsize=10)
    style_axis(zoom_axis)

    for lead, per_value in zip(zoom_leads, zoom_values):
        zoom_axis.annotate(
            f"{per_value:.1f}",
            (lead, per_value),
            xytext=(0, 7),
            textcoords="offset points",
            ha="center",
            fontsize=8,
        )

    figure.tight_layout()
    return save_figure(figure, output_dir, f"{prefix}_per_by_lead")


def plot_accum(runs: list[Exp1Run], output_dir: Path, prefix: str) -> list[Path]:
    figure, axis = plt.subplots(figsize=(9.2, 4.8))
    mode_x: list[int] = []
    mode_y: list[int] = []

    for run in runs:
        if not run.accum_histogram:
            continue
        total = sum(run.accum_histogram.values())
        mode_value = max(run.accum_histogram, key=run.accum_histogram.get)
        mode_x.append(run.lead_us or 0)
        mode_y.append(mode_value)
        for accum, count in sorted(run.accum_histogram.items()):
            fraction = count / total
            axis.scatter(
                run.lead_us,
                accum,
                s=50 + 240 * fraction,
                color="#59A14F",
                edgecolor="#2F5D36",
                linewidth=0.8,
                zorder=3,
            )
            if count != total:
                axis.annotate(
                    f"n={count}",
                    (run.lead_us, accum),
                    xytext=(6, -2),
                    textcoords="offset points",
                    fontsize=8,
                )

    axis.plot(mode_x, mode_y, color="#2F5D36", linewidth=1.5, zorder=2)
    for lead, accum in zip(mode_x, mode_y):
        axis.annotate(
            str(accum),
            (lead, accum),
            xytext=(0, 8),
            textcoords="offset points",
            ha="center",
            fontsize=9,
        )

    axis.set_title("Experiment 1: Ipatov accumCount vs. lead margin")
    axis.set_xlabel("Lead margin (us)")
    axis.set_ylabel("accumCount (symbols)")
    axis.set_xticks([run.lead_us for run in runs])
    axis.set_ylim(0, max(20, max(mode_y, default=0) + 3))
    style_axis(axis)
    figure.tight_layout()
    return save_figure(figure, output_dir, f"{prefix}_accum_by_lead")


def draw_failure_bars(axis: plt.Axes, runs: list[Exp1Run], title: str) -> None:
    leads = [run.lead_us for run in runs]
    bottom = [0] * len(runs)
    for field in FAILURE_FIELDS:
        values = [run.failure_counts.get(field, 0) for run in runs]
        if not any(values):
            continue
        axis.bar(
            leads,
            values,
            bottom=bottom,
            width=1.45,
            label=FAILURE_LABELS[field],
            color=FAILURE_COLORS[field],
        )
        bottom = [base + value for base, value in zip(bottom, values)]

    for lead, total in zip(leads, bottom):
        if total:
            axis.annotate(
                str(total),
                (lead, total),
                xytext=(0, 4),
                textcoords="offset points",
                ha="center",
                fontsize=8,
            )
    axis.set_title(title, fontsize=10)
    axis.set_xticks(leads)
    style_axis(axis)


def plot_failures(runs: list[Exp1Run], output_dir: Path, prefix: str) -> list[Path]:
    figure, axes = plt.subplots(2, 1, figsize=(9.2, 6.5))
    draw_failure_bars(axes[0], runs, "All lead settings")
    axes[0].set_ylabel("Failed frames")
    axes[0].set_title("Experiment 1: failure causes by lead margin")

    transition_runs = [run for run in runs if (run.lead_us or 0) >= 6]
    draw_failure_bars(axes[1], transition_runs, "Detailed view: lead >= 6 us")
    axes[1].set_ylabel("Failed frames")
    axes[1].set_xlabel("Lead margin (us)")
    axes[1].set_ylim(0, max(10, max((run.miss or 0) for run in transition_runs) + 2))

    handles, labels = axes[0].get_legend_handles_labels()
    figure.legend(handles, labels, loc="upper center", ncol=max(1, len(labels)), frameon=False)
    figure.tight_layout(rect=(0, 0, 1, 0.93))
    return save_figure(figure, output_dir, f"{prefix}_failure_by_lead")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Create CSV and plots from BRRS experiment-1 lead-sweep logs."
    )
    parser.add_argument(
        "inputs",
        type=Path,
        nargs="+",
        help="Log files or a directory containing lead-sweep .log files",
    )
    parser.add_argument("-o", "--output-dir", type=Path, required=True)
    parser.add_argument("--prefix", default="exp1_32sym")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    log_paths = collect_logs(args.inputs)
    runs = parse_runs(log_paths)

    summary_path = args.output_dir / f"{args.prefix}_summary.csv"
    accum_path = args.output_dir / f"{args.prefix}_accum_histogram.csv"
    write_summary_csv(runs, summary_path)
    write_accum_csv(runs, accum_path)

    outputs = [summary_path, accum_path]
    outputs.extend(plot_per(runs, args.output_dir, args.prefix))
    outputs.extend(plot_accum(runs, args.output_dir, args.prefix))
    outputs.extend(plot_failures(runs, args.output_dir, args.prefix))

    print(f"Validated {len(runs)} logs: lead={','.join(str(run.lead_us) for run in runs)} us")
    for output in outputs:
        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
