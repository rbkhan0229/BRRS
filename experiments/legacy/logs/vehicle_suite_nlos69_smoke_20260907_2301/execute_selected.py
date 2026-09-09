#!/usr/bin/env python3
"""Execute each explicitly selected rehearsal case once; stop on invalid collection."""
import json
import pathlib
import subprocess
import sys
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parent
API = pathlib.Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
sys.path.insert(0, str(API))
from brrs_suite_case import checked, save
from brrs_suite_campaign import deploy
from brrs_suite_results import assess

selection = json.loads((ROOT/'selected_cases.json').read_text())
progress = {'started_at':datetime.now(timezone.utc).isoformat(), 'cases':[], 'scope':'one representative per distinct path; completed RF is never repeated'}
save(ROOT/'progress.json', progress)
for selected in selection['new_cases']:
    cid = selected['case_id']
    bundle = ROOT/cid
    checked(bundle)
    item = {'case_id':cid}
    progress['cases'].append(item)
    save(ROOT/'progress.json', progress)
    if (bundle/'results').exists():
        result = assess(bundle)
        item.update(action='REUSE_COMPLETED', verdict=result['verdict'])
        print('REUSE '+cid+' '+result['verdict'], flush=True)
    else:
        print('DEPLOY '+cid, flush=True)
        deployed = deploy(bundle, 's-macbook-air')
        save(ROOT/(cid+'.deployment.json'), deployed)
        print('RUN_ONCE '+cid, flush=True)
        with (ROOT/(cid+'.run.console.log')).open('x') as log:
            completed = subprocess.run([sys.executable,str(bundle/'sdk/Drivers/API/brrs_suite_case.py'),'run','--bundle',str(bundle),'--host','s-macbook-air'], stdout=log, stderr=subprocess.STDOUT)
        item.update(action='RUN_ONCE', returncode=completed.returncode)
        if completed.returncode:
            item['status']='STOPPED_CONTROL_OR_ASSESSMENT_FAILURE'
            save(ROOT/'progress.json',progress)
            raise SystemExit('STOP '+cid+'; original case preserved; no retry')
        result = assess(bundle)
        item['verdict'] = result['verdict']
        print('COMPLETE '+cid+' '+json.dumps({'verdict':result['verdict'],'nodes':result['nodes_by_serial'],'metrics':result.get('stage_metrics')},ensure_ascii=False),flush=True)
    save(ROOT/'progress.json',progress)
progress['finished_at']=datetime.now(timezone.utc).isoformat()
progress['status']='ALL_SELECTED_CASES_COLLECTED'
save(ROOT/'progress.json',progress)
print('ALL_SELECTED_CASES_COLLECTED',flush=True)

