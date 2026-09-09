# Exp4 집 환경 재현 결과

현재 표본 판정: **PASS_EACH_NODE_PER_LT_1_THIS_SAMPLE**. 실행은 사용자 요청대로 이 배치에서 1회만 수행했다.

1. SSH 및 보드 연결 상태

`s-macbook-air` SSH 정상. 로컬 J-Link 1050270933 1개, 원격 1050211584 / 1050273888 / 1050282818 세 개를 실제 인식했다. 4212와 Exp4 잔여 캡처는 없었다. 관련 없는 프로세스/데이터는 삭제하지 않았다.

- local: 종료 후 캡처 프로세스 0개; Git 브랜치·HEAD·dirty 및 tracked diff hash 시작과 동일=True; 지정 HEX 전체 데이터 바이트 readback 일치.
- remote: 종료 후 캡처 프로세스 0개; Git 브랜치·HEAD·dirty 및 tracked diff hash 시작과 동일=True; 지정 HEX 전체 데이터 바이트 readback 일치.

2. 사용한 역할·HEX·실험 조건

Only RX/INIT returned from behind refrigerator to prior dryer-only experiment placement. TX nodes remain in dryer. User-reported placement correspondence, not independently instrumented.

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
| N2 | 1000 | 1000 | 0 | 0.000% |
| N3 | 1000 | 999 | 1 | 0.100% |
| N4 | 1000 | 1000 | 0 | 0.000% |
| 전체 | 3000 | 2999 | 1 | 0.033% |

INIT 수집 시각(KST): 2026-09-06T23:51:35.268289+09:00 – 2026-09-06T23:51:49.199416+09:00. 이는 수집 구간이며 개별 RF 프레임의 wall-clock 시각 계측값은 아니다.

유효 RX>0과 노드별 분모/합계/로그 종료를 확인했다. 전체 평균으로 판정하지 않고 모든 노드 PER<1%를 요구했다.

4. 오류 카운터 및 수집 완결성

| INIT 이벤트 | 횟수 |
|---|---:|
| SFD timeout | 0 |
| PHR error | 0 |
| CRC error | 0 |
| RXFSL | 0 |
| FWTO | 0 |
| PTO | 0 |
| RX errors 전체 | 0 |
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

manual rearm count=2000, service min/max=59/60us, required guard=75us < G250. RX 오류 이벤트는 패킷 손실과 일대일 대응하지 않는다. FWTO/PTO=0은 DATA burst에서 timeout이 꺼진 구조이므로 무검출 손실의 부재를 뜻하지 않는다.

5. 기존 NLOS 고손실과의 비교

N3 PER=0.100%. 사전 고손실 재현 기준 N3≥10%는 미충족. 각 노드 PER<1% 목표는 이번 표본에서 충족. 집 배치를 NLOS 6.9m와 동일 채널로 보지 않으며, 미재현을 기존 문제 해결로 해석하지 않는다.

6. continuous/manual-rearm 가설에 대한 의미

과거 저손실 HEX에도 있었던 첫 슬롯 delayed-RX + 후속 슬롯 manual double-buffer burst 재수신 구조를 그대로 사용했다. 이 실행은 배치 또는 역할을 바꾼 관측이며 RX 재수신 방식만 교차한 대조 실험이 아니다. 약한 M32 링크와 수신 이력의 상호작용은 여전히 가능하지만 지지/반박의 강도는 제한적이다. RXFSL이 있다는 사실로 금속 차폐나 continuous RX를 단일 원인으로 확정하지 않는다. 역할 교환 결과에는 수신 보드·송수신 방향·모든 노드의 RX까지의 경로 변경이 함께 포함된다.

7. 동일 M32·동일 물리 세팅의 burst / 슬롯별 scheduled delayed-RX A/B 가치

현재의 낮은 손실은 A/B 개선 비교의 분별력이 낮다. 원래 NLOS 또는 손실이 안정적으로 재현되는 고정 세팅에서 비교하는 편이 유용하다. 이번에는 코드 구현이나 추가 반복을 하지 않았다.

근거: manifest.json, old_r1/init/init.log, old_r1/tx/N2.log·N3.log·N4.log, 양쪽 status.json, old_r1/home_audit.json, preflight/postflight JSON. 원본은 모두 보존했고 commit/push는 하지 않았다.
