#!/usr/bin/env python3
"""Run explicitly selected Stage0 points once, reusing verified prior evidence."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parent
API=Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
sys.path.insert(0,str(API))
from brrs_suite_case import prepare,checked,save
from brrs_suite_campaign import deploy
from brrs_suite_manifest import load,plan
from brrs_suite_results import assess,context_hash

m=load(ROOT/'manifest.json')
expected={c['id']:c for c in plan(m,'stage0')}
index=ROOT/'observations.json'
observations=json.loads(index.read_text()) if index.exists() else {}
def remember(bundle,reused):
    a=assess(bundle);cid=a['case_id']
    if cid not in expected or a['conditions']!=expected[cid]['conditions'] or a['context_sha256']!=context_hash(m):
        raise ValueError('evidence does not match current physical context/point')
    observations[cid]={'bundle':str(bundle),'reused':reused,'assessment':a}
    save(index,observations)
    return a

for cid,path in json.loads((ROOT/'screening_plan.json').read_text())['reuse'].items():
    if cid not in observations:remember(Path(path),True)

parser=argparse.ArgumentParser()
parser.add_argument('points',nargs='+',help='PAC:lead, each measured at most once')
args=parser.parse_args()
for point in args.points:
    pac,lead=map(int,point.split(':'));cid=f'stage0_m32_pac{pac}_l{lead}'
    if cid not in expected:raise ValueError('point outside existing Stage0 plan')
    if cid in observations:
        a=remember(Path(observations[cid]['bundle']),observations[cid]['reused'])
        print('REUSE '+cid+' PER='+str(a['worst_node_per_percent']),flush=True);continue
    bundle=ROOT/cid
    if not bundle.exists():
        print('PREPARE '+cid,flush=True)
        with (ROOT/(cid+'.prepare.console.log')).open('x') as log:
            cmd=[sys.executable,str(API/'brrs_suite_case.py'),'prepare','--manifest',str(ROOT/'manifest.json'),'--stage','stage0','--profile','preparation','--case',cid,'--bundle',str(bundle)]
            result=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
        if result.returncode:raise SystemExit('PREPARE_FAILED '+cid)
    checked(bundle)
    if (bundle/'results').exists():
        a=remember(bundle,False)
    else:
        save(ROOT/(cid+'.deployment.json'),deploy(bundle,'s-macbook-air'))
        print('RUN_ONCE '+cid,flush=True)
        with (ROOT/(cid+'.run.console.log')).open('x') as log:
            result=subprocess.run([sys.executable,str(bundle/'sdk/Drivers/API/brrs_suite_case.py'),'run','--bundle',str(bundle),'--host','s-macbook-air'],stdout=log,stderr=subprocess.STDOUT)
        if result.returncode:raise SystemExit('CONTROL_OR_ASSESSMENT_FAILURE '+cid+'; preserved, no retry')
        a=remember(bundle,False)
    print('RESULT '+cid+' '+json.dumps({'verdict':a['verdict'],'per_percent':a['worst_node_per_percent'],'node':next(iter(a['nodes_by_serial'].values()))},ensure_ascii=False),flush=True)
print('SELECTED_POINTS_COMPLETE',flush=True)

