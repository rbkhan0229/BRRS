#!/usr/bin/env bash

# Experiment 1 and Stage0: build -> flash -> PyLink RTT capture -> verify.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/dw3000_api.emProject"
OUTPUT_DIR="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/Output"
EXPECTED_CYCLES=2000

usage() {
    cat <<EOF
Usage:
  $(basename "$0") <tx|rx> <32|64|128|256> <run> <environment> [distance] [options]

Options:
  --lead <us>          RX lead margin for Exp1 (default: 15).
  --pac <4|8>          RX PAC size for Stage0/Exp1 (default: 8, the BRRS
                        baseline; 4 is the DW3000 vendor-recommended value
                        for preambles under 127 symbols).
  --rx-mode <mode>     Exp1 RX scheduling: delayed (default) or immediate.
                        Immediate keeps the same beacon/PHY/slot and opens RX
                        as soon as DATA configuration is ready.
  --serial <S/N>       Select a J-Link when multiple probes are attached.
  --no-build           Reuse the existing ELF and HEX.
  --timeout <seconds>  Override capture timeout (RX 120 s, TX 600 s).
  --force              Preserve an existing log as .prev.<time> and retry.
  -h, --help           Show this help.

Examples:
  $(basename "$0") tx 32 1 iron_door_nlos 6.9
  $(basename "$0") rx 32 1 iron_door_nlos 6.9

Stage0 users should call brrs_stage0_capture.sh instead.
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
PREAMBLE="$2"
RUN_NUMBER="$3"
ENVIRONMENT="$4"
shift 4

DISTANCE="na"
SERIAL=""
NO_BUILD=0
TIMEOUT=""
FORCE=0
MODE="exp1"
LEAD_US=15
TAIL_US=0
PAC=8
RX_MODE="delayed"
if (( $# > 0 )) && [[ "$1" != --* ]]; then
    DISTANCE="$1"
    shift
fi
while (( $# > 0 )); do
    case "$1" in
        --serial)
            (( $# >= 2 )) || { echo "--serial requires a value" >&2; exit 2; }
            SERIAL="$2"; shift 2 ;;
        --no-build) NO_BUILD=1; shift ;;
        --timeout)
            (( $# >= 2 )) || { echo "--timeout requires a value" >&2; exit 2; }
            TIMEOUT="$2"; shift 2 ;;
        --force) FORCE=1; shift ;;
        --stage0) MODE="stage0"; shift ;;
        --lead)
            (( $# >= 2 )) || { echo "--lead requires a value" >&2; exit 2; }
            LEAD_US="$2"; shift 2 ;;
        --tail)
            (( $# >= 2 )) || { echo "--tail requires a value" >&2; exit 2; }
            TAIL_US="$2"; shift 2 ;;
        --pac)
            (( $# >= 2 )) || { echo "--pac requires a value" >&2; exit 2; }
            PAC="$2"; shift 2 ;;
        --rx-mode)
            (( $# >= 2 )) || { echo "--rx-mode requires a value" >&2; exit 2; }
            RX_MODE="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

case "${ROLE}" in
    tx|rx) ;;
    *) echo "role must be tx or rx" >&2; exit 2 ;;
esac
case "${PREAMBLE}" in
    32|64|128|256) ;;
    *) echo "preamble must be 32, 64, 128, or 256" >&2; exit 2 ;;
esac
[[ "${RUN_NUMBER}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "run must be a positive integer" >&2; exit 2; }
[[ "${ENVIRONMENT}" =~ ^[A-Za-z0-9._-]+$ ]] \
    || { echo "environment may contain only letters, numbers, dot, underscore, and hyphen" >&2; exit 2; }
if [[ "${DISTANCE}" != "na" && ! "${DISTANCE}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "distance must be a non-negative number in meters" >&2
    exit 2
fi
[[ "${LEAD_US}" =~ ^[0-9]+$ && "${TAIL_US}" =~ ^[0-9]+$ ]] \
    && (( LEAD_US <= 1000 && TAIL_US <= 1000 )) \
    || { echo "lead and tail must be between 0 and 1000 us" >&2; exit 2; }
[[ -z "${TIMEOUT}" || "${TIMEOUT}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "timeout must be a positive integer" >&2; exit 2; }
case "${PAC}" in
    4|8) ;;
    *) echo "pac must be 4 or 8" >&2; exit 2 ;;
esac
case "${RX_MODE}" in
    delayed|immediate) ;;
    *) echo "rx-mode must be delayed or immediate" >&2; exit 2 ;;
esac

if [[ "${MODE}" == "stage0" ]]; then
    [[ "${PREAMBLE}" == "32" ]] \
        || { echo "Stage0 uses the 32-symbol preamble" >&2; exit 2; }
    if [[ "${ROLE}" == "rx" ]]; then
        CONFIG="Stage0_L${LEAD_US}_T${TAIL_US}_Init"
    else
        CONFIG="Stage0_Normal"
    fi
    EXPERIMENT_LABEL="Stage0: lead ${LEAD_US} us, tail ${TAIL_US} us, pac ${PAC}"
    LOG_PREFIX="stage0_l${LEAD_US}_t${TAIL_US}_pac${PAC}"
    [[ "${RX_MODE}" == "delayed" ]] \
        || { echo "Stage0 supports delayed RX only" >&2; exit 2; }
else
    TAIL_US=0
    if [[ "${ROLE}" == "rx" ]]; then
        CONFIG="Exp1_${PREAMBLE}_Init"
    else
        CONFIG="Exp1_Normal"
    fi
    EXPERIMENT_LABEL="Experiment 1: ${PREAMBLE} symbols, PAC${PAC}, ${RX_MODE}-RX"
    LOG_PREFIX="exp1_${PREAMBLE}_l${LEAD_US}_pac${PAC}_${RX_MODE}"
fi

grep -Fq "Name=\"${CONFIG}\"" "${PROJECT}" \
    || { echo "build configuration does not exist: ${CONFIG}" >&2; exit 2; }

if [[ "${ROLE}" == "rx" ]]; then
    END_MARKER="EXP1_DONE,"
    TIMEOUT="${TIMEOUT:-120}"
    ROLE_LABEL="RX"
else
    END_MARKER="EXP1_TX_DONE,"
    TIMEOUT="${TIMEOUT:-600}"
    ROLE_LABEL="TX"
fi

HEX_FILE="${OUTPUT_DIR}/${CONFIG}/Exe/dw3000_api.hex"
ELF_FILE="${OUTPUT_DIR}/${CONFIG}/Exe/dw3000_api.elf"
LEAD_STAMP="${OUTPUT_DIR}/${CONFIG}/Exe/.brrs_rx_lead_us"
PAC_STAMP="${OUTPUT_DIR}/${CONFIG}/Exe/.brrs_rx_pac"
RX_MODE_STAMP="${OUTPUT_DIR}/${CONFIG}/Exe/.brrs_rx_mode"
DATE_TAG="$(date '+%Y%m%d')"
DISTANCE_TAG=""
[[ "${DISTANCE}" != "na" && -n "${DISTANCE}" ]] && DISTANCE_TAG="_${DISTANCE}m"
OUTDIR="${SDK_ROOT}/../logs/${MODE}_${ENVIRONMENT}${DISTANCE_TAG}_${DATE_TAG}"
mkdir -p "${OUTDIR}"
BASE="${LOG_PREFIX}_r${RUN_NUMBER}_${ROLE}"
RAW_LOG="${OUTDIR}/${BASE}.log"
META_FILE="${OUTDIR}/${BASE}.meta.txt"
BUILD_LOG="${OUTDIR}/${BASE}.build.log"

if [[ -e "${RAW_LOG}" ]]; then
    if (( FORCE )); then
        BACKUP_TAG="$(date '+%H%M%S')"
        for artifact in "${RAW_LOG}" "${META_FILE}" "${BUILD_LOG}"; do
            [[ -e "${artifact}" ]] && mv "${artifact}" "${artifact}.prev.${BACKUP_TAG}"
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

echo "[${ROLE_LABEL}] ${EXPERIMENT_LABEL}"
echo "  Configuration: ${CONFIG}"
echo "  RX lead:       ${LEAD_US} us"
echo "  RX PAC:        ${PAC}"
echo "  RX mode:       ${RX_MODE}"
echo "  Run:           ${RUN_NUMBER}"
echo "  Environment:   ${ENVIRONMENT}"
echo "  Distance:      ${DISTANCE}"
echo "  Raw log:       ${RAW_LOG}"

if (( NO_BUILD == 0 )); then
    echo "[build] ${CONFIG}"
    BUILD_ARGS=(-threadnum "${EMBUILD_THREADS:-1}")
    if [[ "${MODE}" == "exp1" && "${ROLE}" == "rx" ]]; then
        RX_MODE_DEFINE=0
        [[ "${RX_MODE}" == "immediate" ]] && RX_MODE_DEFINE=1
        DEFS="DEBUG;BRRS_RX_TAIL_MARGIN_US=0;BRRS_TARGET_CYCLES=${EXPECTED_CYCLES}"
        DEFS+=";BRRS_RX_LEAD_MARGIN_US=${LEAD_US}"
        DEFS+=";BRRS_RX_PAC_SYMBOLS=${PAC}"
        DEFS+=";BRRS_RX_IMMEDIATE_CONTROL=${RX_MODE_DEFINE}"
        BUILD_ARGS+=(-sproperty "c_preprocessor_definitions=${DEFS}")
    fi
    if [[ "${MODE}" == "stage0" && "${ROLE}" == "rx" ]]; then
        DEFS="DEBUG;BRRS_TARGET_CYCLES=${EXPECTED_CYCLES}"
        DEFS+=";BRRS_RX_LEAD_MARGIN_US=${LEAD_US}"
        DEFS+=";BRRS_RX_TAIL_MARGIN_US=${TAIL_US}"
        DEFS+=";BRRS_RX_PAC_SYMBOLS=${PAC}"
        BUILD_ARGS+=(-sproperty "c_preprocessor_definitions=${DEFS}")
    fi
    BUILD_ARGS+=(-config "${CONFIG}" -project dw3000_api -rebuild "${PROJECT}")
    "${EMBUILD}" "${BUILD_ARGS[@]}" >"${BUILD_LOG}" 2>&1 \
        || { echo "build failed: ${BUILD_LOG}" >&2; exit 1; }
    if [[ "${MODE}" == "exp1" && "${ROLE}" == "rx" ]]; then
        printf '%s\n' "${LEAD_US}" >"${LEAD_STAMP}"
        printf '%s\n' "${PAC}" >"${PAC_STAMP}"
        printf '%s\n' "${RX_MODE}" >"${RX_MODE_STAMP}"
    fi
    if [[ "${MODE}" == "stage0" && "${ROLE}" == "rx" ]]; then
        printf '%s\n' "${PAC}" >"${PAC_STAMP}"
    fi
fi
[[ -f "${HEX_FILE}" && -f "${ELF_FILE}" ]] \
    || { echo "firmware image missing for ${CONFIG}" >&2; exit 1; }
if (( NO_BUILD == 1 )) && [[ "${MODE}" == "stage0" && "${ROLE}" == "rx" ]]; then
    [[ -f "${PAC_STAMP}" && "$(<"${PAC_STAMP}")" == "${PAC}" ]] \
        || { echo "cached ${CONFIG} was not built with pac ${PAC}" >&2; exit 1; }
fi
if (( NO_BUILD == 1 )) && [[ "${MODE}" == "exp1" && "${ROLE}" == "rx" ]]; then
    [[ -f "${LEAD_STAMP}" && "$(<"${LEAD_STAMP}")" == "${LEAD_US}" ]] \
        || { echo "cached ${CONFIG} was not built with lead ${LEAD_US} us" >&2; exit 1; }
    [[ -f "${PAC_STAMP}" && "$(<"${PAC_STAMP}")" == "${PAC}" ]] \
        || { echo "cached ${CONFIG} was not built with pac ${PAC}" >&2; exit 1; }
    [[ -f "${RX_MODE_STAMP}" && "$(<"${RX_MODE_STAMP}")" == "${RX_MODE}" ]] \
        || { echo "cached ${CONFIG} was not built for ${RX_MODE} RX" >&2; exit 1; }
fi

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
    --end-marker "${END_MARKER}"
    --timeout "${TIMEOUT}"
    --out "${RAW_LOG}"
)
[[ -n "${SERIAL}" ]] && PYLINK_ARGS+=(--serial "${SERIAL}")
"${PYLINK_ARGS[@]}"

VERIFY_OUTPUT="$(python3 "${SCRIPT_DIR}/brrs_exp1_verify.py" "${RAW_LOG}" \
    --mode "${MODE}" --role "${ROLE}" --preamble "${PREAMBLE}" \
    --lead "${LEAD_US}" --tail "${TAIL_US}" --pac "${PAC}" \
    --rx-mode "${RX_MODE}" \
    --expected "${EXPECTED_CYCLES}")"
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
    printf 'mode=%s\n' "${MODE}"
    printf 'role=%s\n' "${ROLE}"
    printf 'configuration=%s\n' "${CONFIG}"
    printf 'preamble_symbols=%s\n' "${PREAMBLE}"
    printf 'lead_us=%s\n' "${LEAD_US}"
    printf 'tail_us=%s\n' "${TAIL_US}"
    printf 'pac=%s\n' "${PAC}"
    printf 'rx_mode=%s\n' "${RX_MODE}"
    printf 'expected_cycles=%s\n' "${EXPECTED_CYCLES}"
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
