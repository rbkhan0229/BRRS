# **Aggregated ACKs 프로토콜 - 종합 분석**

## **🎯 핵심 개념**

이것은 자동 재전송 및 릴레이 지원을 갖춘 신뢰성 있는 멀티 노드 통신을 위해 설계된 **TDMA 기반 UWB 통신 프로토콜 with Aggregated ACK Arrays** 입니다.

---

## **📊 네트워크 토폴로지**

**테스트 모드 구성 (5개 물리 노드, 12개 TDMA 슬롯):**
- **INIT** (SEQ 1, Slot 0): 코디네이터/개시자 - **3개의 전송 슬롯 보유**
- **NODE_2** (SEQ 2, Slot 1): 일반 노드
- **NODE_8** (SEQ 8, Slot 7): 일반 노드
- **FL** (SEQ 9, Slot 8): Front Left - **INIT을 통한 릴레이 필요**
- **FR** (SEQ 11, Slot 10): Front Right - **INIT을 통한 릴레이 필요**
- **FL_RELAY** (SEQ 10, Slot 9): INIT이 FL의 데이터 릴레이
- **FR_RELAY** (SEQ 12, Slot 11): INIT이 FR의 데이터 릴레이

**왜 릴레이가 필요한가?** FL과 FR 노드는 일반 노드와 직접 통신이 불안정합니다 (실제 환경의 거리/장애물 시뮬레이션). 따라서 INIT이 릴레이 브리지 역할을 합니다.

---

## **⏱️ 시간 구조**

```
Period: 21.2ms (신뢰성을 위한 1.0ms 가드 타임)
├─ Slot Interval: 1.1ms (0.1ms TX + 1.0ms guard)
├─ Config Switch: 17.2ms (SYNC 설정으로 전환, 4.0ms 버퍼)
└─ 총 12개 TDMA 슬롯

Cycle: 6개 period = 3개 period 쌍
├─ Pair 1: Period 1 + Period 2
├─ Pair 2: Period 3 + Period 4
└─ Pair 3: Period 5 + Period 6
```

**Period 쌍 = 재전송 기회:**
- **홀수 Period** (1, 3, 5): DATA 전송
- **짝수 Period** (2, 4, 6): ACK_ARRAY 브로드캐스트

어떤 쌍에서든 성공하면 → 사이클의 나머지 쌍에서는 **IDLE** 상태

---

## **🔄 프로토콜 흐름 (사이클당)**

### **Phase 1: SYNC (각 period 시작)**
1. **INIT**이 **SYNC** 비콘 전송 (신뢰성을 위해 PLEN512 사용)
2. 모든 노드가 타이밍 기준점 동기화
3. SYNC 설정 → DATA 설정으로 전환 (속도를 위해 PLEN64)

### **Phase 2: 홀수 Period (1, 3, 5) - DATA 전송**
1. 각 노드가 할당된 슬롯에서 전송:
   - **SEQ 1** (2ms): INIT 자신의 DATA
   - **SEQ 2** (3.1ms): NODE_2 DATA
   - **SEQ 8** (9.7ms): NODE_8 DATA
   - **SEQ 9** (10.8ms): FL 직접 DATA (INIT이 수신)
   - **SEQ 10** (12.4ms): INIT이 FL을 RELAY_DATA로 릴레이
   - **SEQ 11** (13.5ms): FR 직접 DATA (INIT이 수신)
   - **SEQ 12** (15.1ms): INIT이 FR을 RELAY_DATA로 릴레이

2. 모든 노드가 수신한 DATA를 `data_received_from[12]` 배열에 추적
   - Index 0 = INIT, Index 1 = NODE_2, Index 8 = FL, Index 9 = FL_RELAY, 등

### **Phase 3: 짝수 Period (2, 4, 6) - ACK_ARRAY 브로드캐스트**
1. 각 노드가 자신의 `data_received_from[]` 배열을 **ACK_ARRAY**로 브로드캐스트
2. **ACK_ARRAY 구조**: `IDX_ACK_ARRAY` (index 8-19)에 12바이트 페이로드
   ```c
   ack_array[0] = 1  // INIT의 DATA 수신함
   ack_array[1] = 1  // NODE_2의 DATA 수신함
   ack_array[8] = 0  // FL의 직접 DATA 수신 안함 (일반 노드는 FL 직접 무시)
   ack_array[9] = 1  // INIT으로부터 FL_RELAY 수신함
   ```

3. 각 노드가 수신한 ACK_ARRAY 메시지 확인:
   - `ack_array[my_slot_index]` 추출
   - `ack_array[my_slot_index] == 1`이면 → 해당 노드가 내 DATA를 수신함 ✅
   - 예상 노드들로부터의 고유 ACK 카운트
   - `cumulative_ack_count >= expected_acks`이면 → **성공** → **IDLE** 상태로 전환

### **Phase 4: 재전송 결정**
- **Period 1** (Pair 1): 첫 번째 TX - **새** 메시지 준비, `retrans_msg[]`에 저장
- **Period 2** (Pair 1): ACK 확인 → 성공하면 TX_STATE = IDLE
- **Period 3** (Pair 2): IDLE이 아니면 Period 1과 **동일한** 메시지 재전송
- **Period 4** (Pair 2): ACK 확인 → 성공하면 TX_STATE = IDLE
- **Period 5** (Pair 3): IDLE이 아니면 Period 1과 **동일한** 메시지 재전송
- **Period 6** (Pair 3): ACK 확인 → 사이클 성공 평가

---

## **🔑 핵심 혁신: Aggregated ACK Arrays**

**전통적인 ACK 문제:**
- 각 노드가 각 송신자에게 개별 ACK 전송 → N² 메시지
- 충돌 위험, 높은 오버헤드

**Aggregated ACK 솔루션:**
- 각 노드가 **모든** 슬롯에 대한 상태를 포함하는 **하나의** ACK_ARRAY 브로드캐스트
- 모든 노드가 동시에 모두의 ACK 상태를 수신
- 트래픽을 N²에서 N 브로드캐스트로 감소
- 멀티캐스트 효율성 구현

**예시:**
```
NODE_2가 ACK_ARRAY 브로드캐스트: [1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0]
                                    ↑        ↑           ↑     ↑
                                  INIT    (공백)       N8  FL_REL
```
이것은 **모든 노드에게** NODE_2가 INIT, NODE_8, FL_RELAY로부터 DATA를 수신했음을 알려줍니다.

---

## **🔄 INIT 노드 - 특별한 3-슬롯 동작**

INIT 노드는 **3개의 독립적인 전송 슬롯**을 가진 독특한 구조입니다:

| 슬롯 | SEQ | 시간 | 목적 | 예상 ACK |
|------|-----|------|------|----------|
| Own | SEQ 1 | 2ms | 자신의 DATA | 4개 (NODE_2, NODE_8, FL, FR) |
| FL Relay | SEQ 10 | 12.4ms | FL 데이터 릴레이 | 2개 (NODE_2, NODE_8) |
| FR Relay | SEQ 12 | 15.1ms | FR 데이터 릴레이 | 2개 (NODE_2, NODE_8) |

**각 슬롯은 다음을 가집니다:**
- 독립적인 성공 추적 (`data_success_in_current_cycle`, `fl_relay_success_in_current_cycle`, `fr_relay_success_in_current_cycle`)
- 독립적인 재전송 버퍼 (`retrans_msg[]`, `fl_retrans_msg[]`, `fr_retrans_msg[]`)
- 독립적인 실행 플래그 (`seq1_executed`, `seq10_executed`, `seq12_executed`)

**왜 3개 슬롯인가?**
- **SEQ 1**: INIT 자신의 데이터를 모든 노드에 전달
- **SEQ 10/12**: FL/FR을 일반 노드들과 연결 (릴레이 시스템)

---

## **🌉 릴레이 시스템 (FL/FR 통신)**

**문제:** FL과 FR 노드는 일반 노드와 직접 안정적으로 통신할 수 없습니다.

**솔루션:** INIT이 릴레이 브리지 역할을 합니다.

**흐름:**
1. **Period 1, 3, 5** (홀수 period):
   - **SEQ 9** (10.8ms): FL이 직접 DATA 전송
   - **INIT**이 수신하여 저장: `has_fl_data = true`, `fl_relay_msg[]`에 복사
   - **SEQ 10** (12.4ms): INIT이 `MSG_TYPE_RELAY_DATA`로 릴레이
   - **일반 노드들** (NODE_2, NODE_8)이 RELAY_DATA 수신
   - **FL**은 RELAY_DATA 무시 (이미 자신의 데이터를 가지고 있음)

2. **Period 2, 4, 6** (짝수 period):
   - **일반 노드들**이 `ack_array[9] = 1` (FL_RELAY 수신함)을 포함한 ACK_ARRAY 브로드캐스트
   - **INIT**이 `SLOT_IDX_FL_RELAY`에 대한 ACK를 별도로 카운트
   - `fl_relay_unique_ack_count >= 2`이면 → `fl_relay_completed = true`

**필터링 규칙:**
```c
// 일반 노드
if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA && (src_node == NODE_FL || src_node == NODE_FR)) {
    should_process = false;  // FL/FR의 직접 DATA 무시
}

// FL/FR 노드
if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_RELAY_DATA) {
    should_process = false;  // FL/FR은 릴레이된 버전 무시
}
```

---

## **📈 성공 기준 및 예상값**

| 노드 타입 | 예상 ACK 수 | 누구로부터 |
|-----------|-------------|------------|
| **INIT own DATA** | 4개 | NODE_2, NODE_8, FL, FR |
| **INIT FL_RELAY** | 2개 | NODE_2, NODE_8만 |
| **INIT FR_RELAY** | 2개 | NODE_2, NODE_8만 |
| **NODE_2, NODE_8** | 2개 | INIT, 다른 일반 노드 (FL/FR 직접 제외) |
| **FL** | 2개 | INIT, FR |
| **FR** | 2개 | INIT, FL |

**왜 예상값이 다른가?**
- **FL/FR**은 **INIT + 상대방**으로부터 ACK 기대 (서로 및 INIT과 잘 통신됨)
- **일반 노드**는 **INIT + 다른 일반 노드들**로부터 ACK 기대 (FL/FR 직접 제외, 대신 릴레이 사용)
- **INIT 릴레이 슬롯**은 **일반 노드들로부터만** ACK 기대 (FL/FR은 릴레이 메시지에 ACK 안함)

---

## **📊 통계 추적**

### **Pair 레벨 (사이클당 3개 pair)**
각 전송 항목에 대해 (INIT은 3개: DATA, FL_RELAY, FR_RELAY):
- **Success**: 이 pair에서 ACK 수신
- **Fail**: 이 pair에서 충분한 ACK 못받음
- **IDLE**: 이전 pair에서 이미 성공, TX 건너뜀

INIT 예시:
```
Pair 1: DATA가 Period 2에서 성공 → pair1_data_success++
Pair 2: DATA는 IDLE (TX 불필요) → pair2_data_idle++
Pair 3: DATA는 IDLE (TX 불필요) → pair3_data_idle++
```

### **Cycle 레벨 (100 사이클)**
- **Successful Cycles**: 각 항목에 대해 최소 하나의 pair가 성공
- **Failed Cycles**: 모든 항목의 3개 pair가 모두 실패
- **Perfect Cycles** (INIT만 해당): 3개 항목 모두 성공 (DATA + FL_RELAY + FR_RELAY)

---

## **⚙️ 설정 전환**

**두 가지 설정:**
1. **config_sync** (PLEN512): SYNC 신뢰성을 위한 긴 프리앰블
2. **config_data** (PLEN64): 속도를 위한 짧은 프리앰블

**전환 타임라인:**
```
0ms: SYNC TX/RX (PLEN512)
↓
0ms: DATA 설정으로 전환 (PLEN64)
↓
2-17.2ms: DATA/ACK_ARRAY 동작
↓
17.2ms: SYNC 설정으로 전환 (PLEN512)
↓
21.2ms: 다음 SYNC 예상
```

**왜 전환하나?**
- **SYNC**는 신뢰성 필요 (긴 프리앰블, 더 나은 감지)
- **DATA/ACK**는 속도 필요 (짧은 프리앰블, 더 많은 처리량)

---

## **🔧 구현 세부사항**

### **타이밍 정밀도 (DWT Cycle Counter)**
```c
// ARM Cortex-M4 DWT: 64MHz CPU → 64 사이클 = 1 마이크로초
uint32_t cycles = dwt_timer_get_cycles();  // SPI 오버헤드 없음!
if (dwt_timer_elapsed(last_sync_cycles, slot_interval_cycles)) {
    // 슬롯 시간 도달
}
```

### **폴링 모드 (인터럽트 없음)**
```c
// 지속적으로 RX 상태 확인
uint32_t status_reg = dwt_readsysstatuslo();
if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
    // RX 프레임 준비됨 - 즉시 처리
    dwt_readrxdata(rx_buffer, frame_len, 0);
    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);  // 플래그 클리어
}
```

**왜 폴링인가?** 복잡한 타이밍 시나리오에서 인터럽트로 인한 SPI 락 문제 방지.

### **지연시간 측정**
```c
// TX 측: DATA에 타임스탬프 삽입
uint32_t tx_timestamp_us = (dwt_timer_get_cycles() - last_sync_cycles) / 64;
memcpy(&tx_msg[IDX_TX_TIMESTAMP], &tx_timestamp_us, sizeof(uint32_t));

// RX 측: 단방향 지연시간 계산
uint32_t rx_timestamp_us = (dwt_timer_get_cycles() - last_sync_cycles) / 64;
uint32_t oneway_delay_us = rx_timestamp_us - tx_timestamp_us;
```

모든 타임스탬프는 **SYNC 기준** (마지막 SYNC 이후 마이크로초)으로, 모든 노드에서 일관된 기준점 보장.

---

## **🎯 프로토콜 강점**

1. **효율적인 ACK 집계**: N² 개별 ACK 대신 N 브로드캐스트
2. **자동 재전송**: 사이클당 3번 시도, IDLE 최적화
3. **릴레이 시스템**: 원거리 노드를 위한 네트워크 범위 확장
4. **사이클 기반 통계**: pair/사이클별 성공률 추적 용이
5. **정밀한 타이밍**: DWT 사이클 카운터가 SPI 오버헤드 없이 마이크로초 정확도 제공
6. **폴링 모드**: 인터럽트 관련 SPI 락 문제 방지
7. **설정 전환**: 신뢰성(SYNC) vs 속도(DATA) 최적화
8. **지연시간 측정**: 노드별 단방향 지연시간 추적 (최소/최대/평균)

---

## **📝 중요한 설계 결정사항**

1. **동일 메시지 재전송**: Period 3, 5는 Period 1 메시지 재사용 (새 데이터 아님) - 일관성 보장
2. **독립적인 INIT 슬롯**: 독립적인 성공 추적을 가진 3개의 분리된 TX 슬롯
3. **릴레이 필터링**: 일반 노드는 FL/FR 직접 무시, FL/FR은 RELAY_DATA 무시 - 중복 처리 방지
4. **SYNC 타이밍 기준**: 클럭 드리프트 처리를 위해 모든 타이밍이 SYNC 기준
5. **버퍼 클리어**: 이전 period의 오래된 메시지 폐기를 위해 SYNC 후 RX 버퍼 클리어
6. **예상 ACK 카운트**: 네트워크 토폴로지에 따라 노드 타입별로 다름

---

## **🚀 테스트 모드 요약**

**5개 노드**: INIT, NODE_2, NODE_8, FL, FR
**12개 슬롯**: FL/FR용 릴레이 슬롯 포함
**100 사이클**: 최종 통계까지 실행
**21.2ms period**: 신뢰성을 위한 1.0ms 가드 타임
**사이클당 3개 period pair**: 최대 3번 재전송 시도

**예상 ACK:**
- INIT own: 4개 (다른 모든 노드)
- INIT FL_RELAY: 2개 (일반 노드만)
- INIT FR_RELAY: 2개 (일반 노드만)
- NODE_2, NODE_8: 2개 (INIT + 다른 일반)
- FL, FR: 2개 (INIT + 상대방)

---

이것은 멀티홉 릴레이, 자동 재전송, 효율적인 ACK 집계를 위한 우아한 솔루션을 갖춘 정교한 **신뢰성 우선** 프로토콜입니다. 페어로 구성된 파일들이 완벽하게 함께 작동합니다 - INIT이 조정하고 릴레이하는 동안, normal/FL/FR 노드들은 aggregated ACK 브로드캐스팅과 함께 TDMA 스케줄에 참여합니다. 🎯
