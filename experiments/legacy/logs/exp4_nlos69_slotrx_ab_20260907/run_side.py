"""Bounded fixed-role capture side. Same collector for both firmware versions.

No erase-all, no retries, no build, no overwrites; explicit board serials.
Run TX first; only after ALL_READY should the local INIT side be launched.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parent
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def now(): return datetime.now(timezone.utc).isoformat()
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--variant',choices=['A','B'],required=True)
    ap.add_argument('--run',type=int,required=True); ap.add_argument('--side',choices=['tx','init'],required=True)
    ap.add_argument('--dry-run',action='store_true'); args=ap.parse_args()
    assert args.run>0
    manifest=json.loads((ROOT/'manifest.json').read_text())
    roles=['N2','N3','N4'] if args.side=='tx' else ['init']
    target=ROOT/f'{args.variant}_r{args.run}'/args.side
    if target.exists(): raise RuntimeError(f'Existing run side {target}')
    capture=Path(manifest['capture_script'])
    assert digest(capture)==manifest['capture_sha256'], 'collector hash mismatch'
    records=manifest['variants'][args.variant]
    commands={}
    for role in roles:
        rec=records[role]; assert digest(ROOT/rec['hex'])==rec['sha256']
        commands[role]=[sys.executable,str(capture),'--hex',str(ROOT/rec['hex']),
            '--serial',rec['serial'],'--rtt-address',rec['rtt'],'--channel','1',
            '--ready-marker','EXP_LOG_READY,channel=1','--end-marker','===== END STATS =====',
            '--timeout','180' if args.side=='tx' else '90','--out',str(target/f'{role}.log')]
    if args.dry_run:
        print(json.dumps(commands,indent=2)); return 0
    import pylink
    actual={str(x.SerialNumber) for x in pylink.JLink().connected_emulators()}
    expected={records[role]['serial'] for role in roles}
    if actual!=expected: raise RuntimeError(f'Connected probes {actual} != expected {expected}')
    target.mkdir(parents=True,exist_ok=False)
    state={'variant':args.variant,'run':args.run,'side':args.side,'started_at':now(),
        'status':'STARTING','parameters':manifest['parameters'],'workers':{},
        'manifest_sha256':digest(ROOT/'manifest.json'),'capture_sha256':manifest['capture_sha256']}
    workers=[]; handles=[]; deadline=time.monotonic()+240
    def persist():
        temp=target/'status.tmp'; temp.write_text(json.dumps(state,indent=2)+'\n'); temp.replace(target/'status.json')
    def stop(sig,_frame): raise RuntimeError(f'interrupted signal {sig}')
    for sig in (signal.SIGINT,signal.SIGTERM,signal.SIGHUP): signal.signal(sig,stop)
    result=1
    try:
        persist()
        for role in roles:
            for prior in workers:
                if prior.poll() is not None: raise RuntimeError('premature worker completion during setup')
            console=target/f'{role}.console.log'; handle=console.open('x',buffering=1); handles.append(handle)
            proc=subprocess.Popen(commands[role],stdout=handle,stderr=subprocess.STDOUT,start_new_session=True)
            workers.append(proc)
            state['workers'][role]=dict(records[role],pid=proc.pid,command=commands[role],started_at=now(),ready=False)
            persist(); print(f'STARTED {role} serial={records[role]["serial"]}',flush=True)
            ready_deadline=time.monotonic()+45
            while 'READY marker seen' not in console.read_text():
                if proc.poll() is not None: raise RuntimeError(f'{role} exited before READY')
                if time.monotonic()>ready_deadline: raise TimeoutError(f'{role} READY timeout')
                time.sleep(.1)
            state['workers'][role]['ready']=True; persist()
        state['status']='READY'; persist(); print(f'ALL_READY {args.variant} r{args.run} {args.side}',flush=True)
        while any(p.poll() is None for p in workers):
            if time.monotonic()>deadline: raise TimeoutError('supervisor deadline')
            time.sleep(.1)
        for role,proc in zip(roles,workers):
            raw=target/f'{role}.log'; text=raw.read_text()
            record=state['workers'][role]
            record.update(exit_code=proc.returncode,raw_sha256=digest(raw),raw_bytes=raw.stat().st_size)
            if proc.returncode!=0 or text.count('===== END STATS =====')!=1 or 'EXP_LOG_READY,channel=1' not in text:
                raise RuntimeError(f'{role} incomplete capture')
        state['status']='CAPTURE_COMPLETE_NOT_YET_RF_VERIFIED'; result=0
    except Exception as exc:
        state['status']='FAIL_CAPTURE'; state['error']=repr(exc)
    finally:
        for proc in workers:
            if proc.poll() is None:
                try: os.killpg(proc.pid,signal.SIGTERM)
                except ProcessLookupError: pass
        for proc in workers:
            try: proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid,signal.SIGKILL); proc.wait(timeout=2)
        for h in handles: h.close()
        state['finished_at']=now(); persist(); print(json.dumps(state),flush=True)
    return result
if __name__=='__main__': sys.exit(main())
