#!/usr/bin/env python3
"""Verify one BRRS Experiment 4 coordinator or sensor raw RTT log."""

import argparse
import sys
from pathlib import Path


class VerificationError(Exception):
    pass


def fail(message):
    raise VerificationError(message)


def last_line(lines, prefix):
    matches = [line for line in lines if line.startswith(prefix)]
    if not matches:
        fail(f"missing record: {prefix}")
    return matches[-1]


def csv_fields(line):
    return line.split(",")


def key_values(line):
    values = {}
    for field in csv_fields(line)[1:]:
        if "=" in field:
            key, value = field.split("=", 1)
            values[key] = value
    return values


def integer(values, key):
    try:
        return int(values[key], 0)
    except (KeyError, ValueError):
        fail(f"invalid or missing integer {key!r} in {values}")


def require(values, key, expected):
    actual = values.get(key)
    if actual != str(expected):
        fail(f"{key}={actual!r}, expected {expected!r}")


def require_status_line(lines, prefix):
    values = key_values(last_line(lines, prefix))
    require(values, "status", "PASS")
    return values


FRAME_AIRTIME_US = {32: 97, 64: 130, 128: 195, 256: 325}


def verify_revision(lines, prefix):
    revision = key_values(last_line(lines, prefix))
    if integer(revision, "rev") < 24:
        fail(f"firmware revision {revision.get('rev')} is older than 24")
    require(revision, "beacon_protocol", 3)


def expected_owners(sensors, sequence=None):
    if sequence is not None:
        return sequence
    return "".join(str(node) for node in range(2, sensors + 2))


def verify_init(lines, preamble, sensors, expected_guard, expected_lead,
                expected_pac, sequence=None, spi_opt=False):
    slot_count = len(sequence) if sequence is not None else sensors
    expected = 1000 * slot_count

    config = key_values(last_line(lines, "EXP4_CONFIG_CSV,"))
    require(config, "physical_sensors", sensors)
    require(config, "data_slots", slot_count)
    require(config, "slot_repeats", 1)
    require(config, "data_plen", preamble)
    require(config, "data_pac", expected_pac)
    require(config, "psdu_bytes", 26)
    require(config, "app_payload_bytes", 16)
    require(config, "superframe_us", 10000)
    guard_us = integer(config, "guard_us")
    if guard_us != expected_guard:
        fail(f"guard_us={guard_us}, expected {expected_guard}")
    lead_us = integer(config, "lead_us")
    if lead_us != expected_lead:
        fail(f"lead_us={lead_us}, expected {expected_lead}")
    require(config, "frame_airtime_us", FRAME_AIRTIME_US[preamble])
    require(config, "slot_us", FRAME_AIRTIME_US[preamble] + expected_guard)
    expected_spi_mode = ("persistent_data_burst" if spi_opt else
                         "legacy_per_transaction")
    require(config, "spi_mode", expected_spi_mode)

    verify_revision(lines, "EXP4_FIRMWARE_REV,")

    beacon = key_values(last_line(lines, "BRRS_BEACON_CONFIG_CSV,"))
    require(beacon, "m", preamble)
    require(beacon, "data_psdu", 26)
    require(beacon, "period_us", 10000)
    require(beacon, "slot_interval_us",
            FRAME_AIRTIME_US[preamble] + expected_guard)

    schedule = key_values(last_line(lines, "EXP4_SLOT_SCHEDULE_CSV,"))
    require(schedule, "slot_count", slot_count)
    require(schedule, "slot_owners", expected_owners(sensors, sequence))
    require(schedule, "repeats", 1)

    require_status_line(lines, "EXP4_DOUBLE_BUFFER_CONFIG_CSV,")
    require_status_line(lines, "EXP4_EVENT_MASK_CONFIG_CSV,")
    require_status_line(lines, "EXP4_HOST_IRQ_CONFIG_CSV,")

    done = key_values(last_line(lines, "EXP4_DONE,"))
    require(done, "plen", preamble)
    require(done, "physical_sensors", sensors)
    require(done, "data_slots", slot_count)
    require(done, "slot_repeats", 1)
    require(done, "superframes", 1000)
    require(done, "expected", expected)
    require(done, "collection", "PASS")
    require(done, "status", "PASS")
    rx = integer(done, "rx")
    if not 0 < rx <= expected:
        fail(f"rx={rx}, expected range 1..{expected}")
    expected_link = "PASS" if rx == expected else "LOSS"
    require(done, "link", expected_link)

    status = key_values(last_line(lines, "EXP4_STATUS_CSV,"))
    require(status, "schedule", "PASS")
    require(status, "timing", "PASS")
    require(status, "collection", "PASS")
    require(status, "link", expected_link)

    timing = key_values(last_line(lines, "EXP4_TIMING_CSV,"))
    require(timing, "period_count", 1000)
    require(timing, "sync_delayed_late", 0)
    require(timing, "tx_wait_timeout", 0)
    require(timing, "end_tx", 3)
    period_avg_x1000 = integer(timing, "avg_x1000_us")
    if not 9_999_000 <= period_avg_x1000 <= 10_001_000:
        fail(f"average superframe period {period_avg_x1000 / 1000:.3f} us")

    prep = key_values(last_line(lines, "EXP4_SYNC_PREP_CSV,"))
    require(prep, "count", 999)
    require(prep, "delayed_late", 0)
    prep_budget = integer(prep, "budget_us")
    prep_max = integer(prep, "max_us")
    if prep_max >= prep_budget:
        fail(f"SYNC preparation max {prep_max} us >= budget {prep_budget} us")

    burst = key_values(last_line(lines, "EXP4_BURST_CSV,"))
    require(burst, "forced_prep_close", 0)
    require(burst, "total", 1000)
    if integer(burst, "early_close") + integer(burst, "deadline_close") != 1000:
        fail("burst close counts do not sum to 1000")

    rearm = key_values(last_line(lines, "EXP4_REARM_CSV,"))
    require(rearm, "delayed_schedule_late", 0)
    required_guard = integer(rearm, "required_guard_us")
    if required_guard > guard_us:
        fail(f"required guard {required_guard} us > configured guard {guard_us} us")

    double_buffer = key_values(last_line(lines, "EXP4_DOUBLE_BUFFER_CSV,"))
    good_events = integer(double_buffer, "rx_good_events")
    for key in ("rdb_good_events", "rdb_dispatches", "free_count"):
        if integer(double_buffer, key) != good_events:
            fail(f"{key} does not match rx_good_events")
    for key in ("rdb_host_mismatch", "rdb_incomplete", "overrun"):
        require(double_buffer, key, 0)
    if good_events != rx:
        fail(f"double-buffer good events {good_events} != accepted RX {rx}")

    spi = key_values(last_line(lines, "EXP4_SPI_CSV,"))
    require(spi, "mode", expected_spi_mode)
    require(spi, "active", 0)
    require(spi, "begin_fail", 0)
    require(spi, "end_fail", 0)
    require(spi, "device_id_fail", 0)
    require(spi, "state_error", 0)
    require(spi, "transfer_error", 0)
    require(spi, "recovery", 0)
    require(spi, "status", "PASS")
    if spi_opt:
        require(spi, "begin", 1000)
        require(spi, "end", 1000)
    else:
        require(spi, "begin", 0)
        require(spi, "end", 0)

    hot_path = key_values(last_line(lines, "EXP4_HOT_PATH_CSV,"))
    require(hot_path, "scope", "event_detect_to_buffer_free")
    require(hot_path, "count", good_events)
    require(hot_path, "hist_overflow", 0)
    if integer(hot_path, "max_us") < integer(hot_path, "p999_us"):
        fail("hot-path max is below p99.9")

    hot_phases = [key_values(line) for line in lines
                  if line.startswith("EXP4_HOT_PATH_PHASE_CSV,")]
    expected_hot_phases = {
        "event_detect_to_metadata_done",
        "event_detect_to_status_clear_done",
        "event_detect_to_header_copy_done",
    }
    if {row.get("phase") for row in hot_phases} != expected_hot_phases:
        fail("hot-path phase set is incomplete")
    for row in hot_phases:
        require(row, "count", good_events)

    deferred = key_values(last_line(lines, "EXP4_DEFERRED_CSV,"))
    require(deferred, "pending", 0)
    require(deferred, "queue_overflow", 0)
    require(deferred, "rearm_deadline_miss", 0)
    require(deferred, "rx_timeout", 0)
    require(deferred, "rx_error", 0)
    require(deferred, "status", "PASS")

    summary = csv_fields(last_line(lines, "EXP4_SUMMARY_CSV,"))
    if len(summary) != 20:
        fail(f"EXP4_SUMMARY_CSV has {len(summary)} fields, expected 20")
    summary_numbers = [int(value) for value in summary[1:19]]
    if summary_numbers[0] != preamble or summary_numbers[1] != sensors:
        fail("summary preamble/sensor count mismatch")
    if summary_numbers[2] != slot_count or summary_numbers[3] != 1:
        fail("summary slot count/repeat mismatch")
    if summary_numbers[4:8] != [26, 16,
                               FRAME_AIRTIME_US[preamble] + expected_guard,
                               expected_guard]:
        fail("summary PSDU/payload/slot/guard mismatch")
    if summary_numbers[9] != 1000 or summary_numbers[10] != expected:
        fail("summary superframe/expected count mismatch")
    if summary_numbers[11] != rx:
        fail("summary RX count mismatch")
    expected_per_ppm = ((expected - rx) * 1_000_000 + expected // 2) // expected
    if summary_numbers[12] != expected_per_ppm:
        fail(f"summary PER {summary_numbers[12]} ppm != {expected_per_ppm} ppm")
    if summary_numbers[16] != 0 or summary_numbers[17] != 0:
        fail("summary reports delayed RX or wrong-slot events")
    if summary[19] != "PASS":
        fail("summary status is not PASS")

    node_lines = [line for line in lines if line.startswith("EXP4_NODE_CSV,")]
    if len(node_lines) != sensors:
        fail(f"EXP4_NODE_CSV rows {len(node_lines)}/{sensors}")
    seen_nodes = set()
    node_expected = node_rx = 0
    for line in node_lines:
        fields = csv_fields(line)
        if len(fields) != 7:
            fail(f"malformed node row: {line}")
        node = fields[1]
        if node in seen_nodes:
            fail(f"duplicate node row: {node}")
        seen_nodes.add(node)
        node_slots = sequence.count(node[1:]) if sequence is not None else 1
        node_slot_expected = 1000 * node_slots
        if int(fields[2]) != preamble or int(fields[3]) != node_slot_expected:
            fail(f"unexpected preamble/expected count for {node}")
        received, missed = int(fields[4]), int(fields[5])
        if received + missed != node_slot_expected:
            fail(f"received + missed != {node_slot_expected} for {node}")
        node_expected += int(fields[3])
        node_rx += received
    expected_nodes = {f"N{node}" for node in range(2, sensors + 2)}
    if seen_nodes != expected_nodes:
        fail(f"node set {sorted(seen_nodes)} != {sorted(expected_nodes)}")
    if node_expected != expected or node_rx != rx:
        fail("node totals do not match aggregate totals")

    timing_rows = [line for line in lines if line.startswith("BRRS_SLOT_TIMING_CSV,")]
    if len(timing_rows) != sensors:
        fail(f"slot timing rows {len(timing_rows)}/{sensors}")
    timing_samples = 0
    for line in timing_rows:
        fields = csv_fields(line)
        if len(fields) != 9 or fields[8] != "PASS":
            fail(f"invalid slot timing row: {line}")
        if int(fields[2]) != preamble or int(fields[6]) != int(fields[7]):
            fail(f"slot timing sample mismatch: {line}")
        timing_samples += int(fields[6])
    if timing_samples != rx:
        fail(f"slot timing samples {timing_samples} != RX {rx}")

    missed = expected - rx
    per_percent = 100.0 * missed / expected
    return (
        f"collection=PASS; superframes=1000; rx={rx}/{expected}; "
        f"PER={per_percent:.3f}%; link={expected_link}; "
        f"period={period_avg_x1000 / 1000:.3f}us; "
        f"guard={guard_us}us(required={required_guard}us); lead={lead_us}us"
        f"; pac={expected_pac}"
    )


def verify_sensor(lines, preamble, sensors, node, expected_guard, sequence=None):
    node_slots = sequence.count(str(node)) if sequence is not None else 1
    verify_revision(lines, "EXP4_TX_FIRMWARE_REV,")

    result = csv_fields(last_line(lines, "EXP4_TX_RESULT_CSV,"))
    if len(result) != 12:
        fail(f"EXP4_TX_RESULT_CSV has {len(result)} fields, expected 12")
    result_node = int(result[2])
    result_plen = int(result[3])
    beacons = int(result[4])
    beacon_missed = int(result[5])
    attempts = int(result[6])
    success = int(result[7])
    delayed_late = int(result[8])
    end = int(result[9])
    schedule, beacon_status = result[10], result[11]

    if result_node != node or result_plen != preamble:
        fail("sensor node or preamble mismatch")
    if not 0 < beacons <= 1000 or beacon_missed != 1000 - beacons:
        fail("invalid beacon counts")
    if attempts != success or attempts != beacons * node_slots:
        fail("attempt/success count does not match received beacons * owned slots")
    if delayed_late != 0 or end != 1 or schedule != "PASS":
        fail("sensor schedule, delayed-TX, or END validation failed")
    expected_beacon_status = "PASS" if beacons == 1000 else "LOSS"
    if beacon_status != expected_beacon_status:
        fail("beacon status does not match beacon count")

    done = key_values(last_line(lines, "EXP4_TX_DONE,"))
    require(done, "plen", preamble)
    beacon_pair = done.get("beacons", "").split("/", 1)
    if beacon_pair != [str(beacons), "1000"]:
        fail("EXP4_TX_DONE beacon counts mismatch")
    require(done, "attempts", attempts)
    require(done, "success", success)
    require(done, "end", 1)
    require(done, "schedule", "PASS")
    require(done, "beacon", expected_beacon_status)
    require(done, "status", "PASS")

    beacon = key_values(last_line(lines, "BRRS_BEACON_RX_CSV,"))
    require(beacon, "m", preamble)
    require(beacon, "data_psdu", 26)
    slot_count = len(sequence) if sequence is not None else sensors
    require(beacon, "slot_count", slot_count)
    require(beacon, "period_us", 10000)
    require(beacon, "slot_interval_us",
            FRAME_AIRTIME_US[preamble] + expected_guard)
    owners = beacon.get("slot_owners", "")
    if owners != expected_owners(sensors, sequence):
        fail(f"slot_owners={owners!r}, expected {expected_owners(sensors, sequence)!r}")

    applied = key_values(last_line(lines, "BRRS_DATA_PHY_APPLIED_CSV,"))
    require(applied, "experiment", 4)
    require(applied, "source", "beacon")
    require(applied, "m", preamble)
    require(applied, "node", node)

    sync_rx = key_values(last_line(lines, "EXP4_TX_SYNC_RX_CSV,"))
    require(sync_rx, "delayed_late", 0)
    # The final superframe's post-TX rearm waits for the coordinator's END
    # beacon on an immediate wide RX window instead of the narrow
    # delayed-schedule window used between data superframes (brrs_normal.c,
    # BRRS_EXPERIMENT == 4 branch), so it is not counted in "scheduled".
    if integer(sync_rx, "scheduled") != beacons - 1:
        fail("scheduled SYNC RX count does not match received beacons - 1")

    tx_timing = key_values(last_line(lines, "EXP4_TX_SLOT_TIMING_CSV,"))
    require(tx_timing, "node", f"N{node}")
    require(tx_timing, "n", success)
    require(tx_timing, "reference", "sync_rx_rmarker")
    require(tx_timing, "observation", "data_tx_rmarker")

    beacon_loss = 1000 - beacons
    return (
        f"collection=PASS; node=N{node}; tx={success}/{beacons * node_slots} "
        f"({node_slots} slot{'s' if node_slots != 1 else ''}/superframe); "
        f"beacon_loss={beacon_loss}/1000; beacon={expected_beacon_status}; "
        f"schedule=PASS; plen={preamble}"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--role", required=True, choices=("init", "sensor"))
    parser.add_argument("--preamble", required=True, type=int,
                        choices=(32, 64, 128, 256))
    parser.add_argument("--sensors", required=True, type=int,
                        choices=range(1, 8))
    parser.add_argument("--node", type=int, choices=range(2, 9))
    parser.add_argument("--guard", required=True, type=int,
                        choices=range(0, 1001))
    parser.add_argument("--lead", required=True, type=int,
                        choices=range(0, 1001))
    parser.add_argument("--pac", required=True, type=int, choices=(4, 8))
    parser.add_argument("--sequence",
                        help="Custom per-slot owner digit string (init image "
                             "only), e.g. 2323232323232. Omit for the "
                             "default one-slot-per-node round robin.")
    parser.add_argument("--spi-opt", action="store_true",
                        help="Expect persistent SPIM during DATA bursts.")
    args = parser.parse_args()

    if args.role == "sensor" and args.node is None:
        parser.error("--node is required for role=sensor")
    if args.role == "init" and args.node is not None:
        parser.error("--node is only valid for role=sensor")
    if args.node is not None and args.node > args.sensors + 1:
        parser.error("node is outside the configured sensor set")
    if args.sequence is not None and not (
            1 <= len(args.sequence) <= 32 and
            all(c in "2345678" for c in args.sequence)):
        parser.error("--sequence must be 1-32 digits, each 2-8")

    try:
        lines = args.log.read_text(errors="replace").splitlines()
        if args.role == "init":
            detail = verify_init(lines, args.preamble, args.sensors,
                                 args.guard, args.lead, args.pac, args.sequence,
                                 args.spi_opt)
        else:
            detail = verify_sensor(lines, args.preamble, args.sensors,
                                   args.node, args.guard, args.sequence)
    except (OSError, ValueError, VerificationError) as exc:
        print(f"[verify] FAIL: {exc}", file=sys.stderr)
        return 3

    print(f"[verify] PASS: {detail}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
