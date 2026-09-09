#!/usr/bin/env python3
"""Measure explicitly selected distinct network PAC:lead points exactly once."""
import argparse
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

ap=argparse.ArgumentParser();ap.add_argument('points',nargs='+');args=ap.parse_args()
records=json.loads((R/'rotation_observations.json').read_text())
for point in args.points:
    pac,lead=map(int,point.split(':'))
    assert pac in [4,8] and 0<=lead<=40
    cid=f'paper_exp4_m32_pac{pac}_l{lead}_k13_s6_b02'
    if cid in records:
        a=assess(Path(records[cid]['bundle']))
        print('REUSE '+cid+' '+str(a['worst_node_per_percent']),flush=True);continue
    mp=R/f'rotation_{lead}us_diagnostic_manifest.json'
    if not mp.exists():
        m=load(R/'rotation_24us_diagnostic_manifest.json')
        m['lead_selection']={'frozen':True,'lead_us_by_pac':{'4':lead,'8':lead},'evidence':{
            'kind':'refined_network_lead_diagnostic','paper_qualified':False,
            'meaning_of_frozen':'locked diagnostic setting only; not Stage0/paper qualification',
            'policy':str(R/'refined_network_policy.json')}}
        m['diagnostic_scope']={'representative_block_only':2,'paper_campaign_complete':False,
            'description':f'All-six-node lead refinement at{lead}us; same mapping and13slots; no exact RF repeats'}
        save(mp,m)
    c={c['id']:c for c in plan(load(mp),'exp4')}[cid]
    parent=checked(Path(records[f'paper_exp4_m32_pac{pac}_l20_k13_s6_b02']['bundle']))
    binding=lambda x:[(j['physical_role'],j['logical_node'],j['serial']) for j in x['jobs']]
    assert binding(c)==binding(parent)
    for k in ['preamble','sensors','slot_owners','guard_us','sync_buffer_us','sync_prep_us','cycles','slotted_rx','spi_opt']:
        assert c['conditions'][k]==parent['conditions'][k]
    bundle=R/cid
    if not bundle.exists():
        print('PREPARE '+cid,flush=True)
        with (R/(cid+'.prepare.console.log')).open('x') as log:
            rc=subprocess.run([sys.executable,str(API/'brrs_suite_case.py'),'prepare','--manifest',str(mp),
                '--stage','exp4','--profile','paper','--case',cid,'--bundle',str(bundle)],
                stdout=log,stderr=subprocess.STDOUT).returncode
        if rc:raise SystemExit('PREPARE_FAILED '+cid)
    actual=checked(bundle)
    txhash=lambda x:{j['serial']:j['hex_sha256'] for j in x['jobs'] if j['logical_node']!=1}
    assert txhash(actual)==txhash(parent)
    if not (bundle/'results').exists():
        save(R/(cid+'.deployment.json'),deploy(bundle,'s-macbook-air'))
        print('RUN_ONCE '+cid,flush=True)
        with (R/(cid+'.run.console.log')).open('x') as log:
            rc=subprocess.run([sys.executable,str(bundle/'sdk/Drivers/API/brrs_suite_case.py'),
                'run','--bundle',str(bundle),'--host','s-macbook-air'],stdout=log,stderr=subprocess.STDOUT).returncode
        if rc:raise SystemExit('CONTROL_OR_ASSESSMENT_FAILURE '+cid+'; preserved, no retry')
    a=assess(bundle);records[cid]=a;save(R/'rotation_observations.json',records)
    print('RESULT '+cid+' '+json.dumps({'verdict':a['verdict'],'nodes':a['nodes_by_serial'],
        'aggregate':a['aggregate'],'rx_error_summary':a['rx_error_summary']},ensure_ascii=False),flush=True)
print('REQUESTED_NETWORK_POINTS_COMPLETE',flush=True)
