"""Compile each distinct suite path once in a disposable non-Git SDK copy.

No flash, reset, capture or RF. Definitions mirror existing capture scripts.
These images verify the current pipeline, not corrected/release firmware.
"""
from pathlib import Path
import datetime
import hashlib
import json
import re
import subprocess
import xml.etree.ElementTree as ET

BASE = Path('/Users/songchieon/Desktop/DWM3000')
ROOT = Path(__file__).resolve().parent
SDK = BASE / 'DW3_QM33_SDK_1.0.2_vehicle_suite_audit_20260907'
PROJECT = SDK / 'Drivers/API/Build_Platforms/nRF52840-DK/dw3000_api.emProject'
EMBUILD = '/Applications/SEGGER/SEGGER Embedded Studio 8.28/bin/emBuild'
BIN = Path('/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin')
CONFIGS = {x.attrib['Name']: x.attrib for x in ET.parse(PROJECT).getroot().findall('configuration')}
cases = []

def add(key, config, defs=None):
    cases.append((key, config, defs))

add('stage0_rx_p4', 'Stage0_L25_T0_Init', 'DEBUG;BRRS_TARGET_CYCLES=2000;BRRS_RX_LEAD_MARGIN_US=25;BRRS_RX_TAIL_MARGIN_US=0;BRRS_RX_PAC_SYMBOLS=4')
add('stage0_tx', 'Stage0_Normal')
for m in [32, 64, 128, 256]:
    add(f'exp1_m{m}_rx_p8', f'Exp1_{m}_Init', 'DEBUG;BRRS_RX_TAIL_MARGIN_US=0;BRRS_TARGET_CYCLES=2000;BRRS_RX_LEAD_MARGIN_US=25;BRRS_RX_PAC_SYMBOLS=8;BRRS_RX_IMMEDIATE_CONTROL=0')
add('exp1_tx', 'Exp1_Normal')
for m in [32, 64, 128, 256]:
    add(f'exp2_m{m}_rx_default_pac', f'Exp2_{m}_Init', 'DEBUG;BRRS_TARGET_CYCLES=1000;BRRS_RX_LEAD_MARGIN_US=25')
add('exp2_tx_and_exp5_tx', 'Exp2_Normal')
for variant in ['A', 'B', 'C']:
    for role in ['Init', 'Normal']:
        defs = 'DEBUG;BRRS_TARGET_CYCLES=1000;BRRS_APP_PAYLOAD_BYTES=16'
        if role == 'Init':
            defs += ';BRRS_RX_LEAD_MARGIN_US=25'
        add(f'exp3_{variant}_{role}', f'Exp3_{variant}_{role}', defs)
add('exp5_rx', 'Exp5_Init', 'DEBUG;BRRS_TARGET_CYCLES=1000;BRRS_RX_LEAD_MARGIN_US=25')
results = json.loads((ROOT / 'build_results.json').read_text()) if (ROOT / 'build_results.json').exists() else []
for key, config, defs in cases:
    if any(x['key'] == key for x in results):
        continue
    # All non-Exp4 TX profiles share the unguarded Exp4-only helper calls.
    # Stage0_TX already demonstrated this compile failure. Do not rerun it
    # under other configuration names; inspect the preprocessor guards.
    if config.endswith('_Normal'):
        results.append({'key': key, 'config': config, 'rc': None, 'status': 'SKIPPED_SHARED_TX_COMPILE_FAILURE', 'evidence': 'stage0_tx.build.log; brrs_normal.c:1982,2005; helper defined only under BRRS_EXPERIMENT==4'})
        (ROOT / 'build_results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
        continue
    command = [EMBUILD, '-threadnum', '8']
    if defs:
        command += ['-sproperty', 'c_preprocessor_definitions=' + defs]
    command += ['-config', config, '-project', 'dw3000_api', '-rebuild', str(PROJECT)]
    rec = {'key': key, 'config': config, 'config_attributes': CONFIGS[config], 'command': command,
           'started_at': datetime.datetime.now().astimezone().isoformat()}
    build_log = ROOT / (key + '.build.log')
    # The first successful compile already happened before marker extraction
    # was corrected; reuse it rather than compiling the same image twice.
    cached_first = key == 'stage0_rx_p4' and (PROJECT.parent / 'Output' / config / 'Exe/dw3000_api.elf').exists()
    if cached_first:
        rec['rc'] = 0
        rec['reused_completed_build'] = True
        rec['command'][2] = '1'
    else:
        with build_log.open('w') as log:
            completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        rec['rc'] = completed.returncode
    if rec['rc'] == 0:
        folder = PROJECT.parent / 'Output' / config / 'Exe'
        rec['images'] = {}
        for suffix in ['hex', 'elf']:
            path = folder / ('dw3000_api.' + suffix)
            rec['images'][suffix] = {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        elf = folder / 'dw3000_api.elf'
        nm = subprocess.check_output([str(BIN / 'nm'), '-S', '-n', str(elf)], text=True)
        selected = [line for line in nm.splitlines() if any(line.endswith(' ' + name) for name in ['_SEGGER_RTT', 'config_data', 'config_sync'])]
        rec['symbols'] = selected
        chunks = []
        for line in selected:
            addr, size, kind, name = line.split()
            if name not in ['config_data', 'config_sync']:
                continue
            chunks.append(subprocess.check_output([str(BIN / 'objdump'), '-s', '--start-address=0x' + addr,
                '--stop-address=' + hex(int(addr, 16) + int(size, 16)), str(elf)], text=True))
        (ROOT / (key + '.config_bytes.txt')).write_text('\n'.join(chunks))
        strings = '\n'.join(x.decode('ascii') for x in re.findall(rb'[\x20-\x7e]{5,}', elf.read_bytes()))
        rec['compiled_marker_templates'] = [s for s in strings.splitlines() if any(t in s for t in ['EXP_LOG_READY,', 'CIR_RTT_READY,', 'EXP1_DONE,', 'EXP1_TX_DONE,', 'EXP2_DONE,', 'EXP2_TX_DONE,', 'EXP3_TX_DUMP_DONE,', 'EXP3_RX_DONE,', 'CIR_RAW_DUMP_DONE,'])]
        rec['sections'] = subprocess.check_output([str(BIN / 'objdump'), '-h', str(elf)], text=True).strip()
    else:
        log = (ROOT / (key + '.build.log')).read_text(errors='replace')
        rec['errors'] = [line for line in log.splitlines() if any(t in line.lower() for t in ['error:', 'undefined reference', 'build failed'])][-12:]
    rec['finished_at'] = datetime.datetime.now().astimezone().isoformat()
    results.append(rec)
    (ROOT / 'build_results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
    print(f'{key}: rc={rec["rc"]}', flush=True)
    # A common INIT compile failure makes further M/PAC builds of the same
    # broken source redundant. Capture that failure before deciding follow-up.
    if rec['rc'] != 0:
        break
