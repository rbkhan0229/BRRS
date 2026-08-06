/*! ----------------------------------------------------------------------------
 *  @file    UWB_protocol_normal_node.c
 *  @brief   UWB Protocol Normal Node example code
 *
 * @author Decawave
 *
 * @copyright SPDX-FileCopyrightText: Copyright (c) 2024 Qorvo US, Inc.
 *            SPDX-License-Identifier: LicenseRef-QORVO-2
 *
 */

#include <example_selection.h>

#if defined(TEST_UWB_PROTOCOL_NORMAL_NODE)

#include "deca_probe_interface.h"
#include <deca_device_api.h>
#include <deca_spi.h>
#include <port.h>
#include <shared_defines.h>
#include <shared_functions.h>
#include <string.h>
#include <stdio.h>

// Timer includes - use direct hardware timer
#include <stdint.h>
#include <stdbool.h>
#include "nrf.h"
#include "nrf_delay.h"

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "UWB PROTOCOL NORMAL NODE v1.0"

/* ========== Node ID Definitions ========== */
#define NODE_INIT '1'    // Initiator node 
#define NODE_FL   '2'    // Front Left (needs relay)
#define NODE_FR   '3'    // Front Right (needs relay)
#define NODE_4    '4'    // Normal nodes (this node can be any of these)
#define NODE_5    '5'
#define NODE_6    '6'
#define NODE_7    '7'
#define NODE_8    '8'
#define NODE_9    '9'
#define NODE_10   '0'
#define NODE_ALL  'A'    // Broadcast to all nodes

/* ========== This Node Configuration ========== */
// Node type can be configured at compile time using -DNODE_TYPE=X
// Example: -DNODE_TYPE=FL, -DNODE_TYPE=FR, -DNODE_TYPE=N6, etc.

// Define node type values
#define FL  1
#define FR  2
#define N4  3
#define N5  4
#define N6  5
#define N7  6
#define N8  7
#define N9  8
#define N10 9

#ifndef NODE_TYPE
#define NODE_TYPE N6  // Default to NODE_6 if not specified
#endif

// Configure node ID and slot based on NODE_TYPE
#if NODE_TYPE == FL
    #define THIS_NODE_ID     NODE_FL
    #define THIS_SLOT_NUMBER 0          // FL: Slot 0 (0ms)
    #define NODE_TYPE_NAME   "FL"
#elif NODE_TYPE == FR  
    #define THIS_NODE_ID     NODE_FR
    #define THIS_SLOT_NUMBER 1          // FR: Slot 1 (2ms)
    #define NODE_TYPE_NAME   "FR"
#elif NODE_TYPE == N4
    #define THIS_NODE_ID     NODE_4
    #define THIS_SLOT_NUMBER 2          // N4: Slot 2 (4ms)
    #define NODE_TYPE_NAME   "N4"
#elif NODE_TYPE == N5
    #define THIS_NODE_ID     NODE_5
    #define THIS_SLOT_NUMBER 3          // N5: Slot 3 (6ms)
    #define NODE_TYPE_NAME   "N5"
#elif NODE_TYPE == N6
    #define THIS_NODE_ID     NODE_6
    #define THIS_SLOT_NUMBER 4          // N6: Slot 4 (8ms)
    #define NODE_TYPE_NAME   "N6"
#elif NODE_TYPE == N7
    #define THIS_NODE_ID     NODE_7
    #define THIS_SLOT_NUMBER 5          // N7: Slot 5 (10ms)
    #define NODE_TYPE_NAME   "N7"
#elif NODE_TYPE == N8
    #define THIS_NODE_ID     NODE_8
    #define THIS_SLOT_NUMBER 6          // N8: Slot 6 (12ms)
    #define NODE_TYPE_NAME   "N8"
#elif NODE_TYPE == N9
    #define THIS_NODE_ID     NODE_9
    #define THIS_SLOT_NUMBER 7          // N9: Slot 7 (14ms)
    #define NODE_TYPE_NAME   "N9"
#elif NODE_TYPE == N10
    #define THIS_NODE_ID     NODE_10
    #define THIS_SLOT_NUMBER 8          // N10: Slot 8 (16ms)
    #define NODE_TYPE_NAME   "N10"
#else
    #error "Invalid NODE_TYPE specified. Use FL, FR, N4, N5, N6, N7, N8, N9, or N10"
#endif

/* ========== Message Type Definitions ========== */
#define MSG_TYPE_SYNC      0x01
#define MSG_TYPE_DATA      0x02
#define MSG_TYPE_ACK       0x03
#define MSG_TYPE_URGENT    0x04
#define MSG_TYPE_RELAY     0x05
#define MSG_TYPE_ACK_STATUS 0x06

/* ========== Timing Constants (Fixed Period) - in ms for app_timer ========== */
#define PERIOD_MS        20      // 20ms period to accommodate 2.0ms slots (10 nodes × 2.0ms = 20ms)
#define SLOT_DURATION_US 2000    // 2.0ms per slot to allow ACK reception (increased from 1.1ms)
#define GUARD_TIME_US    200     // 200us guard time
#define ACK_TIMEOUT_MS   1       // 1ms ACK timeout

/* ========== Slot Timing (Node-specific) ========== */
// Slots: FL(0), FR(1), Node4(2), Node5(3), Node6(4), Node7(5), Node8(6), Node9(7), Node10(8), Initiator(9)
#define MY_SLOT_START_US  (THIS_SLOT_NUMBER * SLOT_DURATION_US)  // Calculate my slot start time

/* ========== Protocol Parameters ========== */
#define MAX_NODES        10
#define QUEUE_SIZE       32
#define MAX_RETRY        3
#define MAX_PACKET_SIZE  125  // 127 - 2 (FCS)

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
    STATE_WAIT_SYNC,     // Waiting for SYNC from initiator (default state)
    STATE_WAIT_MY_SLOT,  // Waiting for my slot interval after receiving SYNC
    STATE_TX_ACTIVE,     // TX slot active (limited duration)
    STATE_RX_LISTEN      // RX mode (most of the time)
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
} ack_tracker_t;

/* ========== Forward Declarations ========== */
static void process_own_message(uint8_t* buffer, uint32_t len);
static void update_period_state(void);

/* ========== Global Variables (needed for timer handlers) ========== */
static protocol_state_t current_state = STATE_WAIT_SYNC;  // Normal node starts waiting for SYNC

/* ========== Global Variables ========== */

// Timer constants
#define TIMER_INTERVAL_MS_PERIOD    PERIOD_MS    // Use consistent period timing (20ms)
#define TIMER_INTERVAL_MS_SLOT      ((MY_SLOT_START_US + 500) / 1000)  // My slot wait time in ms

// Hardware timer pointers
static NRF_TIMER_Type* m_timer_period = NRF_TIMER0;  // Unused in normal node - could be used for timeout detection
static NRF_TIMER_Type* m_timer_slot = NRF_TIMER1;    // Slot timers

// Timer flags
static volatile bool slot_interval_timer_expired = false;
static volatile bool slot_duration_timer_expired = false;
static volatile uint32_t period_count = 0;
static volatile bool slot_timer_is_interval = true;  // Track which slot timer is active

// Protocol state
bool in_my_slot = false;
static volatile bool sync_received = false;  // Flag to indicate SYNC detection

// Period cycle tracking (5-period cycle with simplified 3+N structure)
period_mode_t current_period_mode = PERIOD_MODE_DATA_TX;
uint32_t cycle_number = 0;           // Which 1000-cycle iteration we're in
uint32_t period_in_cycle = 1;        // 1-5 within current cycle
bool is_first_period_in_cycle = false;  // Compatibility flag for existing code

// Reliability counter for received messages
static uint32_t messages_received_count = 0;

// Protocol statistics counters
typedef struct {
    uint32_t periods_completed;
    uint32_t sync_messages_received;  // Changed from sent to received
    uint32_t data_messages_sent;
    uint32_t acks_received;
    uint32_t acks_expected;
    uint32_t fl_messages_received;
    uint32_t fr_messages_received;
    uint32_t relay_messages_stored;
    uint32_t relay_messages_sent;
    uint32_t retransmissions_sent;
    uint32_t rx_timeouts;
    uint32_t tx_failures;
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
// FL=0, FR=1, Node4=2, Node5=3, Node6=4, Node7=5, Node8=6, Node9=7, Node10=8, Initiator=9
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
message_queue_t normal_queue = {0};   // Periodic messages
message_queue_t relay_queue = {0};    // FL/FR messages for relay
message_queue_t retrans_queue = {0};  // Retransmissions (old data) 

// ACK tracking
ack_tracker_t ack_tracker;

// Buffers
static uint8_t tx_msg[MAX_PACKET_SIZE] = { 
    0xC5,        // Frame type
    0,           // Seq num
    THIS_NODE_ID,// Source id (this node)
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

/* Index to access to sequence number of the blink frame in the tx_msg array. */
#define BLINK_FRAME_SN_IDX 1

#define FRAME_LENGTH (sizeof(tx_msg) + FCS_LEN) // The real length that is going to be transmitted

/* Inter-frame delay period, in milliseconds. */
#define TX_DELAY_US 200

/* Values for the PG_DELAY and TX_POWER registers reflect the bandwidth and power of the spectrum at the current
 * temperature. These values can be calibrated prior to taking reference measurements. See NOTE 2 below. */
extern dwt_txconfig_t txconfig_options;

/* ========== Timer Helper Functions ========== */

// Check if slot timer expired
static bool check_slot_timer_expired(void) {
    if (m_timer_slot->EVENTS_COMPARE[0]) {
        m_timer_slot->EVENTS_COMPARE[0] = 0; // Clear event
        
        if (slot_timer_is_interval) {
            slot_interval_timer_expired = true;
        } else {
            slot_duration_timer_expired = true;
        }
        return true;
    }
    return false;
}

/* ========== Timer Functions ========== */

// Initialize protocol timers
static void init_protocol_timers(void) {
    // ========== Initialize Slot Timer (for interval and duration) ==========
    m_timer_slot->TASKS_STOP = 1;
    m_timer_slot->MODE = TIMER_MODE_MODE_Timer;
    m_timer_slot->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    m_timer_slot->PRESCALER = 9; // 16MHz / 2^9 = 31.25kHz, each tick = 32us
    
    // Reset flags
    slot_interval_timer_expired = false;
    slot_duration_timer_expired = false;
    period_count = 0;
    
    test_run_info((unsigned char *)"Hardware timer initialized");
}

// Start slot interval timer (wait until my slot turn)
static void start_slot_interval_timer(uint32_t wait_duration_us) {
    // Stop timer if running
    m_timer_slot->TASKS_STOP = 1;
    m_timer_slot->TASKS_CLEAR = 1;
    
    slot_interval_timer_expired = false;
    slot_timer_is_interval = true;
    
    // Calculate ticks: wait_duration_us / 32us
    uint32_t time_ticks = wait_duration_us / 32; // Convert us to ticks
    m_timer_slot->CC[0] = time_ticks;
    
    // One-shot mode (no auto-clear)
    m_timer_slot->SHORTS = 0;
    
    // Start timer
    m_timer_slot->TASKS_START = 1;
    
    snprintf(debug_str, sizeof(debug_str), "Slot interval timer started: %lu us (%lu ticks)", wait_duration_us, time_ticks);
    test_run_info((unsigned char *)debug_str);
}

// Start slot duration timer (TX slot duration limit)
static void start_slot_duration_timer(uint32_t duration_us) {
    // Stop timer if running
    m_timer_slot->TASKS_STOP = 1;
    m_timer_slot->TASKS_CLEAR = 1;
    
    slot_duration_timer_expired = false;
    slot_timer_is_interval = false;
    
    // Calculate ticks: duration_us / 32us
    uint32_t time_ticks = duration_us / 32; // Convert us to ticks
    m_timer_slot->CC[0] = time_ticks;
    
    // One-shot mode (no auto-clear)
    m_timer_slot->SHORTS = 0;
    
    // Start timer
    m_timer_slot->TASKS_START = 1;
}

// Stop slot timers
static void stop_slot_timer(void) {
    m_timer_slot->TASKS_STOP = 1;
    slot_interval_timer_expired = false;
    slot_duration_timer_expired = false;
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

/* ========== Helper Functions ========== */

// Check for RX event
static bool check_rx_event(void) {
    return (dwt_readsysstatuslo() & DWT_INT_RXFCG_BIT_MASK) != 0;
}

// Send ACK to specific node
static void send_ack(uint8_t to_node) {
    uint8_t ack_msg[20] = {0};
    ack_msg[IDX_FTYPE] = 0xC5;
    ack_msg[IDX_SOURCE] = THIS_NODE_ID;
    ack_msg[IDX_DEST] = to_node;
    ack_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK;
    ack_msg[IDX_SEQ] = period_count & 0xFF;
    
    snprintf(debug_str, sizeof(debug_str), "Sending ACK to Node %c", to_node);
    test_run_info((unsigned char *)debug_str);
    
    // Force off before TX
    dwt_forcetrxoff();
    nrf_delay_us(GUARD_TIME_US);
    
    dwt_writetxdata(20, ack_msg, 0);
    dwt_writetxfctrl(20, 0, 0);
    dwt_starttx(DWT_START_TX_IMMEDIATE);
    waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    
    test_run_info((unsigned char *)"ACK sent successfully");
}

// Send message from queue
static void send_message_from_queue(message_t* msg) {
    // Copy message to TX buffer
    memcpy(tx_msg, msg->msg, MAX_PACKET_SIZE);
    
    // Set sequence number
    tx_msg[IDX_SEQ] = period_count & 0xFF;
    
    // Mark ACKs as pending for the destination
    if (msg->dest_id != NODE_ALL) {
        mark_ack_pending(msg->dest_id);
        protocol_stats.acks_expected++;
        current_period_stats.tx_acks_expected++;
        
        // Mark ACK as expected in period stats
        int node_index = node_id_to_index(msg->dest_id);
        if (node_index >= 0) {
            current_period_stats.ack_expected[node_index] = 1;
        }
    }
    
    // Update statistics based on message type
    uint8_t msg_type = tx_msg[IDX_MSG_TYPE];
    if (msg_type == MSG_TYPE_RELAY) {
        protocol_stats.relay_messages_sent++;
    } else {
        protocol_stats.data_messages_sent++;
    }
    
    current_period_stats.tx_packets_sent++;
    
    // Force off before TX
    dwt_forcetrxoff();
    nrf_delay_us(GUARD_TIME_US);
    
    // Transmit
    dwt_writetxdata(FRAME_LENGTH - FCS_LEN, tx_msg, 0);
    dwt_writetxfctrl(FRAME_LENGTH, 0, 0);
    dwt_starttx(DWT_START_TX_IMMEDIATE);
    waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
}

// Wait for ACKs with timeout
static void wait_for_acks(uint32_t timeout_us) {
    snprintf(debug_str, sizeof(debug_str), "Waiting for ACKs (timeout: %lu us)", timeout_us);
    test_run_info((unsigned char *)debug_str);
    
    uint32_t start = dwt_readsystimestamphi32();
    uint32_t frame_len;
    
    // Enable RX to wait for ACKs
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    
    while ((dwt_readsystimestamphi32() - start) < timeout_us) {
        if (check_rx_event()) {
            frame_len = dwt_getframelength(0);
            if (frame_len <= 20) {  // ACK is small
                dwt_readrxdata(rx_buffer, frame_len, 0);
                
                if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_ACK && 
                    rx_buffer[IDX_DEST] == THIS_NODE_ID) {
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
    }
}

// Handle received message with ACK response logic
static void handle_rx_message(uint8_t* buffer, uint32_t len) {
    uint8_t source = buffer[IDX_SOURCE];
    uint8_t dest = buffer[IDX_DEST];
    uint8_t msg_type = buffer[IDX_MSG_TYPE];
    
    // Handle SYNC messages - this is how normal nodes synchronize
    if (msg_type == MSG_TYPE_SYNC && source == NODE_INIT) {
        protocol_stats.sync_messages_received++;
        sync_received = true;
        period_count++;
        
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
        snprintf(debug_str, sizeof(debug_str), "SYNC received - Period #%lu, Cycle %lu, P%lu: %s", 
                 period_count, cycle_number, period_in_cycle, mode_names[current_period_mode - 1]);
        test_run_info((unsigned char *)debug_str);
        
        // Normal slot handling - wait for designated slot time
        if (MY_SLOT_START_US == 0) {
            // Slot 0 (FL): Start transmission immediately after guard time
            test_run_info((unsigned char *)"Slot 0 - Starting immediate transmission");
            nrf_delay_us(GUARD_TIME_US);  // Standard guard time
            current_state = STATE_TX_ACTIVE;
            start_slot_duration_timer(SLOT_DURATION_US);
            slot_interval_timer_expired = true;  // Mark as if interval timer expired
            in_my_slot = true;  // Enable message transmission
            test_run_info((unsigned char *)"Slot 0 - TX_ACTIVE state set, in_my_slot = true");
        } else {
            // Other slots: Start interval timer to wait for turn
            start_slot_interval_timer(MY_SLOT_START_US);
            current_state = STATE_WAIT_MY_SLOT;
        }
        
        // Don't send ACK for SYNC broadcasts
        return;
    }
    
    // Handle FL/FR messages (if applicable)
    if (source == NODE_FL || source == NODE_FR) {
        // Update global statistics
        if (source == NODE_FL) {
            protocol_stats.fl_messages_received++;
        } else {
            protocol_stats.fr_messages_received++;
        }
        
        if (dest == THIS_NODE_ID) {
            // Message directed to us
            update_node_rx_stats(source, true);  // We'll send ACK
            
            process_own_message(buffer, len);
            send_ack(source);
            
            snprintf(debug_str, sizeof(debug_str), "Msg from %c processed", source);
            test_run_info((unsigned char *)debug_str);
            
        } else {
            // Messages for other nodes - could store for relay if needed
            // For normal nodes, this depends on the relay strategy
            // For now, just acknowledge if it's broadcast
            if (dest == NODE_ALL) {
                update_node_rx_stats(source, false);  // No ACK for broadcast
            }
        }
    }
    // Handle ACK messages directed to us
    else if (msg_type == MSG_TYPE_ACK && dest == THIS_NODE_ID) {
        // ACK directed to us
        snprintf(debug_str, sizeof(debug_str), "ACK received from Node %c", source);
        test_run_info((unsigned char *)debug_str);
        
        mark_ack_received(source);
        protocol_stats.acks_received++;
        current_period_stats.tx_acks_received++;
        
        // Mark ACK as received in period stats
        int node_index = node_id_to_index(source);
        if (node_index >= 0) {
            current_period_stats.ack_status[node_index] = 1;
        }
    }
    // Handle messages directed to us
    else if (dest == THIS_NODE_ID) {
        update_node_rx_stats(source, true);  // We'll send ACK
        
        process_own_message(buffer, len);
        send_ack(source);
        
        snprintf(debug_str, sizeof(debug_str), "Direct msg from %c", source);
        test_run_info((unsigned char *)debug_str);
    }
    // Handle broadcast messages (but not SYNC - already handled above)
    else if (dest == NODE_ALL && msg_type != MSG_TYPE_SYNC) {
        update_node_rx_stats(source, false);  // No ACK for broadcast
        
        process_own_message(buffer, len);
        
        snprintf(debug_str, sizeof(debug_str), "Broadcast msg from %c", source);
        test_run_info((unsigned char *)debug_str);
    }
    // Handle other messages (could be relays, etc.)
    else {
        update_node_rx_stats(source, true);  // We'll send ACK
        
        // For now, just acknowledge other messages
        send_ack(source);
    }
}

// Process message directed to this node
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
            // SYNC already handled in handle_rx_message
            break;
            
        default:
            break;
    }
}

/* ========== Period State Management ========== */

// Print basic protocol statistics
static void print_protocol_stats(void) {
    const char* mode_names[] = {"DATA_TX", "RELAY_TX", "ACK_STATUS", "RETRANS"};
    snprintf(debug_str, sizeof(debug_str), 
             "Stats C:%lu P:%lu U:%d N:%d Rel:%d R:%d [%s]", 
             cycle_number, period_count, urgent_queue.count, normal_queue.count, 
             relay_queue.count, retrans_queue.count, mode_names[current_period_mode - 1]);
    test_run_info((unsigned char *)debug_str);
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

// Reset period statistics for next period
static void reset_period_stats(void) {
    memset(&current_period_stats, 0, sizeof(current_period_stats));
}

/**
 * Application entry point.
 */
int UWB_protocol_normal_node(void)
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
    
    /* Initialize protocol timers */
    init_protocol_timers();
    
    /* Initialize protocol state */
    current_state = STATE_WAIT_SYNC;  // Normal node starts waiting for SYNC
    reset_ack_tracker();
    
    snprintf(debug_str, sizeof(debug_str), "Node %c (%s) initialized (Slot %d - %lu us after SYNC)", THIS_NODE_ID, NODE_TYPE_NAME, THIS_SLOT_NUMBER, MY_SLOT_START_US);
    test_run_info((unsigned char *)debug_str);
    
    // Add a test message to normal queue for TX testing
    message_t test_msg = {0};
    test_msg.dest_id = NODE_ALL;  // Broadcast to all nodes
    test_msg.priority = 1;       // Normal priority
    test_msg.retry_count = 0;
    test_msg.timestamp = dwt_readsystimestamphi32();
    
    // Set up test message
    test_msg.msg[IDX_FTYPE] = 0xC5;
    test_msg.msg[IDX_SEQ] = 1;
    test_msg.msg[IDX_SOURCE] = THIS_NODE_ID;
    test_msg.msg[IDX_DEST] = NODE_ALL;
    test_msg.msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
    test_msg.msg[IDX_PRIORITY] = 1;
    
    // Add test payload
    snprintf((char*)&test_msg.msg[IDX_PAYLOAD], MAX_PACKET_SIZE - IDX_PAYLOAD, "Test message from Node %c", THIS_NODE_ID);
    
    if (enqueue(&normal_queue, &test_msg)) {
        test_run_info((unsigned char *)"Test message added to normal queue");
    } else {
        test_run_info((unsigned char *)"Failed to add test message to queue");
    }
    
    Sleep(5000); // 5 seconds delay before starting protocol
    
    /* ========== Main Protocol Loop ========== */
    snprintf(debug_str, sizeof(debug_str), "Starting UWB Protocol %s Node %c", NODE_TYPE_NAME, THIS_NODE_ID);
    test_run_info((unsigned char *)debug_str);
    
    // Start in RX mode waiting for SYNC
    test_run_info((unsigned char *)"Enabling RX mode to wait for SYNC...");
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    test_run_info((unsigned char *)"RX mode enabled, entering main loop");
    
    while (1) {
        // Debug: Print current state occasionally
        static uint32_t state_debug_count = 0;
        state_debug_count++;
        if (state_debug_count % 1000000 == 0) {
            snprintf(debug_str, sizeof(debug_str), "Main loop state: %d (count: %lu)", 
                    current_state, state_debug_count / 1000000);
            test_run_info((unsigned char *)debug_str);
        }
        
        switch (current_state) {
            case STATE_WAIT_SYNC:
                // Default state - listen for SYNC from initiator
                static uint32_t wait_sync_count = 0;
                wait_sync_count++;
                
                // Print status every 100000 iterations
                if (wait_sync_count % 100000 == 0) {
                    snprintf(debug_str, sizeof(debug_str), "Waiting for SYNC... (count: %lu)", wait_sync_count / 100000);
                    test_run_info((unsigned char *)debug_str);
                }
                
                status_reg = dwt_readsysstatuslo();
                
                // Debug: Print status register occasionally
                if (wait_sync_count % 100000 == 0) {
                    snprintf(debug_str, sizeof(debug_str), "Status reg: 0x%08lX", status_reg);
                    test_run_info((unsigned char *)debug_str);
                }
                
                if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
                    test_run_info((unsigned char *)"RX frame detected!");
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
                    if (wait_sync_count % 100000 == 0) {
                        test_run_info((unsigned char *)"RX error/timeout detected, re-enabling RX");
                    }
                    // Clear error flags and re-enable RX
                    dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                }
                break;
                
            case STATE_WAIT_MY_SLOT:
                // Wait for my slot time after receiving SYNC
                check_slot_timer_expired();
                if (slot_interval_timer_expired) {
                    slot_interval_timer_expired = false;
                    
                    // Start slot duration timer to limit TX time
                    start_slot_duration_timer(SLOT_DURATION_US);  // 1.1ms slot duration
                    in_my_slot = true;
                    current_state = STATE_TX_ACTIVE;
                    
                    snprintf(debug_str, sizeof(debug_str), "My slot started (%lu us)", MY_SLOT_START_US);
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
                // Check slot duration timer
                check_slot_timer_expired();
                
                // Process message queues during TX slot
                static bool message_sent_this_slot = false;
                static message_t last_sent_msg;  // Store last sent message for potential retransmission
                
                if (in_my_slot && !slot_duration_timer_expired && !message_sent_this_slot) {
                    message_t msg;
                    bool msg_found = false;
                    
                    // Period-specific transmission logic for different node types
                    switch (current_period_mode) {
                        case PERIOD_MODE_DATA_TX:
                            // Period 1: All nodes can transmit data
                            if (dequeue(&urgent_queue, &msg)) {
                                msg_found = true;
                                snprintf(debug_str, sizeof(debug_str), "P1: Urgent msg sent to %c", msg.dest_id);
                            } else if (dequeue(&normal_queue, &msg)) {
                                msg_found = true;
                                snprintf(debug_str, sizeof(debug_str), "P1: Normal msg sent to %c", msg.dest_id);
                            }
                            break;
                            
                        case PERIOD_MODE_RELAY_TX:
                        case PERIOD_MODE_ACK_STATUS:
                        case PERIOD_MODE_RETRANS:
                            // Period 2+: FL/FR don't transmit (wait for relay), others send urgent only
                            #if NODE_TYPE == FL || NODE_TYPE == FR
                            // FL/FR nodes don't transmit in periods 2+ (they wait for relay from initiator)
                            snprintf(debug_str, sizeof(debug_str), "P%lu: %s node waiting for relay", period_in_cycle, NODE_TYPE_NAME);
                            test_run_info((unsigned char *)debug_str);
                            #else
                            // Normal nodes only send urgent messages in period 2+
                            if (dequeue(&urgent_queue, &msg)) {
                                msg_found = true;
                                snprintf(debug_str, sizeof(debug_str), "P%lu: Urgent msg sent to %c", period_in_cycle, msg.dest_id);
                            } else {
                                snprintf(debug_str, sizeof(debug_str), "P%lu: Normal node skipping transmission", period_in_cycle);
                                test_run_info((unsigned char *)debug_str);
                            }
                            #endif
                            break;
                    }
                    
                    // Send the message if found
                    if (msg_found) {
                        send_message_from_queue(&msg);
                        message_sent_this_slot = true;
                        memcpy(&last_sent_msg, &msg, sizeof(message_t));  // Store for potential retransmission
                        test_run_info((unsigned char *)debug_str);
                    }
                    
                    // After sending, enable RX to receive ACKs for the rest of the slot
                    if (message_sent_this_slot) {
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);
                        test_run_info((unsigned char *)"RX enabled for ACK reception");
                    }
                    
                    // If no messages to send, just wait
                    if (!message_sent_this_slot) {
                        nrf_delay_us(100);  // Small delay to prevent busy waiting
                    }
                }
                
                // Continue processing ACKs during remaining TX slot time (like initiator)
                if (in_my_slot && !slot_duration_timer_expired) {
                    uint32_t status_reg = dwt_readsysstatuslo();
                    if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
                        uint32_t frame_len = dwt_getframelength(0);
                        if (frame_len <= FRAME_LEN_MAX) {
                            dwt_readrxdata(rx_buffer, frame_len, 0);
                            test_run_info((unsigned char *)"Frame received during TX slot");
                            handle_rx_message(rx_buffer, frame_len);
                        }
                        dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);
                    }
                }
                
                // Check if TX slot duration expired
                if (slot_duration_timer_expired) {
                    slot_duration_timer_expired = false;
                    in_my_slot = false;
                    stop_slot_timer();  // Stop the timer
                    
                    // Process failed transmissions (add to retransmission queue if ACKs not received)
                    if (message_sent_this_slot) {
                        // Check if we got all expected ACKs
                        if (!check_all_acks_received()) {
                            // Not all ACKs received, add to retransmission queue if not already a retransmission
                            if (last_sent_msg.retry_count < MAX_RETRY) {
                                last_sent_msg.retry_count++;
                                if (enqueue(&retrans_queue, &last_sent_msg)) {
                                    snprintf(debug_str, sizeof(debug_str), 
                                            "Message to %c added to retrans queue (retry %d)", 
                                            last_sent_msg.dest_id, last_sent_msg.retry_count);
                                    test_run_info((unsigned char *)debug_str);
                                } else {
                                    test_run_info((unsigned char *)"Retrans queue full!");
                                }
                            } else {
                                snprintf(debug_str, sizeof(debug_str), 
                                        "Message to %c exceeded max retries (%d)", 
                                        last_sent_msg.dest_id, MAX_RETRY);
                                test_run_info((unsigned char *)debug_str);
                            }
                        } else {
                            test_run_info((unsigned char *)"All expected ACKs received");
                        }
                    }
                    
                    message_sent_this_slot = false;  // Reset for next slot
                    
                    snprintf(debug_str, sizeof(debug_str), "TX slot ended, switching to RX");
                    test_run_info((unsigned char *)debug_str);
                    
                    current_state = STATE_RX_LISTEN;
                    
                    snprintf(debug_str, sizeof(debug_str), "STATE set to RX_LISTEN (%d)", current_state);
                    test_run_info((unsigned char *)debug_str);
                    
                    // Force transceiver off before enabling RX (safety measure)
                    test_run_info((unsigned char *)"Forcing transceiver off before RX enable...");
                    dwt_forcetrxoff();
                    nrf_delay_us(GUARD_TIME_US);  // Small delay for cleanup
                    
                    // Enable RX mode
                    test_run_info((unsigned char *)"Attempting to enable RX mode...");
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                    test_run_info((unsigned char *)"RX mode enabled after slot end");
                }
                break;
                
            case STATE_RX_LISTEN:
                // First time entering RX_LISTEN - log it
                static bool first_rx_listen = true;
                if (first_rx_listen) {
                    test_run_info((unsigned char *)"=== ENTERED RX_LISTEN STATE ===");
                    first_rx_listen = false;
                }
                
                // Check for incoming messages (non-blocking)
                status_reg = dwt_readsysstatuslo();
                
                // Add periodic debug log to show RX_LISTEN is active
                static uint32_t rx_listen_count = 0;
                rx_listen_count++;
                if (rx_listen_count % 200000 == 0) {  // More frequent - every ~200k iterations
                    snprintf(debug_str, sizeof(debug_str), "RX_LISTEN active #%lu (status: 0x%08lX)", 
                            rx_listen_count / 200000, status_reg);
                    test_run_info((unsigned char *)debug_str);
                }
                
                if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
                    // Frame received successfully
                    test_run_info((unsigned char *)"Frame received in RX_LISTEN!");
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
                
                // Return to waiting for next SYNC if period seems to be over
                // This is a simplified approach - in practice you might use timeouts
                // For now, stay in RX_LISTEN until next SYNC is detected
                break;
                
            default:
                // Should not reach here
                current_state = STATE_WAIT_SYNC;
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
        if (period_count >= 5000) {  // Run for 5000 periods (100 seconds total)
            test_run_info((unsigned char *)"=== 1000 Cycles (5000 Periods) Completed ===");
            print_period_summary();
            
            snprintf(debug_str, sizeof(debug_str), "Total runtime: %lu cycles, %lu periods (%.1f seconds)", 
                     cycle_number + 1, period_count, (period_count * PERIOD_MS) / 1000.0f);
            test_run_info((unsigned char *)debug_str);
            
            snprintf(debug_str, sizeof(debug_str), "Node %c (%s) protocol test completed", THIS_NODE_ID, NODE_TYPE_NAME);
            test_run_info((unsigned char *)debug_str);
            break;
        }
    }
}

#endif
/*****************************************************************************************************************************************************
 * NOTES:
 *
 * 1. Normal nodes start in STATE_WAIT_SYNC and listen for SYNC messages from the initiator
 * 2. Upon receiving SYNC, they start their slot interval timer and wait for their designated slot
 * 3. When their slot arrives, they transmit one packet and wait for ACKs within their slot duration
 * 4. After the slot expires, they return to RX_LISTEN mode until the next SYNC
 * 5. The node ID and slot number can be configured at the top of the file
 ****************************************************************************************************************************************************/