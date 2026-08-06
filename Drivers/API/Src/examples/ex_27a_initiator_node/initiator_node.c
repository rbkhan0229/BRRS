/*! ----------------------------------------------------------------------------
 *  @file    tx_wait_resp.c
 *  @brief   TX then wait for multiple responses example code (modified)
 *
 *           이 버전은 프레임을 송신한 뒤, 하나의 응답만 기다리는 대신
 *           while 루프를 돌면서 여러 응답(최대 MAX_RESPONSES)을 수신할 수 있도록 수정한 예제입니다.
 *
 * @author  Decawave + Modified
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

#if defined(TEST_INITIATOR_NODE)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "INITIATOR NODE v1.0"


/* ========== TDMA Protocol Parameters ========== */
#

#define TOTAL_NODES         10      // Total nodes in network
#define PERIODS_PER_CYCLE   3       // 3 periods per cycle for 100ms message cycle
#define MY_NODE_SEQ         1       // This node's number - slot 1 (first after SYNC)


/* ========== ACK Reception Parameters ========== */
#define ACK_SLOT_DURATION_US  450   // 450us per ACK slot
#define ACK_TX_INTERVAL_US    800   // 800us interval between ACK transmissions (increased for reliability)

/* Default communication configuration - MATCH rx_send_resp.c */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_64,     /* Preamble length - SAME AS WORKING EXAMPLE */
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
#define NODE_INIT '1'    // Initiator node (this node) - slot 0
#define NODE_FL   '2'    // Front Left (needs relay)
#define NODE_FR   '3'    // Front Right (needs relay)
#define NODE_4    '4'    // Normal nodes
#define NODE_5    '5'
#define NODE_6    '6'
#define NODE_7    '7'
#define NODE_8    '8'
#define NODE_9    '9'
#define NODE_10   'A'
#define NODE_ALL  'B'    // Broadcast to all nodes

/* ========== TEST MODE Configuration ========== */
#define TEST_MODE 1  // Enable test mode with 4 nodes only

#if TEST_MODE
#define TEST_TOTAL_NODES 4     // Only 4 nodes in test: INIT(0), NODE_2, NODE_8, NODE_4
#define TEST_EXPECTED_ACKS 3   // Expect ACKs from 3 other nodes (exclude self)
#endif

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

/* Frame structure matching normal node */
static uint8_t tx_msg[] = { 0x41, 0x8C, 0, 0x9A, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 'D', 'W', 0x10, 0x00, 0, 0, 0, 0 };
#define BLINK_FRAME_SN_IDX 1
/* Indexes to access to sequence number and destination address of the data frame in the tx_msg array. */
#define DATA_FRAME_SN_IDX   2
#define DATA_FRAME_DEST_IDX 5

/* Configs */
#define TX_DELAY_MS       1000
#define TX_TO_RX_DELAY_UUS 60
#define RX_RESP_TO_UUS    5000

/* 수신 버퍼 */
static uint8_t rx_buffer[FRAME_LEN_MAX];

// Message queues
static message_queue_t retrans_queue = {0};
static message_queue_t relay_queue = {0};

// ACK tracking
static uint8_t ack_status[TOTAL_NODES] = {0};

/* RF TX config */
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

/* Cycle and Period management for retransmission */
static uint32_t current_cycle = 1;  // Start from cycle 1 for 10000 cycles
static uint8_t period_in_cycle = 1;  // 1-5

/* Long-term statistics for 10000 cycles */
static uint32_t total_cycles = 0;
static uint32_t successful_cycles = 0;
static uint32_t first_tx_success = 0;    // Cycles successful in Period 1
static uint32_t retrans_success = 0;     // Cycles successful in Period 2 or 3
static bool cycle_completed = false;     // Track if current cycle finished
#if TEST_MODE
static uint8_t expected_nodes = TEST_EXPECTED_ACKS;  // Test mode: expect 2 ACKs (from NODE_2 and NODE_8)
#else
static uint8_t expected_nodes = TOTAL_NODES - 1;     // Normal mode: expect all normal nodes should ACK
#endif

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

/* Helper function to convert node ID character to array index */
static uint8_t node_id_to_index(uint8_t node_id) {
    switch(node_id) {
        case NODE_INIT: return 0;  // '0' -> index 0
        case NODE_10:   return 1;  // '1' -> index 1
        case NODE_FL:   return 2;  // '2' -> index 2
        case NODE_FR:   return 3;  // '3' -> index 3
        case NODE_4:    return 4;  // '4' -> index 4
        case NODE_5:    return 5;  // '5' -> index 5
        case NODE_6:    return 6;  // '6' -> index 6
        case NODE_7:    return 7;  // '7' -> index 7
        case NODE_8:    return 8;  // '8' -> index 8
        case NODE_9:    return 9;  // '9' -> index 9
        default:        return 0xFF;  // Invalid node ID
    }
}

/* Helper function to convert array index to node ID character */
static uint8_t index_to_node_id(uint8_t index) {
    switch(index) {
        case 0:  return NODE_INIT;  // index 0 -> '0'
        case 1:  return NODE_10;    // index 1 -> '1'
        case 2:  return NODE_FL;    // index 2 -> '2'
        case 3:  return NODE_FR;    // index 3 -> '3'
        case 4:  return NODE_4;     // index 4 -> '4'
        case 5:  return NODE_5;     // index 5 -> '5'
        case 6:  return NODE_6;     // index 6 -> '6'
        case 7:  return NODE_7;     // index 7 -> '7'
        case 8:  return NODE_8;     // index 8 -> '8'
        case 9:  return NODE_9;     // index 9 -> '9'
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

int initiator_node(void)
{
    uint32_t status_reg = 0;
    uint16_t frame_len = 0;

    test_run_info((unsigned char *)APP_NAME);

    port_set_dw_ic_spi_fastrate();

    reset_DWIC();
    Sleep(2);

    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    while (!dwt_checkidlerc()) { };

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        test_run_info((unsigned char *)"INIT FAILED     ");
        while (1) { };
    }

    /* Enabling LEDs here for debug - SAME AS WORKING EXAMPLE */
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

    if (dwt_configure(&config))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    dwt_configuretxrf(&txconfig_options);

    dwt_setrxaftertxdelay(TX_TO_RX_DELAY_UUS);
    dwt_setrxtimeout(RX_RESP_TO_UUS);

    /* Setup interrupt callbacks - same as normal node */
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

    test_run_info((unsigned char *)"Initiator Node initialized with RX interrupts");

    /* Print current configuration for debugging */
    static char config_debug[200];
    snprintf(config_debug, sizeof(config_debug),
             "INIT CONFIG: Ch=%d, PLen=%d, TxCode=%d, RxCode=%d, Rate=%d",
             config.chan, config.txPreambLength, config.txCode, config.rxCode, config.dataRate);
    test_run_info((unsigned char *)config_debug);
    
    //MARK: 타이머 Configuration
    /* Non-blocking loop with periodic SYNC and real-time RX using UWB timestamps */
    int period_count = 0;
    uint8_t dummy_time[5]; // For clearing latch
    dwt_readsystime(dummy_time); // Clear latch first
    uint32_t last_sync_time = 0;                         // When last SYNC was completed (like normal node)
    uint32_t period_interval = us_to_uwb_ticks(73000);     // 73ms period interval (allows all 10 nodes with 3ms slot + 4ms guard)
    uint32_t slot_interval = us_to_uwb_ticks(7000);      // 7ms (SEQ 1 * 7ms) - wait for my turn with 3ms slot + 4ms guard
    uint32_t slot_duration = us_to_uwb_ticks(3000);       // 3ms slot duration for TX + ACK collection (early termination when successful)
    bool slot_active = false;                               // Track if currently in own slot
    bool slot_executed_this_sync = false;                // Flag to ensure slot executes only once per SYNC
    bool is_period_expired = false;                        // Flag to reduce SPI frequency

    /* Debug: Print calculated intervals */
    static char interval_debug[150];
    snprintf(interval_debug, sizeof(interval_debug),
             "Intervals: period=%u ticks (73ms), slot=%u ticks (35ms) [3ms+4ms guard]",
             period_interval, slot_interval);
    test_run_info((unsigned char *)interval_debug);

    /* Enable RX immediately for continuous listening */
    test_run_info((unsigned char *)"Starting non-blocking loop: 73ms period + 35ms slot + 3ms+4ms guard + real-time RX");

    while (1)
    {
        /* 1. Check if it's time to send SYNC (Period Timer) - same pattern as normal node */
        //MARK: 새로운 period: SYNC 전송

        /* Only check period timing periodically to avoid SPI lock - similar to normal node */
        static uint32_t period_check_counter = 0;
        period_check_counter++;

        if (last_sync_time == 0 || (period_check_counter % 1000 == 0)) {  // Check every 10 loops or first time
            /* Critical section for timestamp access - only when checking period timing */
            decaIrqStatus_t irq_state = decamutexon();

            /* Clear timestamp latch and get current time */
            dwt_writesysstatuslo(0x00000000);
            dwt_readsystime(dummy_time);
            uint32_t current_time = dwt_readsystimestamphi32();

            decamutexoff(irq_state);

            if (last_sync_time == 0 || current_time - last_sync_time >= period_interval) {
                period_count++;

                /* Update cycle and period tracking */
                if ((period_count - 1) % PERIODS_PER_CYCLE == 0) {
                    // New cycle started
                    if (period_count > 1) {
                        current_cycle++;
                    }
                    period_in_cycle = 1;

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

                    static char cycle_msg[150];
                    snprintf(cycle_msg, sizeof(cycle_msg),
                            "NEW CYCLE %d started - Period %d (global period %d) - TX state reset",
                            current_cycle, period_in_cycle, period_count);
                    test_run_info((unsigned char *)cycle_msg);
                } else {
                    period_in_cycle = ((period_count - 1) % PERIODS_PER_CYCLE) + 1;

                    static char period_msg[100];
                    snprintf(period_msg, sizeof(period_msg), "Cycle %d - Period %d (global period %d)",
                            current_cycle, period_in_cycle, period_count);
                    test_run_info((unsigned char *)period_msg);
                }

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

                /* IMMEDIATELY reset slot flag when new period starts - before any TX operations */
                slot_executed_this_sync = false;

                /* Disable RX temporarily for TX */
                dwt_forcetrxoff();

                /* Debug: Show period interval timing */
                uint32_t elapsed_since_last = (last_sync_time != 0) ? (current_time - last_sync_time) : 0;
                char info_str[120];
                snprintf(info_str, sizeof(info_str), "Period %d start (interval: %u ticks, expected: %u) - slot flag RESET",
                         period_count, elapsed_since_last, period_interval);
                test_run_info((unsigned char *)info_str);

                /* Prepare SYNC frame */
                tx_msg[0] = 0xC5;
                tx_msg[IDX_MSG_TYPE] = MSG_TYPE_SYNC;

                /* Print SYNC frame content */
                static char sync_debug[150];
                snprintf(sync_debug, sizeof(sync_debug), "SYNC TX: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                        tx_msg[0], tx_msg[1], tx_msg[2], tx_msg[3], tx_msg[4], tx_msg[5],
                        tx_msg[6], tx_msg[7], tx_msg[8], tx_msg[9], tx_msg[10], tx_msg[11]);
                test_run_info((unsigned char *)sync_debug);

                dwt_setrxaftertxdelay(0);
                dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
                dwt_writetxfctrl(sizeof(tx_msg), 0, 0);

                /* Send SYNC */
                int sync_result = dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                if (sync_result == DWT_SUCCESS) {
                    test_run_info((unsigned char *)"SYNC TX started successfully");

                    /* Wait for TX completion */
                    uint32_t tx_status = 0;
                    waitforsysstatus(&tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                    if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
                        test_run_info((unsigned char *)"SYNC TX completed successfully");
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                        /* Update last SYNC time to current system time - this is the baseline for slot timing */
                        decaIrqStatus_t irq_state = decamutexon();
                        dwt_readsystime(dummy_time); // Clear latch
                        last_sync_time = dwt_readsystimestamphi32();
                        decamutexoff(irq_state);

                        /* Reset slot flag - the new SYNC period starts now, slot should execute 5s later */
                        slot_executed_this_sync = false;
                        slot_active = false;
                    } else {
                        test_run_info((unsigned char *)"SYNC TX completion failed!");
                        /* Still update time to prevent rapid retries */
                        last_sync_time = current_time;
                    }
                } else {
                    test_run_info((unsigned char *)"SYNC TX failed to start");
                    /* Still update time to prevent rapid retries */
                    last_sync_time = current_time;
                }

                /* Increment sequence number */
                tx_msg[DATA_FRAME_SN_IDX]++;

                /* Safely re-enable RX for continuous listening */
                dwt_forcetrxoff();                          // Ensure RX is off first
                dwt_setrxtimeout(0);                        // Reset timeout
                dwt_writesysstatuslo(0xFFFFFFFF);           // Clear all status flags
                dwt_rxenable(DWT_START_RX_IMMEDIATE);       // Now safely enable RX
                continue;    
            }
        }

        /* 2. Check if it's time for slot interval action */
        //MARK: Time slot 시작!
        // Check if 5 seconds have passed since last SYNC completion AND slot hasn't been executed yet
        static uint32_t slot_check_counter = 0;
        if (last_sync_time != 0 && !slot_executed_this_sync) {
            slot_check_counter++;

            /* Only check slot timing every 10 loops for ms-scale timing */
            if (slot_check_counter % 10 == 0) {
                /* Critical section for timestamp access */
                decaIrqStatus_t irq_state = decamutexon();

                /* Clear timestamp latch and get current time */
                dwt_writesysstatuslo(0x00000000);
                dwt_readsystime(dummy_time);
                uint32_t current_time = dwt_readsystimestamphi32();

                decamutexoff(irq_state);

                uint32_t time_since_sync = current_time - last_sync_time;

                // Debug: Print slot timing info every 1000 loops
                // static uint32_t debug_counter = 0;
                // debug_counter++;
                // if (debug_counter % 10000 == 0) {
                //     static char slot_debug[250];
                //     snprintf(slot_debug, sizeof(slot_debug),
                //             "Slot check: executed=%s, sync_time=%u, current=%u, time_since=%u, slot_int=%u",
                //             slot_executed_this_sync ? "YES" : "NO", last_sync_time, current_time, time_since_sync, slot_interval);
                //     test_run_info((unsigned char *)slot_debug);
                // }

                // Improved slot condition with better overflow handling (normal node method)
                bool sync_valid = (last_sync_time != 0);
                bool time_advanced = (current_time != last_sync_time);
                bool slot_time_reached = (time_since_sync >= slot_interval);
                bool slot_not_executed = (!slot_executed_this_sync);

                // Check if slot interval has elapsed
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
                    tx_msg[0] = 0xC5;
                    tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
                    tx_msg[IDX_SOURCE] = NODE_INIT;
                    tx_msg[IDX_DEST] = NODE_ALL;  // Broadcast to all nodes
                    tx_msg[IDX_PRIORITY] = 1;     // Normal priority

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
                        tx_type_desc, tx_state, period_in_cycle, PERIODS_PER_CYCLE, retrans_attempt);
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

                            /* Calculate slot time for ACK collection */
                            dwt_writesysstatuslo(0x00000000);  // Clear latch
                            dwt_readsystime(dummy_time);
                            uint32_t slot_start = dwt_readsystimestamphi32();
                            uint32_t slot_end = slot_start + slot_duration;  // Use current time as base

                            /* Start interrupt-based ACK collection only if not already collecting */
                            if (!collecting_acks) {
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
                            } else {
                                test_run_info((unsigned char *)"ACK collection already in progress, skipping restart");
                            }

                            /* Note: ACK collection results will be reported by main loop */
                            /* Don't turn off RX - let it continue collecting ACKs */
                        
                        } else {
                            test_run_info((unsigned char *)"TX completed but status check failed");
                        }

                        /* Increment sequence number for next TX */
                        tx_msg[DATA_FRAME_SN_IDX]++;
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
        }




        /* Check if ACK collection has finished */
        static uint32_t ack_check_counter = 0;
        if (collecting_acks && ((ack_check_counter++ % 100) == 0)) {
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
                //uint8_t my_node_index = node_id_to_index(NODE_INIT);
                // 0 is init node index (fixed)
                int cumulative_acks = 0;
                for (int i = 0; i < TOTAL_NODES; i++) {
                    if (i != 0 && ack_received_from[i]) cumulative_acks++;
                }

                if (cumulative_acks >= expected_nodes) {
                    /* SUCCESS: All expected nodes responded (cumulative) */
                    tx_state = TX_STATE_IDLE;
                    has_pending_retrans = false;

                    /* Update success statistics */
                    if (!cycle_completed) {
                        total_cycles++;
                        successful_cycles++;
                        if (period_in_cycle == 1) {
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

                    static char success_msg[150];
                    snprintf(success_msg, sizeof(success_msg),
                            "TRANSMISSION SUCCESS: %d/%d nodes responded (cumulative) - No more TX needed in this cycle",
                            cumulative_acks, expected_nodes);
                    test_run_info((unsigned char *)success_msg);

                } else {
                    /* FAILURE: Need retransmission */
                    if (tx_state == TX_STATE_FIRST_TX) {
                        /* First transmission failed - prepare for retransmission */
                        tx_state = TX_STATE_RETRANS;
                        memcpy(retrans_msg, tx_msg, sizeof(tx_msg));
                        has_pending_retrans = true;
                        retrans_attempt = 1;

                        static char fail_msg[150];
                        snprintf(fail_msg, sizeof(fail_msg),
                                "FIRST TX FAILED: %d/%d nodes responded (cumulative) - Message stored for retransmission",
                                cumulative_acks, expected_nodes);
                        test_run_info((unsigned char *)fail_msg);

                    } else {
                        /* Retransmission also failed */
                        retrans_attempt++;

                        static char retry_msg[150];
                        snprintf(retry_msg, sizeof(retry_msg),
                                "RETRANS FAILED: %d/%d nodes responded (cumulative) - Attempt %d (will continue in cycle)",
                                cumulative_acks, expected_nodes, retrans_attempt);
                        test_run_info((unsigned char *)retry_msg);
                    }

                    /* List missing nodes for debugging */
                    static char missing_list[100];
                    int pos = snprintf(missing_list, sizeof(missing_list), "Missing ACKs from: ");
                    for (int i = 0; i < TOTAL_NODES && pos < sizeof(missing_list) - 10; i++) {
                        if (!ack_received_from[i] && i != 0) {  // Skip initiator node (index 0)
                            uint8_t node_char = index_to_node_id(i);
                            pos += snprintf(missing_list + pos, sizeof(missing_list) - pos, "%c ", node_char);
                        }
                    }
                    if (pos > strlen("Missing ACKs from: ")) {
                        test_run_info((unsigned char *)missing_list);
                    }
                }

                /* Critical section for RX configuration */
                decaIrqStatus_t irq_state2 = decamutexon();

                /* Reset timeout and re-enable normal RX */
                dwt_setrxtimeout(0);  // Disable timeout for normal operation
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);

                decamutexoff(irq_state2);
            }
            /* Add delay during ACK collection to reduce SPI conflicts */
            //nrf_delay_us(200);  // 200us delay to give time for ACK interrupts
        }

        /* 2. Check for RX events signaled by interrupt handler */
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
                            //uint8_t my_idx = node_id_to_index(MY_NODE_ID);
                            int cumulative_acks = 0;
                            for (int i = 0; i < TOTAL_NODES; i++) {
                                if (i != 0 && ack_received_from[i]) cumulative_acks++;
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
                    /* Log what type of non-ACK message was received */
                    static char non_ack_msg[100];
                    snprintf(non_ack_msg, sizeof(non_ack_msg),
                            "Non-ACK received during collection: type=0x%02X, src=0x%02X, len=%d",
                            rx_buffer[IDX_MSG_TYPE], rx_buffer[IDX_SOURCE], rx_frame_len);
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

            /* Normal RX processing */
            test_run_info((unsigned char *)"RX received !!");

            /* Print received frame */
            static char frame_debug[200];
            snprintf(frame_debug, sizeof(frame_debug),
                     "Frame RX: len=%d, src=%c, type=%02X, data: %02X %02X %02X %02X",
                     rx_frame_len,
                     (rx_frame_len > IDX_SOURCE) ? (char)rx_buffer[IDX_SOURCE] : '?',
                     (rx_frame_len > IDX_MSG_TYPE) ? rx_buffer[IDX_MSG_TYPE] : 0xFF,
                     rx_buffer[0], rx_buffer[1], rx_buffer[2], rx_buffer[3]);
            test_run_info((unsigned char *)frame_debug);

            if ((rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA || rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_URGENT || rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_RELAY_DATA))
            {
                /* Only send ACK if NOT in our own slot (prevents ACK during ACK collection) */
                if (!slot_active) {
                    test_run_info((unsigned char *)"DATA received, sending ACK");

                    /* Apply ACK transmission delay based on node sequence to prevent collisions */
                    uint32_t ack_delay_us = MY_NODE_SEQ * ACK_TX_INTERVAL_US;
                    if (ack_delay_us > 0) {
                        static char delay_msg[100];
                        snprintf(delay_msg, sizeof(delay_msg), "ACK delay: %u us for node %d", ack_delay_us, MY_NODE_SEQ);
                        test_run_info((unsigned char *)delay_msg);
                        nrf_delay_us(ack_delay_us);
                    }

                    tx_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK; // Frame type for ACK
                    tx_msg[IDX_SOURCE] = NODE_INIT;     // This node (initiator)
                    tx_msg[IDX_DEST] = rx_buffer[IDX_SOURCE]; // Reply to sender

                    /* Critical section for ACK transmission */
                    decaIrqStatus_t irq_state = decamutexon();

                    /* Write response frame data to DW IC and prepare transmission. */
                    dwt_writetxdata(sizeof(tx_msg), tx_msg, 0); /* Zero offset in TX buffer. */
                    dwt_writetxfctrl(sizeof(tx_msg), 0, 0);     /* Zero offset in TX buffer, no ranging. */

                    /* Send the response with immediate RX return */
                    dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                    decamutexoff(irq_state);

                    /* Poll DW IC until TX frame sent event set. */
                    waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                    /* Clear TX frame sent event. */
                    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                    /* Increment the data frame sequence number (modulo 256). */
                    tx_msg[DATA_FRAME_SN_IDX]++;

                    test_run_info((unsigned char *)"ACK sent successfully");
                } else {
                    test_run_info((unsigned char *)"DATA received during own slot - no ACK sent");
                }
            }

            /* Clear RX OK event flag and re-enable RX */
            rx_event_flags &= ~RX_EVENT_OK_BIT;

            /* Critical section for RX re-enable */
            decaIrqStatus_t irq_state = decamutexon();
            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
            decamutexoff(irq_state);
        }

        /* Handle RX timeout */
        if (rx_event_flags & RX_EVENT_TO_BIT) {
            /* Clear timeout event flag */
            rx_event_flags &= ~RX_EVENT_TO_BIT;

            /* Skip timeout handling during ACK collection since we use time-based ending */
            if (!collecting_acks) {
                /* Normal timeout recovery only when not collecting ACKs */
                decaIrqStatus_t irq_state = decamutexon();
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
                decamutexoff(irq_state);
            }
            /* During ACK collection, ignore timeout events - use only time-based ending */
        }

        /* Handle RX errors */
        if (rx_event_flags & RX_EVENT_ERR_BIT) {
            /* Clear error event flag and re-enable RX */
            rx_event_flags &= ~RX_EVENT_ERR_BIT;

            /* Critical section for RX recovery */
            decaIrqStatus_t irq_state = decamutexon();
            //dwt_writesysstatuslo(DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXFCE_BIT_MASK | DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXSTO_BIT_MASK);
            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
            decamutexoff(irq_state);
        }

        /* Small delay to prevent overwhelming the processor */
        nrf_delay_us(100); // 100 microseconds delay
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
    /* Set error bit without clearing other events */
    rx_event_flags |= RX_EVENT_ERR_BIT;
}

#endif
