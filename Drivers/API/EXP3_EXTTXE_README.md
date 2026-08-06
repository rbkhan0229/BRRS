# BRRS Experiment 3: SFD/PHR airtime 차분 검증

## 1. 무엇을 측정하는가

실험 3은 수신기 소프트웨어 폴링 시각을 프레임의 실제 종료 시각으로
간주하지 않는다. 송신 중에만 High가 되는 DWM3000의 `EXTTXE` 신호를
nRF52840 하드웨어 타이머로 캡처하여 실제 송신 구간의 폭을 측정한다.

- `EXTTXE`: External Transmit Enable. DWM3000이 송신 상태인 동안 High인
  GPIO 신호.
- `GPIO5`: DWM3000에서 `EXTTXE` 기능을 내보내는 핀.
- `Arduino D1`: DWM3000EVB에서 GPIO5가 연결된 nRF52840 입력 핀.
- `GPIOTE`: GPIO의 상승/하강 에지를 하드웨어 이벤트로 만드는 nRF52840
  장치.
- `PPI`: CPU를 거치지 않고 GPIOTE 이벤트를 TIMER 캡처 작업으로 직접
  연결하는 장치.
- `TIMER4`: 16 MHz로 동작하며 1 tick은 62.5 ns이다.

일반 DWM3000EVB에서는 GPIO5와 Arduino D1이 보드 안에서 연결되어 있어
외부 점퍼선이 필요 없다. 초기 DWM3000 v1.3 엔지니어링 샘플은 GPIO5와
GPIO6 배선이 서로 바뀐 보드가 있으므로 캡처 수가 0이면 보드 버전을 먼저
확인한다.

## 2. 세 가지 조건

| 조건 | SFD | PHR 속도 | 목적 |
|---|---:|---|---|
| A | DW SFD 8 symbol | STD | 기준 조건 |
| B | DW SFD 16 symbol | STD | `B - A`로 SFD 8 symbol 증가분 측정 |
| C | DW SFD 8 symbol | DTA | `A - C`로 STD PHR의 추가 시간 측정 |

세 조건 모두 preamble 32 symbol, PSDU 127 byte, 6.81 Mb/s, PAC8,
STS OFF이다. INIT와 NORMAL은 반드시 같은 조건의 펌웨어를 사용한다.

## 3. 이론 airtime

시간 계산은 정수 마이크로초 근사가 아니라 나노초 단위 올림 계산을 쓴다.

```text
T_frame = T_preamble + T_SFD + T_PHR + T_PSDU

data_bits    = 127 byte x 8 = 1016 bit
RS_blocks    = ceil(1016 / 330) = 4
RS_parity    = 4 x 48 = 192 bit
encoded_bits = 1016 + 192 = 1208 bit
T_PSDU       = 1208 x 128.21 ns = 154.878 us
```

- PHR: PHY Header. PSDU의 길이와 PHY 정보를 전달하는 21-symbol 헤더.
- PSDU: PHY Service Data Unit. MAC 프레임과 FCS를 포함해 PHY가 전송하는
  데이터 영역.
- Reed-Solomon 부호: 데이터 오류 복구용 패리티를 추가하는 전방 오류
  정정 부호. 최대 330 data bit마다 48 parity bit가 추가된다.
- A 이론값: 약 217.124 us
- B 이론값: 약 225.265 us
- C 이론값: 약 198.278 us
- `B - A`: 약 8.141 us
- `A - C`: 약 18.846 us

EXTTXE의 절대 폭에는 DWM3000의 고정적인 송신 준비/종료 시간이 포함될
수 있다. 따라서 논문의 핵심 비교는 절대 폭이 아니라 `B-A`와 `A-C`
차분이다. 동일한 고정 지연은 차분에서 소거된다.

## 4. 펌웨어 빌드

SDK 루트에서 다음 명령을 한 번 실행한다.

```bash
chmod +x Drivers/API/brrs_exp3_build_all.sh
Drivers/API/brrs_exp3_build_all.sh
```

생성 위치:

```text
Drivers/API/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp3/
```

생성되는 HEX:

```text
exp3_A_init.hex       exp3_A_normal.hex
exp3_B_init.hex       exp3_B_normal.hex
exp3_C_init.hex       exp3_C_normal.hex
```

HEX는 컴파일된 펌웨어를 보드 플래시에 기록할 수 있도록 만든 파일이다.

## 5. 두 노트북에서 실행

각 조건 A, B, C에 대해 아래 순서로 한 번씩 실행한다.

### SES의 Build and Debug를 사용하는 방법

SES 상단의 build configuration 목록에서 역할과 조건을 선택한 뒤
`Build and Debug`를 누른다. 소스 코드의 `#define`은 직접 수정하지 않는다.

| 조건 | TX/NORMAL 노트북 | RX/INIT 노트북 |
|---|---|---|
| A | `Exp3_A_Normal` | `Exp3_A_Init` |
| B | `Exp3_B_Normal` | `Exp3_B_Init` |
| C | `Exp3_C_Normal` | `Exp3_C_Init` |

각 조건에서 NORMAL을 먼저 `Build and Debug`하여 실행 상태로 만든다. 그
다음 INIT를 `Build and Debug`한다. INIT가 시작되면 10초 startup grace 후
SYNC를 보내고 1000-cycle 측정을 시작한다.

SES 터미널은 실행 상태와 최종 통계를 확인하는 용도이다. 1000개의
`EXP3_TX_CSV` 원시 측정값은 터미널 scrollback에서 유실될 수 있으므로
그래프용 로그는 별도 터미널의 `JLinkRTTLogger` 채널 1로 저장한다.

가장 간단한 방법은 준비된 HEX를 플래시하고 RTT 로그까지 바로 연결하는
스크립트를 사용하는 것이다. 스크립트는 macOS와 Linux에서 모두 실행되는
`bash` 기반이다.

```bash
chmod +x Drivers/API/brrs_exp3_flash_and_log.sh
```

TX/NORMAL 노트북:

```bash
Drivers/API/brrs_exp3_flash_and_log.sh \
  A normal \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_A_tx.log
```

위 명령은 NORMAL 펌웨어를 플래시한 뒤 logger 상태로 계속 대기한다. 그
상태에서 RX/INIT 노트북으로 이동하여 다음을 실행한다.

```bash
Drivers/API/brrs_exp3_flash_and_log.sh \
  A init \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_A_rx.log
```

INIT도 플래시 직후 logger가 자동으로 연결되므로 10초 startup grace 안에
수동으로 별도 명령을 입력할 필요가 없다. B와 C는 첫 번째 인자를 각각
`B`, `C`로 바꾸고 로그 파일명도 맞춰 사용한다.

직접 SES 또는 J-Link Commander를 사용할 경우에는 아래 순서를 따른다.

1. TX 노트북의 NORMAL 보드에 `exp3_X_normal.hex`를 플래시하고 실행한다.
2. TX 로그용 `JLinkRTTLogger`를 채널 1로 연결한다.
3. RX 노트북의 INIT 보드에 같은 X의 `exp3_X_init.hex`를 플래시하고
   실행한다.
4. INIT의 10초 startup grace 안에 RX 로그용 `JLinkRTTLogger`를 채널 1로
   연결한다.
5. `EXP3_TX_RESULT ... status=PASS`와 `EXP3_RX_DONE ...`이 나올 때까지
   기다린 뒤 logger를 종료한다.

TX 예시:

```bash
mkdir -p ~/Desktop/DWM3000/result3/exp3_exttxe

JLinkRTTLogger \
  -Device NRF52840_XXAA \
  -If SWD \
  -Speed 4000 \
  -RTTChannel 1 \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_A_tx.log
```

RX에서는 파일명만 `exp3_A_rx.log`로 바꾼다. B와 C도 같은 방식으로
저장한다.

## 6. 로그 완전성 확인

TX 원시 측정값은 조건마다 반드시 정확히 1000줄이어야 한다.

```bash
grep -c '^EXP3_TX_CSV,' \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_A_tx.log

grep 'EXP3_TX_RESULT' \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_A_tx.log

grep 'EXP3_RX_RESULT_CSV' \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_A_rx.log
```

첫 명령의 결과가 `1000`, TX status가 `PASS`여야 한다. RX의 PER은
측정 결과이므로 패킷 손실이 있어도 로그 수집 자체는 완료될 수 있다.

## 7. CSV와 그래프 생성

```bash
python3 Drivers/API/brrs_exp3_exttxe_analyze.py \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_A_tx.log \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_A_rx.log \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_B_tx.log \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_B_rx.log \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_C_tx.log \
  ~/Desktop/DWM3000/result3/exp3_exttxe/exp3_C_rx.log \
  -o ~/Desktop/DWM3000/result3/exp3_exttxe/result \
  --prefix exp3
```

분석기는 A/B/C 각각에서 원시 TX 샘플이 1000개인지 검사한다. 하나라도
누락되거나 펌웨어 TX 결과가 FAIL이면 그래프를 만들지 않고 실패를
출력한다.

주요 결과:

- `exp3_summary.csv`: 조건별 절대 EXTTXE 폭과 이론 airtime
- `exp3_differential.csv`: `B-A`, `A-C` 실측/이론 차이
- `exp3_airtime.png`: 조건별 절대시간 비교
- `exp3_differential.png`: 논문 핵심인 차분 검증
- `exp3_rx_per.csv`, `exp3_rx_per.png`: 조건별 수신 PER
