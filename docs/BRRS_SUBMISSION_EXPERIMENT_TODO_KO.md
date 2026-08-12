# BRRS 제출용 실험 TODO

기준 펌웨어: v2.6, beacon protocol v3

공통 DATA: 8 B protocol header + 16 B application + 2 B FCS = PSDU 26 B

## 0. 실험 시작 전 동결

- [ ] 두 노트북과 모든 보드가 같은 Git commit의 소스를 사용하는지 확인한다.
- [ ] SES 일반 `Debug`/`Release`가 아니라 아래의 명명된 구성을 선택한다.
- [ ] 부팅 로그가 `v2.6`인지 확인한다.
- [ ] INIT의 `BRRS_BEACON_CONFIG_CSV`와 NORMAL의 `BRRS_BEACON_RX_CSV`에서
      `data_psdu=26`, data rate, M, 슬롯 정보가 일치하는지 확인한다.
- [ ] 채널 9, code 9, PRF 64 MHz, PAC8, STS off, TX power를 기록한다.
- [ ] 거리, 높이, 안테나 방향, 지그 위치, 차폐물 위치를 사진과 치수로 기록한다.
- [ ] 사람과 이동 물체를 측정 구역에서 치우고 조건 실행 순서를 무작위로 정한다.
- [ ] 기존 PSDU 127 B 로그와 새 PSDU 26 B 로그를 다른 폴더에 둔다.

## 1. 빌드 구성표

| 단계 | INIT | NORMAL | run 길이 |
|---|---|---|---:|
| Stage 0 | `Stage0_L*_T*_Init` | `Stage0_Normal` | 2,000 |
| Exp1 M32 | `Exp1_32_Init` | `Exp1_Normal` | 2,000 |
| Exp1 M64 | `Exp1_64_Init` | `Exp1_Normal` | 2,000 |
| Exp1 M128 | `Exp1_128_Init` | `Exp1_Normal` | 2,000 |
| Exp1 M256 | `Exp1_256_Init` | `Exp1_Normal` | 2,000 |
| Exp2 M32 | `Exp2_32_Init` | `Exp2_Normal` | 1,000 |
| Exp2 M64 | `Exp2_64_Init` | `Exp2_Normal` | 1,000 |
| Exp2 M128 | `Exp2_128_Init` | `Exp2_Normal` | 1,000 |
| Exp2 M256 | `Exp2_256_Init` | `Exp2_Normal` | 1,000 |
| Exp3 A | `Exp3_A_Init` | `Exp3_A_Normal` | 1,000 |
| Exp3 B | `Exp3_B_Init` | `Exp3_B_Normal` | 1,000 |
| Exp3 C | `Exp3_C_Init` | `Exp3_C_Normal` | 1,000 |
| Exp4 | `Exp4_<M>_S<n>_Init` | `Exp4_N2`~`Exp4_N8` | 1,000 superframes |

Exp1/2 Normal은 비컨의 M을 적용하므로 M별 재빌드가 필요 없다. Exp3는
SFD/PHR이 비컨 필드가 아니므로 반드시 같은 문자 A/B/C의 INIT와 NORMAL을
사용한다. Exp4 Normal도 비컨의 M과 슬롯 배정을 적용한다.

## 2. Stage 0: lead 고정

### 2.1 측정 블록과 smoke test

- [ ] 첫 측정 블록은 철문 사이에 노드를 고정한 통제 NLOS로 수행한다.
- [ ] 노드 간 거리, 철문 열림 상태, 보드 높이, 안테나 방향, 금속면과의 거리,
      주변 물체를 사진과 치수로 기록하고 sweep 도중 움직이지 않는다.
- [ ] `Exp1_256_Init` + `Exp1_Normal`을 1회 실행해 같은 NLOS에서 RF 경로가
      존재하는지 확인한다. M256도 거의 전멸하면 배치를 조정한 뒤 다시 시작한다.
- [ ] M32 `Stage0_L12_T0_Init`과 `Stage0_L20_T0_Init` smoke test로 설정 적용,
      2,000회 종료, 스케줄 지연과 설정 오류가 없는지 확인한다.
- [ ] smoke test는 실행 절차 검증용이며 공식 독립 반복 횟수에 포함하지 않는다.

### 2.2 0~40 us 탐색 sweep

- [ ] TX는 항상 `Stage0_Normal`, RX는 해당 `Stage0_L<lead>_T0_Init`을 사용한다.
- [ ] 각 run 전에 INIT과 NORMAL을 모두 재시작하고 부팅 로그에서 `v2.6`,
      `EXP=1`, `DATA_PLEN=3(32sym)`, `data_psdu=26`, `TARGET=2000`, 선택한
      `LEAD`, `TAIL=0`을 확인한다.
- [ ] 0~40 us를 2 us 간격으로, 조건당 2,000 frame x 1 run 탐색한다.
- [ ] 시간 경과와 온도 드리프트가 lead 순서와 겹치지 않도록 아래 고정 무작위
      순서를 사용한다.

```text
20, 0, 40, 10, 30, 4, 24, 14, 34, 8, 28,
18, 38, 2, 22, 12, 32, 6, 26, 16, 36
```

- [ ] 앞서 수행한 lead 12/20 smoke 결과를 대신 사용하지 말고 위 순서에서 다시
      측정한다.
- [ ] 각 run 직후 INIT과 NORMAL의 부팅 설정 및 최종 통계 전체를 Notion 코드
      블록에 복사한다. 다음 요약 열도 함께 채운다.

| block | lead | tail | run | rx/expected | PER | FWTO | PTO | SFDTO | PHE/FCE/FSL | accum mode/min/max | TX attempts | beacon loss | delayed/config error |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---:|---:|---|
| iron-door-NLOS | 20 | 0 | explore-1 |  |  |  |  |  |  |  |  |  |  |

- [ ] `EXP1_DONE status=PASS`는 수집 완료 의미임을 유의하고, 실제 링크 결과는
      `rx`, `PER`, `link`를 함께 본다.
- [ ] 실패 프레임 accum은 `RXPRD`가 있고 값이 1~PLEN인 경우만 유효하다.
      `invalid_raw` 또는 stale 가능성이 표시된 값은 분석에서 제외한다.

### 2.3 전이 구간과 PAC 주기 후보 검증

- [ ] 탐색 결과에서 PER가 급변하는 경계, 국소 최저/최고점, accumCount가 바뀌는
      지점을 표시한다.
- [ ] 약 8.14 us 간격으로 PER 굴곡 또는 8-symbol 단위 accum 변화가 반복되는지
      확인한다. lead 12와 20 두 점만으로 PAC 주기성을 판정하지 않는다.
- [ ] 후보 지점 전후를 1 us 간격으로 보강한다. `Stage0_L0_T0_Init`부터
      `Stage0_L40_T0_Init`까지 모든 정수 lead 구성이 준비되어 있다.
- [ ] 최종 후보와 양옆 조건을 2,000 frame x 5 독립 run으로 반복한다. 각 run은
      양쪽 보드 reset부터 새로 시작한다.

### 2.4 LOS와 tail 대조

- [ ] NLOS 분석 후 보드를 1 m LOS 지그로 옮기고 새 replicate block으로 구분한다.
- [ ] LOS에서도 같은 탐색/보강 절차를 수행하되 NLOS 결과와 합쳐 평균내지 않는다.
- [ ] LOS/NLOS 모두에서 기준을 만족하는 가장 작은 안정 lead 후보를 정한다.
- [ ] `(lead,tail)=(0,0),(선택값,0),(0,100),(선택값,100)`을 비교한다.
- [ ] FWTO, PTO, SFDTO, PHE, FCE, FSL, accumCount 분포를 저장한다.
- [ ] 최종 lead와 tail을 동결하고 설정값, 선택 기준, 배제한 후보를 기록한다.

Stage 0 이후 lead를 바꾸면 Exp1/2/4의 제출용 데이터는 다시 측정한다.

## 3. Exp1: 최소 신뢰 프리앰블

- [ ] M=32/64/128/256, 거리=0.5/1/2/3/5 m, LOS/NLOS를 수행한다.
- [ ] 각 조건을 2,000 frame x 5 독립 run으로 측정한다.

## 4. Exp2: CIR 및 first-path SNR

- [ ] Exp1과 같은 지그에서 M=32/64/128/256을 수행한다.
- [ ] 거리=1/3 m, LOS/NLOS, 조건당 1,000 attempt x 3 run을 수행한다.
- [ ] RTT channel 1 원시 로그를 저장한다.
- [ ] 각 로그에 `CIR_CSV` 1,000행과 완료 PASS가 있는지 확인한다.
- [ ] 수신 실패가 있으면 로그를 숨기지 말고 PER 및 유효 CIR 수와 함께 기록한다.
- [ ] RSSI=-128 dBm은 주 결과에서 제외하고 FP-SNR을 사용한다.

## 5. Exp3: SFD/PHR airtime

- [ ] A, B, C를 각 1,000 capture x 3 독립 reset으로 수행한다.
- [ ] TX와 RX 모두 RTT channel 1 로그를 저장한다.
- [ ] TX에서 `attempts=success=captures=1000`, `status=PASS`를 확인한다.
- [ ] `B-A`로 추가 SFD 8 symbol 시간, `A-C`로 STD/DTA PHR 차이를 계산한다.
- [ ] 주 비교는 PSDU 26 B로 수행한다.
- [ ] 여유가 있으면 41/42, 82/83, 123/124, 127 B를 별도 보조 sweep으로
      측정하되 주 데이터와 섞지 않는다.

## 6. Exp4: 수용량과 goodput

- [ ] `Exp4_32_S1_Init` + `Exp4_N2`로 smoke test를 수행한다.
- [ ] S2에서 N2/N3의 슬롯 분류, wrong-slot, delayed-late, rearm 진단을 확인한다.
- [ ] M32/M64/M256에서 S1부터 가능한 실제 노드 수까지 증가시킨다.
- [ ] 각 조건을 1,000 superframe x 10 run으로 수행한다.
- [ ] 모든 노드 PER, all-slots-ok, aggregate goodput, wrong-slot,
      wrong-superframe, delayed-late, beacon error를 저장한다.
- [ ] 두 보드 반복 슬롯 시험은 `virtual-node load`로만 표기한다.

## 7. 매 run 종료 직후

- [ ] Stage 0/Exp1/Exp4는 SES scrollback이 지워지기 전에 INIT과 모든 NORMAL의
      부팅 설정 및 최종 통계 전체를 Notion 코드 블록에 즉시 복사한다.
- [ ] Notion에 조건 ID, 측정 시각, 물리 배치, Build Configuration, firmware
      banner, beacon config, expected/received, 오류 세분화, 최종 PASS/FAIL을 적는다.
- [ ] Exp2/Exp3는 개별 `CIR_CSV`/`EXP3_TX_CSV` 행이 필요하므로 SES 복사로
      대체하지 않고 RTT channel 1 원시 로그 파일을 저장한다.
- [ ] RTT logger를 사용한 run은 로그 파일을 닫고 파일 크기가 0이 아닌지 확인한다.
- [ ] expected/received 및 실험별 raw-row/capture 수를 확인한다.
- [ ] 실패 run도 삭제하지 않고 실패 원인과 함께 보관한다.
- [ ] 장비 위치가 움직였으면 새 replicate block으로 구분한다.

상세 통계 기준과 차량 case study는 `BRRS_IEEE_IOTJ_EXPERIMENT_PLAN_KO.md`를
따른다.
