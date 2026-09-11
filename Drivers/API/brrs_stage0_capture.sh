#!/usr/bin/env bash

# Friendly Stage0 wrapper around the Experiment 1 automatic capture engine.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<EOF
Usage:
  $(basename "$0") <tx|rx> <lead-us> <run> <environment> [distance] [options]

Options are the same as brrs_exp1_capture.sh. Use --tail <us> only for an
existing Stage0_L<lead>_T<tail>_Init build configuration. Use
--preamble 32 --pac <4|8> for the DATA profiles or
--preamble 1024 --pac 32 for the Exp5 acquisition calibration.

Examples:
  $(basename "$0") tx 15 1 iron_door_nlos 6.9
  $(basename "$0") rx 15 1 iron_door_nlos 6.9
  $(basename "$0") rx 0  1 iron_door_nlos 6.9 --tail 100
  $(basename "$0") rx 15 1 iron_door_nlos 6.9 --pac 4
  $(basename "$0") rx 25 1 vehicle --preamble 1024 --pac 32
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

PREAMBLE=32
FORWARD_ARGS=()
while (( $# > 0 )); do
    case "$1" in
        --preamble)
            (( $# >= 2 )) || { echo "--preamble requires a value" >&2; exit 2; }
            PREAMBLE="$2"; shift 2 ;;
        *) FORWARD_ARGS+=("$1"); shift ;;
    esac
done
case "${PREAMBLE}" in
    32|1024) ;;
    *) echo "Stage0 preamble must be 32 or 1024" >&2; exit 2 ;;
esac

exec "${SCRIPT_DIR}/brrs_exp1_capture.sh" \
    "${ROLE}" "${PREAMBLE}" "${RUN_NUMBER}" "${ENVIRONMENT}" "${FORWARD_ARGS[@]}" \
    --stage0 --lead "${LEAD_US}"
