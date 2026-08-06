# Preamble Code Hopping Protocol

## 1. 개요

Aggregated ACKs MAC 프로토콜 위에 **BLE CSA#2 기반 Preamble Code Hopping**을 추가한 프로토콜.
동일 채널(Channel 9)에서 Period마다 Preamble Code를 변경하여 타 차량/간섭원과의 충돌을 확률적으로 회피한다.

### 관련 파일

| 파일 | 경로 | 역할 |
|------|------|------|
| `init_preamble_hop.c` | `ex_33a_MAC_PREAMBLE_CODE_HOPPING/` | INIT 노드 (SYNC 송신, 네트워크 동기화) |
| `normal_preamble_hop.c` | `ex_33a_MAC_PREAMBLE_CODE_HOPPING/` | Normal 노드 (SYNC 수신, TDMA 슬롯 통신) |
| `interference_preamble_hop.c` | `ex_34_INTERFERENCE/` | 간섭원 (다른 VEHICLE_AA, 모든 슬롯 전송) |
| `example_selection.h` | `Src/` | 테스트 선택 (`TEST_INIT_PREAMBLE_HOP` 등) |

### Baseline 대비 차이점

| 항목 | Baseline (`ex_29a/29b`) | Preamble Hopping (`ex_33a`) |
|------|------------------------|----------------------------|
| Preamble Code | 고정 (9) | Period마다 변경 (BLE CSA#2) |
| SYNC 메시지 | Period 정보만 포함 | Current/Next Preamble Code 포함 |
| Config 전환 | SYNC↔DATA (PLEN만) | SYNC↔DATA (PLEN + Preamble Code) |
| VEHICLE_AA | 없음 | 0x12345678U (차량 고유 ID) |

---

## 2. Preamble Code Hopping 알고리즘

### 2.1 BLE CSA#2 (Channel Selection Algorithm #2) 기반

Bluetooth 5.0의 채널 선택 알고리즘을 Preamble Code 선택에 적용.

**입력:**
- `counter`: Global Period Counter (매 Period마다 1씩 증가)
- `channel_id`: VEHICLE_AA에서 파생된 16-bit ID

**파생:**
```
VEHICLE_AA = 0x12345678
CHANNEL_ID = AA[31:16] XOR AA[15:0] = 0x1234 XOR 0x5678 = 0x444C
```

### 2.2 PRNG 구조 (generate_prn_e)

```
x = counter XOR channel_id

// 3회 반복
x = PERM(x)      // 상위/하위 바이트 각각 bit-reverse
x = MAM(x, cid)  // (17 * x + cid) mod 2^16

result = x XOR channel_id
```

**세부 함수:**

```c
// Bit-Reverse (8-bit)
reverse8(x):
    x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1)
    x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2)
    x = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4)
    return x

// Permutation (16-bit)
PERM(x) = (reverse8(x >> 8) << 8) | reverse8(x & 0xFF)

// Multiply-Add-Mod
MAM(a, b) = (17 * a + b) mod 2^16
```

### 2.3 Preamble Code 매핑

```c
// Channel 9, PRF 64MHz에서 유효한 12개 Preamble Code
VALID_PREAMBLE_CODES[12] = {9, 10, 11, 12, 13, 14, 15, 16, 21, 22, 23, 24}

// 선택
prn_e = generate_prn_e(counter, CHANNEL_ID)
preamble_code = VALID_PREAMBLE_CODES[prn_e % 12]
```

### 2.4 Preamble 시퀀스 예시

```
VEHICLE_AA = 0x12345678:
  Period 0: 23, Period 1: 23, Period 2: 15, Period 3: 11
  Period 4: 22, Period 5: 10, Period 6: 22, Period 7: 14, ...

VEHICLE_AA = 0xAABBCCDD (간섭원):
  → 완전히 다른 시퀀스 생성 → 확률적으로 1/12 충돌
```

---

## 3. 프로토콜 구조

### 3.1 시간 구조

```
┌─────────────────────── 1 Cycle = 6 Periods (127.2ms) ───────────────────────┐
│ Period 1  │ Period 2  │ Period 3  │ Period 4  │ Period 5  │ Period 6        │
│ (DATA)    │ (ACK)     │ (DATA)    │ (ACK)     │ (DATA)    │ (ACK)           │
│  21.2ms   │  21.2ms   │  21.2ms   │  21.2ms   │  21.2ms   │  21.2ms         │
│ Pair 1 ──────────────│ Pair 2 ──────────────│ Pair 3 ──────────────────────│
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Period 내부 구조 (21.2ms)

```
0ms      2ms                                           17.2ms    21.2ms
├────────┼───┬───┬───┬───┬───┬───┬───┬───┬───┬────┬────┼─────────┤
│ SYNC   │S1 │S2 │S3 │S4 │S5 │S6 │S7 │S8 │S9 │S10│S11│S12│Buffer│
│(PLEN512)│   │   │   │   │   │   │   │   │   │   │   │   │      │
├────────┼───┴───┴───┴───┴───┴───┴───┴───┴───┴────┴────┼─────────┤
│        │  DATA/ACK 슬롯 (PLEN64, 각 1.1ms)           │ → SYNC  │
│        │  각 슬롯: 0.1ms TX + 1.0ms Guard             │ Config  │
└────────┴──────────────────────────────────────────────┴─────────┘
```

**슬롯 타이밍 (SYNC 기준):**

| 슬롯 | 시작 시각 | 노드 | 비고 |
|------|----------|------|------|
| SEQ 1 | 2.0ms | INIT | 자체 DATA/ACK |
| SEQ 2 | 3.1ms | Node 2 | |
| SEQ 3 | 4.2ms | Node 3 | |
| SEQ 4 | 5.3ms | Node 4 | |
| SEQ 5 | 6.4ms | Node 5 | |
| SEQ 6 | 7.5ms | Node 6 | |
| SEQ 7 | 8.6ms | Node 7 | |
| SEQ 8 | 9.7ms | Node 8 | |
| SEQ 9 | 10.8ms | FL (직접) | |
| SEQ 10 | 12.4ms | INIT FL 릴레이 | |
| SEQ 11 | 13.5ms | FR (직접) | |
| SEQ 12 | 15.1ms | INIT FR 릴레이 | |
| - | 17.2ms | Config Switch | PLEN64 → PLEN512 |
| - | 21.2ms | 다음 Period SYNC | |

### 3.3 Preamble Code 적용 흐름

```
Period N 시작:
  ┌──────────────────────────────────────────────────────────────┐
  │ 1. INIT: current = get_preamble(global_counter)             │
  │          next    = get_preamble(global_counter + 1)         │
  │                                                              │
  │ 2. SYNC TX: config_sync.txCode = current                    │
  │             msg[IDX_CURRENT_PREAMBLE] = current              │
  │             msg[IDX_NEXT_PREAMBLE]    = next                 │
  │                                                              │
  │ 3. global_counter++                                          │
  │                                                              │
  │ 4. DATA config: config_data.txCode = current                 │
  │                 config_data.rxCode = current                 │
  │                                                              │
  │ 5. Normal Node: SYNC 수신 → current/next 추출               │
  │                 config_data.txCode = current                 │
  │                 config_data.rxCode = current                 │
  │                                                              │
  │ 6. 17.2ms: INIT/Normal 모두 config_sync로 전환              │
  │            config_sync.rxCode = next (다음 SYNC 수신용)      │
  └──────────────────────────────────────────────────────────────┘
```

**핵심 순서:**
1. `get_preamble_code_for_period(global_period_counter)` 호출 (current 계산)
2. `get_preamble_code_for_period(global_period_counter + 1)` 호출 (next 계산)
3. SYNC 메시지에 current, next 포함하여 TX
4. **이후** `global_period_counter++` (다음 Period를 위해)

> 순서 중요: counter 증가는 반드시 preamble 계산 **이후**에 해야 INIT과 Normal의 preamble이 일치함.

---

## 4. INIT 노드 (init_preamble_hop.c) 동작

### 4.1 메인 루프 구조

```
while (1) {
    [A] SYNC 전송 (21.2ms Period Timer)
        → Preamble 계산 → Config Switch → SYNC TX → DATA Config → RX Enable

    [B] Config Switch (17.2ms)
        → DATA(PLEN64) → SYNC(PLEN512) 전환 (다음 Period SYNC 준비)

    [C] 3-Slot TX: SEQ 1, SEQ 10, SEQ 12
        → SEQ 1:  자체 DATA (홀수 Period) / ACK_ARRAY (짝수 Period)
        → SEQ 10: FL 릴레이 (has_fl_data일 때만)
        → SEQ 12: FR 릴레이 (has_fr_data일 때만)

    [D] RX 폴링
        → 홀수 Period: DATA 수신 → data_received_from[] 기록
        → 짝수 Period: ACK_ARRAY 수신 → ACK 확인 → 성공 판정
        → FL/FR DATA: 릴레이용 버퍼에 저장
}
```

### 4.2 SYNC 전송 상세

```c
// [A] Period timer 발동 시:
current_preamble = get_preamble_code_for_period(global_period_counter);
next_preamble    = get_preamble_code_for_period(global_period_counter + 1);

// Config 설정
config_data.txCode = current_preamble;
config_data.rxCode = current_preamble;
config_sync.txCode = current_preamble;
config_sync.rxCode = current_preamble;

// SYNC TX
dwt_configure(&config_sync);       // PLEN512 + current preamble
dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
waitforsysstatus(...);             // TX 완료 대기

// Counter 증가
global_period_counter++;

// DATA Config으로 전환
dwt_configure(&config_data);       // PLEN64 + current preamble
dwt_rxenable(DWT_START_RX_IMMEDIATE);
```

### 4.3 성공 판정 로직

```
Cycle 내 3회 시도 (Period 1/2, 3/4, 5/6):
  - Period 1: DATA 첫 전송 + N5 DATA 수신 대기
  - Period 2: ACK_ARRAY 전송 (수신한 DATA 목록) + N5 ACK_ARRAY 수신 확인
  - Period 3: DATA 재전송 (미성공 시) + N5 DATA 수신 대기
  - ...

DATA Success 조건:
  - INIT의 ACK_ARRAY에 N5 비트가 포함됨 (INIT이 N5의 DATA를 수신함)
  - N5의 ACK_ARRAY에 INIT 비트가 포함됨 (N5가 INIT의 DATA를 수신함)
  - 양방향 모두 확인되어야 성공
```

---

## 5. Normal 노드 (normal_preamble_hop.c) 동작

### 5.1 Preamble 동기화

Normal 노드는 자체적으로 Preamble을 계산하지 않고, **INIT의 SYNC 메시지에서 추출**한다.

```c
// SYNC 수신 시:
uint8_t current_preamble = rx_buffer[IDX_CURRENT_PREAMBLE];  // 이번 Period DATA/ACK용
uint8_t next_preamble    = rx_buffer[IDX_NEXT_PREAMBLE];     // 다음 Period SYNC 수신용

// DATA Config 적용
config_data.txCode = current_preamble;
config_data.rxCode = current_preamble;
dwt_configure(&config_data);

// next_preamble 저장 → 17.2ms에 config_sync에 적용
next_sync_preamble = next_preamble;
```

### 5.2 17.2ms Config Switch

```c
// 17.2ms 시점:
config_sync.txCode = next_sync_preamble;
config_sync.rxCode = next_sync_preamble;
dwt_configure(&config_sync);   // PLEN512 + next preamble
dwt_rxenable(DWT_START_RX_IMMEDIATE);
// → 다음 SYNC를 next preamble로 수신 대기
```

### 5.3 초기 SYNC 수신 (첫 부팅)

Normal 노드는 첫 부팅 시 어떤 preamble을 사용해야 할지 모르므로:
```c
// 초기화 시:
uint8_t initial_preamble = get_preamble_code_for_period(0);
next_sync_preamble = initial_preamble;
config_sync.txCode = initial_preamble;
config_sync.rxCode = initial_preamble;
```

> INIT도 global_period_counter=0에서 시작하므로, 첫 SYNC는 동일한 preamble 사용.
> 이후에는 SYNC 메시지의 next_preamble로 동기화.

---

## 6. 간섭원 (interference_preamble_hop.c) 설계

### 6.1 설계 원칙

| 항목 | 프로토콜 | 간섭원 |
|------|---------|--------|
| VEHICLE_AA | 0x12345678U | 0xAABBCCDDU |
| Preamble 시퀀스 | 프로토콜 고유 | 간섭원 고유 (다른 시퀀스) |
| 전송 슬롯 | SEQ 1, 10, 12 (INIT) | **모든 12개 슬롯** |
| RX | DATA/ACK 수신 | 없음 (TX only) |
| 통계 | 성공률 추적 | 없음 |

### 6.2 충돌 확률

12개 유효 Preamble Code에서 독립적 균일 선택 시:
- **Period당 충돌 확률**: 1/12 ≈ 8.3%
- 간섭원이 12개 슬롯 모두 전송 → 프로토콜의 어떤 슬롯이든 8.3% 확률로 간섭
- 실제로는 PRNG 시퀀스의 상관관계에 따라 약간 다를 수 있음

### 6.3 타이밍

간섭원은 프로토콜과 동일한 21.2ms Period, 1.1ms 슬롯 타이밍 사용.
DWT Cycle Counter 기반으로 정밀한 타이밍 보장.

```
간섭원 Period 구조:
  SYNC (PLEN512) → SEQ1 ~ SEQ12 (PLEN64, 모든 슬롯) → Config Switch → 반복
```

---

## 7. 메시지 프레임 구조

### 7.1 공통 헤더 (22 bytes + 2 CRC)

```
Byte  0: Frame Type (0xC5 = DATA/ACK, 0x41 = default)
Byte  1: Sequence Number (auto-increment)
Byte  2: Source ID ('1'=INIT, '2'~'8'=Normal, '9'=FL, 'A'=FR)
Byte  3: Destination ID ('B'=Broadcast)
Byte  4: Message Type (0x01=SYNC, 0x02=DATA, 0x07=ACK_ARRAY, ...)
Byte  5: Priority
Byte  6: Original Source (relay용) / Period Info (SYNC용)
Byte  7: Original Dest (relay용)
```

### 7.2 SYNC 메시지 (Preamble Hopping 전용)

```
Byte  8: IDX_CURRENT_PREAMBLE - 현재 Period의 DATA/ACK Preamble Code
Byte  9: IDX_NEXT_PREAMBLE    - 다음 Period의 SYNC Preamble Code
```

### 7.3 DATA 메시지

```
Byte  8~11: TX Timestamp (SYNC 기준 마이크로초, 4 bytes)
Byte 12~:   Data Payload
```

### 7.4 ACK_ARRAY 메시지

```
Byte  8~19: ACK Array (12 bytes, 슬롯별 1 byte)
            [0]=INIT, [1]=N2, ..., [4]=N5, ..., [8]=FL, [9]=FL_REL, [10]=FR, [11]=FR_REL
            값 1 = 해당 슬롯의 DATA를 수신했음을 의미
```

---

## 8. UWB Radio Configuration

### 8.1 SYNC Config (PLEN512)

```c
config_sync = {
    .chan     = 9,
    .txPreambLength = DWT_PLEN_512,    // 긴 Preamble → 높은 수신 신뢰도
    .rxPAC   = DWT_PAC8,
    .txCode  = current_preamble,       // Period마다 변경
    .rxCode  = current_preamble,       // Period마다 변경
    .sfdType = 1,                      // Non-standard 8-symbol SFD
    .dataRate = DWT_BR_6M8,
    .sfdTO   = (512 + 1 + 8 - 8),     // SFD timeout
};
```

### 8.2 DATA Config (PLEN64)

```c
config_data = {
    .chan     = 9,
    .txPreambLength = DWT_PLEN_64,     // 짧은 Preamble → 빠른 전송
    .rxPAC   = DWT_PAC8,
    .txCode  = current_preamble,       // Period마다 변경
    .rxCode  = current_preamble,       // Period마다 변경
    .sfdType = 1,
    .dataRate = DWT_BR_6M8,
    .sfdTO   = (64 + 1 + 8 - 8),
};
```

### 8.3 TX Power

Linear TX Power API 사용, 인덱스 1당 -0.25dB:

| 인덱스 | 감쇄량 | 정의 |
|--------|--------|------|
| 0 | 0dB (최대) | `TX_POWER_INDEX_0dB` |
| 12 | -3dB | `TX_POWER_INDEX_3dB` |
| 24 | -6dB | `TX_POWER_INDEX_6dB` |
| 40 | -10dB | `TX_POWER_INDEX_10dB` |
| 80 | -20dB | `TX_POWER_INDEX_20dB` |

---

## 9. 타이밍 구현 (DWT Cycle Counter)

### 9.1 ARM Cortex-M4 DWT_CYCCNT

nRF52840의 CPU 클럭 (64MHz)으로 구동되는 32-bit 사이클 카운터 사용.

```c
// 카운터 활성화
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

// 현재 사이클 읽기
uint32_t cycles = DWT->CYCCNT;

// 변환 상수
#define CPU_FREQ_HZ   64000000
#define CYCLES_PER_US 64
#define CYCLES_PER_MS 64000
```

### 9.2 주요 타이밍 값

| 항목 | 시간 | 사이클 (64MHz) |
|------|------|---------------|
| Period Interval | 21.2ms | 1,356,800 |
| Slot Interval | 1.1ms | 70,400 |
| TX Duration | 0.1ms | 6,400 |
| SYNC Buffer | 2.0ms | 128,000 |
| Config Switch | 17.2ms | 1,100,800 |
| SEQ 5 Start (Normal) | 6.4ms (SYNC 기준) | 409,600 |

### 9.3 경과 시간 체크

```c
static inline bool dwt_timer_elapsed(uint32_t start, uint32_t duration) {
    return (DWT->CYCCNT - start) >= duration;
}
// 32-bit unsigned 뺄셈 → wrap-around 자동 처리
```

