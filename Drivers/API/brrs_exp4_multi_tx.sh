#!/usr/bin/env bash

# Exp4 multi-sensor TX orchestrator: discover, rotate, flash, and capture all nodes.

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

usage() {
    cat <<EOF
Usage:
  $(basename "$0") <32|64|128|256> <sensor-count:1..7> <run> \
      <environment> [distance] [options]

Options:
  --guard <us>              Inter-slot guard (default: 200).
  --lead <us>               Coordinator RX lead margin (default: 15).
  --pac <4|8>               Coordinator DATA RX PAC size (default: 8).
  --sync-buffer <us>        SYNC-to-first-DATA budget (default: 3000).
  --sync-prep <us>          DATA-to-next-SYNC reserve (default: 2500).
  --cycles <n>              Superframes to collect (default: 1000).
  --max-per-percent <n>     Recorded verifier PER ceiling (default: 5.0).
  --sequence <digits>       Custom per-slot owner schedule, e.g. 232323.
  --spi-opt                 Use the matching persistent-SPIM image set.
  --irq                     Use the matching GPIO IRQ pending-event image set.
  --phy-profile             Use dwt_configure internal profiling images.
  --rx-path-profile         Use polling RX service profiling images.
  --phy-fast-switch         Use the BRRS delta PHY switch path.
  --phy-fast-skip-pgf       Skip delta-path PGF calibration (requires fast switch).
  --timeout <seconds>       Sensor capture timeout (default: 180).
  --probe-serials <csv>     Diagnostic override; default discovers every USB probe.
  --no-build                Reuse previously built node images.
  --force                   Preserve existing artifacts and repeat the run.
  -h, --help                Show this help.

The TX computer must have exactly sensor-count J-Link probes connected. Probes
are sorted by serial number and cyclically rotated across roles for each run.
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

PREAMBLE="$1"
SENSOR_COUNT="$2"
RUN_NUMBER="$3"
ENVIRONMENT="$4"
shift 4

DISTANCE="na"
GUARD_US=200
LEAD_US=15
PAC=8
SEQUENCE=""
TIMEOUT=180
PROBE_SERIALS=""
NO_BUILD=0
FORCE=0
SPI_OPT=0
IRQ_PENDING=0
PHY_PROFILE=0
RX_PATH_PROFILE=0
PHY_FAST_SWITCH=0
PHY_FAST_SKIP_PGF=0
TARGET_CYCLES=1000
SYNC_BUFFER_US=3000
SYNC_PREP_US=2500
MAX_PER_PERCENT=5.0
if (( $# > 0 )) && [[ "$1" != --* ]]; then
    DISTANCE="$1"
    shift
fi
while (( $# > 0 )); do
    case "$1" in
        --guard)
            (( $# >= 2 )) || { echo "--guard requires a value" >&2; exit 2; }
            GUARD_US="$2"; shift 2 ;;
        --lead)
            (( $# >= 2 )) || { echo "--lead requires a value" >&2; exit 2; }
            LEAD_US="$2"; shift 2 ;;
        --pac)
            (( $# >= 2 )) || { echo "--pac requires a value" >&2; exit 2; }
            PAC="$2"; shift 2 ;;
        --sync-buffer)
            (( $# >= 2 )) || { echo "--sync-buffer requires a value" >&2; exit 2; }
            SYNC_BUFFER_US="$2"; shift 2 ;;
        --sync-prep)
            (( $# >= 2 )) || { echo "--sync-prep requires a value" >&2; exit 2; }
            SYNC_PREP_US="$2"; shift 2 ;;
        --cycles)
            (( $# >= 2 )) || { echo "--cycles requires a value" >&2; exit 2; }
            TARGET_CYCLES="$2"; shift 2 ;;
        --max-per-percent)
            (( $# >= 2 )) || { echo "--max-per-percent requires a value" >&2; exit 2; }
            MAX_PER_PERCENT="$2"; shift 2 ;;
        --sequence)
            (( $# >= 2 )) || { echo "--sequence requires a value" >&2; exit 2; }
            SEQUENCE="$2"; shift 2 ;;
        --timeout)
            (( $# >= 2 )) || { echo "--timeout requires a value" >&2; exit 2; }
            TIMEOUT="$2"; shift 2 ;;
        --probe-serials)
            (( $# >= 2 )) || { echo "--probe-serials requires a value" >&2; exit 2; }
            PROBE_SERIALS="$2"; shift 2 ;;
        --no-build) NO_BUILD=1; shift ;;
        --force) FORCE=1; shift ;;
        --spi-opt) SPI_OPT=1; shift ;;
        --irq) IRQ_PENDING=1; shift ;;
        --phy-profile) PHY_PROFILE=1; shift ;;
        --rx-path-profile) RX_PATH_PROFILE=1; shift ;;
        --phy-fast-switch) PHY_FAST_SWITCH=1; shift ;;
        --phy-fast-skip-pgf) PHY_FAST_SKIP_PGF=1; shift ;;
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
    || { echo "invalid environment name" >&2; exit 2; }
if [[ "${DISTANCE}" != "na" && ! "${DISTANCE}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "distance must be a non-negative number in meters" >&2
    exit 2
fi
[[ "${GUARD_US}" =~ ^[0-9]+$ ]] && (( GUARD_US <= 1000 )) \
    || { echo "guard must be between 0 and 1000 us" >&2; exit 2; }
[[ "${LEAD_US}" =~ ^[0-9]+$ ]] && (( LEAD_US <= 1000 )) \
    || { echo "lead must be between 0 and 1000 us" >&2; exit 2; }
case "${PAC}" in
    4|8) ;;
    *) echo "pac must be 4 or 8" >&2; exit 2 ;;
esac
if [[ -n "${SEQUENCE}" ]]; then
    [[ "${SEQUENCE}" =~ ^[2-8]+$ ]] \
        || { echo "sequence must contain only node digits 2-8" >&2; exit 2; }
    (( ${#SEQUENCE} <= 32 )) \
        || { echo "sequence must contain at most 32 slots" >&2; exit 2; }
    max_node=$((SENSOR_COUNT + 1))
    for (( index=0; index<${#SEQUENCE}; index++ )); do
        (( ${SEQUENCE:index:1} <= max_node )) \
            || { echo "sequence references a node outside S${SENSOR_COUNT}" >&2; exit 2; }
    done
fi
if (( SENSOR_COUNT > 1 && GUARD_US < LEAD_US )); then
    echo "multi-sensor guard must be at least the ${LEAD_US} us RX lead margin" >&2
    exit 2
fi
[[ "${SYNC_BUFFER_US}" =~ ^[1-9][0-9]*$ ]] && (( SYNC_BUFFER_US < 10000 )) \
    || { echo "sync buffer must be between 1 and 9999 us" >&2; exit 2; }
[[ "${SYNC_PREP_US}" =~ ^[1-9][0-9]*$ ]] && (( SYNC_PREP_US < 10000 )) \
    || { echo "sync prep must be between 1 and 9999 us" >&2; exit 2; }
(( SYNC_BUFFER_US + SYNC_PREP_US < 10000 )) \
    || { echo "sync buffer + sync prep must leave a positive DATA budget" >&2; exit 2; }
[[ "${TARGET_CYCLES}" =~ ^[1-9][0-9]*$ ]] && (( TARGET_CYCLES <= 10000 )) \
    || { echo "cycles must be an integer between 1 and 10000" >&2; exit 2; }
[[ "${MAX_PER_PERCENT}" =~ ^([0-9]+)([.][0-9]+)?$ ]] \
    || { echo "max PER percent must be numeric" >&2; exit 2; }
awk -v value="${MAX_PER_PERCENT}" 'BEGIN { exit !(value >= 0 && value <= 100) }' \
    || { echo "max PER percent must be between 0 and 100" >&2; exit 2; }
if (( PHY_FAST_SKIP_PGF && ! PHY_FAST_SWITCH )); then
    echo "--phy-fast-skip-pgf requires --phy-fast-switch" >&2
    exit 2
fi
if (( RX_PATH_PROFILE && IRQ_PENDING )); then
    echo "--rx-path-profile cannot be combined with --irq" >&2
    exit 2
fi
[[ "${TIMEOUT}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "timeout must be a positive integer" >&2; exit 2; }

ASSIGN_ARGS=(--sensors "${SENSOR_COUNT}" --run "${RUN_NUMBER}" --format tsv)
[[ -z "${PROBE_SERIALS}" ]] || ASSIGN_ARGS+=(--serials "${PROBE_SERIALS}")
ASSIGNMENT_OUTPUT="$(python3 "${SCRIPT_DIR}/brrs_exp4_probe_assign.py" "${ASSIGN_ARGS[@]}")"

ROLES=()
SERIALS=()
while IFS=$'\t' read -r role serial; do
    [[ -n "${role}" && -n "${serial}" ]] || continue
    ROLES+=("${role}")
    SERIALS+=("${serial}")
done <<<"${ASSIGNMENT_OUTPUT}"
(( ${#ROLES[@]} == SENSOR_COUNT )) \
    || { echo "failed to create ${SENSOR_COUNT} probe assignments" >&2; exit 1; }

DATE_TAG="$(date '+%Y%m%d')"
DISTANCE_TAG=""
[[ "${DISTANCE}" != "na" ]] && DISTANCE_TAG="_${DISTANCE}m"
OUTDIR="${SDK_ROOT}/../logs/exp4_${ENVIRONMENT}${DISTANCE_TAG}_g${GUARD_US}_l${LEAD_US}_pac${PAC}_sb${SYNC_BUFFER_US}_sp${SYNC_PREP_US}_${DATE_TAG}"
if [[ -n "${SEQUENCE}" ]]; then
    OUTDIR="${SDK_ROOT}/../logs/exp4_${ENVIRONMENT}${DISTANCE_TAG}_g${GUARD_US}_l${LEAD_US}_pac${PAC}_sb${SYNC_BUFFER_US}_sp${SYNC_PREP_US}_seq${SEQUENCE}_${DATE_TAG}"
fi
if (( SPI_OPT )); then
    OUTDIR+="_spiopt"
fi
if (( IRQ_PENDING )); then
    OUTDIR+="_irq"
fi
if (( PHY_PROFILE )); then
    OUTDIR+="_phyprofile"
fi
if (( RX_PATH_PROFILE )); then
    OUTDIR+="_rxprofile"
fi
if (( PHY_FAST_SWITCH )); then
    OUTDIR+="_phyfast"
fi
if (( PHY_FAST_SKIP_PGF )); then
    OUTDIR+="_skippgf"
fi
if (( TARGET_CYCLES != 1000 )); then
    OUTDIR+="_cycles${TARGET_CYCLES}"
fi
mkdir -p "${OUTDIR}"
BASE="exp4_${PREAMBLE}_s${SENSOR_COUNT}_r${RUN_NUMBER}_multi_tx"
ASSIGNMENT_FILE="${OUTDIR}/${BASE}.assignments.csv"
ORCHESTRATOR_LOG="${OUTDIR}/${BASE}.console.log"

if [[ -e "${ASSIGNMENT_FILE}" || -e "${ORCHESTRATOR_LOG}" ]]; then
    if (( FORCE )); then
        BACKUP_TAG="$(date '+%H%M%S')"
        for artifact in "${ASSIGNMENT_FILE}" "${ORCHESTRATOR_LOG}"; do
            [[ ! -e "${artifact}" ]] || mv "${artifact}" "${artifact}.prev.${BACKUP_TAG}"
        done
    else
        echo "refusing to overwrite ${ASSIGNMENT_FILE}" >&2
        echo "  (retry with --force or increment the run number)" >&2
        exit 1
    fi
fi

exec > >(tee -a "${ORCHESTRATOR_LOG}") 2>&1

ROTATION=$(( (RUN_NUMBER - 1) % SENSOR_COUNT ))
echo "[multi-tx] Exp4 ${PREAMBLE} sym / S${SENSOR_COUNT} / run ${RUN_NUMBER} / lead ${LEAD_US} us / PAC ${PAC} / sync ${SYNC_BUFFER_US}+${SYNC_PREP_US} us / cycles ${TARGET_CYCLES}"
echo "[multi-tx] slot sequence: ${SEQUENCE:-default-round-robin}"
echo "[multi-tx] probe source: $([[ -z "${PROBE_SERIALS}" ]] && echo auto-discovery || echo diagnostic-override)"
echo "[multi-tx] cyclic rotation: ${ROTATION}"
printf 'run,preamble_symbols,sensor_count,sync_buffer_us,sync_prep_us,rotation,role,serial\n' >"${ASSIGNMENT_FILE}"
for (( index=0; index<SENSOR_COUNT; index++ )); do
    role="${ROLES[index]}"
    serial="${SERIALS[index]}"
    echo "EXP4_PROBE_ASSIGNMENT_CSV,run=${RUN_NUMBER},rotation=${ROTATION},role=${role},serial=${serial}"
    printf '%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "${RUN_NUMBER}" "${PREAMBLE}" "${SENSOR_COUNT}" \
        "${SYNC_BUFFER_US}" "${SYNC_PREP_US}" "${ROTATION}" \
        "${role}" "${serial}" >>"${ASSIGNMENT_FILE}"
done

if (( NO_BUILD == 0 )); then
    echo "[build] preparing every TX role once"
    BUILD_ARGS=("${PREAMBLE}" "${SENSOR_COUNT}" "${GUARD_US}" tx "${LEAD_US}"
        --pac "${PAC}" --sync-buffer "${SYNC_BUFFER_US}" --sync-prep "${SYNC_PREP_US}"
        --cycles "${TARGET_CYCLES}")
    [[ -n "${SEQUENCE}" ]] && BUILD_ARGS+=(--sequence "${SEQUENCE}")
    (( SPI_OPT == 0 )) || BUILD_ARGS+=(--spi-opt)
    (( IRQ_PENDING == 0 )) || BUILD_ARGS+=(--irq)
    (( PHY_PROFILE == 0 )) || BUILD_ARGS+=(--phy-profile)
    (( RX_PATH_PROFILE == 0 )) || BUILD_ARGS+=(--rx-path-profile)
    (( PHY_FAST_SWITCH == 0 )) || BUILD_ARGS+=(--phy-fast-switch)
    (( PHY_FAST_SKIP_PGF == 0 )) || BUILD_ARGS+=(--phy-fast-skip-pgf)
    "${SCRIPT_DIR}/brrs_exp4_build.sh" "${BUILD_ARGS[@]}"
fi

PIDS=()
CONSOLE_LOGS=()
cleanup_children() {
    local pid
    for pid in "${PIDS[@]:-}"; do
        kill_process_tree "${pid}"
    done
    wait 2>/dev/null || true
}
kill_process_tree() {
    local parent="$1"
    local child
    while IFS= read -r child; do
        [[ -n "${child}" ]] && kill_process_tree "${child}"
    done < <(pgrep -P "${parent}" 2>/dev/null || true)
    kill -0 "${parent}" 2>/dev/null && kill "${parent}" 2>/dev/null || true
}
trap 'cleanup_children; exit 130' INT TERM

for (( index=0; index<SENSOR_COUNT; index++ )); do
    role="${ROLES[index]}"
    serial="${SERIALS[index]}"
    role_lower="$(printf '%s' "${role}" | tr '[:upper:]' '[:lower:]')"
    console_log="${OUTDIR}/${BASE}_${role_lower}.worker.log"
    if [[ -e "${console_log}" ]]; then
        if (( FORCE )); then
            mv "${console_log}" "${console_log}.prev.$(date '+%H%M%S')"
        else
            echo "refusing to overwrite ${console_log}" >&2
            exit 1
        fi
    fi
    command=(
        "${SCRIPT_DIR}/brrs_exp4_capture.sh"
        "${role}" "${PREAMBLE}" "${SENSOR_COUNT}" "${RUN_NUMBER}"
        "${ENVIRONMENT}"
    )
    [[ "${DISTANCE}" == "na" ]] || command+=("${DISTANCE}")
    command+=(--guard "${GUARD_US}" --lead "${LEAD_US}" --pac "${PAC}"
        --sync-buffer "${SYNC_BUFFER_US}" --sync-prep "${SYNC_PREP_US}"
        --cycles "${TARGET_CYCLES}"
        --max-per-percent "${MAX_PER_PERCENT}"
        --serial "${serial}" --timeout "${TIMEOUT}" --no-build)
    [[ -n "${SEQUENCE}" ]] && command+=(--sequence "${SEQUENCE}")
    (( SPI_OPT == 0 )) || command+=(--spi-opt)
    (( IRQ_PENDING == 0 )) || command+=(--irq)
    (( PHY_PROFILE == 0 )) || command+=(--phy-profile)
    (( RX_PATH_PROFILE == 0 )) || command+=(--rx-path-profile)
    (( PHY_FAST_SWITCH == 0 )) || command+=(--phy-fast-switch)
    (( PHY_FAST_SKIP_PGF == 0 )) || command+=(--phy-fast-skip-pgf)
    (( FORCE == 0 )) || command+=(--force)
    # J-Link OB probes on one USB host can intermittently lose a connection
    # when several flash operations run concurrently. Start each worker and
    # wait for its RTT READY marker before flashing the next board. Workers
    # remain alive and listen together once INIT begins, so this serializes
    # only setup, not the radio experiment. A transient 0 V/J-Link timeout is
    # retried only before READY; a running radio experiment is never retried.
    startup_ready=0
    for startup_attempt in 1 2 3; do
        worker_console_log="${console_log}"
        (( startup_attempt == 1 )) \
            || worker_console_log="${console_log}.setup_retry${startup_attempt}"
        if [[ -e "${worker_console_log}" ]]; then
            mv "${worker_console_log}" \
               "${worker_console_log}.prev.$(date '+%H%M%S')"
        fi

        retry_command=("${command[@]}")
        if (( startup_attempt > 1 && FORCE == 0 )); then
            retry_command+=(--force)
        fi
        "${retry_command[@]}" >"${worker_console_log}" 2>&1 &
        worker_pid="$!"
        echo "[multi-tx] started ${role} on J-Link ${serial} (setup ${startup_attempt}/3)"

        STARTUP_DEADLINE=$(( $(date '+%s') + 60 ))
        while kill -0 "${worker_pid}" 2>/dev/null; do
            if grep -q "READY marker seen" "${worker_console_log}" 2>/dev/null; then
                startup_ready=1
                break
            fi
            if (( $(date '+%s') >= STARTUP_DEADLINE )); then
                kill_process_tree "${worker_pid}"
                break
            fi
            sleep 0.2
        done

        if (( startup_ready )); then
            PIDS+=("${worker_pid}")
            CONSOLE_LOGS+=("${worker_console_log}")
            echo "[multi-tx] ${role} READY on J-Link ${serial} (setup ${startup_attempt}/3)"
            break
        fi

        wait "${worker_pid}" 2>/dev/null || true
        echo "WARN: ${role} did not reach READY on setup ${startup_attempt}/3" >&2
        cat "${worker_console_log}" >&2
        if (( startup_attempt < 3 )); then
            sleep 2
        fi
    done
    if (( startup_ready == 0 )); then
        echo "ERROR: ${role} did not reach READY after 3 setup attempts" >&2
        cleanup_children
        exit 1
    fi
done

READY_DEADLINE=$(( $(date '+%s') + 60 ))
while :; do
    ready_count=0
    for (( index=0; index<SENSOR_COUNT; index++ )); do
        if ! kill -0 "${PIDS[index]}" 2>/dev/null; then
            echo "ERROR: ${ROLES[index]} exited before READY" >&2
            cat "${CONSOLE_LOGS[index]}" >&2
            cleanup_children
            exit 1
        elif grep -q "READY marker seen" "${CONSOLE_LOGS[index]}" 2>/dev/null; then
            ready_count=$((ready_count + 1))
        fi
    done
    (( ready_count == SENSOR_COUNT )) && break
    if (( $(date '+%s') >= READY_DEADLINE )); then
        echo "ERROR: only ${ready_count}/${SENSOR_COUNT} TX nodes reached READY in 60 seconds" >&2
        cleanup_children
        exit 1
    fi
    sleep 0.2
done

echo "EXP4_MULTI_TX_READY,run=${RUN_NUMBER},preamble=${PREAMBLE},sensors=${SENSOR_COUNT},status=PASS"
echo "[multi-tx] all nodes are listening for the INIT beacon; start INIT now"

FAILED=0
for (( index=0; index<SENSOR_COUNT; index++ )); do
    if wait "${PIDS[index]}"; then
        verify_line="$(grep -F '[verify] PASS:' "${CONSOLE_LOGS[index]}" | tail -1 || true)"
        echo "[multi-tx] ${ROLES[index]} PASS ${verify_line}"
    else
        FAILED=1
        echo "[multi-tx] ${ROLES[index]} FAIL; console follows" >&2
        cat "${CONSOLE_LOGS[index]}" >&2
    fi
done
trap - INT TERM

if (( FAILED )); then
    echo "EXP4_MULTI_TX_DONE,run=${RUN_NUMBER},preamble=${PREAMBLE},sensors=${SENSOR_COUNT},status=FAIL"
    exit 1
fi

echo "EXP4_MULTI_TX_DONE,run=${RUN_NUMBER},preamble=${PREAMBLE},sensors=${SENSOR_COUNT},status=PASS"
echo "[done] assignments=${ASSIGNMENT_FILE}"
echo "[done] console=${ORCHESTRATOR_LOG}"
