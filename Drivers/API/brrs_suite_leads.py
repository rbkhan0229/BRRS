#!/usr/bin/env python3
"""Stage0 grid -> repeated candidate -> confirmed PAC-specific lead manifest.

Lead response can be quantized or non-monotonic, so adjacent integer lead
values are characterization points rather than mandatory pass conditions.
"""
import argparse
import copy
import json
from pathlib import Path

from brrs_suite_manifest import load,plan
from brrs_suite_results import collect,ERRORS
from brrs_suite_case import sha
from brrs_suite_paper import digest,confirmation_leads

def candidates(m,report):
    if report['rejected'] or any(g['status'] in ['INVALID','INCOMPLETE'] for g in report['groups']):
        raise ValueError('complete valid PAC4/PAC8 Stage0 grid required; invalid/incomplete grid cannot select lead')
    selected={}
    for pac in m['pacs']:
        points={r['conditions']['lead_us']:r['worst_node_per_percent'] for r in report['observations'].values() if r['conditions']['rx_pac']==pac}
        if set(points)!=set(m['stage0']['leads_us']):raise ValueError('incomplete lead grid')
        eligible=[l for l in points if points[l]<1]
        if not eligible:raise ValueError(f'PAC{pac}: no measured lead with PER<1%; do not freeze')
        passed=set(eligible)
        def interior_radius(lead):
            radius=0
            while lead-radius-1 in passed and lead+radius+1 in passed:
                radius+=1
            return radius
        # Adjacency is a preference for the centre of an observed pass band,
        # not a gate: an isolated phase-aligned point remains eligible and is
        # then accepted or rejected by independent confirmation repetitions.
        selected[str(pac)]=min(eligible,key=lambda l:(-interior_radius(l),points[l],abs(l-20),l))
    return selected

def freeze(m,report):
    if report['rejected'] or any(g['status']!='PASS' for g in report['groups']):
        raise ValueError('every candidate confirmation must be complete and each run PER<1%')
    expected={(pac,lead) for pac in m['pacs'] for lead in confirmation_leads(m,pac)}
    observed={(r['conditions']['rx_pac'],r['conditions']['lead_us']) for r in report['observations'].values()}
    if observed!=expected:raise ValueError('confirmation condition set mismatch')
    # Point estimate is the operational goal; require an additional pooled
    # upper confidence bound for the candidate before freezing it.
    for g in report['groups']:
        if any(n['per_wilson95_percent'][1]>=1 for n in g['nodes_by_serial'].values()):
            raise ValueError('confirmation Wilson upper bound reaches 1%; no automatic freeze')
    return copy.deepcopy(m['lead_candidates_us_by_pac'])

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action',choices=['candidates','freeze']);ap.add_argument('manifest',type=Path)
    ap.add_argument('--bundles',nargs='+',type=Path,required=True);ap.add_argument('--output-manifest',type=Path,required=True)
    ap.add_argument('--exclusions',type=Path)
    a=ap.parse_args();m=load(a.manifest)
    c=plan(m,'stage0',profile='paper',confirmation=a.action=='freeze')
    report=collect(m,c,a.bundles,json.loads(a.exclusions.read_text()) if a.exclusions else None)
    result=copy.deepcopy(m)
    if a.action=='candidates':
        result['lead_candidates_us_by_pac']=candidates(m,report)
        result['lead_selection']={'frozen':False,'lead_us_by_pac':{'4':None,'8':None},'evidence':None}
    else:
        values=freeze(m,report)
        result['lead_selection']={'frozen':True,'lead_us_by_pac':values,'evidence':{'method':'complete 0..40 us grid; repeated selected-lead confirmations; every run PER<1%; pooled Wilson95 upper<1%; adjacent leads are sensitivity evidence, not pass requirements',
            'report_sha256':digest(report),'input_manifest_sha256':sha(a.manifest),
            'bundles':{str(p.resolve()):sha(p/'payload_hashes.json') for p in a.bundles}}}
    target=a.output_manifest.resolve();evidence=target.with_suffix('.stage0_evidence.json')
    if target.exists() or evidence.exists():raise ValueError('output exists; use a new manifest path')
    with evidence.open('x') as f:json.dump(report,f,indent=2)
    with target.open('x') as f:json.dump(result,f,indent=2)
    print(json.dumps({'manifest':str(target),'evidence':str(evidence),'selection':result['lead_selection'],
        'candidates':result['lead_candidates_us_by_pac'],'rf_execution_performed':False},indent=2))

if __name__=='__main__':
    try:raise SystemExit(main())
    except ERRORS as exc:raise SystemExit(f'ERROR: {exc}')
