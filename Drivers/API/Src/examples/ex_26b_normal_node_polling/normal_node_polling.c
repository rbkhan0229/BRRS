/*! ----------------------------------------------------------------------------
 *  @file    normal_node_polling.c
 *  @brief   UWB Protocol Normal Node with Polling-based DWT timers
 *
 *           Normal node implementation that synchronizes with Initiator node
 *           - Uses polling-based DWT (Data Watchpoint and Trace) timers
 *           - Receives SYNC signals from Initiator to synchronize periods
 *           - Participates in 5-period TDMA cycle structure
 *           - Simple polling loop without interrupts
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

#if defined(TEST_NORMAL_NODE_POLLING)

extern void test_run_info(unsigned char *data);

/* ========== TEST MODE ========== */
#define TEST_MODE 1  // Set to 0 for default Node 7, 1 for FL/Normal selection

#if TEST_MODE
    // Select node type for testing (uncomment one):
    #define TEST_NODE_FL        // FL node (Node 4, slot 4)
    //#define TEST_NODE_NORMAL    // Normal node (Node 8, slot 8)

    #ifdef TEST_NODE_FL
        #define APP_NAME "FL NODE POLLING v1.0"
        #define TEST_MY_NODE_ID NODE_FL
        #define TEST_MY_NODE_SEQ 4
        #define TEST_MY_SLOT_START_MS (TEST_MY_NODE_SEQ * SLOT_DURATION_MS)  // 20ms for FL
    #elif defined(TEST_NODE_NORMAL)
        #define APP_NAME "NORMAL NODE POLLING v1.0"
        #define TEST_MY_NODE_ID NODE_8
        #define TEST_MY_NODE_SEQ 8
        #define TEST_MY_SLOT_START_MS (TEST_MY_NODE_SEQ * SLOT_DURATION_MS)  // 40ms for Node 8
    #else
        #error "Please select a node type for TEST_MODE"
    #endif
#else
    /* Default configuration */
    #define APP_NAME "NORMAL NODE POLLING v1.0"
    #define TEST_MY_NODE_ID NODE_8
    #define TEST_MY_NODE_SEQ 8
    #define TEST_MY_SLOT_START_MS (TEST_MY_NODE_SEQ * SLOT_DURATION_MS)
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
#define PERIOD_MS           50      // 50ms period (changed from 20ms)
#define SLOT_DURATION_MS    5       // 5ms per slot (changed from 2ms)
#define TOTAL_NODES         10      // Total nodes in network
#define PERIODS_PER_CYCLE   4       // 4 periods per cycle (changed from 5)

#if TEST_MODE
    #define MY_NODE_SEQ         TEST_MY_NODE_SEQ       // From TEST_MODE selection
    #define MY_SLOT_START_MS    TEST_MY_SLOT_START_MS  // From TEST_MODE selection
#else
    #define MY_NODE_SEQ         8       // Default node 8
    #define MY_SLOT_START_MS    (MY_NODE_SEQ * SLOT_DURATION_MS)  // 40ms for node 8
#endif

/* ========== ACK Reception Parameters ========== */
#define TX_TO_RX_DELAY_UUS  60      // Delay from TX end to RX activation (60us)
#define RX_ACK_TIMEOUT_UUS  4500    // ACK reception timeout (4.5ms) - enough for all ACK slots

/* ========== ACK Slot Timing Parameters ========== */
#define ACK_SLOT_DURATION_US  450   // 450us per ACK slot (minimum interval requirement)

/* Default communication configuration. */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_64,     /* Preamble length. Used in TX only. */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (64 + 1 + 8 - 8), /* SFD timeout */
    DWT_STS_MODE_OFF, /* No STS mode enabled */
    DWT_STS_LEN_64,   /* STS length */
    DWT_PDOA_M0       /* PDOA mode off */
};

/* ========== Node ID Definitions ========== */
#define NODE_INIT '0'    // Initiator node - slot 0
#define NODE_FL   '2'    // Front Left (needs relay)
#define NODE_FR   '3'    // Front Right (needs relay)
#define NODE_4    '4'    // Normal nodes
#define NODE_5    '5'
#define NODE_6    '6'
#define NODE_7    '7'    // This node
#define NODE_8    '8'
#define NODE_9    '9'
#define NODE_10   '0'
#define NODE_ALL  'A'    // Broadcast to all nodes

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

// Buffers
#if TEST_MODE
static uint8_t tx_msg[FRAME_LEN_MAX] = {
    0xC5,               // Frame type
    0,                  // Seq num
    TEST_MY_NODE_ID,    // Source id (from TEST_MODE selection)
    NODE_ALL,           // Dest id (broadcast by default)
    MSG_TYPE_DATA,      // Message type
    0,                  // Priority
    0,                  // Original source for relay
    0,                  // Original dest for relay
    [8 ... (FRAME_LEN_MAX-1)] = 0x00
};
#else
static uint8_t tx_msg[FRAME_LEN_MAX] = {
    0xC5,        // Frame type
    0,           // Seq num
    NODE_8,      // Source id (this node) - changed from NODE_7 to NODE_8
    NODE_ALL,    // Dest id (broadcast by default)
    MSG_TYPE_DATA, // Message type
    0,           // Priority
    0,           // Original source for relay
    0,           // Original dest for relay
    [8 ... (FRAME_LEN_MAX-1)] = 0x00
};
#endif

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

/* ========== Global Variables ========== */

// DWT-based non-blocking timers for UWB protocol
static dwt_timer_t slot_interval_timer; // Wait for my slot after SYNC
static dwt_timer_t slot_duration_timer; // TX slot duration limit
static dwt_timer_t ack_slot_timer;      // ACK transmission delay timer

// Protocol state
static volatile uint32_t period_count = 0;
static bool synchronized = false;       // Flag to track if synchronized with initiator

// Period and Cycle Management
static uint8_t current_period_in_cycle = 1;
static uint32_t current_cycle = 0;

// Current transmission status
static uint8_t current_tx_seq = 0;
static bool rx_mode_active = false;     // Flag to track if RX mode is active
static bool slot_active = false;        // Flag to track if we're in our TX slot

// ACK tracking and retransmission
static uint8_t ack_status[TOTAL_NODES] = {0};

// ACK slot management
static bool pending_ack = false;        // Flag to indicate ACK is pending transmission
static uint8_t pending_ack_msg[16];     // Buffer for pending ACK message

// Message queues
static message_queue_t retrans_queue = {0};    // Retransmission queue for failed messages

// Buffer to store received frame
static uint8_t rx_buffer[FRAME_LEN_MAX];

/* ========== DWT Timer Functions ========== */

// Initialize DWT timer hardware
static void dwt_timer_init(void) {
    // Enable trace and debug blocks (including DWT)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    // Enable cycle counter in DWT
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // Reset cycle counter
    DWT->CYCCNT = 0;

    test_run_info((unsigned char *)"DWT timer initialized for polling timing");
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

/* ========== ACK Reception Functions ========== */

// Calculate remaining time in current slot (in microseconds)
static uint32_t calculate_remaining_slot_time_us(void) {
    if (!slot_duration_timer.active) return 0;

    uint32_t elapsed_cycles = DWT->CYCCNT - slot_duration_timer.start_cycles;
    uint32_t elapsed_us = elapsed_cycles / CYCLES_PER_US;
    uint32_t total_slot_us = SLOT_DURATION_MS * 1000;

    if (elapsed_us >= total_slot_us) return 0;
    return total_slot_us - elapsed_us;
}

// Start RX mode for ACK collection with appropriate timeout
static void start_ack_reception(void) {
    test_run_info((unsigned char *)"Starting ACK reception...");

    // Calculate remaining slot time and set RX timeout
    uint32_t remaining_us = calculate_remaining_slot_time_us();

    // Use remaining slot time as timeout (max 4ms to leave some margin)
    if (remaining_us > 100) {  // Only set RX if we have at least 100us left
        uint32_t rx_timeout_uus = (remaining_us < 4000) ? remaining_us : 4000;

        static char timeout_msg[80];
        snprintf(timeout_msg, sizeof(timeout_msg), "RX timeout set to %lu us", rx_timeout_uus);
        test_run_info((unsigned char *)timeout_msg);

        dwt_setrxtimeout(rx_timeout_uus);
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
        rx_mode_active = true;
    } else {
        test_run_info((unsigned char *)"Not enough time for ACK reception");
        rx_mode_active = false;
    }
}

// Process received ACK messages during RX polling using improved while loop structure
static void process_received_acks(void) {
    if (!rx_mode_active) return;

    // Enhanced ACK collection with while loop structure from tx_wait_resp.c
    int ack_count = 0;
    const int MAX_ACKS = 9;  // Maximum expected ACKs (excluding self)
    uint32_t status_reg = 0;

    test_run_info((unsigned char *)"Starting enhanced ACK collection...");

    // Continue collecting ACKs while we haven't reached the maximum and slot is still active
    while (ack_count < MAX_ACKS && slot_active && rx_mode_active) {
        // Wait for RX events using waitforsysstatus for better responsiveness
        waitforsysstatus(&status_reg, NULL,
                         (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR),
                         0);

        if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
            
            // Read received frame
            uint16_t frame_len = dwt_getframelength(0);
            if (frame_len <= FRAME_LEN_MAX) {
                dwt_readrxdata(rx_buffer, frame_len, 0);

                // Check if it's an ACK message
                if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_ACK) {
                    uint8_t source_node = rx_buffer[IDX_SOURCE];
                    uint8_t ack_seq = rx_buffer[IDX_SEQ];

#if TEST_MODE
                    // Test mode: only accept ACKs from Initiator
                    if (source_node == NODE_INIT) {
                        ack_status[0] = 1;  // Mark Initiator ACK received
                        ack_count++;

                        static char ack_msg[80];
                        snprintf(ack_msg, sizeof(ack_msg), "ACK %d/%d from node %c (seq %d)",
                                ack_count, MAX_ACKS, source_node, ack_seq);
                        test_run_info((unsigned char *)ack_msg);
                    } else {
                        // Ignore ACKs from other nodes in test mode
                        static char ignore_msg[80];
                        snprintf(ignore_msg, sizeof(ignore_msg), "Ignoring ACK from node %c (test mode - Initiator only)", source_node);
                        test_run_info((unsigned char *)ignore_msg);
                    }
#else
                    // Full network mode: accept ACKs from all nodes
                    // Convert node ID to array index for tracking
                    int node_index = -1;
                    switch(source_node) {
                        case NODE_INIT: node_index = 0; break;
                        case NODE_FL:   node_index = 1; break;
                        case NODE_FR:   node_index = 2; break;
                        case NODE_4:    node_index = 3; break;
                        case NODE_5:    node_index = 4; break;
                        case NODE_6:    node_index = 5; break;
                        case NODE_8:    node_index = 6; break;
                        case NODE_9:    node_index = 7; break;
                        case NODE_10:   node_index = 8; break;
                        default: break;
                    }

                    if (node_index >= 0 && node_index < TOTAL_NODES) {
                        ack_status[node_index] = 1;  // Mark ACK received
                        ack_count++;

                        static char ack_msg[80];
                        snprintf(ack_msg, sizeof(ack_msg), "ACK %d/%d from node %c (seq %d)",
                                ack_count, MAX_ACKS, source_node, ack_seq);
                        test_run_info((unsigned char *)ack_msg);
                    }
#endif
                }
            }

            // Clear RX good frame event
            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

            // Re-enable RX with updated timeout for next ACK (critical for 450us intervals)
            // if (rx_mode_active && slot_active) {
                // uint32_t remaining_us = calculate_remaining_slot_time_us();
                // if (remaining_us > 100) {  // Continue only if enough time left
                //     dwt_setrxtimeout(remaining_us);
                //     dwt_rxenable(DWT_START_RX_IMMEDIATE);
                // } else {
                //     test_run_info((unsigned char *)"Slot time exhausted during ACK collection");
                //     break;  // Exit loop if slot time is up
                // }
            // }
            uint32_t remaining_us = calculate_remaining_slot_time_us();
            if (remaining_us > 100) {  // Continue only if enough time left
                dwt_setrxtimeout(remaining_us);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            } else {
                test_run_info((unsigned char *)"Slot time exhausted during ACK collection");
                break;  // Exit loop if slot time is up
            }
            
        } else {
            /* 타임아웃/에러 시 루프 종료 */
            dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
            break;
        }
    }

    // ACK collection is complete - deactivate RX mode to prevent duplicate calls
    if (rx_mode_active) {
        rx_mode_active = false;
    }

    // Log final ACK collection summary
    static char summary_msg[100];
#if TEST_MODE
    int initiator_ack = ack_status[0];
    snprintf(summary_msg, sizeof(summary_msg),
             "Enhanced ACK collection complete: %d total (Initiator:%d)",
             ack_count, initiator_ack);
#else
    snprintf(summary_msg, sizeof(summary_msg),
             "Enhanced ACK collection complete: %d/%d received", ack_count, MAX_ACKS);
#endif
    test_run_info((unsigned char *)summary_msg);
}

// Stop RX mode and check ACK status
static void stop_ack_reception_and_check_status(void) {
    if (!rx_mode_active) return;

    // Deactivate RX mode tracking
    rx_mode_active = false;

    // Count received ACKs
    int acks_received = 0;

#if TEST_MODE
    // Test mode: only count Initiator ACK
    if (ack_status[0]) acks_received++; // Initiator node

    static char result_msg[100];
    snprintf(result_msg, sizeof(result_msg), "ACK collection ended: %d/1 received (Initiator:%d)",
            acks_received, ack_status[0]);
    test_run_info((unsigned char *)result_msg);
#else
    // Full network mode: count all nodes
    for (int i = 0; i < TOTAL_NODES; i++) {
        if (ack_status[i]) acks_received++;
    }

    static char result_msg[100];
    snprintf(result_msg, sizeof(result_msg), "ACK collection ended: %d/%d received", acks_received, TOTAL_NODES);
    test_run_info((unsigned char *)result_msg);
#endif
}

// Process incoming messages during continuous RX mode and send ACKs
static void process_incoming_messages(void) {
    if (!rx_mode_active) return;

    uint32_t status_reg = dwt_readsysstatuslo();

    // Check for received frame
    if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
        // Clear RX buffer
        for (int i = 0; i < FRAME_LEN_MAX; i++) {
            rx_buffer[i] = 0;
        }

        // Read received frame
        uint16_t frame_len = dwt_getframelength(0);
        if (frame_len <= FRAME_LEN_MAX) {
            dwt_readrxdata(rx_buffer, frame_len, 0);

            // Check message type and handle accordingly
            uint8_t msg_type = rx_buffer[IDX_MSG_TYPE];
            uint8_t source_node = rx_buffer[IDX_SOURCE];
            uint8_t dest_node = rx_buffer[IDX_DEST];

            static char rx_msg[100];
            snprintf(rx_msg, sizeof(rx_msg), "RX: %c->%c type=%d seq=%d",
                    source_node, dest_node, msg_type, rx_buffer[IDX_SEQ]);
            test_run_info((unsigned char *)rx_msg);

#if TEST_MODE
            // In test mode, only process messages destined for us or broadcast
            if (dest_node != NODE_ALL && dest_node != TEST_MY_NODE_ID) {
                test_run_info((unsigned char *)"Message not for this node - ignoring");
                // Clear RX event and restore RX mode before returning
                dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
                if (!slot_active) {
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                    rx_mode_active = true;
                    test_run_info((unsigned char *)"RX mode restored after ignoring message");
                }
                return;  // Exit early without further processing
            }
#endif

            // Handle SYNC message from Initiator
            if (msg_type == MSG_TYPE_SYNC && source_node == NODE_INIT) {
                // Synchronize with initiator's period
                period_count = rx_buffer[IDX_SEQ];
                synchronized = true;

                // Calculate period in cycle (4 periods per cycle now)
                current_period_in_cycle = ((period_count - 1) % PERIODS_PER_CYCLE) + 1;
                current_cycle = (period_count - 1) / PERIODS_PER_CYCLE;

                static char sync_msg[100];
                snprintf(sync_msg, sizeof(sync_msg), "SYNC received: Period #%d (Cycle %lu, Period %d/4)",
                        period_count, current_cycle, current_period_in_cycle);
                test_run_info((unsigned char *)sync_msg);

                // Reset and start slot interval timer for our slot
                dwt_timer_stop(&slot_interval_timer);  // Stop any existing timer first

                static char timer_msg[100];
                snprintf(timer_msg, sizeof(timer_msg), "Starting timer for %dms wait", MY_SLOT_START_MS);
                test_run_info((unsigned char *)timer_msg);

                dwt_timer_start(&slot_interval_timer, MY_SLOT_START_MS * 1000);

                // Resume RX mode after SYNC processing to continue receiving DATA
                if (!slot_active) {
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                    rx_mode_active = true;
                    test_run_info((unsigned char *)"RX mode resumed after SYNC processing");
                }
            }

            // Send ACK based on node type and source filtering (but NOT for ACK messages)
            bool should_send_ack = false;
            if (msg_type == MSG_TYPE_DATA || msg_type == MSG_TYPE_RELAY_DATA || msg_type == MSG_TYPE_RELAY_ACK) {
#if TEST_MODE && defined(TEST_NODE_FL)
                // FL node: Only respond to Initiator messages
                if (source_node == NODE_INIT) {
                    should_send_ack = true;
                }
#elif TEST_MODE && defined(TEST_NODE_NORMAL)
                // Normal node: Respond to all nodes EXCEPT FL node
                if (source_node != NODE_FL) {
                    should_send_ack = true;
                }
#else
                // Default behavior for non-test mode
                should_send_ack = true;
#endif
            }

            if (should_send_ack) {
                // Prepare ACK message
                uint8_t ack_msg[16];
                ack_msg[IDX_FTYPE] = 0xC5;
                ack_msg[IDX_SEQ] = rx_buffer[IDX_SEQ];  // Same sequence number
#if TEST_MODE
                ack_msg[IDX_SOURCE] = TEST_MY_NODE_ID;  // This node as source (from TEST_MODE)
#else
                ack_msg[IDX_SOURCE] = NODE_8;           // This node as source - changed from NODE_7 to NODE_8
#endif
                ack_msg[IDX_DEST] = source_node;        // Send ACK back to sender
                ack_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK;
                ack_msg[IDX_PRIORITY] = 0;
                ack_msg[IDX_ORIG_SRC] = 0;
                ack_msg[IDX_ORIG_DST] = 0;

                // Calculate ACK slot delay based on node sequence
                // All nodes use delayed ACK transmission with 450us intervals
                // FL=0us (minimal delay), Node3=450us, Init=900us, Node5=1350us, etc.
                if (MY_NODE_SEQ == 2) {  // FL node uses minimal delay instead of immediate
                    // Use sufficient delay for FL node to allow Initiator to enter ACK RX mode
                    uint32_t ack_delay_us = 10;  // 10us delay - enough for Initiator setup

                    // Store ACK info for later transmission
                    pending_ack = true;
                    memcpy(pending_ack_msg, ack_msg, 16);

                    // Start ACK slot timer
                    dwt_timer_start(&ack_slot_timer, ack_delay_us);

                    static char ack_delay_msg[100];
                    snprintf(ack_delay_msg, sizeof(ack_delay_msg), "ACK scheduled for node %c (minimal delay: %luus)",
                            source_node, ack_delay_us);
                    test_run_info((unsigned char *)ack_delay_msg);

                } else {  // Other nodes use standard delayed ACK transmission with 450us intervals
                    // Calculate ACK delay: (MY_NODE_SEQ - 2) * 450us
                    // FL=0us, Node3=450us, Init=900us, Node5=1350us, Node6=1800us, Node7=2250us, Node8=2700us, etc.
                    uint32_t ack_delay_us = (MY_NODE_SEQ - 2) * ACK_SLOT_DURATION_US;

                    // Store ACK info for later transmission
                    pending_ack = true;
                    memcpy(pending_ack_msg, ack_msg, 16);

                    // Start ACK slot timer
                    dwt_timer_start(&ack_slot_timer, ack_delay_us);

                    static char ack_delay_msg[100];
                    snprintf(ack_delay_msg, sizeof(ack_delay_msg), "ACK scheduled for node %c (delay: %luus)",
                            source_node, ack_delay_us);
                    test_run_info((unsigned char *)ack_delay_msg);
                }
            }
        }

        // Clear RX good frame event
        dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

        // Ensure RX mode continues after any message processing
        if (!slot_active && !rx_mode_active) {
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
            rx_mode_active = true;
            test_run_info((unsigned char *)"RX mode restored after message processing");
        }
    }

    // Check for RX errors and clear them
    if (status_reg & SYS_STATUS_ALL_RX_ERR) {
        dwt_writesysstatuslo(SYS_STATUS_ALL_RX_ERR);
    }
}

/**
 * Application entry point.
 */
int normal_node_polling(void)
{
    /* Display application name on LCD. */
    test_run_info((unsigned char *)APP_NAME);

    /* Configure SPI rate, DW3000 supports up to 38 MHz */
    port_set_dw_ic_spi_fastrate();

    /* Reset DW IC */
    reset_DWIC();

    Sleep(2); // Time needed for DW3000 to start up

    /* Probe for the correct device driver. */
    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    while (!dwt_checkidlerc()) { };

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        test_run_info((unsigned char *)"INIT FAILED     ");
        while (1) { };
    }

    /* Configure DW IC. */
    if (dwt_configure(&config))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    /* Configure TX spectrum parameters */
    extern dwt_txconfig_t txconfig_options;
    dwt_configuretxrf(&txconfig_options);

    /* Set delay to turn reception on after transmission */
    dwt_setrxaftertxdelay(0);

    /* ========== TDMA Protocol Initialization ========== */
    test_run_info((unsigned char *)"Initializing Normal Node with DWT timers...");

    // Initialize DWT timers
    dwt_timer_init();

    // Print protocol configuration
    static char config_msg[150];
    snprintf(config_msg, sizeof(config_msg),
             "Normal Node Config: Period=%dms, Slot=%dms, MyNode=%d, MySlotStart=%dms",
             PERIOD_MS, SLOT_DURATION_MS, MY_NODE_SEQ, MY_SLOT_START_MS);
    test_run_info((unsigned char *)config_msg);

    test_run_info((unsigned char *)"Waiting for SYNC from Initiator...");
    test_run_info((unsigned char *)"Normal Node ready!");

    // Start in continuous RX mode to wait for SYNC
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    rx_mode_active = true;

    /* ========== TDMA Protocol Main Loop ========== */
    while (1)
    {
        // Continuous RX mode processing
        // Process incoming messages whenever RX is active
        if (rx_mode_active) {
            // Process incoming messages (including SYNC) and send ACKs
            process_incoming_messages();
        }

        // Start RX mode when not in TX slot and not already active
        if (!slot_active && !rx_mode_active) {
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
            rx_mode_active = true;
            test_run_info((unsigned char *)"RX mode activated");
        }

        // Check ACK slot timer for delayed ACK transmission
        // 다른 노드로부터 메시지를 받고 ACK를 보내기 위해 기다리다가 시간이 되면 ACK 송신
        if (pending_ack && dwt_timer_is_expired(&ack_slot_timer)) {
            // RX 모드 중지 (자연스러운 전환)
            if (rx_mode_active) {
                rx_mode_active = false;
            }

            // 대기 중인 ACK 전송
            dwt_writetxdata(16, pending_ack_msg, 0);
            dwt_writetxfctrl(16, 0, 0);
            dwt_starttx(DWT_START_TX_IMMEDIATE);

            // TX 완료 대기
            while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK)) { };
            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

            test_run_info((unsigned char *)"ACK sent (delayed)");

            // ACK 전송 완료
            pending_ack = false;
            dwt_timer_stop(&ack_slot_timer);

            // RX 모드 복원 (ACK 전송 후 항상 RX 모드로 복원)
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
            rx_mode_active = true;
            test_run_info((unsigned char *)"RX mode restored after delayed ACK");
        }

        

        // Check slot interval timer (only when synchronized)
        // 내 TX 슬롯이 시작될 때
        if (synchronized && dwt_timer_is_expired(&slot_interval_timer)) {
            // Our TX slot is starting - stop continuous RX (자연스러운 전환)
            slot_active = true;
            if (rx_mode_active) {
                rx_mode_active = false;
                test_run_info((unsigned char *)"Continuous RX stopped - entering TX slot");
            }

            static char debug_msg[100];
            snprintf(debug_msg, sizeof(debug_msg), "TX Active: Cycle %lu, Period %d/4, Slot %d",
                    current_cycle, current_period_in_cycle, MY_NODE_SEQ);
            test_run_info((unsigned char *)debug_msg);

            // Start slot duration timer
#if TEST_MODE
            // Test mode: extend slot duration to allow more time for ACK reception
            dwt_timer_start(&slot_duration_timer, (SLOT_DURATION_MS + 2) * 1000);  // +5ms extra for ACK
#else
            dwt_timer_start(&slot_duration_timer, SLOT_DURATION_MS * 1000);
#endif
            dwt_timer_stop(&slot_interval_timer);

            // Normal node TX logic (send data)
            test_run_info((unsigned char *)"Normal Node: Transmitting data...");

            // Clear ACK status array
            for (int i = 0; i < TOTAL_NODES; i++) {
                ack_status[i] = 0;
            }

            // Prepare DATA message
            current_tx_seq++;
            tx_msg[IDX_SEQ] = current_tx_seq;
#if TEST_MODE
            tx_msg[IDX_SOURCE] = TEST_MY_NODE_ID;
#else
            tx_msg[IDX_SOURCE] = NODE_7;
#endif
            tx_msg[IDX_DEST] = NODE_ALL;
            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
            tx_msg[IDX_PRIORITY] = 1;
            
            dwt_forcetrxoff();
            dwt_setrxaftertxdelay(0); // No delay needed here
            dwt_writetxdata(16, tx_msg, 0);
            dwt_writetxfctrl(16, 0, 0);
            dwt_starttx(DWT_START_TX_IMMEDIATE);

            // Wait for TX complete (polling)
            waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
        
            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

            static char tx_debug[80];
            snprintf(tx_debug, sizeof(tx_debug), "DATA transmitted! seq=%d, period=%d", current_tx_seq, current_period_in_cycle);
            test_run_info((unsigned char *)tx_debug);

            // Start ACK reception immediately after data transmission
            start_ack_reception();
        }

        // Process received ACKs during TX slot ACK collection
        if (slot_active) {
            process_received_acks();
        }

        // Check slot duration timer (polling-based)
        if (dwt_timer_is_expired(&slot_duration_timer)) {
            test_run_info((unsigned char *)"Slot ended!");

            // Mark slot as inactive
            slot_active = false;

            // Stop ACK reception and check final status
            stop_ack_reception_and_check_status();

            // Check ACK status and manage retransmission
            int acks_missing = 0;

#if TEST_MODE
            // Test mode: only check Initiator ACK
            if (!ack_status[0]) acks_missing++; // Initiator node (index 0)

            static char test_ack_msg[100];
            snprintf(test_ack_msg, sizeof(test_ack_msg), "Test Mode ACK: Initiator=%d", ack_status[0]);
            test_run_info((unsigned char *)test_ack_msg);
#else
            // Full network mode: check all nodes
            for (int i = 0; i < TOTAL_NODES; i++) {
                if (!ack_status[i]) acks_missing++;
            }
#endif

            if (acks_missing > 0) {
                static char missing_msg[80];
                snprintf(missing_msg, sizeof(missing_msg), "%d ACKs missing", acks_missing);
                test_run_info((unsigned char *)missing_msg);

                // Add failed message to retransmission queue
                message_t retry_msg;
                memcpy(retry_msg.msg, tx_msg, FRAME_LEN_MAX);
                retry_msg.dest_id = NODE_ALL;
                retry_msg.priority = 1;
                retry_msg.retry_count = 1;

                if (retry_msg.retry_count <= MAX_RETRY && enqueue(&retrans_queue, &retry_msg)) {
                    test_run_info((unsigned char *)"Message added to retransmission queue");
                } else if (retry_msg.retry_count > MAX_RETRY) {
                    test_run_info((unsigned char *)"Max retries reached, dropping message");
                }
            } else {
                test_run_info((unsigned char *)"All ACKs received successfully!");
            }

            dwt_timer_stop(&slot_duration_timer);
        }

        // Small delay to prevent excessive CPU usage
        //nrf_delay_us(10);

        // Exit after 10 periods (2 complete cycles) for demonstration
        if (period_count > 8) {  // 2 cycles * 4 periods = 8 periods
            test_run_info((unsigned char *)"Normal Node demo completed - synchronized for 8 periods");

            // Stop all timers
            test_run_info((unsigned char *)"Stopping all TDMA timers...");
            dwt_timer_stop(&slot_interval_timer);
            dwt_timer_stop(&slot_duration_timer);
            test_run_info((unsigned char *)"All TDMA timers stopped!");

            break;
        }
    }

    return 0;
}

#endif
/*****************************************************************************************************************************************************
 * NOTES:
 *
 * 1. This example uses polling-based DWT (Data Watchpoint and Trace) timers for TDMA timing
 * 2. Normal node waits for SYNC signals from Initiator to synchronize periods
 * 3. No interrupts are used - everything is checked in the main loop
 * 4. DWT timers provide high-precision timing with minimal CPU overhead
 * 5. Participates in the same 5-period TDMA cycle structure as Initiator
 * 6. All UWB transmissions use simple polling method
 ****************************************************************************************************************************************************/