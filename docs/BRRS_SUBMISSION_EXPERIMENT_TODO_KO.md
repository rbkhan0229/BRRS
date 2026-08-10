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

- [ ] 고정 1 m LOS에서 lead 0, 2, 4, 6, 8, 10, 12, 14, 15, 16, 20 us를
      각각 2,000 frame x 5 run 수행한다.
- [ ] `(lead,tail)=(0,0),(선택값,0),(0,100),(선택값,100)`을 비교한다.
- [ ] FWTO, PTO, SFDTO, PHE, FCE, FSL, accumCount 분포를 저장한다.
- [ ] 통제 NLOS에서 전이 구간을 반복한다.
- [ ] LOS/NLOS 모두에서 기준을 만족하는 가장 작은 안정 lead를 고정한다.

Stage 0 이후 lead를 바꾸면 Exp1/2/4의 제출용 데이터는 다시 측정한다.

## 3. Exp1: 최소 신뢰 프리앰블

- [ ] M=32/64/128/256, 거리=0.5/1/2/3/5 m, LOS/NLOS를 수행한다.
- [ ] 각 조건을 2,000 frame x 5 독립 run으로 측정한다.
- [ ] 같은 배치 블록에서는 보드를 움직이지 않고 M 순서만 무작위화한다.
- [ ] PER, 오류 원인, beacon loss, delayed-late를 분리해 저장한다.
- [ ] 각 run의 `expected=2000`과 종료 상태를 확인한다.

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

- [ ] 원시 로그 파일을 닫고 파일 크기가 0이 아닌지 확인한다.
- [ ] expected/received/raw-row/capture 수를 확인한다.
- [ ] firmware banner, beacon config, 최종 PASS/FAIL을 manifest에 적는다.
- [ ] 실패 run도 삭제하지 않고 실패 원인과 함께 보관한다.
- [ ] 장비 위치가 움직였으면 새 replicate block으로 구분한다.

상세 통계 기준과 차량 case study는 `BRRS_IEEE_IOTJ_EXPERIMENT_PLAN_KO.md`를
따른다.
