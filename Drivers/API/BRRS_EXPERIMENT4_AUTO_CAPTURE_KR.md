# BRRS 실험 4 자동 수집 사용법

## 고정 조건

- 슈퍼프레임: 10 ms, 1,000회
- SYNC 비컨 프리앰블: 256 symbols
- DATA PSDU: 26 bytes (헤더 8 + 응용 데이터 16 + FCS 2)
- ACK 및 재전송: 없음
- 기본 슬롯 guard: **200 us**
- 로그: RTT channel 1
- 실행 순서: 모든 TX 센서 먼저, INIT 마지막

`brrs_exp4_capture.sh`는 요청한 조건의 펌웨어 한 개만 빌드하고, 플래시,
RTT 원시 로그 저장, 결과 검증, 메타데이터 생성을 차례로 수행한다. INIT와
모든 센서 명령에서 preamble, sensor-count, guard를 반드시 같게 입력한다.

## 최초 준비

macOS와 Ubuntu 모두 SDK의 `Drivers/API`에서 다음을 한 번 실행한다.

```bash
python3 -m pip install pylink-square
chmod +x brrs_exp4_capture.sh brrs_exp4_build.sh
```

`emBuild`와 `arm-none-eabi-nm`이 PATH에 없다면 해당 전체 경로를 환경변수
`EMBUILD`, `ARM_NM`으로 지정한다. SES는 열어 두어도 되지만 디버그 세션은
반드시 정지해야 한다. 한 컴퓨터에 J-Link가 여러 개 연결되어 있으면 각
명령에 `--serial <J-Link S/N>`을 붙인다.

## S1: 코디네이터 + N2

TX 노트북에서 N2를 먼저 실행한다.

```bash
cd /path/to/DW3_QM33_SDK_1.0.2/Drivers/API
./brrs_exp4_capture.sh N2 32 1 1 iron_door_nlos 6.9
```

`READY marker seen` 뒤 RX 노트북에서 INIT를 실행한다.

```bash
cd /path/to/DW3_QM33_SDK_1.0.2/Drivers/API
./brrs_exp4_capture.sh init 32 1 1 iron_door_nlos 6.9
```

두 번째 반복은 양쪽 명령의 run만 `2`로 바꾼다. 256-symbol 비교도 같은
순서로 수행한다.

```bash
./brrs_exp4_capture.sh N2   256 1 1 iron_door_nlos 6.9
./brrs_exp4_capture.sh init 256 1 1 iron_door_nlos 6.9
```

## S2: 코디네이터 + N2 + N3

N2와 N3 명령은 각각 연결된 노트북/터미널에서 먼저 시작한다.

```bash
./brrs_exp4_capture.sh N2 32 2 1 iron_door_nlos 6.9
./brrs_exp4_capture.sh N3 32 2 1 iron_door_nlos 6.9
```

두 센서 모두 `READY marker seen`이 나온 뒤 INIT를 시작한다.

```bash
./brrs_exp4_capture.sh init 32 2 1 iron_door_nlos 6.9
```

256-symbol 조건도 세 명령의 preamble만 모두 `256`으로 바꾼다. S3 이상도
같은 방식이며, S3이면 N2/N3/N4를 먼저 실행한 뒤 INIT를 실행한다.

## 다른 guard를 의도적으로 시험할 때

기본값은 검증된 200 us다. 별도 guard 실험에서만 **모든 보드 명령에 동일한
값**을 붙인다.

```bash
./brrs_exp4_capture.sh N2   32 2 1 guard_sweep 1.0 --guard 220
./brrs_exp4_capture.sh N3   32 2 1 guard_sweep 1.0 --guard 220
./brrs_exp4_capture.sh init 32 2 1 guard_sweep 1.0 --guard 220
```

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
../logs/exp4_<환경>_<거리>m_g200_<날짜>/
  exp4_32_s2_r1_init.log
  exp4_32_s2_r1_init.meta.txt
  exp4_32_s2_r1_n2.log
  exp4_32_s2_r1_n2.meta.txt
  exp4_32_s2_r1_n3.log
  exp4_32_s2_r1_n3.meta.txt
```

같은 조건과 run 번호를 다시 쓸 때는 `--force`를 붙인다. 기존 파일은
`.prev.<시각>`으로 보존된다. `--no-build`는 같은 preamble, 센서 수, guard의
이미지가 이미 생성된 경우에만 사용한다.
