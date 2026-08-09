#!/usr/bin/env python3

import argparse
import csv
import os
import statistics
import tempfile
from collections import defaultdict
from pathlib import Path


FIELDS = (
    "preamble_symbols",
    "sensor_nodes",
    "data_slots",
    "slot_repeats",
    "psdu_bytes",
    "app_payload_bytes",
    "slot_us",
    "guard_us",
    "max_slots",
    "superframes",
    "expected_frames",
    "received_frames",
    "per_ppm",
    "goodput_bps",
    "offered_bps",
    "elapsed_us",
    "delayed_late",
    "wrong_slot_frames",
    "status",
)

LEGACY_FIELDS = tuple(
    field for field in FIELDS if field not in ("data_slots", "slot_repeats")
)

EXTRA_FIELDS = (
    "period_count",
    "period_min_us",
    "period_max_us",
    "period_avg_us",
    "sync_delayed_late",
    "tx_wait_timeout",
    "end_tx_count",
    "sync_prep_budget_us",
    "sync_prep_count",
    "sync_prep_min_us",
    "sync_prep_max_us",
    "sync_prep_avg_us",
    "rearm_count",
    "rearm_service_min_us",
    "rearm_service_max_us",
    "rearm_service_avg_us",
    "rearm_slack_min_us",
    "rearm_slack_max_us",
    "rearm_slack_avg_us",
    "burst_scheduled_end_us",
    "burst_early_close",
    "burst_deadline_close",
    "burst_forced_prep_close",
)


def parse_key_values(record: str):
    values = {}
    for item in record.split(",")[1:]:
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        values[key] = value
    return values


def parse_summary(path: Path):
    summaries = []
    timing = None
    sync_prep = None
    rearm = None
    burst = None
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            marker = line.find("EXP4_SUMMARY_CSV,")
            if marker >= 0:
                values = line[marker:].strip().split(",")[1:]
                if len(values) == len(FIELDS):
                    row = dict(zip(FIELDS, values))
                elif len(values) == len(LEGACY_FIELDS):
                    row = dict(zip(LEGACY_FIELDS, values))
                    row["data_slots"] = row["sensor_nodes"]
                    row["slot_repeats"] = "1"
                else:
                    raise ValueError(
                        f"{path}: EXP4_SUMMARY_CSV has {len(values)} fields; "
                        f"expected {len(FIELDS)} (current) or {len(LEGACY_FIELDS)} (legacy)"
                    )
                for field in FIELDS[:-1]:
                    row[field] = int(row[field])
                row["source_log"] = str(path)
                summaries.append(row)

            marker = line.find("EXP4_TIMING_CSV,")
            if marker >= 0:
                timing = parse_key_values(line[marker:].strip())

            marker = line.find("EXP4_REARM_CSV,")
            if marker >= 0:
                rearm = parse_key_values(line[marker:].strip())

            marker = line.find("EXP4_SYNC_PREP_CSV,")
            if marker >= 0:
                sync_prep = parse_key_values(line[marker:].strip())

            marker = line.find("EXP4_BURST_CSV,")
            if marker >= 0:
                burst = parse_key_values(line[marker:].strip())

    if not summaries:
        raise ValueError(f"{path}: EXP4_SUMMARY_CSV not found")
    if timing is None:
        raise ValueError(f"{path}: EXP4_TIMING_CSV not found; rerun with the fixed-period firmware")
    if rearm is None:
        raise ValueError(f"{path}: EXP4_REARM_CSV not found; rerun with the rearm diagnostics")

    row = summaries[-1]
    row.update({
        "period_count": int(timing["period_count"]),
        "period_min_us": int(timing["min_x1000_us"]) / 1000.0,
        "period_max_us": int(timing["max_x1000_us"]) / 1000.0,
        "period_avg_us": int(timing["avg_x1000_us"]) / 1000.0,
        "sync_delayed_late": int(timing["sync_delayed_late"]),
        "tx_wait_timeout": int(timing.get("tx_wait_timeout", "0")),
        "end_tx_count": int(timing["end_tx"]),
        "sync_prep_budget_us": int(sync_prep["budget_us"]) if sync_prep else 0,
        "sync_prep_count": int(sync_prep["count"]) if sync_prep else 0,
        "sync_prep_min_us": int(sync_prep["min_us"]) if sync_prep else 0,
        "sync_prep_max_us": int(sync_prep["max_us"]) if sync_prep else 0,
        "sync_prep_avg_us": (
            int(sync_prep["avg_x1000_us"]) / 1000.0 if sync_prep else 0
        ),
        "rearm_count": int(rearm["count"]),
        "rearm_service_min_us": int(rearm["service_min_us"]),
        "rearm_service_max_us": int(rearm["service_max_us"]),
        "rearm_service_avg_us": int(rearm["service_avg_x1000_us"]) / 1000.0,
        "rearm_slack_min_us": int(rearm.get("slack_min_us", "0")),
        "rearm_slack_max_us": int(rearm.get("slack_max_us", "0")),
        "rearm_slack_avg_us": int(rearm.get("slack_avg_x1000_us", "0")) / 1000.0,
        "burst_scheduled_end_us": int(burst["scheduled_end_us"]) if burst else 0,
        "burst_early_close": int(burst["early_close"]) if burst else 0,
        "burst_deadline_close": int(burst["deadline_close"]) if burst else 0,
        "burst_forced_prep_close": int(burst["forced_prep_close"]) if burst else 0,
    })

    if row["period_count"] != row["superframes"]:
        raise ValueError(
            f"{path}: measured {row['period_count']} period boundaries; "
            f"expected {row['superframes']}"
        )
    if abs(row["period_avg_us"] - 10000.0) > 5.0:
        raise ValueError(
            f"{path}: average superframe is {row['period_avg_us']:.3f} us, not 10000 us"
        )
    if (row["sync_delayed_late"] != 0 or
            row["tx_wait_timeout"] != 0 or
            row["end_tx_count"] != 3):
        raise ValueError(
            f"{path}: invalid timing completion "
            f"(sync_delayed_late={row['sync_delayed_late']}, "
            f"tx_wait_timeout={row['tx_wait_timeout']}, "
            f"end_tx={row['end_tx_count']})"
        )
    if sync_prep is not None:
        expected_prep_count = max(row["superframes"] - 1, 0)
        if (row["sync_prep_count"] != expected_prep_count or
                row["sync_prep_max_us"] > row["sync_prep_budget_us"] or
                int(sync_prep["delayed_late"]) != 0):
            raise ValueError(
                f"{path}: invalid SYNC preparation "
                f"(count={row['sync_prep_count']}/{expected_prep_count}, "
                f"max={row['sync_prep_max_us']} us, "
                f"budget={row['sync_prep_budget_us']} us, "
                f"delayed_late={sync_prep['delayed_late']})"
            )
    if burst is not None:
        burst_total = int(burst["total"])
        counted_total = (row["burst_early_close"] +
                         row["burst_deadline_close"] +
                         row["burst_forced_prep_close"])
        if (burst_total != row["superframes"] or
                counted_total != burst_total or
                row["burst_forced_prep_close"] != 0):
            raise ValueError(
                f"{path}: invalid DATA-burst completion "
                f"(early={row['burst_early_close']}, "
                f"deadline={row['burst_deadline_close']}, "
                f"forced={row['burst_forced_prep_close']}, "
                f"total={burst_total}, superframes={row['superframes']})"
            )
    return row


def write_csv(rows, output_path: Path):
    columns = ("source_log",) + FIELDS + EXTRA_FIELDS + ("per_percent", "delivery_percent")
    with output_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for row in rows:
            out = dict(row)
            out["per_percent"] = row["per_ppm"] / 10000.0
            out["delivery_percent"] = (
                row["received_frames"] * 100.0 / row["expected_frames"]
                if row["expected_frames"]
                else 0.0
            )
            writer.writerow(out)


def grouped_means(rows, value_field):
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row["preamble_symbols"], row["data_slots"])].append(row[value_field])
    return {
        key: (statistics.mean(values), statistics.stdev(values) if len(values) > 1 else 0.0)
        for key, values in grouped.items()
    }


def validate_topologies(rows):
    topologies = defaultdict(set)
    for row in rows:
        topologies[(row["preamble_symbols"], row["data_slots"])].add(
            (row["sensor_nodes"], row["slot_repeats"])
        )

    mixed = {key: value for key, value in topologies.items() if len(value) > 1}
    if mixed:
        details = "; ".join(
            f"{plen}sym/{slots}slots={sorted(values)}"
            for (plen, slots), values in sorted(mixed.items())
        )
        raise SystemExit(
            "FAIL: physical-node and repeated-slot topologies would be averaged "
            f"together ({details}); analyze them as separate datasets"
        )


def validate_group_parameters(rows):
    groups = defaultdict(set)
    for row in rows:
        key = (
            row["preamble_symbols"], row["data_slots"],
            row["sensor_nodes"], row["slot_repeats"]
        )
        groups[key].add(
            (
                row["psdu_bytes"], row["app_payload_bytes"], row["slot_us"],
                row["guard_us"], row["max_slots"], row["superframes"],
                row["expected_frames"]
            )
        )

    inconsistent = {key: values for key, values in groups.items() if len(values) > 1}
    if inconsistent:
        details = "; ".join(
            f"{key}={sorted(values)}" for key, values in sorted(inconsistent.items())
        )
        raise SystemExit(
            "FAIL: runs grouped as replicates have different firmware/timing "
            f"parameters ({details})"
        )


def validate_dataset_parameters(rows):
    shared = {
        (row["psdu_bytes"], row["app_payload_bytes"], row["superframes"])
        for row in rows
    }
    if len(shared) > 1:
        raise SystemExit(
            "FAIL: logs mix incompatible DATA formats or run lengths "
            f"(psdu_bytes, app_payload_bytes, superframes={sorted(shared)})"
        )

def write_aggregate_csv(rows, output_path: Path):
    groups = defaultdict(list)
    for row in rows:
        groups[(row["preamble_symbols"], row["data_slots"],
                row["sensor_nodes"], row["slot_repeats"])].append(row)

    columns = (
        "preamble_symbols",
        "sensor_nodes",
        "data_slots",
        "slot_repeats",
        "runs",
        "slot_us",
        "guard_us",
        "max_slots",
        "per_mean_percent",
        "per_std_percent",
        "goodput_mean_bps",
        "goodput_std_bps",
        "offered_mean_bps",
    )
    with output_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for key in sorted(groups):
            group = groups[key]
            per_values = [row["per_ppm"] / 10000.0 for row in group]
            goodput_values = [row["goodput_bps"] for row in group]
            writer.writerow({
                "preamble_symbols": key[0],
                "data_slots": key[1],
                "sensor_nodes": key[2],
                "slot_repeats": key[3],
                "runs": len(group),
                "slot_us": group[0]["slot_us"],
                "guard_us": group[0]["guard_us"],
                "max_slots": group[0]["max_slots"],
                "per_mean_percent": statistics.mean(per_values),
                "per_std_percent": statistics.stdev(per_values) if len(group) > 1 else 0.0,
                "goodput_mean_bps": statistics.mean(goodput_values),
                "goodput_std_bps": statistics.stdev(goodput_values) if len(group) > 1 else 0.0,
                "offered_mean_bps": statistics.mean(row["offered_bps"] for row in group),
            })


def make_plots(rows, output_dir: Path, prefix: str):
    try:
        matplotlib_cache = Path(tempfile.gettempdir()) / "brrs-matplotlib"
        matplotlib_cache.mkdir(parents=True, exist_ok=True)
        os.environ.setdefault("MPLCONFIGDIR", str(matplotlib_cache))
        os.environ.setdefault("XDG_CACHE_HOME", str(matplotlib_cache))
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit("matplotlib is required to create Exp4 plots") from exc

    colors = {32: "#2678b8", 64: "#2f9e73", 128: "#d98c20", 256: "#c94b4b"}
    preambles = sorted({row["preamble_symbols"] for row in rows})

    goodput = grouped_means(rows, "goodput_bps")
    offered = grouped_means(rows, "offered_bps")
    fig, ax = plt.subplots(figsize=(9.2, 5.4))
    for plen in preambles:
        data_slots = sorted({n for p, n in goodput if p == plen})
        measured = [goodput[(plen, n)][0] / 1000.0 for n in data_slots]
        measured_std = [goodput[(plen, n)][1] / 1000.0 for n in data_slots]
        offered_values = [offered[(plen, n)][0] / 1000.0 for n in data_slots]
        ax.errorbar(data_slots, measured, yerr=measured_std, marker="o", linewidth=2.2,
                    capsize=4, color=colors.get(plen), label=f"{plen} sym measured")
        ax.plot(data_slots, offered_values, linestyle="--", linewidth=1.4,
                color=colors.get(plen), alpha=0.7, label=f"{plen} sym offered")
    ax.set_xlabel("Scheduled DATA slots per 10 ms superframe")
    ax.set_ylabel("Application throughput (kbps)")
    ax.set_title("Experiment 4: aggregate application throughput")
    ax.grid(True, alpha=0.25)
    ax.legend(ncol=2)
    fig.tight_layout()
    fig.savefig(output_dir / f"{prefix}_goodput.png", dpi=200)
    plt.close(fig)

    per = grouped_means(rows, "per_ppm")
    fig, ax = plt.subplots(figsize=(9.2, 5.4))
    for plen in preambles:
        data_slots = sorted({n for p, n in per if p == plen})
        values = [per[(plen, n)][0] / 10000.0 for n in data_slots]
        std_values = [per[(plen, n)][1] / 10000.0 for n in data_slots]
        ax.errorbar(data_slots, values, yerr=std_values, marker="o", linewidth=2.2,
                    capsize=4, color=colors.get(plen), label=f"{plen} sym")
    ax.set_xlabel("Scheduled DATA slots per 10 ms superframe")
    ax.set_ylabel("Aggregate PER (%)")
    ax.set_title("Experiment 4: aggregate packet error rate")
    ax.grid(True, alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(output_dir / f"{prefix}_per.png", dpi=200)
    plt.close(fig)

    capacity = {}
    slot_us = {}
    for row in rows:
        capacity[row["preamble_symbols"]] = row["max_slots"]
        slot_us[row["preamble_symbols"]] = row["slot_us"]
    fig, ax = plt.subplots(figsize=(8.4, 5.2))
    x = list(range(len(preambles)))
    bars = ax.bar(x, [capacity[p] for p in preambles],
                  color=[colors.get(p) for p in preambles], width=0.62)
    ax.set_xticks(x, [str(p) for p in preambles])
    ax.set_xlabel("Data preamble (symbols)")
    ax.set_ylabel("Calculated sensor-slot capacity")
    ax.set_title("10 ms superframe capacity from implemented slot timing")
    ax.set_ylim(0, max(capacity.values()) * 1.2)
    ax.grid(axis="y", alpha=0.25)
    for bar, plen in zip(bars, preambles):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.5,
                f"{capacity[plen]} slots\n({slot_us[plen]} us/slot)",
                ha="center", va="bottom", fontsize=10)
    fig.tight_layout()
    fig.savefig(output_dir / f"{prefix}_capacity.png", dpi=200)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(
        description="Parse Experiment 4 coordinator logs and create CSV/plots."
    )
    parser.add_argument("logs", nargs="+", type=Path, help="INIT RTT log files")
    parser.add_argument("-o", "--output-dir", type=Path, default=Path("exp4_result"))
    parser.add_argument("--prefix", default="exp4")
    args = parser.parse_args()

    rows = [parse_summary(path) for path in args.logs]
    rows.sort(key=lambda row: (row["preamble_symbols"], row["data_slots"],
                               row["sensor_nodes"], row["source_log"]))

    guards = {row["guard_us"] for row in rows}
    if len(guards) > 1:
        values = ", ".join(str(value) for value in sorted(guards))
        raise SystemExit(
            f"FAIL: logs contain multiple guard values ({values} us); "
            "analyze each guard separately"
        )

    invalid = [row for row in rows if row["status"] != "PASS"]
    if invalid:
        sources = ", ".join(row["source_log"] for row in invalid)
        raise SystemExit(f"FAIL: incomplete Exp4 collection in {sources}")

    validate_topologies(rows)
    validate_group_parameters(rows)
    validate_dataset_parameters(rows)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    csv_path = args.output_dir / f"{args.prefix}_summary.csv"
    aggregate_csv_path = args.output_dir / f"{args.prefix}_aggregate.csv"
    write_csv(rows, csv_path)
    write_aggregate_csv(rows, aggregate_csv_path)
    make_plots(rows, args.output_dir, args.prefix)

    print(f"Parsed {len(rows)} Exp4 run(s)")
    print(f"CSV: {csv_path}")
    print(f"Aggregate CSV: {aggregate_csv_path}")
    print(f"Plots: {args.output_dir}/{args.prefix}_*.png")


if __name__ == "__main__":
    main()
