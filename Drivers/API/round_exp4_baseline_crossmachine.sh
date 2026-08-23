#!/usr/bin/env bash
# Cross-machine rerun of Exp4 baseline (S1/S2/S3 x M=32,64,256): sensors/TX on
# the Air (via SSH), INIT local on this machine (the Pro). Mirrors
# round_exp4_baseline.sh's retry logic, just with the multi-tx side launched
# remotely instead of locally.
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

AIR_HOST="s-macbook-air"
AIR_DIR="~/Desktop/DWM3000/DW3_QM33_SDK_1.0.2/Drivers/API"
INIT_SERIAL="1050270933"
S1_TX="1050282818"
S2_PROBES="1050211584,1050273888"
S3_PROBES="1050211584,1050273888,1050282818"
ENV="${1:-home}"
DISTANCE="${2:-}"
RUN="${3:-2}"
PREAMBLES=(32 64 256)
RETRIES=3

run_case() {
    local m="$1" sensors="$2" probes="$3" run="$4"
    for (( attempt=1; attempt<=RETRIES; attempt++ )); do
        echo "=== Exp4 baseline (cross): M=${m} S=${sensors} run=${run} attempt=${attempt}/${RETRIES} ==="
        local mtx_log="/tmp/round_exp4x_mtx_m${m}_s${sensors}_r${run}_a${attempt}.log"
        ssh "${AIR_HOST}" "cd ${AIR_DIR} && nohup ./brrs_exp4_multi_tx.sh ${m} ${sensors} ${run} ${ENV} ${DISTANCE} --probe-serials ${probes} --force > ${mtx_log} 2>&1 &"

        local ready=0
        for _ in $(seq 1 90); do
            if ssh "${AIR_HOST}" "grep -q 'EXP4_MULTI_TX_READY' ${mtx_log} 2>/dev/null"; then
                ready=1
                break
            fi
            sleep 2
        done
        if [[ "${ready}" != "1" ]]; then
            echo "EXP4_BASELINE_FAIL m=${m} s=${sensors} phase=tx_ready_timeout attempt=${attempt}"
            ssh "${AIR_HOST}" "pkill -9 -f brrs_exp4_multi_tx.sh 2>/dev/null; pkill -9 -f rtt_capture.py 2>/dev/null; pkill -9 -f JLinkGUIServerExe 2>/dev/null" || true
            continue
        fi
        echo "EXP4_BASELINE_TX_READY m=${m} s=${sensors}"

        if ./brrs_exp4_capture.sh init "${m}" "${sensors}" "${run}" "${ENV}" ${DISTANCE} \
            --serial "${INIT_SERIAL}" --force; then
            echo "EXP4_BASELINE_PASS m=${m} s=${sensors} attempt=${attempt}"
            return 0
        else
            echo "EXP4_BASELINE_RETRY m=${m} s=${sensors} attempt=${attempt}"
        fi
    done
    echo "EXP4_BASELINE_GAVE_UP m=${m} s=${sensors} exhausted_retries=${RETRIES}"
}

for m in "${PREAMBLES[@]}"; do
    run_case "${m}" 1 "${S1_TX}" "${RUN}"
    run_case "${m}" 2 "${S2_PROBES}" "${RUN}"
    run_case "${m}" 3 "${S3_PROBES}" "${RUN}"
done

echo "EXP4_BASELINE_DONE status=PASS"
