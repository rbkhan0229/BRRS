from pathlib import Path
import csv,hashlib,json,re
r=Path(__file__).resolve().parent;m=json.loads((r/'manifest.json').read_text());rows=[];pooled={};summaries=[]
for variant,run in json.loads((r/'comparison_order.json').read_text()):
    f=r/f'{variant}_r{run}';a=json.loads((f/'audit.json').read_text());assert a['valid'],a
    raw=(f/'init/init.log').read_text()
    pattern=r'RX timeouts=(\d+) \(fwto=(\d+) pto=(\d+)\)  RX errors=(\d+) \(sfdto=(\d+) phe=(\d+) fce=(\d+) fsl=(\d+) fint-only=(\d+) overrun=(\d+)\)'
    match=re.search(pattern,raw);assert match
    err=dict(zip(['rx_timeouts','fwto','pto','rx_errors','sfd_timeout','phr','crc','rxfsl','fint_only','overrun'],map(int,match.groups())))
    orch=json.loads((r/f'{variant}_r{run}_orchestration.json').read_text())
    s={'tag':f'{variant}{run}','variant':variant,'run':run,'nodes':a['nodes'],'total':a['total'],'errors':err,'goal':a['goal'],'window_records':a['init_records'].get('EXP4_SLOT_RX_CSV',[]),'started_at':orch['started_at'],'finished_at':orch['finished_at']};summaries.append(s)
    for node,d in a['nodes'].items():
        rows.append({'variant':variant,'run':run,'node':node,**d})
        target=pooled.setdefault(variant,{}).setdefault(node,dict(offered=0,rx=0,lost=0))
        for key in ['offered','rx','lost']:target[key]+=d[key]
for nodes in pooled.values():
    for d in nodes.values():d['per_pct']=100*d['lost']/d['offered']
(r/'RESULTS.json').write_text(json.dumps({'primary_runs':summaries,'pooled':pooled,'manifest_sha256':hashlib.sha256((r/'manifest.json').read_bytes()).hexdigest(),'per_goal':'each node <1%, not aggregate','development_runs':m['development_runs']},indent=2)+'\n')
with (r/'RESULTS.csv').open('w') as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
print(json.dumps({'runs':summaries,'pooled':pooled},indent=2))
