/*! ----------------------------------------------------------------------------
 *  @file    initiator_node_polling.c
 *  @brief   UWB Protocol Initiator Node with Polling-based DWT timers
 *
 *           Combines UWB_protocol_new_init.c TDMA logic with UWB_protocol_init.c DWT timers
 *           - Uses polling-based DWT (Data Watchpoint and Trace) timers
 *           - Maintains the same 5-period TDMA cycle structure
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

#if defined(TEST_INITIATOR_NODE_POLLING)

extern void test_run_info(unsigned char *data);
extern dwt_txconfig_t txconfig_options;

/* ========== TEST MODE ========== */
#define TEST_MODE 1  // Set to 0 for full network, 1 for 3-node test

/* Example application name */
#define APP_NAME "INITIATOR NODE POLLING v1.0"

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
#define SLOT_DURATION_MS    5       // 5ms per slot (changed from 2ms)
#define TOTAL_NODES         10      // Total nodes in network
#define PERIODS_PER_CYCLE   4       // 4 periods per cycle (changed from 5)
#define MY_NODE_SEQ         0       // This node's number (0-9) - slot 0 for immediate transmission
#define MY_SLOT_START_MS    1       // 1ms guard time after SYNC

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
#define NODE_INIT '0'    // Initiator node (this node) - slot 0
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
static uint8_t tx_msg[FRAME_LEN_MAX] = {
    0xC5,        // Frame type
    0,           // Seq num
    NODE_INIT,   // Source id (this node)
    NODE_ALL,    // Dest id (broadcast by default)
    MSG_TYPE_SYNC, // Message type
    0,           // Priority
    0,           // Original source for relay
    0,           // Original dest for relay
    [8 ... (FRAME_LEN_MAX-1)] = 0x00
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

/* ========== Global Variables ========== */

// DWT-based non-blocking timers for UWB protocol
static dwt_timer_t period_timer;        // 20ms period timer
static dwt_timer_t slot_interval_timer; // 10ms wait for my slot
static dwt_timer_t slot_duration_timer; // 2ms TX slot duration limit
static dwt_timer_t ack_slot_timer;      // ACK transmission delay timer

// Protocol state
static volatile uint32_t period_count = 0;  // Start from 0

// Period and Cycle Management
static uint8_t current_period_in_cycle = 1;
static uint32_t current_cycle = 0;

// ACK tracking and retransmission
static uint8_t ack_status[TOTAL_NODES] = {0};

// Message queues
static message_queue_t retrans_queue = {0};    // Retransmission queue for failed messages
static message_queue_t relay_queue = {0};      // Relay queue for FL/FR messages

// Current transmission status
static uint8_t current_tx_seq = 0;
static bool rx_mode_active = false;  // Flag to track if RX mode is active
static bool slot_active = false;     // Flag to track if we're in our TX slot
static message_t current_retry_msg;  // Store current retry message for ACK checking

// ACK slot management
static bool pending_ack = false;        // Flag to indicate ACK is pending transmission
static uint8_t pending_ack_msg[16];     // Buffer for pending ACK message

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
            // Clear RX buffer
            for (int i = 0; i < FRAME_LEN_MAX; i++) {
                rx_buffer[i] = 0;
            }

            // Read received frame
            uint16_t frame_len = dwt_getframelength(0);
            if (frame_len <= FRAME_LEN_MAX) {
                dwt_readrxdata(rx_buffer, frame_len, 0);

                // Check if it's an ACK message
                if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_ACK) {
                    uint8_t source_node = rx_buffer[IDX_SOURCE];
                    uint8_t ack_seq = rx_buffer[IDX_SEQ];

                    // Convert node ID to array index for tracking
                    int node_index = -1;
                    switch(source_node) {
                        case NODE_FL:   node_index = 0; break;
                        case NODE_FR:   node_index = 1; break;
                        case NODE_4:    node_index = 2; break;
                        case NODE_5:    node_index = 3; break;
                        case NODE_6:    node_index = 4; break;
                        case NODE_7:    node_index = 5; break;
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
        } else
        {
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
    int fl_ack = ack_status[0];
    int normal_ack = ack_status[5];
    snprintf(summary_msg, sizeof(summary_msg),
             "Enhanced ACK collection complete: %d total (FL:%d, Normal:%d)",
             ack_count, fl_ack, normal_ack);
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
    // Test mode: only count FL and Normal node ACKs
    if (ack_status[0]) acks_received++; // FL node (index 0)
    if (ack_status[5]) acks_received++; // Normal node 7 (index 5)

    static char result_msg[100];
    snprintf(result_msg, sizeof(result_msg), "ACK collection ended: %d/2 received (FL:%d, Normal:%d)",
            acks_received, ack_status[0], ack_status[5]);
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

            // Send ACK for DATA, RELAY_DATA, or RELAY_ACK messages using slot timing
            if (msg_type == MSG_TYPE_DATA || msg_type == MSG_TYPE_RELAY_DATA || msg_type == MSG_TYPE_RELAY_ACK) {
                // Prepare ACK message
                uint8_t ack_msg[16];
                ack_msg[IDX_FTYPE] = 0xC5;
                ack_msg[IDX_SEQ] = rx_buffer[IDX_SEQ];  // Same sequence number
                ack_msg[IDX_SOURCE] = NODE_INIT;        // This node as source
                ack_msg[IDX_DEST] = source_node;        // Send ACK back to sender
                ack_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK;
                ack_msg[IDX_PRIORITY] = 0;
                ack_msg[IDX_ORIG_SRC] = 0;
                ack_msg[IDX_ORIG_DST] = 0;

                // Calculate ACK delay for Initiator (slot 4): (4-2) * 450us = 900us
                uint32_t ack_delay_us = (4 - 2) * ACK_SLOT_DURATION_US;  // 900us delay

                // Store ACK info for delayed transmission
                pending_ack = true;
                memcpy(pending_ack_msg, ack_msg, 16);

                // Start ACK slot timer
                dwt_timer_start(&ack_slot_timer, ack_delay_us);

                static char ack_delay_msg[100];
                snprintf(ack_delay_msg, sizeof(ack_delay_msg), "ACK scheduled for node %c (delay: %luus)",
                        source_node, ack_delay_us);
                test_run_info((unsigned char *)ack_delay_msg);
            }

            // Handle FL/FR relay messages
            if (msg_type == MSG_TYPE_DATA && (source_node == NODE_FL || source_node == NODE_FR)) {
                // Add FL/FR message to relay queue for Period 2
                message_t relay_msg;
                memcpy(relay_msg.msg, rx_buffer, FRAME_LEN_MAX);
                relay_msg.dest_id = NODE_ALL;
                relay_msg.priority = 1;
                relay_msg.retry_count = 0;

                if (enqueue(&relay_queue, &relay_msg)) {
                    static char relay_queue_msg[80];
                    snprintf(relay_queue_msg, sizeof(relay_queue_msg), "FL/FR message from %c queued for relay", source_node);
                    test_run_info((unsigned char *)relay_queue_msg);
                }
            }
        }

        // Clear RX good frame event
        dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
    }

    // Check for RX errors and clear them
    if (status_reg & SYS_STATUS_ALL_RX_ERR) {
        dwt_writesysstatuslo(SYS_STATUS_ALL_RX_ERR);
    }
}

/**
 * Application entry point.
 */
int initiator_node_polling(void)
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
    dwt_configuretxrf(&txconfig_options);

    /* Set delay to turn reception on after transmission */
    dwt_setrxaftertxdelay(0);

    /* ========== TDMA Protocol Initialization ========== */
    test_run_info((unsigned char *)"Initializing TDMA Protocol with DWT timers...");

    // Initialize DWT timers
    dwt_timer_init();

    // Print protocol configuration
    static char config_msg[150];
    snprintf(config_msg, sizeof(config_msg),
             "TDMA Config: Period=%dms, Slot=%dms, Nodes=%d, MyNode=%d, MySlotStart=%dms",
             PERIOD_MS, SLOT_DURATION_MS, TOTAL_NODES, MY_NODE_SEQ, MY_SLOT_START_MS);
    test_run_info((unsigned char *)config_msg);

    // Start the first period timer
    Sleep(2000);
    test_run_info((unsigned char *)"Starting TDMA protocol...");
    bool is_first_period = true;
    /* ========== TDMA Protocol Main Loop ========== */
    while (1)
    {
        // Check ACK slot timer for delayed ACK transmission
        // ACK time slot내에서 내가 ACK를 보낼 순서가 온 경우
        if (pending_ack && dwt_timer_is_expired(&ack_slot_timer)) {
            // RX 모드 중지 (자연스러운 전환)
            if (rx_mode_active) {
                rx_mode_active = false;
            }

            // 대기 중인 ACK 전송
            dwt_setrxaftertxdelay(0);
            dwt_writetxdata(16, pending_ack_msg, 0);
            dwt_writetxfctrl(16, 0, 0);
            dwt_starttx(DWT_START_TX_IMMEDIATE);

            // TX 완료 대기
            /* Poll DW IC until TX frame sent event set. */
            waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

            test_run_info((unsigned char *)"ACK sent (in ACK SLOT)");

            // ACK 전송 완료
            pending_ack = false;
            dwt_timer_stop(&ack_slot_timer);

            // RX 모드 복원
            if (!slot_active) {
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
                rx_mode_active = true;
            }
        }

        // Check period timer (polling-based)
        // 새로운 period가 시작된 경우
        if (is_first_period || dwt_timer_is_expired(&period_timer)) {
            is_first_period = false;
            period_count++;

            // Calculate period in cycle (4 periods per cycle now)
            current_period_in_cycle = ((period_count - 1) % PERIODS_PER_CYCLE) + 1;

            // Check for new cycle
            if (current_period_in_cycle == 1 && period_count > 1) {
                current_cycle = (period_count - 1) / PERIODS_PER_CYCLE;
                static char cycle_msg[80];
                snprintf(cycle_msg, sizeof(cycle_msg), "=== NEW CYCLE %lu STARTED ===", current_cycle);
                test_run_info((unsigned char *)cycle_msg);

                // Clear retransmission queue for new cycle
                // This ensures old messages from previous cycle don't persist
                retrans_queue.head = 0;
                retrans_queue.tail = 0;
                retrans_queue.count = 0;
                test_run_info((unsigned char *)"Retransmission queue cleared for new cycle");
            }

            static char debug_msg[100];
            snprintf(debug_msg, sizeof(debug_msg), "Period #%lu (Cycle %lu, Period %d/4) - Node %d",
                    period_count, current_cycle, current_period_in_cycle, MY_NODE_SEQ);
            test_run_info((unsigned char *)debug_msg);

            // Send SYNC signal (polling)
            test_run_info((unsigned char *)"Sending SYNC signal...");
            
            // Stop continuous RX before sending SYNC (자연스러운 전환)
            if (rx_mode_active) {
                rx_mode_active = false;
                /* Clear good RX frame event in the DW IC status register. */
                dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
            }

            tx_msg[IDX_SEQ] = (uint8_t)(period_count & 0xFF);
            tx_msg[IDX_SOURCE] = NODE_INIT;
            tx_msg[IDX_DEST] = NODE_ALL;
            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_SYNC;
            tx_msg[IDX_PRIORITY] = 0;
            
            dwt_setrxaftertxdelay(0);
            dwt_writetxdata(16, tx_msg, 0);
            dwt_writetxfctrl(16, 0, 0);
            int ans;
            ans = dwt_starttx(DWT_START_TX_IMMEDIATE);
            if (ans == DWT_SUCCESS) {
                // TX started successfully
                test_run_info((unsigned char *)"SYNC signal transmitted!");
            } else {
                // Handle TX start error if needed
                test_run_info((unsigned char *)"Error starting SYNC transmission");
            }


            // Wait for TX to complete (polling)
            while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK)) { };
            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

            // Start slot interval timer and restart period timer
            dwt_timer_start(&slot_interval_timer, MY_SLOT_START_MS * 1000);
            dwt_timer_start(&period_timer, PERIOD_MS * 1000);
        }

        // Check slot interval timer (polling-based)
        // 내 노드 할당된 time slot이 시작된 경우
        if (dwt_timer_is_expired(&slot_interval_timer)) {
            // Our TX slot is starting - stop continuous RX (자연스러운 전환)
            slot_active = true;
            if (rx_mode_active) {
                rx_mode_active = false;
                test_run_info((unsigned char *)"Continuous RX stopped - entering TX slot");
            }

            // Save period value at TX slot entry for consistency
            uint8_t tx_period = current_period_in_cycle;

            static char debug_msg[100];
            snprintf(debug_msg, sizeof(debug_msg), "TX Active: Cycle %lu, Period %d/4, Slot %d",
                    current_cycle, tx_period, MY_NODE_SEQ);
            test_run_info((unsigned char *)debug_msg);

            // Start slot duration timer
            dwt_timer_start(&slot_duration_timer, SLOT_DURATION_MS * 1000);
            dwt_timer_stop(&slot_interval_timer);

            // Period-specific TX logic (use saved period value)
            switch(tx_period) {
                case 1:  // Period 1: Normal data TX
                    test_run_info((unsigned char *)"Period 1: Transmitting normal data...");

                    // Clear ACK status array
                    for (int i = 0; i < TOTAL_NODES; i++) {
                        ack_status[i] = 0;
                    }

                    // Reset current_retry_msg for new cycle
                    memset(&current_retry_msg, 0, sizeof(current_retry_msg));

                    // Prepare DATA message
                    current_tx_seq++;
                    tx_msg[IDX_SEQ] = current_tx_seq;
                    tx_msg[IDX_SOURCE] = NODE_INIT;
                    tx_msg[IDX_DEST] = NODE_ALL;
                    tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
                    tx_msg[IDX_PRIORITY] = 1;
                    dwt_forcetrxoff();
                    dwt_setrxaftertxdelay(0);
                    dwt_writetxdata(16, tx_msg, 0);
                    dwt_writetxfctrl(16, 0, 0);
                    dwt_starttx(DWT_START_TX_IMMEDIATE);

                    // Wait for TX complete (polling)
                    waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
                    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                    test_run_info((unsigned char *)"DATA transmitted!");

                    // Start ACK reception immediately after data transmission
                    start_ack_reception();
                    break;

                case 2:  // Period 2: Relay TX from relay_queue
                    if (!is_queue_empty(&relay_queue)) {
                        test_run_info((unsigned char *)"Period 2: Relaying FL/FR messages...");

                        message_t relay_msg;
                        if (dequeue(&relay_queue, &relay_msg)) {
                            // Clear ACK status array for relay
                            for (int i = 0; i < TOTAL_NODES; i++) {
                                ack_status[i] = 0;
                            }

                            // Save original message for later use
                            current_retry_msg = relay_msg;

                            memcpy(tx_msg, relay_msg.msg, FRAME_LEN_MAX);
                            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_RELAY_DATA;
                            tx_msg[IDX_SOURCE] = NODE_INIT;

                            dwt_writetxdata(16, tx_msg, 0);
                            dwt_writetxfctrl(16, 0, 0);
                            dwt_starttx(DWT_START_TX_IMMEDIATE);

                            // Wait for TX complete (polling)
                            while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK)) { };
                            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                            test_run_info((unsigned char *)"Relay message transmitted!");

                            // Start ACK reception for relayed message
                            start_ack_reception();
                        }
                    } else {
                        test_run_info((unsigned char *)"Period 2: No messages to relay");
                    }
                    break;

                case 3:  // Period 3: ACK relay to FL/FR
                    if (!is_queue_empty(&relay_queue)) {
                        test_run_info((unsigned char *)"Period 3: Sending relay ACKs to FL/FR...");

                        message_t ack_relay_msg;
                        if (dequeue(&relay_queue, &ack_relay_msg)) {
                            // Clear ACK status array for Period 3
                            for (int i = 0; i < TOTAL_NODES; i++) {
                                ack_status[i] = 0;
                            }

                            // Save message for potential retry
                            current_retry_msg = ack_relay_msg;

                            // Prepare ACK message for original source (FL/FR)
                            tx_msg[IDX_SEQ] = ack_relay_msg.msg[IDX_SEQ];
                            tx_msg[IDX_SOURCE] = NODE_INIT;
                            tx_msg[IDX_DEST] = ack_relay_msg.msg[IDX_ORIG_SRC];  // Send ACK to original source
                            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_RELAY_ACK;
                            tx_msg[IDX_PRIORITY] = 0;
                            tx_msg[IDX_ORIG_SRC] = ack_relay_msg.msg[IDX_ORIG_SRC];
                            tx_msg[IDX_ORIG_DST] = NODE_ALL;

                            dwt_writetxdata(16, tx_msg, 0);
                            dwt_writetxfctrl(16, 0, 0);
                            dwt_starttx(DWT_START_TX_IMMEDIATE);

                            // Wait for TX complete (polling)
                            while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK)) { };
                            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                            static char ack_msg[100];
                            snprintf(ack_msg, sizeof(ack_msg), "Relay ACK sent to node %c", tx_msg[IDX_DEST]);
                            test_run_info((unsigned char *)ack_msg);

                            // Start ACK reception to confirm FL/FR received the ACK
                            start_ack_reception();
                        }
                    } else {
                        test_run_info((unsigned char *)"Period 3: No relay ACKs to send");
                    }
                    break;

                case 4:  // Period 4: Retransmission
                    if (!is_queue_empty(&retrans_queue)) {
                        test_run_info((unsigned char *)"Period 4: Retransmitting failed messages...");

                        message_t retry_msg;
                        if (dequeue(&retrans_queue, &retry_msg)) {
                            // Increment retry count for this retransmission
                            retry_msg.retry_count++;

                            // Clear ACK status for retry
                            for (int i = 0; i < TOTAL_NODES; i++) {
                                ack_status[i] = 0;
                            }

                            memcpy(tx_msg, retry_msg.msg, FRAME_LEN_MAX);

                            dwt_writetxdata(16, tx_msg, 0);
                            dwt_writetxfctrl(16, 0, 0);
                            dwt_starttx(DWT_START_TX_IMMEDIATE);

                            // Wait for TX complete (polling)
                            while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK)) { };
                            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                            static char retry_msg_str[80];
                            snprintf(retry_msg_str, sizeof(retry_msg_str),
                                    "Retransmitting (retry %d)...", retry_msg.retry_count);
                            test_run_info((unsigned char *)retry_msg_str);

                            // Save the retry message temporarily in case ACK fails again
                            // We'll check ACK status and re-enqueue if needed
                            current_retry_msg = retry_msg;

                            // Start ACK reception after retransmission
                            start_ack_reception();
                        }
                    } else {
                        test_run_info((unsigned char *)"Period 4/5: No retransmissions needed");
                    }
                    break;
            }
        }

        // Continuous RX mode when not in TX slot
        if (!slot_active) {
            // Start continuous RX if not already active
            if (!rx_mode_active) {
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
                rx_mode_active = true;
                test_run_info((unsigned char *)"Continuous RX mode started");
            }
            // Process incoming messages and send ACKs
            process_incoming_messages();
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

            // Immediately start continuous RX mode after slot ends
            if (!rx_mode_active) {
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
                rx_mode_active = true;
                test_run_info((unsigned char *)"Continuous RX mode started immediately after slot");
            }

            // Check ACK status and manage retransmission for data transmission periods
            if (current_period_in_cycle == 1 || current_period_in_cycle == 2 || current_period_in_cycle == 3 || current_period_in_cycle == 4 || current_period_in_cycle == 5) {
                // Count missing ACKs
                int acks_missing = 0;

#if TEST_MODE
                // Test mode: only check FL and Normal node ACKs
                if (!ack_status[0]) acks_missing++; // FL node (index 0)
                if (!ack_status[5]) acks_missing++; // Normal node 7 (index 5)

                static char test_ack_msg[100];
                snprintf(test_ack_msg, sizeof(test_ack_msg), "Test Mode ACKs: FL=%d, Normal=%d",
                        ack_status[0], ack_status[5]);
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

                    // Period 2 or 3: relay or ACK relay failed, add to retrans queue
                    if (current_period_in_cycle == 2 || current_period_in_cycle == 3) {
                        // Create retry message for relay
                        message_t retry_msg;
                        memcpy(retry_msg.msg, current_retry_msg.msg, FRAME_LEN_MAX);
                        retry_msg.dest_id = NODE_ALL;
                        retry_msg.priority = 1;
                        retry_msg.retry_count = current_retry_msg.retry_count;

                        if (retry_msg.retry_count < MAX_RETRY) {
                            if (enqueue(&retrans_queue, &retry_msg)) {
                                if (current_period_in_cycle == 2) {
                                    test_run_info((unsigned char *)"Relay failed - added to retransmission queue");
                                } else {
                                    test_run_info((unsigned char *)"ACK relay to FL/FR failed - added to retransmission queue");
                                }
                            }
                        } else {
                            test_run_info((unsigned char *)"Max retries reached for relay, dropping message");
                        }
                    } else {
                        // Period 1, 4, 5: normal data or retransmission
                        message_t retry_msg;
                        memcpy(retry_msg.msg, tx_msg, FRAME_LEN_MAX);
                        retry_msg.dest_id = NODE_ALL;
                        retry_msg.priority = 1;

                        // Set retry count based on period type
                        if (current_period_in_cycle == 1) {
                            // Period 1: First transmission failed, set retry_count to 0 (will become 1 on first retry)
                            retry_msg.retry_count = 0;
                        } else if (current_period_in_cycle == 4 || current_period_in_cycle == 5) {
                            // Period 4/5: Get retry count from saved message (already incremented)
                            retry_msg.retry_count = current_retry_msg.retry_count;
                        }

                        // Check if we should retry or drop
                        if (retry_msg.retry_count < MAX_RETRY) {
                            if (enqueue(&retrans_queue, &retry_msg)) {
                                static char queue_msg[80];
                                snprintf(queue_msg, sizeof(queue_msg),
                                        "Message added to retransmission queue (will be retry %d)",
                                        retry_msg.retry_count + 1);
                                test_run_info((unsigned char *)queue_msg);
                            } else {
                                test_run_info((unsigned char *)"Failed to enqueue - queue full");
                            }
                        } else {
                            test_run_info((unsigned char *)"Max retries reached, dropping message");
                        }
                    }
                } else {
                    // All ACKs received successfully
                    if (current_period_in_cycle == 2) {
                        // Period 2: relay successful, add to relay_queue for Period 3
                        message_t ack_msg;
                        memcpy(ack_msg.msg, current_retry_msg.msg, FRAME_LEN_MAX);
                        ack_msg.dest_id = current_retry_msg.msg[IDX_ORIG_SRC];  // Send ACK back to original source
                        ack_msg.priority = 0;
                        ack_msg.retry_count = 0;

                        if (enqueue(&relay_queue, &ack_msg)) {
                            test_run_info((unsigned char *)"Relay successful - queued ACK for FL/FR");
                        } else {
                            test_run_info((unsigned char *)"Failed to queue ACK - relay queue full");
                        }
                    } else if (current_period_in_cycle == 3) {
                        // Period 3: ACK relay to FL/FR successful
                        test_run_info((unsigned char *)"ACK relay to FL/FR successful!");
                    } else {
                        test_run_info((unsigned char *)"All ACKs received successfully!");
                    }
                }
            }

            dwt_timer_stop(&slot_duration_timer);
        }

        // Small delay to prevent excessive CPU usage
        //nrf_delay_us(10);

        // Exit after 10 periods (2 complete cycles) for demonstration
        if (period_count > 8) {  // 2 cycles * 4 periods = 8 periods
            test_run_info((unsigned char *)"TDMA demo completed - 2 cycles (8 periods) executed");

            // Stop all timers
            test_run_info((unsigned char *)"Stopping all TDMA timers...");
            dwt_timer_stop(&period_timer);
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
 * 2. No interrupts are used - everything is checked in the main loop
 * 3. DWT timers provide high-precision timing with minimal CPU overhead
 * 4. Maintains the same 5-period TDMA cycle structure
 * 5. All UWB transmissions use simple polling method
 ****************************************************************************************************************************************************/