import argparse,datetime,json,os,pathlib,shlex,signal,subprocess,sys,time
import pylink
root=pathlib.Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('variant',choices=['A','B']);ap.add_argument('run',type=int);a=ap.parse_args();tag=f'{a.variant}_r{a.run}'
assert not (root/tag).exists()
m=json.loads((root/'manifest.json').read_text());serial=m['roles']['init'];assert serial=='1050270933'
actual={str(x.SerialNumber) for x in pylink.JLink().connected_emulators()};assert actual=={serial},actual
jl=pylink.JLink();jl.open(serial_no=int(serial));jl.set_tif(pylink.enums.JLinkInterfaces.SWD);jl.connect('NRF52840_XXAA',speed=4000);jl.halt();assert jl.halted();jl.close()
remote=['python3','-u',str(root/'run_side.py'),'--variant',a.variant,'--run',str(a.run),'--side','tx']
local=[sys.executable,'-u',str(root/'run_side.py'),'--variant',a.variant,'--run',str(a.run),'--side','init']
state={'variant':a.variant,'run':a.run,'started_at':datetime.datetime.now().astimezone().isoformat(),'init_precondition':'halted specified INIT before TX startup','remote_command':remote,'local_command':local}
workers=[]
try:
    with (root/(tag+'_remote_supervisor.log')).open('x') as remote_log, (root/(tag+'_local_supervisor.log')).open('x') as local_log:
        tx=subprocess.Popen(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8','s-macbook-air',shlex.join(remote)],stdout=remote_log,stderr=subprocess.STDOUT);workers.append(tx)
        deadline=time.monotonic()+100
        while 'ALL_READY' not in (root/(tag+'_remote_supervisor.log')).read_text():
            if tx.poll() is not None:raise RuntimeError('remote side ended before all READY')
            if time.monotonic()>deadline:raise TimeoutError('remote READY')
            time.sleep(.1)
        print(f'{tag}: all TX ready, starting INIT',flush=True)
        init=subprocess.Popen(local,stdout=local_log,stderr=subprocess.STDOUT);workers.append(init)
        deadline=time.monotonic()+110
        while any(p.poll() is None for p in workers):
            if time.monotonic()>deadline:raise TimeoutError('capture completion')
            time.sleep(.1)
        state['exit_codes']=[p.returncode for p in workers];assert state['exit_codes']==[0,0],state['exit_codes']
    (root/tag/'manifest.json').write_bytes((root/'manifest.json').read_bytes())
    assert not (root/tag/'tx').exists()
    subprocess.run(['scp','-q','-r',f's-macbook-air:{root}/{tag}/tx',str(root/tag)],check=True)
    for side in ['init','tx']:
        command=[sys.executable,str(root/'verify_flash.py'),'--variant',a.variant,'--side',side] if side=='init' else ['ssh','-o','BatchMode=yes','s-macbook-air',shlex.join(['python3',str(root/'verify_flash.py'),'--variant',a.variant,'--side',side])]
        with (root/tag/(side+'_flash_readback.json')).open('x') as f:subprocess.run(command,stdout=f,check=True)
    state['status']='CAPTURE_AND_FLASH_READBACK_COMPLETE'
except Exception as e:
    state['status']='FAILED';state['error']=repr(e);raise
finally:
    for p in workers:
        if p.poll() is None:p.terminate()
    for p in workers:
        try:p.wait(timeout=5)
        except subprocess.TimeoutExpired:p.kill()
    state['finished_at']=datetime.datetime.now().astimezone().isoformat()
    (root/(tag+'_orchestration.json')).write_text(json.dumps(state,indent=2)+'\n')
print(json.dumps(state),flush=True)
