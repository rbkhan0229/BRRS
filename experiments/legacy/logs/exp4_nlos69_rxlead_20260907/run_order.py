import json,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parent
order=json.loads((root/(sys.argv[1] if len(sys.argv)>1 else 'screen_order.json')).read_text())
for variant,run in order:
    subprocess.run([sys.executable,'-u',str(root/'orchestrate.py'),variant,str(run)],check=True)
    subprocess.run([sys.executable,str(root/'audit_run.py'),variant,str(run)],check=True)
print('ALL PLANNED RUNS COMPLETE',flush=True)
