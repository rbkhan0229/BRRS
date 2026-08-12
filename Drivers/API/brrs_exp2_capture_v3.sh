#!/usr/bin/env bash
#
# brrs_exp2_capture_v3.sh — JLinkRTTLogger 없이 단일 J-Link 연결로
# build → flash → run → RTT ch1 캡처 → 검증을 수행한다.
#
# 핵심 변경점 (v2 대비):
#   * JLinkRTTLogger 프로세스를 완전히 제거.
#   * JLinkExe 세션 하나를 FIFO로 열어둔 채 유지 (flash 후 종료하지 않음).
#     → 두 번째 debug 연결이 없으므로 re-connect/re-init 문제가 사라진다.
#   * RTT 데이터는 같은 JLinkExe 세션이 여는 RTT TELNET 포트(19021)에서 수신.
#     채널 1 선택은 접속 직후 100ms 안에 SEGGER TELNET config string 전송으로
#     수행한다 (공식 문서 요구사항: kb.segger.com/J-Link_RTT_TELNET_Channel).
#   * --method pylink 지정 시 rtt_capture.py(pylink-square)로 전 과정 수행.
#
# 사용:
#   ./brrs_exp2_capture_v3.sh <tx|rx> <32|64|128|256> <run> <environment> [distance]
#       [--serial <S/N>] [--no-build] [--timeout <s>] [--method telnet|pylink]

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/dw3000_api.emProject"
OUTPUT_DIR="${SCRIPT_DIR}/Build_Platforms/nRF52840-DK/Output"
RTT_TELNET_PORT="${RTT_TELNET_PORT:-19021}"
EXPECTED_SAMPLES=1000

usage() {
    cat <<EOF
Usage:
  $(basename "$0") <tx|rx> <32|64|128|256> <run> <environment> [distance] [options]

Options:
  --serial <S/N>             Select a J-Link when multiple probes are attached.
  --no-build                 Reuse the existing ELF and HEX.
  --timeout <seconds>        Override the capture timeout.
  --method <telnet|pylink>   Capture backend (default: telnet).
  --force                    Preserve the old log as .prev.<time> and retry.
  -h, --help                 Show this help.
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

ROLE="${1:?role tx|rx}"; PREAMBLE="${2:?preamble}"; RUN_NUMBER="${3:?run}"
ENVIRONMENT="${4:?environment}"; shift 4
DISTANCE="na"; SERIAL=""; NO_BUILD=0; TIMEOUT=""; METHOD="telnet"; FORCE=0
if (( $# > 0 )) && [[ "${1}" != --* ]]; then DISTANCE="$1"; shift; fi
while (( $# > 0 )); do
    case "$1" in
        --serial)  SERIAL="$2"; shift 2 ;;
        --no-build) NO_BUILD=1; shift ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --method)  METHOD="$2"; shift 2 ;;
        --force)   FORCE=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

case "${PREAMBLE}" in
    32|64|128|256) ;;
    *) echo "preamble must be 32, 64, 128, or 256" >&2; exit 2 ;;
esac
[[ "${RUN_NUMBER}" =~ ^[1-9][0-9]*$ ]] \
    || { echo "run must be a positive integer" >&2; exit 2; }
case "${METHOD}" in
    pylink|telnet) ;;
    *) echo "method must be pylink or telnet" >&2; exit 2 ;;
esac

case "${ROLE}" in
    rx) CONFIG="Exp2_${PREAMBLE}_Init"
        READY_MARKER="CIR_RTT_READY,channel=1"
        END_MARKER="EXP2_DONE,"
        TIMEOUT="${TIMEOUT:-90}" ;;
    tx) CONFIG="Exp2_Normal"
        READY_MARKER="EXP_LOG_READY,channel=1"
        END_MARKER="===== END STATS ====="
        TIMEOUT="${TIMEOUT:-600}" ;;
    *)  echo "role must be tx|rx" >&2; exit 2 ;;
esac

HEX_FILE="${OUTPUT_DIR}/${CONFIG}/Exe/dw3000_api.hex"
ELF_FILE="${OUTPUT_DIR}/${CONFIG}/Exe/dw3000_api.elf"
DATE_TAG="$(date '+%Y%m%d')"
DISTANCE_TAG=""
[[ "${DISTANCE}" != "na" && -n "${DISTANCE}" ]] && DISTANCE_TAG="_${DISTANCE}m"
OUTDIR="${SDK_ROOT}/../logs/exp2_${ENVIRONMENT}${DISTANCE_TAG}_${DATE_TAG}"
mkdir -p "${OUTDIR}"
BASE="exp2_${PREAMBLE}_r${RUN_NUMBER}_${ROLE}"
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
    "${EMBUILD}" -config "${CONFIG}" -project dw3000_api -rebuild \
        "${PROJECT}" >"${BUILD_LOG}" 2>&1 \
        || { echo "build failed: ${BUILD_LOG}" >&2; exit 1; }
fi
[[ -f "${HEX_FILE}" && -f "${ELF_FILE}" ]] \
    || { echo "firmware image missing" >&2; exit 1; }

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
    if [[ "${ROLE}" == "rx" ]]; then
        PYLINK_ARGS+=(--require "status=PASS" --expect-lines "CIR_CSV,:${EXPECTED_SAMPLES}")
    fi
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
    CSV_ROWS="$(grep -c '^CIR_CSV,' "${RAW_LOG}" || true)"
    read -r UNIQUE_FRAMES UNIQUE_CYCLES BAD_ROWS <<EOF
$(awk -F, -v expected_m="${PREAMBLE}" '
    $1 == "CIR_CSV" {
        frames[$2] = 1;
        cycles[$3] = 1;
        if ($4 != "N2" || $5 != expected_m || NF != 15) bad++;
    }
    END {
        for (key in frames) frame_count++;
        for (key in cycles) cycle_count++;
        printf "%d %d %d\n", frame_count, cycle_count, bad;
    }
' "${RAW_LOG}")
EOF
    if (( CSV_ROWS != EXPECTED_SAMPLES ||
          UNIQUE_FRAMES != EXPECTED_SAMPLES ||
          UNIQUE_CYCLES != EXPECTED_SAMPLES ||
          BAD_ROWS != 0 )); then
        echo "[verify] FAIL: rows=${CSV_ROWS}, frames=${UNIQUE_FRAMES}, cycles=${UNIQUE_CYCLES}, bad=${BAD_ROWS}" >&2
        exit 3
    fi
    if ! grep -q 'EXP2_DONE,.*status=PASS' "${RAW_LOG}"; then
        echo "[verify] FAIL: EXP2_DONE status=PASS not found" >&2; exit 3
    fi
    DETAIL="CIR rows=${CSV_ROWS}; unique_frames=${UNIQUE_FRAMES}; unique_cycles=${UNIQUE_CYCLES}; EXP2_DONE=PASS"
else
    TX_LINE="$(grep 'My TX: success=' "${RAW_LOG}" | tail -1 || true)"
    ERROR_LINE="$(grep 'SYNC loss:' "${RAW_LOG}" | tail -1 || true)"
    BEACON_LINE="$(grep 'BRRS_BEACON_RX_CSV,' "${RAW_LOG}" | tail -1 || true)"
    FINAL_HEADER="$(grep 'FINAL STATS.*sym)' "${RAW_LOG}" | tail -1 || true)"
    if [[ "${TX_LINE}" != *"success=${EXPECTED_SAMPLES} attempts=${EXPECTED_SAMPLES} delayed_late=0"* ||
          "${BEACON_LINE}" != *"m=${PREAMBLE},"* ||
          "${FINAL_HEADER}" != *"${PREAMBLE}sym)"* ||
          "${ERROR_LINE}" != *"beacon_config_errors=0"* ||
          "${ERROR_LINE}" != *"data_config_errors=0"* ]]; then
        echo "[verify] FAIL: TX final statistics are incomplete or inconsistent" >&2
        exit 3
    fi
    DETAIL="${TX_LINE}; ${ERROR_LINE}"
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
    printf 'status=PASS\n'
    printf 'detail=%s\n' "${DETAIL}"
} >"${META_FILE}"

echo "[verify] PASS: ${DETAIL}"
echo "[done] raw=${RAW_LOG}"
echo "[done] meta=${META_FILE}"
