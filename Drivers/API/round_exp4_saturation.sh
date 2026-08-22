#!/usr/bin/env bash
# Today's Exp4 saturation + fail-closed tests using --sequence.
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

INIT_SERIAL="1050270933"
N2_SERIAL="1050211584"
N3_SERIAL="1050273888"
ENV="home"

run_saturation_case() {
    local m="$1" sequence="$2" run="$3"
    echo "=== Saturation: M=${m} sequence=${sequence} (${#sequence} slots) ==="

    # brrs_exp4_capture.sh's build step shares one SES "Debug" config across
    # every Exp4 role, so two concurrent builds race and corrupt each
    # other's intermediate output. Build N2 then N3 sequentially first
    # (this also produces each role's .hex), then launch the two *captures*
    # in parallel with --no-build so they can run concurrently safely.
    ./brrs_exp4_build.sh "${m}" 2 200 N2 15 >/tmp/round_exp4_sat_m${m}_build_n2.log 2>&1
    ./brrs_exp4_build.sh "${m}" 2 200 N3 15 >/tmp/round_exp4_sat_m${m}_build_n3.log 2>&1

    local n2_log="/tmp/round_exp4_sat_m${m}_n2.log"
    local n3_log="/tmp/round_exp4_sat_m${m}_n3.log"
    nohup ./brrs_exp4_capture.sh N2 "${m}" 2 "${run}" "${ENV}" \
        --serial "${N2_SERIAL}" --no-build --force > "${n2_log}" 2>&1 &
    local n2_pid=$!
    nohup ./brrs_exp4_capture.sh N3 "${m}" 2 "${run}" "${ENV}" \
        --serial "${N3_SERIAL}" --no-build --force > "${n3_log}" 2>&1 &
    local n3_pid=$!

    for _ in $(seq 1 90); do
        grep -q "READY marker seen" "${n2_log}" 2>/dev/null && \
        grep -q "READY marker seen" "${n3_log}" 2>/dev/null && break
        sleep 2
    done
    if ! { grep -q "READY marker seen" "${n2_log}" 2>/dev/null && \
           grep -q "READY marker seen" "${n3_log}" 2>/dev/null; }; then
        echo "SATURATION_FAIL m=${m} phase=tx_ready_timeout"
        kill "${n2_pid}" "${n3_pid}" 2>/dev/null || true
        return 1
    fi
    echo "SATURATION_TX_READY m=${m}"

    if ./brrs_exp4_capture.sh init "${m}" 2 "${run}" "${ENV}" \
        --serial "${INIT_SERIAL}" --sequence "${sequence}" --force; then
        echo "SATURATION_PASS m=${m} sequence=${sequence}"
    else
        echo "SATURATION_FAIL m=${m} sequence=${sequence} phase=init"
    fi
    wait "${n2_pid}" "${n3_pid}" 2>/dev/null || true
}

# M=32, guard=200 -> documented max 15 slots.
run_saturation_case 32 "232323232323232" 1

# M=64, guard=200 -> documented max 13 slots (already spot-checked earlier
# this session; re-confirm now that STARTUP_GRACE_MS/bash-3.2 fixes landed).
run_saturation_case 64 "2323232323232" 1

# Fail-closed: 14 slots at M=64/guard=200 exceeds the documented max (13).
# The build itself must reject this (static_assert), not the runtime.
echo "=== Fail-closed check: M=64, 14-slot sequence should fail to BUILD ==="
if ./brrs_exp4_build.sh 64 2 200 init 15 --sequence 23232323232323 \
    > /tmp/round_exp4_failclosed_build.log 2>&1; then
    echo "FAILCLOSED_UNEXPECTED_SUCCESS: build should have been rejected"
else
    echo "FAILCLOSED_PASS: build correctly rejected the oversized sequence"
    grep -i "static_assert\|error" /tmp/round_exp4_failclosed_build.log | head -5
fi

echo "SATURATION_SUITE_DONE"
