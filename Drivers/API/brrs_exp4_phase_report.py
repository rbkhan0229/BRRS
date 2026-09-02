#!/usr/bin/env python3
"""Extract Exp4 wait-phase records from preserved coordinator/sensor RTT logs."""

import argparse
import csv
import re
from collections import defaultdict
from pathlib import Path

from brrs_exp4_wait_report import (
    COORDINATOR_PHASES,
    SENSOR_PHASES,
    infer_experiment,
    integer,
    key_values,
    last_record,
    phase_records,
)


COLUMNS = (
    "experiment",
    "run",
    "role",
    "node",
    "source_log",
    "sync_buffer_us",
    "sync_prep_us",
    "guard_us",
    "pac",
    "phase",
    "count",
    "min_us",
    "max_us",
    "avg_us",
    "first_slot_prep_max_us",
    "phase_max_share_percent",
    "result",
)


def read_lines(path):
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def run_name(path):
    match = re.search(r"_(r\d+)_", path.name)
    return match.group(1) if match else "unknown"


def matching_init_path(sensor_path):
    name = re.sub(r"_n[2-8]\.log$", "_init.log", sensor_path.name)
    return sensor_path.with_name(name)


def context_from_init(path):
    if not path.exists():
        return {}
    return last_record(read_lines(path), "EXP4_CONFIG_CSV,")


def result_for_sensor(lines):
    done = last_record(lines, "EXP4_TX_DONE,")
    return done.get("status", "INVALID")


def result_for_coordinator(lines):
    done = last_record(lines, "EXP4_DONE,")
    status = last_record(lines, "EXP4_STATUS_CSV,")
    if done.get("status") == "PASS" and status.get("collection") == "PASS":
        return "PASS"
    if done and status:
        return "FAIL"
    return "INVALID"


def phase_row(path, role, node, config, phase, record, first_max, result):
    phase_max = integer(record, "max_us")
    share = round(100.0 * phase_max / first_max, 3) if first_max else 0.0
    return {
        "experiment": infer_experiment(path),
        "run": run_name(path),
        "role": role,
        "node": node,
        "source_log": str(path.resolve()),
        "sync_buffer_us": integer(config, "sync_buffer_us"),
        "sync_prep_us": integer(config, "sync_prep_us"),
        "guard_us": integer(config, "guard_us"),
        "pac": integer(config, "data_pac"),
        "phase": phase,
        "count": integer(record, "count"),
        "min_us": integer(record, "min_us"),
        "max_us": phase_max,
        "avg_us": integer(record, "avg_x1000_us") / 1000.0,
        "first_slot_prep_max_us": first_max,
        "phase_max_share_percent": share,
        "result": result,
    }


def collect_rows(log_root):
    sensor_rows = []
    coordinator_rows = []
    for path in sorted(log_root.rglob("*.log")):
        lines = read_lines(path)
        sensor = phase_records(lines, "EXP4_TX_WAIT_PHASE_CSV,")
        coordinator = phase_records(lines, "EXP4_WAIT_PHASE_CSV,")
        if sensor:
            config = context_from_init(matching_init_path(path))
            boot = last_record(lines, "EXP4_TX_BOOT_CSV,")
            if not config:
                config = {
                    "sync_buffer_us": boot.get("sync_buffer_us", 0),
                    "sync_prep_us": boot.get("sync_prep_us", 0),
                }
            first = last_record(lines, "EXP4_TX_FIRST_ARM_CSV,")
            first_max = integer(first, "max_us")
            node = next(iter(sensor.values())).get("node", "unknown")
            result = result_for_sensor(lines)
            for phase in SENSOR_PHASES:
                if phase in sensor:
                    sensor_rows.append(
                        phase_row(
                            path, "sensor", node, config, phase,
                            sensor[phase], first_max, result
                        )
                    )
        if coordinator:
            config = last_record(lines, "EXP4_CONFIG_CSV,")
            first = last_record(lines, "EXP4_FIRST_RX_ARM_CSV,")
            first_max = integer(first, "max_us")
            result = result_for_coordinator(lines)
            for phase in COORDINATOR_PHASES:
                if phase in coordinator:
                    coordinator_rows.append(
                        phase_row(
                            path, "coordinator", "INIT", config, phase,
                            coordinator[phase], first_max, result
                        )
                    )
    return sensor_rows + coordinator_rows


def aggregate(rows):
    count = sum(row["count"] for row in rows)
    if not count:
        return {"count": 0, "min_us": 0, "max_us": 0, "avg_us": 0.0}
    return {
        "count": count,
        "min_us": min(row["min_us"] for row in rows if row["count"]),
        "max_us": max(row["max_us"] for row in rows if row["count"]),
        "avg_us": sum(row["avg_us"] * row["count"] for row in rows) / count,
    }


def write_csv(rows, path):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def write_phase_summary(lines, title, rows, phases):
    lines.extend([
        f"## {title}",
        "",
        "| phase | count | min (us) | max (us) | weighted avg (us) |",
        "|---|---:|---:|---:|---:|",
    ])
    for phase in phases:
        stats = aggregate([row for row in rows if row["phase"] == phase])
        lines.append(
            f"| {phase} | {stats['count']} | {stats['min_us']} | "
            f"{stats['max_us']} | {stats['avg_us']:.3f} |"
        )
    lines.append("")


def write_markdown(rows, path):
    sensor_rows = [row for row in rows if row["role"] == "sensor"]
    coordinator_rows = [row for row in rows if row["role"] == "coordinator"]
    sensor_logs = {row["source_log"] for row in sensor_rows}
    coordinator_logs = {row["source_log"] for row in coordinator_rows}
    sensor_complete = all(
        sum(1 for row in sensor_rows if row["source_log"] == source) ==
        len(SENSOR_PHASES)
        for source in sensor_logs
    )
    coordinator_complete = all(
        sum(1 for row in coordinator_rows if row["source_log"] == source) ==
        len(COORDINATOR_PHASES)
        for source in coordinator_logs
    )
    sensor_config = [
        row for row in sensor_rows if row["phase"] == "sensor_data_phy_config"
    ]
    coordinator_config = [
        row for row in coordinator_rows
        if row["phase"] == "coordinator_data_phy_config"
    ]
    lines = [
        "# Exp4 preserved wait-phase log inventory (2026-09-02)",
        "",
        "## Coverage",
        "",
        f"- Sensor logs with phase records: {len(sensor_logs)}.",
        f"- Coordinator logs with equivalent phase records: {len(coordinator_logs)}.",
        f"- Five sensor phases complete in every sensor log: {sensor_complete}.",
        f"- Four coordinator phases complete in every coordinator log: "
        f"{coordinator_complete}.",
        f"- Long-form CSV rows: {len(rows)}.",
        "",
    ]
    write_phase_summary(lines, "Sensor phase aggregate", sensor_rows, SENSOR_PHASES)
    write_phase_summary(
        lines, "Coordinator phase aggregate", coordinator_rows, COORDINATOR_PHASES
    )
    sensor_share = [row["phase_max_share_percent"] for row in sensor_config]
    coordinator_share = [row["phase_max_share_percent"] for row in coordinator_config]
    lines.extend([
        "## PHY configuration share of first-slot preparation",
        "",
        f"- Sensor DATA PHY configuration: max "
        f"{max(row['max_us'] for row in sensor_config)} us; per-log share "
        f"{min(sensor_share):.1f}% to {max(sensor_share):.1f}%.",
        f"- Coordinator DATA PHY configuration: max "
        f"{max(row['max_us'] for row in coordinator_config)} us; per-log share "
        f"{min(coordinator_share):.1f}% to {max(coordinator_share):.1f}%.",
        "- Coordinator equivalent phase instrumentation is present; it was not "
        "missing from the preserved INIT logs.",
        "",
        "## Sensor log-by-log DATA PHY phase",
        "",
        "| experiment | run | node | config max (us) | first arm max (us) | share (%) | result |",
        "|---|---|---:|---:|---:|---:|:---:|",
    ])
    for row in sensor_config:
        lines.append(
            f"| {row['experiment']} | {row['run']} | {row['node']} | "
            f"{row['max_us']} | {row['first_slot_prep_max_us']} | "
            f"{row['phase_max_share_percent']:.1f} | {row['result']} |"
        )
    lines.extend([
        "",
        "The companion long-form CSV contains count/min/max/average for every "
        "available phase in every preserved sensor and coordinator log.",
    ])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log-root", required=True, type=Path)
    parser.add_argument("--csv", required=True, type=Path)
    parser.add_argument("--markdown", required=True, type=Path)
    args = parser.parse_args()
    rows = collect_rows(args.log_root.resolve())
    if not rows:
        raise SystemExit("no EXP4 wait-phase records found")
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    write_csv(rows, args.csv)
    write_markdown(rows, args.markdown)
    counts = defaultdict(int)
    for row in rows:
        counts[row["role"]] += 1
    print(f"rows={len(rows)} sensor={counts['sensor']} coordinator={counts['coordinator']}")
    print(f"csv={args.csv}")
    print(f"markdown={args.markdown}")


if __name__ == "__main__":
    main()
