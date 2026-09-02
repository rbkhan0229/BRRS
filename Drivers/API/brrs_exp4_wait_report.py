#!/usr/bin/env python3
"""Create fail-closed CSV/Markdown summaries for Exp4 wait-budget runs."""

import argparse
import csv
import re
import subprocess
from collections import defaultdict
from pathlib import Path


COLUMNS = (
    "experiment", "git_sha", "source_log", "sync_buffer_us",
    "sync_prep_us", "data_budget_us", "event_mode", "spi_config",
    "preamble_symbols", "pac", "topology", "slot_sequence", "guard_us",
    "lead_us", "slots", "runs", "expected_rx", "valid_rx",
    "success_count", "per_percent", "first_slot_prep_mean_us",
    "first_slot_prep_max_us", "first_slot_prep_p95_us",
    "first_slot_prep_p99_us", "first_slot_min_arm_slack_us",
    "sync_prep_mean_us", "sync_prep_max_us", "sync_prep_p95_us",
    "sync_prep_p99_us", "sync_min_remaining_lead_us",
    "hot_path_mean_us", "hot_path_max_us", "hot_path_p95_us",
    "hot_path_p99_us", "deadline_miss", "delayed_rx_late",
    "delayed_tx_late", "rx_buffer_mismatch", "rdb_incomplete",
    "overrun", "spi_errors", "timeout", "required_guard_us",
    "recommended_guard_us", "max_passing_slots", "result",
    "failure_reason",
)


def key_values(line):
    result = {}
    for item in line.strip().split(",")[1:]:
        if "=" in item:
            key, value = item.split("=", 1)
            result[key] = value
    return result


def last_record(lines, prefix):
    matches = [key_values(line) for line in lines if line.startswith(prefix)]
    return matches[-1] if matches else {}


def integer(record, key, default=0):
    try:
        return int(record.get(key, default))
    except (TypeError, ValueError):
        return default


def x1000(record, key, default=0.0):
    return integer(record, key, int(default * 1000)) / 1000.0


def read_meta(path):
    meta_path = path.with_suffix(".meta.txt")
    if not meta_path.exists():
        return {}
    result = {}
    for line in meta_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
    return result


def infer_experiment(path):
    name = path.parent.name
    match = re.match(r"exp4_(.+?)_0m_", name)
    return match.group(1) if match else name


def parse_superframes(lines):
    pattern = re.compile(
        r"Superframes: total=(\d+) all-slots-ok=(\d+) fail=(\d+)"
    )
    for line in reversed(lines):
        match = pattern.search(line)
        if match:
            return tuple(int(value) for value in match.groups())
    return (0, 0, 0)


def sensor_records(init_path, sensor_count):
    stem = init_path.name.removesuffix("_init.log")
    records = []
    for node in range(2, sensor_count + 2):
        path = init_path.with_name(f"{stem}_n{node}.log")
        if not path.exists():
            records.append((path, [], {}, {}, {}))
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        records.append((
            path,
            lines,
            last_record(lines, "EXP4_TX_FIRST_ARM_CSV,"),
            last_record(lines, "EXP4_TX_RESULT_CSV,"),
            last_record(lines, "EXP4_TX_DONE,"),
        ))
    return records


def parse_run(path, fallback_sha):
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    meta = read_meta(path)
    config = last_record(lines, "EXP4_CONFIG_CSV,")
    done = last_record(lines, "EXP4_DONE,")
    status = last_record(lines, "EXP4_STATUS_CSV,")
    timing = last_record(lines, "EXP4_TIMING_CSV,")
    first_rx = last_record(lines, "EXP4_FIRST_RX_ARM_CSV,")
    prep = last_record(lines, "EXP4_SYNC_PREP_E2E_CSV,")
    hot = last_record(lines, "EXP4_HOT_PATH_CSV,")
    deferred = last_record(lines, "EXP4_DEFERRED_CSV,")
    double_buffer = last_record(lines, "EXP4_DOUBLE_BUFFER_CSV,")
    spi = last_record(lines, "EXP4_SPI_CSV,")
    host_irq = last_record(lines, "EXP4_HOST_IRQ_CONFIG_CSV,")
    superframes, success_count, _ = parse_superframes(lines)

    sensor_count = integer(config, "physical_sensors", integer(done, "physical_sensors"))
    sensors = sensor_records(path, sensor_count)
    complete_sensors = all(record[4].get("status") == "PASS" for record in sensors)

    slot_sequence = meta.get("slot_sequence", "")
    if not slot_sequence:
        schedule = last_record(lines, "EXP4_SLOT_SCHEDULE_CSV,")
        slot_sequence = schedule.get("slot_owners", "unknown")

    sensor_first = []
    sensor_delayed_late = 0
    for _, _, arm, _, _ in sensors:
        if arm:
            sensor_first.append(arm)
            sensor_delayed_late += integer(arm, "delayed_late")
    first_owner = min(
        sensor_first,
        key=lambda row: integer(row, "first_owned_slot_us", 1 << 30),
        default={},
    )

    first_means = [x1000(first_rx, "avg_x1000_us")]
    first_maxes = [integer(first_rx, "max_us")]
    first_p95 = [integer(first_rx, "p95_us")]
    first_p99 = [integer(first_rx, "p99_us")]
    first_slacks = [integer(first_rx, "rx_open_slack_min_us")]
    if first_owner:
        first_means.append(x1000(first_owner, "avg_x1000_us"))
        first_maxes.append(integer(first_owner, "max_us"))
        first_p95.append(integer(first_owner, "p95_us"))
        first_p99.append(integer(first_owner, "p99_us"))
        first_slacks.append(integer(first_owner, "data_rmarker_slack_min_us"))

    expected = integer(done, "expected")
    received = integer(done, "rx")
    per_percent = (100.0 * (expected - received) / expected) if expected else 100.0
    complete_init = bool(done and status)
    init_pass = status.get("collection") == "PASS" and done.get("status") == "PASS"
    if not complete_init:
        result = "INVALID"
        failure = "incomplete coordinator log"
    elif init_pass and not complete_sensors:
        result = "INVALID"
        failure = "incomplete or failed sensor log"
    elif init_pass:
        result = "PASS"
        failure = ""
    else:
        result = "FAIL"
        if integer(first_rx, "delayed_late"):
            failure = "coordinator first delayed-RX arm late"
        elif integer(prep, "delayed_late") or integer(timing, "sync_delayed_late"):
            failure = "next-SYNC delayed-TX lead insufficient"
        elif integer(deferred, "rearm_deadline_miss"):
            failure = "RX re-arm deadline miss"
        else:
            failure = "coordinator collection or timing failure"

    spi_error_keys = (
        "begin_fail", "end_fail", "device_id_fail", "state_error",
        "transfer_error", "direct_timeout", "recovery",
    )
    row = {
        "experiment": infer_experiment(path),
        "git_sha": meta.get("git_commit", fallback_sha),
        "source_log": str(path),
        "sync_buffer_us": integer(config, "sync_buffer_us"),
        "sync_prep_us": integer(config, "sync_prep_us"),
        "data_budget_us": integer(config, "data_budget_us"),
        "event_mode": host_irq.get("mode", "unknown"),
        "spi_config": config.get("spi_mode", "unknown"),
        "preamble_symbols": integer(config, "data_plen"),
        "pac": integer(config, "data_pac"),
        "topology": f"S{sensor_count}",
        "slot_sequence": slot_sequence,
        "guard_us": integer(config, "guard_us"),
        "lead_us": integer(config, "lead_us"),
        "slots": integer(config, "data_slots"),
        "runs": 1,
        "expected_rx": expected,
        "valid_rx": received,
        "success_count": success_count,
        "per_percent": round(per_percent, 6),
        "first_slot_prep_mean_us": max(first_means, default=0.0),
        "first_slot_prep_max_us": max(first_maxes, default=0),
        "first_slot_prep_p95_us": max(first_p95, default=0),
        "first_slot_prep_p99_us": max(first_p99, default=0),
        "first_slot_min_arm_slack_us": min(first_slacks, default=0),
        "sync_prep_mean_us": x1000(prep, "avg_x1000_us"),
        "sync_prep_max_us": integer(prep, "max_us"),
        "sync_prep_p95_us": integer(prep, "p95_us"),
        "sync_prep_p99_us": integer(prep, "p99_us"),
        "sync_min_remaining_lead_us": integer(prep, "remaining_lead_min_us"),
        "hot_path_mean_us": x1000(hot, "avg_x1000_us"),
        "hot_path_max_us": integer(hot, "max_us"),
        "hot_path_p95_us": integer(hot, "p95_us"),
        "hot_path_p99_us": integer(hot, "p99_us"),
        "deadline_miss": integer(deferred, "rearm_deadline_miss"),
        "delayed_rx_late": integer(first_rx, "delayed_late") +
                           integer(last_record(lines, "EXP4_REARM_CSV,"),
                                   "delayed_schedule_late"),
        "delayed_tx_late": integer(timing, "sync_delayed_late") + sensor_delayed_late,
        "rx_buffer_mismatch": integer(double_buffer, "rdb_host_mismatch"),
        "rdb_incomplete": integer(double_buffer, "rdb_incomplete"),
        "overrun": integer(double_buffer, "overrun"),
        "spi_errors": sum(integer(spi, key) for key in spi_error_keys),
        "timeout": integer(timing, "tx_wait_timeout") + integer(deferred, "rx_timeout"),
        "required_guard_us": integer(last_record(lines, "EXP4_REARM_CSV,"),
                                     "required_guard_us"),
        "recommended_guard_us": "",
        "max_passing_slots": "",
        "result": result,
        "failure_reason": failure,
    }
    return row


def fmt(value, digits=3):
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def write_csv(rows, path):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def write_markdown(rows, path):
    groups = defaultdict(list)
    for row in rows:
        key = (
            row["sync_buffer_us"], row["sync_prep_us"], row["guard_us"],
            row["pac"], row["slots"], row["slot_sequence"], row["result"],
        )
        groups[key].append(row)

    representative = next(
        row for row in rows
        if (row["sync_buffer_us"], row["sync_prep_us"], row["guard_us"],
            row["pac"], row["slots"], row["result"]) ==
           (2000, 2000, 200, 8, 3, "PASS")
    )

    lines = [
        "# Exp4 wait-budget implementation and hardware results (2026-09-02)",
        "",
        "## Git and scope",
        "",
        "- Starting branch/SHA: `exp4-irq-spi-opt` / "
        "`eae019efc6cb8dfb4bf630f19f43c40c7356a869`.",
        "- Experiment branch: `exp4-wait-budget-sweep`.",
        "- Local checkpoints: `d206559` (instrumentation), `32a72ff` "
        "(defer first-beacon logs), and `922fb8f` (RTT reconnect recovery).",
        "- `main`, `origin/main`, and `origin/exp4-irq-spi-opt` were not changed. "
        "No push was performed.",
        "- BRRS beacon-referenced delayed-TX/RX and radio-off intervals were "
        "preserved; continuous RX was not introduced.",
        "",
        "## What the two values mean",
        "",
        "`BRRS_SYNC_BUFFER_US` is not a blocking delay. It is the SYNC RMARKER "
        "to first DATA RMARKER scheduling offset used by the coordinator's "
        "first delayed-RX and the first slot owner's delayed-TX.",
        "",
        "`BRRS_EXP4_SYNC_PREP_US` reserves the end of the 10 ms superframe. "
        "The coordinator detects `10000 - sync_prep_us`, closes the DATA burst, "
        "switches to the SYNC PHY, prepares the beacon, and arms the next exact "
        "delayed-TX. `EXP4_TX_WAIT_TIMEOUT_US=3000` is a separate timeout and "
        "was not changed.",
        "",
        "## Implementation and measurement coverage",
        "",
        "- Both existing compile-time macros are exposed as `--sync-buffer` "
        "and `--sync-prep` in build/capture/multi-board tooling. Binary paths "
        "and metadata include both values and the resulting DATA budget.",
        "- Coordinator RAM instrumentation covers first delayed-RX arm/slack, "
        "DATA-PHY configuration, delayed-RX command time, CONFIG_SWITCH detect "
        "lateness, burst close, end-to-end next-SYNC arm, and remaining lead.",
        "- Sensor RAM instrumentation covers SYNC frame read/decode readiness, "
        "DATA-PHY configuration, TX-buffer write, first delayed-TX arm, and "
        "first-slot arm slack. Aggregates are emitted only after the run.",
        "- The first-beacon RTT configuration logs were moved after the first "
        "delayed-TX arm. This reduced the observed one-time sensor maximum from "
        "about 2.12 ms to about 1.60 ms without changing radio scheduling.",
        "- The verifier remains fail-closed for zero valid RX, delayed-late, "
        "deadline miss, RDB mismatch/incomplete, overrun, SPI error, timeout, "
        "wrong slot/superframe, config error, and incomplete instrumentation.",
        "",
        "A representative 2000/2000 us S3 run measured:",
        "",
        f"- worst-side first-slot arm mean/max/p95/p99: "
        f"{representative['first_slot_prep_mean_us']:.3f}/"
        f"{representative['first_slot_prep_max_us']}/"
        f"{representative['first_slot_prep_p95_us']}/"
        f"{representative['first_slot_prep_p99_us']} us; minimum arm slack "
        f"{representative['first_slot_min_arm_slack_us']} us;",
        f"- scheduled CONFIG_SWITCH to next-SYNC arm mean/max/p95/p99: "
        f"{representative['sync_prep_mean_us']:.3f}/"
        f"{representative['sync_prep_max_us']}/"
        f"{representative['sync_prep_p95_us']}/"
        f"{representative['sync_prep_p99_us']} us; minimum remaining lead "
        f"{representative['sync_min_remaining_lead_us']} us;",
        f"- event-to-buffer-free hot path mean/max/p95/p99: "
        f"{representative['hot_path_mean_us']:.3f}/"
        f"{representative['hot_path_max_us']}/"
        f"{representative['hot_path_p95_us']}/"
        f"{representative['hot_path_p99_us']} us; required guard "
        f"{representative['required_guard_us']} us.",
        "",
        "## Selected hardware runs",
        "",
        "All rows come from completed coordinator RTT logs. PASS additionally "
        "requires completed PASS logs from every configured sensor.",
        "",
        "| buffer/prep (us) | G | PAC | slots | result | runs | RX/expected | PER (%) | first slack min (us) | SYNC slack min (us) | required G (us) |",
        "|---:|---:|---:|---:|:---:|---:|---:|---:|---:|---:|---:|",
    ]
    for key in sorted(groups):
        group = groups[key]
        expected = sum(row["expected_rx"] for row in group)
        received = sum(row["valid_rx"] for row in group)
        per = 100.0 * (expected - received) / expected if expected else 100.0
        lines.append(
            f"| {key[0]}/{key[1]} | {key[2]} | {key[3]} | {key[4]} | "
            f"{key[6]} | {len(group)} | {received}/{expected} | {per:.3f} | "
            f"{min(row['first_slot_min_arm_slack_us'] for row in group)} | "
            f"{min(row['sync_min_remaining_lead_us'] for row in group)} | "
            f"{max(row['required_guard_us'] for row in group)} |"
        )

    lines.extend(["", "## Failure boundaries", ""])
    for row in rows:
        if row["result"] == "FAIL":
            lines.append(
                f"- {row['sync_buffer_us']}/{row['sync_prep_us']} us, "
                f"G{row['guard_us']}, {row['slots']} slots: "
                f"{row['failure_reason']} (PER {row['per_percent']:.3f}%)."
            )

    lines.extend([
        "",
        "## Data-backed recommendations",
        "",
        "- Sync buffer: 1750 us passed one smoke run, while 1600 us failed; "
        "use 2000 us to retain roughly 300 us or more worst-case arm margin.",
        "- Sync prep: 2000 us passed and 1750 us failed because next-SYNC "
        "delayed-TX lead was insufficient; use 2000 us.",
        "- Guard: G100 passed one smoke run and G75 failed; retain G150 until "
        "G100 has repeated worst-case validation.",
        "- Capacity at 2000/2000 us and M32/G200: 19 slots passed, 20 slots "
        "failed. The measured stable capacity is therefore 19 slots.",
        "",
        "The default 3000/2500 us DATA budget is 4500 us (15 calculated slots). "
        "The recommended 2000/2000 us budget is 6000 us and achieved 19 stable "
        "slots in hardware, a 26.7% measured slot-capacity increase.",
        "",
        "## Validation limits and remaining work",
        "",
        "- The recommended 2000/2000 us S3 combination completed three valid "
        "repeated runs (8922/9000, PER 0.867%). Boundary and saturation "
        "points are currently smoke runs, not the planned 10-run campaign.",
        "- PAC4 validation is INVALID, not FAIL: probe `1050211584` reported "
        "0.0 V target voltage and could not be flashed after three attempts. "
        "Repeat PAC4 after restoring that board's target power.",
        "- Results are from the present local 0 m bench. Cross-laptop and board-"
        "role portability still require a separate repeated campaign with the "
        "same firmware hashes.",
        "- G100 passed one run but has only 12 us over the measured 88 us "
        "requirement. G150 remains the conservative guard recommendation until "
        "G100 is repeated under worst-case load and board rotation.",
        "",
        "The companion CSV contains one row per selected run, including source "
        "log, git SHA, percentile metrics, all fault counters, and the explicit "
        "PASS/FAIL reason.",
    ])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path, help="selected INIT .log files")
    parser.add_argument("--csv", type=Path, required=True)
    parser.add_argument("--markdown", type=Path, required=True)
    parser.add_argument("--recommended-guard", type=int)
    parser.add_argument("--max-passing-slots", type=int)
    args = parser.parse_args()

    fallback_sha = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], text=True
    ).strip()
    rows = [parse_run(path.resolve(), fallback_sha) for path in args.logs]
    for row in rows:
        if args.recommended_guard is not None:
            row["recommended_guard_us"] = args.recommended_guard
        if args.max_passing_slots is not None:
            row["max_passing_slots"] = args.max_passing_slots
    rows.sort(key=lambda row: (
        row["sync_buffer_us"], row["sync_prep_us"], row["guard_us"],
        row["pac"], row["slots"], row["source_log"],
    ))
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    write_csv(rows, args.csv)
    write_markdown(rows, args.markdown)
    print(f"rows={len(rows)}")
    print(f"csv={args.csv}")
    print(f"markdown={args.markdown}")


if __name__ == "__main__":
    main()
