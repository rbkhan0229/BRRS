# BRRS Lite profile — 133 case

Lite는 essential과 같은 기본 조건을 각각 한 block만 실행하는 현장 점검 profile이다.
실험 경로, 보드 연결, PHY 설정, 로그와 분석기가 전체 단계에서 정상인지 빠르게 확인한다.

## 전체 구성

| 구분 | 조건 | case |
|---|---:|---:|
| Stage0 grid | 41 leads × 2 PAC | 82 |
| Stage0 confirmation | 2 PAC × 선정 lead × 1 block | 2 |
| Exp1 | 4 M × 2 PAC × 1 block | 8 |
| Exp2 | M32/M256 × 2 PAC × 6 links × 1 block | 24 |
| Exp3 | 3 variants × 1 block | 3 |
| Exp4 | S6 × M32/M256 × 2 K × 2 PAC × 1 block | 8 |
| Exp5 | 6 links × 1 block | 6 |
| **합계** |  | **133** |

## Essential과 같은 조건

- Exp1은 M32/64/128/256 × PAC4/8이다.
- Exp2는 M32/M256 × PAC4/8 × 물리 6링크다.
- Exp3은 A/B/C다.
- Exp4는 S6에서 M32 K6/K13과 M256 K6/K8을 PAC4/8로 측정한다.
- Exp5는 M1024/PAC32의 물리 6링크다.

차이는 반복뿐이다. Lite는 원래 설치표의 논리 배정을 사용하고 회전하지 않는다.
따라서 essential의 **block 1과 같은 기본 조건 범위**지만 시간 변화와 논리 슬롯 역할에
대한 반복 근거는 제공하지 않는다.

## 실행 순서

1. Stage0 grid 82 case를 실행한다.
2. PAC별 후보를 고르고 각 1회 confirmation한다.
3. lead를 lite 범위로 동결한다.
4. Exp1 8 → Exp2 24 → Exp3 3 → Exp4 8 → Exp5 6 case를 실행한다.

Stage0 이후 Exp1~Exp5 한 바퀴는 49 case다. 이 지점에서 lite가 완료된다.
사람 통행이나 수집 오류가 없는 한 같은 조건을 즉시 반복해 좋은 결과만 고르지 않는다.

## 증거 범위와 확장

Lite 통과는 “모든 핵심 경로를 한 번 확인했다”는 뜻이다. 반복 신뢰도나 모든 논리 슬롯
역할을 검증했다는 뜻은 아니다. 이후 essential/full까지 갈 계획이라면 lite로 별도 시작하기
보다 처음부터 해당 profile의 `--blocks 1`을 실행하는 편이 낫다. Profile명이 다른 bundle은
서로 자동 합산되지 않는다.
