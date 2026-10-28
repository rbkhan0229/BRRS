#!/usr/bin/env python3
"""Independent, bounded home interference study. One immutable case per run."""
import argparse,datetime,fcntl,hashlib,json,os,re,shutil,subprocess,sys,time
from pathlib import Path
from types import SimpleNamespace
HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
sys.path[:0]=[str(HERE),str(HERE/'sdk/Drivers/API')]
import interference_victim as victim
import interference_host as host
import beacon_trace
from brrs_suite_case import prepare_resolved,save,sha,now,link
AB_IMAGES={'A': '/Users/songchieon/Desktop/DWM3000/logs/home_beacon_recovery_timing_20260929_005253_v2/controller/sdk/Drivers/API/Build_Platforms/nRF52840-DK/Output/Exp1_Normal/Exe'}
AB_SOURCES={'A': '/Users/songchieon/Desktop/DWM3000/logs/home_beacon_recovery_timing_20260929_005253_v2/controller/sdk/Drivers/API/Src/examples/ex_35b_brrs_normal/brrs_normal.c'}
AB_HASHES={'A': {'hex': '3456cd01b4a3796cc397d37efa59859580266206182a07ceeff716add55fa361', 'elf': '361beb1ea5e759a51832ca4df115b43a057403fe8d0332d5132f3989266e1fd3', 'source': '68acca53ed29074714945de24f7f20f65b95d6a24167e5f98a141a244f5327bd'}}
BASE=Path('/Users/songchieon/Desktop/DWM3000/logs/ubuntu_controlled')
API=HERE/'sdk/Drivers/API'
def need(x,m):
 if not x:raise RuntimeError(m)
def new(p,v):
 with Path(p).open('x') as f:json.dump(v,f,ensure_ascii=False,indent=2);f.write('\n')
def source_pins():
 return {p:sha(HERE/p) for p in ('study.py','interference_host.py','interference_victim.py','aux_capture.py','sniffer_capture.py','sniffer_firmware.c','safe_run_case.sh','safe_recovery.py','link_diagnostic.py','beacon_trace.py','sdk/Drivers/API/Src/examples/ex_35b_brrs_normal/brrs_beacon_trace.h','sdk/Drivers/API/Src/examples/ex_35b_brrs_normal/brrs_normal.c','sdk/Drivers/API/Src/examples/ex_35b_brrs_normal/brrs_holdover.h','sdk/Drivers/API/Src/examples/ex_35a_brrs_init/brrs_init.c','holdover_verify.py')}
def select_arm(phase,preamble):
 mode=1 # holdover ON for all phases; phase now labels AUX state
 for config in (f'Exp1_{preamble}_Init','Exp1_Normal'):
  origin=ROOT/'images'/str(mode)/f'M{preamble}'/config
  target=API/'Build_Platforms/nRF52840-DK/Output'/config/'Exe'
  pins=json.loads((origin/'hashes.json').read_text())
  for ext in ('hex','elf'):
   name='dw3000_api.'+ext
   need(sha(origin/name)==pins[name],'sealed candidate image changed')
   shutil.copy2(origin/name,target/name)
  for stamp in origin.glob('.brrs_*'):shutil.copy2(stamp,target/stamp.name)
 return str(mode)

def prepare(args):
 need(sys.platform=='darwin' and os.environ.get('BRRS_AIR_NATIVE')=='1','Air-native build required')
 if getattr(args,'offline_held_prepare',False):
  need((ROOT/'STOP').exists() and (BASE/'STOP_ALL').exists(),'offline preparation requires root/global STOP')
 else:
  need(not (ROOT/'STOP').exists(),'study STOP')
 arm=select_arm(args.phase,args.preamble)
 aux_mode='OFF' if args.phase.startswith('off') else 'ON'
 cid=f'interference_victim_{ROOT.name}_d{args.data_code}_s{args.sync_code}_l44_ch{args.channel}_m{args.preamble}_{args.phase}_a{args.attempt}'
 r=ROOT/'cases'/cid;r.mkdir(parents=True,exist_ok=False)
 env=json.loads((ROOT/'AUTHORIZATION.json').read_text())['environment']
 need(int(arm)==1,'holdover ON image required')
 env.update(holdover_study=True,holdover_mode=int(arm),phase=args.phase,extra_transmitter_channel=5,extra_transmitter_M=64,extra_transmitter_code=9,
  extra_transmitter_period_us=1009,requested_user_scope='CH5/M64 actual AUX TX timestamp trace; ON one RF; no retry or Standard')
 m=victim.manifest(cid,args.preamble,args.channel,aux_mode.lower(),env,lead_us=44,data_code=args.data_code,sync_code=args.sync_code,code_diagnostic=True)
 new(r/'manifest.json',m)
 c=victim.resolve(m)
 prepare_resolved(SimpleNamespace(manifest=r/'manifest.json',bundle=r/'bundle',reuse=True,source_build=False),m,c)
 b=r/'bundle';c=json.loads((b/'case.json').read_text())
 adir=b/'auxiliary';adir.mkdir()
 source=API/'Build_Platforms/nRF52840-DK/Output/Aux_Finite_TX/Exe/dw3000_api.hex'
 for p in (source,source.with_suffix('.elf')):shutil.copy2(p,adir/p.name)
 elf=adir/'dw3000_api.elf'
 nm=subprocess.check_output([os.environ['ARM_NM'],'-S','--defined-only',str(elf)],text=True)
 syms={line.split()[-1]:{'address':int(line.split()[0],16),'size':int(line.split()[1],16)} for line in nm.splitlines() if len(line.split())==4}
 keys=['aux_host_arm','aux_host_stop','aux_duration_ms','aux_state','aux_tx_count','aux_tx_attempts','aux_error_code','aux_end_reason','aux_elapsed_us','aux_tx_timeout_count','aux_schedule_overrun_count','aux_last_status','aux_rf_off_commanded','aux_tx_timestamp_count']
 need(all(k in syms and syms[k]['size']==4 for k in keys),'AUX RAM symbols missing/bad size')
 need('aux_tx_timestamp_hi32' in syms and syms['aux_tx_timestamp_hi32']['size']==180000,'AUX timestamp array missing/bad size')
 c['auxiliary_job']=dict(physical_role='AUX',serial=victim.SERIALS['AUX'],hex='auxiliary/dw3000_api.hex',
  hex_sha256=sha(adir/'dw3000_api.hex'),elf_sha256=sha(elf),rtt_address=hex(syms['_SEGGER_RTT']['address']),
  symbols={k:syms[k] for k in [*keys,'aux_tx_timestamp_hi32']},timestamp_trace=dict(schema='aux-tx-rmarker-hi32-v1',capacity=45000),arm_magic=0x41524d31,stop_magic=0x53544f50,
  duration_ms=60000,mode=aux_mode,schema='aux-finite-packet-v1',
  stop_paths=[str(ROOT/'STOP'),str(BASE/'STOP_ALL')],power_index=40,nominal_reduction_db_from_index40=0,channel=5,preamble=64,period_us=1009)
 sdir=b/'sniffer';sdir.mkdir()
 snsource=ROOT/'images/N2/dw3000_api.hex'
 snpins=json.loads((ROOT/'images/N2/hashes.json').read_text())
 need(sha(snsource)==snpins['hex'] and sha(snsource.with_suffix('.elf'))==snpins['elf'],'N2 image changed')
 for ext in ('hex','elf'):shutil.copy2(snsource.with_suffix('.'+ext),sdir/('dw3000_api.'+ext))
 snelf=sdir/'dw3000_api.elf'
 snnm=subprocess.check_output([os.environ['ARM_NM'],'-S','--defined-only',str(snelf)],text=True)
 snsyms={line.split()[-1]:{'address':int(line.split()[0],16),'size':int(line.split()[1],16)} for line in snnm.splitlines() if len(line.split())==4}
 snkeys=['sn_host_arm','sn_host_stop','sn_state','sn_error','sn_rf_off','sn_aux_count','sn_beacon_count','sn_window_count','sn_aux_bad','sn_beacon_bad','sn_rx_errors','sn_spi_errors','sn_initial_anchor','sn_last_anchor','sn_first_aux_seq','sn_last_aux_seq']
 snarrays={'sn_aux_records':168000,'sn_beacon_records':768,'sn_window_start':128,'sn_window_end':128}
 need(all(k in snsyms and snsyms[k]['size']==4 for k in snkeys),'N2 scalar symbols missing')
 need(all(k in snsyms and snsyms[k]['size']==size for k,size in snarrays.items()),'N2 array symbols missing')
 c['sniffer_job']=dict(physical_role='N2',serial=victim.SERIALS['N2'],hex='sniffer/dw3000_api.hex',elf='sniffer/dw3000_api.elf',
  hex_sha256=sha(sdir/'dw3000_api.hex'),elf_sha256=sha(snelf),rtt_address=hex(snsyms['_SEGGER_RTT']['address']),
  symbols={k:snsyms[k] for k in [*snkeys,*snarrays]},arm_magic=0x534e4131,stop_magic=0x534e5354,
  schema='passive-timebase-v1',stop_paths=[str(ROOT/'STOP'),str(BASE/'STOP_ALL')],mode='PASSIVE',tx_allowed=False)
 save(b/'case.json',c)
 for name in source_pins():
  target=b/'provenance'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(HERE/name,target)
 shutil.copy2(API/'Src/examples/ex_34_INTERFERENCE/interference_nonhop.c',b/'provenance/aux_firmware.c')
 shutil.copy2(HERE/'sniffer_firmware.c',b/'provenance/sniffer_firmware.c')
 shutil.copy2(API/'Src/example_selection.h',b/'provenance/example_selection.h')
 shutil.copy2(ROOT/'BASE_SOURCE_INDEX.json',b/'provenance/BASE_SOURCE_INDEX.json')
 new(b/'provenance/STUDY_SOURCE_PINS.json',source_pins())
 hashes={str(p.relative_to(b)):sha(p) for p in sorted(b.rglob('*')) if p.is_file() and p.name!='payload_hashes.json'}
 save(b/'payload_hashes.json',hashes)
 index=sha(b/'payload_hashes.json');victim.validate_bundle(b,index)
 image={j['physical_role']:{'hex':j['hex_sha256'],'elf':j['elf_sha256']} for j in c['jobs']}
 image['AUX']={'hex':c['auxiliary_job']['hex_sha256'],'elf':c['auxiliary_job']['elf_sha256']}
 image['N2']={'hex':c['sniffer_job']['hex_sha256'],'elf':c['sniffer_job']['elf_sha256']}
 group=ROOT/f'IMAGES_CH{args.channel}_M{args.preamble}_D{args.data_code}_S{args.sync_code}_{arm}.json'
 if not group.exists():new(group,image)
 else:need(json.loads(group.read_text())==image,'OFF/ON/OFF firmware differs')
 new(r/'prepared.json',dict(case_id=cid,payload_index_sha256=index,phase=args.phase,source_pins=source_pins(),sealed_at=now(),rf_started=False))
 print(json.dumps({'prepared':str(r),'case_id':cid,'payload_index_sha256':index}),flush=True)
 return r

def hardware_audit(c):
 out={}
 for role,row in c['boards'].items():
  j=link(row['serial'])
  try:out[role]=dict(serial=row['serial'],halted=bool(j.halted()),millivolts=int(j.hardware_status.VTarget))
  finally:j.close()
 need(all(x['halted'] and 3000<=x['millivolts']<=3600 for x in out.values()),'eight-board halt/voltage audit')
 return out

def c_mode(b):
 return json.loads((b/'case.json').read_text())['auxiliary_job']['mode']

def assess(b):
 a=victim.assess_diagnostic(b)
 a['beacon_rx_trace']=beacon_trace.assess(b,a)
 import holdover_verify
 a['holdover']=holdover_verify.assess(b,a)
 s=json.loads((b/'results/status.json').read_text());aux=s['auxiliary']
 need(aux.get('status')=='PASS','AUX invalid')
 need(s.get('sniffer',{}).get('status')=='PASS','passive N2 invalid')
 a['passive_n2']=s['sniffer']
 rows=[json.loads(line) for line in (b/'results/AUX.progress.jsonl').read_text().splitlines()]
 if c_mode(b)=='ON':
  start=datetime.datetime.fromisoformat(s['workers']['init']['started_at']).timestamp()
  end=datetime.datetime.fromisoformat(s['victim_capture_finished_at']).timestamp()
  samples=[(datetime.datetime.fromisoformat(x['at']).timestamp(),x['aux_tx_count']) for x in rows if x['aux_state']==2]
  need(len(samples)>=10 and samples[0][0]<=start and samples[-1][0]>=end-1.0,'AUX active-time coverage missing')
  within=[(t,n) for t,n in samples if start-1<=t<=end]
  need(len(within)>=10 and all(0<b[0]-a[0]<=2.0 and b[1]>a[1] for a,b in zip(within,within[1:])),'AUX did not transmit continuously across victim capture')
  a['auxiliary_overlap']=dict(status='PASS',first_tx_before_init=True,samples=len(within),observed_tx_in_window=within[-1][1]-within[0][1],capture_start=s['workers']['init']['started_at'],capture_end=s['victim_capture_finished_at'],scope='host sampled TXFRS counters, not per-victim-packet collision proof')
 else:
  need(rows and all(x['aux_tx_count']==0 for x in rows),'OFF AUX transmitted')
  a['auxiliary_overlap']=dict(status='PASS',all_sampled_tx_zero=True)
 a['auxiliary']=aux;a['interference_source_identification']='controlled DWM3000 packet source only, not vehicle digital-key identification'
 return a

def official_pass(a):
 return isinstance(a,dict) and a.get("verdict")=="PASS" and type(a.get("worst_node_per_percent")) in (int,float) and 0<=a["worst_node_per_percent"]<1

def stress_valid(a):
 if not isinstance(a,dict) or a.get('verdict') not in ('PASS','FAIL_PER'):return False
 worst=a.get('worst_node_per_percent')
 return type(worst) in (int,float) and 0<=worst<=5

def run(r):
 r=Path(r).resolve();need(r.parent==ROOT/'cases','case outside study')
 need(json.loads((ROOT/'AUTHORIZATION.json').read_text()).get('rf_authorized') is True,'preparation-only authorization: RF held')
 b=r/'bundle';spec=json.loads((r/'prepared.json').read_text())
 need(source_pins()==spec['source_pins'],'runner changed since case seal')
 c,_=victim.validate_bundle(b,spec['payload_index_sha256'])
 need(not any((r/p).exists() for p in ('STOP','started.json','finished.json')),'case used/stopped')
 need(not (ROOT/'STOP').exists() and not (b/'STOP').exists() and not (b/'results').exists(),'stop/used bundle')
 group_prefix=f"interference_victim_ch{c['conditions']['uwb_channel']}_m{c['conditions']['preamble']}_"
 sequence=json.loads((ROOT/'SEQUENCE.json').read_text())['cases']
 need(spec['case_id'] in sequence,'case absent from sequence')
 idx=sequence.index(spec['case_id'])
 need(all(not (ROOT/'cases'/q/'started.json').exists() for q in sequence[idx+1:]),'future case already started')
 if spec['phase']=='on_a' and idx==0:
  need(json.loads((ROOT/'AUTHORIZATION.json').read_text()).get('timestamp_only_on') is True,'ON-only requires explicit timestamp study scope')
 if idx:
  prev=ROOT/'cases'/sequence[idx-1]
  need((prev/'finished.json').exists(),'previous case incomplete')
  old=json.loads((prev/'finished.json').read_text())
  need(old['return_code']==0 and old['status']=='COLLECTION_AND_READBACK_PASS' and old['rf_runs_started']==1,'previous case system invalid')
  if spec['phase']=='on_a' and not json.loads((ROOT/'AUTHORIZATION.json').read_text()).get('timestamp_only_on',False):
   need(official_pass(old.get('assessment')),'preceding OFF baseline not official PASS <1%')
   pc=json.loads((prev/'bundle/case.json').read_text())['conditions']
   cc=c['conditions']
   need(all(pc.get(k)==cc.get(k) for k in ('uwb_channel','preamble','rx_pac','lead_us','data_preamble_code','sync_preamble_code')),'OFF baseline PHY/lead mismatch')
   need(pc.get('aux_state')=='off','preceding baseline AUX not OFF')
 with open(ROOT/'study.lock','a') as study_lock:
  fcntl.flock(study_lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
  with open(host.LOCK_PATH,'a') as hw:
   fcntl.flock(hw,fcntl.LOCK_EX|fcntl.LOCK_NB)
   info=host.preflight(b,c)
   ps=subprocess.check_output(['ps','-axo','pid=,args='],text=True)
   need(not any('aux_capture.py' in x or 'sniffer_capture.py' in x for x in ps.splitlines()),'existing AUX collector')
   need(not host.park_all(c),'pre-run parking failed')
   new(r/'PRE_RUN_AUDIT.json',dict(at=now(),preflight=info,boards=hardware_audit(c)))
   active=BASE/'ACTIVE.json';stop=BASE/'STOP_ALL'
   prior=json.loads(active.read_text());prior_bundle=Path(prior['bundle'])
   need((prior_bundle/'STOP').exists(),'previous owner is not closed')
   if stop.exists():
    auth=datetime.datetime.fromisoformat(json.loads((ROOT/'AUTHORIZATION.json').read_text())['at']).timestamp()
    need(stop.stat().st_mtime<auth,'new global STOP after authorization; cannot consume')
    archive=BASE/('STOP_ALL.reviewed.'+ROOT.name)
    need(not archive.exists(),'STOP archive already exists')
    new(ROOT/'STOP_TRANSITION.json',dict(at=now(),authorization='AUTHORIZATION.json',previous_owner=prior,
      previous_stop_sha256=sha(stop),previous_case_stop_preserved=True,new_study=str(ROOT)))
    shutil.copy2(active,ROOT/'PREVIOUS_ACTIVE.json');stop.rename(archive)
   elif not (ROOT/'STOP_TRANSITION.json').exists():raise RuntimeError('global STOP absent without study transition')
   new(r/'started.json',dict(at=now(),case_id=c['id'],max_rf_runs=1))
   save(active,dict(bundle=str(b),case_id=c['id'],study=str(ROOT)))
  try:
   rc=host.run(b,c,spec['payload_index_sha256'],assessor=assess)
  finally:
   (b/'STOP').touch();(r/'STOP').touch()
   with open(host.LOCK_PATH,'a') as hw:
    fcntl.flock(hw,fcntl.LOCK_EX|fcntl.LOCK_NB)
    errors=host.park_all(c)
    try:audit=hardware_audit(c)
    except BaseException as ex:audit={'error':repr(ex)};errors['audit']=repr(ex)
    new(r/'FINAL_AUDIT.json',dict(at=now(),halt_errors=errors,boards=audit))
    if errors:(ROOT/'STOP').touch();(BASE/'STOP_ALL').touch()
  state=json.loads((b/'results/status.json').read_text())
  assessment=json.loads((b/'results/ASSESSMENT.json').read_text()) if (b/'results/ASSESSMENT.json').exists() else None
  new(r/'finished.json',dict(at=now(),return_code=rc,status=state['status'],rf_runs_started=state['rf_runs_started'],assessment=assessment))
  if rc or errors or not stress_valid(assessment):
   (ROOT/'STOP').touch();(BASE/'STOP_ALL').touch()
  if rc==0 and not errors and spec['phase']=='off_a' and official_pass(assessment):
   new(ROOT/f"BASELINE_CH{c['conditions']['uwb_channel']}_M{c['conditions']['preamble']}_D{c['conditions']['data_preamble_code']}_S{c['conditions']['sync_preamble_code']}.json",dict(case_id=c['id'],assessment=str(b/'results/ASSESSMENT.json')))
  print(json.dumps({'case_id':c['id'],'status':state['status'],'assessment':assessment,'case_root':str(r)}),flush=True)
  return rc

def main():
 ap=argparse.ArgumentParser();ap.add_argument('action',choices=['prepare','run','finish']);ap.add_argument('--attempt',type=int,default=1);ap.add_argument('--preamble',type=int);ap.add_argument('--channel',type=int);ap.add_argument('--phase',choices=['off_a','on_a','on_b','off_b']);ap.add_argument('--case-root',type=Path);ap.add_argument('--data-code',type=int,choices=range(9,13),default=9);ap.add_argument('--sync-code',type=int,choices=range(9,13),default=10);ap.add_argument('--offline-held-prepare',action='store_true');a=ap.parse_args()
 if a.offline_held_prepare and a.action!='prepare':ap.error('offline-held-prepare is preparation only')
 if a.action=='prepare':prepare(a)
 elif a.action=='run':return run(a.case_root)
 else:
  (ROOT/'STOP').touch();(BASE/'STOP_ALL').touch();print('STUDY STOP LATCHED')
if __name__=='__main__':
 try:sys.exit(main() or 0)
 except BaseException as e:
  if not isinstance(e,SystemExit):
   new(ROOT/('failure_'+str(time.time_ns())+'.json'),dict(at=now(),error=repr(e),argv=sys.argv));(ROOT/'STOP').touch();(BASE/'STOP_ALL').touch()
  raise
