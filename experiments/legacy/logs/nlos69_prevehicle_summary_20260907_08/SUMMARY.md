# 차량 실험 직전 NLOS 6.9m 실험 항목 정리

대상은 **2026년 9월 7일 밤부터 9월 8일 새벽까지**, 차량에 설치하기 전에 수행한 NLOS 6.9m 준비 실험이다. 이전 8월·9월 초 실험이나 이후 차량 실험을 섞지 않았다. 저장된 원문과 재검증 결과를 정리했으며 이번 정리에서 추가 RF·SSH·플래시를 수행하지 않았다.

**Stage0부터 Exp5까지 실행했고, 중복 재사용을 제외한 실제 RF bundle은 86개다. 마지막 40조건은 모두 PER 기준을 통과했으며, 그중 TX 6대 16조건은 각 노드 PER <1%였다. 앞서 수행한 lead 탐색의 실패까지 모두 통과한 것으로 바꾸지는 않는다.**

## 1. 단계별로 무엇을 했는가

| 단계 | 확인한 항목 | 실제 RF 수 | 결과와 범위 |
|---|---|---:|---|
| Stage0 | M32에서 PAC4/PAC8별 RX lead 탐색, 물리 N4 단일 링크 | 18 | 각 PAC 9점, 조건당 2,000회. lead 0은 둘 다 RX 0; PAC8/10µs는 PER 38.65%. 전체 정수 lead 탐색은 아님 |
| Exp1 | M32/64/128/256 × PAC4/PAC8의 PER·프리앰블 누적 진단 | 7 | 8개 M/PAC 지점 확보. M32/PAC8 한 지점은 HEX가 동일한 Stage0 RF를 재사용. 조건당 2,000회, 최대 PER 0.05% |
| Exp2 | 같은 8개 M/PAC 지점의 CIR·FP-SNR | 8 | 조건당 1,000회. M256/PAC4만 RX 999, PER 0.1%; 나머지 RX 1,000. 성공 패킷별 CIR 확보 |
| Exp3 | SFD·PHR 설정 A/B/C의 실제 송신 airtime | 3 | 각 1,000개 하드웨어 EXTTXE 표본, PER 0%. 평균 A 95.882µs / B 104.091µs / C 76.973µs |
| Exp4 | 다중 TX TDMA, 역할 회전, PAC/lead, 슬롯 부하·용량 점검 | 49 | 최초 TX6 확인 1 + 네트워크 lead 20 + 전선 보고 후 재측정 1 + 마지막 행렬 27. 탐색 실패 포함 |
| Exp5 | M1024/PAC32에서 채널 관측용 CIR·raw CIR | 1 | RX 1,000/1,000, PER 0%; CIR 1,000행, raw 30프레임 × 300샘플 |
| 합계 | 재사용·후처리 재실행은 RF 수에 중복 가산하지 않음 | **86** | **PER PASS 66, FAIL_PER 20**. 임계/실패 조건도 보존 |

Stage0의 FAIL_PER는 3개, Exp4의 FAIL_PER는 17개다. 빌드 단계에서 멈춰 RF가 없었던 시도는 위 86회에 포함하지 않았다. Stage0의 수신 0 조건은 실패로 유지한다. 통과 판정은 전체 평균이 아니라 참여한 각 물리 노드의 offered 기준 PER <1%다.

## 2. 연결·역할·공통 조건

당시 로컬 RX와 SSH `s-macbook-air`의 TX 6대, 총 7개의 J-Link를 확인했다. TX USB 허브는 사용자가 외부 전원 연결을 확인했다. NLOS 6.9m는 사용자가 설치한 환경 명칭이며 높이·방향 등 상세 배치를 독립 계측한 자료는 아니다.

| 물리 보드 | J-Link serial | 역할 사용 |
|---|---|---|
| INIT/RX | 1050270933 | 코디네이터 겸 RX |
| N2 | 1050211584 | 다중 TX |
| N3 | 1050273888 | 다중 TX |
| N4 | 1050282818 | 다중 TX 및 모든 1:1 실험의 TX |
| N5 | 1050208509 | 다중 TX |
| N6 | 1050227627 | 다중 TX |
| N7 | 1050204212 | 다중 TX |

1:1에서는 물리 N4에 논리 N2 역할을 부여했다. 다중 TX lead 탐색의 block2는 물리 N3/N4/N5/N6/N7/N2 → 논리 N2/N3/N4/N5/N6/N7로 회전했다. 마지막 40조건은 block1 원래 배정 기준이다. 이 문서의 노드별 실패 표시는 물리 보드 번호다.

Exp4는 SF 10ms, 1,000 SF, G250, SB/SP 3,000/2,500µs, 슬롯별 bounded scheduled delayed-RX, 재전송 없는 조건이다. 최종 비교는 PAC4/lead 27µs와 PAC8/lead 26µs를 사용했다. 두 값은 준비 실험에서 고른 후보이며 전역 최적값이나 차량 최적값을 뜻하지 않는다. M64 이상은 심볼별 특성·용량 비교 항목으로, M32 요구사항을 대체하는 해결책이 아니다.

## 3. 마지막 TX 6대 비교: 16조건 모두 각 노드 PER <1%

독립 TX는 6대이며 슬롯 수는 별개다. 같은 보드가 한 SF에 여러 슬롯을 사용했다. 아래 각 행을 PAC4와 PAC8로 한 번씩 실행했으므로 총 16조건이다.

| M | 슬롯/SF | PAC4 / lead27µs 최악 PER(%) | PAC8 / lead26µs 최악 PER(%) |
| --- | --- | --- | --- |
| 32 | 6 | 0 | 0.1 |
| 32 | 12 | 0 | 0 |
| 64 | 6 | 0 | 0 |
| 64 | 12 | 0 | 0 |
| 128 | 6 | 0 | 0 |
| 128 | 10 | 0 | 0 |
| 256 | 6 | 0 | 0 |
| 256 | 8 | 0 | 0 |

위 PER은 **각 조건에서 가장 높은 물리 노드 PER(%)**다. 총 132,000 offered / 132,000 TX 성공 / 131,999 RX였다. 유일한 손실은 M32/PAC8/lead26/6슬롯의 물리 N5에서 1/1,000 = 0.10%, PHR 오류 1회였다. 나머지 노드×조건은 모두 0%였다.

16조건에서 beacon 미수신, delayed-TX late, wrong slot/SF, delayed-RX late, RDB mismatch/incomplete/resync/overrun, SPI/queue 오류는 0이었다. PAC 간 TX HEX와 역할은 각 짝에서 같지만 RX PAC·lead 및 연동 timeout/수신창이 다르며 PAC4를 먼저 측정했다. 따라서 한 패킷 차이로 PAC4가 더 좋다고 결론내릴 수 없다.

## 4. M32·13슬롯은 별도 결과로 유지

| 배정·상황 | PAC / lead | 가장 높은 노드 PER | 판정 |
|---|---|---|---|
| 최초 원래 배정 | PAC8 / 25µs | N7 0.70%; N6 0.50%, N2~N5 0% | PASS |
| block2 역할 회전 | PAC8 / 25µs | N7 0.55% | PASS |
| block2 역할 회전 | PAC8 / 26µs | 전 노드 0% | PASS |
| block2 역할 회전 | PAC8 / 27µs | 전 노드 0% | PASS |
| block2 역할 회전 | PAC4 / 27µs | N7 1.00% | FAIL: <1% 미달 |
| 전선 가림 보고 후 같은 설정 재측정 | PAC4 / 27µs | N7 6.80%; 나머지 0% | FAIL |

최초 PAC8/25µs는 총 13,000 offered/TX, RX 12,976, 전체 PER 0.1846%였다. 오류는 RXFSL 23, PHR 1회였다. 전선 보고 후 PAC4/27µs는 TX 13,000회 모두 성공했지만 RX 12,864, FWTO 130 / PHR 1 / RXFSL 5회였고 수집은 정상 종료했다.

다른 lead에서 큰 손실도 관측됐다. 예를 들어 PAC8/20µs의 물리 N7은 71.4%, PAC4/20µs는 64.9%였다. PAC8은 25~27µs에서 낮은 손실 구간이 관측됐지만, PAC4는 이 13슬롯 탐색에서 <1%를 충족하지 못했다. 전선이 가린 보드와 시작 시각이 불명이라 이전 결과를 임의로 제외하거나 합산하지 않았다.

**마지막 16조건의 M32는 6·12슬롯이다. 그 통과를 PAC4의 13슬롯 통과로 확장할 수 없다.** 원래 배정·회전 배정, 부하, 시각이 달라 13슬롯과 12슬롯 차이의 단일 원인을 정할 수도 없다. 전체 sweep의 물리 노드별 PER·오류는 [lead 탐색 원문](../nlos69_lead_screen_rotation_20260907_2326/RESULTS.md)에 있다.

## 5. 심볼별 부하와 후처리 결과

| DATA M | 마지막 TX6에서 통과한 높은 슬롯 부하 | 현재 타이밍 모델 상한 | 해당 부하의 app goodput |
|---|---:|---:|---:|
| 32 | 12 | 13 | 153.6 kbps |
| 64 | 12 | 12 | 153.6 kbps |
| 128 | 10 | 10 | 128.0 kbps |
| 256 | 8 | 8 | 102.4 kbps |

Application payload 16B, DATA PSDU 26B, 6.8Mbps 조건이다. M32의 13슬롯은 위 별도 PAC8 측정 이력으로만 기록한다. 표는 현재 가드·시간 예산에서 검사한 부하이며, 일반적인 PHY 최대 용량이나 논문용 반복·회전을 마친 최대 수용 노드 수는 아니다.

Exp1은 M32/PAC4와 M256/PAC8만 offered 2,000 중 RX 1,999(0.05%)였다. 특히 M32/PAC4는 TX 자체도 1,999회이므로 한 손실을 DATA 송신 후 수신 실패로 표현하지 않는다. PAC8의 M32/M64 지점은 이전 lead25 자료, 나머지 최종 PAC8 지점은 lead26 자료다.

Exp3의 B−A는 8.209µs, A−C는 18.909µs였다. Exp5의 관측 PDP RMS 폭 평균은 28.91ns(27.18~31.62ns)이며 수신기 응답이 포함된 값으로, 순수 전파 채널의 지연확산이나 Rician K-factor로 해석하지 않는다.

후처리에서 CIR 전력 진단의 제한도 확인했다. M32 RSSI의 -128dBm 하한/오류 값을 실제 전력으로 평균하면 안 된다. 드라이버의 RX code 추출 문제로 FP dBm 상수 선택이 잘못돼 약 7.8984dB 차이가 생긴 경로를 확인했지만, 별도 추정 보정일 뿐 절대 전력 교정은 아니다. CIR 기반 FP-SNR는 이 dBm 계산 경로를 사용하지 않는다. 이 진단 문제를 Exp4 PER 원인으로 확정하지 않았다. 상세 근거는 [기존 후처리 보고서](../vehicle_onepass_run_20260908_0039/postprocess_20260908/REPORT.md)에 있다.

## 6. 실제로 생략하거나 완료하지 않은 부분

- 마지막 40조건은 Exp1 6 + Exp2 7 + Exp4 27이며, Exp4 27은 TX1 8 + TX2 3 + TX6 16이다. 이 마지막 묶음은 모두 PER 기준을 통과했다.
- 남은 소수 TX 28조건은 시간 절약 요청으로 생략했다: TX2/M256/PAC4 1개, TX2/PAC8의 M64/128/256 3개, TX3~5의 M32/64/128/256 × PAC4/8 24개. TX2/M32/PAC8은 앞선 lead20/block2의 실제 결과를 재사용했다.
- TX2/M256/PAC4는 준비 빌드 실패 후 RF 없이 남았다. Stage0 PAC4/25의 첫 병렬 빌드 실패는 RF 전에 복구했고 실제 RF는 1회만 수행했다. 최초 TX6 확인의 준비 실패도 RF 수에 넣지 않았다.
- 현재 로그 형식과 분석기의 불일치는 독립 후처리 사본에서 보완하고 같은 raw를 다시 분석했다. 분석 실패가 해결됐다고 동일 RF를 중복 가산하지 않았다.
- 전체 정수 lead 최적화, 모든 논문 반복·역할 회전, 차량 조건 검증은 이 NLOS 캠페인의 완료 항목이 아니다.
- 이 자료는 현재 슬롯별 delayed-RX와 과거 continuous/manual-rearm의 통제된 A/B가 아니다. scheduled-RX에서 낮은 PER이 가능함은 확인했지만 continuous RX가 과거 고손실의 단일 원인이었다고 확정할 수 없다.

당시 마지막에는 원래 보드 역할의 M32/PAC8/lead25/TX6/13슬롯 이미지로 복원하고 MCU halt를 검증했다. 복원 뒤 RF는 없었다. 이는 그 시점 종료 기록이며 이후 차량 실험에서 쓴 lead26과 혼동하지 않는다. 이번 정리는 이후 보드 상태를 변경하지 않았다.

## 7. 원문과 전체 실행 목록

- [최초 NLOS TX6·13슬롯 확인](../exp4_nlos69_s6_pac8_recheck_20260907_2248/RESULTS.md)
- [Stage0~Exp5 대표 경로 7회](../vehicle_suite_nlos69_smoke_20260907_2301/RESULTS.md)
- [Stage0 lead 탐색·역할 회전·네트워크 sweep](../nlos69_lead_screen_rotation_20260907_2326/RESULTS.md)
- [전선 보고 후 PAC4/27 재측정](../exp4_pac4_lead27_cable_recheck_20260908_0018/RESULTS.md)
- [마지막 40조건 결과](../vehicle_onepass_run_20260908_0039/RESULTS.md)
- [후처리·원문 및 HEX hash 검증](../vehicle_onepass_run_20260908_0039/postprocess_20260908/REPORT.md)
- [이 문서의 실행 목록 JSON](SUMMARY.json), [재검증 원장](../vehicle_onepass_run_20260908_0039/postprocess_20260908/revalidated_assessments.json)

아래는 각 실제 RF bundle을 한 번씩만 기재한다. TX는 물리 보드 수, K는 SF당 슬롯 수이며 M/PAC/lead는 저장된 조건이다. 노드별 offered/TX/RX/PER는 SUMMARY.json, 실제 serial·역할·HEX/ELF hash와 수집 증거는 연결된 bundle 및 재검증 원장에 보존돼 있다.

| 단계 | 실행 | M | PAC | lead µs | TX | K | 최악 노드 PER(%) | 판정 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| stage0 | [stage0_m32_pac4_l0](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac4_l0) | 32 | 4 | 0 | 1 | 1 | 100 | FAIL_PER |
| stage0 | [stage0_m32_pac4_l10](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac4_l10) | 32 | 4 | 10 | 1 | 1 | 0.05 | PASS |
| stage0 | [stage0_m32_pac4_l19](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac4_l19) | 32 | 4 | 19 | 1 | 1 | 0 | PASS |
| stage0 | [stage0_m32_pac4_l20](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac4_l20) | 32 | 4 | 20 | 1 | 1 | 0 | PASS |
| stage0 | [stage0_m32_pac4_l21](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac4_l21) | 32 | 4 | 21 | 1 | 1 | 0 | PASS |
| stage0 | [stage0_m32_pac4_l25](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac4_l25) | 32 | 4 | 25 | 1 | 1 | 0 | PASS |
| stage0 | [stage0_m32_pac4_l30](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac4_l30) | 32 | 4 | 30 | 1 | 1 | 0 | PASS |
| stage0 | [stage0_m32_pac4_l40](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac4_l40) | 32 | 4 | 40 | 1 | 1 | 0.05 | PASS |
| stage0 | [stage0_m32_pac4_l5](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac4_l5) | 32 | 4 | 5 | 1 | 1 | 0 | PASS |
| stage0 | [stage0_m32_pac8_l0](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac8_l0) | 32 | 8 | 0 | 1 | 1 | 100 | FAIL_PER |
| stage0 | [stage0_m32_pac8_l10](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac8_l10) | 32 | 8 | 10 | 1 | 1 | 38.65 | FAIL_PER |
| stage0 | [stage0_m32_pac8_l15](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac8_l15) | 32 | 8 | 15 | 1 | 1 | 0.05 | PASS |
| stage0 | [stage0_m32_pac8_l19](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac8_l19) | 32 | 8 | 19 | 1 | 1 | 0.05 | PASS |
| stage0 | [stage0_m32_pac8_l20](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac8_l20) | 32 | 8 | 20 | 1 | 1 | 0 | PASS |
| stage0 | [stage0_m32_pac8_l21](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac8_l21) | 32 | 8 | 21 | 1 | 1 | 0 | PASS |
| stage0 | [stage0_m32_pac8_l25](/Users/songchieon/Desktop/DWM3000/logs/vehicle_suite_nlos69_smoke_20260907_2301/stage0_m32_pac8_l25) | 32 | 8 | 25 | 1 | 1 | 0 | PASS |
| stage0 | [stage0_m32_pac8_l30](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac8_l30) | 32 | 8 | 30 | 1 | 1 | 0 | PASS |
| stage0 | [stage0_m32_pac8_l40](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/stage0_m32_pac8_l40) | 32 | 8 | 40 | 1 | 1 | 0 | PASS |
| exp1 | [exp1_m64_pac8_l25](/Users/songchieon/Desktop/DWM3000/logs/vehicle_suite_nlos69_smoke_20260907_2301/exp1_m64_pac8_l25) | 64 | 8 | 25 | 1 | 1 | 0 | PASS |
| exp1 | [paper_exp1_m128_pac4_l27_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp1_m128_pac4_l27_b01) | 128 | 4 | 27 | 1 | 1 | 0 | PASS |
| exp1 | [paper_exp1_m128_pac8_l26_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp1_m128_pac8_l26_b01) | 128 | 8 | 26 | 1 | 1 | 0 | PASS |
| exp1 | [paper_exp1_m256_pac4_l27_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp1_m256_pac4_l27_b01) | 256 | 4 | 27 | 1 | 1 | 0 | PASS |
| exp1 | [paper_exp1_m256_pac8_l26_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp1_m256_pac8_l26_b01) | 256 | 8 | 26 | 1 | 1 | 0.05 | PASS |
| exp1 | [paper_exp1_m32_pac4_l27_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp1_m32_pac4_l27_b01) | 32 | 4 | 27 | 1 | 1 | 0.05 | PASS |
| exp1 | [paper_exp1_m64_pac4_l27_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp1_m64_pac4_l27_b01) | 64 | 4 | 27 | 1 | 1 | 0 | PASS |
| exp2 | [exp2_m32_pac8_l25](/Users/songchieon/Desktop/DWM3000/logs/vehicle_suite_nlos69_smoke_20260907_2301/exp2_m32_pac8_l25) | 32 | 8 | 25 | 1 | 1 | 0 | PASS |
| exp2 | [paper_exp2_m128_pac4_l27_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp2_m128_pac4_l27_b01) | 128 | 4 | 27 | 1 | 1 | 0 | PASS |
| exp2 | [paper_exp2_m128_pac8_l26_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp2_m128_pac8_l26_b01) | 128 | 8 | 26 | 1 | 1 | 0 | PASS |
| exp2 | [paper_exp2_m256_pac4_l27_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp2_m256_pac4_l27_b01) | 256 | 4 | 27 | 1 | 1 | 0.1 | PASS |
| exp2 | [paper_exp2_m256_pac8_l26_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp2_m256_pac8_l26_b01) | 256 | 8 | 26 | 1 | 1 | 0 | PASS |
| exp2 | [paper_exp2_m32_pac4_l27_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp2_m32_pac4_l27_b01) | 32 | 4 | 27 | 1 | 1 | 0 | PASS |
| exp2 | [paper_exp2_m64_pac4_l27_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp2_m64_pac4_l27_b01) | 64 | 4 | 27 | 1 | 1 | 0 | PASS |
| exp2 | [paper_exp2_m64_pac8_l26_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp2_m64_pac8_l26_b01) | 64 | 8 | 26 | 1 | 1 | 0 | PASS |
| exp3 | [exp3_m32_pac8_l25_A](/Users/songchieon/Desktop/DWM3000/logs/vehicle_suite_nlos69_smoke_20260907_2301/exp3_m32_pac8_l25_A) | 32 | 8 | 25 | 1 | 1 | 0 | PASS |
| exp3 | [exp3_m32_pac8_l25_B](/Users/songchieon/Desktop/DWM3000/logs/vehicle_suite_nlos69_smoke_20260907_2301/exp3_m32_pac8_l25_B) | 32 | 8 | 25 | 1 | 1 | 0 | PASS |
| exp3 | [exp3_m32_pac8_l25_C](/Users/songchieon/Desktop/DWM3000/logs/vehicle_suite_nlos69_smoke_20260907_2301/exp3_m32_pac8_l25_C) | 32 | 8 | 25 | 1 | 1 | 0 | PASS |
| exp4 | [exp4_m32_pac8_l25_k13](/Users/songchieon/Desktop/DWM3000/logs/exp4_nlos69_s6_pac8_recheck_20260907_2248/capture1) | 32 | 8 | 25 | 6 | 13 | 0.7 | PASS |
| exp4 | [paper_exp4_m128_pac4_l27_k10_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m128_pac4_l27_k10_s6_b01) | 128 | 4 | 27 | 6 | 10 | 0 | PASS |
| exp4 | [paper_exp4_m128_pac4_l27_k1_s1_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m128_pac4_l27_k1_s1_b01) | 128 | 4 | 27 | 1 | 1 | 0 | PASS |
| exp4 | [paper_exp4_m128_pac4_l27_k2_s2_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m128_pac4_l27_k2_s2_b01) | 128 | 4 | 27 | 2 | 2 | 0 | PASS |
| exp4 | [paper_exp4_m128_pac4_l27_k6_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m128_pac4_l27_k6_s6_b01) | 128 | 4 | 27 | 6 | 6 | 0 | PASS |
| exp4 | [paper_exp4_m128_pac8_l26_k10_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m128_pac8_l26_k10_s6_b01) | 128 | 8 | 26 | 6 | 10 | 0 | PASS |
| exp4 | [paper_exp4_m128_pac8_l26_k1_s1_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m128_pac8_l26_k1_s1_b01) | 128 | 8 | 26 | 1 | 1 | 0 | PASS |
| exp4 | [paper_exp4_m128_pac8_l26_k6_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m128_pac8_l26_k6_s6_b01) | 128 | 8 | 26 | 6 | 6 | 0 | PASS |
| exp4 | [paper_exp4_m256_pac4_l27_k1_s1_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m256_pac4_l27_k1_s1_b01) | 256 | 4 | 27 | 1 | 1 | 0 | PASS |
| exp4 | [paper_exp4_m256_pac4_l27_k6_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m256_pac4_l27_k6_s6_b01) | 256 | 4 | 27 | 6 | 6 | 0 | PASS |
| exp4 | [paper_exp4_m256_pac4_l27_k8_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m256_pac4_l27_k8_s6_b01) | 256 | 4 | 27 | 6 | 8 | 0 | PASS |
| exp4 | [paper_exp4_m256_pac8_l26_k1_s1_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m256_pac8_l26_k1_s1_b01) | 256 | 8 | 26 | 1 | 1 | 0 | PASS |
| exp4 | [paper_exp4_m256_pac8_l26_k6_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m256_pac8_l26_k6_s6_b01) | 256 | 8 | 26 | 6 | 6 | 0 | PASS |
| exp4 | [paper_exp4_m256_pac8_l26_k8_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m256_pac8_l26_k8_s6_b01) | 256 | 8 | 26 | 6 | 8 | 0 | PASS |
| exp4 | [paper_exp4_m32_pac4_l20_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac4_l20_k13_s6_b02) | 32 | 4 | 20 | 6 | 13 | 64.9 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac4_l24_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac4_l24_k13_s6_b02) | 32 | 4 | 24 | 6 | 13 | 51.85 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac4_l25_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac4_l25_k13_s6_b02) | 32 | 4 | 25 | 6 | 13 | 56.5 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac4_l26_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac4_l26_k13_s6_b02) | 32 | 4 | 26 | 6 | 13 | 53.65 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac4_l27_k12_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m32_pac4_l27_k12_s6_b01) | 32 | 4 | 27 | 6 | 12 | 0 | PASS |
| exp4 | [paper_exp4_m32_pac4_l27_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/exp4_pac4_lead27_cable_recheck_20260908_0018/capture1) | 32 | 4 | 27 | 6 | 13 | 6.8 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac4_l27_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac4_l27_k13_s6_b02) | 32 | 4 | 27 | 6 | 13 | 1 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac4_l27_k1_s1_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m32_pac4_l27_k1_s1_b01) | 32 | 4 | 27 | 1 | 1 | 0 | PASS |
| exp4 | [paper_exp4_m32_pac4_l27_k2_s2_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m32_pac4_l27_k2_s2_b01) | 32 | 4 | 27 | 2 | 2 | 0 | PASS |
| exp4 | [paper_exp4_m32_pac4_l27_k6_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m32_pac4_l27_k6_s6_b01) | 32 | 4 | 27 | 6 | 6 | 0 | PASS |
| exp4 | [paper_exp4_m32_pac4_l28_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac4_l28_k13_s6_b02) | 32 | 4 | 28 | 6 | 13 | 48.75 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac4_l32_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac4_l32_k13_s6_b02) | 32 | 4 | 32 | 6 | 13 | 47.35 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac4_l36_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac4_l36_k13_s6_b02) | 32 | 4 | 36 | 6 | 13 | 46.75 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac4_l40_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac4_l40_k13_s6_b02) | 32 | 4 | 40 | 6 | 13 | 47.7 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac8_l20_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac8_l20_k13_s6_b02) | 32 | 8 | 20 | 6 | 13 | 71.4 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac8_l20_k2_s2_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac8_l20_k2_s2_b02) | 32 | 8 | 20 | 2 | 2 | 0 | PASS |
| exp4 | [paper_exp4_m32_pac8_l24_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac8_l24_k13_s6_b02) | 32 | 8 | 24 | 6 | 13 | 3.3 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac8_l25_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac8_l25_k13_s6_b02) | 32 | 8 | 25 | 6 | 13 | 0.55 | PASS |
| exp4 | [paper_exp4_m32_pac8_l26_k12_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m32_pac8_l26_k12_s6_b01) | 32 | 8 | 26 | 6 | 12 | 0 | PASS |
| exp4 | [paper_exp4_m32_pac8_l26_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac8_l26_k13_s6_b02) | 32 | 8 | 26 | 6 | 13 | 0 | PASS |
| exp4 | [paper_exp4_m32_pac8_l26_k1_s1_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m32_pac8_l26_k1_s1_b01) | 32 | 8 | 26 | 1 | 1 | 0 | PASS |
| exp4 | [paper_exp4_m32_pac8_l26_k6_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m32_pac8_l26_k6_s6_b01) | 32 | 8 | 26 | 6 | 6 | 0.1 | PASS |
| exp4 | [paper_exp4_m32_pac8_l27_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac8_l27_k13_s6_b02) | 32 | 8 | 27 | 6 | 13 | 0 | PASS |
| exp4 | [paper_exp4_m32_pac8_l28_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac8_l28_k13_s6_b02) | 32 | 8 | 28 | 6 | 13 | 11.55 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac8_l30_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac8_l30_k13_s6_b02) | 32 | 8 | 30 | 6 | 13 | 14.25 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac8_l32_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac8_l32_k13_s6_b02) | 32 | 8 | 32 | 6 | 13 | 3.1 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac8_l36_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac8_l36_k13_s6_b02) | 32 | 8 | 36 | 6 | 13 | 11.55 | FAIL_PER |
| exp4 | [paper_exp4_m32_pac8_l40_k13_s6_b02](/Users/songchieon/Desktop/DWM3000/logs/nlos69_lead_screen_rotation_20260907_2326/paper_exp4_m32_pac8_l40_k13_s6_b02) | 32 | 8 | 40 | 6 | 13 | 15.95 | FAIL_PER |
| exp4 | [paper_exp4_m64_pac4_l27_k12_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m64_pac4_l27_k12_s6_b01) | 64 | 4 | 27 | 6 | 12 | 0 | PASS |
| exp4 | [paper_exp4_m64_pac4_l27_k1_s1_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m64_pac4_l27_k1_s1_b01) | 64 | 4 | 27 | 1 | 1 | 0 | PASS |
| exp4 | [paper_exp4_m64_pac4_l27_k2_s2_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m64_pac4_l27_k2_s2_b01) | 64 | 4 | 27 | 2 | 2 | 0 | PASS |
| exp4 | [paper_exp4_m64_pac4_l27_k6_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m64_pac4_l27_k6_s6_b01) | 64 | 4 | 27 | 6 | 6 | 0 | PASS |
| exp4 | [paper_exp4_m64_pac8_l26_k12_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m64_pac8_l26_k12_s6_b01) | 64 | 8 | 26 | 6 | 12 | 0 | PASS |
| exp4 | [paper_exp4_m64_pac8_l26_k1_s1_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m64_pac8_l26_k1_s1_b01) | 64 | 8 | 26 | 1 | 1 | 0 | PASS |
| exp4 | [paper_exp4_m64_pac8_l26_k6_s6_b01](/Users/songchieon/Desktop/DWM3000/logs/vehicle_onepass_run_20260908_0039/paper_exp4_m64_pac8_l26_k6_s6_b01) | 64 | 8 | 26 | 6 | 6 | 0 | PASS |
| exp5 | [exp5_m1024_pac32_l25](/Users/songchieon/Desktop/DWM3000/logs/vehicle_suite_nlos69_smoke_20260907_2301/exp5_m1024_pac32_l25) | 1024 | 32 | 25 | 1 | 1 | 0 | PASS |
