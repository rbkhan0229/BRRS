#!/usr/bin/env bash

set -euo pipefail

usage() {
    echo "Usage: $0 <32|64|128|256> <sensor-count:1..7> [guard-us] [all|tx|init|N2..N8] [lead-us] [--pac <4|8>] [--sync-buffer <us>] [--sync-prep <us>] [--sequence <digits>] [--spi-opt] [--irq]"
    echo "Example: $0 32 2 200 N3 15"
    echo "Example (custom slot schedule): $0 64 2 200 all 15 --sequence 2323232323232"
    echo
    echo "--sequence lets a small number of physical nodes fill up more of the"
    echo "superframe by owning several slots each: each digit (2-8) names the"
    echo "node_seq owning that slot, e.g. 2323232323232 gives node 2 seven"
    echo "slots and node 3 six, out of 13 total. Only affects the init image;"
    echo "sensor node images are unaffected (they already scan the whole"
    echo "decoded slot_owner[] for every slot they own). Every digit used must"
    echo "be within 2..sensor_count+1."
}

SEQUENCE=""
PAC=8
SPI_OPT=0
IRQ_PENDING=0
SYNC_BUFFER_US=3000
SYNC_PREP_US=2500
ARGS=()
while (( $# > 0 )); do
    case "$1" in
        --sequence)
            (( $# >= 2 )) || { echo "--sequence requires a value" >&2; exit 2; }
            SEQUENCE="$2"; shift 2 ;;
        --pac)
            (( $# >= 2 )) || { echo "--pac requires a value" >&2; exit 2; }
            PAC="$2"; shift 2 ;;
        --sync-buffer)
            (( $# >= 2 )) || { echo "--sync-buffer requires a value" >&2; exit 2; }
            SYNC_BUFFER_US="$2"; shift 2 ;;
        --sync-prep)
            (( $# >= 2 )) || { echo "--sync-prep requires a value" >&2; exit 2; }
            SYNC_PREP_US="$2"; shift 2 ;;
        --spi-opt) SPI_OPT=1; shift ;;
        --irq) IRQ_PENDING=1; shift ;;
        *) ARGS+=("$1"); shift ;;
    esac
done
# bash 3.2 (macOS default) raises "unbound variable" under set -u when
# expanding "${arr[@]}" on a zero-length array, even though it was
# declared -- the ${arr[@]+"${arr[@]}"} idiom works around it.
set -- ${ARGS[@]+"${ARGS[@]}"}

if [[ $# -lt 2 || $# -gt 5 ]]; then
    usage
    exit 2
fi

plen="$1"
sensor_count="$2"
guard_us="${3:-200}"
requested_role="${4:-all}"
lead_us="${5:-15}"

case "${PAC}" in
    4|8) ;;
    *) echo "ERROR: pac must be 4 or 8" >&2; exit 2 ;;
esac

if [[ -n "${SEQUENCE}" ]]; then
    [[ "${SEQUENCE}" =~ ^[2-8]+$ ]] \
        || { echo "ERROR: --sequence must contain only digits 2-8" >&2; exit 2; }
    (( ${#SEQUENCE} <= 32 )) \
        || { echo "ERROR: --sequence is longer than the 32-slot beacon capacity" >&2; exit 2; }
    max_node=$((sensor_count + 1))
    for (( i = 0; i < ${#SEQUENCE}; i++ )); do
        digit="${SEQUENCE:i:1}"
        (( digit <= max_node )) \
            || { echo "ERROR: --sequence references node ${digit}, outside a ${sensor_count}-sensor run" >&2; exit 2; }
    done
fi

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
if ! [[ "${lead_us}" =~ ^[0-9]+$ ]] || (( lead_us > 1000 )); then
    echo "ERROR: lead-us must be an integer between 0 and 1000" >&2
    exit 2
fi
if (( sensor_count > 1 && guard_us < lead_us )); then
    echo "ERROR: multi-sensor guard must be at least the ${lead_us} us RX lead margin" >&2
    exit 2
fi
if ! [[ "${SYNC_BUFFER_US}" =~ ^[1-9][0-9]*$ ]] || (( SYNC_BUFFER_US >= 10000 )); then
    echo "ERROR: --sync-buffer must be an integer between 1 and 9999 us" >&2
    exit 2
fi
if ! [[ "${SYNC_PREP_US}" =~ ^[1-9][0-9]*$ ]] || (( SYNC_PREP_US >= 10000 )); then
    echo "ERROR: --sync-prep must be an integer between 1 and 9999 us" >&2
    exit 2
fi
if (( SYNC_BUFFER_US + SYNC_PREP_US >= 10000 )); then
    echo "ERROR: sync buffer + sync prep must leave a positive DATA budget" >&2
    exit 2
fi

if [[ "${requested_role}" == "init" || "${requested_role}" == "all" || "${requested_role}" == "tx" ]]; then
    :
elif [[ "${requested_role}" =~ ^N([2-8])$ ]]; then
    requested_node="${BASH_REMATCH[1]}"
    if (( requested_node > sensor_count + 1 )); then
        echo "ERROR: ${requested_role} is outside a ${sensor_count}-sensor run" >&2
        exit 2
    fi
else
    echo "ERROR: role must be all, tx, init, or N2 through N8" >&2
    exit 2
fi

script_dir="$(cd "$(dirname "$0")" && pwd)"
project="${script_dir}/Build_Platforms/nRF52840-DK/dw3000_api.emProject"
build_dir="${script_dir}/Build_Platforms/nRF52840-DK/Output/Debug/Exe"
dest_dir="${build_dir}/exp4/plen${plen}_sensors${sensor_count}"
dest_dir+="_sb${SYNC_BUFFER_US}_sp${SYNC_PREP_US}"
if (( guard_us != 100 )); then
    dest_dir+="_guard${guard_us}"
fi
if (( lead_us != 15 )); then
    dest_dir+="_lead${lead_us}"
fi
if (( PAC != 8 )); then
    dest_dir+="_pac${PAC}"
fi
if [[ -n "${SEQUENCE}" ]]; then
    dest_dir+="_seq${SEQUENCE}"
fi
if (( SPI_OPT )); then
    dest_dir+="_spiopt"
fi
if (( IRQ_PENDING )); then
    dest_dir+="_irq"
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
    local extra_defs=";BRRS_EXP4_IRQ_PENDING=${IRQ_PENDING}"
    extra_defs+=";BRRS_SYNC_BUFFER_US=${SYNC_BUFFER_US}"
    extra_defs+=";BRRS_EXP4_SYNC_PREP_US=${SYNC_PREP_US}"

    macros="BRRS_ROLE_DEFINE=${role_define};EXP3_VARIANT_DEFINE=EXP3_PHY_VARIANT=1"
    macros+=";BRRS_EXPERIMENT_DEFINE=BRRS_EXPERIMENT=4"
    macros+=";BRRS_DATA_PLEN_DEFINE=BRRS_DATA_PLEN=${plen_define}"
    macros+=";BRRS_NODE_DEFINE=${node_define}"
    macros+=";BRRS_SENSOR_COUNT_DEFINE=BRRS_SENSOR_NODES=${sensor_count}"

    if [[ -n "${SEQUENCE}" && "${role}" == "init" ]]; then
        extra_defs+=";BRRS_EXP4_CUSTOM_SEQUENCE=\"${SEQUENCE}\""
    fi
    if (( SPI_OPT )) && [[ "${role}" == "init" ]]; then
        extra_defs+=";BRRS_EXP4_SPI_PERSISTENT=1;BRRS_EXP4_SPI_DIRECT=1"
    fi

    echo "Building ${base}..."
    "${embuild}" \
        -threadnum "${EMBUILD_THREADS:-1}" \
        -sproperty "c_preprocessor_definitions=DEBUG;BRRS_EXPLICIT_PROFILE=1;BRRS_SLOT_GUARD_US=${guard_us};BRRS_RX_LEAD_MARGIN_US=${lead_us};BRRS_RX_PAC_SYMBOLS=${PAC}${extra_defs}" \
        -sproperty "macros=${macros}" \
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
    if [[ "${requested_role}" == "all" || "${requested_role}" == "tx" || "${requested_role}" == "N${node}" ]]; then
        build_image "N${node}" TEST_BRRS_NORMAL "TEST_NODE_${node}"
    fi
done

echo
echo "Experiment 4 firmware images are ready in:"
echo "${dest_dir}"
echo "Guard: ${guard_us} us"
echo "RX lead: ${lead_us} us"
echo "RX PAC: ${PAC}"
echo "SYNC buffer: ${SYNC_BUFFER_US} us"
echo "SYNC prep: ${SYNC_PREP_US} us"
echo "DATA budget: $((10000 - SYNC_BUFFER_US - SYNC_PREP_US)) us"
echo "SPI mode: $((( SPI_OPT )) && echo persistent-burst || echo legacy-per-transaction)"
echo "RX event: $((( IRQ_PENDING )) && echo gpio-irq-pending || echo fint-polling)"
echo "Role: ${requested_role}"
