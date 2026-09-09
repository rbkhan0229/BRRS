# 현재 원인 가설·NLOS 비교·PAC4/PAC8 비교 적합성 검토

현재 질문은 원인 및 비교 실험의 적합성 검토로 처리했다. 이 검토에서 새 RF 실행, 플래시, 빌드, 소스·Git 변경을 수행하지 않았다. GitHub 업로드 보류 유지.

## 관측과 원인 추정

집 RX 세탁기 뒤 높이를 올린 배치: N2/N4 예정1000·TX1000·RX1000. N3 예정1000, 비컨989, TX989, RX810. 예정량 기준190/1000=19%, 실제 송신분 집계 기준179/989=18.099%. INIT의 N3 수신창에 SFDtimeout177, PHR1, RXFSL1, FWTO11. N3 TX측 비컨 RXerror33, miss11. TX delayedlate0, RX scheduledlate0, 슬롯/슈퍼프레임/버퍼/SPI/RTT 수집 오류0.

현재 가장 유력한 방향은 N3 링크의 획득·초기 동기화 여유가 배치/채널에 민감해진다는 가설이다. RX 선반 위에서는 같은 이미지로 모든 노드0%였고 세탁기 뒤 높인 배치에서는 N3만 손실이 집중됐다. 그러나 각 배치1회이며 시간 차이도 있어 이동에 의한 감쇠/다중경로 변화, 간섭, 획득 오검출, 수신창과 하드웨어 시작 동작의 상호작용을 분리하지 못한다. CIR·수신전력·CFO·오류 시 상태/패킷별 시각을 수집하지 않았으므로 물리 원인을 확정하지 않는다.

SFD timeout은 유효 SFD를 제한 시간 안에 확보하지 못해 수신을 중단한 이벤트다. 프리앰블 후보 검출 뒤의 실패일 수 있고, 진짜 프리앰블을 정확히 검출했다는 보장도 없다. 단순히 프리앰블을 전혀 못 보았거나 수신창에 잡음이 오래 쌓였다는 증거로 해석하지 않는다. 이미 슬롯별 bounded delayed-RX인 상태여서 continuous RX만을 단일 원인으로 삼을 수 없다. N3 비컨 수신에도 손실이 있으므로 INIT DATA PAC만으로 전체 현상을 설명할 수 없다.

## 기존 환경 비교

| 환경·펌웨어 | N3 수신 손실 | 주요 오류 | N3 TX |
|---|---|---|---|
| 9/4 NLOS6.9m, 과거 G250/lead15/manual burst PAC8 | 합산51.9%, 2회 | RXFSL361/322회, SFD/PHR도 존재 | 매회1000 |
| 9/7 NLOS6.9m, 현재와 동일 P25 PAC8 | 합산0.25%, 4회 | RXFSL8·PHR2 합계 | 매회1000 |
| 9/7 NLOS6.9m, P25 PAC4 | 3.2%, 1회 | PHR14·FWTO15·SFD3 | 1000 |
| 현재 집 세탁기 뒤 높인 배치, P25 PAC8 | 예정량19%, 실제TX 기준18.10% | SFD177·PHR1·RXFSL1·FWTO11 | 989, 비컨11누락 |

공통점은 N3에 집중되는 PHY 수신 손실과 기록된 MAC/예약/버퍼 오류 부재다. 차이는 오류 단계, 비컨 성공률, 사용 펌웨어 및 채널이다. 특히 같은 P25 PAC8 이미지로 NLOS에서는0.25%였으나 현재 배치에서는 훨씬 높은 손실이므로 단순히 기존 NLOS 환경을 복제했다고 할 수 없다. 과거 burst 오류 이벤트는 손실과 일대일 대응하지 않고 귀속도 추정이므로 서로 다른 수신 경로의 오류 횟수를 동일 확률로 직접 비교하지 않는다.

## 다음 비교의 적합성과 권장 설계 — 아직 실행하지 않음

현재 환경은 M32 약한 링크에서 PAC 설정이 미치는 효과를 비교할 탐색 환경으로 가치가 있다. 여기에서 개선돼도 NLOS6.9m에서 재검증이 필요하며 원래 고정 배치의 해결로 승격하지 않는다.

1. 현재 위치·높이·방향·케이블·유전원 허브·보드 역할·출력·M32·lead25·RX창122us·G250·SPI 최적화·TDMA/절전·무재전송을 고정한다.
2. 이미 검증된 두 INIT HEX로 우선 PAC8→PAC4→PAC8, 각1000SF를 가까운 시간에 실행하는 것이 적절하다. 마지막 PAC8은 채널 변화 확인용이다. 한 번의 PAC4와 과거 PAC8을 비교하는 방식보다 해석력이 높다.
3. INIT DATA PAC만 선택하되 연동된 제조사 SFDtimeout 식(32+1+8−PAC)에 따라 PAC8=33, PAC4=37로 맞춘 기존 이미지다. 즉 숫자상 PAC와 파생 SFD timeout 두 설정이 달라진다. 모든 TX 및 비컨 PHY/PAC8은 그대로다. 현재 수신창/lead에서의 PAC 구성 비교이며 PAC의 보편적 우열로 일반화하지 않는다.
4. 모든 실행을 보존하고 각 노드 예정량 기준 미수신률, 실제 송신분 기준 손실률, 비컨 수신/누락, TX attempt/success, 슬롯별 오류와 전체 오류 subtype을 함께 비교한다. 비컨 누락을 PAC 개선으로 숨기지 않는다. 전체 목표 판정은 예정된 노드당1000개 기준 PER<1%를 유지한다.
5. 1000회 정상 송신 조건이 충족되지 않으면 기존 full-TX 벤치마크와 동등한 주비교로 취급하지 않는다. PAC8 앞/뒤 결과나 비컨 성공률이 크게 달라지면 채널 변화가 섞인 것으로 보고 그 묶음만으로 PAC 승자를 정하지 않는다. 오염·실패 실행도 삭제하지 않는다.

두 HEX: PAC8 INIT SHA2569840bb124de05bd83af7f58b5b494c734443c0ca81ee933c9c168febcfbd2373, PAC4 INIT SHA256b623dbb1fab13c1807260f552e576fa5c705ca9c76f7f925e6e3af5717f60d0e. 둘 다 이미 보존돼 있어 이번 설계에 새 펌웨어 코드는 필요하지 않다.

## 근거

- 현재 RESULTS.json 및 H8_r1 원시로그.
- ../exp4_old888_replay_20260904/RESULTS.md
- ../exp4_nlos69_rxlead_20260907/RESULTS.md
- ../exp4_nlos69_pac4_20260907/RESULTS.md, manifest.json 및 compiled_config_evidence.json
- Qorvo DW3xxx API Guide ©2025, pp34–36: PAC의 상관 chunk 역할과 M32/PAC4 권장, SFDtimeout 목적·공식. https://forum.qorvo.com/uploads/short-url/xD3TlXKvkujjdUaJWXv2b4E7GQN.pdf
- DW3000 User Manual v1.1 §8.2.7.2: 프리앰블 검출 후 SFD 타이머와 미검출 종료. https://caramelfur.dev/docs/DW3000-User-Manual/DW3000-User-Manual.html
