# BRRS Essential profile — 279 case

이 문서는 다른 profile 문서를 읽지 않아도 Essential 차량 실험을 이해하고 운영할 수
있도록 범위, 보드 역할, 반복·회전, 실행·재개와 통행 오염 처리를 모두 설명한다.

Essential은 논문의 핵심 데이터를 우선 확보하기 위한 profile이다. 프리앰블 PER와
SFD/PHR 비교는 유지하고, 6개 차량 링크의 M32/M256 끝점과 실제 TX 6대의 기준·포화
용량에 집중한다. Exp4는 고정된 물리 배치에서 논리 노드/슬롯 배정을 여섯 방식으로
한 바퀴 회전한다. 범위와 증거 강도는 `full > standard > essential > lite`다.

## 공통 용어

- `case`: PHY, PAC, lead, 활성 TX, 슬롯 수·순서, 논리 배정과 block 번호가 고정된
  한 번의 무선 실행이다. 정확한 HEX/ELF, source·manifest·조건·raw hash도 함께 보존한다.
- `block`: 한 stage의 비교 조건을 각각 한 case씩 실행한 한 바퀴다.
- `round`: Exp1~Exp5의 같은 block 번호를 차례로 수행하고 쉬기 위한 현장 운영 단위다.
- `S`: Exp4에서 실제로 동시에 활성화한 물리 TX 보드 수다.
- `K`: 한 슈퍼프레임 안의 DATA 보고 슬롯 수다. S6/K13은 물리 TX 6대가 13개
  보고 기회를 나눠 쓰는 것이지 TX 13대를 뜻하지 않는다.

Essential Exp4는 S6만 사용한다. Block이 바뀌어도 차량에 고정한 보드 위치·방향·
케이블은 바꾸지 않고 논리 노드와 슬롯 역할만 회전한다. 이는 보드·위치 효과와 슬롯
역할 효과를 분리하기 위한 block 설계다.

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
| 목표 | 모든 물리 노드의 각 run PER < 1%, 시스템 오류 0 |

환경, 거리, 위치, 허브·전원·케이블 구성은 차량용 manifest에 실제 상태로 기록하고
실험 도중 바꾸지 않는다. Lead는 차량 Stage0 결과로 PAC별 선정·동결하기 전까지
Exp1~Exp5에 임의 적용하지 않는다.

## 전체 case 수

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

같은 조건을 필요한 횟수만큼 연속 실행하지 않는다. 각 stage에서 block 1의 모든 조건을
한 번씩 마친 뒤 block 2로 돌아간다. Block마다 조건 시작 위치를 순환시키고 짝수 block은
역순으로 실행해 시간·온도·주변 통행 편향이 한 PHY 조건에 몰리지 않게 한다.

## Stage0 — PAC별 lead 선정

물리 N4→INIT 단일 링크에서 M32/tail0으로 PAC4와 PAC8 각각 lead 0~40 us의
41개 정수점을 측정한다. Lead당 2,000 전송 기회이며 grid는 총 82 case다.
실행기는 시간 편향을 줄이기 위해 다음 순서로 점을 순회한다.

`20, 0, 40, 10, 30, 5, 25, 15, 35, 2, 22, 12, 32, 7, 27, 17, 37, 4, 24, 14, 34, 9, 29, 19, 39, 1, 21, 11, 31, 6, 26, 16, 36, 3, 23, 13, 33, 8, 28, 18, 38`

전체 grid가 유효해야 후보를 고른다. `lead-1`과 `lead+1`이 반드시 통과할 필요는 없다.
연속 통과 구간이 있으면 넓은 구간의 중앙을 우선하고, 고립된 성공점은 독립 confirmation이
불안정하면 채택하지 않는다. 선정 lead는 PAC별 5회, 총 10 case 확인한다. 모든 run의
PER가 1% 미만이고 합산 Wilson 95% 상한도 1% 미만일 때만 새 manifest에 동결한다.

Stage0은 뒤 단계의 설정을 결정하는 캘리브레이션이므로 round마다 다시 실행하지 않는다.
차량 배치나 환경이 바뀌면 새 manifest에서 Stage0을 다시 시작한다. Exp4 S6/K6에서
lead 후보를 재확인하는 sweep은 Essential 279 case에 포함되지 않는 선택 실험이다.

명령은 `Drivers/API`에서 실행한다. 전체 grid bundle을 모아
`brrs_suite_leads.py candidates`로 후보 manifest를 만들고, Essential confirmation
bundle을 모아 `brrs_suite_leads.py freeze --profile essential`로 동결 manifest를 만든다.
누락·수집 실패·PER 기준 미달을 임의 lead로 대체하지 않는다.

## Exp1~Exp3

- Exp1: 물리 N4→INIT 단일 링크에서 M32/64/128/256 × PAC4/8을 비교한다.
  조건당 2,000 전송 기회, block당 8 case, 5 blocks로 총 40 case다.
- Exp2: 각 case에서 N2~N7 중 한 물리 TX만 활성화한다. M32/M256 × PAC4/8 ×
  6링크, 조건당 1,000 전송 기회와 CIR 요약을 기록한다. Block당 24 case,
  3 blocks로 총 72 case다.
- Exp3: 물리 N4→INIT, M32/PAC8에서 A=SFD8+표준 PHR,
  B=SFD16+표준 PHR, C=SFD8+data-rate PHR을 비교한다. Block당 3 case,
  3 blocks로 총 9 case다.

## Exp4 — 실제 TX 6대와 포화 용량

모든 case는 실제 TX 여섯 대가 활성인 S6이며 1,000 슈퍼프레임을 측정한다.

| M | K | PAC4/8 포함 case/block |
|---:|---|---:|
| 32 | 6, 13 | 4 |
| 256 | 6, 8 | 4 |
| **합계** |  | **8** |

K6은 여섯 물리 노드가 한 번씩 전송하는 기준이고 K13/K8은 같은 여섯 보드가 추가
슬롯을 나눠 쓰는 처리 용량 조건이다. Block 1은 원래 설치 매핑, block 2~6은 논리
배정을 한 칸씩 회전한다. 여섯 blocks로 모든 물리 링크가 모든 논리 슬롯 역할을 한 번씩
경험한다. 총 8 × 6 = 48 case다. S1~S5, M64/M128 및 두 번째 회전 주기는 포함하지 않는다.

## Exp5 — 차량 링크별 raw CIR

N2~N7을 한 대씩 활성화해 M1024/PAC32로 측정한다. Lead는 PAC8 Stage0 선정값을
전달하지만 PAC32 최적값을 측정했다는 뜻은 아니다. 링크당 1,000 전송 기회를 주고
성공 프레임 중 최대 30프레임, 프레임당 300 raw CIR samples를 보존한다. Block당
6 case, 3 blocks로 총 18 case다.

## Round별 실행과 중단 지점

Stage0 92 case를 끝내 lead를 동결한 뒤 Exp1~Exp5를 round 단위로 실행한다.

| round | 실행 stage/block | case |
|---:|---|---:|
| 1~3 | Exp1 8 + Exp2 24 + Exp3 3 + Exp4 8 + Exp5 6 | 각 49 |
| 4~5 | Exp1 8 + Exp4 8 | 각 16 |
| 6 | Exp4 8 | 8 |

처음부터 Essential 완성을 목표로 한다면 `--profile essential --blocks 1`로 시작한다.
다음 round는 `--blocks 2`와 새 root를 사용한다. Lite profile의 case ID와 hash는
다르므로 Lite 결과가 Essential block 1로 자동 합산되지는 않는다.

```bash
python3 brrs_suite_campaign.py prepare --manifest vehicle_frozen.json \
  --stage exp4 --profile essential --blocks 1 \
  --root /private/tmp/vehicle_essential_round01_exp4 --dry-run
```

`--dry-run`은 계획만 출력한다. 실제 준비에서는 이를 빼며, 준비는 펌웨어와 immutable
bundle을 만들 뿐 RF를 시작하지 않는다. 차량 RF는 반드시 한 case씩 실행한다.

```bash
python3 brrs_suite_campaign.py run \
  --root /private/tmp/vehicle_essential_round01_exp4 --one-case
```

같은 명령을 다시 호출하면 완료 case를 검증해 건너뛰고 다음 미완료 case 하나만 실행한다.
`--one-case`를 빼면 남은 case가 연속 실행되므로 차량 현장에서는 사용하지 않는다.
한 campaign root의 명세는 생성 후 바꾸지 않으며, 다음 block은 새 root에 준비한다.

## 사람·차량 통행과 재개

- case 사이에 통행이 생기면 다음 `--one-case` 명령을 시작하지 않고 기다린다.
- 약 10초 RF 측정 도중 통행이 생기면 현재 case ID와 시각을 기록한다. 가능하면 수집은
  정상 종료시켜 raw와 제어 증거를 완전하게 남긴다.
- 오염 bundle을 삭제하거나 덮어쓰지 않는다. 이유와 payload hash를 exclusions에 남기고
  동일 case를 새 root/bundle에서 나중에 다시 측정한다.
- 장시간 교란으로 강제 중단하면 그 case 하나만 불완전 실행으로 보존된다. 다음 case와
  Exp4 회전 index는 꼬이지 않는다.
- 완료 case는 재개 시 원문과 hash를 확인한 뒤 건너뛴다. 안전한 중단 시점은 case 직후,
  stage 경계 또는 round 경계다.

## 공통 판정과 profile 밖 실험

수신 0인 실행은 PASS가 아니다. Deadline miss, delayed RX/TX late, RDB mismatch,
incomplete, overrun, SPI 오류, 잘못된 source/slot/superframe 및 수집 timeout은 별도로
검증한다. PHY 손실은 PER에 반영하고 모든 물리 노드의 각 run이 strict PER < 1%여야 한다.
실패와 오염 로그도 보존한다. 일부 block만 끝낸 slice는 Essential 완료로 표시하지 않는다.

완료 bundle은 stage별로 다음처럼 집계하며 Exp4 용량 판정은 전용 도구도 함께 사용한다.

```bash
python3 brrs_suite_results.py vehicle_frozen.json \
  --stage exp4 --profile essential --bundles <완료-bundle들>
python3 brrs_exp4_capacity.py vehicle_frozen.json \
  --profile essential --bundles <완료-bundle들>
```

차량 장착 직후 6링크 점검, 선택적 S6 lead 확인, SB/SP/guard/K 탐색, 비기본 비컨
프리앰블과 오염 case 대체 실행은 279에 포함되지 않는다. 탐색은 고유 로그 경로에서
저수준 capture 명령으로 수행하고 채택한 조건만 새 manifest에 동결한다. 탐색 로그는
진단 자료이며 Essential 공식 통계에 자동 합산하지 않는다.
