#!/usr/bin/env python3
"""Extract Exp4 dwt_configure profile records from validated RTT logs."""

import argparse
import csv
from pathlib import Path


PHASE_ORDER = {
    "temp_vdddig": 0,
    "register_setup": 1,
    "setchannel": 2,
    "dgc_rx_tuning": 3,
    "pgf_cal": 4,
    "total": 5,
}


def key_values(line):
    values = {}
    for field in line.split(",")[1:]:
        if "=" in field:
            key, value = field.split("=", 1)
            values[key] = value
    return values


def log_status(lines):
    for prefix in ("EXP4_DONE,", "EXP4_TX_DONE,"):
        matches = [key_values(line) for line in lines if line.startswith(prefix)]
        if matches:
            return matches[-1].get("status", "UNKNOWN")
    return "MISSING"


def extract(log):
    lines = log.read_text(errors="replace").splitlines()
    status = log_status(lines)
    states = {}
    rows = []

    for line in lines:
        if line.startswith("EXP4_PHY_CONFIG_STATE_CSV,"):
            values = key_values(line)
            key = (values["role"], values.get("node", "-"), values["target"])
            states[key] = values

    for line in lines:
        if not line.startswith("EXP4_PHY_CONFIG_PROFILE_CSV,"):
            continue
        values = key_values(line)
        key = (values["role"], values.get("node", "-"), values["target"])
        state = states.get(key, {})
        rows.append({
            "log": log.name,
            "validation_status": status,
            "role": key[0],
            "node": key[1],
            "target": key[2],
            "state_samples": state.get("samples", ""),
            "idle": state.get("idle", ""),
            "not_idle": state.get("not_idle", ""),
            "phase": values["phase"],
            "count": values["count"],
            "min_us": values["min_us"],
            "max_us": values["max_us"],
            "avg_x1000_us": values["avg_x1000_us"],
        })
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--csv", required=True, type=Path)
    args = parser.parse_args()

    rows = []
    for log in args.logs:
        rows.extend(extract(log))
    if not rows:
        parser.error("no EXP4 PHY configure profile records found")

    rows.sort(key=lambda row: (
        row["role"], row["node"], row["target"],
        PHASE_ORDER.get(row["phase"], 99), row["log"]))
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    statuses = sorted({row["validation_status"] for row in rows})
    print(f"wrote {len(rows)} rows from {len(args.logs)} logs to {args.csv}")
    print(f"validation statuses: {','.join(statuses)}")


if __name__ == "__main__":
    main()
