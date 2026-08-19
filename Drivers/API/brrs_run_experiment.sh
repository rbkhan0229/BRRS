#!/usr/bin/env bash

# Run one submission experiment block from a deterministic condition matrix.
# Each physical board/laptop runs this script once for its own role.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

usage() {
    cat <<'EOF'
Usage:
  brrs_run_experiment.sh <stage0|exp1|exp2|exp3|exp4> <role> \
      <environment> [distance] [options]

Roles:
  stage0-exp3: tx | rx
  exp4:        init | tx | N2 .. N8

Submission defaults for one fixed physical placement:
  stage0: fixed randomized 0..40 us sweep at 1 us spacing, 1 run per lead
  exp1:   M=32/64/128/256, 1 run per M
  exp2:   M=32/64/128/256, 1 run per M
  exp3:   A/B/C, 1 run per variant
  exp4:   M=32/64/256, 1 run per M (requires --sensors)

Options:
  --serial <S/N>         Select the J-Link attached to this role.
  --run-start <N>        First run number (default: 1).
  --repeats <N>          Run every condition N times (default: 1).
  --preambles <list>     Override M list, e.g. 32,64,128,256.
  --leads <list>         Stage0 lead list, e.g. 14,15,16 or 0-20.
  --lead <us>            Fixed RX lead for Exp1-Exp4 (default: 15).
  --tail <us>            Stage0 tail margin (default: 0).
  --sensors <1..7>       Exp4 physical sensor count (required for Exp4).
  --guard <us>           Exp4 guard (default: 200).
  --method <pylink|telnet> Exp2 backend (default: pylink).
  --timeout <seconds>    Override every case's capture timeout.
  --peer-ready-delay <s> Wait before each case so the peer can become ready.
                         Default: 20 for Exp3 RX, otherwise 0.
  --no-build             Require and reuse existing images for every case.
  --force                Preserve existing case logs and repeat them.
  --dry-run              Print the complete case sequence without hardware.
  -h, --help             Show this help.

Examples:
  brrs_run_experiment.sh exp1 tx iron_door_nlos 6.9
  brrs_run_experiment.sh exp1 rx iron_door_nlos 6.9
  brrs_run_experiment.sh stage0 rx iron_door_nlos 6.9 \
      --leads 14,15,16 --repeats 5
  brrs_run_experiment.sh exp4 N3 iron_door_nlos 6.9 --sensors 3
  brrs_run_experiment.sh exp4 tx iron_door_nlos 6.9 \
      --sensors 3 --repeats 3
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi
if (( $# < 3 )); then
    usage >&2
    exit 2
fi

EXPERIMENT="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
ROLE_INPUT="$2"
ENVIRONMENT="$3"
shift 3

DISTANCE="na"
SERIAL=""
RUN_START=1
REPEATS=""
PREAMBLE_SPEC=""
LEAD_SPEC=""
FIXED_LEAD_US=15
FIXED_LEAD_SET=0
TAIL_US=0
SENSOR_COUNT=""
GUARD_US=200
METHOD="pylink"
GUARD_SET=0
METHOD_SET=0
TIMEOUT=""
PEER_READY_DELAY=""
NO_BUILD=0
FORCE=0
DRY_RUN=0

if (( $# > 0 )) && [[ "$1" != --* ]]; then
    DISTANCE="$1"
    shift
fi
while (( $# > 0 )); do
    case "$1" in
        --serial)
            (( $# >= 2 )) || { echo "--serial requires a value" >&2; exit 2; }
            SERIAL="$2"; shift 2 ;;
        --run-start)
            (( $# >= 2 )) || { echo "--run-start requires a value" >&2; exit 2; }
            RUN_START="$2"; shift 2 ;;
        --repeats)
            (( $# >= 2 )) || { echo "--repeats requires a value" >&2; exit 2; }
            REPEATS="$2"; shift 2 ;;
        --preambles)
            (( $# >= 2 )) || { echo "--preambles requires a value" >&2; exit 2; }
            PREAMBLE_SPEC="$2"; shift 2 ;;
        --leads)
            (( $# >= 2 )) || { echo "--leads requires a value" >&2; exit 2; }
            LEAD_SPEC="$2"; shift 2 ;;
        --lead)
            (( $# >= 2 )) || { echo "--lead requires a value" >&2; exit 2; }
            FIXED_LEAD_US="$2"; FIXED_LEAD_SET=1; shift 2 ;;
        --tail)
            (( $# >= 2 )) || { echo "--tail requires a value" >&2; exit 2; }
            TAIL_US="$2"; shift 2 ;;
        --sensors)
            (( $# >= 2 )) || { echo "--sensors requires a value" >&2; exit 2; }
            SENSOR_COUNT="$2"; shift 2 ;;
        --guard)
            (( $# >= 2 )) || { echo "--guard requires a value" >&2; exit 2; }
            GUARD_US="$2"; GUARD_SET=1; shift 2 ;;
        --method)
            (( $# >= 2 )) || { echo "--method requires a value" >&2; exit 2; }
            METHOD="$2"; METHOD_SET=1; shift 2 ;;
        --timeout)
            (( $# >= 2 )) || { echo "--timeout requires a value" >&2; exit 2; }
            TIMEOUT="$2"; shift 2 ;;
        --peer-ready-delay)
            (( $# >= 2 )) || { echo "--peer-ready-delay requires a value" >&2; exit 2; }
            PEER_READY_DELAY="$2"; shift 2 ;;
        --no-build) NO_BUILD=1; shift ;;
        --force) FORCE=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

case "${EXPERIMENT}" in
    stage0|exp1|exp2|exp3|exp4) ;;
    *) echo "experiment must be stage0, exp1, exp2, exp3, or exp4" >&2; exit 2 ;;
esac

ROLE_LOWER="$(printf '%s' "${ROLE_INPUT}" | tr '[:upper:]' '[:lower:]')"
if [[ "${EXPERIMENT}" == "exp4" ]]; then
    if [[ "${ROLE_LOWER}" == "init" ]]; then
        ROLE="init"
    elif [[ "${ROLE_LOWER}" == "tx" || "${ROLE_LOWER}" == "tx-auto" || "${ROLE_LOWER}" == "auto" ]]; then
        ROLE="tx-auto"
    elif [[ "${ROLE_INPUT}" =~ ^[Nn]([2-8])$ ]]; then
        ROLE="N${BASH_REMATCH[1]}"
    else
        echo "Exp4 role must be init, tx, or N2 through N8" >&2
        exit 2
    fi
    [[ "${SENSOR_COUNT}" =~ ^[1-7]$ ]] \
        || { echo "Exp4 requires --sensors 1..7" >&2; exit 2; }
    if [[ "${ROLE}" =~ ^N([2-8])$ ]] && (( BASH_REMATCH[1] > SENSOR_COUNT + 1 )); then
        echo "${ROLE} is outside a ${SENSOR_COUNT}-sensor run" >&2
        exit 2
    fi
    if [[ "${ROLE}" == "tx-auto" && -n "${SERIAL}" ]]; then
        echo "Exp4 tx auto-discovers every probe; do not pass --serial" >&2
        exit 2
    fi
else
    case "${ROLE_LOWER}" in
        tx|rx) ROLE="${ROLE_LOWER}" ;;
        *) echo "${EXPERIMENT} role must be tx or rx" >&2; exit 2 ;;
    esac
    [[ -z "${SENSOR_COUNT}" ]] \
        || { echo "--sensors is only valid for Exp4" >&2; exit 2; }
fi

[[ "${ENVIRONMENT}" =~ ^[A-Za-z0-9._-]+$ ]] \
    || { echo "invalid environment name" >&2; exit 2; }
if [[ "${DISTANCE}" != "na" && ! "${DISTANCE}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "distance must be a non-negative number in meters" >&2
    exit 2
fi
[[ "${RUN_START}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "run-start must be a positive integer" >&2; exit 2; }
[[ -z "${REPEATS}" || "${REPEATS}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "repeats must be a positive integer" >&2; exit 2; }
[[ "${TAIL_US}" =~ ^[0-9]+$ ]] \
    || { echo "tail must be a non-negative integer" >&2; exit 2; }
[[ "${FIXED_LEAD_US}" =~ ^[0-9]+$ ]] && (( FIXED_LEAD_US <= 1000 )) \
    || { echo "lead must be between 0 and 1000 us" >&2; exit 2; }
[[ "${GUARD_US}" =~ ^[0-9]+$ ]] && (( GUARD_US <= 1000 )) \
    || { echo "guard must be between 0 and 1000 us" >&2; exit 2; }
[[ -z "${TIMEOUT}" || "${TIMEOUT}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "timeout must be a positive integer" >&2; exit 2; }
[[ -z "${PEER_READY_DELAY}" || "${PEER_READY_DELAY}" =~ ^[0-9]+$ ]] \
    || { echo "peer-ready-delay must be a non-negative integer" >&2; exit 2; }
case "${METHOD}" in
    pylink|telnet) ;;
    *) echo "method must be pylink or telnet" >&2; exit 2 ;;
esac

if [[ -z "${PEER_READY_DELAY}" ]]; then
    if [[ "${EXPERIMENT}" == "exp3" && "${ROLE}" == "rx" ]]; then
        PEER_READY_DELAY=20
    else
        PEER_READY_DELAY=0
    fi
fi

PARSED_VALUES=()
parse_integer_list() {
    local spec="$1"
    local minimum="$2"
    local maximum="$3"
    local label="$4"
    local old_ifs token start end value
    local tokens=()
    local seen="|"

    PARSED_VALUES=()
    old_ifs="${IFS}"
    IFS=','
    read -r -a tokens <<<"${spec}"
    IFS="${old_ifs}"
    for token in "${tokens[@]}"; do
        if [[ "${token}" =~ ^([0-9]+)-([0-9]+)$ ]]; then
            start="${BASH_REMATCH[1]}"
            end="${BASH_REMATCH[2]}"
            (( start <= end )) || { echo "invalid ${label} range: ${token}" >&2; exit 2; }
            for (( value=start; value<=end; value++ )); do
                PARSED_VALUES+=("${value}")
            done
        elif [[ "${token}" =~ ^[0-9]+$ ]]; then
            PARSED_VALUES+=("${token}")
        else
            echo "invalid ${label} list item: ${token}" >&2
            exit 2
        fi
    done
    (( ${#PARSED_VALUES[@]} > 0 )) || { echo "empty ${label} list" >&2; exit 2; }
    for value in "${PARSED_VALUES[@]}"; do
        (( value >= minimum && value <= maximum )) \
            || { echo "${label} out of range: ${value}" >&2; exit 2; }
    done

    # Duplicate entries would silently overwrite the same output path.
    for value in "${PARSED_VALUES[@]}"; do
        case "${seen}" in
            *"|${value}|"*) echo "duplicate ${label}: ${value}" >&2; exit 2 ;;
            *) seen="${seen}${value}|" ;;
        esac
    done
}

LEADS=()
PREAMBLES=()
case "${EXPERIMENT}" in
    stage0)
        REPEATS="${REPEATS:-1}"
        if [[ -n "${LEAD_SPEC}" ]]; then
            parse_integer_list "${LEAD_SPEC}" 0 40 lead
            LEADS=("${PARSED_VALUES[@]}")
        else
            LEADS=(20 0 40 10 30 5 25 15 35 2 22 12 32 7 27 17 37 4 24 14 34 9 29 19 39 1 21 11 31 6 26 16 36 3 23 13 33 8 28 18 38)
        fi
        ;;
    exp1)
        REPEATS="${REPEATS:-1}"
        PREAMBLE_SPEC="${PREAMBLE_SPEC:-32,64,128,256}"
        ;;
    exp2)
        REPEATS="${REPEATS:-1}"
        PREAMBLE_SPEC="${PREAMBLE_SPEC:-32,64,128,256}"
        ;;
    exp3)
        REPEATS="${REPEATS:-1}"
        [[ -z "${PREAMBLE_SPEC}" && -z "${LEAD_SPEC}" ]] \
            || { echo "Exp3 does not use --preambles or --leads" >&2; exit 2; }
        ;;
    exp4)
        REPEATS="${REPEATS:-1}"
        PREAMBLE_SPEC="${PREAMBLE_SPEC:-32,64,256}"
        ;;
esac

if [[ -n "${PREAMBLE_SPEC}" ]]; then
    parse_integer_list "${PREAMBLE_SPEC}" 32 256 preamble
    PREAMBLES=("${PARSED_VALUES[@]}")
    for value in "${PREAMBLES[@]}"; do
        case "${value}" in
            32|64|128|256) ;;
            *) echo "unsupported preamble: ${value}" >&2; exit 2 ;;
        esac
    done
fi

if [[ "${EXPERIMENT}" != "stage0" && -n "${LEAD_SPEC}" ]]; then
    echo "--leads is only valid for Stage0" >&2
    exit 2
fi
if [[ "${EXPERIMENT}" == "stage0" && ${FIXED_LEAD_SET} -eq 1 ]]; then
    echo "Stage0 sweeps --leads; do not pass the fixed --lead option" >&2
    exit 2
fi
if [[ "${EXPERIMENT}" == "stage0" && -n "${PREAMBLE_SPEC}" ]]; then
    echo "Stage0 uses a fixed 32-symbol preamble; do not pass --preambles" >&2
    exit 2
fi
if [[ "${EXPERIMENT}" != "stage0" && "${TAIL_US}" != "0" ]]; then
    echo "--tail is only valid for Stage0" >&2
    exit 2
fi
if [[ "${EXPERIMENT}" != "exp2" && ${METHOD_SET} -eq 1 ]]; then
    echo "--method is only valid for Exp2" >&2
    exit 2
fi
if [[ "${EXPERIMENT}" != "exp4" && ${GUARD_SET} -eq 1 ]]; then
    echo "--guard is only valid for Exp4" >&2
    exit 2
fi
if [[ "${EXPERIMENT}" == "exp4" ]] && (( SENSOR_COUNT > 1 && GUARD_US < FIXED_LEAD_US )); then
    echo "Exp4 guard ${GUARD_US} us must be at least lead ${FIXED_LEAD_US} us" >&2
    exit 2
fi

case "${EXPERIMENT}" in
    stage0) CASES_PER_RUN=${#LEADS[@]} ;;
    exp1|exp2|exp4) CASES_PER_RUN=${#PREAMBLES[@]} ;;
    exp3) CASES_PER_RUN=3 ;;
esac
TOTAL_CASES=$((CASES_PER_RUN * REPEATS))

DISTANCE_TAG=""
[[ "${DISTANCE}" != "na" ]] && DISTANCE_TAG="_${DISTANCE}m"
SUITE_DIR="${SDK_ROOT}/../logs/suites"
MANIFEST="${SUITE_DIR}/${EXPERIMENT}_${ENVIRONMENT}${DISTANCE_TAG}_${ROLE}_$(date '+%Y%m%d_%H%M%S').suite.log"
if (( DRY_RUN == 0 )); then
    mkdir -p "${SUITE_DIR}"
fi

record() {
    printf '%s\n' "$*"
    if (( DRY_RUN == 0 )); then
        printf '%s\n' "$*" >>"${MANIFEST}"
    fi
}

BUILT_KEYS="|"
key_is_built() {
    case "${BUILT_KEYS}" in
        *"|$1|"*) return 0 ;;
        *) return 1 ;;
    esac
}
mark_key_built() {
    BUILT_KEYS="${BUILT_KEYS}$1|"
}

CASE_NUMBER=0
run_case() {
    local key="$1"
    local description="$2"
    local script="$3"
    shift 3
    local command=("${script}" "$@")
    local rc

    [[ -z "${SERIAL}" ]] || command+=(--serial "${SERIAL}")
    [[ -z "${TIMEOUT}" ]] || command+=(--timeout "${TIMEOUT}")
    (( FORCE == 0 )) || command+=(--force)
    if (( NO_BUILD == 1 )) || key_is_built "${key}"; then
        command+=(--no-build)
    fi

    CASE_NUMBER=$((CASE_NUMBER + 1))
    record "CASE_START ${CASE_NUMBER}/${TOTAL_CASES} ${description}"
    if (( PEER_READY_DELAY > 0 )); then
        record "PEER_READY_WAIT case=${CASE_NUMBER}/${TOTAL_CASES} seconds=${PEER_READY_DELAY}"
        if (( DRY_RUN == 0 )); then
            sleep "${PEER_READY_DELAY}"
        fi
    fi
    if (( DRY_RUN )); then
        printf '  '
        printf '%q ' "${command[@]}"
        printf '\n'
        mark_key_built "${key}"
        record "CASE_DRY_RUN ${CASE_NUMBER}/${TOTAL_CASES}"
        return 0
    fi

    set +e
    "${command[@]}"
    rc=$?
    set -e
    if (( rc != 0 )); then
        record "CASE_FAIL ${CASE_NUMBER}/${TOTAL_CASES} rc=${rc} ${description}"
        record "SUITE_DONE experiment=${EXPERIMENT} role=${ROLE} status=FAIL"
        exit "${rc}"
    fi
    mark_key_built "${key}"
    record "CASE_PASS ${CASE_NUMBER}/${TOTAL_CASES} ${description}"
}

ordered_values_for_run() {
    local run="$1"
    shift
    local input=("$@")
    local index
    ORDERED_VALUES=()
    if (( run % 2 == 1 )); then
        ORDERED_VALUES=("${input[@]}")
    else
        for (( index=${#input[@]}-1; index>=0; index-- )); do
            ORDERED_VALUES+=("${input[index]}")
        done
    fi
}

record "SUITE_START experiment=${EXPERIMENT} role=${ROLE} environment=${ENVIRONMENT} distance=${DISTANCE} run_start=${RUN_START} repeats=${REPEATS} cases=${TOTAL_CASES}"
if [[ "${EXPERIMENT}" != "stage0" ]]; then
    record "SUITE_PARAMETER rx_lead_us=${FIXED_LEAD_US}"
fi
record "SUITE_PARAMETER peer_ready_delay_s=${PEER_READY_DELAY}"
record "NOTE Start every TX/sensor suite before the matching RX/INIT suite."
(( DRY_RUN == 1 )) || record "MANIFEST ${MANIFEST}"

# Exp4 runs on separate computers. Build the complete condition matrix before
# the first radio run so later conditions only flash and cannot outrun the
# matching computer while it is still compiling multiple sensor images.
if [[ "${EXPERIMENT}" == "exp4" && ${NO_BUILD} -eq 0 ]]; then
    if (( DRY_RUN )); then
        record "SUITE_PREBUILD exp4 role=${ROLE} preambles=${PREAMBLES[*]} status=DRY_RUN"
    else
        record "SUITE_PREBUILD_START exp4 role=${ROLE} preambles=${PREAMBLES[*]}"
        for preamble in "${PREAMBLES[@]}"; do
            if [[ "${ROLE}" == "tx-auto" ]]; then
                build_role="tx"
            else
                build_role="${ROLE}"
            fi
            "${SCRIPT_DIR}/brrs_exp4_build.sh" \
                "${preamble}" "${SENSOR_COUNT}" "${GUARD_US}" "${build_role}" "${FIXED_LEAD_US}"
            mark_key_built "exp4_${ROLE}_m${preamble}_s${SENSOR_COUNT}_g${GUARD_US}_l${FIXED_LEAD_US}"
        done
        record "SUITE_PREBUILD_DONE exp4 role=${ROLE} status=PASS"
    fi
fi

for (( offset=0; offset<REPEATS; offset++ )); do
    run=$((RUN_START + offset))
    case "${EXPERIMENT}" in
        stage0)
            ordered_values_for_run "${run}" "${LEADS[@]}"
            for lead in "${ORDERED_VALUES[@]}"; do
                if [[ "${ROLE}" == "tx" ]]; then
                    key="stage0_tx"
                else
                    key="stage0_rx_l${lead}_t${TAIL_US}"
                fi
                args=("${ROLE}" "${lead}" "${run}" "${ENVIRONMENT}")
                [[ "${DISTANCE}" == "na" ]] || args+=("${DISTANCE}")
                [[ "${TAIL_US}" == "0" ]] || args+=(--tail "${TAIL_US}")
                run_case "${key}" "stage0 lead=${lead} tail=${TAIL_US} run=${run}" \
                    "${SCRIPT_DIR}/brrs_stage0_capture.sh" "${args[@]}"
            done
            ;;
        exp1|exp2)
            ordered_values_for_run "${run}" "${PREAMBLES[@]}"
            for preamble in "${ORDERED_VALUES[@]}"; do
                if [[ "${ROLE}" == "tx" ]]; then
                    key="${EXPERIMENT}_tx"
                else
                    key="${EXPERIMENT}_rx_m${preamble}_l${FIXED_LEAD_US}"
                fi
                args=("${ROLE}" "${preamble}" "${run}" "${ENVIRONMENT}")
                [[ "${DISTANCE}" == "na" ]] || args+=("${DISTANCE}")
                args+=(--lead "${FIXED_LEAD_US}")
                if [[ "${EXPERIMENT}" == "exp2" && "${METHOD}" != "pylink" ]]; then
                    args+=(--method "${METHOD}")
                fi
                run_case "${key}" "${EXPERIMENT} M=${preamble} run=${run}" \
                    "${SCRIPT_DIR}/brrs_${EXPERIMENT}_capture.sh" "${args[@]}"
            done
            ;;
        exp3)
            if (( REPEATS == 2 )); then
                if (( offset == 0 )); then VARIANTS=(A B C); else VARIANTS=(C B A); fi
            else
                case $((offset % 3)) in
                    0) VARIANTS=(A B C) ;;
                    1) VARIANTS=(B C A) ;;
                    2) VARIANTS=(C A B) ;;
                esac
            fi
            for variant in "${VARIANTS[@]}"; do
                if [[ "${ROLE}" == "rx" ]]; then
                    key="exp3_${ROLE}_${variant}_l${FIXED_LEAD_US}"
                else
                    key="exp3_${ROLE}_${variant}"
                fi
                args=("${ROLE}" "${variant}" "${run}" "${ENVIRONMENT}")
                [[ "${DISTANCE}" == "na" ]] || args+=("${DISTANCE}")
                args+=(--lead "${FIXED_LEAD_US}")
                run_case "${key}" "exp3 variant=${variant} run=${run}" \
                    "${SCRIPT_DIR}/brrs_exp3_capture.sh" "${args[@]}"
            done
            ;;
        exp4)
            ordered_values_for_run "${run}" "${PREAMBLES[@]}"
            for preamble in "${ORDERED_VALUES[@]}"; do
                key="exp4_${ROLE}_m${preamble}_s${SENSOR_COUNT}_g${GUARD_US}_l${FIXED_LEAD_US}"
                if [[ "${ROLE}" == "tx-auto" ]]; then
                    args=("${preamble}" "${SENSOR_COUNT}" "${run}" "${ENVIRONMENT}")
                    [[ "${DISTANCE}" == "na" ]] || args+=("${DISTANCE}")
                    [[ "${GUARD_US}" == "200" ]] || args+=(--guard "${GUARD_US}")
                    args+=(--lead "${FIXED_LEAD_US}")
                    run_case "${key}" "exp4 auto-TX M=${preamble} sensors=${SENSOR_COUNT} guard=${GUARD_US} run=${run}" \
                        "${SCRIPT_DIR}/brrs_exp4_multi_tx.sh" "${args[@]}"
                else
                    args=("${ROLE}" "${preamble}" "${SENSOR_COUNT}" "${run}" "${ENVIRONMENT}")
                    [[ "${DISTANCE}" == "na" ]] || args+=("${DISTANCE}")
                    [[ "${GUARD_US}" == "200" ]] || args+=(--guard "${GUARD_US}")
                    args+=(--lead "${FIXED_LEAD_US}")
                    run_case "${key}" "exp4 M=${preamble} sensors=${SENSOR_COUNT} guard=${GUARD_US} run=${run}" \
                        "${SCRIPT_DIR}/brrs_exp4_capture.sh" "${args[@]}"
                fi
            done
            ;;
    esac
done

if (( DRY_RUN )); then
    record "SUITE_DONE experiment=${EXPERIMENT} role=${ROLE} cases=${TOTAL_CASES} status=DRY_RUN"
else
    record "SUITE_DONE experiment=${EXPERIMENT} role=${ROLE} cases=${TOTAL_CASES} status=PASS"
    record "All case logs passed their experiment-specific verifier."
fi
