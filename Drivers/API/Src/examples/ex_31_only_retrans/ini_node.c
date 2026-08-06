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

#if defined(TEST_ONLY_RETRANS_INIT)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "INITIATOR NODE ONLY RETRANS v1.0"


/* ========== TDMA Protocol Parameters - Aggregated ACKs ========== */
// Period: 18.7ms (adjusted for FR relay guard time)
// Slot: 0.6ms (0.1ms TX + 0.5ms guard, except FR relay with 1.6ms guard)
// Config switch: 12.0ms (6.7ms buffer for SYNC reception at 18.7ms)

#define TOTAL_NODES         10      // Total physical nodes in network
#define TOTAL_SLOTS         12      // Total TDMA slots (includes relay slots)
#define TOTAL_ARRAY_SIZE    12      // Array size for tracking all transmissions (nodes + relay slots)
#define PERIODS_PER_CYCLE   6       // 6 periods per cycle (3 period pairs)
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
#define NODE_FL   '9'    // Front Left node - SEQ 9, index 8
#define NODE_FR   'A'    // Front Right node - SEQ 11, index 9
#define NODE_ALL  'B'    // Broadcast to all nodes

/* ========== TEST MODE Configuration ========== */
#define TEST_MODE 1  // Enable test mode with 4 nodes only

#if TEST_MODE
#define TEST_TOTAL_NODES 5     // Only 5 nodes in test: INIT, NODE_4, NODE_5, NODE_FL, NODE_FR
#define TEST_EXPECTED_ACKS 4   // Expect ACKs from 4 other nodes for own data (exclude self)
#endif

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

/* INIT Node has 1 transmission slot: SEQ 1 (own data only - NO RELAY) */
typedef enum {
    SLOT_TYPE_OWN = 1      // SEQ 1 - Own data transmission only
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

/* Frame structure matching normal node - Extended to 22 bytes to fit ACK_ARRAY (index 8-19) + 2 bytes for CRC */
static uint8_t tx_msg[] = { 0x41, 0x8C, 0, 0x9A, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 'D', 'W', 0x10, 0x00, 0, 0, 0, 0, 0 };
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

// ACK tracking
static uint8_t ack_status[TOTAL_NODES] = {0};

/* RF TX config */
extern dwt_txconfig_t txconfig_options;

/* 여러 응답 수신 관련 */
#define MAX_RESPONSES 10

/* ========== Per-Node Latency Tracking ========== */
/* One-way latency statistics structure */
typedef struct {
    uint32_t min_us;           // Minimum latency (microseconds)
    uint32_t max_us;           // Maximum latency
    uint64_t sum_us;           // Sum for average calculation
    uint32_t count;            // Number of samples
} latency_stats_t;

/* Per-node latency tracking (10 slots: INIT, NODE_2-8, FL, FR) */
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

/* DATA message tracking - Aggregated ACKs approach */
/* Instead of collecting ACKs, we track which slots' transmissions we received during the period */
/* Array indices: 0=INIT, 1=Node2, ..., 8=FL, 10=FR (no relay slots) */

/* Period-level tracking (reset every period) */
static uint8_t data_received_from[TOTAL_ARRAY_SIZE] = {0};  // Track which slots' DATA we received (Period 1,3,5)
static uint8_t unique_data_count = 0;                        // Count of unique DATA sources

/* Cycle-level cumulative tracking (reset every cycle) */
static uint8_t cumulative_ack_confirmed[TOTAL_ARRAY_SIZE] = {0};  // Cumulative ACK confirmations across period pairs
static uint8_t cumulative_ack_count = 0;                           // Count of confirmed ACKs in this cycle

/* Slot index mapping for 10-slot system (no relay slots) */
#define SLOT_IDX_INIT       0   // INIT (SEQ 1)
#define SLOT_IDX_NODE_2     1   // Node 2 (SEQ 2)
#define SLOT_IDX_NODE_3     2   // Node 3 (SEQ 3)
#define SLOT_IDX_NODE_4     3   // Node 4 (SEQ 4)
#define SLOT_IDX_NODE_5     4   // Node 5 (SEQ 5)
#define SLOT_IDX_NODE_6     5   // Node 6 (SEQ 6)
#define SLOT_IDX_NODE_7     6   // Node 7 (SEQ 7)
#define SLOT_IDX_NODE_8     7   // Node 8 (SEQ 8)
#define SLOT_IDX_FL         8   // FL (SEQ 9)
#define SLOT_IDX_FR         10  // FR (SEQ 11)

/* Cycle and Period management for retransmission */
static uint32_t current_cycle = 1;  // Start from cycle 1 for 3 cycles
static uint8_t period_in_cycle = 1;  // 1-6 (3 period pairs)

/* ========== Period Recovery System ========== */
/* Internal period counter runs independently of SYNC */
static uint8_t internal_period_count = 1;  // 1-6, increments every 21.2ms
static uint32_t last_period_update_cycles = 0;  // Last period increment time

/* ========== Pair-level Statistics (3 pairs per cycle) ========== */
// Pair 1 (Period 1-2)
static uint32_t pair1_data_success = 0, pair1_data_fail = 0, pair1_data_idle = 0;

// Pair 2 (Period 3-4)
static uint32_t pair2_data_success = 0, pair2_data_fail = 0, pair2_data_idle = 0;

// Pair 3 (Period 5-6)
static uint32_t pair3_data_success = 0, pair3_data_fail = 0, pair3_data_idle = 0;

/* ========== Cycle-level Statistics ========== */
static uint32_t total_cycles = 0;
static uint32_t data_successful_cycles = 0;      // DATA succeeded in any pair
static uint32_t failed_cycles = 0;               // DATA failed in all pairs

/* Failed cycle tracking (최근 10개 저장) */
#define MAX_FAILED_CYCLES_LOG 10
static uint32_t failed_cycle_numbers[MAX_FAILED_CYCLES_LOG] = {0};

/* Tracking per-cycle success for each item (reset every cycle) */
static bool data_success_in_current_cycle = false;
static bool cycle_completed = false;  // Track if current cycle finished

/* Final statistics flag */
static bool final_stats_printed = false;
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

/* Helper function to get slot index from message type and source */
static uint8_t get_slot_index(uint8_t msg_type, uint8_t src_node, uint8_t orig_src) {
    if (msg_type == MSG_TYPE_DATA) {
        // Regular DATA: map to direct transmission slots
        if (src_node == NODE_FL) {
            return SLOT_IDX_FL;  // 8 - FL direct transmission
        } else if (src_node == NODE_FR) {
            return SLOT_IDX_FR;  // 10 - FR direct transmission
        } else {
            return node_id_to_index(src_node);  // Normal nodes 0-7
        }
    }
    return 0xFF;  // Invalid
}

/*
 * 특정 전송 슬롯에 대해 해당 슬롯으로부터 ACK를 기대하는지 확인하는 헬퍼 함수
 *
 * 파라미터:
 *   ack_slot_idx: ACK를 전송하는 슬롯의 인덱스 (어떤 노드가 ACK를 보내는지)
 *   tx_slot_idx:  ACK 대상이 되는 전송 슬롯의 인덱스 (어떤 슬롯의 전송에 대한 ACK인지)
 *
 * 반환값:
 *   true:  ack_slot_idx 슬롯이 tx_slot_idx 슬롯의 전송에 대해 ACK를 보낼 것으로 기대됨
 *   false: ack_slot_idx 슬롯으로부터 ACK를 기대하지 않음
 *
 * 예시:
 *   is_expected_ack_slot_for(SLOT_IDX_NODE_4, SLOT_IDX_INIT)
 *     → true (NODE_4는 INIT의 데이터에 대해 ACK를 보내야 함)
 */
static bool is_expected_ack_slot_for(uint8_t ack_slot_idx, uint8_t tx_slot_idx) {
    #if TEST_MODE
    if (tx_slot_idx == SLOT_IDX_INIT) {
        // SEQ 1 (own data): expect ACK from NODE_4, NODE_5, FL, FR
        // Explicit listing to handle non-contiguous slot indices (FL=8, FR=10)
        if (ack_slot_idx == SLOT_IDX_NODE_4) return true;
        if (ack_slot_idx == SLOT_IDX_NODE_5) return true;
        if (ack_slot_idx == SLOT_IDX_FL) return true;
        if (ack_slot_idx == SLOT_IDX_FR) return true;
        return false;
    }
    return false;
    #else
    // Normal mode: expect all except self
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

int only_retrans_init(void)
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
    uint32_t period_interval_cycles = us_to_cpu_cycles(21200);     // 21.2ms period interval (1.0ms guard time)
    uint32_t slot_interval_cycles = us_to_cpu_cycles(1100);        // 1.1ms slot timing (0.1ms TX + 1.0ms guard)
    uint32_t slot_duration_cycles = us_to_cpu_cycles(100);         // 0.1ms TX duration only
    uint32_t config_switch_time_cycles = us_to_cpu_cycles(17200);  // 17.2ms - switch to SYNC config (4.0ms buffer)
    /* INIT node has only SEQ 1 slot (no relay slots) */
    bool seq1_executed = false;     // SEQ 1 (own data) execution flag
    bool config_is_sync = false;    // Track current config state (starts in SYNC for first TX)

    /* Slot timer for SEQ 1 (absolute timing from SYNC TX completion) */
    uint32_t seq1_start_cycles = us_to_cpu_cycles(2000);   // SEQ 1 (INIT): 2ms buffer after SYNC TX

    bool slot_active = false;                               // Track if currently in any slot
    bool is_period_expired = false;                        // Flag to reduce timing checks

    /* Debug: Print calculated intervals */
    static char interval_debug[150];
    snprintf(interval_debug, sizeof(interval_debug),
             "DWT Intervals: period=%u cycles (20.2ms), slot=%u cycles (1.1ms), TX_duration=%u cycles (0.1ms)",
             period_interval_cycles, slot_interval_cycles, slot_duration_cycles);
    test_run_info((unsigned char *)interval_debug);

    /* Enable RX immediately for continuous listening */
    test_run_info((unsigned char *)"Starting Aggregated ACKs: 20.2ms period + 2ms buffer + 1.1ms slots + 2.5ms SYNC buffer");

    /* Initial RX enable - no critical section needed in polling mode */
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    //Sleep(15000); // 15 sec
    Sleep(20000); // 1 sec

    while (1)
    {
        /* ========== Period Recovery: Internal Period Counter Update ========== */
        /* Independent 21.2ms timer - runs regardless of SYNC */
        uint32_t period_interval_cycles_recovery = us_to_cpu_cycles(21200);  // 21.2ms
        if (last_period_update_cycles == 0 ||
            dwt_timer_elapsed(last_period_update_cycles, period_interval_cycles_recovery)) {

            /* Update timer reference */
            last_period_update_cycles = dwt_timer_get_cycles();

            /* Increment internal period counter (1-6 cycle) */
            internal_period_count++;
            if (internal_period_count > PERIODS_PER_CYCLE) {
                internal_period_count = 1;  // Wrap around 1-6
            }
        }

        /* 1. Check if it's time to send SYNC (Period Timer) - same pattern as normal node */
        //MARK: 새로운 period: SYNC 전송

        /* Check if it's time for new period */
        if (last_sync_cycles == 0 || dwt_timer_elapsed(last_sync_cycles, period_interval_cycles)) {
            /* Variable declarations for C89 compatibility */
            uint32_t current_cycles;
            uint32_t elapsed_since_last;
            char info_str[120];
            static char sync_debug[150];
            int sync_result;
            uint32_t tx_status;
            uint32_t current_sync_cycles;
            uint32_t cycles_since_last_sync;
            float ms_since_last_sync;
            static char sync_interval_log[120];

            /* Get current CPU cycle count - no SPI access needed */
            current_cycles = dwt_timer_get_cycles();

            if (last_sync_cycles == 0 || dwt_timer_elapsed(last_sync_cycles, period_interval_cycles)) {
                /* Store previous sync cycles for interval logging */
                uint32_t prev_sync_cycles = last_sync_cycles;

                /* CRITICAL: Set timing reference IMMEDIATELY at period start to maintain exact 18.7ms intervals */
                /* This prevents config switch and TX processing delays from accumulating */
                last_sync_cycles = current_cycles;

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

                    /* Evaluate previous cycle (for cycle 2+) */
                    if (current_cycle > 1) {
                        /* Update cycle-level statistics */
                        if (data_success_in_current_cycle) {
                            data_successful_cycles++;
                        } else {
                            failed_cycles++;
                            /* Store failed cycle number (최근 10개만 유지) */
                            if (failed_cycles <= MAX_FAILED_CYCLES_LOG) {
                                failed_cycle_numbers[failed_cycles - 1] = current_cycle - 1;
                            }
                        }

                        total_cycles++;
                    }

                    /* Reset per-cycle success flags for new cycle */
                    data_success_in_current_cycle = false;

                    /* Reset cumulative ACK tracking for new cycle */
                    memset(cumulative_ack_confirmed, 0, sizeof(cumulative_ack_confirmed));
                    cumulative_ack_count = 0;

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

                /* Check if we've completed 1000 cycles and print final statistics */
                //MARK: 종료 조건
                if (current_cycle > 1000 && !final_stats_printed) {
                    /* Print final statistics with pair-level and cycle-level details */
                    static char final_stats[800];
                    float data_rate = (total_cycles > 0) ? (float)data_successful_cycles / total_cycles * 100 : 0;

                    snprintf(final_stats, sizeof(final_stats),
                            "\n=== PAIR STATISTICS (1000 cycles) - ONLY RETRANS (NO RELAY) ===\n"
                            "Pair 1 (Period 1-2):\n"
                            "  DATA: Success=%d, Fail=%d, IDLE=%d\n\n"
                            "Pair 2 (Period 3-4):\n"
                            "  DATA: Success=%d, Fail=%d, IDLE=%d\n\n"
                            "Pair 3 (Period 5-6):\n"
                            "  DATA: Success=%d, Fail=%d, IDLE=%d\n\n"
                            "=== CYCLE STATISTICS ===\n"
                            "Total Cycles: %d\n"
                            "Successful Cycles: %d (%.2f%%)\n"
                            "Failed Cycles: %d (%.2f%%)",
                            pair1_data_success, pair1_data_fail, pair1_data_idle,
                            pair2_data_success, pair2_data_fail, pair2_data_idle,
                            pair3_data_success, pair3_data_fail, pair3_data_idle,
                            total_cycles,
                            data_successful_cycles, data_rate,
                            failed_cycles, (total_cycles > 0) ? (float)failed_cycles / total_cycles * 100 : 0);
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

                    /* Print per-node latency statistics */
                    test_run_info((unsigned char *)"\n=== ONE-WAY LATENCY BY NODE ===");
                    for (uint8_t i = 0; i < TOTAL_ARRAY_SIZE; i++) {
                        if (i== 9 || i == 11) {
                            continue; // Skip relay slots
                        }
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

                /* IMMEDIATELY reset slot flag when new period starts - before any TX operations */
                seq1_executed = false;

                /* Switch to SYNC config before SYNC TX */
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    test_run_info((unsigned char *)"Switched to SYNC config (PLEN512) for SYNC TX");
                    config_is_sync = true;  // Mark that we're now in SYNC config
                } else {
                    test_run_info((unsigned char *)"WARNING: Failed to switch to SYNC config!");
                }

                /* Debug: Show period interval timing */
                elapsed_since_last = (last_sync_cycles != 0) ? (current_cycles - last_sync_cycles) : 0;
                snprintf(info_str, sizeof(info_str), "Period %d start (interval: %u cycles, expected: %u) - slot flag RESET",
                         period_count, elapsed_since_last, period_interval_cycles);
                test_run_info((unsigned char *)info_str);

                /* Prepare SYNC frame */
                tx_msg[0] = 0xC5;
                tx_msg[IDX_MSG_TYPE] = MSG_TYPE_SYNC;
                tx_msg[IDX_PERIOD_INFO] = period_in_cycle;  // Add period info (1-6) for recovery

                /* Print SYNC frame content */
                snprintf(sync_debug, sizeof(sync_debug), "SYNC TX: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                        tx_msg[0], tx_msg[1], tx_msg[2], tx_msg[3], tx_msg[4], tx_msg[5],
                        tx_msg[6], tx_msg[7], tx_msg[8], tx_msg[9], tx_msg[10], tx_msg[11]);
                test_run_info((unsigned char *)sync_debug);

                dwt_setrxaftertxdelay(0);
                dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
                dwt_writetxfctrl(sizeof(tx_msg), 0, 0);

                /* Send SYNC - no critical section needed in polling mode */
                sync_result = dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                if (sync_result == DWT_SUCCESS) {

                    test_run_info((unsigned char *)"SYNC TX started successfully");

                    /* Wait for TX completion */
                    tx_status = 0;
                    waitforsysstatus(&tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                    if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
                        test_run_info((unsigned char *)"SYNC TX completed successfully");
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                        /* Calculate and log SYNC transmission interval for timing verification */
                        current_sync_cycles = dwt_timer_get_cycles();
                        if (prev_sync_cycles != 0) {
                            cycles_since_last_sync = current_sync_cycles - prev_sync_cycles;
                            ms_since_last_sync = cycles_since_last_sync / 64.0;
                            snprintf(sync_interval_log, sizeof(sync_interval_log),
                                    "INIT SYNC TX interval: %u cycles (%.2f ms) - expected 18.7ms",
                                    cycles_since_last_sync, ms_since_last_sync);
                            test_run_info((unsigned char *)sync_interval_log);
                        }

                        /* CRITICAL: Clear RX buffer to discard any old messages from previous period */
                        dwt_forcetrxoff();
                        dwt_writesysstatuslo(0xFFFFFFFF);  // Clear all status flags and RX buffer

                        /* Switch to DATA config after SYNC TX for DATA/ACK operations */
                        if (dwt_configure(&config_data) == DWT_SUCCESS) {
                            test_run_info((unsigned char *)"Switched to DATA config (PLEN64) for normal operations");
                            config_is_sync = false;  // Mark that we're now in DATA config
                        } else {
                            test_run_info((unsigned char *)"WARNING: Failed to switch to DATA config!");
                        }

                        /* Note: last_sync_cycles was already set at period start (line 624) */
                        /* This ensures exact 18.7ms intervals regardless of SYNC TX processing delays */

                        /* Reset slot flag - the new SYNC period starts now */
                        seq1_executed = false;
                        slot_active = false;

                        /* Re-enable RX for continuous listening with clean buffer */
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);
                    } else {
                        test_run_info((unsigned char *)"SYNC TX completion failed!");
                        /* Note: last_sync_cycles already set at period start to prevent rapid retries */
                    }
                } else {
                    test_run_info((unsigned char *)"SYNC TX failed to start");
                    /* Note: last_sync_cycles already set at period start to prevent rapid retries */
                }

                /* Increment sequence number */
                tx_msg[DATA_FRAME_SN_IDX]++;

                /* RX already enabled above after buffer clear */
                continue;    
            }
        }

        /* 2. Check for SYNC config switch at 12.5ms (all nodes including INIT) */
        if (last_sync_cycles != 0 && !config_is_sync) {
            uint32_t current_cycles = dwt_timer_get_cycles();
            if (dwt_timer_elapsed(last_sync_cycles, config_switch_time_cycles)) {
                /* Switch to SYNC config at 12.0ms for 6.7ms buffer before next SYNC at 18.7ms */
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    test_run_info((unsigned char *)"INIT: Switched to SYNC config at 12.0ms (6.7ms buffer)");
                    config_is_sync = true;
                } else {
                    test_run_info((unsigned char *)"WARNING: INIT failed to switch to SYNC config!");
                }
                /* Re-enable RX after config change */
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
        }

        /* 3. Check for INIT node's 3 slot handling: SEQ 1 (own), SEQ 10 (FL relay), SEQ 12 (FR relay) */
        //MARK: INIT 3-slot handling: SEQ 1, 10, 12 with independent timers
        if (last_sync_cycles != 0) {
            /* Get current CPU cycle count - no SPI access needed */
            uint32_t current_cycles = dwt_timer_get_cycles();
            uint32_t cycles_since_sync = current_cycles - last_sync_cycles;

            init_slot_type_t active_slot_type = 0;
            bool should_execute_slot = false;

            /* Check SEQ 1: Own data slot (2ms after SYNC TX completion) */
            if (!seq1_executed && dwt_timer_elapsed(last_sync_cycles, seq1_start_cycles)) {
                /* Note: FL/FR data capture removed - no relay in this ablation study */

                active_slot_type = SLOT_TYPE_OWN;
                seq1_executed = true;
                should_execute_slot = true;

                static char slot_debug[200];
                snprintf(slot_debug, sizeof(slot_debug),
                        "SEQ 1 (OWN) start! cycles_since_sync=%u, elapsed=%ums (2ms buffer complete)",
                        cycles_since_sync, cycles_since_sync / (CPU_FREQ_HZ/1000));
                test_run_info((unsigned char *)slot_debug);
            }
            /* SEQ 10 and SEQ 12 (relay slots) removed - no relay in this ablation study */

            if (should_execute_slot) {
                /* Debug: Show which slot is starting */
                slot_active = true;  // Mark slot as active for ACK rejection logic

                /* Decide what to transmit based on active slot type */
                bool should_transmit = false;
                const char* tx_type_desc = "";

                if (active_slot_type == SLOT_TYPE_OWN) {
                    /* SEQ 1 - Determine period type: odd (1,3,5) = DATA, even (2,4,6) = ACK_ARRAY */
                    bool is_data_period = (period_count % 2 == 1);  // Odd period = DATA

                    if (is_data_period) {
                        /* Period 1, 3, 5: Send DATA (first TX or retransmission) */
                        if (tx_state == TX_STATE_IDLE || data_success_in_current_cycle) {
                            /* Already successful - no transmission needed, count as IDLE */
                            should_transmit = false;
                            tx_type_desc = "DATA IDLE (already successful)";

                            /* Count IDLE by pair */
                            if (period_in_cycle == 1) {
                                pair1_data_idle++;
                            } else if (period_in_cycle == 3) {
                                pair2_data_idle++;
                            } else if (period_in_cycle == 5) {
                                pair3_data_idle++;
                            }
                        } else {
                            /* Need to transmit DATA */
                            should_transmit = true;

                            /* CRITICAL: Only prepare NEW message in Period 1 */
                            /* Period 3, 5 should retransmit the SAME message from Period 1 */
                            if (period_in_cycle == 1) {
                                /* Period 1: Prepare NEW DATA frame */
                                tx_type_desc = "OWN DATA FIRST TX";
                                tx_msg[0] = 0xC5;
                                tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
                                tx_msg[IDX_SOURCE] = NODE_INIT;
                                tx_msg[IDX_DEST] = NODE_ALL;  // Broadcast
                                tx_msg[IDX_PRIORITY] = 1;
                                tx_msg[IDX_ORIG_SRC] = 0;
                                tx_msg[IDX_ORIG_DST] = 0;

                                /* Add TX timestamp (SYNC-relative time in microseconds) */
                                uint32_t tx_timestamp_us = (dwt_timer_get_cycles() - last_sync_cycles) / 64;
                                memcpy(&tx_msg[IDX_TX_TIMESTAMP], &tx_timestamp_us, sizeof(uint32_t));

                                /* Store for potential retransmission in Period 3, 5 */
                                memcpy(retrans_msg, tx_msg, sizeof(tx_msg));
                            } else {
                                /* Period 3, 5: RETRANSMIT same message from Period 1 */
                                tx_type_desc = "OWN DATA RETRANS";
                                memcpy(tx_msg, retrans_msg, sizeof(tx_msg));
                            }
                        }

                    } else {
                        /* Period 2, 4, 6: Send ACK_ARRAY */
                        should_transmit = true;
                        tx_type_desc = "OWN ACK_ARRAY TX";

                        /* Prepare ACK_ARRAY frame */
                        tx_msg[0] = 0xC5;
                        tx_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK_ARRAY;
                        tx_msg[IDX_SOURCE] = NODE_INIT;
                        tx_msg[IDX_DEST] = NODE_ALL;  // Broadcast
                        tx_msg[IDX_PRIORITY] = 1;
                        tx_msg[IDX_ORIG_SRC] = 0;
                        tx_msg[IDX_ORIG_DST] = 0;

                        /* Copy data_received_from[] array to payload (all 12 slots including relay) */
                        memcpy(&tx_msg[IDX_ACK_ARRAY], data_received_from, TOTAL_ARRAY_SIZE);

                        static char ack_array_debug[200];
                        int pos = snprintf(ack_array_debug, sizeof(ack_array_debug),
                                          "ACK_ARRAY payload: ");
                        for (int i = 0; i < TOTAL_ARRAY_SIZE && pos < sizeof(ack_array_debug) - 15; i++) {
                            if (data_received_from[i]) {
                                pos += snprintf(ack_array_debug + pos, sizeof(ack_array_debug) - pos,
                                              "%c ", index_to_node_id(i));
                            }
                        }
                        test_run_info((unsigned char *)ack_array_debug);
                    }

                } else {
                    /* No relay slots in this ablation study */
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
                            test_run_info((unsigned char *)"TX completed, returning to continuous RX");
                            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                            /* Note: Relay message storage removed - no relay in this ablation study */
                            /* OWN data is already stored in Period 1 preparation */

                            /* Immediately return to RX mode for continuous listening */
                            dwt_forcetrxoff();
                            dwt_writesysstatuslo(0xFFFFFFFF);
                            dwt_rxenable(DWT_START_RX_IMMEDIATE);

                            /* Note: No ACK collection - we passively listen for DATA/ACK_ARRAY messages */
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

        /* Note: No ACK collection end check - we use continuous RX mode */

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

            /* Process all received messages */
            uint32_t elapsed_ms = get_elapsed_ms(last_sync_cycles);

            /* Print received frame for debugging */
            static char rx_log[100];
            snprintf(rx_log, sizeof(rx_log), "[%ums] RX: type=0x%02X, src=%c",
                    elapsed_ms, rx_buffer[IDX_MSG_TYPE], rx_buffer[IDX_SOURCE]);
            test_run_info((unsigned char *)rx_log);

            /* ========== Aggregated ACKs Protocol: Handle message types ========== */

            /* Period odd (1,3,5): Track DATA messages */
            if ((period_count % 2 == 1) && (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA)) {

                /* Get slot index for this transmission */
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
                                "[%ums] P%d DATA from %s (slot %d) - count: %d",
                                elapsed_ms, period_count, get_slot_description(slot_idx),
                                slot_idx, unique_data_count);
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
                /* FL/FR relay data storage removed - no relay in this ablation study */
            }

            /* Period even (2,4,6): Process ACK_ARRAY messages */
            else if ((period_count % 2 == 0) && (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_ACK_ARRAY)) {

                uint8_t src_node = rx_buffer[IDX_SOURCE];
                uint8_t src_slot_idx = node_id_to_index(src_node);
                uint8_t *ack_array = &rx_buffer[IDX_ACK_ARRAY];  // 12 bytes

                /* Display ACK_ARRAY content for visibility */
                static char ack_array_display[250];
                int pos = snprintf(ack_array_display, sizeof(ack_array_display),
                                  "RX ACK_ARRAY from %s: ", get_slot_description(src_slot_idx));
                for (int i = 0; i < TOTAL_ARRAY_SIZE && pos < sizeof(ack_array_display) - 15; i++) {
                    if (ack_array[i]) {
                        pos += snprintf(ack_array_display + pos, sizeof(ack_array_display) - pos,
                                      "%c ", index_to_node_id(i));
                    }
                }
                test_run_info((unsigned char *)ack_array_display);

                /* INIT has only 1 slot to check: INIT(0) - no relay slots */

                /* Check INIT's own data (SLOT_IDX_INIT = 0) */
                if (is_expected_ack_slot_for(src_slot_idx, SLOT_IDX_INIT)) {
                    if (ack_array[SLOT_IDX_INIT] == 1) {
                        if (!cumulative_ack_confirmed[src_slot_idx]) {
                            cumulative_ack_confirmed[src_slot_idx] = 1;
                            cumulative_ack_count++;

                            static char init_ack_msg[150];
                            snprintf(init_ack_msg, sizeof(init_ack_msg),
                                    "INIT ACK from %s (slot %d) - cumulative: %d/%d",
                                    get_slot_description(src_slot_idx), src_slot_idx,
                                    cumulative_ack_count, TEST_EXPECTED_ACKS);
                            test_run_info((unsigned char *)init_ack_msg);

                            /* Check if all expected ACKs for INIT received */
                            if (cumulative_ack_count >= TEST_EXPECTED_ACKS) {
                                /* DATA success - track by pair and update cycle flag */
                                if (!data_success_in_current_cycle) {
                                    data_success_in_current_cycle = true;

                                    /* Increment pair-specific success counter */
                                    if (period_in_cycle == 1 || period_in_cycle == 2) {
                                        pair1_data_success++;
                                    } else if (period_in_cycle == 3 || period_in_cycle == 4) {
                                        pair2_data_success++;
                                    } else if (period_in_cycle == 5 || period_in_cycle == 6) {
                                        pair3_data_success++;
                                    }

                                    static char init_success_msg[150];
                                    snprintf(init_success_msg, sizeof(init_success_msg),
                                            "DATA SUCCESS in Period %d (Pair %d) - All 4 ACKs received",
                                            period_in_cycle, (period_in_cycle + 1) / 2);
                                    test_run_info((unsigned char *)init_success_msg);
                                }
                            }
                        }
                    }
                }
                /* FL_RELAY and FR_RELAY ACK processing removed - no relay in this ablation study */
            }

            /* FL/FR data storage for relay removed - no relay in this ablation study */

            /* Re-enable RX for continuous reception */
            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }

        /* POLLING: Handle RX timeout */
        if (status_reg & DWT_INT_RXFTO_BIT_MASK) {
            /* Clear timeout flag and re-enable RX */
            dwt_writesysstatuslo(DWT_INT_RXFTO_BIT_MASK);
            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
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
        //nrf_delay_us(10); // 10 microseconds delay
    }
}


#endif
