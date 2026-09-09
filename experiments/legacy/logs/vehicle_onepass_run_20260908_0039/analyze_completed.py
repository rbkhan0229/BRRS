#!/usr/bin/env python3
"""Postprocess completed Exp1/2 evidence only; never access a board."""
from pathlib import Path
import csv
import json
import subprocess
import sys

R = Path(__file__).resolve().parent
API = Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
sys.path.insert(0,str(API))
from brrs_suite_case import save,sha
observations = json.loads((R/'observations.json').read_text())
index = json.loads((R/'analysis_index.json').read_text()) if (R/'analysis_index.json').exists() else {}
for cid,a in observations.items():
    p=a['conditions'];stage=p['stage']
    if stage not in ['exp1','exp2'] or cid in index: continue
    raw=Path(a['bundle'])/'results/local/init.log'
    out=R/'analysis'/cid;out.mkdir(parents=True,exist_ok=True)
    script=API/('brrs_exp1_log_to_csv_plot.py' if stage=='exp1' else 'brrs_cir_log_to_csv_plot.py')
    cmd=[sys.executable,str(script),str(raw),'-o',str(out),'--prefix',cid]
    if stage=='exp2': cmd+=['--run',cid,'--environment','NLOS_6.9m_vehicle_functional_preparation_20260908','--distance-m','6.9','--expected-samples','1000']
    with (out/'analysis.log').open('x') as f:
        subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
    summary=list(csv.DictReader((out/(cid+'_summary.csv')).open()))
    assert len(summary)==1,(cid,len(summary))
    expected_rx=sum(n['rx'] for n in a['nodes_by_serial'].values())
    if stage=='exp2':
        rows=sum(1 for _ in csv.DictReader((out/(cid+'_samples.csv')).open()))
        assert rows==expected_rx,(cid,rows,expected_rx)
    else:rows=None
    index[cid]={'stage':stage,'source_raw':str(raw),'raw_sha256':sha(raw),'analyzer_sha256':sha(script),
        'summary':summary[0],'expected_rx':expected_rx,'cir_rows':rows,
        'outputs':{str(x.relative_to(R)):sha(x) for x in out.iterdir() if x.is_file()}}
    save(R/'analysis_index.json',index)
    print('ANALYSIS_PASS',cid,'rx',expected_rx,'cir',rows,flush=True)
print('ANALYZED_COMPLETED_CASES',len(index))
