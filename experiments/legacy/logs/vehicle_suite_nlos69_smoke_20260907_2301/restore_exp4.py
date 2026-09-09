#!/usr/bin/env python3
"""Restore only the two changed boards to recorded Exp4 images; keep all halted."""
import json
import pathlib
import sys
from datetime import datetime,timezone

BASE=pathlib.Path('/Users/songchieon/Desktop/DWM3000')
BUNDLE=BASE/'logs/exp4_nlos69_s6_pac8_recheck_20260907_2248/capture1'
sys.path.insert(0,str(BUNDLE/'sdk/Drivers/API'))
from brrs_suite_case import checked,probe_check,halt,link,readback

side=sys.argv[1]
if side not in ['local','remote']:raise SystemExit('explicit local or remote side required')
case=checked(BUNDLE)
roles,jobs=probe_check(case,side)
halt(case,roles)
changed='init' if side=='local' else 'N4'
selected=next(j for j in jobs if j['physical_role']==changed)
jl=link(selected['serial'])
try:
    jl.reset(halt=True)
    jl.flash_file(str(BUNDLE/selected['hex']),0)
    jl.reset(halt=True)
    if not jl.halted():raise RuntimeError('restore target not halted')
finally:jl.close()
verification={}
for job in jobs:
    verification[job['physical_role']]=readback(BUNDLE,job)
halt(case,roles)
print(json.dumps({'side':side,'at':datetime.now(timezone.utc).isoformat(),'changed_role':changed,'changed_serial':selected['serial'],'readback':verification,'all_case_boards_halted':True,'extra_rf_run_started':False},indent=2))

