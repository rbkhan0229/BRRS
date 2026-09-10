# BRRS Lite profile — 133 case

이 문서는 다른 profile 문서를 읽지 않아도 Lite 차량 실험을 이해하고 운영할 수 있도록
범위, 보드 역할, 실행·재개와 통행 오염 처리를 모두 설명한다.

Lite는 논문의 핵심 조건을 각각 한 block만 실행하는 현장 점검 profile이다. 장비 연결,
PHY 설정, Stage0~Exp5 실행 경로, 로그와 분석기가 전체 단계에서 정상인지 빠르게 확인한다.
조건 범위는 Essential의 첫 block과 같지만 반복과 Exp4 논리 역할 회전은 없다. 범위와
증거 강도는 `full > essential > lite`다.

## 공통 용어

- `case`: PHY, PAC, lead, 활성 TX, 슬롯 수·순서와 논리 배정이 고정된 한 번의
  무선 실행이다. 정확한 HEX/ELF, source·manifest·조건·raw hash도 함께 보존한다.
- `block`: 한 stage의 비교 조건을 각각 한 case씩 실행한 한 바퀴다. Lite는 모든
  stage에서 block 1만 사용한다.
- `S`: Exp4에서 실제로 동시에 활성화한 물리 TX 보드 수다.
- `K`: 한 슈퍼프레임 안의 DATA 보고 슬롯 수다. S6/K13은 물리 TX 6대가 13개
  보고 기회를 나눠 쓰는 것이지 TX 13대를 뜻하지 않는다.

Lite Exp4는 원래 설치 매핑을 그대로 사용한다. 물리 보드 위치·방향·케이블을 바꾸지
않으며 논리 배정도 회전하지 않는다. 따라서 한 번의 전체 경로 점검에는 적합하지만
시간 변화나 슬롯 역할에 대한 반복 근거는 제공하지 않는다.

## 보드 역할과 물리 위치

| 역할 | J-Link serial | 계획 위치 |
|---|---|---|
| INIT/RX | 1050270933 | frunk |
| N2 | 1050211584 | bumper_A |
| N3 | 1050273888 | bumper_B |
| N4 | 1050282818 | driver_seat |
| N5 | 1050208509 | passenger_seat |
| N6 | 1050227627 | trunk_A |
| N7 | 1050204212 | trunk_B |

Stage0, Exp1과 Exp3의 단일 TX는 물리 N4를 사용한다. Exp2와 Exp5는 N2~N7을
한 대씩 활성화하고 나머지 다섯 TX가 정지했는지 검증한다. 단일 링크 펌웨어의 무선
source는 논리 N2이므로 실제 링크는 case ID, `physical_role`, serial과 위치 metadata로
구분한다. Exp4는 여섯 TX를 모두 활성화한다.

## 공통 무선·펌웨어 조건

| 항목 | 현재 manifest 기준 |
|---|---|
| 슈퍼프레임 | 10,000 us |
| 비컨 프리앰블 | 공식 profile M512; 직접 탐색은 M32/64/128/256/512/1024 지원 |
| DATA payload | application 16 B, 전체 PSDU 26 B |
| DATA 프리앰블 | Exp1 M32/64/128/256, Exp2·4 M32/M256 |
| DATA PAC | PAC4, PAC8 |
| Exp4 guard | 250 us |
| Exp4 SB/SP | 3000/2500 us |
| Exp4 수신 | 슬롯별 scheduled delayed-RX |
| Exp4 SPI | persistent-SPIM 최적화 ON |
| 목표 | 모든 물리 노드의 run PER < 1%, 시스템 오류 0 |

환경, 거리, 위치, 허브·전원·케이블 구성은 차량용 manifest에 실제 상태로 기록하고
실험 도중 바꾸지 않는다. Lead는 차량 Stage0 결과로 PAC별 선정·동결하기 전까지
Exp1~Exp5에 임의 적용하지 않는다.

## 전체 case 수

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

Lite는 반복 block이 없지만 한 stage의 모든 조건을 manifest 순서로 한 번씩 끝낸 뒤
다음 stage로 이동한다. 특정 조건만 즉시 반복해 좋은 결과를 고르지 않는다.

## Stage0 — PAC별 lead 선정

물리 N4→INIT 단일 링크에서 M32/tail0으로 PAC4와 PAC8 각각 lead 0~40 us의
41개 정수점을 측정한다. Lead당 2,000 전송 기회이며 grid는 총 82 case다.
실행기는 시간 편향을 줄이기 위해 다음 순서로 점을 순회한다.

`20, 0, 40, 10, 30, 5, 25, 15, 35, 2, 22, 12, 32, 7, 27, 17, 37, 4, 24, 14, 34, 9, 29, 19, 39, 1, 21, 11, 31, 6, 26, 16, 36, 3, 23, 13, 33, 8, 28, 18, 38`

전체 grid가 유효해야 후보를 고른다. `lead-1`과 `lead+1`이 반드시 통과할 필요는 없다.
연속 통과 구간이 있으면 넓은 구간의 중앙을 우선하고 고립된 성공점은 신중하게 해석한다.
Lite에서는 선정 lead를 PAC별 1회, 총 2 case 확인한다. 각 run의 PER가 1% 미만이고
합산 Wilson 95% 상한도 1% 미만일 때 Lite 범위의 manifest에 동결한다. 한 번의
confirmation은 Essential/Full의 PAC별 5회 확인과 동등한 반복 근거가 아니다.

Stage0은 뒤 단계의 설정을 결정하는 캘리브레이션이므로 Exp1~Exp5 사이에 다시 실행하지
않는다. 차량 배치나 환경이 바뀌면 새 manifest에서 Stage0을 다시 시작한다. Exp4 S6/K6
lead 후보 sweep은 Lite 133 case에 포함되지 않는 선택 실험이다.

명령은 `Drivers/API`에서 실행한다. 전체 grid bundle을 모아
`brrs_suite_leads.py candidates`로 후보 manifest를 만들고, Lite confirmation bundle을
모아 `brrs_suite_leads.py freeze --profile lite`로 동결 manifest를 만든다. 누락·수집
실패·PER 기준 미달을 임의 lead로 대체하지 않는다.

## Exp1~Exp3

- Exp1: 물리 N4→INIT 단일 링크에서 M32/64/128/256 × PAC4/8을 비교한다.
  조건당 2,000 전송 기회이며 총 8 case다.
- Exp2: 각 case에서 N2~N7 중 한 물리 TX만 활성화한다. M32/M256 × PAC4/8 ×
  6링크, 조건당 1,000 전송 기회와 CIR 요약을 기록하며 총 24 case다.
- Exp3: 물리 N4→INIT, M32/PAC8에서 A=SFD8+표준 PHR,
  B=SFD16+표준 PHR, C=SFD8+data-rate PHR을 비교하며 총 3 case다.

## Exp4 — 실제 TX 6대와 포화 용량

모든 case는 실제 TX 여섯 대가 활성인 S6이며 1,000 슈퍼프레임을 측정한다.

| M | K | PAC4/8 포함 case |
|---:|---|---:|
| 32 | 6, 13 | 4 |
| 256 | 6, 8 | 4 |
| **합계** |  | **8** |

K6은 여섯 물리 노드가 한 번씩 전송하는 기준이고 K13/K8은 같은 여섯 보드가 추가
슬롯을 나눠 쓰는 처리 용량 조건이다. 원래 설치 매핑의 block 1만 실행한다. S1~S5,
M64/M128, 논리 역할 회전과 반복은 포함하지 않는다.

## Exp5 — 차량 링크별 raw CIR

N2~N7을 한 대씩 활성화해 M1024/PAC32로 측정한다. Lead는 PAC8 Stage0 선정값을
전달하지만 PAC32 최적값을 측정했다는 뜻은 아니다. 링크당 1,000 전송 기회를 주고
성공 프레임 중 최대 30프레임, 프레임당 300 raw CIR samples를 보존한다. 총 6 case다.

## 실행 순서와 중단 지점

1. Stage0 grid 82 case를 완료한다.
2. PAC별 후보를 고르고 각 1회 confirmation하여 lead를 동결한다.
3. Exp1 8 → Exp2 24 → Exp3 3 → Exp4 8 → Exp5 6 case를 실행한다.

Stage0 이후 Exp1~Exp5 한 바퀴는 49 case이며 여기까지 끝나면 Lite가 완료된다.
전체 bundle을 먼저 준비해도 RF는 한 case씩 실행한다.

```bash
python3 brrs_suite_campaign.py prepare --manifest vehicle_frozen.json \
  --stage exp4 --profile lite --blocks 1 \
  --root /private/tmp/vehicle_lite_exp4 --dry-run
```

`--dry-run`은 계획만 출력한다. 실제 준비에서는 이를 빼며, 준비는 펌웨어와 immutable
bundle을 만들 뿐 RF를 시작하지 않는다. 차량 RF는 다음처럼 실행한다.

```bash
python3 brrs_suite_campaign.py run \
  --root /private/tmp/vehicle_lite_exp4 --one-case
```

같은 명령을 다시 호출하면 완료 case를 검증해 건너뛰고 다음 미완료 case 하나만 실행한다.
`--one-case`를 빼면 남은 case가 연속 실행되므로 차량 현장에서는 사용하지 않는다.
한 campaign root의 명세는 생성 후 바꾸지 않는다.

## 사람·차량 통행과 재개

- case 사이에 통행이 생기면 다음 `--one-case` 명령을 시작하지 않고 기다린다.
- 약 10초 RF 측정 도중 통행이 생기면 현재 case ID와 시각을 기록한다. 가능하면 수집은
  정상 종료시켜 raw와 제어 증거를 완전하게 남긴다.
- 오염 bundle을 삭제하거나 덮어쓰지 않는다. 이유와 payload hash를 exclusions에 남기고
  동일 case를 새 root/bundle에서 나중에 다시 측정한다.
- 장시간 교란으로 강제 중단하면 그 case 하나만 불완전 실행으로 보존되며 다음 case는
  꼬이지 않는다.
- 완료 case는 재개 시 원문과 hash를 확인한 뒤 건너뛴다. 안전한 중단 시점은 case 직후나
  stage 경계다.

## 공통 판정, 증거 범위와 확장

수신 0인 실행은 PASS가 아니다. Deadline miss, delayed RX/TX late, RDB mismatch,
incomplete, overrun, SPI 오류, 잘못된 source/slot/superframe 및 수집 timeout은 별도로
검증한다. PHY 손실은 PER에 반영하고 모든 물리 노드의 run이 strict PER < 1%여야 한다.
실패와 오염 로그도 보존한다.

Lite 통과는 핵심 경로를 한 번 확인했다는 뜻이지 반복 신뢰도나 모든 논리 슬롯 역할을
검증했다는 뜻은 아니다. 이후 Essential/Full까지 갈 계획이면 처음부터 목표 profile의
`--blocks 1`로 시작해야 한다. Profile명이 조건 hash와 case ID에 들어가므로 Lite bundle은
다른 profile의 첫 block으로 자동 합산되지 않는다.

완료 bundle은 stage별로 다음처럼 집계하며 Exp4 용량 판정은 전용 도구도 함께 사용한다.

```bash
python3 brrs_suite_results.py vehicle_frozen.json \
  --stage exp4 --profile lite --bundles <완료-bundle들>
python3 brrs_exp4_capacity.py vehicle_frozen.json \
  --profile lite --bundles <완료-bundle들>
```

차량 장착 직후 6링크 점검, 선택적 S6 lead 확인, SB/SP/guard/K 탐색, 비기본 비컨
프리앰블과 오염 case 대체 실행은 133에 포함되지 않는다. 탐색은 고유 로그 경로에서
저수준 capture 명령으로 수행하고 채택한 조건만 새 manifest에 동결한다. 탐색 로그는
진단 자료이며 Lite 공식 통계에 자동 합산하지 않는다.
