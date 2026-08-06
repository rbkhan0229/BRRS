/*! ----------------------------------------------------------------------------
 *  @file    rx_send_resp.c
 *  @brief   RX then send a response example code
 *
 *           This is a simple code example that turns on the DW IC receiver to receive a frame, (expecting the frame as sent by the companion simple
 *           example "TX then wait for response example code"). When a frame is received and validated as the expected frame a response message is
 *           sent, after which the code returns to await reception of another frame.
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

#if defined(TEST_ONLY_RETRANS_NORMAL)

extern void test_run_info(unsigned char *data);

/* ========== TEST MODE ========== */
#define TEST_MODE 1  // Set to 0 for default Node 8, 1 for easy node selection

//MARK: TEST MODE 설정
#if TEST_MODE
    #define TEST_TOTAL_NODES 5     // Test nodes: INIT, NODE_2, NODE_8, FL, FR
    /* All nodes expect ACKs from all other nodes except self (unified logic) */
    #define TEST_EXPECTED_ACKS (TEST_TOTAL_NODES - 1)   // Total - self = 5 - 1 = 4

    // Select node type for testing (uncomment one) - Ablation study: only retrans, no relay:
    //#define TEST_NODE_1        // Node 1, slot 1 (not used - INIT node slot)
    #define TEST_NODE_2        // Node 2, slot 2 (SEQ 2: 10ms-20ms)
    //#define TEST_NODE_3        // Node 3, slot 3 (SEQ 3: 20ms-30ms)
    //#define TEST_NODE_4        // Node 4, slot 4 (SEQ 4: 30ms-40ms)
    //#define TEST_NODE_5        // Node 5, slot 5 (SEQ 5: 40ms-50ms)
    //#define TEST_NODE_6        // Node 6, slot 6 (SEQ 6: 50ms-60ms)
    //#define TEST_NODE_7        // Node 7, slot 7 (SEQ 7: 60ms-70ms)
    //#define TEST_NODE_8        // Node 8, slot 8 (SEQ 8: 70ms-80ms)
    //#define TEST_NODE_FL       // FL node, slot 9 (SEQ 9: 80ms-90ms)
    //#define TEST_NODE_FR       // FR node, slot 11 (SEQ 11: 100ms-110ms)

    #ifdef TEST_NODE_1
        #define APP_NAME "ONLY RETRANS NORMAL NODE 1 v1.0 (NOT USED - INIT SLOT)"
        #define TEST_MY_NODE_ID NODE_1
        #define TEST_MY_NODE_SEQ 1
    #elif defined(TEST_NODE_2)
        #define APP_NAME "ONLY RETRANS NORMAL NODE 2 v1.0"
        #define TEST_MY_NODE_ID NODE_2
        #define TEST_MY_NODE_SEQ 2
    #elif defined(TEST_NODE_3)
        #define APP_NAME "ONLY RETRANS NORMAL NODE 3 v1.0"
        #define TEST_MY_NODE_ID NODE_3
        #define TEST_MY_NODE_SEQ 3
    #elif defined(TEST_NODE_4)
        #define APP_NAME "ONLY RETRANS NORMAL NODE 4 v1.0"
        #define TEST_MY_NODE_ID NODE_4
        #define TEST_MY_NODE_SEQ 4
    #elif defined(TEST_NODE_5)
        #define APP_NAME "ONLY RETRANS NORMAL NODE 5 v1.0"
        #define TEST_MY_NODE_ID NODE_5
        #define TEST_MY_NODE_SEQ 5
    #elif defined(TEST_NODE_6)
        #define APP_NAME "ONLY RETRANS NORMAL NODE 6 v1.0"
        #define TEST_MY_NODE_ID NODE_6
        #define TEST_MY_NODE_SEQ 6
    #elif defined(TEST_NODE_7)
        #define APP_NAME "ONLY RETRANS NORMAL NODE 7 v1.0"
        #define TEST_MY_NODE_ID NODE_7
        #define TEST_MY_NODE_SEQ 7
    #elif defined(TEST_NODE_8)
        #define APP_NAME "ONLY RETRANS NORMAL NODE 8 v1.0"
        #define TEST_MY_NODE_ID NODE_8
        #define TEST_MY_NODE_SEQ 8
    #elif defined(TEST_NODE_FL)
        #define APP_NAME "ONLY RETRANS FRONT LEFT NODE v1.0"
        #define TEST_MY_NODE_ID NODE_FL
        #define TEST_MY_NODE_SEQ 9
    #elif defined(TEST_NODE_FR)
        #define APP_NAME "ONLY RETRANS FRONT RIGHT NODE v1.0"
        #define TEST_MY_NODE_ID NODE_FR
        #define TEST_MY_NODE_SEQ 11
        #define TEST_MY_SLOT_START_MS 13.5  // FR node: 13.5ms
    #else
        #error "Please select a node type for TEST_MODE"
    #endif
#else
    /* Default configuration - NODE_2 for 12-slot system */
    #define APP_NAME "NORMAL NODE 2 v1.0"
    #define TEST_MY_NODE_ID NODE_2
    #define TEST_MY_NODE_SEQ 2
#endif

/* ========== DWT Timer Constants ========== */
#define CPU_FREQ_MHZ 64                    // nRF52840 CPU frequency
#define CYCLES_PER_US (CPU_FREQ_MHZ)       // 64 cycles per microsecond
#define CYCLES_PER_MS (CPU_FREQ_MHZ * 1000) // 64,000 cycles per millisecond

/* ========== DWT Timer Structure ========== */
typedef struct {
    uint32_t start_cycles;     // Starting cycle count
    uint32_t target_cycles;    // Target cycle duration
    bool active;               // Timer active flag
} dwt_timer_t;

/* ========== TDMA Protocol Parameters - Aggregated ACKs ========== */
// Period: 21.2ms (1.0ms guard time for all slots)
// Slot: 1.1ms (0.1ms TX + 1.0ms guard)
// Config switch: 17.2ms (4.0ms buffer for SYNC reception at 21.2ms)
#define PERIOD_MS           21.2    // 21.2ms period for Aggregated ACKs (1.0ms guard time)
#define SLOT_DURATION_MS    0.1     // 0.1ms per slot (TX only)
#define GUARD_TIME_MS       1.0     // 1.0ms guard time between slots
#define SLOT_INTERVAL_MS    1.1     // Total slot interval (0.1ms + 1.0ms guard)
#define TOTAL_NODES         10      // Total physical nodes in network
#define TOTAL_SLOTS         12      // Total TDMA slots (no relay in this ablation study)
#define TOTAL_ARRAY_SIZE    12      // Array size for tracking all transmissions
#define PERIODS_PER_CYCLE   6       // 6 periods per cycle (3 period pairs)
#define CONFIG_SWITCH_MS    17.2    // Switch to PLEN512 at 17.2ms

/* Slot Assignment:
 * SEQ 1: 0-10ms    - INIT own data
 * SEQ 2: 10-20ms   - NODE_2
 * SEQ 3: 20-30ms   - NODE_3
 * SEQ 4: 30-40ms   - NODE_4
 * SEQ 5: 40-50ms   - NODE_5
 * SEQ 6: 50-60ms   - NODE_6
 * SEQ 7: 60-70ms   - NODE_7
 * SEQ 8: 70-80ms   - NODE_8
 * SEQ 9: 80-90ms   - FL direct slot
 * SEQ 10: (removed - no relay)
 * SEQ 11: 100-110ms - FR direct slot
 * SEQ 12: (removed - no relay)
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

/* ========== ACK Reception Parameters ========== */
#define TX_TO_RX_DELAY_UUS  60      // Delay from TX end to RX activation (60us)
#define RX_ACK_TIMEOUT_UUS  4500    // ACK reception timeout (4.5ms)
#define ACK_SLOT_DURATION_US  450   // 450us per ACK slot
#define ACK_TX_INTERVAL_US    500   // 500us interval between ACK transmissions (increased for reliability)

/* Ablation study: No relay - all nodes communicate directly */

/* Default communication configuration - MATCH rx_send_resp.c */
/* Default communication configuration for DATA/ACK - PLEN64 */
static dwt_config_t config_data = {
    5,                /* Channel number. */
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
    5,                /* Channel number. */
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

/* ========== Node ID Definitions ========== */
#define NODE_INIT '1'    // Initiator node (this node) - SEQ 1, index 0
#define NODE_2    '2'    // Normal node 2 - SEQ 2, index 1
#define NODE_3    '3'    // Normal node 3 - SEQ 3, index 2
#define NODE_4    '4'    // Normal node 4 - SEQ 4, index 3
#define NODE_5    '5'    // Normal node 5 - SEQ 5, index 4
#define NODE_6    '6'    // Normal node 6 - SEQ 6, index 5
#define NODE_7    '7'    // Normal node 7 - SEQ 7, index 6
#define NODE_8    '8'    // Normal node 8 - SEQ 8, index 7
#define NODE_FL   '9'    // Front Left node - SEQ 9, index 8
#define NODE_FR   'A'    // Front Right node - SEQ 11, index 10
#define NODE_ALL  'B'    // Broadcast to all nodes

/* ========== Message Type Definitions ========== */
#define MSG_TYPE_SYNC      0x01
#define MSG_TYPE_DATA      0x02
#define MSG_TYPE_ACK       0x03
#define MSG_TYPE_URGENT    0x04
#define MSG_TYPE_ACK_ARRAY    0x07  // ACK array broadcast (Period 2,4,6)

/* ========== Message Index Definitions ========== */
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
  IDX_ACK_ARRAY = 8        // 8~19: ACK array payload (12 bytes) - ACK_ARRAY only
};

#define QUEUE_SIZE       32
#define MAX_RETRY        3

/* ========== Message Queue Structure ========== */
typedef struct {
    uint8_t msg[FRAME_LEN_MAX];
    uint8_t dest_id;
    uint8_t priority;  // 0:urgent, 1:normal
    uint8_t retry_count;
} message_t;

typedef struct {
    message_t buffer[QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} message_queue_t;

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

/* Inter-frame delay period, in milliseconds. */
#define TX_DELAY_MS 1000

/* Configs */
#define TX_DELAY_MS       1000
#define TX_TO_RX_DELAY_UUS 60
#define RX_RESP_TO_UUS    5000

/* Buffer to store received frame. See NOTE 1 below. */
static uint8_t rx_buffer[FRAME_LEN_MAX];

// DWT-based timers for UWB protocol
static dwt_timer_t period_timer;        // 20ms period timer
static dwt_timer_t slot_interval_timer; // 10ms wait for my slot
static dwt_timer_t slot_duration_timer; // 2ms TX slot duration limit
static dwt_timer_t ack_slot_timer;      // ACK transmission delay timer

// Message queues
static message_queue_t retrans_queue = {0};

// ACK tracking
static uint8_t ack_status[TOTAL_NODES] = {0};

// Protocol state
static bool synchronized = false;
static uint32_t period_count = 0;            // Global period counter
static uint8_t current_period_in_cycle = 0;  // Start from 0, will become 1 on first SYNC
static uint32_t current_cycle = 0;           // Start from cycle 0, will become 1 on first period 1

/* ========== Pair-level Statistics (3 pairs per cycle) ========== */
static uint32_t pair1_success = 0, pair1_fail = 0, pair1_idle = 0;
static uint32_t pair2_success = 0, pair2_fail = 0, pair2_idle = 0;
static uint32_t pair3_success = 0, pair3_fail = 0, pair3_idle = 0;

/* ========== Cycle-level Statistics ========== */
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

/* ========== SYNC Loss Detection System ========== */
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

/* ========== Per-Node Latency Tracking ========== */
/* One-way latency statistics structure */
typedef struct {
    uint32_t min_us;           // Minimum latency (microseconds)
    uint32_t max_us;           // Maximum latency
    uint64_t sum_us;           // Sum for average calculation
    uint32_t count;            // Number of samples
} latency_stats_t;

/* Per-node latency tracking (10 slots: INIT, NODE_2-8, FL, FR) - no relay */
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
    {0xFFFFFFFF, 0, 0, 0},  // FR (slot 10)
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

/* 여러 응답 수신 관련 */
#define MAX_RESPONSES 10

/* ========== DWT Cycle Counter Functions ========== */

// ARM Cortex-M4 DWT register addresses
#define DWT_CTRL     (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT   (*(volatile uint32_t*)0xE0001004)

// DWT Control register bit for cycle counter enable
#ifndef DWT_CTRL_CYCCNTENA_Msk
#define DWT_CTRL_CYCCNTENA_Msk  (1UL << 0)  // Bit 0: Enable cycle counter
#endif

// CPU clock frequency for timing calculations
#define CPU_FREQ_HZ  64000000  // 64MHz nRF52840

// Initialize DWT cycle counter
static void dwt_timer_init(void) {
    // Enable cycle counter if not already enabled
    if (!(DWT_CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
        DWT_CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

// Get current cycle count
static uint32_t dwt_timer_get_cycles(void) {
    return DWT_CYCCNT;
}

// Convert microseconds to CPU cycles
// 64MHz CPU: 64 cycles = 1 microsecond
static uint32_t us_to_cpu_cycles(uint32_t microseconds) {
    return microseconds * (CPU_FREQ_HZ / 1000000);
}

// Check if specified time has elapsed since start_cycles
static bool dwt_timer_elapsed(uint32_t start_cycles, uint32_t duration_cycles) {
    uint32_t current_cycles = dwt_timer_get_cycles();
    uint32_t elapsed = current_cycles - start_cycles;  // Handles 32-bit overflow automatically
    return (elapsed >= duration_cycles);
}

// Convert CPU cycles to milliseconds for logging
static uint32_t cycles_to_ms(uint32_t cycles) {
    return cycles / (CPU_FREQ_HZ / 1000);  // 64MHz = 64000 cycles per ms
}

// Get elapsed time in ms since last SYNC for logging
static uint32_t get_elapsed_ms(uint32_t last_sync_cycles) {
    if (last_sync_cycles == 0) return 0;
    uint32_t current = dwt_timer_get_cycles();
    return cycles_to_ms(current - last_sync_cycles);
}

// Update per-node latency statistics
static void update_node_latency(latency_stats_t *stats, uint32_t latency_us) {
    // Update minimum
    if (latency_us < stats->min_us) {
        stats->min_us = latency_us;
    }

    // Update maximum
    if (latency_us > stats->max_us) {
        stats->max_us = latency_us;
    }

    // Update sum (for average calculation)
    stats->sum_us += latency_us;

    // Increment sample count
    stats->count++;
}

/* ========== Interrupt Callback Functions ========== */
static void rx_ok_cb(const dwt_cb_data_t *cb_data);
static void rx_to_cb(const dwt_cb_data_t *cb_data);
static void rx_err_cb(const dwt_cb_data_t *cb_data);

/* Global flags for RX event signaling from interrupt to main loop */
static volatile uint32_t rx_event_flags = 0;  // Bitmask: bit 0=rx_ok, bit 1=timeout, bit 2=error
static volatile uint16_t rx_frame_len = 0;

#define RX_EVENT_OK_BIT     (1 << 0)  // Bit 0: RX OK event
#define RX_EVENT_TO_BIT     (1 << 1)  // Bit 1: RX timeout event
#define RX_EVENT_ERR_BIT    (1 << 2)  // Bit 2: RX error event

/* Global flags for interrupt-based ACK collection */
static volatile bool collecting_acks = false;           // ACK collection state
static volatile int collected_ack_count = 0;            // Number of ACKs collected
static volatile uint32_t ack_collection_end_time = 0;   // When to stop collecting ACKs

/* ACK source tracking for current collection period */
static uint8_t ack_received_from[TOTAL_NODES] = {0};    // Track which nodes sent ACKs this period
static uint8_t unique_ack_count = 0;                    // Count of unique ACK sources

/* Period-level tracking (reset every odd period) */
static uint8_t data_received_from[TOTAL_ARRAY_SIZE] = {0};
static uint8_t unique_data_count = 0;

/* Cycle-level cumulative tracking (reset every cycle) */
static uint8_t cumulative_ack_confirmed[TOTAL_ARRAY_SIZE] = {0};
static uint8_t cumulative_ack_count = 0;

/* Period별 성공 추적 변수 */
static bool own_data_success_in_period_1 = false;
static bool own_data_success_in_period_2 = false;
static bool own_data_success_in_period_3 = false;
static bool own_data_cycle_evaluated = false;

/* Period 2, 3별 retrans 성공 카운트 */
static uint32_t period_2_retrans_success = 0;
static uint32_t period_3_retrans_success = 0;

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

/* Forward declarations - defined below after these helper functions */
static uint8_t node_id_to_index(uint8_t node_id);
static uint8_t index_to_node_id(uint8_t index);

/* Helper function to get slot index from message */
static uint8_t get_slot_index(uint8_t msg_type, uint8_t src_node, uint8_t orig_src) {
    if (msg_type == MSG_TYPE_DATA) {
        return node_id_to_index(src_node);
    }
    return 0xFF;  // No relay in this ablation study
}

/* Helper function to check if we expect ACK from this slot */
static bool is_expected_ack_slot_for(uint8_t ack_slot_idx, uint8_t tx_slot_idx) {
    #if TEST_MODE
    uint8_t my_slot_idx = node_id_to_index(TEST_MY_NODE_ID);

    // Don't expect ACK from self
    if (ack_slot_idx == my_slot_idx) return false;

    // Invalid slot check - use TOTAL_ARRAY_SIZE to include FR node (index 10)
    if (ack_slot_idx >= TOTAL_ARRAY_SIZE) return false;

    // Unified logic: All nodes (including FL/FR) expect ACK from all other nodes except self
    // No special treatment for FL/FR nodes
    return true;
    #else
    return (ack_slot_idx != tx_slot_idx && ack_slot_idx < TOTAL_NODES);
    #endif
}

/* Helper function to get slot description for logging */
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
        case SLOT_IDX_FR: return "FR";
        default: return "???";
    }
}

/* Helper function to convert node ID character to array index */
static uint8_t node_id_to_index(uint8_t node_id) {
    switch(node_id) {
        case NODE_INIT: return 0;   // '1' -> index 0 (SEQ 1)
        case NODE_2:    return 1;   // '2' -> index 1 (SEQ 2)
        case NODE_3:    return 2;   // '3' -> index 2 (SEQ 3)
        case NODE_4:    return 3;   // '4' -> index 3 (SEQ 4)
        case NODE_5:    return 4;   // '5' -> index 4 (SEQ 5)
        case NODE_6:    return 5;   // '6' -> index 5 (SEQ 6)
        case NODE_7:    return 6;   // '7' -> index 6 (SEQ 7)
        case NODE_8:    return 7;   // '8' -> index 7 (SEQ 8)
        case NODE_FL:   return 8;   // '9' -> index 8 (SEQ 9 - FL direct slot)
        case NODE_FR:   return 10;  // 'A' -> index 10 (SEQ 11 - FR direct slot)
        default:        return 0xFF;  // Invalid node ID
    }
}

/* Helper function to convert array index to node ID character */
static uint8_t index_to_node_id(uint8_t index) {
    switch(index) {
        case 0:  return NODE_INIT;  // index 0 -> '1' (SEQ 1)
        case 1:  return NODE_2;     // index 1 -> '2' (SEQ 2)
        case 2:  return NODE_3;     // index 2 -> '3' (SEQ 3)
        case 3:  return NODE_4;     // index 3 -> '4' (SEQ 4)
        case 4:  return NODE_5;     // index 4 -> '5' (SEQ 5)
        case 5:  return NODE_6;     // index 5 -> '6' (SEQ 6)
        case 6:  return NODE_7;     // index 6 -> '7' (SEQ 7)
        case 7:  return NODE_8;     // index 7 -> '8' (SEQ 8)
        case 8:  return NODE_FL;    // index 8 -> '9' (SEQ 9 - FL direct slot)
        case 10: return NODE_FR;    // index 10 -> 'A' (SEQ 11 - FR direct slot)
        default: return '?';        // Invalid index (including 9 and 11 which are relay slots)
    }
}

/* ========== Queue Management Functions ========== */

// Enqueue message
static bool enqueue(message_queue_t* q, const message_t* msg) {
    if (q->count >= QUEUE_SIZE) return false;
    q->buffer[q->tail] = *msg;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    return true;
}

// Dequeue message
static bool dequeue(message_queue_t* q, message_t* msg) {
    if (q->count == 0) return false;
    *msg = q->buffer[q->head];
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    return true;
}

// Check if queue is empty
static bool is_queue_empty(const message_queue_t* q) {
    return q->count == 0;
}

int only_retrans_normal(void)
{
    /* Hold copy of status register state here for reference so that it can be examined at a debug breakpoint. */
    uint32_t status_reg = 0;
    /* Hold copy of frame length of frame received (if good) so that it can be examined at a debug breakpoint. */
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
    dwt_configuretxrf(&txconfig_options);

    /* POLLING MODE - No interrupt callbacks */
    /* Disable all interrupts to prevent SPI lock issues */
    dwt_setinterrupt(0, 0, DWT_ENABLE_INT);  // Disable all interrupts

    /* Clear any pending interrupts */
    dwt_writesysstatuslo(0xFFFFFFFF);  // Clear all status flags

    /* Do NOT install IRQ handler - using polling instead */
    // port_set_dwic_isr(dwt_isr);  // DISABLED for polling mode

    test_run_info((unsigned char *)"Normal Node initialized in POLLING MODE (no interrupts)");

    /* Loop forever sending and receiving frames periodically. */
    int test_slot_interval = 5000000;

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

    /* Debug counter for loop iterations */
    static uint32_t loop_counter = 0;

    /* Enable RX immediately for continuous listening - no critical section needed in polling mode */
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    while (1)
    {
        /* ========== SYNC Loss Detection (27ms timeout) ========== */
        /* Check if we've lost SYNC signal - if so, halt all TX until next SYNC */
        if (last_sync_cycles != 0 && !sync_lost) {
            uint32_t current_cycles = dwt_timer_get_cycles();
            if (dwt_timer_elapsed(last_sync_cycles, sync_loss_timeout_cycles)) {
                sync_lost = true;
                sync_loss_stats.total_timeouts++;

                static char timeout_log[120];
                snprintf(timeout_log, sizeof(timeout_log),
                        "[SYNC LOST] No SYNC for 27ms at Period %d - TX halted until next SYNC",
                        current_period_in_cycle);
                test_run_info((unsigned char *)timeout_log);
            }
        }

        /* Check for SYNC timeout (5 seconds without SYNC) */
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
                float success_rate = (total_cycles > 0) ? (float)successful_cycles / total_cycles * 100 : 0;

                snprintf(final_stats, sizeof(final_stats),
                        "\n=== PAIR STATISTICS (3 cycles) ===\n"
                        "Pair 1 (Period 1-2): Success=%d, Fail=%d, IDLE=%d\n"
                        "Pair 2 (Period 3-4): Success=%d, Fail=%d, IDLE=%d\n"
                        "Pair 3 (Period 5-6): Success=%d, Fail=%d, IDLE=%d\n\n"
                        "=== CYCLE STATISTICS ===\n"
                        "Total Cycles: %d\n"
                        "Successful Cycles: %d (%.2f%%)\n"
                        "Failed Cycles: %d (%.2f%%)",
                        pair1_success, pair1_fail, pair1_idle,
                        pair2_success, pair2_fail, pair2_idle,
                        pair3_success, pair3_fail, pair3_idle,
                        total_cycles,
                        successful_cycles, success_rate,
                        failed_cycles, (100.0 - success_rate));
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

        /* Check for config switch timing - SEQ 1-9 nodes use 58ms period timer */
#if TEST_MODE
        if (last_sync_cycles != 0 && !config_is_sync) {
#else
        if (last_sync_cycles != 0 && !config_is_sync) {
#endif
            uint32_t current_cycles = dwt_timer_get_cycles();
            if (dwt_timer_elapsed(last_sync_cycles, config_switch_cycles)) {
                /* DISABLED: Pre-switch RX check temporarily disabled to improve config switch timing */
                /*
                // CRITICAL: Check for pending RX BEFORE config switch to avoid interrupting reception
                uint32_t pre_switch_status = dwt_readsysstatuslo();
                if (pre_switch_status & DWT_INT_RXFCG_BIT_MASK) {
                    test_run_info((unsigned char *)"[PRE-SWITCH] RX found in buffer before config switch!");

                    // Process the RX message immediately
                    uint16_t frame_len = FRAME_LEN_MAX;
                    memset(rx_buffer, 0, sizeof(rx_buffer));
                    dwt_readrxdata(rx_buffer, frame_len, 0);
                    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

                    // Log what we received
                    uint32_t elapsed_ms = get_elapsed_ms(last_sync_cycles);
                    static char pre_switch_rx_log[100];
                    snprintf(pre_switch_rx_log, sizeof(pre_switch_rx_log),
                            "[PRE-SWITCH %ums] RX: type=0x%02X, src=%c",
                            elapsed_ms, rx_buffer[IDX_MSG_TYPE], rx_buffer[IDX_SOURCE]);
                    test_run_info((unsigned char *)pre_switch_rx_log);

                    // Process if it's DATA in odd period (no relay in this ablation study)
                    if ((current_period_in_cycle % 2 == 1) && (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA)) {
                        uint8_t slot_idx = get_slot_index(
                            rx_buffer[IDX_MSG_TYPE],
                            rx_buffer[IDX_SOURCE],
                            rx_buffer[IDX_ORIG_SRC]
                        );

                        if (slot_idx != 0xFF && slot_idx < TOTAL_ARRAY_SIZE) {
                            if (!data_received_from[slot_idx]) {
                                data_received_from[slot_idx] = 1;
                                unique_data_count++;
                                test_run_info((unsigned char *)"[PRE-SWITCH] DATA captured before config switch!");
                            }
                        }
                    }
                }
                */

#if TEST_MODE
                /* Switch to SYNC config at 17.2ms for 4.0ms buffer before next SYNC at 21.2ms */
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    static char switch_msg[100];
                    snprintf(switch_msg, sizeof(switch_msg), "SEQ %d: Switched to SYNC config at 17.2ms", TEST_MY_NODE_SEQ);
                    test_run_info((unsigned char *)switch_msg);
                    config_is_sync = true;
                } else {
                    test_run_info((unsigned char *)"WARNING: Failed to switch to SYNC config!");
                }
#else
                /* Default: Switch to SYNC config for next SYNC reception */
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    test_run_info((unsigned char *)"Switched to SYNC config (PLEN512) - ready for next SYNC");
                    config_is_sync = true;
                } else {
                    test_run_info((unsigned char *)"WARNING: Failed to switch to SYNC config!");
                }
#endif
                /* Re-enable RX after config change */
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
        }

        /* Check slot timing CONTINUOUSLY without blocking */
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

                /* ========== CRITICAL: Check SYNC Loss Before TX ========== */
                /* If SYNC is lost, skip ALL transmissions and wait for next SYNC */
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

                if (is_data_period) {
                    /* Period odd (1,3,5): DATA transmission */
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
                } else {
                    /* Period even (2,4,6): ACK_ARRAY transmission */
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

        /* POLLING: Check for RX events by reading status register */
        uint32_t status_reg = dwt_readsysstatuslo();
        if (status_reg & DWT_INT_RXFCG_BIT_MASK) {  // RX frame received
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

            /* Handle SYNC message */
            if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_SYNC) {
                /* CRITICAL: Set timing reference IMMEDIATELY to minimize drift */
                uint32_t current_cycles = dwt_timer_get_cycles();
                uint32_t prev_sync_cycles = last_sync_cycles;  // Store for logging
                last_sync_cycles = current_cycles;  // SET FIRST before any delays
                slot_executed_this_sync = false;
                config_is_sync = false;

                /* ========== Extract period info from SYNC - Trust it completely ========== */
                uint8_t sync_period = rx_buffer[IDX_PERIOD_INFO];  // SYNC's period info (1-6)

                /* Clear SYNC loss flag and log recovery if needed */
                if (sync_lost) {
                    static char recovery_log[120];
                    snprintf(recovery_log, sizeof(recovery_log),
                            "[SYNC RECOVERED] Period %d received - TX operations resumed",
                            sync_period);
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
                    static char sync_interval_log[120];
                    snprintf(sync_interval_log, sizeof(sync_interval_log),
                            "SYNC interval: %u cycles (%.2f ms) - expected 21.2ms",
                            cycles_since_last_sync, ms_since_last_sync);
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
                            "NEW CYCLE %d started - Period %d (global period %d)",
                            current_cycle, current_period_in_cycle, period_count);
                    test_run_info((unsigned char *)cycle_msg);
                } else {
                    static char period_msg[100];
                    snprintf(period_msg, sizeof(period_msg), "Cycle %d - Period %d (global period %d)",
                            current_cycle, current_period_in_cycle, period_count);
                    test_run_info((unsigned char *)period_msg);
                }

                /* Final statistics check moved to top of main loop to handle SYNC timeout */
                /* Commented out to avoid duplicate printing */
                /*
                if (current_cycle > 3) {
                    static char final_stats[500];
                    float success_rate = (total_cycles > 0) ? (float)successful_cycles / total_cycles * 100 : 0;

                    snprintf(final_stats, sizeof(final_stats),
                            "\n=== FINAL STATISTICS (1000 cycles) ===\n"
                            "Total Cycles: %d\n"
                            "Successful Cycles: %d\n"
                            "Success Rate: %.2f%%\n"
                            "First TX Success (Period 1): %d\n"
                            "Period 2 Retrans Success: %d\n"
                            "Period 3 Retrans Success: %d\n"
                            "Total Retrans Success: %d\n"
                            "Failed Cycles: %d",
                            total_cycles, successful_cycles, success_rate,
                            first_tx_success, period_2_retrans_success, period_3_retrans_success,
                            retrans_success, total_cycles - successful_cycles);
                    test_run_info((unsigned char *)final_stats);
                    break;
                }
                */

                /* Reset data_received_from array at start of odd periods */
                if (current_period_in_cycle % 2 == 1) {
                    memset(data_received_from, 0, sizeof(data_received_from));
                    unique_data_count = 0;
                }

                /* Switch to DATA config after SYNC (already trxoff above) */
                if (dwt_configure(&config_data) == DWT_SUCCESS) {
                    test_run_info((unsigned char *)"Switched to DATA config (PLEN64)");
                } else {
                    test_run_info((unsigned char *)"WARNING: Failed to switch to DATA config!");
                }

                /* Re-enable RX for continuous listening with clean buffer */
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
            /* No message type verification - SYNC-only period management */

            /* Period odd (1,3,5): Track DATA messages (no relay in this ablation study) */
            if ((current_period_in_cycle % 2 == 1) && (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA)) {

                uint8_t src_node = rx_buffer[IDX_SOURCE];
                bool should_process = true;

                /* No relay filtering - FL/FR treated as normal nodes in this ablation study */
                #if TEST_MODE
                /* All nodes (including FL/FR) accept DATA from all other nodes */
                (void)src_node;  // Suppress unused variable warning
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

            /* Period even (2,4,6): Process ACK_ARRAY messages */
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

        /* POLLING: Handle RX timeout */
        if (status_reg & DWT_INT_RXFTO_BIT_MASK) {
            dwt_writesysstatuslo(DWT_INT_RXFTO_BIT_MASK);
            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }

        /* POLLING: Handle RX errors */
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
