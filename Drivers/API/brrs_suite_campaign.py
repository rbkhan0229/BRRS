#!/usr/bin/env python3
"""Prepare/deploy/resume explicit experiment cases. Preparation never runs RF."""
import argparse
from datetime import datetime,timezone
import io
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tarfile
from types import SimpleNamespace

from brrs_suite_manifest import load,plan
from brrs_suite_case import prepare,checked,sha,save
from brrs_suite_paper import digest
from brrs_suite_results import assess,aggregate,ERRORS

API=Path(__file__).resolve().parent

def make_campaign(a):
    m=load(a.manifest);cases=plan(m,a.stage,profile=a.profile,confirmation=a.confirmation,capacity_candidates=a.capacity_candidates)
    profile_case_ids=[c['id'] for c in cases]
    blocks=getattr(a,'blocks',None)
    if blocks:
        if len(set(blocks))!=len(blocks) or any(type(x) is not int or x<1 for x in blocks):
            raise ValueError('blocks must be unique positive integers')
        available={c['conditions']['run'] for c in cases}
        if not set(blocks).issubset(available):
            raise ValueError('requested block absent from stage/profile plan')
        wanted_blocks=set(blocks)
        cases=[c for c in cases if c['conditions']['run'] in wanted_blocks]
    if a.cases:
        wanted=set(a.cases)
        if not wanted.issubset({c['id'] for c in cases}):raise ValueError('requested case absent from plan')
        cases=[c for c in cases if c['id'] in wanted]
    if a.dry_run:
        print(json.dumps({'rf_execution_performed':False,'builds_performed':False,
            'profile':a.profile,'stage':a.stage,'blocks':blocks,
            'profile_case_count':len(profile_case_ids),'selected_case_count':len(cases),
            'profile_slice':len(cases)!=len(profile_case_ids),'case_ids':[c['id'] for c in cases]},indent=2));return
    root=a.root.resolve();root.mkdir(parents=True,exist_ok=True);index=root/'campaign.json'
    spec={'manifest':str(a.manifest.resolve()),'manifest_sha256':sha(a.manifest),'profile':a.profile,'stage':a.stage,
          'confirmation':a.confirmation,'capacity_candidates':a.capacity_candidates,'blocks':blocks,
          'profile_case_count':len(profile_case_ids),'profile_slice':len(cases)!=len(profile_case_ids),
          'case_ids':[c['id'] for c in cases]}
    if index.exists() and json.loads(index.read_text())['spec']!=spec:raise ValueError('campaign spec changed; use a new directory')
    state={'spec':spec,'bundles':{},'rf_execution_performed':False}
    for c in cases:
        bundle=root/c['id']
        if bundle.exists():
            old=checked(bundle)
            if old['manifest_file_sha256']!=spec['manifest_sha256'] or old['conditions']!=c['conditions']:raise ValueError('existing bundle does not match campaign')
        else:prepare(SimpleNamespace(manifest=a.manifest,stage=a.stage,case=c['id'],profile=a.profile,confirmation=a.confirmation,reuse=a.reuse,bundle=bundle))
        state['bundles'][c['id']]={'path':str(bundle),'payload_index_sha256':sha(bundle/'payload_hashes.json')}
        save(index,state)
    print(json.dumps({'campaign':str(index),'prepared_cases':len(state['bundles']),'rf_execution_performed':False},indent=2))

REMOTE_EXTRACT=r'''
import hashlib,io,json,pathlib,sys,tarfile
root=pathlib.Path(sys.argv[1]);expected=sys.argv[2]
if root.exists():
    index=root/'payload_hashes.json'
    if not index.exists() or hashlib.sha256(index.read_bytes()).hexdigest()!=expected:raise SystemExit('existing destination mismatch')
    for name,h in json.loads(index.read_text()).items():
        p=(root/name).resolve()
        if not p.is_relative_to(root.resolve()) or hashlib.sha256(p.read_bytes()).hexdigest()!=h:raise SystemExit('existing payload mismatch')
    print('EXISTING_IDENTICAL');sys.exit(0)
data=sys.stdin.buffer.read();archive=tarfile.open(fileobj=io.BytesIO(data),mode='r:gz')
members=archive.getmembers()
for member in members:
    name=pathlib.PurePosixPath(member.name)
    if not member.isfile() or name.is_absolute() or '..' in name.parts:raise SystemExit('invalid archive member')
root.mkdir(parents=True,exist_ok=False)
for member in members:
    p=root/member.name;p.parent.mkdir(parents=True,exist_ok=True)
    with p.open('xb') as f:f.write(archive.extractfile(member).read())
    p.chmod(member.mode & 0o777)
index=root/'payload_hashes.json'
if hashlib.sha256(index.read_bytes()).hexdigest()!=expected:raise SystemExit('deployed index mismatch')
for name,h in json.loads(index.read_text()).items():
    p=(root/name).resolve()
    if not p.is_relative_to(root.resolve()) or hashlib.sha256(p.read_bytes()).hexdigest()!=h:raise SystemExit('deployed payload mismatch')
print('DEPLOYED_VERIFIED')
'''

def transport_host(c,override=None):
    if override:
        if not re.fullmatch(r'[A-Za-z0-9._-]+',override):raise ValueError('invalid SSH host')
        return override
    hosts={b['host'] for b in c['boards'].values()}-{'local'}
    if len(hosts)!=1:raise ValueError('exactly one remote host required')
    return hosts.pop()

def deploy(bundle,host=None,dry_run=False):
    root=Path(bundle).resolve();c=checked(root)
    if {b['host'] for b in c['boards'].values()}=={'local'}:
        if host:raise ValueError('SSH override is not valid for an all-local campaign')
        return {'bundle':str(root),'host':None,'status':'LOCAL_PAYLOAD_VERIFIED',
                'payload_index_sha256':sha(root/'payload_hashes.json'),'network_access_performed':False}
    host=transport_host(c,host)
    command=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=10',host,shlex.join(['python3','-c',REMOTE_EXTRACT,str(root),sha(root/'payload_hashes.json')])]
    if dry_run:return {'bundle':str(root),'host':host,'payload_index_sha256':sha(root/'payload_hashes.json'),'network_access_performed':False}
    buf=io.BytesIO()
    with tarfile.open(fileobj=buf,mode='w:gz') as tar:
        for name in [*json.loads((root/'payload_hashes.json').read_text()),'payload_hashes.json']:tar.add(root/name,arcname=name,recursive=False)
    r=subprocess.run(command,input=buf.getvalue(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    return {'bundle':str(root),'host':host,'status':r.stdout.decode().strip(),'payload_index_sha256':sha(root/'payload_hashes.json')}

def run_campaign(a):
    root=a.root.resolve();campaign=json.loads((root/'campaign.json').read_text());spec=campaign['spec']
    if list(campaign['bundles'])!=spec['case_ids']:raise ValueError('campaign preparation incomplete')
    m=load(spec['manifest'])
    if sha(spec['manifest'])!=spec['manifest_sha256']:raise ValueError('source manifest changed; frozen campaign required')
    planned={c['id']:c for c in plan(m,spec['stage'],profile=spec['profile'],confirmation=spec['confirmation'],capacity_candidates=spec['capacity_candidates'])}
    actual_profile_slice=set(spec['case_ids'])!=set(planned)
    if spec.get('profile_slice',actual_profile_slice)!=actual_profile_slice:
        raise ValueError('campaign profile-slice declaration mismatch')
    if spec.get('profile_case_count',len(planned))!=len(planned):
        raise ValueError('campaign full profile count mismatch')
    profile_slice=actual_profile_slice
    observations={};actions=[]
    for cid,item in campaign['bundles'].items():
        bundle=Path(item['path']);c=checked(bundle)
        if c['id']!=cid or c['conditions']!=planned[cid]['conditions'] or sha(bundle/'payload_hashes.json')!=item['payload_index_sha256']:raise ValueError('campaign payload changed')
        if (bundle/'results').exists():
            observations[cid]=assess(bundle)  # failed/incomplete capture stops; no silent rerun
            actions.append({'case_id':cid,'action':'SKIP_COMPLETED','verdict':observations[cid]['verdict']})
            continue
        actions.append({'case_id':cid,'action':'DEPLOY_THEN_RUN'})
        if a.dry_run:continue
        deployed=deploy(bundle,a.host);save(root/(cid+'.deployment.json'),deployed)
        command=[sys.executable,str(bundle/'sdk/Drivers/API/brrs_suite_case.py'),'run','--bundle',str(bundle)]
        if deployed['host']:command+=['--host',deployed['host']]
        r=subprocess.run(command)
        if r.returncode:raise RuntimeError('case control/collection failed; preserved without retry: '+cid)
        observations[cid]=assess(bundle)
        save(root/'progress.json',{'completed':list(observations),'last_case':cid,'last_verdict':observations[cid]['verdict']})
    groups=aggregate([planned[x] for x in spec['case_ids']],observations)
    for group in groups:
        group['selected_slice_runs_complete']=group['planned_runs_complete']
        if profile_slice:
            group['profile_runs_complete']=False
            group['full_repetitions_complete']=False
            group['paper_repetitions_complete']=False
    report={'actions':actions,'groups':groups,'profile':spec['profile'],'stage':spec['stage'],
            'blocks':spec.get('blocks'),'profile_slice':profile_slice,
            'selected_case_count':len(spec['case_ids']),'profile_case_count':spec.get('profile_case_count',len(planned)),
            'selected_slice_complete':bool(groups) and all(g['status'] in ['PASS','FAIL_PER'] for g in groups),
            'full_stage_profile_complete':not profile_slice and spec['profile'] in ['full','paper'] and bool(groups) and all(g['status']=='PASS' for g in groups),
            'rf_execution_performed':not a.dry_run and any(x['action']=='DEPLOY_THEN_RUN' for x in actions)}
    if not a.dry_run:save(root/'RESULTS.json',report)
    print(json.dumps(report,indent=2))

def main():
    ap=argparse.ArgumentParser(description=__doc__);sub=ap.add_subparsers(dest='command',required=True)
    p=sub.add_parser('prepare');p.add_argument('--manifest',type=Path,required=True);p.add_argument('--stage',required=True)
    p.add_argument('--profile',choices=['preparation','full','essential','lite','paper'],default='essential');p.add_argument('--confirmation',action='store_true');p.add_argument('--capacity-candidates',action='store_true')
    p.add_argument('--blocks',nargs='+',type=int,help='prepare only these predeclared repetition blocks; this is an execution slice, not a new profile')
    p.add_argument('--cases',nargs='+');p.add_argument('--root',type=Path,required=True);p.add_argument('--reuse',action='store_true');p.add_argument('--dry-run',action='store_true')
    p=sub.add_parser('deploy');p.add_argument('--bundle',type=Path,required=True);p.add_argument('--host');p.add_argument('--dry-run',action='store_true')
    p=sub.add_parser('run');p.add_argument('--root',type=Path,required=True);p.add_argument('--host');p.add_argument('--dry-run',action='store_true')
    a=ap.parse_args()
    if a.command=='prepare':make_campaign(a)
    elif a.command=='deploy':print(json.dumps(deploy(a.bundle,a.host,a.dry_run),indent=2))
    else:run_campaign(a)

if __name__=='__main__':
    try:main()
    except (*ERRORS,RuntimeError,subprocess.SubprocessError) as exc:raise SystemExit(f'ERROR: {exc}')
