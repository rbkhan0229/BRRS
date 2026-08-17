#!/usr/bin/env bash

set -euo pipefail

usage() {
    echo "Usage: $0 <32|64|128|256> <sensor-count:1..7> [guard-us] [all|init|N2..N8]"
    echo "Example: $0 32 2 200 N3"
}

if [[ $# -lt 2 || $# -gt 4 ]]; then
    usage
    exit 2
fi

plen="$1"
sensor_count="$2"
guard_us="${3:-200}"
requested_role="${4:-all}"

case "${plen}" in
    32|64|128|256) plen_define="DWT_PLEN_${plen}" ;;
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
if (( sensor_count > 1 && guard_us < 12 )); then
    echo "ERROR: multi-sensor guard must be at least the 12 us RX lead margin" >&2
    exit 2
fi

if [[ "${requested_role}" == "init" || "${requested_role}" == "all" ]]; then
    :
elif [[ "${requested_role}" =~ ^N([2-8])$ ]]; then
    requested_node="${BASH_REMATCH[1]}"
    if (( requested_node > sensor_count + 1 )); then
        echo "ERROR: ${requested_role} is outside a ${sensor_count}-sensor run" >&2
        exit 2
    fi
else
    echo "ERROR: role must be all, init, or N2 through N8" >&2
    exit 2
fi

script_dir="$(cd "$(dirname "$0")" && pwd)"
project="${script_dir}/Build_Platforms/nRF52840-DK/dw3000_api.emProject"
build_dir="${script_dir}/Build_Platforms/nRF52840-DK/Output/Debug/Exe"
dest_dir="${build_dir}/exp4/plen${plen}_sensors${sensor_count}"
if (( guard_us != 100 )); then
    dest_dir+="_guard${guard_us}"
fi

if [[ -n "${EMBUILD:-}" ]]; then
    embuild="${EMBUILD}"
elif command -v emBuild >/dev/null 2>&1; then
    embuild="$(command -v emBuild)"
else
    embuild="/Applications/SEGGER/SEGGER Embedded Studio 8.28/bin/emBuild"
fi

if [[ ! -x "${embuild}" ]]; then
    echo "ERROR: emBuild not found. Set EMBUILD to the full emBuild path." >&2
    exit 1
fi

mkdir -p "${dest_dir}"

build_image() {
    local role="$1"
    local role_define="$2"
    local node_define="$3"
    local base="exp4_${plen}_s${sensor_count}_${role}"
    local macros

    macros="BRRS_ROLE_DEFINE=${role_define};EXP3_VARIANT_DEFINE=EXP3_PHY_VARIANT=1"
    macros+=";BRRS_EXPERIMENT_DEFINE=BRRS_EXPERIMENT=4"
    macros+=";BRRS_DATA_PLEN_DEFINE=BRRS_DATA_PLEN=${plen_define}"
    macros+=";BRRS_NODE_DEFINE=${node_define}"
    macros+=";BRRS_SENSOR_COUNT_DEFINE=BRRS_SENSOR_NODES=${sensor_count}"

    echo "Building ${base}..."
    "${embuild}" \
        -sproperty "c_preprocessor_definitions=DEBUG;BRRS_EXPLICIT_PROFILE=1" \
        -sproperty "macros=${macros}" \
        -sproperty "c_additional_options=-DBRRS_SLOT_GUARD_US=${guard_us}" \
        -config Debug \
        -project dw3000_api \
        -rebuild \
        "${project}" >"${dest_dir}/${base}.build.log" 2>&1

    cp "${build_dir}/dw3000_api.hex" "${dest_dir}/${base}.hex"
    cp "${build_dir}/dw3000_api.elf" "${dest_dir}/${base}.elf"
    echo "  OK: ${dest_dir}/${base}.hex"
}

if [[ "${requested_role}" == "all" || "${requested_role}" == "init" ]]; then
    build_image init TEST_BRRS_INIT TEST_NODE_2
fi

last_node=$((sensor_count + 1))
for node in $(seq 2 "${last_node}"); do
    if [[ "${requested_role}" == "all" || "${requested_role}" == "N${node}" ]]; then
        build_image "N${node}" TEST_BRRS_NORMAL "TEST_NODE_${node}"
    fi
done

echo
echo "Experiment 4 firmware images are ready in:"
echo "${dest_dir}"
echo "Guard: ${guard_us} us"
echo "Role: ${requested_role}"
