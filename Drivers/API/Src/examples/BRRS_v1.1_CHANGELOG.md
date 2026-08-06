# BRRS v1.1 버그 수정 변경사항

## 개요

Delayed-RX 적용 후 PER=100% 발생 문제 수정.

---

## 증상

- INIT 노드: `rx=0 expected=1000 miss=1000 PER=100%`, `RX timeouts=6000`
- Normal 노드: `waiting for SYNC` 상태에서 진행 안 됨

---

## 근본 원인 분석

### 타이밍 흐름 (수정 전, SYNC_BUFFER_US=2000)

```
SYNC TX RMARKER (t=0)
    │
    ├─ +58us: TXFRS 인터럽트 발생
    │         last_sync_tx_ts_high32 = dwt_readtxtimestamphi32()  ← 타임스탬프 캡처
    │
    ├─ +수백us: [문제 1] 블로킹 디버그 프린트 (UART 전송)
    │           snprintf + test_run_info()  →  최대 수 ms 지연 가능
    │
    ├─ +수백~2000us: [문제 2] dwt_configure(&config_data) SPI 트랜잭션
    │                         (레지스터 수십 개 write/read)
    │
    └─ schedule_delayed_rx() 호출 시점: 이미 t > 2582us
           → dwt_rxenable(DWT_START_RX_DELAYED) → HPDWARN 반환
           → 예약 시각이 이미 지났음

Normal 노드의 TX: t = 2592us (MY_SLOT_START_US)
→ INIT은 이미 예약 실패 상태 → 패킷 수신 불가 → PER=100%
```

### 핵심 문제

`slot_offset_us = SYNC_BUFFER_US + SLOT_INTERVAL_US - RX_EARLY_US = 2582us`

이 값이 타임스탬프 캡처 이후 `schedule_delayed_rx()` 호출까지의 실제 코드 실행 시간보다 작았음.

---

## 수정 내용

### 수정 1: `SYNC_BUFFER_US` 증가 (양쪽 파일)

| 항목 | 수정 전 | 수정 후 |
|------|---------|---------|
| `SYNC_BUFFER_US` | 2000 us | 3000 us |
| `slot_offset_us` (INIT RX 예약) | 2582 us | 3582 us |
| `MY_SLOT_START_US` (Normal TX) | 2592 us | 3592 us |
| `PERIOD_US` | ~9184 us | ~10184 us |

**효과**: delayed-RX 예약 호출 전 코드 실행 예산이 2524us → 3524us로 증가.  
`dwt_configure()` SPI 트랜잭션(최대 ~2000us) + 기타 오버헤드를 충분히 수용.

**파일**:
- `ex_35a_brrs_init/brrs_init.c` line 73
- `ex_35b_brrs_normal/brrs_normal.c` line 97

---

### 수정 2: Critical Path에서 블로킹 디버그 프린트 제거 (INIT)

**파일**: `ex_35a_brrs_init/brrs_init.c`

**수정 전**:
```c
last_sync_tx_ts_high32 = dwt_readtxtimestamphi32();

// ↓ 이 블록이 타임스탬프 캡처와 schedule_delayed_rx() 사이에 있었음
if (period_count <= 5) {
    static char dbg[100];
    snprintf(dbg, sizeof(dbg), "DBG: SYNC TX OK p%d, ts_hi=0x%08lX",
             period_in_cycle, (unsigned long)last_sync_tx_ts_high32);
    test_run_info((unsigned char *)dbg);  // UART 블로킹 → 수백us ~ 수ms 지연
}
```

**수정 후**:
```c
last_sync_tx_ts_high32 = dwt_readtxtimestamphi32();
// 디버그 프린트 제거 - critical path에서 지연 없이 즉시 config 전환으로 진행
```

**이유**: `last_sync_tx_ts_high32` 캡처 직후부터 `schedule_delayed_rx()` 호출까지는  
DW3000 타임스탬프 기준의 예약 시각 이전에 완료되어야 하는 critical path임.  
UART 출력은 수백 us ~ 수 ms의 블로킹 지연을 유발하므로 이 구간에서 사용 불가.

---

### 수정 3: `schedule_delayed_rx()` Immediate RX 폴백 제거 (INIT)

**파일**: `ex_35a_brrs_init/brrs_init.c`

**수정 전**:
```c
int result = dwt_rxenable(DWT_START_RX_DELAYED);
if (result != DWT_SUCCESS) {
    dwt_forcetrxoff();
    dwt_rxenable(DWT_START_RX_IMMEDIATE);  // ← 설계 모순
}
```

**수정 후**:
```c
int result = dwt_rxenable(DWT_START_RX_DELAYED);
if (result != DWT_SUCCESS) {
    total_rx_delayed_fallbacks++;  // 진단 카운터만 증가
    dwt_forcetrxoff();
    // immediate RX 없음 - 해당 슬롯은 miss 처리
}
```

**이유**: 본 구현의 목표는 delayed-RX이며, immediate RX 폴백은 설계 모순임.

- delayed-RX 실패(HPDWARN) = 예약 시각이 이미 지남  
- 이 시점에 immediate RX를 켜도 Normal TX는 이미 지나갔거나 RX 윈도우 내에 들어오지 않음  
- 폴백을 유지하면 delayed-RX 실패 여부가 PER 통계에서 숨겨짐  
- 올바른 동작: 실패 시 해당 슬롯을 miss로 처리하고, `total_rx_delayed_fallbacks` 카운터로 원인 추적

**정상 동작 시**: 수정 1, 2에 의해 코드 실행 시간이 slot_offset_us 이내로 줄어들므로  
delayed-RX 실패 자체가 발생하지 않아야 함.

---

## 최종 통계 출력 변경

수정 후 INIT 노드 최종 통계에 다음 항목 추가:

```
RX timeouts=N  delayed fallbacks=M
```

| 값 | 의미 |
|----|------|
| `RX timeouts=0` | delayed-RX 윈도우 내 패킷 수신 성공 |
| `RX timeouts=N` | 윈도우는 열렸으나 패킷 미수신 (진짜 PER) |
| `delayed fallbacks=0` | delayed-RX 예약 항상 성공 (정상) |
| `delayed fallbacks=N` | 예약 시각 초과 발생 → SYNC_BUFFER_US 추가 증가 필요 |

---

## 타이밍 다이어그램 (수정 후, SYNC_BUFFER_US=3000, DATA_PLEN=DWT_PLEN_32)

```
INIT SYNC TX RMARKER (t=0)
│
├── t=58us:    TXFRS, last_sync_tx_ts_high32 캡처
├── t=~60us:   dwt_forcetrxoff()
├── t=~100us:  dwt_configure(&config_data)  [SPI, 최대 ~2000us]
├── t=~2100us: dwt_setrxaftertxdelay()
├── t=~2200us: schedule_delayed_rx(ts, 3582us) 호출
│              → dwt_setdelayedtrxtime(3582us)
│              → dwt_rxenable(DWT_START_RX_DELAYED)  ← 3582us > 2200us ✓
│
├── t=3582us:  INIT delayed-RX 윈도우 오픈  ← preamble hunting 시작
│
Normal SYNC RX RMARKER (≈ t=0, 전파지연 무시)
│
└── t=3592us:  Normal delayed-TX 발사  (MY_SLOT_START_US = 3000 + 592 = 3592us)
               ↑ INIT RX 윈도우 오픈(3582us) 기준 +10us (RX_EARLY_US 마진)

INIT RX 윈도우: 3582us ~ 3582+202us(RX_WINDOW_US) = 3784us
Normal TX 도착: 3592us  → 윈도우 내에 포함 ✓
```

---

## 변경된 파라미터 요약

### `ex_35a_brrs_init/brrs_init.c`

| 파라미터 | 수정 전 | 수정 후 | 비고 |
|---------|---------|---------|------|
| `SYNC_BUFFER_US` | 2000 | 3000 | slot_offset_us 확장 |
| critical path 디버그 프린트 | 있음 | 제거 | UART 블로킹 제거 |
| `schedule_delayed_rx()` 폴백 | immediate RX | 없음 (카운터만) | 설계 모순 제거 |
| `total_rx_delayed_fallbacks` | 없음 | 추가 | 진단용 |

### `ex_35b_brrs_normal/brrs_normal.c`

| 파라미터 | 수정 전 | 수정 후 | 비고 |
|---------|---------|---------|------|
| `SYNC_BUFFER_US` | 2000 | 3000 | INIT과 동기화 |
| `MY_SLOT_START_US` | 2592 | 3592 | 자동 계산됨 |
