2026-09-13 N6 보드 교체: 현재 J-Link serial은 `1050257038` (이전 `1050227627`). 이전 실행의 manifest·원본·평가는 변경하지 않는다. 위치 표는 Standard 배치 기준이며, 새 차량의 실제 장착 위치 확인과 별개다.

## 2026-09-12 야간 실행 승인 및 오류 복구 정책

사용자가 Standard 시작과 자율 오류 복구·동일 조건 재시도·반복 실패 분리 후 나머지 진행을 승인했다. 아래 과거 중단/재시도 금지 설명보다 이번 실행에는 이 정책을 우선한다.

- 실행 root: `/home/chieon/DWM3000/logs/vehicle_standard_ev6_overnight_20260912_022601_a03aea`. 기본 계획은 489개이며 재시도는 추가 attempt로 별도 집계한다. SB1700/SP2200/guard200 µs, fast PHY ON/PGF 유지, S6 최대 부하 M32 K20·M256 K12.
- 기존 `air_control.py` (`stage0-accum-grid-v2`)의 `--one-case`, 직렬 빌드·flash, TX READY 후 INIT, 수집·metadata·종료·readback·공식 verifier를 유지한다. 외부 `standard_overnight.py` 정책은 `standard-immutable-retry-once-continue-v1`이다.
- 동일 논리 case는 최대 2 attempt(원래 1회+재시도 1회), 각각 새 root 및 고유 attempt ID를 사용한다. 공식 조건/논리 case ID는 동일 조건 검증을 위해 유지한다. 원본·실패·STOP을 덮어쓰거나 삭제하지 않는다.
- 유효하게 수집된 PER 실패는 재시도가 좋아도 첫 유효 관측을 공식 결과에 유지한다. 수집/제어가 무효인 attempt의 대체만 명시적인 제외 기록과 함께 허용한다. 재시도 성공으로 원래 실패를 숨기지 않는다.
- Stage0 grid의 높은 PER 자체는 정상 탐색 자료라서 재시도하지 않는다. 각 PHY는 기존 selector로 전체 grid, confirmation 5회 모두 PER<1%, pooled Wilson95 상한<1%를 만족해야 동결한다. 특정 PHY 실패 시 `execution_scope`에 검증된 PHY만 명시하고 의존하는 case만 보류한다. 부분 완료를 전체 Stage0/Standard 완료로 표시하지 않는다.
- 연결/전원/USB 일시 오류는 RF 없이 최대 30분 연결 복구를 기다린다. 수집이 살아 있으면 종료를 확인하기 전 중복 실행하지 않는다. 필요하면 기존 halt/readback API로 최대 3회 복구하고 7대 정지가 확인된 뒤 새 attempt를 준비한다. metadata/timeout/late/SPI/RDB/overrun 오류는 계속 실패로 기록한다.
- 사용자/현장 환경 STOP과 소유권이 다른 ACTIVE는 자동 해제하지 않는다. 현재 실험의 제어권·7대 정지를 회복할 수 없는 경우에는 RF를 보류하고 원본과 복구 사유를 보존한다.
- 상태: `sequence-status.json`, 전체 attempt/제외 기록: `ATTEMPT_LEDGER.json`, 최종: `sequence-finished.json`, 복구 불가: `sequence-failure.json`. 정상 case마다 분석/plot/사용자 대기 없이 다음 case로 진행한다.

# BRRS Standard profile — 489 case

이 문서는 다른 profile 문서를 읽지 않아도 Standard 차량 실험을 이해하고 운영할 수
있도록 범위, 보드 역할, 반복·회전, 실행·재개와 통행 오염 처리를 설명한다.

Standard는 실제 차량 논문의 기본 profile이다. Full의 1045 case보다 시간을 줄이되,
Essential에 없는 S1~S5 노드 수 그래프와 여섯 물리 링크의 반복 근거를 유지한다.
별도 Exp1은 실행하지 않는다. 같은 최종 TDMA 송수신 경로를 사용하는 Exp4 S1에서
M32/PAC4·PAC8 및 M64/128/256/PAC8을 측정하고, block마다 송신 보드를 바꾸어 프리앰블 비교와
차량 위치 차이를 함께 검증한다. 각 profile은 비교 범위와 반복 수가 다르며, 이번 Standard 최대 슬롯 갱신은 다른 profile의 부하값을 자동 변경하지 않는다.

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
| N6 | 1050257038 | trunk_A |
| N7 | 1050204212 | trunk_B |

Stage0과 Exp3의 단일 TX는 물리 N4를 사용한다. Exp2와 Exp5는 N2~N7을 한 대씩
활성화하고 나머지 다섯 TX가 정지했는지 검증한다. Exp4 S1은 block 1=N2, 2=N3,
3=N4, 4=N5, 5=N6, 6=N7 순으로 물리 TX를 바꾼다. 실제 링크는 case ID,
`physical_role`, serial과 위치 metadata로 구분한다.

## 공통 무선·펌웨어 조건

| 항목 | Standard profile의 현재 유효 설정 |
|---|---|
| 슈퍼프레임 | 10,000 us |
| 비컨 프리앰블 | M512 |
| DATA payload | application 16 B, 전체 PSDU 26 B |
| DATA 프리앰블 | S1 M32/64/128/256, S2~S6 M32/M256 |
| DATA PAC | M32는 PAC4/PAC8, M64 이상은 PAC8 |
| Exp4 guard | 200 us |
| Exp4 SB/SP | 1700/2200 us |
| Exp4 수신 | 슬롯별 scheduled delayed-RX |
| Exp4 SPI | persistent-SPIM 최적화 ON |
| Exp4 PHY 전환 | delta fast switch ON, PGF calibration 유지 |
| 목표 | 모든 물리 노드의 각 run PER < 1%, 시스템 오류 0 |

환경, 거리, 위치, 유전원 허브·어댑터·포트·케이블과 차량 상태를 차량용 manifest에
기록하고 실험 도중 바꾸지 않는다. Lead는 차량 Stage0 결과로 PHY 설정별 선정·동결하기
전까지 Exp2~Exp5에 임의 적용하지 않는다.

2026-09-12 차량 진단에서 M32/PAC8 S6/K13, 위 타이밍과 fast PHY/PGF 유지 조합은
13,000/13,000 수신, 모든 물리 노드 PER 0%, 시스템 오류 0으로 1회 통과했다.
이는 다른 PHY·PAC·활성 노드 수의 반복 검증이나 차량 Stage0 완료 근거가 아니다.
Standard의 Exp4 전체에 같은 구현과 타이밍을 적용해 본 실험에서 검증한다.
설정은 `profiles.standard.exp4_overrides`에 명시하며 다른 profile의 기본값은 유지한다.
실제 case 조건·build/capture 인자·metadata·PHY self-test까지 적용 여부를 검증한다.

## 전체 case 수

**최대 후보 반영:** 사용자의 최대 수용량 검증 지시에 따라 S6의 고부하를
M32 K13→K20, M256 K8→K12로 교체했다. K6 기준점과 비교 조건 개수는 그대로이므로
총 489 case를 유지한다. M32는 PAC4/PAC8 각각 K6/K20, M256은 PAC8 K6/K12다.

새 타이밍에서 계산상 최대는 M32 K20, M64 K18, M128 K15, M256 K12다.
Standard S6는 M32/M256만 비교하므로 M64/M128의 높은 부하는 포함하지 않는다.
계산상 최대 후보의 실제 PER 통과는 실측으로 판정한다. 후보가 실패하면 최대 수용량을
확정할 수 없으며 더 낮은 K의 추가 탐색은 위 고정 489개에 포함되지 않는다.
임시 lead27로 별도 실행하는 M32/PAC8 K20 진단은 Standard/Stage0에 합산하지 않는다.
2026-09-12 별도 K20 진단1회는20,000/20,000 수신,전노드PER0%,시스템오류0으로통과했다.
M32/PAC4 K20 및 M256/PAC8 K12의 Standard 반복 검증은 아직 실행하지 않았다.

| 구분 | 조건과 반복 | case |
|---|---:|---:|
| Stage0 grid | 41 leads × 3 PHY 설정 | 123 |
| Stage0 confirmation | 3 PHY 설정 × 선정 lead × 5 blocks | 15 |
| Exp1 | Exp4 S1에 흡수 | 0 |
| Exp2 | (M32 × PAC4/8 + M256 × PAC8) × 6 links × 3 blocks | 54 |
| Exp3 | 3 variants × 1 block | 3 |
| Exp4 | 23조건 × 12 blocks | 276 |
| Exp5 | 6 links × 3 blocks | 18 |
| **합계** |  | **489** |

한 조건만 여러 번 연속 실행하지 않는다. 각 stage에서 block 1의 모든 조건을 마친 뒤
block 2로 돌아가며, block마다 조건 시작 위치를 순환시키고 짝수 block은 역순으로
실행한다. 정상 결과라면 다음 case로 계속 진행한다. 비정상 PER, 시스템 오류 또는
사용자가 통행·환경 변화를 알린 경우에만 현재 case 뒤에서 멈추고 확인한다.

## Stage0 — PHY 설정별 lead 선정

물리 N4→INIT 단일 링크에서 M32/PAC4, M32/PAC8, M1024/PAC32 각각 lead
0~40 us의 41개 정수점을 측정한다. Lead당 2,000 전송 기회이며 grid는 총
123 case다. M1024/PAC32 branch는 Exp5의 scheduled delayed-RX lead를 PAC8에서
빌리지 않고 독립적으로 정하기 위한 것이다.
전체 grid가 유효해야 후보를 고른다. `lead-1`과 `lead+1`이 반드시 통과할 필요는 없다.
선정 lead 자체를 PHY 설정별 5회, 총 15 case 확인하고, 모든 run의 PER가 1% 미만이며 합산 Wilson 95%
상한도 1% 미만일 때만 manifest에 동결한다.

Stage0은 뒤 단계의 설정을 결정하므로 round마다 반복하지 않는다. 차량 배치나 전원·
안테나 환경이 바뀌면 새 manifest에서 다시 수행한다.

## Exp1 — 별도 실행하지 않음

Standard에서는 Exp1을 0회로 명시적으로 비활성화한다. Exp1과 Exp4 S1은 프리앰블
비교 목적이 겹치지만, Exp4 S1은 논문에서 실제 사용하는 비컨 동기화·scheduled RX·
TDMA 데이터 경로를 그대로 사용한다. 따라서 Standard는 Exp4 S1을 본 근거로 삼는다.

다만 두 결과가 완전히 같은 실험이라는 뜻은 아니다. Exp1은 단일 링크 PHY 회귀 시험이고
Exp4 S1은 최종 시스템 경로 시험이다. 펌웨어 디버깅이나 과거 데이터와의 직접 비교가
필요하면 profile 밖 진단으로 Exp1을 실행하되 Standard 489 case에 합산하지 않는다.

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
| S6 | M32/PAC4·8 K6/K20 + M256/PAC8 K6/K12 | 6 |
| **합계** |  | **23** |

PAC8로 고정한 M32~M256은 순수 프리앰블 길이 비교 곡선을 만들고, M32/PAC4는
제조사 권장 짧은 프리앰블 조합과 PAC8을 직접 비교한다. M64 이상에는 PAC4를 적용하지
않는다. S2~S6은 M32와 M256 끝점만 사용해
노드 수에 따른 이용률·PER 변화를 보여준다. S6/K20과 S6/K12는 새 타이밍의 최대 후보다.
모든 물리 노드의 반복 PER와 시스템 오류 검증을 통과한 경우에만 해당 조건의 수용 근거로 사용한다.
M32 K20 기본 sequence는23456723456723456723, M256 K12는234567234567이다.
M32 K20의 논리 N2/N3 offered는각4000, N4~N7은각3000으로 합20000이며,
block 회전으로 추가 슬롯의 물리 노드 배정을 순환한다. M256 K12는각2000으로 합12000이다.
Block 1~6에서 설치표를 한 칸씩 순환하고 block 7~12에서 같은 회전을 한 번 더
반복한다. 따라서 S1은 모든 물리 링크를 두 번씩, S2~S6은 가능한 논리 슬롯 역할을
두 주기 경험한다. 총 23 × 12 = 276 case다.

## Exp5 — 차량 링크별 raw CIR

N2~N7을 한 대씩 활성화해 M1024/PAC32로 측정한다. Lead는 Stage0의
M1024/PAC32 grid와 confirmation에서 독립적으로 선정한 값을 사용한다. 링크당 1,000 전송 기회를 주고
성공 프레임 중 최대 30프레임, 프레임당 300 raw CIR samples를 보존한다. Block당
6 case, 3 blocks로 총 18 case다.

차량별 실현 가능한 이용률은 Exp4의 최소 신뢰 프리앰블과 용량 결과로 정한다. Exp5는
링크별 CIR을 제공해 그 차이를 채널 관점에서 해석하지만, Exp5만으로 이용률을 직접
예측하거나 보정된 K-factor·순수 지연 확산을 주장하지 않는다.

## Round별 실행과 예상 시간

Stage0 138 case를 끝내 lead를 동결한 뒤 다음 순서로 진행한다.

| round | 실행 stage/block | case |
|---:|---|---:|
| 1 | Exp2 18 + Exp3 3 + Exp4 23 + Exp5 6 | 50 |
| 2~3 | Exp2 18 + Exp4 23 + Exp5 6 | 각 47 |
| 4~12 | Exp4 23 | 각 23 |

Case당 준비·flash·READY·약 10초 RF·수집 검증을 합쳐 평균 1분 정도면 약 8시간,
현장 중단과 재측정을 포함하면 **약 9~11시간**을 예상한다. 시간은 장비·빌드 캐시 상태에
따라 달라진다. 한 번에 끝낼 필요는 없으며 case, stage 또는 round 경계에서 중단한다.

## 계획·실행·재개

Ubuntu 제어/Air 수집 환경에서는 `/home/chieon/DWM3000/setup/ubuntu_air_20260911/brrs-env.sh`를
source하고 같은 디렉터리의 `standard_sequence.py`/`air_control.py`를 사용한다.
모든 보드는 Air에 있으므로 차량 manifest의 host는 Air 기준 `local`이다.
`standard_sequence.py prepare --root <새 Standard root>`는 첫 Stage0 case만 빌드·검증하고
flash/RF를 하지 않는다. `prepared-start.json`의 source/manifest/payload hash가 같을 때만
후속 실행에서 이 첫 case를 재사용한다. 나머지 case는 실제 Stage0 lead 선정·confirmation·
frozen 검증에 맞춰 실행 직전에 준비한다. 임시 진단 lead를 채워 전체 이미지를 미리 만들지 않는다.
준비 완료는 RF 시작 승인이 아니며, 기존 STOP/STOP_ALL은 보존한다.

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
대체 실행은 489 case에 포함되지 않는다. 탐색은 고유 로그 경로에서 수행하고 채택한
조건만 새 manifest에 동결한다. 이 로그는 진단 자료이며 Standard 통계에 자동 합산하지 않는다.
