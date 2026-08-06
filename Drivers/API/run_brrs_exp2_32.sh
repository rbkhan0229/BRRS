#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
API_DIR="$ROOT/Drivers/API"
EXE_DIR="$API_DIR/Build_Platforms/nRF52840-DK/Output/Debug/Exe"
JLINK="/Applications/SEGGER/JLink_V934b/JLinkExe"
RTT_LOGGER="/Applications/SEGGER/JLink_V934b/JLinkRTTLoggerExe"
PARSER="$API_DIR/brrs_cir_log_to_csv_plot.py"

INIT_HEX="$EXE_DIR/dw3000_api_brrs_exp2_init_cir_psdu127_plen32.hex"
NORMAL_HEX="$EXE_DIR/dw3000_api_brrs_exp2_normal_psdu127_plen32.hex"

INIT_SERIAL=""
NORMAL_SERIAL=""
ENV_LABEL="office"
DISTANCE_M="1"
OUTDIR="$ROOT/output/brrs_exp2"
PREFIX=""
NO_FLASH=0

usage() {
    cat <<EOF
Usage:
  $0 --init-serial <S/N> --normal-serial <S/N> [options]

Options:
  --init-serial <S/N>     J-Link serial number of the INIT/RX board.
  --normal-serial <S/N>   J-Link serial number of the NORMAL/TX board.
  --environment <label>   Environment label for CSV output. Default: office
  --distance-m <value>    Distance label for CSV output. Default: 1
  --prefix <label>        Output filename prefix. Default: timestamped exp2_32...
  --outdir <path>         Output directory. Default: output/brrs_exp2
  --no-flash              Skip flashing; only capture INIT RTT log and analyze it.
  -h, --help              Show this help.

Example:
  $0 --init-serial 1050283069 --normal-serial 1050XXXX --environment office --distance-m 1
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --init-serial)
            INIT_SERIAL="${2:-}"
            shift 2
            ;;
        --normal-serial)
            NORMAL_SERIAL="${2:-}"
            shift 2
            ;;
        --environment)
            ENV_LABEL="${2:-}"
            shift 2
            ;;
        --distance-m)
            DISTANCE_M="${2:-}"
            shift 2
            ;;
        --prefix)
            PREFIX="${2:-}"
            shift 2
            ;;
        --outdir)
            OUTDIR="${2:-}"
            shift 2
            ;;
        --no-flash)
            NO_FLASH=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "$INIT_SERIAL" ]]; then
    echo "Missing --init-serial" >&2
    usage >&2
    exit 2
fi

if [[ "$NO_FLASH" -eq 0 && -z "$NORMAL_SERIAL" ]]; then
    echo "Missing --normal-serial, or pass --no-flash if both boards are already programmed." >&2
    usage >&2
    exit 2
fi

for required in "$JLINK" "$RTT_LOGGER" "$PARSER"; do
    if [[ ! -e "$required" ]]; then
        echo "Required file not found: $required" >&2
        exit 1
    fi
done

for hex in "$INIT_HEX" "$NORMAL_HEX"; do
    if [[ ! -e "$hex" ]]; then
        echo "HEX file not found: $hex" >&2
        exit 1
    fi
done

timestamp="$(date '+%Y%m%d_%H%M%S')"
safe_env="$(echo "$ENV_LABEL" | tr -c 'A-Za-z0-9_.-' '_')"
safe_dist="$(echo "$DISTANCE_M" | tr -c 'A-Za-z0-9_.-' '_')"
if [[ -z "$PREFIX" ]]; then
    PREFIX="exp2_32_${safe_env}_${safe_dist}m_${timestamp}"
fi

mkdir -p "$OUTDIR"
LOG_FILE="$OUTDIR/${PREFIX}.log"
RESULT_DIR="$OUTDIR/${PREFIX}_result"

program_and_run() {
    local serial="$1"
    local hex="$2"
    local role="$3"
    local cmdfile

    cmdfile="$(mktemp "/tmp/brrs_${role}.XXXXXX.jlink")"
    {
        echo "r"
        echo "loadfile $hex"
        echo "r"
        echo "g"
        echo "exit"
    } > "$cmdfile"

    echo "[$role] Flashing and running $hex on J-Link S/N $serial"
    "$JLINK" \
        -Device NRF52840_XXAA \
        -If SWD \
        -Speed 4000 \
        -USB "$serial" \
        -CommandFile "$cmdfile"

    rm -f "$cmdfile"
}

if [[ "$NO_FLASH" -eq 0 ]]; then
    program_and_run "$NORMAL_SERIAL" "$NORMAL_HEX" "normal"
    sleep 1
    program_and_run "$INIT_SERIAL" "$INIT_HEX" "init"
else
    echo "Skipping flash because --no-flash was set."
fi

echo
echo "[log] Capturing INIT RTT log to:"
echo "      $LOG_FILE"
echo "[log] Stop logging after the final stats appear. If RTT Logger exits with 'Failed to read data' after writing data, that is usually okay."
echo

set +e
"$RTT_LOGGER" \
    -Device NRF52840_XXAA \
    -If SWD \
    -Speed 4000 \
    -USB "$INIT_SERIAL" \
    -RTTChannel 0 \
    "$LOG_FILE"
logger_status=$?
set -e

echo
echo "[log] RTT logger exited with status $logger_status"
echo "[parse] Generating CSV/SVG outputs..."

python3 "$PARSER" "$LOG_FILE" \
    -o "$RESULT_DIR" \
    --prefix "$PREFIX" \
    --environment "$ENV_LABEL" \
    --distance-m "$DISTANCE_M"

echo
echo "Done."
echo "Log:     $LOG_FILE"
echo "Results: $RESULT_DIR"
