from pathlib import Path
import json,subprocess,sys
r=Path(__file__).resolve().parent;m=json.loads((r/'manifest.json').read_text())
for variant,run in [('B',5),('A',5),('B',6)]:
    print(f'BEGIN {variant}{run}',flush=True)
    subprocess.run([sys.executable,'-u',str(r/'orchestrate.py'),variant,str(run)],check=True)
    subprocess.run([sys.executable,str(r/'audit_run.py'),variant,str(run)],check=True)
print('ALL_FOUR_VALID_RUNS_COMPLETE',flush=True)
