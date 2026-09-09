# 차량 RX 대시보드 이동 — 1회 측정 결과

**결과: 범퍼 링크 유지 실패. 측정 조기 중단으로 유효한 1,000SF PER은 산출하지 않는다.**

2026-09-08 17:41:06–17:41:42 KST에 한 번만 실행했다. SSH `s-macbook-air`와 로컬 INIT 1대·원격 TX 6대 인식을 확인했다. 사용자가 시동 켜짐, 허브 외부 전원, 모든 문 닫힘 및 RX 위치만 변경을 확인했다. RX는 대시보드 위, TX 배치는 직전 글러브박스 실험과 동일하다.

## 조건

직전 글러브박스 측정과 동일한 7개 HEX를 사용했다. DATA M32/PAC8, lead 26µs, 비컨 M256/PAC8, TX 6대·13슬롯(`2345672345673`), G250, SB/SP=3000/2500µs, 주기 10ms, 목표 1,000SF, 슬롯별 scheduled delayed-RX 및 SPI 최적화. RX 창 123µs/FWTO 120UUS. 재전송 없음. 비컨 512는 이번에 적용하지 않았다.

## 중단 시점 관측

| 역할·위치 | 비컨 수신 | TX attempt | TX success | TX late | 비컨 RX 오류 합계 |
|---|---:|---:|---:|---:|---:|
| N2 앞범퍼 A | 0 | 0 | 0 | 0 | 5 |
| N3 앞범퍼 B | 3 | 9 | 9 | 0 | 24 |
| N4 운전석 | 311 | 622 | 622 | 0 | 0 |
| N5 조수석 | 424 | 848 | 848 | 0 | 0 |
| N6 트렁크 A | 536 | 1072 | 1072 | 0 | 0 |
| N7 트렁크 B | 649 | 1298 | 1298 | 0 | 0 |

위 값은 종료 후 리셋 없이 회수한 RAM 누계다. 노드별 정지 시각이 달라 같은 길이의 측정 구간이 아니며, 비율이나 노드 간 성능 비교에 사용하지 않는다. TX success는 송신 완료 횟수이며 INIT의 수신 성공 횟수가 아니다.

N3는 비컨 3개 수신 후 TX 9/9회를 완료했지만 동기 상실(timeout 1회), 비컨 RX 오류 24회와 END 비컨 미수신으로 조기 종료했다. 마지막 비컨 seq=18, 중간 gap=15였다. 비컨/DATA 설정 오류와 delayed-TX late는 0이다. 출력의 `miss=997`은 조기 종료 시 목표 1,000에서 수신 3을 뺀 값이므로 완주한 실험의 손실률로 쓰지 않는다.

N3 검증 실패를 감지한 감독 수집기가 다른 TX와 INIT를 중단했다. INIT 마지막 진행 로그는 seq=800이며 최종 통계·종료 마커가 없다. 60초 수집 timeout에 걸린 것이 아니다. N3 RTT 종료 통계는 수집됐지만 정상 END 검증은 실패했다. 다른 보드는 정상 종료 마커가 없다.

따라서 노드별 offered/RX/PER, 전체 RX/PER 및 INIT의 SFD timeout·PHR·CRC·RXFSL·FWTO/PTO·wrong source/slot/superframe·delayed-RX late·RDB 상세 최종 카운터는 확보되지 않았다. 누락값을 0으로 처리하지 않으며 PER <1% 통과로 판정하지 않는다.

## 직전 글러브박스 측정과의 비교

직전에는 범퍼 N2·N3 모두 비컨 0개, TX 0회였다. 이번에도 N2는 0/0이고 N3만 비컨 3개와 TX 9회가 관측됐다. 대시보드 위치에서 일시적으로 N3 비컨 연결이 생겼지만 유지되지 않았다. 수집 구간이 다르고 이번 PER이 없어 위치 변경 효과를 수치로 판정할 수는 없다. 범퍼에서는 DATA M32 수신 성능 이전에 비컨 링크 확보가 여전히 문제다. 원인을 특정 금속 부품·방향·비컨 길이 하나로 확정할 수 없다.

다음에 비교한다면 DATA M32/PAC8·lead 26µs와 현재 배치를 유지하고 비컨 M512를 적용하는 시험이 후보이다. 이번에는 구현·추가 RF 실행을 하지 않았다.

## 역할 및 실제 HEX SHA256

| 역할 | J-Link serial | SHA256 |
|---|---|---|
| init | 1050270933 | `e79340b212fbbb941506ba9c42693e41ba66232789fb3a575b4913cd20830262` |
| N2 | 1050211584 | `f2b1052a13bdec77110b24f1c8e4c032868804f1ab3ad8dbd90b58362fb5a5fb` |
| N3 | 1050273888 | `e389c39afb4dd58a2b73473dcfdfd721f6c059f4b0e2625e4a94cc5f69a900b8` |
| N4 | 1050282818 | `42737cae87424c4a91a52336d509c766e596fd809a085ff5665818a90690b8d2` |
| N5 | 1050208509 | `fc4ab72282253ed567e8f40881ac0f368fd44701ebd3c19c12d4d299d6d82ae3` |
| N6 | 1050227627 | `37d108c1610f649a7645ca8e2c90f7bd36398a6af855ba29d1147daff25a2096` |
| N7 | 1050204212 | `55c387c78f0ce76c8ef2afa307f2d60f4e2bb624c5a3c0f86bdcee46533cba6f` |

사전 준비에서 직전 HEX와 일치를 검증했고 종료 후 7대 모두 실제 플래시 readback PASS 및 halted 상태를 확인했다. 로컬 최초 정리의 J-Link already-open 경고는 별도 복구 후 정지 확인으로 해소했다. 마지막 로컬·원격 검사에서 캡처 잔여 프로세스가 없고 Git 브랜치·HEAD·dirty/diff 상태도 사전과 같다. 펌웨어 소스 수정, 빌드, commit, push 및 추가 실행 없음.

## 증거 파일

- `RESULTS.json`: 기계 판독 결과와 누락 항목
- `manifest.json`, `image_reuse_proof.json`, `deployment.json`: 설정·이미지 재사용·배포 검증
- `capture1/results/orchestration.json`, `capture1/results/{local,remote}/status.json`: 수집 중단 경로
- `capture1/logs/`: INIT 원시 로그
- `remote_capture_files/`: TX 원시 로그
- `recovery_local.json`, `recovery_remote.json`: 플래시 검증·정지·TX RAM 누계
- `preflight_*.json`, `postflight_*.json`: SSH/보드/프로세스/Git 검사
