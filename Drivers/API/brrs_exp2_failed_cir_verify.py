#!/usr/bin/env python3
"""Strictly verify optional Exp2 failed-CIR candidate records.

This checks log completeness and provenance only. It does not certify that an
error-path accumulator belongs to the intended transmitter or is a channel CIR.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

CAUSES = {"fwto", "pto", "sfdto", "phe", "fce", "fsl", "other"}
HEX_TAP = re.compile(r"^[0-9A-Fa-f]{12}$")
STATUS = re.compile(r"^0x[0-9A-Fa-f]{8}$")
RXPRD, CIADONE, CIAERR = 0x100, 0x400, 0x40000
CAUSE_BITS = {"fwto": 0x20000, "pto": 0x200000, "sfdto": 0x4000000,
              "phe": 0x1000, "fce": 0x8000, "fsl": 0x10000}


class DiagnosticLogError(ValueError):
    pass


def kv(line, prefix):
    if not line.startswith(prefix):
        raise DiagnosticLogError(f"expected {prefix}")
    fields = {}
    for part in line[len(prefix):].split(","):
        key, sep, value = part.partition("=")
        if not sep or not key or key in fields:
            raise DiagnosticLogError(f"malformed or repeated key: {line}")
        fields[key] = value
    return fields


def integer(fields, key, low=0):
    try:
        value = int(fields[key])
    except (KeyError, ValueError) as exc:
        raise DiagnosticLogError(f"invalid {key}") from exc
    if value < low:
        raise DiagnosticLogError(f"{key} below {low}")
    return value


def signed24(triple):
    value = int.from_bytes(triple, "little")
    return value - (1 << 24) if value & (1 << 23) else value


def verify(text):
    lines = [line.strip() for line in text.splitlines()]
    def unique(prefix):
        matches = [(i, line) for i, line in enumerate(lines) if line.startswith(prefix)]
        if len(matches) != 1:
            raise DiagnosticLogError(f"expected one {prefix}, found {len(matches)}")
        return matches[0]

    start_i, start_line = unique("FAIL_CIR_DUMP_START,")
    done_i, done_line = unique("FAIL_CIR_DUMP_DONE,")
    exp_i, exp_line = unique("EXP2_DONE,")
    config_i, config_line = unique("EXP_LOG_CONFIG_CSV,")
    if not config_i < start_i < done_i < exp_i:
        raise DiagnosticLogError("diagnostic dump or EXP2_DONE marker out of order")
    start = kv(start_line, "FAIL_CIR_DUMP_START,")
    done = kv(done_line, "FAIL_CIR_DUMP_DONE,")
    config = kv(config_line, "EXP_LOG_CONFIG_CSV,")
    if start.get("version") != "1" or start.get("experiment") != "2" or start.get("scope") != "candidate_only":
        raise DiagnosticLogError("unsupported failed-CIR format")
    if done.get("version") != "1" or config.get("experiment") != "2":
        raise DiagnosticLogError("version/experiment mismatch")
    samples = integer(start, "samples", 1)
    if samples != integer(done, "samples_per_frame", 1):
        raise DiagnosticLogError("sample count mismatch")
    maximum = integer(start, "max_total", 1)
    per_cause = integer(start, "max_per_cause", 1)
    plen = integer(config, "plen", 1)
    target = integer(config, "target", 1)
    if plen not in (32, 64, 128, 256) or samples != 64:
        raise DiagnosticLogError("not an Exp2 64-tap diagnostic")
    captured = integer(done, "captured")
    events = integer(done, "events")
    skips = {k: integer(done, k) for k in
             ("no_preamble", "invalid_accum", "no_ciadone", "cia_error", "invalid_fp", "capped")}
    if events != captured + sum(skips.values()) or captured > maximum:
        raise DiagnosticLogError("event accounting mismatch")

    headers = {}
    taps = {}
    cause_counts = {}
    for line in lines[start_i + 1:done_i]:
        if line.startswith("FAIL_CIR_HEADER,"):
            h = kv(line, "FAIL_CIR_HEADER,")
            ident = integer(h, "id", 1)
            cycle = integer(h, "cycle", 1)
            accum = integer(h, "accum", 1)
            fp = integer(h, "fp_sample", 1)
            offset = integer(h, "sample_offset")
            if ident in headers or ident > captured or cycle > target or accum > plen:
                raise DiagnosticLogError("duplicate/out-of-range frame identity")
            if h.get("node") != "N2" or h.get("cause") not in CAUSES:
                raise DiagnosticLogError("unexpected node/error class")
            if h.get("rxprd") != "1" or h.get("ciadone") != "1" or not STATUS.fullmatch(h.get("status", "")):
                raise DiagnosticLogError("invalid gate/status metadata")
            status = int(h["status"], 16)
            if (status & (RXPRD | CIADONE)) != (RXPRD | CIADONE) or status & CIAERR:
                raise DiagnosticLogError("raw status contradicts candidate gates")
            if h["cause"] in CAUSE_BITS and not status & CAUSE_BITS[h["cause"]]:
                raise DiagnosticLogError("raw status contradicts error cause")
            if offset != fp - 16 or integer(h, "n_samples") != samples:
                raise DiagnosticLogError("invalid CIR window metadata")
            headers[ident] = h
            taps[ident] = {}
            cause_counts[h["cause"]] = cause_counts.get(h["cause"], 0) + 1
        elif line.startswith("FAIL_CIR_RAW,"):
            parts = line.split(",")
            if len(parts) != 4:
                raise DiagnosticLogError("malformed tap row")
            try:
                ident, tap = int(parts[1]), int(parts[2])
            except ValueError as exc:
                raise DiagnosticLogError("noninteger tap identity") from exc
            if ident not in headers or not 0 <= tap < samples or tap in taps[ident]:
                raise DiagnosticLogError("orphan/duplicate/out-of-range tap")
            if not HEX_TAP.fullmatch(parts[3]):
                raise DiagnosticLogError("invalid 48-bit complex tap")
            taps[ident][tap] = bytes.fromhex(parts[3])
        else:
            raise DiagnosticLogError(f"unexpected line inside failed-CIR dump: {line[:80]}")
    if set(headers) != set(range(1, captured + 1)):
        raise DiagnosticLogError("missing failed-CIR header")
    if any(set(frame) != set(range(samples)) for frame in taps.values()):
        raise DiagnosticLogError("missing failed-CIR tap")
    if any(n > per_cause for n in cause_counts.values()):
        raise DiagnosticLogError("per-cause capture limit exceeded")
    frames = []
    for ident in sorted(headers):
        packed = b"".join(taps[ident][tap] for tap in range(samples))
        frames.append({"id": ident, "cycle": int(headers[ident]["cycle"]),
                       "cause": headers[ident]["cause"],
                       "accum": int(headers[ident]["accum"]),
                       "sample_offset": int(headers[ident]["sample_offset"]),
                       "all_zero": not any(packed),
                       "taps_sha256": hashlib.sha256(packed).hexdigest(),
                       "first_tap_iq": [signed24(packed[:3]), signed24(packed[3:6])]})
    return {"status": "COMPLETE_CANDIDATE_DIAGNOSTIC_NOT_CHANNEL_VALIDATION",
            "events_with_diagnostics": events, "captured": captured,
            "skipped": skips, "samples_per_frame": samples,
            "cause_counts": cause_counts, "frames": frames}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    text = args.log.read_text(errors="replace")
    report = verify(text)
    report["raw_log_sha256"] = hashlib.sha256(args.log.read_bytes()).hexdigest()
    output = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.out:
        args.out.write_text(output)
    else:
        print(output, end="")


if __name__ == "__main__":
    main()
