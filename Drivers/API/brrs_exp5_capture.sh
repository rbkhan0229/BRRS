#!/usr/bin/env bash
#
# brrs_exp5_capture.sh — Experiment 5 (vehicle interior multipath channel
# characterization) capture: build → flash → run → RTT ch1 캡처 → 검증.
#
# Npre is fixed at 1024 (max CIR quality) — this is NOT part of Exp2's
# preamble sweep (32/64/128/256, see brrs_exp2_capture_v3.sh). It uses its
# own BRRS_EXPERIMENT=5 / Exp5_Init build configuration so that Exp2's own
# CIR window size and logging format are never affected by Exp5's needs.
# The TX/Normal side has no preamble-specific or CIR-specific behavior (PLEN
# is negotiated at runtime from the beacon), so it intentionally reuses the
# generic Exp2_Normal build.
#
# Exp5 is a multi-run experiment: repeat this script across vehicle sensor
# positions (dashboard/door/roof/trunk/...) via <environment>, and across
# TX-RX distances via [distance]. The raw CIR supports observed PDP width,
# RMS delay-spread, and dominant-path-to-residual-energy measurements. A
# calibrated Rician K-factor or path-loss exponent additionally requires a
# propagation/reference model and is not claimed by this capture alone.
#
# 사용:
#   ./brrs_exp5_capture.sh <tx|rx> <run> <environment> [distance]
#       [--serial <S/N>] [--no-build] [--timeout <s>] [--method telnet|pylink]

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/dw3000_api.emProject"
OUTPUT_DIR="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/Output"
RTT_TELNET_PORT="${RTT_TELNET_PORT:-19021}"
EXPECTED_SAMPLES=1000
PREAMBLE=1024

usage() {
    cat <<EOF
Usage:
  $(basename "$0") <tx|rx> <run> <environment> [distance] [options]

Options:
  --lead <us>                RX lead margin (default: 15).
  --serial <S/N>             Select a J-Link when multiple probes are attached.
  --no-build                 Reuse the existing ELF and HEX.
  --timeout <seconds>        Override the capture timeout.
  --method <pylink|telnet>   Capture backend (default: pylink).
  --force                    Preserve the old log as .prev.<time> and retry.
  -h, --help                 Show this help.
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

ROLE="${1:?role tx|rx}"; RUN_NUMBER="${2:?run}"
ENVIRONMENT="${3:?environment}"; shift 3
DISTANCE="na"; SERIAL=""; NO_BUILD=0; TIMEOUT=""; METHOD="${BRRS_EXP2_CAPTURE_METHOD:-pylink}"; FORCE=0; LEAD_US=15
if (( $# > 0 )) && [[ "${1}" != --* ]]; then DISTANCE="$1"; shift; fi
while (( $# > 0 )); do
    case "$1" in
        --lead)   (( $# >= 2 )) || { echo "--lead requires a value" >&2; exit 2; }; LEAD_US="$2"; shift 2 ;;
        --serial) (( $# >= 2 )) || { echo "--serial requires a value" >&2; exit 2; }; SERIAL="$2"; shift 2 ;;
        --no-build) NO_BUILD=1; shift ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --method)  METHOD="$2"; shift 2 ;;
        --force)   FORCE=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[[ "${RUN_NUMBER}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "run must be a positive integer" >&2; exit 2; }
[[ "${LEAD_US}" =~ ^[0-9]+$ ]] && (( LEAD_US <= 1000 )) \
    || { echo "lead must be between 0 and 1000 us" >&2; exit 2; }
case "${METHOD}" in
    pylink|telnet) ;;
    *) echo "method must be pylink or telnet" >&2; exit 2 ;;
esac

case "${ROLE}" in
    rx) CONFIG="Exp5_Init"
        READY_MARKER="CIR_RTT_READY,channel=1"
        END_MARKER="EXP2_DONE,"
        TIMEOUT="${TIMEOUT:-90}" ;;
    tx) CONFIG="Exp2_Normal"
        READY_MARKER="EXP_LOG_READY,channel=1"
        END_MARKER="EXP2_TX_DONE,"
        TIMEOUT="${TIMEOUT:-600}" ;;
    *)  echo "role must be tx|rx" >&2; exit 2 ;;
esac

HEX_FILE="${OUTPUT_DIR}/${CONFIG}/Exe/dw3000_api.hex"
ELF_FILE="${OUTPUT_DIR}/${CONFIG}/Exe/dw3000_api.elf"
LEAD_STAMP="${OUTPUT_DIR}/${CONFIG}/Exe/.brrs_rx_lead_us"
DATE_TAG="$(date '+%Y%m%d')"
DISTANCE_TAG=""
[[ "${DISTANCE}" != "na" && -n "${DISTANCE}" ]] && DISTANCE_TAG="_${DISTANCE}m"
OUTDIR="${SDK_ROOT}/../logs/exp5_${ENVIRONMENT}${DISTANCE_TAG}_${DATE_TAG}"
mkdir -p "${OUTDIR}"
BASE="exp5_${PREAMBLE}_l${LEAD_US}_r${RUN_NUMBER}_${ROLE}"
RAW_LOG="${OUTDIR}/${BASE}.log"
META_FILE="${OUTDIR}/${BASE}.meta.txt"
FLASH_LOG="${OUTDIR}/${BASE}.flash.log"
BUILD_LOG="${OUTDIR}/${BASE}.build.log"
if [[ -e "${RAW_LOG}" ]]; then
    if (( FORCE )); then
        BACKUP="${RAW_LOG}.prev.$(date '+%H%M%S')"
        mv "${RAW_LOG}" "${BACKUP}"
        echo "[log] existing log moved to ${BACKUP}"
    else
        echo "refusing to overwrite ${RAW_LOG}" >&2
        echo "  (재시도라면 --force를 붙이거나 run 번호를 올리세요)" >&2
        exit 1
    fi
fi

# ---------------------------------------------------- 실행 파일 자동 탐색
find_executable() {
    local name="$1"; shift
    if command -v "${name}" >/dev/null 2>&1; then
        command -v "${name}"; return 0
    fi
    local candidate
    for candidate in "$@"; do
        [[ -x "${candidate}" ]] && { printf '%s\n' "${candidate}"; return 0; }
    done
    local root
    for root in "/Applications/SEGGER" "/opt/SEGGER" "/usr/share" \
                "/usr/local" "/opt/homebrew" "${HOME}/SEGGER" \
                "${HOME}/Downloads" "${HOME}/JLink"*; do
        [[ -d "${root}" ]] || continue
        candidate="$(find "${root}" -maxdepth 6 -type f -name "${name}" \
            -perm -111 -print -quit 2>/dev/null || true)"
        [[ -n "${candidate}" ]] && { printf '%s\n' "${candidate}"; return 0; }
    done
    return 1
}

EMBUILD="${EMBUILD:-$(find_executable emBuild \
    "/Applications/SEGGER/SEGGER Embedded Studio 8.28/bin/emBuild" \
    "/opt/SEGGER/EmbeddedStudio/bin/emBuild" \
    "/usr/share/segger_embedded_studio/bin/emBuild" || true)}"
JLINK="${JLINK:-$(find_executable JLinkExe \
    "/Applications/SEGGER/JLink/JLinkExe" \
    "/Applications/SEGGER/JLink_V934b/JLinkExe" \
    "/opt/SEGGER/JLink/JLinkExe" || true)}"
ARM_NM="${ARM_NM:-$(find_executable arm-none-eabi-nm \
    "/opt/homebrew/bin/arm-none-eabi-nm" \
    "/usr/local/bin/arm-none-eabi-nm" \
    "/usr/bin/arm-none-eabi-nm" || true)}"

if (( NO_BUILD == 0 )) && [[ ! -x "${EMBUILD:-}" ]]; then
    echo "ERROR: emBuild를 찾지 못했습니다. EMBUILD=<경로>로 지정하세요." >&2; exit 1
fi
# JLinkExe는 telnet 방식에서만 필요 (pylink는 J-Link DLL을 직접 사용)
if [[ "${METHOD}" != "pylink" && ! -x "${JLINK:-}" ]]; then
    echo "ERROR: JLinkExe를 찾지 못했습니다. JLINK=<경로>로 지정하거나 --method pylink를 사용하세요." >&2; exit 1
fi
[[ -x "${ARM_NM:-}" ]] || { echo "ERROR: arm-none-eabi-nm을 찾지 못했습니다. ARM_NM=<경로>로 지정하세요." >&2; exit 1; }

# ---------------------------------------------------------------- build
if (( NO_BUILD == 0 )); then
    echo "[build] ${CONFIG}"
    BUILD_ARGS=(-threadnum "${EMBUILD_THREADS:-1}")
    if [[ "${ROLE}" == "rx" ]]; then
        DEFS="DEBUG;BRRS_TARGET_CYCLES=${EXPECTED_SAMPLES}"
        DEFS+=";BRRS_RX_LEAD_MARGIN_US=${LEAD_US}"
        BUILD_ARGS+=(-sproperty "c_preprocessor_definitions=${DEFS}")
    fi
    BUILD_ARGS+=(-config "${CONFIG}" -project dw3000_api -rebuild "${PROJECT}")
    "${EMBUILD}" "${BUILD_ARGS[@]}" >"${BUILD_LOG}" 2>&1 \
        || { echo "build failed: ${BUILD_LOG}" >&2; exit 1; }
    if [[ "${ROLE}" == "rx" ]]; then
        printf '%s\n' "${LEAD_US}" >"${LEAD_STAMP}"
    fi
fi
[[ -f "${HEX_FILE}" && -f "${ELF_FILE}" ]] \
    || { echo "firmware image missing" >&2; exit 1; }
if (( NO_BUILD == 1 )) && [[ "${ROLE}" == "rx" ]]; then
    [[ -f "${LEAD_STAMP}" && "$(<"${LEAD_STAMP}")" == "${LEAD_US}" ]] \
        || { echo "cached ${CONFIG} was not built with lead ${LEAD_US} us" >&2; exit 1; }
fi

# ELF에서 매 빌드마다 RTT 제어 블록 주소를 다시 추출 (주소 하드코딩 금지)
RTT_ADDR="0x$("${ARM_NM}" -n "${ELF_FILE}" \
    | awk '$3 == "_SEGGER_RTT" { print $1; exit }')"
[[ "${RTT_ADDR}" =~ ^0x[0-9A-Fa-f]+$ ]] \
    || { echo "_SEGGER_RTT not found in ELF" >&2; exit 1; }
echo "[rtt] control block @ ${RTT_ADDR}"

# ---------------------------------------------------------------- pylink 경로
if [[ "${METHOD}" == "pylink" ]]; then
    PYLINK_ARGS=(
        python3 "${SCRIPT_DIR}/rtt_capture.py"
        --hex "${HEX_FILE}"
        --rtt-address "${RTT_ADDR}"
        --channel 1
        --ready-marker "${READY_MARKER}"
        --end-marker "${END_MARKER}"
        --timeout "${TIMEOUT}"
        --out "${RAW_LOG}"
    )
    [[ -n "${SERIAL}" ]] && PYLINK_ARGS+=(--serial "${SERIAL}")
    "${PYLINK_ARGS[@]}"
    CAPTURE_RC=0
else
    CAPTURE_RC=-1
fi

# ---------------------------------------------------------------- telnet 경로
# JLinkExe 세션 하나를 FIFO로 열고 캡처가 끝날 때까지 유지한다.
if [[ "${METHOD}" == "telnet" ]]; then
    FIFO="$(mktemp -u "${TMPDIR:-/tmp}/jlink_fifo.XXXXXX")"
    mkfifo "${FIFO}"
    JLINK_PID=""
    cleanup() {
        if [[ -n "${JLINK_PID}" ]] && kill -0 "${JLINK_PID}" 2>/dev/null; then
            printf 'exit\n' >&9 2>/dev/null || true
            exec 9>&- 2>/dev/null || true
            sleep 1
            kill "${JLINK_PID}" 2>/dev/null || true
        fi
        rm -f "${FIFO}"
    }
    trap cleanup EXIT INT TERM

    "${JLINK}" -Device NRF52840_XXAA -If SWD -Speed 4000 -AutoConnect 1 \
    ${SERIAL:+-USB "${SERIAL}"} \
    -RTTTelnetPort "${RTT_TELNET_PORT}" \
    <"${FIFO}" >"${FLASH_LOG}" 2>&1 &
    JLINK_PID=$!
    exec 9>"${FIFO}"      # writer를 열어둬야 JLinkExe의 stdin이 EOF가 안 됨

# flash + UICR.APPROTECT(0x5A) + RTT 주소 지정 + 리셋/실행.
# 이 세션은 여기서 끝나지 않고 열려 있는 채로 RTT TELNET 서버를 제공한다.
    {
    printf 'r\n'
    printf 'loadfile %s\n' "${HEX_FILE}"
    printf 'w4 0x4001E504, 0x00000001\nsleep 10\n'
    printf 'w4 0x10001208, 0x0000005A\nsleep 10\n'
    printf 'w4 0x4001E504, 0x00000000\nsleep 10\n'
    printf 'mem32 0x10001208, 1\n'
    printf 'exec SetRTTAddr %s\n' "${RTT_ADDR}"
    printf 'r\n'
    printf 'g\n'
    } >&9

# loadfile 완료 대기: JLinkExe 출력에서 실행 재개(g) 흔적을 폴링
    for _ in $(seq 1 100); do
        grep -q "O.K." "${FLASH_LOG}" 2>/dev/null && break
        sleep 0.2
    done
    echo "[flash] done (session kept open)"

# TELNET 캡처: 접속 직후 100ms 안에 config string으로 채널 1 선택.
# (수동 telnet으로는 100ms를 지킬 수 없어 실패한다 — 자동 전송이 필수)
    echo "[capture] RTT ch1 via 127.0.0.1:${RTT_TELNET_PORT} (timeout ${TIMEOUT}s)"
    CAPTURE_RC=0
    python3 - "${RTT_ADDR}" "${RAW_LOG}" "${TIMEOUT}" "${END_MARKER}" \
    "${READY_MARKER}" "${RTT_TELNET_PORT}" <<'PYEOF' || CAPTURE_RC=$?
import socket, sys, time

rtt_addr, out_path, timeout_s, end_marker, ready_marker, port = sys.argv[1:7]
deadline = time.time() + float(timeout_s)
end_b, ready_b = end_marker.encode(), ready_marker.encode()

sock = None
for _ in range(50):                       # 서버 기동 대기 (최대 ~10s)
    try:
        sock = socket.create_connection(("127.0.0.1", int(port)), timeout=2)
        break
    except OSError:
        time.sleep(0.2)
if sock is None:
    sys.exit(4)

# 반드시 접속 직후(100ms 이내) 전송해야 config로 해석된다.
cfg = f"$$SEGGER_TELNET_ConfigStr=RTTCh;1;SetRTTAddr;{rtt_addr};$$"
sock.sendall(cfg.encode())

buf = bytearray()
ready_seen = False
sock.settimeout(0.5)
with open(out_path, "wb") as f:
    while time.time() < deadline:
        try:
            data = sock.recv(16384)
        except socket.timeout:
            continue
        if not data:
            break
        f.write(data); f.flush()
        buf += data
        if not ready_seen and ready_b in buf:
            ready_seen = True
            print("[capture] READY marker seen", flush=True)
        if end_b in buf:
            time.sleep(0.5)               # 잔여 데이터 flush
            try:
                while True:
                    tail = sock.recv(16384)
                    if not tail:
                        break
                    f.write(tail); buf += tail
            except socket.timeout:
                pass
            sys.exit(0)
sys.exit(2)                               # end marker 미검출
PYEOF

# 세션 정상 종료
    printf 'exit\n' >&9
    exec 9>&-
    wait "${JLINK_PID}" 2>/dev/null || true
    JLINK_PID=""
fi

# ---------------------------------------------------------------- verify
if (( CAPTURE_RC != 0 )); then
    echo "[capture] FAILED (rc=${CAPTURE_RC}) — see ${RAW_LOG}" >&2
    exit "${CAPTURE_RC}"
fi
if [[ "${ROLE}" == "rx" ]]; then
    if ! grep -Fxq "EXP_LOG_CONFIG_CSV,experiment=5,plen=${PREAMBLE},lead_us=${LEAD_US},tail_us=0,target=1000,cir=1" "${RAW_LOG}"; then
        echo "[verify] FAIL: firmware did not report requested lead ${LEAD_US} us" >&2
        exit 3
    fi
    CSV_ROWS="$(grep -c '^CIR_CSV,' "${RAW_LOG}" || true)"
    read -r UNIQUE_FRAMES UNIQUE_CYCLES BAD_ROWS FRAME_MIN FRAME_MAX CYCLE_MIN CYCLE_MAX <<EOF
$(awk -F, -v expected_m="${PREAMBLE}" '
    $1 == "CIR_CSV" {
        frames[$2] = 1;
        cycles[$3] = 1;
        if (frame_count == 0 || $2 < frame_min) frame_min = $2;
        if (frame_count == 0 || $2 > frame_max) frame_max = $2;
        if (cycle_count == 0 || $3 < cycle_min) cycle_min = $3;
        if (cycle_count == 0 || $3 > cycle_max) cycle_max = $3;
        frame_count++;
        cycle_count++;
        if ($4 != "N2" || $5 != expected_m || NF != 15) bad++;
    }
    END {
        for (key in frames) unique_frames++;
        for (key in cycles) unique_cycles++;
        printf "%d %d %d %d %d %d %d\n",
               unique_frames, unique_cycles, bad,
               frame_min, frame_max, cycle_min, cycle_max;
    }
' "${RAW_LOG}")
EOF

    EXP2_LINE="$(grep '^EXP2_DONE,' "${RAW_LOG}" | tail -1 || true)"
    if [[ -z "${EXP2_LINE}" ]]; then
        echo "[verify] FAIL: EXP2_DONE marker not found" >&2
        exit 3
    fi

    RAW_DONE_LINE="$(grep '^CIR_RAW_DUMP_DONE,' "${RAW_LOG}" | tail -1 || true)"
    if [[ -z "${RAW_DONE_LINE}" ]]; then
        echo "[verify] FAIL: CIR_RAW_DUMP_DONE marker not found" >&2
        exit 3
    fi

    read -r DONE_PLEN DONE_EXPECTED DONE_RX DONE_VALID DONE_DUMP DONE_END_TX DONE_COLLECTION DONE_LINK DONE_PER_X1000 DONE_STATUS <<EOF
$(printf '%s\n' "${EXP2_LINE}" | awk -F, '
    {
        for (i = 2; i <= NF; i++) {
            split($i, kv, "=");
            value[kv[1]] = kv[2];
        }
        printf "%s %s %s %s %s %s %s %s %s %s\n",
               value["plen"], value["expected"], value["rx"],
               value["valid_cir"], value["dump_count"],
               value["end_tx"],
               value["collection"], value["link"],
               value["per_x1000"], value["status"];
    }
')
EOF

    read -r RAW_DONE_PLEN RAW_DONE_COUNT RAW_DONE_SAMPLES <<EOF
$(printf '%s\n' "${RAW_DONE_LINE}" | awk -F, '
    {
        for (i = 2; i <= NF; i++) {
            split($i, kv, "=");
            value[kv[1]] = kv[2];
        }
        printf "%s %s %s\n",
               value["plen"], value["count"], value["samples_per_frame"];
    }
')
EOF

    read -r RAW_HEADERS RAW_UNIQUE_FRAMES RAW_ROWS RAW_COMPLETE_FRAMES RAW_BAD_ROWS <<EOF
$(awk -F, -v expected_m="${PREAMBLE}" -v expected_samples=300 '
    $1 == "CIR_RAW_HEADER" {
        value["frame"] = "";
        value["plen"] = "";
        value["n_samples"] = "";
        for (i = 2; i <= NF; i++) {
            split($i, kv, "=");
            value[kv[1]] = kv[2];
        }
        frame = value["frame"];
        headers++;
        if (frame == "" || seen_header[frame]++) bad++;
        if (value["plen"] != expected_m ||
            value["n_samples"] != expected_samples) bad++;
        declared[frame] = value["n_samples"];
        next;
    }
    $1 == "CIR_RAW" {
        frame = $2;
        sample_idx = $3;
        rows++;
        if (NF != 5 || !(frame in declared) ||
            sample_idx < 0 || sample_idx >= expected_samples ||
            seen_sample[frame SUBSEP sample_idx]++) bad++;
        samples[frame]++;
    }
    END {
        for (frame in declared) {
            unique_frames++;
            if (samples[frame] == declared[frame] &&
                samples[frame] == expected_samples) complete_frames++;
            else bad++;
        }
        printf "%d %d %d %d %d\n",
               headers, unique_frames, rows, complete_frames, bad;
    }
' "${RAW_LOG}")
EOF

    if [[ ! "${DONE_PLEN}" =~ ^[0-9]+$ ||
          ! "${DONE_EXPECTED}" =~ ^[0-9]+$ ||
          ! "${DONE_RX}" =~ ^[0-9]+$ ||
          ! "${DONE_VALID}" =~ ^[0-9]+$ ||
          ! "${DONE_DUMP}" =~ ^[0-9]+$ ||
          ! "${DONE_END_TX}" =~ ^[0-9]+$ ||
          ! "${DONE_PER_X1000}" =~ ^[0-9]+$ ||
          "${DONE_COLLECTION}" != "PASS" ||
          "${DONE_STATUS}" != "PASS" ]]; then
        echo "[verify] FAIL: malformed EXP2_DONE: ${EXP2_LINE}" >&2
        exit 3
    fi

    if [[ ! "${RAW_DONE_PLEN}" =~ ^[0-9]+$ ||
          ! "${RAW_DONE_COUNT}" =~ ^[0-9]+$ ||
          ! "${RAW_DONE_SAMPLES}" =~ ^[0-9]+$ ]]; then
        echo "[verify] FAIL: malformed CIR_RAW_DUMP_DONE: ${RAW_DONE_LINE}" >&2
        exit 3
    fi

    EXPECTED_RAW_FRAMES="${DONE_VALID}"
    if (( EXPECTED_RAW_FRAMES > 30 )); then
        EXPECTED_RAW_FRAMES=30
    fi

    if (( DONE_EXPECTED != EXPECTED_SAMPLES ||
          DONE_PLEN != PREAMBLE ||
          DONE_RX > DONE_EXPECTED ||
          DONE_RX == 0 ||
          CSV_ROWS != DONE_RX ||
          DONE_VALID != DONE_RX ||
          DONE_DUMP != DONE_RX ||
          DONE_END_TX != 3 ||
          UNIQUE_FRAMES != CSV_ROWS ||
          UNIQUE_CYCLES != CSV_ROWS ||
          FRAME_MIN != 1 ||
          FRAME_MAX != CSV_ROWS ||
          CYCLE_MIN < 1 ||
          CYCLE_MAX > DONE_EXPECTED ||
          BAD_ROWS != 0 ||
          RAW_DONE_PLEN != PREAMBLE ||
          RAW_DONE_COUNT != EXPECTED_RAW_FRAMES ||
          RAW_DONE_SAMPLES != 300 ||
          RAW_HEADERS != EXPECTED_RAW_FRAMES ||
          RAW_UNIQUE_FRAMES != EXPECTED_RAW_FRAMES ||
          RAW_COMPLETE_FRAMES != EXPECTED_RAW_FRAMES ||
          RAW_ROWS != EXPECTED_RAW_FRAMES * 300 ||
          RAW_BAD_ROWS != 0 )); then
        echo "[verify] FAIL: expected=${DONE_EXPECTED}, rx=${DONE_RX}, valid=${DONE_VALID}, dump=${DONE_DUMP}, rows=${CSV_ROWS}, frames=${UNIQUE_FRAMES}(${FRAME_MIN}-${FRAME_MAX}), cycles=${UNIQUE_CYCLES}(${CYCLE_MIN}-${CYCLE_MAX}), bad=${BAD_ROWS}; raw_frames=${RAW_COMPLETE_FRAMES}/${EXPECTED_RAW_FRAMES}, raw_rows=${RAW_ROWS}, raw_bad=${RAW_BAD_ROWS}" >&2
        exit 3
    fi

    MISSED=$((DONE_EXPECTED - DONE_RX))
    EXPECTED_PER_X1000=$(((MISSED * 100000 + DONE_EXPECTED / 2) / DONE_EXPECTED))
    PER_PERCENT="$(awk -v missed="${MISSED}" -v expected="${DONE_EXPECTED}" 'BEGIN { printf "%.2f", 100.0 * missed / expected }')"
    if (( MISSED == 0 )); then
        LINK_STATUS="PASS"
    else
        LINK_STATUS="LOSS"
    fi
    if [[ "${DONE_LINK}" != "${LINK_STATUS}" ]] ||
       (( DONE_PER_X1000 != EXPECTED_PER_X1000 )); then
        echo "[verify] FAIL: firmware link/PER mismatch: ${EXP2_LINE}" >&2
        exit 3
    fi
    DETAIL="collection=PASS; expected=${DONE_EXPECTED}; rx=${DONE_RX}; CIR rows=${CSV_ROWS}; PER=${PER_PERCENT}%; link=${LINK_STATUS}; lead=${LEAD_US}us; firmware=PASS"
else
    TX_DONE_LINE="$(grep '^EXP2_TX_DONE,' "${RAW_LOG}" | tail -1 || true)"
    BEACON_LINE="$(grep 'BRRS_BEACON_RX_CSV,' "${RAW_LOG}" | tail -1 || true)"
    if [[ -z "${TX_DONE_LINE}" ]]; then
        echo "[verify] FAIL: EXP2_TX_DONE marker not found" >&2
        exit 3
    fi
    read -r TX_PLEN TX_EXPECTED TX_ATTEMPTS TX_SUCCESS TX_LATE TX_BEACON_ERRORS TX_DATA_ERRORS TX_END TX_COLLECTION TX_LINK TX_STATUS <<EOF
$(printf '%s\n' "${TX_DONE_LINE}" | awk -F, '
    {
        for (i = 2; i <= NF; i++) {
            split($i, kv, "=");
            value[kv[1]] = kv[2];
        }
        printf "%s %s %s %s %s %s %s %s %s %s %s\n",
               value["plen"], value["expected"], value["attempts"],
               value["success"], value["delayed_late"],
               value["beacon_config_errors"], value["data_config_errors"],
               value["end"],
               value["collection"], value["link"], value["status"];
    }
')
EOF
    if [[ ! "${TX_PLEN:-}" =~ ^[0-9]+$ ||
          ! "${TX_EXPECTED:-}" =~ ^[0-9]+$ ||
          ! "${TX_ATTEMPTS:-}" =~ ^[0-9]+$ ||
          ! "${TX_SUCCESS:-}" =~ ^[0-9]+$ ||
          ! "${TX_LATE:-}" =~ ^[0-9]+$ ||
          ! "${TX_BEACON_ERRORS:-}" =~ ^[0-9]+$ ||
          ! "${TX_DATA_ERRORS:-}" =~ ^[0-9]+$ ||
          ! "${TX_END:-}" =~ ^[0-9]+$ ]]; then
        echo "[verify] FAIL: malformed EXP2_TX_DONE: ${TX_DONE_LINE}" >&2
        exit 3
    fi
    if (( TX_PLEN != PREAMBLE ||
          TX_EXPECTED != EXPECTED_SAMPLES ||
          TX_SUCCESS != TX_ATTEMPTS ||
          TX_ATTEMPTS == 0 ||
          TX_ATTEMPTS > EXPECTED_SAMPLES ||
          TX_LATE != 0 ||
          TX_BEACON_ERRORS != 0 ||
          TX_DATA_ERRORS != 0 ||
          TX_END != 1 )) ||
       [[ "${TX_COLLECTION}" != "PASS" ||
          "${TX_STATUS}" != "PASS" ||
          "${BEACON_LINE}" != *"m=${PREAMBLE},"* ]]; then
        echo "[verify] FAIL: inconsistent EXP2 TX collection: ${TX_DONE_LINE}" >&2
        exit 3
    fi
    if (( TX_ATTEMPTS == EXPECTED_SAMPLES )); then
        TX_LINK_STATUS="PASS"
    else
        TX_LINK_STATUS="LOSS"
    fi
    if [[ "${TX_LINK}" != "${TX_LINK_STATUS}" ]]; then
        echo "[verify] FAIL: firmware TX link mismatch: ${TX_DONE_LINE}" >&2
        exit 3
    fi
    DETAIL="collection=PASS; tx=${TX_SUCCESS}/${EXPECTED_SAMPLES}; link=${TX_LINK_STATUS}; firmware=PASS"
fi

if command -v sha256sum >/dev/null 2>&1; then
    RAW_SHA256="$(sha256sum "${RAW_LOG}" | awk '{print $1}')"
    FIRMWARE_SHA256="$(sha256sum "${HEX_FILE}" | awk '{print $1}')"
else
    RAW_SHA256="$(shasum -a 256 "${RAW_LOG}" | awk '{print $1}')"
    FIRMWARE_SHA256="$(shasum -a 256 "${HEX_FILE}" | awk '{print $1}')"
fi
{
    printf 'role=%s\n' "${ROLE}"
    printf 'configuration=%s\n' "${CONFIG}"
    printf 'preamble_symbols=%s\n' "${PREAMBLE}"
    printf 'lead_us=%s\n' "${LEAD_US}"
    printf 'run_number=%s\n' "${RUN_NUMBER}"
    printf 'environment=%s\n' "${ENVIRONMENT}"
    printf 'distance_m=%s\n' "${DISTANCE}"
    printf 'captured_at=%s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')"
    printf 'firmware_sha256=%s\n' "${FIRMWARE_SHA256}"
    printf 'rtt_address=%s\n' "${RTT_ADDR}"
    printf 'capture_method=%s\n' "${METHOD}"
    printf 'raw_log=%s\n' "${RAW_LOG}"
    printf 'raw_size_bytes=%s\n' "$(wc -c <"${RAW_LOG}" | tr -d '[:space:]')"
    printf 'raw_sha256=%s\n' "${RAW_SHA256}"
    if [[ "${ROLE}" == "rx" ]]; then
        printf 'expected_cycles=%s\n' "${DONE_EXPECTED}"
        printf 'rx_success=%s\n' "${DONE_RX}"
        printf 'valid_cir=%s\n' "${DONE_VALID}"
        printf 'cir_csv_rows=%s\n' "${CSV_ROWS}"
        printf 'per_percent=%s\n' "${PER_PERCENT}"
        printf 'link_status=%s\n' "${LINK_STATUS}"
    else
        printf 'expected_cycles=%s\n' "${EXPECTED_SAMPLES}"
        printf 'tx_success=%s\n' "${TX_SUCCESS}"
        printf 'tx_attempts=%s\n' "${TX_ATTEMPTS}"
        printf 'link_status=%s\n' "${TX_LINK_STATUS}"
    fi
    printf 'collection_status=PASS\n'
    printf 'status=PASS\n'
    printf 'detail=%s\n' "${DETAIL}"
} >"${META_FILE}"

echo "[verify] PASS: ${DETAIL}"
echo "[done] raw=${RAW_LOG}"
echo "[done] meta=${META_FILE}"
