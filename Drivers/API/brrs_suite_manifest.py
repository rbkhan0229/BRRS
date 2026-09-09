#!/usr/bin/env python3
"""Resolve fixed hardware and stage settings into reviewable capture commands.

This module never accesses hardware or executes a capture. A later supervisor
must quiesce inactive boards and wait for each case's TX READY before RX starts.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

API = Path(__file__).resolve().parent
AIRTIME_US = {32: 97, 64: 130, 128: 195, 256: 325}
ROLES = ['init', 'N2', 'N3', 'N4', 'N5', 'N6', 'N7']

def load(path):
    m = json.loads(Path(path).read_text())
    if m.get('schema_version') != 1 or set(m['boards']) != set(ROLES):
        raise ValueError('manifest must contain exactly init and N2..N7')
    serials = [m['boards'][r]['serial'] for r in ROLES]
    if any(not isinstance(s, str) or not re.fullmatch(r'[0-9]+', s) for s in serials) or len(set(serials)) != 7:
        raise ValueError('board serials must be unique decimal strings')
    if m['single_link_tx_role'] not in ROLES[1:]:
        raise ValueError('invalid single-link physical TX role')
    if 'cir_link_tx_roles' in m:
        links=m['cir_link_tx_roles']
        if not isinstance(links,list) or not links or any(r not in ROLES[1:] for r in links) or len(set(links))!=len(links):
            raise ValueError('cir_link_tx_roles must be a nonempty ordered list of unique physical TX roles')
    if m['pacs'] != [4, 8] or m['repeat_each_condition'] != 1:
        raise ValueError('this campaign requires PAC4/PAC8 and one run per condition')
    if not re.fullmatch(r'[A-Za-z0-9._-]+', m['environment']):
        raise ValueError('invalid environment tag')
    distance = m.get('distance_m')
    if distance is not None and (type(distance) not in [int, float] or distance < 0):
        raise ValueError('distance must be a non-negative number or null')
    if not m['stage0']['leads_us'] or len(set(m['stage0']['leads_us'])) != len(m['stage0']['leads_us']):
        raise ValueError('Stage0 leads must be nonempty and unique')
    if any(type(v) is not int or not 0 <= v <= 40 for v in m['stage0']['leads_us']) or m['stage0']['tail_us'] != 0:
        raise ValueError('supported Stage0 project range is 0..40 us with tail0')
    for stage in ['exp1', 'exp2', 'exp4']:
        if not m[stage]['preambles'] or any(v not in AIRTIME_US for v in m[stage]['preambles']):
            raise ValueError('unsupported preamble list: ' + stage)
    if m['exp3'] != {'variants': ['A','B','C'], 'pac': 8, 'lead_from_pac': 8}:
        raise ValueError('Exp3 must retain its matched A/B/C PHY variants and PAC8')
    if m['exp5'] != {'preamble': 1024, 'pac': 32, 'lead_from_pac': 8}:
        raise ValueError('Exp5 must retain M1024/PAC32; lead refers to frozen PAC8 Stage0 margin')
    e = m['exp4']
    if type(e['sensors']) is not int or not 1 <= e['sensors'] <= 6:
        raise ValueError('physical sensor count must be 1..6')
    if not e['slotted_rx'] or not e['spi_opt']:
        raise ValueError('this campaign requires the validated slotted RX and SPI path')
    if not (0 <= e['guard_us'] <= 1000 and 0 < e['cycles'] <= 10000 and 0 < e['sync_buffer_us'] < 10000 and 0 < e['sync_prep_us'] < 10000):
        raise ValueError('invalid Exp4 timing/cycles')
    search = e.get('capacity_search')
    if search is not None and search != {'mode':'baseline_then_descending', 'minimum_slots':e['sensors'],
                                        'invalid_run_action':'stop', 'goal_percent_strictly_below':1.0}:
        raise ValueError('capacity search must retain all physical TX, stop on invalid capture, and require per-node PER<1%')
    for plen in e['preambles']:
        maximum = max_slots(e, plen)
        counts = e['slot_counts_by_preamble'][str(plen)]
        if len(set(counts)) != len(counts) or not counts or any(type(n) is not int or not e['sensors'] <= n <= maximum for n in counts):
            raise ValueError(f'M{plen} slot count must include each physical TX and fit timing maximum {maximum}')
        if search and not {e['sensors'],maximum}.issubset(counts):
            raise ValueError('capacity baseline and timing maximum must be present in the comparison plan')
        for count in (capacity_counts(e,plen) if search else counts):
            owners = e.get('sequences_by_preamble_slotcount', {}).get(f'{plen}:{count}')
            if owners is not None and (len(owners) != count or set(owners) != set(str(i) for i in range(2, e['sensors'] + 2))):
                raise ValueError('custom sequence must cover the exact configured TX set and slot count')
    from brrs_suite_paper import validate
    validate(m)
    return m

def max_slots(e, plen):
    # Same integer formula as EXP4_TIMING_MAX_DATA_SLOTS in brrs_init.c.
    budget = 10000 - e['sync_buffer_us'] - e['sync_prep_us']
    if budget <= 55 + e['guard_us']:
        raise ValueError('Exp4 DATA budget cannot fit a guarded slot')
    return min(32, 1 + (budget - 55 - e['guard_us']) // (AIRTIME_US[plen] + e['guard_us']))

def cir_links(m):
    # Missing field keeps historical manifests and case IDs unchanged.
    return m.get('cir_link_tx_roles',[m['single_link_tx_role']])

def serial_for(m, stage, role, physical_tx_role=None):
    if stage == 'exp4':
        if physical_tx_role is not None:
            raise ValueError('physical TX selector is for Exp2/Exp5 TX only')
        physical = role
        if physical not in ROLES[:m['exp4']['sensors'] + 1]:
            raise ValueError('role outside the configured Exp4 sensor set')
        if role=='N2' and m['exp4']['sensors']==1:
            physical=m['single_link_tx_role']
    else:
        if role not in ['rx', 'tx']:
            raise ValueError('single-link role must be rx or tx')
        physical = 'init' if role == 'rx' else m['single_link_tx_role']
        if role=='tx' and stage in ['exp2','exp5']:
            links=cir_links(m)
            if physical_tx_role is None and len(links)!=1:
                raise ValueError('Exp2/Exp5 has multiple links; select --physical-tx-role or use the campaign plan')
            physical=physical_tx_role or links[0]
            if physical not in links:raise ValueError('physical TX is not in cir_link_tx_roles')
        elif physical_tx_role is not None:
            raise ValueError('physical TX selector is for Exp2/Exp5 TX only')
    return m['boards'][physical]['serial']

def fixed_assignments(m, sensors, actual):
    roles = ROLES[1:sensors + 1]
    assigned = [(r, m['boards'][m['single_link_tx_role'] if sensors==1 else r]['serial']) for r in roles]
    if len(actual) != len(set(map(str, actual))) or set(map(str, actual)) != {s for _, s in assigned}:
        raise ValueError('connected TX probes do not match the explicitly selected fixed roles')
    return assigned

def selected_lead(m, pac):
    s = m['lead_selection']
    value = s['lead_us_by_pac'].get(str(pac))
    if s['frozen'] is not True or not s.get('evidence') or type(value) is not int or not 0 <= value <= 40:
        raise ValueError(f'PAC{pac} lead is not selected/frozen; complete Stage0 and record evidence first')
    return value

def capacity_counts(e, plen):
    """One shared baseline, then every larger legal load from high to low."""
    if 'capacity_search' not in e:
        raise ValueError('manifest does not enable capacity search')
    return [e['sensors'], *range(max_slots(e,plen),e['sensors'],-1)]

def plan(m, stage, *, capacity_candidates=False, profile='preparation', confirmation=False):
    if profile == 'paper':
        from brrs_suite_paper import plan as paper_plan
        return paper_plan(m,stage,capacity_candidates,confirmation)
    if profile != 'preparation' or confirmation:
        raise ValueError('confirmation requires paper profile')
    if capacity_candidates and stage != 'exp4':
        raise ValueError('capacity candidates are only supported for Exp4')
    cases = []
    canonical_manifest_sha = hashlib.sha256(json.dumps(m,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    def case(plen, pac, lead, variant=None, count=None, link_role=None):
        e = m['exp4']; cycles = 2000 if stage in ['stage0','exp1'] else e['cycles'] if stage == 'exp4' else 1000
        owners = ''.join(str(2 + i % e['sensors']) for i in range(count)) if stage == 'exp4' else '2'
        if stage == 'exp4':
            owners = e.get('sequences_by_preamble_slotcount', {}).get(f'{plen}:{count}', owners)
        conditions = {'stage': stage, 'preamble': plen, 'rx_pac': pac, 'lead_us': lead, 'tail_us': 0, 'cycles': cycles,
                      'variant': variant, 'slot_owners': owners, 'run': 1}
        if link_role is not None and 'cir_link_tx_roles' in m:
            conditions.update(link_tx_role=link_role,link_mode='sequential_single_tx')
        if stage == 'exp4':
            if lead > e['guard_us']:
                raise ValueError('Exp4 lead exceeds guard')
            conditions.update({k: e[k] for k in ['sensors','guard_us','sync_buffer_us','sync_prep_us','slotted_rx','spi_opt']})
            conditions['rx_window_us'] = AIRTIME_US[plen] + lead
            conditions['fwto_uus'] = (conditions['rx_window_us'] * 10000 + 10255) // 10256
            conditions['max_slots'] = max_slots(e, plen)
        digest = hashlib.sha256(json.dumps(conditions, sort_keys=True).encode()).hexdigest()
        cid = f'{stage}_m{plen}_pac{pac}_l{lead}' + (f'_{variant}' if variant else '') + (f'_k{count}' if count else '')
        if 'link_tx_role' in conditions:cid += '_tx'+link_role
        roles = ROLES[:e['sensors'] + 1] if stage == 'exp4' else ['rx','tx']
        jobs = []
        for role in roles:
            physical = role if stage == 'exp4' else 'init' if role == 'rx' else (link_role or m['single_link_tx_role'])
            if stage=='exp4' and e['sensors']==1 and role=='N2':
                physical=m['single_link_tx_role']
            board = m['boards'][physical]
            if stage == 'stage0':
                script = 'brrs_stage0_capture.sh'; args = [role, str(lead), '1', m['environment'], '--pac', str(pac)]
            elif stage in ['exp1', 'exp2']:
                script = f'brrs_{stage}_capture.sh'; args = [role, str(plen), '1', m['environment'], '--lead', str(lead), '--pac', str(pac)]
            elif stage == 'exp3':
                script = 'brrs_exp3_capture.sh'; args = [role, variant, '1', m['environment'], '--lead', str(lead)]
            elif stage == 'exp5':
                script = 'brrs_exp5_capture.sh'; args = [role, '1', m['environment'], '--lead', str(lead)]
            else:
                script = 'brrs_exp4_capture.sh'; args = [role, str(plen), str(e['sensors']), '1', m['environment'],
                    '--lead', str(lead), '--pac', str(pac), '--guard', str(e['guard_us']), '--sync-buffer', str(e['sync_buffer_us']),
                    '--sync-prep', str(e['sync_prep_us']), '--cycles', str(cycles), '--sequence', owners, '--slotted-rx', '--spi-opt', '--max-per-percent', '100']
            args += ['--serial', board['serial']]
            if m.get('distance_m') is not None:
                args.insert(args.index(m['environment']) + 1, str(m['distance_m']))
            logical = int(role[1:]) if stage == 'exp4' and role != 'init' else 1 if physical == 'init' else 2
            jobs.append({'physical_role': physical, 'logical_node': logical, 'serial': board['serial'], 'host': board['host'],
                         'location': board['location'], 'argv': ['bash', str(API / script)] + args,
                         'build_only_argv': ['bash', str(API / script)] + args + ['--build-only'],
                         'environment': {'BRRS_SUITE_MANIFEST_SHA256':canonical_manifest_sha,'BRRS_SUITE_CONDITIONS_SHA256':digest,
                                         'BRRS_SUITE_CASE_ID':cid,'BRRS_SUITE_PHYSICAL_ROLE':physical,'BRRS_SUITE_LOGICAL_NODE':str(logical)},
                         'source_script_sha256': hashlib.sha256((API / script).read_bytes()).hexdigest()})
        cases.append({'id': cid, 'conditions': conditions, 'conditions_sha256': digest, 'jobs': jobs,
                      'per_node_goal_strictly_below': 1.0,
                      'inactive_tx_roles': [r for r in ROLES[1:] if r not in [j['physical_role'] for j in jobs]],
                      'requires_supervisor': ['quiesce_inactive_tx','tx_ready_before_rx','verify_image_and_tool_hashes','per_node_goal_verification']})
    if stage == 'stage0':
        for pac in m['pacs']:
            for lead in m['stage0']['leads_us']: case(32,pac,lead)
    elif stage in ['exp1','exp2']:
        for pac in m['pacs']:
            lead = selected_lead(m,pac)
            for plen in m[stage]['preambles']:
                for link_role in (cir_links(m) if stage=='exp2' else [None]):
                    case(plen,pac,lead,link_role=link_role)
    elif stage == 'exp3':
        for variant in m['exp3']['variants']: case(32,8,selected_lead(m,8),variant)
    elif stage == 'exp4':
        for pac in m['pacs']:
            for plen in m['exp4']['preambles']:
                counts = (capacity_counts(m['exp4'],plen) if capacity_candidates
                          else m['exp4']['slot_counts_by_preamble'][str(plen)])
                for count in counts: case(plen,pac,selected_lead(m,pac),count=count)
    elif stage == 'exp5':
        for link_role in cir_links(m):case(1024,32,selected_lead(m,8),link_role=link_role)
    else: raise ValueError('unknown stage')
    return cases

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action',choices=['check','serial','plan'])
    ap.add_argument('manifest',type=Path)
    ap.add_argument('--stage',choices=['stage0','exp1','exp2','exp3','exp4','exp5'])
    ap.add_argument('--role')
    ap.add_argument('--physical-tx-role',choices=ROLES[1:],help='explicit Exp2/Exp5 TX for serial lookup')
    ap.add_argument('--sensors',type=int,choices=range(1,7),help='serial lookup: selected Exp4 physical TX count')
    ap.add_argument('--profile',choices=['preparation','paper'],default='preparation')
    ap.add_argument('--confirmation',action='store_true',help='Stage0 candidate and adjacent-lead repetitions')
    ap.add_argument('--capacity-candidates', action='store_true',
                    help='Exp4 only: include conditional descending-load candidates; does not execute them')
    a=ap.parse_args()
    try:
        m=load(a.manifest)
        if a.action=='serial':
            if not a.stage or not a.role: raise ValueError('serial requires stage and role')
            if a.sensors is not None:
                if a.stage!='exp4':raise ValueError('sensor override is Exp4 only')
                m['exp4']['sensors']=a.sensors
            print(serial_for(m,a.stage,a.role,a.physical_tx_role)); return
        if a.action=='plan':
            if not a.stage: raise ValueError('plan requires stage')
            print(json.dumps({'manifest':str(a.manifest.resolve()),'manifest_file_sha256':hashlib.sha256(a.manifest.read_bytes()).hexdigest(),
                              'canonical_manifest_sha256':hashlib.sha256(json.dumps(m,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
                              'rf_execution_performed':False,'capacity_candidates':a.capacity_candidates,
                              'profile':a.profile,'cases':plan(m,a.stage,capacity_candidates=a.capacity_candidates,profile=a.profile,confirmation=a.confirmation)},indent=2)); return
        print(json.dumps({'valid':True,'single_link_tx':serial_for(m,'exp1','tx'),'cir_link_tx_roles':cir_links(m),'exp4_max_slots':{str(n):max_slots(m['exp4'],n) for n in m['exp4']['preambles']},'lead_selection_frozen':m['lead_selection']['frozen']},indent=2))
    except (KeyError,TypeError,ValueError,OSError) as e: ap.error(str(e))

if __name__=='__main__': main()
