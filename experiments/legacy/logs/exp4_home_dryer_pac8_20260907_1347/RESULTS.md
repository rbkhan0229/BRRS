# 집 건조기 배치 PAC8 1회 결과 — 유효 PER 비교 불가

2026-09-07 13:52:32–13:52:59 KST(로컬 측정 제어·플래시 검증 포함), H8_r1. 요청대로 한 번만 실행했다. INIT는 1000 superframe을 완료했으나 수신 0개였고 TX 세 보드는 각각 비컨 1회 수신, 송신 1회 후 종료했다. 모든 노드 PER<1% 목표를 달성하지 못했다. 1000회 정상 송신을 전제로 하는 데이터 링크 PER 비교에서는 제외한다.

## 환경·연결·조건

- 사용자가 새로 구성한 집 환경: TX 3개는 건조기 안, RX는 건조기 뒤. TX 허브는 외부 전원 어댑터 연결됨(사용자 확인).
- SSH `s-macbook-air` 정상. 로컬 INIT=1050270933; 원격 N2=1050211584, N3=1050273888, N4=1050282818. 추가 4212 없음.
- 실제 측정 직전 USB: 로컬 INIT@00100000; 원격 허브@01100000, N2@01130000, N3@01140000, N4@01120000. 최초 탐색 후 변경된 USB 경로는 측정 직전 preflight 기준으로 manifest에 반영했다. 측정 직전/직후 registry ID·포트·serial 모두 동일.
- 기존 성공 P25 INIT 이미지: M32/PAC8/S3/G250, RX lead25 µs, 슬롯별 bounded scheduled delayed-RX, SPI 최적화. SB/SP=3000/2500 µs, SF=10000 µs, 1000 SF, RX창122 µs/FWTO119 UUS. TX는 보존된 과거 HEX 그대로 사용.
- 네 보드만 명시적 serial로 플래시. 네 HEX 모두 플래시 후 populated-byte readback PASS. 코드 수정·빌드·commit·push 없음. 이전 GitHub 업로드 보류 유지.
- 실행 중 위치·방향·케이블·포트·전원 변경을 수행하지 않음. 환경 교란에 대한 사용자 신고 없음. NLOS 6.9m 및 이전 집 실험과 별도 환경이며 합산하지 않음.

## 노드별 관측

| 노드 | Serial | 비컨 수신/기대 | TX 성공/시도 | INIT 수신/예정 | 예정량 기준 손실 |
|---|---|---:|---:|---:|---:|
| N2 | 1050211584 | 1 / 1000 | 1 / 1 | 0 / 1000 | 100%* |
| N3 | 1050273888 | 1 / 1000 | 1 / 1 | 0 / 1000 | 100%* |
| N4 | 1050282818 | 1 / 1000 | 1 / 1 | 0 / 1000 | 100%* |

전체 INIT RX=0/3000, 실제 TX 성공 합계=3회. *100%는 예정된 1000회에 대한 미수신률이다. 실제로 1000회 송신하지 않았으므로 이를 정상 송신 조건의 M32 데이터 링크 PER=100%라고 해석하지 않는다. 유효 수신 0개를 PASS로 판단하지 않았다.

TX 세 보드 모두 첫 수신 비컨 seq=5, 초기 gaps=4, 총 비컨1/1000, miss999, TXattempt=1/success=1/late=0, SYNC loss timeout=1, end-frame 수신=0. 공통 비컨 한 번을 받은 후 세 보드 모두 실험을 조기 종료한 것이 관측상의 핵심이다.

## 오류·수집 무결성

| 항목 | 관측 |
|---|---|
| SFD timeout / PHR / CRC / RXFSL | 모두 0 |
| FWTO / PTO | 3000 / 0; 슬롯당 1000 FWTO |
| 슬롯별 attempted / armed / late | 각 1000 / 1000 / 0 |
| wrong length / slot / superframe | 모두 0; 유효 패킷이 없어 검증력 없음 |
| wrong source | 수신·source 분류 기록 0건; 무오류 링크의 증거로 사용할 수 없음 |
| delayed-RX late / delayed-SYNC late / TX-wait timeout | 모두 0 |
| RDB host mismatch / incomplete / recovered / resync / overrun | 모두 0 |
| deferred overflow / rearm deadline miss | 모두 0 |
| SPI begin/end | 1000/1000; 오류·복구0, status PASS |
| RTT READY / END STATS 마커 | 네 보드 각각1회/1회 |
| RTT 캡처 프로세스 exit | 네 보드 모두0, 캡처 timeout 없음 |
| 무선 END 프레임 수신 | TX 모두0 — 로그 종료 마커와 다름 |
| INIT firmware 판정 | schedule PASS, timing PASS, collection FAIL, link LOSS |
| 엄격 검증 | INVALID: collection FAIL, RX0, TX 횟수 부족 |

FWTO3000은 무선 수신창 종료 횟수다. RTT 수집 timeout과 구별한다. 원시 로그와 실패한 audit를 그대로 보존했다. PHY 오류 0건 또한 모든 수신창에서 패킷을 받았다는 뜻이 아니다.

## 해석과 다음 확인 지점

이번에는 기존 NLOS의 'TX1000회 정상 송신, N3 데이터만 손실' 조건이 성립하지 않았다. PAC8의 데이터 수신 성능, PAC4 대비 우열, continuous/manual-rearm 가설을 이 실행으로 판정할 수 없다. 건조기 차폐가 원인이라고 단정하지 않는다.

참고 TX 소스에는 마지막 SYNC로부터 27 ms 경과 시 SYNC loss 처리, 100 ms 경과 시 최종 통계 후 종료하는 경로가 있다. 이번 로그의 end=0·SYNC timeout=1·TX1회는 비컨을 지속적으로 받지 못해 종료한 경로와 부합한다. 이는 종료 동작에 대한 설명이며, 왜 비컨이 끊겼는지를 입증하지 않는다. 세 보드 모두 seq5만 받은 공통 패턴이므로 다음에는 무선 비컨 경로뿐 아니라 INIT 시작/플래시 과정의 일시 실행과 TX 시작 동기화도 확인할 필요가 있다. 이번 로그는 RF 패킷별 동기화 시각이나 J-Link 단계별 시각이 없어 이 가능성들을 분리하지 못한다.

요청된 한 번을 마쳤으므로 추가 RF 재실행이나 설정 변경은 하지 않았다. 보드에는 PAC8 P25/기존 TX 이미지가 남아 있다.

## 역할·HEX SHA256

| 역할 | Serial | SHA256 |
|---|---|---|
| init | 1050270933 | `9840bb124de05bd83af7f58b5b494c734443c0ca81ee933c9c168febcfbd2373` |
| N2 | 1050211584 | `9b84be4f94f75257b64b52ac248195d54e669315dc3f8cae054e99a41606ab20` |
| N3 | 1050273888 | `9a1fa14ef6fcfd81619c33c5720aec2603b6c8ceb3c1403717f4e7d6ed58f651` |
| N4 | 1050282818 | `3b4611d82afb69f4be3a23aa9275c20bab6d02a49a77ac7baf8c79c9789c07e1` |

## 보존 자료

- `H8_r1/init/init.log`, `H8_r1/tx/N2.log`, `N3.log`, `N4.log`: 원시 RTT.
- `H8_r1/manifest.json`: 실행 시 고정된 환경·설정·HEX hash.
- `H8_r1/init_flash_readback.json`, `tx_flash_readback.json`: 네 보드 실플래시 검증.
- `H8_r1/audit.json`: 원본 엄격 검증 실패 결과, 수정하지 않음.
- `RESULTS.json`: 실패 실행도 누락 없이 추출한 카운터와 판정.
- `local_preflight.json`, `remote_preflight.json`, `local_postflight.json`, `remote_postflight.json`: 연결 및 Git 증거.
- `postflight_verification.json`: 로컬/원격 Git branch·HEAD·dirty·tracked diff SHA 유지, 동일 보드/USB 구성, 잔여 캡처 프로세스 없음.
