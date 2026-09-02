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


def verify_phy_fast_self_test(lines, role, enabled, skip_pgf, node=None):
    if not enabled:
        return
    values = key_values(last_line(lines, "EXP4_PHY_FAST_SELFTEST_CSV,"))
    require(values, "role", role)
    if node is not None:
        require(values, "node", f"N{node}")
    require(values, "enabled", 1)
    require(values, "skip_pgf", 1 if skip_pgf else 0)
    require(values, "stage", 0)
    require(values, "data_mismatch", "0x00")
    require(values, "sync_mismatch", "0x00")
    require(values, "status", "PASS")


def verify_phy_fast_first_rx(lines, role, enabled, expected_events, node=None):
    if not enabled:
        return
    values = key_values(last_line(lines, "EXP4_PHY_FAST_FIRST_RX_CSV,"))
    require(values, "role", role)
    if node is not None:
        require(values, "node", f"N{node}")
    events = integer(values, "events")
    good = integer(values, "good")
    no_preamble = integer(values, "no_preamble")
    if role == "coordinator":
        require(values, "target", "data")
        failures = (integer(values, "sfd_fail") +
                    integer(values, "post_sfd_error"))
    else:
        require(values, "target", "sync")
        failures = integer(values, "error")
    if events != expected_events:
        fail(f"fast-switch first-RX events {events}, expected {expected_events}")
    if good + no_preamble + failures != events:
        fail("fast-switch first-RX outcome counts do not sum to events")
    expected_status = "PASS" if good == events else "LOSS"
    require(values, "status", expected_status)


def verify_init(lines, preamble, sensors, expected_guard, expected_lead,
                expected_pac, expected_sync_buffer, expected_sync_prep,
                max_per_percent, sequence=None, spi_opt=False,
                irq_pending=False, expected_cycles=1000,
                phy_fast=False, phy_fast_skip_pgf=False,
                rx_path_profile=False):
    slot_count = len(sequence) if sequence is not None else sensors
    expected = expected_cycles * slot_count

    config = key_values(last_line(lines, "EXP4_CONFIG_CSV,"))
    require(config, "physical_sensors", sensors)
    require(config, "data_slots", slot_count)
    require(config, "slot_repeats", 1)
    require(config, "data_plen", preamble)
    require(config, "data_pac", expected_pac)
    require(config, "psdu_bytes", 26)
    require(config, "app_payload_bytes", 16)
    require(config, "superframe_us", 10000)
    require(config, "sync_buffer_us", expected_sync_buffer)
    require(config, "sync_prep_us", expected_sync_prep)
    require(config, "data_budget_us",
            10000 - expected_sync_buffer - expected_sync_prep)
    require(config, "sync_prep_deadline_us", 10000 - expected_sync_prep)
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
    actual_rx_path_profile = config.get("rx_path_profile", "disabled")
    expected_rx_path_profile = "enabled" if rx_path_profile else "disabled"
    if actual_rx_path_profile != expected_rx_path_profile:
        fail(f"rx_path_profile={actual_rx_path_profile!r}, expected "
             f"{expected_rx_path_profile!r}")

    verify_revision(lines, "EXP4_FIRMWARE_REV,")
    verify_phy_fast_self_test(
        lines, "coordinator", phy_fast, phy_fast_skip_pgf)

    beacon = key_values(last_line(lines, "BRRS_BEACON_CONFIG_CSV,"))
    require(beacon, "m", preamble)
    require(beacon, "data_psdu", 26)
    require(beacon, "period_us", 10000)
    require(beacon, "first_slot_rmarker_us", expected_sync_buffer)
    require(beacon, "slot_interval_us",
            FRAME_AIRTIME_US[preamble] + expected_guard)

    schedule = key_values(last_line(lines, "EXP4_SLOT_SCHEDULE_CSV,"))
    require(schedule, "slot_count", slot_count)
    require(schedule, "slot_owners", expected_owners(sensors, sequence))
    require(schedule, "repeats", 1)

    require_status_line(lines, "EXP4_DOUBLE_BUFFER_CONFIG_CSV,")
    require_status_line(lines, "EXP4_EVENT_MASK_CONFIG_CSV,")
    host_irq = require_status_line(lines, "EXP4_HOST_IRQ_CONFIG_CSV,")
    expected_host_irq_mode = ("pending_event" if irq_pending else
                              "polling_irq_timestamp" if rx_path_profile else
                              "polling")
    require(host_irq, "mode", expected_host_irq_mode)
    require(host_irq, "spi_owner", "foreground")
    require(host_irq, "enabled", 0)

    done = key_values(last_line(lines, "EXP4_DONE,"))
    require(done, "plen", preamble)
    require(done, "physical_sensors", sensors)
    require(done, "data_slots", slot_count)
    require(done, "slot_repeats", 1)
    require(done, "superframes", expected_cycles)
    require(done, "expected", expected)
    require(done, "collection", "PASS")
    require(done, "status", "PASS")
    rx = integer(done, "rx")
    if not 0 < rx <= expected:
        fail(f"rx={rx}, expected range 1..{expected}")
    expected_link = "PASS" if rx == expected else "LOSS"
    require(done, "link", expected_link)
    per_percent = 100.0 * (expected - rx) / expected
    if per_percent > max_per_percent:
        fail(f"PER {per_percent:.3f}% > limit {max_per_percent:.3f}%")
    verify_phy_fast_first_rx(
        lines, "coordinator", phy_fast, expected_cycles)

    status = key_values(last_line(lines, "EXP4_STATUS_CSV,"))
    require(status, "schedule", "PASS")
    require(status, "timing", "PASS")
    require(status, "collection", "PASS")
    require(status, "link", expected_link)

    timing = key_values(last_line(lines, "EXP4_TIMING_CSV,"))
    require(timing, "period_count", expected_cycles)
    require(timing, "sync_delayed_late", 0)
    require(timing, "tx_wait_timeout", 0)
    require(timing, "end_tx", 3)
    period_avg_x1000 = integer(timing, "avg_x1000_us")
    if not 9_999_000 <= period_avg_x1000 <= 10_001_000:
        fail(f"average superframe period {period_avg_x1000 / 1000:.3f} us")

    prep = key_values(last_line(lines, "EXP4_SYNC_PREP_CSV,"))
    require(prep, "count", expected_cycles - 1)
    require(prep, "delayed_late", 0)
    prep_budget = integer(prep, "budget_us")
    if prep_budget != expected_sync_prep:
        fail(f"SYNC prep budget {prep_budget} us != {expected_sync_prep} us")
    prep_max = integer(prep, "max_us")
    if prep_max >= prep_budget:
        fail(f"SYNC preparation max {prep_max} us >= budget {prep_budget} us")

    first_arm = key_values(last_line(lines, "EXP4_FIRST_RX_ARM_CSV,"))
    require(first_arm, "budget_us", expected_sync_buffer)
    require(first_arm, "count", expected_cycles)
    require(first_arm, "sample_overflow", 0)
    require(first_arm, "delayed_late", 0)
    if integer(first_arm, "rx_open_slack_min_us") <= 0:
        fail("coordinator first delayed-RX arm has no positive RX-open slack")

    prep_e2e = key_values(last_line(lines, "EXP4_SYNC_PREP_E2E_CSV,"))
    require(prep_e2e, "budget_us", expected_sync_prep)
    require(prep_e2e, "count", expected_cycles - 1)
    require(prep_e2e, "sample_overflow", 0)
    require(prep_e2e, "delayed_late", 0)
    if integer(prep_e2e, "remaining_lead_min_us") <= 0:
        fail("next-SYNC delayed-TX arm has no positive remaining lead")

    wait_phases = [key_values(line) for line in lines
                   if line.startswith("EXP4_WAIT_PHASE_CSV,")]
    expected_wait_phases = {
        "coordinator_data_phy_config": expected_cycles,
        "coordinator_delayed_rx_arm_call": expected_cycles,
        "sync_prep_deadline_detect_lateness": expected_cycles - 1,
        "sync_prep_burst_close": expected_cycles - 1,
    }
    if {row.get("phase") for row in wait_phases} != set(expected_wait_phases):
        fail("coordinator wait-budget phase set is incomplete")
    for row in wait_phases:
        require(row, "count", expected_wait_phases[row["phase"]])

    burst = key_values(last_line(lines, "EXP4_BURST_CSV,"))
    require(burst, "forced_prep_close", 0)
    require(burst, "total", expected_cycles)
    if (integer(burst, "early_close") + integer(burst, "deadline_close") !=
            expected_cycles):
        fail(f"burst close counts do not sum to {expected_cycles}")

    rearm = key_values(last_line(lines, "EXP4_REARM_CSV,"))
    require(rearm, "event_source",
            "gpio_irq_pending" if irq_pending else "fint_polling")
    require(rearm, "delayed_schedule_late", 0)
    required_guard = integer(rearm, "required_guard_us")
    if required_guard > guard_us:
        fail(f"required guard {required_guard} us > configured guard {guard_us} us")

    irq_lines = [line for line in lines if line.startswith("EXP4_IRQ_CSV,")]
    if irq_pending:
        if len(irq_lines) != 1:
            fail(f"EXP4_IRQ_CSV rows {len(irq_lines)}, expected 1")
        irq = key_values(irq_lines[0])
        require(irq, "mode", "pending_event")
        require(irq, "spi_owner", "foreground")
        require(irq, "pending", 0)
        require(irq, "duplicates", 0)
        require(irq, "spurious", 0)
        require(irq, "burst_arms", expected_cycles)
        require(irq, "arm_failures", 0)
        require(irq, "enabled", 0)
        require(irq, "status", "PASS")
        if integer(irq, "events") != integer(irq, "dispatches"):
            fail("IRQ event and foreground dispatch counts differ")
    elif irq_lines:
        fail("polling run unexpectedly emitted EXP4_IRQ_CSV")

    double_buffer = key_values(last_line(lines, "EXP4_DOUBLE_BUFFER_CSV,"))
    good_events = integer(double_buffer, "rx_good_events")
    for key in ("rdb_good_events", "rdb_dispatches", "free_count"):
        if integer(double_buffer, key) != good_events:
            fail(f"{key} does not match rx_good_events")
    for key in ("rdb_host_mismatch", "rdb_incomplete", "overrun"):
        require(double_buffer, key, 0)
    if good_events != rx:
        fail(f"double-buffer good events {good_events} != accepted RX {rx}")

    profile_rows = [key_values(line) for line in lines
                    if line.startswith("EXP4_RX_PATH_PROFILE_CSV,")]
    profile_irq_rows = [key_values(line) for line in lines
                        if line.startswith("EXP4_RX_PATH_PROFILE_IRQ_CSV,")]
    profile_summary_rows = [key_values(line) for line in lines
                            if line.startswith(
                                "EXP4_RX_PATH_PROFILE_SUMMARY_CSV,")]
    if rx_path_profile:
        if len(profile_irq_rows) != 1 or len(profile_summary_rows) != 1:
            fail("RX path profile IRQ/summary row count is not one")
        profile_summary = profile_summary_rows[0]
        good_rearm_commands = integer(
            profile_summary, "good_rearm_commands")
        if good_rearm_commands > good_events:
            fail("RX profile good-path rearm count exceeds good events")
        if good_rearm_commands > integer(rearm, "rx_enable_count"):
            fail("RX profile good-path rearm count exceeds all rearm commands")
        expected_profile_counts = {
            "poll_irq_assert_to_fint_detect": good_events,
            "sys_status_read_not_on_good_path": 0,
            "rdb_status_read": good_events,
            "next_rx_fast_command": good_rearm_commands,
            "rx_metadata_spi_combined": good_events,
            "frame_length_decode": good_events,
            "rx_timestamp_decode": good_events,
            "header_copy_8b": good_events,
            "payload_copy_not_on_good_path": 0,
            "status_clear": good_events,
            "rdb_status_clear": good_events,
            "cmd_db_toggle": good_events,
            "irq_assert_to_db_toggle_total": good_events,
        }
        if {row.get("phase") for row in profile_rows} != set(
                expected_profile_counts):
            fail("RX path profile phase set is incomplete")
        for row in profile_rows:
            require(row, "count", expected_profile_counts[row["phase"]])
            require(row, "hist_overflow", 0)
            if integer(row, "p99_us") > integer(row, "max_us"):
                fail(f"RX path phase p99 exceeds max: {row['phase']}")
        profile_irq = profile_irq_rows[0]
        require(profile_irq, "mode", "timestamp_only_polling")
        require(profile_irq, "pending", 0)
        require(profile_irq, "duplicates", 0)
        require(profile_irq, "spurious", 0)
        require(profile_irq, "burst_arms", expected_cycles)
        require(profile_irq, "arm_failures", 0)
        require(profile_irq, "enabled", 0)
        require(profile_irq, "status", "PASS")
        if integer(profile_irq, "events") != integer(
                profile_irq, "dispatches"):
            fail("RX profile IRQ event and timestamp dispatch counts differ")
        require(profile_summary, "enabled", 1)
        require(profile_summary, "good_events", good_events)
        require(profile_summary, "timestamp_samples", good_events)
        require(profile_summary, "missing_irq_timestamps", 0)
        require(profile_summary, "sys_status_good_reads", 0)
        require(profile_summary, "metadata_mode",
                "single_finfo_rx_time_burst")
        require(profile_summary, "header_bytes", 8)
        require(profile_summary, "payload_bytes_copied", 0)
        require(profile_summary, "status", "PASS")
    elif profile_rows or profile_irq_rows or profile_summary_rows:
        fail("non-profile run unexpectedly emitted RX path profile records")

    spi = key_values(last_line(lines, "EXP4_SPI_CSV,"))
    require(spi, "mode", expected_spi_mode)
    require(spi, "active", 0)
    require(spi, "begin_fail", 0)
    require(spi, "end_fail", 0)
    require(spi, "device_id_fail", 0)
    require(spi, "state_error", 0)
    require(spi, "transfer_error", 0)
    require(spi, "direct_timeout", 0)
    require(spi, "recovery", 0)
    require(spi, "status", "PASS")
    if spi_opt:
        require(spi, "begin", expected_cycles)
        require(spi, "end", expected_cycles)
        if integer(spi, "direct_xfers") <= 0:
            fail("optimized SPI run used no direct transfers")
    else:
        require(spi, "begin", 0)
        require(spi, "end", 0)
        require(spi, "direct_xfers", 0)

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
    if (summary_numbers[9] != expected_cycles or
            summary_numbers[10] != expected):
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
        node_slot_expected = expected_cycles * node_slots
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
    return (
        f"collection=PASS; superframes={expected_cycles}; rx={rx}/{expected}; "
        f"PER={per_percent:.3f}%; link={expected_link}; "
        f"PER_limit={max_per_percent:.3f}%; "
        f"period={period_avg_x1000 / 1000:.3f}us; "
        f"guard={guard_us}us(required={required_guard}us); lead={lead_us}us"
        f"; pac={expected_pac}"
    )


def verify_sensor(lines, preamble, sensors, node, expected_guard,
                  expected_sync_buffer, expected_sync_prep,
                  sequence=None, expected_cycles=1000,
                  phy_fast=False, phy_fast_skip_pgf=False):
    node_slots = sequence.count(str(node)) if sequence is not None else 1
    verify_revision(lines, "EXP4_TX_FIRMWARE_REV,")
    verify_phy_fast_self_test(
        lines, "sensor", phy_fast, phy_fast_skip_pgf, node)

    boot = key_values(last_line(lines, "EXP4_TX_BOOT_CSV,"))
    require(boot, "superframe_us", 10000)
    require(boot, "sync_buffer_us", expected_sync_buffer)
    require(boot, "sync_prep_us", expected_sync_prep)
    require(boot, "data_budget_us",
            10000 - expected_sync_buffer - expected_sync_prep)

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
    if (not 0 < beacons <= expected_cycles or
            beacon_missed != expected_cycles - beacons):
        fail("invalid beacon counts")
    if attempts != success or attempts != beacons * node_slots:
        fail("attempt/success count does not match received beacons * owned slots")
    if delayed_late != 0 or end != 1 or schedule != "PASS":
        fail("sensor schedule, delayed-TX, or END validation failed")
    expected_beacon_status = "PASS" if beacons == expected_cycles else "LOSS"
    if beacon_status != expected_beacon_status:
        fail("beacon status does not match beacon count")
    verify_phy_fast_first_rx(
        lines, "sensor", phy_fast, beacons, node)

    done = key_values(last_line(lines, "EXP4_TX_DONE,"))
    require(done, "plen", preamble)
    beacon_pair = done.get("beacons", "").split("/", 1)
    if beacon_pair != [str(beacons), str(expected_cycles)]:
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
    require(beacon, "first_slot_rmarker_us", expected_sync_buffer)
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

    first_arm = key_values(last_line(lines, "EXP4_TX_FIRST_ARM_CSV,"))
    require(first_arm, "node", f"N{node}")
    require(first_arm, "sync_buffer_us", expected_sync_buffer)
    require(first_arm, "count", beacons)
    require(first_arm, "sample_overflow", 0)
    require(first_arm, "delayed_late", 0)
    if integer(first_arm, "first_owned_slot_us") < expected_sync_buffer:
        fail("first owned slot precedes the configured sync buffer")
    if integer(first_arm, "data_rmarker_slack_min_us") <= 0:
        fail("sensor first delayed-TX arm has no positive DATA-RMARKER slack")

    wait_phases = [key_values(line) for line in lines
                   if line.startswith("EXP4_TX_WAIT_PHASE_CSV,")]
    expected_wait_phases = {
        "sync_event_detect_to_frame_read",
        "sync_event_detect_to_beacon_ready",
        "sensor_data_phy_config",
        "sensor_tx_buffer_write",
        "sensor_delayed_tx_arm_call",
    }
    if {row.get("phase") for row in wait_phases} != expected_wait_phases:
        fail("sensor wait-budget phase set is incomplete")
    for row in wait_phases:
        require(row, "node", f"N{node}")
        require(row, "count", beacons)

    beacon_loss = expected_cycles - beacons
    return (
        f"collection=PASS; node=N{node}; tx={success}/{beacons * node_slots} "
        f"({node_slots} slot{'s' if node_slots != 1 else ''}/superframe); "
        f"beacon_loss={beacon_loss}/{expected_cycles}; "
        f"beacon={expected_beacon_status}; "
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
    parser.add_argument("--sync-buffer", required=True, type=int,
                        choices=range(1, 10000))
    parser.add_argument("--sync-prep", required=True, type=int,
                        choices=range(1, 10000))
    parser.add_argument("--max-per-percent", type=float, default=100.0,
                        help="Maximum aggregate PER for coordinator PASS "
                             "(default: 100.0; timing sweeps should set an "
                             "explicit tighter threshold).")
    parser.add_argument("--sequence",
                        help="Custom per-slot owner digit string (init image "
                             "only), e.g. 2323232323232. Omit for the "
                             "default one-slot-per-node round robin.")
    parser.add_argument("--spi-opt", action="store_true",
                        help="Expect persistent SPIM during DATA bursts.")
    parser.add_argument("--irq", action="store_true",
                        help="Expect GPIO IRQ pending-event dispatch.")
    parser.add_argument("--cycles", type=int, default=1000,
                        choices=range(1, 10001),
                        help="Expected superframe count (default: 1000).")
    parser.add_argument("--phy-fast-switch", action="store_true",
                        help="Expect the delta PHY switch and its self-test.")
    parser.add_argument("--phy-fast-skip-pgf", action="store_true",
                        help="Expect the no-PGF delta PHY switch variant.")
    parser.add_argument("--rx-path-profile", action="store_true",
                        help="Expect polling RX-path phase profiling.")
    args = parser.parse_args()

    if args.role == "sensor" and args.node is None:
        parser.error("--node is required for role=sensor")
    if args.role == "init" and args.node is not None:
        parser.error("--node is only valid for role=sensor")
    if args.node is not None and args.node > args.sensors + 1:
        parser.error("node is outside the configured sensor set")
    if args.sync_buffer + args.sync_prep >= 10000:
        parser.error("sync buffer + sync prep must leave a positive DATA budget")
    if not 0.0 <= args.max_per_percent <= 100.0:
        parser.error("--max-per-percent must be between 0 and 100")
    if args.sequence is not None and not (
            1 <= len(args.sequence) <= 32 and
            all(c in "2345678" for c in args.sequence)):
        parser.error("--sequence must be 1-32 digits, each 2-8")
    if args.phy_fast_skip_pgf and not args.phy_fast_switch:
        parser.error("--phy-fast-skip-pgf requires --phy-fast-switch")
    if args.rx_path_profile and args.irq:
        parser.error("--rx-path-profile cannot be combined with --irq")

    try:
        lines = args.log.read_text(errors="replace").splitlines()
        if args.role == "init":
            detail = verify_init(lines, args.preamble, args.sensors,
                                 args.guard, args.lead, args.pac,
                                 args.sync_buffer, args.sync_prep,
                                 args.max_per_percent, args.sequence,
                                 args.spi_opt, args.irq, args.cycles,
                                 args.phy_fast_switch,
                                 args.phy_fast_skip_pgf,
                                 args.rx_path_profile)
        else:
            detail = verify_sensor(lines, args.preamble, args.sensors,
                                   args.node, args.guard, args.sync_buffer,
                                   args.sync_prep, args.sequence, args.cycles,
                                   args.phy_fast_switch,
                                   args.phy_fast_skip_pgf)
    except (OSError, ValueError, VerificationError) as exc:
        print(f"[verify] FAIL: {exc}", file=sys.stderr)
        return 3

    print(f"[verify] PASS: {detail}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
