#!/usr/bin/env python3
"""Independent per-probe halt/readback after a native runner failure. Never resets or flashes."""
import datetime,json,subprocess,sys
from pathlib import Path
CHILD=r"""
import json,sys,pylink
from pathlib import Path
sys.path.insert(0,sys.argv[4])
from brrs_suite_case import hex_chunks
serial=int(sys.argv[1]);role=sys.argv[2];image=sys.argv[3]
j=pylink.JLink();j.open(serial_no=serial);j.set_tif(pylink.enums.JLinkInterfaces.SWD)
j.connect('NRF52840_XXAA',speed=4000);j.exec_command('SetRestartOnClose = 0')
j.halt()
result={'role':role,'serial':serial,'halted':bool(j.halted()),'millivolts':int(j.hardware_status.VTarget),'readback':None}
if image!='-':
 chunks=hex_chunks(Path(image))
 bad=[hex(a) for a,v in chunks if bytes(j.memory_read8(a,len(v)))!=v]
 result['readback']={'status':'PASS' if not bad else 'FAIL','bytes_verified':sum(len(v) for _,v in chunks),'bad_addresses':bad}
print(json.dumps(result),flush=True)
j.close()
"""
def main():
 case=Path(sys.argv[1]).resolve();bundle=case/'bundle'
 root=case.parent.parent
 assert (root/'STOP').exists()
 assert Path('/Users/songchieon/Desktop/DWM3000/logs/ubuntu_controlled/STOP_ALL').exists()
 c=json.loads((bundle/'case.json').read_text())
 jobs={j['physical_role']:j for j in c['jobs']}
 jobs['AUX']=c['auxiliary_job'];jobs['N2']=c['sniffer_job']
 out={}
 for role,board in c['boards'].items():
  image=str(bundle/jobs[role]['hex']) if role in jobs else '-'
  try:
   p=subprocess.run([sys.executable,'-c',CHILD,str(board['serial']),role,image,str(bundle/'sdk/Drivers/API')],capture_output=True,text=True,timeout=35)
   out[role]=json.loads(p.stdout) if p.returncode==0 else {'role':role,'serial':board['serial'],'status':'ERROR','exit_code':p.returncode,'stderr':p.stderr[-500:]}
  except Exception as e:
   out[role]={'role':role,'serial':board['serial'],'status':'ERROR','error':repr(e)}
  print(role,json.dumps(out[role]),flush=True)
 audit={'at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'case_id':c['id'],'scope':'halt and readback only, no reset/flash/RF','boards':out,'all_halted':all(v.get('halted') is True for v in out.values()),'active_readback_pass':all(out[k].get('readback',{}).get('status')=='PASS' for k in jobs),'root_stop':(root/'STOP').exists(),'global_stop':Path('/Users/songchieon/Desktop/DWM3000/logs/ubuntu_controlled/STOP_ALL').exists()}
 path=case/'POST_FAILURE_SAFE_AUDIT.json'
 with path.open('x') as f:json.dump(audit,f,indent=2);f.write('\n')
 if not audit['all_halted'] or not audit['active_readback_pass']:raise SystemExit(1)
if __name__=='__main__':main()
