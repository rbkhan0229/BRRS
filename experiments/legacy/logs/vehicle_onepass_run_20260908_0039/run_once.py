#!/usr/bin/env python3
"""Execute the approved remaining configurations once, preserving failures."""
import json
from pathlib import Path
import subprocess
import sys
from datetime import datetime

R = Path(__file__).resolve().parent
API = Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
sys.path.insert(0, str(API))
from brrs_suite_case import checked, save, sha
from brrs_suite_campaign import deploy
from brrs_suite_results import assess

plan = json.loads((R/('ACTIVE_PLAN.json' if (R/'ACTIVE_PLAN.json').exists() else 'PLAN.json')).read_text())
total = len(plan['cases'])
assert sha(R/'manifest.json') == plan['manifest_sha256']
for p, expected in plan['source_sha256'].items():
    assert sha(API/p) == expected, p
records = json.loads((R/'observations.json').read_text()) if (R/'observations.json').exists() else {}
for i, c in enumerate(plan['cases'], 1):
    cid = c['id']; bundle = R/cid
    if (R/'pause_after_current').exists():
        print('PAUSED_BETWEEN_CASES', flush=True); sys.exit(2)
    if cid in records:
        records[cid] = assess(Path(records[cid]['bundle']))
        print(f'REUSE {i}/{total} {cid}', flush=True); continue
    save(R/'progress.json', {'completed':len(records),'total':total,'current':cid,'phase':'prepare','at':datetime.now().astimezone().isoformat()})
    print(f'PREPARE {i}/{total} {cid}', flush=True)
    if not bundle.exists():
        with (R/(cid+'.prepare.console.log')).open('x') as log:
            rc = subprocess.run([sys.executable,str(API/'brrs_suite_case.py'),'prepare','--manifest',str(R/'manifest.json'),
                '--stage',c['conditions']['stage'],'--profile','paper','--case',cid,'--bundle',str(bundle)],stdout=log,stderr=subprocess.STDOUT).returncode
        if rc: raise SystemExit('PREPARE_FAILED '+cid+'; preserved without RF')
    actual = checked(bundle)
    assert actual['conditions'] == c['conditions']
    assert actual['firmware_source_sha256'] == plan['source_sha256']
    if not (bundle/'results').exists():
        save(R/(cid+'.deployment.json'), deploy(bundle,'s-macbook-air'))
        save(R/'progress.json', {'completed':len(records),'total':total,'current':cid,'phase':'rf_capture','at':datetime.now().astimezone().isoformat()})
        print(f'RUN_ONCE {i}/{total} {cid}', flush=True)
        with (R/(cid+'.run.console.log')).open('x') as log:
            rc = subprocess.run([sys.executable,str(bundle/'sdk/Drivers/API/brrs_suite_case.py'),'run','--bundle',str(bundle),'--host','s-macbook-air'],stdout=log,stderr=subprocess.STDOUT).returncode
        if rc: raise SystemExit('CONTROL_OR_ASSESSMENT_FAILURE '+cid+'; preserved without RF retry')
    a = assess(bundle); records[cid] = a; save(R/'observations.json',records)
    save(R/'progress.json', {'completed':len(records),'total':total,'current':cid,'phase':'complete','at':datetime.now().astimezone().isoformat()})
    print(f'RESULT {i}/{total} {cid} {a["verdict"]} worst_PER={a["worst_node_per_percent"]}%',flush=True)
print('ALL_SELECTED_FUNCTIONAL_CASES_COMPLETE',flush=True)
