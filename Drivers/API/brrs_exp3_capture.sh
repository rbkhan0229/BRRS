#!/usr/bin/env bash

# Experiment 3: build -> flash -> single-J-Link PyLink RTT capture -> verify.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/dw3000_api.emProject"
OUTPUT_DIR="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/Output"
EXPECTED_SAMPLES=1000

usage() {
    cat <<EOF
Usage:
  $(basename "$0") <tx|rx> <A|B|C> <run> <environment> [distance] [options]

Options:
  --lead <us>          RX lead margin (default: 15).
  --serial <S/N>       Select a J-Link when multiple probes are attached.
  --no-build           Reuse the existing ELF and HEX.
  --timeout <seconds>  Override the capture timeout (default: 120).
  --force              Preserve an existing log as .prev.<time> and retry.
  -h, --help           Show this help.

Examples:
  $(basename "$0") tx A 1 iron_door_nlos 6.9
  $(basename "$0") rx A 1 iron_door_nlos 6.9
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

ROLE="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
VARIANT="$(printf '%s' "$2" | tr '[:lower:]' '[:upper:]')"
RUN_NUMBER="$3"
ENVIRONMENT="$4"
shift 4

DISTANCE="na"
SERIAL=""
NO_BUILD=0
TIMEOUT=120
FORCE=0
LEAD_US=15
if (( $# > 0 )) && [[ "$1" != --* ]]; then
    DISTANCE="$1"
    shift
fi
while (( $# > 0 )); do
    case "$1" in
        --lead) (( $# >= 2 )) || { echo "--lead requires a value" >&2; exit 2; }; LEAD_US="$2"; shift 2 ;;
        --serial) (( $# >= 2 )) || { echo "--serial requires a value" >&2; exit 2; }; SERIAL="$2"; shift 2 ;;
        --no-build) NO_BUILD=1; shift ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --force) FORCE=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

case "${ROLE}" in
    tx)
        ROLE_NAME="Normal"
        READY_MARKER="EXP_LOG_READY,channel=1"
        END_MARKER="EXP3_TX_DUMP_DONE,variant=${VARIANT}"
        ;;
    rx)
        ROLE_NAME="Init"
        READY_MARKER="EXP_LOG_READY,channel=1"
        END_MARKER="EXP3_RX_DONE,variant=${VARIANT}"
        ;;
    *) echo "role must be tx or rx" >&2; exit 2 ;;
esac

case "${VARIANT}" in
    A) SFD_SYMBOLS=8;  PHR_RATE="STD" ;;
    B) SFD_SYMBOLS=16; PHR_RATE="STD" ;;
    C) SFD_SYMBOLS=8;  PHR_RATE="DTA" ;;
    *) echo "variant must be A, B, or C" >&2; exit 2 ;;
esac

[[ "${RUN_NUMBER}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "run must be a positive integer" >&2; exit 2; }
[[ "${TIMEOUT}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "timeout must be a positive integer" >&2; exit 2; }
[[ "${LEAD_US}" =~ ^[0-9]+$ ]] && (( LEAD_US <= 1000 )) \
    || { echo "lead must be between 0 and 1000 us" >&2; exit 2; }

CONFIG="Exp3_${VARIANT}_${ROLE_NAME}"
HEX_FILE="${OUTPUT_DIR}/${CONFIG}/Exe/dw3000_api.hex"
ELF_FILE="${OUTPUT_DIR}/${CONFIG}/Exe/dw3000_api.elf"
LEAD_STAMP="${OUTPUT_DIR}/${CONFIG}/Exe/.brrs_rx_lead_us"
DATE_TAG="$(date '+%Y%m%d')"
DISTANCE_TAG=""
[[ "${DISTANCE}" != "na" && -n "${DISTANCE}" ]] && DISTANCE_TAG="_${DISTANCE}m"
OUTDIR="${SDK_ROOT}/../logs/exp3_${ENVIRONMENT}${DISTANCE_TAG}_${DATE_TAG}"
mkdir -p "${OUTDIR}"

BASE="exp3_${VARIANT}_l${LEAD_US}_r${RUN_NUMBER}_${ROLE}"
RAW_LOG="${OUTDIR}/${BASE}.log"
META_FILE="${OUTDIR}/${BASE}.meta.txt"
BUILD_LOG="${OUTDIR}/${BASE}.build.log"
if [[ -e "${RAW_LOG}" ]]; then
    if (( FORCE )); then
        BACKUP="${RAW_LOG}.prev.$(date '+%H%M%S')"
        mv "${RAW_LOG}" "${BACKUP}"
        echo "[log] existing log moved to ${BACKUP}"
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

echo "[${ROLE_NAME}] Experiment 3 ${VARIANT}: SFD${SFD_SYMBOLS}, ${PHR_RATE} PHR"
echo "  Configuration: ${CONFIG}"
echo "  RX lead:       ${LEAD_US} us"
echo "  Run:           ${RUN_NUMBER}"
echo "  Environment:   ${ENVIRONMENT}"
echo "  Distance:      ${DISTANCE}"
echo "  Raw log:       ${RAW_LOG}"

if (( NO_BUILD == 0 )); then
    echo "[build] ${CONFIG}"
    BUILD_ARGS=(-threadnum "${EMBUILD_THREADS:-1}")
    if [[ "${ROLE}" == "rx" ]]; then
        BUILD_ARGS+=(-sproperty "c_additional_options=-DBRRS_RX_LEAD_MARGIN_US=${LEAD_US}")
    fi
    BUILD_ARGS+=(-config "${CONFIG}" -project dw3000_api -rebuild "${PROJECT}")
    "${EMBUILD}" "${BUILD_ARGS[@]}" >"${BUILD_LOG}" 2>&1 \
        || { echo "build failed: ${BUILD_LOG}" >&2; exit 1; }
    if [[ "${ROLE}" == "rx" ]]; then
        printf '%s\n' "${LEAD_US}" >"${LEAD_STAMP}"
    fi
fi
[[ -f "${HEX_FILE}" && -f "${ELF_FILE}" ]] \
    || { echo "firmware image missing for ${CONFIG}" >&2; exit 1; }
if (( NO_BUILD == 1 )) && [[ "${ROLE}" == "rx" ]]; then
    [[ -f "${LEAD_STAMP}" && "$(<"${LEAD_STAMP}")" == "${LEAD_US}" ]] \
        || { echo "cached ${CONFIG} was not built with lead ${LEAD_US} us" >&2; exit 1; }
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
    --ready-marker "${READY_MARKER}"
    --end-marker "${END_MARKER}"
    --timeout "${TIMEOUT}"
    --out "${RAW_LOG}"
)
[[ -n "${SERIAL}" ]] && PYLINK_ARGS+=(--serial "${SERIAL}")
"${PYLINK_ARGS[@]}"

if [[ "${ROLE}" == "tx" ]]; then
    CSV_ROWS="$(grep -c '^EXP3_TX_CSV,' "${RAW_LOG}" || true)"
    RESULT_LINE="$(grep '^EXP3_TX_RESULT,' "${RAW_LOG}" | tail -1 || true)"
    DUMP_LINE="$(grep '^EXP3_TX_DUMP_DONE,' "${RAW_LOG}" | tail -1 || true)"

    read -r UNIQUE_SEQS BAD_ROWS SEQ_MIN SEQ_MAX <<EOF
$(awk -F, -v variant="${VARIANT}" -v sfd="${SFD_SYMBOLS}" \
        -v phr="${PHR_RATE}" '
    $1 == "EXP3_TX_CSV" {
        seqs[$2] = 1;
        if (count == 0 || $2 < seq_min) seq_min = $2;
        if (count == 0 || $2 > seq_max) seq_max = $2;
        count++;
        if ($3 != variant || $4 != sfd || $5 != phr || $6 != 26 || NF != 8) bad++;
    }
    END {
        for (key in seqs) unique++;
        printf "%d %d %d %d\n", unique, bad, seq_min, seq_max;
    }
' "${RAW_LOG}")
EOF

    read -r ATTEMPTS SUCCESS CAPTURES END_RECEIVED RESULT_STATUS <<EOF
$(printf '%s\n' "${RESULT_LINE}" | awk -F, '
    { for (i=2; i<=NF; i++) { split($i, kv, "="); v[kv[1]]=kv[2] } }
    END { printf "%s %s %s %s %s\n", v["attempts"], v["success"], v["captures"], v["end"], v["status"] }
')
EOF
    read -r DUMP_EXPECTED DUMP_COUNT DUMP_STATUS <<EOF
$(printf '%s\n' "${DUMP_LINE}" | awk -F, '
    { for (i=2; i<=NF; i++) { split($i, kv, "="); v[kv[1]]=kv[2] } }
    END { printf "%s %s %s\n", v["expected"], v["count"], v["status"] }
')
EOF

    if [[ ! "${ATTEMPTS:-}" =~ ^[0-9]+$ ||
          ! "${SUCCESS:-}" =~ ^[0-9]+$ ||
          ! "${CAPTURES:-}" =~ ^[0-9]+$ ||
          ! "${END_RECEIVED:-}" =~ ^[0-9]+$ ||
          ! "${DUMP_EXPECTED:-}" =~ ^[0-9]+$ ||
          ! "${DUMP_COUNT:-}" =~ ^[0-9]+$ ]] ||
       (( ATTEMPTS != EXPECTED_SAMPLES || SUCCESS != EXPECTED_SAMPLES ||
          CAPTURES != EXPECTED_SAMPLES || DUMP_EXPECTED != EXPECTED_SAMPLES ||
          END_RECEIVED != 1 ||
          DUMP_COUNT != EXPECTED_SAMPLES || CSV_ROWS != EXPECTED_SAMPLES ||
          UNIQUE_SEQS != EXPECTED_SAMPLES || SEQ_MIN != 1 ||
          SEQ_MAX != EXPECTED_SAMPLES || BAD_ROWS != 0 )) ||
       [[ "${RESULT_STATUS}" != "PASS" || "${DUMP_STATUS}" != "PASS" ]]; then
        echo "[verify] FAIL: TX attempts=${ATTEMPTS:-?}, success=${SUCCESS:-?}, captures=${CAPTURES:-?}, rows=${CSV_ROWS}, unique=${UNIQUE_SEQS}, bad=${BAD_ROWS}" >&2
        exit 3
    fi
    DETAIL="collection=PASS; captures=${CAPTURES}/1000; variant=${VARIANT}; SFD=${SFD_SYMBOLS}; PHR=${PHR_RATE}"
else
    if ! grep -Fxq "EXP_LOG_CONFIG_CSV,experiment=3,plen=32,lead_us=${LEAD_US},tail_us=0,target=1000,cir=0" "${RAW_LOG}"; then
        echo "[verify] FAIL: firmware did not report requested lead ${LEAD_US} us" >&2
        exit 3
    fi
    RESULT_LINE="$(grep '^EXP3_RX_RESULT_CSV,' "${RAW_LOG}" | tail -1 || true)"
    DONE_LINE="$(grep '^EXP3_RX_DONE,' "${RAW_LOG}" | tail -1 || true)"

    IFS=, read -r RX_TAG RX_VARIANT RX_SFD RX_PHR RX_PSDU RX_EXPECTED \
        RX_COUNT RX_MISSED RX_PER_X1000 RX_MODEL RX_RESULT_STATUS <<<"${RESULT_LINE}"
    read -r DONE_EXPECTED DONE_RX DONE_PER_X1000 DONE_END_TX DONE_STATUS <<EOF
$(printf '%s\n' "${DONE_LINE}" | awk -F, '
    { for (i=2; i<=NF; i++) { split($i, kv, "="); v[kv[1]]=kv[2] } }
    END { printf "%s %s %s %s %s\n", v["expected"], v["rx"], v["per_x1000"], v["end_tx"], v["status"] }
')
EOF

    if [[ "${RX_TAG:-}" != "EXP3_RX_RESULT_CSV" ||
          "${RX_VARIANT:-}" != "${VARIANT}" ||
          "${RX_SFD:-}" != "${SFD_SYMBOLS}" ||
          "${RX_PHR:-}" != "${PHR_RATE}" ||
          "${RX_PSDU:-}" != "26" ||
          ! "${RX_EXPECTED:-}" =~ ^[0-9]+$ ||
          ! "${RX_COUNT:-}" =~ ^[0-9]+$ ||
          ! "${RX_MISSED:-}" =~ ^[0-9]+$ ||
          ! "${RX_PER_X1000:-}" =~ ^[0-9]+$ ||
          ! "${DONE_EXPECTED:-}" =~ ^[0-9]+$ ||
          ! "${DONE_RX:-}" =~ ^[0-9]+$ ||
          ! "${DONE_PER_X1000:-}" =~ ^[0-9]+$ ||
          ! "${DONE_END_TX:-}" =~ ^[0-9]+$ ]] ||
       (( RX_EXPECTED != EXPECTED_SAMPLES || RX_COUNT <= 0 ||
          RX_COUNT > RX_EXPECTED || RX_MISSED != RX_EXPECTED - RX_COUNT ||
          DONE_EXPECTED != RX_EXPECTED || DONE_RX != RX_COUNT ||
          DONE_PER_X1000 != RX_PER_X1000 || DONE_END_TX != 3 )) ||
       [[ "${RX_RESULT_STATUS}" != "PASS" || "${DONE_STATUS}" != "PASS" ]]; then
        echo "[verify] FAIL: inconsistent Exp3 RX collection" >&2
        echo "  ${RESULT_LINE}" >&2
        echo "  ${DONE_LINE}" >&2
        exit 3
    fi
    PER_PERCENT="$(awk -v missed="${RX_MISSED}" -v expected="${RX_EXPECTED}" \
        'BEGIN { printf "%.2f", 100.0 * missed / expected }')"
    LINK_STATUS="PASS"
    (( RX_MISSED > 0 )) && LINK_STATUS="LOSS"
    DETAIL="collection=PASS; rx=${RX_COUNT}/${RX_EXPECTED}; PER=${PER_PERCENT}%; link=${LINK_STATUS}; variant=${VARIANT}; lead=${LEAD_US}us"
fi

if command -v sha256sum >/dev/null 2>&1; then
    RAW_SHA256="$(sha256sum "${RAW_LOG}" | awk '{print $1}')"
    FIRMWARE_SHA256="$(sha256sum "${HEX_FILE}" | awk '{print $1}')"
else
    RAW_SHA256="$(shasum -a 256 "${RAW_LOG}" | awk '{print $1}')"
    FIRMWARE_SHA256="$(shasum -a 256 "${HEX_FILE}" | awk '{print $1}')"
fi

{
    printf 'role=%s\n' "${ROLE}"
    printf 'configuration=%s\n' "${CONFIG}"
    printf 'variant=%s\n' "${VARIANT}"
    printf 'sfd_symbols=%s\n' "${SFD_SYMBOLS}"
    printf 'phr_rate=%s\n' "${PHR_RATE}"
    printf 'psdu_bytes=26\n'
    printf 'lead_us=%s\n' "${LEAD_US}"
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

echo "[verify] PASS: ${DETAIL}"
echo "[done] raw=${RAW_LOG}"
echo "[done] meta=${META_FILE}"
