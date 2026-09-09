#!/usr/bin/env python3
"""Offline Stage0..Exp5 assessment and paper aggregation by physical serial."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import statistics
from types import SimpleNamespace

from brrs_suite_case import checked,sha
from brrs_suite_manifest import load,plan
from brrs_suite_paper import digest
from brrs_suite_evidence import read_evidence
from brrs_exp1_verify import validate_rx,validate_tx,parse_kv_marker
from brrs_wilson_ci import wilson_interval
from brrs_exp4_verify import VerificationError

ERRORS=(KeyError,IndexError,TypeError,OSError,ValueError,VerificationError)
_plans={}

def context_hash(m):
    return digest({k:v for k,v in m.items() if k not in ['lead_selection','lead_candidates_us_by_pac']})

def marker(lines,prefix):
    rows=[l for l in lines if l.startswith(prefix)]
    if len(rows)!=1: raise ValueError('missing/duplicate '+prefix)
    return parse_kv_marker(rows[0],prefix)

def require(values,**expected):
    for k,v in expected.items():
        if values.get(k)!=str(v): raise ValueError(f'{k} mismatch: {values.get(k)} != {v}')

def rows(lines,prefix): return [l.split(',') for l in lines if l.startswith(prefix)]

def validate_cir(rx,tx,p):
    n=p['cycles'];m=p['preamble'];stage=p['stage']
    require(marker(rx,'EXP_LOG_CONFIG_CSV,'),experiment=2 if stage=='exp2' else 5,plen=m,lead_us=p['lead_us'],tail_us=0,target=n,cir=1)
    if stage=='exp2':require(marker(rx,'EXP2_PHY_CONFIG_CSV,'),plen=m,pac=p['rx_pac'],sfd_timeout=m+9-p['rx_pac'],lead_us=p['lead_us'])
    rv=marker(rx,'EXP2_DONE,');tv=marker(tx,'EXP2_TX_DONE,');received=int(rv['rx']);sent=int(tv['success'])
    if not 0<received<=sent<=n or int(tv['attempts'])!=sent: raise ValueError('CIR TX/RX counts inconsistent')
    require(rv,plen=m,expected=n,valid_cir=received,dump_count=received,end_tx=3,collection='PASS',status='PASS',
        per_x1000=((n-received)*100000+n//2)//n,link='PASS' if received==n else 'LOSS')
    require(tv,plen=m,expected=n,delayed_late=0,beacon_config_errors=0,data_config_errors=0,end=1,collection='PASS',status='PASS',link='PASS' if sent==n else 'LOSS')
    beacons=[l for l in tx if l.startswith('BRRS_BEACON_RX_CSV,')]
    if not beacons or f',m={m},' not in beacons[-1]:raise ValueError('CIR TX beacon PHY mismatch')
    cir=rows(rx,'CIR_CSV,')
    if len(cir)!=received or any(len(r)!=15 or r[3]!='N2' or int(r[4])!=m for r in cir):raise ValueError('CIR row shape/count mismatch')
    if {int(r[1]) for r in cir}!=set(range(1,received+1)) or len({r[2] for r in cir})!=received or any(not 1<=int(r[2])<=n for r in cir):
        raise ValueError('CIR frame/cycle identity mismatch')
    extra={'cir_rows':len(cir),'cir_scope':'successful receptions only; report PER alongside CIR'}
    if stage=='exp5':
        expected=min(received,30);done=marker(rx,'CIR_RAW_DUMP_DONE,');require(done,plen=m,count=expected,samples_per_frame=300)
        headers=[parse_kv_marker(l,'CIR_RAW_HEADER,') for l in rx if l.startswith('CIR_RAW_HEADER,')]
        if len(headers)!=expected or len({h['frame'] for h in headers})!=expected:raise ValueError('raw CIR headers missing/duplicate')
        samples={h['frame']:{} for h in headers}
        for h in headers:require(h,plen=m,n_samples=300)
        for r in rows(rx,'CIR_RAW,'):
            if len(r)!=5 or r[1] not in samples or not 0<=int(r[2])<300 or int(r[2]) in samples[r[1]]:raise ValueError('invalid raw CIR sample')
            samples[r[1]][int(r[2])]=(int(r[3]),int(r[4]))
        if any(set(v)!=set(range(300)) for v in samples.values()):raise ValueError('incomplete raw CIR frame')
        extra.update(raw_cir_frames=expected,raw_samples_per_frame=300)
    return received,sent,extra

def validate_exttxe(rx,tx,p):
    n=p['cycles'];v=p['variant'];sfd=16 if v=='B' else 8;phr='DTA' if v=='C' else 'STD'
    require(marker(rx,'EXP_LOG_CONFIG_CSV,'),experiment=3,plen=32,lead_us=p['lead_us'],tail_us=0,target=n,cir=0)
    rv=marker(rx,'EXP3_RX_DONE,');tv=marker(tx,'EXP3_TX_RESULT,');dump=marker(tx,'EXP3_TX_DUMP_DONE,')
    require(tv,variant=v,attempts=n,success=n,captures=n,end=1,status='PASS')
    require(dump,variant=v,expected=n,count=n,status='PASS')
    received=int(rv['rx']);require(rv,variant=v,expected=n,end_tx=3,status='PASS',per_x1000=((n-received)*100000+n//2)//n)
    if not 0<received<=n:raise ValueError('invalid Exp3 RX count')
    rr=rows(rx,'EXP3_RX_RESULT_CSV,')
    if len(rr)!=1 or len(rr[0])!=11 or rr[0][1:6]!=[v,str(sfd),phr,'26',str(n)] or list(map(int,rr[0][6:9]))!=[received,n-received,int(rv['per_x1000'])] or rr[0][-1]!='PASS':
        raise ValueError('Exp3 RX result mismatch')
    data=rows(tx,'EXP3_TX_CSV,')
    if len(data)!=n or {int(r[1]) for r in data}!=set(range(1,n+1)) or any(len(r)!=8 or r[2:6]!=[v,str(sfd),phr,'26'] for r in data):
        raise ValueError('Exp3 EXTTXE rows missing/duplicate/wrong PHY')
    measured=[int(r[7]) for r in data]
    if any(x<=0 for x in measured):raise ValueError('nonpositive EXTTXE measurement')
    if any(int(r[7])!=(int(r[6])*125+1)//2 for r in data):raise ValueError('EXTTXE tick/nanosecond mismatch')
    return received,n,{'exttxe_captures':n,'duration_mean_ns':statistics.mean(measured),'duration_min_ns':min(measured),'duration_max_ns':max(measured)}

def assess(root):
    root=Path(root).resolve();c=checked(root);p=c['conditions'];m=load(root/'board_manifest.json')
    key=(digest(m),p['stage'],p.get('profile','preparation'),p.get('phase')=='confirmation')
    if key not in _plans:
        _plans[key]={x['id']:x for x in plan(m,p['stage'],profile=key[2],capacity_candidates=p['stage']=='exp4' and 'capacity_search' in m['exp4'],confirmation=key[3])}
    expected=_plans[key].get(c['id'])
    if expected is None:raise ValueError('case absent from its immutable manifest')
    if p['stage']=='exp4':
        from brrs_exp4_capacity import assess_bundle
        result=assess_bundle(root,expected)
    else:
        c,raw,orchestration=read_evidence(root,expected)
        txjob=next(j for j in c['jobs'] if j['logical_node']!=1)
        rx=raw['init'];tx=raw[txjob['physical_role']];extra={}
        if p['stage'] in ['stage0','exp1']:
            args=SimpleNamespace(preamble=p['preamble'],lead=p['lead_us'],tail=p['tail_us'],pac=p['rx_pac'],rx_mode='delayed',expected=p['cycles'])
            validate_rx('\n'.join(rx),args);validate_tx('\n'.join(tx),args)
            received=int(marker(rx,'EXP1_DONE,')['rx']);sent=int(marker(tx,'EXP1_TX_DONE,')['success'])
            if not 0<=received<=sent<=p['cycles']:raise ValueError('RX/TX mismatch')
            if received==0 and p['stage']!='stage0':raise ValueError('zero valid packets')
        elif p['stage'] in ['exp2','exp5']:
            received,sent,extra=validate_cir(rx,tx,p)
            rxjob=next(j for j in c['jobs'] if j['logical_node']==1)
            extra['physical_link']={'tx_role':txjob['physical_role'],'tx_serial':txjob['serial'],
                'tx_location':txjob['location'],'rx_serial':rxjob['serial'],'rx_location':rxjob['location'],
                'raw_node_id':'N2','mode':'single_active_tx'}
        else:received,sent,extra=validate_exttxe(rx,tx,p)
        offered=p['cycles'];per=100*(offered-received)/offered
        result={'case_id':c['id'],'conditions':p,'condition_id':c.get('condition_id',c['id']),'bundle':str(root),
            'verdict':'PASS' if (offered-received)*100<offered else 'FAIL_PER','worst_node_per_percent':per,
            'measurement_status':'ZERO_RX_CALIBRATION_ONLY' if received==0 else 'VALID',
            'nodes_by_serial':{txjob['serial']:{'physical_role':txjob['physical_role'],'logical_node':2,'location':txjob['location'],
                'offered':offered,'tx_attempts':sent,'tx_success':sent,'rx':received,'per_percent':per}},'stage_metrics':extra,
            'diagnostic_lines':{'rx':[l for l in rx if l.startswith(('RX timeouts=','TDMA validation:'))],
                'tx':[l for l in tx if l.startswith(('SYNC loss:','My TX:'))]}}
    result.update(context_sha256=context_hash(m),payload_index_sha256=sha(root/'payload_hashes.json'),
        firmware_source_sha256=c.get('firmware_source_sha256',{}),
        log_sha256_by_serial={j['serial']:sha(root/'results'/('local' if j['host']=='local' else 'remote')/(j['physical_role']+'.log')) for j in c['jobs']})
    for row in result['nodes_by_serial'].values():
        row['per_wilson95_percent']=[100*x for x in wilson_interval(row['offered']-row['rx'],row['offered'],.95)]
    return result

def aggregate(cases,observations):
    """Completeness and every-run PER remain visible beside pooled estimates."""
    groups=defaultdict(list)
    for c in cases:groups[c.get('condition_id',c['id'])].append(c)
    output=[]
    for cid,expected in groups.items():
        present=[observations[c['id']] for c in expected if c['id'] in observations]
        missing=[c['id'] for c in expected if c['id'] not in observations]
        invalid=[r['case_id'] for r in present if r['verdict'] not in ['PASS','FAIL_PER']]
        valid=[r for r in present if r['verdict'] in ['PASS','FAIL_PER']]
        pooled={}
        for r in valid:
            for serial,row in r['nodes_by_serial'].items():
                s=pooled.setdefault(serial,{'physical_role':row['physical_role'],'location':row['location'],'offered':0,'rx':0,'tx_success':0,'runs':[]})
                for k in ['offered','rx','tx_success']:s[k]+=row[k]
                s['runs'].append({'case_id':r['case_id'],'logical_node':row['logical_node'],'per_percent':row['per_percent']})
        for s in pooled.values():
            s['per_percent']=100*(s['offered']-s['rx'])/s['offered']
            s['per_wilson95_percent']=[100*x for x in wilson_interval(s['offered']-s['rx'],s['offered'],.95)]
        signatures={digest(r.get('firmware_source_sha256',{})) for r in valid}
        if len(signatures)>1:invalid.append('MIXED_FIRMWARE_SOURCES')
        status='INVALID' if invalid else 'INCOMPLETE' if missing else 'FAIL_PER' if any(r['verdict']=='FAIL_PER' for r in valid) else 'PASS'
        output.append({'condition_id':cid,'status':status,'expected_runs':len(expected),'valid_runs':len(valid),'missing_case_ids':missing,
            'invalid_case_ids':invalid,'failed_per_case_ids':[r['case_id'] for r in valid if r['verdict']=='FAIL_PER'],
            'nodes_by_serial':pooled,'planned_runs_complete':not missing and not invalid,
            'paper_repetitions_complete':all(c['conditions'].get('profile')=='paper' for c in expected) and not missing and not invalid,
            'confidence_interval_scope':'packet-level Wilson; correlated/burst losses can reduce effective sample size; per-run results retained'})
    return output

def collect(m,cases,bundles,exclusions=None):
    expected={c['id']:c for c in cases};observations={};rejected=[];excluded=[]
    exclusions={str(Path(k).resolve()):v for k,v in (exclusions or {}).items()}
    for path in bundles:
        path=Path(path).resolve()
        if str(path) in exclusions:
            ex=exclusions[str(path)]
            if not ex.get('reason') or ex.get('payload_index_sha256')!=sha(path/'payload_hashes.json'):
                rejected.append({'bundle':str(path),'error':'invalid exclusion evidence'});continue
            excluded.append({'bundle':str(path),**ex});continue
        try:
            c=checked(path);cid=c['id']
            if cid not in expected:raise ValueError('case is outside selected campaign')
            if cid in observations:raise ValueError('duplicate case; explicitly exclude contaminated runs, never select best PER')
            try:
                r=assess(path)
                if r['context_sha256']!=context_hash(m) or r['conditions']!=expected[cid]['conditions']:raise ValueError('mixed campaign context/conditions')
                observations[cid]=r
            except ERRORS as exc:
                observations[cid]={'case_id':cid,'verdict':'INVALID','error':str(exc),'bundle':str(path)}
        except ERRORS as exc:rejected.append({'bundle':str(path),'error':str(exc)})
    groups=aggregate(cases,observations)
    status=('INVALID' if rejected or any(g['status']=='INVALID' for g in groups) else
            'INCOMPLETE' if any(g['status']=='INCOMPLETE' for g in groups) else
            'FAIL_PER' if any(g['status']=='FAIL_PER' for g in groups) else 'PASS')
    return {'observations':observations,'rejected':rejected,'excluded':excluded,'groups':groups,
            'campaign_status':status,'paper_protocol_pass':bool(cases) and status=='PASS' and all(c['conditions'].get('profile')=='paper' for c in cases),
            'rf_execution_performed':False}

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('manifest',type=Path);ap.add_argument('--stage',required=True,choices=['stage0','exp1','exp2','exp3','exp4','exp5'])
    ap.add_argument('--profile',default='paper',choices=['preparation','paper']);ap.add_argument('--confirmation',action='store_true')
    ap.add_argument('--capacity-candidates',action='store_true');ap.add_argument('--bundles',nargs='*',type=Path,default=[]);ap.add_argument('--exclusions',type=Path)
    a=ap.parse_args();m=load(a.manifest);cases=plan(m,a.stage,profile=a.profile,confirmation=a.confirmation,capacity_candidates=a.capacity_candidates)
    r=collect(m,cases,a.bundles,json.loads(a.exclusions.read_text()) if a.exclusions else None)
    print(json.dumps(r,indent=2));return 2 if r['rejected'] or any(g['status']=='INVALID' for g in r['groups']) else 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except ERRORS as exc:raise SystemExit(f'ERROR: {exc}')
