"""Summarize one completed capture without restarting any board."""
import hashlib
import json
import re
from pathlib import Path

R = Path(__file__).resolve().parent
B = R / 'capture1'

def read(path):
    return json.loads((R / path).read_text())

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def line(text, prefix):
    return next((s for s in text.splitlines() if s.startswith(prefix)), None)

def kv(text):
    return dict(s.split('=', 1) for s in text.split(',') if '=' in s)

case = read('capture1/case.json')
manifest = read('manifest.json')
prior = json.loads((R.parent / 'vehicle_dashboard_b512_n2flipped_once_20260908/RESULTS.json').read_text())
init_path = next((B / 'logs').rglob('*_init.log'))
init = init_path.read_text(errors='replace')
complete = 'Superframes: total=1000' in init and '===== END STATS =====' in init
boards = {**read('recovery_local.json')['boards'], **read('recovery_remote.json')['boards']}
assert len(boards) == 7
assert all(x['halted'] and x['readback']['status'] == 'PASS' for x in boards.values())
checks = {}
for side in ('local', 'remote'):
    pre, post = read(f'preflight_{side}.json'), read(f'postflight_{side}.json')
    assert pre['git'] == post['git']
    assert not post['capture_processes'], post['capture_processes']
    checks[side] = {'git_unchanged': True, 'probes': post['probes'], 'capture_processes': []}

config = kv(line(init, 'EXP4_CONFIG_CSV,') or '')
assert all(config.get(k) == v for k, v in {
    'sync_plen': '512', 'data_plen': '32', 'data_pac': '8', 'lead_us': '26', 'data_slots': '13'
}.items()), config
labels = {'N2': '앞범퍼 A (뒤집음)', 'N3': '앞범퍼 B', 'N4': '운전석',
          'N5': '조수석', 'N6': '트렁크 A', 'N7': '트렁크 B'}
rows = []
for role, label in labels.items():
    job = next(j for j in case['jobs'] if j['physical_role'] == role)
    raw = next((R / 'remote_capture_files').rglob(f'*_{role.lower()}.log'))
    tx = raw.read_text(errors='replace')
    boot = kv(line(tx, 'EXP4_TX_BOOT_CSV,') or '')
    assert boot.get('sync_frame_us') == '598' and boot.get('sync_rx_window_us') == '610'
    snap = {k: v['u32'] for k, v in boards[role]['ram_snapshot'].items()}
    offered = rx = missing = errors = per = None
    if complete:
        fields = line(init, f'EXP4_NODE_CSV,{role},').split(',')
        offered, rx, missing, errors = map(int, fields[3:7])
        assert offered == 1000 * case['conditions']['sequence'].count(role[1:]) if 'sequence' in case['conditions'] else offered > 0
        assert offered == rx + missing
        per = missing * 100 / offered
    metadata_status = None
    meta_path = raw.with_suffix('.meta.txt')
    if meta_path.exists():
        meta = dict(s.split('=', 1) for s in meta_path.read_text().splitlines() if '=' in s)
        for key, expected in {'serial': job['serial'], 'firmware_sha256': job['hex_sha256'],
                              'raw_sha256': sha(raw), 'suite_conditions_sha256': case['conditions_sha256'],
                              'suite_manifest_sha256': job['environment']['BRRS_SUITE_MANIFEST_SHA256']}.items():
            assert meta.get(key) == expected, (role, key)
        metadata_status = meta.get('collection_status')
    previous = next(x for x in prior['nodes'] if x['role'] == role)
    rows.append({'role': role, 'location': label, 'serial': job['serial'],
                 'beacons_received': snap['exp4_sync_frames_received'],
                 'tx_attempts': snap['total_tx_attempts'], 'tx_success': snap['own_tx_success'],
                 'tx_late': snap['total_tx_delayed_late'], 'offered': offered, 'rx': rx,
                 'missing': missing, 'offered_loss_percent': per, 'slot_attributed_errors': errors,
                 'metadata_status': metadata_status, 'end_stats': '===== END STATS =====' in tx,
                 'end_beacon': bool(re.search(r'EXP4_TX_DONE,.*end=1,', tx)),
                 'tx_snapshot': snap, 'tx_sync_loss_line': line(tx, 'SYNC loss:'),
                 'hex_sha256': job['hex_sha256'], 'raw_sha256': sha(raw),
                 'previous_beacons': previous['beacons_received'], 'previous_tx': previous['tx_success'],
                 'previous_rx': previous['rx'], 'previous_per': previous['offered_loss_percent']})

aggregate = None
if complete:
    offered = sum(x['offered'] for x in rows)
    rx = sum(x['rx'] for x in rows)
    transmitted = sum(x['tx_success'] for x in rows)
    assert offered == 13000 and 0 <= rx <= transmitted <= offered
    aggregate = {'offered': offered, 'rx': rx, 'tx_success': transmitted,
                 'offered_loss_percent': (offered-rx)*100/offered,
                 'data_loss_after_tx_percent': (transmitted-rx)*100/transmitted if transmitted else None}
prefixes = ['RX timeouts=', 'TDMA validation:', 'EXP4_DOUBLE_BUFFER_CSV,', 'EXP4_TIMING_CSV,',
            'EXP4_DEFERRED_CSV,', 'EXP4_SPI_CSV,', 'EXP4_STATUS_CSV,', 'EXP4_SUMMARY_CSV,']
counter_lines = {p: line(init, p) for p in prefixes}
passed = complete and all(x['rx'] > 0 and x['offered_loss_percent'] < 1 and x['metadata_status'] == 'PASS' and x['end_beacon'] for x in rows)
result = {'verdict': 'PASS_PER_NODE' if passed else 'FAIL', 'rf_runs_started': 1,
          'rx_1000sf_complete': complete, 'nodes': rows, 'aggregate': aggregate,
          'rx_config': config, 'counter_source_lines': counter_lines,
          'conditions': case['conditions'], 'setup': manifest['setup_record'],
          'orchestration': read('capture1/results/orchestration.json'), 'postflight': checks,
          'all_seven_halted': True, 'all_seven_flash_readbacks_pass': True,
          'same_hex_as_previous_512': True, 'firmware_modified': False, 'git_modified': False,
          'limitations': ['N2 flip axis was not specified.',
                         'One run per orientation; result is an observation, not a proof of sole cause.',
                         'Offered loss includes slots with no TX due to missing beacons.',
                         'TX early termination does not provide complete beacon PHY PER.',
                         'A zero-packet node never passes.']}
(R / 'RESULTS.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')

lines = ['# 차량 좌석 사이 수납함 위 RX · 비컨 M512 · 1회', '',
         f"**노드별 PER <1% 목표: {'PASS' if passed else 'FAIL'}. RX 1,000SF 완료: {'예' if complete else '아니오'}.**", '',
         '사용자는 RX를 운전석과 조수석 사이 수납함 위로 옮겼다고 알렸다. N2는 뒤집은 상태를 유지한 것으로 기록했다. 나머지 배치·시동 켜짐·유전원 허브·문 닫힘은 직전 확인 상태를 이어 기록했다. N2/N3 앞범퍼 바깥, N4 운전석, N5 조수석, N6/N7 트렁크. 천장 전등 위치는 사용자가 다음 후보로 언급했으며 이번에는 측정하지 않았다.', '',
         'SSH 및 실제 serial 7개를 확인했다. 직전 512 측정과 정확히 같은 7개 HEX를 재사용했다. 비컨 M512/PAC8, DATA M32/PAC8, lead 26µs, TX 6대·13슬롯(2345672345673), G250, SB/SP 3000/2500µs, 주기 10ms, 목표 1,000SF, 슬롯별 delayed-RX, 재전송 없음. 빌드나 펌웨어 코드 변경 없음.', '',
         '## 노드별 결과', '',
         '| 노드 | 비컨 | TX 시도 / 완료 | RX / 예정 | 예정 전송 기준 PER | 직전 PER |',
         '|---|---:|---:|---:|---:|---:|']
for x in rows:
    delivery = f"{x['rx']:,} / {x['offered']:,}" if complete else '미확보'
    per_text = f"{x['offered_loss_percent']:.2f}%" if complete else '산출 불가'
    lines.append(f"| {x['role']} {x['location']} | {x['beacons_received']:,} | {x['tx_attempts']:,} / {x['tx_success']:,} | {delivery} | {per_text} | {x['previous_per']:.2f}% |")
lines += ['', '예정 전송 기준 손실에는 비컨 미수신으로 송신하지 않은 슬롯도 포함된다. 송신 0회인 노드의 송신 후 PHY PER은 정의하지 않는다. TX success는 송신 완료이며 수신 보장이 아니다. 조기 종료된 TX의 비컨 누계를 정상 완주 비컨 PER로 해석하지 않는다.', '']
if aggregate:
    lines.append(f"전체 RX {aggregate['rx']:,}/{aggregate['offered']:,}, 예정 전송 기준 손실률 {aggregate['offered_loss_percent']:.4f}%. 실제 TX 완료 {aggregate['tx_success']:,}회.")
lines += ['', '## 수집 및 오류', '']
for x in rows:
    lines.append(f"- {x['role']}: metadata={x['metadata_status'] or '없음'}, END 통계={x['end_stats']}, END 비컨={x['end_beacon']}, delayed-TX late={x['tx_late']}; {x['tx_sync_loss_line'] or 'TX 최종 통계 없음; recovery RAM 사용'}")
lines += ['', '```text', *[s for s in counter_lines.values() if s], '```', '',
          '세부 카운터가 없는 항목은 0으로 가정하지 않는다. N2/N3 beacon RX 오류 누계는 RX DATA 구간 오류와 합산하지 않는다. 수집 실패 판정은 그대로 보존했으며, 다른 보드 수집을 끝까지 기다리는 직전 수집 도구를 유지했다.', '',
          '## 역할 및 HEX SHA256', '', '| 역할 | Serial | SHA256 |', '|---|---|---|']
for role, board in boards.items():
    rb = board['readback']
    lines.append(f"| {role} | {rb['serial']} | `{rb['hex_sha256']}` |")
lines += ['', '정확히 1회 RF 실행. 종료 후 7대 플래시 readback PASS 및 정지를 확인했다. 로컬·원격 잔여 캡처 프로세스 0, 기존 Git 브랜치·HEAD·dirty 상태 변경 없음. commit/push 없음. RX 위치 변경 1회 관측으로 단일 원인을 확정하지 않는다.', '',
          '원시 자료: `capture1/logs/`(RX), `remote_capture_files/`(TX), `recovery_*.json`(정지·플래시·RAM), `preflight_*.json`/`postflight_*.json`, `manifest.json`, `image_reuse_proof.json`, `deployment.json`, `capture1/results/`.', '']
(R / 'RESULTS.md').write_text('\n'.join(lines))
print(json.dumps({'report': str(R/'RESULTS.md'), 'verdict': result['verdict'], 'rx_complete': complete,
                  'nodes': [{k: x[k] for k in ('role', 'beacons_received', 'tx_success', 'rx', 'offered_loss_percent')} for x in rows],
                  'aggregate': aggregate}, ensure_ascii=False, indent=2))
