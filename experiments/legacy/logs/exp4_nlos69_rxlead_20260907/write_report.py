from pathlib import Path
import json
r=Path(__file__).resolve().parent
d=json.loads((r/'RESULTS.json').read_text());m=json.loads((r/'manifest.json').read_text())
p=d['groups']['P25_all'];c=d['groups']['P25_confirmation'];b=d['groups']['L15_reference']
assert len(p['runs'])==4 and p['each_run_goal_pass']
assert len(c['runs'])==3 and c['each_run_goal_pass']
best=[x for x in d['runs'] if x['variant']=='P25']
err={k:sum(x['errors'][k] for x in best) for k in best[0]['errors']}
lines=[f'''# M32/PAC8 NLOS 6.9m · 각 노드 PER<1% 확인

**슬롯별 scheduled delayed-RX + RX lead25µs + 기존 SPI 최적화(P25)** 조합으로, 동일 HEX 4회 모두 각 노드 PER<1%를 충족했다. 노드당4000 offered 합산 N3 PER은 **{p['nodes']['N3']['per_pct']:.3f}%**, N2/N4는0%다. 목표 판정은 전체 평균이 아닌 각 노드 기준이다.

## 적용 방법

- 이전 B의 각 슬롯 절대시각 예약·유한 FWTO·단일 수신시도 구조 유지.
- INIT `BRRS_RX_LEAD_MARGIN_US=25`: 기존15µs보다10µs 일찍 RX를 연다. M32/SFD 모델 기준 RMARKER67µs 전에 예약한다.
- 요청 RX 창122µs, FWTO119UUS≈122.0464µs. 창 종료 예정시각은 데이터 끝 부근이며, UUS 올림에 따른 소수µs 차이는 존재한다. 후속 슬롯에 즉시RX fallback 없음.
- 기존 `--spi-opt` 사용: `BRRS_EXP4_SPI_PERSISTENT=1`, `BRRS_EXP4_SPI_DIRECT=1`. SPI 주파수는 기존32MHz 그대로이며 DATA burst 동안 SPIM 세션을 유지하고 직접 SPI 전송을 사용한다. RX 메타데이터를 CIA 완료 후 저장한 뒤 다음 창을 예약하는 순서를 유지한다.
- TX는 보존된 정확한 기존 세 HEX 그대로. M32/PAC8/S3/G250/SB3000/SP2500/SF10000µs/1000SF/PSDU26B/app16B/owners234 유지. 재전송, 출력 변경, PAC 변경, 위치 변경 없음.

실측 이벤트 감지→버퍼 반환 최대는 이전15µs B 약337µs에서 P25 약239µs로 감소했다(아래 raw 통계 참조). 처리 시간 단축으로 다음 슬롯 예약 여유를 확보하려는 조합이다. 수신기에 머무는 잡음이 단일 원인임을 증명한 것은 아니다.

## 연결 및 보존

SSH `s-macbook-air` 정상. 로컬 INIT1050270933, 원격 N2=1050211584/N3=1050273888/N4=1050282818를 J-Link 실제 열거로 확인했다. 관련 없는 보드에 플래시하지 않았다. 시작 시 잔여 RTT 실험 프로세스 없음.

현재 NLOS6.9m, TX GenesysLogic USB2.1 Hub에 외부 전원 어댑터 연결(사용자 확인). 포트는 N2=0x01130000/N3=0x01140000/N4=0x01120000. 보드 배치·방향·케이블·허브 포트를 유지했다. 과거 집 환경 결과와 합산하지 않았다.

## 모든 실행

각 실행은 노드당1000 offered다. PER 수치가 낮아도 구조적 검증에 실패하면 PASS로 판정하지 않는다.

| 실행 | RX lead / SPI | N2 RX / PER | N3 RX / PER | N4 RX / PER | 판정 |
|---|---|---:|---:|---:|---|''']
for x in d['runs']:
    node=x['nodes'];params=m['parameters_by_variant'][x['variant']]
    cells=[f"{node[n]['rx']} / {node[n]['per_pct']:.1f}%" for n in ['N2','N3','N4']]
    status=x['goal'] if x['valid'] else ('FAIL_RX0' if x['variant']=='L5' else 'FAIL_schedule_late1')
    lines.append(f"| {x['tag']} | {params['lead_us']}µs / {'최적화' if params.get('SPI_OPT') else '기존'} | "+' | '.join(cells)+f' | {status} |')
lines.extend(['', 'L5r1은 전체 RX0으로 실패했다. 각 TX1000송신 및 수신창 예약1000/late0을 확인했지만 FWTO2761/PHYerror239가 발생했다. 더 작은0µs는 생략했다. L25r2는 N3 0.5%였으나 CIA/RDB readiness 대기1회와 N4 슬롯 예약late1회가 있어 유효 비교에서 제외하고 원본을 보존했다. 이 실행을 실패로 보존하고, SPI 처리시간을 줄인 새 P25를 별도로 평가했다.','', '## 기준과 최종 반복 합산','', '| 구분 | N2 RX / PER | N3 RX / PER | N4 RX / PER |','|---|---:|---:|---:|'])
for label,g in [('15µs 기준2회',b),('P25 후보확인3회(P25r2–4)',c),('P25 전체4회(P25r1–4)',p)]:
    lines.append('| '+label+' | '+' | '.join(f"{g['nodes'][n]['rx']}/{g['nodes'][n]['offered']} / {g['nodes'][n]['per_pct']:.3f}%" for n in ['N2','N3','N4'])+' |')
ci=c['nodes']['N3']['wilson_95_pct']
lines.extend(['',f"최종 후보를 고른 첫 실행과 분리한 확인3회도 각 실행에서 세 노드 모두1% 미만이다. 확인3회 N3의 Wilson 양측95% 구간은 {ci[0]:.3f}–{ci[1]:.3f}%다. 이 구간은 독립 Bernoulli 가정의 참고값이다. NLOS의 시간상관·채널 변동이 있으면 가정이 달라지므로 장시간 또는 다른 배치의 성능 보증으로 해석하지 않는다. 총 RF 구간은 최종4회 합계 약40초다.", '', '## 오류 및 수집 검증','', '| 실행 | RXerr | SFD timeout | PHR | CRC | RXFSL | FWTO/PTO |','|---|---:|---:|---:|---:|---:|---:|'])
for x in d['runs']:
    e=x['errors'];lines.append(f"| {x['tag']} | {e['rx_errors']} | {e['sfd_timeout']} | {e['phr']} | {e['crc']} | {e['rxfsl']} | {e['fwto']}/{e['pto']} |")
lines.extend(['',f"P25 합계 RXerr{err['rx_errors']}: RXFSL{err['rxfsl']}, PHR{err['phr']}, CRC{err['crc']}, SFD timeout{err['sfd_timeout']}, FWTO/PTO{err['fwto']}/{err['pto']}. PHY 오류는 손실로 포함했다. 최종4회 모두 각 TX beacon/attempt/success=1000/1000/1000, beacon miss0, delayed-TX late0, END1이다.", '', '최종4회 모두 wrong length/slot/superframe, config error, delayed-RX late, RDB mismatch/incomplete/recovered/resync/overrun, SPI error/recovery 및 수집 timeout0. 각 창 attempted=armed=1000/late0/good+timeout+error=1000, source→slot owner 매칭과 전체 합계를 확인했다. READY/END 마커와 종료코드 및 네 HEX populated bytes의 flash readback을 매 실행 확인했다. 독립 wrong-source 카운터가 없는 포맷은 source/owner 일치로 검증했다.', '', '| P25 실행 | hotpath 최대µs | SPI begin/end | 직접 전송수 |','|---|---:|---:|---:|'])
for x in best:
    z=x['measured'];sp=z['EXP4_SPI_CSV'];lines.append(f"| {x['tag']} | {z['EXP4_HOT_PATH_CSV']['max_us']} | {sp['begin']}/{sp['end']} | {sp['direct_xfers']} |")
lines.extend(['', '## 해석과 범위', '', '이번 개선은 수신 창을 무조건 좁히는 방향이 아니었다. 5µs는 RX0,15µs 기준은 N3 2.7%/2.5%,25µs는 0.6%/0.5%였다. 더 일찍 RX를 준비하는 것이 유리하다는 실측 근거다. 실제 RF 수신기 ON 시점·AGC·상관기·오류 패킷 CIR을 직접 측정하지 않았으므로 준비 지연, 프리앰블 획득/축적, 내부 동기화 중 하나로 단정하지 않는다.', '', '25µs만으로는 드물게 CIA 대기→다음 슬롯 예약late가 나타났으므로 처리시간 단축을 결합했다. P25 반복에서 이 지연은0이다. SPI 최적화가 RF 복호 오류를 독립적으로 얼마나 줄였는지는 별도의 완전한2×2 비교를 수행하지 않아 분리하지 않는다.', '', 'DW3000 scheduled-RX도 프리앰블 탐색을 수행한다. 이상적 BRRS의 SFD/PHR·검출 생략 PHY를 구현한 것은 아니다. 현재 하드웨어의 M32/TDMA/슬롯별 수신창을 유지한 개선이다. 수신창 밖의 RF idle 및 기존 TX 비컨 수신 예약 정책을 유지했다. 전류/에너지는 직접 계측하지 않았으며 deep sleep 달성을 주장하지 않는다.', '', '참고: [DW3000 User Manual §4.1/4.3, §8.2.7](https://caramelfur.dev/docs/DW3000-User-Manual/DW3000-User-Manual.html). RX lead25µs는 이번 장치와 세팅의 실측 선택값이며 매뉴얼의 보편적 권장값으로 제시하지 않는다.', '', '## 이미지와 재현', '', '| 최종 역할 | serial | SHA256 |','|---|---|---|'])
for role,v in m['variants']['P25'].items():lines.append(f"| {role} | {v['serial']} | `{v['sha256']}` |")
lines.extend(['', '독립 소스: `'+m['source_trial']+'`. 기존 B runtime 소스는 동일하며 빌드 옵션만 바꿨다. 사본 경로가 `__FILE__` 문자열을 바꾸지 않도록 빌드 helper에 macro prefix mapping만 추가했다. lead15의 기준 HEX가 이전 B와 byte-identical임을 확인했다. 정규화 전 빌드는 `images/L15_unflashed_pathdiff`에 보존했고 플래시하지 않았다.', '', '최종 재빌드 명령(위 독립 소스에서):', '', '```sh', 'bash Drivers/API/brrs_exp4_build.sh 32 3 250 init 25 --pac 8 --sync-buffer 3000 --sync-prep 2500 --cycles 1000 --slotted-rx --spi-opt','```', '', '최종 HEX는 `images/P25/init.hex`. 실제 serial=1050270933, RTT=0x200000a4. TX는 `images/P25/N2.hex`, `N3.hex`, `N4.hex`이며 RTT0x200000b4. 각 run에 immutable manifest, raw log, status, audit, flash readback과 orchestration 시각이 있다. manifest v1 및 이전 캠페인을 보존했다. 실행 순서는 `actual_order.json`이 기준이며 최초 예정된0µs/후속 L25 반복의 생략 사유는 `EXPERIMENT_PLAN.md`에 있다.', '', '원본 저장소의 branch/HEAD/dirty/diff hash 및 USB 연결 보존 여부는 `postflight_verification.json` 참조. commit/push 없음. 종료 시 INIT은 최종 P25, TX는 기존 세 HEX이며 END 이후 캡처를 종료했다.', '', '## 실행 시각(KST)',''])
for x in d['runs']:lines.append(f"- {x['tag']}: {x['started_at']} – {x['finished_at']}")
(r/'RESULTS.md').write_text('\n'.join(lines)+'\n')
print(r/'RESULTS.md')
