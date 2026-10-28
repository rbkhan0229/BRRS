#!/usr/bin/env python3
"""Isolated 8-probe runner; reuses unchanged victim collection/evidence checks."""
import json,os,signal,subprocess,sys,threading,time
from pathlib import Path
API=Path(__file__).resolve().parent/'sdk/Drivers/API'
sys.path.insert(0,str(API))
from brrs_single_host import *
from brrs_suite_case import save,now,sha

class Auxiliary:
    def __init__(self,root,c,index):
        self.root=Path(root);self.c=c;self.index=index;self.proc=None;self.handle=None
        self.stopping=False;self.done=threading.Event();self.monitor=None
        self.job=c['auxiliary_job'];self.status_file=self.root/'results/AUX_STATUS.json'
    def status(self):
        return json.loads(self.status_file.read_text()) if self.status_file.exists() else {}
    def stopped(self):
        return (self.root/'STOP').exists() or any(Path(p).exists() for p in self.job['stop_paths'])
    def check(self):
        if self.stopped():raise RuntimeError('user/case/global STOP')
        s=self.status()
        if s.get('error') or s.get('status')=='FAIL':raise RuntimeError('AUX failure: '+repr(s))
        if self.proc and self.proc.poll() is not None:raise RuntimeError('AUX exited before victim completion')
        return s
    def wait(self,predicate,seconds,label):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            s=self.check()
            if predicate(s):return s
            time.sleep(.05)
        raise TimeoutError('AUX '+label+' timeout')
    def start(self):
        if self.stopped():raise RuntimeError('STOP before AUX start')
        self.handle=(self.root/'results/AUX.console.log').open('x')
        self.proc=subprocess.Popen([sys.executable,str(self.root/'provenance/aux_capture.py'),
            '--bundle',str(self.root),'--index',self.index],stdin=subprocess.DEVNULL,
            stdout=self.handle,stderr=subprocess.STDOUT,start_new_session=True,
            env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
        self.wait(lambda s:s.get('ready') is True,40,'READY')
        def monitor():
            while not self.done.wait(.1):
                if self.stopping:return
                try:self.check()
                except BaseException as ex:
                    save(self.root/'results/AUX_MONITOR_ERROR.json',dict(at=now(),error=repr(ex)))
                    (self.root/'STOP').touch();Path(self.job['stop_paths'][-1]).touch();return
        self.monitor=threading.Thread(target=monitor,daemon=True);self.monitor.start()
    def before_init(self):
        self.check()
        if self.job['mode']=='ON':
            (self.root/'results/AUX_ARM').touch(exist_ok=False)
            s=self.wait(lambda s:s.get('tx_success',0)>0,5,'first TX')
            save(self.root/'results/AUX_FIRST_TX_BEFORE_INIT.json',dict(at=now(),status=s))
        elif self.job['mode']=='OFF':
            if self.status().get('tx_success')!=0:raise RuntimeError('OFF AUX transmitted')
        else:raise ValueError('AUX mode')
    def finish(self):
        self.stopping=True;self.done.set()
        if self.monitor:self.monitor.join(timeout=1)
        if self.proc is None:raise RuntimeError('AUX not started')
        (self.root/'results/AUX_STOP').touch(exist_ok=True)
        try:self.proc.wait(timeout=12)
        except subprocess.TimeoutExpired:
            os.killpg(self.proc.pid,signal.SIGTERM)
            try:self.proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                os.killpg(self.proc.pid,signal.SIGKILL);self.proc.wait(timeout=3)
            raise RuntimeError('AUX stop deadline exceeded')
        finally:
            if self.handle:self.handle.close()
        meta=self.root/'results/AUX.meta.json'
        if self.proc.returncode!=0 or not meta.exists():raise RuntimeError('AUX collector failed; see raw/console')
        result=json.loads(meta.read_text())
        if result.get('status')!='PASS':raise RuntimeError('AUX final metadata failed: '+repr(result))
        return result


class Sniffer:
    """N2 passive observer; all RF ownership and immutable logs remain case-local."""
    def __init__(self,root,c,index):
        self.root=Path(root);self.c=c;self.index=index;self.proc=None;self.handle=None
        self.stopping=False;self.done=threading.Event();self.monitor=None
        self.job=c['sniffer_job'];self.status_file=self.root/'results/SNIFFER_STATUS.json'
    def status(self):
        return json.loads(self.status_file.read_text()) if self.status_file.exists() else {}
    def stopped(self):
        return (self.root/'STOP').exists() or any(Path(p).exists() for p in self.job['stop_paths'])
    def check(self):
        if self.stopped():raise RuntimeError('user/case/global STOP')
        s=self.status()
        if s.get('error') or s.get('status')=='FAIL':raise RuntimeError('N2 sniffer failure: '+repr(s))
        if self.proc and self.proc.poll() is not None:raise RuntimeError('N2 sniffer exited before victim completion')
        return s
    def wait(self,predicate,seconds,label):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            s=self.check()
            if predicate(s):return s
            time.sleep(.05)
        raise TimeoutError('N2 sniffer '+label+' timeout')
    def start(self):
        if self.stopped():raise RuntimeError('STOP before N2 sniffer start')
        self.handle=(self.root/'results/SNIFFER.console.log').open('x')
        self.proc=subprocess.Popen([sys.executable,str(self.root/'provenance/sniffer_capture.py'),
            '--bundle',str(self.root),'--index',self.index],stdin=subprocess.DEVNULL,
            stdout=self.handle,stderr=subprocess.STDOUT,start_new_session=True,
            env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
        self.wait(lambda s:s.get('ready') is True,40,'READY')
        def monitor():
            while not self.done.wait(.1):
                if self.stopping:return
                try:self.check()
                except BaseException as ex:
                    save(self.root/'results/SNIFFER_MONITOR_ERROR.json',dict(at=now(),error=repr(ex)))
                    (self.root/'STOP').touch();Path(self.job['stop_paths'][-1]).touch();return
        self.monitor=threading.Thread(target=monitor,daemon=True);self.monitor.start()
    def arm(self):
        self.check()
        (self.root/'results/SNIFFER_ARM').touch(exist_ok=False)
        self.wait(lambda s:s.get('running') is True,5,'ARM')
    def finish(self):
        self.stopping=True;self.done.set()
        if self.monitor:self.monitor.join(timeout=1)
        if self.proc is None:raise RuntimeError('N2 sniffer not started')
        (self.root/'results/SNIFFER_STOP').touch(exist_ok=True)
        try:self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(self.proc.pid,signal.SIGTERM)
            try:self.proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                os.killpg(self.proc.pid,signal.SIGKILL);self.proc.wait(timeout=3)
            raise RuntimeError('N2 sniffer stop deadline exceeded')
        finally:
            if self.handle:self.handle.close()
        meta=self.root/'results/SNIFFER.meta.json'
        if self.proc.returncode!=0 or not meta.exists():raise RuntimeError('N2 sniffer collector failed; see raw/console')
        result=json.loads(meta.read_text())
        if result.get('status')!='PASS':raise RuntimeError('N2 sniffer final metadata failed: '+repr(result))
        return result

def run(root, c, index, assessor=None):
    auxiliary = Auxiliary(root, c, index)
    sniffer = Sniffer(root, c, index)
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
            if job['logical_node'] == 1:
                sniffer.arm()
                auxiliary.before_init()
            console = out/(job['physical_role']+'.console.log')
            handle = console.open('x'); handles.append(handle)
            cmd = ['bash',str(root/'sdk/Drivers/API'/job['script']),*job['args']]
            env = dict(os.environ, **job['environment'], ARM_NM=NM, PYTHONDONTWRITEBYTECODE='1')
            if c['conditions'].get('profile') == 'standard':
                env.update(BRRS_RECONNECT_ATTEMPTS='0', BRRS_STRICT_CONNECTION='1')
            return subprocess.Popen(cmd, env=env, stdin=subprocess.DEVNULL, stdout=handle,
                                    stderr=subprocess.STDOUT, start_new_session=True), console
        try:
            if (root/'STOP').exists(): raise RuntimeError('explicit stop requested before run')
            halt(c,list(c['boards']))
            sniffer.start()
            auxiliary.start()
            supervise(root,c,state,spawn)
            state['victim_capture_finished_at'] = now()
        except BaseException as exc:
            state['error'] = repr(exc)
        finally:
            try:
                state['sniffer'] = sniffer.finish()
            except BaseException as exc:
                state['sniffer_error'] = repr(exc)
            try:
                state['auxiliary'] = auxiliary.finish()
            except BaseException as exc:
                state['auxiliary_error'] = repr(exc)
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
            aux_ok = state.get('auxiliary',{}).get('status') == 'PASS' and 'auxiliary_error' not in state
            sniffer_ok = state.get('sniffer',{}).get('status') == 'PASS' and 'sniffer_error' not in state
            state['status'] = 'COLLECTION_AND_READBACK_PASS' if aux_ok and sniffer_ok and good_workers and good_recovery and not state.get('halt_errors') and 'error' not in state else 'FAIL'
            state['finished_at'] = now(); save(out/'status.json', state)
            if state['status']=='COLLECTION_AND_READBACK_PASS':
                try:
                    export_cir_evidence(root,c,state)
                    if assessor is None:
                        from brrs_suite_results import assess
                        assessor = assess
                    assessment=assessor(root);save(out/'ASSESSMENT.json',assessment)
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
