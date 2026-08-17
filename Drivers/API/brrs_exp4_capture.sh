#!/usr/bin/env bash

# Experiment 4: parameterized build -> flash -> PyLink RTT capture -> verify.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/dw3000_api.emProject"
OUTPUT_DIR="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/Output"

usage() {
    cat <<EOF
Usage:
  $(basename "$0") <init|N2..N8> <32|64|128|256> <sensor-count:1..7> <run> <environment> [distance] [options]

Options:
  --guard <us>         Inter-slot guard used by every board (default: 200).
  --serial <S/N>       Select a J-Link when multiple probes are attached.
  --no-build           Reuse an image previously made with the same parameters.
  --timeout <seconds>  Override capture timeout (INIT 90 s, sensor 180 s).
  --force              Preserve an existing log as .prev.<time> and retry.
  -h, --help           Show this help.

Examples:
  $(basename "$0") N2   32 2 1 iron_door_nlos 6.9
  $(basename "$0") N3   32 2 1 iron_door_nlos 6.9
  $(basename "$0") init 32 2 1 iron_door_nlos 6.9
  $(basename "$0") init 256 2 1 iron_door_nlos 6.9 --guard 200
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi
if (( $# < 5 )); then
    usage >&2
    exit 2
fi

ROLE_INPUT="$1"
PREAMBLE="$2"
SENSOR_COUNT="$3"
RUN_NUMBER="$4"
ENVIRONMENT="$5"
shift 5

DISTANCE="na"
GUARD_US=200
SERIAL=""
NO_BUILD=0
TIMEOUT=""
FORCE=0
if (( $# > 0 )) && [[ "$1" != --* ]]; then
    DISTANCE="$1"
    shift
fi
while (( $# > 0 )); do
    case "$1" in
        --guard)
            (( $# >= 2 )) || { echo "--guard requires a value" >&2; exit 2; }
            GUARD_US="$2"; shift 2
            ;;
        --serial)
            (( $# >= 2 )) || { echo "--serial requires a value" >&2; exit 2; }
            SERIAL="$2"; shift 2
            ;;
        --no-build) NO_BUILD=1; shift ;;
        --timeout)
            (( $# >= 2 )) || { echo "--timeout requires a value" >&2; exit 2; }
            TIMEOUT="$2"; shift 2
            ;;
        --force) FORCE=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

case "${PREAMBLE}" in
    32|64|128|256) ;;
    *) echo "preamble must be 32, 64, 128, or 256" >&2; exit 2 ;;
esac
[[ "${SENSOR_COUNT}" =~ ^[1-7]$ ]] \
    || { echo "sensor-count must be between 1 and 7" >&2; exit 2; }
[[ "${RUN_NUMBER}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "run must be a positive integer" >&2; exit 2; }
[[ "${ENVIRONMENT}" =~ ^[A-Za-z0-9._-]+$ ]] \
    || { echo "environment may contain only letters, numbers, dot, underscore, and hyphen" >&2; exit 2; }
if [[ "${DISTANCE}" != "na" && ! "${DISTANCE}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "distance must be a non-negative number in meters" >&2
    exit 2
fi
[[ "${GUARD_US}" =~ ^[0-9]+$ ]] && (( GUARD_US <= 1000 )) \
    || { echo "guard must be an integer between 0 and 1000 us" >&2; exit 2; }
if (( SENSOR_COUNT > 1 && GUARD_US < 12 )); then
    echo "multi-sensor guard must be at least the 12 us RX lead margin" >&2
    exit 2
fi

ROLE_LOWER="$(printf '%s' "${ROLE_INPUT}" | tr '[:upper:]' '[:lower:]')"
if [[ "${ROLE_LOWER}" == "init" ]]; then
    ROLE="init"
    ROLE_LABEL="INIT"
    IMAGE_ROLE="init"
    TIMEOUT="${TIMEOUT:-90}"
    VERIFY_ARGS=(--role init)
elif [[ "${ROLE_INPUT}" =~ ^[Nn]([2-8])$ ]]; then
    ROLE="sensor"
    NODE="${BASH_REMATCH[1]}"
    if (( NODE > SENSOR_COUNT + 1 )); then
        echo "N${NODE} is outside a ${SENSOR_COUNT}-sensor run" >&2
        exit 2
    fi
    ROLE_LABEL="N${NODE}"
    IMAGE_ROLE="N${NODE}"
    TIMEOUT="${TIMEOUT:-180}"
    VERIFY_ARGS=(--role sensor --node "${NODE}")
else
    echo "role must be init or N2 through N8" >&2
    exit 2
fi
[[ "${TIMEOUT}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "timeout must be a positive integer" >&2; exit 2; }

IMAGE_DIR="${OUTPUT_DIR}/Debug/Exe/exp4/plen${PREAMBLE}_sensors${SENSOR_COUNT}"
if (( GUARD_US != 100 )); then
    IMAGE_DIR+="_guard${GUARD_US}"
fi
IMAGE_BASE="exp4_${PREAMBLE}_s${SENSOR_COUNT}_${IMAGE_ROLE}"
HEX_FILE="${IMAGE_DIR}/${IMAGE_BASE}.hex"
ELF_FILE="${IMAGE_DIR}/${IMAGE_BASE}.elf"
CONFIG="Generated_Exp4_${PREAMBLE}_S${SENSOR_COUNT}_G${GUARD_US}_${ROLE_LABEL}"
DATE_TAG="$(date '+%Y%m%d')"
DISTANCE_TAG=""
[[ "${DISTANCE}" != "na" && -n "${DISTANCE}" ]] && DISTANCE_TAG="_${DISTANCE}m"
OUTDIR="${SDK_ROOT}/../logs/exp4_${ENVIRONMENT}${DISTANCE_TAG}_g${GUARD_US}_${DATE_TAG}"
mkdir -p "${OUTDIR}"

ROLE_TAG="$(printf '%s' "${ROLE_LABEL}" | tr '[:upper:]' '[:lower:]')"
BASE="exp4_${PREAMBLE}_s${SENSOR_COUNT}_r${RUN_NUMBER}_${ROLE_TAG}"
RAW_LOG="${OUTDIR}/${BASE}.log"
META_FILE="${OUTDIR}/${BASE}.meta.txt"
BUILD_LOG="${OUTDIR}/${BASE}.build.log"
if [[ -e "${RAW_LOG}" ]]; then
    if (( FORCE )); then
        BACKUP_TAG="$(date '+%H%M%S')"
        for artifact in "${RAW_LOG}" "${META_FILE}" "${BUILD_LOG}"; do
            if [[ -e "${artifact}" ]]; then
                mv "${artifact}" "${artifact}.prev.${BACKUP_TAG}"
            fi
        done
        echo "[log] existing artifacts preserved with .prev.${BACKUP_TAG}"
    else
        echo "refusing to overwrite ${RAW_LOG}" >&2
        echo "  (retry with --force or increment the run number)" >&2
        exit 1
    fi
fi

find_executable() {
    local name="$1"
    shift
    if command -v "${name}" >/dev/null 2>&1; then
        command -v "${name}"
        return 0
    fi
    local candidate root
    for candidate in "$@"; do
        [[ -x "${candidate}" ]] && { printf '%s\n' "${candidate}"; return 0; }
    done
    for root in "/Applications/SEGGER" "/opt/SEGGER" "/usr/share" \
                "/usr/local" "/opt/homebrew" "${HOME}/SEGGER" \
                "${HOME}/Downloads" "${HOME}/JLink"*; do
        [[ -d "${root}" ]] || continue
        candidate="$(find "${root}" -maxdepth 6 -type f -name "${name}" \
            -perm -111 -print -quit 2>/dev/null || true)"
        [[ -n "${candidate}" ]] && { printf '%s\n' "${candidate}"; return 0; }
    done
    return 1
}

EMBUILD="${EMBUILD:-$(find_executable emBuild \
    "/Applications/SEGGER/SEGGER Embedded Studio 8.28/bin/emBuild" \
    "/opt/SEGGER/EmbeddedStudio/bin/emBuild" \
    "/usr/share/segger_embedded_studio/bin/emBuild" || true)}"
ARM_NM="${ARM_NM:-$(find_executable arm-none-eabi-nm \
    "/opt/homebrew/bin/arm-none-eabi-nm" \
    "/usr/local/bin/arm-none-eabi-nm" \
    "/usr/bin/arm-none-eabi-nm" || true)}"

if (( NO_BUILD == 0 )) && [[ ! -x "${EMBUILD:-}" ]]; then
    echo "ERROR: emBuild not found. Set EMBUILD to its full path." >&2
    exit 1
fi
[[ -x "${ARM_NM:-}" ]] \
    || { echo "ERROR: arm-none-eabi-nm not found. Set ARM_NM to its full path." >&2; exit 1; }

echo "[${ROLE_LABEL}] Experiment 4: ${PREAMBLE} sym, S${SENSOR_COUNT}"
echo "  Configuration: ${CONFIG}"
echo "  Guard:         ${GUARD_US} us"
echo "  Run:           ${RUN_NUMBER}"
echo "  Environment:   ${ENVIRONMENT}"
echo "  Distance:      ${DISTANCE}"
echo "  Raw log:       ${RAW_LOG}"

if (( NO_BUILD == 0 )); then
    echo "[build] Exp4 ${PREAMBLE} sym / S${SENSOR_COUNT} / guard ${GUARD_US} us"
    EMBUILD="${EMBUILD}" "${SCRIPT_DIR}/brrs_exp4_build.sh" \
        "${PREAMBLE}" "${SENSOR_COUNT}" "${GUARD_US}" "${IMAGE_ROLE}" \
        >"${BUILD_LOG}" 2>&1 \
        || { echo "build failed: ${BUILD_LOG}" >&2; exit 1; }
fi
[[ -f "${HEX_FILE}" && -f "${ELF_FILE}" ]] \
    || { echo "firmware image missing for ${CONFIG}" >&2; exit 1; }

RTT_SYMBOL="$("${ARM_NM}" -n "${ELF_FILE}" \
    | awk '$3 == "_SEGGER_RTT" { print $1; exit }')"
[[ "${RTT_SYMBOL}" =~ ^[0-9A-Fa-f]+$ ]] \
    || { echo "_SEGGER_RTT not found in ELF" >&2; exit 1; }
RTT_ADDR="0x${RTT_SYMBOL}"
echo "[rtt] control block @ ${RTT_ADDR}"

PYLINK_ARGS=(
    python3 "${SCRIPT_DIR}/rtt_capture.py"
    --hex "${HEX_FILE}"
    --rtt-address "${RTT_ADDR}"
    --channel 1
    --ready-marker "EXP_LOG_READY,channel=1"
    --end-marker "===== END STATS ====="
    --timeout "${TIMEOUT}"
    --out "${RAW_LOG}"
)
[[ -n "${SERIAL}" ]] && PYLINK_ARGS+=(--serial "${SERIAL}")
"${PYLINK_ARGS[@]}"

VERIFY_OUTPUT="$(python3 "${SCRIPT_DIR}/brrs_exp4_verify.py" "${RAW_LOG}" \
    "${VERIFY_ARGS[@]}" --preamble "${PREAMBLE}" \
    --sensors "${SENSOR_COUNT}" --guard "${GUARD_US}")"
echo "${VERIFY_OUTPUT}"
DETAIL="${VERIFY_OUTPUT#\[verify\] PASS: }"

if command -v sha256sum >/dev/null 2>&1; then
    RAW_SHA256="$(sha256sum "${RAW_LOG}" | awk '{print $1}')"
    FIRMWARE_SHA256="$(sha256sum "${HEX_FILE}" | awk '{print $1}')"
else
    RAW_SHA256="$(shasum -a 256 "${RAW_LOG}" | awk '{print $1}')"
    FIRMWARE_SHA256="$(shasum -a 256 "${HEX_FILE}" | awk '{print $1}')"
fi

{
    printf 'role=%s\n' "${ROLE_LABEL}"
    printf 'configuration=%s\n' "${CONFIG}"
    printf 'preamble_symbols=%s\n' "${PREAMBLE}"
    printf 'sensor_count=%s\n' "${SENSOR_COUNT}"
    printf 'guard_us=%s\n' "${GUARD_US}"
    printf 'run_number=%s\n' "${RUN_NUMBER}"
    printf 'environment=%s\n' "${ENVIRONMENT}"
    printf 'distance_m=%s\n' "${DISTANCE}"
    printf 'captured_at=%s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')"
    printf 'firmware_sha256=%s\n' "${FIRMWARE_SHA256}"
    printf 'rtt_address=%s\n' "${RTT_ADDR}"
    printf 'capture_method=pylink\n'
    printf 'raw_log=%s\n' "${RAW_LOG}"
    printf 'raw_size_bytes=%s\n' "$(wc -c <"${RAW_LOG}" | tr -d '[:space:]')"
    printf 'raw_sha256=%s\n' "${RAW_SHA256}"
    printf 'collection_status=PASS\n'
    printf 'status=PASS\n'
    printf 'detail=%s\n' "${DETAIL}"
} >"${META_FILE}"

echo "[done] raw=${RAW_LOG}"
echo "[done] meta=${META_FILE}"
