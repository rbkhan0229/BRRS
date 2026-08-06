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

#if defined(TEST_NORMAL_NODE)

extern void test_run_info(unsigned char *data);

/* ========== TEST MODE ========== */
#define TEST_MODE 1  // Set to 0 for default Node 8, 1 for easy node selection

#if TEST_MODE
    #define TEST_TOTAL_NODES 4     // Only 4 nodes in test: INIT(0), NODE_2, NODE_8, NODE_4
    #define TEST_EXPECTED_ACKS 3   // Expect ACKs from 3 other nodes (exclude self)

    // Select node type for testing (uncomment one):
    //#define TEST_NODE_1        // Node 1, slot 1
    #define TEST_NODE_FL       // Front Left node, seq 2
    //#define TEST_NODE_FR       // Front Right node, seq 3
    //#define TEST_NODE_4        // Node 4, slot 4
    //#define TEST_NODE_5        // Node 5, slot 5
    //#define TEST_NODE_6        // Node 6, slot 6
    //#define TEST_NODE_7        // Node 7, slot 7
    //#define TEST_NODE_8        // Node 8, slot 8 (default)
    //#define TEST_NODE_9        // Node 9, slot 9
    //#define TEST_NODE_10       // Node 10, slot 10

    #ifdef TEST_NODE_1
        #define APP_NAME "NORMAL NODE 1 v1.0"
        #define TEST_MY_NODE_ID NODE_1
        #define TEST_MY_NODE_SEQ 1
        #define TEST_MY_SLOT_INTERVAL_TICKS us_to_uwb_ticks(7000)    // 7ms (SEQ 1 * 7ms with 3ms slot + 4ms guard)
    #elif defined(TEST_NODE_FL)
        #define APP_NAME "FRONT LEFT NODE v1.0"
        #define TEST_MY_NODE_ID NODE_FL
        #define TEST_MY_NODE_SEQ 2
        #define TEST_MY_SLOT_INTERVAL_TICKS us_to_uwb_ticks(14000)   // 14ms (SEQ 2 * 7ms with 3ms slot + 4ms guard)
    #elif defined(TEST_NODE_FR)
        #define APP_NAME "FRONT RIGHT NODE v1.0"
        #define TEST_MY_NODE_ID NODE_FR
        #define TEST_MY_NODE_SEQ 3
        #define TEST_MY_SLOT_INTERVAL_TICKS us_to_uwb_ticks(21000)   // 21ms (SEQ 3 * 7ms with 3ms slot + 4ms guard)
    #elif defined(TEST_NODE_4)
        #define APP_NAME "NORMAL NODE 4 v1.0"
        #define TEST_MY_NODE_ID NODE_4
        #define TEST_MY_NODE_SEQ 4
        #define TEST_MY_SLOT_INTERVAL_TICKS us_to_uwb_ticks(28000)   // 28ms (SEQ 4 * 7ms with 3ms slot + 4ms guard)
    #elif defined(TEST_NODE_5)
        #define APP_NAME "NORMAL NODE 5 v1.0"
        #define TEST_MY_NODE_ID NODE_5
        #define TEST_MY_NODE_SEQ 5
        #define TEST_MY_SLOT_INTERVAL_TICKS us_to_uwb_ticks(35000)   // 35ms (SEQ 5 * 7ms with 3ms slot + 4ms guard)
    #elif defined(TEST_NODE_6)
        #define APP_NAME "NORMAL NODE 6 v1.0"
        #define TEST_MY_NODE_ID NODE_6
        #define TEST_MY_NODE_SEQ 6
        #define TEST_MY_SLOT_INTERVAL_TICKS us_to_uwb_ticks(42000)   // 42ms (SEQ 6 * 7ms with 3ms slot + 4ms guard)
    #elif defined(TEST_NODE_7)
        #define APP_NAME "NORMAL NODE 7 v1.0"
        #define TEST_MY_NODE_ID NODE_7
        #define TEST_MY_NODE_SEQ 7
        #define TEST_MY_SLOT_INTERVAL_TICKS us_to_uwb_ticks(49000)   // 49ms (SEQ 7 * 7ms with 3ms slot + 4ms guard)
    #elif defined(TEST_NODE_8)
        #define APP_NAME "NORMAL NODE 8 v1.0"
        #define TEST_MY_NODE_ID NODE_8
        #define TEST_MY_NODE_SEQ 8
        #define TEST_MY_SLOT_INTERVAL_TICKS us_to_uwb_ticks(56000)   // 56ms (SEQ 8 * 7ms with 3ms slot + 4ms guard)
    #elif defined(TEST_NODE_9)
        #define APP_NAME "NORMAL NODE 9 v1.0"
        #define TEST_MY_NODE_ID NODE_9
        #define TEST_MY_NODE_SEQ 9
        #define TEST_MY_SLOT_INTERVAL_TICKS us_to_uwb_ticks(63000)   // 63ms (SEQ 9 * 7ms with 3ms slot + 4ms guard)
    #elif defined(TEST_NODE_10)
        #define APP_NAME "NORMAL NODE 10 v1.0"
        #define TEST_MY_NODE_ID NODE_10
        #define TEST_MY_NODE_SEQ 10
        #define TEST_MY_SLOT_INTERVAL_TICKS us_to_uwb_ticks(70000)   // 70ms (SEQ 10 * 7ms with 3ms slot + 4ms guard)
    #else
        #error "Please select a node type for TEST_MODE"
    #endif
#else
    /* Default configuration */
    #define APP_NAME "NORMAL NODE v1.0"
    #define TEST_MY_NODE_ID NODE_8
    #define TEST_MY_NODE_SEQ 8
    #define TEST_MY_SLOT_INTERVAL_TICKS us_to_uwb_ticks(56000)  // 56ms (SEQ 8 * 7ms with 3ms slot + 4ms guard)
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

/* ========== TDMA Protocol Parameters ========== */
#define PERIOD_MS           50      // 50ms period
#define SLOT_DURATION_MS    5       // 5ms per slot
#define TOTAL_NODES         10      // Total nodes in network
#define PERIODS_PER_CYCLE   3       // 3 periods per cycle for 100ms message cycle

#if TEST_MODE
    #define MY_NODE_SEQ         TEST_MY_NODE_SEQ       // From TEST_MODE selection
    #define MY_SLOT_START_MS    (MY_NODE_SEQ * SLOT_DURATION_MS)  // Dynamic based on node
#else
    #define MY_NODE_SEQ         8       // Default node 8
    #define MY_SLOT_START_MS    (MY_NODE_SEQ * SLOT_DURATION_MS)  // 40ms for node 8
#endif

/* ========== ACK Reception Parameters ========== */
#define TX_TO_RX_DELAY_UUS  60      // Delay from TX end to RX activation (60us)
#define RX_ACK_TIMEOUT_UUS  4500    // ACK reception timeout (4.5ms)
#define ACK_SLOT_DURATION_US  450   // 450us per ACK slot
#define ACK_TX_INTERVAL_US    800   // 800us interval between each node's ACK transmission (increased for reliability)

/* Default communication configuration - MATCH rx_send_resp.c */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_64,      /* Preamble length - SAME AS WORKING EXAMPLE */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (64 + 1 + 8 - 8),    /* SFD timeout - SAME AS WORKING EXAMPLE */
    DWT_STS_MODE_OFF, /* No STS mode enabled */
    DWT_STS_LEN_64,   /* STS length */
    DWT_PDOA_M0       /* PDOA mode off */
};

/* ========== Node ID Definitions ========== */
#define NODE_INIT '1'    // Initiator node - slot 0
#define NODE_FL   '2'    // Front Left (needs relay)
#define NODE_FR   '3'    // Front Right (needs relay)
#define NODE_4    '4'    // Normal nodes
#define NODE_5    '5'
#define NODE_6    '6'
#define NODE_7    '7'
#define NODE_8    '8'    // This node
#define NODE_9    '9'
#define NODE_10   'A'
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
static uint8_t expected_nodes = TEST_EXPECTED_ACKS;        // Test mode: expect 2 ACKs (from INIT and NODE_2)
#else
static uint8_t expected_nodes = TOTAL_NODES - 1;           // Normal mode: all other nodes should ACK (excluding self)
#endif
/* Index to access to source address of the blink frame in the rx_buffer array. */
#define BLINK_FRAME_SRC_IDX 2

/* Values for the PG_DELAY and TX_POWER registers reflect the bandwidth and power of the spectrum at the current
 * temperature. These values can be calibrated prior to taking reference measurements. See NOTE 3 below. */
extern dwt_txconfig_t txconfig_options;

/* 여러 응답 수신 관련 */
#define MAX_RESPONSES 10

/* ========== UWB Timestamp Helper Functions ========== */

// Convert microseconds to UWB timestamp ticks (hi32)
// dwt_readsystimestamphi32() returns upper 32 bits of 40-bit timestamp
// This effectively divides the base clock by 256 (2^8)
// Base: 15.65 ps per tick, Hi32: ~4 ns per tick
// 1 second ≈ 250,000,000 hi32 ticks
// 1 microsecond ≈ 250 hi32 ticks
static uint32_t us_to_uwb_ticks(uint32_t microseconds) {
    // Approximately 250 ticks per microsecond for hi32 timestamp
    return (uint32_t)(microseconds * 250);
}

/**
 * Application entry point.
 */
/* ========== DWT Timer Functions - DISABLED TO PREVENT CONFLICTS ========== */

// All DWT timer functions disabled to prevent conflicts with dwt_rxenable()
// The CoreDebug and DWT registers used by these functions conflict with UWB operations

// static void dwt_timer_init(void) { /* DISABLED */ }
// static void dwt_timer_start(dwt_timer_t* timer, uint32_t duration_us) { /* DISABLED */ }
// static bool dwt_timer_is_expired(dwt_timer_t* timer) { /* DISABLED */ }
// static void dwt_timer_stop(dwt_timer_t* timer) { /* DISABLED */ }

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
        case NODE_INIT: return 0;  // '1' -> index 0
        case NODE_FL:   return 1;  // '2' -> index 1
        case NODE_FR:   return 2;  // '3' -> index 2
        case NODE_4:    return 3;  // '4' -> index 3
        case NODE_5:    return 4;  // '5' -> index 4
        case NODE_6:    return 5;  // '6' -> index 5
        case NODE_7:    return 6;  // '7' -> index 6
        case NODE_8:    return 7;  // '8' -> index 7
        case NODE_9:    return 8;  // '9' -> index 8
        case NODE_10:   return 9;  // 'A' -> index 9
        default:        return 0xFF;  // Invalid node ID
    }
}

/* Helper function to convert array index to node ID character */
static uint8_t index_to_node_id(uint8_t index) {
    switch(index) {
        case 0:  return NODE_INIT;  // index 0 -> '1'
        case 1:  return NODE_FL;    // index 1 -> '2'
        case 2:  return NODE_FR;    // index 2 -> '3'
        case 3:  return NODE_4;     // index 3 -> '4'
        case 4:  return NODE_5;     // index 4 -> '5'
        case 5:  return NODE_6;     // index 5 -> '6'
        case 6:  return NODE_7;     // index 6 -> '7'
        case 7:  return NODE_8;     // index 7 -> '8'
        case 8:  return NODE_9;     // index 8 -> '9'
        case 9:  return NODE_10;    // index 9 -> 'A'
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

int normal_node(void)
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
    int config_result = dwt_configure(&config);
    if (config_result)
    {
        static char config_error[100];
        snprintf(config_error, sizeof(config_error), "CONFIG FAILED - Error code: %d", config_result);
        test_run_info((unsigned char *)config_error);
        while (1) { };
    } else {
        test_run_info((unsigned char *)"Configuration successful");
    }

    /* Configure the TX spectrum parameters (power, PG delay and PG count) */
    dwt_configuretxrf(&txconfig_options);

    /* Setup interrupt callbacks */
    dwt_callbacks_s cbs = {NULL};
    cbs.cbRxOk = rx_ok_cb;
    cbs.cbRxTo = rx_to_cb;
    cbs.cbRxErr = rx_err_cb;
    dwt_setcallbacks(&cbs);

    /* Enable RX interrupts */
    dwt_setinterrupt(DWT_INT_RXFCG_BIT_MASK | DWT_INT_RXFTO_BIT_MASK | DWT_INT_RXPTO_BIT_MASK |
                     DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXFCE_BIT_MASK | DWT_INT_RXFSL_BIT_MASK |
                     DWT_INT_RXSTO_BIT_MASK, 0, DWT_ENABLE_INT);

    /* Clear any pending interrupts */
    dwt_writesysstatuslo(DWT_INT_RCINIT_BIT_MASK | DWT_INT_SPIRDY_BIT_MASK);

    /* Install DW IC IRQ handler */
    port_set_dwic_isr(dwt_isr);

    test_run_info((unsigned char *)"Normal Node initialized with RX interrupts");

    /* Loop forever sending and receiving frames periodically. */
    int test_slot_interval = 5000000;

    /* Print current configuration for debugging */
    static char config_debug[200];
    snprintf(config_debug, sizeof(config_debug),
             "CONFIG: Ch=%d, PLen=%d, TxCode=%d, RxCode=%d, Rate=%d",
             config.chan, config.txPreambLength, config.txCode, config.rxCode, config.dataRate);
    test_run_info((unsigned char *)config_debug);

    /* Disable RX timeout - same as working example */
    dwt_setrxtimeout(0);

    /* UWB timestamp-based slot timing variables */
    uint8_t dummy_time[5]; // For clearing timestamp latch
    uint32_t last_sync_time = 0;                            // When SYNC was received
#if TEST_MODE
    uint32_t slot_interval = TEST_MY_SLOT_INTERVAL_TICKS;   // Dynamic based on node selection
#else
    uint32_t slot_interval = us_to_uwb_ticks(56000);      // 56ms wait time (default SEQ 8 with 3ms slot + 4ms guard)
#endif
    uint32_t slot_duration = us_to_uwb_ticks(3000);       // 3ms for TX + ACK collection (early termination when successful)
    bool slot_active = false;                               // Track if currently in own slot
    bool slot_executed_this_sync = false;                   // Flag to ensure slot executes only once per SYNC

    /* Debug: Print calculated intervals */
    static char interval_debug[150];
#if TEST_MODE
    snprintf(interval_debug, sizeof(interval_debug),
             "Node %d: slot=%u ticks (%dms), seq=%d [3ms+4ms guard]",
             TEST_MY_NODE_SEQ, slot_interval, TEST_MY_NODE_SEQ * 7, MY_NODE_SEQ);
#else
    snprintf(interval_debug, sizeof(interval_debug),
             "Intervals: period=(73ms), slot=%u ticks (56ms) [3ms+4ms guard]",
              slot_interval);
#endif
    test_run_info((unsigned char *)interval_debug);

    test_run_info((unsigned char *)"Starting interrupt-based RX loop: 73ms period + 56ms slot + 3ms+4ms guard");

    /* Debug counter for loop iterations */
    static uint32_t loop_counter = 0;

    /* Enable RX immediately for continuous listening */
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    while (1)
    {
        /* Check if we've completed 10000 cycles and exit */
        if (current_cycle > 10000) {
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
            break;
        }

        static uint32_t loop_check_counter = 0;
        loop_check_counter++;
        
        /* Check slot timing CONTINUOUSLY without blocking */
        /* This runs in parallel with RX interrupt handling */
        if (last_sync_time != 0 && !slot_active && !slot_executed_this_sync && ((loop_check_counter % 2000 == 0))) {
            /* Critical section for timestamp access */
            decaIrqStatus_t irq_state = decamutexon();

            /* Clear timestamp latch and get current time */
            //dwt_writesysstatuslo(0x00000000);
            dwt_readsystime(dummy_time);
            uint32_t current_time = dwt_readsystimestamphi32();

            decamutexoff(irq_state);

            uint32_t time_since_sync = current_time - last_sync_time;

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

            /* Improved slot condition with better overflow handling */
            bool sync_valid = (last_sync_time != 0);
            bool time_advanced = (current_time != last_sync_time);
            bool slot_time_reached = (time_since_sync >= slot_interval);
            bool slot_not_executed = (!slot_executed_this_sync);

            /* Small delay to prevent overwhelming the processor - reduced for ms timing */
            nrf_delay_us(10); // 10 microseconds delay

            /* Check if slot interval has elapsed */
            if (sync_valid && time_advanced && slot_time_reached && slot_not_executed) {

                /* Debug: Show exact timing when slot starts */
                static char slot_timing_debug[200];
                snprintf(slot_timing_debug, sizeof(slot_timing_debug),
                        "Own slot start! time_since_sync=%u, slot_interval=%u, current_time=%u",
                        time_since_sync, slot_interval, current_time);
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
                        "TX Decision: %s - State: %d, Period: %d/%d, Attempt: %d",
                        tx_type_desc, tx_state, current_period_in_cycle, PERIODS_PER_CYCLE, retrans_attempt);
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
                        test_run_info((unsigned char *)"DATA TX completed, collecting ACKs...");
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                        /* Start interrupt-based ACK collection */
                        decaIrqStatus_t irq_state = decamutexon();
                        dwt_writesysstatuslo(0x00000000);
                        dwt_readsystime(dummy_time);
                        uint32_t collection_start = dwt_readsystimestamphi32();
                        decamutexoff(irq_state);

                        /* Initialize ACK collection state */
                        collecting_acks = true;
                        collected_ack_count = 0;
                        /* Note: ack_received_from is NOT cleared - cumulative per cycle */
                        ack_collection_end_time = collection_start + slot_duration;  // slot duration

                        /* No timeout - continuous RX mode for ACK collection */
                        dwt_setrxtimeout(0);  // Disable timeout for continuous reception

                        /* Enable RX for ACK collection - interrupts will handle the rest */
                        dwt_forcetrxoff();
                        dwt_writesysstatuslo(0xFFFFFFFF);
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);

                        static char start_msg[150];
                        snprintf(start_msg, sizeof(start_msg),
                                "ACK collection started: duration=%d ticks, no timeout, continuous RX",
                                (int)(slot_duration));
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

        /* Check if ACK collection has finished - reduce frequency to avoid SPI conflicts */
        static uint32_t ack_check_counter = 0;
        if (collecting_acks && ((ack_check_counter++ % 1000) == 0)) {
            /* Critical section for timestamp access */
            decaIrqStatus_t irq_state = decamutexon();

            /* Clear timestamp latch and get current time */
            dwt_writesysstatuslo(0x00000000);  // Clear latch
            dwt_readsystime(dummy_time);
            uint32_t current_time = dwt_readsystimestamphi32();

            decamutexoff(irq_state);
        
            /* Check if collection period has ended */
            if (current_time >= ack_collection_end_time) {
                collecting_acks = false;

                /* Report detailed ACK collection results */
                static char ack_result[200];
                snprintf(ack_result, sizeof(ack_result),
                        "ACK collection ended: %d unique nodes, %d total ACKs, time-based ending",
                        unique_ack_count, collected_ack_count);
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
                /* Count total unique ACKs received in this cycle (excluding self) */
                uint8_t my_node_index = node_id_to_index(TEST_MY_NODE_ID);
                int cumulative_acks = 0;
                for (int i = 0; i < TOTAL_NODES; i++) {
                    if (i != my_node_index && ack_received_from[i]) cumulative_acks++;
                }

                if (cumulative_acks >= expected_nodes) {
                    /* SUCCESS: All expected nodes responded (cumulative) */
                    tx_state = TX_STATE_IDLE;
                    has_pending_retrans = false;

                    /* Update success statistics */
                    if (!cycle_completed) {
                        total_cycles++;
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
                    snprintf(success_msg, sizeof(success_msg),
                            "TRANSMISSION SUCCESS: %d/%d nodes responded (cumulative) - State: %d->IDLE(0) - Cycle %d Period %d",
                            cumulative_acks, expected_nodes, tx_state, current_cycle, current_period_in_cycle);
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
                        snprintf(fail_msg, sizeof(fail_msg),
                                "FIRST TX FAILED: %d/%d nodes responded (cumulative) - State: FIRST_TX(1)->RETRANS(2) - Cycle %d Period %d",
                                cumulative_acks, expected_nodes, current_cycle, current_period_in_cycle);
                        test_run_info((unsigned char *)fail_msg);

                    } else {
                        /* Retransmission also failed */
                        retrans_attempt++;

                        static char retry_msg[200];
                        snprintf(retry_msg, sizeof(retry_msg),
                                "RETRANS FAILED: %d/%d nodes responded (cumulative) - Attempt %d - State: RETRANS(2) - Cycle %d Period %d",
                                cumulative_acks, expected_nodes, retrans_attempt, current_cycle, current_period_in_cycle);
                        test_run_info((unsigned char *)retry_msg);
                    }

                    /* List missing nodes for debugging */
                    static char missing_list[100];
                    int pos = snprintf(missing_list, sizeof(missing_list), "Missing ACKs from: ");
                    uint8_t my_node_index = 0;  // Get own node index for exclusion
#if TEST_MODE
                    my_node_index = node_id_to_index(TEST_MY_NODE_ID);
#else
                    my_node_index = node_id_to_index(NODE_8);
#endif
                    for (int i = 0; i < TOTAL_NODES && pos < sizeof(missing_list) - 10; i++) {
                        if (!ack_received_from[i] && i != my_node_index) {  // Skip own node
                            uint8_t node_char = index_to_node_id(i);
                            pos += snprintf(missing_list + pos, sizeof(missing_list) - pos, "%c ", node_char);
                        }
                    }
                    if (pos > strlen("Missing ACKs from: ")) {
                        test_run_info((unsigned char *)missing_list);
                    }
                }

                /* Critical section for RX configuration */
                decaIrqStatus_t irq_state = decamutexon();

                /* Reset timeout and re-enable normal RX */
                dwt_setrxtimeout(0);  // Disable timeout for normal operation
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);

                decamutexoff(irq_state);
            }
            /* Small delay to prevent overwhelming the processor - same as initiator */
            //nrf_delay_us(450); // 450 microseconds delay
        }

        /* Check for RX events signaled by interrupt handler */
        if (rx_event_flags & RX_EVENT_OK_BIT) {  // RX OK event
            /* Clear buffer and read frame */
            memset(rx_buffer, 0, sizeof(rx_buffer));

            if (rx_frame_len <= FRAME_LEN_MAX) {
                dwt_readrxdata(rx_buffer, rx_frame_len, 0);
            }

            /* Clear interrupt flag */
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
                            int cumulative_acks = 0;
                            for (int i = 0; i < TOTAL_NODES; i++) {
                                if (i != my_idx && ack_received_from[i]) cumulative_acks++;
                            }

                            static char ack_msg[100];
                            snprintf(ack_msg, sizeof(ack_msg), "ACK from node %c (idx:%d, cumulative:%d/%d)",
                                    ack_source_char, ack_index, cumulative_acks, expected_nodes);
                            test_run_info((unsigned char *)ack_msg);

                            /* Check if we've received all expected ACKs cumulatively - if so, end collection immediately */
                            if (cumulative_acks >= expected_nodes) {
                                collecting_acks = false;
                                static char complete_msg[100];
                                snprintf(complete_msg, sizeof(complete_msg),
                                        "ACK collection COMPLETE: %d/%d nodes (cumulative) - ending early",
                                        cumulative_acks, expected_nodes);
                                test_run_info((unsigned char *)complete_msg);

                                /* Immediately evaluate and set transmission state */
                                tx_state = TX_STATE_IDLE;
                                has_pending_retrans = false;
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

                /* Clear RX OK event flag and quickly re-enable RX for next ACK */
                rx_event_flags &= ~RX_EVENT_OK_BIT;

                /* Optimized critical section - minimal operations for fast ACK reception */
                decaIrqStatus_t irq_state = decamutexon();
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);  // Clear all status
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
                decamutexoff(irq_state);
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
                /* Critical section for timestamp access */
                decaIrqStatus_t irq_state = decamutexon();

                /* Clear timestamp latch and get current time FIRST */
                dwt_writesysstatuslo(0x00000000);  // Clear latch
                dwt_readsystime(dummy_time);
                uint32_t current_sync_time = dwt_readsystimestamphi32();

                decamutexoff(irq_state);
                uint32_t old_sync_time = last_sync_time;

                /* Update sync time for new period */
                last_sync_time = current_sync_time;

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

                    /* Update statistics for previous cycle if it was completed */
                    if (current_cycle > 1 && !cycle_completed) {
                        /* Previous cycle failed - count it */
                        total_cycles++;
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
                            "Cycle %d - Period %d",
                            current_cycle, current_period_in_cycle);
                    test_run_info((unsigned char *)period_msg);
                }

                /* Debug: Show SYNC timing details */
                static char sync_timing_debug[200];
                snprintf(sync_timing_debug, sizeof(sync_timing_debug),
                        "SYNC received from src=%c! old=%u, new=%u, diff=%u",
                        rx_buffer[IDX_SOURCE], old_sync_time, current_sync_time,
                        (old_sync_time != 0) ? (current_sync_time - old_sync_time) : 0);
                test_run_info((unsigned char *)sync_timing_debug);

                test_run_info((unsigned char *)"Slot timing reset, waiting for my slot... - slot flag RESET");

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
                    static char data_received_msg[100];
                    snprintf(data_received_msg, sizeof(data_received_msg),
                            "DATA received from src=%c, sending ACK",
                            rx_buffer[IDX_SOURCE]);
                    test_run_info((unsigned char *)data_received_msg);

                    /* Add delay based on node sequence to prevent ACK collisions */
                    /* Use staggered timing: Node 2=800us, Node 3=1600us, Node 10=8000us */
                    uint32_t ack_delay_us = MY_NODE_SEQ * ACK_TX_INTERVAL_US;
                    if (ack_delay_us > 0) {
                        static char delay_msg[100];
                        snprintf(delay_msg, sizeof(delay_msg), "ACK delay: %u us for node %d", ack_delay_us, MY_NODE_SEQ);
                        test_run_info((unsigned char *)delay_msg);
                        nrf_delay_us(ack_delay_us);  // Wait for our turn to send ACK
                    }

                    tx_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK; // Frame type for ACK
#if TEST_MODE
                    tx_msg[IDX_SOURCE] = TEST_MY_NODE_ID;        // This node (from TEST_MODE)
#else
                    tx_msg[IDX_SOURCE] = NODE_8;                 // This node (default)
#endif
                    tx_msg[IDX_DEST] = rx_buffer[IDX_SOURCE]; // Reply to sender

                    /* Write response frame data to DW IC and prepare transmission. */
                    dwt_writetxdata(sizeof(tx_msg), tx_msg, 0); /* Zero offset in TX buffer. */
                    dwt_writetxfctrl(sizeof(tx_msg), 0, 0);     /* Zero offset in TX buffer, no ranging. */

                    /* Send the response with immediate RX return */
                    dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                    /* Poll DW IC until TX frame sent event set. */
                    waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                    /* Clear TX frame sent event. */
                    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                    /* Increment the data frame sequence number (modulo 256). */
                    tx_msg[DATA_FRAME_SN_IDX]++;

                    test_run_info((unsigned char *)"ACK sent successfully");
                } else {
                    static char data_no_ack_msg[100];
                    snprintf(data_no_ack_msg, sizeof(data_no_ack_msg),
                            "DATA received from src=%c during own slot - no ACK sent",
                            rx_buffer[IDX_SOURCE]);
                    test_run_info((unsigned char *)data_no_ack_msg);
                }
            }

            /* Clear RX OK event flag and re-enable RX */
            rx_event_flags &= ~RX_EVENT_OK_BIT;
            /* Safely start RX for next ACK */
            dwt_forcetrxoff();                      // Ensure clean state
            dwt_writesysstatuslo(0xFFFFFFFF); 
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
        else if (rx_event_flags & RX_EVENT_TO_BIT) {  // RX timeout
            /* Clear timeout event flag */
            rx_event_flags &= ~RX_EVENT_TO_BIT;

            /* Skip timeout handling during ACK collection since we use time-based ending */
            if (!collecting_acks) {
                /* Normal timeout recovery only when not collecting ACKs */
                dwt_forcetrxoff();                      // Ensure clean state
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
            /* During ACK collection, ignore timeout events - use only time-based ending */
        }
        else if (rx_event_flags & RX_EVENT_ERR_BIT) {  // RX error
            /* Clear error event flag and re-enable RX */
            rx_event_flags &= ~RX_EVENT_ERR_BIT;
            /* Safely start RX for next ACK */
            dwt_forcetrxoff();                      // Ensure clean state
            dwt_writesysstatuslo(0xFFFFFFFF); 
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }

        /* Small delay to prevent overwhelming the processor - reduced for ms timing */
        nrf_delay_us(10); // 10 microseconds delay
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
