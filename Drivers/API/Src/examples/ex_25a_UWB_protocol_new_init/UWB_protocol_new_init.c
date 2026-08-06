/*! ----------------------------------------------------------------------------
 *  @file    UWB_protocol_new_init.c
 *  @brief   UWB Protocol New Init - TX then wait for response using interrupts
 *
 *           Based on tx_wait_resp_int.c example
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
#include "nrf.h"
#include "nrfx_timer.h"

/* ========== TDMA Protocol Timer Instances ========== */
const nrfx_timer_t period_timer = NRFX_TIMER_INSTANCE(2);      // 20ms period timer
const nrfx_timer_t slot_interval_timer = NRFX_TIMER_INSTANCE(3); // Wait for my slot
const nrfx_timer_t slot_duration_timer = NRFX_TIMER_INSTANCE(4); // TX slot duration

/* ========== TDMA Protocol Parameters ========== */
#define PERIOD_MS           20      // 25ms period (increased to avoid timer conflicts)
#define SLOT_DURATION_MS    2       // 2ms per slot
#define TOTAL_NODES         10      // Total nodes in network
#define MY_NODE_SEQ      5       // This node's number (0-9)
#define MY_SLOT_START_MS    (MY_NODE_SEQ * SLOT_DURATION_MS)  // 10ms for node 5

/* ========== TDMA State Machine ========== */
typedef enum {
    TDMA_STATE_PERIOD_START,    // Period timer expired, new period starting
    TDMA_STATE_WAIT_MY_SLOT,    // Waiting for my slot interval
    TDMA_STATE_TX_ACTIVE,       // In my TX slot
    TDMA_STATE_SLOT_END         // Slot ended, wait for next period
} period_state_t;

static volatile period_state_t current_tdma_state = TDMA_STATE_PERIOD_START;
static volatile uint32_t period_count = 0;  // Start from 0, first period will be triggered by timer handler

#if defined(TEST_UWB_PROTOCOL_NEW_INIT)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "UWB NEW INIT v1.0"

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

/* The frame sent in this example is a blink encoded as per the ISO/IEC 24730-62:2013 standard. It is a 14-byte frame composed of the following fields:
 *     - byte 0: frame control (0xC5 to indicate a multipurpose frame using 64-bit addressing).
 *     - byte 1: sequence number, incremented for each new frame.
 *     - byte 2 -> 9: device ID, see NOTE 1 below.
 *     - byte 10: encoding header (0x43 to indicate no extended ID, temperature, or battery status is carried in the message).
 *     - byte 11: EXT header (0x02 to indicate tag is listening for a response immediately after this message).
 *     - byte 12/13: frame check-sum, automatically set by DW IC. */
//static uint8_t tx_msg[] = { 0xC5, 0, 'D', 'E', 'C', 'A', 'W', 'A', 'V', 'E', 0x43, 0x02, 0, 0 };

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
/* Index to access the sequence number of the blink frame in the tx_msg array. */
#define BLINK_FRAME_SN_IDX 1

/* Delay from end of transmission to activation of reception, expressed in UWB microseconds (1 uus is 512/499.2 microseconds). See NOTE 3 below. */
#define TX_TO_RX_DELAY_UUS 60

/* Receive response timeout, expressed in UWB microseconds. See NOTE 4 below. */
#define RX_RESP_TO_UUS 5000

/* Default inter-frame delay period, in microseconds. */
#define DFLT_TX_DELAY_US 100

/* Inter-frame delay period in case of RX timeout, in microseconds.
 * In case of RX timeout, assume the receiver is not present and lower the rate of blink transmission. */
#define RX_TO_TX_DELAY_MS 3000
/* Inter-frame delay period in case of RX error, in milliseconds.
 * In case of RX error, assume the receiver is present but its response has not been received for any reason and retry blink transmission immediately. */
#define RX_ERR_TX_DELAY_MS 0
/* Current inter-frame delay period.
 * This global static variable is also used as the mechanism to signal events to the background main loop from the interrupt handler callbacks,
 * which set it to positive delay values. */
static int32_t tx_delay_us = -1;

/* Buffer to store received frame. See NOTE 5 below. */
static uint8_t rx_buffer[FRAME_LEN_MAX];

/* ========== ACK Tracking and Retransmission ========== */
// ACK tracking array: index = node_id, value = 0(no ACK) or 1(ACK received)
static uint8_t ack_status[TOTAL_NODES] = {0};

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
/* =============================== */

// Message queues
//static message_queue_t main_queue = {0};       // Main transmission queue
static message_queue_t retrans_queue = {0};    // Retransmission queue for failed messages
static message_queue_t relay_queue = {0};      // Relay queue for FL/FR messages

// Current transmission status
static bool is_data_transmission_active = false;
static uint8_t current_tx_seq = 0;

// Timer synchronization flags
static volatile bool period_transition_in_progress = false;

/* ========== Period and Cycle Management ========== */
// Period tracking within cycle (1-5)
static uint8_t current_period_in_cycle = 1;
static uint32_t current_cycle = 0;

// TX slot period - captures period value when slot starts
// 타이밍 측정을 위해 슬롯 시작 시점의 period 값을 저장
static uint8_t tx_slot_period = 1;
static uint32_t tx_slot_cycle = 0;

// Relay ACK tracking for FL/FR messages
// FL, FR Relay msg에 대한 ACK tracking용
static uint8_t relay_ack_status[TOTAL_NODES] = {0};




/* Declaration of static functions. */
static void rx_ok_cb(const dwt_cb_data_t *cb_data);
static void rx_to_cb(const dwt_cb_data_t *cb_data);
static void rx_err_cb(const dwt_cb_data_t *cb_data);
static void tx_conf_cb(const dwt_cb_data_t *cb_data);

/* ========== TDMA Timer Callback Declarations ========== */
void period_timer_handler(nrf_timer_event_t event_type, void* p_context);
void slot_interval_timer_handler(nrf_timer_event_t event_type, void* p_context);
void slot_duration_timer_handler(nrf_timer_event_t event_type, void* p_context);

/* ========== TDMA Functions ========== */
static void init_tdma_timers(void);
static void start_period_timer(void);
static void start_slot_interval_timer(void);
static void start_slot_duration_timer(void);
static void process_tdma_state_machine(void);

/* Values for the PG_DELAY and TX_POWER registers reflect the bandwidth and power of the spectrum at the current
 * temperature. These values can be calibrated prior to taking reference measurements. See NOTE 6 below. */
extern dwt_txconfig_t txconfig_options;

/**
 * Application entry point.
 */
int UWB_protocol_new_init(void)
{
    dwt_callbacks_s cbs = {NULL};

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

    /* Configure DW IC. See NOTE 2 below. */
    /* if the dwt_configure returns DWT_ERROR either the PLL or RX calibration has failed the host should reset the device */
    if (dwt_configure(&config))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    /* Configure the TX spectrum parameters (power, PG delay and PG count) */
    //dwt_configuretxrf(&txconfig_options);

    /* Define all the callback functions that will be called by the DW IC driver as a result of DW IC events. */
    cbs.cbTxDone = tx_conf_cb;
    cbs.cbRxOk = rx_ok_cb;
    cbs.cbRxTo = rx_to_cb;
    cbs.cbRxErr = rx_err_cb;

    /* Register the call-backs (SPI CRC error callback is not used). */
    dwt_setcallbacks(&cbs);

    /* Enable wanted interrupts (TX confirmation, RX good frames, RX timeouts and RX errors). */
    dwt_setinterrupt(DWT_INT_TXFRS_BIT_MASK | DWT_INT_RXFCG_BIT_MASK | DWT_INT_RXFTO_BIT_MASK | DWT_INT_RXPTO_BIT_MASK | DWT_INT_RXPHE_BIT_MASK
                         | DWT_INT_RXFCE_BIT_MASK | DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXSTO_BIT_MASK,
        0, DWT_ENABLE_INT);

    /*Clearing the SPI ready interrupt*/
    dwt_writesysstatuslo(DWT_INT_RCINIT_BIT_MASK | DWT_INT_SPIRDY_BIT_MASK);

    /* Install DW IC IRQ handler. */
    port_set_dwic_isr(dwt_isr);

    /* Set delay to turn reception on after transmission of the frame. See NOTE 3 below. */
    dwt_setrxaftertxdelay(TX_TO_RX_DELAY_UUS);

    /* Set response frame timeout. */
    dwt_setrxtimeout(RX_RESP_TO_UUS);

    /* ========== TDMA Protocol Initialization ========== */
    test_run_info((unsigned char *)"Initializing TDMA Protocol...");

    // Initialize all TDMA timers
    init_tdma_timers();

    // Print protocol configuration
    static char config_msg[150];
    snprintf(config_msg, sizeof(config_msg),
             "TDMA Config: Period=%dms, Slot=%dms, Nodes=%d, MyNode=%d, MySlotStart=%dms",
             PERIOD_MS, SLOT_DURATION_MS, TOTAL_NODES, MY_NODE_SEQ, MY_SLOT_START_MS);
    test_run_info((unsigned char *)config_msg);

    // Start the first period timer - it will expire and trigger the handler
    test_run_info((unsigned char *)"Starting TDMA protocol...");

    // Set initial state to wait for period handler
    current_tdma_state = TDMA_STATE_WAIT_MY_SLOT;
    
    Sleep(100); // Short delay to let first period start
    // Start period timer - first expire will trigger period_timer_handler
    start_period_timer();

    
    test_run_info((unsigned char *)"TDMA Protocol running!");

    /* ========== TDMA Protocol Main Loop ========== */
    while (1)
    {
        // Process TDMA state machine
        process_tdma_state_machine();

        // Small delay to prevent excessive CPU usage
        Sleep(1); // 1ms delay

        // Exit after 10 periods (2 complete cycles) for demonstration
        if (period_count > 10) {
            test_run_info((unsigned char *)"TDMA demo completed - 2 cycles (10 periods) executed");

            // Stop all timers
            test_run_info((unsigned char *)"Stopping all TDMA timers...");
            nrfx_timer_disable(&period_timer);
            nrfx_timer_disable(&slot_interval_timer);
            nrfx_timer_disable(&slot_duration_timer);
            test_run_info((unsigned char *)"All TDMA timers stopped!");

            break;
        }
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
    int i;

    /* Clear local RX buffer to avoid having leftovers from previous receptions. This is not necessary but is included here to aid reading the RX
     * buffer. */
    for (i = 0; i < FRAME_LEN_MAX; i++)
    {
        rx_buffer[i] = 0;
    }

    /* A frame has been received, copy it to our local buffer. */
    if (cb_data->datalength <= FRAME_LEN_MAX)
    {
        dwt_readrxdata(rx_buffer, cb_data->datalength, 0);
    }

    /* Process received message if we're expecting ACKs */
    if (is_data_transmission_active && rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_ACK) {
        uint8_t ack_source = rx_buffer[IDX_SOURCE];
        uint8_t ack_seq = rx_buffer[IDX_SEQ];

        // Check if this ACK is for our current transmission
        if (ack_seq == current_tx_seq) {
            // Convert source node ID to array index (NODE_FL='2' -> index 1, etc.)
            int node_index = -1;
            switch (ack_source) {
                case NODE_FL:   node_index = 1; break;  // '2' -> index 1
                case NODE_FR:   node_index = 2; break;  // '3' -> index 2
                case NODE_4:    node_index = 3; break;  // '4' -> index 3
                case NODE_5:    node_index = 4; break;  // '5' -> index 4
                case NODE_6:    node_index = 5; break;  // '6' -> index 5
                case NODE_7:    node_index = 6; break;  // '7' -> index 6
                case NODE_8:    node_index = 7; break;  // '8' -> index 7
                case NODE_9:    node_index = 8; break;  // '9' -> index 8
                case NODE_10:   node_index = 9; break;  // '0' -> index 9
                default: break;
            }

            if (node_index >= 0 && node_index < TOTAL_NODES) {
                ack_status[node_index] = 1;  // Mark ACK received
                static char ack_msg[80];
                snprintf(ack_msg, sizeof(ack_msg), "ACK received from node %c (index %d)", ack_source, node_index);
                test_run_info((unsigned char *)ack_msg);
            }
        }
    }

    /* Set corresponding inter-frame delay. */
    tx_delay_us = DFLT_TX_DELAY_US;

    /* TESTING BREAKPOINT LOCATION #1 */
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
    /* Set corresponding inter-frame delay. */
    tx_delay_us = RX_TO_TX_DELAY_MS;

    /* TESTING BREAKPOINT LOCATION #2 */
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
    /* Set corresponding inter-frame delay. */
    tx_delay_us = RX_ERR_TX_DELAY_MS;

    /* TESTING BREAKPOINT LOCATION #3 */
}

/*! ------------------------------------------------------------------------------------------------------------------
 * @fn tx_conf_cb()
 *
 * @brief Callback to process TX confirmation events
 *
 * @param  cb_data  callback data
 *
 * @return  none
 */
static void tx_conf_cb(const dwt_cb_data_t *cb_data)
{
    (void)cb_data;
    test_run_info((unsigned char *)"TX COMPLETE !!");
    tx_delay_us = DFLT_TX_DELAY_US;
    /* This callback has been defined so that a breakpoint can be put here to check it is correctly called but there is actually nothing specific to
     * do on transmission confirmation in this example. Typically, we could activate reception for the response here but this is automatically handled
     * by DW IC using DWT_RESPONSE_EXPECTED parameter when calling dwt_starttx().
     * An actual application that would not need this callback could simply not define it and set the corresponding field to NULL when calling
     * dwt_setcallbacks(). The ISR will not call it which will allow to save some interrupt processing time. */

    /* TESTING BREAKPOINT LOCATION #4 */
}

/* ========== TDMA Timer Callback Implementations ========== */

/*!
 * @brief Period timer callback - triggers new TDMA period
 */
void period_timer_handler(nrf_timer_event_t event_type, void* p_context)
{
    if (event_type == NRF_TIMER_EVENT_COMPARE0)
    {
        // Set atomic flag to prevent slot timer conflicts
        period_transition_in_progress = true;

        period_count++;
        current_tdma_state = TDMA_STATE_PERIOD_START;

        // Immediately restart period timer and start slot interval timer
        // This ensures perfect synchronization for every period
        

        // Flag will be cleared in PERIOD_START after SYNC transmission
        // test_run_info((unsigned char *)"Period timer expired!"); // REMOVED for timing
    }
}

/*!
 * @brief Slot interval timer callback - my slot time arrived
 */
void slot_interval_timer_handler(nrf_timer_event_t event_type, void* p_context)
{
    if (event_type == NRF_TIMER_EVENT_COMPARE0)
    {
        // Check if period transition is in progress
        if (period_transition_in_progress) {
            // test_run_info((unsigned char *)"Slot timer blocked by period transition"); // REMOVED
            return; // Skip this slot start to avoid conflicts
        }

        // Safety re-capture of period values at slot start time
        // Should be same as PERIOD_START values since timers start simultaneously
        tx_slot_period = current_period_in_cycle;
        tx_slot_cycle = current_cycle;

        current_tdma_state = TDMA_STATE_TX_ACTIVE;
        // test_run_info((unsigned char *)"My TX slot started!"); // REMOVED for timing
    }
}

/*!
 * @brief Slot duration timer callback - my slot time ended
 */
void slot_duration_timer_handler(nrf_timer_event_t event_type, void* p_context)
{
    if (event_type == NRF_TIMER_EVENT_COMPARE0)
    {
        current_tdma_state = TDMA_STATE_SLOT_END;
        // test_run_info((unsigned char *)"My TX slot ended!"); // REMOVED for timing

        // Check ACK status and handle retransmission if data was transmitted
        if (is_data_transmission_active) {
            int acks_received = 0;
            int acks_missing = 0;
            static char ack_summary[120];

            // Count received ACKs
            for (int i = 0; i < TOTAL_NODES; i++) {
                if (ack_status[i] == 1) {
                    acks_received++;
                } else {
                    acks_missing++;
                }
            }

            snprintf(ack_summary, sizeof(ack_summary),
                    "Slot ended: ACKs received=%d, missing=%d", acks_received, acks_missing);
            test_run_info((unsigned char *)ack_summary);

            // If not all ACKs received, add to retransmission queue
            if (acks_missing > 0) {
                message_t retry_msg;

                // Copy current transmission to retry message
                memcpy(retry_msg.msg, tx_msg, FRAME_LEN_MAX);
                retry_msg.dest_id = NODE_ALL;
                retry_msg.priority = 1;  // Normal priority
                retry_msg.retry_count = 1;  // First retry

                if (enqueue(&retrans_queue, &retry_msg)) {
                    test_run_info((unsigned char *)"Message added to retransmission queue");
                } else {
                    test_run_info((unsigned char *)"Failed to add message to retrans queue (full)");
                }
            } else {
                test_run_info((unsigned char *)"All ACKs received - transmission successful!");
            }

            // Reset transmission status
            is_data_transmission_active = false;
        }
    }
}

/* ========== TDMA Timer Functions ========== */

/*!
 * @brief Initialize all 3 TDMA timers
 */
static void init_tdma_timers(void)
{
    nrfx_timer_config_t timer_cfg = NRFX_TIMER_DEFAULT_CONFIG;
    timer_cfg.frequency = NRF_TIMER_FREQ_16MHz;  // 16MHz for maximum precision (62.5ns resolution)
    timer_cfg.mode = NRF_TIMER_MODE_TIMER;
    timer_cfg.bit_width = NRF_TIMER_BIT_WIDTH_32; // 32-bit for sufficient range

    // Initialize period timer (TIMER2)
    
    if (nrfx_timer_init(&period_timer, &timer_cfg, period_timer_handler) != NRFX_SUCCESS) {
        test_run_info((unsigned char *)"Period timer init failed");
        return;
    }
    nrfx_timer_enable(&period_timer);

    // Initialize slot interval timer (TIMER3)
    
    if (nrfx_timer_init(&slot_interval_timer, &timer_cfg, slot_interval_timer_handler) != NRFX_SUCCESS) {
        test_run_info((unsigned char *)"Slot interval timer init failed");
        return;
    }
    nrfx_timer_enable(&slot_interval_timer);

    // Initialize slot duration timer (TIMER4)
    
    if (nrfx_timer_init(&slot_duration_timer, &timer_cfg, slot_duration_timer_handler) != NRFX_SUCCESS) {
        test_run_info((unsigned char *)"Slot duration timer init failed");
        return;
    }
    nrfx_timer_enable(&slot_duration_timer);

    test_run_info((unsigned char *)"All TDMA timers initialized with priority levels!");
}

/*!
 * @brief Start period timer (20ms)
 */
static void start_period_timer(void)
{
    uint32_t period_ticks = nrfx_timer_ms_to_ticks(&period_timer, PERIOD_MS);
    nrfx_timer_extended_compare(&period_timer, NRF_TIMER_CC_CHANNEL0, period_ticks,
                               NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);
    nrfx_timer_clear(&period_timer);
}

/*!
 * @brief Start slot interval timer (wait for my slot) - ONE SHOT
 */
static void start_slot_interval_timer(void)
{
    uint32_t slot_wait_ticks = nrfx_timer_ms_to_ticks(&slot_interval_timer, MY_SLOT_START_MS);
    // ONE SHOT: No auto-clear, just trigger once
    nrfx_timer_extended_compare(&slot_interval_timer, NRF_TIMER_CC_CHANNEL0, slot_wait_ticks,
                               0, true); // No SHORT mask - one shot
    nrfx_timer_clear(&slot_interval_timer);
}

/*!
 * @brief Start slot duration timer (2ms TX slot) - ONE SHOT
 */
static void start_slot_duration_timer(void)
{
    uint32_t slot_duration_ticks = nrfx_timer_ms_to_ticks(&slot_duration_timer, SLOT_DURATION_MS);
    // ONE SHOT: No auto-clear, just trigger once
    nrfx_timer_extended_compare(&slot_duration_timer, NRF_TIMER_CC_CHANNEL0, slot_duration_ticks,
                               0, true); // No SHORT mask - one shot
    nrfx_timer_clear(&slot_duration_timer);
}

/*!
 * @brief TDMA State Machine - processes current state and handles transitions
 */
static void process_tdma_state_machine(void)
{
    static char debug_msg[100];
    static period_state_t last_processed_state = TDMA_STATE_SLOT_END; // Track last processed state

    // Only process if state has changed (avoid duplicate processing)
    if (current_tdma_state == last_processed_state) {
        return;
    }

    switch (current_tdma_state) {
        case TDMA_STATE_PERIOD_START:

            // Calculate period in cycle at the start of period
            current_period_in_cycle = ((period_count - 1) % 5) + 1;

            // Set TX slot period immediately for this period
            tx_slot_period = current_period_in_cycle;
            tx_slot_cycle = current_cycle;

            // Check for new cycle
            if (current_period_in_cycle == 1 && period_count > 1) {
                current_cycle = (period_count - 1) / 5;
                tx_slot_cycle = current_cycle;  // Update captured cycle too
                if (period_count == 6 || period_count == 11 || period_count == 16) {
                    static char cycle_msg[80];
                    snprintf(cycle_msg, sizeof(cycle_msg), "=== NEW CYCLE %lu STARTED ===", current_cycle);
                    test_run_info((unsigned char *)cycle_msg);
                }
            }

            snprintf(debug_msg, sizeof(debug_msg), "Period #%lu (Cycle %lu, Period %d/5) - Node %d",
                    period_count, tx_slot_cycle, tx_slot_period, MY_NODE_SEQ);
            test_run_info((unsigned char *)debug_msg);

            // Send SYNC signal to all nodes (no ACK required)
            test_run_info((unsigned char *)"Sending SYNC signal to all nodes...");

            // Prepare SYNC message
            tx_msg[IDX_SEQ] = (uint8_t)(period_count & 0xFF);  // Period sequence number
            tx_msg[IDX_SOURCE] = NODE_INIT;                    // This node is initiator
            tx_msg[IDX_DEST] = NODE_ALL;                       // Broadcast to all nodes
            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_SYNC;              // SYNC message type
            tx_msg[IDX_PRIORITY] = 0;                          // High priority

            // Clear status registers
            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

            // Transmit SYNC message (no response expected)
            dwt_writetxdata(16, tx_msg, 0);  // Send 16 bytes for SYNC
            dwt_writetxfctrl(16, 0, 0);      // Frame length, no ranging
            dwt_starttx(DWT_START_TX_IMMEDIATE);  // No response expected for SYNC

            // Wait for TX to complete
            
            while (tx_delay_us == -1) { };

            /* Execute the defined delay before next transmission. */
            if (tx_delay_us > 0)
            {
                nrf_delay_us(tx_delay_us);
            }
            tx_delay_us = -1;

            //dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);  // Clear TX done flag

            test_run_info((unsigned char *)"SYNC signal transmitted!");

            start_period_timer();        // 20ms period timer restart
            start_slot_interval_timer(); // 10ms slot timer start (synchronized)

            current_tdma_state = TDMA_STATE_WAIT_MY_SLOT;
            test_run_info((unsigned char *)"Waiting for my slot...");

            // Clear period transition flag AFTER SYNC transmission is complete
            period_transition_in_progress = false;
            break;

        case TDMA_STATE_WAIT_MY_SLOT:
            // Just waiting - timer will change state
            // No action needed here
            break;

        case TDMA_STATE_TX_ACTIVE:
            // My slot started - behavior depends on period within cycle
            // Use captured values from slot start time
            snprintf(debug_msg, sizeof(debug_msg), "TX Active: Cycle %lu, Period %d/5, Slot %d",
                    tx_slot_cycle, tx_slot_period, MY_NODE_SEQ);
            test_run_info((unsigned char *)debug_msg);

            // Start slot duration timer
            start_slot_duration_timer();

            // Only transmit if not already transmitting
            if (!is_data_transmission_active) {
                // Behavior depends on which period in the cycle (use captured value)
                switch(tx_slot_period) {
                    case 1:  // Period 1: Normal data TX
                        test_run_info((unsigned char *)"Period 1: Transmitting normal data...");

                        // Clear ACK status array
                        for (int i = 0; i < TOTAL_NODES; i++) {
                            ack_status[i] = 0;
                        }

                        // Prepare DATA message
                        current_tx_seq++;
                        tx_msg[IDX_SEQ] = current_tx_seq;
                        tx_msg[IDX_SOURCE] = NODE_INIT;
                        tx_msg[IDX_DEST] = NODE_ALL;
                        tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
                        tx_msg[IDX_PRIORITY] = 1;

                        // Clear status and transmit
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
                        is_data_transmission_active = true;

                        dwt_writetxdata(16, tx_msg, 0);
                        dwt_writetxfctrl(16, 0, 1);
                        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                        test_run_info((unsigned char *)"DATA transmitted, waiting for ACKs...");

                        // Wait for TX complete
                        while (tx_delay_us == -1) { };
                        if (tx_delay_us > 0) {
                            nrf_delay_us(tx_delay_us);
                        }
                        tx_delay_us = -1;
                        break;

                    case 2:  // Period 2: Relay TX from relay_queue
                        if (!is_queue_empty(&relay_queue)) {
                            test_run_info((unsigned char *)"Period 2: Relaying FL/FR messages...");

                            message_t relay_msg;
                            if (dequeue(&relay_queue, &relay_msg)) {
                                // Clear relay ACK status
                                for (int i = 0; i < TOTAL_NODES; i++) {
                                    relay_ack_status[i] = 0;
                                }

                                // Prepare relay message
                                memcpy(tx_msg, relay_msg.msg, FRAME_LEN_MAX);
                                tx_msg[IDX_MSG_TYPE] = MSG_TYPE_RELAY_DATA;
                                tx_msg[IDX_SOURCE] = NODE_INIT;  // We are relaying it
                                // Keep original source in IDX_ORIG_SRC

                                dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
                                is_data_transmission_active = true;

                                dwt_writetxdata(16, tx_msg, 0);
                                dwt_writetxfctrl(16, 0, 1);
                                dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                                test_run_info((unsigned char *)"Relay message transmitted!");

                                while (tx_delay_us == -1) { };
                                if (tx_delay_us > 0) {
                                    nrf_delay_us(tx_delay_us);
                                }
                                tx_delay_us = -1;
                            }
                        } else {
                            test_run_info((unsigned char *)"Period 2: No messages to relay");
                        }
                        break;

                    case 3:  // Period 3: ACK relay to FL/FR
                        test_run_info((unsigned char *)"Period 3: Sending relay ACKs to FL/FR...");
                        // TODO: Implement ACK relay logic
                        break;

                    case 4:  // Period 4-5: Retransmission
                    case 5:
                        if (!is_queue_empty(&retrans_queue)) {
                            test_run_info((unsigned char *)"Period 4/5: Retransmitting failed messages...");

                            message_t retry_msg;
                            if (dequeue(&retrans_queue, &retry_msg)) {
                                // Clear ACK status for retry
                                for (int i = 0; i < TOTAL_NODES; i++) {
                                    ack_status[i] = 0;
                                }

                                memcpy(tx_msg, retry_msg.msg, FRAME_LEN_MAX);

                                dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
                                is_data_transmission_active = true;

                                dwt_writetxdata(16, tx_msg, 0);
                                dwt_writetxfctrl(16, 0, 1);
                                dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                                static char retry_msg_str[80];
                                snprintf(retry_msg_str, sizeof(retry_msg_str),
                                        "Retransmitting (retry %d)...", retry_msg.retry_count);
                                test_run_info((unsigned char *)retry_msg_str);

                                while (tx_delay_us == -1) { };
                                if (tx_delay_us > 0) {
                                    nrf_delay_us(tx_delay_us);
                                }
                                tx_delay_us = -1;
                            }
                        } else {
                            test_run_info((unsigned char *)"Period 4/5: No retransmissions needed");
                        }
                        break;
                }
            }

            // Change state immediately to prevent re-entry
            current_tdma_state = TDMA_STATE_WAIT_MY_SLOT;
            break;

        case TDMA_STATE_SLOT_END:
            // My slot ended - wait for next period
            test_run_info((unsigned char *)"Slot ended - waiting for next period");

            // Stay in this state until next period starts
            break;
    }

    last_processed_state = current_tdma_state;
}

#endif
/*****************************************************************************************************************************************************
 * NOTES:
 *
 * 1. The device ID is a hard coded constant in the blink to keep the example simple but for a real product every device should have a unique ID.
 *    For development purposes it is possible to generate a DW IC unique ID by combining the Lot ID & Part Number values programmed into the
 *    DW IC during its manufacture. However there is no guarantee this will not conflict with someone else's implementation. We recommended that
 *    customers buy a block of addresses from the IEEE Registration Authority for their production items. See "EUI" in the DW IC User Manual.
 * 2. Desired configuration by user may be different to the current programmed configuration. dwt_configure is called to set desired
 *    configuration.
 * 3. TX to RX delay can be set to 0 to activate reception immediately after transmission. But, on the responder side, it takes time to process the
 *    received frame and generate the response (this has been measured experimentally to be around 70 us). Using an RX to TX delay slightly less than
 *    this minimum turn-around time allows the application to make the communication efficient while reducing power consumption by adjusting the time
 *    spent with the receiver activated.
 * 4. This timeout is for complete reception of a frame, i.e. timeout duration must take into account the length of the expected frame. Here the value
 *    is arbitrary but chosen large enough to make sure that there is enough time to receive a complete frame sent by the "RX then send a response"
 *    example at the 110k data rate used (around 3 ms).
 * 5. In this example, maximum frame length is set to 127 bytes which is 802.15.4 UWB standard maximum frame length. DW IC supports an extended frame
 *    length (up to 1023 bytes long) mode which is not used in this example.
 * 6. In a real application, for optimum performance within regulatory limits, it may be necessary to set TX pulse bandwidth and TX power, (using
 *    the dwt_configuretxrf API call) to per device calibrated values saved in the target system or the DW IC OTP memory.
 * 7. dwt_writetxdata() takes the full size of tx_msg as a parameter but only copies (size - 2) bytes as the check-sum at the end of the frame is
 *    automatically appended by the DW IC. This means that our tx_msg could be two bytes shorter without losing any data (but the sizeof would not
 *    work anymore then as we would still have to indicate the full length of the frame to dwt_writetxdata()).
 ****************************************************************************************************************************************************/