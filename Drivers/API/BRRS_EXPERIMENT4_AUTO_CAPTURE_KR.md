# BRRS 실험 4 자동 수집 사용법

## 고정 조건

- 슈퍼프레임: 10 ms, 1,000회
- SYNC 비컨 프리앰블: 256 symbols
- DATA PSDU: 26 bytes (헤더 8 + 응용 데이터 16 + FCS 2)
- ACK 및 재전송: 없음
- 기본 슬롯 guard: **200 us**
- 기본 RX lead: **15 us** (`--lead`로 변경 가능)
- 로그: RTT channel 1
- 실행 순서: 모든 TX 센서 먼저, INIT 마지막

`brrs_exp4_multi_tx.sh`는 TX 노트북에 연결된 모든 probe를 자동 탐색하고
N2~N8 역할을 배정한 뒤 병렬 플래시와 RTT 수집을 수행한다.
`brrs_exp4_capture.sh`는 INIT 또는 센서 한 대만 실행하는 저수준 명령이다.
INIT와 모든 센서에서 preamble, sensor-count, guard, lead를 반드시 같게 입력한다.

## 최초 준비

macOS와 Ubuntu 모두 SDK의 `Drivers/API`에서 다음을 한 번 실행한다.

```bash
python3 -m pip install pylink-square
chmod +x brrs_exp4_multi_tx.sh brrs_exp4_probe_assign.py \
  brrs_exp4_capture.sh brrs_exp4_build.sh
```

`emBuild`와 `arm-none-eabi-nm`이 PATH에 없다면 해당 전체 경로를 환경변수
`EMBUILD`, `ARM_NM`으로 지정한다. SES는 열어 두어도 되지만 디버그 세션은
반드시 정지해야 한다. 자동 TX 실행에서는 `--serial`을 입력하지 않는다.
연결된 USB J-Link 수가 요청한 sensor-count와 정확히 일치해야 한다.

## S1: 코디네이터 + N2

TX 노트북에서 자동 TX를 먼저 실행한다.

```bash
cd /path/to/DW3_QM33_SDK_1.0.2/Drivers/API
./brrs_exp4_multi_tx.sh 32 1 1 iron_door_nlos 6.9
```

`READY marker seen` 뒤 RX 노트북에서 INIT를 실행한다.

```bash
cd /path/to/DW3_QM33_SDK_1.0.2/Drivers/API
./brrs_exp4_capture.sh init 32 1 1 iron_door_nlos 6.9
```

두 번째 반복은 양쪽 명령의 run만 `2`로 바꾼다. 256-symbol 비교도 같은
순서로 수행한다.

```bash
./brrs_exp4_multi_tx.sh     256 1 1 iron_door_nlos 6.9
./brrs_exp4_capture.sh init 256 1 1 iron_door_nlos 6.9
```

## S2: 코디네이터 + N2 + N3

TX 노트북에 센서 보드 두 개를 연결한 뒤 명령 하나를 실행한다.

```bash
./brrs_exp4_multi_tx.sh 32 2 1 iron_door_nlos 6.9
```

`EXP4_MULTI_TX_READY`가 나온 뒤 INIT를 시작한다.

```bash
./brrs_exp4_capture.sh init 32 2 1 iron_door_nlos 6.9
```

256-symbol 조건도 두 명령의 preamble만 모두 `256`으로 바꾼다. S3 이상도
같은 방식이며 sensor-count와 실제 TX probe 수를 함께 늘린다.

## 반복별 물리 보드 순환

probe는 serial 오름차순으로 정렬된다. run 1은 N2부터 순서대로 배정하고,
run 2부터 매번 한 칸씩 순환한다. 예를 들어 S3에서는 각 물리 보드가 세 번의
run 동안 N2, N3, N4를 모두 한 번씩 맡는다. 화면에는 다음 형식으로 실제
배정이 표시된다.

```text
EXP4_PROBE_ASSIGNMENT_CSV,run=2,rotation=1,role=N2,serial=1050273888
EXP4_PROBE_ASSIGNMENT_CSV,run=2,rotation=1,role=N3,serial=1050283069
EXP4_PROBE_ASSIGNMENT_CSV,run=2,rotation=1,role=N4,serial=1050211584
```

배정표는 `*_multi_tx.assignments.csv`에도 저장되고 각 노드의 meta에는
`probe_serial=`이 저장된다. 물리적 위치는 바꾸지 않는다.

## 다른 guard를 의도적으로 시험할 때

기본값은 검증된 200 us다. 별도 guard 실험에서만 **모든 보드 명령에 동일한
값**을 붙인다.

```bash
./brrs_exp4_multi_tx.sh     32 2 1 guard_sweep 1.0 --guard 220
./brrs_exp4_capture.sh init 32 2 1 guard_sweep 1.0 --guard 220
```

Stage0에서 선택한 lead가 18 us라면 두 명령 모두 `--lead 18`을 추가한다.
비기본 lead로 만든 이미지는 별도 출력 폴더와 로그 폴더에 저장되며,
`--no-build`는 같은 lead로 빌드한 이미지에만 허용된다.

## 성공 판정

정상 종료 시 마지막에 다음과 같이 표시된다.

```text
[rtt_capture] PASS
[verify] PASS: collection=PASS; ...
[done] raw=...
[done] meta=...
```

INIT 검증기는 다음을 모두 확인한다.

- 펌웨어 진단 revision 20 이상, beacon protocol 3
- 요청한 preamble, 센서 수, guard, 슬롯 간격과 실제 로그의 일치
- 1,000 슈퍼프레임 수집 완료와 평균 주기 10 ms
- delayed scheduling, wrong-slot, buffer overrun이 모두 0
- SYNC 준비 시간이 예산보다 작음
- double RX buffer의 수신/반환/해제 카운터가 실제 RX 수와 일치
- 각 노드 expected, rx, miss 합계와 aggregate 결과가 일치
- RMARKER 기반 슬롯 타이밍 표본 수와 수신 성공 수가 일치

`collection=PASS, link=LOSS`는 정상적인 실험 결과다. 이는 로그와 스케줄
수집은 완전하지만 실제 무선 손실(PER)이 있었다는 뜻이다. 반대로
`[verify] FAIL`이면 해당 로그를 제출용 결과로 쓰지 말고 원인을 해결한 뒤
새 run 번호로 다시 측정한다.

## 로그 위치

SDK의 한 단계 위 `logs`에 자동 저장된다.

```text
../logs/exp4_<환경>_<거리>m_g200_l15_<날짜>/
  exp4_32_s2_r1_init.log
  exp4_32_s2_r1_init.meta.txt
  exp4_32_s2_r1_n2.log
  exp4_32_s2_r1_n2.meta.txt
  exp4_32_s2_r1_n3.log
  exp4_32_s2_r1_n3.meta.txt
  exp4_32_s2_r1_multi_tx.assignments.csv
  exp4_32_s2_r1_multi_tx.console.log
```

같은 조건과 run 번호를 다시 쓸 때는 `--force`를 붙인다. 기존 파일은
`.prev.<시각>`으로 보존된다. `--no-build`는 같은 preamble, 센서 수, guard, lead의
이미지가 이미 생성된 경우에만 사용한다.

노드 하나만 따로 진단할 때는 기존 방식도 사용할 수 있다.

```bash
./brrs_exp4_capture.sh N3 32 3 1 diagnosis 0 --serial 1050273888
```
