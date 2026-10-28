#!/usr/bin/env python3
"""Read-only comparison of two immutable home beacon diagnostics."""
from pathlib import Path
import json
import statistics

BASE = Path(__file__).resolve().parent.parent
ROOTS = [
    ('800us', BASE / 'home_beacon_rearm_timing_20260930_215100'),
    ('600us', BASE / 'home_beacon_rearm_window600_20260930_222000'),
]
U32 = 2 ** 32
TICKS_PER_US = 249.6
PERIOD_TICKS = 2496000

def read_case(root, phase):
    case = next(p for p in (root / 'cases').iterdir() if p.name.endswith('_' + phase + '_a1'))
    result = case / 'bundle/results'
    assessment = json.loads((result / 'ASSESSMENT.json').read_text())
    trace = json.loads((result / 'BEACON_RX_TRACE.json').read_text())
    finished = json.loads((case / 'finished.json').read_text())
    return case, assessment, trace, finished

def midpoint(before, after):
    return (before + ((after - before) % U32) / 2) % U32

def signed32(x):
    return ((x + U32 / 2) % U32) - U32 / 2

def clock_records(trace):
    out = []
    for gap in trace['gaps']:
        if gap['next'] > 2000:
            continue
        c0 = midpoint(gap['anchor_before_cpu_before'], gap['anchor_before_cpu_after'])
        c1 = midpoint(gap['anchor_after_cpu_before'], gap['anchor_after_cpu_after'])
        cpu_delta = (c1 - c0) % U32
        rf_delta = (gap['anchor_after_rf'] - gap['anchor_before_rf']) % U32
        slope = rf_delta / cpu_delta
        def event_rf(event):
            return (gap['anchor_before_rf'] + ((event['tick'] - c0) % U32) * slope) % U32
        for seq in range(gap['prev'] + 1, gap['next']):
            nominal = (gap['rf_start'] + (seq - gap['prev']) * PERIOD_TICKS) % U32
            def offset(event):
                return signed32(event_rf(event) - nominal) / TICKS_PER_US
            errors = [e for e in gap['events'] if e['kind'] == 1 and (e['arg'] & 0x4007f00)]
            nearby = [e for e in errors if abs(offset(e)) < 1000]
            error = min(nearby, key=lambda e: abs(offset(e))) if nearby else None
            rearm = None
            if error is not None:
                following = [e for e in gap['events'] if e['kind'] == 4 and
                             0 < (e['tick'] - error['tick']) % U32 < 65000]
                rearm = min(following, key=lambda e: (e['tick'] - error['tick']) % U32) if following else None
            width0 = (gap['anchor_before_cpu_after'] - gap['anchor_before_cpu_before']) % U32
            width1 = (gap['anchor_after_cpu_after'] - gap['anchor_after_cpu_before']) % U32
            out.append({
                'seq': seq,
                'clock_slope_radio_ticks_per_cpu_cycle': round(slope, 8),
                'anchor_max_read_width_us': round(max(width0, width1) * slope / TICKS_PER_US, 3),
                'error_status_hex': hex(error['arg']) if error else None,
                'error_offset_from_expected_rmarker_us': round(offset(error), 3) if error else None,
                'rearm_offset_from_expected_rmarker_us': round(offset(rearm), 3) if rearm else None,
            })
    return out

report = {'scope': 'CH5 M64/PAC8 home isolated comparison, not vehicle or Standard; no new RF in analysis',
          'radio_time_method': 'W1C=0 then bracketed SYS_TIME read once per CRC-good beacon; interpolate CPU events between adjacent measured anchors',
          'limitations': ['RF-ready state inside DW3000 is not directly observed',
                          'first beacon without adjacent anchors is unclassified',
                          'OFF/ON observations are at different times; two ON cases do not prove causal improvement',
                          'SYS_TIME read occurs after valid beacon processing and may perturb timing; both compared variants use identical instrumentation'],
          'conditions': {}}
for label, root in ROOTS:
    entries = {}
    for phase in ('off_a', 'on_a'):
        case, assessment, trace, finished = read_case(root, phase)
        obs = assessment['code_observations']
        tx = obs['tx_account']['success']
        rx = obs['rx_classes']['accepted']
        item = {'case_id': case.name, 'official_verdict': assessment['verdict'],
                'offered_per_percent': assessment['worst_node_per_percent'],
                'offered': obs['tx_account']['offered'], 'actual_tx': tx, 'data_rx': rx,
                'unsent': obs['tx_account']['unsent'], 'post_tx_loss': tx - rx,
                'beacon_missed': obs['sync']['missed'],
                'predicted_tx_attempts': obs['predicted_tx_attempts'],
                'system_status': finished['status'], 'return_code': finished['return_code'],
                'trace_gaps': trace['summary']['gaps'],
                'clock_records': clock_records(trace)}
        if phase == 'on_a':
            item['auxiliary_overlap'] = assessment.get('auxiliary_overlap')
            item['aux_tx_count_total'] = assessment.get('auxiliary', {}).get('tx_success')
        entries[phase] = item
    report['conditions'][label] = entries
(root / 'CLOCK_ANCHOR_EFFECT_ANALYSIS.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
for label in ('800us', '600us'):
    on = report['conditions'][label]['on_a']
    timings = on['clock_records']
    errors = [x['error_offset_from_expected_rmarker_us'] for x in timings if x['error_offset_from_expected_rmarker_us'] is not None]
    rearms = [x['rearm_offset_from_expected_rmarker_us'] for x in timings if x['rearm_offset_from_expected_rmarker_us'] is not None]
    print(label, on['official_verdict'], on['offered_per_percent'], 'beacon_missed', on['beacon_missed'],
          'unsent', on['unsent'], 'post_tx_loss', on['post_tx_loss'],
          'error_range', (min(errors), max(errors)) if errors else None,
          'rearm_range', (min(rearms), max(rearms)) if rearms else None,
          'aux_tx_total', on['aux_tx_count_total'])
