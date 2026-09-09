# Exp4 집 환경 재현 결과

현재 표본 판정: **FAIL_PER**. 실행은 사용자 요청대로 이 배치에서 1회만 수행했다.

1. SSH 및 보드 연결 상태

`s-macbook-air` SSH 정상. 로컬 J-Link 1050270933 1개, 원격 1050211584 / 1050273888 / 1050282818 세 개를 실제 인식했다. 4212와 Exp4 잔여 캡처는 없었다. 관련 없는 프로세스/데이터는 삭제하지 않았다.

- local: 종료 후 캡처 프로세스 0개; Git 브랜치·HEAD·dirty 및 tracked diff hash 시작과 동일=True; 지정 HEX 전체 데이터 바이트 readback 일치.
- remote: 종료 후 캡처 프로세스 0개; Git 브랜치·HEAD·dirty 및 tracked diff hash 시작과 동일=True; 지정 HEX 전체 데이터 바이트 readback 일치.

2. 사용한 역할·HEX·실험 조건

User moved RX/INIT behind the dryer and requested one run. TX nodes remain in dryer per preceding explicit confirmation, with no further TX change reported. Original board roles. No agent changes to physical placement, cables, ports, power or appliance controls. Door/operation states unmeasured.

M32/PAC8/S3/G250/lead15us/SB3000us/SP2500us/1000SF. SF=10000us, slot=347us, owner=234, 재전송·role rotation 없음. 실행 로그에서 실제 설정을 검증했다. 폴더명 lead11에서 설정을 추측하지 않았다.

| 역할 | 실제 serial | SHA256 |
|---|---|---|
| init | 1050270933 | `0a020d4cef54c3ac81c248fdf4595c589e500073699750650ca44a59a0d2b737` |
| N2 | 1050211584 | `9b84be4f94f75257b64b52ac248195d54e669315dc3f8cae054e99a41606ab20` |
| N3 | 1050273888 | `9a1fa14ef6fcfd81619c33c5720aec2603b6c8ceb3c1403717f4e7d6ed58f651` |
| N4 | 1050282818 | `3b4611d82afb69f4be3a23aa9275c20bab6d02a49a77ac7baf8c79c9789c07e1` |

8월 25일 과거 HEX 그대로이며 소스 재빌드/수정은 하지 않았다. TX 허브는 사용자 확인상 외부 어댑터 없는 노트북 USB 전원. GenesysLogic USB2.1 Hub 0x01100000, 818=0x01120000 / 584=0x01130000 / 888=0x01140000. 로컬 933은 USB 직결 0x00100000. 역할 교환 시에도 물리 USB 경로와 보드별 전원 공급 위치는 그대로였다.

3. 실행별·노드별 PER

| 노드 | Offered | RX | 손실 | PER |
|---|---:|---:|---:|---:|
| N2 | 1000 | 546 | 454 | 45.400% |
| N3 | 1000 | 1000 | 0 | 0.000% |
| N4 | 1000 | 1000 | 0 | 0.000% |
| 전체 | 3000 | 2546 | 454 | 15.133% |

INIT 수집 시각(KST): 2026-09-07T00:28:43.211365+09:00 – 2026-09-07T00:28:57.148763+09:00. 이는 수집 구간이며 개별 RF 프레임의 wall-clock 시각 계측값은 아니다.

유효 RX>0과 노드별 분모/합계/로그 종료를 확인했다. 전체 평균으로 판정하지 않고 모든 노드 PER<1%를 요구했다.

4. 오류 카운터 및 수집 완결성

| INIT 이벤트 | 횟수 |
|---|---:|
| SFD timeout | 454 |
| PHR error | 0 |
| CRC error | 0 |
| RXFSL | 0 |
| FWTO | 0 |
| PTO | 0 |
| RX errors 전체 | 454 |
| delayed-RX late | 0 |
| RX overrun | 0 |
| RDB mismatch | 0 |
| RDB incomplete | 0 |
| RDB incomplete recovered | 0 |
| RDB resync | 0 |
| RDB overrun | 0 |

| TX | beacon 수신/1000 | attempt | success | delayed-TX late |
|---|---:|---:|---:|---:|
| N2 | 1000 | 1000 | 1000 | 0 |
| N3 | 1000 | 1000 | 1000 | 0 |
| N4 | 1000 | 1000 | 1000 | 0 |

wrong length/slot/superframe=0, data-config-error=0, sync-delayed-late=0, TX sync delayed-RX late=0. 관측 source→owner 불일치=0. **독립 wrong-source 카운터와 현대 SPI 오류/timeout 카운터는 이 과거 펌웨어에서 미제공**이므로 측정된 0으로 기재하지 않는다.

네 역할 모두 READY 1개, END STATS 1개, 수집 exit code 0, 수집 timeout 없음. TX END beacon은 각 1개; INIT END TX=3. TX들의 SYNC timeout/RX error/beacon/data config error=0. Raw/manifest/HEX hash 및 네 역할 verifier 통과. PHY 손실은 collection PASS와 별개로 PER에 남겼다.

manual rearm count=2000, service min/max=59/178us, required guard=193us < G250. RX 오류 이벤트는 패킷 손실과 일대일 대응하지 않는다. FWTO/PTO=0은 DATA burst에서 timeout이 꺼진 구조이므로 무검출 손실의 부재를 뜻하지 않는다.

5. 기존 NLOS 고손실과의 비교

이번에는 N2에서 PER45.4%의 큰 손실이 재현됐으나, N3≥10%라는 최초 기준은 미충족이다. N3/N4는0%다. 과거 N3(888) 약50%·RXFSL 우세와 달리, 이번에는 N2(584) 첫 슬롯·SFD timeout454가 특징이다. 동일 채널 또는 동일 원인으로 보지 않는다.

6. continuous/manual-rearm 가설

N2는 이미 delayed-RX로 여는 첫 DATA 슬롯이므로, 앞 DATA 처리 후 후속 슬롯을 continuous/manual-rearm한 것만이 고손실의 필요 원인이라는 주장은 이번 결과를 설명하지 못한다. DW3000 CMD_DRX는 RX 켜는 시각을 예약하며 이후 프리앰블 검출을 수행한다. 논문 III의 이상적 검출 제거와 IV의 DWM3000 실험을 구분해야 한다. 이전 SF/오류 복구/PHY 전환 등 넓은 수신 이력 영향은 미배제다. 자세한 논문·매뉴얼·코드 대조는 HYPOTHESIS_REVIEW.md에 보존했다.

7. 슬롯별 scheduled delayed-RX A/B 가치

같은 M32·현재 물리 세팅에서 RX 방식만 교차할 가치는 있다. 다만 이번 고손실 N2의 첫 창은 이미 scheduled이므로 단순한 후속-slot 변경만으로 N2 개선을 기대할 근거는 부족하다. 첫 창과 공통 초기화를 동등하게 두고 노드별 결과 및 오류 종류/시점을 비교해야 한다. 이번에는 추가 RF 실행, 코드 구현, commit/push를 하지 않았다.

근거: manifest.json, old_r1/init/init.log, old_r1/tx/N2.log·N3.log·N4.log, 양쪽 status.json, old_r1/home_audit.json, preflight/postflight JSON. 원본은 모두 보존했고 commit/push는 하지 않았다.
