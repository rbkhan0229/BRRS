#!/usr/bin/env python3
"""Validate Stage0/Experiment 1 RTT logs without treating RF loss as log loss."""

import argparse
import re
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise ValueError(message)


def parse_kv_marker(text: str, prefix: str) -> dict[str, str]:
    lines = [line for line in text.splitlines() if line.startswith(prefix)]
    if not lines:
        fail(f"{prefix} marker not found")
    values: dict[str, str] = {}
    for field in lines[-1].split(",")[1:]:
        key, separator, value = field.partition("=")
        if not separator:
            fail(f"malformed field in {prefix}: {field!r}")
        values[key] = value
    return values


def integer(values: dict[str, str], key: str) -> int:
    value = values.get(key, "")
    if not re.fullmatch(r"[0-9]+", value):
        fail(f"missing or invalid {key}: {value!r}")
    return int(value)


def validate_rx(text: str, args: argparse.Namespace) -> str:
    values = parse_kv_marker(text, "EXP1_DONE,")
    plen = integer(values, "plen")
    lead = integer(values, "lead_us")
    tail = integer(values, "tail_us")
    pac = integer(values, "pac")
    expected = integer(values, "expected")
    received = integer(values, "rx")
    delayed_late = integer(values, "delayed_late")
    data_errors = integer(values, "data_config_errors")
    end_tx = integer(values, "end_tx")
    per_x1000 = integer(values, "per_x1000")

    if plen != args.preamble or lead != args.lead or tail != args.tail:
        fail(
            f"firmware parameters plen/lead/tail={plen}/{lead}/{tail}, "
            f"requested={args.preamble}/{args.lead}/{args.tail}"
        )
    if pac != args.pac:
        fail(f"firmware pac={pac}, requested={args.pac}")
    if expected != args.expected or received > expected:
        fail(f"invalid RX totals: rx={received}, expected={expected}")
    if delayed_late != 0 or data_errors != 0 or end_tx != 3:
        fail(
            f"RX schedule/config failure: delayed_late={delayed_late}, "
            f"data_config_errors={data_errors}, end_tx={end_tx}"
        )
    if values.get("collection") != "PASS" or values.get("status") != "PASS":
        fail(f"firmware collection failed: {values}")

    missed = expected - received
    calculated_per_x1000 = (missed * 100000 + expected // 2) // expected
    expected_link = "PASS" if missed == 0 else "LOSS"
    if per_x1000 != calculated_per_x1000 or values.get("link") != expected_link:
        fail("firmware PER/link fields are inconsistent with RX totals")

    per_lines = re.findall(
        r"^N2: rx=(\d+) expected=(\d+) miss=(\d+) "
        r"PER=([0-9.]+)% err=(\d+)$",
        text,
        flags=re.MULTILINE,
    )
    if not per_lines:
        fail("human-readable N2 PER line not found")
    human_rx, human_expected, human_missed, _, _ = per_lines[-1]
    if (int(human_rx), int(human_expected), int(human_missed)) != (
        received,
        expected,
        missed,
    ):
        fail("EXP1_DONE and N2 PER line disagree")

    per_percent = 100.0 * missed / expected
    return (
        f"collection=PASS; expected={expected}; rx={received}; "
        f"PER={per_percent:.3f}%; link={expected_link}; "
        f"lead={lead}us; tail={tail}us; plen={plen}; pac={pac}"
    )


def validate_tx(text: str, args: argparse.Namespace) -> str:
    values = parse_kv_marker(text, "EXP1_TX_DONE,")
    plen = integer(values, "plen")
    expected = integer(values, "expected")
    attempts = integer(values, "attempts")
    success = integer(values, "success")
    delayed_late = integer(values, "delayed_late")
    beacon_errors = integer(values, "beacon_config_errors")
    data_errors = integer(values, "data_config_errors")
    end_received = integer(values, "end")

    if plen != args.preamble or expected != args.expected:
        fail(
            f"firmware plen/expected={plen}/{expected}, "
            f"requested={args.preamble}/{args.expected}"
        )
    if not (0 < attempts <= expected) or success != attempts:
        fail(f"invalid TX totals: success={success}, attempts={attempts}")
    if (
        delayed_late != 0
        or beacon_errors != 0
        or data_errors != 0
        or end_received != 1
    ):
        fail(
            "TX schedule/config failure: "
            f"late={delayed_late}, beacon_errors={beacon_errors}, "
            f"data_errors={data_errors}, end={end_received}"
        )
    if values.get("collection") != "PASS" or values.get("status") != "PASS":
        fail(f"firmware collection failed: {values}")

    expected_link = "PASS" if attempts == expected else "LOSS"
    if values.get("link") != expected_link:
        fail("firmware TX link field is inconsistent with attempt count")

    beacon_lines = [
        line for line in text.splitlines() if line.startswith("BRRS_BEACON_RX_CSV,")
    ]
    if not beacon_lines or f",m={args.preamble}," not in beacon_lines[-1]:
        fail("received beacon does not advertise the requested preamble")

    match = re.findall(
        r"^My TX: success=(\d+) attempts=(\d+) delayed_late=(\d+)",
        text,
        flags=re.MULTILINE,
    )
    if not match or tuple(map(int, match[-1])) != (success, attempts, delayed_late):
        fail("EXP1_TX_DONE and human-readable TX totals disagree")

    return (
        f"collection=PASS; tx={success}/{expected}; link={expected_link}; "
        f"plen={plen}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--mode", choices=("stage0", "exp1"), required=True)
    parser.add_argument("--role", choices=("rx", "tx"), required=True)
    parser.add_argument("--preamble", type=int, required=True)
    parser.add_argument("--lead", type=int, required=True)
    parser.add_argument("--tail", type=int, required=True)
    parser.add_argument("--pac", type=int, default=8)
    parser.add_argument("--expected", type=int, default=2000)
    args = parser.parse_args()

    try:
        text = args.log.read_text(errors="replace")
        detail = validate_rx(text, args) if args.role == "rx" else validate_tx(text, args)
    except (OSError, ValueError) as exc:
        print(f"[verify] FAIL: {exc}", file=sys.stderr)
        return 3

    print(f"[verify] PASS: mode={args.mode}; {detail}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
