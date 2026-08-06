#!/bin/zsh

set -eu

SCRIPT_DIR=${0:A:h}
PROJECT="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/dw3000_api.emProject"
BUILD_DIR="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/Output/Debug/Exe"
DEST_DIR="${BUILD_DIR}/exp3"
EMBUILD=${EMBUILD:-"/Applications/SEGGER/SEGGER Embedded Studio 8.28/bin/emBuild"}

if [[ ! -x "${EMBUILD}" ]]; then
    print -u2 "ERROR: emBuild not found: ${EMBUILD}"
    exit 1
fi

mkdir -p "${DEST_DIR}"

for variant in A B C; do
    case "${variant}" in
        A) variant_id=1 ;;
        B) variant_id=2 ;;
        C) variant_id=3 ;;
    esac

    for role in init normal; do
        if [[ "${role}" == "init" ]]; then
            role_define=TEST_BRRS_INIT
        else
            role_define=TEST_BRRS_NORMAL
        fi

        log_path="${DEST_DIR}/build_${variant}_${role}.log"
        print "Building Experiment 3 ${variant}/${role}..."

        "${EMBUILD}" \
            -sproperty "c_preprocessor_definitions=DEBUG;BRRS_EXPLICIT_PROFILE=1" \
            -sproperty "macros=BRRS_ROLE_DEFINE=${role_define};EXP3_VARIANT_DEFINE=EXP3_PHY_VARIANT=${variant_id};BRRS_EXPERIMENT_DEFINE=BRRS_EXPERIMENT=3;BRRS_DATA_PLEN_DEFINE=BRRS_DATA_PLEN=DWT_PLEN_32;BRRS_NODE_DEFINE=TEST_NODE_2;BRRS_SENSOR_COUNT_DEFINE=BRRS_SENSOR_NODES=2" \
            -config Debug \
            -project dw3000_api \
            -rebuild \
            "${PROJECT}" >"${log_path}" 2>&1

        cp "${BUILD_DIR}/dw3000_api.hex" \
            "${DEST_DIR}/exp3_${variant}_${role}.hex"
        cp "${BUILD_DIR}/dw3000_api.elf" \
            "${DEST_DIR}/exp3_${variant}_${role}.elf"
        print "  OK: exp3_${variant}_${role}.hex"
    done
done

print ""
print "All Experiment 3 firmware images are ready in:"
print "${DEST_DIR}"
