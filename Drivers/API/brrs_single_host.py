#!/usr/bin/env python3
"""One Stage0..Exp5 capture, all probes on one host; no RF retries.

check is read-only; park halts/readbacks without reset; start performs one RF run.
Existing capture scripts and exact HEX files are reused without firmware edits.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import platform
import re
import signal
import shutil
import subprocess
import sys
import time

from brrs_suite_case import checked, halt, link, NM, now, readback, save, sha, stop_workers

PROCESS_TOKENS = ('rtt_capture.py', 'brrs_stage0_capture.sh', 'brrs_exp1_capture.sh',
                  'brrs_exp2_capture', 'brrs_exp3_capture.sh', 'brrs_exp4_capture.sh',
                  'brrs_exp5_capture.sh', 'JLinkExe', 'JLinkRTT')

# Retain the shared Mac lock; Linux has no /private/tmp directory.
LOCK_PATH = '/private/tmp/brrs-single-host-jlink.lock' if platform.system() == 'Darwin' else '/tmp/brrs-single-host-jlink.lock'

def detached_command(command):
    return ['/usr/bin/caffeinate', '-i', *command] if platform.system() == 'Darwin' else command

def layout(c):
    stage=c['conditions']['stage']
    if stage not in ['stage0','exp1','exp2','exp3','exp4','exp5']:
        raise ValueError('single-host runner supports Stage0 through Exp5')
    if len({b['host'] for b in c['boards'].values()}) != 1:
        raise ValueError('all boards must name the same physical host')
    rx = [j for j in c['jobs'] if j['logical_node'] == 1]
    tx = [j for j in c['jobs'] if j['logical_node'] != 1]
    if len(rx) != 1 or not tx:
        raise ValueError('exactly one RX and at least one TX required')
    if stage!='exp4' and (len(tx)!=1 or tx[0]['logical_node']!=2):
        raise ValueError('single-link capture requires exactly one active TX as logical N2')
    if len({j['physical_role'] for j in c['jobs']}) != len(c['jobs']):
        raise ValueError('duplicate role')
    for j in c['jobs']:
        if j['host'] != c['boards'][j['physical_role']]['host']:
            raise ValueError('job physical host mismatch')
        if '--no-build' not in j['args'] or '--serial' not in j['args']:
            raise ValueError('explicit serial and no-build required')
        if j['args'][j['args'].index('--serial')+1] != j['serial']:
            raise ValueError('capture serial mismatch')
        if any(x in j['args'] for x in ('--force', '--build-only')):
            raise ValueError('force/build-only not permitted for capture')
    return tx, rx[0]

def load_bundle(root, expected_index):
    if sha(root/'payload_hashes.json') != expected_index:
        raise ValueError('payload index mismatch')
    c = checked(root)
    layout(c)
    for j in c['jobs']:
        if sha((root/j['hex']).with_suffix('.elf')) != j['elf_sha256']:
            raise ValueError('ELF mismatch: '+j['physical_role'])
    return c

def preflight(root, c):
    import pylink
    actual = sorted(str(e.SerialNumber) for e in pylink.JLink().connected_emulators())
    expected = sorted(b['serial'] for b in c['boards'].values())
    ps = subprocess.check_output(['ps', '-axo', 'pid=,args='], text=True)
    active = [s for s in ps.splitlines() if any(t in s for t in PROCESS_TOKENS)]
    info = dict(at=now(), host=platform.node(), actual=actual, expected=expected,
                capture_processes=active, bundle=str(root), rf_started=False)
    if actual != expected:
        raise RuntimeError('probe set mismatch: '+json.dumps(info))
    if active:
        raise RuntimeError('existing capture/J-Link process; not stopping it: '+json.dumps(active))
    return info

def snapshot(root, c):
    # Called only after workers have ended and all boards have been halted.
    result = {}
    names = ['exp4_last_sync_seq', 'exp4_sync_frames_missed',
             'exp4_sync_frames_received', 'exp4_sync_rx_delayed_late',
             'exp4_sync_rx_scheduled', 'total_rx_errors', 'total_tx_attempts',
             'total_tx_delayed_late']
    for j in c['jobs']:
        role = j['physical_role']
        try:
            item = {'readback': readback(root, j)}
            jl = link(j['serial'])
            try:
                item['halted'] = bool(jl.halted())
                if not item['halted']: raise RuntimeError('target is not halted')
                if j['logical_node'] != 1 and c['conditions']['stage']=='exp4':
                    symbols = subprocess.check_output([NM, '-a', '-S', str((root/j['hex']).with_suffix('.elf'))], text=True)
                    fields = {}
                    for name in names:
                        match = re.search(r'^([0-9a-f]+)\s+00000004\s+\w\s+'+re.escape(name)+r'$', symbols, re.M)
                        if not match: raise ValueError('missing RAM symbol: '+name)
                        addr = int(match[1], 16)
                        fields[name] = int(jl.memory_read32(addr, 1)[0])
                    stats = re.search(r'^([0-9a-f]+)\s+([0-9a-f]+)\s+\w\s+per_stats$', symbols, re.M)
                    offset = (j['logical_node']-1)*12
                    if not stats or offset+12 > int(stats[2], 16): raise ValueError('per_stats bounds')
                    fields['own_tx_success'] = int(jl.memory_read32(int(stats[1], 16)+offset, 1)[0])
                    item['ram_snapshot'] = fields
            finally: jl.close()
            result[role] = item
        except Exception as exc:
            result[role] = {'error': repr(exc)}
    return result

def park_all(c):
    errors = {}
    # If a cable goes away, still stop every other explicitly assigned board.
    for role in c['boards']:
        try: halt(c,[role])
        except Exception as exc: errors[role] = repr(exc)
    return errors

def metadata(root, c, job, console):
    paths = dict(re.findall(r'^\[done\] (raw|meta)=(.+)$', console, re.M))
    # A capture timeout may have no [done] lines: preserve its raw log anyway.
    if 'raw' not in paths:
        match = re.search(r'^\s*Raw log:\s+(.+)$', console, re.M)
        if match: paths['raw'] = match[1]
    result = {'paths': paths}
    for path in paths.values():
        if not Path(path).resolve().is_relative_to(root):
            raise ValueError('capture output outside bundle')
    if paths.get('raw') and Path(paths['raw']).is_file():
        raw = Path(paths['raw'])
        result['raw_sha256'] = sha(raw)
        result['end_stats'] = '===== END STATS =====' in raw.read_text(errors='replace')
    if set(paths) != {'raw', 'meta'}:
        result['metadata_valid'] = False
        return result
    fields = [s.split('=',1) for s in Path(paths['meta']).read_text().splitlines() if '=' in s]
    meta = dict(fields)
    if len(fields) != len(meta): raise ValueError('duplicate metadata keys')
    expected = {'serial': job['serial'], 'physical_role': job['physical_role'],
        'logical_node': str(job['logical_node']), 'suite_case_id': c['id'],
        'suite_conditions_sha256': c['conditions_sha256'],
        'suite_manifest_sha256': job['environment']['BRRS_SUITE_MANIFEST_SHA256'],
        'firmware_sha256': job['hex_sha256'], 'raw_sha256': result['raw_sha256'],
        'run_number': str(c['conditions']['run']), 'collection_status': 'PASS'}
    if c['conditions'].get('profile') in ['standard','essential','lite','full','paper']:
        expected.update(suite_profile=c['conditions']['profile'],suite_block=str(c['conditions']['run']),
            suite_rotation_index=str(c['conditions']['rotation_index']),physical_location=job['location'],
            suite_assignment_sha256=c['assignment_sha256'])
    result['metadata_valid'] = all(meta.get(k) == v for k,v in expected.items())
    result['metadata_mismatches'] = {k: {'actual':meta.get(k), 'expected':v} for k,v in expected.items() if meta.get(k)!=v}
    return result

def supervise(root, c, state, spawn, ready_timeout=55, capture_timeout=200):
    """Dependency-injected worker flow, exercised without RF in tests."""
    tx, rx = layout(c)
    workers = []
    def persist(): save(root/'results/status.json', state)
    def stop_requested():
        if (root/'STOP').exists(): raise RuntimeError('explicit stop requested')
    try:
        for job in [*tx, rx]:
            stop_requested()
            if any(p.poll() is not None for _,p,_ in workers):
                raise RuntimeError('a TX ended before RX setup completed')
            role = job['physical_role']
            proc, console = spawn(job)
            workers.append((job, proc, console))
            state['workers'][role] = {'serial':job['serial'], 'pid':proc.pid, 'started_at':now(), 'console':str(console)}
            if job is rx:
                state['rf_runs_started'] = 1
                state['rx_started_at'] = state['workers'][role]['started_at']
            persist()
            deadline = time.monotonic()+ready_timeout
            while 'READY marker seen' not in console.read_text():
                stop_requested()
                if proc.poll() is not None: raise RuntimeError(role+' exited before READY')
                if time.monotonic() > deadline: raise TimeoutError(role+' READY timeout')
                time.sleep(.05)
            state['workers'][role]['ready_at'] = now()
            if job is tx[-1]: state['all_tx_ready_at'] = now()
            persist()
        state['status'] = 'CAPTURING'; persist()
        deadline = time.monotonic()+capture_timeout
        # A weak TX failing must not truncate the RX/other TX observation.
        while any(p.poll() is None for _,p,_ in workers):
            stop_requested()
            if time.monotonic()>deadline: raise TimeoutError('case timeout')
            time.sleep(.05)
    finally:
        stop_workers([p for _,p,_ in workers])
        for job, proc, console in workers:
            state['workers'][job['physical_role']]['exit_code'] = proc.returncode
        persist()

def summarize(root, c, state):
    if c['conditions']['stage']!='exp4':
        # Standard assessment independently checks CIR rows/END and the physical
        # link binding. Never interpret logical N2 as the physical N2 board.
        p=root/'results/ASSESSMENT.json'
        if state['status']=='COLLECTION_AND_READBACK_PASS' and p.exists():
            assessment=json.loads(p.read_text())
            return {'verdict':assessment['verdict'],'collection_status':state['status'],
                    'case_id':c['id'],'nodes_by_serial':assessment['nodes_by_serial'],
                    'stage_metrics':assessment['stage_metrics'],'rf_runs_started':state.get('rf_runs_started',0)}
        return {'verdict':'INVALID','collection_status':state['status'],'case_id':c['id'],
                'workers':state['workers'],'error':state.get('error') or state.get('assessment_error'),
                'rf_runs_started':state.get('rf_runs_started',0)}
    _, rxjob = layout(c)
    rw = state['workers'].get(rxjob['physical_role'], {})
    raw = rw.get('paths', {}).get('raw')
    rx = Path(raw).read_text(errors='replace') if raw and Path(raw).is_file() else ''
    n = c['conditions']['cycles']
    complete = f'Superframes: total={n}' in rx and '===== END STATS =====' in rx
    config = next((s for s in rx.splitlines() if s.startswith('EXP4_CONFIG_CSV,')), '')
    cfg = dict(x.split('=',1) for x in config.split(',') if '=' in x)
    expected_cfg = {'sync_plen':str(c['conditions'].get('beacon_preamble_symbols',512)),
        'data_plen':str(c['conditions']['preamble']), 'data_pac':str(c['conditions']['rx_pac']),
        'lead_us':str(c['conditions']['lead_us']), 'data_slots':str(len(c['conditions']['slot_owners']))}
    config_valid = all(cfg.get(k)==v for k,v in expected_cfg.items())
    rows = []
    for j in c['jobs']:
        if j['logical_node']==1: continue
        role = j['physical_role']
        row = {'role':role, 'serial':j['serial'], 'location':j['location'],
               'offered':n*c['conditions']['slot_owners'].count(str(j['logical_node'])),
               'rx':None, 'per_percent':None}
        line = next((s for s in rx.splitlines() if s.startswith(f'EXP4_NODE_CSV,N{j["logical_node"]},')), None)
        if complete and config_valid and line:
            fields = line.split(','); offered, received, lost = map(int, fields[3:6])
            if offered != row['offered'] or offered != received+lost or not 0<=received<=offered:
                raise ValueError('inconsistent node counters: '+role)
            row.update(rx=received, per_percent=100*lost/offered)
        row['tx_ram'] = state.get('recovery', {}).get(role, {}).get('ram_snapshot')
        row['collection'] = state['workers'].get(role, {})
        rows.append(row)
    passed = state['status']=='COLLECTION_AND_READBACK_PASS' and complete and config_valid and all(r['rx'] and r['per_percent']<1 for r in rows)
    prefixes = ('RX timeouts=', 'TDMA validation:', 'EXP4_DOUBLE_BUFFER_CSV,', 'EXP4_TIMING_CSV,',
                'EXP4_STATUS_CSV,', 'EXP4_SUMMARY_CSV,', 'EXP4_DEFERRED_CSV,')
    return {'verdict':'PASS_PER_NODE' if passed else 'FAIL', 'rx_complete':complete,
            'config_valid':config_valid, 'rx_config':cfg, 'nodes':rows,
            'rx_counter_lines':[s for s in rx.splitlines() if s.startswith(prefixes)],
            'rf_runs_started':state.get('rf_runs_started',0),
            'collection_status':state['status'],
            'limitations':['Missing/early TX END remains a collection failure.',
                          'Offered PER includes transmissions missed due to beacon loss.',
                          'Exp4 TX keeps beacon reacquisition active for up to 5s after the last acquired beacon.']}

def export_cir_evidence(root,c,state):
    """Write the same evidence layout as split-host capture, with real proofs."""
    if state['status']!='COLLECTION_AND_READBACK_PASS' or state.get('halt_errors'):
        raise ValueError('cannot export successful evidence for failed control')
    out=root/'results'
    for side in ['local','remote']:
        roles={r for r,b in c['boards'].items() if (b['host']=='local')==(side=='local')}
        if not roles:continue
        target=out/side;target.mkdir(exist_ok=False)
        workers={}
        for job in c['jobs']:
            role=job['physical_role']
            if role not in roles:continue
            worker=state['workers'][role]
            for kind,suffix in [('raw','.log'),('meta','.meta.txt')]:
                shutil.copy2(worker['paths'][kind],target/(role+suffix))
            workers[role]={**worker,'readback':state['recovery'][role]['readback']}
        save(target/'status.json',{'status':'COLLECTION_METADATA_AND_FLASH_PASS','case_id':c['id'],
            'workers':workers,'inactive_halted_after':{r:True for r in c['inactive_tx_roles'] if r in roles}})
    save(out/'orchestration.json',{'status':'COLLECTION_AND_READBACK_PASS_PER_PENDING','case_id':c['id'],
        'payload_index_sha256':state['payload_index_sha256'],'all_tx_ready_at':state['all_tx_ready_at'],
        'started_at':state['started_at'],'finished_at':state['finished_at'],'execution_mode':'single_host'})

def run(root, c, index):
    # Machine-wide lock; never kill or adopt another experiment's processes.
    with open(LOCK_PATH,'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        info = preflight(root,c)
        out = root/'results'; out.mkdir(exist_ok=False)
        state = dict(started_at=now(), host=platform.node(), status='STARTING',
                     payload_index_sha256=index, preflight=info, workers={}, rf_runs_started=0)
        handles = []
        def abort(sig, frame): raise RuntimeError('interrupted signal '+str(sig))
        for sig in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP): signal.signal(sig,abort)
        def spawn(job):
            console = out/(job['physical_role']+'.console.log')
            handle = console.open('x'); handles.append(handle)
            cmd = ['bash',str(root/'sdk/Drivers/API'/job['script']),*job['args']]
            env = dict(os.environ, **job['environment'], ARM_NM=NM, PYTHONDONTWRITEBYTECODE='1')
            return subprocess.Popen(cmd, env=env, stdin=subprocess.DEVNULL, stdout=handle,
                                    stderr=subprocess.STDOUT, start_new_session=True), console
        try:
            if (root/'STOP').exists(): raise RuntimeError('explicit stop requested before run')
            halt(c,list(c['boards']))
            supervise(root,c,state,spawn)
        except BaseException as exc:
            state['error'] = repr(exc)
        finally:
            for h in handles: h.close()
            for job in c['jobs']:
                worker = state['workers'].get(job['physical_role'])
                if worker:
                    try: worker.update(metadata(root,c,job,Path(worker['console']).read_text()))
                    except Exception as exc: worker['metadata_error'] = repr(exc)
            try:
                state['halt_errors'] = park_all(c)
                state['recovery'] = snapshot(root,c)
            except Exception as exc: state['recovery_error'] = repr(exc)
            good_workers = len(state['workers'])==len(c['jobs']) and all(w.get('exit_code')==0 and w.get('metadata_valid') for w in state['workers'].values())
            good_recovery = len(state.get('recovery',{}))==len(c['jobs']) and all(x.get('halted') and x.get('readback',{}).get('status')=='PASS' for x in state.get('recovery',{}).values())
            state['status'] = 'COLLECTION_AND_READBACK_PASS' if good_workers and good_recovery and not state.get('halt_errors') and 'error' not in state else 'FAIL'
            state['finished_at'] = now(); save(out/'status.json', state)
            if state['status']=='COLLECTION_AND_READBACK_PASS':
                try:
                    export_cir_evidence(root,c,state)
                    from brrs_suite_results import assess
                    assessment=assess(root);save(out/'ASSESSMENT.json',assessment)
                    state['assessment_verdict']=assessment['verdict']
                except Exception as exc:
                    state['assessment_error']=repr(exc);state['status']='FAIL'
                    if (out/'orchestration.json').exists():
                        invalid=json.loads((out/'orchestration.json').read_text());invalid.update(status='FAIL',error=repr(exc))
                        save(out/'orchestration.json',invalid)
                save(out/'status.json',state)
            try: save(out/'SUMMARY.json',summarize(root,c,state))
            except Exception as exc:
                state['summary_error']=repr(exc); state['status']='FAIL'; save(out/'status.json',state)
        print(json.dumps(state,indent=2),flush=True)
        return 0 if state['status']=='COLLECTION_AND_READBACK_PASS' else 1

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action',choices=['check','park','start','run','status','stop','transport-test'])
    ap.add_argument('--bundle',type=Path,required=True)
    ap.add_argument('--expected-index',required=True)
    a=ap.parse_args(); root=a.bundle.resolve(); c=load_bundle(root,a.expected_index)
    if a.action=='check': print(json.dumps(preflight(root,c),indent=2)); return 0
    if a.action=='park':
        with open(LOCK_PATH,'a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
            info=preflight(root,c)
            info.update(halt_errors=park_all(c),recovery=snapshot(root,c),rf_started=False,reset_performed=False)
        save(root/'park.json',info); print(json.dumps(info,indent=2))
        return 0 if all(x.get('halted') and x.get('readback',{}).get('status')=='PASS' for x in info['recovery'].values()) else 1
    if a.action=='status':
        for path in (root/'launch.json',root/'results/status.json',root/'results/SUMMARY.json'):
            if path.exists(): print(path.name+'\n'+path.read_text())
        return 0
    if a.action=='stop':
        (root/'STOP').touch(exist_ok=True); print('Stop requested; no retry.'); return 0
    if a.action=='transport-test':
        # No board imports/access: proof survives SSH exit, with bounded duration.
        log=root/'transport-test.log'; done=root/'transport-test-done.json'
        code='import time,json,pathlib,os;time.sleep(3);pathlib.Path('+repr(str(done))+').write_text(json.dumps({"pid":os.getpid(),"ssh_detached":True,"rf_started":False}))'
        with log.open('x') as f:
            p=subprocess.Popen(detached_command([sys.executable,'-c',code]),stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
        print(json.dumps({'pid':p.pid,'proof':str(done),'rf_started':False})); return 0
    if a.action=='start':
        preflight(root,c)
        if (root/'STOP').exists() or (root/'results').exists(): raise RuntimeError('used/stopped bundle; create a fresh bundle, never retry in place')
        with (root/'launch.json').open('x') as f:
            # Popen has no inherited SSH stdio or session. No network is used by run.
            with (root/'supervisor.log').open('x') as log:
                cmd=detached_command([sys.executable,str(Path(__file__).resolve()),'run','--bundle',str(root),'--expected-index',a.expected_index])
                p=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            json.dump({'pid':p.pid,'started_at':now(),'command':cmd,'payload_index_sha256':a.expected_index},f,indent=2)
        print((root/'launch.json').read_text()); return 0
    return run(root,c,a.expected_index)

if __name__=='__main__':
    try: sys.exit(main())
    except Exception as exc:
        print('FAIL: '+repr(exc),file=sys.stderr); sys.exit(1)
