"""After collectors stop: halt exact probes, read flash and failed-link RAM. No reset/RF."""
from pathlib import Path
import sys,json,subprocess,re
from datetime import datetime
R=Path('/Users/songchieon/Desktop/DWM3000/logs/vehicle_dashboard_beacon512_pac8_l26_once_20260908');B=R/'capture1'
sys.path.insert(0,str(B/'sdk/Drivers/API'))
from brrs_suite_case import checked,probe_check,halt,readback,link,NM,sha
side=sys.argv[1];c=checked(B);roles,jobs=probe_check(c,side)
ps=subprocess.check_output(['ps','-axo','pid,command'],text=True)
active=[v for v in ps.splitlines() if str(B) in v and any(t in v for t in ['rtt_capture.py','brrs_exp4_capture.sh'])]
assert not active,active
halt(c,roles)
out={'at':datetime.now().astimezone().isoformat(),'side':side,'boards':{},'reset_performed':False,'rf_rerun':False}
names=['exp4_last_sync_seq','exp4_sync_frames_missed','exp4_sync_frames_received','exp4_sync_rx_delayed_late','exp4_sync_rx_scheduled','total_rx_errors','total_tx_attempts','total_tx_delayed_late']
for j in jobs:
 item={'readback':readback(B,j)}
 jl=link(j['serial'])
 try:
  assert jl.halted();item['halted']=True
  if j['physical_role'] != 'init':
   elf=(B/j['hex']).with_suffix('.elf');assert sha(elf)==j['elf_sha256']
   symbols=subprocess.check_output([NM,'-a','-S',str(elf)],text=True)
   fields={}
   for name in names:
    match=re.search(r'^([0-9a-f]+)\s+00000004\s+\w\s+'+re.escape(name)+r'$',symbols,re.M);assert match,name
    addr=int(match[1],16);fields[name]={'address':hex(addr),'u32':int(jl.memory_read32(addr,1)[0])}
   stats=re.search(r'^([0-9a-f]+)\s+([0-9a-f]+)\s+\w\s+per_stats$',symbols,re.M);assert stats
   stats_base=int(stats[1],16);stats_bytes=int(stats[2],16)
   offset=(j['logical_node']-1)*12;assert offset+12<=stats_bytes
   fields['own_tx_success']={'address':hex(stats_base+offset),'u32':int(jl.memory_read32(stats_base+offset,1)[0])}
   item['ram_snapshot']=fields;item['elf_sha256']=sha(elf)
 finally:jl.close()
 out['boards'][j['physical_role']]=item
print(json.dumps(out,indent=2))
