#!/usr/bin/env python3
"""HALT-only A/B on seven already assigned J-Links; never flash, reset or run RF.

This is an isolated diagnostic, not a replacement for the official runner.
It uses the sealed bundle's existing J-Link helpers and preserves its STOP.
"""

import argparse
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def require_stops(bundle, global_stop):
    if not (bundle / "STOP").is_file() or not global_stop.is_file():
        raise RuntimeError("case and global STOP must both remain set")


def bundle_api(bundle):
    api = bundle / "sdk/Drivers/API"
    if not (api / "brrs_suite_case.py").is_file():
        raise ValueError("sealed bundle API missing")
    sys.path.insert(0, str(api))
    return api


def worker_command(script, bundle, global_stop, role, serial):
    return [sys.executable, str(script), "worker", "--bundle", str(bundle),
            "--global-stop", str(global_stop), "--role", role, "--serial", serial]


def worker(args):
    bundle = args.bundle.resolve()
    require_stops(bundle, args.global_stop)
    bundle_api(bundle)
    from brrs_suite_case import link

    case = json.loads((bundle / "case.json").read_text())
    if case["boards"].get(args.role, {}).get("serial") != args.serial:
        raise ValueError("worker role/serial differs from sealed case")

    # First connection halts, second proves HALT persists after disconnect.
    jl = link(args.serial)
    try:
        jl.halt()
        if not jl.halted():
            raise RuntimeError("HALT failed: " + args.role)
    finally:
        jl.close()
    require_stops(bundle, args.global_stop)
    jl = link(args.serial)
    try:
        halted = bool(jl.halted())
        voltage_mv = int(jl.hardware_status.VTarget)
    finally:
        jl.close()
    if not halted or not 3000 <= voltage_mv <= 3600:
        raise RuntimeError("HALT retention or VTarget failed: " + args.role)
    print(json.dumps({"role": args.role, "serial": args.serial,
                      "halted_after_reconnect": halted, "voltage_mv": voltage_mv}))


def verify_all(c, link):
    result = {}
    for role, board in c["boards"].items():
        jl = link(board["serial"])
        try:
            result[role] = {"serial": board["serial"], "halted": bool(jl.halted()),
                            "voltage_mv": int(jl.hardware_status.VTarget)}
        finally:
            jl.close()
    if len(result) != 7 or any(not x["halted"] or not 3000 <= x["voltage_mv"] <= 3600
                               for x in result.values()):
        raise RuntimeError("seven-board HALT/VTarget verification failed")
    return result


def parallel_halt(script, bundle, global_stop, c, timeout_s=30):
    processes = {}
    started = time.monotonic()
    try:
        for role, board in c["boards"].items():
            cmd = worker_command(script, bundle, global_stop, role, board["serial"])
            processes[role] = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                               stderr=subprocess.STDOUT, text=True,
                                               start_new_session=True)
        rows = {}
        for role, proc in processes.items():
            remaining = max(0.1, timeout_s - (time.monotonic() - started))
            try:
                output, _ = proc.communicate(timeout=remaining)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                output, _ = proc.communicate(timeout=3)
                raise TimeoutError(role + " worker timeout: " + output[-1000:])
            if proc.returncode:
                raise RuntimeError(role + " worker failed: " + output[-1000:])
            row = json.loads(output)
            if row.get("role") != role or row.get("serial") != c["boards"][role]["serial"]:
                raise RuntimeError("worker identity mismatch: " + role)
            rows[role] = row
        return time.monotonic() - started, rows
    finally:
        for proc in processes.values():
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait(timeout=3)


def trial(args):
    bundle = args.bundle.resolve()
    require_stops(bundle, args.global_stop)
    bundle_api(bundle)
    from brrs_suite_case import link, save, sha
    from brrs_single_host import LOCK_PATH, load_bundle, park_all, preflight, snapshot

    if args.out.exists():
        raise FileExistsError("new output directory required")
    c = load_bundle(bundle, args.expected_index)
    if c["conditions"]["stage"] != "stage0" or len(c["boards"]) != 7:
        raise ValueError("only sealed seven-board Stage0 bundles are allowed")
    args.out.mkdir(parents=True)
    report = {"bundle": str(bundle), "case_id": c["id"], "payload_index_sha256": sha(bundle / "payload_hashes.json"),
              "global_stop": str(args.global_stop), "rf_performed": False,
              "flash_performed": False, "reset_performed": False, "phases": []}
    with open(LOCK_PATH, "a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        hardware_started = False
        try:
            require_stops(bundle, args.global_stop)
            report["preflight"] = preflight(bundle, c)
            for mode in ("serial_a", "parallel_b", "serial_a_return"):
                require_stops(bundle, args.global_stop)
                hardware_started = True
                if mode == "parallel_b":
                    elapsed, workers = parallel_halt(Path(__file__).resolve(), bundle, args.global_stop, c)
                else:
                    start = time.monotonic()
                    errors = park_all(c)
                    elapsed = time.monotonic() - start
                    if errors:
                        raise RuntimeError("serial HALT errors: " + repr(errors))
                    workers = None
                report["phases"].append({"mode": mode, "halt_seconds": elapsed,
                                         "workers": workers, "verified": verify_all(c, link)})
        except BaseException as exc:
            report["error"] = repr(exc)
        finally:
            # Always use the proven serial procedure for final recovery.
            if hardware_started:
                try:
                    report["final_serial_halt_errors"] = park_all(c)
                    report["final_verified"] = verify_all(c, link)
                    report["active_image_readback"] = snapshot(bundle, c)
                    report["final_pass"] = (not report["final_serial_halt_errors"] and
                        all(x.get("halted") and x.get("readback", {}).get("status") == "PASS"
                            for x in report["active_image_readback"].values()) and
                        "error" not in report)
                except BaseException as exc:
                    report["final_error"] = repr(exc)
                    report["final_pass"] = False
            else:
                report["final_pass"] = False
            report["global_stop_at_end"] = args.global_stop.is_file()
            report["case_stop_at_end"] = (bundle / "STOP").is_file()
            save(args.out / "RESULT.json", report)
    print(json.dumps(report, indent=2))
    return 0 if report["final_pass"] and report["global_stop_at_end"] and report["case_stop_at_end"] else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="action", required=True)
    p = sub.add_parser("worker")
    p.add_argument("--bundle", type=Path, required=True)
    p.add_argument("--global-stop", type=Path, required=True)
    p.add_argument("--role", required=True)
    p.add_argument("--serial", required=True)
    p = sub.add_parser("trial")
    p.add_argument("--bundle", type=Path, required=True)
    p.add_argument("--expected-index", required=True)
    p.add_argument("--global-stop", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--halt-only-authorized", action="store_true", required=True)
    args = ap.parse_args()
    if args.action == "worker":
        worker(args)
        return 0
    return trial(args)


if __name__ == "__main__":
    raise SystemExit(main())
