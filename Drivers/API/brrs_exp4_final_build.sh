#!/usr/bin/env bash

# Build the frozen BRRS Exp4 final-evaluation firmware configuration.

set -euo pipefail

usage() {
    echo "Usage: $0 <32|64|128|256> <sensor-count:1..7> [all|tx|init|N2..N8]"
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi
if (( $# < 2 || $# > 3 )); then
    usage >&2
    exit 2
fi

PREAMBLE="$1"
SENSOR_COUNT="$2"
ROLE="${3:-all}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Frozen final-evaluation configuration. Diagnostic profiling and IRQ flags
# are intentionally absent. Do not add experimental flags to this wrapper;
# create a separately named diagnostic build instead.
exec "${SCRIPT_DIR}/brrs_exp4_build.sh" \
    "${PREAMBLE}" "${SENSOR_COUNT}" 200 "${ROLE}" 15 \
    --pac 8 \
    --sync-buffer 1703 \
    --sync-prep 2002 \
    --cycles 1000 \
    --spi-opt \
    --phy-fast-switch
