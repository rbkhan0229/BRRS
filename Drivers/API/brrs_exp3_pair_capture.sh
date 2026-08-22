#!/usr/bin/env bash

# Run Experiment 3 with two J-Link probes attached to one computer.
# TX is started first; RX starts only after the TX capture reports READY.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<EOF
Usage:
  $(basename "$0") <environment> [distance] [options]

Options:
  --lead <us>          RX lead margin (default: 15).
  --payload <bytes>    App payload size, 1-117 (default: 16). Sweeping this
                        across runs traces the Reed-Solomon block-boundary
                        step in airtime.
  --run-start <N>      First run number (default: 1).
  --repeats <N>        Number of A/B/C repetitions (default: 1).
  --variants <list>    Variant order (default: A,B,C).
  --tx-serial <S/N>    TX J-Link. Auto-assigned when exactly two are attached.
  --rx-serial <S/N>    RX J-Link. Auto-assigned when exactly two are attached.
  --ready-timeout <s>  Maximum wait for TX READY (default: 30).
  --timeout <s>        Per-board capture timeout (default: 180).
  --case-retries <N>   Retry a variant until 1000 TX captures (default: 5).
  --no-build           Reuse images already built for the selected lead.
  --force              Preserve existing logs and rerun the same cases.
  -h, --help           Show this help.

Example:
  $(basename "$0") home 1 --lead 11 --repeats 2
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi
(( $# >= 1 )) || { usage >&2; exit 2; }

ENVIRONMENT="$1"
shift

DISTANCE="na"
LEAD_US=15
PAYLOAD_BYTES=16
RUN_START=1
REPEATS=1
VARIANT_SPEC="A,B,C"
TX_SERIAL=""
RX_SERIAL=""
READY_TIMEOUT=30
CAPTURE_TIMEOUT=180
CASE_RETRIES=5
NO_BUILD=0
FORCE=0

if (( $# > 0 )) && [[ "$1" != --* ]]; then
    DISTANCE="$1"
    shift
fi

while (( $# > 0 )); do
    case "$1" in
        --lead) LEAD_US="$2"; shift 2 ;;
        --payload) PAYLOAD_BYTES="$2"; shift 2 ;;
        --run-start) RUN_START="$2"; shift 2 ;;
        --repeats) REPEATS="$2"; shift 2 ;;
        --variants) VARIANT_SPEC="$2"; shift 2 ;;
        --tx-serial) TX_SERIAL="$2"; shift 2 ;;
        --rx-serial) RX_SERIAL="$2"; shift 2 ;;
        --ready-timeout) READY_TIMEOUT="$2"; shift 2 ;;
        --timeout) CAPTURE_TIMEOUT="$2"; shift 2 ;;
        --case-retries) CASE_RETRIES="$2"; shift 2 ;;
        --no-build) NO_BUILD=1; shift ;;
        --force) FORCE=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[[ "${ENVIRONMENT}" =~ ^[A-Za-z0-9._-]+$ ]] \
    || { echo "invalid environment name" >&2; exit 2; }
[[ "${DISTANCE}" == "na" || "${DISTANCE}" =~ ^[0-9]+([.][0-9]+)?$ ]] \
    || { echo "distance must be a non-negative number" >&2; exit 2; }
[[ "${LEAD_US}" =~ ^[0-9]+$ ]] && (( LEAD_US <= 1000 )) \
    || { echo "lead must be between 0 and 1000 us" >&2; exit 2; }
[[ "${PAYLOAD_BYTES}" =~ ^[0-9]+$ ]] && (( PAYLOAD_BYTES >= 1 && PAYLOAD_BYTES <= 117 )) \
    || { echo "payload must be between 1 and 117 bytes" >&2; exit 2; }
for value in "${RUN_START}" "${REPEATS}" "${READY_TIMEOUT}" \
             "${CAPTURE_TIMEOUT}" "${CASE_RETRIES}"; do
    [[ "${value}" =~ ^[1-9][0-9]*$ ]] \
        || { echo "run, repeats, and timeouts must be positive integers" >&2; exit 2; }
done

VARIANTS=()
old_ifs="${IFS}"
IFS=',' read -r -a VARIANTS <<<"${VARIANT_SPEC}"
IFS="${old_ifs}"
(( ${#VARIANTS[@]} > 0 )) || { echo "empty variant list" >&2; exit 2; }
for index in "${!VARIANTS[@]}"; do
    VARIANTS[index]="$(printf '%s' "${VARIANTS[index]}" | tr '[:lower:]' '[:upper:]')"
    case "${VARIANTS[index]}" in
        A|B|C) ;;
        *) echo "variant must be A, B, or C" >&2; exit 2 ;;
    esac
done

if [[ -z "${TX_SERIAL}" || -z "${RX_SERIAL}" ]]; then
    PROBES=()
    while IFS= read -r serial; do
        [[ -n "${serial}" ]] && PROBES+=("${serial}")
    done < <(python3 - <<'PY'
import pylink
for probe in pylink.JLink().connected_emulators():
    print(probe.SerialNumber)
PY
)
    (( ${#PROBES[@]} == 2 )) \
        || { echo "auto-assignment requires exactly two connected J-Links" >&2; exit 1; }
    TX_SERIAL="${TX_SERIAL:-${PROBES[0]}}"
    if [[ -z "${RX_SERIAL}" ]]; then
        if [[ "${PROBES[0]}" == "${TX_SERIAL}" ]]; then
            RX_SERIAL="${PROBES[1]}"
        else
            RX_SERIAL="${PROBES[0]}"
        fi
    fi
fi
[[ "${TX_SERIAL}" =~ ^[0-9]+$ && "${RX_SERIAL}" =~ ^[0-9]+$ ]] \
    || { echo "J-Link serials must be numeric" >&2; exit 2; }
[[ "${TX_SERIAL}" != "${RX_SERIAL}" ]] \
    || { echo "TX and RX must use different J-Link serials" >&2; exit 2; }

echo "EXP3_PAIR_ASSIGNMENT,tx=${TX_SERIAL},rx=${RX_SERIAL},lead_us=${LEAD_US}"

if (( NO_BUILD == 0 )); then
    "${SCRIPT_DIR}/brrs_exp3_build_all.sh" "${LEAD_US}" "${PAYLOAD_BYTES}"
fi

TX_PID=""
TX_CONSOLE=""
cleanup() {
    if [[ -n "${TX_PID}" ]] && kill -0 "${TX_PID}" 2>/dev/null; then
        kill "${TX_PID}" 2>/dev/null || true
        wait "${TX_PID}" 2>/dev/null || true
    fi
    [[ -z "${TX_CONSOLE}" ]] || rm -f "${TX_CONSOLE}"
}
trap cleanup EXIT INT TERM

run_pair() {
    local variant="$1"
    local run="$2"
    local attempt="$3"
    local tx_rc rx_rc deadline
    local common=("${variant}" "${run}" "${ENVIRONMENT}")
    [[ "${DISTANCE}" == "na" ]] || common+=("${DISTANCE}")

    TX_CONSOLE="$(mktemp "${TMPDIR:-/tmp}/brrs-exp3-tx.XXXXXX")"
    tx_cmd=("${SCRIPT_DIR}/brrs_exp3_capture.sh" tx "${common[@]}"
        --lead "${LEAD_US}" --payload "${PAYLOAD_BYTES}"
        --serial "${TX_SERIAL}" --no-build
        --timeout "${CAPTURE_TIMEOUT}")
    rx_cmd=("${SCRIPT_DIR}/brrs_exp3_capture.sh" rx "${common[@]}"
        --lead "${LEAD_US}" --payload "${PAYLOAD_BYTES}"
        --serial "${RX_SERIAL}" --no-build
        --timeout "${CAPTURE_TIMEOUT}")
    if (( FORCE || attempt > 1 )); then
        tx_cmd+=(--force)
        rx_cmd+=(--force)
    fi

    echo "PAIR_CASE_START,variant=${variant},run=${run},attempt=${attempt}/${CASE_RETRIES}"
    "${tx_cmd[@]}" >"${TX_CONSOLE}" 2>&1 &
    TX_PID=$!

    deadline=$((SECONDS + READY_TIMEOUT))
    while ! grep -q 'READY marker seen' "${TX_CONSOLE}" 2>/dev/null; do
        if ! kill -0 "${TX_PID}" 2>/dev/null; then
            wait "${TX_PID}" || tx_rc=$?
            cat "${TX_CONSOLE}"
            echo "PAIR_CASE_FAIL,variant=${variant},run=${run},phase=tx_start,rc=${tx_rc:-1}" >&2
            TX_PID=""
            rm -f "${TX_CONSOLE}"
            TX_CONSOLE=""
            return 1
        fi
        if (( SECONDS >= deadline )); then
            cat "${TX_CONSOLE}"
            echo "PAIR_CASE_FAIL,variant=${variant},run=${run},phase=tx_ready_timeout" >&2
            kill "${TX_PID}" 2>/dev/null || true
            wait "${TX_PID}" 2>/dev/null || true
            TX_PID=""
            rm -f "${TX_CONSOLE}"
            TX_CONSOLE=""
            return 1
        fi
        sleep 0.1
    done
    echo "PAIR_TX_READY,variant=${variant},run=${run},serial=${TX_SERIAL}"

    set +e
    "${rx_cmd[@]}"
    rx_rc=$?
    wait "${TX_PID}"
    tx_rc=$?
    set -e
    TX_PID=""
    cat "${TX_CONSOLE}"
    rm -f "${TX_CONSOLE}"
    TX_CONSOLE=""

    if (( tx_rc != 0 || rx_rc != 0 )); then
        echo "PAIR_CASE_FAIL,variant=${variant},run=${run},tx_rc=${tx_rc},rx_rc=${rx_rc}" >&2
        return 1
    fi
    echo "PAIR_CASE_PASS,variant=${variant},run=${run}"
}

for (( offset=0; offset<REPEATS; offset++ )); do
    run=$((RUN_START + offset))
    for variant in "${VARIANTS[@]}"; do
        case_pass=0
        for (( attempt=1; attempt<=CASE_RETRIES; attempt++ )); do
            if run_pair "${variant}" "${run}" "${attempt}"; then
                case_pass=1
                break
            fi
            if (( attempt < CASE_RETRIES )); then
                echo "PAIR_CASE_RETRY,variant=${variant},run=${run},next_attempt=$((attempt + 1))"
                sleep 2
            fi
        done
        (( case_pass == 1 )) \
            || { echo "PAIR_SUITE_DONE,environment=${ENVIRONMENT},status=FAIL" >&2; exit 1; }
    done
done

echo "PAIR_SUITE_DONE,environment=${ENVIRONMENT},repeats=${REPEATS},status=PASS"
