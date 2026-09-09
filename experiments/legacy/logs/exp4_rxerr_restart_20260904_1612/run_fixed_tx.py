#!/usr/bin/env python3
"""One bounded, fixed-role TX run; no retries, role rotation, or broad cleanup.

Run on the Air only after the supervising operator verifies the three boards.
This wrapper flashes the verified TX images through brrs_exp4_capture.sh.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


API = Path("/Users/songchieon/Desktop/DWM3000/"
           "DW3_QM33_SDK_1.0.2_exp4_rx_error_diag_20260904/Drivers/API")
ENVIRONMENT = "nlos_rxerr_new4212"
ROLES = (
    ("N2", "1050211584", "c646743417caa3c533f50af170e0ed426f11b697bfdd653148e90e43ca95d6e4",
     "e1834cdadcadf2833465451dcff83f0f743bc07e2eeeaffda44fbdad81c6f41d"),
    ("N3", "1050204212", "d7e50b966bbfc1024c81339e45303e8304049aaad94af5723539453287956c27",
     "bb6dabcb4350fcf8d1d95e74d0e163534fc8a2c7c35b78b730d280c5cc8f7974"),
    ("N4", "1050282818", "77674eac4b0f01cb52a3a0a03687dca6dae967a438bbdbee7124fa3ec319fbe6",
     "de90edb9572f3b8f4acd39faec63374f50034128148ee4e9174c03383e294cab"),
)
TOTAL_SECONDS = 240
STARTUP_SECONDS = 60
READY_TEXT = "READY marker seen"
END_TEXT = "===== END STATS ====="


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def positive(value):
    result = int(value)
    if result < 1:
        raise argparse.ArgumentTypeError("--run must be a positive integer")
    return result


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=positive, required=True)
    parser.add_argument("--diag", choices=("off", "on"), required=True)
    args = parser.parse_args()
    session = Path(__file__).resolve().parent / f"tx_{args.diag}_r{args.run}"
    # Refuse repeated invocation before touching any probe or existing log.
    session.mkdir(exist_ok=False)
    console = (session / "console.log").open("x", encoding="utf-8", buffering=1)
    status_path = session / "status.json"
    started = time.monotonic()
    deadline = started + TOTAL_SECONDS
    workers = []
    handles = []
    stop_signal = None
    state = {
        "version": 1, "status": "PREFLIGHT", "run": args.run,
        "diag": args.diag, "environment": ENVIRONMENT,
        "started_at": utc_now(), "supervisor_pid": os.getpid(),
        "api": str(API), "session": str(session),
        "deadline_seconds": TOTAL_SECONDS, "workers": [],
        "parameters": {"preamble": 32, "sensors": 3, "guard_us": 200,
                       "lead_us": 15, "pac": 8, "sync_buffer_us": 2000,
                       "sync_prep_us": 2002, "cycles": 1000,
                       "spi_opt": True, "phy_fast_switch": False,
                       "irq": False, "distance_m": 6.9},
    }

    def persist():
        temporary = session / "status.json.tmp"
        temporary.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
        temporary.replace(status_path)

    def say(message):
        line = f"{utc_now()} {message}"
        console.write(line + "\n")
        # A broken SSH output pipe must not prevent status preservation/cleanup.
        try:
            print(line, flush=True)
        except (BrokenPipeError, OSError):
            pass

    def requested_stop(signum, _frame):
        nonlocal stop_signal
        stop_signal = signum

    def check_stop():
        if stop_signal is not None:
            raise RuntimeError(f"interrupted by signal {stop_signal}")
        if time.monotonic() >= deadline:
            raise TimeoutError(f"overall {TOTAL_SECONDS}s deadline exceeded")

    def cleanup():
        # Every worker was started with start_new_session=True. Its saved PID
        # identifies only that child process group, never another experiment.
        for worker in workers:
            try:
                os.killpg(worker["process"].pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        until = time.monotonic() + 3
        while time.monotonic() < until:
            if all(w["process"].poll() is not None for w in workers):
                break
            time.sleep(0.05)
        for worker in workers:
            # The capture shell may already have exited while an RTT child
            # remains in its group, so do not condition this on shell poll().
            try:
                os.killpg(worker["process"].pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        for worker in workers:
            try:
                worker["record"]["exit_code"] = worker["process"].wait(timeout=1)
            except subprocess.TimeoutExpired:
                worker["record"]["cleanup_error"] = "worker did not reap"

    previous_handlers = {}
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        previous_handlers[sig] = signal.signal(sig, requested_stop)

    exit_code = 1
    try:
        (session / "supervisor.pid").write_text(f"{os.getpid()}\n", encoding="ascii")
        persist()
        capture = API / "brrs_exp4_capture.sh"
        if not capture.is_file():
            raise RuntimeError(f"capture script missing: {capture}")
        suffix = "_rxerrdiag" if args.diag == "on" else ""
        image_dir = API / "Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4" / (
            "plen32_sensors3_sb2000_sp2002_guard200_spiopt" + suffix)
        date_tag = datetime.now().strftime("%Y%m%d")
        capture_dir = API.parents[2] / "logs" / (
            f"exp4_{ENVIRONMENT}_6.9m_g200_l15_pac8_sb2000_sp2002_{date_tag}_spiopt" + suffix)
        # Hash all three roles before flashing the first one.
        for role, serial, expected_hex, expected_elf in ROLES:
            for extension, expected in (("hex", expected_hex), ("elf", expected_elf)):
                image = image_dir / f"exp4_32_s3_{role}.{extension}"
                if sha256(image) != expected:
                    raise RuntimeError(f"{role} {extension} hash mismatch: {image}")
            base = f"exp4_32_s3_r{args.run}_{role.lower()}"
            for extension in ("log", "meta.txt", "build.log"):
                existing = capture_dir / f"{base}.{extension}"
                if existing.exists():
                    raise RuntimeError(f"refusing existing run artifact: {existing}")
        say(f"FIXED_TX_PREFLIGHT,run={args.run},diag={args.diag},status=PASS")
        state["status"] = "STARTING"
        persist()

        for role, serial, expected_hex, expected_elf in ROLES:
            check_stop()
            # A prior worker completing before all three are ready indicates
            # premature INIT activity or a capture failure, not a valid run.
            for older in workers:
                if older["process"].poll() is not None:
                    raise RuntimeError(f"{older['record']['role']} exited during TX setup")
            command = ["bash", str(capture), role, "32", "3", str(args.run),
                       ENVIRONMENT, "6.9", "--serial", serial,
                       "--guard", "200", "--lead", "15", "--pac", "8",
                       "--sync-buffer", "2000", "--sync-prep", "2002",
                       "--cycles", "1000", "--spi-opt", "--no-build",
                       "--timeout", "180", "--max-per-percent", "5.0"]
            if args.diag == "on":
                command.append("--rx-error-diag")
            worker_log = session / f"{role.lower()}.worker.log"
            handle = worker_log.open("x", encoding="utf-8", buffering=1)
            handles.append(handle)
            record = {"role": role, "serial": serial, "command": command,
                      "hex_sha256": expected_hex, "elf_sha256": expected_elf,
                      "worker_log": str(worker_log), "started_at": utc_now(),
                      "ready": False, "exit_code": None}
            process = subprocess.Popen(command, cwd=API, stdout=handle,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            record["pid"] = process.pid
            workers.append({"process": process, "record": record})
            state["workers"].append(record)
            persist()
            say(f"FIXED_TX_STARTED,role={role},serial={serial},pid={process.pid}")
            startup_deadline = min(deadline, time.monotonic() + STARTUP_SECONDS)
            while True:
                check_stop()
                for active in workers:
                    if active["process"].poll() is not None:
                        raise RuntimeError(f"{active['record']['role']} exited before all TX READY")
                if READY_TEXT in worker_log.read_text(encoding="utf-8", errors="replace"):
                    record["ready"] = True
                    record["ready_at"] = utc_now()
                    persist()
                    say(f"FIXED_TX_NODE_READY,role={role},serial={serial}")
                    break
                if time.monotonic() >= startup_deadline:
                    raise TimeoutError(f"{role} did not reach READY within {STARTUP_SECONDS}s")
                time.sleep(0.1)

        check_stop()
        if not all(w["process"].poll() is None for w in workers):
            raise RuntimeError("a TX exited at all-ready barrier")
        state["status"] = "READY"
        state["ready_at"] = utc_now()
        persist()
        say(f"FIXED_TX_READY,run={args.run},diag={args.diag},sensors=3,status=PASS")

        while any(w["process"].poll() is None for w in workers):
            check_stop()
            for worker in workers:
                code = worker["process"].poll()
                worker["record"]["exit_code"] = code
                if code is not None and code != 0:
                    raise RuntimeError(f"{worker['record']['role']} capture failed with exit {code}")
            time.sleep(0.1)

        for worker in workers:
            record = worker["record"]
            record["exit_code"] = worker["process"].wait(timeout=1)
            text = Path(record["worker_log"]).read_text(encoding="utf-8", errors="replace")
            if record["exit_code"] != 0 or "[verify] PASS:" not in text:
                raise RuntimeError(f"{record['role']} has no successful capture verification")
            raw_matches = re.findall(r"^\[done\] raw=(.+)$", text, flags=re.MULTILINE)
            meta_matches = re.findall(r"^\[done\] meta=(.+)$", text, flags=re.MULTILINE)
            if len(raw_matches) != 1 or len(meta_matches) != 1:
                raise RuntimeError(f"{record['role']} missing/duplicate completion paths")
            raw_path, meta_path = Path(raw_matches[0]), Path(meta_matches[0])
            raw = raw_path.read_bytes()
            if raw.decode("utf-8", errors="replace").splitlines().count(END_TEXT) != 1:
                raise RuntimeError(f"{record['role']} missing/duplicate END STATS")
            metadata = dict(line.split("=", 1) for line in
                            meta_path.read_text(encoding="utf-8").splitlines() if "=" in line)
            expected = {"role": record["role"], "probe_serial": record["serial"],
                        "firmware_sha256": record["hex_sha256"],
                        "raw_sha256": hashlib.sha256(raw).hexdigest(),
                        "run_number": str(args.run), "environment": ENVIRONMENT,
                        "status": "PASS", "collection_status": "PASS",
                        "rx_error_diag": "enabled-on-init" if args.diag == "on" else "disabled"}
            for key, value in expected.items():
                if metadata.get(key) != value:
                    raise RuntimeError(f"{record['role']} metadata mismatch: {key}")
            record.update(raw_log=str(raw_path), metadata=str(meta_path),
                          raw_sha256=expected["raw_sha256"], status="PASS")
        state["status"] = "PASS"
        exit_code = 0
        say(f"FIXED_TX_DONE,run={args.run},diag={args.diag},status=PASS")
    except (Exception, KeyboardInterrupt) as exc:
        state["status"] = "FAIL"
        state["error"] = f"{type(exc).__name__}: {exc}"
        state["signal"] = stop_signal
        say(f"FIXED_TX_DONE,run={args.run},diag={args.diag},status=FAIL,error={exc}")
    finally:
        if exit_code != 0:
            cleanup()
        for worker in workers:
            worker["record"]["exit_code"] = worker["process"].poll()
        state["ended_at"] = utc_now()
        state["elapsed_seconds"] = round(time.monotonic() - started, 3)
        state["exit_code"] = exit_code
        persist()
        for handle in handles:
            handle.close()
        console.close()
        for sig, previous in previous_handlers.items():
            signal.signal(sig, previous)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
