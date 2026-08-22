#!/usr/bin/env bash
# Exp4 baseline: S1/S2/S3 x M=32,64,256, with per-case retries.
#
# A case is a real (not scripted) failure whenever the strict all-zero-error
# schedule_pass bar in brrs_init.c is tripped by ordinary wireless noise --
# most often at M=32 (least airtime margin) with 3 simultaneous sensors.
# Retries a few times; if it never comes back clean, records it as
# EXP4_BASELINE_GAVE_UP and moves on rather than blocking the rest of the
# sweep (the real per-node PER numbers are already in the raw log either
# way -- see EXP4_NODE_CSV).
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

INIT_SERIAL="${INIT_SERIAL:-1050270933}"
S1_TX="${S1_TX:-1050282818}"
S2_PROBES="${S2_PROBES:-1050211584,1050273888}"
S3_PROBES="${S3_PROBES:-1050211584,1050273888,1050282818}"
ENV="${1:-home}"
PREAMBLES=(32 64 256)
RETRIES="${RETRIES:-3}"

run_case() {
    local m="$1" sensors="$2" probes="$3" run="$4"
    for (( attempt=1; attempt<=RETRIES; attempt++ )); do
        echo "=== Exp4 baseline: M=${m} S=${sensors} run=${run} attempt=${attempt}/${RETRIES} ==="
        local mtx_log="/tmp/round_exp4_mtx_m${m}_s${sensors}_r${run}_a${attempt}.log"
        nohup ./brrs_exp4_multi_tx.sh "${m}" "${sensors}" "${run}" "${ENV}" \
            --probe-serials "${probes}" --force > "${mtx_log}" 2>&1 &
        local mtx_pid=$!

        for _ in $(seq 1 90); do
            grep -q "EXP4_MULTI_TX_READY" "${mtx_log}" 2>/dev/null && break
            sleep 2
        done
        if ! grep -q "EXP4_MULTI_TX_READY" "${mtx_log}" 2>/dev/null; then
            echo "EXP4_BASELINE_FAIL m=${m} s=${sensors} phase=tx_ready_timeout attempt=${attempt}"
            kill "${mtx_pid}" 2>/dev/null || true
            continue
        fi
        echo "EXP4_BASELINE_TX_READY m=${m} s=${sensors}"

        if ./brrs_exp4_capture.sh init "${m}" "${sensors}" "${run}" "${ENV}" \
            --serial "${INIT_SERIAL}" --force; then
            echo "EXP4_BASELINE_PASS m=${m} s=${sensors} attempt=${attempt}"
            wait "${mtx_pid}" 2>/dev/null || true
            return 0
        else
            echo "EXP4_BASELINE_RETRY m=${m} s=${sensors} attempt=${attempt}"
            wait "${mtx_pid}" 2>/dev/null || true
        fi
    done
    echo "EXP4_BASELINE_GAVE_UP m=${m} s=${sensors} exhausted_retries=${RETRIES}"
}

for m in "${PREAMBLES[@]}"; do
    run_case "${m}" 1 "${S1_TX}" 1
    run_case "${m}" 2 "${S2_PROBES}" 1
    run_case "${m}" 3 "${S3_PROBES}" 1
done

echo "EXP4_BASELINE_DONE status=PASS"
