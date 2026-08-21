#!/usr/bin/env bash

# Friendly Stage0 wrapper around the Experiment 1 automatic capture engine.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<EOF
Usage:
  $(basename "$0") <tx|rx> <lead-us> <run> <environment> [distance] [options]

Options are the same as brrs_exp1_capture.sh. Use --tail <us> only for an
existing Stage0_L<lead>_T<tail>_Init build configuration. Use --pac <4|8>
to sweep the RX PAC size (default 8, the BRRS baseline; 4 is the DW3000
vendor-recommended value for preambles under 127 symbols).

Examples:
  $(basename "$0") tx 15 1 iron_door_nlos 6.9
  $(basename "$0") rx 15 1 iron_door_nlos 6.9
  $(basename "$0") rx 0  1 iron_door_nlos 6.9 --tail 100
  $(basename "$0") rx 15 1 iron_door_nlos 6.9 --pac 4
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi
if (( $# < 4 )); then
    usage >&2
    exit 2
fi

ROLE="$1"
LEAD_US="$2"
RUN_NUMBER="$3"
ENVIRONMENT="$4"
shift 4

exec "${SCRIPT_DIR}/brrs_exp1_capture.sh" \
    "${ROLE}" 32 "${RUN_NUMBER}" "${ENVIRONMENT}" "$@" \
    --stage0 --lead "${LEAD_US}"
