/*! ----------------------------------------------------------------------------
 *  @file    UWB_protocol_init.c
 *  @brief   UWB Protocol Init example code
 *
 * @author Decawave
 *
 * @copyright SPDX-FileCopyrightText: Copyright (c) 2024 Qorvo US, Inc.
 *            SPDX-License-Identifier: LicenseRef-QORVO-2
 *
 */

#include <example_selection.h>

#if defined(TEST_UWB_PROTOCOL_INIT)

#include "deca_probe_interface.h"
#include <deca_device_api.h>
#include <deca_spi.h>
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
#include "nrfx_timer.h"

// DWT (Data Watchpoint and Trace) for high-precision timing
#include "nrf.h"

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "UWB PROTOCOL INIT v1.0"

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

/* ========== Node ID Definitions ========== */
#define NODE_INIT '1'    // Initiator node (this node)
#define NODE_FL   '2'    // Front Left (needs relay)
#define NODE_FR   '3'    // Front Right (needs relay)
#define NODE_4    '4'    // Normal nodes
#define NODE_5    '5'
#define NODE_6    '6'
#define NODE_7    '7'
#define NODE_8    '8'
#define NODE_9    '9'
#define NODE_10   '0'
#define NODE_ALL  'A'    // Broadcast to all nodes

/* ========== Message Type Definitions ========== */
#define MSG_TYPE_SYNC      0x01
#define MSG_TYPE_DATA      0x02
#define MSG_TYPE_ACK       0x03
#define MSG_TYPE_URGENT    0x04
#define MSG_TYPE_RELAY     0x05
#define MSG_TYPE_ACK_STATUS 0x06

/* ========== Timing Constants (Fixed Period) - using Sleep() blocking delays ========== */
#define PERIOD_MS        20      // 20ms period to accommodate 2.0ms slots (10 nodes × 2.0ms = 20ms)
#define SLOT_DURATION_US 2000    // 2.0ms per slot to allow ACK reception (increased from 1.1ms)
#define GUARD_TIME_US    200     // 200us guard time
#define ACK_TIMEOUT_MS   1       // 1ms ACK timeout

/* ========== Slot Timing (Initiator is 10th/last slot) ========== */
#define INITIATOR_SLOT_NUMBER    9       // 10th slot (0-indexed)
#define INITIATOR_SLOT_START_US  (INITIATOR_SLOT_NUMBER * SLOT_DURATION_US)  // 9 * 2000 = 18000us = 18.0ms

/* ========== Protocol Parameters ========== */
#define MAX_NODES        10
#define QUEUE_SIZE       32
#define MAX_RETRY        3
#define MAX_PACKET_SIZE  125  // 127 - 2 (FCS) CRC 포함

#define USE_SPI2 1 // set this to 1 to use DW37X0 SPI2



/* Default communication configuration. We use default non-STS DW mode. */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_64,     /* Preamble length. Used in TX only. */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,//original #is 1                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (64 + 1 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, /* No STS mode enabled (STS Mode 0). */
    DWT_STS_LEN_64,   /* STS length, see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       /* PDOA mode off */
};

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
  IDX_RESERVED  = 8,  // 8~9: Reserved
  IDX_PAYLOAD   = 10, // 10~: payload
  TX_MSG_SIZE   = 127
};

/* ========== State Machine ========== */
typedef enum {
    STATE_WAIT_SYNC,     // Waiting for period timer: SYNC 신호 기다리기
    STATE_TX_SYNC,       // Transmitting SYNC signal: SYNC 신호 송신
    STATE_WAIT_MY_SLOT,  // Waiting for my slot interval: SYNC 후 내 슬롯까지 대기
    STATE_TX_ACTIVE,     // TX slot active (limited duration): 내 슬롯에서 송신 활성
    STATE_RX_LISTEN      // RX mode (most of the time): 대부분 수신 모드
} protocol_state_t;

/* ========== Period Mode (Simplified 3+N Cycle) ========== */
typedef enum {
    PERIOD_MODE_DATA_TX = 1,     // Period 1: Data transmission (all nodes)
    PERIOD_MODE_RELAY_TX = 2,    // Period 2: Initiator relay TX (FL/FR → others)
    PERIOD_MODE_ACK_STATUS = 3,  // Period 3: ACK status relay (Initiator → FL/FR)
    PERIOD_MODE_RETRANS = 4      // Period 4+: Retransmission periods
} period_mode_t;

/* ========== Message Queue Structure ========== */
typedef struct {
    uint8_t msg[MAX_PACKET_SIZE];
    uint8_t dest_id;
    uint8_t priority;  // 0:urgent, 1:normal
    uint8_t retry_count;
    uint32_t timestamp;
} message_t;

typedef struct {
    message_t buffer[QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} message_queue_t;

/* ========== ACK Tracker ========== */
typedef struct {
    uint8_t pending_nodes[MAX_NODES];   // ACK pending status
    uint8_t ack_received[MAX_NODES];    // ACK received status
    uint8_t retry_count[MAX_NODES];     // Retry counter per node
    uint32_t timeout_timestamp[MAX_NODES];
} ack_tracker_t; // TX한 메시지에 대한 ACK 추적

/* The frame sent in this example is an 802.15.4e standard blink. It is a 12-byte frame composed of the following fields:
 *     - byte 0: frame type (0xC5 for a blink).
 *     - byte 1: sequence number, incremented for each new frame.
 *     - byte 2 -> 9: device ID, see NOTE 1 below.
 */
static void process_own_message(uint8_t* buffer, uint32_t len);
static void update_period_state(void);

/* ========== Global Variables (needed for timer handlers) ========== */
static protocol_state_t current_state = STATE_TX_SYNC;  // Moved here for timer handler access

/* ========== Global Variables ========== */

// DWT-based non-blocking timers for UWB protocol
static dwt_timer_t period_timer;        // 20ms period timer
static dwt_timer_t slot_interval_timer; // 18ms wait for initiator slot
static dwt_timer_t slot_duration_timer; // 2ms TX slot duration limit

// Simple timing variables
static uint32_t period_count = 0;
static bool message_sent_this_slot = false;

// Period cycle tracking (5-period cycle with simplified 3+N structure)
period_mode_t current_period_mode = PERIOD_MODE_DATA_TX;
uint32_t cycle_number = 0;           // Which 1000-cycle iteration we're in
uint32_t period_in_cycle = 1;        // 1-5 within current cycle
bool is_first_period_in_cycle = false;  // Compatibility flag for existing code

// Protocol state
bool in_my_slot = false;
uint32_t my_slot_number = 0;  // Initiator takes slot 0

// Reliability counter for received messages
static uint32_t messages_received_count = 0;

// Protocol statistics counters
typedef struct {
    uint32_t periods_completed;
    uint32_t cycles_completed;
    uint32_t sync_messages_sent;
    uint32_t data_messages_sent;
    uint32_t acks_received;
    uint32_t acks_expected;
    uint32_t fl_messages_received;
    uint32_t fr_messages_received;
    uint32_t relay_messages_stored;
    uint32_t relay_messages_sent;
    uint32_t ack_status_sent;
    uint32_t retransmissions_sent;
    uint32_t rx_timeouts;
    uint32_t tx_failures;
    
    // Per-period mode statistics
    uint32_t period1_tx_count;    // Data transmission period
    uint32_t period2_tx_count;    // Relay transmission period  
    uint32_t period3_tx_count;    // ACK status period
    uint32_t period4plus_tx_count; // Retransmission periods
} protocol_stats_t;

static protocol_stats_t protocol_stats = {0};

// Node-specific RX statistics
typedef struct {
    uint8_t node_id;
    uint32_t packets_received;
    uint32_t acks_sent;
} node_rx_stats_t;

// Per-period statistics for detailed logging
typedef struct {
    // RX phase statistics (per node)
    node_rx_stats_t node_stats[10];  // Support up to 10 different nodes
    uint8_t active_nodes_count;
    uint32_t total_rx_packets;
    uint32_t relay_stored;
    
    // TX phase statistics  
    uint32_t tx_packets_sent;
    uint32_t tx_acks_expected;
    uint32_t tx_acks_received;
    
    // ACK status per node (1=received, 0=not received/not expected)
    uint8_t ack_status[10];  // Index 0=NODE_FL, 1=NODE_FR, 2=NODE_4, 3=NODE_5, 4=NODE_INIT, etc.
    uint8_t ack_expected[10]; // Track which nodes we expected ACKs from
    
    // Buffer status after period
    uint32_t normal_queue_remaining;
    uint32_t relay_queue_remaining;
    uint32_t retrans_queue_remaining;
} period_stats_t;

static period_stats_t current_period_stats = {0};

// Helper function to convert node ID to array index  
static int node_id_to_index(uint8_t node_id) {
    switch (node_id) {
        case NODE_FL:   return 0;  // '2'
        case NODE_FR:   return 1;  // '3' 
        case NODE_4:    return 2;  // '4'
        case NODE_5:    return 3;  // '5'
        case NODE_6:    return 4;  // '6'
        case NODE_7:    return 5;  // '7'
        case NODE_8:    return 6;  // '8'
        case NODE_9:    return 7;  // '9'
        case NODE_10:   return 8;  // '0'
        case NODE_INIT: return 9;  // '1' (initiator - 10th/last position)
        default: return -1;
    }
}

// Helper function to update RX statistics for a specific node
static void update_node_rx_stats(uint8_t node_id, bool sent_ack) {
    // Find existing node or create new entry
    int node_index = -1;
    for (int i = 0; i < current_period_stats.active_nodes_count; i++) {
        if (current_period_stats.node_stats[i].node_id == node_id) {
            node_index = i;
            break;
        }
    }
    
    // If node not found, create new entry
    if (node_index == -1 && current_period_stats.active_nodes_count < 10) {
        node_index = current_period_stats.active_nodes_count;
        current_period_stats.node_stats[node_index].node_id = node_id;
        current_period_stats.node_stats[node_index].packets_received = 0;
        current_period_stats.node_stats[node_index].acks_sent = 0;
        current_period_stats.active_nodes_count++;
    }
    
    // Update statistics
    if (node_index >= 0) {
        current_period_stats.node_stats[node_index].packets_received++;
        if (sent_ack) {
            current_period_stats.node_stats[node_index].acks_sent++;
        }
    }
    
    current_period_stats.total_rx_packets++;
}

// Message queues (priority order: urgent → normal → relay → retrans)
message_queue_t urgent_queue = {0};   // Aperiodic, highest priority
message_queue_t normal_queue = {0};   // Periodic messages, 1st period에서 모두 TX를 성공하는 게 best case. 여기서 실패하면 retransmission queue로
message_queue_t relay_queue = {0};    // FL/FR messages for relay, 2+ period에서부터 relay 해주기. 여기서 실패하는 것도 retransmission queue로
message_queue_t retrans_queue = {0};  // Retransmissions (old data) 

// ACK tracking
ack_tracker_t ack_tracker;

// ACK status tracking for FL/FR relay feedback
typedef struct {
    uint8_t fl_ack_status[MAX_NODES];  // ACK status for FL's messages to other nodes
    uint8_t fr_ack_status[MAX_NODES];  // ACK status for FR's messages to other nodes
    bool fl_has_pending_status;        // FL has ACK status to receive
    bool fr_has_pending_status;        // FR has ACK status to receive
} relay_ack_status_t;

static relay_ack_status_t relay_ack_status = {0};

// Buffers
static uint8_t tx_msg[MAX_PACKET_SIZE] = { 
    0xC5,        // Frame type
    0,           // Seq num
    NODE_INIT,   // Source id (this node)
    NODE_ALL,    // Dest id (broadcast by default)
    MSG_TYPE_DATA, // Message type
    0,           // Priority
    0, 0,        // Original source/dest for relay
    0, 0,        // Reserved
    0, 0,        // Topic
    [IDX_PAYLOAD ... (MAX_PACKET_SIZE-1)] = 0x00
};

static uint8_t rx_buffer[FRAME_LEN_MAX];
static char debug_str[200];
//static uint8_t tx_msg[TX_MSG_SIZE] = {
//  0xC5,      // frame type (MAC Header)
//  0,         // seq (나중에 ++) (Mac Header)
//  'D','E','C','A','W','A','V','E', // device ID (MAC Header)
//  0,0,       // topic placeholder
//  [12 ... TX_MSG_SIZE-1] = 0 // 나머지 0으로 초기화
//};

/* Index to access to sequence number of the blink frame in the tx_msg array. */
#define BLINK_FRAME_SN_IDX 1

#define FRAME_LENGTH (sizeof(tx_msg) + FCS_LEN) // The real length that is going to be transmitted

/* Inter-frame delay period, in milliseconds. */
#define TX_DELAY_US 200

/* Values for the PG_DELAY and TX_POWER registers reflect the bandwidth and power of the spectrum at the current
 * temperature. These values can be calibrated prior to taking reference measurements. See NOTE 2 below. */
extern dwt_txconfig_t txconfig_options;


/* ========== DWT Timer Functions (Non-blocking) ========== */

// Initialize DWT timer hardware
static void dwt_timer_init(void) {
    // Enable trace and debug blocks (including DWT)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    // Enable cycle counter in DWT
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // Reset cycle counter
    DWT->CYCCNT = 0;

    test_run_info((unsigned char *)"DWT timer initialized for non-blocking timing");
}

// Start a DWT timer (non-blocking)
static void dwt_timer_start(dwt_timer_t* timer, uint32_t duration_us) {
    if (timer == NULL) return;

    timer->start_cycles = DWT->CYCCNT;
    timer->target_cycles = duration_us * CYCLES_PER_US;
    timer->active = true;
}

// Check if DWT timer has expired (non-blocking)
static bool dwt_timer_is_expired(dwt_timer_t* timer) {
    if (timer == NULL || !timer->active) return false;

    uint32_t elapsed_cycles = DWT->CYCCNT - timer->start_cycles;
    return elapsed_cycles >= timer->target_cycles;
}

// Get elapsed time in microseconds
static uint32_t dwt_timer_get_elapsed_us(dwt_timer_t* timer) {
    if (timer == NULL || !timer->active) return 0;

    uint32_t elapsed_cycles = DWT->CYCCNT - timer->start_cycles;
    return elapsed_cycles / CYCLES_PER_US;
}

// Stop/deactivate a DWT timer
static void dwt_timer_stop(dwt_timer_t* timer) {
    if (timer != NULL) {
        timer->active = false;
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

/* ========== ACK Tracking Functions ========== */

// Reset ACK tracker
static void reset_ack_tracker(void) {
    memset(&ack_tracker, 0, sizeof(ack_tracker_t));
}

// Mark node as pending ACK
static void mark_ack_pending(uint8_t node_id) {
    uint8_t idx = node_id - '0';  // Convert char to index
    if (node_id >= '1' && node_id <= '9') {
        idx = node_id - '1';
    } else if (node_id == '0') {
        idx = 9;
    } else {
        return;  // Invalid node ID
    }
    
    if (idx < MAX_NODES) {
        ack_tracker.pending_nodes[idx] = 1;
        ack_tracker.ack_received[idx] = 0;
        ack_tracker.timeout_timestamp[idx] = dwt_readsystimestamphi32();
    }
}

// Mark ACK as received
static void mark_ack_received(uint8_t node_id) {
    uint8_t idx = node_id - '0';
    if (node_id >= '1' && node_id <= '9') {
        idx = node_id - '1';
    } else if (node_id == '0') {
        idx = 9;
    } else {
        return;
    }
    
    if (idx < MAX_NODES) {
        ack_tracker.ack_received[idx] = 1;
    }
}

// Check if all expected ACKs received
static bool check_all_acks_received(void) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (ack_tracker.pending_nodes[i] && !ack_tracker.ack_received[i]) {
            return false;
        }
    }
    return true;
}

/* ========== Timer Helper Functions ========== */

// Timer functions now use interrupt-based nrf_drv_timer
// No polling functions needed - interrupts handle timer events automatically

/* ========== Helper Functions ========== */

// Check for RX event
static bool check_rx_event(void) {
    return (dwt_readsysstatuslo() & DWT_INT_RXFCG_BIT_MASK) != 0;
}

// Send ACK to specific node
static void send_ack(uint8_t to_node) {
    uint8_t ack_msg[20] = {0};
    ack_msg[IDX_FTYPE] = 0xC5;
    ack_msg[IDX_SOURCE] = NODE_INIT;
    ack_msg[IDX_DEST] = to_node;
    ack_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK;
    ack_msg[IDX_SEQ] = period_count & 0xFF;
    
    snprintf(debug_str, sizeof(debug_str), "Sending ACK to Node %c", to_node);
    test_run_info((unsigned char *)debug_str);
    
    dwt_writetxdata(20, ack_msg, 0);
    dwt_starttx(DWT_START_TX_IMMEDIATE);
    waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    
    test_run_info((unsigned char *)"ACK sent successfully");
}

// Send SYNC signal to all nodes
static void send_sync_signal(void) {
    test_run_info((unsigned char *)"Setting up SYNC message...");
    
    // Small delay for stability (like other examples)
    nrf_delay_us(GUARD_TIME_US);
    
    tx_msg[IDX_SOURCE] = NODE_INIT;
    tx_msg[IDX_DEST] = NODE_ALL;
    tx_msg[IDX_MSG_TYPE] = MSG_TYPE_SYNC;
    tx_msg[IDX_SEQ] = period_count & 0xFF;
    tx_msg[IDX_PRIORITY] = 0;  // High priority
    
    // SYNC message is much smaller than full frame - only header + minimal payload
    uint16_t sync_msg_len = 20;  // Small SYNC message size
    
    dwt_writetxdata(sync_msg_len - FCS_LEN, tx_msg, 0);
    
    dwt_writetxfctrl(sync_msg_len, 0, 0);
    
    dwt_starttx(DWT_START_TX_IMMEDIATE);
    
    /* Poll DW IC until TX frame sent event set. */
    waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

    /* Clear TX frame sent event. */
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    
    test_run_info((unsigned char *)"TX complete!");
    
    // Update statistics
    protocol_stats.sync_messages_sent++;
    
    snprintf(debug_str, sizeof(debug_str), "SYNC #%lu sent", period_count);
    test_run_info((unsigned char *)debug_str);
}

// Send message from queue
static void send_message_from_queue(message_t* msg) {
    // Copy message to TX buffer
    memcpy(tx_msg, msg->msg, MAX_PACKET_SIZE);
    
    // Set sequence number
    tx_msg[IDX_SEQ] = period_count & 0xFF;
    
    // Mark ACKs as pending for the destination
    // SYNC messages don't need ACKs, but DATA messages to NODE_ALL do
    uint8_t msg_type = tx_msg[IDX_MSG_TYPE];
    if (msg_type != MSG_TYPE_SYNC) {
        if (msg->dest_id == NODE_ALL) {
            // NODE_ALL for DATA messages means expect ACK from the one active normal node (NODE_6 for now)
            mark_ack_pending(NODE_6);  // For simple test with one normal node
            protocol_stats.acks_expected++;
            current_period_stats.tx_acks_expected++;
            
            // Mark ACK as expected in period stats
            int node_index = node_id_to_index(NODE_6);
            if (node_index >= 0) {
                current_period_stats.ack_expected[node_index] = 1;
            }
        } else {
            // Regular unicast
            mark_ack_pending(msg->dest_id);
            protocol_stats.acks_expected++;
            current_period_stats.tx_acks_expected++;
            
            // Mark ACK as expected in period stats
            int node_index = node_id_to_index(msg->dest_id);
            if (node_index >= 0) {
                current_period_stats.ack_expected[node_index] = 1;
            }
        }
    }
    
    // Update statistics based on message type and period
    if (msg_type == MSG_TYPE_RELAY) {
        protocol_stats.relay_messages_sent++;
    } else if (msg_type == MSG_TYPE_ACK_STATUS) {
        protocol_stats.ack_status_sent++;
    } else {
        protocol_stats.data_messages_sent++;
    }
    
    // Update per-period statistics
    switch (current_period_mode) {
        case PERIOD_MODE_DATA_TX: protocol_stats.period1_tx_count++; break;
        case PERIOD_MODE_RELAY_TX: protocol_stats.period2_tx_count++; break;
        case PERIOD_MODE_ACK_STATUS: protocol_stats.period3_tx_count++; break;
        case PERIOD_MODE_RETRANS: protocol_stats.period4plus_tx_count++; break;
    }
    
    current_period_stats.tx_packets_sent++;
    
    // Clear status registers first
    dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR | DWT_INT_RXFCG_BIT_MASK | DWT_INT_TXFRS_BIT_MASK);
    
    
    dwt_forcetrxoff();
    nrf_delay_us(100);  // <- 조절해봐야함 휴리스틱
    
    // Transmit
    dwt_writetxdata(FRAME_LENGTH - FCS_LEN, tx_msg, 0);
    dwt_writetxfctrl(FRAME_LENGTH, 0, 0);
    dwt_starttx(DWT_START_TX_IMMEDIATE);

    // Wait for TX to complete
    waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
}

// Wait for ACKs with timeout
static void wait_for_acks(uint32_t timeout_us) {
    uint32_t start;
    uint32_t frame_len;
    start = dwt_readsystimestamphi32();
    
    while ((dwt_readsystimestamphi32() - start) < timeout_us) {
        if (check_rx_event()) {
            frame_len = dwt_getframelength(0);
            if (frame_len <= 20) {  // ACK is small
                dwt_readrxdata(rx_buffer, frame_len, 0);
                
                if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_ACK && 
                    rx_buffer[IDX_DEST] == NODE_INIT) {
                    mark_ack_received(rx_buffer[IDX_SOURCE]);
                    protocol_stats.acks_received++;
                    current_period_stats.tx_acks_received++;
                    
                    // Mark ACK as received in period stats
                    int node_index = node_id_to_index(rx_buffer[IDX_SOURCE]);
                    if (node_index >= 0) {
                        current_period_stats.ack_status[node_index] = 1;
                    }
                }
            }
            
            // Clear RX flag and re-enable
            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
        
        // Check if all ACKs received
        if (check_all_acks_received()) {
            break;
        }
    }
}

/* ========== RX Message Handling and Relay Logic ========== */

// Store FL/FR message for later relay (used in 1st period)
static void store_fl_fr_message_for_relay(uint8_t* buffer, uint32_t len) {
    message_t relay_msg;
    
    // Copy message data
    memcpy(relay_msg.msg, buffer, MAX_PACKET_SIZE);
    relay_msg.dest_id = buffer[IDX_DEST];
    relay_msg.priority = 1;  // Normal priority for relay
    relay_msg.retry_count = 0;
    relay_msg.timestamp = dwt_readsystimestamphi32();
    
    // Add to relay queue for processing in 2nd+ periods
    if (!enqueue(&relay_queue, &relay_msg)) {
        snprintf(debug_str, sizeof(debug_str), "Relay queue full! FL/FR msg dropped");
        test_run_info((unsigned char *)debug_str);
    } else {
        protocol_stats.relay_messages_stored++;
        current_period_stats.relay_stored++;
        uint8_t source = buffer[IDX_SOURCE];
        uint8_t dest = buffer[IDX_DEST];
        snprintf(debug_str, sizeof(debug_str), "FL/FR %c->%c stored for relay", source, dest);
        test_run_info((unsigned char *)debug_str);
        
        // Mark that source node has pending ACK status to receive
        if (source == NODE_FL) {
            relay_ack_status.fl_has_pending_status = true;
        } else if (source == NODE_FR) {
            relay_ack_status.fr_has_pending_status = true;
        }
    }
}

// Create ACK status message for FL/FR
static bool create_ack_status_message(uint8_t target_node, message_t* ack_status_msg) {
    if (target_node != NODE_FL && target_node != NODE_FR) {
        return false;
    }
    
    // Check if this node has pending ACK status
    bool has_status = false;
    if (target_node == NODE_FL && relay_ack_status.fl_has_pending_status) {
        has_status = true;
    } else if (target_node == NODE_FR && relay_ack_status.fr_has_pending_status) {
        has_status = true;
    }
    
    if (!has_status) {
        return false;
    }
    
    // Create ACK status message
    memset(ack_status_msg, 0, sizeof(message_t));
    ack_status_msg->dest_id = target_node;
    ack_status_msg->priority = 0;  // High priority
    ack_status_msg->retry_count = 0;
    ack_status_msg->timestamp = dwt_readsystimestamphi32();
    
    // Set up message header
    ack_status_msg->msg[IDX_FTYPE] = 0xC5;
    ack_status_msg->msg[IDX_SEQ] = period_count & 0xFF;
    ack_status_msg->msg[IDX_SOURCE] = NODE_INIT;
    ack_status_msg->msg[IDX_DEST] = target_node;
    ack_status_msg->msg[IDX_MSG_TYPE] = MSG_TYPE_ACK_STATUS;
    ack_status_msg->msg[IDX_PRIORITY] = 0;
    
    // Copy ACK status array to payload (simplified: just copy current period stats)
    memcpy(&ack_status_msg->msg[IDX_PAYLOAD], current_period_stats.ack_status, 10);
    
    snprintf(debug_str, sizeof(debug_str), "ACK status msg created for %c", target_node);
    test_run_info((unsigned char *)debug_str);
    
    return true;
}

// Mark ACK status as sent
static void mark_ack_status_sent(uint8_t target_node) {
    if (target_node == NODE_FL) {
        relay_ack_status.fl_has_pending_status = false;
        memset(relay_ack_status.fl_ack_status, 0, sizeof(relay_ack_status.fl_ack_status));
    } else if (target_node == NODE_FR) {
        relay_ack_status.fr_has_pending_status = false;
        memset(relay_ack_status.fr_ack_status, 0, sizeof(relay_ack_status.fr_ack_status));
    }
}

// Handle received message with period-aware logic
static void handle_rx_message(uint8_t* buffer, uint32_t len) {
    uint8_t source = buffer[IDX_SOURCE];
    uint8_t dest = buffer[IDX_DEST];
    uint8_t msg_type = buffer[IDX_MSG_TYPE];
    
    // Handle FL/FR messages with period-aware logic
    if (source == NODE_FL || source == NODE_FR) {
        // Update global statistics
        if (source == NODE_FL) {
            protocol_stats.fl_messages_received++;
        } else {
            protocol_stats.fr_messages_received++;
        }
        
        if (dest == NODE_INIT) {
            // Message directed to us (the initiator) - always process immediately
            update_node_rx_stats(source, true);  // We'll send ACK
            
            process_own_message(buffer, len);
            send_ack(source);
            
            snprintf(debug_str, sizeof(debug_str), "Msg from %c processed", source);
            test_run_info((unsigned char *)debug_str);
            
        } else {
            // Messages for other nodes - handle based on period
            if (is_first_period_in_cycle) {
                // 1st Period: Store for relay in 2nd+ periods (no immediate relay)
                store_fl_fr_message_for_relay(buffer, len);
                // Note: We don't send ACK back to FL/FR in this case
            } else {
                // 2nd+ Periods: Should not receive new FL/FR messages (they only TX in 1st)
                snprintf(debug_str, sizeof(debug_str), "Unexpected FL/FR msg in 2nd+ period from %c", source);
                test_run_info((unsigned char *)debug_str);
            }
        }
    }
    // Handle ACK messages
    else if (msg_type == MSG_TYPE_ACK) {
        if (dest == NODE_INIT) {
            // ACK directed to us
            mark_ack_received(source);
            protocol_stats.acks_received++;
            current_period_stats.tx_acks_received++;
            
            // Mark ACK as received in period stats
            int node_index = node_id_to_index(source);
            if (node_index >= 0) {
                current_period_stats.ack_status[node_index] = 1;
            }
        }
    }
    // Handle messages directed to us
    else if (dest == NODE_INIT) {
        update_node_rx_stats(source, true);  // We'll send ACK
        
        process_own_message(buffer, len);
        send_ack(source);
        
        snprintf(debug_str, sizeof(debug_str), "Direct msg from %c", source);
        test_run_info((unsigned char *)debug_str);
    }
    // Handle messages directed to FL/FR (need relay) - 1st period only
    else if ((dest == NODE_FL || dest == NODE_FR) && is_first_period_in_cycle) {
        update_node_rx_stats(source, true);  // We'll send ACK
        
        // Store for relay in 2nd+ periods
        store_fl_fr_message_for_relay(buffer, len);
        // Send ACK to original sender
        send_ack(source);
    }
    // Handle other messages
    else {
        update_node_rx_stats(source, true);  // We'll send ACK
        
        // 지금은 일단 ACK
        send_ack(source);
    }
}

// Process message directed to this node (initiator)
static void process_own_message(uint8_t* buffer, uint32_t len) {
    uint8_t msg_type = buffer[IDX_MSG_TYPE];
    uint8_t source = buffer[IDX_SOURCE];
    
    // Increment reliability counter
    messages_received_count++;
    
    // Log reliability statistics every 100 messages
    if (messages_received_count % 100 == 0) {
        snprintf(debug_str, sizeof(debug_str), "RX reliability: %lu messages received", messages_received_count);
        test_run_info((unsigned char *)debug_str);
    }
    
    switch (msg_type) {
        case MSG_TYPE_DATA:
            // Handle data message
            snprintf(debug_str, sizeof(debug_str), "Data from %c received", source);
            test_run_info((unsigned char *)debug_str);
            break;
            
        case MSG_TYPE_URGENT:
            // Handle urgent message
            snprintf(debug_str, sizeof(debug_str), "URGENT from %c received", source);
            test_run_info((unsigned char *)debug_str);
            break;
            
        case MSG_TYPE_SYNC:
            // Another node sending SYNC? Should not happen normally
            snprintf(debug_str, sizeof(debug_str), "Unexpected SYNC from %c", source);
            test_run_info((unsigned char *)debug_str);
            break;
            
        default:
            break;
    }
}

/* ========== Period State Management ========== */

// Update period state management for 5-period cycle (3+2 retrans)
static void update_period_state(void) {
    // Calculate 5-period cycle position
    period_in_cycle = ((period_count - 1) % 5) + 1;  // 1-5
    cycle_number = (period_count - 1) / 5;           // 0-based cycle number
    
    // Set period mode based on position in cycle
    if (period_in_cycle == 1) {
        current_period_mode = PERIOD_MODE_DATA_TX;
    } else if (period_in_cycle == 2) {
        current_period_mode = PERIOD_MODE_RELAY_TX;
    } else if (period_in_cycle == 3) {
        current_period_mode = PERIOD_MODE_ACK_STATUS;
    } else {
        current_period_mode = PERIOD_MODE_RETRANS;  // Period 4 and 5
    }
    
    // Compatibility flag for existing code
    is_first_period_in_cycle = (current_period_mode == PERIOD_MODE_DATA_TX);
    
    const char* mode_names[] = {"DATA_TX", "RELAY_TX", "ACK_STATUS", "RETRANS"};
    snprintf(debug_str, sizeof(debug_str), "Period #%lu - Cycle %lu, P%lu: %s", 
             period_count, cycle_number, period_in_cycle, mode_names[current_period_mode - 1]);
    test_run_info((unsigned char *)debug_str);
}

// Print basic protocol statistics
static void print_protocol_stats(void) {
    const char* mode_names[] = {"DATA_TX", "RELAY_TX", "ACK_STATUS", "RETRANS"};
    snprintf(debug_str, sizeof(debug_str), 
             "Stats C:%lu P:%lu U:%d N:%d Rel:%d R:%d [%s]", 
             cycle_number, period_count, urgent_queue.count, normal_queue.count, 
             relay_queue.count, retrans_queue.count, mode_names[current_period_mode - 1]);
    test_run_info((unsigned char *)debug_str);
}

// Print detailed protocol statistics
static void print_detailed_stats(void) {
    test_run_info((unsigned char *)"=== Detailed Protocol Statistics ===");
    
    snprintf(debug_str, sizeof(debug_str), "Cycles completed: %lu", cycle_number);
    test_run_info((unsigned char *)debug_str);
    
    snprintf(debug_str, sizeof(debug_str), "Periods completed: %lu", period_count);
    test_run_info((unsigned char *)debug_str);
    
    snprintf(debug_str, sizeof(debug_str), "SYNC messages sent: %lu", protocol_stats.sync_messages_sent);
    test_run_info((unsigned char *)debug_str);
    
    test_run_info((unsigned char *)"--- Per-Period Statistics ---");
    snprintf(debug_str, sizeof(debug_str), "Period 1 (DATA_TX): %lu transmissions", protocol_stats.period1_tx_count);
    test_run_info((unsigned char *)debug_str);
    
    snprintf(debug_str, sizeof(debug_str), "Period 2 (RELAY_TX): %lu transmissions", protocol_stats.period2_tx_count);
    test_run_info((unsigned char *)debug_str);
    
    snprintf(debug_str, sizeof(debug_str), "Period 3 (ACK_STATUS): %lu transmissions", protocol_stats.period3_tx_count);
    test_run_info((unsigned char *)debug_str);
    
    snprintf(debug_str, sizeof(debug_str), "Period 4+ (RETRANS): %lu transmissions", protocol_stats.period4plus_tx_count);
    test_run_info((unsigned char *)debug_str);
    
    test_run_info((unsigned char *)"--- Message Type Statistics ---");
    snprintf(debug_str, sizeof(debug_str), "Data messages sent: %lu", protocol_stats.data_messages_sent);
    test_run_info((unsigned char *)debug_str);
    
    snprintf(debug_str, sizeof(debug_str), "Relay messages sent: %lu", protocol_stats.relay_messages_sent);
    test_run_info((unsigned char *)debug_str);
    
    snprintf(debug_str, sizeof(debug_str), "ACK status messages sent: %lu", protocol_stats.ack_status_sent);
    test_run_info((unsigned char *)debug_str);
    
    snprintf(debug_str, sizeof(debug_str), "Retransmissions sent: %lu", protocol_stats.retransmissions_sent);
    test_run_info((unsigned char *)debug_str);
    
    test_run_info((unsigned char *)"--- ACK Statistics ---");
    snprintf(debug_str, sizeof(debug_str), "ACKs expected: %lu, received: %lu", protocol_stats.acks_expected, protocol_stats.acks_received);
    test_run_info((unsigned char *)debug_str);
    
    if (protocol_stats.acks_expected > 0) {
        uint32_t ack_success_rate = (protocol_stats.acks_received * 100) / protocol_stats.acks_expected;
        snprintf(debug_str, sizeof(debug_str), "ACK success rate: %lu%%", ack_success_rate);
        test_run_info((unsigned char *)debug_str);
    }
    
    test_run_info((unsigned char *)"--- FL/FR Statistics ---");
    snprintf(debug_str, sizeof(debug_str), "FL messages received: %lu", protocol_stats.fl_messages_received);
    test_run_info((unsigned char *)debug_str);
    
    snprintf(debug_str, sizeof(debug_str), "FR messages received: %lu", protocol_stats.fr_messages_received);
    test_run_info((unsigned char *)debug_str);
    
    snprintf(debug_str, sizeof(debug_str), "Relay messages stored: %lu", protocol_stats.relay_messages_stored);
    test_run_info((unsigned char *)debug_str);
    
    test_run_info((unsigned char *)"======================================");
}

// Print period summary
static void print_period_summary(void) {
    test_run_info((unsigned char *)"=== Period Summary ===");
    
    // RX Phase Summary
    if (current_period_stats.total_rx_packets > 0) {
        snprintf(debug_str, sizeof(debug_str), "RX: Received %lu packets from %d nodes:", 
                current_period_stats.total_rx_packets, current_period_stats.active_nodes_count);
        test_run_info((unsigned char *)debug_str);
        
        for (int i = 0; i < current_period_stats.active_nodes_count; i++) {
            node_rx_stats_t *node = &current_period_stats.node_stats[i];
            snprintf(debug_str, sizeof(debug_str), "  Node %c: %lu packets, %lu ACKs sent", 
                    node->node_id, node->packets_received, node->acks_sent);
            test_run_info((unsigned char *)debug_str);
        }
        
        if (current_period_stats.relay_stored > 0) {
            snprintf(debug_str, sizeof(debug_str), "  Relay stored: %lu messages", current_period_stats.relay_stored);
            test_run_info((unsigned char *)debug_str);
        }
    } else {
        test_run_info((unsigned char *)"RX: No packets received");
    }
    
    // TX Phase Summary
    if (current_period_stats.tx_packets_sent > 0) {
        snprintf(debug_str, sizeof(debug_str), "TX: Sent %lu packets, expected %lu ACKs, received %lu ACKs", 
                current_period_stats.tx_packets_sent, current_period_stats.tx_acks_expected, current_period_stats.tx_acks_received);
        test_run_info((unsigned char *)debug_str);
        
        // Show ACK status array: [FL FR N4 N5 N6 N7 N8 N9 N10 IN]
        char ack_str[64] = "  ACK Status: [";
        char node_names[] = {'F', 'R', '4', '5', '6', '7', '8', '9', '0', 'I'};
        for (int i = 0; i < 10; i++) {
            if (current_period_stats.ack_expected[i]) {
                strcat(ack_str, current_period_stats.ack_status[i] ? "1" : "0");
            } else {
                strcat(ack_str, "-");  // No ACK expected
            }
            if (i < 9) strcat(ack_str, " ");
        }
        strcat(ack_str, "]");
        test_run_info((unsigned char *)ack_str);
    } else {
        test_run_info((unsigned char *)"TX: No packets sent");
    }
    
    // Buffer Status
    snprintf(debug_str, sizeof(debug_str), "Buffers: Normal=%lu, Relay=%lu, Retrans=%lu", 
            current_period_stats.normal_queue_remaining, 
            current_period_stats.relay_queue_remaining, 
            current_period_stats.retrans_queue_remaining);
    test_run_info((unsigned char *)debug_str);
    
    test_run_info((unsigned char *)"======================");
}

// Check for missing ACKs and update period stats
static void update_missing_acks(void) {
    // ACK tracking is now handled in the ack_status and ack_expected arrays
    // No additional processing needed - arrays are updated in real-time
}

// Reset period statistics for next period
static void reset_period_stats(void) {
    memset(&current_period_stats, 0, sizeof(current_period_stats));
}

/**
 * Application entry point.
 */
int UWB_protocol_init(void)
{
#if USE_SPI2
    uint8_t sema_res;
#endif
    uint32_t dev_id;
    uint32_t status_reg;
    uint32_t frame_len;

    /* Display application name on LCD. */
    test_run_info((unsigned char *)APP_NAME);

    /* Configure SPI rate, DW3000 supports up to 38 MHz */
    port_set_dw_ic_spi_fastrate();

    /* Reset DW IC */
    reset_DWIC(); /* Target specific drive of RSTn line into DW IC low for a period. */

    Sleep(2); // Time needed for DW3000 to start up (transition from INIT_RC to IDLE_RC, or could wait for SPIRDY event)

    /* Probe for the correct device driver. */
    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    dev_id = dwt_readdevid();
    if (dev_id == (uint32_t)DWT_DW3720_PDOA_DEV_ID)
    {
        /* If host is using SPI 2 to connect to DW3000 the code in the USE_SPI2 above should be set to 1 */
#if USE_SPI2
        change_SPI(SPI_2);

        /* Configure SPI rate, DW3000 supports up to 38 MHz */
        port_set_dw_ic_spi_fastrate();

        /* Reset DW IC */
        reset_DWIC(); /* Target specific drive of RSTn line into DW IC low for a period. */

        Sleep(2); // Time needed for DW3000 to start up (transition from INIT_RC to IDLE_RC, or could wait for SPIRDY event)

        /* If host is using SPI 2 to connect to DW3000 the it needs to request access or force access */

        sema_res = dwt_ds_sema_status();

        if ((sema_res & (0x2)) == 0) // the SPI2 is free
        {
            dwt_ds_sema_request();
        }
        else
        {
            test_run_info((unsigned char *)"SPI2 IS NOT FREE"); // If SPI2 is not free the host can force access
            while (1) { };
        }
#endif
    }

    while (!dwt_checkidlerc()) /* Need to make sure DW IC is in IDLE_RC before proceeding */ { };

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        test_run_info((unsigned char *)"INIT FAILED     ");
        while (1) { };
    }

    /* If host is using SPI 2 to connect to DW3000 then the GPIOs are used for SPI2 and LEDs functionality cannot be used */
#if USE_SPI2 == 0
    /* Enabling LEDs here for debug so that for each TX the D1 LED will flash on DW3000 red eval-shield boards. */
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
#endif

    /* Configure DW IC. See NOTE 5 below. */
    /* if the dwt_configure returns DWT_ERROR either the PLL or RX calibration has failed the host should reset the device */
    if (dwt_configure(&config))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    /* Configure the TX spectrum parameters (power PG delay and PG Count) */
    dwt_configuretxrf(&txconfig_options);
    
    /* Initialize DWT-based non-blocking timers */
    dwt_timer_init();

    // Start the first period timer (20ms)
    dwt_timer_start(&period_timer, PERIOD_MS * 1000);  // 20ms = 20,000us
    
    /* Initialize protocol state */
    current_state = STATE_TX_SYNC;
    reset_ack_tracker();
    
    test_run_info((unsigned char *)"Protocol initialization complete");
    
    // Add a test message to normal queue for TX testing
    message_t test_msg = {0};
    test_msg.dest_id = NODE_ALL;  // Broadcast to all nodes
    test_msg.priority = 1;       // Normal priority
    test_msg.retry_count = 0;
    test_msg.timestamp = dwt_readsystimestamphi32();
    
    // Set up test message
    test_msg.msg[IDX_FTYPE] = 0xC5;
    test_msg.msg[IDX_SEQ] = 1;
    test_msg.msg[IDX_SOURCE] = NODE_INIT;
    test_msg.msg[IDX_DEST] = NODE_FL;
    test_msg.msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
    test_msg.msg[IDX_PRIORITY] = 1;
    
    // Add test payload
    strcpy((char*)&test_msg.msg[IDX_PAYLOAD], "Test message from Initiator");
    
    if (enqueue(&normal_queue, &test_msg)) {
        test_run_info((unsigned char *)"Test message added to normal queue");
    } else {
        test_run_info((unsigned char *)"Failed to add test message to queue");
    }
    
    Sleep(15000); // 15 seconds delay before starting protocol
    
    /* ========== Main Protocol Loop ========== */
    test_run_info((unsigned char *)"Starting UWB Protocol Initiator");
    
    
    
    while (1) {        
        
        // Period timer check (DWT non-blocking)
        if (dwt_timer_is_expired(&period_timer)) {
            period_count++;

            // Update period state for new period
            update_period_state();

            snprintf(debug_str, sizeof(debug_str), "Period #%lu expired! Switching to TX_SYNC", period_count);
            test_run_info((unsigned char *)debug_str);

            // Restart period timer for next 20ms period
            dwt_timer_start(&period_timer, PERIOD_MS * 1000);
            current_state = STATE_TX_SYNC;

            snprintf(debug_str, sizeof(debug_str), "State changed to TX_SYNC (%d)", current_state);
            test_run_info((unsigned char *)debug_str);
        }
        
        switch (current_state) {
            case STATE_TX_SYNC:
                // Print previous period summary and reset stats
                if (period_count > 1) {  // Skip first period (no previous data)
                    current_period_stats.normal_queue_remaining = normal_queue.count;
                    current_period_stats.relay_queue_remaining = relay_queue.count;
                    current_period_stats.retrans_queue_remaining = retrans_queue.count;
                    print_period_summary();
                }
                reset_period_stats();
                
                // Send SYNC signal to all nodes
                test_run_info((unsigned char *)"Entering STATE_TX_SYNC");
                
                /* Try a gentler approach - just clear status first */
                test_run_info((unsigned char *)"Clearing status registers...");
                dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR | DWT_INT_RXFCG_BIT_MASK | DWT_INT_TXFRS_BIT_MASK);
                
                /* Small delay */
                nrf_delay_us(GUARD_TIME_US);
                
                /* Use force off with better error handling */
                test_run_info((unsigned char *)"Checking transceiver state...");
                if (!dwt_checkidlerc()) {
                    test_run_info((unsigned char *)"Forcing transceiver off (with timeout)...");
                    dwt_forcetrxoff();
                    
                    // Don't wait for IDLE_RC confirmation - just proceed after delay
                    nrf_delay_us(500);  // Give more time but don't wait indefinitely
                    test_run_info((unsigned char *)"Force off delay completed");
                } else {
                    test_run_info((unsigned char *)"Already in IDLE_RC");
                }
                
                send_sync_signal();
                test_run_info((unsigned char *)"SYNC signal sent");
                
                // Start slot interval timer to wait for initiator's turn (10th/last slot)
                // INITIATOR_SLOT_START_US = 18000us = 18ms
                dwt_timer_start(&slot_interval_timer, INITIATOR_SLOT_START_US);
                current_state = STATE_WAIT_MY_SLOT;
                
                // Reset ACK tracker for new period
                reset_ack_tracker();
                
                // Enable RX mode to listen to other nodes during wait  
                test_run_info((unsigned char *)"Enabling RX for WAIT_MY_SLOT...");
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
                test_run_info((unsigned char *)"RX enabled for WAIT_MY_SLOT");
                break;
                
            case STATE_WAIT_MY_SLOT:
                
                
                // Check slot interval timer (non-blocking)
                if (dwt_timer_is_expired(&slot_interval_timer)) {
                    // My slot has started! Start slot duration timer to limit TX time
                    dwt_timer_start(&slot_duration_timer, SLOT_DURATION_US);  // 2000us (2ms)
                    in_my_slot = true;
                    current_state = STATE_TX_ACTIVE;
                    
                    snprintf(debug_str, sizeof(debug_str), "My slot started (%dms)", INITIATOR_SLOT_START_US / 1000);
                    test_run_info((unsigned char *)debug_str);
                }
                
                // Continue listening during wait
                status_reg = dwt_readsysstatuslo();
                if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
                    frame_len = dwt_getframelength(0);
                    if (frame_len <= FRAME_LEN_MAX) {
                        dwt_readrxdata(rx_buffer, frame_len, 0);
                        handle_rx_message(rx_buffer, frame_len);
                    }
                    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                }
                break;
                
            case STATE_TX_ACTIVE:
                // Process message queues during TX slot
                static bool message_sent_this_slot = false;
                static message_t last_sent_msg;  // Store last sent message for potential retransmission

                // Send one message at the start of the slot (based on period mode)
                if (in_my_slot && !dwt_timer_is_expired(&slot_duration_timer) && !message_sent_this_slot) {
                    message_t msg;
                    bool msg_found = false;

                    test_run_info((unsigned char *)"TX slot: Looking for message to send");

                    // Period-specific transmission logic
                    switch (current_period_mode) {
                        case PERIOD_MODE_DATA_TX:
                            // Period 1: Urgent -> Normal (no relay/retrans)
                            if (dequeue(&urgent_queue, &msg)) {
                                msg_found = true;
                                snprintf(debug_str, sizeof(debug_str), "P1: Urgent msg sent to %c", msg.dest_id);
                            } else if (dequeue(&normal_queue, &msg)) {
                                msg_found = true;
                                snprintf(debug_str, sizeof(debug_str), "P1: Normal msg sent to %c", msg.dest_id);
                            }
                            break;

                        case PERIOD_MODE_RELAY_TX:
                            // Period 2: Urgent -> Relay (relay has higher priority than retrans)
                            if (dequeue(&urgent_queue, &msg)) {
                                msg_found = true;
                                snprintf(debug_str, sizeof(debug_str), "P2: Urgent msg sent to %c", msg.dest_id);
                            } else if (dequeue(&relay_queue, &msg)) {
                                msg_found = true;
                                snprintf(debug_str, sizeof(debug_str), "P2: Relay msg sent to %c", msg.dest_id);
                            }
                            break;

                        case PERIOD_MODE_ACK_STATUS:
                            // Period 3: Urgent -> ACK status messages to FL/FR
                            if (dequeue(&urgent_queue, &msg)) {
                                msg_found = true;
                                snprintf(debug_str, sizeof(debug_str), "P3: Urgent msg sent to %c", msg.dest_id);
                            } else {
                                // Try to send ACK status to FL first, then FR
                                if (create_ack_status_message(NODE_FL, &msg)) {
                                    msg_found = true;
                                    mark_ack_status_sent(NODE_FL);
                                    snprintf(debug_str, sizeof(debug_str), "P3: ACK status sent to FL");
                                } else if (create_ack_status_message(NODE_FR, &msg)) {
                                    msg_found = true;
                                    mark_ack_status_sent(NODE_FR);
                                    snprintf(debug_str, sizeof(debug_str), "P3: ACK status sent to FR");
                                }
                            }
                            break;

                        case PERIOD_MODE_RETRANS:
                            // Period 4+: Urgent -> Retransmissions
                            if (dequeue(&urgent_queue, &msg)) {
                                msg_found = true;
                                snprintf(debug_str, sizeof(debug_str), "P4+: Urgent msg sent to %c", msg.dest_id);
                            } else if (dequeue(&retrans_queue, &msg)) {
                                protocol_stats.retransmissions_sent++;
                                msg_found = true;
                                snprintf(debug_str, sizeof(debug_str), "P4+: Retrans msg sent to %c", msg.dest_id);
                            }
                            break;
                    }

                    // Send the message if found
                    if (msg_found) {
                        test_run_info((unsigned char *)"TX slot: Sending message");
                        send_message_from_queue(&msg);
                        message_sent_this_slot = true;
                        memcpy(&last_sent_msg, &msg, sizeof(message_t));  // Store for potential retransmission
                        test_run_info((unsigned char *)debug_str);

                        // After sending, enable RX to receive ACKs for the rest of the slot
                        test_run_info((unsigned char *)"TX slot: Enabling RX for ACK reception");
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);
                        test_run_info((unsigned char *)"RX enabled for ACK reception");
                    } else {
                        test_run_info((unsigned char *)"TX slot: No message to send");
                    }
                }
                
                // Process ACKs during the remaining slot time (separate condition)
                if (in_my_slot && !dwt_timer_is_expired(&slot_duration_timer) && message_sent_this_slot) {
                    uint32_t status_reg = dwt_readsysstatuslo();

                    if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
                        uint32_t frame_len = dwt_getframelength(0);
                        if (frame_len <= 20) {  // ACK is small
                            dwt_readrxdata(rx_buffer, frame_len, 0);

                            if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_ACK &&
                                rx_buffer[IDX_DEST] == NODE_INIT) {
                                mark_ack_received(rx_buffer[IDX_SOURCE]);
                                protocol_stats.acks_received++;
                                current_period_stats.tx_acks_received++;

                                // Mark ACK as received in period stats
                                int node_index = node_id_to_index(rx_buffer[IDX_SOURCE]);
                                if (node_index >= 0) {
                                    current_period_stats.ack_status[node_index] = 1;
                                }

                                snprintf(debug_str, sizeof(debug_str), "ACK received from %c", rx_buffer[IDX_SOURCE]);
                                test_run_info((unsigned char *)debug_str);
                            }
                        }

                        // Clear RX flag and re-enable
                        dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);
                    }

                    // Check for RX errors/timeouts
                    if (status_reg & (SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)) {
                        // Clear error flags and re-enable RX
                        dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);
                    }
                }

                // If no messages to send and haven't sent anything, just wait
                if (in_my_slot && !dwt_timer_is_expired(&slot_duration_timer) && !message_sent_this_slot) {
                    test_run_info((unsigned char *)"TX slot: Waiting for message or slot end");
                    nrf_delay_us(100);  // Small delay to prevent busy waiting
                }

                // Check if TX slot duration expired
                if (dwt_timer_is_expired(&slot_duration_timer)) {
                    test_run_info((unsigned char *)"TX_ACTIVE: Slot duration expired!");

                    // Stop timers and reset slot state
                    dwt_timer_stop(&slot_duration_timer);
                    in_my_slot = false;

                    // Update missing ACKs for period summary
                    update_missing_acks();

                    // Process failed transmissions (period-specific retransmission logic)
                    if (message_sent_this_slot) {
                        test_run_info((unsigned char *)"TX_ACTIVE: Processing transmission results");

                        // Check if we got all expected ACKs
                        if (!check_all_acks_received()) {
                            test_run_info((unsigned char *)"TX_ACTIVE: Not all ACKs received, adding to retrans queue");

                            // Add to retransmission queue based on period mode
                            if (last_sent_msg.retry_count < MAX_RETRY) {
                                // Only add to retrans queue if message type is appropriate for retransmission
                                uint8_t msg_type = last_sent_msg.msg[IDX_MSG_TYPE];
                                if (msg_type == MSG_TYPE_DATA || msg_type == MSG_TYPE_RELAY || msg_type == MSG_TYPE_URGENT) {
                                    last_sent_msg.retry_count++;
                                    if (enqueue(&retrans_queue, &last_sent_msg)) {
                                        snprintf(debug_str, sizeof(debug_str),
                                                "P%lu: Message to %c added to retrans queue (retry %d)",
                                                period_in_cycle, last_sent_msg.dest_id, last_sent_msg.retry_count);
                                        test_run_info((unsigned char *)debug_str);
                                    } else {
                                        test_run_info((unsigned char *)"Retrans queue full!");
                                    }
                                }
                            } else {
                                snprintf(debug_str, sizeof(debug_str),
                                        "P%lu: Message to %c exceeded max retries (%d)",
                                        period_in_cycle, last_sent_msg.dest_id, MAX_RETRY);
                                test_run_info((unsigned char *)debug_str);
                            }
                        } else {
                            snprintf(debug_str, sizeof(debug_str), "P%lu: All expected ACKs received", period_in_cycle);
                            test_run_info((unsigned char *)debug_str);
                        }
                    } else {
                        test_run_info((unsigned char *)"TX_ACTIVE: No message was sent this slot");
                    }

                    message_sent_this_slot = false;  // Reset for next slot
                    current_state = STATE_RX_LISTEN;

                    snprintf(debug_str, sizeof(debug_str), "TX slot ended, switching to RX_LISTEN");
                    test_run_info((unsigned char *)debug_str);

                    // Force transceiver off before enabling RX (ensure clean state)
                    test_run_info((unsigned char *)"Forcing transceiver off before RX...");
                    dwt_forcetrxoff();

                    // Small delay
                    nrf_delay_us(10);

                    // Clear any pending RX status before enabling RX
                    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);

                    // Enable RX mode
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                    test_run_info((unsigned char *)"RX enabled successfully!");
                } else {
                    // Debug: Print timer status occasionally
                    static uint32_t timer_debug_count = 0;
                    timer_debug_count++;
                    if (timer_debug_count % 10000 == 0) {
                        snprintf(debug_str, sizeof(debug_str), "TX_ACTIVE: Timer not expired yet (count: %lu)", timer_debug_count / 10000);
                        test_run_info((unsigned char *)debug_str);
                    }
                }
                break;
                
            case STATE_RX_LISTEN:
                
                
                // Check for incoming messages (non-blocking)
                status_reg = dwt_readsysstatuslo();
                
                if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
                    // Frame received successfully
                    frame_len = dwt_getframelength(0);
                    if (frame_len <= FRAME_LEN_MAX) {
                        dwt_readrxdata(rx_buffer, frame_len, 0);
                        handle_rx_message(rx_buffer, frame_len);
                    }
                    
                    // Clear RX flag and re-enable RX
                    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                }
                
                // Check for RX errors/timeouts
                if (status_reg & (SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)) {
                    // Clear error flags and re-enable RX
                    dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                }
                break;
                
            case STATE_WAIT_SYNC:
            default:
                // Should not reach here in normal operation
                current_state = STATE_RX_LISTEN;
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
                break;
        }
        
        // Print statistics every complete cycle (5 periods)
        if (period_in_cycle == 5 && period_count > 0) {
            static uint32_t last_stats_cycle = 0;
            if (cycle_number != last_stats_cycle) {
                snprintf(debug_str, sizeof(debug_str), "=== Cycle %lu Complete ===", cycle_number);
                test_run_info((unsigned char *)debug_str);
                print_protocol_stats();
                last_stats_cycle = cycle_number;
                
                // Update cycle counter
                protocol_stats.cycles_completed = cycle_number;
            }
        }
        
        // Print brief stats every 25 periods for progress tracking
        if (period_count % 25 == 0 && period_count > 0) {
            static uint32_t last_brief_stats = 0;
            if (period_count != last_brief_stats) {
                snprintf(debug_str, sizeof(debug_str), "Progress: %lu periods, %lu cycles", period_count, cycle_number);
                test_run_info((unsigned char *)debug_str);
                last_brief_stats = period_count;
            }
        }
        
        // Small delay to prevent excessive CPU usage
        nrf_delay_us(10);
        
        // Exit after 5000 periods (1000 cycles of 5 periods each)
        if (period_count >= 10) {  // Run for 5000 periods (100 seconds total)
            test_run_info((unsigned char *)"=== 2 Cycles (10 Periods) Completed ===");
            print_detailed_stats();
            
            snprintf(debug_str, sizeof(debug_str), "Total runtime: %lu cycles, %lu periods (%.1f seconds)", 
                     cycle_number + 1, period_count, (period_count * PERIOD_MS) / 1000.0f);
            test_run_info((unsigned char *)debug_str);
            
            test_run_info((unsigned char *)"Protocol test completed");
            break;
        }
    }
}

#endif
/*****************************************************************************************************************************************************
 * NOTES:
 *
 * 1. The device ID is a hard coded constant in the blink to keep the example simple but for a real product every device should have a unique ID.
 *    For development purposes it is possible to generate a DW IC unique ID by combining the Lot ID & Part Number values programmed into the
 *    DW IC during its manufacture. However there is no guarantee this will not conflict with someone else's implementation. We recommended that
 *    customers buy a block of addresses from the IEEE Registration Authority for their production items. See "EUI" in the DW IC User Manual.
 * 2. In a real application, for optimum performance within regulatory limits, it may be necessary to set TX pulse bandwidth and TX power, (using
 *    the dwt_configuretxrf API call) to per device calibrated values saved in the target system or the DW IC OTP memory.
 * 3. dwt_writetxdata() takes the full size of tx_msg as a parameter but only copies (size - 2) bytes as the check-sum at the end of the frame is
 *    automatically appended by the DW IC. This means that our tx_msg could be two bytes shorter without losing any data (but the sizeof would not
 *    work anymore then as we would still have to indicate the full length of the frame to dwt_writetxdata()).
 * 4. We use polled mode of operation here to keep the example as simple as possible, but the TXFRS status event can be used to generate an interrupt.
 *    Please refer to DW IC User Manual for more details on "interrupts".
 * 5. Desired configuration by user may be different to the current programmed configuration. dwt_configure is called to set desired
 *    configuration.
 ****************************************************************************************************************************************************/