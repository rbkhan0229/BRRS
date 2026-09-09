"""Compile repaired TX profiles once; no board access, flash or RF."""
from pathlib import Path
import datetime, hashlib, json, os, re, subprocess
import xml.etree.ElementTree as ET

BASE = Path('/Users/songchieon/Desktop/DWM3000')
ROOT = Path(__file__).resolve().parent
SDK = BASE / 'DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907'
API = SDK / 'Drivers/API'
PROJECT = API / 'Build_Platforms/nRF52840-DK/dw3000_api.emProject'
EMBUILD = '/Applications/SEGGER/SEGGER Embedded Studio 8.28/bin/emBuild'
BIN = Path('/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin')
configs = {c.attrib['Name']: c.attrib for c in ET.parse(PROJECT).getroot().findall('configuration')}
a, b = [dict(configs[k]) for k in ['Stage0_Normal', 'Exp1_Normal']]
a.pop('Name'); b.pop('Name')
assert a == b
results = json.loads((ROOT / 'verification.json').read_text()) if (ROOT / 'verification.json').exists() else {'equivalent_configuration_not_rebuilt': {'configuration': 'Exp1_Normal', 'same_attributes_as': 'Stage0_Normal'}, 'builds': []}
cases = [('Stage0_Normal', None, 'EXP1_TX_DONE,', ['stage0', 'exp1']),
         ('Exp2_Normal', None, 'EXP2_TX_DONE,', ['exp2', 'exp5'])]
for v in ['A', 'B', 'C']:
    cases.append((f'Exp3_{v}_Normal', 'DEBUG;BRRS_TARGET_CYCLES=1000;BRRS_APP_PAYLOAD_BYTES=16', f'EXP3_TX_DUMP_DONE,variant={v}', ['exp3']))
for config, defs, end, stages in cases:
    previous = next((r for r in results['builds'] if r['configuration'] == config), None)
    if previous and previous.get('verified'):
        continue
    cmd = [EMBUILD, '-threadnum', '8']
    if defs:
        cmd += ['-sproperty', 'c_preprocessor_definitions=' + defs]
    cmd += ['-config', config, '-project', 'dw3000_api', '-rebuild', str(PROJECT)]
    rec = {'configuration': config, 'stages': stages, 'command': cmd, 'started_at': datetime.datetime.now().astimezone().isoformat()}
    if previous and previous['build_rc'] == 0:
        rec = previous
        rec['reused_completed_build'] = True
    else:
        with (ROOT / (config + '.build.log')).open('w') as log:
            p = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
        rec['build_rc'] = p.returncode
    if rec['build_rc'] == 0:
        folder = PROJECT.parent / 'Output' / config / 'Exe'
        elf = folder / 'dw3000_api.elf'
        nm = subprocess.check_output([str(BIN / 'nm'), '-n', str(elf)], text=True)
        strings = '\n'.join(x.decode('ascii') for x in re.findall(rb'[\x20-\x7e]{5,}', elf.read_bytes()))
        rec['ready_present'] = 'EXP_LOG_READY,channel=1' in strings
        # The ELF stores snprintf's template; the variant is filled at runtime.
        rec['end_present'] = 'EXP3_TX_DUMP_DONE,variant=%s,' in strings if config.startswith('Exp3') else end in strings
        rec['end_marker'] = end
        rec['exp4_helper_absent'] = not any(l.endswith(' brrs_exp4_phy_switch') for l in nm.splitlines())
        rec['rtt_address'] = next(l.split()[0] for l in nm.splitlines() if l.endswith(' _SEGGER_RTT'))
        rec['images'] = {s: {'path': str(folder / ('dw3000_api.' + s)), 'sha256': hashlib.sha256((folder / ('dw3000_api.' + s)).read_bytes()).hexdigest()} for s in ['hex', 'elf']}
        rec['verified'] = rec['ready_present'] and rec['end_present'] and rec['exp4_helper_absent']
    else:
        rec['errors'] = [l for l in (ROOT / (config + '.build.log')).read_text(errors='replace').splitlines() if 'error:' in l or 'Build failed' in l]
        rec['verified'] = False
    results['builds'] = [r for r in results['builds'] if r['configuration'] != config] + [rec]
    (ROOT / 'verification.json').write_text(json.dumps(results, indent=2) + '\n')
    print(config, 'rc=', rec['build_rc'], 'verified=', rec['verified'], flush=True)
    if not rec['verified']:
        raise SystemExit(1)

cmd = ['bash', str(API / 'brrs_exp4_build.sh'), '32', '6', '250', 'N4', '25',
       '--pac', '8', '--sync-buffer', '3000', '--sync-prep', '2500', '--cycles', '1000',
       '--slotted-rx', '--spi-opt']
with (ROOT / 'Exp4_N4.build_driver.log').open('w') as log:
    p = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, env={**os.environ, 'EMBUILD_THREADS': '8'})
rec = {'command': cmd, 'build_rc': p.returncode}
if p.returncode == 0:
    folder = PROJECT.parent / 'Output/Debug/Exe/exp4/plen32_sensors6_sb3000_sp2500_guard250_lead25_spiopt_slottedrx'
    hx = folder / 'exp4_32_s6_N4.hex'
    campaign = BASE / 'logs/exp4_home_s6_multislot_pac_ab_20260907'
    old = json.loads((campaign / 'manifest.json').read_text())['variants']['L12P8']['N4']
    rec.update(hex=str(hx), sha256=hashlib.sha256(hx.read_bytes()).hexdigest(), reference_sha256=old['sha256'])
    rec['identical_to_previously_tested_exp4_hex'] = rec['sha256'] == rec['reference_sha256']
results['exp4_regression'] = rec
(ROOT / 'verification.json').write_text(json.dumps(results, indent=2) + '\n')
print('Exp4 N4:', rec, flush=True)
