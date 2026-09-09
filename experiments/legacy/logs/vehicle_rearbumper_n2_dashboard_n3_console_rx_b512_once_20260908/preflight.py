import json, subprocess, platform, hashlib, re
from pathlib import Path
from datetime import datetime
import pylink
base=Path('/Users/songchieon/Desktop/DWM3000')
def cmd(args):
    p=subprocess.run(args,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    return {'rc':p.returncode,'stdout':p.stdout,'stderr':p.stderr}
out={'at':datetime.now().astimezone().isoformat(),'host':platform.node(),
     'probes':sorted(str(x.SerialNumber) for x in pylink.JLink().connected_emulators()),'git':{}}
for name in ['DW3_QM33_SDK_1.0.2','DW3_QM33_SDK_1.0.2_exp4_rx_error_diag_20260904']:
    repo=base/name
    out['git'][name]={key:cmd(['git','--no-optional-locks','-C',str(repo),*args])['stdout'].strip()
                     for key,args in [('branch',['branch','--show-current']),('head',['rev-parse','HEAD']),('status',['status','--porcelain'])]}
    out['git'][name]['diff_sha256']=hashlib.sha256(cmd(['git','--no-optional-locks','-C',str(repo),'diff','HEAD'])['stdout'].encode()).hexdigest()
ps=cmd(['ps','-axo','pid,etime,command'])
out['capture_processes']=[s for s in ps['stdout'].splitlines() if re.search(r'JLinkExe|JLinkRTT|rtt_capture\.py|brrs_suite_case\.py|brrs_exp4_capture\.sh',s)]
out['usb']=cmd(['ioreg','-p','IOUSB','-l','-w','0'])
print(json.dumps(out,indent=2))
