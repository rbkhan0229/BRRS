"""Exercise real capture/verification/metadata scripts with a fake RTT source.

All fixtures live under this test directory. No J-Link package is imported by
the fake collector; historical logs are explicitly marked as MOCK REPLAY.
"""
from pathlib import Path
import copy, hashlib, json, os, shutil, subprocess, sys
BASE=Path('/Users/songchieon/Desktop/DWM3000');ROOT=Path(__file__).resolve().parent
API=BASE/'DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API'
sys.path.insert(0,str(API))
import brrs_suite_manifest as manifest

sdk=ROOT/'mock_sdk';api=sdk/'Drivers/API';api.mkdir(parents=True,exist_ok=False)
for name in ['brrs_exp2_capture_v3.sh','brrs_exp2_capture.sh','brrs_exp4_capture.sh','brrs_exp4_verify.py']:
    shutil.copy2(API/name,api/name)
project='Build_Platforms/nRF52840-DK'
(api/project).mkdir(parents=True)
shutil.copy2(API/project/'dw3000_api.emProject',api/project/'dw3000_api.emProject')
for cfg in ['Exp2_32_Init','Exp2_Normal']:
    shutil.copytree(API/project/'Output'/cfg/'Exe',api/project/'Output'/cfg/'Exe')
(api/'rtt_capture.py').write_text('''import argparse,json,os,pathlib
p=argparse.ArgumentParser();p.add_argument('--out');p.add_argument('--serial');a,rest=p.parse_known_args()
text=pathlib.Path(os.environ['TEST_FIXTURE_FILE']).read_text()
pathlib.Path(a.out).write_text('MOCK REPLAY — NO RADIO MEASUREMENT\\n'+text)
pathlib.Path(a.out+'.mock_args.json').write_text(json.dumps({'serial':a.serial,'other_args':rest,'hardware_access':False}))
print('[MOCK RTT] no board access')
''')
m=manifest.load(API/'brrs_vehicle_manifest.json')
m['lead_selection']={'frozen':True,'lead_us_by_pac':{'4':17,'8':25},'evidence':'TEST ONLY'}
case=manifest.plan(m,'exp2')[0];results=[]
envbase={**os.environ,'ARM_NM':'/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin/nm'}
for job in case['jobs']:
    role='rx' if job['physical_role']=='init' else 'tx'
    source=BASE/f'logs/exp2_nlos_6.9m_20260823/exp2_32_l15_r1_{role}.log'
    raw=source.read_text().replace('experiment=2,plen=32,lead_us=15,','experiment=2,plen=32,lead_us=17,')
    if role=='rx':raw='EXP2_PHY_CONFIG_CSV,plen=32,pac=4,sfd_timeout=37,lead_us=17\n'+raw
    fixture=ROOT/f'mock_exp2_{role}.txt';fixture.write_text(raw)
    argv=job['argv'].copy();argv[1]=str(api/Path(argv[1]).name);argv[argv.index('vehicle_preparation')]='mock_exp2_'+role;argv+=['--no-build']
    p=subprocess.run(argv,capture_output=True,text=True,env={**envbase,**job['environment'],'TEST_FIXTURE_FILE':str(fixture)})
    (ROOT/f'mock_exp2_{role}.console.log').write_text(p.stdout+p.stderr)
    assert p.returncode==0,p.stdout+p.stderr
    metas=list((ROOT/'logs').glob(f'exp2_mock_exp2_{role}_*/*.meta.txt'));assert len(metas)==1
    values=dict(line.split('=',1) for line in metas[0].read_text().splitlines() if '=' in line)
    for key,want in {'serial':job['serial'],'pac':'4','lead_us':'17','physical_role':job['physical_role'],'logical_node':str(job['logical_node']),'suite_conditions_sha256':case['conditions_sha256']}.items():assert values[key]==want,(key,values)
    results.append({'role':role,'rc':p.returncode,'metadata':str(metas[0]),'serial':values['serial'],'physical_role':values['physical_role'],'logical_node':values['logical_node'],'pac':values['pac'],'lead':values['lead_us'],'mock_only':True})

# A mismatched runtime PAC must fail even when the cached image stamp agrees.
fixture=ROOT/'mock_exp2_bad_pac.txt';fixture.write_text((ROOT/'mock_exp2_rx.txt').read_text().replace('pac=4,sfd_timeout=37','pac=8,sfd_timeout=33'))
job=case['jobs'][0];argv=job['argv'].copy();argv[1]=str(api/Path(argv[1]).name);argv[argv.index('vehicle_preparation')]='mock_bad_pac';argv+=['--no-build']
p=subprocess.run(argv,capture_output=True,text=True,env={**envbase,**job['environment'],'TEST_FIXTURE_FILE':str(fixture)})
(ROOT/'mock_bad_pac.console.log').write_text(p.stdout+p.stderr)
assert p.returncode!=0 and 'PHY/PAC/lead mismatch' in p.stdout+p.stderr
results.append({'test':'runtime_PAC_mismatch','rejected':True,'mock_only':True})
(ROOT/'capture_metadata_checks.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
