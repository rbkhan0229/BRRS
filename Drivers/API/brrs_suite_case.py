#!/usr/bin/env python3
"""Prepare immutable images; run TX READY -> RX with explicit host/board roles.

Builds happen only in prepare. Runtime always calls the existing stage capture
and verifier with --no-build, after exact probe-set and payload checks.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time

API = Path(__file__).resolve().parent
NM = os.environ.get('ARM_NM') or shutil.which('arm-none-eabi-nm') or '/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin/nm'

def now(): return datetime.now(timezone.utc).isoformat()
def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path, value):
    path = Path(path)
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(value, indent=2) + '\n')
    tmp.replace(path)

def git_provenance(api):
    """Record the source commit without treating uncommitted edits as that commit."""
    def query(*args):
        return subprocess.run(['git','--no-optional-locks','-C',str(api),*args],
                              stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
    try:
        top=query('rev-parse','--show-toplevel')
    except FileNotFoundError:
        return {'available':False,'reason':'git_unavailable'},b''
    if top.returncode:
        return {'available':False,'reason':'source_is_not_a_git_worktree'},b''
    values={}
    for key,args in {'commit':('rev-parse','HEAD'),
                     'branch':('rev-parse','--abbrev-ref','HEAD'),
                     'status':('status','--porcelain=v1','--untracked-files=all')}.items():
        result=query(*args)
        if result.returncode:raise ValueError('cannot record Git provenance: '+key)
        values[key]=result.stdout.decode().strip()
    patch=query('diff','--binary','HEAD')
    if patch.returncode:raise ValueError('cannot record source Git diff')
    return {'available':True,'root':top.stdout.decode().strip(),**values,
            'dirty':bool(values['status']),'tracked_diff_sha256':hashlib.sha256(patch.stdout).hexdigest(),
            'commit_alone_describes_worktree':not bool(values['status'])},patch.stdout

def prepare(a):
    from brrs_suite_manifest import load, plan
    m = load(a.manifest)
    cases = [c for c in plan(m, a.stage, capacity_candidates=(a.stage == 'exp4' and 'capacity_search' in m['exp4']),
             profile=getattr(a,'profile','preparation'),confirmation=getattr(a,'confirmation',False)) if c['id'] == a.case]
    if len(cases) != 1: raise ValueError('case must match one manifest condition')
    root = a.bundle.resolve()
    root.mkdir(parents=True, exist_ok=False)
    c = cases[0]
    c.update(boards=m['boards'], prepared_at=now(), source_api=str(API),
             manifest_file_sha256=sha(a.manifest), deployment='portable capture-only SDK; build on source host')
    c['source_git'],source_patch=git_provenance(API)
    (root/'provenance').mkdir()
    save(root/'provenance/source_git.json',c['source_git'])
    if source_patch:(root/'provenance/source_git.patch').write_bytes(source_patch)
    runtime = root / 'sdk/Drivers/API'
    runtime.mkdir(parents=True)
    # All stage tools travel together; only this case's images are executable.
    for path in API.iterdir():
        if path.is_file() and path.suffix in ['.py', '.sh', '.json', '.md']:
            shutil.copy2(path, runtime / path.name)
    shutil.copy2(a.manifest, root / 'board_manifest.json')
    project = Path('Build_Platforms/nRF52840-DK/dw3000_api.emProject')
    (runtime / project).parent.mkdir(parents=True)
    shutil.copy2(API / project, runtime / project)
    sources = ['Src/examples/ex_35a_brrs_init/brrs_init.c', 'Src/examples/ex_35b_brrs_normal/brrs_normal.c']
    c['firmware_source_sha256'] = {p: sha(API / p) for p in sources}
    for p in sources:
        target = root / 'provenance' / p
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(API / p, target)
    for job in c['jobs']:
        env = dict(os.environ, **job['environment'], ARM_NM=NM, EMBUILD_THREADS=os.environ.get('EMBUILD_THREADS','8'))
        # Validate an exact cached image before rebuilding. Cached mismatch is
        # allowed to trigger a source build only without explicit --reuse.
        cmd = job['build_only_argv'] + ['--no-build']
        result = subprocess.run(cmd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        cache_output=result.stdout
        if result.returncode and not a.reuse:
            result = subprocess.run(job['build_only_argv'], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            result.stdout='[cache-check]\n'+cache_output+'\n[source-build]\n'+result.stdout
        (root / (job['physical_role'] + '.prepare.log')).write_text(result.stdout)
        if result.returncode: raise RuntimeError(f'build/resolve failed: {job["physical_role"]}')
        match = re.search(r'\[build-only\] verified image (.+); no board access', result.stdout)
        if not match: raise ValueError('image resolution marker missing')
        image = Path(match[1]); elf = image.with_suffix('.elf')
        if not image.is_file() or not elf.is_file(): raise ValueError('HEX/ELF missing')
        for file in [image, elf, *image.parent.glob('.brrs_*')]:
            target = runtime / file.relative_to(API)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)
        job['hex'] = str((runtime / image.relative_to(API)).relative_to(root))
        job['hex_sha256'] = sha(image)
        job['elf_sha256'] = sha(elf)
        job['rtt_address'] = re.search(r'control block @ (0x[0-9a-fA-F]+)', result.stdout)[1]
        job['script'] = Path(job['argv'][1]).name
        job['args'] = job['argv'][2:] + ['--no-build', '--timeout', '180']
    c['runtime_schema_version']=2
    save(root / 'case.json', c)
    hashes = {str(p.relative_to(root)): sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
    save(root / 'payload_hashes.json', hashes)
    print(json.dumps({'prepared':str(root), 'id':c['id'], 'files':len(hashes),
                      'payload_index_sha256':sha(root/'payload_hashes.json'), 'rf_performed':False}, indent=2))

def checked(root):
    hashes = json.loads((root / 'payload_hashes.json').read_text())
    for path, expected in hashes.items():
        p = (root / path).resolve()
        if not p.is_relative_to(root.resolve()) or sha(p) != expected:
            raise ValueError('payload mismatch: ' + path)
    c = json.loads((root / 'case.json').read_text())
    if len({j['serial'] for j in c['jobs']}) != len(c['jobs']): raise ValueError('duplicate active serial')
    if 'link_tx_role' in c['conditions']:
        p=c['conditions'];tx=[j for j in c['jobs'] if j['logical_node']!=1]
        rx=[j for j in c['jobs'] if j['logical_node']==1]
        if p['stage'] not in ['exp2','exp5'] or p.get('link_mode')!='sequential_single_tx':
            raise ValueError('invalid sequential CIR condition')
        if len(tx)!=1 or len(rx)!=1 or tx[0]['physical_role']!=p['link_tx_role'] or tx[0]['logical_node']!=2 or rx[0]['physical_role']!='init':
            raise ValueError('sequential CIR requires INIT and exactly the selected physical TX as logical N2')
        if set(c['inactive_tx_roles'])!=set(c['boards'])-{'init',p['link_tx_role']}:
            raise ValueError('sequential CIR inactive TX set mismatch')
    for j in c['jobs']:
        if j['serial'] != c['boards'][j['physical_role']]['serial']: raise ValueError('role/serial mismatch')
        if sha(root / j['hex']) != j['hex_sha256']: raise ValueError('image mismatch')
        if c['conditions']['stage']=='exp4' and c.get('runtime_schema_version',1)>=2:
            logical='init' if j['logical_node']==1 else f'N{j["logical_node"]}'
            if j['argv'][2]!=logical:raise ValueError('capture role does not match logical node')
            expected=f'exp4_{c["conditions"]["preamble"]}_s{c["conditions"]["sensors"]}_{logical}.hex'
            if Path(j['hex']).name!=expected:raise ValueError('HEX role/preamble/sensor mismatch')
    return c

def assigned(c, side):
    roles = [r for r,b in c['boards'].items() if (b['host'] == 'local') == (side == 'local')]
    jobs = [j for j in c['jobs'] if j['physical_role'] in roles]
    return roles, jobs

def probe_check(c, side):
    import pylink
    roles, jobs = assigned(c, side)
    actual = {str(e.SerialNumber) for e in pylink.JLink().connected_emulators()}
    expected = {c['boards'][r]['serial'] for r in roles}
    if actual != expected: raise RuntimeError(f'probe mismatch: actual={sorted(actual)}, expected={sorted(expected)}')
    return roles, jobs

def link(serial):
    import pylink
    jl = pylink.JLink()
    try:
        jl.open(serial_no=int(serial)); jl.set_tif(pylink.enums.JLinkInterfaces.SWD)
        jl.connect('NRF52840_XXAA', speed=4000)
        # SEGGER defaults to resuming the target on close. Keep explicit halt
        # effective after disconnect, including read-only verification closes.
        # https://kb.segger.com/J-Link_command_strings#SetRestartOnClose
        jl.exec_command('SetRestartOnClose = 0')
        return jl
    except BaseException:
        jl.close(); raise

def halt(c, roles):
    for role in roles:
        jl = link(c['boards'][role]['serial'])
        try:
            jl.halt()
            if not jl.halted(): raise RuntimeError('failed to halt ' + role)
        finally: jl.close()
    # Check after disconnect/reconnect, not only inside the halt connection.
    for role in roles:
        jl = link(c['boards'][role]['serial'])
        try:
            if not jl.halted(): raise RuntimeError('halt not retained across close: ' + role)
        finally: jl.close()

def hex_chunks(path):
    memory = {}; base = 0; eof = False
    for line in path.read_text().splitlines():
        raw = bytes.fromhex(line[1:])
        if not line.startswith(':') or len(raw) != raw[0]+5 or sum(raw)%256: raise ValueError('invalid HEX record')
        n, address, typ = raw[0], int.from_bytes(raw[1:3], 'big'), raw[3]
        data = raw[4:4+n]
        if typ == 0:
            for i,b in enumerate(data):
                addr = base + address + i
                if addr in memory and memory[addr] != b: raise ValueError('conflicting HEX bytes')
                memory[addr] = b
        elif typ == 4: base = int.from_bytes(data, 'big') << 16
        elif typ == 2: base = int.from_bytes(data, 'big') << 4
        elif typ == 1: eof = True
        elif typ not in [3,5]: raise ValueError('unsupported HEX record')
    if not eof or not memory: raise ValueError('empty/incomplete HEX')
    chunks = []; start = None; data = bytearray()
    for addr in sorted(memory):
        if start is not None and (addr != start+len(data) or len(data) == 4096):
            chunks.append((start,bytes(data))); start = None; data = bytearray()
        if start is None: start = addr
        data.append(memory[addr])
    chunks.append((start,bytes(data)))
    return chunks

def readback(root, job):
    chunks = hex_chunks(root / job['hex']); jl = link(job['serial'])
    try:
        for addr, data in chunks:
            if bytes(jl.memory_read8(addr,len(data))) != data: raise ValueError(f'flash mismatch {job["physical_role"]} at {addr:#x}')
    finally: jl.close()
    return {'serial':job['serial'], 'hex_sha256':job['hex_sha256'], 'bytes_verified':sum(len(x[1]) for x in chunks), 'status':'PASS'}

def stop_workers(workers):
    for p in workers:
        if p.poll() is None:
            try: os.killpg(p.pid, signal.SIGTERM)
            except ProcessLookupError: pass
    for p in workers:
        try: p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid,signal.SIGKILL); p.wait(timeout=2)

def side(a):
    root = a.bundle.resolve()
    if sha(root/'payload_hashes.json') != a.expected_index: raise ValueError('deployed payload index differs from source host')
    c = checked(root)
    roles, jobs = probe_check(c, a.side)
    if a.action == 'check':
        print(json.dumps({'checked':True, 'side':a.side, 'roles':roles, 'active':[j['physical_role'] for j in jobs],
                          'payload_index_sha256':sha(root/'payload_hashes.json')})); return
    if a.action == 'quiesce':
        halt(c, roles); print('QUIESCED ' + a.side, flush=True); return
    out = root / 'results' / a.side
    out.mkdir(parents=True, exist_ok=False)
    state = {'started_at':now(), 'side':a.side, 'case_id':c['id'], 'status':'STARTING', 'workers':{}, 'quiesced':roles}
    workers = []; handles = []; result = 1
    def persist(): save(out/'status.json', state)
    def abort(sig, frame): raise RuntimeError(f'interrupted signal {sig}')
    for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]: signal.signal(sig,abort)
    def check_stop():
        if (root/'STOP').exists(): raise RuntimeError('explicit case stop requested')
    try:
        persist(); halt(c, roles)
        for job in jobs:
            check_stop()
            if any(p.poll() is not None for p in workers): raise RuntimeError('TX ended during setup')
            role = job['physical_role']; console = out/(role+'.console.log')
            handle = console.open('x',buffering=1); handles.append(handle)
            cmd = ['bash',str(root/'sdk/Drivers/API'/job['script']),*job['args']]
            env = dict(os.environ, **job['environment'], ARM_NM=NM, PYTHONDONTWRITEBYTECODE='1')
            proc = subprocess.Popen(cmd,env=env,stdout=handle,stderr=subprocess.STDOUT,start_new_session=True)
            workers.append(proc)
            state['workers'][role] = {'serial':job['serial'],'pid':proc.pid,'command':cmd,'started_at':now()}
            persist(); deadline = time.monotonic()+55
            while 'READY marker seen' not in console.read_text():
                check_stop()
                if proc.poll() is not None: raise RuntimeError(role+' exited before READY')
                if time.monotonic()>deadline: raise TimeoutError(role+' READY timeout')
                time.sleep(.1)
            state['workers'][role]['ready_at'] = now(); persist()
        state['status']='READY'; persist(); print('ALL_READY '+a.side,flush=True)
        deadline=time.monotonic()+200
        while any(p.poll() is None for p in workers):
            check_stop()
            # Preserve peer observations until bounded collectors finish; the exit-code
            # validation below still rejects any failed worker.
            if time.monotonic()>deadline: raise TimeoutError('case capture timeout')
            time.sleep(.1)
        for job, proc in zip(jobs,workers):
            role=job['physical_role']; console=(out/(role+'.console.log')).read_text()
            if proc.returncode != 0: raise RuntimeError(role+' capture/verification failed')
            paths={k:v for k,v in re.findall(r'^\[done\] (raw|meta)=(.+)$',console,re.M)}
            if set(paths) != {'raw','meta'}: raise ValueError('capture output paths missing')
            for kind,path in paths.items(): shutil.copy2(path,out/(role+('.log' if kind=='raw' else '.meta.txt')))
            meta=dict(line.split('=',1) for line in Path(paths['meta']).read_text().splitlines() if '=' in line)
            expected={'serial':job['serial'],'physical_role':role,'logical_node':str(job['logical_node']),
                      'suite_case_id':c['id'],'suite_conditions_sha256':c['conditions_sha256'], 'firmware_sha256':job['hex_sha256'],
                      'suite_manifest_sha256':job['environment']['BRRS_SUITE_MANIFEST_SHA256'],'raw_sha256':sha(out/(role+'.log')),
                      'run_number':str(c['conditions']['run']),'collection_status':'PASS'}
            if c['conditions'].get('profile') in ['essential','lite','full','paper']:
                expected.update(suite_profile=c['conditions']['profile'],suite_block=str(c['conditions']['run']),
                    suite_rotation_index=str(c['conditions']['rotation_index']),physical_location=job['location'],suite_assignment_sha256=c['assignment_sha256'])
            for k,v in expected.items():
                if meta.get(k)!=v: raise ValueError(f'{role} metadata mismatch: {k}')
            state['workers'][role].update(exit_code=proc.returncode, raw_sha256=sha(out/(role+'.log')),
                                         readback=readback(root,job))
        state['inactive_halted_after']={}
        for role in roles:
            if role in [j['physical_role'] for j in jobs]: continue
            jl=link(c['boards'][role]['serial'])
            try: is_halted=jl.halted()
            finally: jl.close()
            if not is_halted: raise RuntimeError('inactive TX resumed: '+role)
            state['inactive_halted_after'][role]=True
        state['status']='COLLECTION_METADATA_AND_FLASH_PASS'; result=0
    except Exception as exc:
        state['status']='FAIL'; state['error']=repr(exc)
    finally:
        stop_workers(workers)
        for handle in handles: handle.close()
        if result != 0:
            try: halt(c, roles)
            except Exception as exc: state['cleanup_halt_error']=repr(exc)
        state['finished_at']=now(); persist(); print(json.dumps(state),flush=True)
    return result

def run_single_host(a,c):
    """Dispatch one single-host case and copy its standard evidence."""
    root=a.bundle.resolve();host=next(iter({b['host'] for b in c['boards'].values()}))
    if c['conditions']['stage'] not in ['stage0','exp1','exp2','exp3','exp4','exp5']:
        raise ValueError('unknown single-host stage')
    if (root/'results').exists():raise ValueError('case already has results; assess/resume the campaign, never rerun in place')
    index=sha(root/'payload_hashes.json')
    if host=='local':
        if getattr(a,'host',None):raise ValueError('SSH override is not valid for an all-local case')
        from brrs_single_host import run as single_run
        return single_run(root,c,index)
    host=getattr(a,'host',None) or host
    if not re.fullmatch(r'[A-Za-z0-9._-]+',host):raise ValueError('invalid SSH host')
    cmd=['python3',str(root/'sdk/Drivers/API/brrs_single_host.py'),'run',
         '--bundle',str(root),'--expected-index',index]
    rc=1
    try:
        rc=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=10',host,shlex.join(cmd)],timeout=360).returncode
    finally:
        # Even failed captures remain reviewable. No retry or reflash on error.
        copy=subprocess.run(['scp','-q','-r',f'{host}:{root}/results',str(root)],timeout=60)
        if copy.returncode:raise RuntimeError('single-host evidence copy failed; remote results preserved')
    if rc:return rc
    from brrs_suite_results import assess
    save(root/'results/ASSESSMENT.json',assess(root))
    return 0

def run(a):
    root=a.bundle.resolve(); c=checked(root)
    if len({b['host'] for b in c['boards'].values()})==1:
        return run_single_host(a,c)
    if c['boards']['init']['host']!='local' or any(c['boards'][r]['host']=='local' for r in c['boards'] if r!='init'):
        raise ValueError('split-host mode requires local INIT and all TX on the remote host')
    hosts={b['host'] for b in c['boards'].values()}-{'local'}
    if len(hosts)!=1: raise ValueError('exactly one remote host required')
    host=getattr(a,'host',None) or hosts.pop(); script=str(root/'sdk/Drivers/API/brrs_suite_case.py')
    base=['--bundle',str(root),'--expected-index',sha(root/'payload_hashes.json')]
    def remote(args): return ['ssh','-o','BatchMode=yes','-o','ConnectTimeout=10',host,shlex.join(args)]
    def side_cmd(which,action): return ['python3',script,'side',*base,'--side',which,'--action',action]
    subprocess.run([sys.executable,script,'side',*base,'--side','local','--action','check'],check=True)
    subprocess.run(remote(side_cmd('remote','check')),check=True,timeout=20)
    out=root/'results'; out.mkdir(exist_ok=False)
    state={'started_at':now(),'case_id':c['id'],'status':'STARTING','payload_index_sha256':sha(root/'payload_hashes.json')}
    workers=[]; handles=[]; failed=False
    def abort(sig,frame): raise RuntimeError(f'interrupted signal {sig}')
    for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]: signal.signal(sig,abort)
    try:
        subprocess.run([sys.executable,script,'side',*base,'--side','local','--action','quiesce'],check=True,timeout=20)
        txlog=out/'remote.supervisor.log'; txhandle=txlog.open('x'); handles.append(txhandle)
        tx=subprocess.Popen(remote(side_cmd('remote','run')),stdout=txhandle,stderr=subprocess.STDOUT,start_new_session=True); workers.append(tx)
        deadline=time.monotonic()+100
        while 'ALL_READY remote' not in txlog.read_text():
            if tx.poll() is not None: raise RuntimeError('remote failed before READY')
            if time.monotonic()>deadline: raise TimeoutError('remote READY timeout')
            time.sleep(.1)
        state['all_tx_ready_at']=now(); print('TX READY confirmed; starting RX',flush=True)
        rxhandle=(out/'local.supervisor.log').open('x'); handles.append(rxhandle)
        rx=subprocess.Popen([sys.executable,script,'side',*base,'--side','local','--action','run'],stdout=rxhandle,stderr=subprocess.STDOUT,start_new_session=True); workers.append(rx)
        deadline=time.monotonic()+240
        while any(p.poll() is None for p in workers):
            if any(p.poll() not in [None,0] for p in workers): raise RuntimeError('side failed')
            if time.monotonic()>deadline: raise TimeoutError('case completion timeout')
            time.sleep(.1)
        if any(p.returncode for p in workers): raise RuntimeError('side failed')
        state['status']='COLLECTION_AND_READBACK_PASS_PER_PENDING'
    except BaseException as exc:
        failed=True; state['status']='FAIL'; state['error']=repr(exc)
        (root/'STOP').touch()
        # Explicit remote stop file avoids orphan capture after an SSH failure.
        try: subprocess.run(remote(['touch',str(root/'STOP')]),check=True,timeout=15)
        except Exception as stop_error: state['remote_stop_error']=repr(stop_error)
    finally:
        stop_workers(workers)
        for h in handles: h.close()
        # Preserve remote failure logs too; never erase or retry a condition.
        try:
            transfer=subprocess.run(['scp','-q','-r',f'{host}:{root}/results/remote',str(out)],timeout=30)
            state['remote_results_copy_rc']=transfer.returncode
        except Exception as exc:
            state['remote_results_copy_rc']=-1;state['remote_results_copy_error']=repr(exc)
        if state['remote_results_copy_rc']:
            failed=True;state['status']='FAIL'
        state['finished_at']=now(); save(out/'orchestration.json',state)
    if not failed:
        try:
            from brrs_suite_results import assess
            assessment=assess(root);save(out/'ASSESSMENT.json',assessment)
            state['assessment_verdict']=assessment['verdict']
        except Exception as exc:
            failed=True;state['assessment_error']=repr(exc)
        save(out/'orchestration.json',state)
    print(json.dumps(state,indent=2))
    return 1 if failed else 0

def main():
    ap=argparse.ArgumentParser(description=__doc__); sub=ap.add_subparsers(dest='command',required=True)
    p=sub.add_parser('prepare'); p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--stage',required=True); p.add_argument('--case',required=True); p.add_argument('--reuse',action='store_true')
    p.add_argument('--bundle',type=Path,required=True)
    p.add_argument('--profile',choices=['preparation','full','essential','lite','paper'],default='preparation')
    p.add_argument('--confirmation',action='store_true')
    p=sub.add_parser('side'); p.add_argument('--bundle',type=Path,required=True)
    p.add_argument('--expected-index',required=True)
    p.add_argument('--side',choices=['local','remote'],required=True); p.add_argument('--action',choices=['check','quiesce','run'],required=True)
    p=sub.add_parser('run'); p.add_argument('--bundle',type=Path,required=True)
    p.add_argument('--host',help='SSH transport alias/address; exact board serial set still enforced')
    a=ap.parse_args()
    try: return {'prepare':prepare,'side':side,'run':run}[a.command](a) or 0
    except Exception as exc:
        print(f'FAIL: {exc!r}',file=sys.stderr); return 1

if __name__=='__main__': raise SystemExit(main())
