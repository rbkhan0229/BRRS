import datetime, hashlib, json, pathlib, platform, subprocess
import pylink
base=pathlib.Path('/Users/songchieon/Desktop/DWM3000')
def run(argv,cwd=None): return subprocess.check_output(argv,cwd=cwd,text=True)
git={}
for name in ['DW3_QM33_SDK_1.0.2_exp4_rx_error_diag_20260904','DW3_QM33_SDK_1.0.2']:
    path=base/name
    git[name]={k:run(['git','--no-optional-locks',*v],path).strip() for k,v in {'branch':['branch','--show-current'],'head':['rev-parse','HEAD'],'status':['status','--porcelain=v1']}.items()}
    git[name]['diff_sha256']=hashlib.sha256(run(['git','--no-optional-locks','diff','HEAD'],path).encode()).hexdigest()
probes=[str(x.SerialNumber) for x in pylink.JLink().connected_emulators()]
ps=run(['ps','-axo','pid,ppid,etime,command'])
processes=[x for x in ps.splitlines() if any(t in x for t in ['rtt_capture.py','run_side.py','JLinkRTTLogger','nrfjprog']) and not any(t in x for t in ['preflight.py','exec_command'])]
usb=run(['ioreg','-p','IOUSB','-l','-w','0'])
usb='\n'.join(x for x in usb.splitlines() if any(t in x for t in ['J-Link@','USB Serial Number','Hub@','locationID']))
print(json.dumps({'at':datetime.datetime.now().astimezone().isoformat(),'host':platform.node(),'probes':probes,'git':git,'capture_processes':processes,'usb':usb},indent=2))
