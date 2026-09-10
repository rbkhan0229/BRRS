# BRRS Essential profile — 279 case

Essential은 논문의 핵심 주장에 필요한 조건을 반복 검증하는 우선 profile이다.
Full에서 Exp2의 M64/M128, Exp4의 S1~S5와 M64/M128, Exp4 두 번째 회전 주기를
제외한다. 물리 TX 6대의 끝점 신뢰성과 처리 용량 비교에 집중한다.

## 전체 구성

| 구분 | 조건과 반복 | case |
|---|---:|---:|
| Stage0 grid | 41 leads × 2 PAC | 82 |
| Stage0 confirmation | 2 PAC × 선정 lead × 5 blocks | 10 |
| Exp1 | 4 M × 2 PAC × 5 blocks | 40 |
| Exp2 | M32/M256 × 2 PAC × 6 links × 3 blocks | 72 |
| Exp3 | 3 variants × 3 blocks | 9 |
| Exp4 | S6 × M32/M256 × 2 K × 2 PAC × 6 blocks | 48 |
| Exp5 | 6 links × 3 blocks | 18 |
| **합계** |  | **279** |

## 유지하는 조건

- Stage0와 Exp1은 full과 같은 조건 및 반복을 유지한다.
- Exp2는 짧은 M32와 기준 M256 끝점만 모든 6링크에서 측정한다.
- Exp3 A/B/C와 Exp5 6링크는 full과 같은 3 blocks를 유지한다.
- Exp4는 실제 TX 6대만 사용한다. M32는 K6/K13, M256은 K6/K8을 PAC4/8에서
  측정한다. 한 block은 8 case다.
- Exp4 block 1~6에서 물리 장착은 고정하고 논리 노드/슬롯 배정을 한 바퀴 회전한다.
- 별도 S6 lead 후보 sweep은 포함하지 않는다.

## 끊어 실행하는 round

Stage0 92 case와 lead 동결을 먼저 끝낸다.

| round | 실행 stage/block | case |
|---:|---|---:|
| 1~3 | Exp1 8 + Exp2 24 + Exp3 3 + Exp4 8 + Exp5 6 | 각 49 |
| 4~5 | Exp1 8 + Exp4 8 | 각 16 |
| 6 | Exp4 8 | 8 |

각 round 뒤 안전하게 쉴 수 있다. 예를 들어 첫 round의 Exp4는
`--profile essential --blocks 1`로 준비한다. 다음 round는 새 root에서
`--blocks 2`를 사용한다. Essential의 여섯 Exp4 blocks는 완전히 같은 보드 배정의
복제가 아니라 모든 물리 링크가 논리 슬롯 역할을 경험하도록 만든 한 회전 주기다.

## Lite와의 관계

[Lite](BRRS_EXPERIMENT_LITE_KR.md)는 여기 정의한 기본 조건을 block 1에서 한 번씩만
실행한다. 다만 profile명이 hash와 case ID에 들어가므로 lite 결과를 essential block 1로
자동 합산하지 않는다. Essential 완성을 목표로 한다면 처음부터
`--profile essential --blocks 1`로 시작한다.
