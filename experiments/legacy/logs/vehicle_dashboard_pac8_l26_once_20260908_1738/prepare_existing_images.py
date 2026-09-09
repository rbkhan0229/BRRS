"""New vehicle metadata and fixed roles; reuse the exact successful PHY images."""
from pathlib import Path
import sys, json, shutil, subprocess, os, re
from datetime import datetime
R=Path(__file__).resolve().parent
BASE=R.parent.parent
API=BASE/'DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API'
sys.path.insert(0,str(API))
from brrs_suite_case import checked, save, sha, NM
from brrs_suite_manifest import load, plan
old=BASE/'logs/vehicle_glovebox_pac8_l26_once_20260908/capture1'
prior=checked(old)
m=json.loads((BASE/'logs/vehicle_onepass_run_20260908_0039/vehicle_manifest_TEMPLATE.json').read_text())
m['environment']='VEHICLE_DASHBOARD_20260908_1738'
m['boards']['init']['location']='dashboard_top'
m['boards']['N2']['location']='front_bumper_left_or_A_outside_plastic'
m['boards']['N3']['location']='front_bumper_right_or_B_outside_plastic'
m['lead_selection']={'frozen':True,'lead_us_by_pac':{'4':27,'8':26},'evidence':{
 'purpose':'one vehicle check, inherited NLOS screening candidate; not vehicle optimum',
 'PAC8':str(old),'PAC4':'not used in this run','paper_qualified':False}}
m['exp4']['preambles']=[32]
m['exp4']['slot_counts_by_preamble']={'32':[13]}
m['exp4'].pop('capacity_search',None)
m['setup_record']={'vehicle_model': 'Hyundai Kona, latest model per user; exact model year/trim not specified', 'vehicle_type': 'internal combustion per user context', 'actual_rx_location': 'dashboard top, user confirmed', 'bumper_installation': 'N2/N3 on front bumper left/right outside plastic; nRF board + DWM3000 mounted to plastic', 'installation_mapping': 'fixed prior physical roles', 'engine_state': 'awaiting current confirmation; previous run on', 'hub_supply': 'awaiting current confirmation; previous run external power', 'doors': 'awaiting current confirmation; previous run all closed', 'physical_changes_by_agent': False, 'distance_m': None, 'scope': 'one RF run, beacon M256, DATA M32/PAC8 lead26, S6 K13 1000SF', 'comparison': 'same seven HEX as glovebox run; RX position changed; other state confirmation pending', 'capture_timeout_seconds': 60}
m['setup_record']['recorded_at']=datetime.now().astimezone().isoformat()
m.pop('lead_candidates_us_by_pac',None)
save(R/'manifest.json',m)
m=load(R/'manifest.json')
c=next(c for c in plan(m,'exp4') if c['id']=='exp4_m32_pac8_l26_k13')
root=R/'capture1';root.mkdir(exist_ok=False)
runtime=root/'sdk/Drivers/API';runtime.mkdir(parents=True)
for path in API.iterdir():
 if path.is_file() and path.suffix in ['.py','.sh','.json','.md']:shutil.copy2(path,runtime/path.name)
shutil.copy2(R/'manifest.json',root/'board_manifest.json')
project=Path('Build_Platforms/nRF52840-DK/dw3000_api.emProject')
(runtime/project).parent.mkdir(parents=True,exist_ok=True);shutil.copy2(API/project,runtime/project)
sources=prior['firmware_source_sha256']
for p,h in sources.items():
 assert sha(API/p)==h,p
 target=root/'provenance'/p;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(API/p,target)
c.update(boards=m['boards'],prepared_at=datetime.now().astimezone().isoformat(),source_api=str(API),
 manifest_file_sha256=sha(R/'manifest.json'),firmware_source_sha256=sources,runtime_schema_version=2,
 deployment='Exact archived successful lead26 images; fresh vehicle metadata, original fixed roles; no build',
 image_origin_bundle=str(old),image_origin_payload_index_sha256=sha(old/'payload_hashes.json'))
proof=[]
for j in c['jobs']:
 prev=next(v for v in prior['jobs'] if v['logical_node']==j['logical_node'])
 for source in [(old/prev['hex']), (old/prev['hex']).with_suffix('.elf'),*(old/prev['hex']).parent.glob('.brrs_*')]:
  target=root/source.relative_to(old);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
 j.update(hex=prev['hex'],hex_sha256=prev['hex_sha256'],elf_sha256=prev['elf_sha256'],rtt_address=prev['rtt_address'],
          script=Path(j['argv'][1]).name,args=j['argv'][2:]+['--no-build','--timeout','60'])
 assert sha(root/j['hex'])==j['hex_sha256']
 assert sha((root/j['hex']).with_suffix('.elf'))==j['elf_sha256']
 args=['bash',str(runtime/j['script']),*j['argv'][2:],'--no-build','--build-only']
 result=subprocess.run(args,env=dict(os.environ,**j['environment'],ARM_NM=NM,PYTHONDONTWRITEBYTECODE='1'),text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 (root/(j['physical_role']+'.prepare.log')).write_text(result.stdout)
 if result.returncode:raise RuntimeError(j['physical_role']+' image validation failed: '+result.stdout[-2000:])
 assert '[build-only] verified image ' in result.stdout
 proof.append({'physical_role':j['physical_role'],'logical_node':j['logical_node'],'serial':j['serial'],
               'hex_sha256':j['hex_sha256'],'same_as_prior_logical_role':True,'build_only_validation_rc':result.returncode})
save(root/'case.json',c)
save(root/'payload_hashes.json',{str(p.relative_to(root)):sha(p) for p in sorted(root.rglob('*')) if p.is_file()})
checked(root)
save(R/'image_reuse_proof.json',proof)
print(json.dumps({'bundle':str(root),'conditions':c['conditions'],'images':proof,'rf_performed':False},indent=2))
