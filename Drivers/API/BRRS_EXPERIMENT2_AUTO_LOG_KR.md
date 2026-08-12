# BRRS 실험 2 원시 로그 자동 저장

이 도구는 실험 중 필요한 작업만 수행한다.

- 선택한 Exp2 펌웨어 빌드
- 보드 플래시 및 실행
- RTT 채널 1 원시 로그 저장
- RX의 `CIR_CSV` 1000개 및 `EXP2_DONE` 검사
- TX의 송신 1000회 및 설정 오류 검사
- 원시 로그와 플래시된 HEX의 SHA-256 기록

CSV와 그래프는 생성하지 않는다. 분석은 모든 원시 로그 수집이 끝난 뒤
`brrs_cir_log_to_csv_plot.py`로 별도 수행한다.

## 준비

두 노트북에 같은 SDK 폴더가 있어야 한다. 자동화 도구가 J-Link를 직접
사용하므로, 실행 전 SES의 기존 디버그 세션은 중지한다.

최초 한 번 `pylink-square`를 설치한다. 이 라이브러리는 J-Link DLL을 직접
사용해 플래시와 RTT 수집을 하나의 연결에서 처리한다.

```bash
python3 -m pip install pylink-square
```

Ubuntu에서는 압축을 푼 현재 경로로 이동한다. `/path/to/...`를 글자 그대로
입력하지 않는다. 현재 TX 노트북의 예시는 다음과 같다.

```bash
cd "$HOME/Desktop/CHIEON/BRRS_FW_v2.6_exp2_autolog_20260811/DW3_QM33_SDK_1.0.2"
```

기본 방식은 `JLinkRTTLogger`를 사용하지 않는다. 별도 Logger가 두 번째
디버그 연결을 만들면서 펌웨어가 초기화되던 문제를 피하기 위해, Python의
`pylink-square`가 한 J-Link 연결 안에서 build 이후 flash, run, RTT 채널 1
수집과 결과 검증을 연속 수행한다.

### Ubuntu 첫 실행 주의

nRF52840 Fxx 이후 칩은 디버그 접근 보호가 기본 활성화될 수 있다. 구버전
펌웨어에서는 플래시 후 RTT Logger가 두 번째 J-Link 연결을 만들 때 자동
복구가 실행되어 펌웨어가 지워지고 HardFault가 발생할 수 있었다.

현재 Debug 펌웨어는 부팅 초기에 software APPROTECT disable 값을 설정한다.
자동 수집기는 같은 J-Link 연결에서 `UICR.APPROTECT`의 PALL 하위 바이트가
`0x5A`인지 확인하고, 지워진 새 보드에서는 한 번 기록한 뒤 실행한다. 플래시
후 별도 Logger로 재접속하지 않으므로 이 과정에서 펌웨어가 지워지지 않는다.

스크립트 위치:

```text
Drivers/API/brrs_exp2_capture.sh
```

이 파일은 안정된 진입점이며 내부적으로 `brrs_exp2_capture_v3.sh`를 호출한다.
기본 backend는 `pylink`다. RTT TELNET 방식은 비교 진단이 필요할 때만
`--method telnet`으로 선택한다.

현재 NLOS 측정 조건은 다음과 같다.

- 장소: Room 519 철문 사이 NLOS
- TX: 방 외부, 문 기준 2.7 m
- Beacon + RX: 방 내부, 문 기준 4.2 m
- TX-RX 총 수평거리: 6.9 m
- 설치 높이: 바닥에서 1.5 m

따라서 아래 명령의 마지막 위치 인자는 `1`이 아니라 `6.9`를 사용한다.

## 실행 순서

같은 조건은 TX와 RX에서 preamble, run, environment, distance를 동일하게
입력한다.

1. TX 노트북에서 먼저 실행한다.

```bash
cd "$HOME/Desktop/CHIEON/BRRS_FW_v2.6_exp2_autolog_20260811/DW3_QM33_SDK_1.0.2"
Drivers/API/brrs_exp2_capture.sh tx 32 1 iron_door_nlos 6.9
```

2. TX에 아래 문구가 나오면 RX 노트북에서 실행한다.

```text
[rtt_capture] READY marker seen
```

```bash
cd "$HOME/Desktop/CHIEON/BRRS_FW_v2.6_exp2_autolog_20260811/DW3_QM33_SDK_1.0.2"
Drivers/API/brrs_exp2_capture.sh rx 32 1 iron_door_nlos 6.9
```

3. 양쪽 모두 마지막에 `[verify] PASS`가 나오는지 확인한다.

4. 실험이 끝나면 TX 노트북의 `*_tx.log`와 `*_tx.meta.txt`를 RX
   노트북의 같은 실험 폴더로 복사한다. 두 노트북은 파일을 자동으로
   공유하지 않는다.

RX의 PASS는 CIR 행 수가 정확히 1000개인지뿐 아니라 frame 번호 1000개,
cycle 번호 1000개, 노드 ID, 프리앰블 값과 열 개수까지 모두 확인한다.
`EXP2_DONE`도 `status=PASS`여야 한다. TX는 송신 성공/시도 1000회,
비컨의 프리앰블 값, delayed-late와 설정 오류를 함께 확인한다.

## 조건 변경

프리앰블과 반복 번호만 변경한다.

```bash
Drivers/API/brrs_exp2_capture.sh tx 64 2 iron_door_nlos 6.9
Drivers/API/brrs_exp2_capture.sh rx 64 2 iron_door_nlos 6.9
```

TX 펌웨어는 항상 `Exp2_Normal`이지만, 각 독립 반복을 새로 시작하기 위해
스크립트가 매번 보드를 다시 플래시하고 실행한다. RX는 preamble에 맞는
`Exp2_32_Init`, `Exp2_64_Init`, `Exp2_128_Init`, `Exp2_256_Init`을 선택한다.

## 저장 위치

기본 저장 위치는 SDK 상위의 `logs` 폴더다.

```text
DWM3000/logs/exp2_iron_door_nlos_6.9m_20260811/
  exp2_32_r1_tx.log
  exp2_32_r1_tx.meta.txt
  exp2_32_r1_rx.log
  exp2_32_r1_rx.meta.txt
```

기존 원시 로그가 있으면 덮어쓰지 않고 실패한다. 같은 조건을 다시 측정할
때는 run number를 증가시킨다.

## J-Link가 여러 개인 경우

```bash
Drivers/API/brrs_exp2_capture.sh rx 32 1 iron_door_nlos 6.9 \
  --serial 1050270933
```

## 기존 HEX 사용

빌드를 생략하려면 `--no-build`를 추가한다.

```bash
Drivers/API/brrs_exp2_capture.sh rx 32 1 iron_door_nlos 6.9 --no-build
```

논문용 원시 로그는 기본 동작처럼 매번 다시 빌드하는 것을 권장한다.

## 자동 수집 확인

정상 완료 후 같은 폴더에 원시 로그와 메타데이터가 생성된다.

```bash
grep -c '^CIR_CSV,' ../logs/exp2_iron_door_nlos_6.9m_YYYYMMDD/exp2_32_r1_rx.log
grep 'EXP2_DONE' ../logs/exp2_iron_door_nlos_6.9m_YYYYMMDD/exp2_32_r1_rx.log
```

첫 명령은 `1000`, 두 번째 명령은 `status=PASS`를 보여야 한다. 메타 파일에는
펌웨어와 원시 로그의 SHA-256, RTT 제어 블록 주소와 실행 조건이 기록된다.

`CIR_CSV`가 0개이면서 `EXP2_DONE status=FAIL`인 로그가 정상적으로 저장됐다면
자동 로깅은 동작한 것이고 무선 링크가 실패한 것이다. 이 경우 TX가 먼저
READY 상태였는지와 양쪽 비컨 설정을 점검한다.
