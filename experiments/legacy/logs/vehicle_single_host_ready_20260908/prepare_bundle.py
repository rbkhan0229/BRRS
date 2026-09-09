import hashlib,json,shutil,sys
from pathlib import Path
from datetime import datetime
R=Path(__file__).resolve().parent;B=R/'capture1'
API=R.parent.parent/'DW3_QM33_SDK_1.0.2_vehicle_beacon512_20260908/Drivers/API'
sys.path.insert(0,str(API))
from brrs_suite_case import checked,save,sha
from brrs_suite_manifest import plan
P=R.parent/'vehicle_rearbumper_n2_dashboard_n3_console_rx_b512_once_20260908/capture1'
prior=checked(P);B.mkdir(exist_ok=False)
for rel in json.loads((P/'payload_hashes.json').read_text()):
 if rel in ['case.json','board_manifest.json']:continue
 dest=B/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(P/rel,dest)
shutil.copy2(API/'brrs_single_host.py',B/'sdk/Drivers/API/brrs_single_host.py')
m=json.loads((P/'board_manifest.json').read_text())
m['environment']='VEHICLE_SINGLE_HOST_READY_20260908'
for b in m['boards'].values():b['host']='s-macbook-air'
m['setup_record'].update(engine_state='not reconfirmed after packing; do not inherit engine-on',
 hub_supply='external power previously confirmed; power after engine-off requires reconfirmation',
 doors='not reconfirmed after packing',recorded_at=datetime.now().astimezone().isoformat(),
 scope='single-host preparation; no new RF run performed',user_confirmation='user authorized all-seven capture on MacBook Air while packing',
 capture_timeout_seconds=120,physical_positions='last reported positions retained; packing changes not yet confirmed',
 capture_host='s-macbook-air',rx_usb_connection='requested move to MacBook Air; enumeration pending',
 collection_policy='six TX READY before RX, detached one-shot, retain failed peers, halt/readback all seven at completion')
save(R/'manifest.json',m);save(B/'board_manifest.json',m)
c=next(x for x in plan(m,'exp4') if x['id']=='exp4_m32_pac8_l26_k13')
c['conditions'].update(beacon_preamble_symbols=512,beacon_pac=8)
c['conditions_sha256']=hashlib.sha256(json.dumps(c['conditions'],sort_keys=True).encode()).hexdigest()
c.update(boards=m['boards'],prepared_at=datetime.now().astimezone().isoformat(),source_api=str(API),
 manifest_file_sha256=sha(B/'board_manifest.json'),firmware_source_sha256=prior['firmware_source_sha256'],
 runtime_schema_version=2,deployment='single-host Exp4 detached runner; exact existing seven images; no firmware build',
 image_origin_bundle=str(P),execution={'mode':'single_host','host':'s-macbook-air','rf_retries':0,'supports':['exp4'],
 'runner':'sdk/Drivers/API/brrs_single_host.py','requires_explicit_start':True})
for j in c['jobs']:
 old=next(x for x in prior['jobs'] if x['serial']==j['serial'])
 for key in ['hex','hex_sha256','elf_sha256','rtt_address','script']:j[key]=old[key]
 j['args']=j['argv'][2:]+['--no-build','--timeout','120']
 j['environment']['BRRS_SUITE_CONDITIONS_SHA256']=c['conditions_sha256']
save(B/'case.json',c)
index={str(p.relative_to(B)):sha(p) for p in sorted(B.rglob('*')) if p.is_file()}
save(B/'payload_hashes.json',index);checked(B)
save(R/'deployment.json',{'bundle':str(B),'index_sha256':sha(B/'payload_hashes.json'),'host':'s-macbook-air','rf_performed':False,
 'hex_hashes':{j['physical_role']:j['hex_sha256'] for j in c['jobs']}})
print(json.dumps({'bundle':str(B),'index_sha256':sha(B/'payload_hashes.json'),'files':len(index),'rf_performed':False}))
