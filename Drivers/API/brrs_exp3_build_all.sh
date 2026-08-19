#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/dw3000_api.emProject"
OUTPUT_DIR="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/Output"
LEAD_US="${1:-15}"
[[ "${LEAD_US}" =~ ^[0-9]+$ ]] && (( LEAD_US <= 1000 )) \
    || { echo "Usage: $(basename "$0") [lead-us:0..1000]" >&2; exit 2; }
DEST_DIR="${OUTPUT_DIR}/exp3_lead${LEAD_US}"
EMBUILD=${EMBUILD:-"/Applications/SEGGER/SEGGER Embedded Studio 8.28/bin/emBuild"}

if [[ ! -x "${EMBUILD}" ]]; then
    printf '%s\n' "ERROR: emBuild not found: ${EMBUILD}" >&2
    printf '%s\n' "Set EMBUILD to the local SES emBuild executable." >&2
    exit 1
fi

mkdir -p "${DEST_DIR}"

for variant in A B C; do
    for role in init normal; do
        if [[ "${role}" == "init" ]]; then
            role_name=Init
        else
            role_name=Normal
        fi

        config="Exp3_${variant}_${role_name}"
        log_path="${DEST_DIR}/build_${variant}_${role}.log"
        printf '%s\n' "Building Experiment 3 ${variant}/${role} (${config})..."

        build_args=(-threadnum "${EMBUILD_THREADS:-1}")
        if [[ "${role}" == "init" ]]; then
            build_args+=(-sproperty "c_additional_options=-DBRRS_RX_LEAD_MARGIN_US=${LEAD_US}")
        fi
        build_args+=(-config "${config}" -project dw3000_api -rebuild "${PROJECT}")
        "${EMBUILD}" "${build_args[@]}" >"${log_path}" 2>&1

        if [[ "${role}" == "init" ]]; then
            printf '%s\n' "${LEAD_US}" \
                >"${OUTPUT_DIR}/${config}/Exe/.brrs_rx_lead_us"
        fi

        cp "${OUTPUT_DIR}/${config}/Exe/dw3000_api.hex" \
            "${DEST_DIR}/exp3_${variant}_${role}.hex"
        cp "${OUTPUT_DIR}/${config}/Exe/dw3000_api.elf" \
            "${DEST_DIR}/exp3_${variant}_${role}.elf"
        printf '%s\n' "  OK: exp3_${variant}_${role}.hex"
    done
done

printf '\n%s\n%s\n' \
    "All Experiment 3 firmware images (RX lead ${LEAD_US} us) are ready in:" \
    "${DEST_DIR}"
