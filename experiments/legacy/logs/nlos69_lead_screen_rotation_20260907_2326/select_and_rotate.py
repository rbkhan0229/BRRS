#!/usr/bin/env python3
"""One-run diagnostic selection/rotation, explicitly not paper qualification."""
import copy
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
API = Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
sys.path.insert(0, str(API))
from brrs_suite_case import checked, save
from brrs_suite_campaign import deploy
from brrs_suite_manifest import load
from brrs_suite_paper import plan
from brrs_suite_results import assess, context_hash

m = load(ROOT / 'manifest.json')
observations = json.loads((ROOT / 'observations.json').read_text())
measured = {4: {}, 8: {}}
for record in observations.values():
    a = assess(Path(record['bundle']))
    if a['context_sha256'] != context_hash(m):
        raise ValueError('physical context mismatch')
    p = a['conditions']
    if p['stage'] != 'stage0' or p['preamble'] != 32 or p['tail_us'] != 0:
        raise ValueError('unexpected Stage0 evidence')
    measured[p['rx_pac']][p['lead_us']] = a

selection = {'created_at': datetime.now(timezone.utc).isoformat(),
             'scope': 'quick partial-grid diagnostic screening; each point once',
             'paper_qualified': False, 'global_optimum_established': False,
             'full_grid_complete': False, 'confirmation_repeats_complete': False,
             'policy': json.loads((ROOT / 'screening_plan.json').read_text())['selection_policy'],
             'candidates_by_pac': {}}
for pac, points in measured.items():
    eligible = []
    for lead in sorted(points):
        neighbours = [lead - 1, lead, lead + 1]
        if not all(v in points for v in neighbours):
            continue
        values = [points[v]['worst_node_per_percent'] for v in neighbours]
        if max(values) >= 1.0:
            continue
        eligible.append((max(values), sum(values), abs(lead - 20), lead, values))
    if not eligible:
        raise SystemExit(f'PAC{pac}: no measured three-point candidate; adaptive extension required')
    worst, total, _, lead, values = min(eligible)
    selection['candidates_by_pac'][str(pac)] = {
        'lead_us': lead, 'neighbours_us': [lead - 1, lead, lead + 1],
        'per_percent': values, 'worst_percent': worst, 'mean_percent': total / 3,
        'measured_leads_us': sorted(points), 'evidence_cases':
            [points[v]['case_id'] for v in [lead - 1, lead, lead + 1]]}
save(ROOT / 'quick_selection.json', selection)
values = {k: v['lead_us'] for k, v in selection['candidates_by_pac'].items()}
candidate = copy.deepcopy(m)
candidate['lead_candidates_us_by_pac'] = values
candidate['lead_selection'] = {'frozen': False, 'lead_us_by_pac': {'4': None, '8': None},
                             'evidence': str(ROOT / 'quick_selection.json')}
save(ROOT / 'candidate_manifest_not_paper_frozen.json', candidate)

diagnostic = copy.deepcopy(candidate)
diagnostic['lead_selection'] = {
    'frozen': True, 'lead_us_by_pac': values,
    'evidence': {'kind': 'quick_screen_provisional_only', 'paper_qualified': False,
                 'meaning_of_frozen': 'settings locked only for these three diagnostic cases; no paper qualification',
                 'path': str(ROOT / 'quick_selection.json')}}
diagnostic['diagnostic_scope'] = {
    'representative_block_only': 2, 'paper_campaign_complete': False,
    'description': 'S2/PAC8 partial activation and S6/PAC8 versus S6/PAC4 at quick-screen candidates; one run each'}
manifest = ROOT / 'rotation_diagnostic_manifest.json'
save(manifest, diagnostic)
cases = {c['id']: c for c in plan(load(manifest), 'exp4')}
ids = [f'paper_exp4_m32_pac8_l{values["8"]}_k2_s2_b02',
       f'paper_exp4_m32_pac8_l{values["8"]}_k13_s6_b02',
       f'paper_exp4_m32_pac4_l{values["4"]}_k13_s6_b02']
save(ROOT / 'rotation_selected_plan.json', [cases[cid] for cid in ids])
print('PROVISIONAL_LEAD_CANDIDATES ' + json.dumps(values), flush=True)
results_path = ROOT / 'rotation_observations.json'
results = json.loads(results_path.read_text()) if results_path.exists() else {}
for cid in ids:
    c = cases[cid]
    print('EXACT_ASSIGNMENT ' + cid + ' ' + json.dumps(
        [(j['physical_role'], j['logical_node'], j['serial']) for j in c['jobs']]), flush=True)
    bundle = ROOT / cid
    if not bundle.exists():
        print('PREPARE ' + cid, flush=True)
        with (ROOT / (cid + '.prepare.console.log')).open('x') as log:
            rc = subprocess.run([sys.executable, str(API / 'brrs_suite_case.py'),
                'prepare', '--manifest', str(manifest), '--stage', 'exp4',
                '--profile', 'paper', '--case', cid, '--bundle', str(bundle)],
                stdout=log, stderr=subprocess.STDOUT).returncode
        if rc:
            raise SystemExit('PREPARE_FAILED ' + cid)
    actual = checked(bundle)
    assert [(j['physical_role'], j['logical_node'], j['serial']) for j in actual['jobs']] == [
        (j['physical_role'], j['logical_node'], j['serial']) for j in c['jobs']]
    if not (bundle / 'results').exists():
        save(ROOT / (cid + '.deployment.json'), deploy(bundle, 's-macbook-air'))
        print('RUN_ONCE ' + cid, flush=True)
        with (ROOT / (cid + '.run.console.log')).open('x') as log:
            rc = subprocess.run([sys.executable, str(bundle / 'sdk/Drivers/API/brrs_suite_case.py'),
                'run', '--bundle', str(bundle), '--host', 's-macbook-air'],
                stdout=log, stderr=subprocess.STDOUT).returncode
        if rc:
            raise SystemExit('CONTROL_OR_ASSESSMENT_FAILURE ' + cid + '; preserved, no retry')
    a = assess(bundle)
    results[cid] = a
    save(results_path, results)
    print('RESULT ' + cid + ' ' + json.dumps({'verdict': a['verdict'],
        'nodes': a['nodes_by_serial'], 'aggregate': a['aggregate'],
        'rx_error_summary': a['rx_error_summary']}, ensure_ascii=False), flush=True)
print('SELECTED_ROTATIONS_COMPLETE', flush=True)
