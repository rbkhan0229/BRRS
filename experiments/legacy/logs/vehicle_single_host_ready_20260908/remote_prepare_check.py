"""Validate deployment without RF, hold idle sleep for four hours, inspect probes."""
import json,subprocess,sys,os
from pathlib import Path
from datetime import datetime
R=Path(__file__).resolve().parent;B=R/'capture1';API=B/'sdk/Drivers/API'
sys.path.insert(0,str(API))
from brrs_single_host import load_bundle,preflight
from brrs_suite_case import sha,NM,save
c=load_bundle(B,json.loads((R/'deployment.json').read_text())['index_sha256'])
out={'at':datetime.now().astimezone().isoformat(),'rf_started':False,'build_checks':{}}
for j in c['jobs']:
 cmd=['bash',str(API/j['script']),*j['args'],'--build-only']
 p=subprocess.run(cmd,env=dict(os.environ,**j['environment'],ARM_NM=NM,PYTHONDONTWRITEBYTECODE='1'),text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
 (R/(j['physical_role']+'.image-check.log')).write_text(p.stdout)
 out['build_checks'][j['physical_role']]={'rc':p.returncode,'no_board_access_marker':'[build-only] verified image' in p.stdout,'hex_sha256':j['hex_sha256']}
 if p.returncode or '[build-only] verified image' not in p.stdout:raise RuntimeError('image validation failed: '+j['physical_role'])
out['detached_proof']=json.loads((B/'transport-test-done.json').read_text())
with (R/'idle_sleep_guard.json').open('x') as f:
 with (R/'idle_sleep_guard.log').open('x') as log:
  p=subprocess.Popen(['/usr/bin/caffeinate','-i','-t','14400'],stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
 record={'pid':p.pid,'duration_seconds':14400,'started_at':datetime.now().astimezone().isoformat(),'kind':'temporary idle sleep assertion; lid must stay open'};json.dump(record,f,indent=2)
out['idle_sleep_guard']=record
try:out['hardware']=preflight(B,c)
except Exception as exc:out['hardware_error']=repr(exc)
out['battery']=subprocess.check_output(['pmset','-g','batt'],text=True)
out['assertions']=subprocess.check_output(['pmset','-g','assertions'],text=True)
save(R/'preparation_check.json',out);print(json.dumps(out,indent=2))
