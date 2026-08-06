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

#if defined(TEST_DWT_CYCLE_COUNTER_NORMAL)

extern void test_run_info(unsigned char *data);

/* ========== TEST MODE ========== */
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
    #endif

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

/* ========== TDMA Protocol Parameters - Updated for 12-slot relay system ========== */
#define PERIOD_MS           140     // 140ms period for 12-slot system
#define SLOT_DURATION_MS    6       // 6ms per slot (active time)
#define GUARD_TIME_MS       4       // 4ms guard time between slots
#define SLOT_INTERVAL_MS    10      // Total slot interval (6ms + 4ms guard)
#define TOTAL_NODES         10      // Total physical nodes in network
#define TOTAL_SLOTS         12      // Total TDMA slots (includes relay slots)
#define PERIODS_PER_CYCLE   3       // 3 periods per cycle
#define CONFIG_SWITCH_MS    133     // Switch to PLEN64 at 133ms

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
 * SEQ 10: 90-100ms - INIT FL relay
 * SEQ 11: 100-110ms - FR direct slot
 * SEQ 12: 110-120ms - INIT FR relay
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

/* FL/FR nodes need relay - other normal nodes should not ACK FL/FR direct messages */
#define RELAY_TEST_MODE     1       // Enable relay testing behavior

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
#define NODE_FL   '9'    // Front Left node - SEQ 9, index 8 (needs relay)
#define NODE_FR   'A'    // Front Right node - SEQ 11, index 9 (needs relay)
#define NODE_ALL  'B'    // Broadcast to all nodes

/* ========== Message Type Definitions ========== */
#define MSG_TYPE_SYNC      0x01
#define MSG_TYPE_DATA      0x02
#define MSG_TYPE_ACK       0x03
#define MSG_TYPE_URGENT    0x04
#define MSG_TYPE_RELAY_DATA   0x05  // Relayed data from FL/FR
#define MSG_TYPE_RELAY_ACK    0x06  // ACK for relayed message

/* ========== Message Index Definitions ========== */
enum {
  IDX_FTYPE     = 0,  // 0: frame type
  IDX_SEQ       = 1,  // 1: seq
  IDX_SOURCE    = 2,  // 2: Source ID
  IDX_DEST      = 3,  // 3: Destination ID
  IDX_MSG_TYPE  = 4,  // 4: Message type (SYNC/DATA/ACK/etc)
  IDX_PRIORITY  = 5,  // 5: Priority (URGENT/NORMAL)
  IDX_ORIG_SRC  = 6,  // 6: Original source (for relay)
  IDX_ORIG_DST  = 7,  // 7: Original dest (for relay)
  IDX_RESERVED  = 8   // 8~: payload
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
static uint8_t tx_msg[] = { 0x41, 0x8C, 0, 0x9A, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 'D', 'W', 0x10, 0x00, 0, 0, 0, 0 };
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
static message_queue_t relay_queue = {0};

// ACK tracking
static uint8_t ack_status[TOTAL_NODES] = {0};

// Protocol state
static bool synchronized = false;
static uint8_t current_period_in_cycle = 0;  // Start from 0, will become 1 on first SYNC
static uint32_t current_cycle = 1;  // Start from cycle 1 for 10000 cycles

/* Long-term statistics for 10000 cycles */
static uint32_t total_cycles = 0;
static uint32_t successful_cycles = 0;
static uint32_t first_tx_success = 0;    // Cycles successful in Period 1
static uint32_t retrans_success = 0;     // Cycles successful in Period 2 or 3
static bool cycle_completed = false;     // Track if current cycle finished

/* SYNC timeout detection for final statistics */
#define SYNC_TIMEOUT_SECONDS 5                           // 5 seconds without SYNC = network ended
static uint32_t sync_timeout_cycles = 0;                 // CPU cycles for SYNC timeout
static bool final_stats_printed = false;                // Prevent multiple final stats output

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

/* Helper function to convert node ID character to array index */
static uint8_t node_id_to_index(uint8_t node_id) {
    switch(node_id) {
        case NODE_INIT: return 0;  // '1' -> index 0 (SEQ 1)
        case NODE_2:    return 1;  // '2' -> index 1 (SEQ 2)
        case NODE_3:    return 2;  // '3' -> index 2 (SEQ 3)
        case NODE_4:    return 3;  // '4' -> index 3 (SEQ 4)
        case NODE_5:    return 4;  // '5' -> index 4 (SEQ 5)
        case NODE_6:    return 5;  // '6' -> index 5 (SEQ 6)
        case NODE_7:    return 6;  // '7' -> index 6 (SEQ 7)
        case NODE_8:    return 7;  // '8' -> index 7 (SEQ 8)
        case NODE_FL:   return 8;  // '9' -> index 8 (SEQ 9 - FL)
        case NODE_FR:   return 9;  // 'A' -> index 9 (SEQ 11 - FR)
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
        case 8:  return NODE_FL;    // index 8 -> '9' (SEQ 9 - FL)
        case 9:  return NODE_FR;    // index 9 -> 'A' (SEQ 11 - FR)
        default: return '?';        // Invalid index
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

int dwt_cycle_counter_normal(void)
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

    /* Calculate SYNC timeout in CPU cycles (5 seconds) */
    sync_timeout_cycles = us_to_cpu_cycles(SYNC_TIMEOUT_SECONDS * 1000000);  // 5 seconds = 5,000,000 microseconds

    /* CPU cycle-based slot timing variables */
    uint32_t last_sync_cycles = 0;                          // When SYNC was received (CPU cycles)
#if TEST_MODE
    uint32_t slot_interval_cycles = us_to_cpu_cycles(TEST_MY_NODE_SEQ * 10000);  // Dynamic based on node selection (10ms slots)
#else
    uint32_t slot_interval_cycles = us_to_cpu_cycles(80000);      // 80ms wait time (default SEQ 8 with 6ms slot + 4ms guard)
#endif
    uint32_t slot_duration_cycles = us_to_cpu_cycles(6000);       // 6ms for TX + ACK collection (early termination when successful)
    bool slot_active = false;                               // Track if currently in own slot
    bool slot_executed_this_sync = false;                   // Flag to ensure slot executes only once per SYNC

    /* Period timer for config switching - different strategy based on node sequence */
#if TEST_MODE
    uint32_t config_switch_cycles;
    if (TEST_MY_NODE_SEQ == 10) {
        /* NODE_10: Immediate switch after ACK collection (handled in slot logic) */
        config_switch_cycles = 0;  // Not used for NODE_10
    } else {
        /* SEQ 1-9: Switch at 133ms for 7ms buffer before next SYNC at 140ms */
        config_switch_cycles = us_to_cpu_cycles(133000);
    }
#else
    /* Default: 133ms period timer for SEQ 1-9, immediate for NODE_10 if applicable */
    uint32_t config_switch_cycles = us_to_cpu_cycles(133000); // 133ms after SYNC for 7ms buffer
#endif
    bool config_is_sync = true;                               // Start with SYNC config

    /* Debug: Print calculated intervals */
    static char interval_debug[150];
#if TEST_MODE
    snprintf(interval_debug, sizeof(interval_debug),
             "Node %d: slot=%u cycles (%dms), seq=%d [6ms+4ms guard]",
             TEST_MY_NODE_SEQ, slot_interval_cycles, TEST_MY_NODE_SEQ * 10, MY_NODE_SEQ);
#else
    snprintf(interval_debug, sizeof(interval_debug),
             "DWT Intervals: period=(140ms), slot=%u cycles (80ms) [6ms+4ms guard]",
              slot_interval_cycles);
#endif
    test_run_info((unsigned char *)interval_debug);

    test_run_info((unsigned char *)"Starting DWT cycle counter RX loop (POLLING MODE): 140ms period + slot timing + 6ms+4ms guard");

    /* Debug counter for loop iterations */
    static uint32_t loop_counter = 0;

    /* Enable RX immediately for continuous listening - no critical section needed in polling mode */
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    while (1)
    {
        /* Check for SYNC timeout - if no SYNC received for 5 seconds, print final statistics */
        if (last_sync_cycles != 0 && !final_stats_printed) {
            uint32_t current_cycles = dwt_timer_get_cycles();
            if (dwt_timer_elapsed(last_sync_cycles, sync_timeout_cycles)) {
                /* SYNC timeout detected - network probably ended */
                static char timeout_msg[100];
                snprintf(timeout_msg, sizeof(timeout_msg),
                        "SYNC timeout detected (%d seconds) - Network ended, printing final statistics",
                        SYNC_TIMEOUT_SECONDS);
                test_run_info((unsigned char *)timeout_msg);

                /* Print final statistics */
                static char final_stats[500];
                float success_rate = (total_cycles > 0) ? (float)successful_cycles / total_cycles * 100 : 0;
                snprintf(final_stats, sizeof(final_stats),
                        "\n=== FINAL STATISTICS (SYNC timeout) ===\n"
                        "Total Cycles: %d\n"
                        "Successful Cycles: %d\n"
                        "Success Rate: %.2f%%\n"
                        "First TX Success: %d\n"
                        "Retrans Success: %d\n"
                        "Failed Cycles: %d",
                        total_cycles, successful_cycles, success_rate,
                        first_tx_success, retrans_success, total_cycles - successful_cycles);
                test_run_info((unsigned char *)final_stats);
                final_stats_printed = true;
                break;  // Exit the loop
            }
        }

        /* Check if we've completed 10000 cycles and exit */
        //MARK: 종료 조건
        if (current_cycle > 3 && !final_stats_printed) {
            /* Print final statistics */
            static char final_stats[500];
            float success_rate = (total_cycles > 0) ? (float)successful_cycles / total_cycles * 100 : 0;
            snprintf(final_stats, sizeof(final_stats),
                    "\n=== FINAL STATISTICS (10000 cycles) ===\n"
                    "Total Cycles: %d\n"
                    "Successful Cycles: %d\n"
                    "Success Rate: %.2f%%\n"
                    "First TX Success: %d\n"
                    "Retrans Success: %d\n"
                    "Failed Cycles: %d",
                    total_cycles, successful_cycles, success_rate,
                    first_tx_success, retrans_success, total_cycles - successful_cycles);
            test_run_info((unsigned char *)final_stats);
            final_stats_printed = true;
            break;
        }

        /* Check for config switch timing - SEQ 1-9 nodes use 58ms period timer */
#if TEST_MODE
        if (TEST_MY_NODE_SEQ != 10 && last_sync_cycles != 0 && !config_is_sync) {
#else
        if (last_sync_cycles != 0 && !config_is_sync) {
#endif
            uint32_t current_cycles = dwt_timer_get_cycles();
            if (dwt_timer_elapsed(last_sync_cycles, config_switch_cycles)) {
#if TEST_MODE
                /* SEQ 1-9: Switch to SYNC config at 133ms for 7ms buffer before next SYNC at 140ms */
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    static char switch_msg[100];
                    snprintf(switch_msg, sizeof(switch_msg), "SEQ %d: Switched to SYNC config at 133ms", TEST_MY_NODE_SEQ);
                    test_run_info((unsigned char *)switch_msg);
                    config_is_sync = true;
                } else {
                    test_run_info((unsigned char *)"WARNING: SEQ 1-9 failed to switch to SYNC config!");
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

            /* Debug: Print slot timing info periodically */
            // static uint32_t debug_counter = 0;
            // debug_counter++;
            // if (debug_counter % 10000 == 0) {
            //     static char slot_debug[250];
            //     snprintf(slot_debug, sizeof(slot_debug),
            //             "Slot check: executed=%s, sync_time=%u, current=%u, time_since=%u, slot_int=%u",
            //             slot_executed_this_sync ? "YES" : "NO", last_sync_time, current_time, time_since_sync, slot_interval);
            //     test_run_info((unsigned char *)slot_debug);
            // }

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

                /* Decide what to transmit based on current state */
                bool should_transmit = false;
                const char* tx_type_desc = "";

                if (tx_state == TX_STATE_FIRST_TX) {
                    /* First transmission of new data */
                    should_transmit = true;
                    tx_type_desc = "FIRST TX";

                    /* Prepare new DATA frame */
                    tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
#if TEST_MODE
                    tx_msg[IDX_SOURCE] = TEST_MY_NODE_ID;    // This node (from TEST_MODE)
#else
                    tx_msg[IDX_SOURCE] = NODE_8;             // This node (default)
#endif
                    tx_msg[IDX_DEST] = NODE_ALL;     // Broadcast to all nodes
                    tx_msg[IDX_PRIORITY] = 1;        // Normal priority

                } else if (tx_state == TX_STATE_RETRANS && has_pending_retrans) {
                    /* Retransmission of failed message */
                    should_transmit = true;
                    tx_type_desc = "RETRANSMISSION";

                    /* Use stored retransmission message */
                    memcpy(tx_msg, retrans_msg, sizeof(tx_msg));

                } else {
                    /* No transmission needed - already successful or idle */
                    should_transmit = false;
                    tx_type_desc = "SKIP (already successful or idle)";
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
                        static char tx_complete_msg[150];
                        snprintf(tx_complete_msg, sizeof(tx_complete_msg),
                                "[%ums] DATA TX completed, collecting ACKs...",
                                get_elapsed_ms(last_sync_cycles));
                        test_run_info((unsigned char *)tx_complete_msg);
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                        /* Start interrupt-based ACK collection using CPU cycles */
                        uint32_t collection_start_cycles = dwt_timer_get_cycles();

                        /* Initialize ACK collection state */
                        collecting_acks = true;
                        collected_ack_count = 0;
                        /* Note: ack_received_from is NOT cleared - cumulative per cycle */
                        ack_collection_end_time = collection_start_cycles + slot_duration_cycles;  // slot duration

                        /* No timeout - continuous RX mode for ACK collection */
                        dwt_setrxtimeout(0);  // Disable timeout for continuous reception

                        /* Enable RX for ACK collection - interrupts will handle the rest */
                        dwt_forcetrxoff();
                        dwt_writesysstatuslo(0xFFFFFFFF);
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);

                        static char start_msg[150];
                        snprintf(start_msg, sizeof(start_msg),
                                "[%ums] ACK collection started: duration=%d cycles (%dms), no timeout, continuous RX",
                                get_elapsed_ms(last_sync_cycles), (int)(slot_duration_cycles), (int)(slot_duration_cycles / (CPU_FREQ_HZ/1000)));
                        test_run_info((unsigned char *)start_msg);

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

        /* Check if ACK collection has finished using CPU cycles */
        if (collecting_acks) {
            /* Get current CPU cycle count - no SPI access needed */
            uint32_t current_cycles = dwt_timer_get_cycles();

            /* Check if collection period has ended */
            if (current_cycles >= ack_collection_end_time) {
                collecting_acks = false;

#if TEST_MODE
                /* NODE_10: Immediate config switch after ACK collection */
                if (TEST_MY_NODE_SEQ == 10 && !config_is_sync) {
                    dwt_forcetrxoff();
                    if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                        test_run_info((unsigned char *)"NODE_10: Immediate switch to SYNC config after ACK collection");
                        config_is_sync = true;
                    } else {
                        test_run_info((unsigned char *)"WARNING: NODE_10 failed to switch to SYNC config!");
                    }
                    /* Re-enable RX after config change */
                    dwt_writesysstatuslo(0xFFFFFFFF);
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                }
#endif

                /* Report detailed ACK collection results */
                static char ack_result[200];
                snprintf(ack_result, sizeof(ack_result),
                        "[%ums] ACK collection ended: %d unique nodes, %d total ACKs, time-based ending",
                        get_elapsed_ms(last_sync_cycles), unique_ack_count, collected_ack_count);
                test_run_info((unsigned char *)ack_result);

                /* List which nodes responded */
                if (unique_ack_count > 0) {
                    static char node_list[100];
                    int pos = snprintf(node_list, sizeof(node_list), "Responding nodes: ");
                    for (int i = 0; i < TOTAL_NODES && pos < sizeof(node_list) - 10; i++) {
                        if (ack_received_from[i]) {
                            uint8_t node_char = index_to_node_id(i);
                            pos += snprintf(node_list + pos, sizeof(node_list) - pos, "%c ", node_char);
                        }
                    }
                    test_run_info((unsigned char *)node_list);
                }

                /* Evaluate ACK collection result based on cumulative ACKs in this cycle */
                /* FL node needs ACK from INIT + FR */
                /* FR node needs ACK from INIT + FL */
                /* Normal nodes exclude FL/FR from expected ACKs */
                uint8_t my_node_index = node_id_to_index(TEST_MY_NODE_ID);
                bool is_fl_node = (TEST_MY_NODE_ID == NODE_FL);
                bool is_fr_node = (TEST_MY_NODE_ID == NODE_FR);
                bool is_fl_fr_node = (is_fl_node || is_fr_node);
                int cumulative_acks = 0;
                bool has_init_ack = false;
                bool has_fl_ack = false;
                bool has_fr_ack = false;

                /* Count ACKs and check for specific node ACKs */
                /* Normal nodes: exclude FL/FR from expected ACKs */
                uint8_t fl_index = node_id_to_index(NODE_FL);  // Index 8
                uint8_t fr_index = node_id_to_index(NODE_FR);  // Index 9

                for (int i = 0; i < TOTAL_NODES; i++) {
                    if (i != my_node_index && ack_received_from[i]) {
                        /* Normal nodes don't count FL/FR ACKs as they shouldn't receive them */
                        if (!is_fl_fr_node && (i == fl_index || i == fr_index)) {
                            /* Skip FL/FR ACKs for normal nodes */
                            continue;
                        }
                        cumulative_acks++;
                        if (i == 0) {  // Index 0 is INIT node
                            has_init_ack = true;
                        } else if (i == fl_index) {  // FL node ACK
                            has_fl_ack = true;
                        } else if (i == fr_index) {  // FR node ACK
                            has_fr_ack = true;
                        }
                    }
                }

                /* FL node: success if INIT + FR ACK received */
                /* FR node: success if INIT + FL ACK received */
                /* Other nodes: success if all expected nodes ACK */
                bool success;
                if (is_fl_node) {
                    success = has_init_ack && has_fr_ack;
                } else if (is_fr_node) {
                    success = has_init_ack && has_fl_ack;
                } else {
                    success = (cumulative_acks >= expected_nodes);
                }

                if (success) {
                    /* SUCCESS: All expected nodes responded (cumulative) */
                    tx_state = TX_STATE_IDLE;
                    has_pending_retrans = false;

                    /* Update success statistics */
                    if (!cycle_completed) {
                        successful_cycles++;
                        if (current_period_in_cycle == 1) {
                            first_tx_success++;
                        } else {
                            retrans_success++;
                        }
                        cycle_completed = true;

                        /* Print progress every 100 cycles */
                        if (total_cycles % 100 == 0) {
                            static char progress[200];
                            float success_rate = (float)successful_cycles / total_cycles * 100;
                            snprintf(progress, sizeof(progress),
                                    "Progress: %d/10000 cycles (%.1f%% success, First:%d, Retrans:%d)",
                                    total_cycles, success_rate, first_tx_success, retrans_success);
                            test_run_info((unsigned char *)progress);
                        }
                    }

                    static char success_msg[200];
                    if (is_fl_node) {
                        snprintf(success_msg, sizeof(success_msg),
                                "FL SUCCESS: INIT + FR ACKs received - State: %d->IDLE(0) - Cycle %d Period %d",
                                tx_state, current_cycle, current_period_in_cycle);
                    } else if (is_fr_node) {
                        snprintf(success_msg, sizeof(success_msg),
                                "FR SUCCESS: INIT + FL ACKs received - State: %d->IDLE(0) - Cycle %d Period %d",
                                tx_state, current_cycle, current_period_in_cycle);
                    } else {
                        snprintf(success_msg, sizeof(success_msg),
                                "TRANSMISSION SUCCESS: %d/%d nodes responded (cumulative) - State: %d->IDLE(0) - Cycle %d Period %d",
                                cumulative_acks, expected_nodes, tx_state, current_cycle, current_period_in_cycle);
                    }
                    test_run_info((unsigned char *)success_msg);

                } else {
                    /* FAILURE: Need retransmission */
                    if (tx_state == TX_STATE_FIRST_TX) {
                        /* First transmission failed - prepare for retransmission */
                        tx_state = TX_STATE_RETRANS;
                        memcpy(retrans_msg, tx_msg, sizeof(tx_msg));
                        has_pending_retrans = true;
                        retrans_attempt = 1;

                        static char fail_msg[200];
                        if (is_fl_node) {
                            snprintf(fail_msg, sizeof(fail_msg),
                                    "FL FIRST TX FAILED: Missing ACK from %s%s - State: FIRST_TX(1)->RETRANS(2) - Cycle %d Period %d",
                                    has_init_ack ? "" : "INIT ", has_fr_ack ? "" : "FR", current_cycle, current_period_in_cycle);
                        } else if (is_fr_node) {
                            snprintf(fail_msg, sizeof(fail_msg),
                                    "FR FIRST TX FAILED: Missing ACK from %s%s - State: FIRST_TX(1)->RETRANS(2) - Cycle %d Period %d",
                                    has_init_ack ? "" : "INIT ", has_fl_ack ? "" : "FL", current_cycle, current_period_in_cycle);
                        } else {
                            snprintf(fail_msg, sizeof(fail_msg),
                                    "FIRST TX FAILED: %d/%d nodes responded (cumulative) - State: FIRST_TX(1)->RETRANS(2) - Cycle %d Period %d",
                                    cumulative_acks, expected_nodes, current_cycle, current_period_in_cycle);
                        }
                        test_run_info((unsigned char *)fail_msg);

                    } else {
                        /* Retransmission also failed */
                        retrans_attempt++;

                        static char retry_msg[200];
                        if (is_fl_node) {
                            snprintf(retry_msg, sizeof(retry_msg),
                                    "FL RETRANS FAILED: Missing ACK from %s%s - Attempt %d - State: RETRANS(2) - Cycle %d Period %d",
                                    has_init_ack ? "" : "INIT ", has_fr_ack ? "" : "FR", retrans_attempt, current_cycle, current_period_in_cycle);
                        } else if (is_fr_node) {
                            snprintf(retry_msg, sizeof(retry_msg),
                                    "FR RETRANS FAILED: Missing ACK from %s%s - Attempt %d - State: RETRANS(2) - Cycle %d Period %d",
                                    has_init_ack ? "" : "INIT ", has_fl_ack ? "" : "FL", retrans_attempt, current_cycle, current_period_in_cycle);
                        } else {
                            snprintf(retry_msg, sizeof(retry_msg),
                                    "RETRANS FAILED: %d/%d nodes responded (cumulative) - Attempt %d - State: RETRANS(2) - Cycle %d Period %d",
                                    cumulative_acks, expected_nodes, retrans_attempt, current_cycle, current_period_in_cycle);
                        }
                        test_run_info((unsigned char *)retry_msg);
                    }

                    /* List missing nodes for debugging */
                    if (is_fl_node || is_fr_node) {
                        /* FL/FR specific: report missing ACKs */
                        static char missing_fl_fr[100];
                        int pos = snprintf(missing_fl_fr, sizeof(missing_fl_fr), "%s Missing ACKs from: ",
                                         is_fl_node ? "FL" : "FR");
                        if (!has_init_ack) {
                            pos += snprintf(missing_fl_fr + pos, sizeof(missing_fl_fr) - pos, "INIT ");
                        }
                        if (is_fl_node && !has_fr_ack) {
                            pos += snprintf(missing_fl_fr + pos, sizeof(missing_fl_fr) - pos, "FR ");
                        } else if (is_fr_node && !has_fl_ack) {
                            pos += snprintf(missing_fl_fr + pos, sizeof(missing_fl_fr) - pos, "FL ");
                        }
                        test_run_info((unsigned char *)missing_fl_fr);
                    } else {
                        /* Normal nodes: list all missing ACKs */
                        static char missing_list[100];
                        int pos = snprintf(missing_list, sizeof(missing_list), "Missing ACKs from: ");
                        uint8_t my_node_index = 0;  // Get own node index for exclusion
#if TEST_MODE
                        my_node_index = node_id_to_index(TEST_MY_NODE_ID);
#else
                        my_node_index = node_id_to_index(NODE_2);
#endif
                        uint8_t fl_idx = node_id_to_index(NODE_FL);
                        uint8_t fr_idx = node_id_to_index(NODE_FR);

                        for (int i = 0; i < TOTAL_NODES && pos < sizeof(missing_list) - 10; i++) {
                            if (!ack_received_from[i] && i != my_node_index) {  // Skip own node
                                /* Normal nodes don't expect FL/FR ACKs */
                                if (i == fl_idx || i == fr_idx) {
                                    continue;  // Skip FL/FR as they're not expected
                                }
                                uint8_t node_char = index_to_node_id(i);
                                pos += snprintf(missing_list + pos, sizeof(missing_list) - pos, "%c ", node_char);
                            }
                        }
                        if (pos > strlen("Missing ACKs from: ")) {
                            test_run_info((unsigned char *)missing_list);
                        }
                    }
                }

                /* Reset timeout and re-enable normal RX - POLLING MODE: No critical section needed */
                dwt_setrxtimeout(0);  // Disable timeout for normal operation
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
            /* Small delay to prevent overwhelming the processor - same as initiator */
            //nrf_delay_us(450); // 450 microseconds delay
        }

        /* POLLING: Check for RX events by reading status register */
        uint32_t status_reg = dwt_readsysstatuslo();
        if (status_reg & DWT_INT_RXFCG_BIT_MASK) {  // RX frame received
            /* Get frame length - use a fixed max length for simplicity */
            uint16_t frame_len = FRAME_LEN_MAX;  // Read max possible length

            /* Clear buffer and read frame */
            memset(rx_buffer, 0, sizeof(rx_buffer));

            /* Read the received frame data */
            dwt_readrxdata(rx_buffer, frame_len, 0);

            /* Clear RX good flag */
            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

            /* Special handling for ACK collection mode */
            if (collecting_acks) {
                /* Check if received frame is an ACK */
                if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_ACK) {
                    /* Track ACK source to prevent duplicates */
                    uint8_t ack_source_char = rx_buffer[IDX_SOURCE];
                    uint8_t ack_index = node_id_to_index(ack_source_char);

                    if (ack_index != 0xFF && ack_index < TOTAL_NODES) {
                        if (!ack_received_from[ack_index]) {
                            /* First ACK from this node in this cycle - mark it */
                            ack_received_from[ack_index] = 1;
                            collected_ack_count++;

                            /* Count cumulative unique ACKs in this cycle (excluding self) */
                            uint8_t my_idx = node_id_to_index(TEST_MY_NODE_ID);
                            uint8_t init_idx = 0;  // INIT is always index 0
                            uint8_t fl_idx = node_id_to_index(NODE_FL);
                            uint8_t fr_idx = node_id_to_index(NODE_FR);
                            bool is_fl = (TEST_MY_NODE_ID == NODE_FL);
                            bool is_fr = (TEST_MY_NODE_ID == NODE_FR);
                            int cumulative_acks = 0;
                            bool early_success = false;

                            if (is_fl) {
                                /* FL node: check for INIT and FR ACKs specifically */
                                bool has_init = ack_received_from[init_idx];
                                bool has_fr = ack_received_from[fr_idx];
                                cumulative_acks = (has_init ? 1 : 0) + (has_fr ? 1 : 0);
                                early_success = has_init && has_fr;
                            } else if (is_fr) {
                                /* FR node: check for INIT and FL ACKs specifically */
                                bool has_init = ack_received_from[init_idx];
                                bool has_fl = ack_received_from[fl_idx];
                                cumulative_acks = (has_init ? 1 : 0) + (has_fl ? 1 : 0);
                                early_success = has_init && has_fl;
                            } else {
                                /* Normal nodes: count all ACKs except self and FL/FR */
                                for (int i = 0; i < TOTAL_NODES; i++) {
                                    if (i != my_idx && ack_received_from[i]) {
                                        /* Skip FL/FR ACKs for normal nodes */
                                        if (i == fl_idx || i == fr_idx) {
                                            continue;
                                        }
                                        cumulative_acks++;
                                    }
                                }
                                early_success = (cumulative_acks >= expected_nodes);
                            }

                            static char ack_msg[100];
                            snprintf(ack_msg, sizeof(ack_msg), "[%ums] ACK from node %c (idx:%d, cumulative:%d/%d)",
                                    get_elapsed_ms(last_sync_cycles), ack_source_char, ack_index, cumulative_acks, expected_nodes);
                            test_run_info((unsigned char *)ack_msg);

                            /* Check if we've received sufficient ACKs for early completion */

                            if (early_success) {
                                collecting_acks = false;

#if TEST_MODE
                                /* NODE_10: Immediate config switch after early ACK collection completion */
                                if (TEST_MY_NODE_SEQ == 10 && !config_is_sync) {
                                    dwt_forcetrxoff();
                                    if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                                        test_run_info((unsigned char *)"NODE_10: Immediate switch to SYNC config after early ACK completion");
                                        config_is_sync = true;
                                    } else {
                                        test_run_info((unsigned char *)"WARNING: NODE_10 failed to switch to SYNC config after early completion!");
                                    }
                                    /* Re-enable RX after config change */
                                    dwt_writesysstatuslo(0xFFFFFFFF);
                                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                                }
#endif

                                static char complete_msg[150];
                                if (is_fl) {
                                    snprintf(complete_msg, sizeof(complete_msg),
                                            "[%ums] FL ACK collection COMPLETE: INIT + FR ACKs received - ending early",
                                            get_elapsed_ms(last_sync_cycles));
                                } else if (is_fr) {
                                    snprintf(complete_msg, sizeof(complete_msg),
                                            "[%ums] FR ACK collection COMPLETE: INIT + FL ACKs received - ending early",
                                            get_elapsed_ms(last_sync_cycles));
                                } else {
                                    snprintf(complete_msg, sizeof(complete_msg),
                                            "[%ums] ACK collection COMPLETE: %d/%d nodes (cumulative) - ending early",
                                            get_elapsed_ms(last_sync_cycles), cumulative_acks, expected_nodes);
                                }
                                test_run_info((unsigned char *)complete_msg);

                                /* Immediately evaluate and set transmission state */
                                tx_state = TX_STATE_IDLE;
                                has_pending_retrans = false;

                                /* Update success statistics - same as main success logic */
                                if (!cycle_completed) {
                                    successful_cycles++;
                                    if (current_period_in_cycle == 1) {
                                        first_tx_success++;
                                    } else {
                                        retrans_success++;
                                    }
                                    cycle_completed = true;

                                    /* Print progress every 100 cycles */
                                    if (total_cycles % 100 == 0) {
                                        static char progress[200];
                                        float success_rate = (float)successful_cycles / total_cycles * 100;
                                        snprintf(progress, sizeof(progress),
                                                "Progress: %d/10000 cycles (%.1f%% success, First:%d, Retrans:%d)",
                                                total_cycles, success_rate, first_tx_success, retrans_success);
                                        test_run_info((unsigned char *)progress);
                                    }
                                }

                                test_run_info((unsigned char *)"TRANSMISSION SUCCESS - All ACKs received (cumulative)");
                            }
                        } else {
                            /* Duplicate ACK from same node */
                            collected_ack_count++;  // Still count for total
                            static char dup_msg[100];
                            snprintf(dup_msg, sizeof(dup_msg), "Duplicate ACK from node %c (total:%d)",
                                    ack_source_char, collected_ack_count);
                            test_run_info((unsigned char *)dup_msg);
                        }
                    } else {
                        /* Invalid node ID */
                        static char err_msg[100];
                        snprintf(err_msg, sizeof(err_msg), "ACK from invalid node ID: 0x%02X ('%c')",
                                ack_source_char, (ack_source_char >= 32 && ack_source_char < 127) ? ack_source_char : '?');
                        test_run_info((unsigned char *)err_msg);
                    }
                } else {
                    static char non_ack_msg[100];
                    snprintf(non_ack_msg, sizeof(non_ack_msg),
                            "Non-ACK received during collection from src=%c",
                            rx_buffer[IDX_SOURCE]);
                    test_run_info((unsigned char *)non_ack_msg);
                }

                /* Quickly re-enable RX for next ACK - no critical section needed in polling mode */
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);  // Clear all status
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
                continue;  // Skip normal processing - optimized for speed
            }
        // /* Debug: Print status register periodically - show even if 0 */
        // if (loop_counter % 50000 == 0) {
        //     static char status_debug[150];
        //     snprintf(status_debug, sizeof(status_debug), "Status reg: 0x%08X, RXFCG=%s, ERRORS=%s",
        //              status_reg,
        //              (status_reg & DWT_INT_RXFCG_BIT_MASK) ? "YES" : "no",
        //              (status_reg & SYS_STATUS_ALL_RX_ERR) ? "YES" : "no");
        //     test_run_info((unsigned char *)status_debug);
        // }

        /* Print received event for debugging */
        // static char event_msg[200];
        // snprintf(event_msg, sizeof(event_msg),
        //         "*** FRAME EVENT: 0x%08X - RXFCG:%s RXPRD:%s RXSFDD:%s RXPHD:%s ERRORS:%s ***",
        //         status_reg,
        //         (status_reg & 0x4000) ? "Y" : "n",   // Frame good
        //         (status_reg & 0x0100) ? "Y" : "n",   // Preamble detected
        //         (status_reg & 0x0200) ? "Y" : "n",   // SFD detected
        //         (status_reg & 0x0800) ? "Y" : "n",   // PHY header detected
        //         (status_reg & 0x0F8000) ? "Y" : "n"); // Any errors
        // test_run_info((unsigned char *)event_msg);

            /* Log RX event */
            test_run_info((unsigned char *)"*** RX OK EVENT ***");
            /* Print detailed RX frame information */
            static char rx_debug[200];
            snprintf(rx_debug, sizeof(rx_debug),
                    "RX Frame: len=%d, [0-11]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X, IDX_MSG_TYPE[4]=%02X",
                    rx_frame_len,
                    rx_buffer[0], rx_buffer[1], rx_buffer[2], rx_buffer[3], rx_buffer[4], rx_buffer[5],
                    rx_buffer[6], rx_buffer[7], rx_buffer[8], rx_buffer[9], rx_buffer[10], rx_buffer[11],
                    rx_buffer[IDX_MSG_TYPE]);
            test_run_info((unsigned char *)rx_debug);

            /* Validate the frame is the one expected as sent by "TX then wait for a response" example. */
            if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_SYNC) {
                /* Get current CPU cycle count for SYNC timing */
                uint32_t current_sync_cycles = dwt_timer_get_cycles();
                uint32_t old_sync_cycles = last_sync_cycles;

                /* Update sync time for new period */
                last_sync_cycles = current_sync_cycles;

                /* Each SYNC starts a new period - always reset slot flag */
                slot_executed_this_sync = false;
                slot_active = false;

                /* Update cycle and period tracking */
                current_period_in_cycle++;
                if (current_period_in_cycle > PERIODS_PER_CYCLE) {
                    /* New cycle started */
                    current_cycle++;
                    current_period_in_cycle = 1;

                    /* Reset retransmission state for new cycle */
                    tx_state = TX_STATE_FIRST_TX;
                    has_pending_retrans = false;
                    retrans_attempt = 0;
                    memset(retrans_msg, 0, sizeof(retrans_msg));

                    /* Reset ACK tracking for new cycle (cumulative per cycle) */
                    memset(ack_received_from, 0, sizeof(ack_received_from));
                    unique_ack_count = 0;

                    /* Update total_cycles to reflect the current cycle we just started */
                    if (current_cycle > total_cycles) {
                        /* We've started a new cycle, count it immediately */
                        total_cycles = current_cycle;
                    }
                    cycle_completed = false;  // Reset for new cycle

                    static char cycle_msg[200];
                    snprintf(cycle_msg, sizeof(cycle_msg),
                            "NEW CYCLE %d started - Period %d - State: %d->FIRST_TX(1) - Retrans cleared",
                            current_cycle, current_period_in_cycle, tx_state);
                    test_run_info((unsigned char *)cycle_msg);
                } else {
                    static char period_msg[100];
                    snprintf(period_msg, sizeof(period_msg),
                            "[%ums] Cycle %d - Period %d",
                            get_elapsed_ms(last_sync_cycles), current_cycle, current_period_in_cycle);
                    test_run_info((unsigned char *)period_msg);
                }

                /* Debug: Show SYNC timing details */
                static char sync_timing_debug[200];
                snprintf(sync_timing_debug, sizeof(sync_timing_debug),
                        "[%ums] SYNC received from src=%c! old=%u, new=%u, diff=%u cycles",
                        get_elapsed_ms(last_sync_cycles), rx_buffer[IDX_SOURCE], old_sync_cycles, current_sync_cycles,
                        (old_sync_cycles != 0) ? (current_sync_cycles - old_sync_cycles) : 0);
                test_run_info((unsigned char *)sync_timing_debug);

                test_run_info((unsigned char *)"Slot timing reset, waiting for my slot... - slot flag RESET");

                /* Switch to DATA config after receiving SYNC */
                if (config_is_sync) {
                    dwt_forcetrxoff();
                    if (dwt_configure(&config_data) == DWT_SUCCESS) {
                        test_run_info((unsigned char *)"Switched to DATA config (PLEN64) after SYNC RX");
                        config_is_sync = false;
                    } else {
                        test_run_info((unsigned char *)"WARNING: Failed to switch to DATA config!");
                    }
                }

                /* Clear RX OK event flag and re-enable RX */
                rx_event_flags &= ~RX_EVENT_OK_BIT;

                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
                continue;
            }
            
            else if ((rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA || rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_URGENT || rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_RELAY_DATA))
            {
                /* Check if this is our own message (self-reception) */
                if (rx_buffer[IDX_SOURCE] == TEST_MY_NODE_ID) {
                    test_run_info((unsigned char *)"Self-reception detected - ignoring own DATA message");
                }
                /* Only send ACK if NOT our own message AND NOT in our own slot */
                else if (!slot_active) {
                    /* RELAY TEST MODE: Normal nodes should NOT ACK FL/FR direct messages */
                    /* But should ACK RELAY_DATA messages to the relay node (INIT) */
                    uint8_t src_node = rx_buffer[IDX_SOURCE];
                    bool should_skip_ack = false;
                    bool is_relay_data = (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_RELAY_DATA);

#if RELAY_TEST_MODE
                    /* Skip ACK only for direct FL/FR DATA messages, not for RELAY_DATA */
                    if (!is_relay_data && (src_node == NODE_FL || src_node == NODE_FR)) {
                        should_skip_ack = true;
                        static char relay_skip_msg[150];
                        snprintf(relay_skip_msg, sizeof(relay_skip_msg),
                                "[%ums] DATA from %c (FL/FR) - NO ACK (simulating poor connectivity for relay test)",
                                get_elapsed_ms(last_sync_cycles), src_node);
                        test_run_info((unsigned char *)relay_skip_msg);
                    }
#endif

                    if (!should_skip_ack) {
                        /* Log message based on message type */
                        if (is_relay_data) {
                            static char relay_ack_msg[150];
                            snprintf(relay_ack_msg, sizeof(relay_ack_msg),
                                    "[%ums] RELAY_DATA from %c (orig_src=%c) - sending ACK to relay node",
                                    get_elapsed_ms(last_sync_cycles), src_node, rx_buffer[IDX_ORIG_SRC]);
                            test_run_info((unsigned char *)relay_ack_msg);
                        } else {
                            static char data_received_msg[100];
                            snprintf(data_received_msg, sizeof(data_received_msg),
                                    "[%ums] DATA received from src=%c, sending ACK",
                                    get_elapsed_ms(last_sync_cycles), rx_buffer[IDX_SOURCE]);
                            test_run_info((unsigned char *)data_received_msg);
                        }

                        /* Add delay based on node sequence to prevent ACK collisions */
                        /* Use staggered timing: Node 2=500us, Node 3=1000us, etc. */
                        /* Special handling for FL (SEQ 9) and FR (SEQ 11) - they use slots 9 and 11 but are nodes 9 and 10 */
                        uint32_t ack_delay_us;
#if defined(TEST_NODE_FL)
                        ack_delay_us = (MY_NODE_SEQ - 1) * ACK_TX_INTERVAL_US;  // SEQ 9: (9-1)*600 = 4800us
#elif defined(TEST_NODE_FR)
                        ack_delay_us = (MY_NODE_SEQ - 2) * ACK_TX_INTERVAL_US;  // SEQ 11: (11-2)*600 = 5400us (acts as 10th node)
#else
                        ack_delay_us = (MY_NODE_SEQ - 1) * ACK_TX_INTERVAL_US;  // Normal nodes use standard calculation
#endif
                        if (ack_delay_us > 0) {
                            static char delay_msg[100];
                            snprintf(delay_msg, sizeof(delay_msg), "ACK delay: %u us for node %d", ack_delay_us, MY_NODE_SEQ);
                            test_run_info((unsigned char *)delay_msg);
                            nrf_delay_us(ack_delay_us);  // Wait for our turn to send ACK
                        }

                        /* Prepare ACK message */
                        if (is_relay_data) {
                            /* For RELAY_DATA: send ACK to relay node (INIT), not original source */
                            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_RELAY_ACK;  // Use RELAY_ACK type
#if TEST_MODE
                            tx_msg[IDX_SOURCE] = TEST_MY_NODE_ID;       // This node
#else
                            tx_msg[IDX_SOURCE] = NODE_2;                // This node (default)
#endif
                            tx_msg[IDX_DEST] = rx_buffer[IDX_SOURCE];   // Reply to relay node (INIT)
                            tx_msg[IDX_ORIG_SRC] = rx_buffer[IDX_ORIG_SRC]; // Include original source
                            tx_msg[IDX_ORIG_DST] = rx_buffer[IDX_ORIG_DST]; // Include original dest
                        } else {
                            /* For normal DATA: send regular ACK to sender */
                            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK;        // Regular ACK
#if TEST_MODE
                            tx_msg[IDX_SOURCE] = TEST_MY_NODE_ID;       // This node
#else
                            tx_msg[IDX_SOURCE] = NODE_2;                // This node (default)
#endif
                            tx_msg[IDX_DEST] = rx_buffer[IDX_SOURCE];   // Reply to sender
                            tx_msg[IDX_ORIG_SRC] = 0;                   // Clear relay fields
                            tx_msg[IDX_ORIG_DST] = 0;                   // Clear relay fields
                        }

                        /* Write response frame data to DW IC and prepare transmission. */
                        dwt_writetxdata(sizeof(tx_msg), tx_msg, 0); /* Zero offset in TX buffer. */
                        dwt_writetxfctrl(sizeof(tx_msg), 0, 0);     /* Zero offset in TX buffer, no ranging. */

                        /* Send the response with immediate RX return */
                        int ack_tx_result = dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                        if (ack_tx_result == DWT_SUCCESS) {
                            /* Poll DW IC until TX frame sent event set. */
                            uint32_t ack_tx_status = 0;
                            waitforsysstatus(&ack_tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                            if (ack_tx_status & DWT_INT_TXFRS_BIT_MASK) {
                                /* Clear TX frame sent event. */
                                dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                                /* Increment the data frame sequence number (modulo 256). */
                                tx_msg[DATA_FRAME_SN_IDX]++;

                                /* Log ACK sent message */
                                if (is_relay_data) {
                                    static char relay_ack_sent[150];
                                    snprintf(relay_ack_sent, sizeof(relay_ack_sent),
                                            "[%ums] RELAY_ACK sent successfully to %c for orig_src=%c",
                                            get_elapsed_ms(last_sync_cycles), rx_buffer[IDX_SOURCE], rx_buffer[IDX_ORIG_SRC]);
                                    test_run_info((unsigned char *)relay_ack_sent);
                                } else {
                                    static char ack_sent_msg[100];
                                    snprintf(ack_sent_msg, sizeof(ack_sent_msg),
                                            "[%ums] ACK sent successfully to %c",
                                            get_elapsed_ms(last_sync_cycles), rx_buffer[IDX_SOURCE]);
                                    test_run_info((unsigned char *)ack_sent_msg);
                                }
                            } else {
                                /* ACK TX completion failed */
                                static char ack_fail_log[150];
                                snprintf(ack_fail_log, sizeof(ack_fail_log),
                                        "[%ums] ACK TX completion failed to %c (status: 0x%08X)",
                                        get_elapsed_ms(last_sync_cycles), rx_buffer[IDX_SOURCE], ack_tx_status);
                                test_run_info((unsigned char *)ack_fail_log);
                            }
                        } else {
                            /* ACK TX failed to start */
                            static char ack_start_fail_log[150];
                            snprintf(ack_start_fail_log, sizeof(ack_start_fail_log),
                                    "[%ums] ACK TX failed to start to %c (result: %d)",
                                    get_elapsed_ms(last_sync_cycles), rx_buffer[IDX_SOURCE], ack_tx_result);
                            test_run_info((unsigned char *)ack_start_fail_log);
                        }
                    } else {
                        /* ACK skipped due to relay test mode */
                        static char skip_msg[100];
                        snprintf(skip_msg, sizeof(skip_msg), "[%ums] ACK skipped for relay testing", get_elapsed_ms(last_sync_cycles));
                        test_run_info((unsigned char *)skip_msg);
                    }
                } else {
                    static char data_no_ack_msg[100];
                    snprintf(data_no_ack_msg, sizeof(data_no_ack_msg),
                            "[%ums] DATA/RELAY_DATA received from src=%c during own slot - no ACK sent",
                            get_elapsed_ms(last_sync_cycles), rx_buffer[IDX_SOURCE]);
                    test_run_info((unsigned char *)data_no_ack_msg);
                }
            }

            /* Re-enable RX for continuous reception */
            dwt_forcetrxoff();                      // Ensure clean state
            dwt_writesysstatuslo(0xFFFFFFFF);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }

        /* POLLING: Handle RX timeout */
        if (status_reg & DWT_INT_RXFTO_BIT_MASK) {
            /* Clear timeout flag and re-enable RX */
            dwt_writesysstatuslo(DWT_INT_RXFTO_BIT_MASK);

            /* Skip timeout handling during ACK collection since we use time-based ending */
            if (!collecting_acks) {
                /* Normal timeout recovery only when not collecting ACKs */
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
            /* During ACK collection, ignore timeout events - use only time-based ending */
        }

        /* POLLING: Handle RX errors */
        if (status_reg & (DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXFCE_BIT_MASK |
                          DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXSTO_BIT_MASK)) {
            /* Clear error flags and re-enable RX */
            dwt_writesysstatuslo(DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXFCE_BIT_MASK |
                               DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXSTO_BIT_MASK);
            dwt_forcetrxoff();
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }

        /* Small delay to prevent overwhelming the processor - reduced for ms timing */
        //nrf_delay_us(10); // 10 microseconds delay
    }
}

/*! ------------------------------------------------------------------------------------------------------------------
 * @fn rx_ok_cb()
 *
 * @brief Callback to process RX good frame events
 *
 * @param  cb_data  callback data
 *
 * @return  none
 */
static void rx_ok_cb(const dwt_cb_data_t *cb_data)
{
    /* CRITICAL: No SPI operations in interrupt context! */
    /* Only set flags - let main loop handle the actual processing */

    /* Always capture frame length and set RX OK bit */
    rx_frame_len = cb_data->datalength;
    rx_event_flags |= RX_EVENT_OK_BIT;  // Set RX OK bit without clearing others
}

/*! ------------------------------------------------------------------------------------------------------------------
 * @fn rx_to_cb()
 *
 * @brief Callback to process RX timeout events
 *
 * @param  cb_data  callback data
 *
 * @return  none
 */
static void rx_to_cb(const dwt_cb_data_t *cb_data)
{
    (void)cb_data;
    /* Set timeout bit without clearing other events */
    rx_event_flags |= RX_EVENT_TO_BIT;
}

/*! ------------------------------------------------------------------------------------------------------------------
 * @fn rx_err_cb()
 *
 * @brief Callback to process RX error events
 *
 * @param  cb_data  callback data
 *
 * @return  none
 */
static void rx_err_cb(const dwt_cb_data_t *cb_data)
{
    (void)cb_data;
    /* Set error bit without clearing other events */
    rx_event_flags |= RX_EVENT_ERR_BIT;
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
