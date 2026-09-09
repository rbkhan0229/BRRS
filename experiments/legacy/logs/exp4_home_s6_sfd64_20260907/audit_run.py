"""Read-only multi-node, multi-slot audit; preserve losses separately from integrity."""
import argparse
import hashlib
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def kv(line):
    return dict(f.split('=', 1) for f in line.split(',')[1:] if '=' in f)

def inspect(folder, manifest, variant):
    out = {'variant': variant, 'run_folder': str(folder), 'failures': [],
           'valid': False, 'goal_pass': False, 'nodes': {}, 'tx': {}, 'markers': {}}
    def check(condition, reason):
        if not condition:
            out['failures'].append(reason)
    params = manifest['parameters_by_variant'][variant]
    sequence = params['sequence']
    cycles = params['SF']
    roles = list(manifest['roles'])
    txroles = [r for r in roles if r != 'init']
    texts = {}
    for side, selected in [('init', ['init']), ('tx', txroles)]:
        state = json.loads((folder / side / 'status.json').read_text())
        check(state['status'] == 'CAPTURE_COMPLETE_NOT_YET_RF_VERIFIED', side + ' collector status')
        check(state['manifest_sha256'] == digest(folder / 'manifest.json'), side + ' manifest hash')
        readback = json.loads((folder / (side + '_flash_readback.json')).read_text())
        for role in selected:
            raw = folder / side / (role + '.log')
            text = raw.read_text()
            texts[role] = text
            record = manifest['variants'][variant][role]
            worker = state['workers'][role]
            check(worker['serial'] == record['serial'], role + ' serial')
            check(worker['sha256'] == record['sha256'], role + ' image hash')
            check(worker['raw_sha256'] == digest(raw), role + ' raw hash')
            check(worker['exit_code'] == 0, role + ' collector exit')
            rb = readback['boards'][role]
            check(rb['status'] == 'PASS' and rb['hex_sha256'] == record['sha256'], role + ' flash readback')
            markers = {'ready': text.count('EXP_LOG_READY,channel=1'),
                       'end_stats': text.count('===== END STATS =====')}
            out['markers'][role] = markers
            check(markers == {'ready': 1, 'end_stats': 1}, role + ' markers')
    records = {}
    for line in texts['init'].splitlines():
        if ',' in line and '=' in line:
            records.setdefault(line.split(',')[0], []).append(kv(line))
        if line.startswith('EXP4_NODE_CSV,'):
            f = line.split(',')
            role = f[1]
            offered, rx, lost, error = map(int, f[3:7])
            check(role not in out['nodes'], 'duplicate node row ' + role)
            expected = cycles * sequence.count(role[1:])
            check(int(f[2]) == 32 and offered == expected, role + ' offered/PLEN')
            check(rx + lost == offered and 0 <= rx <= offered, role + ' RX arithmetic')
            out['nodes'][role] = {'offered': offered, 'rx': rx, 'lost': lost,
                                  'per_pct': 100 * lost / offered,
                                  'rx_error_attributed': error}
    out['init_records'] = records
    trial = records.get('EXP4_SFDTO_TRIAL_CSV', [])
    check(len(trial) == 1, 'SFD trial readback record count')
    if len(trial) == 1:
        check(trial[0] == {'requested': '64', 'readback': '64', 'reads': '1',
                           'default_formula': '37', 'status': 'PASS'}, 'SFD register readback')
    check(set(out['nodes']) == set(txroles), 'node set')
    cfg = records['EXP4_CONFIG_CSV'][0]
    for key, expected in {'physical_sensors': len(txroles), 'data_slots': len(sequence),
                          'slot_repeats': 1, 'data_plen': 32, 'data_pac': params['PAC'],
                          'psdu_bytes': 26, 'app_payload_bytes': 16, 'superframe_us': 10000,
                          'sync_buffer_us': 3000, 'sync_prep_us': 2500,
                          'slot_us': 347, 'guard_us': 250, 'lead_us': 25,
                          'sync_prep_deadline_us': 7500, 'spi_mode': 'persistent_data_burst'}.items():
        check(cfg.get(key) == str(expected), 'INIT config ' + key)
    check(records['EXP4_FIRMWARE_REV'][0]['data_rx'] == 'per_slot_delayed_bounded_single_attempt', 'RX mode')
    check(records['EXP4_SLOT_SCHEDULE_CSV'][0]['slot_owners'] == sequence, 'slot owners')
    done = records['EXP4_DONE'][0]
    for key, expected in {'superframes': cycles, 'expected': cycles * len(sequence),
                          'collection': 'PASS', 'status': 'PASS'}.items():
        check(done.get(key) == str(expected), 'INIT done ' + key)
    tdma = next(l for l in texts['init'].splitlines() if l.startswith('TDMA validation:'))
    td = {k: int(v) for k, v in re.findall(r'([\w-]+)=(\d+)', tdma)}
    for key in ['wrong-length', 'wrong-slot', 'wrong-superframe', 'data-config-errors',
                'rx-schedule-late', 'sync-delayed-late', 'rearm-deadline-miss',
                'rx-buffer-overrun', 'deferred-overflow']:
        check(td[key] == 0, 'TDMA ' + key)
    for key in ['rdb_host_mismatch', 'rdb_incomplete', 'rdb_incomplete_recovered', 'rdb_resync', 'overrun']:
        check(int(records['EXP4_DOUBLE_BUFFER_CSV'][0][key]) == 0, 'RDB ' + key)
    spi = records['EXP4_SPI_CSV'][0]
    for key in ['active', 'begin_fail', 'end_fail', 'device_id_fail', 'state_error',
                'transfer_error', 'direct_timeout', 'recovery']:
        check(int(spi[key]) == 0, 'SPI ' + key)
    check(int(spi['begin']) == int(spi['end']) == cycles and int(spi['direct_xfers']) > 0, 'SPI sessions')
    check(int(records['EXP4_TIMING_CSV'][0]['tx_wait_timeout']) == 0, 'SYNC TX wait timeout')
    windows = records.get('EXP4_SLOT_RX_CSV', [])
    check(len(windows) == len(sequence), 'window count')
    seen = set()
    for w in windows:
        slot = int(w['slot'])
        check(slot not in seen and 0 <= slot < len(sequence), 'duplicate/out-of-range window')
        seen.add(slot)
        if not 0 <= slot < len(sequence):
            continue
        check(w['owner'] == sequence[slot], 'window owner ' + str(slot))
        check(int(w['attempted']) == int(w['armed']) == cycles and int(w['late']) == 0, 'window arm ' + str(slot))
        check(sum(int(w[k]) for k in ['rx_good', 'timeout', 'error']) == cycles, 'window terminal count ' + str(slot))
        check(w['window_us'] == '122' and w['fwto_uus'] == '119', 'window width ' + str(slot))
    for role, n in out['nodes'].items():
        owned = [w for w in windows if w['owner'] == role[1:]]
        check(sum(int(w['rx_good']) for w in owned) == n['rx'], role + ' window/node RX')
        check(sum(int(w['error']) for w in owned) == n['rx_error_attributed'], role + ' window/node error')
    classified = 0
    for row in records.get('EXP4_SLOT_CLASS_CSV', []):
        slot = int(row['observed_slot'])
        check(0 <= slot < len(sequence), 'classified slot range')
        if 0 <= slot < len(sequence):
            check(row['src'] == row['observed_owner'] == 'N' + sequence[slot], 'wrong source/owner')
        classified += int(row['count'])
    total = {k: sum(n[k] for n in out['nodes'].values()) for k in ['offered', 'rx', 'lost']}
    total['per_pct'] = 100 * total['lost'] / total['offered']
    out['total'] = total
    check(total['rx'] > 0, 'zero valid RX')
    check(classified == total['rx'] == int(done['rx']), 'RX/source/done sums')
    check(int(records['EXP4_DOUBLE_BUFFER_CSV'][0]['rx_good_events']) == total['rx'], 'RDB good count')
    check(int(records['EXP4_HOT_PATH_CSV'][0]['count']) == total['rx'], 'hot path count')
    for role in txroles:
        lines = texts[role].splitlines()
        f = next(l for l in lines if l.startswith('EXP4_TX_RESULT_CSV,')).split(',')
        received, missed, attempts, success, late, end = map(int, f[4:10])
        owned = sequence.count(role[1:])
        check(int(f[2]) == int(role[1:]) and int(f[3]) == 32, role + ' role/PLEN')
        check(received + missed == cycles and 0 < received <= cycles, role + ' beacon counts')
        check(attempts == success == received * owned, role + ' TX/owned-slot arithmetic')
        check(late == 0 and end == 1 and f[10] == 'PASS', role + ' late/END/schedule')
        beacon = kv(next(l for l in lines if l.startswith('BRRS_BEACON_RX_CSV,')))
        check(beacon['slot_owners'] == sequence and int(beacon['slot_count']) == len(sequence), role + ' beacon schedule')
        sync_rx = kv(next(l for l in lines if l.startswith('EXP4_TX_SYNC_RX_CSV,')))
        check(int(sync_rx['delayed_late']) == 0 and int(sync_rx['scheduled']) == received - 1, role + ' SYNC RX schedule')
        n = out['nodes'][role]
        check(n['rx'] <= success, role + ' RX exceeds actual TX')
        n.update(tx_success=success, beacon_received=received, beacon_missed=missed,
                 not_transmitted=n['offered'] - success,
                 transmitted_lost=success - n['rx'],
                 transmitted_per_pct=100 * (success - n['rx']) / success if success else None)
        sync = next(l for l in lines if l.startswith('SYNC loss:'))
        out['tx'][role] = {'beacons': received, 'beacon_missed': missed, 'attempts': attempts,
                           'success': success, 'delayed_late': late, 'end_received': end,
                           'sync_loss_timeouts': int(re.search(r'SYNC loss: (\d+)', sync)[1]),
                           'rx_errors': int(re.search(r'RX errors=(\d+)', sync)[1])}
    line = next(l for l in texts['init'].splitlines() if l.startswith('RX timeouts='))
    out['error_line'] = line
    names = {'sfd_timeout': 'sfdto', 'phr_error': 'phe', 'crc_error': 'fce',
             'rxfsl': 'fsl', 'fwto': 'fwto', 'pto': 'pto', 'fint_only': 'fint-only'}
    out['errors'] = {k: int(re.search(r'\b' + name + r'=(\d+)', line)[1]) for k, name in names.items()}
    check(sum(out['errors'].values()) == total['lost'], 'loss/error accounting')
    out['full_tx'] = all(t['beacons'] == cycles for t in out['tx'].values())
    out['valid'] = not out['failures']
    out['goal_pass'] = out['valid'] and all(n['per_pct'] < 1 for n in out['nodes'].values())
    out['goal'] = 'INVALID' if not out['valid'] else ('PASS' if out['goal_pass'] else 'FAIL_PER')
    return out

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('variant')
    ap.add_argument('run', type=int)
    args = ap.parse_args()
    folder = ROOT / f'{args.variant}_r{args.run}'
    manifest = json.loads((folder / 'manifest.json').read_text())
    try:
        out = inspect(folder, manifest, args.variant)
    except Exception as exc:
        out = {'valid': False, 'goal_pass': False, 'goal': 'INVALID', 'failures': [repr(exc)]}
    (folder / 'audit.json').write_text(json.dumps(out, indent=2) + '\n')
    print(json.dumps({k: v for k, v in out.items() if k not in ['init_records', 'markers']}, indent=2))
    raise SystemExit(0 if out['valid'] else 2)
