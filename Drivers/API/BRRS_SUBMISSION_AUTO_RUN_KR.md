# 제출용 실험별 일괄 실행기

`brrs_run_experiment.sh`는 Stage0부터 Exp4를 한 번에 섞어 실행하는 스크립트가
아니다. 사용자가 선택한 **실험 한 종류**에 대해서만 조건 변경, 반복, 빌드,
플래시, RTT channel 1 저장과 검증을 순서대로 수행한다.

## 기본 실행 행렬

아래 횟수는 한 가지 물리 배치와 환경을 고정했을 때의 기본값이다.

| 선택한 실험 | 자동 실행 내용 | 역할별 케이스 수 |
|---|---|---:|
| `stage0` | lead 0~40 us의 1 us 간격 고정 무작위 탐색, 조건당 1회 | 41 |
| `exp1` | M=32/64/128/256, 조건당 1회 | 4 |
| `exp2` | M=32/64/128/256, 조건당 1회 | 4 |
| `exp3` | A/B/C, 조건당 1회 | 3 |
| `exp4` | M=32/64/256, 조건당 1회 | 3 |

실행 순서는 시간 경과 효과가 한 조건에 몰리지 않도록 run마다 정방향과 역방향을
교대한다. Exp3는 A/B/C의 시작점을 run마다 바꾼다. 어느 한 케이스에서 수집 검증이
실패하면 다음 조건으로 넘어가지 않고 즉시 중단한다.

Stage0은 `--leads`로 후보를 탐색한다. Exp1~4는 Stage0에서 선택한 값을
`--lead <us>`로 고정한다. `--lead`를 생략하면 15 us이며, TX와 RX/INIT
명령에 같은 값을 입력해야 로그와 메타데이터의 실험 조건도 일치한다.

## 공통 준비

```bash
cd Drivers/API
chmod +x brrs_run_experiment.sh brrs_*_capture.sh brrs_exp4_build.sh
python3 -m pip install pylink-square
```

각 물리 보드에는 한 개의 명령이 필요하다. 서로 다른 노트북의 보드를 한 로컬
명령으로 동시에 제어할 수는 없으므로 **TX/센서 명령을 먼저 시작하고 RX/INIT
명령을 마지막에 시작**한다. 각 역할의 case 순서는 동일하다.

환경 이름에는 공백을 쓰지 않는다. 한 컴퓨터에 J-Link가 여러 개 연결되어 있으면
각 명령에 `--serial <S/N>`을 붙인다.

## Stage0

먼저 TX 노트북에서:

```bash
./brrs_run_experiment.sh stage0 tx iron_door_nlos 6.9
```

그다음 RX 노트북에서:

```bash
./brrs_run_experiment.sh stage0 rx iron_door_nlos 6.9
```

기본 실행은 전이 구간과 주기성을 함께 확인하는 41조건 전수 탐색이다. 탐색 결과로 정한 후보와 양옆 lead를
5회씩 확인할 때는 두 노트북에서 같은 목록을 사용한다.

```bash
./brrs_run_experiment.sh stage0 tx iron_door_nlos 6.9 \
  --leads 14,15,16 --repeats 5
./brrs_run_experiment.sh stage0 rx iron_door_nlos 6.9 \
  --leads 14,15,16 --repeats 5
```

Stage0의 최종 후보는 첫 탐색 데이터에서 정해지므로 이 확인 목록만큼은 자동으로
추측하지 않는다. 필요하면 `--leads 0-40`처럼 범위를 지정할 수 있다.

## Exp1

```bash
# TX 먼저
./brrs_run_experiment.sh exp1 tx iron_door_nlos 6.9

# RX 다음
./brrs_run_experiment.sh exp1 rx iron_door_nlos 6.9
```

기본 명령은 M=32/64/128/256을 한 번씩, 총 4개 케이스로 수행한다. 각 케이스는
2,000회 측정 기회를 가진다. 논문용 5회 반복은 양쪽 명령에 `--repeats 5`를 붙인다.
예를 들어 Stage0에서 18 us를 선택했다면 두 명령 모두 `--lead 18`을 붙인다.

## Exp2

```bash
# TX 먼저
./brrs_run_experiment.sh exp2 tx iron_door_nlos 6.9

# RX 다음
./brrs_run_experiment.sh exp2 rx iron_door_nlos 6.9
```

기본 명령은 M=32/64/128/256을 한 번씩, 총 4개 케이스로 수행한다. 논문용 3회
반복은 `--repeats 3`을 명시한다. RX 원시 로그에는
성공 수신 프레임마다 CIR 행이 저장되며, 무선 손실은 수집 실패와 구분된다.
Stage0 선택값이 18 us라면 TX/RX 양쪽에 `--lead 18`을 붙인다.

## Exp3

```bash
# TX 먼저
./brrs_run_experiment.sh exp3 tx iron_door_nlos 6.9

# RX 다음
./brrs_run_experiment.sh exp3 rx iron_door_nlos 6.9
```

기본 명령은 A(SFD8+STD PHR), B(SFD16+STD PHR), C(SFD8+DTA PHR)를 한 번씩,
총 3개 케이스로 수행한다. 논문용 3회 반복은 `--repeats 3`을 명시한다. A/B/C는 비컨으로 바꿀 수 없는 PHY 설정이므로 각
케이스에서 대응하는 TX/RX 펌웨어를 다시 플래시한다.
Exp3도 RX 링크 검증 조건을 통일하기 위해 같은 `--lead` 값을 기록하고 적용한다.

## Exp4

Exp4는 실제 연결한 센서 수를 `--sensors`로 지정해야 한다. TX 노트북에는 센서
수와 정확히 같은 개수의 J-Link 보드를 연결한다. `tx` 역할은 연결된 probe를
자동 탐색하고 serial 오름차순으로 정렬한 다음 N2부터 순서대로 배정한다.

반복 run이 바뀌면 배정을 한 칸씩 순환한다. S3의 예는 다음과 같다.

| run | N2 | N3 | N4 |
|---:|---|---|---|
| 1 | 첫 번째 probe | 두 번째 probe | 세 번째 probe |
| 2 | 두 번째 probe | 세 번째 probe | 첫 번째 probe |
| 3 | 세 번째 probe | 첫 번째 probe | 두 번째 probe |

실행 화면과 `*.assignments.csv`, 각 역할의 meta 파일에 실제 역할과 J-Link serial이
기록된다. 물리 보드 위치는 옮기지 않고 펌웨어 역할만 순환해야 슬롯 역할과
보드·위치 편차를 분리할 수 있다.

```bash
# TX 노트북: 한 명령이 N2/N3/N4를 모두 자동 배정하고 실행한다.
./brrs_run_experiment.sh exp4 tx iron_door_nlos 6.9 \
  --sensors 3 --repeats 3 --lead 15

# EXP4_MULTI_TX_READY가 출력된 다음 INIT 노트북에서 시작한다.
./brrs_run_experiment.sh exp4 init iron_door_nlos 6.9 \
  --sensors 3 --repeats 3 --lead 15
```

Exp4의 lead는 코디네이터 delayed-RX 창에 적용된다. TX 명령에도 같은 값을
넣는 이유는 양쪽 실행 manifest에 동일한 실험 조건을 남기기 위해서다.

각 역할은 기본적으로 M=32/64/256을 한 번씩, 총 3개 케이스로 수행한다. 완전한
순환 균형은 S2에서 2회, S3에서 3회처럼 센서 수와 같은 반복 수에서 얻는다.
기본 guard는 200 us이다. TX probe 수가 `--sensors`와 다르면 잘못된 보드를
임의 선택하지 않고 실행 전에 실패한다. 기존의 N2/N3 개별 명령은 진단용으로
계속 사용할 수 있다.

한 조건만 먼저 시험할 때는 다음 직접 실행 명령을 사용한다.

```bash
# TX 노트북
./brrs_exp4_multi_tx.sh 32 3 1 smoke 0

# EXP4_MULTI_TX_READY 확인 후 INIT 노트북
./brrs_exp4_capture.sh init 32 3 1 smoke 0
```

일괄 실행기는 본 측정 전에 모든 Exp4 펌웨어 이미지를 먼저 빌드한다. 따라서 첫
케이스 이후에는 양쪽 노트북이 컴파일 시간 차이로 서로 다른 조건을 실행할 위험을
줄이고 플래시와 측정만 진행한다.

## 짧은 예비 측정과 재개

제출용 횟수 전에 두 번만 예비 측정하려면 다음처럼 범위와 반복 수를 줄인다.

```bash
./brrs_run_experiment.sh exp1 rx test 1 --preambles 32,256 --repeats 2
```

run 6부터 새 블록을 시작하려면 `--run-start 6`을 사용한다. 같은 run 번호의 기존
로그를 의도적으로 다시 만들 때만 `--force`를 붙인다.

실행 전 전체 순서만 확인하려면 `--dry-run`을 사용한다.

```bash
./brrs_run_experiment.sh exp2 rx iron_door_nlos 6.9 --dry-run
```

## 결과 위치와 판정

- 개별 원시 로그와 meta: SDK 상위 `logs/exp*_*` 폴더
- 일괄 실행 manifest: SDK 상위 `logs/suites` 폴더
- `CASE_PASS`: 해당 케이스의 실험별 수집 검증 통과
- `SUITE_DONE ... status=PASS`: 선택한 실험 블록의 모든 케이스 통과
- Exp1~3의 `end=1`/`end_tx=3`: 양쪽 보드가 같은 케이스의 명시적 END 비컨으로 종료됨
- `link=LOSS`: 실제 무선 손실이며, 원시 로그 수집 실패와는 다름

물리 거리, LOS/NLOS 배치, 센서 보드 수는 펌웨어가 바꿀 수 없다. 따라서 환경이나
거리 블록을 바꿀 때는 지그를 고정한 뒤 같은 실험 명령을 새 환경 이름으로 다시
실행한다.
