#!/usr/bin/env bash

set -euo pipefail

script_name="$(basename "$0")"

usage() {
    printf '%s\n' \
        "Usage:" \
        "  ${script_name} <32|64|128|256> <sensor-count:1..7> <init|N2..N8> [log-file] [guard-us]" \
        "" \
        "Examples:" \
        "  ${script_name} 32 7 N5 ~/Desktop/DWM3000/result4/exp4_32_s7_N5.log 50" \
        "  ${script_name} 32 7 init ~/Desktop/DWM3000/result4/exp4_32_s7_init.log 50"
}

if (( $# < 3 || $# > 5 )); then
    usage
    exit 2
fi

plen="$1"
sensor_count="$2"
role="$3"
log_file="${4:-}"
guard_us="${5:-100}"

case "${plen}" in
    32|64|128|256) ;;
    *) echo "ERROR: preamble must be 32, 64, 128, or 256 symbols" >&2; exit 2 ;;
esac

if ! [[ "${sensor_count}" =~ ^[1-7]$ ]]; then
    echo "ERROR: sensor-count must be between 1 and 7" >&2
    exit 2
fi

if ! [[ "${guard_us}" =~ ^[0-9]+$ ]] || (( guard_us > 1000 )); then
    echo "ERROR: guard-us must be an integer between 0 and 1000" >&2
    exit 2
fi

if [[ "${role}" == "init" ]]; then
    image_role="init"
elif [[ "${role}" =~ ^N([2-8])$ ]]; then
    node="${BASH_REMATCH[1]}"
    if (( node > sensor_count + 1 )); then
        echo "ERROR: ${role} is outside a ${sensor_count}-sensor run" >&2
        exit 2
    fi
    image_role="N${node}"
else
    echo "ERROR: role must be init or N2 through N8" >&2
    exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
hex_dir="${script_dir}/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4/plen${plen}_sensors${sensor_count}"
if (( guard_us != 100 )); then
    hex_dir+="_guard${guard_us}"
fi
hex_file="${hex_dir}/exp4_${plen}_s${sensor_count}_${image_role}.hex"

jlink="${JLINK:-}"
rtt_logger="${RTT_LOGGER:-}"

if [[ -z "${jlink}" ]]; then
    jlink="$(command -v JLinkExe 2>/dev/null || true)"
fi
if [[ -z "${jlink}" && -x "/opt/SEGGER/JLink/JLinkExe" ]]; then
    jlink="/opt/SEGGER/JLink/JLinkExe"
fi

if [[ -z "${rtt_logger}" ]]; then
    rtt_logger="$(command -v JLinkRTTLogger 2>/dev/null || true)"
fi
if [[ -z "${rtt_logger}" ]]; then
    rtt_logger="$(command -v JLinkRTTLoggerExe 2>/dev/null || true)"
fi
if [[ -z "${rtt_logger}" && -x "/opt/SEGGER/JLink/JLinkRTTLoggerExe" ]]; then
    rtt_logger="/opt/SEGGER/JLink/JLinkRTTLoggerExe"
fi

if [[ ! -f "${hex_file}" ]]; then
    echo "ERROR: firmware not found: ${hex_file}" >&2
    echo "Run Drivers/API/brrs_exp4_build.sh ${plen} ${sensor_count} ${guard_us} first." >&2
    exit 1
fi

if [[ -z "${jlink}" ]]; then
    echo "ERROR: JLinkExe was not found in PATH" >&2
    exit 1
fi

cmd_file="$(mktemp "/tmp/brrs_exp4_${plen}_${image_role}.XXXXXX.jlink")"
trap 'rm -f "${cmd_file}"' EXIT

{
    printf '%s\n' \
        "r" \
        "loadfile ${hex_file}" \
        "r" \
        "g" \
        "exit"
} >"${cmd_file}"

printf '%s\n' \
    "Flashing Experiment 4 ${plen} sym / ${sensor_count} sensors / ${image_role}:" \
    "  ${hex_file}"

"${jlink}" \
    -Device NRF52840_XXAA \
    -If SWD \
    -Speed 4000 \
    -CommandFile "${cmd_file}"

echo "Firmware is running."

if [[ -z "${log_file}" ]]; then
    echo "No log file was supplied, so RTT logging was not started."
    exit 0
fi

if [[ -z "${rtt_logger}" ]]; then
    echo "ERROR: JLinkRTTLogger was not found in PATH" >&2
    exit 1
fi

mkdir -p "$(dirname "${log_file}")"
printf '%s\n' \
    "Capturing RTT channel 1:" \
    "  ${log_file}" \
    "Stop after EXP4_DONE (INIT) or EXP4_TX_DONE (sensor) appears."

"${rtt_logger}" \
    -Device NRF52840_XXAA \
    -If SWD \
    -Speed 4000 \
    -RTTChannel 1 \
    "${log_file}"
