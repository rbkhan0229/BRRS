# BRRS Standard profile — 305 case

이 문서는 다른 profile 문서를 읽지 않아도 Standard 차량 실험을 이해하고 운영할 수
있도록 범위, 보드 역할, 반복·회전, 실행·재개와 통행 오염 처리를 설명한다.

Standard는 실제 차량 논문의 기본 profile이다. Full의 999 case보다 시간을 줄이되,
Essential에 없는 S1~S5 노드 수 그래프와 여섯 물리 링크의 반복 근거를 유지한다.
별도 Exp1은 실행하지 않는다. 같은 최종 TDMA 송수신 경로를 사용하는 Exp4 S1에서
M32/PAC4·PAC8 및 M64/128/256/PAC8을 측정하고, block마다 송신 보드를 바꾸어 프리앰블 비교와
차량 위치 차이를 함께 검증한다. 전체 범위는 `full > standard > essential > lite`다.

## 공통 용어

- `case`: PHY, PAC, lead, 활성 TX, 슬롯 수·순서, 논리 배정과 block 번호가 고정된
  한 번의 무선 실행이다. 정확한 HEX/ELF, source·manifest·조건·raw hash도 보존한다.
- `block`: 한 stage의 비교 조건을 각각 한 case씩 실행한 한 바퀴다.
- `round`: 여러 stage의 같은 block 번호를 차례로 수행하고 쉬기 위한 현장 운영 단위다.
- `S`: Exp4에서 실제로 동시에 활성화한 물리 TX 보드 수다.
- `K`: 한 슈퍼프레임 안의 DATA 보고 슬롯 수다. S6/K13은 TX 6대가 13개 보고
  기회를 나눠 쓰는 것이지 TX 13대를 뜻하지 않는다.

Block이 바뀌어도 차량에 고정한 보드 위치·방향·케이블은 바꾸지 않는다. 물리 보드와
논리 노드/슬롯 배정만 계획대로 회전한다. S1도 block 1~6에서 N2~N7을 한 번씩 사용한다.

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

Stage0과 Exp3의 단일 TX는 물리 N4를 사용한다. Exp2와 Exp5는 N2~N7을 한 대씩
활성화하고 나머지 다섯 TX가 정지했는지 검증한다. Exp4 S1은 block 1=N2, 2=N3,
3=N4, 4=N5, 5=N6, 6=N7 순으로 물리 TX를 바꾼다. 실제 링크는 case ID,
`physical_role`, serial과 위치 metadata로 구분한다.

## 공통 무선·펌웨어 조건

| 항목 | 현재 manifest 기준 |
|---|---|
| 슈퍼프레임 | 10,000 us |
| 비컨 프리앰블 | M512 |
| DATA payload | application 16 B, 전체 PSDU 26 B |
| DATA 프리앰블 | S1 M32/64/128/256, S2~S6 M32/M256 |
| DATA PAC | M32는 PAC4/PAC8, M64 이상은 PAC8 |
| Exp4 guard | 250 us |
| Exp4 SB/SP | 3000/2500 us |
| Exp4 수신 | 슬롯별 scheduled delayed-RX |
| Exp4 SPI | persistent-SPIM 최적화 ON |
| 목표 | 모든 물리 노드의 각 run PER < 1%, 시스템 오류 0 |

환경, 거리, 위치, 유전원 허브·어댑터·포트·케이블과 차량 상태를 차량용 manifest에
기록하고 실험 도중 바꾸지 않는다. Lead는 차량 Stage0 결과로 PAC별 선정·동결하기
전까지 Exp2~Exp5에 임의 적용하지 않는다.

## 전체 case 수

| 구분 | 조건과 반복 | case |
|---|---:|---:|
| Stage0 grid | 41 leads × 2 PAC | 82 |
| Stage0 confirmation | 2 PAC × 선정 lead × 5 blocks | 10 |
| Exp1 | Exp4 S1에 흡수 | 0 |
| Exp2 | (M32 × PAC4/8 + M256 × PAC8) × 6 links × 3 blocks | 54 |
| Exp3 | 3 variants × 1 block | 3 |
| Exp4 | 23조건 × 6 blocks | 138 |
| Exp5 | 6 links × 3 blocks | 18 |
| **합계** |  | **305** |

한 조건만 여러 번 연속 실행하지 않는다. 각 stage에서 block 1의 모든 조건을 마친 뒤
block 2로 돌아가며, block마다 조건 시작 위치를 순환시키고 짝수 block은 역순으로
실행한다. 정상 결과라면 다음 case로 계속 진행한다. 비정상 PER, 시스템 오류 또는
사용자가 통행·환경 변화를 알린 경우에만 현재 case 뒤에서 멈추고 확인한다.

## Stage0 — PAC별 lead 선정

물리 N4→INIT 단일 링크에서 M32/tail0으로 PAC4와 PAC8 각각 lead 0~40 us의
41개 정수점을 측정한다. Lead당 2,000 전송 기회이며 grid는 총 82 case다.
전체 grid가 유효해야 후보를 고른다. `lead-1`과 `lead+1`이 반드시 통과할 필요는 없다.
선정 lead 자체를 PAC별 5회 확인하고, 모든 run의 PER가 1% 미만이며 합산 Wilson 95%
상한도 1% 미만일 때만 manifest에 동결한다.

Stage0은 뒤 단계의 설정을 결정하므로 round마다 반복하지 않는다. 차량 배치나 전원·
안테나 환경이 바뀌면 새 manifest에서 다시 수행한다.

## Exp1 — 별도 실행하지 않음

Standard에서는 Exp1을 0회로 명시적으로 비활성화한다. Exp1과 Exp4 S1은 프리앰블
비교 목적이 겹치지만, Exp4 S1은 논문에서 실제 사용하는 비컨 동기화·scheduled RX·
TDMA 데이터 경로를 그대로 사용한다. 따라서 Standard는 Exp4 S1을 본 근거로 삼는다.

다만 두 결과가 완전히 같은 실험이라는 뜻은 아니다. Exp1은 단일 링크 PHY 회귀 시험이고
Exp4 S1은 최종 시스템 경로 시험이다. 펌웨어 디버깅이나 과거 데이터와의 직접 비교가
필요하면 profile 밖 진단으로 Exp1을 실행하되 Standard 377 case에 합산하지 않는다.

## Exp2 — 여섯 링크의 CIR 요약

각 case에서 N2~N7 중 한 물리 TX만 활성화한다. M32는 PAC4/PAC8, M256은 PAC8로
여섯 링크를 측정하며 조건당 1,000 전송 기회와 CIR 요약을 기록한다. Block당 18 case,
3 blocks로 총 54 case다. 짧은 프리앰블과 M256 기준의 링크별 손실·채널 차이를 반복 검증한다.

## Exp3 — 차량 보조 확인

물리 N4→INIT, M32/PAC8에서 A=SFD8+표준 PHR, B=SFD16+표준 PHR,
C=SFD8+data-rate PHR을 각 한 번 측정한다. 총 3 case다. 주된 airtime/기능 근거는
연구실 측정을 사용하고, 차량에서는 세 PHY 변형이 실제 채널에서도 동작하는지만 확인한다.

## Exp4 — 노드 수 그래프와 포화 용량

모든 case는 1,000 슈퍼프레임이다. S1~S5에서는 K=S이고, S6에서는 K6 기준과
M별 포화 후보를 비교한다.

| 활성 TX | M과 K | PAC4/8 포함 case/block |
|---:|---|---:|
| S1 | M32/PAC4·8 + M64/128/256/PAC8, K1 | 5 |
| S2 | M32/PAC4·8 + M256/PAC8, K2 | 3 |
| S3 | M32/PAC4·8 + M256/PAC8, K3 | 3 |
| S4 | M32/PAC4·8 + M256/PAC8, K4 | 3 |
| S5 | M32/PAC4·8 + M256/PAC8, K5 | 3 |
| S6 | M32/PAC4·8 K6/K13 + M256/PAC8 K6/K8 | 6 |
| **합계** |  | **23** |

PAC8로 고정한 M32~M256은 순수 프리앰블 길이 비교 곡선을 만들고, M32/PAC4는
제조사 권장 짧은 프리앰블 조합과 PAC8을 직접 비교한다. M64 이상에는 PAC4를 적용하지
않는다. S2~S6은 M32와 M256 끝점만 사용해
노드 수에 따른 이용률·PER 변화를 보여준다. S6/K13과 S6/K8은 각 PHY의 포화 처리량을
검증한다. Block 1~6에서 설치표를 한 칸씩 순환하므로 S1은 모든 물리 링크를 한 번씩,
S2~S6은 가능한 논리 슬롯 역할을 한 바퀴 경험한다. 총 23 × 6 = 138 case다.

## Exp5 — 차량 링크별 raw CIR

N2~N7을 한 대씩 활성화해 M1024/PAC32로 측정한다. Lead는 PAC8 Stage0 선정값을
전달하지만 PAC32 최적값을 측정했다는 뜻은 아니다. 링크당 1,000 전송 기회를 주고
성공 프레임 중 최대 30프레임, 프레임당 300 raw CIR samples를 보존한다. Block당
6 case, 3 blocks로 총 18 case다.

## Round별 실행과 예상 시간

Stage0 92 case를 끝내 lead를 동결한 뒤 다음 순서로 진행한다.

| round | 실행 stage/block | case |
|---:|---|---:|
| 1 | Exp2 18 + Exp3 3 + Exp4 23 + Exp5 6 | 50 |
| 2~3 | Exp2 18 + Exp4 23 + Exp5 6 | 각 47 |
| 4~6 | Exp4 23 | 각 23 |

Case당 준비·flash·READY·약 10초 RF·수집 검증을 합쳐 평균 1분 정도면 약 5시간,
현장 중단과 재측정을 포함하면 **약 6~7시간**을 예상한다. 시간은 장비·빌드 캐시 상태에
따라 달라진다. 한 번에 끝낼 필요는 없으며 case, stage 또는 round 경계에서 중단한다.

## 계획·실행·재개

명령은 `Drivers/API`에서 실행한다. 아래는 Exp4 block 1 계획 확인 예다.

```bash
python3 brrs_suite_campaign.py prepare --manifest vehicle_frozen.json \
  --stage exp4 --profile standard --blocks 1 \
  --root /private/tmp/vehicle_standard_round01_exp4 --dry-run
```

실제 준비에서는 `--dry-run`을 빼며, 준비는 immutable bundle과 펌웨어 이미지를 만들 뿐
RF를 시작하지 않는다. 차량 RF는 한 case씩 실행한다.

```bash
python3 brrs_suite_campaign.py run \
  --root /private/tmp/vehicle_standard_round01_exp4 --one-case
```

같은 명령을 다시 호출하면 완료 case를 검증해 건너뛰고 다음 미완료 case 하나만 실행한다.
한 campaign root의 명세는 생성 후 바꾸지 않으며 다음 block은 새 root를 사용한다.

## 통행·이상 결과와 공통 판정

- case 사이에 사람이나 차량이 지나가면 다음 case를 시작하지 않는다.
- RF 도중 교란이 생기면 case ID와 시각을 기록하고 가능하면 수집을 정상 종료한다.
- 오염 bundle은 삭제·덮어쓰기하지 않고 exclusions에 이유와 payload hash를 남긴다.
- 정상 PER 실패는 오염으로 임의 제외하지 않는다.
- 수신 0인 실행은 PASS가 아니다.
- deadline miss, delayed RX/TX late, RDB mismatch/incomplete, overrun, SPI 오류,
  잘못된 source/slot/superframe와 수집 timeout은 0이어야 한다.
- PHY 손실은 PER에 반영하며 모든 물리 노드의 각 run이 strict PER < 1%여야 한다.

완료 bundle은 다음처럼 집계한다.

```bash
python3 brrs_suite_results.py vehicle_frozen.json \
  --stage exp4 --profile standard --bundles <완료-bundle들>
python3 brrs_exp4_capacity.py vehicle_frozen.json \
  --profile standard --bundles <완료-bundle들>
```

차량 장착 직후 6링크 점검, SB/SP/guard/K 탐색, 비기본 비컨 프리앰블과 오염 case
대체 실행은 305 case에 포함되지 않는다. 탐색은 고유 로그 경로에서 수행하고 채택한
조건만 새 manifest에 동결한다. 이 로그는 진단 자료이며 Standard 통계에 자동 합산하지 않는다.
