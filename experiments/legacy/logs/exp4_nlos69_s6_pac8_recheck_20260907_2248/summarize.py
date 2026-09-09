import datetime
import hashlib
import json
import pathlib
import re

root = pathlib.Path(__file__).resolve().parent
bundle = root / 'capture1'
def read(path): return json.loads(path.read_text())
def save(path, obj): path.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + '\n')
case = read(bundle / 'case.json')
a = read(bundle / 'results/ASSESSMENT.json')
orchestration = read(bundle / 'results/orchestration.json')
raw = (bundle / 'results/local/init.log').read_text()
classifications = []
for line in raw.splitlines():
    if line.startswith('EXP4_SLOT_CLASS_CSV,'):
        classifications.append(dict(item.split('=', 1) for item in line.split(',')[1:]))
assert sum(int(x['count']) for x in classifications) == a['aggregate']['rx']
source_owner_mismatches = sum(int(x['count']) for x in classifications if x['src'] != x['observed_owner'])
assert source_owner_mismatches == 0
controls = {}
for job in case['jobs']:
    role = job['physical_role']
    side = 'local' if role == 'init' else 'remote'
    p = bundle / 'results' / side
    log = (p / (role + '.log')).read_text()
    meta = dict(line.split('=', 1) for line in (p / (role + '.meta.txt')).read_text().splitlines() if '=' in line)
    status = read(p / 'status.json')
    console = (p / (role + '.console.log')).read_text()
    controls[role] = {
        'serial': job['serial'],
        'ready_marker_count': log.splitlines().count('EXP_LOG_READY,channel=1'),
        'end_marker_count': log.splitlines().count('===== END STATS ====='),
        'collection_status': meta['collection_status'],
        'capture_timeout_reported': bool(re.search(r'^\[rtt_capture\].*(?:TIMEOUT|timed out)', console, re.M)),
        'readback': status['workers'][role]['readback'],
    }
    assert controls[role]['ready_marker_count'] == controls[role]['end_marker_count'] == 1
    assert controls[role]['collection_status'] == controls[role]['readback']['status'] == 'PASS'
    assert not controls[role]['capture_timeout_reported']
images = read(root / 'image_comparison.json')
assert all(v['matches_historical'] for v in images['images'].values())
payload = read(bundle / 'payload_hashes.json')
assert all(hashlib.sha256((bundle / p).read_bytes()).hexdigest() == expected for p, expected in payload.items())
err = re.search(r'RX timeouts=(\d+) \(fwto=(\d+) pto=(\d+)\)  RX errors=(\d+) \(sfdto=(\d+) phe=(\d+) fce=(\d+) fsl=(\d+) fint-only=(\d+) overrun=(\d+)\)', raw)
errors = dict(zip(['rx_timeout','fwto','pto','rx_error','sfd_timeout','phr_error','crc_error','rxfsl','fint_only','overrun'], map(int, err.groups())))
checks = read(root / 'postflight_comparison.json')
report = {
    'assessment': a,
    'orchestration': orchestration,
    'setup_confirmation': read(root / 'setup_confirmation.json'),
    'images': images,
    'controls': controls,
    'error_counters': errors,
    'wrong_source_observed_owner_count': source_owner_mismatches,
    'wrong_source_metric_note': 'Derived from completed-packet source-to-observed-slot classification; no separate wrong-source raw counter.',
    'postflight': checks,
    'valid_rf_runs': 1,
    'prepare_only_failed_attempt': 'run1 cache-only preparation lacked image; no hardware access. capture1 rebuilt exact historical HEX then performed the only RF run.',
    'conclusion': 'All six observed node PERs below 1% in this single current NLOS setup run; large high-loss behavior not observed. Residual PHY errors remain; no long-term or cross-environment guarantee.',
}
save(root / 'RESULTS.json', report)
kst = datetime.timezone(datetime.timedelta(hours=9))
def stamp(s): return datetime.datetime.fromisoformat(s).astimezone(kst).strftime('%Y-%m-%d %H:%M:%S KST')
lines = [
    '# NLOS 6.9m · M32/PAC8 · TX 6대 · 단일 재확인',
    '',
    '**이번 1회에서는 고손실 현상이 재현되지 않았다. 모든 노드의 관측 PER가 1% 미만이며 최악 노드는 N7의 0.70%다.** 손실은 N6 10개, N7 14개로 남아 있다.',
    '',
    f"수집·제어 시각: {stamp(orchestration['started_at'])} ~ {stamp(orchestration['finished_at'])}. 실제 1,000 superframe 구간은 약 10초(펌웨어 elapsed=9,999,998µs)다. RF 실행은 정확히 1회다.",
    '',
    '1. SSH·보드·배치',
    '',
    'SSH s-macbook-air 정상. 로컬 INIT 1050270933과 원격 TX 6대의 실물 J-Link serial을 대조했다. 시작·종료 시 잔여 RTT 캡처 프로세스는 없었고 종료할 잔여 프로세스도 없었다. 기존 로컬·원격 Git branch/HEAD/dirty/diff hash는 전후 동일하다.',
    '',
    '환경은 사용자가 재설치한 NLOS 6.9m이며 거리를 별도 계측하지 않았다. 노드별 상세 위치·높이·방향도 독립 계측하지 않고 현재 배치를 유지했다. TX 허브 외부 전원 어댑터 연결은 이번 사용자 답변으로 확인했다. 로컬 RX와 원격 노트북 아래 두 USB 허브 분기에 연결된 TX 6대의 port/locationID는 preflight/postflight에 보존했다. 차량 예정 위치명은 이번 실제 위치로 사용하지 않았다.',
    '',
    '2. 펌웨어·조건',
    '',
    '현재 Exp4 M32/PAC8/S6, G250µs, lead25µs, SB/SP=3000/2500µs, SF10ms, 13슬롯, 순서 2345672345673. 슬롯347µs, RX창122µs/FWTO119UUS, SFD8/SFD timeout33, 슬롯별 bounded scheduled delayed-RX 및 기존 SPI 최적화. DATA PSDU26B/응용 payload16B, 비컨 M256/PAC8. 기존 TDMA·비수신 구간 절전 정책과 TX 출력 유지, ACK/재전송 없음. 노드별 여러 슬롯은 별도의 예정 송신 기회다.',
    '',
    '모든 보드를 원래 역할로 명시적 serial 플래시했고 일곱 HEX 모두 이전 집 R13P8의 정확한 SHA256과 일치한다. 이번 펌웨어 C 수정은 없다. lead25는 이전 조건을 재생하기 위한 고정값이며 현 환경 Stage0 최적값이나 PAC 우열을 선정한 값이 아니다. 실험 전용 manifest의 frozen은 이 진단 조건 고정에만 해당하며 공통 미선정 manifest는 변경하지 않았다. 최초 run1 폴더는 캐시 이미지가 없어 준비 단계만 실패한 기록이며 RF를 실행하지 않았다.',
    '',
    '3. 노드별 관측 PER',
    '',
    '| 역할 | Serial | 예정/송신 성공 | RX | 손실 | PER |',
    '|---|---|---:|---:|---:|---:|',
]
for serial,n in a['nodes_by_serial'].items():
    lines.append(f"| {n['physical_role']} | {serial} | {n['offered']}/{n['tx_success']} | {n['rx']} | {n['offered']-n['rx']} | {n['per_percent']:.2f}% |")
lines += [
    '| 전체 | — | 13000/13000 | 12976 | 24 | 0.1846% |',
    '',
    '각 TX의 비컨 수신은 1000/1000, 비컨 누락·gap·중복 0, TX attempt=success=예정량, delayed-TX late 0이다. N3만 3슬롯×1000=3000회, 나머지는 2슬롯×1000=2000회다. 전체 평균과 별도로 각 노드 PER<1%를 판정해 이번 실행은 PASS다.',
    '',
    '4. 오류·수집 무결성',
    '',
    '| 카운터 | 횟수 |',
    '|---|---:|',
    '| RXFSL | 23 |',
    '| PHR error | 1 |',
    '| SFD timeout / CRC error / FWTO / PTO | 모두 0 |',
    '| delayed-RX late / delayed-TX late / SYNC delayed late | 모두 0 |',
    '| wrong slot / superframe / length / DATA config | 모두 0 |',
    '| source와 관측 슬롯 소유자 불일치 | 0 (수신 완료 패킷 분류에서 계산) |',
    '| RDB mismatch / incomplete / recovered / resync / overrun | 모두 0 |',
    '| deferred overflow / rearm deadline miss / SPI error·timeout·recovery | 모두 0 |',
    '| RTT 수집 timeout | 0 |',
    '',
    '13개 수신창은 각각 1000/1000회 arm됐고 late·timeout은 0이다. N6의 슬롯4/10에서 오류6/4회, N7의 슬롯5/11에서 오류5/9회(슬롯번호는0부터)가 발생했다. 나머지 슬롯 오류는0이다. RXFSL/PHR 세부 유형은 전체 RX 카운터이며 이 로그만으로 두 노드 각각에 정확한 세부 유형을 배분하지 않는다.',
    '',
    '7대 모두 READY/END 마커 각각1개, 수집·metadata·raw hash 검증 PASS, 지정 HEX flash readback PASS. 원격 결과 복사와 준비 payload 97개 파일 hash 확인도 PASS다. capture의 PER_limit100%는 실패 링크 원문도 보존하기 위한 수집 설정이며 최종 목표 판정은 별도 각 노드<1%다.',
    '',
    '5. 과거 결과와 비교',
    '',
    '이전 집 R13P8은 같은 일곱 HEX와 슬롯 배정에서 N4 PER16.30%/9.90%였다. 이번 N4는0%이고 과거 NLOS에서 취약했던 N3도0%다. 반면 이번 손실은 N6/N7에 집중됐다. 따라서 현재 재설치 상태에서는 과거처럼 큰 손실이 관찰되지 않았다.',
    '',
    '9월4일 NLOS의 N3 48.7~55.1%는 S3·lead15·burst 재수신이므로 이번 S6·lead25·슬롯별 RX와 직접 동일 조건 비교가 아니다. 이번 RXFSL23회는 과거의 RXFSL 우세 양상과 오류 종류는 겹치지만 노드·발생률·수신 경로가 다르므로 같은 원인이라고 단정하지 않는다.',
    '',
    '6. 해석과 다음 단계',
    '',
    '이번 한 번으로 문제 해결이나 모든 환경 PER<1%를 확정하지 않는다. 특히 N7의 packet-level Wilson95 구간은 약0.417~1.172%로 1%를 걸치며, 약10초 관측은 장기 안정성을 검증하지 않는다. 동일 NLOS6.9m라는 이름도 과거와 채널이 완전히 같다는 증거가 아니다.',
    '',
    '현재 이미 슬롯별 delayed-RX이고 이번에는 낮은 PER이므로 continuous/manual-rearm 단독 원인 가설을 이번 결과로 증명할 수 없다. 기존 집에서는 동일한 슬롯별 RX HEX에서도 높은 PER이 있었으므로 환경·링크별 수신 조건의 영향을 함께 검토해야 한다. RXFSL만으로 금속·잡음·다중경로 중 단일 원인을 특정할 수 없다.',
    '',
    '사용자 요청 범위인 단일 재확인을 마쳤으며 반복·A/B·PAC/lead 변경은 실행하지 않았다. 본 실험 준비를 이어갈 때는 현재 배치에서 PAC별 lead를 실측 선정하고 취약 노드 N6/N7을 포함한 최악 노드 기준으로 비교하는 것이 다음 단계다. Git commit/push는 하지 않았다.',
    '',
    '7. 역할별 실제 HEX SHA256',
    '',
    '| 역할 | Serial | SHA256 |',
    '|---|---|---|',
]
for role,img in images['images'].items():
    lines.append(f"| {role} | {img['serial']} | {img['sha256']} |")
lines += [
    '',
    '원문: capture1/results/local/init.log 및 capture1/results/remote/N2.log~N7.log. 기계 판정: capture1/results/ASSESSMENT.json. 역할·조건·이미지 경로: capture1/case.json. 제어·readback: capture1/results/{local,remote}/status.json. 외부 전원 추가 확인: setup_confirmation.json. 전체 요약: RESULTS.json.',
    '',
]
(root/'RESULTS.md').write_text('\n'.join(lines))
print(json.dumps({'verdict':a['verdict'],'nodes':{n['physical_role']:n['per_percent'] for n in a['nodes_by_serial'].values()},'errors':errors,'rf_runs':1,'report':str(root/'RESULTS.md')},ensure_ascii=False,indent=2))

