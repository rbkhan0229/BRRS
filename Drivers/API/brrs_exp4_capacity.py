#!/usr/bin/env python3
"""Read-only Exp4 capacity screening: validate completed bundles, suggest next case.

This never builds, flashes, or starts RF. Paper repetitions/rotation are a
separate campaign requirement; a single-run screening maximum is labelled so.
"""
import argparse
import json
from pathlib import Path

from brrs_suite_manifest import load, plan, max_slots
from brrs_suite_case import checked, sha
import brrs_exp4_verify as verify

def per_goal(nodes):
    if not nodes: raise ValueError('no node observations')
    for row in nodes.values():
        offered, received, sent = row['offered'],row['rx'],row['tx_success']
        if any(type(x) is not int for x in [offered,received,sent]): raise ValueError('counts must be integers')
        if offered<=0 or not 0<=received<=sent<=offered: raise ValueError('inconsistent TX/RX/offered counts')
    if sum(n['rx'] for n in nodes.values())==0: raise ValueError('zero valid packets is not a valid capacity observation')
    return 'PASS' if all((n['offered']-n['rx'])*100 < n['offered'] for n in nodes.values()) else 'FAIL_PER'

def strict_lines(text):
    lines=text.splitlines()
    for prefix in ['EXP_LOG_READY,channel=1','===== END STATS =====']:
        if sum(l.startswith(prefix) for l in lines)!=1: raise ValueError('missing/duplicate '+prefix)
    return lines

def assess_bundle(root, expected_case):
    """Verify original raw logs, metadata, TX denominators and supervisor evidence."""
    from brrs_suite_evidence import read_evidence
    root=Path(root).resolve()
    c,lines_by_role,orchestration=read_evidence(root,expected_case);p=c['conditions']
    init_job=next(j for j in c['jobs'] if j['logical_node']==1)
    rx_lines=lines_by_role[init_job['physical_role']]
    verify.verify_init(rx_lines,p['preamble'],p['sensors'],p['guard_us'],p['lead_us'],p['rx_pac'],
        p['sync_buffer_us'],p['sync_prep_us'],100,p['slot_owners'],spi_opt=p['spi_opt'],slotted_rx=p['slotted_rx'],expected_cycles=p['cycles'])
    rows={f[1]:f for f in (l.split(',') for l in rx_lines if l.startswith('EXP4_NODE_CSV,'))}
    nodes={}
    for job in c['jobs']:
        node=job['logical_node']
        if node==1: continue
        tx_lines=lines_by_role[job['physical_role']]
        verify.verify_sensor(tx_lines,p['preamble'],p['sensors'],node,p['guard_us'],p['sync_buffer_us'],p['sync_prep_us'],p['slot_owners'],p['cycles'])
        tx=verify.csv_fields(verify.last_line(tx_lines,'EXP4_TX_RESULT_CSV,'))
        rx=rows[f'N{node}']; offered=p['cycles']*p['slot_owners'].count(str(node))
        if int(rx[3])!=offered: raise ValueError('offered denominator mismatch')
        nodes[job['serial']]={'physical_role':job['physical_role'],'logical_node':node,'location':job['location'],
            'offered':offered,'tx_attempts':int(tx[6]),'tx_success':int(tx[7]),'rx':int(rx[4]),
            'beacons':int(tx[4]),'beacon_missed':int(tx[5]),'delayed_tx_late':int(tx[8]),'end_marker':int(tx[9]),
            'rx_errors':int(rx[6]),'per_percent':100*(offered-int(rx[4]))/offered}
    verdict=per_goal(nodes)
    config=verify.key_values(verify.last_line(rx_lines,'EXP4_CONFIG_CSV,'))
    timing=verify.key_values(verify.last_line(rx_lines,'EXP4_TIMING_CSV,'))
    elapsed_us=int(timing['elapsed_us'])
    if elapsed_us<=0: raise ValueError('nonpositive measurement duration')
    offered=sum(n['offered'] for n in nodes.values()); received=sum(n['rx'] for n in nodes.values())
    payload_bits=8*int(config['app_payload_bytes'])
    slots=[verify.key_values(l) for l in rx_lines if l.startswith('EXP4_SLOT_RX_CSV,')]
    by_node={str(n['logical_node']):(serial,n) for serial,n in nodes.items()}
    for slot in slots:
        serial,n=by_node[slot['owner']]
        slot.update(physical_serial=serial,physical_role=n['physical_role'],location=n['location'])
    return {'case_id':c['id'],'conditions_sha256':c['conditions_sha256'],'bundle':str(root),'verdict':verdict,
            'nodes_by_serial':nodes,'worst_node_per_percent':max(n['per_percent'] for n in nodes.values()),
            'slots':slots,'error_counters':{key:verify.key_values(verify.last_line(rx_lines,prefix)) for key,prefix in
                [('rdb','EXP4_DOUBLE_BUFFER_CSV,'),('deferred','EXP4_DEFERRED_CSV,'),('spi','EXP4_SPI_CSV,'),('timing','EXP4_TIMING_CSV,')]},
            'rx_error_summary':[l for l in rx_lines if l.startswith('RX timeouts=')],
            'aggregate':{'offered':offered,'rx':received,'per_percent':100*(offered-received)/offered,
                'elapsed_us':elapsed_us,'app_payload_bytes':int(config['app_payload_bytes']),
                'offered_reports_per_second':offered*1e6/elapsed_us,
                'received_reports_per_second':received*1e6/elapsed_us,
                'app_goodput_bps':received*payload_bits*1e6/elapsed_us},
            'conditions':p,'condition_id':c.get('condition_id',c['id']),
            'scope':'one case; repeated paper conditions must be aggregated by physical serial'}

def next_case(cases, observations):
    """No monotonic-PER assumption: inspect all higher loads before naming a max."""
    if not cases: raise ValueError('empty candidate group')
    pending=[c for c in cases if c['id'] not in observations]
    for c in cases:
        row=observations.get(c['id'])
        if row is not None and row['verdict'] not in ['PASS','FAIL_PER']:
            return {'status':'STOP_INVALID','case_id':c['id'],'reason':row.get('error','invalid evidence')}
    baseline=min(cases,key=lambda c:len(c['conditions']['slot_owners']))
    if baseline in pending: return {'status':'NEXT','case_id':baseline['id'],'purpose':'common-load baseline'}
    descending=sorted(cases,key=lambda c:len(c['conditions']['slot_owners']),reverse=True)
    for c in descending:
        if c in pending: return {'status':'NEXT','case_id':c['id'],'purpose':'largest unmeasured candidate'}
        if observations[c['id']]['verdict']=='PASS':
            return {'status':'SCREENING_MAX_FOUND','case_id':c['id'],'slots':len(c['conditions']['slot_owners']),
                    'paper_capacity_validated':False,'reason':'timing maximum reached or all higher candidates failed PER; selected profile repetitions still required'}
    return {'status':'NO_PASS_IN_CONFIGURED_RANGE','paper_capacity_validated':False,
            'reason':'does not establish a smaller physical-node capacity'}

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('manifest',type=Path); ap.add_argument('--bundles',type=Path,nargs='*',default=[])
    ap.add_argument('--profile',choices=['preparation','full','essential','lite','paper'],default='preparation')
    ap.add_argument('--exclusions',type=Path)
    args=ap.parse_args()
    if args.profile!='preparation':return profile_capacity(args)
    m=load(args.manifest); cases=plan(m,'exp4',capacity_candidates=True)
    by_id={c['id']:c for c in cases}; observations={}; rejected=[]
    for root in args.bundles:
        try:
            c=checked(root.resolve()); cid=c['id']
            if cid not in by_id: raise ValueError('unknown case')
            if cid in observations: raise ValueError('duplicate case: do not pool repeats or select the best run')
            try: observations[cid]=assess_bundle(root,by_id[cid])
            except (KeyError,IndexError,TypeError,OSError,ValueError,verify.VerificationError) as exc:
                observations[cid]={'case_id':cid,'verdict':'INVALID','error':str(exc),'bundle':str(root)}
        except (KeyError,IndexError,TypeError,OSError,ValueError) as exc:
            rejected.append({'bundle':str(root),'error':str(exc)})
    groups=[]
    for pac in m['pacs']:
        for plen in m['exp4']['preambles']:
            group=[c for c in cases if c['conditions']['rx_pac']==pac and c['conditions']['preamble']==plen]
            groups.append({'preamble':plen,'pac':pac,'timing_max_slots':max_slots(m['exp4'],plen),
                'candidate_case_ids':[c['id'] for c in group],
                'decision':({'status':'STOP_INVALID_INPUT'} if rejected else next_case(group,observations))})
    print(json.dumps({'rf_execution_performed':False,'groups':groups,'observations':observations,'rejected':rejected,
                      'physical_tx_limit':m['exp4']['sensors'],'paper_repetition_rotation_validated':False},indent=2))
    return 2 if rejected or any(o['verdict']=='INVALID' for o in observations.values()) else 0

def profile_capacity(args):
    from brrs_suite_results import collect
    m=load(args.manifest)
    cases=[c for c in plan(m,'exp4',profile=args.profile,capacity_candidates=True) if c['conditions']['sensors']==6]
    report=collect(m,cases,args.bundles,json.loads(args.exclusions.read_text()) if args.exclusions else None)
    aggregates={g['condition_id']:g for g in report['groups']};groups=[]
    combinations=sorted({(c['conditions']['rx_pac'],c['conditions']['preamble']) for c in cases})
    for pac,plen in combinations:
        subset=[c for c in cases if c['conditions']['rx_pac']==pac and c['conditions']['preamble']==plen]
        representatives={c['condition_id']:{'id':c['condition_id'],'conditions':c['conditions']} for c in subset}
        seen={cid:{'verdict':g['status']} for cid,g in aggregates.items() if cid in representatives and g['status']!='INCOMPLETE'}
        decision={'status':'STOP_INVALID_INPUT'} if report['rejected'] else next_case(list(representatives.values()),seen)
        if decision['status']=='NEXT':
            g=aggregates[decision['case_id']];decision['condition_id']=decision['case_id'];decision['case_id']=g['missing_case_ids'][0]
            decision['purpose']='complete predeclared repetitions at this load'
        elif decision['status']=='SCREENING_MAX_FOUND':
            is_full=args.profile in ['full','paper']
            decision.update(status='FULL_OBSERVED_MAX_FOUND' if is_full else 'PROFILE_OBSERVED_MAX_FOUND',
                profile_capacity_validated=True,paper_capacity_validated=is_full,full_capacity_validated=is_full,
                reason='complete selected-profile cases; every observed run and physical TX PER<1%; scope limited to measured campaign')
        groups.append({'preamble':plen,'pac':pac,'decision':decision})
    report['capacity_decisions']=groups
    print(json.dumps(report,indent=2))
    return 2 if report['rejected'] or any(g['status']=='INVALID' for g in report['groups']) else 0

if __name__=='__main__':
    try: raise SystemExit(main())
    except (KeyError,IndexError,TypeError,OSError,ValueError,verify.VerificationError) as exc:
        raise SystemExit(f'ERROR: {exc}')
