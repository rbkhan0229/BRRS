import datetime,json,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parent
order=json.loads((root/sys.argv[1]).read_text())
for variant,num in order:
 tag=f'{variant}_r{num}';print(f'START {tag}',flush=True)
 record={'tag':tag,'started_at':datetime.datetime.now().astimezone().isoformat()}
 with (root/(tag+'_workflow.log')).open('x') as f:
  capture=subprocess.run([sys.executable,'-u',str(root/'orchestrate.py'),variant,str(num)],stdout=f,stderr=subprocess.STDOUT)
  record['capture_exit']=capture.returncode
  if capture.returncode==0:
   audit=subprocess.run([sys.executable,str(root/'audit_run.py'),variant,str(num)],stdout=f,stderr=subprocess.STDOUT);record['audit_exit']=audit.returncode
 record['finished_at']=datetime.datetime.now().astimezone().isoformat()
 ap=root/'actual_order.json';actual=json.loads(ap.read_text()) if ap.exists() else [];actual.append(record);ap.write_text(json.dumps(actual,indent=2)+'\n')
 auditpath=root/tag/'audit.json'
 if auditpath.exists():
  a=json.loads(auditpath.read_text());print(json.dumps({'tag':tag,'valid':a['valid'],'goal':a['goal'],'nodes':a.get('nodes'),'errors':a.get('errors'),'full_tx':a.get('full_tx'),'failures':a.get('failures')},ensure_ascii=False),flush=True)
 if record.get('audit_exit',1)!=0:raise SystemExit('STOP: collection/system audit did not pass; preserve and inspect before more RF')
print('PLANNED BLOCK COMPLETE',flush=True)
