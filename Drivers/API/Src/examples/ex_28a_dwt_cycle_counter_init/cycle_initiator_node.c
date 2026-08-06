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

#if defined(TEST_DWT_CYCLE_COUNTER_INIT)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "INITIATOR NODE v1.0"


/* ========== TDMA Protocol Parameters ========== */
#

#define TOTAL_NODES         10      // Total physical nodes in network
#define TOTAL_SLOTS          12      // Total TDMA slots (includes relay slots)
#define PERIODS_PER_CYCLE   3       // 3 periods per cycle for 100ms message cycle
#define MY_NODE_SEQ         1       // This node's number - slot 1 (first after SYNC)


/* ========== ACK Reception Parameters ========== */
//MARK: ACK INTERVAL값
#define ACK_TX_INTERVAL_US    600   // 500us interval between ACK transmissions (increased for reliability)

/* Default communication configuration for DATA/ACK - PLEN64 */
static dwt_config_t config_data = {
    5,                /* Channel number. */
    DWT_PLEN_64,     /* Preamble length - Fast for DATA/ACK */
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

/* ========== TEST MODE Configuration ========== */
#define TEST_MODE 1  // Enable test mode with 4 nodes only

#if TEST_MODE
#define TEST_TOTAL_NODES 5     // Only 5 nodes in test: INIT, NODE_4, NODE_5, NODE_FL, NODE_FR
#define TEST_EXPECTED_ACKS 4   // Expect ACKs from 4 other nodes for own data (exclude self)
#define TEST_RELAY_EXPECTED_ACKS 2  // For relay slots: only NODE_4 and NODE_5 respond (FL/FR don't ACK relayed messages)
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

/* INIT Node has 3 transmission slots: SEQ 1 (own), SEQ 10 (FL relay), SEQ 12 (FR relay) */
typedef enum {
    SLOT_TYPE_OWN = 1,      // SEQ 1 - Own data transmission
    SLOT_TYPE_FL_RELAY = 10, // SEQ 10 - FL relay transmission
    SLOT_TYPE_FR_RELAY = 12  // SEQ 12 - FR relay transmission
} init_slot_type_t;

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
static volatile init_slot_type_t current_collection_slot = 0; // Which slot is currently collecting ACKs

/* ACK source tracking for current collection period */
static uint8_t ack_received_from[TOTAL_NODES] = {0};    // Track which nodes sent ACKs this period (for own data)
static uint8_t unique_ack_count = 0;                    // Count of unique ACK sources (for own data)

/* Separate ACK tracking for FL and FR relay transmissions */
static uint8_t fl_relay_ack_received_from[TOTAL_NODES] = {0};  // Track ACKs for FL relay
static uint8_t fr_relay_ack_received_from[TOTAL_NODES] = {0};  // Track ACKs for FR relay
static uint8_t fl_relay_unique_ack_count = 0;                  // Count of unique FL relay ACKs
static uint8_t fr_relay_unique_ack_count = 0;                  // Count of unique FR relay ACKs

/* Cycle and Period management for retransmission */
static uint32_t current_cycle = 1;  // Start from cycle 1 for 10000 cycles
static uint8_t period_in_cycle = 1;  // 1-5

/* Long-term statistics for 10000 cycles */
static uint32_t total_cycles = 0;
static uint32_t successful_cycles = 0;
static uint32_t first_tx_success = 0;    // Cycles successful in Period 1
static uint32_t retrans_success = 0;     // Cycles successful in Period 2 or 3
static bool cycle_completed = false;     // Track if current cycle finished

/* SEQ 1 (Own Data) specific state tracking */
static bool own_data_success_in_period_1 = false;
static bool own_data_success_in_period_2 = false;
static bool own_data_success_in_period_3 = false;
static bool own_data_cycle_evaluated = false;    // Prevent double evaluation

/* Period-specific retransmission success tracking */
static uint32_t period_2_retrans_success = 0;    // Successful retransmissions in Period 2
static uint32_t period_3_retrans_success = 0;    // Successful retransmissions in Period 3

/* Relay statistics */
static uint32_t fl_relay_attempts = 0;   // Number of FL relay transmission attempts
static uint32_t fr_relay_attempts = 0;   // Number of FR relay transmission attempts
static uint32_t fl_relay_success = 0;    // Number of successful FL relay transmissions
static uint32_t fr_relay_success = 0;    // Number of successful FR relay transmissions
#if TEST_MODE
static uint8_t expected_nodes = TEST_EXPECTED_ACKS;  // Test mode: dynamically set based on slot type (own data vs relay)
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
static uint8_t retrans_msg[FRAME_LEN_MAX];                 // Store message for retransmission (SEQ 1)
static bool has_pending_retrans = false;                   // Flag for pending retransmission
static uint8_t retrans_attempt = 0;                        // Number of retransmission attempts in current cycle

/* FL/FR Relay system */
static uint8_t fl_relay_msg[FRAME_LEN_MAX];                // Store FL message for relay
static uint8_t fr_relay_msg[FRAME_LEN_MAX];                // Store FR message for relay
static uint8_t fl_retrans_msg[FRAME_LEN_MAX];              // FL relay retransmission buffer
static uint8_t fr_retrans_msg[FRAME_LEN_MAX];              // FR relay retransmission buffer
static bool has_fl_data = false;                           // Flag for FL data availability
static bool has_fr_data = false;                           // Flag for FR data availability
static bool fl_pending_retrans = false;                    // FL relay retransmission flag
static bool fr_pending_retrans = false;                    // FR relay retransmission flag
static bool fl_relay_completed = false;                    // FL relay completed successfully this cycle
static bool fr_relay_completed = false;                    // FR relay completed successfully this cycle

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

int dwt_cycle_counter_init(void)
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

    /* Start with SYNC config since we begin with SYNC transmission */
    if (dwt_configure(&config_sync))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    dwt_configuretxrf(&txconfig_options);

    dwt_setrxaftertxdelay(TX_TO_RX_DELAY_UUS);
    dwt_setrxtimeout(RX_RESP_TO_UUS);

    /* POLLING MODE - No interrupt callbacks */
    /* Disable all interrupts to prevent SPI lock issues */
    dwt_setinterrupt(0, 0, DWT_ENABLE_INT);  // Disable all interrupts

    /* Clear any pending interrupts */
    dwt_writesysstatuslo(0xFFFFFFFF);  // Clear all status flags

    /* Do NOT install IRQ handler - using polling instead */
    // port_set_dwic_isr(dwt_isr);  // DISABLED for polling mode

    test_run_info((unsigned char *)"Initiator Node initialized in POLLING MODE (no interrupts)");

    /* Print current configuration for debugging */
    static char config_debug[200];
    snprintf(config_debug, sizeof(config_debug),
             "INIT CONFIG: SYNC(512 symbols) DATA(64 symbols) Ch=%d, TxCode=%d, RxCode=%d",
             config_sync.chan, config_sync.txCode, config_sync.rxCode);
    test_run_info((unsigned char *)config_debug);
    
    //MARK: DWT Cycle Counter Timer Configuration
    /* Initialize DWT cycle counter for SPI-free timing */
    dwt_timer_init();

    /* Non-blocking loop with periodic SYNC and real-time RX using CPU cycle timing */
    int period_count = 0;
    uint32_t last_sync_cycles = 0;                         // When last SYNC was completed (CPU cycles)
    uint32_t period_interval_cycles = us_to_cpu_cycles(140000);    // 140ms period interval
    uint32_t slot_interval_cycles = us_to_cpu_cycles(10000);       // 10ms slot timing (6ms duration + 4ms guard)
    uint32_t slot_duration_cycles = us_to_cpu_cycles(6000);        // 6ms slot duration
    /* INIT node has 3 independent slots with separate execution flags */
    bool seq1_executed = false;     // SEQ 1 (own data) execution flag
    bool seq10_executed = false;    // SEQ 10 (FL relay) execution flag
    bool seq12_executed = false;    // SEQ 12 (FR relay) execution flag

    /* Independent slot timers for SEQ 10 and SEQ 12 */
    uint32_t seq10_start_cycles = us_to_cpu_cycles(100000); // SEQ 10: 100ms
    uint32_t seq12_start_cycles = us_to_cpu_cycles(120000); // SEQ 12: 120ms

    bool slot_active = false;                               // Track if currently in any slot
    bool is_period_expired = false;                        // Flag to reduce timing checks

    /* Debug: Print calculated intervals */
    static char interval_debug[150];
    snprintf(interval_debug, sizeof(interval_debug),
             "DWT Intervals: period=%u cycles (140ms), slot=%u cycles (10ms), slot_duration=%u cycles (6ms)",
             period_interval_cycles, slot_interval_cycles, slot_duration_cycles);
    test_run_info((unsigned char *)interval_debug);

    /* Enable RX immediately for continuous listening */
    test_run_info((unsigned char *)"Starting DWT cycle counter loop: 140ms period + 12 slots + 6ms duration + 4ms guard + relay system");

    /* Initial RX enable - no critical section needed in polling mode */
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    Sleep(60000);

    while (1)
    {
        /* 1. Check if it's time to send SYNC (Period Timer) - same pattern as normal node */
        //MARK: 새로운 period: SYNC 전송

        /* Check if it's time for new period */
        if (last_sync_cycles == 0 || dwt_timer_elapsed(last_sync_cycles, period_interval_cycles)) {
            /* Get current CPU cycle count - no SPI access needed */
            uint32_t current_cycles = dwt_timer_get_cycles();

            if (last_sync_cycles == 0 || dwt_timer_elapsed(last_sync_cycles, period_interval_cycles)) {
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

                    /* Reset relay ACK tracking arrays */
                    memset(fl_relay_ack_received_from, 0, sizeof(fl_relay_ack_received_from));
                    memset(fr_relay_ack_received_from, 0, sizeof(fr_relay_ack_received_from));
                    fl_relay_unique_ack_count = 0;
                    fr_relay_unique_ack_count = 0;

                    /* Reset relay completion flags for new cycle */
                    fl_relay_completed = false;
                    fr_relay_completed = false;

                    /* Evaluate SEQ 1 (own data) success for previous cycle */
                    if (current_cycle > 1 && !own_data_cycle_evaluated) {
                        /* Check if own data succeeded in any period */
                        bool own_data_succeeded = own_data_success_in_period_1 ||
                                                  own_data_success_in_period_2 ||
                                                  own_data_success_in_period_3;

                        /* Update statistics based on SEQ 1 success */
                        total_cycles++;
                        if (own_data_succeeded) {
                            successful_cycles++;

                            /* Determine which period succeeded first */
                            if (own_data_success_in_period_1) {
                                first_tx_success++;
                            } else if (own_data_success_in_period_2) {
                                retrans_success++;
                                period_2_retrans_success++;
                            } else if (own_data_success_in_period_3) {
                                retrans_success++;
                                period_3_retrans_success++;
                            }
                        }

                        /* Mark cycle as evaluated */
                        own_data_cycle_evaluated = true;
                    }

                    /* Reset SEQ 1 flags for new cycle */
                    own_data_success_in_period_1 = false;
                    own_data_success_in_period_2 = false;
                    own_data_success_in_period_3 = false;
                    own_data_cycle_evaluated = false;

                    cycle_completed = false;  // Reset for new cycle (for relay slots)

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

                /* Check if we've completed 1000 cycles and exit */
                //MARK: 종료 조건
                if (current_cycle > 1000) {
                    /* Print final statistics including relay performance */
                    static char final_stats[800];
                    float success_rate = (total_cycles > 0) ? (float)successful_cycles / total_cycles * 100 : 0;
                    float fl_relay_rate = (fl_relay_attempts > 0) ? (float)fl_relay_success / fl_relay_attempts * 100 : 0;
                    float fr_relay_rate = (fr_relay_attempts > 0) ? (float)fr_relay_success / fr_relay_attempts * 100 : 0;

                    snprintf(final_stats, sizeof(final_stats),
                            "\n=== FINAL STATISTICS (1000 cycles) ===\n"
                            "=== MAIN DATA TRANSMISSION (SEQ 1) ===\n"
                            "Total Cycles: %d\n"
                            "Successful Cycles: %d\n"
                            "Success Rate: %.2f%%\n"
                            "First TX Success (Period 1): %d\n"
                            "Period 2 Retrans Success: %d\n"
                            "Period 3 Retrans Success: %d\n"
                            "Total Retrans Success: %d\n"
                            "Failed Cycles: %d\n"
                            "\n=== RELAY STATISTICS ===\n"
                            "FL Relay Attempts: %d\n"
                            "FL Relay Success: %d\n"
                            "FL Relay Rate: %.2f%%\n"
                            "FR Relay Attempts: %d\n"
                            "FR Relay Success: %d\n"
                            "FR Relay Rate: %.2f%%\n"
                            "Total Relay Attempts: %d\n"
                            "Total Relay Success: %d\n"
                            "Overall Relay Rate: %.2f%%",
                            total_cycles, successful_cycles, success_rate,
                            first_tx_success, period_2_retrans_success, period_3_retrans_success,
                            retrans_success, total_cycles - successful_cycles,
                            fl_relay_attempts, fl_relay_success, fl_relay_rate,
                            fr_relay_attempts, fr_relay_success, fr_relay_rate,
                            fl_relay_attempts + fr_relay_attempts, fl_relay_success + fr_relay_success,
                            ((fl_relay_attempts + fr_relay_attempts) > 0) ?
                            (float)(fl_relay_success + fr_relay_success) / (fl_relay_attempts + fr_relay_attempts) * 100 : 0);
                    test_run_info((unsigned char *)final_stats);
                    break;
                }

                /* IMMEDIATELY reset all slot flags when new period starts - before any TX operations */
                seq1_executed = false;
                seq10_executed = false;
                seq12_executed = false;

                /* Switch to SYNC config before SYNC TX */
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    test_run_info((unsigned char *)"Switched to SYNC config (PLEN512) for SYNC TX");
                } else {
                    test_run_info((unsigned char *)"WARNING: Failed to switch to SYNC config!");
                }

                /* Debug: Show period interval timing */
                uint32_t elapsed_since_last = (last_sync_cycles != 0) ? (current_cycles - last_sync_cycles) : 0;
                char info_str[120];
                snprintf(info_str, sizeof(info_str), "Period %d start (interval: %u cycles, expected: %u) - slot flag RESET",
                         period_count, elapsed_since_last, period_interval_cycles);
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

                /* Send SYNC - no critical section needed in polling mode */
                int sync_result = dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                if (sync_result == DWT_SUCCESS) {
                    test_run_info((unsigned char *)"SYNC TX started successfully");

                    /* Wait for TX completion */
                    uint32_t tx_status = 0;
                    waitforsysstatus(&tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                    if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
                        test_run_info((unsigned char *)"SYNC TX completed successfully");
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                        /* Switch to DATA config after SYNC TX for DATA/ACK operations */
                        
                        if (dwt_configure(&config_data) == DWT_SUCCESS) {
                            test_run_info((unsigned char *)"Switched to DATA config (PLEN64) for normal operations");
                        } else {
                            test_run_info((unsigned char *)"WARNING: Failed to switch to DATA config!");
                        }

                        /* Update last SYNC time to current CPU cycles - baseline for slot timing */
                        last_sync_cycles = dwt_timer_get_cycles();

                        /* Reset all slot flags - the new SYNC period starts now */
                        seq1_executed = false;
                        seq10_executed = false;
                        seq12_executed = false;
                        slot_active = false;
                    } else {
                        test_run_info((unsigned char *)"SYNC TX completion failed!");
                        /* Still update time to prevent rapid retries */
                        last_sync_cycles = current_cycles;
                    }
                } else {
                    test_run_info((unsigned char *)"SYNC TX failed to start");
                    /* Still update time to prevent rapid retries */
                    last_sync_cycles = current_cycles;
                }

                /* Increment sequence number */
                tx_msg[DATA_FRAME_SN_IDX]++;

                /* Re-enable RX for continuous listening */
                dwt_forcetrxoff();                          // Ensure RX is off first
                dwt_setrxtimeout(0);                        // Reset timeout
                dwt_writesysstatuslo(0xFFFFFFFF);           // Clear all status flags
                dwt_rxenable(DWT_START_RX_IMMEDIATE);       // Now enable RX
                continue;    
            }
        }

        /* 2. Check for INIT node's 3 slot handling: SEQ 1 (own), SEQ 10 (FL relay), SEQ 12 (FR relay) */
        //MARK: INIT 3-slot handling: SEQ 1, 10, 12 with independent timers
        if (last_sync_cycles != 0) {
            /* Get current CPU cycle count - no SPI access needed */
            uint32_t current_cycles = dwt_timer_get_cycles();
            uint32_t cycles_since_sync = current_cycles - last_sync_cycles;

            init_slot_type_t active_slot_type = 0;
            bool should_execute_slot = false;

            /* Check SEQ 1 (0-10ms): Own data slot */
            if (!seq1_executed && dwt_timer_elapsed(last_sync_cycles, slot_interval_cycles)) {
                active_slot_type = SLOT_TYPE_OWN;
                seq1_executed = true;
                should_execute_slot = true;

                static char slot_debug[200];
                snprintf(slot_debug, sizeof(slot_debug),
                        "SEQ 1 (OWN) start! cycles_since_sync=%u, elapsed=%ums",
                        cycles_since_sync, cycles_since_sync / (CPU_FREQ_HZ/1000));
                test_run_info((unsigned char *)slot_debug);
            }
            /* Check SEQ 10 (100ms): FL relay slot */
            else if (!seq10_executed && has_fl_data && !fl_relay_completed && dwt_timer_elapsed(last_sync_cycles, seq10_start_cycles)) {
                active_slot_type = SLOT_TYPE_FL_RELAY;
                seq10_executed = true;
                should_execute_slot = true;

                static char slot_debug[200];
                snprintf(slot_debug, sizeof(slot_debug),
                        "SEQ 10 (FL_RELAY) start! cycles_since_sync=%u, elapsed=%ums, has_fl_data=%d",
                        cycles_since_sync, cycles_since_sync / (CPU_FREQ_HZ/1000), has_fl_data);
                test_run_info((unsigned char *)slot_debug);
            }
            /* Check SEQ 12 (120ms): FR relay slot */
            else if (!seq12_executed && has_fr_data && !fr_relay_completed && dwt_timer_elapsed(last_sync_cycles, seq12_start_cycles)) {
                active_slot_type = SLOT_TYPE_FR_RELAY;
                seq12_executed = true;
                should_execute_slot = true;

                static char slot_debug[200];
                snprintf(slot_debug, sizeof(slot_debug),
                        "SEQ 12 (FR_RELAY) start! cycles_since_sync=%u, elapsed=%ums, has_fr_data=%d",
                        cycles_since_sync, cycles_since_sync / (CPU_FREQ_HZ/1000), has_fr_data);
                test_run_info((unsigned char *)slot_debug);
            }

            if (should_execute_slot) {
                /* Debug: Show which slot is starting */
                slot_active = true;  // Mark slot as active for ACK rejection logic

                /* Decide what to transmit based on active slot type */
                bool should_transmit = false;
                const char* tx_type_desc = "";

                if (active_slot_type == SLOT_TYPE_OWN) {
                    /* SEQ 1 - Own data transmission */
                    if (tx_state == TX_STATE_FIRST_TX) {
                        /* First transmission of new data */
                        should_transmit = true;
                        tx_type_desc = "OWN FIRST TX";

                        /* Prepare new DATA frame */
                        tx_msg[0] = 0xC5;
                        tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
                        tx_msg[IDX_SOURCE] = NODE_INIT;
                        tx_msg[IDX_DEST] = NODE_ALL;  // Broadcast to all nodes
                        tx_msg[IDX_PRIORITY] = 1;     // Normal priority
                        /* Clear relay fields for own data */
                        tx_msg[IDX_ORIG_SRC] = 0;
                        tx_msg[IDX_ORIG_DST] = 0;

                    } else if (tx_state == TX_STATE_RETRANS && has_pending_retrans) {
                        /* Retransmission of failed message */
                        should_transmit = true;
                        tx_type_desc = "OWN RETRANSMISSION";

                        /* Use stored retransmission message */
                        memcpy(tx_msg, retrans_msg, sizeof(tx_msg));

                    } else {
                        /* No transmission needed - already successful or idle */
                        should_transmit = false;
                        tx_type_desc = "OWN SKIP (already successful or idle)";
                    }

                } else if (active_slot_type == SLOT_TYPE_FL_RELAY) {
                    /* SEQ 10 - FL relay transmission */
                    if (has_fl_data && !fl_relay_completed) {
                        if (!fl_pending_retrans) {
                            /* First FL relay transmission */
                            should_transmit = true;
                            tx_type_desc = "FL RELAY FIRST TX";
                            memcpy(tx_msg, fl_relay_msg, sizeof(tx_msg));
                            fl_relay_attempts++;  // Count relay attempt
                        } else {
                            /* FL relay retransmission */
                            should_transmit = true;
                            tx_type_desc = "FL RELAY RETRANS";
                            memcpy(tx_msg, fl_retrans_msg, sizeof(tx_msg));
                            fl_relay_attempts++;  // Count retry attempt
                        }
                    } else if (fl_relay_completed) {
                        should_transmit = false;
                        tx_type_desc = "FL RELAY SKIP (already completed)";
                    } else {
                        should_transmit = false;
                        tx_type_desc = "FL RELAY SKIP (no FL data)";
                    }

                } else if (active_slot_type == SLOT_TYPE_FR_RELAY) {
                    /* SEQ 12 - FR relay transmission */
                    if (has_fr_data && !fr_relay_completed) {
                        if (!fr_pending_retrans) {
                            /* First FR relay transmission */
                            should_transmit = true;
                            tx_type_desc = "FR RELAY FIRST TX";
                            memcpy(tx_msg, fr_relay_msg, sizeof(tx_msg));
                            fr_relay_attempts++;  // Count relay attempt
                        } else {
                            /* FR relay retransmission */
                            should_transmit = true;
                            tx_type_desc = "FR RELAY RETRANS";
                            memcpy(tx_msg, fr_retrans_msg, sizeof(tx_msg));
                            fr_relay_attempts++;  // Count retry attempt
                        }
                    } else if (fr_relay_completed) {
                        should_transmit = false;
                        tx_type_desc = "FR RELAY SKIP (already completed)";
                    } else {
                        should_transmit = false;
                        tx_type_desc = "FR RELAY SKIP (no FR data)";
                    }
                } else {
                    /* Invalid slot type */
                    should_transmit = false;
                    tx_type_desc = "INVALID SLOT TYPE";
                }

                static char tx_decision_msg[200];
                snprintf(tx_decision_msg, sizeof(tx_decision_msg),
                        "TX Decision: %s - SlotType: %d, State: %d, Period: %d/%d, Attempt: %d",
                        tx_type_desc, active_slot_type, tx_state, period_in_cycle, PERIODS_PER_CYCLE, retrans_attempt);
                test_run_info((unsigned char *)tx_decision_msg);

                if (should_transmit) {
                    /* Disable RX temporarily for TX */
                    dwt_forcetrxoff();

                    /* Send frame */
                    dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
                    dwt_writetxfctrl(sizeof(tx_msg), 0, 0);

                    /* Send DATA - no critical section needed in polling mode */
                    int tx_result = dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                    if (tx_result == DWT_SUCCESS) {
                        /* Wait for TX completion */
                        uint32_t tx_status = 0;
                        waitforsysstatus(&tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                        if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
                            test_run_info((unsigned char *)"DATA TX completed, collecting ACKs...");
                            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                            /* Store message for potential retransmission based on slot type */
                            if (active_slot_type == SLOT_TYPE_OWN) {
                                /* Store for own data retransmission */
                                memcpy(retrans_msg, tx_msg, sizeof(tx_msg));
                            } else if (active_slot_type == SLOT_TYPE_FL_RELAY) {
                                /* Store for FL relay retransmission */
                                memcpy(fl_retrans_msg, tx_msg, sizeof(tx_msg));
                            } else if (active_slot_type == SLOT_TYPE_FR_RELAY) {
                                /* Store for FR relay retransmission */
                                memcpy(fr_retrans_msg, tx_msg, sizeof(tx_msg));
                            }

                            /* Calculate slot time for ACK collection using CPU cycles */
                            uint32_t slot_start_cycles = dwt_timer_get_cycles();

                            /* Start ACK collection only if not already collecting */
                            if (!collecting_acks) {
                                /* Initialize ACK collection state using CPU cycles */
                                collecting_acks = true;
                                collected_ack_count = 0;
                                current_collection_slot = active_slot_type;  // Remember which slot is collecting
                                /* Note: ack_received_from is NOT cleared - cumulative per cycle */
                                ack_collection_end_time = slot_start_cycles + slot_duration_cycles;  // Use CPU cycles

                                /* Set expected ACKs based on slot type */
#if TEST_MODE
                                if (active_slot_type == SLOT_TYPE_FL_RELAY || active_slot_type == SLOT_TYPE_FR_RELAY) {
                                    expected_nodes = TEST_RELAY_EXPECTED_ACKS;  // Relay slots: only normal nodes respond
                                } else {
                                    expected_nodes = TEST_EXPECTED_ACKS;  // Own data: all nodes respond
                                }
#else
                                expected_nodes = TOTAL_NODES - 1;  // Normal mode: all nodes except self
#endif

                                /* No timeout - continuous RX mode for ACK collection */
                                dwt_setrxtimeout(0);  // Disable timeout for continuous reception

                                /* Enable RX for ACK collection - polling will handle the rest */
                                dwt_forcetrxoff();
                                dwt_writesysstatuslo(0xFFFFFFFF);
                                dwt_rxenable(DWT_START_RX_IMMEDIATE);

                                static char start_msg[200];
                                snprintf(start_msg, sizeof(start_msg),
                                        "ACK collection started (slot %d): duration=%d cycles (%dms), no timeout, continuous RX",
                                        active_slot_type, (int)(slot_duration_cycles), (int)(slot_duration_cycles / (CPU_FREQ_HZ/1000)));
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

        /* Check if ACK collection has finished using CPU cycles */
        if (collecting_acks) {
            /* Get current CPU cycle count - no SPI access needed */
            uint32_t current_cycles = dwt_timer_get_cycles();

            /* Check if collection period has ended */
            if (current_cycles >= ack_collection_end_time) {
                collecting_acks = false;

                /* Get the appropriate count for current slot */
                uint8_t slot_unique_count = 0;
                const char *slot_desc = "";
                if (current_collection_slot == SLOT_TYPE_OWN) {
                    slot_unique_count = unique_ack_count;
                    slot_desc = "OWN";
                } else if (current_collection_slot == SLOT_TYPE_FL_RELAY) {
                    slot_unique_count = fl_relay_unique_ack_count;
                    slot_desc = "FL_RELAY";
                } else if (current_collection_slot == SLOT_TYPE_FR_RELAY) {
                    slot_unique_count = fr_relay_unique_ack_count;
                    slot_desc = "FR_RELAY";
                }

                /* Report detailed ACK collection results */
                static char ack_result[200];
                snprintf(ack_result, sizeof(ack_result),
                        "%s ACK collection ended: %d unique nodes, %d total ACKs, time-based ending",
                        slot_desc, slot_unique_count, collected_ack_count);
                test_run_info((unsigned char *)ack_result);

                /* List which nodes responded for current slot */
                if (slot_unique_count > 0) {
                    static char node_list[150];
                    int pos = snprintf(node_list, sizeof(node_list), "%s responding nodes: ", slot_desc);

                    uint8_t *current_ack_array = NULL;
                    if (current_collection_slot == SLOT_TYPE_OWN) {
                        current_ack_array = ack_received_from;
                    } else if (current_collection_slot == SLOT_TYPE_FL_RELAY) {
                        current_ack_array = fl_relay_ack_received_from;
                    } else if (current_collection_slot == SLOT_TYPE_FR_RELAY) {
                        current_ack_array = fr_relay_ack_received_from;
                    }

                    if (current_ack_array) {
                        for (int i = 0; i < TOTAL_NODES && pos < sizeof(node_list) - 10; i++) {
                            if (current_ack_array[i]) {
                                uint8_t node_char = index_to_node_id(i);
                                pos += snprintf(node_list + pos, sizeof(node_list) - pos, "%c ", node_char);
                            }
                        }
                    }
                    test_run_info((unsigned char *)node_list);
                }

                /* Evaluate ACK collection result for current slot */
                if (slot_unique_count >= expected_nodes) {
                    /* SUCCESS: All expected nodes responded for this slot */
                    if (current_collection_slot == SLOT_TYPE_OWN) {
                        /* Own data transmission successful */
                        tx_state = TX_STATE_IDLE;
                        has_pending_retrans = false;

                        /* Record success in current period for SEQ 1 */
                        if (period_in_cycle == 1) {
                            own_data_success_in_period_1 = true;
                        } else if (period_in_cycle == 2) {
                            own_data_success_in_period_2 = true;
                        } else if (period_in_cycle == 3) {
                            own_data_success_in_period_3 = true;
                        }

                        /* Note: Cycle statistics will be updated at new cycle start */
                    } else if (current_collection_slot == SLOT_TYPE_FL_RELAY) {
                        /* FL relay transmission successful */
                        fl_relay_success++;
                        fl_pending_retrans = false;
                        fl_relay_completed = true;  // Mark FL relay as completed for this cycle
                    } else if (current_collection_slot == SLOT_TYPE_FR_RELAY) {
                        /* FR relay transmission successful */
                        fr_relay_success++;
                        fr_pending_retrans = false;
                        fr_relay_completed = true;  // Mark FR relay as completed for this cycle
                    }

                    static char success_msg[150];
                    snprintf(success_msg, sizeof(success_msg),
                            "%s TRANSMISSION SUCCESS: %d/%d nodes responded - slot completed",
                            slot_desc, slot_unique_count, expected_nodes);
                    test_run_info((unsigned char *)success_msg);

                } else {
                    /* FAILURE: Need retransmission based on slot type */
                    if (current_collection_slot == SLOT_TYPE_OWN) {
                        /* Own data transmission failed */
                        if (tx_state == TX_STATE_FIRST_TX) {
                            /* First transmission failed - prepare for retransmission */
                            tx_state = TX_STATE_RETRANS;
                            memcpy(retrans_msg, tx_msg, sizeof(tx_msg));
                            has_pending_retrans = true;
                            retrans_attempt = 1;

                            static char fail_msg[150];
                            snprintf(fail_msg, sizeof(fail_msg),
                                    "OWN FIRST TX FAILED: %d/%d nodes responded - Message stored for retransmission",
                                    slot_unique_count, expected_nodes);
                            test_run_info((unsigned char *)fail_msg);
                        } else {
                            /* Retransmission also failed */
                            retrans_attempt++;

                            static char retry_msg[150];
                            snprintf(retry_msg, sizeof(retry_msg),
                                    "OWN RETRANS FAILED: %d/%d nodes responded - Attempt %d (will continue in cycle)",
                                    slot_unique_count, expected_nodes, retrans_attempt);
                            test_run_info((unsigned char *)retry_msg);
                        }
                    } else if (current_collection_slot == SLOT_TYPE_FL_RELAY) {
                        /* FL relay transmission failed */
                        fl_pending_retrans = true;
                        static char fl_fail_msg[150];
                        snprintf(fl_fail_msg, sizeof(fl_fail_msg),
                                "FL RELAY FAILED: %d/%d nodes responded - will retry in next period",
                                slot_unique_count, expected_nodes);
                        test_run_info((unsigned char *)fl_fail_msg);
                    } else if (current_collection_slot == SLOT_TYPE_FR_RELAY) {
                        /* FR relay transmission failed */
                        fr_pending_retrans = true;
                        static char fr_fail_msg[150];
                        snprintf(fr_fail_msg, sizeof(fr_fail_msg),
                                "FR RELAY FAILED: %d/%d nodes responded - will retry in next period",
                                slot_unique_count, expected_nodes);
                        test_run_info((unsigned char *)fr_fail_msg);
                    }

                    /* List missing nodes for debugging */
                    static char missing_list[150];
                    int pos = snprintf(missing_list, sizeof(missing_list), "%s Missing ACKs from: ", slot_desc);

                    uint8_t *current_ack_array = NULL;
                    if (current_collection_slot == SLOT_TYPE_OWN) {
                        current_ack_array = ack_received_from;
                    } else if (current_collection_slot == SLOT_TYPE_FL_RELAY) {
                        current_ack_array = fl_relay_ack_received_from;
                    } else if (current_collection_slot == SLOT_TYPE_FR_RELAY) {
                        current_ack_array = fr_relay_ack_received_from;
                    }

                    if (current_ack_array) {
                        for (int i = 0; i < TOTAL_NODES && pos < sizeof(missing_list) - 10; i++) {
                            if (!current_ack_array[i] && i != 0) {  // Skip initiator node (index 0)
                                uint8_t node_char = index_to_node_id(i);
                                pos += snprintf(missing_list + pos, sizeof(missing_list) - pos, "%c ", node_char);
                            }
                        }
                    }
                    if (pos > strlen("Missing ACKs from: ") + strlen(slot_desc)) {
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

        /* 2. POLLING: Check for RX events by reading status register */
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
                /* Check if received frame is an ACK or RELAY_ACK */
                if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_ACK || rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_RELAY_ACK) {
                    /* Track ACK source to prevent duplicates */
                    uint8_t ack_source_char = rx_buffer[IDX_SOURCE];
                    uint8_t ack_index = node_id_to_index(ack_source_char);

                    if (ack_index != 0xFF && ack_index < TOTAL_NODES) {
                        /* Determine which ACK tracking array to use based on current collection slot */
                        uint8_t *current_ack_array = NULL;
                        uint8_t *current_unique_count = NULL;
                        const char *slot_desc = "";

                        if (current_collection_slot == SLOT_TYPE_OWN) {
                            current_ack_array = ack_received_from;
                            current_unique_count = &unique_ack_count;
                            slot_desc = "OWN";
                        } else if (current_collection_slot == SLOT_TYPE_FL_RELAY) {
                            current_ack_array = fl_relay_ack_received_from;
                            current_unique_count = &fl_relay_unique_ack_count;
                            slot_desc = "FL_RELAY";
                        } else if (current_collection_slot == SLOT_TYPE_FR_RELAY) {
                            current_ack_array = fr_relay_ack_received_from;
                            current_unique_count = &fr_relay_unique_ack_count;
                            slot_desc = "FR_RELAY";
                        }

                        if (current_ack_array && current_unique_count) {
                            if (!current_ack_array[ack_index]) {
                                /* First ACK from this node for this slot type - mark it */
                                current_ack_array[ack_index] = 1;
                                (*current_unique_count)++;
                                collected_ack_count++;

                                static char ack_msg[150];
                                snprintf(ack_msg, sizeof(ack_msg), "%s ACK from node %c (idx:%d, slot_unique:%d/%d)",
                                        slot_desc, ack_source_char, ack_index, *current_unique_count, expected_nodes);
                                test_run_info((unsigned char *)ack_msg);

                                /* Check if we've received all expected ACKs for this slot - if so, end collection immediately */
                                if (*current_unique_count >= expected_nodes) {
                                collecting_acks = false;
                                static char complete_msg[150];
                                snprintf(complete_msg, sizeof(complete_msg),
                                        "%s ACK collection COMPLETE: %d/%d nodes - ending early",
                                        slot_desc, *current_unique_count, expected_nodes);
                                test_run_info((unsigned char *)complete_msg);

                                /* Handle success based on slot type */
                                if (current_collection_slot == SLOT_TYPE_OWN) {
                                    /* Own data transmission successful - record success in current period */
                                    tx_state = TX_STATE_IDLE;
                                    has_pending_retrans = false;

                                    if (period_in_cycle == 1) {
                                        own_data_success_in_period_1 = true;
                                    } else if (period_in_cycle == 2) {
                                        own_data_success_in_period_2 = true;
                                    } else if (period_in_cycle == 3) {
                                        own_data_success_in_period_3 = true;
                                    }

                                    /* Note: Cycle statistics will be updated at new cycle start */
                                } else if (current_collection_slot == SLOT_TYPE_FL_RELAY) {
                                    /* FL relay transmission successful */
                                    fl_relay_success++;
                                    fl_pending_retrans = false;
                                    fl_relay_completed = true;  // Mark FL relay as completed for this cycle

                                    /* Update statistics for FL relay (not SEQ 1) */
                                    if (!cycle_completed) {
                                        total_cycles++;
                                        successful_cycles++;
                                        if (period_in_cycle == 1) {
                                            first_tx_success++;
                                        } else if (period_in_cycle == 2) {
                                            retrans_success++;
                                            period_2_retrans_success++;
                                        } else if (period_in_cycle == 3) {
                                            retrans_success++;
                                            period_3_retrans_success++;
                                        }
                                        cycle_completed = true;
                                    }
                                } else if (current_collection_slot == SLOT_TYPE_FR_RELAY) {
                                    /* FR relay transmission successful */
                                    fr_relay_success++;
                                    fr_pending_retrans = false;
                                    fr_relay_completed = true;  // Mark FR relay as completed for this cycle

                                    /* Update statistics for FR relay (not SEQ 1) */
                                    if (!cycle_completed) {
                                        total_cycles++;
                                        successful_cycles++;
                                        if (period_in_cycle == 1) {
                                            first_tx_success++;
                                        } else if (period_in_cycle == 2) {
                                            retrans_success++;
                                            period_2_retrans_success++;
                                        } else if (period_in_cycle == 3) {
                                            retrans_success++;
                                            period_3_retrans_success++;
                                        }
                                        cycle_completed = true;
                                    }
                                }

                                test_run_info((unsigned char *)"TRANSMISSION SUCCESS - All ACKs received (cumulative)");
                            }
                            } else {
                                /* Duplicate ACK from same node for this slot */
                                collected_ack_count++;  // Still count for total
                                static char dup_msg[150];
                                snprintf(dup_msg, sizeof(dup_msg), "%s Duplicate ACK from node %c (total:%d)",
                                        slot_desc, ack_source_char, collected_ack_count);
                                test_run_info((unsigned char *)dup_msg);
                            }
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

                /* Quickly re-enable RX for next ACK */
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);  // Clear all status
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
                continue;  // Skip normal processing - optimized for speed
            }

            /* Normal RX processing */
            uint32_t elapsed_ms = get_elapsed_ms(last_sync_cycles);
            static char rx_log[100];
            snprintf(rx_log, sizeof(rx_log), "[%ums] RX received !!", elapsed_ms);
            test_run_info((unsigned char *)rx_log);

            /* Print received frame */
            static char frame_debug[200];
            snprintf(frame_debug, sizeof(frame_debug),
                     "[%ums] Frame RX: len=%d, src=%c, type=%02X, data: %02X %02X %02X %02X",
                     elapsed_ms, rx_frame_len,
                     (rx_frame_len > IDX_SOURCE) ? (char)rx_buffer[IDX_SOURCE] : '?',
                     (rx_frame_len > IDX_MSG_TYPE) ? rx_buffer[IDX_MSG_TYPE] : 0xFF,
                     rx_buffer[0], rx_buffer[1], rx_buffer[2], rx_buffer[3]);
            test_run_info((unsigned char *)frame_debug);

            /* Handle FL/FR data for relay system - store messages for later relay */
            if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA) {
                uint8_t src_node = rx_buffer[IDX_SOURCE];

                if (src_node == NODE_FL) {
                    /* Store FL message for relay in SEQ 10 */
                    memcpy(fl_relay_msg, rx_buffer, FRAME_LEN_MAX);
                    /* Convert to relay message */
                    fl_relay_msg[IDX_MSG_TYPE] = MSG_TYPE_RELAY_DATA;
                    fl_relay_msg[IDX_SOURCE] = NODE_INIT;  // INIT becomes the sender
                    fl_relay_msg[IDX_DEST] = NODE_ALL;     // Broadcast relay
                    fl_relay_msg[IDX_ORIG_SRC] = src_node; // Original source
                    fl_relay_msg[IDX_ORIG_DST] = rx_buffer[IDX_DEST]; // Original destination
                    has_fl_data = true;
                    fl_pending_retrans = false;

                    static char fl_msg[100];
                    snprintf(fl_msg, sizeof(fl_msg), "[%ums] FL data stored for relay in SEQ 10", elapsed_ms);
                    test_run_info((unsigned char *)fl_msg);

                } else if (src_node == NODE_FR) {
                    /* Store FR message for relay in SEQ 12 */
                    memcpy(fr_relay_msg, rx_buffer, FRAME_LEN_MAX);
                    /* Convert to relay message */
                    fr_relay_msg[IDX_MSG_TYPE] = MSG_TYPE_RELAY_DATA;
                    fr_relay_msg[IDX_SOURCE] = NODE_INIT;  // INIT becomes the sender
                    fr_relay_msg[IDX_DEST] = NODE_ALL;     // Broadcast relay
                    fr_relay_msg[IDX_ORIG_SRC] = src_node; // Original source
                    fr_relay_msg[IDX_ORIG_DST] = rx_buffer[IDX_DEST]; // Original destination
                    has_fr_data = true;
                    fr_pending_retrans = false;

                    static char fr_msg[100];
                    snprintf(fr_msg, sizeof(fr_msg), "[%ums] FR data stored for relay in SEQ 12", elapsed_ms);
                    test_run_info((unsigned char *)fr_msg);
                }
            }

            if ((rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA || rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_URGENT || rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_RELAY_DATA))
            {
                /* Only send ACK if NOT in our own slot (prevents ACK during ACK collection) */
                if (!slot_active) {
                    static char ack_log[100];
                    snprintf(ack_log, sizeof(ack_log), "[%ums] DATA received, sending ACK", elapsed_ms);
                    test_run_info((unsigned char *)ack_log);

                    /* Apply ACK transmission delay based on node sequence to prevent collisions */
                    uint32_t ack_delay_us = (MY_NODE_SEQ-1) * ACK_TX_INTERVAL_US;
                    if (ack_delay_us > 0) {
                        static char delay_msg[100];
                        snprintf(delay_msg, sizeof(delay_msg), "ACK delay: %u us for node %d", ack_delay_us, MY_NODE_SEQ);
                        test_run_info((unsigned char *)delay_msg);
                        //nrf_delay_us(ack_delay_us);
                    }

                    tx_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK; // Frame type for ACK
                    tx_msg[IDX_SOURCE] = NODE_INIT;     // This node (initiator)
                    tx_msg[IDX_DEST] = rx_buffer[IDX_SOURCE]; // Reply to sender

                    /* Write response frame data to DW IC and prepare transmission. */
                    dwt_writetxdata(sizeof(tx_msg), tx_msg, 0); /* Zero offset in TX buffer. */
                    dwt_writetxfctrl(sizeof(tx_msg), 0, 0);     /* Zero offset in TX buffer, no ranging. */

                    /* Send the response with immediate RX return - no critical section in polling mode */
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

                            static char ack_sent_log[100];
                            snprintf(ack_sent_log, sizeof(ack_sent_log), "[%ums] ACK sent successfully", elapsed_ms);
                            test_run_info((unsigned char *)ack_sent_log);
                        } else {
                            static char ack_fail_log[100];
                            snprintf(ack_fail_log, sizeof(ack_fail_log), "[%ums] ACK TX completion failed (status: 0x%08X)", elapsed_ms, ack_tx_status);
                            test_run_info((unsigned char *)ack_fail_log);
                        }
                    } else {
                        static char ack_start_fail_log[100];
                        snprintf(ack_start_fail_log, sizeof(ack_start_fail_log), "[%ums] ACK TX failed to start (result: %d)", elapsed_ms, ack_tx_result);
                        test_run_info((unsigned char *)ack_start_fail_log);
                    }
                } else {
                    static char no_ack_log[100];
                    snprintf(no_ack_log, sizeof(no_ack_log), "[%ums] DATA received during own slot - no ACK sent", elapsed_ms);
                    test_run_info((unsigned char *)no_ack_log);
                }
            }

            /* Re-enable RX for continuous reception */
            dwt_forcetrxoff();
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

        /* Small delay to prevent overwhelming the processor */
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
