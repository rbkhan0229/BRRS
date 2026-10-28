import json,re
from pathlib import Path

def eligible_prediction_checks(real_sequences):
 n=last=checks=0
 for seq in real_sequences:
  if last:
   if n>=8:checks+=1
   if seq==last+1:n=min(n+1,8)
   else:n=0
  last=seq
 return checks

def stress_counter_consistent(h,eligible):
 blocked=h.get('blocked');pred=h.get('predicted_success')
 checks=h.get('prediction_checks');scheduled=h.get('rx_scheduled')
 return (eligible>=1000 and all(type(x) is int for x in (blocked,pred,checks,scheduled))
         and 0<=blocked<=pred and eligible-blocked<=checks<=eligible
         and 1000<=scheduled<=2000 and abs(scheduled-eligible)<=blocked)

def assess(b,a):
 b=Path(b);c=json.loads((b/'case.json').read_text());mode=c['conditions']['holdover_mode']
 def rows(role,prefix):
  return [dict((k,int(v)) for k,v in re.findall(r'(\w+)=([0-9]+)',x)) for x in (b/'results/local'/f'{role}.log').read_text().splitlines() if x.startswith(prefix+',')]
 def need(x,m):
  if not x:raise ValueError('holdover: '+m)
 def one(role,p):
  r=rows(role,p);need(len(r)==1,p+' cardinality');return r[0]
 h=one('N7','BRRS_HO_SUMMARY');init=one('init','BRRS_HO_INIT');end=one('N7','BRRS_HO_END');r=rows('N7','BRRS_HO_ROW')
 need(h['mode']==mode and end=={'version':1},'mode/END')
 need(init['period_us']==10000 and init['count']==1999 and init['late']==0,'fixed INIT schedule')
 need(abs(init['min_ticks']-2496000)<=4 and abs(init['max_ticks']-2496000)<=4,'actual INIT period')
 need(h['system_errors']==h['rx_late']==h['prediction_bad']==0,'system/late/prediction error')
 eligible=eligible_prediction_checks([x['seq'] for x in r if x['kind']==0])
 need(stress_counter_consistent(h,eligible),'prediction coverage/count mismatch')
 need(h['max_error_ticks']<=125,'prediction error exceeds 125 ticks')
 need(h['rows']==len(r) and len({x['seq'] for x in r})==len(r),'row count or duplicate TX schedule')
 need(all(1<=x['seq']<=2000 and x['kind'] in (0,1) for x in r),'row scope')
 real=[x for x in r if x['kind']==0];pred=[x for x in r if x['kind']==1]
 need(len(real)==h['accepted']==a['code_observations']['sync']['unique'],'received beacon accounting')
 need(len(pred)==h['predicted_attempts']==h['predicted_success'],'predicted TX accounting')
 need(all(x['actual']==0 for x in pred),'predicted timestamp presented as observed')
 actualseq={x['seq'] for x in real}
 need(all(x['seq']-1 in actualseq for x in pred),'more than one consecutive extrapolation')
 # scheduled receive count was bounded against sequence eligibility above
 errors=[abs(((x['actual']-x['predicted']+2**31)%2**32)-2**31) for x in real if x['predicted']]
 need(errors and max(errors)==h['max_error_ticks'] and max(errors)<=125,'raw timestamp prediction reconstruction')
 timing=[x.split(',') for x in (b/'results/local/init.log').read_text().splitlines() if x.startswith('BRRS_SLOT_TIMING_CSV,N2,')]
 need(len(timing)==1 and timing[0][-1]=='PASS','actual DATA slot timing absent')
 need(max(abs(int(timing[0][3])),abs(int(timing[0][4])))<=1000,'DATA slot error exceeds 1us')
 need(all(all(x['seq']-k in actualseq for k in range(1,10)) for x in pred),'insufficient real-beacon training before prediction')
 if mode==0:need(not pred and h['injected']==0,'shadow transmitted prediction')
 elif mode==2:
  need(h['injected']==4 and {x['seq'] for x in pred}=={100,200},'single/double/startup injection expectations')
  need(3 not in actualseq and 201 not in actualseq and h['blocked']>=1,'initial/double loss not blocked')
 else:need(h['injected']==0,'unexpected injected loss')
 out={'status':'PASS','mode':mode,'summary':h,'init':init,'predicted_sequences':[x['seq'] for x in pred],
      'max_prediction_error_us':h['max_error_ticks']/249.6,'scope':'isolated fixed single-link schedule; software rejection is not on-air beacon loss; prediction confidence is empirical, not guaranteed under changed clocks/environment'}
 with (b/'results/HOLDOVER_VALIDATION.json').open('x') as f:json.dump(out,f,indent=2)
 return out
