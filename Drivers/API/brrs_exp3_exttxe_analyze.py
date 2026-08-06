#!/usr/bin/env python3
"""Validate and analyze BRRS Experiment 3 EXTTXE/RX logs."""

from __future__ import annotations

import argparse
import csv
import os
import re
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path


TX_RAW_RE = re.compile(
    r"EXP3_TX_CSV,(\d+),([ABC]),(\d+),(STD|DTA),(\d+),(\d+),(\d+)"
)
TX_SUMMARY_RE = re.compile(
    r"EXP3_TX_SUMMARY_CSV,([ABC]),(\d+),(STD|DTA),(\d+),"
    r"(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)"
)
TX_RESULT_RE = re.compile(
    r"EXP3_TX_RESULT,variant=([ABC]),attempts=(\d+),success=(\d+),"
    r"captures=(\d+),status=(PASS|FAIL)"
)
RX_RESULT_RE = re.compile(
    r"EXP3_RX_RESULT_CSV,([ABC]),(\d+),(STD|DTA),(\d+),"
    r"(\d+),(\d+),(\d+),(\d+),(\d+),(PASS|FAIL)"
)

VARIANT_ORDER = ("A", "B", "C")
VARIANT_LABELS = {
    "A": "A: SFD 8 / PHR STD",
    "B": "B: SFD 16 / PHR STD",
    "C": "C: SFD 8 / PHR DTA",
}


@dataclass(frozen=True)
class TxSummary:
    variant: str
    sfd_symbols: int
    phr_rate: str
    psdu_bytes: int
    attempts: int
    success: int
    captures: int
    invalid: int
    min_ns: int
    max_ns: int
    avg_ns: int
    theory_ns: int


@dataclass(frozen=True)
class RxResult:
    variant: str
    sfd_symbols: int
    phr_rate: str
    psdu_bytes: int
    expected: int
    received: int
    missed: int
    per_x1000: int
    theory_ns: int
    status: str


def fail(message: str) -> None:
    raise ValueError(message)


def add_unique(target: dict, key: str, value: object, source: Path) -> None:
    if key in target and target[key] != value:
        fail(f"conflicting {key} records, latest source: {source}")
    target[key] = value


def parse_logs(paths: list[Path]):
    raw: dict[str, dict[int, tuple[int, int]]] = {}
    summaries: dict[str, TxSummary] = {}
    tx_results: dict[str, tuple[int, int, int, str]] = {}
    rx_results: dict[str, RxResult] = {}

    for path in paths:
        if not path.is_file():
            fail(f"log file not found: {path}")

        for line in path.read_text(errors="replace").splitlines():
            match = TX_RAW_RE.search(line)
            if match:
                seq, variant, _, _, _, ticks, width_ns = match.groups()
                seq_i = int(seq)
                value = (int(ticks), int(width_ns))
                variant_rows = raw.setdefault(variant, {})
                if seq_i in variant_rows:
                    fail(f"duplicate TX sample {variant}/{seq_i} in {path}")
                variant_rows[seq_i] = value
                continue

            match = TX_SUMMARY_RE.search(line)
            if match:
                fields = match.groups()
                summary = TxSummary(
                    fields[0], int(fields[1]), fields[2], int(fields[3]),
                    *[int(value) for value in fields[4:]],
                )
                add_unique(summaries, summary.variant, summary, path)
                continue

            match = TX_RESULT_RE.search(line)
            if match:
                variant, attempts, success, captures, status = match.groups()
                add_unique(
                    tx_results,
                    variant,
                    (int(attempts), int(success), int(captures), status),
                    path,
                )
                continue

            match = RX_RESULT_RE.search(line)
            if match:
                fields = match.groups()
                result = RxResult(
                    fields[0], int(fields[1]), fields[2], int(fields[3]),
                    int(fields[4]), int(fields[5]), int(fields[6]),
                    int(fields[7]), int(fields[8]), fields[9],
                )
                add_unique(rx_results, result.variant, result, path)

    return raw, summaries, tx_results, rx_results


def validate(raw, summaries, tx_results, rx_results, expected: int) -> None:
    for variant in VARIANT_ORDER:
        if variant not in summaries:
            fail(f"missing EXP3_TX_SUMMARY_CSV for variant {variant}")
        if variant not in tx_results:
            fail(f"missing EXP3_TX_RESULT for variant {variant}")

        rows = raw.get(variant, {})
        expected_seq = set(range(1, expected + 1))
        missing_seq = sorted(expected_seq - set(rows))
        extra_seq = sorted(set(rows) - expected_seq)
        if len(rows) != expected or missing_seq or extra_seq:
            detail = (
                f"variant {variant}: expected {expected} unique raw TX rows, "
                f"found {len(rows)}"
            )
            if missing_seq:
                detail += f"; missing seq starts {missing_seq[:10]}"
            if extra_seq:
                detail += f"; unexpected seq starts {extra_seq[:10]}"
            fail(detail)

        summary = summaries[variant]
        attempts, success, captures, status = tx_results[variant]
        if status != "PASS":
            fail(f"variant {variant}: firmware TX result is {status}")
        if (
            attempts != expected
            or success != expected
            or captures != expected
            or summary.attempts != expected
            or summary.success != expected
            or summary.captures != expected
            or summary.invalid != 0
        ):
            fail(f"variant {variant}: TX counters are not a clean {expected}/{expected}")

    if rx_results and set(rx_results) != set(VARIANT_ORDER):
        missing = sorted(set(VARIANT_ORDER) - set(rx_results))
        fail(f"RX logs were supplied but variants are incomplete: missing {missing}")


def write_csv(path: Path, fieldnames: list[str], rows: list[dict]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def analyze(raw, summaries, rx_results, output_dir: Path, prefix: str) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    summary_rows = []
    means: dict[str, float] = {}

    for variant in VARIANT_ORDER:
        widths = [raw[variant][seq][1] for seq in sorted(raw[variant])]
        summary = summaries[variant]
        measured_mean = statistics.fmean(widths)
        means[variant] = measured_mean
        summary_rows.append(
            {
                "variant": variant,
                "sfd_symbols": summary.sfd_symbols,
                "phr_rate": summary.phr_rate,
                "psdu_bytes": summary.psdu_bytes,
                "samples": len(widths),
                "measured_min_ns": min(widths),
                "measured_max_ns": max(widths),
                "measured_mean_ns": f"{measured_mean:.3f}",
                "measured_stdev_ns": f"{statistics.stdev(widths):.3f}",
                "theory_airtime_ns": summary.theory_ns,
                "fixed_offset_ns": f"{measured_mean - summary.theory_ns:.3f}",
            }
        )

    summary_path = output_dir / f"{prefix}_summary.csv"
    write_csv(summary_path, list(summary_rows[0]), summary_rows)

    comparisons = [
        ("B-A", "SFD 16 - SFD 8", "B", "A"),
        ("A-C", "PHR STD - PHR DTA", "A", "C"),
    ]
    diff_rows = []
    for name, meaning, positive, negative in comparisons:
        measured = means[positive] - means[negative]
        theory = (
            summaries[positive].theory_ns - summaries[negative].theory_ns
        )
        diff_rows.append(
            {
                "comparison": name,
                "meaning": meaning,
                "measured_diff_ns": f"{measured:.3f}",
                "theory_diff_ns": theory,
                "error_ns": f"{measured - theory:.3f}",
                "error_percent": (
                    f"{100.0 * (measured - theory) / theory:.3f}"
                    if theory else "nan"
                ),
            }
        )

    diff_path = output_dir / f"{prefix}_differential.csv"
    write_csv(diff_path, list(diff_rows[0]), diff_rows)

    if rx_results:
        rx_rows = []
        for variant in VARIANT_ORDER:
            result = rx_results[variant]
            rx_rows.append(
                {
                    "variant": variant,
                    "expected": result.expected,
                    "received": result.received,
                    "missed": result.missed,
                    "per_percent": f"{result.per_x1000 / 1000.0:.3f}",
                    "collection_status": result.status,
                }
            )
        write_csv(
            output_dir / f"{prefix}_rx_per.csv",
            list(rx_rows[0]),
            rx_rows,
        )

    mpl_config = output_dir / ".matplotlib"
    cache_home = output_dir / ".cache"
    mpl_config.mkdir(exist_ok=True)
    cache_home.mkdir(exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(mpl_config))
    os.environ.setdefault("XDG_CACHE_HOME", str(cache_home))

    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError as exc:
        fail(
            "CSV files were created, but matplotlib is required for plots: "
            f"{exc}"
        )

    colors = ["#2878B5", "#D95F02", "#2A9D6F"]
    labels = [VARIANT_LABELS[v] for v in VARIANT_ORDER]
    measured_us = [means[v] / 1000.0 for v in VARIANT_ORDER]
    theory_us = [summaries[v].theory_ns / 1000.0 for v in VARIANT_ORDER]

    fig, ax = plt.subplots(figsize=(10, 5.6))
    x = list(range(3))
    width = 0.36
    ax.bar(
        [value - width / 2 for value in x],
        measured_us,
        width,
        label="Measured EXTTXE width",
        color=colors,
    )
    ax.bar(
        [value + width / 2 for value in x],
        theory_us,
        width,
        label="Analytical airtime",
        color="#B8BDC6",
    )
    ax.set_xticks(x, labels)
    ax.set_ylabel("Time (us)")
    ax.set_title("Experiment 3: measured TX duration and analytical airtime")
    ax.grid(axis="y", alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(output_dir / f"{prefix}_airtime.png", dpi=200)
    plt.close(fig)

    measured_diff_us = [
        float(row["measured_diff_ns"]) / 1000.0 for row in diff_rows
    ]
    theory_diff_us = [
        float(row["theory_diff_ns"]) / 1000.0 for row in diff_rows
    ]
    fig, ax = plt.subplots(figsize=(8.5, 5.4))
    x = list(range(2))
    ax.bar(
        [value - width / 2 for value in x],
        measured_diff_us,
        width,
        label="Measured difference",
        color=["#D95F02", "#2A9D6F"],
    )
    ax.bar(
        [value + width / 2 for value in x],
        theory_diff_us,
        width,
        label="Analytical difference",
        color="#B8BDC6",
    )
    ax.set_xticks(x, [row["meaning"] for row in diff_rows])
    ax.set_ylabel("Saved/added time (us)")
    ax.set_title("Experiment 3: differential validation")
    ax.grid(axis="y", alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(output_dir / f"{prefix}_differential.png", dpi=200)
    plt.close(fig)

    if rx_results:
        per_values = [
            rx_results[v].per_x1000 / 1000.0 for v in VARIANT_ORDER
        ]
        fig, ax = plt.subplots(figsize=(8.5, 5.2))
        bars = ax.bar(labels, per_values, color=colors)
        ax.bar_label(bars, fmt="%.3f%%", padding=3)
        ax.set_ylabel("PER (%)")
        ax.set_title("Experiment 3: packet error rate by PHY condition")
        ax.grid(axis="y", alpha=0.25)
        upper = max(per_values)
        ax.set_ylim(0, max(0.1, upper * 1.2))
        fig.tight_layout()
        fig.savefig(output_dir / f"{prefix}_rx_per.png", dpi=200)
        plt.close(fig)

    print(f"PASS: all A/B/C TX logs contain exactly {len(raw['A'])} samples.")
    for row in diff_rows:
        print(
            f"{row['comparison']} ({row['meaning']}): "
            f"measured={float(row['measured_diff_ns']) / 1000.0:.3f} us, "
            f"theory={float(row['theory_diff_ns']) / 1000.0:.3f} us, "
            f"error={float(row['error_ns']) / 1000.0:.3f} us"
        )
    print(f"Results: {output_dir}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Validate 1000 EXTTXE samples per A/B/C condition and generate "
            "Experiment 3 differential-airtime results."
        )
    )
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("-o", "--output-dir", type=Path, required=True)
    parser.add_argument("--prefix", default="exp3_exttxe")
    parser.add_argument("--expected", type=int, default=1000)
    args = parser.parse_args()

    try:
        parsed = parse_logs(args.logs)
        validate(*parsed, expected=args.expected)
        analyze(parsed[0], parsed[1], parsed[3], args.output_dir, args.prefix)
    except ValueError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
