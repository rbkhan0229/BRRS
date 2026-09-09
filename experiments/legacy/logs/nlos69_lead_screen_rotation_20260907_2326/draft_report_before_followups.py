#!/usr/bin/env python3
"""Revalidate and document completed quick screening and selected rotations."""
from datetime import datetime
import json
from pathlib import Path
import re
import sys
from zoneinfo import ZoneInfo

R = Path(__file__).resolve().parent
API = Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
sys.path.insert(0, str(API))
from brrs_suite_case import checked, save
from brrs_suite_results import assess

def time_kst(t):
    return datetime.fromisoformat(t).astimezone(ZoneInfo('Asia/Seoul')).isoformat(timespec='seconds')

def evidence(bundle):
    b = Path(bundle); c = checked(b); a = assess(b)
    o = json.loads((b / 'results/orchestration.json').read_text())
    a['started_at_kst'] = time_kst(o['started_at'])
    a['finished_at_kst'] = time_kst(o['finished_at'])
    a['control_status'] = o['status']
    a['images'] = [{k: j[k] for k in ['physical_role','logical_node','serial','hex','hex_sha256','elf_sha256']} for j in c['jobs']]
    a['raw_diagnostics_by_role'] = {}
    a['collection_by_role'] = {}
    for j in c['jobs']:
        role = j['physical_role']; side = 'local' if j['host'] == 'local' else 'remote'
        lines = (b / 'results' / side / (role + '.log')).read_text().splitlines()
        a['raw_diagnostics_by_role'][role] = [l for l in lines if l.startswith((
            'RX timeouts=', 'TDMA validation:', 'SYNC loss:', 'My TX:',
            'EXP1_DONE,', 'EXP1_TX_DONE,', 'EXP4_', '===== END STATS ====='))]
        state = json.loads((b / 'results' / side / 'status.json').read_text())
        meta = dict(l.split('=', 1) for l in (b / 'results' / side / (role + '.meta.txt')).read_text().splitlines() if '=' in l)
        a['collection_by_role'][role] = {
            'exit_code': state['workers'][role]['exit_code'], 'readback': state['workers'][role]['readback'],
            'capture_metadata': {k: v for k, v in meta.items() if any(s in k for s in ['collection', 'timeout', 'marker', 'ready', 'end'])},
            'ready_marker_count': sum(l.startswith('EXP_LOG_READY,channel=1') for l in lines),
            'end_stats_marker_count': sum(l == '===== END STATS =====' for l in lines)}
    rxline = next(l for l in a['raw_diagnostics_by_role']['init'] if l.startswith('RX timeouts='))
    a['rx_counters_parsed'] = {k: int(v) for k, v in re.findall(r'([\w-]+)=(\d+)', rxline)}
    return a

records = json.loads((R / 'observations.json').read_text())
stage0 = {k: {**evidence(v['bundle']), 'reused': v['reused']} for k, v in records.items()}
rotations = {k: evidence(v['bundle']) for k, v in json.loads((R / 'rotation_observations.json').read_text()).items()}
selection = json.loads((R / 'quick_selection.json').read_text())
restoration = {s: json.loads((R / ('restore_' + s + '.json')).read_text()) for s in ['local', 'remote']}
assert len(rotations) == 5
assert all(v['all_original_roles_halted'] and not v['extra_rf_run_started'] for v in restoration.values())
preservation = {}
for s in ['local', 'remote']:
    pre = json.loads((R / ('preflight_' + s + '.json')).read_text())
    post = json.loads((R / ('postflight_' + s + '.json')).read_text())
    preservation[s] = {'git_unchanged': pre['git'] == post['git'],
                       'probe_set_unchanged': sorted(pre['probes']) == sorted(post['probes']),
                       'remaining_capture_processes': post['capture_processes']}
    assert preservation[s]['git_unchanged'] and preservation[s]['probe_set_unchanged'] and not post['capture_processes']

report = {'scope': 'NLOS 6.9m quick screening and representative block2 diagnostics; not a complete paper campaign',
          'new_stage0_runs': sum(not a['reused'] for a in stage0.values()), 'reused_stage0_runs': sum(a['reused'] for a in stage0.values()),
          'new_exp4_runs': len(rotations), 'stage0': stage0, 'exp4': rotations, 'selection': selection,
          'restoration': restoration, 'preservation': preservation,
          'hub_external_power': True, 'physical_configuration_changed_by_agent': False,
          'firmware_c_modified': False, 'commit_or_push_performed': False,
          'paper_qualified': False, 'complete_rotation_campaign': False}
save(R / 'RESULTS.json', report)
save(R / 'image_inventory.json', {k: a['images'] for k, a in {**stage0, **rotations}.items()})

lines = ['# NLOS 6.9m — PAC별 lead 빠른 탐색과 회전 실행 점검', '',
         '현재 설치에서 서로 다른 Stage0 조건을 1회씩 측정하고 필요한 구간만 확장했다. PAC8/25µs의 직전 유효 결과 1개는 원문·해시·환경을 재검증해 재사용했다. 같은 RF 조건의 반복 실행은 하지 않았다.', '',
         f"신규 Stage0 {report['new_stage0_runs']}회 + 기존 1회 재사용, Exp4 대표 회전 {len(rotations)}회(계획 3회와 실패 후 4µs 확장 2회). 각 TX PER **<1%** 기준이며, RX 0개는 성공이 아니다. 전체 grid·확인 반복·차량 성능의 완료 판정은 아니다.", '',
         '사용자가 약4µs 간격으로 다음 단계에 넘어가도록 지시했을 때 Stage0의 기존 선택 구간은 이미 완료됐다. 추가 Stage0 미세 탐색 없이 진행했고, Exp4에서 두 PAC 모두 lead20µs가 실패하여 동일 조건에서 lead24µs만 각1회 추가했다. [범위 변경 기록](screening_policy_update.json).', '',
         '## 1. 연결과 보존', '',
         'SSH `s-macbook-air` 정상. 로컬 INIT 1대와 원격 TX 6대의 실제 probe 집합을 매 실행 대조했다. TX 허브 외부 전원은 사용자 확인 상태를 유지했다. 원격 USB 트리의 상·하위 허브 연결은 preflight/postflight JSON에 보존했다. 보드 위치·방향·전원·케이블·포트를 변경하지 않았다.', '',
         '| 원래 물리 역할 | J-Link serial |', '|---|---|']
manifest = json.loads((R / 'manifest.json').read_text())
for role, b in manifest['boards'].items(): lines.append(f"| {role} | {b['serial']} |")
lines += ['', '원본 Git 4곳의 branch/HEAD/dirty/diff hash는 전후 동일하며 펌웨어 C 수정·commit·push는 없다. Stage0용 빌드 설정만 적용했다. 종료 후 원래 PAC8/lead25/S6/13슬롯 역할 HEX를 7대 모두 readback 검증하고 halt 상태로 두었다. 복원 후 추가 RF는 시작하지 않았다. 잔여 캡처 프로세스 없음.', '',
          '## 2. Stage0: PAC × lead', '',
          'M32, tail 0, 2,000 superframes, 물리 N4(1050282818)→INIT(1050270933), 논리 N2 단일 링크, G500/slot597µs. DATA PAC만 RX에서 변경하며 TX DATA 및 비컨 PAC는 기존 설정을 유지한다. Exp4의 G250·연속 여러 슬롯 조건과 구분한다.', '',
          '| lead (µs) | PAC4 PER | PAC8 PER |', '|---:|---:|---:|']
leads = sorted({a['conditions']['lead_us'] for a in stage0.values()})
for lead in leads:
    values = []
    for pac in [4, 8]:
        a = stage0.get(f'stage0_m32_pac{pac}_l{lead}')
        values.append('미측정' if a is None else f"{a['worst_node_per_percent']:.2f}%" + (' (기존)' if a['reused'] else ''))
    lines.append(f'| {lead} | {values[0]} | {values[1]} |')
lines += ['', '| PAC/lead | TX 성공/예정 | RX | FWTO | PTO | SFD timeout | PHR | CRC | RXFSL | TX SYNC RX 오류 | 시각(KST, 시작–종료) |',
          '|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|']
for a in sorted(stage0.values(), key=lambda a: (a['conditions']['lead_us'], a['conditions']['rx_pac'])):
    p = a['conditions']; n = next(iter(a['nodes_by_serial'].values())); e = a['rx_counters_parsed']
    sync = next(l for l in a['raw_diagnostics_by_role']['N4'] if l.startswith('SYNC loss:'))
    se = re.search(r'RX errors=(\d+)', sync)[1]
    lines.append(f"| {p['rx_pac']}/{p['lead_us']} | {n['tx_success']}/{n['offered']} | {n['rx']} | " +
                 ' | '.join(str(e[k]) for k in ['fwto','pto','sfdto','phe','fce','fsl']) +
                 f" | {se} | {a['started_at_kst']}–{a['finished_at_kst']} |")
lines += ['', 'lead0의 0/2000 수신은 전이 탐색용 실패 자료다. PAC8/10µs는 TX 2000 성공에도 RX 1227, FWTO 773이었다. PAC8/15µs와 PAC4/10·40µs의 1개 손실은 각각 TX 1999 성공·SYNC RX 오류 1, RX 1999로 DATA 수신 오류와 구분된다. Stage0는 별도 beacon 수신 총계 및 Exp4 RDB/slot 검증 카운터를 출력하지 않으므로 0으로 추정하지 않는다. 로그에 있는 TX attempt/success, END, delayed-late, config 및 RX 오류 원문은 RESULTS.json에 보존했다.', '',
          '## 3. 빠른 탐색 후보', '']
for pac, c in selection['candidates_by_pac'].items():
    lines.append(f"- PAC{pac}: 후보 **lead {c['lead_us']}µs**, 이웃 {c['neighbours_us']}µs의 PER {c['per_percent']}%.")
lines += ['', '사전 기록한 기준은 연속 정수 3점 모두 PER<1%, 세 점의 최대 PER→평균 PER→20µs와의 거리→작은 lead 순이다. 전체 0~40µs의 전역 최적값이나 반복 확인을 완료한 값이 아니다. 후보 manifest의 frozen은 false로 유지한다. 회전 진단에만 별도 manifest의 설정을 고정했으며 evidence에 paper_qualified=false를 명시했다. **단일 링크 후보20µs는 아래 S6에서 실패하므로 네트워크 공통 설정으로 채택하지 않는다.**', '',
          '## 4. Exp4 부분 활성화·회전', '',
          'M32, G250, SB/SP=3000/2500µs, SF10ms, 1,000SF, 슬롯별 bounded delayed-RX와 기존 SPI 최적화. 물리 위치를 유지하고 block2 논리 역할만 회전했다. S2는 2슬롯 `23`; S6는 13슬롯 `2345672345673`이다. S2 비참여 TX 4대 정지를 전후 검증했다.', '',
          '| 물리 보드 | block2 논리 역할 | S2 활성 |', '|---|---|---|']
sample = next(a for a in rotations.values() if a['conditions']['sensors'] == 6)
for serial, n in sample['nodes_by_serial'].items():
    lines.append(f"| {n['physical_role']} / {serial} | N{n['logical_node']} | {'예' if n['logical_node'] in [2,3] else '아니오'} |")
for cid, a in rotations.items():
    p = a['conditions']; g = a['aggregate']
    lines += ['', f"### S{p['sensors']} / PAC{p['rx_pac']} / lead{p['lead_us']}µs / block2 — {a['verdict']}", '',
              f"{a['started_at_kst']}–{a['finished_at_kst']}. 전체 RX {g['rx']}/{g['offered']}, PER {g['per_percent']:.4f}%.", '',
              '| 물리 역할/serial | 논리 역할 | beacon/누락 | TX attempt/success | RX/offered | PER | TX late | END |',
              '|---|---|---:|---:|---:|---:|---:|---:|']
    for serial, n in a['nodes_by_serial'].items():
        lines.append(f"| {n['physical_role']}/{serial} | N{n['logical_node']} | {n['beacons']}/{n['beacon_missed']} | {n['tx_attempts']}/{n['tx_success']} | {n['rx']}/{n['offered']} | {n['per_percent']:.3f}% | {n['delayed_tx_late']} | {n['end_marker']} |")
    lines += ['', '```text']
    lines += [l for l in a['raw_diagnostics_by_role']['init'] if l.startswith(('RX timeouts=', 'TDMA validation:', 'EXP4_DOUBLE_BUFFER_CSV,', 'EXP4_DEFERRED_CSV,', 'EXP4_SPI_CSV,'))]
    lines += ['```', '', f"[원문·metadata·상태·readback]({cid}/results/)"]
lines += ['', '## 5. 해석과 한계', '',
          'PAC8/10µs 고손실과 15µs 이상에서의 낮은 손실, PAC4/5µs 성공은 PAC와 수신창 여유 시간의 상호작용을 지지한다. 이번 짧은-lead 손실은 FWTO 중심이며 과거 NLOS의 RXFSL 중심 N3 약50% 손실과 같은 현상으로 단정할 수 없다. 채널·시간 변동과 단일 실행의 한계가 있다.', '',
          '이번 실행은 현재 슬롯별 RX 경로다. continuous/manual-rearm과의 A/B가 아니므로 예전 burst 재수신 구조를 단일 원인으로 증명하거나 반박하지 않는다. 실제로 Stage0의 물리 N4에서 고른20µs가 S6의 N6/N7을 보호하지 못했다. PAC4/PAC8은 같은 block2·13슬롯 배정에서20µs와24µs를 비교했다. 시간 변동을 제거한 반복 교차 시험은 아니다.', '',
          '차량에서는 채널이 달라진다. 현재 결과는 차량 전 준비 점검이며 차량의 각 노드 PER<1% 보장이 아니다. 전체 논문 반복·6단계 역할 회전, 모든 M의 용량 탐색 및 차량 실측은 별도다. 이번 block2의 실행 성공과 전체 반복 완료를 구분한다.', '',
          '## 6. 빌드 중단 기록과 증거', '',
          'PAC4/25µs 첫 8-worker 소스 빌드가 비정상 종료했다. RF 전에 멈췄고 원인을 명시한 컴파일러 오류는 발견하지 못했다. 실패 준비 폴더·compiler log를 보존한 뒤 소스 수정 없이 1-worker build-only로 성공했고, 완성 이미지 검증 후 최초 RF 1회만 수행했다. [build_recovery.json](build_recovery.json).', '',
          '- [전체 수치·원문 진단·시각·수집/복원 증거](RESULTS.json)',
          '- [각 실행의 실제 serial/논리 역할/HEX·ELF SHA256](image_inventory.json)',
          '- [빠른 탐색 후보 근거](quick_selection.json)',
          '- [논문 동결 전 후보 manifest](candidate_manifest_not_paper_frozen.json)',
          '- [이번 회전 진단에만 고정한 manifest](rotation_diagnostic_manifest.json)',
          '- [원래 역할 복원: 로컬](restore_local.json), [원격](restore_remote.json)',
          '- [로컬 사후 상태](postflight_local.json), [원격 사후 상태](postflight_remote.json)', '']
(R / 'RESULTS.md').write_text('\n'.join(lines))
print(json.dumps({'new_stage0': report['new_stage0_runs'], 'reused_stage0': report['reused_stage0_runs'],
                  'exp4': {cid: a['verdict'] for cid, a in rotations.items()}, 'report': str(R / 'RESULTS.md')}, ensure_ascii=False, indent=2))
