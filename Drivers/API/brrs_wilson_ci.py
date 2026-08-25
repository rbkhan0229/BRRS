#!/usr/bin/env python3
"""Extract BRRS RX totals and report Wilson confidence intervals for PER."""

import argparse
import csv
import math
import sys
from pathlib import Path
from statistics import NormalDist


MARKERS = (
    ("stage0_or_exp1", "EXP1_DONE,"),
    ("exp2_or_exp5", "EXP2_DONE,"),
    ("exp3", "EXP3_RX_DONE,"),
    ("exp4", "EXP4_DONE,"),
)


def key_values(line: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for field in line.split(",")[1:]:
        key, separator, value = field.partition("=")
        if separator:
            values[key] = value
    return values


def last_values(lines: list[str], prefix: str) -> dict[str, str]:
    matches = [key_values(line) for line in lines if line.startswith(prefix)]
    return matches[-1] if matches else {}


def integer(values: dict[str, str], key: str) -> int:
    try:
        return int(values[key], 0)
    except (KeyError, ValueError) as exc:
        raise ValueError(f"missing or invalid {key!r}") from exc


def wilson_interval(events: int, trials: int, confidence: float) -> tuple[float, float]:
    if trials <= 0 or not 0 <= events <= trials:
        raise ValueError("invalid event/trial totals")
    z = NormalDist().inv_cdf(0.5 + confidence / 2.0)
    p_hat = events / trials
    z2 = z * z
    denominator = 1.0 + z2 / trials
    center = (p_hat + z2 / (2.0 * trials)) / denominator
    half_width = z * math.sqrt(
        p_hat * (1.0 - p_hat) / trials + z2 / (4.0 * trials * trials)
    ) / denominator
    return max(0.0, center - half_width), min(1.0, center + half_width)


def discover_paths(inputs: list[Path]) -> list[Path]:
    paths: list[Path] = []
    for item in inputs:
        if item.is_dir():
            paths.extend(sorted(item.rglob("*.log")))
        else:
            paths.append(item)
    return paths


def parse_log(path: Path, confidence: float) -> dict[str, object] | None:
    lines = path.read_text(errors="replace").splitlines()
    marker_name = ""
    values: dict[str, str] = {}
    for name, prefix in MARKERS:
        candidate = last_values(lines, prefix)
        if candidate:
            marker_name = name
            values = candidate
            break
    if not values:
        return None

    expected = integer(values, "expected")
    received = integer(values, "rx")
    missed = expected - received
    lower, upper = wilson_interval(missed, expected, confidence)

    experiment = marker_name
    config = last_values(lines, "EXP_LOG_CONFIG_CSV,")
    exp4_config = last_values(lines, "EXP4_CONFIG_CSV,")
    if marker_name == "stage0_or_exp1":
        experiment = "stage0" if "stage0" in path.name.lower() else "exp1"
    elif marker_name == "exp2_or_exp5":
        experiment = "exp5" if config.get("experiment") == "5" else "exp2"

    return {
        "path": str(path),
        "experiment": experiment,
        "plen": values.get("plen", config.get("plen", exp4_config.get("data_plen", ""))),
        "lead_us": values.get("lead_us", config.get("lead_us", exp4_config.get("lead_us", ""))),
        "tail_us": values.get("tail_us", config.get("tail_us", exp4_config.get("tail_us", ""))),
        "pac": values.get("pac", exp4_config.get("data_pac", "")),
        "rx_mode": values.get("rx_mode", ""),
        "physical_sensors": values.get("physical_sensors", exp4_config.get("physical_sensors", "")),
        "data_slots": values.get("data_slots", exp4_config.get("data_slots", "")),
        "expected": expected,
        "rx": received,
        "miss": missed,
        "per_percent": 100.0 * missed / expected,
        "per_ci_low_percent": 100.0 * lower,
        "per_ci_high_percent": 100.0 * upper,
        "confidence_percent": 100.0 * confidence,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path,
                        help="RX raw log files or directories searched recursively")
    parser.add_argument("-o", "--output", type=Path,
                        help="CSV output path; stdout when omitted")
    parser.add_argument("--confidence", type=float, default=0.95,
                        help="interval confidence in (0,1), default 0.95")
    args = parser.parse_args()
    if not 0.0 < args.confidence < 1.0:
        parser.error("confidence must be between 0 and 1")

    rows = []
    for path in discover_paths(args.inputs):
        try:
            row = parse_log(path, args.confidence)
        except (OSError, ValueError) as exc:
            print(f"warning: {path}: {exc}", file=sys.stderr)
            continue
        if row is not None:
            rows.append(row)
    if not rows:
        print("no supported BRRS RX result markers found", file=sys.stderr)
        return 2

    fieldnames = list(rows[0])
    stream = args.output.open("w", newline="") if args.output else sys.stdout
    try:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            formatted = dict(row)
            for key in ("per_percent", "per_ci_low_percent", "per_ci_high_percent"):
                formatted[key] = f"{float(row[key]):.6f}"
            formatted["confidence_percent"] = f"{float(row['confidence_percent']):.2f}"
            writer.writerow(formatted)
    finally:
        if args.output:
            stream.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
