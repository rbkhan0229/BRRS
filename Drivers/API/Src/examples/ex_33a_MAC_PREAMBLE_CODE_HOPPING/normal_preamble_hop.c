/*! ----------------------------------------------------------------------------
 *  @file    normal_preamble_hop.c
 *  @brief   Normal Node for UWB MAC Protocol V2 - Aggregated ACKs
 *
 *           TDMA-based UWB protocol with aggregated acknowledgments.
 *           Normal nodes transmit DATA in odd periods (1,3,5) and ACK_ARRAY in even periods (2,4,6).
 *           Uses 12-slot structure with 21.2ms period and relay support for FL/FR nodes.
 *
 * @author Decawave
 *
 * @copyright SPDX-FileCopyrightText: Copyright (c) 2024 Qorvo US, Inc.
 *            SPDX-License-Identifier: LicenseRef-QORVO-2
 *
 */
#include "deca_probe_interface.h"
#include <deca_device_api.h>
#include <deca_spi.h>
#include <example_selection.h>
#include <port.h>
#include <shared_defines.h>
#include <shared_functions.h>

// Standard C includes
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Platform includes
#include "nrf_delay.h"
#include "nrf.h"

#if defined(TEST_NORMAL_PREAMBLE_HOP)

extern void test_run_info(unsigned char *data);

/* ========== 테스트 모드 ========== */
#define TEST_MODE 1  // Set to 0 for default Node 8, 1 for easy node selection

//MARK: TEST MODE 설정
#if TEST_MODE
    #define TEST_TOTAL_NODES 5     // Test nodes: INIT, NODE_2, NODE_8, FL, FR
    /* FL node expects ACK from INIT + FR */
    /* FR node expects ACK from INIT + FL */
    /* Normal nodes expect ACKs from other normal nodes + INIT (exclude FL/FR) */
    #if defined(TEST_NODE_FL) || defined(TEST_NODE_FR)
        #define TEST_EXPECTED_ACKS 2   // FL/FR need INIT + counterpart ACK
    #else
        #define TEST_EXPECTED_ACKS (TEST_TOTAL_NODES - 2 - 1)   // Total - FL - FR - self = 5 - 2 - 1 = 2
        //#define TEST_EXPECTED_ACKS 1   // Total - FL - FR - self = 5 - 2 - 1 = 2
    #endif

    // 테스트하고자 하는 노드 주석 해제
    // Select node type for testing (uncomment one) - Updated for 12-slot relay system:
    //#define TEST_NODE_1        // Node 1, slot 1 (not used - INIT node slot)
    #define TEST_NODE_2        // Node 2, slot 2 (SEQ 2: 10ms-20ms)
    //#define TEST_NODE_3        // Node 3, slot 3 (SEQ 3: 20ms-30ms)
    //#define TEST_NODE_4        // Node 4, slot 4 (SEQ 4: 30ms-40ms)
    //#define TEST_NODE_5        // Node 5, slot 5 (SEQ 5: 40ms-50ms)
    //#define TEST_NODE_6        // Node 6, slot 6 (SEQ 6: 50ms-60ms)
    //#define TEST_NODE_7        // Node 7, slot 7 (SEQ 7: 60ms-70ms)
    //#define TEST_NODE_8        // Node 8, slot 8 (SEQ 8: 70ms-80ms)
    //#define TEST_NODE_FL       // FL node, slot 9 (SEQ 9: 80ms-90ms) - needs relay via INIT
    //#define TEST_NODE_FR       // FR node, slot 11 (SEQ 11: 100ms-110ms) - needs relay via INIT

    #ifdef TEST_NODE_1
        #define APP_NAME "NORMAL NODE 1 v1.0 (NOT USED - INIT SLOT)"
        #define TEST_MY_NODE_ID NODE_1
        #define TEST_MY_NODE_SEQ 1
    #elif defined(TEST_NODE_2)
        #define APP_NAME "NORMAL NODE 2 v1.0"
        #define TEST_MY_NODE_ID NODE_2
        #define TEST_MY_NODE_SEQ 2
    #elif defined(TEST_NODE_3)
        #define APP_NAME "NORMAL NODE 3 v1.0"
        #define TEST_MY_NODE_ID NODE_3
        #define TEST_MY_NODE_SEQ 3
    #elif defined(TEST_NODE_4)
        #define APP_NAME "NORMAL NODE 4 v1.0"
        #define TEST_MY_NODE_ID NODE_4
        #define TEST_MY_NODE_SEQ 4
    #elif defined(TEST_NODE_5)
        #define APP_NAME "NORMAL NODE 5 v1.0"
        #define TEST_MY_NODE_ID NODE_5
        #define TEST_MY_NODE_SEQ 5
    #elif defined(TEST_NODE_6)
        #define APP_NAME "NORMAL NODE 6 v1.0"
        #define TEST_MY_NODE_ID NODE_6
        #define TEST_MY_NODE_SEQ 6
    #elif defined(TEST_NODE_7)
        #define APP_NAME "NORMAL NODE 7 v1.0"
        #define TEST_MY_NODE_ID NODE_7
        #define TEST_MY_NODE_SEQ 7
    #elif defined(TEST_NODE_8)
        #define APP_NAME "NORMAL NODE 8 v1.0"
        #define TEST_MY_NODE_ID NODE_8
        #define TEST_MY_NODE_SEQ 8
    #elif defined(TEST_NODE_FL)
        #define APP_NAME "FRONT LEFT NODE v1.0 (NEEDS RELAY)"
        #define TEST_MY_NODE_ID NODE_FL
        #define TEST_MY_NODE_SEQ 9
    #elif defined(TEST_NODE_FR)
        #define APP_NAME "FRONT RIGHT NODE v1.0 (NEEDS RELAY)"
        #define TEST_MY_NODE_ID NODE_FR
        #define TEST_MY_NODE_SEQ 11
        #define TEST_MY_SLOT_START_MS 13.5  // FR node: 13.5ms (1.1ms after FL_RELAY at 12.4ms)
    #else
        #error "Please select a node type for TEST_MODE"
    #endif
#else
    /* Default configuration - NODE_2 for 12-slot system */
    #define APP_NAME "NORMAL NODE 2 v1.0"
    #define TEST_MY_NODE_ID NODE_2
    #define TEST_MY_NODE_SEQ 2
#endif

/* ========== DWT 타이머 상수 ========== */
#define CPU_FREQ_MHZ 64                    // nRF52840 CPU frequency
#define CYCLES_PER_US (CPU_FREQ_MHZ)       // 64 cycles per microsecond
#define CYCLES_PER_MS (CPU_FREQ_MHZ * 1000) // 64,000 cycles per millisecond

/* ========== DWT 타이머 구조체 ========== */
typedef struct {
    uint32_t start_cycles;     // Starting cycle count
    uint32_t target_cycles;    // Target cycle duration
    bool active;               // Timer active flag
} dwt_timer_t;

/* ========== TDMA 프로토콜 파라미터 - Aggregated ACKs ========== */
// Period: 21.2ms (1.0ms guard time for all slots)
// Slot: 1.1ms (0.1ms TX + 1.0ms guard)
// Config switch: 17.2ms (4.0ms buffer for SYNC reception at 21.2ms)
#define PERIOD_MS           21.2    // 21.2ms period for Aggregated ACKs (1.0ms guard time)
#define SLOT_DURATION_MS    0.1     // 0.1ms per slot (TX only)
#define GUARD_TIME_MS       1.0     // 1.0ms guard time between slots
#define SLOT_INTERVAL_MS    1.1     // Total slot interval (0.1ms + 1.0ms guard)
#define TOTAL_NODES         10      // Total physical nodes in network
#define TOTAL_SLOTS         12      // Total TDMA slots (includes relay slots)
#define TOTAL_ARRAY_SIZE    12      // Array size for tracking all transmissions
#define PERIODS_PER_CYCLE   6       // 6 periods per cycle (3 period pairs)
#define CONFIG_SWITCH_MS    17.2    // Switch to PLEN512 at 17.2ms

/* Slot Assignment (21.2ms period, 1.1ms slot interval):
 * SEQ 1:  0.0ms  - INIT own data (2.0ms buffer start)
 * SEQ 2:  1.1ms  - NODE_2
 * SEQ 3:  2.2ms  - NODE_3
 * SEQ 4:  3.3ms  - NODE_4
 * SEQ 5:  4.4ms  - NODE_5
 * SEQ 6:  5.5ms  - NODE_6
 * SEQ 7:  6.6ms  - NODE_7
 * SEQ 8:  7.7ms  - NODE_8
 * SEQ 9:  8.8ms  - FL direct slot
 * SEQ 10: 9.9ms  - INIT FL relay
 * SEQ 11: 11.0ms - FR direct slot
 * SEQ 12: 12.1ms - INIT FR relay
 * Config switch at 17.2ms for 4.0ms SYNC buffer
 */

#if TEST_MODE
    #define MY_NODE_SEQ         TEST_MY_NODE_SEQ       // From TEST_MODE selection
    #if defined(TEST_MY_SLOT_START_MS)
        #define MY_SLOT_START_MS    TEST_MY_SLOT_START_MS  // From TEST_MODE selection
    #else
        #define MY_SLOT_START_MS    ((MY_NODE_SEQ-1) * SLOT_INTERVAL_MS)  // Calculate based on sequence
    #endif
#else
    #define MY_NODE_SEQ         2       // Default node 2 for 12-slot system
    #define MY_SLOT_START_MS    10      // 10ms for node 2 (SEQ 2)
#endif

/* ========== ACK 수신 파라미터 ========== */
#define TX_TO_RX_DELAY_UUS  60      // Delay from TX end to RX activation (60us)
#define RX_ACK_TIMEOUT_UUS  4500    // ACK reception timeout (4.5ms)
#define ACK_SLOT_DURATION_US  450   // 450us per ACK slot
#define ACK_TX_INTERVAL_US    500   // 500us interval between ACK transmissions (increased for reliability)

/* FL/FR nodes need relay - other normal nodes should not ACK FL/FR direct messages */
#define RELAY_TEST_MODE     1       // Enable relay testing behavior

/* Default communication configuration - MATCH rx_send_resp.c */
/* Default communication configuration for DATA/ACK - PLEN64 */
static dwt_config_t config_data = {
    9,                /* Channel number. */
    DWT_PLEN_64,      /* Preamble length - Fast for DATA/ACK */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (64 + 1 + 8 - 8),    /* SFD timeout for PLEN64 */
    DWT_STS_MODE_OFF, /* No STS mode enabled */
    DWT_STS_LEN_64,   /* STS length */
    DWT_PDOA_M0       /* PDOA mode off */
};

/* SYNC communication configuration - PLEN512 for better reliability */
static dwt_config_t config_sync = {
    9,                /* Channel number. */
    DWT_PLEN_512,    /* Preamble length - Long for SYNC reliability */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (512 + 1 + 8 - 8),   /* SFD timeout for PLEN512 */
    DWT_STS_MODE_OFF, /* No STS mode enabled */
    DWT_STS_LEN_64,   /* STS length */
    DWT_PDOA_M0       /* PDOA mode off */
};

/* ========== 노드 ID 정의 ========== */
#define NODE_INIT '1'    // Initiator node (this node) - SEQ 1, index 0
#define NODE_2    '2'    // Normal node 2 - SEQ 2, index 1
#define NODE_3    '3'    // Normal node 3 - SEQ 3, index 2
#define NODE_4    '4'    // Normal node 4 - SEQ 4, index 3
#define NODE_5    '5'    // Normal node 5 - SEQ 5, index 4
#define NODE_6    '6'    // Normal node 6 - SEQ 6, index 5
#define NODE_7    '7'    // Normal node 7 - SEQ 7, index 6
#define NODE_8    '8'    // Normal node 8 - SEQ 8, index 7
#define NODE_FL   '9'    // Front Left node - SEQ 9, index 8 (needs relay)
#define NODE_FR   'A'    // Front Right node - SEQ 11, index 9 (needs relay)
#define NODE_ALL  'B'    // Broadcast to all nodes

/* ========== Preamble Code Hopping 설정 ========== */
#define PREAMBLE_CODE_COUNT  12

/* Channel 9, PRF 64MHz 유효 Preamble Code */
static const uint8_t VALID_PREAMBLE_CODES[PREAMBLE_CODE_COUNT] = {
    9, 10, 11, 12, 13, 14, 15, 16, 21, 22, 23, 24
};

/* 차량 고유 ID (32-bit Access Address) */
#define VEHICLE_AA  0x12345678U

/* Channel Identifier = AA[31:16] XOR AA[15:0] (BLE CSA#2 spec) */
#define CHANNEL_ID  ((uint16_t)(((VEHICLE_AA) >> 16) ^ ((VEHICLE_AA) & 0xFFFF)))

/* Global Period Counter for Preamble Hopping (synced from INIT via SYNC message, 16-bit) */
static uint16_t global_period_counter = 0;

/* Next SYNC preamble code (received from INIT via SYNC message) */
/* Used at 17.2ms config switch for next SYNC reception */
/* Note: Initialized at runtime with get_preamble_code_for_period(0) */
static uint8_t next_sync_preamble = 9;  // Placeholder, set properly at init

/* ========== 메시지 타입 정의 ========== */
#define MSG_TYPE_SYNC      0x01
#define MSG_TYPE_DATA      0x02
#define MSG_TYPE_ACK       0x03
#define MSG_TYPE_URGENT    0x04
#define MSG_TYPE_RELAY_DATA   0x05  // Relayed data from FL/FR
#define MSG_TYPE_RELAY_ACK    0x06  // ACK for relayed message
#define MSG_TYPE_ACK_ARRAY    0x07  // ACK array broadcast (Period 2,4,6)

/* ========== 메시지 인덱스 정의 ========== */
enum {
  IDX_FTYPE     = 0,  // 0: frame type
  IDX_SEQ       = 1,  // 1: seq
  IDX_SOURCE    = 2,  // 2: Source ID
  IDX_DEST      = 3,  // 3: Destination ID
  IDX_MSG_TYPE  = 4,  // 4: Message type (SYNC/DATA/ACK/etc)
  IDX_PRIORITY  = 5,  // 5: Priority (URGENT/NORMAL)
  IDX_ORIG_SRC  = 6,  // 6: Original source (for relay)
  IDX_PERIOD_INFO = 6,  // 6: Period number (1-6) - SYNC only, reuses ORIG_SRC position
  IDX_ORIG_DST  = 7,  // 7: Original dest (for relay)
  IDX_TX_TIMESTAMP = 8,    // 8~11: TX timestamp (4 bytes, us since SYNC) - DATA only
  IDX_DATA_PAYLOAD = 12,   // 12~: Data payload (for DATA messages)
  IDX_ACK_ARRAY = 8,       // 8~19: ACK array payload (12 bytes) - ACK_ARRAY only
  /* Preamble Code for Preamble Hopping (SYNC only) - 2 bytes */
  IDX_CURRENT_PREAMBLE = 8,   // Current period DATA/ACK preamble code
  IDX_NEXT_PREAMBLE = 9       // Next period SYNC preamble code
};

/* As "TX then wait for a response" example sends a blink message encoded as per the ISO/IEC 24730-62:2013 standard which includes a bit signalling
 * that a response is listened for, this example will respond with a valid frame (that will be ignored anyway) following the same standard. The
 * response is a 21-byte frame composed of the following fields:
 *     - byte 0/1: frame control (0x8C41 to indicate a data frame using 16-bit source addressing and 64-bit destination addressing).
 *     - byte 2: sequence number, incremented for each new frame.
 *     - byte 3/4: application ID (0x609A for data frames in this standard).
 *     - byte 5 -> 12: 64-bit destination address.
 *     - byte 13/14: 16-bit source address, hard coded in this example to keep it simple.
 *     - byte 15: function code (0x10 to indicate this is an activity control message).
 *     - byte 16: activity code (0x00 to indicate activity is finished).
 *     - byte 17/18: new tag blink rate.
 *     - byte 19/20: frame check-sum, automatically set by DW IC.  */
/* TX message buffer - Extended to 22 bytes to fit ACK_ARRAY (index 8-19) + 2 bytes for CRC */
static uint8_t tx_msg[] = { 0x41, 0x8C, 0, 0x9A, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 'D', 'W', 0x10, 0x00, 0, 0, 0, 0, 0 };
#define BLINK_FRAME_SN_IDX 1
/* Indexes to access to sequence number and destination address of the data frame in the tx_msg array. */
#define DATA_FRAME_SN_IDX   2
#define DATA_FRAME_DEST_IDX 5

/* Inter-frame delay period, in milliseconds. (unused - legacy definition) */
#define TX_DELAY_MS 1000

/* Buffer to store received frame. See NOTE 1 below. */
static uint8_t rx_buffer[FRAME_LEN_MAX];

// DWT-based timers for UWB protocol
static dwt_timer_t period_timer;        // 20ms period timer
static dwt_timer_t slot_interval_timer; // 10ms wait for my slot
static dwt_timer_t slot_duration_timer; // 2ms TX slot duration limit
static dwt_timer_t ack_slot_timer;      // ACK transmission delay timer

/* Message queues removed - not used in Aggregated ACKs protocol */
/* static message_queue_t retrans_queue = {0}; */
/* static message_queue_t relay_queue = {0}; */

/* ACK tracking removed - not used, cumulative_ack_confirmed is used instead */
/* static uint8_t ack_status[TOTAL_NODES] = {0}; */

// Protocol state
static bool synchronized = false;
static uint32_t period_count = 0;            // Global period counter
static uint8_t current_period_in_cycle = 0;  // Start from 0, will become 1 on first SYNC
static uint32_t current_cycle = 0;           // Start from cycle 0, will become 1 on first period 1

/* ========== Pair별 통계 (사이클당 3쌍) ========== */
static uint32_t pair1_success = 0, pair1_fail = 0, pair1_idle = 0;
static uint32_t pair2_success = 0, pair2_fail = 0, pair2_idle = 0;
static uint32_t pair3_success = 0, pair3_fail = 0, pair3_idle = 0;

/* ========== Cycle별 통계 ========== */
static uint32_t total_cycles = 0;
static uint32_t successful_cycles = 0;  // Succeeded in any pair
static uint32_t failed_cycles = 0;

/* Failed cycle tracking (최근 10개 저장) */
#define MAX_FAILED_CYCLES_LOG 10
static uint32_t failed_cycle_numbers[MAX_FAILED_CYCLES_LOG] = {0};

/* Tracking per-cycle success (reset every cycle) */
static bool success_in_current_cycle = false;

/* SYNC timeout detection for final statistics */
#define SYNC_TIMEOUT_SECONDS 5                           // 5 seconds without SYNC = network ended
static uint32_t sync_timeout_cycles = 0;                 // CPU cycles for SYNC timeout
static bool final_stats_printed = false;                // Prevent multiple final stats output

/* ========== SYNC Loss 감지 시스템 ========== */
/* SYNC-only period management: no internal timer, trust SYNC completely */
#define SYNC_LOSS_TIMEOUT_MS 27.0  // 27ms = 21.2ms period + 5.8ms margin

static uint32_t sync_loss_timeout_cycles = 0;  // CPU cycles for 27ms timeout
static bool sync_lost = false;  // SYNC loss flag - true when timeout occurs

/* SYNC loss statistics */
typedef struct {
    uint32_t total_timeouts;           // Total SYNC timeout occurrences
    uint32_t data_skips;               // DATA TX skips due to SYNC loss
    uint32_t ack_skips;                // ACK_ARRAY TX skips due to SYNC loss
    uint32_t period_skip_count[6];     // Skip count per period (index 0-5 for P1-P6)
} sync_loss_stats_t;

static sync_loss_stats_t sync_loss_stats = {0};

/* ========== 노드별 지연시간 추적 ========== */
/* One-way latency statistics structure */
typedef struct {
    uint32_t min_us;           // Minimum latency (microseconds)
    uint32_t max_us;           // Maximum latency
    uint64_t sum_us;           // Sum for average calculation
    uint32_t count;            // Number of samples
} latency_stats_t;

/* Per-node latency tracking (12 slots: INIT, NODE_2-8, FL, FL_RELAY, FR, FR_RELAY) */
static latency_stats_t node_latency[TOTAL_ARRAY_SIZE] = {
    {0xFFFFFFFF, 0, 0, 0},  // INIT (slot 0)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_2 (slot 1)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_3 (slot 2)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_4 (slot 3)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_5 (slot 4)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_6 (slot 5)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_7 (slot 6)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_8 (slot 7)
    {0xFFFFFFFF, 0, 0, 0},  // FL (slot 8)
    {0xFFFFFFFF, 0, 0, 0},  // FL_RELAY (slot 9)
    {0xFFFFFFFF, 0, 0, 0},  // FR (slot 10)
    {0xFFFFFFFF, 0, 0, 0}   // FR_RELAY (slot 11)
};

/* Retransmission state management */
typedef enum {
    TX_STATE_IDLE,        // No transmission needed
    TX_STATE_FIRST_TX,    // First transmission of new data
    TX_STATE_RETRANS      // Retransmission mode
} transmission_state_t;

static transmission_state_t tx_state = TX_STATE_FIRST_TX;  // Start with first transmission
static uint8_t retrans_msg[FRAME_LEN_MAX];                 // Store message for retransmission
static bool has_pending_retrans = false;                   // Flag for pending retransmission
static uint8_t retrans_attempt = 0;                        // Number of retransmission attempts in current cycle
#if TEST_MODE
static uint8_t expected_nodes = TEST_EXPECTED_ACKS;        // Test mode: expect specific ACKs
#else
/* Normal mode ACK expectations:
 * - FL node: expects INIT + FR ACKs (2 ACKs)
 * - FR node: expects INIT + FL ACKs (2 ACKs)
 * - Normal nodes: expect ACKs from other normal nodes + INIT (exclude FL/FR)
 * - Total nodes = 10, FL/FR = 2, so normal nodes expect 10 - 1(self) - 2(FL/FR) = 7 ACKs
 */
static uint8_t expected_nodes = TOTAL_NODES - 3;           // Exclude self + FL + FR = 7 ACKs for normal nodes
#endif
/* Index to access to source address of the blink frame in the rx_buffer array. */
#define BLINK_FRAME_SRC_IDX 2

/* Values for the PG_DELAY and TX_POWER registers reflect the bandwidth and power of the spectrum at the current
 * temperature. These values can be calibrated prior to taking reference measurements. See NOTE 3 below. */
extern dwt_txconfig_t txconfig_options;

/* ========== TX Power 설정 (Linear TX Power API 기반) ========== */
/*
 * dwt_calculate_linear_tx_setting() API를 사용한 정확한 TX Power 제어
 * 인덱스 1당 -0.25dB 감쇄 (선형 제어)
 *
 * 인덱스 = 감쇄량(dB) / 0.25
 */
#define TX_POWER_INDEX_0dB      0    // 0dB  (최대 출력)
#define TX_POWER_INDEX_3dB     12    // -3dB  (3 / 0.25 = 12)
#define TX_POWER_INDEX_6dB     24    // -6dB  (6 / 0.25 = 24)
#define TX_POWER_INDEX_10dB    40    // -10dB (10 / 0.25 = 40)
#define TX_POWER_INDEX_15dB    60    // -15dB (15 / 0.25 = 60)
#define TX_POWER_INDEX_16dB    64    // -16dB (16 / 0.25 = 64)
#define TX_POWER_INDEX_17dB    68    // -17dB (17 / 0.25 = 68)
#define TX_POWER_INDEX_18dB    72    // -18dB (18 / 0.25 = 72)
#define TX_POWER_INDEX_19dB    76    // -19dB (19 / 0.25 = 76)
#define TX_POWER_INDEX_20dB    80    // -20dB (20 / 0.25 = 80)

/* 현재 사용할 TX Power Index 선택 (하나만 활성화) */
#define USE_TX_POWER_INDEX  TX_POWER_INDEX_10dB    // <-- 여기서 변경

/* 여러 응답 수신 관련 */
#define MAX_RESPONSES 10

/* ========== DWT 사이클 카운터 함수 ========== */

// ARM Cortex-M4 DWT register addresses
#define DWT_CTRL     (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT   (*(volatile uint32_t*)0xE0001004)

// DWT Control register bit for cycle counter enable
#ifndef DWT_CTRL_CYCCNTENA_Msk
#define DWT_CTRL_CYCCNTENA_Msk  (1UL << 0)  // Bit 0: Enable cycle counter
#endif

// CPU clock frequency for timing calculations
#define CPU_FREQ_HZ  64000000  // 64MHz nRF52840

/*
 * DWT 사이클 카운터 초기화
 *
 * 동작: ARM Cortex-M4 DWT(Data Watchpoint and Trace) 유닛의
 *       사이클 카운터를 활성화하여 SPI 없이 정밀한 타이밍 측정 가능
 */
static void dwt_timer_init(void) {
    if (!(DWT_CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
        DWT_CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

/*
 * 현재 CPU 사이클 카운트 반환
 *
 * 반환값: 32비트 사이클 카운터 값 (약 67초마다 오버플로우)
 */
static uint32_t dwt_timer_get_cycles(void) {
    return DWT_CYCCNT;
}

/*
 * 마이크로초를 CPU 사이클로 변환
 *
 * 파라미터:
 *   microseconds: 변환할 마이크로초 값
 *
 * 반환값: CPU 사이클 수 (64MHz 기준: 1us = 64 cycles)
 */
static uint32_t us_to_cpu_cycles(uint32_t microseconds) {
    return microseconds * (CPU_FREQ_HZ / 1000000);
}

/*
 * 지정된 시간이 경과했는지 체크
 *
 * 파라미터:
 *   start_cycles: 시작 시점의 사이클 카운트
 *   duration_cycles: 경과 확인할 사이클 수
 *
 * 반환값: true=경과함, false=아직 경과 안함
 * 참고: 32비트 오버플로우 자동 처리됨
 */
static bool dwt_timer_elapsed(uint32_t start_cycles, uint32_t duration_cycles) {
    uint32_t current_cycles = dwt_timer_get_cycles();
    uint32_t elapsed = current_cycles - start_cycles;
    return (elapsed >= duration_cycles);
}

/*
 * CPU 사이클을 밀리초로 변환
 *
 * 파라미터:
 *   cycles: 변환할 CPU 사이클 수
 *
 * 반환값: 밀리초 값 (64MHz 기준: 64000 cycles = 1ms)
 */
static uint32_t cycles_to_ms(uint32_t cycles) {
    return cycles / (CPU_FREQ_HZ / 1000);
}

/*
 * SYNC 이후 경과 시간 반환 (ms)
 *
 * 파라미터:
 *   last_sync_cycles: 마지막 SYNC 수신 시점의 사이클 카운트
 *
 * 반환값: SYNC 이후 경과한 밀리초 (last_sync_cycles=0이면 0 반환)
 */
static uint32_t get_elapsed_ms(uint32_t last_sync_cycles) {
    if (last_sync_cycles == 0) return 0;
    uint32_t current = dwt_timer_get_cycles();
    return cycles_to_ms(current - last_sync_cycles);
}

/*
 * 노드별 지연시간 통계 업데이트
 *
 * 파라미터:
 *   stats: 업데이트할 통계 구조체 포인터
 *   latency_us: 측정된 지연시간 (마이크로초)
 *
 * 동작: min/max 업데이트, sum 누적, count 증가
 */
static void update_node_latency(latency_stats_t *stats, uint32_t latency_us) {
    if (latency_us < stats->min_us) {
        stats->min_us = latency_us;
    }
    if (latency_us > stats->max_us) {
        stats->max_us = latency_us;
    }
    stats->sum_us += latency_us;
    stats->count++;
}

/* ========== BLE CSA#2 PRNG 함수 ========== */

/*
 * 8-bit Bit-Reverse (PERM 연산용)
 *
 * 파라미터:
 *   x: 입력 8-bit 값
 *
 * 반환값:
 *   비트 순서가 뒤집힌 8-bit 값
 */
static uint8_t reverse8(uint8_t x) {
    x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1);
    x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2);
    x = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4);
    return x;
}

/*
 * PERM 연산 (16-bit permutation)
 *
 * 파라미터:
 *   x: 입력 16-bit 값
 *
 * 반환값:
 *   상위/하위 바이트 각각 bit-reverse 후 결합
 */
static uint16_t perm(uint16_t x) {
    uint8_t hi = reverse8((uint8_t)(x >> 8));
    uint8_t lo = reverse8((uint8_t)(x & 0xFF));
    return ((uint16_t)hi << 8) | lo;
}

/*
 * MAM 연산 (Multiply-Add-Mod)
 *
 * 파라미터:
 *   a: 곱셈 대상
 *   b: 덧셈 대상
 *
 * 반환값:
 *   (17 * a + b) mod 2^16
 */
static uint16_t mam(uint16_t a, uint16_t b) {
    return (uint16_t)((17U * a + b) & 0xFFFF);
}

/*
 * prn_e 생성 (BLE CSA#2 Core)
 *
 * 파라미터:
 *   counter: 현재 period의 global counter (eventCounter)
 *   channel_id: 차량 고유 ID (channelID)
 *
 * 반환값:
 *   의사 난수 값 (16-bit)
 */
static uint16_t generate_prn_e(uint16_t counter, uint16_t channel_id) {
    uint16_t x = counter ^ channel_id;

    /* 3회 PERM + MAM 반복 */
    x = perm(x);
    x = mam(x, channel_id);

    x = perm(x);
    x = mam(x, channel_id);

    x = perm(x);
    x = mam(x, channel_id);

    return x ^ channel_id;
}

/*
 * 현재 period의 preamble code 선택
 *
 * 파라미터:
 *   counter: 현재 period의 global counter
 *
 * 반환값:
 *   사용할 preamble code (9-24 범위)
 *
 * 동작:
 *   BLE CSA#2 PRNG로 prn_e 생성 후 12개 코드 중 선택
 */
static uint8_t get_preamble_code_for_period(uint16_t counter) {
    uint16_t prn_e = generate_prn_e(counter, CHANNEL_ID);
    uint8_t idx = prn_e % PREAMBLE_CODE_COUNT;
    return VALID_PREAMBLE_CODES[idx];
}

/* Period-level tracking (reset every odd period) */
static uint8_t data_received_from[TOTAL_ARRAY_SIZE] = {0};
static uint8_t unique_data_count = 0;

/* Cycle-level cumulative tracking (reset every cycle) */
static uint8_t cumulative_ack_confirmed[TOTAL_ARRAY_SIZE] = {0};
static uint8_t cumulative_ack_count = 0;

/* Slot index mappings for 12-slot system */
#define SLOT_IDX_INIT       0   // INIT (SEQ 1)
#define SLOT_IDX_NODE_2     1   // Node 2 (SEQ 2)
#define SLOT_IDX_NODE_3     2   // Node 3 (SEQ 3)
#define SLOT_IDX_NODE_4     3   // Node 4 (SEQ 4)
#define SLOT_IDX_NODE_5     4   // Node 5 (SEQ 5)
#define SLOT_IDX_NODE_6     5   // Node 6 (SEQ 6)
#define SLOT_IDX_NODE_7     6   // Node 7 (SEQ 7)
#define SLOT_IDX_NODE_8     7   // Node 8 (SEQ 8)
#define SLOT_IDX_FL         8   // FL (SEQ 9)
#define SLOT_IDX_FL_RELAY   9   // FL Relay by INIT (SEQ 10)
#define SLOT_IDX_FR         10  // FR (SEQ 11)
#define SLOT_IDX_FR_RELAY   11  // FR Relay by INIT (SEQ 12)

/* Forward declarations */
static uint8_t node_id_to_index(uint8_t node_id);
static uint8_t index_to_node_id(uint8_t index);

/*
 * 메시지에서 슬롯 인덱스 추출
 *
 * 파라미터:
 *   msg_type: 메시지 타입 (MSG_TYPE_DATA 또는 MSG_TYPE_RELAY_DATA)
 *   src_node: 송신 노드 ID
 *   orig_src: 원본 송신자 (릴레이 메시지의 경우)
 *
 * 반환값:
 *   슬롯 인덱스 (0-11), 잘못된 경우 0xFF
 *
 * 동작:
 *   - DATA: src_node로 인덱스 계산
 *   - RELAY_DATA: orig_src로 릴레이 슬롯 인덱스 반환
 */
static uint8_t get_slot_index(uint8_t msg_type, uint8_t src_node, uint8_t orig_src) {
    if (msg_type == MSG_TYPE_DATA) {
        return node_id_to_index(src_node);
    } else if (msg_type == MSG_TYPE_RELAY_DATA) {
        if (orig_src == NODE_FL) return SLOT_IDX_FL_RELAY;
        if (orig_src == NODE_FR) return SLOT_IDX_FR_RELAY;
    }
    return 0xFF;
}

/*
 * 해당 슬롯으로부터 ACK를 기대하는지 체크
 *
 * 파라미터:
 *   ack_slot_idx: ACK 송신자의 슬롯 인덱스
 *   tx_slot_idx: 자신의 TX 슬롯 인덱스
 *
 * 반환값: true=ACK 기대함, false=ACK 기대 안함
 *
 * 규칙:
 *   - 자기 자신 제외
 *   - 릴레이 슬롯 제외
 *   - FL/FR 노드: INIT + 상대방만 기대
 *   - 일반 노드: FL/FR 제외한 모든 노드
 */
static bool is_expected_ack_slot_for(uint8_t ack_slot_idx, uint8_t tx_slot_idx) {
    #if TEST_MODE
    uint8_t my_slot_idx = node_id_to_index(TEST_MY_NODE_ID);

    if (ack_slot_idx == my_slot_idx) return false;
    if (ack_slot_idx == SLOT_IDX_FL_RELAY || ack_slot_idx == SLOT_IDX_FR_RELAY) return false;
    if (ack_slot_idx >= TOTAL_ARRAY_SIZE) return false;

    if (TEST_MY_NODE_ID == NODE_FL) {
        if (ack_slot_idx == SLOT_IDX_INIT) return true;
        if (ack_slot_idx == SLOT_IDX_FR) return true;
        return false;
    } else if (TEST_MY_NODE_ID == NODE_FR) {
        if (ack_slot_idx == SLOT_IDX_INIT) return true;
        if (ack_slot_idx == SLOT_IDX_FL) return true;
        return false;
    } else {
        if (ack_slot_idx == SLOT_IDX_FL || ack_slot_idx == SLOT_IDX_FR) return false;
        return true;
    }
    #else
    return (ack_slot_idx != tx_slot_idx && ack_slot_idx < TOTAL_NODES);
    #endif
}

/*
 * 슬롯 인덱스에 대한 설명 문자열 반환
 *
 * 파라미터:
 *   slot_idx: 슬롯 인덱스 (0-11)
 *
 * 반환값: 슬롯 설명 문자열 (로깅용)
 */
static const char* get_slot_description(uint8_t slot_idx) {
    switch(slot_idx) {
        case SLOT_IDX_INIT: return "INIT";
        case SLOT_IDX_NODE_2: return "N2";
        case SLOT_IDX_NODE_3: return "N3";
        case SLOT_IDX_NODE_4: return "N4";
        case SLOT_IDX_NODE_5: return "N5";
        case SLOT_IDX_NODE_6: return "N6";
        case SLOT_IDX_NODE_7: return "N7";
        case SLOT_IDX_NODE_8: return "N8";
        case SLOT_IDX_FL: return "FL";
        case SLOT_IDX_FL_RELAY: return "FL_REL";
        case SLOT_IDX_FR: return "FR";
        case SLOT_IDX_FR_RELAY: return "FR_REL";
        default: return "???";
    }
}

/*
 * 노드 ID 문자를 배열 인덱스로 변환
 *
 * 파라미터:
 *   node_id: 노드 ID 문자 ('1'-'9', 'A')
 *
 * 반환값:
 *   배열 인덱스 (0-10), 잘못된 ID인 경우 0xFF
 *
 * 매핑:
 *   '1'(INIT)→0, '2'→1, ..., '9'(FL)→8, 'A'(FR)→10
 */
static uint8_t node_id_to_index(uint8_t node_id) {
    switch(node_id) {
        case NODE_INIT: return 0;
        case NODE_2:    return 1;
        case NODE_3:    return 2;
        case NODE_4:    return 3;
        case NODE_5:    return 4;
        case NODE_6:    return 5;
        case NODE_7:    return 6;
        case NODE_8:    return 7;
        case NODE_FL:   return 8;
        case NODE_FR:   return 10;
        default:        return 0xFF;
    }
}

/*
 * 배열 인덱스를 노드 ID 문자로 변환
 *
 * 파라미터:
 *   index: 배열 인덱스 (0-10)
 *
 * 반환값:
 *   노드 ID 문자, 잘못된 인덱스면 '?'
 *
 * 매핑:
 *   0→'1'(INIT), 1→'2', ..., 8→'9'(FL), 10→'A'(FR)
 *   9, 11은 릴레이 슬롯이므로 '?' 반환
 */
static uint8_t index_to_node_id(uint8_t index) {
    switch(index) {
        case 0:  return NODE_INIT;
        case 1:  return NODE_2;
        case 2:  return NODE_3;
        case 3:  return NODE_4;
        case 4:  return NODE_5;
        case 5:  return NODE_6;
        case 6:  return NODE_7;
        case 7:  return NODE_8;
        case 8:  return NODE_FL;
        case 10: return NODE_FR;
        default: return '?';
    }
}

/*
 * Normal 노드 Aggregated ACKs 프로토콜 메인 함수
 *
 * 프로토콜 개요:
 *   - TDMA 기반 UWB MAC 프로토콜
 *   - 21.2ms 주기, 12-슬롯 구조
 *   - Period 홀수(1,3,5): DATA 전송/수신
 *   - Period 짝수(2,4,6): ACK_ARRAY 전송/수신
 *
 * Normal 노드 동작:
 *   - SYNC 수신으로 Period 동기화
 *   - 자신의 슬롯에서 DATA/ACK_ARRAY 전송
 *   - 다른 노드의 DATA 수신 및 ACK_ARRAY로 확인 응답
 *
 * 메인 루프 구조:
 *   [A] SYNC Loss Detection (27ms 타임아웃)
 *   [B] SYNC Timeout 및 최종 통계 (5초)
 *   [C] Config 전환 (17.2ms 시점)
 *   [D] 슬롯 타이밍 체크 및 TX
 *   [E] RX 폴링: 메시지 수신 처리
 *   [F] RX 타임아웃/에러 처리
 */
int dwt_normal_preamble_hop(void)
{
    uint32_t status_reg = 0;
    uint16_t frame_len = 0;

    /* Display application name on LCD. */
    test_run_info((unsigned char *)APP_NAME);

    /* Configure SPI rate, DW3000 supports up to 38 MHz */
    port_set_dw_ic_spi_fastrate();

    /* Reset DW IC */
    reset_DWIC(); /* Target specific drive of RSTn line into DW IC low for a period. */

    Sleep(2); // Time needed for DW3000 to start up (transition from INIT_RC to IDLE_RC, or could wait for SPIRDY event)

    /* Probe for the correct device driver. */
    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    while (!dwt_checkidlerc()) /* Need to make sure DW IC is in IDLE_RC before proceeding */ { };

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        test_run_info((unsigned char *)"INIT FAILED     ");
        while (1) { };
    }

    /* Enabling LEDs here for debug - SAME AS WORKING EXAMPLE */
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

    /* Configure DW IC. See NOTE 2 below. */
    /* if the dwt_configure returns DWT_ERROR either the PLL or RX calibration has failed the host should reset the device */
    test_run_info((unsigned char *)"Starting configuration...");
    int config_result = dwt_configure(&config_sync);
    if (config_result)
    {
        static char config_error[100];
        snprintf(config_error, sizeof(config_error), "CONFIG FAILED - Error code: %d", config_result);
        test_run_info((unsigned char *)config_error);
        while (1) { };
    } else {
        test_run_info((unsigned char *)"Configuration successful - SYNC mode (PLEN512)");
    }

    /* Configure the TX spectrum parameters (power, PG delay and PG count) */
    /* Linear TX Power 설정 - dwt_calculate_linear_tx_setting API 사용 */
    power_indexes_t power_indexes = {0};
    tx_adj_res_t linear_results = {0};
    dwt_txconfig_t linear_txconfig;

    /* 모든 프레임 부분에 동일한 인덱스 적용 */
    power_indexes.input[0] = USE_TX_POWER_INDEX;  // DATA
    power_indexes.input[1] = USE_TX_POWER_INDEX;  // PHR
    power_indexes.input[2] = USE_TX_POWER_INDEX;  // SHR
    power_indexes.input[3] = USE_TX_POWER_INDEX;  // STS

    if (dwt_calculate_linear_tx_setting((int)config_data.chan, &power_indexes, &linear_results) == DWT_SUCCESS) {
        linear_txconfig.power = linear_results.tx_frame_cfg.tx_power_setting;
        linear_txconfig.PGcount = txconfig_options.PGcount;
        linear_txconfig.PGdly = txconfig_options.PGdly;

        dwt_configuretxrf(&linear_txconfig);
        dwt_set_pll_config(linear_results.tx_frame_cfg.pll_cfg);

        /* 디버그 출력: 적용된 TX Power 설정 */
        static char tx_power_debug[150];
        snprintf(tx_power_debug, sizeof(tx_power_debug),
                 "TX Power Index: %d (%.2fdB), Setting: 0x%08lX, PLL: 0x%X",
                 USE_TX_POWER_INDEX, -(float)USE_TX_POWER_INDEX * 0.25,
                 linear_results.tx_frame_cfg.tx_power_setting,
                 linear_results.tx_frame_cfg.pll_cfg);
        test_run_info((unsigned char *)tx_power_debug);
    } else {
        test_run_info((unsigned char *)"TX Power API failed, using default txconfig_options");
        dwt_configuretxrf(&txconfig_options);
    }

    /* POLLING MODE - No interrupt callbacks */
    /* Disable all interrupts to prevent SPI lock issues */
    dwt_setinterrupt(0, 0, DWT_ENABLE_INT);  // Disable all interrupts

    /* Clear any pending interrupts */
    dwt_writesysstatuslo(0xFFFFFFFF);  // Clear all status flags

    test_run_info((unsigned char *)"Normal Node initialized in POLLING MODE (no interrupts)");

    /* Print current configuration for debugging */
    static char config_debug[200];
    snprintf(config_debug, sizeof(config_debug),
             "CONFIG: SYNC(512 symbols) DATA(64 symbols) Ch=%d, TxCode=%d, RxCode=%d",
             config_sync.chan, config_sync.txCode, config_sync.rxCode);
    test_run_info((unsigned char *)config_debug);

    /* Disable RX timeout - same as working example */
    dwt_setrxtimeout(0);

    /* Initialize DWT cycle counter for SPI-free timing */
    dwt_timer_init();

    /* Initialize preamble code for first SYNC reception */
    /* MUST match INIT's first preamble code (global_period_counter=0) */
    uint8_t initial_preamble = get_preamble_code_for_period(0);
    next_sync_preamble = initial_preamble;
    config_sync.txCode = initial_preamble;
    config_sync.rxCode = initial_preamble;
    config_data.txCode = initial_preamble;
    config_data.rxCode = initial_preamble;

    /* Re-configure with correct initial preamble code */
    dwt_configure(&config_sync);

    static char init_preamble_log[100];
    snprintf(init_preamble_log, sizeof(init_preamble_log),
            "Initial preamble code for first SYNC: %d", initial_preamble);
    test_run_info((unsigned char *)init_preamble_log);

    /* Calculate SYNC timeout in CPU cycles (5 seconds for final statistics) */
    sync_timeout_cycles = us_to_cpu_cycles(SYNC_TIMEOUT_SECONDS * 1000000);  // 5 seconds = 5,000,000 microseconds

    /* Calculate SYNC loss timeout in CPU cycles (27ms) */
    sync_loss_timeout_cycles = us_to_cpu_cycles(SYNC_LOSS_TIMEOUT_MS * 1000);  // 27ms = 27,000 microseconds

    /* CPU cycle-based slot timing variables */
    uint32_t last_sync_cycles = 0;                          // When SYNC was received (CPU cycles)
#if TEST_MODE
    #if defined(TEST_MY_SLOT_START_MS)
        uint32_t slot_interval_cycles = us_to_cpu_cycles((uint32_t)(TEST_MY_SLOT_START_MS * 1000));  // FR node: 13.5ms
    #else
        uint32_t slot_interval_cycles = us_to_cpu_cycles(2000 + (TEST_MY_NODE_SEQ-1) * 1100);  // 2ms buffer + 1.1ms slots
    #endif
#else
    uint32_t slot_interval_cycles = us_to_cpu_cycles(2000 + (MY_NODE_SEQ-1) * 1100);       // 2ms buffer + 1.1ms slots
#endif
    uint32_t slot_duration_cycles = us_to_cpu_cycles(100);        // 0.1ms for TX only
    bool slot_active = false;                               // Track if currently in own slot
    bool slot_executed_this_sync = false;                   // Flag to ensure slot executes only once per SYNC

    /* Period timer for config switching - 10.7ms (4.5ms buffer for SYNC reception) */
    uint32_t config_switch_cycles = us_to_cpu_cycles(CONFIG_SWITCH_MS * 1000);  // 10.7ms after SYNC
    bool config_is_sync = true;                                 // Start with SYNC config

    /* Debug: Print calculated intervals */
    static char interval_debug[150];
#if TEST_MODE
    snprintf(interval_debug, sizeof(interval_debug),
             "Node SEQ %d: slot=%u cycles (%.1fms) [2ms buffer + 0.1ms TX + 1.0ms guard]",
             TEST_MY_NODE_SEQ, slot_interval_cycles, (float)(2000 + (TEST_MY_NODE_SEQ-1) * 1100) / 1000);
#else
    snprintf(interval_debug, sizeof(interval_debug),
             "DWT Intervals: period=20.2ms, slot=%u cycles (%.1fms) [2ms buffer + 0.1ms TX + 1.0ms guard]",
              slot_interval_cycles);
#endif
    test_run_info((unsigned char *)interval_debug);

    test_run_info((unsigned char *)"Starting DWT cycle counter RX loop (POLLING MODE): 21.2ms period + 1.1ms slot timing + 4.0ms SYNC buffer");

    /* Enable RX immediately for continuous listening - no critical section needed in polling mode */
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    while (1)
    {
        /*
         * ========== [A] SYNC Loss Detection (27ms 타임아웃) ==========
         *
         * 조건: last_sync_cycles != 0 && !sync_lost && 27ms 경과
         * 동작:
         *   1. sync_lost = true 설정, TX 중단
         *   2. global_period_counter++ (자체적으로 증가)
         *   3. 다음 preamble 예측하여 config_sync에 적용
         *   4. RX 재활성화로 복구 시도
         * 복구: INIT의 실제 GC와 일치하면 다음 SYNC 수신 가능
         */
        if (last_sync_cycles != 0 && !sync_lost) {
            uint32_t current_cycles = dwt_timer_get_cycles();
            if (dwt_timer_elapsed(last_sync_cycles, sync_loss_timeout_cycles)) {
                sync_lost = true;
                sync_loss_stats.total_timeouts++;

                /* Self-increment counter to match INIT's counter */
                global_period_counter++;

                /* Predict next preamble code using self-incremented counter */
                uint8_t predicted_preamble = get_preamble_code_for_period(global_period_counter);
                next_sync_preamble = predicted_preamble;

                /* Apply predicted preamble to config_sync */
                config_sync.txCode = predicted_preamble;
                config_sync.rxCode = predicted_preamble;

                /* Reconfigure to predicted preamble */
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    config_is_sync = true;
                }
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);

                /* Reset timing reference for next timeout check */
                last_sync_cycles = current_cycles;

                static char timeout_log[150];
                snprintf(timeout_log, sizeof(timeout_log),
                        "[SYNC LOST] GC++ -> %u, Predicted Preamble=%d - Trying recovery",
                        global_period_counter, predicted_preamble);
                test_run_info((unsigned char *)timeout_log);
            }
        }

        /*
         * ========== [B] SYNC Timeout 및 최종 통계 (5초) ==========
         *
         * 조건: last_sync_cycles != 0 && !final_stats_printed && 5초 경과
         * 동작:
         *   1. 현재 사이클 평가 (성공/실패 카운트)
         *   2. Pair별 통계 출력 (Period 1-2, 3-4, 5-6)
         *   3. Cycle 성공률 출력
         *   4. SYNC Loss 통계 출력
         *   5. 노드별 지연시간 통계 출력
         *   6. final_stats_printed = true 후 루프 종료
         */
        if (last_sync_cycles != 0 && !final_stats_printed) {
            uint32_t current_cycles = dwt_timer_get_cycles();
            if (dwt_timer_elapsed(last_sync_cycles, sync_timeout_cycles)) {
                /* Evaluate current cycle before printing statistics */
                if (current_cycle >= 1) {
                    total_cycles++;
                    if (success_in_current_cycle) {
                        successful_cycles++;
                    } else {
                        failed_cycles++;
                        /* Store failed cycle number (최근 10개만 유지) */
                        if (failed_cycles <= MAX_FAILED_CYCLES_LOG) {
                            failed_cycle_numbers[failed_cycles - 1] = current_cycle;
                        }
                    }
                }

                test_run_info((unsigned char *)"SYNC TIMEOUT - No SYNC received for 5 seconds");

                /* Print final statistics */
                static char final_stats[800];
                /* 1000개 기준 퍼센트 계산 */
                float success_rate = (float)successful_cycles / 1000.0 * 100;
                float fail_rate = (float)failed_cycles / 1000.0 * 100;

                snprintf(final_stats, sizeof(final_stats),
                        "\n=== PAIR STATISTICS (3 cycles) ===\n"
                        "Pair 1 (Period 1-2): Success=%d, Fail=%d, IDLE=%d\n"
                        "Pair 2 (Period 3-4): Success=%d, Fail=%d, IDLE=%d\n"
                        "Pair 3 (Period 5-6): Success=%d, Fail=%d, IDLE=%d\n\n"
                        "=== CYCLE STATISTICS (based on 1000 cycles) ===\n"
                        "Total Cycles: %d / 1000\n"
                        "Successful Cycles: %d (%.2f%%)\n"
                        "Failed Cycles: %d (%.2f%%)",
                        pair1_success, pair1_fail, pair1_idle,
                        pair2_success, pair2_fail, pair2_idle,
                        pair3_success, pair3_fail, pair3_idle,
                        total_cycles,
                        successful_cycles, success_rate,
                        failed_cycles, fail_rate);
                test_run_info((unsigned char *)final_stats);

                /* Print failed cycle numbers (최근 10개) */
                if (failed_cycles > 0) {
                    static char failed_cycles_msg[300];
                    int pos = snprintf(failed_cycles_msg, sizeof(failed_cycles_msg),
                                      "\nFailed Cycles (first %d): ",
                                      (failed_cycles < MAX_FAILED_CYCLES_LOG) ? failed_cycles : MAX_FAILED_CYCLES_LOG);

                    uint32_t max_to_print = (failed_cycles < MAX_FAILED_CYCLES_LOG) ? failed_cycles : MAX_FAILED_CYCLES_LOG;
                    for (uint32_t i = 0; i < max_to_print && pos < sizeof(failed_cycles_msg) - 10; i++) {
                        pos += snprintf(failed_cycles_msg + pos, sizeof(failed_cycles_msg) - pos,
                                       "%d ", failed_cycle_numbers[i]);
                    }
                    test_run_info((unsigned char *)failed_cycles_msg);
                }

                /* Print SYNC Loss statistics */
                static char sync_loss_output[450];
                uint32_t total_skips = sync_loss_stats.data_skips + sync_loss_stats.ack_skips;

                snprintf(sync_loss_output, sizeof(sync_loss_output),
                        "\n=== SYNC LOSS STATISTICS ===\n"
                        "Total SYNC Timeouts: %u (27ms timeout)\n"
                        "Total TX Skips: %u (DATA: %u, ACK_ARRAY: %u)\n"
                        "Skip Count by Period:\n"
                        "  Period 1: %u skips\n"
                        "  Period 2: %u skips\n"
                        "  Period 3: %u skips\n"
                        "  Period 4: %u skips\n"
                        "  Period 5: %u skips\n"
                        "  Period 6: %u skips\n",
                        sync_loss_stats.total_timeouts,
                        total_skips, sync_loss_stats.data_skips, sync_loss_stats.ack_skips,
                        sync_loss_stats.period_skip_count[0],
                        sync_loss_stats.period_skip_count[1],
                        sync_loss_stats.period_skip_count[2],
                        sync_loss_stats.period_skip_count[3],
                        sync_loss_stats.period_skip_count[4],
                        sync_loss_stats.period_skip_count[5]);
                test_run_info((unsigned char *)sync_loss_output);

                /* Print per-node latency statistics */
                test_run_info((unsigned char *)"\n=== ONE-WAY LATENCY BY NODE ===");
                for (uint8_t i = 0; i < TOTAL_ARRAY_SIZE; i++) {
                    latency_stats_t *stats = &node_latency[i];

                    /* Only print nodes with samples */
                    if (stats->count > 0) {
                        float min_ms = stats->min_us / 1000.0;
                        float max_ms = stats->max_us / 1000.0;
                        float avg_us = (float)stats->sum_us / stats->count;
                        float avg_ms = avg_us / 1000.0;

                        static char latency_output[200];
                        snprintf(latency_output, sizeof(latency_output),
                                "%s (slot %d):\n"
                                "  Min: %u us (%.3f ms)\n"
                                "  Max: %u us (%.3f ms)\n"
                                "  Avg: %.1f us (%.3f ms)\n"
                                "  Samples: %u",
                                get_slot_description(i), i,
                                stats->min_us, min_ms,
                                stats->max_us, max_ms,
                                avg_us, avg_ms,
                                stats->count);
                        test_run_info((unsigned char *)latency_output);
                    }
                }

                final_stats_printed = true;
                break;
            }
        }

        /*
         * ========== [C] Config 전환 (17.2ms 시점) ==========
         *
         * 조건: last_sync_cycles != 0 && !config_is_sync && 17.2ms 경과
         * 동작:
         *   1. dwt_forcetrxoff()로 RX 중단
         *   2. config_sync (PLEN512)로 설정 변경
         *   3. config_is_sync = true
         *   4. RX 재활성화
         * 목적: 다음 SYNC 수신을 위한 4.0ms 버퍼 확보 (21.2ms - 17.2ms)
         */
        if (last_sync_cycles != 0 && !config_is_sync) {
            uint32_t current_cycles = dwt_timer_get_cycles();
            if (dwt_timer_elapsed(last_sync_cycles, config_switch_cycles)) {
                /* Apply next_sync_preamble (received from INIT's SYNC message) to config_sync */
                config_sync.txCode = next_sync_preamble;
                config_sync.rxCode = next_sync_preamble;

                /* Switch to SYNC config at 17.2ms for 4.0ms buffer before next SYNC at 21.2ms */
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    static char switch_msg[120];
                    snprintf(switch_msg, sizeof(switch_msg),
                            "SEQ %d: Switched to SYNC config at 17.2ms, NextPreamble=%d",
                            MY_NODE_SEQ, next_sync_preamble);
                    test_run_info((unsigned char *)switch_msg);
                    config_is_sync = true;
                } else {
                    test_run_info((unsigned char *)"WARNING: Failed to switch to SYNC config!");
                }
                /* Re-enable RX after config change */
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
        }

        /*
         * ========== [D] 슬롯 타이밍 체크 및 TX ==========
         *
         * 조건: last_sync_cycles != 0 && !slot_active && !slot_executed_this_sync
         * 동작: 자신의 슬롯 시간이 되면 TX 수행
         *
         * 하위 분기:
         *   [D-1] SYNC Loss 시 TX 스킵
         *   [D-2] Period 홀수(1,3,5): DATA TX
         *   [D-3] Period 짝수(2,4,6): ACK_ARRAY TX
         */
        if (last_sync_cycles != 0 && !slot_active && !slot_executed_this_sync) {
            /* Get current CPU cycle count - no SPI access needed */
            uint32_t current_cycles = dwt_timer_get_cycles();
            uint32_t cycles_since_sync = current_cycles - last_sync_cycles;

            /* Check slot condition using DWT cycles */
            bool sync_valid = (last_sync_cycles != 0);
            bool time_advanced = (current_cycles != last_sync_cycles);
            bool slot_time_reached = dwt_timer_elapsed(last_sync_cycles, slot_interval_cycles);
            bool slot_not_executed = (!slot_executed_this_sync);

            /* Small delay to prevent overwhelming the processor - reduced for ms timing */
            //nrf_delay_us(10); // 10 microseconds delay

            /* Check if slot interval has elapsed */
            if (sync_valid && time_advanced && slot_time_reached && slot_not_executed) {

                /* Debug: Show exact timing when slot starts */
                static char slot_timing_debug[200];
                snprintf(slot_timing_debug, sizeof(slot_timing_debug),
                        "[%ums] Own slot start! cycles_since_sync=%u, slot_interval=%u, current_cycles=%u",
                        get_elapsed_ms(last_sync_cycles), cycles_since_sync, slot_interval_cycles, current_cycles);
                test_run_info((unsigned char *)slot_timing_debug);
                slot_active = true;
                slot_executed_this_sync = true;  // Mark slot as executed for this SYNC

                /*
                 * ----- [D-1] SYNC Loss 시 TX 스킵 -----
                 *
                 * 조건: sync_lost == true
                 * 동작: DATA/ACK_ARRAY TX 모두 스킵, 통계 업데이트
                 * 복구: 다음 SYNC 수신 시 자동 복구
                 */
                if (sync_lost) {
                    bool is_data_period = (current_period_in_cycle % 2 == 1);
                    static char skip_log[150];

                    if (is_data_period) {
                        sync_loss_stats.data_skips++;
                        snprintf(skip_log, sizeof(skip_log),
                                "[SYNC LOST] Skipping DATA TX in Period %d - waiting for SYNC recovery",
                                current_period_in_cycle);
                    } else {
                        sync_loss_stats.ack_skips++;
                        snprintf(skip_log, sizeof(skip_log),
                                "[SYNC LOST] Skipping ACK_ARRAY TX in Period %d - waiting for SYNC recovery",
                                current_period_in_cycle);
                    }

                    sync_loss_stats.period_skip_count[current_period_in_cycle - 1]++;
                    test_run_info((unsigned char *)skip_log);

                    slot_active = false;
                    /* slot_executed_this_sync already set to true above - no retry */
                    continue;  /* Skip to next iteration, do not transmit */
                }

                /* Decide what to transmit based on period and state */
                bool should_transmit = false;
                const char* tx_type_desc = "";
                bool is_data_period = (current_period_in_cycle % 2 == 1);  // Odd period = DATA

                /*
                 * ----- [D-2] Period 홀수(1,3,5): DATA TX -----
                 *
                 * 조건: is_data_period == true
                 * 동작:
                 *   - TX_STATE_IDLE: 이미 성공 → 스킵
                 *   - Period 1: 새 DATA 프레임 생성, retrans_msg에 저장
                 *   - Period 3,5: retrans_msg 재전송
                 */
                if (is_data_period) {
                    if (tx_state == TX_STATE_IDLE) {
                        should_transmit = false;
                        tx_type_desc = "SKIP (already successful)";
                    } else {
                        /* Need to transmit DATA */
                        should_transmit = true;

                        /* CRITICAL: Only prepare NEW message in Period 1 */
                        /* Period 3, 5 should retransmit the SAME message from Period 1 */
                        if (current_period_in_cycle == 1) {
                            /* Period 1: Prepare NEW DATA frame */
                            tx_type_desc = "DATA FIRST TX";
                            tx_msg[0] = 0xC5;
                            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
#if TEST_MODE
                            tx_msg[IDX_SOURCE] = TEST_MY_NODE_ID;
#else
                            tx_msg[IDX_SOURCE] = NODE_2;
#endif
                            tx_msg[IDX_DEST] = NODE_ALL;
                            tx_msg[IDX_PRIORITY] = 1;

                            /* Add TX timestamp (SYNC-relative time in microseconds) */
                            uint32_t tx_timestamp_us = (dwt_timer_get_cycles() - last_sync_cycles) / 64;
                            memcpy(&tx_msg[IDX_TX_TIMESTAMP], &tx_timestamp_us, sizeof(uint32_t));

                            /* Store for potential retransmission in Period 3, 5 */
                            memcpy(retrans_msg, tx_msg, sizeof(tx_msg));
                        } else {
                            /* Period 3, 5: RETRANSMIT same message from Period 1 */
                            tx_type_desc = "DATA RETRANS";
                            memcpy(tx_msg, retrans_msg, sizeof(tx_msg));
                        }
                    }
                }
                /*
                 * ----- [D-3] Period 짝수(2,4,6): ACK_ARRAY TX -----
                 *
                 * 조건: is_data_period == false
                 * 동작:
                 *   - 항상 전송 (should_transmit = true)
                 *   - data_received_from[] 배열을 페이로드에 복사
                 *   - 수신한 DATA에 대한 집합적 ACK 응답
                 */
                else {
                    should_transmit = true;
                    tx_type_desc = "ACK_ARRAY TX";

                    tx_msg[0] = 0xC5;
                    tx_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK_ARRAY;
#if TEST_MODE
                    tx_msg[IDX_SOURCE] = TEST_MY_NODE_ID;
#else
                    tx_msg[IDX_SOURCE] = NODE_2;
#endif
                    tx_msg[IDX_DEST] = NODE_ALL;
                    tx_msg[IDX_PRIORITY] = 1;

                    /* Copy data_received_from array to payload */
                    memcpy(&tx_msg[IDX_ACK_ARRAY], data_received_from, TOTAL_ARRAY_SIZE);

                    /* DEBUG: Print data_received_from array as hex */
                    static char data_hex_debug[200];
                    int hex_pos = snprintf(data_hex_debug, sizeof(data_hex_debug), "data_received_from[]: ");
                    for (int i = 0; i < TOTAL_ARRAY_SIZE && hex_pos < sizeof(data_hex_debug) - 10; i++) {
                        hex_pos += snprintf(data_hex_debug + hex_pos, sizeof(data_hex_debug) - hex_pos,
                                          "%02X ", data_received_from[i]);
                    }
                    test_run_info((unsigned char *)data_hex_debug);

                    static char ack_array_debug[150];
                    int pos = snprintf(ack_array_debug, sizeof(ack_array_debug), "ACK_ARRAY payload: ");
                    for (int i = 0; i < TOTAL_ARRAY_SIZE && pos < sizeof(ack_array_debug) - 10; i++) {
                        if (data_received_from[i]) {
                            pos += snprintf(ack_array_debug + pos, sizeof(ack_array_debug) - pos,
                                          "%s ", get_slot_description(i));
                        }
                    }
                    test_run_info((unsigned char *)ack_array_debug);
                }

                static char tx_decision_msg[150];
                snprintf(tx_decision_msg, sizeof(tx_decision_msg),
                        "[%ums] TX Decision: %s - State: %d, Period: %d/%d, Attempt: %d",
                        get_elapsed_ms(last_sync_cycles), tx_type_desc, tx_state, current_period_in_cycle, PERIODS_PER_CYCLE, retrans_attempt);
                test_run_info((unsigned char *)tx_decision_msg);

                if (should_transmit) {
                    /* Disable RX temporarily for TX */
                    dwt_forcetrxoff();

                    /* Send frame */
                    dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
                    dwt_writetxfctrl(sizeof(tx_msg), 0, 0);

                    int tx_result = dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                    if (tx_result == DWT_SUCCESS) {
                    /* Wait for TX completion */
                    uint32_t tx_status = 0;
                    waitforsysstatus(&tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                    if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
                        test_run_info((unsigned char *)"TX completed, returning to continuous RX");
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                        /* Note: retrans_msg is already stored in Period 1 preparation */

                        /* Immediately return to RX mode for continuous listening */
                        dwt_forcetrxoff();
                        dwt_writesysstatuslo(0xFFFFFFFF);
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);

                        /* Increment sequence number */
                        tx_msg[BLINK_FRAME_SN_IDX]++;
                    }
                    } else {
                        test_run_info((unsigned char *)"DATA TX failed to start");
                    }
                } else {
                    /* Not transmitting - just log and finish slot */
                    test_run_info((unsigned char *)"Slot completed - No transmission needed");

                    /* Re-enable RX for normal operation */
                    dwt_forcetrxoff();
                    dwt_writesysstatuslo(0xFFFFFFFF);
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                }

                /* Slot finished */
                slot_active = false;
            }
        }

        /* Note: No ACK collection end check - we use continuous RX mode */

        /*
         * ========== [E] RX 폴링: 메시지 수신 처리 ==========
         *
         * 조건: status_reg & DWT_INT_RXFCG_BIT_MASK (프레임 수신 완료)
         * 동작: 메시지 타입별 처리
         *
         * 하위 분기:
         *   [E-1] SYNC 메시지 처리 (타이밍 기준점 설정)
         *   [E-2] Period 홀수 DATA 처리 (data_received_from 업데이트)
         *   [E-3] Period 짝수 ACK_ARRAY 처리 (ACK 확인)
         */
        uint32_t status_reg = dwt_readsysstatuslo();
        if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
            /* Get frame length */
            uint16_t frame_len = FRAME_LEN_MAX;

            /* Clear buffer and read frame */
            memset(rx_buffer, 0, sizeof(rx_buffer));
            dwt_readrxdata(rx_buffer, frame_len, 0);
            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

            /* Process all received messages */
            uint32_t elapsed_ms = get_elapsed_ms(last_sync_cycles);

            /* Print received frame for debugging */
            static char rx_log[100];
            snprintf(rx_log, sizeof(rx_log), "[%ums] RX: type=0x%02X, src=%c",
                    elapsed_ms, rx_buffer[IDX_MSG_TYPE], rx_buffer[IDX_SOURCE]);
            test_run_info((unsigned char *)rx_log);

            /*
             * ----- [E-1] SYNC 메시지 처리 -----
             *
             * 동작:
             *   1. last_sync_cycles 갱신 (타이밍 기준점)
             *   2. sync_lost 복구 처리
             *   3. Period 정보 추출 및 업데이트
             *   4. 새 Cycle 시작 시 상태 초기화
             *   5. config_data로 전환, RX 재활성화
             */
            if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_SYNC) {
                /* CRITICAL: Set timing reference IMMEDIATELY to minimize drift */
                uint32_t current_cycles = dwt_timer_get_cycles();
                uint32_t prev_sync_cycles = last_sync_cycles;  // Store for logging
                last_sync_cycles = current_cycles;  // SET FIRST before any delays
                slot_executed_this_sync = false;
                config_is_sync = false;

                /* ========== SYNC에서 Period 정보 추출 - 완전 신뢰 ========== */
                uint8_t sync_period = rx_buffer[IDX_PERIOD_INFO];  // SYNC's period info (1-6)

                /* ========== Extract Preamble Codes from SYNC message ========== */
                uint8_t current_preamble = rx_buffer[IDX_CURRENT_PREAMBLE];  // Current period DATA/ACK
                uint8_t next_preamble = rx_buffer[IDX_NEXT_PREAMBLE];        // Next period SYNC

                /* Store next_preamble for 17.2ms config switch */
                next_sync_preamble = next_preamble;

                /* Increment global counter (we count periods ourselves) */
                global_period_counter++;

                /* Reset data_received_from ONLY at odd period start (DATA periods) */
                /* Even periods need previous DATA info for ACK_ARRAY transmission */
                if (sync_period % 2 == 1) {
                    memset(data_received_from, 0, sizeof(data_received_from));
                    unique_data_count = 0;
                }

                /* Apply current preamble code to DATA config */
                config_data.txCode = current_preamble;
                config_data.rxCode = current_preamble;
                /* Note: config_sync will use next_sync_preamble at 17.2ms switch */

                /* Clear SYNC loss flag and log recovery if needed */
                if (sync_lost) {
                    static char recovery_log[150];
                    snprintf(recovery_log, sizeof(recovery_log),
                            "[SYNC RECOVERED] P%d cur=%d, next=%d stored - TX resumed",
                            sync_period, current_preamble, next_preamble);
                    test_run_info((unsigned char *)recovery_log);
                    sync_lost = false;
                }

                /* CRITICAL: Clear both hardware and software RX buffers */
                /* This discards any old messages from previous period */
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);  // Clear hardware RX buffer and status
                memset(rx_buffer, 0, sizeof(rx_buffer));   // Clear software buffer

                /* Calculate and log SYNC reception interval for timing drift detection */
                if (prev_sync_cycles != 0) {
                    uint32_t cycles_since_last_sync = current_cycles - prev_sync_cycles;
                    float ms_since_last_sync = cycles_since_last_sync / 64.0;
                    static char sync_interval_log[150];
                    snprintf(sync_interval_log, sizeof(sync_interval_log),
                            "SYNC interval: %.2fms, cur=%d, next=%d stored",
                            ms_since_last_sync, current_preamble, next_preamble);
                    test_run_info((unsigned char *)sync_interval_log);
                }

                /* Update period directly from SYNC - no verification, trust completely */
                period_count++;
                current_period_in_cycle = sync_period;  // Use SYNC's period info directly

                /* Check for new cycle */
                if (current_period_in_cycle == 1) {
                    /* New cycle started */
                    current_cycle++;

                    /* Evaluate previous cycle (if not first cycle) */
                    if (current_cycle > 1) {
                        /* Update cycle-level statistics */
                        total_cycles++;
                        if (success_in_current_cycle) {
                            successful_cycles++;
                        } else {
                            failed_cycles++;
                            /* Store failed cycle number (최근 10개만 유지) */
                            if (failed_cycles <= MAX_FAILED_CYCLES_LOG) {
                                failed_cycle_numbers[failed_cycles - 1] = current_cycle - 1;
                            }
                        }
                    }

                    /* Reset per-cycle success flag for new cycle */
                    success_in_current_cycle = false;

                    memset(cumulative_ack_confirmed, 0, sizeof(cumulative_ack_confirmed));
                    cumulative_ack_count = 0;

                    tx_state = TX_STATE_FIRST_TX;
                    has_pending_retrans = false;

                    static char cycle_msg[150];
                    snprintf(cycle_msg, sizeof(cycle_msg),
                            "NEW CYCLE %d - P%d (global %d) cur=%d, next=%d",
                            current_cycle, current_period_in_cycle, period_count,
                            current_preamble, next_preamble);
                    test_run_info((unsigned char *)cycle_msg);
                } else {
                    static char period_msg[150];
                    snprintf(period_msg, sizeof(period_msg), "Cycle %d - P%d (global %d) cur=%d, next=%d",
                            current_cycle, current_period_in_cycle, period_count,
                            current_preamble, next_preamble);
                    test_run_info((unsigned char *)period_msg);
                }

                /* Note: data_received_from already reset above */

                /* Switch to DATA config after SYNC with current preamble code (already trxoff above) */
                if (dwt_configure(&config_data) == DWT_SUCCESS) {
                    static char config_log[100];
                    snprintf(config_log, sizeof(config_log),
                            "Switched to DATA config (PLEN64) Preamble=%d", current_preamble);
                    test_run_info((unsigned char *)config_log);
                } else {
                    test_run_info((unsigned char *)"WARNING: Failed to switch to DATA config!");
                }

                /* Re-enable RX for continuous listening with clean buffer */
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
            /* No message type verification - SYNC-only period management */

            /*
             * ----- [E-2] Period 홀수(1,3,5): DATA 수신 처리 -----
             *
             * 조건: 홀수 Period && (DATA || RELAY_DATA)
             * 동작:
             *   1. 릴레이 필터링 (FL/FR 통신 시뮬레이션)
             *   2. slot_idx로 송신자 식별
             *   3. data_received_from[slot_idx] = 1 설정
             *   4. 지연시간 통계 업데이트
             */
            if ((current_period_in_cycle % 2 == 1) &&
                (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA || rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_RELAY_DATA)) {

                uint8_t src_node = rx_buffer[IDX_SOURCE];
                bool should_process = true;

                /* Relay filtering: simulate real-world poor communication between FL/FR and Normal nodes */
                #if TEST_MODE
                if (TEST_MY_NODE_ID == NODE_FL || TEST_MY_NODE_ID == NODE_FR) {
                    /* FL/FR nodes: ignore RELAY_DATA (relay is for normal nodes, not for FL/FR) */
                    if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_RELAY_DATA) {
                        should_process = false;  // FL/FR don't need relayed messages
                    }
                    /* FL/FR nodes: only accept DATA from INIT and counterpart (FL<->FR) */
                    else if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA) {
                        if (src_node != NODE_INIT && src_node != NODE_FL && src_node != NODE_FR) {
                            should_process = false;  // Ignore direct DATA from normal nodes
                        }
                    }
                } else {
                    /* Normal nodes: only accept RELAY_DATA from INIT for FL/FR, ignore direct DATA from FL/FR */
                    if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA) {
                        if (src_node == NODE_FL || src_node == NODE_FR) {
                            should_process = false;  // Ignore direct DATA from FL/FR
                        }
                    }
                }
                #endif

                if (should_process) {
                    uint8_t slot_idx = get_slot_index(
                        rx_buffer[IDX_MSG_TYPE],
                        rx_buffer[IDX_SOURCE],
                        rx_buffer[IDX_ORIG_SRC]
                    );

                    if (slot_idx != 0xFF && slot_idx < TOTAL_ARRAY_SIZE) {
                        if (!data_received_from[slot_idx]) {
                            data_received_from[slot_idx] = 1;
                            unique_data_count++;

                            static char data_msg[150];
                            snprintf(data_msg, sizeof(data_msg),
                                    "DATA from %s (slot %d) - unique count: %d",
                                    get_slot_description(slot_idx), slot_idx, unique_data_count);
                            test_run_info((unsigned char *)data_msg);
                        }

                        /* Latency measurement (both DATA and RELAY_DATA) */
                        /* Calculate RX timestamp (SYNC-relative, in microseconds) */
                        uint32_t rx_timestamp_us = (dwt_timer_get_cycles() - last_sync_cycles) / 64;

                        /* Extract TX timestamp from message */
                        uint32_t tx_timestamp_us;
                        memcpy(&tx_timestamp_us, &rx_buffer[IDX_TX_TIMESTAMP], sizeof(uint32_t));

                        /* Calculate one-way delay (sanity check: RX > TX) */
                        if (rx_timestamp_us > tx_timestamp_us) {
                            uint32_t oneway_delay_us = rx_timestamp_us - tx_timestamp_us;

                            /* For RELAY_DATA, attribute latency to relay slot (includes relay overhead) */
                            uint8_t latency_slot_idx = slot_idx;

                            /* Update per-node latency statistics */
                            update_node_latency(&node_latency[latency_slot_idx], oneway_delay_us);

                            /* Debug log */
                            static char latency_log[150];
                            snprintf(latency_log, sizeof(latency_log),
                                    "Latency from %s: %u us (%.3f ms) - TX:%u, RX:%u",
                                    get_slot_description(latency_slot_idx), oneway_delay_us,
                                    oneway_delay_us / 1000.0, tx_timestamp_us, rx_timestamp_us);
                            test_run_info((unsigned char *)latency_log);
                        }
                    }
                }
            }

            /*
             * ----- [E-3] Period 짝수(2,4,6): ACK_ARRAY 수신 처리 -----
             *
             * 조건: 짝수 Period && ACK_ARRAY 메시지
             * 동작:
             *   1. 송신자의 ACK 배열 추출
             *   2. 내 슬롯에 대한 ACK 확인
             *   3. cumulative_ack_confirmed 업데이트
             *   4. 모든 ACK 수신 시 tx_state = IDLE
             */
            else if ((current_period_in_cycle % 2 == 0) && (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_ACK_ARRAY)) {

                uint8_t src_node = rx_buffer[IDX_SOURCE];
                uint8_t src_slot_idx = node_id_to_index(src_node);
                uint8_t *ack_array = &rx_buffer[IDX_ACK_ARRAY];  // 12 bytes

                uint8_t my_slot_idx = node_id_to_index(TEST_MY_NODE_ID);

                /* Display ACK_ARRAY content for visibility */
                static char ack_array_display[250];
                int pos = snprintf(ack_array_display, sizeof(ack_array_display),
                                  "RX ACK_ARRAY from %s: ", get_slot_description(src_slot_idx));
                for (int i = 0; i < TOTAL_ARRAY_SIZE && pos < sizeof(ack_array_display) - 15; i++) {
                    if (ack_array[i]) {
                        // Print node ID or relay slot name
                        if (i == SLOT_IDX_FL_RELAY) {
                            pos += snprintf(ack_array_display + pos, sizeof(ack_array_display) - pos, "FL_REL ");
                        } else if (i == SLOT_IDX_FR_RELAY) {
                            pos += snprintf(ack_array_display + pos, sizeof(ack_array_display) - pos, "FR_REL ");
                        } else {
                            pos += snprintf(ack_array_display + pos, sizeof(ack_array_display) - pos,
                                          "%c ", index_to_node_id(i));
                        }
                    }
                }
                test_run_info((unsigned char *)ack_array_display);

                /* Check if this node's data was received */
                if (is_expected_ack_slot_for(src_slot_idx, my_slot_idx)) {
                    if (ack_array[my_slot_idx] == 1) {
                        if (!cumulative_ack_confirmed[src_slot_idx]) {
                            cumulative_ack_confirmed[src_slot_idx] = 1;
                            cumulative_ack_count++;

                            static char ack_msg[150];
                            snprintf(ack_msg, sizeof(ack_msg),
                                    "ACK from %s (slot %d) - cumulative: %d",
                                    get_slot_description(src_slot_idx), src_slot_idx,
                                    cumulative_ack_count);
                            test_run_info((unsigned char *)ack_msg);

                            /* Check if all expected ACKs received */
                            if (cumulative_ack_count >= TEST_EXPECTED_ACKS) {
                                /* Success - track by pair and update cycle flag */
                                if (!success_in_current_cycle) {
                                    success_in_current_cycle = true;

                                    /* Set TX state to IDLE for rest of cycle */
                                    tx_state = TX_STATE_IDLE;
                                    has_pending_retrans = false;

                                    /* Increment pair-specific success counter */
                                    if (current_period_in_cycle == 2) {
                                        pair1_success++;  // Period 2 evaluates Pair 1
                                    } else if (current_period_in_cycle == 4) {
                                        pair2_success++;  // Period 4 evaluates Pair 2
                                    } else if (current_period_in_cycle == 6) {
                                        pair3_success++;  // Period 6 evaluates Pair 3
                                    }

                                    static char success_msg[150];
                                    snprintf(success_msg, sizeof(success_msg),
                                            "DATA SUCCESS in Period %d (Pair %d) - All %d ACKs received - IDLE for rest of cycle",
                                            current_period_in_cycle, (current_period_in_cycle + 1) / 2, TEST_EXPECTED_ACKS);
                                    test_run_info((unsigned char *)success_msg);
                                }
                            }
                        }
                    }
                }
            }

            /* Re-enable RX for continuous reception */
            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }

        /*
         * ========== [F] RX 타임아웃/에러 처리 ==========
         *
         * 조건: RX 타임아웃 또는 에러 플래그 설정
         * 동작: 상태 클리어 후 RX 재활성화
         * 에러 유형:
         *   - RXFTO: 수신 타임아웃
         *   - RXPHE: 프리앰블 헤더 에러
         *   - RXFCE: 프레임 체크 에러
         *   - RXFSL: 프레임 동기화 손실
         *   - RXSTO: SFD 타임아웃
         */
        if (status_reg & DWT_INT_RXFTO_BIT_MASK) {
            dwt_writesysstatuslo(DWT_INT_RXFTO_BIT_MASK);
            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }

        if (status_reg & (DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXFCE_BIT_MASK |
                          DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXSTO_BIT_MASK)) {
            dwt_writesysstatuslo(DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXFCE_BIT_MASK |
                               DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXSTO_BIT_MASK);
            dwt_forcetrxoff();
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
    }
}


#endif
/*****************************************************************************************************************************************************
 * NOTES:
 *
 * 1. In this example, maximum frame length is set to 127 bytes which is 802.15.4 UWB standard maximum frame length. DW IC supports an extended frame
 *    length (up to 1023 bytes long) mode which is not used in this example.
 * 2. Desired configuration by user may be different to the current programmed configuration. dwt_configure is called to set desired
 *    configuration.
 * 3. In a real application, for optimum performance within regulatory limits, it may be necessary to set TX pulse bandwidth and TX power, (using
 *    the dwt_configuretxrf API call) to per device calibrated values saved in the target system or the DW IC OTP memory.
 * 4. Manual reception activation is performed here but DW IC offers several features that can be used to handle more complex scenarios or to
 *    optimise system's overall performance (e.g. timeout after a given time, automatic re-enabling of reception in case of errors, etc.).
 * 5. We use polled mode of operation here to keep the example as simple as possible but all status events can be used to generate interrupts. Please
 *    refer to DW IC User Manual for more details on "interrupts".
 * 6. dwt_writetxdata() takes the full size of tx_msg as a parameter but only copies (size - 2) bytes as the check-sum at the end of the frame is
 *    automatically appended by the DW IC. This means that our tx_msg could be two bytes shorter without losing any data (but the sizeof would not
 *    work anymore then as we would still have to indicate the full length of the frame to dwt_writetxdata()).
 ****************************************************************************************************************************************************/
