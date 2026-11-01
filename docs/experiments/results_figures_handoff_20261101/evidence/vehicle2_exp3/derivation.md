# 2차 차량 Exp3 — 계산 airtime과 EXTTXE 실측 비교


## 주 비교: 일반 M256 대비 SFD·PHR 제거 M32

**동일 payload의 프레임 시간 효율은 계산상 4.940배, Exp3 실측 차분을 반영한 추정은 4.904배다. 약 4.9배로 요약한다.** 일반 M256은 SFD8·STD PHR을 포함하고, 비교 대상 M32는 SFD와 PHR을 모두 제거하여 preamble32와 동일 PSDU26B만 남긴다고 가정한다.

| 산정 방식 | 일반 M256 (µs) | SFD·PHR 제거 M32 (µs) | 시간 효율 배수 |
|---|---:|---:|---:|
| PHY 시간 계산 | 323.017 | 65.387 | 4.940배 |
| Exp3 기반 추정 | 323.871 | 66.044 | 4.904배 |

계산식: 일반 M256 = 260.514(preamble) + 8.142(SFD) + 21.539(PHR) + 32.822(PSDU) = 323.017µs. SFD·PHR 제거 M32 = 32.565(preamble) + 32.822(PSDU) = 65.387µs. 따라서 323.017 / 65.387 = 4.940배다.

Exp3 기반 추정에서는 SFD8 시간을 B−A = 8.213790µs로 추론한다. C에 남은 DTA PHR은 계산값 2.693µs를 사용하므로, 헤더 제거 M32 = C 실측 76.950920 − 8.213790 − 2.693 = 66.044130µs다. 일반 M256은 계산 323.017µs에 A의 실측−계산 차이 0.854009µs가 동일하게 적용된다고 가정하여 323.871009µs로 추정한다.

**4.9배는 SFD·PHR을 제거한 프레임의 시간 효율 추정이며, 헤더 제거 펌웨어로 직접 측정한 goodput이 아니다.** Exp3 A/B/C는 모두 SFD와 PHR을 포함한다. 헤더 제거 시 수신 성공률, 동기화 및 대체 정보 전달 비용은 이 실험에서 검증하지 않았다. 기존 Standard의 1.67배는 헤더를 유지하고 guard·비컨 등을 포함한 다른 비교 지표다.

2026-09-14에 완료한 A/B/C 3개 케이스를 정리했다. 각 조건에서 EXTTXE 캡처 1,000개, TX/RX 1,000/1,000, PER 0%였으며 공식 판정은 모두 PASS다.

공통 조건: CH5, preamble M32, PAC8, lead 23µs, PRF64, 6.81Mb/s, STS OFF, PSDU 26byte. 물리 N4(1050282818, 논리 N2) → INIT(1050270933) 단일 링크다.

## 조건별 절대 시간

단위 µs. 차이 = 실측 평균 − 계산값. 상대 차이의 분모는 계산값이다.

| 조건 | SFD(symbol) | PHR | 계산 airtime | 실측 평균 | 실측 최소–최대 | 차이 | 상대 차이 |
|---|---:|---|---:|---:|---:|---:|---:|
| A | 8 | STD | 95.068 | 95.922 | 95.688–96.063 | +0.854 | +0.898% |
| B | 16 | STD | 103.209 | 104.136 | 103.813–104.250 | +0.927 | +0.898% |
| C | 8 | DTA | 76.222 | 76.951 | 76.813–77.063 | +0.729 | +0.956% |

## SFD·PHR 변경 효과 비교

| 비교 | 의미 | 계산 시간차(µs) | 실측 시간차(µs) | 실측−계산(µs) | 상대 오차 |
|---|---|---:|---:|---:|---:|
| B−A | SFD 8→16 symbol 증가분 | 8.141 | 8.214 | +0.073 | +0.894% |
| A−C | PHR STD→DTA 단축분 | 18.846 | 18.971 | +0.125 | +0.664% |

SFD를 8→16 symbol로 늘리면 실측 약 8.214µs가 증가하고, PHR을 STD→DTA로 바꾸면 약 18.971µs가 단축된다. 두 변경 효과 모두 계산값과 상대 오차 1% 이내였다. 이 1%는 계산과 측정의 시간차 비교 결과이며, PER 합격 기준과는 별개다.

## 계산 및 측정 범위

`T_frame = T_preamble + T_SFD + T_PHR + T_PSDU`.

실행 당시 소스의 타이밍 모델을 재현했다. preamble/SFD는 symbol당 1,017.63ns, PHR은 21symbol × STD 1,025.64ns 또는 DTA 128.21ns다. PSDU는 26×8=208bit에 RS parity 48bit를 더한 256bit × 128.21ns다. 각 구성요소를 정수 ns로 올림한 뒤 합산한다.

| 구성요소 | A (µs) | B (µs) | C (µs) |
|---|---:|---:|---:|
| preamble | 32.565 | 32.565 | 32.565 |
| sfd | 8.142 | 16.283 | 8.142 |
| phr | 21.539 | 21.539 | 2.693 |
| psdu | 32.822 | 32.822 | 32.822 |

실측은 송신기의 EXTTXE 상승·하강 에지를 TIMER4/GPIOTE/PPI로 캡처한 폭이며, 타이머 해상도는 62.5ns다. 전파의 ToF나 수신기 소프트웨어 처리 시간 측정이 아니다. 평균의 소수점 자릿수는 개별 측정 해상도와 다르다.

계산값은 PHY 프레임 airtime이며 EXTTXE 폭에는 칩의 송신 준비·종료 구간이 포함될 수 있다. 따라서 절대 시간 차이를 모두 모델 오차나 고정 지연이라고 단정하지 않는다. 동일한 고정 오프셋이 있다면 조건 간 차분에서 소거되므로 변경 효과는 두 번째 표로 비교한다. 조건별 RF 실행은 각 1회이며, 1,000프레임은 독립 실행 1,000회를 뜻하지 않는다.

## 검증 및 근거

보존된 assessment의 실측 평균·최소·최대·캡처 수를 사용했다. 이번 작업에서 Air 원시 로그를 다시 읽어 평균을 재계산한 것은 아니다. 실행 당시 수집·metadata·END·readback·HALT PASS 기록을 대조했고, RX timeout/error, delayed-late, PHY config 오류 및 SYNC loss는 기록상 모두 0이다. 펌웨어 계산에 사용한 로컬 소스 SHA256이 각 실행 assessment의 source pin과 일치함을 확인했다.

- [기존 assessment 정규화 자료](/home/chieon/DWM3000/analysis_records/vehicle_chronology_20260916/normalized_cases.json)
- [실행 소스의 계산식](/home/chieon/DWM3000/logs/air_host_recovery_20260914_103508/controller/sdk/Drivers/API/Src/examples/ex_35b_brrs_normal/brrs_normal.c:291)
- [Exp3 측정 정의](/home/chieon/DWM3000/logs/air_host_recovery_20260914_103508/controller/sdk/Drivers/API/EXP3_EXTTXE_README.md)
- [정밀 수치 및 로그 경로](/home/chieon/DWM3000/analysis_records/vehicle2_exp3_20260917/comparison.json)

| 조건 | INIT 시작(KST) | Case ID |
|---|---|---|
| A | 2026-09-14T13:39:01.023740+09:00 | `standard_exp3_m32_pac8_l23_A_ch5_b01` |
| B | 2026-09-14T13:40:11.702645+09:00 | `standard_exp3_m32_pac8_l23_B_ch5_b01` |
| C | 2026-09-14T13:41:20.072785+09:00 | `standard_exp3_m32_pac8_l23_C_ch5_b01` |

Air 원본 실행 root:

- A: `/Users/songchieon/Desktop/DWM3000/logs/air_host_recovery_20260914_103508/standard_ch5_air_20260914_103508/157_exp3_a1_6b2609`
- B: `/Users/songchieon/Desktop/DWM3000/logs/air_host_recovery_20260914_103508/standard_ch5_air_20260914_103508/158_exp3_a1_5de879`
- C: `/Users/songchieon/Desktop/DWM3000/logs/air_host_recovery_20260914_103508/standard_ch5_air_20260914_103508/159_exp3_a1_a38fb7`

## 참고: 헤더를 유지한 기존 Standard의 최대 부하 goodput

**실제 최대 부하의 앱 goodput은 M32가 M256의 1.667배(+66.67%)다.** 2차 차량 Standard의 PAC8·CH5·S6 block 1 완료 케이스끼리 비교했다. 공통 SB1700/SP2200/guard200µs, 빠른 PHY 전환 ON이며, 명목 주기 10ms × 1,000 SF = 10초에 수신한 앱 payload 16byte를 기준으로 한다. 시작 대기·빌드·플래시 시간은 포함하지 않는다.

| 항목 | M32/PAC8 | M256/PAC8 |
|---|---:|---:|
| 10ms당 슬롯 | 20 | 12 |
| DATA RX / offered | 20,000 / 20,000 | 12,000 / 12,000 |
| 전체·최악 노드 PER | 0% / 0% | 0% / 0% |
| 앱 goodput (kbps) | 256.00 | 153.60 |
| 공식 판정 | PASS | PASS |

`goodput = DATA RX × 16byte × 8 / 10초`이므로 `256 / 153.6 = 1.6667배`다. 현재 완료된 최대 부하 비교는 조건별 1회이며 전체 반복 검증 완료나 모든 환경의 성능 보장을 의미하지 않는다. 같은 K6로 제한하면 양쪽 무손실 goodput은 모두 76.8kbps로 1배다. 1.667배는 짧은 프레임으로 더 많은 슬롯을 수용한 최대 부하 비교다.

Exp3까지 고려한 프레임 단위 비교는 다음과 같다. 실제 Exp4 빌드는 `EXP3_PHY_VARIANT=1`, 즉 A(SFD8·STD PHR)를 사용한다. C(DTA PHR)의 18.971µs 단축을 현재 Standard 성능에 추가로 적용하거나, airtime 비율과 goodput 비율을 곱하지 않는다.

| 비교 범위 | M32 시간(µs) | M256 시간(µs) | 동일 payload당 시간 효율(M256/M32) | 근거 |
|---|---:|---:|---:|---|
| PHY 프레임 계산 | 95.068 | 323.017 | 3.398배 | 양쪽 모두 SFD8·STD PHR·PSDU26B |
| Exp3 오프셋을 반영한 추정 | 95.922 (실측) | 323.871 (추정) | 3.376배 (추정) | M32의 실측−계산 차이가 M256에도 같다고 가정 |

M256의 EXTTXE 폭을 이 Exp3에서 직접 측정하지 않았으므로 약 3.38배를 양쪽 실측 성능이라고 쓰면 안 된다. 프레임 시간 효율 약 3.4배가 전체 goodput 1.67배로 줄어드는 것은 슬롯별 guard, 비컨·PHY 전환을 위한 고정 시간, 정수 슬롯 수 제약을 포함하기 때문이다. 실제 시스템 성능의 대표 배수는 위의 **1.67배**를 사용한다.

- [Exp4 A 프로파일 지정](/home/chieon/DWM3000/logs/air_host_recovery_20260914_103508/controller/sdk/Drivers/API/brrs_exp4_build.sh:276)
- [슬롯 간격 및 최대 슬롯 계산](/home/chieon/DWM3000/logs/air_host_recovery_20260914_103508/controller/sdk/Drivers/API/Src/examples/ex_35b_brrs_normal/brrs_normal.c:347)

최대 부하 비교 원본:

- M32: `standard_exp4_m32_pac8_l23_k20_ch5_s6_b01` (2026-09-14T14:22:43.315042+09:00); `/Users/songchieon/Desktop/DWM3000/logs/air_host_recovery_20260914_103508/standard_ch5_air_20260914_103508/180_exp4_a1_089560`
- M256: `standard_exp4_m256_pac8_l23_k12_ch5_s6_b01` (2026-09-14T14:28:23.767662+09:00); `/Users/songchieon/Desktop/DWM3000/logs/air_host_recovery_20260914_103508/standard_ch5_air_20260914_103508/182_exp4_a1_e8cb0f`
