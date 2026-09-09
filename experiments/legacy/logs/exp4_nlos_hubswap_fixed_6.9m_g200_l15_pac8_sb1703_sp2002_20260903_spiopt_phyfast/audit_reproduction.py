#!/usr/bin/env python3
"""Read-only audit; retains PER failures while checking subsequent integrity tests.

No firmware, verifier, raw log, metadata, or acceptance threshold is changed.
The stock verifier exits at the first PER failure. For diagnostic completeness,
record that specific failure and continue its remaining checks in memory.
Every recorded failure is retained in the final run disposition.
"""
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SDK = ROOT.parents[1] / "DW3_QM33_SDK_1.0.2"
spec = importlib.util.spec_from_file_location(
    "exp4_verifier", SDK / "Drivers/API/brrs_exp4_verify.py")
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)
original_fail = verifier.fail
ROLES = {
    "init": ("1050270933", "8c39923d0ba863126597de6497bc428d6c0bfd8e85f91513f004e2a865120907"),
    "n2": ("1050211584", "36f27fc6bab9dd7be10b3d2bb83eedc53eed6e03d457f0239ccdc135f95cc3ae"),
    "n3": ("1050273888", "fa1cd328a43d8cf52db99854f10fda655ccbdb53c0ba30c02d6305490afea560"),
    "n4": ("1050282818", "8fa8df5912b8e4c606f73d89c37c254a2ebed5721898fe1b242b6539de4750c8"),
}


def wilson(losses, expected):
    # Descriptive binomial interval, not a model of temporal/within-SF correlation.
    z = 1.959963984540054
    p = losses / expected
    d = 1 + z * z / expected
    mid = (p + z * z / (2 * expected)) / d
    half = z * math.sqrt(p * (1 - p) / expected + z * z / (4 * expected**2)) / d
    return [round(100 * (mid - half), 3), round(100 * (mid + half), 3)]


report = []
for repeat, run in enumerate((1, 4, 7), 1):
    base = f"exp4_32_s3_r{run}"
    per_failures = []

    def record_per_failure(message):
        if message.startswith("PER ") and " > limit 5.000%" in message:
            per_failures.append(message)
        else:
            original_fail(message)

    verifier.fail = record_per_failure
    logs = {}
    sensors = {}
    init_meta_status = None
    for role, (serial, image_hash) in ROLES.items():
        raw = (ROOT / f"{base}_{role}.log").read_bytes()
        meta = dict(line.split("=", 1) for line in
                    (ROOT / f"{base}_{role}.meta.txt").read_text().splitlines()
                    if "=" in line)
        assert meta["probe_serial"] == serial, (run, role, "serial")
        assert meta["firmware_sha256"] == image_hash, (run, role, "firmware hash")
        assert meta["raw_sha256"] == hashlib.sha256(raw).hexdigest(), (run, role, "raw hash")
        assert int(meta["raw_size_bytes"]) == len(raw), (run, role, "size")
        assert meta["git_commit"] == "f420f95e29351e4b737cc76561bffae7191f4376"
        assert meta["git_worktree"] == "clean"
        assert meta["run_number"] == str(run)
        assert float(meta["max_per_percent"]) == 5.0
        assert meta["pac"] == "8" and meta["lead_us"] == "15"
        assert meta["rx_event_source"] == "fint-polling"
        assert meta["phy_fast_switch"] == "enabled"
        assert meta["phy_fast_skip_pgf"] == "disabled"
        for key in ("phy_config_profile", "rx_path_profile", "spim_start_end_profile"):
            assert meta[key] == "disabled"
        logs[role] = raw.decode().splitlines()
        assert logs[role].count("===== END STATS =====") == 1
        if role == "init":
            init_meta_status = meta["status"]
        else:
            verifier.verify_sensor(logs[role], 32, 3, int(role[1]), 200,
                                   1703, 2002, expected_cycles=1000, phy_fast=True)
            assert meta["status"] == "PASS"
            tx = verifier.key_values(verifier.last_line(logs[role], "EXP4_TX_DONE,"))
            sensors[role.upper()] = {
                "beacons": tx["beacons"], "beacon_status": tx["beacon"],
                "attempts": int(tx["attempts"]), "success": int(tx["success"]),
                "schedule": tx["schedule"],
            }
    verifier.verify_init(logs["init"], 32, 3, 200, 15, 8, 1703, 2002, 5.0,
                         spi_opt=True, expected_cycles=1000, phy_fast=True)
    verifier.fail = original_fail
    assert init_meta_status == ("FAIL" if per_failures else "PASS")
    assignments = list(csv.DictReader((ROOT / f"{base}_multi_tx.assignments.csv").open()))
    assert len(assignments) == 3
    assert {r["role"].lower(): r["serial"] for r in assignments} == {
        role: value[0] for role, value in ROLES.items() if role != "init"}
    assert all(r["rotation"] == "0" for r in assignments)
    rows = [line.split(",") for line in logs["init"] if line.startswith("EXP4_NODE_CSV,")]
    nodes = {r[1]: {"expected": int(r[3]), "rx": int(r[4]),
                       "lost": int(r[5]), "rx_error_events": int(r[6])} for r in rows}
    expected = sum(n["expected"] for n in nodes.values())
    rx = sum(n["rx"] for n in nodes.values())
    report.append({"repeat": repeat, "run_id": run, "rx": rx, "expected": expected,
                   "per_percent": round(100 * (expected - rx) / expected, 6),
                   "wilson95_percent": wilson(expected - rx, expected),
                   "nodes": nodes, "sensor_transmission": sensors,
                   "recorded_failures": per_failures,
                   "disposition": "FAIL_PER" if per_failures else "PASS",
                   "subsequent_integrity_checks": "all passed",
                   "raw_hashes_roles_and_firmware": "all matched"})
print(json.dumps(report, indent=2))
