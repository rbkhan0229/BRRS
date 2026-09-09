#!/usr/bin/env python3
"""4us-only continuation: stop each PAC at its first all-node PER<1% point."""
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
chosen={}
for pac in [8,4]:
    for lead in [20,24]:
        cid=f'paper_exp4_m32_pac{pac}_l{lead}_k13_s6_b02'
        a=assess(Path(records[cid]['bundle']))
        if a['verdict']=='PASS':chosen[str(pac)]={'lead_us':lead,'case_id':cid,'worst_per_percent':a['worst_node_per_percent']};break
save(R/'network_coarse_policy.json',{'created_at':datetime.now(timezone.utc).isoformat(),
    'lead_grid_us':[20,24,28,32,36,40],'run_each_once':True,'same_physical_assignment':'block2',
    'stop_rule':'Stop each PAC at first observed all-physical-node PER<1%; no within-step optimization or confirmation repeat',
    'selection_scope':'provisional current-NLOS network candidate; not global optimum, complete paper qualification or vehicle guarantee'})
for lead in [28,32,36,40]:
    pending=[pac for pac in [8,4] if str(pac) not in chosen]
    if not pending:break
    m=load(R/'rotation_24us_diagnostic_manifest.json')
    m['lead_selection']={'frozen':True,'lead_us_by_pac':{'4':lead,'8':lead},'evidence':{
        'kind':'coarse_4us_first_pass_network_diagnostic','paper_qualified':False,
        'meaning_of_frozen':'locked diagnostic setting only; not Stage0/paper qualification',
        'policy':str(R/'network_coarse_policy.json')}}
    m['diagnostic_scope']={'representative_block_only':2,'paper_campaign_complete':False,
        'description':f'4us coarse continuation at{lead}us, same mapping and13slots; once per still-unresolved PAC'}
    mp=R/f'rotation_{lead}us_diagnostic_manifest.json';save(mp,m)
    cases={c['id']:c for c in plan(load(mp),'exp4')}
    save(R/f'rotation_{lead}us_selected_plan.json',[cases[f'paper_exp4_m32_pac{pac}_l{lead}_k13_s6_b02'] for pac in pending])
    for pac in pending:
        cid=f'paper_exp4_m32_pac{pac}_l{lead}_k13_s6_b02';c=cases[cid];bundle=R/cid
        parent=checked(Path(records[f'paper_exp4_m32_pac{pac}_l20_k13_s6_b02']['bundle']))
        binding=lambda x:[(j['physical_role'],j['logical_node'],j['serial']) for j in x['jobs']]
        assert binding(c)==binding(parent)
        for k in ['preamble','sensors','slot_owners','guard_us','sync_buffer_us','sync_prep_us','cycles','slotted_rx','spi_opt']:
            assert c['conditions'][k]==parent['conditions'][k]
        if not bundle.exists():
            print('PREPARE '+cid,flush=True)
            with (R/(cid+'.prepare.console.log')).open('x') as log:
                rc=subprocess.run([sys.executable,str(API/'brrs_suite_case.py'),'prepare','--manifest',str(mp),
                    '--stage','exp4','--profile','paper','--case',cid,'--bundle',str(bundle)],
                    stdout=log,stderr=subprocess.STDOUT).returncode
            if rc:raise SystemExit('PREPARE_FAILED '+cid)
        actual=checked(bundle)
        # A pure RX margin comparison must keep each physical transmitter's exact image.
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
        if a['verdict']=='PASS':
            chosen[str(pac)]={'lead_us':lead,'case_id':cid,'worst_per_percent':a['worst_node_per_percent']}
            print('PAC_FIRST_PASS_STOP '+str(pac)+' '+str(lead),flush=True)
    save(R/'network_candidate_progress.json',chosen)
save(R/'network_candidates.json',{'candidates_by_pac':chosen,'unresolved_pacs':[p for p in ['4','8'] if p not in chosen],
    'rule':'first all-node pass on measured20,24,28,...40us coarse grid; do not rank by lowest observed PER',
    'paper_qualified':False,'globally_optimal':False,'repeat_confirmation_performed':False})
print('COARSE_NETWORK_SCREEN_COMPLETE '+json.dumps(chosen),flush=True)
