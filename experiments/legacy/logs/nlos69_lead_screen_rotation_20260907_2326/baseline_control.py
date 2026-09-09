#!/usr/bin/env python3
"""Replay exact earlier passing baseline only if rotated lead25 fails."""
from datetime import datetime,timezone
import json
from pathlib import Path
import shutil
import subprocess
import sys

R=Path(__file__).resolve().parent
API=Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
sys.path.insert(0,str(API))
from brrs_suite_case import checked,save,sha
from brrs_suite_results import assess
from brrs_suite_campaign import deploy

rotated=R/'paper_exp4_m32_pac8_l25_k13_s6_b02'
ra=assess(rotated)
if ra['verdict']=='PASS':
    save(R/'baseline_control_decision.json',{'at':datetime.now(timezone.utc).isoformat(),
        'decision':'SKIP_EXTRA_RF','reason':'Rotated lead25 also passes; no repeat of earlier baseline needed',
        'rotated_assessment':ra})
    print('ROTATED_25_PASS; original baseline repeat skipped',flush=True);raise SystemExit(0)
source=Path('/Users/songchieon/Desktop/DWM3000/logs/exp4_nlos69_s6_pac8_recheck_20260907_2248/capture1')
original=assess(source);assert original['verdict']=='PASS'
bundle=R/'original_roles_pac8_lead25_control'
save(R/'baseline_control_decision.json',{'at':datetime.now(timezone.utc).isoformat(),
    'decision':'ONE_EXACT_BASELINE_CONTROL','reason':'New rotated25 failure contradicts old original-role25 pass; necessary role/time control, not a repeat to cherry-pick best PER',
    'previous_bundle':str(source),'previous_assessment':original,'rotated_assessment':ra,
    'new_bundle':str(bundle),'preserve_both_valid_results':True})
if not bundle.exists():
    checked(source);bundle.mkdir()
    index=json.loads((source/'payload_hashes.json').read_text())
    for rel in index:
        target=bundle/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source/rel,target)
    shutil.copy2(source/'payload_hashes.json',bundle/'payload_hashes.json')
checked(bundle)
assert sha(source/'payload_hashes.json')==sha(bundle/'payload_hashes.json')
if not (bundle/'results').exists():
    save(R/'baseline_control.deployment.json',deploy(bundle,'s-macbook-air'))
    print('RUN_EXACT_BASELINE_CONTROL_ONCE',flush=True)
    with (R/'baseline_control.run.console.log').open('x') as log:
        rc=subprocess.run([sys.executable,str(bundle/'sdk/Drivers/API/brrs_suite_case.py'),
            'run','--bundle',str(bundle),'--host','s-macbook-air'],stdout=log,stderr=subprocess.STDOUT).returncode
    if rc:raise SystemExit('BASELINE_CONTROL_FAILURE; preserve, no retry')
a=assess(bundle);save(R/'baseline_control_assessment.json',a)
print('BASELINE_CONTROL_RESULT '+json.dumps({'verdict':a['verdict'],'per_by_role':{
    n['physical_role']:n['per_percent'] for n in a['nodes_by_serial'].values()},'rx_errors':a['rx_error_summary']},ensure_ascii=False),flush=True)
