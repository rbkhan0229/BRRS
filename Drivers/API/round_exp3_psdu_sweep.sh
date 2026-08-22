#!/usr/bin/env bash
# One-off orchestration for today's Exp3 PSDU-length sweep (Variant A only).
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

TX_SERIAL="1050282818"
RX_SERIAL="1050270933"
ENV="home"
PAYLOADS=(5 16 31 32 72 73 100)

for payload in "${PAYLOADS[@]}"; do
    echo "=== PSDU sweep: payload=${payload} ==="
    TX_LOG="/tmp/round_exp3_psdu_tx_${payload}.log"
    nohup ./brrs_exp3_capture.sh tx A 1 "${ENV}" --payload "${payload}" \
        --serial "${TX_SERIAL}" --force > "${TX_LOG}" 2>&1 &
    tx_pid=$!

    for _ in $(seq 1 60); do
        grep -q "READY marker seen" "${TX_LOG}" 2>/dev/null && break
        sleep 2
    done
    if ! grep -q "READY marker seen" "${TX_LOG}" 2>/dev/null; then
        echo "PSDU_CASE_FAIL payload=${payload} phase=tx_ready_timeout"
        kill "${tx_pid}" 2>/dev/null || true
        exit 1
    fi
    echo "PSDU_TX_READY payload=${payload}"

    if ./brrs_exp3_capture.sh rx A 1 "${ENV}" --payload "${payload}" \
        --serial "${RX_SERIAL}" --force; then
        echo "PSDU_CASE_PASS payload=${payload}"
    else
        echo "PSDU_CASE_FAIL payload=${payload} phase=rx"
        wait "${tx_pid}" 2>/dev/null || true
        exit 1
    fi
    wait "${tx_pid}" 2>/dev/null || true
done

echo "PSDU_SWEEP_DONE status=PASS"
