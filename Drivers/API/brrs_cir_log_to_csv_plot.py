#!/usr/bin/env python3
"""Validate raw BRRS experiment-2 CIR logs and create CSV/SVG results."""

from __future__ import annotations

import argparse
import csv
import math
import re
import statistics
from collections import defaultdict
from pathlib import Path


SAMPLE_FIELDS = [
    "log_file",
    "run",
    "environment",
    "distance_m",
    "frame",
    "rx_seq",
    "cycle",
    "node",
    "plen",
    "fp_sample",
    "peak_idx",
    "accum",
    "rssi_dbm",
    "fp_dbm",
    "rssi_fp_gap_db",
    "fp_peak_power",
    "noise_floor_power",
    "noise_samples",
    "fp_snr_ratio_x1000",
    "fp_snr_ratio",
    "fp_snr_db",
    "processing_gain_db",
]

SUMMARY_FIELDS = [
    "log_file",
    "run",
    "environment",
    "distance_m",
    "node",
    "plen",
    "n",
    "expected_n",
    "missing_n",
    "status",
    "fp_snr_db_min",
    "fp_snr_db_max",
    "fp_snr_db_mean",
    "fp_snr_db_median",
    "fp_snr_db_stdev",
    "rssi_dbm_mean",
    "fp_dbm_mean",
    "processing_gain_db",
]


def clean_line(line: str) -> str:
    line = line.strip()
    match = re.match(r"^\d+>\s*(.*)$", line)
    if match:
        line = match.group(1).strip()
    return line


def to_int(value: str) -> int:
    return int(value.strip())


def to_float(value: str) -> float:
    return float(value.strip())


def ratio_x1000_to_db(value: int) -> float:
    ratio = value / 1000.0
    return 10.0 * math.log10(ratio) if ratio > 0 else float("nan")


def x100_to_float(value: int) -> float:
    return value / 100.0


def parse_log(path: Path, run: str, environment: str, distance_m: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []

    with path.open("r", errors="replace") as f:
        for raw_line in f:
            line = clean_line(raw_line)
            idx = line.find("CIR_CSV,")
            if idx < 0:
                continue

            parts = next(csv.reader([line[idx:]]))
            if len(parts) < 14:
                continue

            if len(parts) >= 15:
                _, rx_seq, cycle, node, plen, fp_sample, peak_idx, accum, rssi_dbm, fp_dbm, gap_db, fp_peak, noise_floor, noise_samples, ratio_x1000 = parts[:15]
            else:
                _, rx_seq, node, plen, fp_sample, peak_idx, accum, rssi_dbm, fp_dbm, gap_db, fp_peak, noise_floor, noise_samples, ratio_x1000 = parts[:14]
                cycle = ""

            fp_peak_power = to_int(fp_peak)
            noise_floor_power = to_int(noise_floor)
            snr_ratio_x1000 = to_int(ratio_x1000)
            snr_ratio = snr_ratio_x1000 / 1000.0
            fp_snr_db = ratio_x1000_to_db(snr_ratio_x1000)
            plen_i = to_int(plen)
            rx_seq_i = to_int(rx_seq)
            cycle_i: int | str = to_int(cycle) if cycle else ""
            processing_gain_db = 10.0 * math.log10(127.0 * plen_i) if plen_i > 0 else float("nan")

            rows.append(
                {
                    "log_file": path.name,
                    "run": run,
                    "environment": environment,
                    "distance_m": distance_m,
                    "frame": rx_seq_i,
                    "rx_seq": rx_seq_i,
                    "cycle": cycle_i,
                    "node": node,
                    "plen": plen_i,
                    "fp_sample": to_int(fp_sample),
                    "peak_idx": to_int(peak_idx),
                    "accum": to_int(accum),
                    "rssi_dbm": to_float(rssi_dbm),
                    "fp_dbm": to_float(fp_dbm),
                    "rssi_fp_gap_db": to_float(gap_db),
                    "fp_peak_power": fp_peak_power,
                    "noise_floor_power": noise_floor_power,
                    "noise_samples": to_int(noise_samples),
                    "fp_snr_ratio_x1000": snr_ratio_x1000,
                    "fp_snr_ratio": snr_ratio,
                    "fp_snr_db": fp_snr_db,
                    "processing_gain_db": processing_gain_db,
                }
            )

    return rows


def run_status(n: int, expected_samples: int) -> str:
    return "PASS" if n == expected_samples else "FAIL"


def validate_raw_log(path: Path, rows: list[dict[str, object]], expected_samples: int) -> None:
    groups: dict[tuple[str, int], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        groups[(str(row["node"]), int(row["plen"]))].append(row)

    if not groups:
        raise ValueError(
            f"{path}: no raw CIR_CSV samples; a CIR_SUMMARY_CSV line is not a "
            "substitute for the required per-frame samples"
        )

    for (node, plen), group in groups.items():
        valid = [
            row for row in group
            if int(row["fp_snr_ratio_x1000"]) > 0
            and not math.isnan(float(row["fp_snr_db"]))
        ]
        sequences = [int(row["rx_seq"]) for row in group]
        unique_sequences = set(sequences)
        expected_sequences = set(range(1, expected_samples + 1))

        if len(group) != expected_samples:
            raise ValueError(
                f"{path}: {node}/{plen}sym has {len(group)} raw CIR_CSV rows; "
                f"expected exactly {expected_samples}"
            )
        if len(unique_sequences) != expected_samples:
            raise ValueError(
                f"{path}: {node}/{plen}sym has duplicate rx_seq values "
                f"({len(unique_sequences)} unique of {expected_samples})"
            )
        if unique_sequences != expected_sequences:
            missing = sorted(expected_sequences - unique_sequences)
            extra = sorted(unique_sequences - expected_sequences)
            raise ValueError(
                f"{path}: {node}/{plen}sym rx_seq is incomplete; "
                f"missing={missing[:10]}, extra={extra[:10]}"
            )
        if len(valid) != expected_samples:
            raise ValueError(
                f"{path}: {node}/{plen}sym has {len(valid)} valid first-path "
                f"SNR samples; expected exactly {expected_samples}"
            )


def validate_firmware_summary(path: Path,
                              raw_summaries: list[dict[str, object]],
                              firmware_summaries: list[dict[str, object]]) -> None:
    if not firmware_summaries:
        raise ValueError(f"{path}: CIR_SUMMARY_CSV not found")

    raw_by_key = {(str(row["node"]), int(row["plen"])): row for row in raw_summaries}
    firmware_by_key = {
        (str(row["node"]), int(row["plen"])): row for row in firmware_summaries
    }
    if raw_by_key.keys() != firmware_by_key.keys():
        raise ValueError(f"{path}: raw CIR groups and CIR_SUMMARY_CSV groups differ")

    for key, raw_summary in raw_by_key.items():
        firmware_summary = firmware_by_key[key]
        if int(raw_summary["n"]) != int(firmware_summary["n"]):
            raise ValueError(
                f"{path}: {key[0]}/{key[1]}sym raw sample count "
                f"{raw_summary['n']} disagrees with firmware summary "
                f"{firmware_summary['n']}"
            )


def parse_summary_log(path: Path, run: str, environment: str, distance_m: str, expected_samples: int) -> list[dict[str, object]]:
    summaries: list[dict[str, object]] = []

    with path.open("r", errors="replace") as f:
        for raw_line in f:
            line = clean_line(raw_line)
            idx = line.find("CIR_SUMMARY_CSV,")
            if idx < 0:
                continue

            parts = next(csv.reader([line[idx:]]))
            if len(parts) < 13:
                continue

            _, node, plen, n, snr_min, snr_max, snr_avg, _rssi_min, _rssi_max, rssi_avg, _fp_min, _fp_max, fp_avg = parts[:13]
            plen_i = to_int(plen)
            snr_min_i = to_int(snr_min)
            snr_max_i = to_int(snr_max)
            snr_avg_i = to_int(snr_avg)
            n_i = to_int(n)

            summaries.append(
                {
                    "log_file": path.name,
                    "run": run,
                    "environment": environment,
                    "distance_m": distance_m,
                    "node": node,
                    "plen": plen_i,
                    "n": n_i,
                    "expected_n": expected_samples,
                    "missing_n": max(expected_samples - n_i, 0),
                    "status": run_status(n_i, expected_samples),
                    "fp_snr_db_min": ratio_x1000_to_db(snr_min_i),
                    "fp_snr_db_max": ratio_x1000_to_db(snr_max_i),
                    "fp_snr_db_mean": ratio_x1000_to_db(snr_avg_i),
                    "fp_snr_db_median": float("nan"),
                    "fp_snr_db_stdev": float("nan"),
                    "rssi_dbm_mean": x100_to_float(to_int(rssi_avg)),
                    "fp_dbm_mean": x100_to_float(to_int(fp_avg)),
                    "processing_gain_db": 10.0 * math.log10(127.0 * plen_i) if plen_i > 0 else float("nan"),
                }
            )

    return summaries


def fmt(value: object) -> object:
    if isinstance(value, float):
        if math.isnan(value):
            return ""
        return f"{value:.3f}"
    return value


def write_csv(path: Path, fields: list[str], rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({field: fmt(row.get(field, "")) for field in fields})


def summarize(rows: list[dict[str, object]], expected_samples: int) -> list[dict[str, object]]:
    groups: dict[tuple[object, ...], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        key = (
            row["log_file"],
            row["run"],
            row["environment"],
            row["distance_m"],
            row["node"],
            row["plen"],
        )
        groups[key].append(row)

    summaries: list[dict[str, object]] = []
    for key, group_rows in sorted(
        groups.items(),
        key=lambda item: (
            str(item[0][2]),
            str(item[0][3]),
            str(item[0][1]),
            str(item[0][4]),
            int(item[0][5]),
            str(item[0][0]),
        ),
    ):
        snr_values = [float(row["fp_snr_db"]) for row in group_rows if not math.isnan(float(row["fp_snr_db"]))]
        rssi_values = [float(row["rssi_dbm"]) for row in group_rows]
        fp_values = [float(row["fp_dbm"]) for row in group_rows]
        if not snr_values:
            continue

        log_file, run, environment, distance_m, node, plen = key
        n = len(snr_values)
        summaries.append(
            {
                "log_file": log_file,
                "run": run,
                "environment": environment,
                "distance_m": distance_m,
                "node": node,
                "plen": plen,
                "n": n,
                "expected_n": expected_samples,
                "missing_n": max(expected_samples - n, 0),
                "status": run_status(n, expected_samples),
                "fp_snr_db_min": min(snr_values),
                "fp_snr_db_max": max(snr_values),
                "fp_snr_db_mean": statistics.fmean(snr_values),
                "fp_snr_db_median": statistics.median(snr_values),
                "fp_snr_db_stdev": statistics.stdev(snr_values) if len(snr_values) > 1 else 0.0,
                "rssi_dbm_mean": statistics.fmean(rssi_values) if rssi_values else float("nan"),
                "fp_dbm_mean": statistics.fmean(fp_values) if fp_values else float("nan"),
                "processing_gain_db": 10.0 * math.log10(127.0 * int(plen)),
            }
        )

    return summaries


def summary_key(row: dict[str, object]) -> tuple[object, ...]:
    return (row["log_file"], row["run"], row["environment"], row["distance_m"], row["node"], row["plen"])


def summary_sort_key(row: dict[str, object]) -> tuple[object, ...]:
    return (
        str(row["environment"]),
        str(row["distance_m"]),
        str(row["run"]),
        str(row["node"]),
        int(row["plen"]),
        str(row["log_file"]),
    )


def preamble_series_name(row: dict[str, object]) -> str:
    parts = []
    if row["environment"]:
        parts.append(str(row["environment"]))
    if row["distance_m"]:
        parts.append(f"{row['distance_m']}m")
    parts.append(str(row["node"]))
    if row["run"]:
        parts.append(str(row["run"]))
    return " ".join(parts)


def svg_escape(value: object) -> str:
    return str(value).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def make_svg_plot(path: Path, title: str, x_label: str, y_label: str, series: dict[str, list[tuple[float, float]]]) -> None:
    width = 980
    height = 540
    left = 76
    right = 28
    top = 48
    bottom = 72
    plot_w = width - left - right
    plot_h = height - top - bottom
    colors = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd", "#ff7f0e", "#17becf"]

    points = [(x, y) for values in series.values() for x, y in values if not math.isnan(y)]
    if not points:
        path.write_text("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>\n")
        return

    min_x = min(x for x, _ in points)
    max_x = max(x for x, _ in points)
    min_y = min(y for _, y in points)
    max_y = max(y for _, y in points)

    if min_x == max_x:
        min_x -= 1
        max_x += 1
    if min_y == max_y:
        min_y -= 1
        max_y += 1

    y_pad = max(1.0, (max_y - min_y) * 0.08)
    min_y -= y_pad
    max_y += y_pad

    def sx(x: float) -> float:
        return left + ((x - min_x) / (max_x - min_x)) * plot_w

    def sy(y: float) -> float:
        return top + (1.0 - ((y - min_y) / (max_y - min_y))) * plot_h

    lines: list[str] = [
        f"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{width}\" height=\"{height}\" viewBox=\"0 0 {width} {height}\">",
        "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>",
        f"<text x=\"{width / 2:.1f}\" y=\"28\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"20\" font-weight=\"700\">{svg_escape(title)}</text>",
        f"<line x1=\"{left}\" y1=\"{top + plot_h}\" x2=\"{left + plot_w}\" y2=\"{top + plot_h}\" stroke=\"#222\"/>",
        f"<line x1=\"{left}\" y1=\"{top}\" x2=\"{left}\" y2=\"{top + plot_h}\" stroke=\"#222\"/>",
    ]

    for i in range(6):
        frac = i / 5.0
        y_value = min_y + (max_y - min_y) * frac
        y_px = sy(y_value)
        lines.append(f"<line x1=\"{left}\" y1=\"{y_px:.1f}\" x2=\"{left + plot_w}\" y2=\"{y_px:.1f}\" stroke=\"#e8e8e8\"/>")
        lines.append(f"<text x=\"{left - 10}\" y=\"{y_px + 4:.1f}\" text-anchor=\"end\" font-family=\"Arial\" font-size=\"12\">{y_value:.1f}</text>")

    unique_x_values = sorted({x for x, _ in points})
    x_ticks = unique_x_values if len(unique_x_values) <= 8 else [
        min_x + (max_x - min_x) * (i / 5.0) for i in range(6)
    ]
    for x_value in x_ticks:
        x_px = sx(x_value)
        lines.append(f"<line x1=\"{x_px:.1f}\" y1=\"{top + plot_h}\" x2=\"{x_px:.1f}\" y2=\"{top + plot_h + 5}\" stroke=\"#222\"/>")
        lines.append(f"<text x=\"{x_px:.1f}\" y=\"{top + plot_h + 22}\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"12\">{x_value:.0f}</text>")

    for idx, (name, values) in enumerate(series.items()):
        color = colors[idx % len(colors)]
        valid_values = [(sx(x), sy(y)) for x, y in sorted(values) if not math.isnan(y)]
        if not valid_values:
            continue
        path_d = " ".join(("M" if i == 0 else "L") + f"{x:.1f},{y:.1f}" for i, (x, y) in enumerate(valid_values))
        lines.append(f"<path d=\"{path_d}\" fill=\"none\" stroke=\"{color}\" stroke-width=\"2\"/>")
        for x, y in valid_values:
            lines.append(f"<circle cx=\"{x:.1f}\" cy=\"{y:.1f}\" r=\"2.8\" fill=\"{color}\"/>")

    legend_x = left + plot_w - 180
    legend_y = top + 18
    for idx, name in enumerate(series.keys()):
        color = colors[idx % len(colors)]
        y = legend_y + idx * 20
        lines.append(f"<rect x=\"{legend_x}\" y=\"{y - 10}\" width=\"12\" height=\"12\" fill=\"{color}\"/>")
        lines.append(f"<text x=\"{legend_x + 18}\" y=\"{y}\" font-family=\"Arial\" font-size=\"12\">{svg_escape(name)}</text>")

    lines.extend(
        [
            f"<text x=\"{left + plot_w / 2:.1f}\" y=\"{height - 22}\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"14\">{svg_escape(x_label)}</text>",
            f"<text x=\"20\" y=\"{top + plot_h / 2:.1f}\" transform=\"rotate(-90 20 {top + plot_h / 2:.1f})\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"14\">{svg_escape(y_label)}</text>",
            "</svg>",
        ]
    )
    path.write_text("\n".join(lines) + "\n")


def write_plots(outdir: Path, prefix: str, rows: list[dict[str, object]], summaries: list[dict[str, object]]) -> list[Path]:
    plots: list[Path] = []

    frame_series: dict[str, list[tuple[float, float]]] = defaultdict(list)
    for i, row in enumerate(rows, start=1):
        value = float(row["fp_snr_db"])
        if math.isnan(value):
            continue
        name = f"{row['log_file']} {row['node']} {row['plen']}sym"
        frame_series[name].append((float(i), value))

    by_frame = outdir / f"{prefix}_fp_snr_by_frame.svg"
    make_svg_plot(by_frame, "First-path SNR by received frame", "received frame index", "first-path SNR (dB)", frame_series)
    plots.append(by_frame)

    plen_series: dict[str, list[tuple[float, float]]] = defaultdict(list)
    gp_points: dict[float, float] = {}
    for row in sorted(summaries, key=summary_sort_key):
        name = preamble_series_name(row)
        plen = float(row["plen"])
        plen_series[name].append((plen, float(row["fp_snr_db_mean"])))
        gp_points[plen] = float(row["processing_gain_db"])

    combined_series = dict(plen_series)
    if gp_points:
        combined_series["Gp model 10log10(127M)"] = sorted(gp_points.items())

    by_plen = outdir / f"{prefix}_fp_snr_by_preamble.svg"
    make_svg_plot(by_plen, "First-path SNR by preamble length", "preamble length M (symbols)", "first-path SNR / processing gain (dB)", combined_series)
    plots.append(by_plen)

    return plots


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert BRRS CIR logs to CSV and SVG plots.")
    parser.add_argument("logs", nargs="+", type=Path, help="INIT-node terminal log files containing CIR_CSV or CIR_SUMMARY_CSV lines.")
    parser.add_argument("-o", "--outdir", type=Path, default=Path("output/brrs_cir"), help="Output directory.")
    parser.add_argument("--prefix", default="brrs_cir", help="Output filename prefix.")
    parser.add_argument("--run", default="", help="Optional run label.")
    parser.add_argument("--environment", default="", help="Optional environment label.")
    parser.add_argument("--distance-m", default="", help="Optional distance label in meters.")
    parser.add_argument("--expected-samples", type=int, default=1000, help="Expected valid CIR sample count per preamble run (submission default: 1000).")
    args = parser.parse_args()

    rows: list[dict[str, object]] = []
    try:
        for log_path in args.logs:
            log_rows = parse_log(
                log_path, args.run, args.environment, args.distance_m
            )
            validate_raw_log(log_path, log_rows, args.expected_samples)
            raw_summaries = summarize(log_rows, args.expected_samples)
            firmware_summaries = parse_summary_log(
                log_path, args.run, args.environment, args.distance_m,
                args.expected_samples
            )
            validate_firmware_summary(log_path, raw_summaries, firmware_summaries)
            rows.extend(log_rows)
    except ValueError as exc:
        raise SystemExit(f"FAIL: {exc}") from exc

    args.outdir.mkdir(parents=True, exist_ok=True)
    sample_csv = args.outdir / f"{args.prefix}_samples.csv"
    summary_csv = args.outdir / f"{args.prefix}_summary.csv"

    summaries = sorted(summarize(rows, args.expected_samples), key=summary_sort_key)
    write_csv(sample_csv, SAMPLE_FIELDS, rows)
    write_csv(summary_csv, SUMMARY_FIELDS, summaries)
    plots = write_plots(args.outdir, args.prefix, rows, summaries)

    print(f"Wrote {sample_csv}")
    print(f"Wrote {summary_csv}")
    for plot in plots:
        print(f"Wrote {plot}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
