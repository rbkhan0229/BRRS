# 집 RX 세탁기 뒤·높이 증가 — PAC8 1회, N3 고손실

2026-09-07 14:11:18–14:11:44 KST(측정 제어·플래시 검증 포함), H8_r1. 요청한 추가 1회를 완료했다. N2/N4는1000/1000 수신, N3는 예정1000개 중810개 수신으로19% 미수신이다. **각 노드 PER<1% 목표 실패.**

## 환경·설정

사용자 설명 그대로 RX는 세탁기 뒤에서 높이를 올린 배치다. 정확한 높이는 제공되지 않았다. 세탁기와 건조기를 동일한 기기로 추정하지 않는다. TX 세 보드는 건조기 안, USB 허브 외부 전원 연결은 이전 사용자 확인 유지(추가 변경 신고 없음). 직전 선반 위, 그 이전 건조기 뒤 및 NLOS6.9m와 별도 환경으로 기록한다.

동일 P25 INIT 및 과거 세 TX HEX: M32/PAC8/S3/G250, lead25 µs, SB/SP=3000/2500 µs, SF10 ms×1000, 슬롯347 µs, RX창122 µs/FWTO119 UUS. 슬롯별 bounded scheduled delayed-RX·SPI 최적화 사용. 재전송·새 빌드·코드 수정 없음.

SSH `s-macbook-air` 및 지정 네 보드 인식 정상. 로컬 INIT@00100000; 원격 USB2.1 Hub@01100000 → N2@01130000, N3@01140000, N4@01120000. 측정 전후 registry ID·포트·locationID·serial 동일, 잔여 캡처 프로세스 없음. 실험 중 배치·방향·케이블·포트·전원 변경을 수행하지 않았다.

## 노드별 결과

| 노드 | Serial | 예정량 | 실제 TX 성공 | RX | 예정량 기준 미수신률 | 실제 TX 기준 손실률 |
|---|---|---:|---:|---:|---:|---:|
| N2 | 1050211584 | 1000 | 1000 | 1000 | 0.0% | 0.00% |
| N3 | 1050273888 | 1000 | 989 | 810 | 19.0% | 18.10% |
| N4 | 1050282818 | 1000 | 1000 | 1000 | 0.0% | 0.00% |

전체 RX2810/예정3000, 예정량 기준 미수신률6.333%. 실제 송신 성공 합계2989회. N3는 비컨11개를 놓쳐11회 미송신했고, 나머지989회 송신 중179개가 수신되지 않았다. 따라서 예정량 기준190/1000=19.0%, 실제 송신분 기준179/989≈18.10%다. 후자는 집계 카운터의 차이로 계산했으며 패킷별 손실 시점을 별도로 수집한 것은 아니다.

`audit.json`의 원래 strict 판정은 INVALID: 모든 TX1000회 성공 조건에서 N3가989회여서 중단됐다. 원시 수집과 기록된 시스템 카운터는 정상이며 이 실행을 삭제하지 않았다. 추가 읽기 전용 검증으로 네 보드 hash·마커·readback·슬롯/버퍼/SPI 카운터를 확인하고 `RESULTS.json`에 보존했다. 정상1000회 송신 실행과 합산하지 않는다.

## TX·오류·종료

- N2/N4: beacon received1000/miss0, TXattempt/success1000/1000, delayed-TX late0, 무선 END수신1, SYNC loss0, TX측 RX error0.
- N3: beacon received989/miss11/gaps11/duplicates0, TXattempt/success989/989, delayed-TX late0, 무선 END수신1. SYNC loss timeout0, TX측 RX error33, beacon/data config error0. RX error33을 미수신 비컨11개와 일대일 대응시키지 않는다.
- INIT 오류: **SFD timeout177, PHR1, RXFSL1, CRC0**, 총RX error179. FWTO11/PTO0/FINT-only0. 모두 N3 담당 수신창에서 발생한 terminal error179/timeout11이고, N2/N4 창에서는0.
- 각 슬롯 attempted/armed1000/1000, delayed-RX late0. N3창 good810+error179+timeout11=1000. FWTO11과 비컨 미수신11은 집계상 일치하나 패킷별 대응은 확인하지 않았다.
- wrong length/slot/superframe0. source-to-slot N2→0=1000, N3→1=810, N4→2=1000으로 수신2810개와 일치, 잘못된 조합 없음.
- delayed-SYNC late/TX-wait timeout/rearm deadline miss/deferred overflow0. RDB mismatch/incomplete/recovered/resync/overrun 모두0.
- SPI begin/end1000/1000, direct transfers103992. SPI 오류·timeout·recovery0.
- 네 보드 각각 RTT READY1/END STATS1, 캡처 exit0, 수집 timeout 없음. 각 TX 무선 END도 수신했다. PHY FWTO11은 RTT 수집 timeout과 구별한다.
- 네 HEX SHA256 및 실플래시 populated-byte readback PASS. 로컬/원격 Git branch·HEAD·dirty·tracked diff SHA 유지. 추가 실행·commit·push 없음, GitHub 업로드 보류 유지.

## 해석

직전 선반 위에서는 세 노드0%였으나 이번 세탁기 뒤 높인 배치에서는 N3에 손실이 집중됐다. N3 데이터 수신뿐 아니라 비컨 수신에도 문제가 관측됐다. 배치/채널 영향과 부합하지만 각1회씩이므로 높이 변화 하나로 원인을 확정하지 않는다. 기존 NLOS6.9m와 동일한 채널·단일 원인이라고 결론내리지 않는다. 이미 슬롯별 delayed-RX인 상태에서도 손실이 있으므로 continuous RX만의 문제로 설명할 수 없다.

## 역할·HEX SHA256

| 역할 | Serial | SHA256 |
|---|---|---|
| init | 1050270933 | `9840bb124de05bd83af7f58b5b494c734443c0ca81ee933c9c168febcfbd2373` |
| N2 | 1050211584 | `9b84be4f94f75257b64b52ac248195d54e669315dc3f8cae054e99a41606ab20` |
| N3 | 1050273888 | `9a1fa14ef6fcfd81619c33c5720aec2603b6c8ceb3c1403717f4e7d6ed58f651` |
| N4 | 1050282818 | `3b4611d82afb69f4be3a23aa9275c20bab6d02a49a77ac7baf8c79c9789c07e1` |

## 증거

`H8_r1/init/init.log`, `H8_r1/tx/N2.log`, `N3.log`, `N4.log`: 원시 RTT. `H8_r1/audit.json`: 수정하지 않은 strict 실패. `H8_r1/manifest.json`, `init_flash_readback.json`, `tx_flash_readback.json`: 조건·hash·readback. `RESULTS.json`: 전체 카운터와 두 분모의 손실률, 추가 검증 결과. 전후 로컬/원격 preflight/postflight 및 `postflight_verification.json`: USB·Git·프로세스 증거. `prior_placement_comparison.json`: 이전 유효 배치 결과(합산하지 않음).
