#!/usr/bin/env python3
"""Match discovered J-Link probes to fixed vehicle experiment roles."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from brrs_suite_manifest import load as load_board_map, fixed_assignments


def parse_serials(spec: str) -> list[int]:
    serials: list[int] = []
    for token in spec.split(","):
        token = token.strip()
        if not token or not token.isdigit():
            raise ValueError(f"invalid J-Link serial: {token!r}")
        serials.append(int(token))
    return serials


def discover_serials() -> list[int]:
    try:
        import pylink
    except ImportError as exc:
        raise RuntimeError(
            "pylink-square is required: python3 -m pip install pylink-square"
        ) from exc

    probes = pylink.JLink().connected_emulators()
    return [int(probe.SerialNumber) for probe in probes]


def make_assignments(serials: list[int], sensors: int, run: int) -> list[tuple[str, int]]:
    ordered = sorted(set(serials))
    if len(ordered) != len(serials):
        raise ValueError("duplicate J-Link serial numbers were discovered")
    if len(ordered) != sensors:
        raise ValueError(
            f"expected exactly {sensors} TX probes, but found {len(ordered)}: "
            + (",".join(str(serial) for serial in ordered) or "none")
        )

    rotation = (run - 1) % sensors
    return [
        (f"N{role_index + 2}", ordered[(role_index + rotation) % sensors])
        for role_index in range(sensors)
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sensors", type=int, required=True, choices=range(1, 8))
    parser.add_argument("--run", type=int, required=True)
    parser.add_argument(
        "--serials",
        help="comma-separated override for diagnostics; default is USB auto-discovery",
    )
    parser.add_argument("--format", choices=("tsv", "csv", "human"), default="human")
    parser.add_argument("--board-map", default=str(Path(__file__).with_name('brrs_vehicle_manifest.json')),
                        help="Fixed board manifest; never sort or rotate roles.")
    args = parser.parse_args()

    if args.run < 1:
        parser.error("--run must be a positive integer")

    try:
        serials = parse_serials(args.serials) if args.serials else discover_serials()
        assignments = (fixed_assignments(load_board_map(args.board_map), args.sensors, serials)
                       if args.board_map else make_assignments(serials, args.sensors, args.run))
    except (RuntimeError, ValueError, KeyError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    rotation = 0 if args.board_map else (args.run - 1) % args.sensors
    if args.format == "tsv":
        for role, serial in assignments:
            print(f"{role}\t{serial}")
    elif args.format == "csv":
        for role, serial in assignments:
            print(
                "EXP4_PROBE_ASSIGNMENT_CSV,"
                f"run={args.run},rotation={rotation},role={role},serial={serial}"
            )
    else:
        print(
            f"Exp4 probe assignment: run={args.run}, sensors={args.sensors}, "
            f"rotation={rotation}"
        )
        for role, serial in assignments:
            print(f"  {role} <- J-Link {serial}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
