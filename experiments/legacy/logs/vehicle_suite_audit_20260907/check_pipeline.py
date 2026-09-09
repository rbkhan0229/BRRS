"""Read-only command expansion and validation checks. Never touches hardware."""
from pathlib import Path
import json, subprocess, xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
BASE = Path('/Users/songchieon/Desktop/DWM3000')
API = BASE / 'DW3_QM33_SDK_1.0.2_exp4_s6_multislot_20260907/Drivers/API'
runner = API / 'brrs_run_experiment.sh'
results = []
for stage in ['stage0', 'exp1', 'exp2', 'exp3', 'exp4', 'exp5']:
    for role in (['init', 'N4'] if stage == 'exp4' else ['rx', 'tx']):
        serial = '1050270933' if role in ['rx', 'init'] else '1050282818'
        command = ['bash', str(runner), stage, role, 'vehicle_suite_audit', '--serial', serial, '--dry-run']
        if stage == 'exp4':
            command += ['--sensors', '6', '--guard', '250']
        if stage != 'stage0':
            command += ['--lead', '25']
        result = subprocess.run(command, capture_output=True, text=True)
        (ROOT / f'{stage}_{role}.dry_run.log').write_text(result.stdout + result.stderr)
        results.append({'stage': stage, 'role': role, 'command': command, 'rc': result.returncode,
                        'cases': result.stdout.count('CASE_START '),
                        'all_expanded_commands_have_serial': all('--serial ' + serial in line for line in result.stdout.splitlines() if line.startswith('  '))})

unsupported = []
for stage, args in [('exp2', ['--pac', '4']), ('exp5', ['--pac', '8']),
                    ('exp4', ['--sensors', '6', '--slotted-rx', '--spi-opt'])]:
    cmd = ['bash', str(runner), stage, 'init' if stage == 'exp4' else 'rx', 'vehicle_suite_audit', '--dry-run'] + args
    result = subprocess.run(cmd, capture_output=True, text=True)
    unsupported.append({'command': cmd, 'rc': result.returncode, 'stderr': result.stderr.strip()})

serials = ['1050211584', '1050273888', '1050282818', '1050208509', '1050227627', '1050204212']
cmd = ['python3', str(API / 'brrs_exp4_probe_assign.py'), '--sensors', '6', '--run', '1', '--serials', ','.join(serials), '--format', 'tsv']
assignment = subprocess.run(cmd, capture_output=True, text=True)
actual = dict(line.split() for line in assignment.stdout.splitlines())
expected = dict(zip(['N2', 'N3', 'N4', 'N5', 'N6', 'N7'], serials))
syntax = {}
for pattern in ['brrs*sh', 'rtt_capture.py', 'brrs*py']:
    for file in sorted(API.glob(pattern)):
        if file.suffix == '.sh':
            r = subprocess.run(['bash', '-n', str(file)], capture_output=True, text=True)
            syntax[file.name] = {'rc': r.returncode, 'stderr': r.stderr.strip()}
        else:
            try:
                compile(file.read_text(), str(file), 'exec')
                syntax[file.name] = {'rc': 0}
            except Exception as e:
                syntax[file.name] = {'rc': 1, 'error': str(e)}

project = ET.parse(API / 'Build_Platforms/nRF52840-DK/dw3000_api.emProject').getroot()
names = {x.attrib['Name'] for x in project.findall('configuration')}
out = {'dry_runs': results, 'unsupported_options': unsupported,
       'stage0_missing_configs': [i for i in range(41) if f'Stage0_L{i}_T0_Init' not in names],
       'auto_tx_assignment': {'actual_run1': actual, 'required': expected, 'matches': actual == expected},
       'syntax_checks': syntax, 'rf_runs': 0}
(ROOT / 'pipeline_checks.json').write_text(json.dumps(out, indent=2) + '\n')
print(json.dumps({'dry_runs': len(results), 'dry_run_cases': sum(x['cases'] for x in results),
                  'syntax_files': len(syntax), 'syntax_failures': [k for k,v in syntax.items() if v['rc']],
                  'role_assignment_matches': actual == expected, 'auto_assignment': actual}, indent=2))
