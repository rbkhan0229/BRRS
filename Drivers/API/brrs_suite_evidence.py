#!/usr/bin/env python3
"""Read and cross-check immutable case collection evidence, without hardware."""
import json
from pathlib import Path
from brrs_suite_case import checked, sha
from brrs_suite_paper import is_publication_profile


def capture_lines(text,stage,is_rx):
    ready='CIR_RTT_READY,channel=1' if is_rx and stage in ['exp2','exp5'] else 'EXP_LOG_READY,channel=1'
    end=('===== END STATS =====' if stage=='exp4' else
         ('EXP1_DONE,' if is_rx else 'EXP1_TX_DONE,') if stage in ['stage0','exp1'] else
         ('EXP2_DONE,' if is_rx else 'EXP2_TX_DONE,') if stage in ['exp2','exp5'] else
         ('EXP3_RX_DONE,' if is_rx else 'EXP3_TX_DUMP_DONE,'))
    lines=text.splitlines()
    for prefix in [ready,end]:
        if sum(l.startswith(prefix) for l in lines)!=1: raise ValueError('missing/duplicate '+prefix)
    return lines


def read_evidence(root,expected_case):
    root=Path(root).resolve(); c=checked(root); p=c['conditions']
    if c['id']!=expected_case['id'] or c['conditions_sha256']!=expected_case['conditions_sha256'] or p!=expected_case['conditions']:
        raise ValueError('bundle belongs to a different experiment condition')
    bindings=lambda x: [(j['physical_role'],j['logical_node'],j['serial'],j['host'],j['location']) for j in x['jobs']]
    if bindings(c)!=bindings(expected_case): raise ValueError('physical/logical board assignments differ')
    if c['inactive_tx_roles']!=expected_case['inactive_tx_roles']:raise ValueError('inactive board plan mismatch')
    out=root/'results'; orchestration=json.loads((out/'orchestration.json').read_text())
    if orchestration['status']!='COLLECTION_AND_READBACK_PASS_PER_PENDING': raise ValueError('supervisor did not complete successfully')
    if orchestration['payload_index_sha256']!=sha(root/'payload_hashes.json'): raise ValueError('executed payload index mismatch')
    lines_by_role={}
    for job in c['jobs']:
        role=job['physical_role']; side='local' if job['host']=='local' else 'remote'
        state=json.loads((out/side/'status.json').read_text())
        if state['status']!='COLLECTION_METADATA_AND_FLASH_PASS': raise ValueError('side control/collection failed')
        if set(state['workers'])!={j['physical_role'] for j in c['jobs'] if (j['host']=='local')==(side=='local')}:
            raise ValueError('active worker set mismatch')
        worker=state['workers'][role]; readback=worker['readback']
        if worker['serial']!=job['serial'] or worker['exit_code']!=0 or readback['status']!='PASS' or readback['serial']!=job['serial'] or readback['hex_sha256']!=job['hex_sha256']:
            raise ValueError('worker or flash readback mismatch')
        raw=out/side/(role+'.log'); meta_path=out/side/(role+'.meta.txt')
        fields=[l.split('=',1) for l in meta_path.read_text().splitlines() if '=' in l]
        meta=dict(fields)
        if len(meta)!=len(fields):raise ValueError('duplicate metadata keys')
        expected={'serial':job['serial'],'physical_role':role,'logical_node':str(job['logical_node']),
                  'suite_case_id':c['id'],'suite_conditions_sha256':c['conditions_sha256'],
                  'suite_manifest_sha256':expected_case['jobs'][0]['environment']['BRRS_SUITE_MANIFEST_SHA256'],
                  'firmware_sha256':job['hex_sha256'],'raw_sha256':sha(raw),'collection_status':'PASS'}
        if is_publication_profile(p.get('profile')):
            expected.update(suite_profile=p['profile'],suite_block=str(p['run']),suite_rotation_index=str(p['rotation_index']),
                physical_location=job['location'],suite_assignment_sha256=expected_case['assignment_sha256'],run_number=str(p['run']))
        if is_publication_profile(p.get('profile')) or 'link_tx_role' in p:
            if state['case_id']!=c['id']: raise ValueError('side case identity mismatch')
            if job['logical_node']==1 and not orchestration['all_tx_ready_at']<=worker['started_at']:
                raise ValueError('RX started before all TX READY')
        if any(meta.get(k)!=v for k,v in expected.items()) or worker['raw_sha256']!=sha(raw): raise ValueError('raw/metadata hash or identity mismatch')
        lines_by_role[role]=capture_lines(raw.read_text(),p['stage'],job['logical_node']==1)
    for side in ['local','remote']:
        inactive={r for r in c['inactive_tx_roles'] if (c['boards'][r]['host']=='local')==(side=='local')}
        if not inactive:continue
        state=json.loads((out/side/'status.json').read_text())
        if set(state['inactive_halted_after'])!=inactive or not all(state['inactive_halted_after'].values()):
            raise ValueError('inactive-board control evidence incomplete')
    return c,lines_by_role,orchestration
