from pathlib import Path
import hashlib, json

ROOT = Path(__file__).resolve().parent
API = Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
before = {}

def edit(name, replacements):
    p = API / name
    s = p.read_text()
    before[name] = {'sha256': hashlib.sha256(p.read_bytes()).hexdigest(), 'text': s}
    for old, new, count in replacements:
        if s.count(old) != count:
            raise ValueError((name, old[:90], s.count(old), count))
        s = s.replace(old, new)
    p.write_text(s)

edit('brrs_run_experiment.sh', [
    ('  --serial <S/N>', '  --board-map <json>     Fixed board manifest; overrides discovery/rotation.\n  --slotted-rx           Exp4 bounded delayed-RX per slot.\n  --spi-opt              Exp4 persistent/direct SPI.\n  --sync-buffer <us>     Exp4 SYNC-to-DATA budget (default: 3000).\n  --sync-prep <us>       Exp4 next-SYNC reserve (default: 2500).\n  --cycles <n>           Exp4 superframes (default: 1000).\n  --build-only           Build each case without board access.\n  --serial <S/N>', 1),
    ('Stage0/Exp1/Exp4 RX PAC size.', 'Stage0/Exp1/Exp2/Exp4 RX PAC size.', 1),
    ('DRY_RUN=0\n', 'DRY_RUN=0\nBOARD_MAP=""\nSLOTTED_RX=0\nSPI_OPT=0\nSYNC_BUFFER_US=3000\nSYNC_PREP_US=2500\nTARGET_CYCLES=1000\nBUILD_ONLY=0\n', 1),
    ('        --serial)\n', '        --board-map) BOARD_MAP="$2"; shift 2 ;;\n        --slotted-rx) SLOTTED_RX=1; shift ;;\n        --spi-opt) SPI_OPT=1; shift ;;\n        --sync-buffer) SYNC_BUFFER_US="$2"; shift 2 ;;\n        --sync-prep) SYNC_PREP_US="$2"; shift 2 ;;\n        --cycles) TARGET_CYCLES="$2"; shift 2 ;;\n        --build-only) BUILD_ONLY=1; shift ;;\n        --serial)\n', 1),
    ('&& "${EXPERIMENT}" != "exp4" && ${PAC_SET}', '&& "${EXPERIMENT}" != "exp2" && "${EXPERIMENT}" != "exp4" && ${PAC_SET}', 1),
    ('--pac is only valid for Stage0, Exp1, or Exp4', '--pac is only valid for Stage0, Exp1, Exp2, or Exp4', 1),
    ('CASE_NUMBER=0\n', '''if [[ -n "${BOARD_MAP}" && "${ROLE}" != "tx-auto" ]]; then
    SELECTED_SERIAL="$(python3 "${SCRIPT_DIR}/brrs_suite_manifest.py" serial "${BOARD_MAP}" --stage "${EXPERIMENT}" --role "${ROLE}")"
    [[ -z "${SERIAL}" || "${SERIAL}" == "${SELECTED_SERIAL}" ]] || { echo "serial conflicts with fixed board manifest" >&2; exit 2; }
    SERIAL="${SELECTED_SERIAL}"
fi
if [[ "${EXPERIMENT}" != "exp4" ]] && (( SLOTTED_RX || SPI_OPT || SYNC_BUFFER_US != 3000 || SYNC_PREP_US != 2500 || TARGET_CYCLES != 1000 )); then
    echo "Exp4 RX/SPI/timing options are only valid for Exp4" >&2; exit 2
fi
EXP4_OPTIONS=(--sync-buffer "${SYNC_BUFFER_US}" --sync-prep "${SYNC_PREP_US}" --cycles "${TARGET_CYCLES}")
(( SLOTTED_RX == 0 )) || EXP4_OPTIONS+=(--slotted-rx)
(( SPI_OPT == 0 )) || EXP4_OPTIONS+=(--spi-opt)

CASE_NUMBER=0
''', 1),
    ('    (( FORCE == 0 )) || command+=(--force)\n', '    (( FORCE == 0 )) || command+=(--force)\n    (( BUILD_ONLY == 0 )) || command+=(--build-only)\n', 1),
    ('--pac "${PAC}")\n                    [[ -n', '--pac "${PAC}" "${EXP4_OPTIONS[@]}")\n                    [[ -n', 1),
    ('key="${EXPERIMENT}_rx_m${preamble}_l${FIXED_LEAD_US}"', 'key="${EXPERIMENT}_rx_m${preamble}_l${FIXED_LEAD_US}_pac${PAC}"', 1),
    ('args+=(--lead "${FIXED_LEAD_US}")\n                if [[ "${METHOD}"', 'args+=(--lead "${FIXED_LEAD_US}" --pac "${PAC}")\n                if [[ "${METHOD}"', 1),
    ('common_args=(--guard "${guard}" --lead "${FIXED_LEAD_US}" --pac "${PAC}")', 'common_args=(--guard "${guard}" --lead "${FIXED_LEAD_US}" --pac "${PAC}" "${EXP4_OPTIONS[@]}")', 1),
    ('args+=("${common_args[@]}")\n                            run_case "${key}" "exp4 auto-TX', 'args+=("${common_args[@]}")\n                            [[ -z "${BOARD_MAP}" ]] || args+=(--board-map "${BOARD_MAP}")\n                            run_case "${key}" "exp4 auto-TX', 1),
])

# Build-only mode stops before opening a J-Link. Metadata always identifies
# the actual selected serial, including in the single-link logical-N2 case.
for name in ['brrs_exp1_capture.sh', 'brrs_exp2_capture_v3.sh', 'brrs_exp3_capture.sh', 'brrs_exp5_capture.sh', 'brrs_exp4_capture.sh']:
    edit(name, [
        ('set -Eeuo pipefail\n', 'set -Eeuo pipefail\nBUILD_ONLY=0\n', 1),
        ('        --no-build)', '        --build-only) BUILD_ONLY=1; shift ;;\n        --no-build)', 1),
        ('echo "[rtt] control block @ ${RTT_ADDR}"\n', 'echo "[rtt] control block @ ${RTT_ADDR}"\nif (( BUILD_ONLY )); then echo "[build-only] verified image ${HEX_FILE}; no board access"; exit 0; fi\n', 1),
        ("    printf 'configuration=%s\\n'", "    printf 'serial=%s\\n' \"${SERIAL}\"\n    printf 'configuration=%s\\n'", 1),
    ])

edit('brrs_exp2_capture_v3.sh', [
    ('  --lead <us>', '  --pac <4|8>               DATA RX PAC (default: 8).\n  --lead <us>', 1),
    ('; FORCE=0; LEAD_US=15', '; FORCE=0; LEAD_US=15; PAC=8', 1),
    ('        --lead) ', '        --pac) PAC="$2"; shift 2 ;;\n        --lead) ', 1),
    ('case "${METHOD}" in', 'case "${PAC}" in 4|8) ;; *) echo "pac must be 4 or 8" >&2; exit 2 ;; esac\ncase "${METHOD}" in', 1),
    ('LEAD_STAMP="${OUTPUT_DIR}/${CONFIG}/Exe/.brrs_rx_lead_us"', 'LEAD_STAMP="${OUTPUT_DIR}/${CONFIG}/Exe/.brrs_rx_lead_us"\nPAC_STAMP="${OUTPUT_DIR}/${CONFIG}/Exe/.brrs_rx_pac"', 1),
    ('BASE="exp2_${PREAMBLE}_l${LEAD_US}_r${RUN_NUMBER}_${ROLE}"', 'BASE="exp2_${PREAMBLE}_l${LEAD_US}_pac${PAC}_r${RUN_NUMBER}_${ROLE}"', 1),
    ('DEFS+=";BRRS_RX_LEAD_MARGIN_US=${LEAD_US}"', 'DEFS+=";BRRS_RX_LEAD_MARGIN_US=${LEAD_US};BRRS_RX_PAC_SYMBOLS=${PAC}"', 1),
    ('printf \'%s\\n\' "${LEAD_US}" >"${LEAD_STAMP}"', 'printf \'%s\\n\' "${LEAD_US}" >"${LEAD_STAMP}"\n        printf \'%s\\n\' "${PAC}" >"${PAC_STAMP}"', 1),
    ('    [[ -f "${LEAD_STAMP}" &&', '    [[ -f "${PAC_STAMP}" && "$(<"${PAC_STAMP}")" == "${PAC}" ]] || { echo "cached ${CONFIG} PAC mismatch" >&2; exit 1; }\n    [[ -f "${LEAD_STAMP}" &&', 1),
    ('    read -r DONE_PLEN DONE_EXPECTED', '    grep -Fxq "EXP2_PHY_CONFIG_CSV,plen=${PREAMBLE},pac=${PAC},sfd_timeout=$((PREAMBLE + 9 - PAC)),lead_us=${LEAD_US}" "${RAW_LOG}" || { echo "[verify] FAIL: Exp2 PHY/PAC/lead mismatch" >&2; exit 3; }\n    read -r DONE_PLEN DONE_EXPECTED', 1),
    ("    printf 'lead_us=%s\\n'", "    printf 'pac=%s\\n' \"${PAC}\"\n    printf 'lead_us=%s\\n'", 1),
])

edit('Src/examples/ex_35a_brrs_init/brrs_init.c', [
    ('#if ENABLE_CIR\n    cir_log_info("CIR_RTT_READY,channel=1,name=CIR_CSV");', '''#if BRRS_EXPERIMENT == 2
    {
        static char exp2_phy_cfg[128];
        snprintf(exp2_phy_cfg, sizeof(exp2_phy_cfg),
                 "EXP2_PHY_CONFIG_CSV,plen=%d,pac=%u,sfd_timeout=%u,lead_us=%d",
                 PREAMBLE_SYMBOLS, (unsigned)DATA_PAC_SYMBOLS,
                 (unsigned)config_data.sfdTO, RX_LEAD_MARGIN_US);
        cir_log_info(exp2_phy_cfg);
    }
#endif
#if ENABLE_CIR
    cir_log_info("CIR_RTT_READY,channel=1,name=CIR_CSV");''', 1),
])

edit('brrs_exp4_capture.sh', [
    ('SPI_OPT=0\n', 'SPI_OPT=0\nSLOTTED_RX=0\n', 1),
    ('        --spi-opt)', '        --slotted-rx) SLOTTED_RX=1; shift ;;\n        --spi-opt)', 1),
    ('if (( TARGET_CYCLES != 1000 )); then\n    IMAGE_DIR', 'if (( SLOTTED_RX )); then IMAGE_DIR+="_slottedrx"; fi\nif (( TARGET_CYCLES != 1000 )); then\n    IMAGE_DIR', 1),
    ('if (( TARGET_CYCLES != 1000 )); then\n    CONFIG', 'if (( SLOTTED_RX )); then CONFIG+="_SLOTTEDRX"; fi\nif (( TARGET_CYCLES != 1000 )); then\n    CONFIG', 1),
    ('if (( TARGET_CYCLES != 1000 )); then\n    OUTDIR', 'if (( SLOTTED_RX )); then OUTDIR+="_slottedrx"; fi\nif (( TARGET_CYCLES != 1000 )); then\n    OUTDIR', 1),
    ('    (( SPI_OPT == 0 )) || BUILD_CMD+=(--spi-opt)', '    (( SLOTTED_RX == 0 )) || BUILD_CMD+=(--slotted-rx)\n    (( SPI_OPT == 0 )) || BUILD_CMD+=(--spi-opt)', 1),
    ('VERIFY_SPI_ARGS=()\n', 'VERIFY_SPI_ARGS=()\n(( SLOTTED_RX == 0 )) || VERIFY_SPI_ARGS+=(--slotted-rx)\n', 1),
    ("    printf 'pac=%s\\n'", "    printf 'slotted_rx=%s\\n' \"${SLOTTED_RX}\"\n    printf 'pac=%s\\n'", 1),
    ('GIT_COMMIT="$(git -C "${SDK_ROOT}" rev-parse HEAD)"', 'GIT_COMMIT="$(git -C "${SDK_ROOT}" rev-parse HEAD 2>/dev/null || echo independent-copy)"', 1),
    ('GIT_BRANCH="$(git -C "${SDK_ROOT}" branch --show-current)"', 'GIT_BRANCH="$(git -C "${SDK_ROOT}" branch --show-current 2>/dev/null || echo independent-copy)"', 1),
    ('git -C "${SDK_ROOT}" status --porcelain --untracked-files=no)', 'git -C "${SDK_ROOT}" status --porcelain --untracked-files=no 2>/dev/null || true)', 1),
])

edit('brrs_exp4_multi_tx.sh', [
    ('SPI_OPT=0\n', 'SPI_OPT=0\nSLOTTED_RX=0\nBOARD_MAP=""\nBUILD_ONLY=0\n', 1),
    ('        --spi-opt)', '        --board-map) BOARD_MAP="$2"; shift 2 ;;\n        --slotted-rx) SLOTTED_RX=1; shift ;;\n        --build-only) BUILD_ONLY=1; shift ;;\n        --spi-opt)', 1),
    ('[[ -z "${PROBE_SERIALS}" ]] || ASSIGN_ARGS+=(--serials "${PROBE_SERIALS}")', '[[ -z "${PROBE_SERIALS}" ]] || ASSIGN_ARGS+=(--serials "${PROBE_SERIALS}")\n[[ -z "${BOARD_MAP}" ]] || ASSIGN_ARGS+=(--board-map "${BOARD_MAP}")', 1),
    ('    (( SPI_OPT == 0 )) || BUILD_ARGS+=(--spi-opt)', '    (( SLOTTED_RX == 0 )) || BUILD_ARGS+=(--slotted-rx)\n    (( SPI_OPT == 0 )) || BUILD_ARGS+=(--spi-opt)', 1),
    ('    (( SPI_OPT == 0 )) || command+=(--spi-opt)', '    (( SLOTTED_RX == 0 )) || command+=(--slotted-rx)\n    (( BUILD_ONLY == 0 )) || command+=(--build-only)\n    (( SPI_OPT == 0 )) || command+=(--spi-opt)', 1),
])
(ROOT / 'before_edits.json').write_text(json.dumps(before, indent=2) + '\n')
print('Updated', len(before), 'files')
