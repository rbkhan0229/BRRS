#!/usr/bin/env python3
"""Read-only audit of matched M32/M256 SB2000/SP2002 full-configure controls. Preserve PER failures."""
import csv
import hashlib
import importlib.util
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SDK = ROOT.parent.parent / "DW3_QM33_SDK_1.0.2_exp4_phy_ab_20260903"
spec = importlib.util.spec_from_file_location("verifier", SDK / "Drivers/API/brrs_exp4_verify.py")
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)
original_fail = v.fail
MANIFEST = {(r["mode"], r["role"].lower()): r for r in
            json.loads((ROOT / "firmware_manifest.json").read_text())}
SERIALS = {"init": "1050270933", "n2": "1050211584", "n3": "1050273888", "n4": "1050282818"}


def wilson(k, n):
    z = 1.959963984540054
    p, den = k / n, 1 + z*z/n
    center = (p + z*z/(2*n)) / den
    half = z * math.sqrt(p*(1-p)/n + z*z/(4*n*n)) / den
    return [round(100*(center-half), 3), round(100*(center+half), 3)]


def audit(mode, run):
    fast = False
    sb = 2000
    preamble = int(mode)
    folder = ROOT.parent / "exp4_nlos_mcontrol_full_6.9m_g200_l15_pac8_sb2000_sp2002_20260903_spiopt"
    base = f"exp4_{preamble}_s3_r{run}"
    failures, logs, metadata, sensors = [], {}, {}, {}

    def record_per(message):
        if message.startswith("PER ") and " > limit 5.000%" in message:
            failures.append(message)
        else:
            original_fail(message)

    v.fail = record_per
    try:
        for role, serial in SERIALS.items():
            raw = (folder / f"{base}_{role}.log").read_bytes()
            meta = dict(l.split("=", 1) for l in (folder / f"{base}_{role}.meta.txt").read_text().splitlines() if "=" in l)
            expected = MANIFEST[(mode, role)]
            assert meta["probe_serial"] == serial, (mode, run, role, "serial")
            assert meta["firmware_sha256"] == expected["hex_sha256"], (mode, run, role, "firmware")
            assert meta["raw_sha256"] == hashlib.sha256(raw).hexdigest(), (mode, run, role, "raw hash")
            assert int(meta["raw_size_bytes"]) == len(raw)
            assert meta["git_commit"] == expected["commit"]
            assert meta["git_branch"] == "exp4-nlos-phy-ab-20260903"
            assert meta["git_worktree"] == "clean"
            assert meta["run_number"] == str(run)
            assert meta["environment"] == "nlos_mcontrol_full"
            for key, value in {"preamble_symbols":str(preamble), "sensor_count":"3", "guard_us":"200", "lead_us":"15", "pac":"8", "sync_buffer_us":str(sb), "sync_prep_us":"2002", "target_cycles":"1000", "rx_event_source":"fint-polling", "phy_fast_skip_pgf":"disabled", "phy_config_profile":"disabled", "rx_path_profile":"disabled", "spim_start_end_profile":"disabled"}.items():
                assert meta[key] == value, (mode, run, role, key)
            assert meta["phy_fast_switch"] == ("enabled" if fast else "disabled")
            assert float(meta["max_per_percent"]) == 5.0
            lines = raw.decode().splitlines()
            assert lines.count("===== END STATS =====") == 1
            boot = v.key_values(v.last_line(lines, "EXP4_PHY_FAST_SELFTEST_CSV,"))
            assert boot["enabled"] == str(int(fast))
            assert boot["status"] == ("PASS" if fast else "SKIP")
            logs[role], metadata[role] = lines, meta
            if role != "init":
                v.verify_sensor(lines, preamble, 3, int(role[1]), 200, sb, 2002, expected_cycles=1000, phy_fast=fast)
                assert meta["status"] == "PASS"
                tx = v.key_values(v.last_line(lines, "EXP4_TX_DONE,"))
                sensors[role.upper()] = {k: tx[k] for k in ("beacons", "attempts", "success", "beacon", "schedule")}
        v.verify_init(logs["init"], preamble, 3, 200, 15, 8, sb, 2002, 5.0, spi_opt=True, expected_cycles=1000, phy_fast=fast)
        assert metadata["init"]["status"] == ("FAIL" if failures else "PASS")
    finally:
        v.fail = original_fail
    with (folder / f"{base}_multi_tx.assignments.csv").open() as f:
        assignments = list(csv.DictReader(f))
    assert len(assignments) == 3
    assert {r["role"].lower(): r["serial"] for r in assignments} == {r:s for r,s in SERIALS.items() if r != "init"}
    assert all(r["rotation"] == "0" for r in assignments)
    nodes = {}
    for line in logs["init"]:
        if line.startswith("EXP4_NODE_CSV,"):
            fields = line.split(",")
            n, rx = int(fields[3]), int(fields[4])
            nodes[fields[1]] = {"expected":n, "rx":rx, "lost":int(fields[5]), "rx_error_events":int(fields[6]), "per_percent":100*(n-rx)/n, "wilson95_percent":wilson(n-rx,n)}
    n = sum(r["expected"] for r in nodes.values())
    rx = sum(r["rx"] for r in nodes.values())
    prefixes = ("EXP4_FIRST_RX_ARM_CSV,", "EXP4_SYNC_PREP_E2E_CSV,", "EXP4_HOT_PATH_CSV,", "EXP4_REARM_CSV,", "EXP4_DOUBLE_BUFFER_CSV,", "EXP4_SPI_CSV,", "EXP4_DEFERRED_CSV,")
    return {"mode":mode, "run_id":run, "captured_at":metadata["init"]["captured_at"], "expected":n, "rx":rx, "per_percent":100*(n-rx)/n, "wilson95_percent":wilson(n-rx,n), "nodes":nodes, "sensors":sensors, "recorded_failures":failures, "disposition":"FAIL_PER" if failures else "PASS", "subsequent_system_integrity_checks":"all passed", "hashes_roles_and_parameters":"all matched", "rx_error_breakdown":v.last_line(logs["init"], "RX timeouts="), "timing_and_system":{p[:-1]:v.key_values(v.last_line(logs["init"],p)) for p in prefixes}, "raw_log":str(folder / f"{base}_init.log")}


if __name__ == "__main__":
    plan = [(m,1) for m in ("32","256")]
    if len(sys.argv) == 3:
        mode, run = sys.argv[1], int(sys.argv[2])
        assert (mode, run) in plan
        plan = [(mode,run)]
    elif len(sys.argv) != 1:
        raise SystemExit("usage: audit_mcontrol.py [32|256 1]")
    print(json.dumps([audit(m,r) for m,r in plan], indent=2))
