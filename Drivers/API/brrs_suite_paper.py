#!/usr/bin/env python3
"""Paper repetitions and balanced physical-board assignments. No hardware I/O."""
import copy
import hashlib
import json

STAGES=['stage0','exp1','exp2','exp3','exp4','exp5']

def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()

def validate(m):
    p=m.get('paper')
    if p is None: return
    counts=p['repeats_by_stage']
    if set(counts)!=set(STAGES) or any(type(x) is not int or not 1<=x<=100 for x in counts.values()):
        raise ValueError('paper repeats must specify every stage, 1..100')
    if counts['stage0']!=1 or counts['exp4']%6:
        raise ValueError('Stage0 grid is once; paper Exp4 repeats must be complete six-block cycles')
    if p['active_tx_counts']!=[1,2,3,4,5,6] or p['exp4_assignment']!='installation_cyclic':
        raise ValueError('paper requires S1..S6 and installation-based cyclic assignments')
    if p['condition_order']!='alternating_rotated': raise ValueError('unsupported condition order')
    if type(p['stage0_confirmation_repeats']) is not int or not 1<=p['stage0_confirmation_repeats']<=100:
        raise ValueError('invalid confirmation repetitions')
    from brrs_suite_manifest import max_slots
    for plen in m['exp4']['preambles']:
        ks=p['s6_slot_counts_by_preamble'][str(plen)]
        if not ks or len(set(ks))!=len(ks) or any(type(k) is not int or not 6<=k<=max_slots(m['exp4'],plen) for k in ks):
            raise ValueError('invalid paper S6 load plan')

def assignments(m, sensors, block):
    """Each logical role visits every physical board once in six blocks."""
    roles=['N2','N3','N4','N5','N6','N7']
    if sensors==1: return [m['single_link_tx_role']]
    return [roles[(i+block-1)%6] for i in range(sensors)]

def plan(m, stage, capacity_candidates=False, confirmation=False):
    from brrs_suite_manifest import plan as base_plan, capacity_counts
    validate(m)
    if 'paper' not in m: raise ValueError('paper configuration missing')
    if capacity_candidates and stage!='exp4':raise ValueError('capacity candidates are Exp4 only')
    if confirmation and stage!='stage0': raise ValueError('confirmation is Stage0 only')
    if confirmation and not m.get('lead_candidates_us_by_pac'):
        raise ValueError('Stage0 candidates missing; assess grid before planning confirmations')
    paper=m['paper']; result=[]
    repeats=paper['stage0_confirmation_repeats'] if confirmation else paper['repeats_by_stage'][stage]
    for block in range(1,repeats+1):
        batch=[]
        for sensors in (paper['active_tx_counts'] if stage=='exp4' else [m['exp4']['sensors']]):
            t=copy.deepcopy(m)
            if stage=='exp4':
                e=t['exp4'];e['sensors']=sensors;e.pop('capacity_search',None)
                e['slot_counts_by_preamble']={str(plen):([sensors] if sensors<6 else
                    capacity_counts({**m['exp4'],'sensors':6},plen) if capacity_candidates else paper['s6_slot_counts_by_preamble'][str(plen)]) for plen in e['preambles']}
                if sensors<6:e['sequences_by_preamble_slotcount']={}
                physical=assignments(m,sensors,block)
                for i,role in enumerate(physical,2): t['boards'][f'N{i}']=copy.deepcopy(m['boards'][role])
            raw=base_plan(t,stage)
            if confirmation:
                raw=[c for c in raw if c['conditions']['lead_us'] in confirmation_leads(m,c['conditions']['rx_pac'])]
            for c in raw:
                p=c['conditions'];p.update(run=block,profile='paper',rotation_index=(block-1)%6 if stage=='exp4' and sensors>1 else 0,
                    phase='confirmation' if confirmation else 'main')
                c['condition_id']=c['id']+(f'_s{sensors}' if stage=='exp4' else '')+('_confirmation' if confirmation else '')
                c['id']='paper_'+c['condition_id']+f'_b{block:02d}'
                for j in c['jobs']:
                    # Capture role/HEX is logical; serial and output key are physical.
                    role=next(r for r,b in m['boards'].items() if b['serial']==j['serial'])
                    j['physical_role']=role;j['location']=m['boards'][role]['location']
                    idx=5 if stage=='exp4' else 3 if stage=='exp5' else 4
                    j['argv'][idx]=str(block);j['build_only_argv'][idx]=str(block)
                p['active_physical_roles']=[j['physical_role'] for j in c['jobs'] if j['logical_node']!=1]
                c['conditions_sha256']=hashlib.sha256(json.dumps(p,sort_keys=True).encode()).hexdigest()
                c['inactive_tx_roles']=[r for r in ['N2','N3','N4','N5','N6','N7'] if r not in p['active_physical_roles']]
                c['assignment_sha256']=digest([(j['physical_role'],j['logical_node'],j['serial'],j['location']) for j in c['jobs']])
                for j in c['jobs']:
                    j['environment'].update(BRRS_SUITE_MANIFEST_SHA256=digest(m),BRRS_SUITE_CONDITIONS_SHA256=c['conditions_sha256'],
                        BRRS_SUITE_CASE_ID=c['id'],BRRS_SUITE_PHYSICAL_ROLE=j['physical_role'],BRRS_SUITE_LOGICAL_NODE=str(j['logical_node']),
                        BRRS_SUITE_PROFILE='paper',BRRS_SUITE_BLOCK=str(block),BRRS_SUITE_ROTATION_INDEX=str(p['rotation_index']),
                        BRRS_SUITE_LOCATION=j['location'],BRRS_SUITE_ASSIGNMENT_SHA256=c['assignment_sha256'])
                batch.append(c)
        # Counterbalance condition order independently of role rotation.
        offset=(block-1)%len(batch);batch=batch[offset:]+batch[:offset]
        if block%2==0:batch.reverse()
        result.extend(batch)
    return result

def confirmation_leads(m,pac):
    candidate=m['lead_candidates_us_by_pac'].get(str(pac))
    if type(candidate) is not int or candidate not in m['stage0']['leads_us']:
        raise ValueError('candidate must be a measured Stage0 grid lead')
    # A PAC/acquisition boundary can make the integer-lead response
    # non-monotonic.  Reliability is established by independent repetitions
    # of the selected point; the full grid preserves its neighbouring shape.
    return [candidate]
