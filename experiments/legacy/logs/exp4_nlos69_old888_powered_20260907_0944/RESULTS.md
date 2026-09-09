# NLOS 6.9m · 유전원 허브 · 과거 G250 1회 결과

**N3(1050273888) PER19.4%로 고손실 재현. 각 노드 PER<1% 목표는 실패.** 사용자 요청대로 한 번만 실행하고 종료했다.

1. SSH 및 보드 연결 상태

`ssh s-macbook-air` 정상. 로컬 INIT1050270933, 원격 TX1050211584/1050273888/1050282818을 실제 J-Link 열거로 확인했다. 4212 및 실행 전·후 잔여 캡처 프로세스는 없었다. 관련 없는 프로세스/데이터를 종료하거나 삭제하지 않았다.

허브는 **외부 전원 어댑터 연결 상태**라고 사용자가 명시했다. GenesysLogic USB2.1 Hub 0x01100000; 818=0x01120000, 584=0x01130000, 888=0x01140000. 집에서의 노트북 USB 전원만 사용 조건과 분리 기록했다. 현재 배치는 사용자 확인 NLOS6.9m이며 agent는 위치·방향·케이블·포트·전원을 변경하지 않았다.

2. 역할·HEX·실험 조건

M32/PAC8/S3/G250/lead15us/SB3000us/SP2500us/1000SF, SF10000us, 슬롯347us, owners234. 실측 실행 로그의 실제 설정과 역할을 verifier로 확인했다. lead11 디렉터리 이름으로 설정을 추측하지 않았다. 8월25일의 정확한 네 HEX를 사용했으며 source rebuild는 없었다.

| 역할 | Serial | SHA256 |
|---|---|---|
| init | 1050270933 | `0a020d4cef54c3ac81c248fdf4595c589e500073699750650ca44a59a0d2b737` |
| N2 | 1050211584 | `9b84be4f94f75257b64b52ac248195d54e669315dc3f8cae054e99a41606ab20` |
| N3 | 1050273888 | `9a1fa14ef6fcfd81619c33c5720aec2603b6c8ceb3c1403717f4e7d6ed58f651` |
| N4 | 1050282818 | `3b4611d82afb69f4be3a23aa9275c20bab6d02a49a77ac7baf8c79c9789c07e1` |

네 보드에 명시적 serial로 해당 역할 HEX를 적용했다. INIT는 TX 준비 전에 정지 상태로 두고 TX 전부 READY 이후 시작했다. 실행 뒤 reset/halt 없는 flash readback으로 HEX의 모든 populated bytes 일치를 확인했다.

분석 트리: branch exp4-rx-error-diag-20260904, HEAD55a23fa94fb7b4e372ec7b03d54be72f01b81e7a. 로컬의 기존 미추적 sdk 항목, 원격 main의 기존 수정 12항목을 포함해 모든 조사 대상 작업 트리의 branch/HEAD/dirty/tracked diff hash는 실행 전후 동일했다. source 수정·commit·push 없음.

3. 실행별·노드별 PER

| 노드 | Offered | RX | 손실 | PER |
|---|---:|---:|---:|---:|
| N2 | 1000 | 1000 | 0 | 0.000% |
| N3 | 1000 | 806 | 194 | 19.400% |
| N4 | 1000 | 995 | 5 | 0.500% |
| 전체 | 3000 | 2801 | 199 | 6.633% |

실행 준비/수집 전체: 2026-09-07 09:46:10–09:46:31 KST. INIT 수집 시작 약09:46:17 KST. 이는 호스트 수집 시각이며 개별 RF 프레임 wall-clock 시각의 별도 계측은 아니다. 유효 RX>0, 각 노드1000 offered, 합계 일치 확인.

4. 오류 카운터와 수집 완결성

| INIT 이벤트 | 횟수 |
|---|---:|
| SFD timeout | 11 |
| PHR error | 28 |
| CRC error | 5 |
| RXFSL | 120 |
| RX errors 합계 | 164 |
| FWTO | 0 |
| PTO | 0 |
| delayed-RX late | 0 |
| RX overrun | 0 |
| RDB mismatch | 0 |
| RDB incomplete | 0 |
| RDB incomplete recovered | 0 |
| RDB resync | 0 |
| RDB overrun | 0 |

| TX | Beacon 수신/1000 | Attempt | Success | delayed-TX late |
|---|---:|---:|---:|---:|
| N2 | 1000 | 1000 | 1000 | 0 |
| N3 | 1000 | 1000 | 1000 | 0 |
| N4 | 1000 | 1000 | 1000 | 0 |

wrong length/slot/superframe=0, data-config-error=0, sync-delayed-late=0, TX sync delayed-RX late=0. 관측 source→owner 불일치=0. 독립 wrong-source 및 현대 SPI error/timeout 카운터는 이 과거 펌웨어에서 미제공이므로 측정된0으로 기재하지 않는다.

네 역할 모두 READY1개·END STATS1개·수집 exit0·수집 timeout없음. TX END beacon 각1개, INIT END TX3개. TX SYNC timeout/RX error/beacon-config-error/data-config-error=0. Raw/manifest/HEX hash와 네 역할 verifier 통과. Collection PASS는 PER PASS를 뜻하지 않는다.

Rearm count=2000, service min/max=59/178us, required guard=193us. RX 오류164회와 손실199개는 일대일 대응하지 않는다. FWTO/PTO0도 burst 동안 timeout을 끈 구조이므로 무검출 손실의 부재를 뜻하지 않는다.

5. 기존 NLOS 고손실과의 유사성

사전 기준 N3 PER≥10%를 충족했다. N3만 큰 손실·N2 0%·N4 소량 손실, RXFSL 우세라는 패턴이 2026-09-04 과거 HEX replay와 유사하다. 당시 N3 55.1%/48.7%(합산51.9%)보다 이번19.4%는 낮지만, 여전히 목표1%를 크게 넘는다. 단일 실행의 관측이며 동일한 통계 분포·완전히 동일한 RF 상태·확정된 동일 원인까지 주장하지 않는다.

6. continuous/manual-rearm 가설에 대한 의미

정확한 과거 HEX에서도 고손실이 나므로 최근 최적화만의 문제나 TX 미송신으로 설명되지 않는다. 이번 N3 후속 슬롯의 손실은 약한 링크와 수신 재진입 이력의 상호작용 가설과 양립한다. 그러나 RX방식 A/B를 한 것이 아니므로 원인을 확정하지 않는다. 과거 저손실 HEX에도 같은 첫-slot delayed-RX + 후속 manual double-buffer burst 구조가 존재한다. RXFSL만으로 continuous-RX나 금속을 단일 원인으로 결정하지 않는다.

7. 다음 A/B 가치

현재 M32·동일 물리 세팅에서 노드별19.4% 손실이 확보됐으므로 current burst와 슬롯별 scheduled delayed-RX를 비교할 실험적 가치가 있다. 전원·역할·위치·PHY·TX 출력·TDMA·비수신 절전·재전송 없음 조건을 고정하고, 공통 초기화/첫 창을 동등하게 유지해야 한다. 수신 방식 변경은 이번 요청 범위에 포함하지 않아 구현하지 않았다. 사용자 지시에 따라 확인 실행은 정확히1회로 종료했다.

근거: manifest.json, old_r1/init/init.log, old_r1/tx/N2.log·N3.log·N4.log, 양쪽 status.json, old_r1/audit.json, local/remote preflight/postflight JSON.
