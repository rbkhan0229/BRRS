#!/usr/bin/env bash
# Cross-machine rerun of Exp4 saturation + fail-closed test: N2/N3 sensors
# build+capture on the Air (via SSH), INIT local on this machine (the Pro).
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

AIR_HOST="s-macbook-air"
AIR_DIR="~/Desktop/DWM3000/DW3_QM33_SDK_1.0.2/Drivers/API"
INIT_SERIAL="1050270933"
N2_SERIAL="1050211584"
N3_SERIAL="1050273888"
ENV="${1:-home}"
DISTANCE="${2:-}"
RUN="${3:-2}"

run_saturation_case() {
    local m="$1" sequence="$2" run="$3"
    echo "=== Saturation (cross): M=${m} sequence=${sequence} (${#sequence} slots) ==="

    ssh "${AIR_HOST}" "cd ${AIR_DIR} && ./brrs_exp4_build.sh ${m} 2 200 N2 15" \
        > "/tmp/round_exp4x_sat_m${m}_build_n2.log" 2>&1
    ssh "${AIR_HOST}" "cd ${AIR_DIR} && ./brrs_exp4_build.sh ${m} 2 200 N3 15" \
        > "/tmp/round_exp4x_sat_m${m}_build_n3.log" 2>&1

    local n2_log="/tmp/round_exp4x_sat_m${m}_n2.log"
    local n3_log="/tmp/round_exp4x_sat_m${m}_n3.log"
    ssh "${AIR_HOST}" "cd ${AIR_DIR} && nohup ./brrs_exp4_capture.sh N2 ${m} 2 ${run} ${ENV} ${DISTANCE} --serial ${N2_SERIAL} --no-build --force > ${n2_log} 2>&1 &"
    ssh "${AIR_HOST}" "cd ${AIR_DIR} && nohup ./brrs_exp4_capture.sh N3 ${m} 2 ${run} ${ENV} ${DISTANCE} --serial ${N3_SERIAL} --no-build --force > ${n3_log} 2>&1 &"

    local ready=0
    for _ in $(seq 1 90); do
        if ssh "${AIR_HOST}" "grep -q 'READY marker seen' ${n2_log} 2>/dev/null && grep -q 'READY marker seen' ${n3_log} 2>/dev/null"; then
            ready=1
            break
        fi
        sleep 2
    done
    if [[ "${ready}" != "1" ]]; then
        echo "SATURATION_FAIL m=${m} phase=tx_ready_timeout"
        ssh "${AIR_HOST}" "pkill -9 -f rtt_capture.py 2>/dev/null; pkill -9 -f JLinkGUIServerExe 2>/dev/null" || true
        return 1
    fi
    echo "SATURATION_TX_READY m=${m}"

    if ./brrs_exp4_capture.sh init "${m}" 2 "${run}" "${ENV}" ${DISTANCE} \
        --serial "${INIT_SERIAL}" --sequence "${sequence}" --force; then
        echo "SATURATION_PASS m=${m} sequence=${sequence}"
    else
        echo "SATURATION_FAIL m=${m} sequence=${sequence} phase=init"
    fi
}

run_saturation_case 32 "232323232323232" "${RUN}"
run_saturation_case 64 "2323232323232" "${RUN}"

echo "=== Fail-closed check: M=64, 14-slot sequence should fail to BUILD ==="
if ssh "${AIR_HOST}" "cd ${AIR_DIR} && ./brrs_exp4_build.sh 64 2 200 init 15 --sequence 23232323232323" \
    > /tmp/round_exp4x_failclosed_build.log 2>&1; then
    echo "FAILCLOSED_UNEXPECTED_SUCCESS: build should have been rejected"
else
    echo "FAILCLOSED_PASS: build correctly rejected the oversized sequence"
    grep -i "static_assert\|error" /tmp/round_exp4x_failclosed_build.log | head -5 || true
fi

echo "SATURATION_SUITE_DONE"
