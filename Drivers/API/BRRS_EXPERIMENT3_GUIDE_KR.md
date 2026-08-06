# BRRS 실험 3 실행 가이드

## 현재 설정

- 원본 BRRS 코드의 `BRRS_EXPERIMENT=3` 사용
- INIT/RX: `ex_35a_brrs_init/brrs_init.c`
- Normal/TX: `ex_35b_brrs_normal/brrs_normal.c`
- DATA preamble: 32 symbols
- DATA PAC: PAC8
- PSDU: 127 bytes
- delayed-RX lead/tail margin: 14 us / 0 us
- 측정 횟수: 1,000회

PAC4 비교군인 `ex_36a_pac4_control_init`과
`ex_36b_pac4_control_normal`은 이 실험에서 사용하지 않는다.

## 준비된 펌웨어

- INIT/RX:
  `Output/Debug/Exe/dw3000_api_brrs_exp3_init_plen32_psdu127.hex`
- Normal/TX:
  `Output/Debug/Exe/dw3000_api_brrs_exp3_normal_plen32_psdu127.hex`

현재 `example_selection.h`와 기본 `dw3000_api.hex`는 INIT/RX로
맞춰져 있다.

## 로그 저장

INIT 노드가 실행 중일 때 RTT channel 1에 logger를 연결한다.
개별 측정값은 실험 중 RAM에 저장되므로, 1,000회가 끝나기 전에만
logger를 연결하면 된다.

```bash
mkdir -p /Users/songchieon/Desktop/DWM3000/logs/exp3_overhead_lab_1m_20260724

JLinkRTTLogger \
  -Device NRF52840_XXAA \
  -If SWD \
  -Speed 4000 \
  -RTTChannel 1 \
  /Users/songchieon/Desktop/DWM3000/logs/exp3_overhead_lab_1m_20260724/exp3_32_pac8_psdu127.log
```

Normal/TX를 먼저 실행하고 INIT/RX를 실행한다. INIT에는 첫 SYNC 전
10초의 시작 대기 시간이 있다. SES Terminal은 channel 0에서 계속
볼 수 있다.

## 완료 확인

```bash
grep "EXP3_DONE" /Users/songchieon/Desktop/DWM3000/logs/exp3_overhead_lab_1m_20260724/exp3_32_pac8_psdu127.log
grep -c "^EXP3_CSV," /Users/songchieon/Desktop/DWM3000/logs/exp3_overhead_lab_1m_20260724/exp3_32_pac8_psdu127.log
```

정상 완료 조건은 다음과 같다.

```text
EXP3_DONE,...expected=1000,rx=1000,samples=1000,status=PASS
```

두 번째 명령의 결과도 `1000`이어야 한다.

## 기록값 해석

- `rmarker_high32`: DWM3000 하드웨어가 저장한 정확한 RMARKER 시각
- `rx_open_high32`: delayed-RX에 프로그램한 창 시작 시각
- `rxprd_obs_cpu`: RXPRD 상태를 처음 읽은 SPI transaction의 MCU-cycle 중간점
- `rxsfdd_obs_cpu`: RXSFDD 상태를 처음 읽은 SPI transaction의 MCU-cycle 중간점
- `rxphd_obs_cpu`: RXPHD 상태를 처음 읽은 SPI transaction의 MCU-cycle 중간점
- `rxfr_obs_cpu`: RXFR 또는 RXFCG 상태를 처음 읽은 SPI transaction의 MCU-cycle 중간점
- `anchor_sys_high32`, `anchor_cpu`: 프레임 종료 후 한 번 측정한 UWB/MCU clock 정렬점
- `*_poll_cycles`: 각 상태 read transaction의 폭
- `obs_mask=15`: 네 수신 단계가 모두 관측됨

`*_obs_cpu`는 해당 이벤트의 하드웨어 timestamp가 아니라 SPI 폴링으로
처음 확인한 시각이다. 수신 중에는 MCU cycle만 저장하고, 프레임 종료 후
한 번 측정한 `anchor_sys_high32`와 `anchor_cpu`를 이용해 RMARKER 기준 ns로
환산한다. 따라서 per-event SYS_TIME read가 다음 단계 관측을 지연시키지는
않지만, `status_poll_transaction_width` 범위의 폴링 불확실성은 남는다.
DW3000의 `SYS_TIME` read latch는 anchor 측정 전에 `SYS_STATUS=0` SPI write로
해제한다. 이 write는 W1C 상태 비트를 지우지 않는다.

현재 코드 기준 해석 모델은 다음과 같다.

- Preamble 32 symbols: 32,567 ns
- SFD 8 symbols: 8,142 ns
- SHR 합계: 40,709 ns
- PHR: 24,706 ns
- PSDU 127 bytes: 149,412 ns
- PHR+PSDU: 174,118 ns
- 슬롯 예약에 사용되는 정수 올림값: 175 us

따라서 이 실험은 RMARKER는 정밀 기준으로 사용하지만, RXFR과 중간
수신 단계는 소프트웨어 관측값으로 해석해야 한다.
