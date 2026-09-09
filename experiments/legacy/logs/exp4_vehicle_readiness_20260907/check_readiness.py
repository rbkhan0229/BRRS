import pathlib,hashlib,json,subprocess,sys,datetime,platform
base=pathlib.Path('/Users/songchieon/Desktop/DWM3000');logs=base/'logs';root=logs/'exp4_home_s6_multislot_pac_ab_20260907'
pre=json.loads(subprocess.check_output([sys.executable,str(root/'preflight.py')],text=True))
out={'checked_at':datetime.datetime.now().astimezone().isoformat(),'host':platform.node(),'preflight':pre,'candidates':{},'scripts':{}}
for campaign,variant in [('exp4_home_s6_multislot_pac_ab_20260907','R13P4'),('exp4_home_s6_multislot_pac_ab_20260907','R13P8'),('exp4_home_s6_sfd64_20260907','R13P4S64')]:
 folder=logs/campaign;m=json.loads((folder/'manifest.json').read_text());checks={}
 for role,rec in m['variants'][variant].items():
  p=folder/rec['hex'];actual=hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None
  checks[role]={'serial':rec['serial'],'path':str(p),'sha256':actual,'matches_manifest':actual==rec['sha256']}
 cap=pathlib.Path(m['capture_script']);out['candidates'][variant]={'images':checks,'parameters':m['parameters_by_variant'][variant],'collector_hash_matches':hashlib.sha256(cap.read_bytes()).hexdigest()==m['capture_sha256']}
 for name in ['run_side.py','orchestrate.py','audit_run.py','verify_flash.py']:
  p=folder/name;out['scripts'][campaign+'/'+name]={'exists':p.exists(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None}
out['all_images_and_collectors_ok']=all(c['collector_hash_matches'] and all(x['matches_manifest'] for x in c['images'].values()) for c in out['candidates'].values())
print(json.dumps(out,ensure_ascii=False,indent=2))
