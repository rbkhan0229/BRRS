#!/usr/bin/env python3
"""Read-only audit of fixed-role 4212 RX-diag ON/OFF runs; never waive PER.

Only the verifier's precise high-PER exception is deferred so all subsequent
system checks run. Every other verifier failure remains fatal for that role.
This writes JSON to stdout only and never contacts hardware or changes logs.
"""
import argparse
import contextlib
import csv
import hashlib
import importlib.util
import io
import json
import math
import re
import subprocess
import sys
from pathlib import Path

# Imported verifier modules must not create __pycache__ during this audit.
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parent
PROJECT = ROOT.parent.parent
SDK = PROJECT / "DW3_QM33_SDK_1.0.2_exp4_rx_error_diag_20260904"
BUNDLE = ROOT.parent / "exp4_rx_error_diag_build_20260904"
COMMIT = "55a23fa94fb7b4e372ec7b03d54be72f01b81e7a"
FIRMWARE_COMMIT = "4f0c9be67f9bd903d7a55f13ce55673719208cbf"
BRANCH = "exp4-rx-error-diag-20260904"
SERIALS = {"init": "1050270933", "n2": "1050211584",
           "n3": "1050204212", "n4": "1050282818"}
ENVIRONMENT = "nlos_rxerr_new4212"
PARAMETERS = {
    "preamble_symbols": "32", "sensor_count": "3", "guard_us": "200",
    "lead_us": "15", "pac": "8", "sync_buffer_us": "2000",
    "sync_prep_us": "2002", "data_budget_us": "5998",
    "target_cycles": "1000", "spi_mode": "persistent-burst",
    "rx_event_source": "fint-polling", "phy_config_profile": "disabled",
    "rx_path_profile": "disabled", "spim_start_end_profile": "disabled",
    "phy_fast_switch": "disabled", "phy_fast_skip_pgf": "disabled",
    "spi_clock_hz": "32000000", "cpu_clock_hz": "64000000",
    "slot_sequence": "default-round-robin", "environment": ENVIRONMENT,
    "distance_m": "6.9", "git_commit": COMMIT, "git_branch": BRANCH,
    "git_worktree": "clean", "capture_method": "pylink",
    "build_configuration": "Debug",
}
DIAG_PREFIXES = {
    "config": "EXP4_RX_ERROR_DIAG_CONFIG_CSV,",
    "summary": "EXP4_RX_ERROR_DIAG_CSV,",
    "bits": "EXP4_RX_ERROR_BIT_CSV,",
    "samples": "EXP4_RX_ERROR_SAMPLE_CSV,",
    "timing": "EXP4_RX_ERROR_TIMING_CSV,",
}
SYSTEM_PREFIXES = (
    "EXP4_DONE,", "EXP4_FIRST_RX_ARM_CSV,", "EXP4_SYNC_PREP_E2E_CSV,",
    "EXP4_HOT_PATH_CSV,", "EXP4_REARM_CSV,", "EXP4_DOUBLE_BUFFER_CSV,",
    "EXP4_SPI_CSV,", "EXP4_DEFERRED_CSV,", "EXP4_STATUS_CSV,",
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def key_values(line):
    result = {}
    for field in line.split(",")[1:]:
        if "=" in field:
            key, value = field.split("=", 1)
            require(key not in result, f"duplicate CSV field {key}")
            result[key] = value
    return result


def rows(lines, prefix):
    return [key_values(line) for line in lines if line.startswith(prefix)]


def one_row(lines, prefix):
    matches = rows(lines, prefix)
    require(len(matches) == 1, f"{prefix} row count {len(matches)}, expected 1")
    return matches[0]


def read_metadata(path):
    result = {}
    for line in path.read_text().splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        require(key not in result, f"duplicate metadata field {key}")
        result[key] = value
    return result


def wilson(lost, count):
    require(count > 0 and 0 <= lost <= count, "invalid Wilson counts")
    z = 1.959963984540054
    p = lost / count
    den = 1 + z * z / count
    center = (p + z * z / (2 * count)) / den
    half = z * math.sqrt(p * (1 - p) / count + z * z / (4 * count * count)) / den
    return [round(max(0, 100 * (center - half)), 4),
            round(min(100, 100 * (center + half)), 4)]


def load_verifier():
    # The audited verifier itself must be the exact reviewed version, not a
    # later local edit. Both git commands are read-only and operate locally.
    relpath = "Drivers/API/brrs_exp4_verify.py"
    expected = subprocess.run(["git", "-C", str(SDK), "show", f"{COMMIT}:{relpath}"],
                              check=True, capture_output=True).stdout
    path = SDK / relpath
    require(path.read_bytes() == expected, "local verifier differs from pinned commit")
    spec = importlib.util.spec_from_file_location("rxdiag_verifier", path)
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    return verifier, sha256(expected)


def firmware_hashes(diag):
    readme = (BUNDLE / "README.md").read_text()
    require(COMMIT in readme and FIRMWARE_COMMIT in readme,
            "bundle README lacks reviewed source provenance")
    checksums = {}
    for line in (BUNDLE / "SHA256SUMS.txt").read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        name = name.lstrip("*")
        require(re.fullmatch(r"[0-9a-f]{64}", digest) is not None,
                "malformed bundle SHA256 digest")
        require(name not in checksums, "duplicate bundle SHA256 path")
        checksums[name] = digest
    result = {}
    for role in SERIALS:
        label = "init" if role == "init" else role.upper()
        name = f"diag_{diag}/exp4_32_s3_{label}.hex"
        require(name in checksums, f"missing firmware manifest entry {name}")
        require(sha256((BUNDLE / name).read_bytes()) == checksums[name],
                f"bundle firmware hash mismatch: {name}")
        result[role] = checksums[name]
    return result


def extract_evidence(lines):
    """Extract original evidence even for a failed integrity check."""
    nodes = {}
    for line in lines:
        if not line.startswith("EXP4_NODE_CSV,"):
            continue
        fields = line.split(",")
        require(len(fields) == 7, f"malformed node row {line}")
        node, preamble, expected, rx, lost, errors = fields[1:]
        expected, rx, lost, errors = map(int, (expected, rx, lost, errors))
        require(node not in nodes, f"duplicate node {node}")
        require(int(preamble) == 32 and expected == 1000 and rx + lost == expected
                and 0 <= rx <= expected and errors >= 0, f"invalid node counts {node}")
        nodes[node] = {"expected": expected, "rx": rx, "lost": lost,
                       "rx_error_events_estimated": errors,
                       "per_percent": 100 * lost / expected,
                       "wilson95_percent": wilson(lost, expected)}
    result = {"nodes": nodes,
              "diagnostic": {key: rows(lines, prefix) for key, prefix in DIAG_PREFIXES.items()},
              "timing_and_system": {prefix[:-1]: rows(lines, prefix)
                                    for prefix in SYSTEM_PREFIXES},
              "slot_classification": rows(lines, "EXP4_SLOT_CLASS_CSV,"),
              "rx_error_breakdown": [line for line in lines if line.startswith("RX timeouts=")]}
    if nodes:
        expected = sum(node["expected"] for node in nodes.values())
        rx = sum(node["rx"] for node in nodes.values())
        result.update(expected=expected, rx=rx, lost=expected-rx,
                      per_percent=100 * (expected-rx)/expected,
                      wilson95_percent=wilson(expected-rx, expected))
    return result


def audit(run, diag, date):
    suffix = "_rxerrdiag" if diag == "on" else ""
    folder = ROOT.parent / (f"exp4_{ENVIRONMENT}_6.9m_g200_l15_pac8_"
                            f"sb2000_sp2002_{date}_spiopt{suffix}")
    base = f"exp4_32_s3_r{run}"
    result = {"run_id": run, "diag": diag, "date": date, "folder": str(folder),
              "source_commit": COMMIT, "firmware_source_commit": FIRMWARE_COMMIT,
              "serials": SERIALS, "parameters": PARAMETERS,
              "disposition": "FAIL_SYSTEM", "system_failures": [],
              "recorded_per_failures": [], "verified_roles": {}, "metadata": {},
              "raw_logs": {}, "limitations": [
                  "Diagnostic sample slots are estimates, not decoded failed-packet source IDs.",
                  "Only the first 64 raw events are retained; bit histograms cover all processed events.",
                  "Zero RX events do not establish that missing packets had no PHY errors.",
                  "ON introduces extra error-path SPI reads; compare matched OFF runs before causal claims.",
                  "New 4212 and old 888 results are distinct board conditions."]}

    def check(label, action):
        try:
            return action()
        except (Exception, SystemExit) as exc:
            result["system_failures"].append(f"{label}: {type(exc).__name__}: {exc}")
            return None

    loaded = check("verifier provenance", load_verifier)
    hashes = check("firmware bundle", lambda: firmware_hashes(diag))
    logs = {}
    for role, serial in SERIALS.items():
        def load_role(role=role, serial=serial):
            raw_path = folder / f"{base}_{role}.log"
            raw = raw_path.read_bytes()
            lines = raw.decode().splitlines()
            logs[role] = lines
            result["raw_logs"][role] = {"path": str(raw_path), "sha256": sha256(raw),
                                         "size_bytes": len(raw)}
            meta = read_metadata(folder / f"{base}_{role}.meta.txt")
            result["metadata"][role] = meta
            expected = dict(PARAMETERS, probe_serial=serial, run_number=str(run),
                            rx_error_diag="enabled-on-init" if diag == "on" else "disabled")
            for key, value in expected.items():
                require(meta.get(key) == value,
                        f"{key}={meta.get(key)!r}, expected {value!r}")
            require(meta.get("role", "").lower() == role, "role metadata mismatch")
            require(hashes is not None and meta.get("firmware_sha256") == hashes[role],
                    "firmware SHA256 mismatch/unverified bundle")
            require(meta.get("raw_sha256") == sha256(raw), "raw SHA256 mismatch")
            require(int(meta["raw_size_bytes"]) == len(raw), "raw size mismatch")
            require(float(meta["max_per_percent"]) == 5.0, "PER threshold must remain 5%")
            require(meta["captured_at"].replace("-", "")[:8] == date, "capture date mismatch")
            require(lines.count("===== END STATS =====") == 1, "END STATS count != 1")
            boot = one_row(lines, "EXP4_PHY_FAST_SELFTEST_CSV,")
            require(boot.get("enabled") == "0" and boot.get("status") == "SKIP",
                    "full-PHY selftest mode mismatch")
        check(f"{role} metadata", load_role)

    if "init" in logs:
        evidence = check("INIT evidence extraction", lambda: extract_evidence(logs["init"]))
        if evidence is not None:
            result.update(evidence)

    if loaded:
        verifier, digest = loaded
        result["verifier_sha256"] = digest
        original_fail = verifier.fail

        def defer_only_per(message):
            if re.fullmatch(r"PER [0-9]+\.[0-9]{3}% > limit 5\.000%", message):
                result["recorded_per_failures"].append(message)
            else:
                original_fail(message)

        verifier.fail = defer_only_per
        try:
            for role in SERIALS:
                if role not in logs:
                    continue

                def verify_role(role=role):
                    lines = logs[role]
                    if role == "init":
                        detail = verifier.verify_init(lines, 32, 3, 200, 15, 8,
                            2000, 2002, 5.0, spi_opt=True, expected_cycles=1000,
                            phy_fast=False, rx_error_diag=(diag == "on"))
                    else:
                        detail = verifier.verify_sensor(lines, 32, 3, int(role[1]),
                            200, 2000, 2002, expected_cycles=1000, phy_fast=False)
                        tx = one_row(lines, "EXP4_TX_DONE,")
                        for key, value in {"beacons": "1000/1000", "attempts": "1000",
                                           "success": "1000", "beacon": "PASS",
                                           "schedule": "PASS"}.items():
                            require(tx.get(key) == value, f"TX {key} != {value}")
                        result.setdefault("sensors", {})[role.upper()] = tx
                    # The original verifier detail starts with collection PASS;
                    # preserve it but never promote this audit's FAIL_PER.
                    result["verified_roles"][role] = {"system_checks": "PASS", "detail": detail}
                check(f"{role} original verifier", verify_role)
        finally:
            verifier.fail = original_fail

    def final_checks():
        require(set(result["verified_roles"]) == set(SERIALS), "not all four roles passed system verification")
        nodes = result.get("nodes", {})
        require(set(nodes) == {"N2", "N3", "N4"}, "node set mismatch")
        require(result.get("expected") == 3000 and result.get("rx", 0) > 0,
                "invalid aggregate counts / zero valid RX")
        double_buffer = one_row(logs["init"], "EXP4_DOUBLE_BUFFER_CSV,")
        for key in ("rdb_resync", "rdb_incomplete_recovered"):
            require(double_buffer.get(key) == "0", f"{key} != 0")
        classes = rows(logs["init"], "EXP4_SLOT_CLASS_CSV,")
        require(len(classes) == 3 and {row.get("src") for row in classes} == set(nodes),
                "slot classification must contain each node exactly once")
        for row in classes:
            source = row["src"]
            require(row.get("observed_owner") == source, "slot owner mismatch")
            require(int(row["observed_slot"]) == int(source[1])-2, "observed slot mismatch")
            require(int(row["count"]) == nodes[source]["rx"], "slot/node RX count mismatch")
            host0, host1 = int(row["host0"]), int(row["host1"])
            require(host0 >= 0 and host1 >= 0 and host0+host1 == int(row["count"]),
                    "slot host counts mismatch")
        has_high_per = result["per_percent"] > 5.0
        require(bool(result["recorded_per_failures"]) == has_high_per,
                "computed PER and original verifier PER disposition mismatch")
        for role, meta in result["metadata"].items():
            expected = "FAIL" if role == "init" and has_high_per else "PASS"
            require(meta.get("status") == expected and meta.get("collection_status") == expected,
                    f"{role} metadata status mismatch: expected {expected}")
        # The manual fixed-role driver may omit the stock auto-assignment file.
        # In that case explicit per-role serial metadata is authoritative.
        assignment_file = folder / f"{base}_multi_tx.assignments.csv"
        if assignment_file.exists():
            with assignment_file.open() as stream:
                assigned = list(csv.DictReader(stream))
            require(len(assigned) == 3, "assignment row count mismatch")
            require({row["role"].lower(): row["serial"] for row in assigned} ==
                    {role: serial for role, serial in SERIALS.items() if role != "init"},
                    "assignment serial/role mismatch")
            require(all(row["rotation"] == "0" for row in assigned), "unexpected role rotation")
            result["assignment_check"] = "PASS: assignments.csv and explicit metadata"
        else:
            result["assignment_check"] = "PASS: explicit per-role serial metadata; no auto-assignment file"
    check("cross-role integrity", final_checks)
    if not result["system_failures"]:
        result["disposition"] = "FAIL_PER" if result["recorded_per_failures"] else "PASS"
    result["subsequent_system_integrity_checks"] = (
        "all passed (PER threshold retained)" if not result["system_failures"]
        else "FAILED; do not use as clean causal comparison")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True, type=int)
    parser.add_argument("--diag", required=True, choices=("off", "on"))
    parser.add_argument("--date", default="20260904")
    args = parser.parse_args()
    if args.run < 1:
        parser.error("run must be positive")
    if not re.fullmatch(r"\d{8}", args.date):
        parser.error("date must be YYYYMMDD")
    # Keep stdout strictly machine-readable even if an imported verifier grows
    # incidental prints later. Auditing is intentionally read-only.
    with contextlib.redirect_stdout(io.StringIO()):
        result = audit(args.run, args.diag, args.date)
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return {"PASS": 0, "FAIL_PER": 1, "FAIL_SYSTEM": 2}[result["disposition"]]


if __name__ == "__main__":
    raise SystemExit(main())
