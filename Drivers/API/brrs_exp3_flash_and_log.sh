#!/usr/bin/env bash

set -euo pipefail

SCRIPT_NAME="$(basename "$0")"

usage() {
    printf '%s\n' \
        "Usage:" \
        "  ${SCRIPT_NAME} <A|B|C> <init|normal> [log_file]" \
        "" \
        "Examples:" \
        "  ${SCRIPT_NAME} A normal ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_A_tx.log" \
        "  ${SCRIPT_NAME} A init   ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_A_rx.log"
}

if (( $# < 2 || $# > 3 )); then
    usage
    exit 2
fi

VARIANT=$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')
ROLE=$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')
LOG_FILE=${3:-}

if [[ "${VARIANT}" != "A" && "${VARIANT}" != "B" && "${VARIANT}" != "C" ]]; then
    printf '%s\n' "ERROR: variant must be A, B, or C" >&2
    exit 2
fi

if [[ "${ROLE}" != "init" && "${ROLE}" != "normal" ]]; then
    printf '%s\n' "ERROR: role must be init or normal" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HEX_DIR="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/Output/exp3"
HEX_FILE="${HEX_DIR}/exp3_${VARIANT}_${ROLE}.hex"
JLINK=${JLINK:-}
RTT_LOGGER=${RTT_LOGGER:-}

if [[ -z "${JLINK}" ]]; then
    JLINK=$(command -v JLinkExe 2>/dev/null || true)
fi
if [[ -z "${JLINK}" && -x "/opt/SEGGER/JLink/JLinkExe" ]]; then
    JLINK="/opt/SEGGER/JLink/JLinkExe"
fi

if [[ -z "${RTT_LOGGER}" ]]; then
    RTT_LOGGER=$(command -v JLinkRTTLogger 2>/dev/null || true)
fi
if [[ -z "${RTT_LOGGER}" ]]; then
    RTT_LOGGER=$(command -v JLinkRTTLoggerExe 2>/dev/null || true)
fi
if [[ -z "${RTT_LOGGER}" && -x "/opt/SEGGER/JLink/JLinkRTTLoggerExe" ]]; then
    RTT_LOGGER="/opt/SEGGER/JLink/JLinkRTTLoggerExe"
fi

if [[ ! -f "${HEX_FILE}" ]]; then
    printf '%s\n' "ERROR: firmware not found: ${HEX_FILE}" >&2
    printf '%s\n' "Run Drivers/API/brrs_exp3_build_all.sh first." >&2
    exit 1
fi

if [[ -z "${JLINK}" ]]; then
    printf '%s\n' "ERROR: JLinkExe was not found in PATH" >&2
    exit 1
fi

CMD_FILE=$(mktemp "/tmp/brrs_exp3_${VARIANT}_${ROLE}.XXXXXX.jlink")
trap 'rm -f "${CMD_FILE}"' EXIT

{
    printf '%s\n' \
        "r" \
        "loadfile ${HEX_FILE}" \
        "r" \
        "g" \
        "exit"
} >"${CMD_FILE}"

printf '%s\n' \
    "Flashing Experiment 3 ${VARIANT}/${ROLE}:" \
    "  ${HEX_FILE}"

"${JLINK}" \
    -Device NRF52840_XXAA \
    -If SWD \
    -Speed 4000 \
    -CommandFile "${CMD_FILE}"

printf '\n%s\n' "Firmware is running."

if [[ -z "${LOG_FILE}" ]]; then
    printf '%s\n' "No log file was supplied, so RTT logging was not started."
    exit 0
fi

if [[ -z "${RTT_LOGGER}" ]]; then
    printf '%s\n' "ERROR: JLinkRTTLogger was not found in PATH" >&2
    exit 1
fi

mkdir -p "$(dirname "${LOG_FILE}")"
printf '%s\n' \
    "Capturing RTT channel 1:" \
    "  ${LOG_FILE}" \
    "Stop the logger after EXP3_TX_RESULT or EXP3_RX_DONE appears."

"${RTT_LOGGER}" \
    -Device NRF52840_XXAA \
    -If SWD \
    -Speed 4000 \
    -RTTChannel 1 \
    "${LOG_FILE}"
