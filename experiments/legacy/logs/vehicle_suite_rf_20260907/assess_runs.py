"""Assess completed Stage0 captures; preserve excluded control-failure runs."""
from argparse import Namespace
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parent
API=ROOT.parents[1]/'DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API'
sys.path.insert(0,str(API))
from brrs_exp1_verify import validate_rx, validate_tx, parse_kv_marker
from brrs_suite_case import checked

def goal(offered, received, control_valid):
    if not control_valid or offered<=0 or not 0<received<=offered: return 'INVALID'
    return 'PASS' if (offered-received)*100 < offered else 'FAIL_PER'

def assess(folder):
    root=ROOT/folder; c=checked(root); results=root/'results'
    rx=(results/'local/init.log').read_text(); tx=(results/'remote/N4.log').read_text()
    cond=c['conditions']; args=Namespace(preamble=cond['preamble'],lead=cond['lead_us'],tail=cond['tail_us'],pac=cond['rx_pac'],rx_mode='delayed',expected=cond['cycles'])
    details={'rx':validate_rx(rx,args),'tx':validate_tx(tx,args)}
    for text,end in [(rx,'EXP1_DONE,'),(tx,'EXP1_TX_DONE,')]:
        for prefix in ['EXP_LOG_READY,channel=1',end]:
            assert sum(line.startswith(prefix) for line in text.splitlines())==1
    rv=parse_kv_marker(rx,'EXP1_DONE,'); tv=parse_kv_marker(tx,'EXP1_TX_DONE,')
    offered=int(rv['expected']); received=int(rv['rx']); attempts=int(tv['attempts']); success=int(tv['success'])
    assert int(tv['expected'])==offered and 0<=received<=success<=attempts<=offered
    states={s:json.loads((results/s/'status.json').read_text()) for s in ['local','remote']}
    control_valid=all(s['status']=='COLLECTION_METADATA_AND_FLASH_PASS' for s in states.values())
    inactive=states['remote']['inactive_halted_after']
    control_valid &= set(inactive)==set(c['inactive_tx_roles']) and all(inactive.values())
    orchestration=json.loads((results/'orchestration.json').read_text())
    control_valid &= orchestration['status']=='COLLECTION_AND_READBACK_PASS_PER_PENDING'
    errors=re.search(r'RX timeouts=(\d+) \(fwto=(\d+) pto=(\d+)\)  RX errors=(\d+) \(sfdto=(\d+) phe=(\d+) fce=(\d+) fsl=(\d+)\)  delayed schedule late=(\d+)  data config errors=(\d+)',rx)
    assert errors
    counters=dict(zip(['rx_timeout','fwto','pto','rx_error','sfd_timeout','phr_error','crc_error','rxfsl','delayed_rx_late','data_config_error'],map(int,errors.groups())))
    sync=re.search(r'SYNC loss: (\d+) timeouts  RX errors=(\d+)  beacon_config_errors=(\d+)  data_config_errors=(\d+)',tx)
    assert sync
    counters.update(dict(zip(['tx_sync_loss_timeout','tx_sync_rx_error','tx_beacon_config_error','tx_data_config_error'],map(int,sync.groups()))))
    counters['delayed_tx_late']=int(tv['delayed_late'])
    counters['wrong_source_slot_superframe']='not separately emitted by this Stage0 firmware'
    counters['rdb']='not applicable to Stage0 single-buffer path; no Exp4 RDB validation claim'
    result={'case_id':c['id'],'bundle':str(root),'conditions':cond,'started_at_utc':orchestration['started_at'],
        'finished_at_utc':orchestration['finished_at'],'collection_markers_and_totals':'PASS',
        'control_valid':bool(control_valid),'valid_for_stage0':bool(control_valid),'excluded_reason':None if control_valid else states['remote'].get('error',orchestration.get('error')),
        'physical_tx':'N4','tx_serial':'1050282818','logical_tx':'N2','rx_serial':'1050270933',
        'offered':offered,'tx_attempts':attempts,'tx_success':success,'rx':received,
        'per_percent':100*(offered-received)/offered,'per_node_goal':goal(offered,received,control_valid),
        'counters':counters,'inactive_halted_after':inactive,'flash_readback':{side:state['workers'][role]['readback'] for side,role,state in [('local','init',states['local']),('remote','N4',states['remote'])]},
        'hex_sha256':{j['physical_role']:j['hex_sha256'] for j in c['jobs']},
        'verifier_details':details,'ready_order':{'all_tx_ready_at':orchestration['all_tx_ready_at'],'rx_started_at':states['local']['workers']['init']['started_at']},
        'no_rtt_timeout':all('timeout 미검출' not in (results/side/(role+'.console.log')).read_text() and '[rtt_capture] PASS' in (results/side/(role+'.console.log')).read_text() for side,role in [('local','init'),('remote','N4')])}
    assert result['ready_order']['all_tx_ready_at']<result['ready_order']['rx_started_at']
    (root/'ASSESSMENT.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    assert goal(2000,0,True)=='INVALID'
    assert goal(2000,2000,False)=='INVALID'
    assert goal(2000,1980,True)=='FAIL_PER'  # exactly 1% fails strict target
    assert goal(2000,1981,True)=='PASS'
    runs=[assess(x) for x in ['stage0_m32_pac8_l25','stage0_m32_pac8_l25_controlfix']]
    summary={'generated_at':datetime.now().astimezone().isoformat(),'new_rf_runs':len(runs),'valid_runs':sum(r['valid_for_stage0'] for r in runs),'runs':runs,
        'interpretation':'One Stage0 single-link point. Does not establish optimal lead or six-node/13-slot Exp4 PER.',
        'environment':{'distance_m':None,'tag':'vehicle_preparation','placement':'user installed in documented order; no placement changes by agent','tx_hub_external_power':'connected, user reported'},
        'git_changes':'none; original repositories checked separately','repeat_reason':'First capture had a detected inactive-TX control failure; second validates the fix with identical HEX files.'}
    (ROOT/'SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({'runs':[{k:r[k] for k in ['case_id','control_valid','offered','tx_success','rx','per_percent','per_node_goal']} for r in runs]},indent=2))
