#!/usr/bin/env python3
"""Restore recorded Exp4 roles after rotated diagnostics, without starting RF."""
import json
from pathlib import Path
import sys
from datetime import datetime,timezone

BUNDLE=Path('/Users/songchieon/Desktop/DWM3000/logs/exp4_nlos69_s6_pac8_recheck_20260907_2248/capture1')
sys.path.insert(0,str(BUNDLE/'sdk/Drivers/API'))
from brrs_suite_case import checked,probe_check,halt,link,readback
side=sys.argv[1]
if side not in ['local','remote']:raise SystemExit('explicit local/remote required')
c=checked(BUNDLE);roles,jobs=probe_check(c,side);halt(c,roles)
result={'side':side,'started_at':datetime.now(timezone.utc).isoformat(),'boards':{}}
for job in jobs:
    role=job['physical_role'];action='ALREADY_MATCHED'
    try:verification=readback(BUNDLE,job)
    except ValueError as exc:
        if not str(exc).startswith('flash mismatch'):raise
        action='RESTORED'
        jl=link(job['serial'])
        try:
            jl.reset(halt=True)
            jl.flash_file(str(BUNDLE/job['hex']),0)
            jl.reset(halt=True)
            if not jl.halted():raise RuntimeError('restore halt failed')
        finally:jl.close()
        verification=readback(BUNDLE,job)
    result['boards'][role]={'action':action,**verification}
halt(c,roles)
result.update(finished_at=datetime.now(timezone.utc).isoformat(),all_original_roles_halted=True,extra_rf_run_started=False)
print(json.dumps(result,indent=2))

