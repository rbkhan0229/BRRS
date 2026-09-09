#!/usr/bin/env python3
"""One user-requested PAC4/27 recheck; separate uncertain cable context."""
from datetime import datetime
import copy
import json
from pathlib import Path
import subprocess
import sys

PARENT=Path(__file__).resolve().parent
API=Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
sys.path.insert(0,str(API))
from brrs_suite_case import checked,save
from brrs_suite_manifest import load
from brrs_suite_results import assess
from brrs_suite_campaign import deploy

prior=PARENT/'paper_exp4_m32_pac4_l27_k13_s6_b02';pa=assess(prior);pc=checked(prior)
records=json.loads((PARENT/'rotation_observations.json').read_text())
pac4=[a for a in records.values() if a['conditions']['sensors']==6 and a['conditions']['rx_pac']==4]
assert pa['worst_node_per_percent']==min(a['worst_node_per_percent'] for a in pac4)
index=PARENT/'cable_recheck_root.json'
if index.exists():R=Path(json.loads(index.read_text())['root'])
else:
    R=PARENT.parent/('exp4_pac4_lead27_cable_recheck_'+datetime.now().strftime('%Y%m%d_%H%M'))
    R.mkdir();save(index,{'root':str(R),'prior_bundle':str(prior)})
m=copy.deepcopy(load(prior/'board_manifest.json'))
m['environment']='NLOS_6.9m_cable_report_'+datetime.now().strftime('%Y%m%d_%H%M')
m['lead_selection']['evidence']={'kind':'user_requested_best_observed_PAC4_single_recheck',
    'paper_qualified':False,'prior_bundle':str(prior),'prior_worst_node_per_percent':pa['worst_node_per_percent'],
    'meaning_of_frozen':'exact27us setting held for this one recheck only'}
m['diagnostic_scope']={'run_once':True,'paper_campaign_complete':False,'description':'Same PAC4/27us, M32/S6/13slots, block2 mapping after user reported cable obscuring a node; not a new optimization sweep'}
m['setup_record'].update(created_at=datetime.now().astimezone().isoformat(),
    scope=m['diagnostic_scope']['description'],
    cable_occlusion_report={'user_report':'방금 전선이 노드를 가리고 있었어',
        'affected_physical_node':'unknown; user does not know','onset':'unknown; user does not know',
        'user_followup':'특정 leadmargin으로 한번 해보고 결과가 많이 다르면 그때 생각해보자',
        'current_configuration':'as left by user at recheck request; cable clearance not independently observed',
        'comparison_policy':'preserve prior records without inventing exclusion boundaries; separate current physical-context record, do not pool'})
mp=R/'manifest.json'
if not mp.exists():save(mp,m)
save(R/'recheck_plan.json',{'prior_bundle':str(prior),'prior_assessment':pa,
    'reason':'User requests one PAC4 run at its best observed lead after cable obstruction report',
    'repeat_count':1,'exact_images_and_mapping_required':True,'prior_exclusion_boundaries_known':False})
bundle=R/'capture1'
if not bundle.exists():
    print('PREPARE_RECHECK '+str(R),flush=True)
    with (R/'prepare.console.log').open('x') as log:
        rc=subprocess.run([sys.executable,str(API/'brrs_suite_case.py'),'prepare','--manifest',str(mp),
            '--stage','exp4','--profile','paper','--case',pc['id'],'--bundle',str(bundle),'--reuse'],
            stdout=log,stderr=subprocess.STDOUT).returncode
    if rc:raise SystemExit('RECHECK_PREPARE_FAILED; no RF started')
c=checked(bundle)
identity=lambda x:[(j['physical_role'],j['logical_node'],j['serial'],j['hex_sha256']) for j in x['jobs']]
assert identity(c)==identity(pc)
save(R/'exact_image_and_assignment_comparison.json',{'all_seven_exact_match':True,'prior_bundle':str(prior),
    'images':[{'physical_role':j['physical_role'],'logical_node':j['logical_node'],'serial':j['serial'],'hex_sha256':j['hex_sha256']} for j in c['jobs']]})
if not (bundle/'results').exists():
    save(R/'deployment.json',deploy(bundle,'s-macbook-air'))
    print('RUN_PAC4_LEAD27_RECHECK_ONCE',flush=True)
    with (R/'run.console.log').open('x') as log:
        rc=subprocess.run([sys.executable,str(bundle/'sdk/Drivers/API/brrs_suite_case.py'),
            'run','--bundle',str(bundle),'--host','s-macbook-air'],stdout=log,stderr=subprocess.STDOUT).returncode
    if rc:raise SystemExit('RECHECK_CONTROL_OR_ASSESSMENT_FAILURE; preserve, no automatic retry')
a=assess(bundle);save(R/'ASSESSMENT.json',a)
save(R/'comparison.json',{'prior':pa,'current':a,'pool_results':False,
    'physical_context_uncertainty':'which node and when cable occlusion began are unknown; no retrospective exclusion boundary invented'})
print('CABLE_RECHECK_RESULT '+json.dumps({'root':str(R),'verdict':a['verdict'],
    'nodes':a['nodes_by_serial'],'aggregate':a['aggregate'],'rx_error_summary':a['rx_error_summary']},ensure_ascii=False),flush=True)
