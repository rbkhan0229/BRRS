#!/usr/bin/env python3
"""One coarse 4us step after both PACs failed the six-board lead20 check."""
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys

R=Path(__file__).resolve().parent
API=Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
sys.path.insert(0,str(API))
from brrs_suite_case import checked,save
from brrs_suite_manifest import load
from brrs_suite_paper import plan
from brrs_suite_results import assess
from brrs_suite_campaign import deploy

records=json.loads((R/'rotation_observations.json').read_text())
parents=[records[f'paper_exp4_m32_pac{pac}_l20_k13_s6_b02'] for pac in [8,4]]
assert all(assess(Path(a['bundle']))['verdict']=='FAIL_PER' for a in parents)
m=load(R/'rotation_diagnostic_manifest.json')
m['lead_selection']={'frozen':True,'lead_us_by_pac':{'4':24,'8':24},'evidence':{
    'kind':'coarse_4us_diagnostic_extension_after_s6_failure','paper_qualified':False,
    'meaning_of_frozen':'settings locked for this diagnostic pair only; not qualified Stage0 selection',
    'parent_cases':[a['case_id'] for a in parents]}}
m['diagnostic_scope']={'representative_block_only':2,'paper_campaign_complete':False,
    'description':'Single 4us step 20->24 for both PACs, unchanged six-board mapping and 13-slot load; once each'}
mp=R/'rotation_24us_diagnostic_manifest.json';save(mp,m)
cases={c['id']:c for c in plan(load(mp),'exp4')}
ids=[f'paper_exp4_m32_pac{pac}_l24_k13_s6_b02' for pac in [8,4]]
save(R/'rotation_24us_extension_plan.json',{'created_at':datetime.now(timezone.utc).isoformat(),
    'reason':'Both PACs at20us failed physical N7; user requests coarse ~4us steps and no exact repeats',
    'lead_step_us':4,'runs_per_condition':1,'cases':[cases[cid] for cid in ids]})
for cid in ids:
    c=cases[cid];bundle=R/cid
    # Keep physical board identity, logical assignment, slots and PHY other than PAC/lead fixed.
    parent=next(p for p in parents if p['conditions']['rx_pac']==c['conditions']['rx_pac'])
    expected=checked(Path(parent['bundle']))
    binding=lambda v:[(j['physical_role'],j['logical_node'],j['serial']) for j in v['jobs']]
    assert binding(c)==binding(expected)
    for key in ['preamble','sensors','slot_owners','guard_us','sync_buffer_us','sync_prep_us','cycles','slotted_rx','spi_opt']:
        assert c['conditions'][key]==parent['conditions'][key]
    if not bundle.exists():
        print('PREPARE '+cid,flush=True)
        with (R/(cid+'.prepare.console.log')).open('x') as log:
            rc=subprocess.run([sys.executable,str(API/'brrs_suite_case.py'),'prepare','--manifest',str(mp),
                '--stage','exp4','--profile','paper','--case',cid,'--bundle',str(bundle)],
                stdout=log,stderr=subprocess.STDOUT).returncode
        if rc:raise SystemExit('PREPARE_FAILED '+cid)
    checked(bundle)
    if not (bundle/'results').exists():
        save(R/(cid+'.deployment.json'),deploy(bundle,'s-macbook-air'))
        print('RUN_ONCE '+cid,flush=True)
        with (R/(cid+'.run.console.log')).open('x') as log:
            rc=subprocess.run([sys.executable,str(bundle/'sdk/Drivers/API/brrs_suite_case.py'),
                'run','--bundle',str(bundle),'--host','s-macbook-air'],stdout=log,stderr=subprocess.STDOUT).returncode
        if rc:raise SystemExit('CONTROL_OR_ASSESSMENT_FAILURE '+cid+'; preserve, no retry')
    a=assess(bundle);records[cid]=a;save(R/'rotation_observations.json',records)
    print('RESULT '+cid+' '+json.dumps({'verdict':a['verdict'],'nodes':a['nodes_by_serial'],
        'aggregate':a['aggregate'],'rx_error_summary':a['rx_error_summary']},ensure_ascii=False),flush=True)
print('COARSE_24US_PAIR_COMPLETE',flush=True)
