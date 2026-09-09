from pathlib import Path
import csv,hashlib,json,math,re
root=Path(__file__).resolve().parent
order=[('L15',1),('L5',1),('L25',1),('L15',2),('L25',2),('P25',1),('P25',2),('P25',3),('P25',4)]
rows=[];runs=[]
def wilson(lost,n,z=1.959963984540054):
    p=lost/n;d=1+z*z/n;c=(p+z*z/(2*n))/d
    h=z*math.sqrt(p*(1-p)/n+z*z/(4*n*n))/d
    return [100*max(0,c-h),100*min(1,c+h)]
for variant,run in order:
    tag=f'{variant}_r{run}';folder=root/tag
    if not (folder/'audit.json').exists():continue
    a=json.loads((folder/'audit.json').read_text());raw=(folder/'init/init.log').read_text()
    nodes={}
    for line in raw.splitlines():
        if line.startswith('EXP4_NODE_CSV,'):
            f=line.split(',');offered,rx,lost,errors=map(int,f[3:7])
            assert offered==1000 and rx+lost==offered
            nodes[f[1]]=dict(offered=offered,rx=rx,lost=lost,per_pct=100*lost/offered,rx_errors_attributed=errors)
    p=r'RX timeouts=(\d+) \(fwto=(\d+) pto=(\d+)\)  RX errors=(\d+) \(sfdto=(\d+) phe=(\d+) fce=(\d+) fsl=(\d+) fint-only=(\d+) overrun=(\d+)\)'
    e=re.search(p,raw);assert e
    errors=dict(zip(['rx_timeouts','fwto','pto','rx_errors','sfd_timeout','phr','crc','rxfsl','fint_only','overrun'],map(int,e.groups())))
    orch=json.loads((root/(tag+'_orchestration.json')).read_text())
    tx={}
    for role in ['N2','N3','N4']:
        lines=(folder/'tx'/(role+'.log')).read_text().splitlines()
        fields=next(l for l in lines if l.startswith('EXP4_TX_RESULT_CSV,')).split(',')
        tx[role]=dict(zip(['beacons','missed_beacons','attempts','success','late','end'],map(int,fields[4:10])))
        assert list(tx[role].values())==[1000,0,1000,1000,0,1]
    measured={}
    for prefix in ['EXP4_HOT_PATH_CSV','EXP4_DOUBLE_BUFFER_CSV','EXP4_SPI_CSV']:
        line=next(l for l in raw.splitlines() if l.startswith(prefix+','))
        measured[prefix]=dict(field.split('=',1) for field in line.split(',')[1:])
    s=dict(tag=tag,variant=variant,run=run,valid=a['valid'],goal=a['goal'],failures=a['failures'],nodes=nodes,errors=errors,tx=tx,measured=measured,started_at=orch['started_at'],finished_at=orch['finished_at'])
    runs.append(s)
    for node,v in nodes.items():rows.append(dict(tag=tag,variant=variant,run=run,valid=a['valid'],goal=a['goal'],node=node,**v))
groups={}
for label,tags in [('L15_reference',{'L15_r1','L15_r2'}),('L25_screen',{'L25_r1'}),('P25_confirmation',{'P25_r2','P25_r3','P25_r4'}),('P25_all',{'P25_r1','P25_r2','P25_r3','P25_r4'})]:
    subset=[r for r in runs if r['tag'] in tags]
    if not subset:continue
    assert all(r['valid'] for r in subset)
    nodes={}
    for node in ['N2','N3','N4']:
        v={k:sum(r['nodes'][node][k] for r in subset) for k in ['offered','rx','lost']}
        v['per_pct']=100*v['lost']/v['offered'];v['wilson_95_pct']=wilson(v['lost'],v['offered']);nodes[node]=v
    groups[label]=dict(runs=[r['tag'] for r in subset],nodes=nodes,goal_pass=all(v['per_pct']<1 for v in nodes.values()),each_run_goal_pass=all(r['goal']=='PASS' for r in subset))
out=dict(runs=runs,groups=groups,goal='each TX node PER strictly <1%; zero RX never PASS',ci_note='Wilson two-sided95%, illustrative iid Bernoulli model; correlated NLOS/time variation may violate independence. No unconditional guarantee.',skipped=[dict(variant='L0',reason='L5 all-node RX0; narrower setting not attempted')])
(root/'RESULTS.json').write_text(json.dumps(out,indent=2)+'\n')
with (root/'RESULTS.csv').open('w') as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
print(json.dumps({'runs':[{k:r[k] for k in ['tag','valid','goal','nodes','errors']} for r in runs],'groups':groups},indent=2))
