# **Aggregated ACKs Protocol - Comprehensive Analysis**

## **🎯 Core Concept**

This is a **TDMA-based UWB communication protocol with Aggregated ACK Arrays** designed for reliable multi-node communication with automatic retransmission and relay support.

---

## **📊 Network Topology**

**Test Mode Configuration (5 physical nodes, 12 TDMA slots):**
- **INIT** (SEQ 1, Slot 0): Coordinator/Initiator - **Has 3 transmission slots**
- **NODE_2** (SEQ 2, Slot 1): Normal node
- **NODE_8** (SEQ 8, Slot 7): Normal node
- **FL** (SEQ 9, Slot 8): Front Left - **needs relay via INIT**
- **FR** (SEQ 11, Slot 10): Front Right - **needs relay via INIT**
- **FL_RELAY** (SEQ 10, Slot 9): INIT relays FL's data
- **FR_RELAY** (SEQ 12, Slot 11): INIT relays FR's data

**Why Relay?** FL and FR nodes have poor direct communication with normal nodes (simulating real-world distance/obstruction), so INIT acts as a relay bridge.

---

## **⏱️ Time Structure**

```
Period: 21.2ms (1.0ms guard time for reliability)
├─ Slot Interval: 1.1ms (0.1ms TX + 1.0ms guard)
├─ Config Switch: 17.2ms (switch to SYNC config, 4.0ms buffer)
└─ 12 TDMA Slots total

Cycle: 6 periods = 3 period pairs
├─ Pair 1: Period 1 + Period 2
├─ Pair 2: Period 3 + Period 4
└─ Pair 3: Period 5 + Period 6
```

**Period Pairs = Retransmission Opportunities:**
- **Odd Period** (1, 3, 5): DATA transmission
- **Even Period** (2, 4, 6): ACK_ARRAY broadcasting

If successful in any pair → **IDLE** for remaining pairs in the cycle.

---

## **🔄 Protocol Flow (Per Cycle)**

### **Phase 1: SYNC (Start of each period)**
1. **INIT** sends **SYNC** beacon (PLEN512 for reliability)
2. All nodes synchronize their timing reference
3. Switch from SYNC config → DATA config (PLEN64 for speed)

### **Phase 2: Odd Periods (1, 3, 5) - DATA Transmission**
1. Each node transmits in its assigned slot:
   - **SEQ 1** (2ms): INIT own DATA
   - **SEQ 2** (3.1ms): NODE_2 DATA
   - **SEQ 8** (9.7ms): NODE_8 DATA
   - **SEQ 9** (10.8ms): FL direct DATA (INIT captures this)
   - **SEQ 10** (12.4ms): INIT relays FL as RELAY_DATA
   - **SEQ 11** (13.5ms): FR direct DATA (INIT captures this)
   - **SEQ 12** (15.1ms): INIT relays FR as RELAY_DATA

2. All nodes listen and track received DATA in `data_received_from[12]` array
   - Index 0 = INIT, Index 1 = NODE_2, Index 8 = FL, Index 9 = FL_RELAY, etc.

### **Phase 3: Even Periods (2, 4, 6) - ACK_ARRAY Broadcasting**
1. Each node broadcasts its `data_received_from[]` array as **ACK_ARRAY**
2. **ACK_ARRAY Structure**: 12-byte payload at `IDX_ACK_ARRAY` (index 8-19)
   ```c
   ack_array[0] = 1  // Received INIT's DATA
   ack_array[1] = 1  // Received NODE_2's DATA
   ack_array[8] = 0  // Did NOT receive FL's direct DATA (normal nodes ignore FL direct)
   ack_array[9] = 1  // Received FL_RELAY from INIT
   ```

3. Each node checks incoming ACK_ARRAY messages:
   - Extract `ack_array[my_slot_index]`
   - If `ack_array[my_slot_index] == 1` → That node received my DATA ✅
   - Count unique ACKs from expected nodes
   - If `cumulative_ack_count >= expected_acks` → **SUCCESS** → Go **IDLE**

### **Phase 4: Retransmission Decision**
- **Period 1** (Pair 1): First TX - prepare **NEW** message, store in `retrans_msg[]`
- **Period 2** (Pair 1): Check ACKs → If success, TX_STATE = IDLE
- **Period 3** (Pair 2): If not IDLE, retransmit **SAME** message from Period 1
- **Period 4** (Pair 2): Check ACKs → If success, TX_STATE = IDLE
- **Period 5** (Pair 3): If not IDLE, retransmit **SAME** message from Period 1
- **Period 6** (Pair 3): Check ACKs → Evaluate cycle success

---

## **🔑 Key Innovation: Aggregated ACK Arrays**

**Traditional ACK Problem:**
- Each node sends individual ACK to each sender → N² messages
- Collision risk, high overhead

**Aggregated ACK Solution:**
- Each node broadcasts **ONE** ACK_ARRAY containing status for **ALL** slots
- Everyone hears everyone's ACK status simultaneously
- Reduces traffic from N² to N broadcasts
- Enables multi-cast efficiency

**Example:**
```
NODE_2 broadcasts ACK_ARRAY: [1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0]
                               ↑        ↑           ↑     ↑
                              INIT    (gaps)       N8  FL_REL
```
This tells **everyone** that NODE_2 received DATA from INIT, NODE_8, and FL_RELAY.

---

## **🔄 INIT Node - Special 3-Slot Behavior**

The INIT node is unique with **3 independent transmission slots**:

| Slot | SEQ | Time | Purpose | Expected ACKs |
|------|-----|------|---------|---------------|
| Own | SEQ 1 | 2ms | Own DATA | 4 (NODE_2, NODE_8, FL, FR) |
| FL Relay | SEQ 10 | 12.4ms | Relay FL's data | 2 (NODE_2, NODE_8) |
| FR Relay | SEQ 12 | 15.1ms | Relay FR's data | 2 (NODE_2, NODE_8) |

**Each slot has:**
- Independent success tracking (`data_success_in_current_cycle`, `fl_relay_success_in_current_cycle`, `fr_relay_success_in_current_cycle`)
- Independent retransmission buffers (`retrans_msg[]`, `fl_retrans_msg[]`, `fr_retrans_msg[]`)
- Independent execution flags (`seq1_executed`, `seq10_executed`, `seq12_executed`)

**Why 3 slots?**
- **SEQ 1**: Communicate INIT's own data to all nodes
- **SEQ 10/12**: Bridge FL/FR to normal nodes (relay system)

---

## **🌉 Relay System (FL/FR Communication)**

**Problem:** FL and FR nodes can't reliably communicate with normal nodes directly.

**Solution:** INIT acts as a relay bridge.

**Flow:**
1. **Period 1, 3, 5** (odd periods):
   - **SEQ 9** (10.8ms): FL transmits direct DATA
   - **INIT** receives and stores: `has_fl_data = true`, copy to `fl_relay_msg[]`
   - **SEQ 10** (12.4ms): INIT relays as `MSG_TYPE_RELAY_DATA`
   - **Normal nodes** (NODE_2, NODE_8) receive RELAY_DATA
   - **FL** ignores RELAY_DATA (it already has its own data)

2. **Period 2, 4, 6** (even periods):
   - **Normal nodes** broadcast ACK_ARRAY with `ack_array[9] = 1` (FL_RELAY received)
   - **INIT** counts ACKs for `SLOT_IDX_FL_RELAY` separately
   - If `fl_relay_unique_ack_count >= 2` → `fl_relay_completed = true`

**Filtering Rules:**
```c
// Normal nodes
if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA && (src_node == NODE_FL || src_node == NODE_FR)) {
    should_process = false;  // Ignore direct DATA from FL/FR
}

// FL/FR nodes
if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_RELAY_DATA) {
    should_process = false;  // FL/FR ignore relayed versions
}
```

---

## **📈 Success Criteria & Expectations**

| Node Type | Expected ACKs | From Whom |
|-----------|---------------|-----------|
| **INIT own DATA** | 4 | NODE_2, NODE_8, FL, FR |
| **INIT FL_RELAY** | 2 | NODE_2, NODE_8 only |
| **INIT FR_RELAY** | 2 | NODE_2, NODE_8 only |
| **NODE_2, NODE_8** | 2 | INIT, other normal node (exclude FL/FR direct) |
| **FL** | 2 | INIT, FR |
| **FR** | 2 | INIT, FL |

**Why different expectations?**
- **FL/FR** expect ACKs from **INIT + counterpart** (they communicate well with each other and INIT)
- **Normal nodes** expect ACKs from **INIT + other normal nodes** (exclude FL/FR direct, use relay instead)
- **INIT relay slots** expect ACKs from **normal nodes only** (FL/FR don't ACK relayed messages)

---

## **📊 Statistics Tracking**

### **Pair-Level (3 pairs per cycle)**
For each transmission item (INIT has 3: DATA, FL_RELAY, FR_RELAY):
- **Success**: ACKs received in this pair
- **Fail**: No sufficient ACKs in this pair
- **IDLE**: Already successful in previous pair, skipped TX

Example for INIT:
```
Pair 1: DATA succeeded in Period 2 → pair1_data_success++
Pair 2: DATA is IDLE (no TX needed) → pair2_data_idle++
Pair 3: DATA is IDLE (no TX needed) → pair3_data_idle++
```

### **Cycle-Level (100 cycles)**
- **Successful Cycles**: At least one pair succeeded for each item
- **Failed Cycles**: All 3 pairs failed for all items
- **Perfect Cycles** (INIT only): All 3 items succeeded (DATA + FL_RELAY + FR_RELAY)

---

## **⚙️ Configuration Switching**

**Two configurations:**
1. **config_sync** (PLEN512): Long preamble for SYNC reliability
2. **config_data** (PLEN64): Short preamble for speed

**Switching timeline:**
```
0ms: SYNC TX/RX (PLEN512)
↓
0ms: Switch to DATA config (PLEN64)
↓
2-17.2ms: DATA/ACK_ARRAY operations
↓
17.2ms: Switch to SYNC config (PLEN512)
↓
21.2ms: Next SYNC expected
```

**Why switch?**
- **SYNC** needs reliability (longer preamble, better detection)
- **DATA/ACK** needs speed (shorter preamble, more throughput)

---

## **🔧 Implementation Details**

### **Timing Precision (DWT Cycle Counter)**
```c
// ARM Cortex-M4 DWT: 64MHz CPU → 64 cycles = 1 microsecond
uint32_t cycles = dwt_timer_get_cycles();  // No SPI overhead!
if (dwt_timer_elapsed(last_sync_cycles, slot_interval_cycles)) {
    // Slot time reached
}
```

### **Polling Mode (No Interrupts)**
```c
// Continuously check RX status
uint32_t status_reg = dwt_readsysstatuslo();
if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
    // RX frame ready - process immediately
    dwt_readrxdata(rx_buffer, frame_len, 0);
    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);  // Clear flag
}
```

**Why polling?** Avoids SPI lock issues with interrupts in complex timing scenarios.

### **Latency Measurement**
```c
// TX side: Embed timestamp in DATA
uint32_t tx_timestamp_us = (dwt_timer_get_cycles() - last_sync_cycles) / 64;
memcpy(&tx_msg[IDX_TX_TIMESTAMP], &tx_timestamp_us, sizeof(uint32_t));

// RX side: Calculate one-way latency
uint32_t rx_timestamp_us = (dwt_timer_get_cycles() - last_sync_cycles) / 64;
uint32_t oneway_delay_us = rx_timestamp_us - tx_timestamp_us;
```

All timestamps are **SYNC-relative** (microseconds since last SYNC), ensuring consistent reference across all nodes.

---

## **🎯 Protocol Strengths**

1. **Efficient ACK aggregation**: N broadcasts instead of N² individual ACKs
2. **Automatic retransmission**: 3 attempts per cycle with IDLE optimization
3. **Relay system**: Extends network coverage for distant nodes
4. **Cycle-based statistics**: Easy to track success rates per pair/cycle
5. **Precise timing**: DWT cycle counter provides microsecond accuracy without SPI overhead
6. **Polling mode**: Avoids interrupt-related SPI lock issues
7. **Configuration switching**: Optimizes for reliability (SYNC) vs speed (DATA)
8. **Latency measurement**: Per-node one-way latency tracking with min/max/avg

---

## **📝 Critical Design Decisions**

1. **Same message retransmission**: Period 3, 5 reuse Period 1 message (not new data) - ensures consistency
2. **Independent INIT slots**: 3 separate TX slots with independent success tracking
3. **Relay filtering**: Normal nodes ignore FL/FR direct, FL/FR ignore RELAY_DATA - prevents duplicate processing
4. **SYNC timing reference**: All timing is relative to SYNC to handle clock drift
5. **Buffer clearing**: RX buffer cleared after SYNC to discard old messages from previous period
6. **Expected ACK counts**: Different for each node type based on network topology

---

## **🚀 Test Mode Summary**

**5 nodes**: INIT, NODE_2, NODE_8, FL, FR
**12 slots**: Including relay slots for FL/FR
**100 cycles**: Run until final statistics
**21.2ms period**: With 1.0ms guard times for reliability
**3 period pairs per cycle**: Up to 3 retransmission attempts

**Expected ACKs:**
- INIT own: 4 (all other nodes)
- INIT FL_RELAY: 2 (normal nodes only)
- INIT FR_RELAY: 2 (normal nodes only)
- NODE_2, NODE_8: 2 (INIT + other normal)
- FL, FR: 2 (INIT + counterpart)

---

This is a sophisticated **reliability-first** protocol with elegant solutions for multi-hop relay, automatic retransmission, and efficient ACK aggregation. The paired files work together perfectly - INIT coordinates and relays, while normal/FL/FR nodes participate in the TDMA schedule with aggregated ACK broadcasting. 🎯
