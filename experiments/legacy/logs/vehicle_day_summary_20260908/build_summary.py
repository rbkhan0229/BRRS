"""Offline consolidation of the seven 2026-09-08 vehicle attempts. No hardware."""
import hashlib,json,re
from datetime import datetime
from zoneinfo import ZoneInfo
from pathlib import Path
R=Path(__file__).resolve().parent;L=R.parent
specs=[
('vehicle_glovebox_pac8_l26_once_20260908','글러브박스',256,'N2/N3 앞범퍼, 원래 방향'),
('vehicle_dashboard_pac8_l26_once_20260908_1738','대시보드',256,'RX 위치만 변경'),
('vehicle_dashboard_beacon512_pac8_l26_once_20260908','대시보드',512,'비컨 512 적용'),
('vehicle_dashboard_b512_n2flipped_once_20260908','대시보드',512,'N2만 뒤집음; N3 앞범퍼'),
('vehicle_center_console_b512_n2flipped_once_20260908','가운데 수납함 위',512,'N2 뒤집은 상태; N2/N3 앞범퍼'),
('vehicle_ceiling_light_b512_n2flipped_once_20260908','천장 전등',512,'N2 뒤집은 상태; N2/N3 앞범퍼'),
('vehicle_rearbumper_n2_dashboard_n3_console_rx_b512_once_20260908','가운데 수납함 위',512,'N2 뒷범퍼·N3 대시보드로 이동')]
runs=[]
for i,(name,loc,beacon,change) in enumerate(specs,1):
 path=L/name/'RESULTS.json';d=json.loads(path.read_text());invalid=d.get('valid_1000sf_run') is False
 c=json.loads((L/name/'capture1/case.json').read_text())
 ns=d['nodes'];normalized=[]
 for role in ['N2','N3','N4','N5','N6','N7']:
  x=ns[role] if isinstance(ns,dict) else next(x for x in ns if x['role']==role)
  job=next(j for j in c['jobs'] if j['physical_role']==role)
  if invalid:
   snap=x['snapshot_counters'];row=dict(role=role,serial=x['serial'],offered=None,rx=None,per_percent=None,
      beacons=snap['exp4_sync_frames_received'],tx_attempts=snap['total_tx_attempts'],tx_success=snap['own_tx_success'],tx_late=snap['total_tx_delayed_late'],observation='partial; different halt times')
  else:
   offered,rx=x['offered'],x['rx'];per=(offered-rx)*100/offered
   assert abs(per-x.get('offered_loss_percent',x.get('offered_per_percent')))<1e-8
   assert offered==1000*c['conditions']['slot_owners'].count(role[1:])
   row=dict(role=role,serial=x['serial'],offered=offered,rx=rx,per_percent=per,
       beacons=x.get('beacons_received',x.get('beacons')),tx_attempts=x['tx_attempts'],tx_success=x['tx_success'],tx_late=x['tx_late'],
       observation='RX complete; failed TX END/timeout remains failure where applicable')
  row['hex_sha256']=job['hex_sha256'];normalized.append(row)
 line=d.get('rx_error_summary') or d.get('counter_source_lines',{}).get('RX timeouts=')
 errors={}
 if line:
  for k in ['fwto','pto','sfdto','phe','fce','fsl','overrun']:
   match=re.search(r'\b'+k+r'=(\d+)',line);assert match;errors[k]=int(match[1])
 aggregate=None
 if not invalid:
  offered=sum(x['offered'] for x in normalized);rx=sum(x['rx'] for x in normalized);tx=sum(x['tx_success'] for x in normalized)
  assert offered==13000 and rx==d['aggregate']['rx'] and tx==d['aggregate']['tx_success'] and 0<=rx<=tx<=offered
  aggregate=dict(offered=offered,rx=rx,tx_success=tx,offered_per_percent=(offered-rx)*100/offered,data_loss_after_tx_percent=(tx-rx)*100/tx if tx else None)
 t=d['orchestration'];local=lambda v:datetime.fromisoformat(v).astimezone(ZoneInfo('Asia/Seoul')).strftime('%H:%M:%S')
 runs.append(dict(number=i,source=str(path),source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),source_report=str(path.with_suffix('.md')),
   rx_location=loc,beacon_symbols=beacon,change=change,start_kst=local(t['started_at']),finish_kst=local(t['finished_at']),
   rx_1000sf_complete=not invalid,whole_collection='INCOMPLETE_FAIL',nodes=normalized,aggregate=aggregate,rx_errors=errors,rx_error_source=line,
   payload_index_sha256=t['payload_index_sha256'],hex_hashes={j['physical_role']:j['hex_sha256'] for j in c['jobs']},
   control_counters=d.get('counter_source_lines',{})))
assert len(runs)==7 and sum(x['rx_1000sf_complete'] for x in runs)==6
assert runs[0]['hex_hashes']==runs[1]['hex_hashes']
assert all(x['hex_hashes']==runs[2]['hex_hashes'] for x in runs[2:])
data={'experiment_date_kst':'2026-09-08','compiled_at':datetime.now().astimezone().isoformat(),'scope':'vehicle Kona only; excludes early-hours NLOS preparation',
 'new_rf_runs_during_summary':0,'attempts':7,'rx_complete_attempts':6,'full_seven_board_collection_pass':0,'per_all_nodes_below_1_pass':0,'runs':runs}
(R/'SUMMARY.json').write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
lines=['# 2026-09-08 현대 코나 차량 실험 정리','',
 '**차량 Exp4를 서로 다른 조건으로 총 7회 실행했다. RX 1,000SF 완료는 6회이며, TX 6대 모두 PER <1%인 조건은 없었다. 범퍼의 비컨 획득·유지 실패와, 실내·트렁크의 DATA 수신 손실이 함께 관측됐다.**','',
 '사용자가 말한 “오늘 차에서 한 실험”은 로그 날짜 기준 2026년 9월 8일이다. 9월 8일 새벽 NLOS 준비 실험은 아래 차량 결과에 포함하지 않았다. 이번 정리는 저장된 자료만 읽어 작성했으며 SSH·보드 접근·추가 RF·빌드·플래시를 하지 않았다. 재개 지시 전 실험을 실행하지 않는다.','',
 '## 공통 조건','',
 '- 차량: 사용자 설명상 최신형 현대 코나, 내연기관. 정확한 연식/트림 미기록.',
 '- 시동 켜짐, TX 허브 외부 전원 연결, 모든 문 닫힘을 확인했으며 뒤 실행은 별도 변경 보고가 없으면 이 조건을 이어 기록했다. 창문·보닛·트렁크의 개별 개폐 상태를 추가 확인했다고 주장하지 않는다.',
 '- DATA M32/PAC8, lead 26µs, TX 6대, 13슬롯(`2345672345673`), G250, SB/SP 3000/2500µs.',
 '- 주기 10ms, 목표 1,000SF(약 10초 DATA 측정). N3 예정 3,000개, 나머지 TX 각 2,000개, 합계 13,000개.',
 '- 모든 DATA 슬롯에 bounded scheduled delayed-RX. 수신창 123µs/FWTO 120UUS, SPI 최적화, 재전송 없음.',
 '- 비컨 PAC8 유지. 1–2번은 M256, 3–7번은 M512. 차량에서 PAC4 또는 lead sweep은 하지 않았다. lead26은 NLOS 선별값이며 차량 최적점으로 확정하지 않았다.',
 '- 물리 보드 역할은 고정했다. 실험 간 사용자가 RX 위치, N2 방향, 마지막에는 N2/N3 위치를 변경했다.','',
 '## 노드 역할','', '| 역할 | Serial | 최초 배치 | 마지막 배치 |','|---|---|---|---|',
 '| INIT/RX | 1050270933 | 글러브박스 | 운전석/조수석 사이 수납함 위 |',
 '| N2 | 1050211584 | 앞범퍼 A, 플라스틱 바깥 | 뒷범퍼 |',
 '| N3 | 1050273888 | 앞범퍼 B, 플라스틱 바깥 | 대시보드 |',
 '| N4 | 1050282818 | 운전석 | 동일 |','| N5 | 1050208509 | 조수석 | 동일 |',
 '| N6 | 1050227627 | 트렁크 A | 동일 |','| N7 | 1050204212 | 트렁크 B | 동일 |','',
 'N2/N3의 앞범퍼 좌우와 A/B 간 정확한 대응은 추정하지 않는다. N2를 뒤집은 것은 4번부터이며, 7번 뒷범퍼 재부착 후 방향은 미기록이다.','',
 '## 실행별·노드별 PER','',
 '**표의 PER은 예정 전송 대비 미전달 비율이며, 비컨 미수신으로 송신하지 못한 슬롯도 포함한다. TX=0인 노드의 100%를 송신 후 PHY PER로 해석하지 않는다.**','',
 '| # | RX 위치 / 변경 | 비컨 | N2 | N3 | N4 | N5 | N6 | N7 |','|---|---|---:|---:|---:|---:|---:|---:|---:|']
for run in runs:
 vals=[f"{x['per_percent']:.2f}%" if x['per_percent'] is not None else '—' for x in run['nodes']]
 lines.append(f"| {run['number']} | {run['rx_location']} / {run['change']} | {run['beacon_symbols']} | "+' | '.join(vals)+' |')
lines+=['','2번은 INIT 최종 통계 전에 중단돼 PER 산출 불가. 나머지 6회도 범퍼 TX의 timeout/END 실패가 있어 전체 7보드 수집은 INCOMPLETE/FAIL이다. RX 완주로 산출한 예정 대비 손실과 정상 수집 판정은 구분한다. 각 조건 1회 관측이며, 다른 실행의 노드별 최저값을 합쳐 전체 성공으로 판단하지 않는다.','',
 '## 실행 시각과 전체 수신','', '| # | 제어 시작–종료(KST) | RX 1000SF | 전체 RX / 예정 | 실제 TX | 예정 대비 손실 | 송신 후 DATA 손실 |','|---|---|---|---:|---:|---:|---:|']
for run in runs:
 a=run['aggregate'];v=[f"{a['rx']:,} / {a['offered']:,}",f"{a['tx_success']:,}",f"{a['offered_per_percent']:.2f}%",f"{a['data_loss_after_tx_percent']:.2f}%"] if a else ['미확보']*4
 lines.append(f"| {run['number']} | {run['start_kst']}–{run['finish_kst']} | {'완료' if run['rx_1000sf_complete'] else '중단'} | "+' | '.join(v)+' |')
lines+=['','제어 시간에는 플래시/READY 준비·END 대기·로그 복사가 포함된다. DATA 측정 시간 자체와 같지 않다. 7번 직전 Wi-Fi 문제로 SSH가 실패한 시도는 RF 시작 전 실패였으므로 추가 RF 횟수로 세지 않았다.','',
 '## 범퍼 노드의 비컨과 송신','', '| # | N2 비컨 | N2 TX 완료 | N2 DATA RX | N3 비컨 | N3 TX 완료 | N3 DATA RX |','|---|---:|---:|---:|---:|---:|---:|']
for run in runs:
 v=[]
 for x in run['nodes'][:2]:v.extend([str(x['beacons']),str(x['tx_success']),str(x['rx']) if x['rx'] is not None else '미확보'])
 lines.append('| '+str(run['number'])+' | '+' | '.join(v)+' |')
lines+=['','2번은 정지 시점의 부분 누계이며 완주한 1,000SF 비컨 PER로 변환하지 않는다. 다른 실행도 조기 종료 노드의 비컨 누계는 전체 관측 기간의 독립적인 비컨 수신률로 쓰지 않는다.',
 '완주한 실행에서 N4–N7은 모두 비컨 1,000개 수신, TX attempt=success=2,000회, delayed-TX late=0이다. 이들의 DATA 손실은 비컨 미수신이나 미송신으로 설명되지 않는다. 7번 N3도 비컨 1,000개·TX 3,000회 완료했다.','',
 '## RX 오류 카운터','', '| # | FWTO | PTO | SFD timeout | PHR | CRC | RXFSL | overrun |','|---|---:|---:|---:|---:|---:|---:|---:|']
for run in runs:
 e=run['rx_errors'];lines.append('| '+str(run['number'])+' | '+' | '.join(str(e[k]) if e else '미확보' for k in ['fwto','pto','sfdto','phe','fce','fsl','overrun'])+' |')
lines+=['','RX 최종 통계를 확보한 6회에서 wrong slot/superframe, delayed-RX late, RDB mismatch/incomplete/resync/overrun 및 관련 제어 오류는 기록상 0이다. 2번은 미확보이며 0으로 채우지 않는다. wrong source의 독립 카운터는 별도로 제공되지 않은 경우 미확보로 둔다.',
 'SFD timeout이 DATA RX 오류의 대부분이었다. 위 카운터는 코디네이터 DATA 수신 구간으로, 범퍼 TX의 비컨 오류 원인에 그대로 적용하면 안 된다. FWTO/오류 이벤트 수가 항상 서로 독립적인 손실 패킷 수를 의미하는 것도 아니다. TX 쪽 비컨 오류는 현재 합계만 기록해 세부 실패 단계를 확정하기 어렵다.','',
 '## 확인된 결론과 해석 한계','',
 '1. **현재 배치와 설정으로 6개 링크를 동시에 만족시키지 못했다.** N2는 모든 RX 완주 조건에서 DATA 수신 0개였다. 뒷범퍼 이동 후에도 비컨 5개·TX 10회에 그쳤다.',
 '2. **N3는 대시보드로 옮긴 마지막 실행에서 통신을 유지했다.** 비컨 1,000개, TX 3,000개, RX 2,958개, PER 1.40%였다. 같은 N3와 같은 512 펌웨어가 다른 배치에서 동작한 것은 위치/전파 경로 영향을 지지하지만, N2·N3·RX를 함께 옮긴 1회이므로 N3 위치 하나의 인과 효과나 하드웨어 완전 정상까지 단정하지 않는다.',
 '3. **RX 위치 변경의 이득은 노드마다 달랐다.** 천장 전등은 N4 0.05%였지만 트렁크 N6/N7은 10.35%/24.35%였다. 수납함 위의 5번은 N4/N7 0.70%였지만 N5 9.20%, 범퍼 100%였다. 전체 성공 배치로 선정할 근거가 없다.',
 '4. **비컨 512 적용 누락이나 공통 타이밍 불일치는 발견하지 못했다.** 송수신 양쪽의 소스·7개 ELF·실행 설정·실제 flash readback을 확인했다. 비컨 길이/SFD timeout/수신창 계산은 512에 맞게 바뀌었고 DATA 설정은 같았다. 별도 RF 파형 길이를 직접 측정한 것은 아니다.',
 '5. **256과 512가 동등한 성능이라는 결론도 아직 불가하다.** 대시보드 256은 중간 종료됐고 512부터 수집기가 다른 노드를 끝까지 기다리도록 보완됐다. N3 비컨 3→28도 같은 완주 관측창의 비율 비교가 아니다.',
 '6. **기존 비컨 단절 100ms 후 조기 종료 정책이 관측을 제한한다.** 첫 비컨을 받은 뒤 장시간 끊기면 해당 TX가 종료해 남은 구간 재획득을 시도하지 않는다. 256/512 모두에 존재하며 아직 수정하지 않았다. 첫 비컨을 아예 받지 못한 노드의 실패 원인을 이것으로 설명할 수는 없다.',
 '7. **이날 차량 시험은 이미 슬롯별 delayed-RX였다.** 따라서 현재 차량 손실을 과거 continuous/manual-rearm 구조 하나로 설명할 수 없다. 수신 경로 전체와 채널/안테나 방향 영향은 추가 분리가 필요하며, 금속·잡음·RXFSL 중 하나를 단일 원인으로 확정하지 않는다.','',
 '## 이날 수행한 변경과 종료 상태','',
 '- 3번부터 독립 차량 SDK에서 비컨 256→512를 적용했다. DATA M32/PAC8·lead26은 유지했다. 3–7번의 7개 HEX hash는 모두 서로 같다.',
 '- 수집 중 한 TX 실패가 다른 노드의 관측을 조기에 끊던 동작을 보완했다. 실패 판정은 유지했다.',
 '- 각 실행 종료 기록에 실제 flash readback과 보드 정지 확인이 있다. 기존 원본 Git 저장소는 변경하지 않았으며 commit/push도 하지 않았다.',
 '- 이후 맥북에어 한 대에 7개 보드를 연결하는 단독 수집 도구를 준비하고 무선 없는 테스트 8개 및 SSH 종료 후 작업 지속을 검증했다. 이것은 8번째 차량 RF 실행이 아니다.',
 '- 마지막 준비 점검(9월 8일 21:07 KST)은 SSH 정상·J-Link 0대였다. 이후 연결 상태를 다시 확인한 것은 아니다. 새 실행은 사용자가 재개를 요청하면 진행한다.',
 '- 9월 8일 21:05부터 4시간짜리 임시 잠자기 방지를 설정했다. 계속 유효하다고 가정하지 말고, 재개할 때 전원/잠자기/SSH/보드 7대 연결을 확인한다.','',
 '## 다음 재개 시 검토할 순서 — 이번에는 미실행','',
 '1. 맥북에어의 7대 연결과 실제 위치·방향·시동·전원 상태를 기록한다.',
 '2. 비컨 단절을 최종 종료와 분리해 전체 관측 시간을 유지하고, 비컨 오류 단계를 분리하는 진단을 먼저 검토한다. 놓친 슬롯은 손실로 남겨 재전송으로 숨기지 않는다.',
 '3. 배치를 고정한 256/512 비교로 비컨 효과를 판정한다. 현재 1회씩의 위치 탐색 결과를 차량 최적 설정으로 확정하지 않는다.','',
 '## 원본 및 검토 문서','']
for run in runs:lines.append(f"- [{run['number']}번: {run['rx_location']} / 비컨 {run['beacon_symbols']}]({run['source_report']})")
lines += [f"- [비컨 512 구현 검토]({L/'vehicle_beacon512_review_20260908/REVIEW.md'})",f"- [단독 수집 준비 기록]({L/'vehicle_single_host_ready_20260908/README.md'})",'',
 '## 사용한 HEX SHA256','', '| 역할 | Serial | 비컨 256 (1–2번) | 비컨 512 (3–7번) |','|---|---|---|---|']
manifest=json.loads((L/specs[0][0]/'manifest.json').read_text())
for role,b in manifest['boards'].items():lines.append(f"| {role} | {b['serial']} | `{runs[0]['hex_hashes'][role]}` | `{runs[2]['hex_hashes'][role]}` |")
lines+=['','상세 기계 판독 자료와 원본 JSON SHA256은 `SUMMARY.json`에 보존했다.','']
(R/'SUMMARY.md').write_text('\n'.join(lines))
print(json.dumps({'report':str(R/'SUMMARY.md'),'attempts':7,'rx_complete':6,'new_rf':0,'arithmetic_and_image_consistency':'PASS'},ensure_ascii=False))
