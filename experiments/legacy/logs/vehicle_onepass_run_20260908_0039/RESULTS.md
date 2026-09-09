> 2026-09-08 후처리 완료: [종합 보고서](postprocess_20260908/REPORT.md), [전력 지표의 새 문제](postprocess_20260908/POWER_DIAGNOSTICS.md). TX6의 PER 판정은 유지된다. M32 RSSI 하한값과 FP power의 RX code 추출 버그를 발견해 품질 플래그·별도 보정 추정값을 제공했다. 원본·펌웨어·보드는 변경하지 않았다. 아래는 수집 직후 인계 기록이다.

# 차량 전 각 조건1회 기능 점검 · 2026-09-08

**선택 범위 완료: 새 조건40/40. TX6 신규16조건 모두 각 노드 PER<1%, 최악0.1%.** 실제 환경은 사용자 설치 NLOS6.9m이며 차량 측정이 아니다. 기존 완료 조건은 기능 실행 증거로만 재사용하며, 다른 lead·회전·시각·전선 가림 전후 PER를 합산하지 않는다.

TX6 신규16조건에서 총132,000개 중131,999개를 수신했다. 유일한 손실은 M32/PAC8/6슬롯의 N5 1개이며 PHR 오류1회를 기록했다. 13슬롯은 이전 별도 실행 증거를 재사용했으며 이번16조건에는 포함하지 않았다. 이전 PAC4/13슬롯 실패를 해결된 것으로 바꾸지 않는다.

PAC8 lead26µs, PAC4 lead27µs를 이번 준비의 고정값으로 사용했다. PAC4의27µs는 안정적인 PER<1% 선정값이 아니다. 각각1회 실행하고 유효한 PER 실패는 반복하지 않았다. 원본 Git과 펌웨어 C를 바꾸지 않으며 commit/push는 하지 않는다.

RX1050270933, 단일 링크 및 Exp4 S1 TX는 물리N4/1050282818. Exp4 S2~S6은 block1 설치표 매핑이다. TX허브 외부 전원은 직전 사용자 확인을 기록했고 이번 측정 중 물리 배치·배선·전원을 변경하지 않았다. SSH·USB 구성·원본 Git 상태는 preflight_local/remote.json에 있다.

| 단계 | 이번 대상 | 완료 | 노드별 PER<1% | PER 실패 |
|---|---:|---:|---:|---:|
| exp1 | 6 | 6 | 6 | 0 |
| exp2 | 7 | 7 | 7 | 0 |
| exp4 | 27 | 27 | 27 | 0 |

Exp1/2 CSV·그림 후처리 완료: 13/13조건. Stage0·Exp3 A/B/C·Exp5는 앞선 대표 실행과 후처리 증거를 재사용한다.

## 이번 실행별 물리 노드 PER (%)

| # | 조건 | N2 | N3 | N4 | N5 | N6 | N7 | 판정 |
|---:|---|---:|---:|---:|---:|---:|---:|---|
| 1 | [exp1 M32/P4/L27](paper_exp1_m32_pac4_l27_b01/results/ASSESSMENT.json) | — | — | 0.050 | — | — | — | PASS |
| 2 | [exp1 M64/P4/L27](paper_exp1_m64_pac4_l27_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 3 | [exp1 M128/P4/L27](paper_exp1_m128_pac4_l27_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 4 | [exp1 M256/P4/L27](paper_exp1_m256_pac4_l27_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 5 | [exp1 M128/P8/L26](paper_exp1_m128_pac8_l26_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 6 | [exp1 M256/P8/L26](paper_exp1_m256_pac8_l26_b01/results/ASSESSMENT.json) | — | — | 0.050 | — | — | — | PASS |
| 7 | [exp2 M32/P4/L27](paper_exp2_m32_pac4_l27_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 8 | [exp2 M64/P4/L27](paper_exp2_m64_pac4_l27_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 9 | [exp2 M128/P4/L27](paper_exp2_m128_pac4_l27_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 10 | [exp2 M256/P4/L27](paper_exp2_m256_pac4_l27_b01/results/ASSESSMENT.json) | — | — | 0.100 | — | — | — | PASS |
| 11 | [exp2 M64/P8/L26](paper_exp2_m64_pac8_l26_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 12 | [exp2 M128/P8/L26](paper_exp2_m128_pac8_l26_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 13 | [exp2 M256/P8/L26](paper_exp2_m256_pac8_l26_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 14 | [exp4 M32/P4/L27/S1/K1](paper_exp4_m32_pac4_l27_k1_s1_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 15 | [exp4 M64/P4/L27/S1/K1](paper_exp4_m64_pac4_l27_k1_s1_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 16 | [exp4 M128/P4/L27/S1/K1](paper_exp4_m128_pac4_l27_k1_s1_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 17 | [exp4 M256/P4/L27/S1/K1](paper_exp4_m256_pac4_l27_k1_s1_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 18 | [exp4 M32/P8/L26/S1/K1](paper_exp4_m32_pac8_l26_k1_s1_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 19 | [exp4 M64/P8/L26/S1/K1](paper_exp4_m64_pac8_l26_k1_s1_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 20 | [exp4 M128/P8/L26/S1/K1](paper_exp4_m128_pac8_l26_k1_s1_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 21 | [exp4 M256/P8/L26/S1/K1](paper_exp4_m256_pac8_l26_k1_s1_b01/results/ASSESSMENT.json) | — | — | 0.000 | — | — | — | PASS |
| 22 | [exp4 M32/P4/L27/S2/K2](paper_exp4_m32_pac4_l27_k2_s2_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | — | — | — | — | PASS |
| 23 | [exp4 M64/P4/L27/S2/K2](paper_exp4_m64_pac4_l27_k2_s2_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | — | — | — | — | PASS |
| 24 | [exp4 M128/P4/L27/S2/K2](paper_exp4_m128_pac4_l27_k2_s2_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | — | — | — | — | PASS |
| 25 | [exp4 M32/P4/L27/S6/K6](paper_exp4_m32_pac4_l27_k6_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 26 | [exp4 M32/P4/L27/S6/K12](paper_exp4_m32_pac4_l27_k12_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 27 | [exp4 M64/P4/L27/S6/K6](paper_exp4_m64_pac4_l27_k6_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 28 | [exp4 M64/P4/L27/S6/K12](paper_exp4_m64_pac4_l27_k12_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 29 | [exp4 M128/P4/L27/S6/K6](paper_exp4_m128_pac4_l27_k6_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 30 | [exp4 M128/P4/L27/S6/K10](paper_exp4_m128_pac4_l27_k10_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 31 | [exp4 M256/P4/L27/S6/K6](paper_exp4_m256_pac4_l27_k6_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 32 | [exp4 M256/P4/L27/S6/K8](paper_exp4_m256_pac4_l27_k8_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 33 | [exp4 M32/P8/L26/S6/K6](paper_exp4_m32_pac8_l26_k6_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.100 | 0.000 | 0.000 | PASS |
| 34 | [exp4 M32/P8/L26/S6/K12](paper_exp4_m32_pac8_l26_k12_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 35 | [exp4 M64/P8/L26/S6/K6](paper_exp4_m64_pac8_l26_k6_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 36 | [exp4 M64/P8/L26/S6/K12](paper_exp4_m64_pac8_l26_k12_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 37 | [exp4 M128/P8/L26/S6/K6](paper_exp4_m128_pac8_l26_k6_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 38 | [exp4 M128/P8/L26/S6/K10](paper_exp4_m128_pac8_l26_k10_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 39 | [exp4 M256/P8/L26/S6/K6](paper_exp4_m256_pac8_l26_k6_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |
| 40 | [exp4 M256/P8/L26/S6/K8](paper_exp4_m256_pac8_l26_k8_s6_b01/results/ASSESSMENT.json) | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | PASS |

## 판정·원문과 차량 인계

추가 CSV·그림·심층 분석은 사용자 요청에 따라 나중으로 미뤘다. Exp1/2의13조건 후처리는 요청 전에 이미 완료했다. 수집·역할·HEX readback·READY/END 성공과 노드별 PER<1%를 구분한다. 미참여 노드는 표에서 —로 표시한다. offered/RX/TX/beacon/late, 오류 카운터, 원문 hash는 observations.json 및 각 bundle/results에 보존한다. 해당 단계가 출력하지 않는 카운터를0으로 가정하지 않는다. Exp2 CIR는 성공 패킷 표본이며 PER와 유효 CIR행 수를 함께 본다.

사용자 요청에 따라 남은 S1~S5 조건을 생략하고 TX6의 미확인16조건만 완료 대상으로 줄였다. 전체58개 Exp4 조합을 이번에 모두 실행한 것은 아니다. 조건1회 점검은 논문 반복·전 역할 회전·차량 성능 검증 또는 안정적인 최대 용량 확정이 아니다. 오늘의 PER 실패 때문에 추가 lead 최적화나 동일 RF를 반복하지 않는다.

[차량 아침 실행 안내](VEHICLE_MORNING.md) · [차량 환경 기록 틀](vehicle_manifest_TEMPLATE.json) · [최종 선택 계획](ACTIVE_PLAN.json) · [HEX/일련번호 목록](image_inventory.json) · [재사용 증거와 남았던 목록](../vehicle_onepass_remaining_20260908/REMAINING.json)

종료 확인: 로컬·원격 원본 Git과 probe 집합 동일, 잔여 캡처 없음. 7대 모두 원래 역할의 실험 전 M32/PAC8/lead25µs/S6/13슬롯 HEX로 복원·readback 검증 후 정지. 복원 뒤 추가 RF 없음. 펌웨어 C 변경 및 commit/push 없음.

[실행기 수정·생략 범위·Exp5 재사용 기록](RUN_NOTES.md)
