#!/usr/bin/env python3
"""Collect one sealed, finite AUX packet transmitter; never retry or resume RF.

This process exclusively owns the AUX probe from programming through shutdown.
The parent creates results/AUX_ARM and results/AUX_STOP. STOP is always checked
before ARM. Study/global STOP paths must be explicitly sealed in auxiliary_job.
No extra erase, UICR programming, or receiver configuration is performed here.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import sys
import time


SCHEMA = "aux-finite-packet-v1"
ARM_MAGIC = 0x41524D31
STOP_MAGIC = 0x53544F50
RAM_LOW, RAM_HIGH = 0x20000000, 0x20040000
SYMBOLS = (
    "aux_host_arm", "aux_host_stop", "aux_duration_ms", "aux_state",
    "aux_tx_count", "aux_tx_attempts", "aux_error_code", "aux_end_reason",
    "aux_elapsed_us", "aux_tx_timeout_count", "aux_schedule_overrun_count",
    "aux_last_status", "aux_rf_off_commanded", "aux_tx_timestamp_count",
)
COUNTERS = tuple(n for n in SYMBOLS if n not in
                 ("aux_host_arm", "aux_host_stop", "aux_duration_ms"))


def utc():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def atomic_json(path, value):
    tmp = path.with_name(path.name + "." + str(os.getpid()) + ".tmp")
    with tmp.open("w") as handle:
        json.dump(value, handle, indent=2, sort_keys=True)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(tmp, path)


def inside(root, relative):
    path = (root / relative).resolve()
    if not path.is_relative_to(root):
        raise ValueError("payload path escapes bundle")
    return path


def validate(root, index):
    """Validate the full immutable payload before importing it or opening SWD."""
    if sha(root / "payload_hashes.json") != index:
        raise ValueError("payload index mismatch")
    hashes = json.loads((root / "payload_hashes.json").read_text())
    for relative, expected in hashes.items():
        if sha(inside(root, relative)) != expected:
            raise ValueError("payload mismatch: " + relative)
    sys.path.insert(0, str(root / "sdk/Drivers/API"))
    from brrs_single_host import load_bundle
    from brrs_suite_case import hex_chunks
    case = load_bundle(root, index)
    job = case["auxiliary_job"]
    if job.get("schema", SCHEMA) != SCHEMA:
        raise ValueError("unknown AUX schema")
    mode = str(job["mode"]).upper()
    if mode not in ("OFF", "ON"):
        raise ValueError("AUX mode must be OFF or ON")
    serial = str(job["serial"])
    boards = case["boards"]
    if len(boards) != 8 or len({str(b["serial"]) for b in boards.values()}) != 8:
        raise ValueError("AUX study requires exactly eight distinct assigned probes")
    if sum(str(b["serial"]) == serial for b in boards.values()) != 1:
        raise ValueError("AUX serial must belong to exactly one assigned board")
    if any(str(j["serial"]) == serial for j in case["jobs"]):
        raise ValueError("AUX serial overlaps a victim job")
    image = inside(root, job["hex"])
    elf = inside(root, job.get("elf", str(Path(job["hex"]).with_suffix(".elf"))))
    if sha(image) != job["hex_sha256"] or sha(elf) != job["elf_sha256"]:
        raise ValueError("AUX image hash mismatch")
    chunks = hex_chunks(image)
    # nRF52840 main flash only. In particular, reject UICR records.
    if any(start < 0 or start + len(data) > 0x100000 for start, data in chunks):
        raise ValueError("AUX HEX contains non-main-flash addresses")
    symbols = {}
    for name in SYMBOLS:
        item = job["symbols"][name]
        address = number(item["address"] if isinstance(item, dict) else item)
        size = number(item.get("size", 4)) if isinstance(item, dict) else 4
        if size != 4 or address % 4 or not RAM_LOW <= address <= RAM_HIGH - 4:
            raise ValueError("invalid uint32 AUX RAM symbol: " + name)
        symbols[name] = address
    if len(set(symbols.values())) != len(symbols):
        raise ValueError("overlapping AUX RAM symbols")
    trace = job.get("timestamp_trace")
    if not isinstance(trace, dict) or trace.get("schema") != "aux-tx-rmarker-hi32-v1":
        raise ValueError("missing AUX timestamp trace schema")
    item = job["symbols"].get("aux_tx_timestamp_hi32")
    if not isinstance(item, dict):
        raise ValueError("missing AUX timestamp array symbol")
    address, size = number(item["address"]), number(item["size"])
    if (address % 4 or size != 180000 or not RAM_LOW <= address <= RAM_HIGH-size
            or trace.get("capacity") != 45000):
        raise ValueError("invalid AUX timestamp array")
    if any(address <= scalar < address+size for scalar in symbols.values()):
        raise ValueError("AUX timestamp array overlaps scalar")
    if number(job["arm_magic"]) != ARM_MAGIC or number(job["stop_magic"]) != STOP_MAGIC:
        raise ValueError("AUX mailbox magic mismatch")
    rtt = number(job["rtt_address"])
    if not RAM_LOW <= rtt < RAM_HIGH:
        raise ValueError("RTT control block outside SRAM")
    duration = number(job.get("duration_ms", 60000))
    if not 1 <= duration <= 60000:
        raise ValueError("AUX duration outside firmware bound")
    stops = [root / "STOP"]
    explicit_stops = job.get("stop_paths", [])
    if len(explicit_stops) < 2:
        raise ValueError("seal both study and global STOP paths")
    for value in explicit_stops:
        path = Path(value)
        if not path.is_absolute():
            raise ValueError("external STOP paths must be absolute")
        stops.append(path)
    return case, job, image, chunks, symbols, rtt, mode, duration, stops


def validate_power_ready(fields, job):
    expected = job["power_index"]
    if type(expected) is not int or expected != 40:
        raise ValueError("unapproved AUX power index")
    requested = number(fields["power_index"])
    applied = [number(fields[k]) for k in ("applied_data", "applied_phr", "applied_shr", "applied_sts")]
    configured, readback = number(fields["tx_power"]), number(fields["tx_power_readback"])
    if requested != expected or applied != [expected]*4:
        raise ValueError("AUX requested/applied power mismatch")
    if not 0 < configured < 0xffffffff or configured != readback:
        raise ValueError("AUX power register mismatch")
    return dict(status="PASS", requested=requested, applied=applied,
                configured_register=configured, readback_register=readback,
                nominal_reduction_db_from_index40=0,
                scope="Driver indices and register readback, not radiated-power measurement")


class Collector:
    def __init__(self, root, index, validated, factory=None, interface=None):
        (self.case, self.job, self.image, self.chunks, self.symbols, self.rtt,
         self.mode, self.duration, self.stops) = validated
        self.root, self.index = root, index
        self.out = root / "results"
        self.factory, self.interface = factory, interface
        self.rtt_not_ready_exceptions = ()
        self.jl = None
        self.raw = self.progress = None
        self.data = bytearray()
        self.raw_path = self.out / "AUX.raw.log"
        self.status_path = self.out / "AUX_STATUS.json"
        self.meta_path = self.out / "AUX.meta.json"
        self.started = time.monotonic()
        self.deadline = self.started + 100.0
        self.signal_error = None
        self.programmed = self.target_started = self.rtt_started = False
        self.arm_sent = self.stop_sent = False
        self.normal_stop = False
        self.last_ram = {}
        self.trace_readback = None
        self.errors = []
        self.marker_counts = {k: 0 for k in ("READY", "START", "END")}
        self.marker_records = {}
        self.state = dict(status="STARTING", schema=SCHEMA, case_id=self.case["id"],
                          serial=str(self.job["serial"]), mode=self.mode,
                          started_at=utc(), ready=False, running=False, end=False,
                          tx_success=0, state=0, error=None, errors=self.errors,
                          payload_index_sha256=index, firmware_sha256=self.job["hex_sha256"],
                          arm_count=0, stop_count=0, reset_count=0,
                          reconnect_attempts=0, rf_retry_count=0)

    def error(self, message):
        message = str(message)
        if message not in self.errors:
            self.errors.append(message)
        self.state["error"] = self.errors[0]

    def persist(self):
        self.state.update(updated_at=utc(), host_monotonic=time.monotonic(),
                          elapsed_host_seconds=time.monotonic() - self.started)
        atomic_json(self.status_path, self.state)

    def external_stop(self):
        if self.signal_error:
            return self.signal_error
        return next(("explicit STOP: " + str(p) for p in self.stops if p.exists()), None)

    def new_link(self):
        jl = self.factory()
        try:
            jl.open(serial_no=int(self.job["serial"]))
            jl.set_tif(self.interface)
            jl.connect("NRF52840_XXAA", speed=4000)
            jl.exec_command("SetRestartOnClose = 0")
            return jl
        except BaseException:
            jl.close()
            raise

    def verify_flash(self, jl):
        for address, expected in self.chunks:
            if bytes(jl.memory_read8(address, len(expected))) != expected:
                raise RuntimeError("AUX full HEX readback mismatch at " + hex(address))
        return dict(status="PASS", bytes_verified=sum(len(x[1]) for x in self.chunks),
                    firmware_sha256=self.job["hex_sha256"], at=utc())

    def write_mailbox(self, name, value):
        self.jl.memory_write32(self.symbols[name], [value])
        if number(self.jl.memory_read32(self.symbols[name], 1)[0]) != value:
            raise RuntimeError("AUX mailbox write/readback mismatch: " + name)

    def request_stop(self, normal=False):
        if normal:
            self.normal_stop = True
        if not self.target_started or self.stop_sent:
            return
        self.write_mailbox("aux_host_stop", STOP_MAGIC)
        self.stop_sent = True
        self.state.update(stop_count=1, stop_requested_at=utc(), status="STOPPING")
        self.persist()

    def read_ram(self):
        ram = {name: number(self.jl.memory_read32(self.symbols[name], 1)[0])
               for name in COUNTERS}
        if ram["aux_state"] not in (0, 1, 2, 3, 4):
            raise RuntimeError("invalid AUX RAM state")
        if ram["aux_tx_timestamp_count"] > 45000:
            raise RuntimeError("AUX timestamp count invalid")
        if ram["aux_tx_timestamp_count"] < self.last_ram.get("aux_tx_timestamp_count", 0):
            raise RuntimeError("AUX timestamp count decreased")
        if ram["aux_tx_count"] < self.last_ram.get("aux_tx_count", 0):
            raise RuntimeError("AUX TX counter decreased; reset or bad ownership")
        self.last_ram = ram
        self.state.update(state=ram["aux_state"], tx_success=ram["aux_tx_count"],
                          running=ram["aux_state"] == 2, ram=ram)
        if ram["aux_tx_count"] and "first_positive_tx_at" not in self.state:
            self.state["first_positive_tx_at"] = utc()
            self.state["first_positive_tx_monotonic"] = time.monotonic()
        record = dict(at=utc(), host_monotonic=time.monotonic(), **ram)
        self.progress.write(json.dumps(record, sort_keys=True) + "\n")
        self.progress.flush()
        self.persist()
        return ram

    def drain(self):
        data = self.jl.rtt_read(1, 16384)
        if not data:
            return
        block = bytes(data)
        self.raw.write(block)
        self.raw.flush()
        self.data.extend(block)
        if len(self.data) > 8 * 1024 * 1024:
            raise RuntimeError("unexpected AUX log volume")
        text = self.data.decode("utf-8", errors="replace")
        lines = text.splitlines()
        # RTT reads may split any line, including schema and numeric fields.
        if text and not text.endswith(("\n", "\r")):
            lines = lines[:-1]
        for kind in self.marker_counts:
            matches = [line for line in lines
                       if re.match(r"^AUX_" + kind + r"(?:[\s,]|$)", line)]
            self.marker_counts[kind] = len(matches)
            if len(matches) > 1:
                raise RuntimeError("duplicate AUX " + kind + " marker")
            if matches and kind != "START" and "schema=" + SCHEMA not in matches[0]:
                raise RuntimeError("AUX marker schema mismatch")
            if matches:
                self.marker_records[kind] = dict(re.findall(r"(\w+)=([^\s,]+)", matches[0]))
        if self.marker_counts["READY"]:
            self.state["power_validation"] = validate_power_ready(self.marker_records["READY"], self.job)
            if self.job["channel"] == 5:
                fields = self.marker_records["READY"]
                if number(fields["channel"]) != 5 or number(fields["channel_hw"]) != 5:
                    raise RuntimeError("AUX CH5 READY/register channel mismatch")
                self.state["channel_validation"] = {"status": "PASS", "requested": 5, "hardware": 5}
            self.state["ready"] = True
            self.state.setdefault("ready_at", utc())
        if self.marker_counts["START"]:
            if self.job["channel"] == 5 and number(self.marker_records["START"]["channel"]) != 5:
                raise RuntimeError("AUX CH5 START channel mismatch")
            self.state.setdefault("start_marker_at", utc())
        if self.marker_counts["END"]:
            self.state["end"] = True
            self.state.setdefault("end_at", utc())
        self.state["marker_counts"] = dict(self.marker_counts)
        self.persist()

    def complete(self):
        return (self.state["end"] and self.last_ram.get("aux_state") in (3, 4)
                and self.last_ram.get("aux_rf_off_commanded") == 1)

    def capture(self):
        why = self.external_stop()
        if why:
            raise RuntimeError(why)
        if (self.out / "AUX_ARM").exists() or (self.out / "AUX_STOP").exists():
            raise RuntimeError("stale AUX control request before collector startup")
        self.jl = self.new_link()
        # No manual erase/UICR calls. Only the exact sealed main-flash image.
        self.jl.reset(halt=True)
        self.state["reset_count"] += 1
        self.jl.flash_file(str(self.image), 0x0)
        self.state["readback_before"] = self.verify_flash(self.jl)
        self.programmed = True
        why = self.external_stop()
        if why:
            raise RuntimeError(why)
        self.jl.reset(halt=False)
        self.target_started = True
        self.state["reset_count"] += 1
        self.state["target_started_at"] = utc()
        self.jl.rtt_start(block_address=self.rtt)
        self.rtt_started = True
        cb_deadline = min(self.deadline, time.monotonic() + 10.0)
        while True:
            try:
                if self.jl.rtt_get_num_up_buffers() > 1:
                    break
            except self.rtt_not_ready_exceptions:
                # Firmware may still be initializing the RTT control block.
                # Only JLinkRTTException is tolerated here, before capture.
                pass
            why = self.external_stop()
            if why:
                raise RuntimeError(why)
            if time.monotonic() >= cb_deadline:
                raise TimeoutError("AUX RTT control block not ready")
            time.sleep(.02)
        next_ram = 0.0
        while True:
            why = self.external_stop()
            if why:
                raise RuntimeError(why)
            if (self.out / "AUX_STOP").exists():
                self.request_stop(normal=True)
            if time.monotonic() >= self.deadline:
                raise TimeoutError("AUX collector 100-second watchdog")
            ready_before_drain = self.state["ready"]
            self.drain()
            if (self.state["ready"] and not ready_before_drain) or time.monotonic() >= next_ram:
                # READY can arrive between polls after a previous BOOT snapshot.
                # Validate its RAM state with a new read, never that stale sample.
                self.read_ram()
                next_ram = time.monotonic() + .1
            if self.state["end"] and not self.stop_sent:
                raise RuntimeError("AUX ended before host STOP; finite protection fired")
            if self.complete():
                break
            if self.last_ram.get("aux_state") == 4 or self.last_ram.get("aux_error_code", 0):
                raise RuntimeError("AUX firmware error")
            if self.state["ready"] and not self.arm_sent and not self.stop_sent:
                self.state["status"] = "READY"
                if self.last_ram.get("aux_state") != 1:
                    raise RuntimeError("AUX READY marker without unarmed READY RAM state")
                if (self.out / "AUX_ARM").exists():
                    if self.mode != "ON":
                        raise RuntimeError("AUX ARM requested in sealed OFF condition")
                    # A fresh STOP wins even if it arrived after the first check.
                    why = self.external_stop()
                    if why or (self.out / "AUX_STOP").exists():
                        if why:
                            raise RuntimeError(why)
                        self.request_stop(normal=True)
                    else:
                        self.write_mailbox("aux_duration_ms", self.duration)
                        self.write_mailbox("aux_host_arm", ARM_MAGIC)
                        self.arm_sent = True
                        self.state.update(arm_count=1, arm_requested_at=utc(), status="RUNNING")
                        self.persist()
            time.sleep(.02)

    def cleanup(self):
        # Every controlled exit tries cooperative STOP before HALT. A failed
        # flash is not allowed to receive writes to unverified firmware RAM.
        if self.jl is not None and self.target_started:
            try:
                self.request_stop()
                until = time.monotonic() + 6.0
                next_ram = 0.0
                while time.monotonic() < until:
                    if self.rtt_started:
                        self.drain()
                    if time.monotonic() >= next_ram:
                        self.read_ram()
                        next_ram = time.monotonic() + .1
                    if self.complete():
                        break
                    time.sleep(.02)
                if not self.complete():
                    self.error("cooperative STOP did not produce END and RF-off command within 6 seconds")
            except BaseException as exc:
                self.error("cooperative STOP/RTT failure: " + repr(exc))
        if self.jl is not None:
            try:
                self.jl.halt()
                if not self.jl.halted():
                    raise RuntimeError("AUX did not halt")
                self.state["halted_before_close"] = True
                n = self.last_ram.get("aux_tx_timestamp_count")
                if not isinstance(n, int) or n > 45000:
                    raise RuntimeError("AUX timestamp count unavailable at HALT")
                address = number(self.job["symbols"]["aux_tx_timestamp_hi32"]["address"])
                import struct
                target = self.out / "AUX_TX_RMARKER_HI32.bin"
                with target.open("xb") as handle:
                    for offset in range(0, n, 512):
                        words = self.jl.memory_read32(address + offset*4, min(512, n-offset))
                        if len(words) != min(512, n-offset):
                            raise RuntimeError("short AUX timestamp readback")
                        handle.write(struct.pack("<" + "I"*len(words), *words))
                    handle.flush()
                    os.fsync(handle.fileno())
                self.trace_readback = {"schema":"aux-tx-rmarker-hi32-v1", "count":n,
                                       "bytes":target.stat().st_size, "sha256":sha(target), "path":str(target)}
                self.state["timestamp_trace"] = self.trace_readback
            except BaseException as exc:
                self.error("final halt failure: " + repr(exc))
            try:
                self.state["readback_after"] = self.verify_flash(self.jl)
            except BaseException as exc:
                self.error("final readback failure: " + repr(exc))
            try:
                if self.rtt_started:
                    self.jl.rtt_stop()
            except BaseException as exc:
                self.error("RTT stop failure: " + repr(exc))
            try:
                self.jl.close()
            except BaseException as exc:
                self.error("probe close failure: " + repr(exc))
            self.jl = None
            # One read-only reconnect verifies halt retention; no reset/resume.
            verification = None
            try:
                verification = self.new_link()
                self.state["halted_after_reconnect"] = bool(verification.halted())
                if not self.state["halted_after_reconnect"]:
                    verification.halt()
                    raise RuntimeError("AUX halt was not retained across reconnect")
                self.state["reconnect_verified_at"] = utc()
            except BaseException as exc:
                self.error("halt reconnect verification failure: " + repr(exc))
            finally:
                if verification is not None:
                    verification.close()

    def finalize(self):
        why = self.external_stop()
        if why:
            self.error(why)
        if not self.normal_stop:
            self.error("normal host AUX_STOP not observed")
        if self.marker_counts["READY"] != 1 or self.marker_counts["END"] != 1:
            self.error("exactly one AUX READY and END required")
        ram = self.last_ram
        if not self.complete() or ram.get("aux_state") != 3 or ram.get("aux_end_reason") != 2:
            self.error("normal END state/reason/RF-off-command evidence missing")
        if not self.trace_readback or self.trace_readback["count"] != ram.get("aux_tx_count"):
            self.error("actual AUX TX timestamp trace missing or count mismatch")
        for name in ("aux_error_code", "aux_tx_timeout_count", "aux_schedule_overrun_count"):
            if ram.get(name) != 0:
                self.error("nonzero or missing " + name)
        end_fields = self.marker_records.get("END", {})
        attempted, transmitted = ram.get("aux_tx_attempts"), ram.get("aux_tx_count")
        unconfirmed = attempted - transmitted if isinstance(attempted, int) and isinstance(transmitted, int) else None
        pending_at_stop = (unconfirmed == 1 and self.mode == "ON" and self.normal_stop
                           and self.stop_sent and self.arm_sent and ram.get("aux_state") == 3
                           and ram.get("aux_end_reason") == 2
                           and all(ram.get(name) == 0 for name in
                                   ("aux_error_code", "aux_tx_timeout_count", "aux_schedule_overrun_count"))
                           and all(end_fields.get(name) == "0" for name in
                                   ("spi_transfer", "spi_state", "spi_timeout")))
        # Cooperative STOP may force off one packet before its TXFRS arrives.
        # Preserve that unconfirmed attempt; never promote it to TX success.
        self.state["tx_attempts_without_txfrs"] = unconfirmed
        self.state["tx_pending_unconfirmed_at_stop"] = pending_at_stop
        if unconfirmed != 0 and not pending_at_stop:
            self.error("AUX attempted/successful TX counts differ beyond a normal STOP interruption")
        bindings = {"reason": "aux_end_reason", "error": "aux_error_code",
                    "tx": "aux_tx_count", "attempts": "aux_tx_attempts",
                    "elapsed_us": "aux_elapsed_us", "timeout": "aux_tx_timeout_count",
                    "overrun": "aux_schedule_overrun_count", "rf_off_commanded": "aux_rf_off_commanded"}
        for field, name in bindings.items():
            try:
                valid = number(end_fields[field]) == ram.get(name)
            except (KeyError, ValueError, TypeError):
                valid = False
            if not valid:
                self.error("AUX END/RAM mismatch: " + field)
        for field in ("spi_transfer", "spi_state", "spi_timeout"):
            if end_fields.get(field) != "0":
                self.error("AUX END nonzero or missing " + field)
        if self.mode == "OFF":
            if self.arm_sent or self.marker_counts["START"] or ram.get("aux_tx_count") != 0 or ram.get("aux_tx_attempts") != 0:
                self.error("OFF requires no ARM, START, TX attempt, or TX success")
        elif not self.arm_sent or self.marker_counts["START"] != 1 or ram.get("aux_tx_count", 0) <= 0:
            self.error("ON requires one ARM/START and positive successful TX count")
        for key in ("readback_before", "readback_after"):
            if self.state.get(key, {}).get("status") != "PASS":
                self.error("missing full HEX " + key)
        if not self.state.get("halted_before_close") or not self.state.get("halted_after_reconnect"):
            self.error("final halt/reconnect proof missing")
        self.state.update(status="FAIL" if self.errors else "PASS", running=False,
                          finished_at=utc(), raw_sha256=sha(self.raw_path),
                          raw_path=str(self.raw_path), marker_counts=dict(self.marker_counts),
                          marker_records=self.marker_records,
                          firmware_elf_sha256=self.job["elf_sha256"],
                          suite_conditions_sha256=self.case["conditions_sha256"],
                          host_normal_stop=self.normal_stop,
                          rf_off_evidence="firmware force-off command plus final MCU halt; not independent RF measurement")
        self.persist()
        atomic_json(self.meta_path, self.state)
        return 1 if self.errors else 0

    def run(self):
        if self.factory is None:
            import pylink
            self.factory = pylink.JLink
            self.interface = pylink.enums.JLinkInterfaces.SWD
            self.rtt_not_ready_exceptions = (pylink.errors.JLinkRTTException,)
        self.out.mkdir(exist_ok=True)
        if self.status_path.exists() or self.meta_path.exists():
            raise RuntimeError("used AUX output; never retry in place")
        with self.raw_path.open("xb") as raw, (self.out / "AUX.progress.jsonl").open("x") as progress:
            self.raw, self.progress = raw, progress
            self.persist()
            try:
                self.capture()
            except BaseException as exc:
                self.error(repr(exc))
            finally:
                try:
                    self.cleanup()
                except BaseException as exc:
                    self.error("cleanup failure: " + repr(exc))
                raw.flush()
                os.fsync(raw.fileno())
            return self.finalize()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bundle", type=Path, required=True)
    ap.add_argument("--index", required=True)
    args = ap.parse_args()
    root = args.bundle.resolve()
    validated = validate(root, args.index)
    collector = Collector(root, args.index, validated)
    def interrupted(sig, _frame):
        collector.signal_error = "interrupted signal " + str(sig)
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, interrupted)
    return collector.run()


if __name__ == "__main__":
    sys.exit(main())
