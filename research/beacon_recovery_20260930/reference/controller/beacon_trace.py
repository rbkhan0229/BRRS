import re,json
from pathlib import Path

def parse(raw,sync):
 rows={}
 for line in raw.splitlines():
  if line.startswith('BRRS_BD_'):
   kind=line.split(',')[0];fields=dict(re.findall(r'(\w+)=([0-9]+)',line))
   rows.setdefault(kind,[]).append({k:int(v) for k,v in fields.items()})
 for key in ['BRRS_BD_SUMMARY','BRRS_BD_BITS','BRRS_BD_END']:
  if len(rows.get(key,[]))!=1:raise ValueError('missing/duplicate '+key)
 s=rows['BRRS_BD_SUMMARY'][0];bits=rows['BRRS_BD_BITS'][0]
 required=['version','first_seq','last_seq','sync_count','missing','initial_missing','tail_missing','gaps','events','truncated','overflow','rx_bad','cfg_bad','seq_bad','cpu_hz']
 if set(s)!=set(required) or s['version']!=1 or s['cpu_hz']!=64000000:raise ValueError('trace schema')
 if any(s[k] for k in ['truncated','overflow','rx_bad','cfg_bad','seq_bad']) or bits.get('overrun')!=0:raise ValueError('trace/system failure')
 if s['sync_count']!=sync['unique'] or s['missing']!=sync['missed'] or s['sync_count']+s['missing']!=2000:raise ValueError('trace/sync mismatch')
 if set(bits)!={'rxgood','sfdto','phe','fce','fsl','fwto','pto','overrun'}:raise ValueError('missing status counts')
 gaps=rows.get('BRRS_BD_GAP',[]);events=rows.get('BRRS_BD_EVENT',[]);counts=rows.get('BRRS_BD_COUNT',[])
 if len(gaps)!=s['gaps'] or len(events)!=s['events'] or [x.get('kind') for x in counts]!=list(range(1,12)):raise ValueError('trace count mismatch')
 if rows['BRRS_BD_END'][0] not in ({'version':1},{'version':2}):raise ValueError('trace END mismatch')
 anchored=rows['BRRS_BD_END'][0]['version']==2
 if [x.get('index') for x in events]!=list(range(len(events))):raise ValueError('event sequence')
 covered=0;missing=s['initial_missing'];decoded=[]
 for i,g in enumerate(gaps):
  if g['id']!=i or g['offset']!=covered or not 1<=g['prev']<g['next']<=2001 or g['next']-g['prev']<=1:raise ValueError('gap bounds')
  if not g.get('rf_start') or ('rf_end' not in g) or (g['next']<=2000 and not g['rf_end']):raise ValueError('missing radio RX timestamp')
  if anchored and g['next']<=2000:
   anchor_keys=('anchor_before_cpu_before','anchor_before_cpu_after','anchor_before_rf','anchor_after_cpu_before','anchor_after_cpu_after','anchor_after_rf')
   if not all(k in g and g[k] for k in anchor_keys):raise ValueError('missing CPU/RF clock anchor')
   for prefix in ('anchor_before','anchor_after'):
    width=(g[prefix+'_cpu_after']-g[prefix+'_cpu_before'])&0xffffffff
    if width==0 or width>64000:raise ValueError('clock anchor read width')
   mid0=(g['anchor_before_cpu_before']+((g['anchor_before_cpu_after']-g['anchor_before_cpu_before'])&0xffffffff)//2)&0xffffffff
   mid1=(g['anchor_after_cpu_before']+((g['anchor_after_cpu_after']-g['anchor_after_cpu_before'])&0xffffffff)//2)&0xffffffff
   cpu_delta=(mid1-mid0)&0xffffffff
   rf_delta=(g['anchor_after_rf']-g['anchor_before_rf'])&0xffffffff
   if not cpu_delta or not 3.75<rf_delta/cpu_delta<4.05:raise ValueError('CPU/RF clock anchor slope')
  missing+=g['next']-g['prev']-1;part=events[covered:covered+g['count']];covered+=g['count'];span=(g['end']-g['start'])&0xffffffff
  if len(part)!=g['count'] or span>=0x80000000:raise ValueError('gap span/count')
  timing=[];last=0
  for e in part:
   dt=(e['tick']-g['start'])&0xffffffff
   if dt<last or dt>span or not 1<=e['kind']<=11:raise ValueError('event timing')
   last=dt;timing.append(dict(e,offset_us=dt/64))
  decoded.append(dict(g,span_us=span/64,radio_rx_interval_ticks=(g['rf_end']-g['rf_start'])&0xffffffff if g['rf_end'] else None,events=timing,nominal_missing_beacon_offsets_us=[(n-g['prev'])*10000 for n in range(g['prev']+1,min(g['next'],2001))]))
 if covered!=len(events) or missing!=s['missing']:raise ValueError('missing-gap coverage')
 return dict(status='PASS',summary=s,status_bit_counts=bits,event_counts=counts,gaps=decoded,kind_legend={'1':'SYNC-mode RX status poll','2':'CRC-good noncontrol frame, packed len/type/src/dst','3':'RX enable call','4':'RX enable return','5':'force-off call','6':'configure start: 1 SYNC, 0 DATA','7':'configure return','8':'beacon rejected','9':'error-bit clear complete','10':'force-off complete','11':'full status clear complete'},scope='CPU recovery substeps plus bracketing good-beacon DW3000 RX RMARKER intervals; no missing-frame radio timestamp or exact RF-ready instant; bit counters can overlap; startup before first SYNC not traced',initial_missing_untraced=s['initial_missing'])

def assess(bundle,assessment):
 raw=(Path(bundle)/'results/local/N7.log').read_text()
 result=parse(raw,assessment['code_observations']['sync'])
 path=Path(bundle)/'results/BEACON_RX_TRACE.json'
 with path.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
 return {'status':'PASS','sidecar':path.name,'summary':result['summary'],'status_bit_counts':result['status_bit_counts'],'scope':result['scope']}
