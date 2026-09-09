# 과거 보드888 + 당시 바이너리 복원 — 2회 결과

2026-09-04 17:40–17:42 KST. 사용자 지시대로2회만 실행하고 종료했다. 보드/역할은8월25일과 동일한 INIT933/N2=584/N3=888/N4=818, 네 역할 HEX 모두8월25일 metadata의 hash와 일치. M32/PAC8/S3/G250/lead15/SB3000/SP2500/1000SF. 원래 위치·방향·케이블/허브 포트 유지라는 합의에 따라 보드만 교체한 조건이다. 위치/전파환경은 독립 계측하지 않았으며 환경 변경을 사실로 가정하지 않는다.

| 실행 | N2 PER | N3(888) PER | N4 PER | RX/3000 | 전체 PER | 판정 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| old_r1 | 0% | 55.1% | 0.6% | 2443 | 18.567% | FAIL_PER |
| old_r2 | 0% | 48.7% | 0.4% | 2509 | 16.367% | FAIL_PER |
| 합산 | 0% | 51.9% | 0.5% | 4952/6000 | 17.467% | FAIL_PER |

N3 RX449/1000,513/1000. 모든 TX의 비컨/시도/송신 성공1000/1000. 총6개 TX 캡처에서6000회 송신. 캡처 종료 마커, raw 및 firmware hash, 명시적serial, PHY/스케줄 및 네 역할 verifier 모두 검증 완료.

공통 기록 항목 wrong length/slot/superframe, delayed-RX/TX late, RDB mismatch/incomplete/recovered/resync/overrun은0. 과거 펌웨어에 없는 현대 SPI error/timeout 계측을0이라고 기록하지 않는다. first run의 rearm_count1999는 모든 예정 패킷 수와 같은 의미가 아니며 verifier 실패 항목이 아니었다. PHY손실은 별도PER에 반영하며 결과를PASS로 승격하지 않았다.

| 오류 이벤트 | r1 | r2 |
| --- | ---: | ---: |
| SFD timeout | 67 | 50 |
| PHR error | 49 | 51 |
| CRC error | 14 | 4 |
| RXFSL | 361 | 322 |
| 전체 RX error | 491 | 427 |

FWTO/PTO=0, 단 Exp4DATA burst의 FWTO 비활성이므로 무검출손실의 부재를 뜻하지 않는다. 이벤트 수는 패킷 손실 수와 일대일 대응하지 않으며 error-attribution slot은 추정이다.

## 의미와 한계

8월25일 같은 보드 역할과 같은 네 바이너리의 N3 PER1.8%가 오늘은48.7–55.1%로 재현되지 않았다. 앞선4212 동일조건 old/current/old가49.0/49.9/46.9%였던 사실과 함께, 최근 코드 수정이나4212교체만을 큰 고손실의 필요 원인으로 삼는 설명은 지지되지 않는다.

이것이 모든 소프트웨어 문제를 배제하거나 금속/환경 변화를 확정하지는 않는다. 과거 코드에도 존재하는 공통 수신 동작, 코디네이터 측 상태, 보드/RF조건 및 시점 사이의 미계측요인이 남아 있다. 원래 위치가 같다는 사용자 확인을 유지한다. 두 번의 결과를 과거 대규모환경과 통계적동등성검정으로 해석하지 않는다.

이번에는 추가버전교차·코드수정·세번째실험을 하지 않았다. 17:42 ps 확인 시 로컬/원격 rtt_capture/run_side/run_fixed_tx 프로세스 없음. 네 보드 최종 펌웨어는과거G250이며N3는888. 이전4212결과·manifest·동결소스는보존했다.

근거파일: 각 old_r1/old_r2의 init/init.log, tx/N2.log, tx/N3.log, tx/N4.log, 양쪽status.json, audit.json. 모든 수집console도 보존. 바이너리목록과 해시는manifest.json.
