# Aggregated ACKs UWB MAC Protocol

## 개요

이 문서는 `ex_29a_dwt_init_aggregated_acks`와 `ex_29b_dwt_normal_aggregated_acks` 두 코드의 동작 방식을 설명합니다. 두 코드는 쌍으로 동작하며, TDMA 기반 UWB MAC 프로토콜을 구현합니다.

---

## 프로토콜 구조

### 기본 파라미터

| 파라미터 | 값 | 설명 |
|---------|-----|------|
| Period 주기 | 21.2ms | 한 Period의 전체 길이 |
| 슬롯 간격 | 1.1ms | 슬롯 간 시간 (0.1ms TX + 1.0ms guard) |
| 총 슬롯 수 | 12개 | INIT + 7개 Normal + FL + FL_RELAY + FR + FR_RELAY |
| Periods/Cycle | 6개 | 3개의 DATA/ACK_ARRAY 쌍 |
| Config 전환 시점 | 17.2ms | SYNC 수신 준비를 위한 설정 전환 |

### Cycle 및 Period 구조

```
Cycle (6 Periods = 127.2ms)
├── Pair 1
│   ├── Period 1 (홀수): DATA 전송/수신
│   └── Period 2 (짝수): ACK_ARRAY 전송/수신
├── Pair 2
│   ├── Period 3 (홀수): DATA 재전송/수신
│   └── Period 4 (짝수): ACK_ARRAY 전송/수신
└── Pair 3
    ├── Period 5 (홀수): DATA 재전송/수신
    └── Period 6 (짝수): ACK_ARRAY 전송/수신
```

### TDMA 슬롯 할당 (21.2ms Period 내)

| SEQ | 시작 시간 | 노드 | 설명 |
|-----|----------|------|------|
| 1 | 2.0ms | INIT | Initiator 자체 DATA/ACK_ARRAY |
| 2 | 1.1ms | NODE_2 | Normal 노드 2 |
| 3 | 2.2ms | NODE_3 | Normal 노드 3 |
| 4 | 3.3ms | NODE_4 | Normal 노드 4 |
| 5 | 4.4ms | NODE_5 | Normal 노드 5 |
| 6 | 5.5ms | NODE_6 | Normal 노드 6 |
| 7 | 6.6ms | NODE_7 | Normal 노드 7 |
| 8 | 7.7ms | NODE_8 | Normal 노드 8 |
| 9 | 8.8ms | FL | Front Left 직접 전송 |
| 10 | 12.4ms | INIT | FL 릴레이 (by INIT) |
| 11 | 11.0ms | FR | Front Right 직접 전송 |
| 12 | 15.1ms | INIT | FR 릴레이 (by INIT) |
| - | 17.2ms | - | SYNC Config 전환 |
| - | 21.2ms | INIT | 다음 Period SYNC 전송 |

---

## 메시지 타입

| 타입 | 값 | 용도 |
|------|-----|------|
| MSG_TYPE_SYNC | 0x01 | 네트워크 동기화 (INIT만 전송) |
| MSG_TYPE_DATA | 0x02 | 일반 데이터 전송 |
| MSG_TYPE_ACK_ARRAY | 0x07 | 집합적 ACK 응답 |
| MSG_TYPE_RELAY_DATA | 0x05 | 릴레이된 FL/FR 데이터 |

### 메시지 프레임 구조

```
Index   Name            Size    Description
0       FTYPE           1       프레임 타입 (0xC5)
1       SEQ             1       시퀀스 번호
2       SOURCE          1       송신 노드 ID
3       DEST            1       목적지 노드 ID
4       MSG_TYPE        1       메시지 타입
5       PRIORITY        1       우선순위
6       PERIOD_INFO     1       Period 번호 (SYNC only) / ORIG_SRC (RELAY)
7       ORIG_DST        1       원본 목적지 (RELAY only)
8-11    TX_TIMESTAMP    4       TX 타임스탬프 (DATA only)
8-19    ACK_ARRAY       12      ACK 배열 (ACK_ARRAY only)
```

---

## INIT 노드 동작 (ex_29a)

### 역할
- **네트워크 마스터**: SYNC 브로드캐스트로 전체 네트워크 동기화
- **릴레이 노드**: FL/FR 노드의 데이터를 일반 노드들에게 릴레이
- **3-슬롯 관리**: SEQ 1 (자신), SEQ 10 (FL 릴레이), SEQ 12 (FR 릴레이)

### 메인 루프 흐름

```
┌─────────────────────────────────────────────────────────────┐
│                    INIT 노드 메인 루프                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  [A] SYNC 전송 체크 (21.2ms Period Timer)                   │
│      ├── 첫 실행 또는 21.2ms 경과 시                          │
│      ├── Period 홀수면 data_received_from[] 초기화            │
│      ├── SYNC 설정(PLEN512)으로 전환                          │
│      ├── SYNC 브로드캐스트 전송                               │
│      └── DATA 설정(PLEN64)으로 전환 후 RX 재활성화             │
│                                                             │
│  [B] Config 전환 (17.2ms 시점)                               │
│      └── SYNC 수신 준비를 위해 PLEN512로 전환                  │
│                                                             │
│  [C] 3-Slot TX 처리                                         │
│      ├── [C-1] SEQ 1  (2.0ms): 자신의 DATA/ACK_ARRAY         │
│      ├── [C-2] SEQ 10 (12.4ms): FL 릴레이                    │
│      └── [C-3] SEQ 12 (15.1ms): FR 릴레이                    │
│                                                             │
│  [D] RX 폴링: 메시지 수신 처리                                 │
│      ├── [D-2] Period 홀수: DATA 수신 → data_received_from[] │
│      ├── [D-3] Period 짝수: ACK_ARRAY → ACK 확인              │
│      └── [D-4] FL/FR DATA → 릴레이 버퍼 저장                  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### SEQ 1: 자신의 데이터 슬롯

**Period 홀수 (1, 3, 5) - DATA 전송:**
```c
if (period_in_cycle == 1) {
    // Period 1: 새 DATA 프레임 생성
    // retrans_msg에 저장 (재전송용)
} else {
    // Period 3, 5: retrans_msg에서 복사하여 재전송
}
```

**Period 짝수 (2, 4, 6) - ACK_ARRAY 전송:**
```c
// data_received_from[] 배열을 페이로드로 전송
memcpy(&tx_msg[IDX_ACK_ARRAY], data_received_from, TOTAL_ARRAY_SIZE);
```

### FL/FR 릴레이 동작

```
FL 노드 → INIT (SEQ 9에서 직접 수신)
                ↓
         fl_relay_msg에 저장
         MSG_TYPE = RELAY_DATA로 변환
         SOURCE = INIT (릴레이어)
         ORIG_SRC = FL (원본 송신자)
                ↓
INIT → Normal 노드들 (SEQ 10에서 릴레이)
```

### ACK 확인 로직

INIT는 3개의 TX에 대해 독립적으로 ACK를 추적:

| TX 슬롯 | 기대 ACK 노드 | 필요 ACK 수 |
|---------|--------------|------------|
| SLOT_IDX_INIT (0) | NODE_4, NODE_5, FL, FR | 4개 |
| SLOT_IDX_FL_RELAY (9) | NODE_4, NODE_5 | 2개 |
| SLOT_IDX_FR_RELAY (11) | NODE_4, NODE_5 | 2개 |

---

## Normal 노드 동작 (ex_29b)

### 역할
- **SYNC 수신**: INIT로부터 SYNC를 수신하여 타이밍 동기화
- **DATA/ACK_ARRAY 교환**: Period 홀수에 DATA, Period 짝수에 ACK_ARRAY 전송
- **수신 확인 응답**: 받은 DATA에 대해 ACK_ARRAY로 응답

### 메인 루프 흐름

```
┌─────────────────────────────────────────────────────────────┐
│                  Normal 노드 메인 루프                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  [A] SYNC Loss Detection (27ms 타임아웃)                     │
│      └── 27ms 동안 SYNC 없으면 TX 중단                        │
│                                                             │
│  [B] SYNC Timeout 및 최종 통계 (5초)                          │
│      └── 5초간 SYNC 없으면 테스트 종료 및 통계 출력            │
│                                                             │
│  [C] Config 전환 (17.2ms 시점)                               │
│      └── SYNC 수신 준비를 위해 PLEN512로 전환                  │
│                                                             │
│  [D] 슬롯 타이밍 체크 및 TX                                    │
│      ├── [D-1] SYNC Loss 시 TX 스킵                          │
│      ├── [D-2] Period 홀수: DATA TX                          │
│      └── [D-3] Period 짝수: ACK_ARRAY TX                     │
│                                                             │
│  [E] RX 폴링: 메시지 수신 처리                                 │
│      ├── [E-1] SYNC: 타이밍 기준점 설정                       │
│      ├── [E-2] Period 홀수 DATA: data_received_from[] 업데이트│
│      └── [E-3] Period 짝수 ACK_ARRAY: ACK 확인                │
│                                                             │
│  [F] RX 타임아웃/에러 처리                                    │
│      └── 상태 클리어 후 RX 재활성화                            │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### SYNC 수신 처리

```c
// SYNC 수신 시 동작
last_sync_cycles = current_cycles;      // 타이밍 기준점 설정
current_period_in_cycle = sync_period;  // Period 정보 직접 신뢰
slot_executed_this_sync = false;        // 슬롯 실행 플래그 초기화

// SYNC Loss 복구
if (sync_lost) {
    sync_lost = false;  // TX 재개
}

// Period 홀수 시작 시
if (current_period_in_cycle % 2 == 1) {
    memset(data_received_from, 0, sizeof(data_received_from));
}

// DATA 설정(PLEN64)으로 전환
dwt_configure(&config_data);
```

### 슬롯 타이밍 계산

```c
// 슬롯 시작 시간 = 2ms 버퍼 + (SEQ-1) * 1.1ms
slot_interval_cycles = us_to_cpu_cycles(2000 + (MY_NODE_SEQ-1) * 1100);
```

### DATA 전송 (Period 홀수)

```c
if (current_period_in_cycle == 1) {
    // Period 1: 새 DATA 생성
    tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
    tx_msg[IDX_SOURCE] = MY_NODE_ID;
    // TX timestamp 추가
    uint32_t tx_timestamp_us = (cycles - last_sync_cycles) / 64;
    memcpy(&tx_msg[IDX_TX_TIMESTAMP], &tx_timestamp_us, 4);
    // 재전송용 저장
    memcpy(retrans_msg, tx_msg, sizeof(tx_msg));
} else {
    // Period 3, 5: 재전송
    memcpy(tx_msg, retrans_msg, sizeof(tx_msg));
}
```

### ACK_ARRAY 전송 (Period 짝수)

```c
tx_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK_ARRAY;
// data_received_from[] 배열을 페이로드에 복사
memcpy(&tx_msg[IDX_ACK_ARRAY], data_received_from, TOTAL_ARRAY_SIZE);
```

### ACK 확인 및 성공 판정

```c
// ACK_ARRAY 수신 시
uint8_t *ack_array = &rx_buffer[IDX_ACK_ARRAY];

// 내 슬롯에 대한 ACK 확인
if (ack_array[my_slot_idx] == 1) {
    cumulative_ack_confirmed[src_slot_idx] = 1;
    cumulative_ack_count++;

    if (cumulative_ack_count >= TEST_EXPECTED_ACKS) {
        success_in_current_cycle = true;
        tx_state = TX_STATE_IDLE;  // 남은 Period 동안 TX 스킵
    }
}
```

---

## 릴레이 시스템

### FL/FR 노드의 특수성

FL(Front Left)과 FR(Front Right) 노드는 일반 노드들과 직접 통신이 어려운 위치에 있다고 가정합니다:

```
                    ┌─────────┐
         직접통신   │  INIT   │   직접통신
    ┌──────────────►│ (Relay) │◄──────────────┐
    │               └────┬────┘               │
    │                    │                    │
    ▼                    │ 릴레이             ▼
┌──────┐                 ▼              ┌──────┐
│  FL  │            ┌─────────┐         │  FR  │
└──────┘            │ Normal  │         └──────┘
    ▲               │ Nodes   │              ▲
    │               │ (2-8)   │              │
    │               └─────────┘              │
    │                    │                   │
    └────────────────────┴───────────────────┘
              직접통신 불가 (릴레이 필요)
```

### 릴레이 메시지 변환

```c
// FL DATA 수신 시 변환
fl_relay_msg[IDX_MSG_TYPE] = MSG_TYPE_RELAY_DATA;  // DATA → RELAY_DATA
fl_relay_msg[IDX_SOURCE] = NODE_INIT;              // 송신자 = INIT
fl_relay_msg[IDX_ORIG_SRC] = NODE_FL;              // 원본 송신자 보존
```

### 릴레이 필터링 (Normal 노드)

```c
// Normal 노드는 FL/FR 직접 DATA 무시 (릴레이만 처리)
if (src_node == NODE_FL || src_node == NODE_FR) {
    should_process = false;
}
```

---

## 설정 전환 타이밍

### 두 가지 PHY 설정

| 설정 | Preamble | 용도 |
|------|----------|------|
| config_sync | PLEN512 | SYNC 송수신 (긴 프리앰블 = 높은 신뢰성) |
| config_data | PLEN64 | DATA/ACK_ARRAY (짧은 프리앰블 = 빠른 전송) |

### 전환 시점

```
0ms      2ms                              17.2ms    21.2ms
│        │                                │         │
│ SYNC   │◄─── DATA/ACK_ARRAY 송수신 ───► │◄─ SYNC ─►│ 다음 SYNC
│ TX     │       (config_data)            │ 버퍼     │
▼        ▼                                ▼         ▼
┌────────┬────────────────────────────────┬─────────┐
│PLEN512 │           PLEN64               │ PLEN512 │
└────────┴────────────────────────────────┴─────────┘
```

---

## 재전송 메커니즘

### 3회 재전송 기회

```
Cycle
├── Pair 1 (Period 1-2)
│   ├── Period 1: 첫 DATA 전송
│   └── Period 2: ACK_ARRAY 수신 → 성공 여부 확인
│
├── Pair 2 (Period 3-4) ─ 실패 시 재전송
│   ├── Period 3: 동일 DATA 재전송 (retrans_msg 사용)
│   └── Period 4: ACK_ARRAY 수신 → 성공 여부 확인
│
└── Pair 3 (Period 5-6) ─ 실패 시 재전송
    ├── Period 5: 동일 DATA 재전송
    └── Period 6: ACK_ARRAY 수신 → 성공 여부 확인
```

### 조기 성공 시 IDLE

한 Pair에서 모든 ACK를 수신하면:
```c
if (cumulative_ack_count >= expected_nodes) {
    tx_state = TX_STATE_IDLE;      // 전송 상태 IDLE로 변경
    success_in_current_cycle = true;
}
```
→ 이후 Period에서는 DATA 전송 스킵 (ACK_ARRAY는 계속 전송)

---

## 지연시간 측정

### 단방향 지연시간 계산

```c
// TX 측: SYNC 이후 경과 시간을 타임스탬프로 기록
uint32_t tx_timestamp_us = (cycles - last_sync_cycles) / 64;

// RX 측: 수신 시점 계산
uint32_t rx_timestamp_us = (cycles - last_sync_cycles) / 64;

// 단방향 지연시간
uint32_t oneway_delay_us = rx_timestamp_us - tx_timestamp_us;
```

### 노드별 통계 추적

```c
typedef struct {
    uint32_t min_us;   // 최소 지연시간
    uint32_t max_us;   // 최대 지연시간
    uint64_t sum_us;   // 합계 (평균 계산용)
    uint32_t count;    // 샘플 수
} latency_stats_t;

static latency_stats_t node_latency[TOTAL_ARRAY_SIZE];
```

---

## 통계 수집

### Pair별 통계 (각 Cycle당 3개 Pair)

```c
// Period 2 종료 시 Pair 1 평가
if (success) pair1_success++;
else pair1_fail++;

// Period 4 종료 시 Pair 2 평가
// Period 6 종료 시 Pair 3 평가
```

### Cycle별 통계

| 지표 | 설명 |
|------|------|
| successful_cycles | 어느 Pair에서든 성공한 Cycle 수 |
| failed_cycles | 모든 Pair에서 실패한 Cycle 수 |
| perfect_cycles (INIT) | DATA + FL_RELAY + FR_RELAY 모두 성공 |

---

## 노드 ID 매핑

| 노드 ID | 문자 | 슬롯 인덱스 | SEQ |
|---------|------|------------|-----|
| NODE_INIT | '1' | 0 | 1 |
| NODE_2 | '2' | 1 | 2 |
| NODE_3 | '3' | 2 | 3 |
| NODE_4 | '4' | 3 | 4 |
| NODE_5 | '5' | 4 | 5 |
| NODE_6 | '6' | 5 | 6 |
| NODE_7 | '7' | 6 | 7 |
| NODE_8 | '8' | 7 | 8 |
| NODE_FL | '9' | 8 | 9 |
| FL_RELAY | - | 9 | 10 |
| NODE_FR | 'A' | 10 | 11 |
| FR_RELAY | - | 11 | 12 |

---

## 테스트 모드 설정

### Normal 노드 (ex_29b)

```c
#define TEST_MODE 1
#define TEST_TOTAL_NODES 5  // INIT, NODE_2, NODE_8, FL, FR

// 테스트할 노드 선택 (하나만 주석 해제)
#define TEST_NODE_2
//#define TEST_NODE_FL
//#define TEST_NODE_FR
```

### ACK 기대값

| 노드 타입 | 기대 ACK 수 | 기대 ACK 송신자 |
|----------|------------|----------------|
| Normal (2-8) | 2 | INIT + 다른 Normal 노드들 |
| FL | 2 | INIT + FR |
| FR | 2 | INIT + FL |

---

## 실행 순서 요약

```
1. DW3000 초기화 및 설정
2. DWT 사이클 카운터 초기화
3. RX 활성화

[메인 루프]
4. SYNC TX (INIT) / SYNC RX (Normal)
   - Period 번호 포함
   - DATA 설정으로 전환

5. 자신의 슬롯 타이밍에 TX
   - Period 홀수: DATA
   - Period 짝수: ACK_ARRAY

6. RX 폴링으로 메시지 수신
   - Period 홀수: DATA 수신 → data_received_from[] 업데이트
   - Period 짝수: ACK_ARRAY 수신 → 성공 확인

7. 17.2ms에 SYNC 설정으로 전환

8. 21.2ms에 다음 Period 시작 (4번으로)

[테스트 종료]
9. 1000 Cycle 완료 또는 5초 SYNC 타임아웃
10. 최종 통계 출력
```

---

## 파일 구조

```
examples/
├── ex_29a_dwt_init_aggregated_acks/
│   └── init_aggregated_acks.c        # INIT 노드 구현
├── ex_29b_dwt_normal_aggregated_acks/
│   └── normal_aggregated_acks.c      # Normal 노드 구현
└── AGGREGATED_ACKS_PROTOCOL.md       # 이 문서
```
