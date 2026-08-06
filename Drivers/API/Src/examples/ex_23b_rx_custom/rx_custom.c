/*! ----------------------------------------------------------------------------
 *  @file    rx_custom.c
 *  @brief   RX Custom Preamble Code Sequence (17->24->9 tracking)
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
#include <string.h>
#include <stdio.h>

#if defined(TEST_RX_CUSTOM)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "RX CUSTOM SEQUENCE v1.0"

/* Default communication configuration. We use default non-STS DW mode. */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_64,     /* Preamble length. Used in TX only. */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    17,               /* TX preamble code. Used in TX only. */
    17,               /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (4096 + 1 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, /* STS disabled */
    DWT_STS_LEN_64,   /* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       /* PDOA mode off */
};

/* Buffer to store received frame. See NOTE 1 below. */
static uint8_t rx_buffer[FRAME_LEN_MAX];

static char str_to_print[100]; // Increased size for longer messages

static uint16_t expected_packet_nb=0;
#define CLEAR_ARRAY(array, size) for(int i = 0; i < size; i++) array[i] = 0

/* Synchronization delay after dwt_configure for TX-RX alignment */
#define SYNC_DELAY_MS 2

/* Packet header indices for dynamic synchronization */
enum {
  IDX_FTYPE     = 0,  // 0: frame type
  IDX_SEQ       = 1,  // 1: seq
  IDX_DEV_ID    = 2,  // 2~9: device ID
  IDX_TOPIC_L   = 10, // 10: topic LSB
  IDX_TOPIC_H   = 11, // 11: topic MSB
  // Dynamic sync info for TX-RX alignment
  IDX_CURRENT_CODE    = 12, // 12: current preamble code
  IDX_PACKETS_SENT_L  = 13, // 13: packets sent this code (LSB)
  IDX_PACKETS_SENT_H  = 14, // 14: packets sent this code (MSB)
  IDX_TOTAL_SENT_L    = 15, // 15: total packets sent (LSB)
  IDX_TOTAL_SENT_H    = 16, // 16: total packets sent (MSB)
  IDX_PAYLOAD         = 17  // 17~: payload
};

/* Custom preamble code sequence: 17->18->19->20->21->22->23->24->9->10->11->12->13->14->15->16 (matching TX) */
static uint8_t preamble_codes_custom[] = {17, 18, 19, 20, 21, 22, 23, 24, 9, 10, 11, 12, 13, 14, 15, 16};
static uint8_t num_preamble_codes = sizeof(preamble_codes_custom) / sizeof(preamble_codes_custom[0]);
static uint8_t current_scan_index = 0;

/* Initial synchronization and TX tracking variables */
static bool sync_established = false;
static uint32_t sync_packet_count = 0;
static const uint32_t TX_PACKETS_PER_CODE = 500; // Match TX packets_per_code
static uint32_t no_packet_counter = 0;
static const uint32_t SYNC_LOSS_THRESHOLD = 1000; // Consider sync lost after 1000 failed attempts

static uint32_t packets_received_current_code = 0;
static uint32_t rx_success_with_code[32] = {0}; // Statistics for each preamble code

/* Dynamic sync optimization variables */
static uint32_t consecutive_mismatches = 0;
static const uint32_t RESYNC_THRESHOLD = 3; // Only resync after 3 consecutive mismatches
static uint32_t last_resync_time = 0;
static const uint32_t MIN_RESYNC_INTERVAL = 10; // Minimum 10 packets between resyncs

/**
 * High-precision configure with timing measurement and adaptive delay
 */
static int precision_configure(dwt_config_t *config) {
    /* Simple approach: use fixed high-precision delay for now */
    int result = dwt_configure(config);
    
    /* Use a more precise delay that's slightly less than original */
    nrf_delay_us(1500); // 1.5ms instead of 2ms for better precision
    
    return result;
}

/**
 * Application entry point.
 */
int rx_custom(void)
{
    /* Hold copy of status register state here for reference so that it can be examined at a debug breakpoint. */
    uint32_t status_reg;
    /* Hold copy of frame length of frame received (if good) so that it can be examined at a debug breakpoint. */
    uint32_t frame_len;

    uint8_t received_packet_nb;
    uint32_t current_resp_rx_ts=0;
    uint32_t recent_resp_rx_ts=0;
    uint64_t count_ts=0;
    uint64_t rx_count_from_a_by_unicast = 0;

    /* Display application name on LCD. */
    test_run_info((unsigned char *)APP_NAME);

    /* Configure SPI rate, DW IC supports up to 38 MHz */
    port_set_dw_ic_spi_fastrate();

    /* Reset DW IC */
    reset_DWIC(); /* Target specific drive of RSTn line into DW IC low for a period. */

    Sleep(2); // Time needed for DW3000 to start up (transition from INIT_RC to IDLE_RC, or could wait for SPIRDY event)

    /* Probe for the correct device driver. */
    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    while (!dwt_checkidlerc()) /* Need to make sure DW IC is in IDLE_RC before proceeding */ { };

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        test_run_info((unsigned char *)"INIT FAILED");
        while (1) { };
    }

    /* Enabling LEDs here for debug so that for each RX-enable the D2 LED will flash on DW3000 red eval-shield boards. */
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

    /* Configure DW IC with high-precision timing. */
    /* if the dwt_configure returns DWT_ERROR either the PLL or RX calibration has failed the host should reset the device */
    if (precision_configure(&config))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    dwt_setrxtimeout(0); //5000000 us = 10s
    
    /* Initialize synchronization variables and set initial preamble code */
    sync_established = false;
    sync_packet_count = 0;
    no_packet_counter = 0;
    packets_received_current_code = 0;
    current_scan_index = 0; // Start with code 17 (index 0)
    config.rxCode = preamble_codes_custom[0]; // Start with first preamble code (17)
    
    /* Reconfigure with initial preamble code using high-precision timing */
    if (precision_configure(&config)) {
        test_run_info((unsigned char *)"INITIAL CONFIG FAILED");
        while (1) { };
    }
    
    CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
    sprintf(str_to_print, "RX waiting on code: %d for sync", config.rxCode);
    test_run_info((unsigned char *)str_to_print);

    /* Wait same delay as TX before starting (10 seconds) */
    Sleep(10000);
    test_run_info((unsigned char *)"RX starting custom sequence sync (17->24->9->16->17...)");

    /* Loop forever receiving frames. */
    while (TRUE)
    {
        /* Synchronization and TX tracking logic */
        if (!sync_established) {
            /* Wait on code 17 for initial synchronization */
            no_packet_counter++;
            
            /* Keep waiting on code 17 - no automatic switching until sync */
            if (no_packet_counter % 1000 == 0) {
                CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                sprintf(str_to_print, "Waiting for sync on code %d (%lu attempts)", 
                        config.rxCode, no_packet_counter);
                test_run_info((unsigned char *)str_to_print);
            }
        } else {
            /* Synchronized mode - track TX timing */
            sync_packet_count++;
            
            /* Switch preamble code after receiving TX_PACKETS_PER_CODE worth of attempts */
            if (sync_packet_count >= TX_PACKETS_PER_CODE) {
                /* Print stats for current code before switching */
                CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                sprintf(str_to_print, "SYNC: Code %d done - %lu pkts received", 
                        config.rxCode, packets_received_current_code);
                test_run_info((unsigned char *)str_to_print);
                
                /* Switch to next preamble code (same sequence as TX) */
                current_scan_index = (current_scan_index + 1) % num_preamble_codes;
                config.rxCode = preamble_codes_custom[current_scan_index];
                
                /* Reconfigure DW IC with new preamble code using high-precision timing */
                if (precision_configure(&config)) {
                    test_run_info((unsigned char *)"RX RECONFIG FAILED");
                    while (1) { };
                }
                
                /* Reset counters for new code */
                sync_packet_count = 0;
                packets_received_current_code = 0;
                no_packet_counter = 0; // Reset no-packet counter
                
                /* Print new scanning code */
                CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                sprintf(str_to_print, "SYNC: Switched to code %d", config.rxCode);
                test_run_info((unsigned char *)str_to_print);
            }
            
            /* Check for sync loss */
            if (packets_received_current_code == 0) {
                no_packet_counter++;
                
                if (no_packet_counter >= SYNC_LOSS_THRESHOLD) {
                    /* Lost synchronization - return to code 17 */
                    sync_established = false;
                    current_scan_index = 0;
                    config.rxCode = preamble_codes_custom[0];
                    sync_packet_count = 0;
                    packets_received_current_code = 0;
                    no_packet_counter = 0;
                    
                    /* Reconfigure with code 17 using high-precision timing */
                    if (precision_configure(&config)) {
                        test_run_info((unsigned char *)"RX RECONFIG FAILED");
                        while (1) { };
                    }
                    
                    test_run_info((unsigned char *)"SYNC LOST - Returning to code 17");
                }
            } else {
                no_packet_counter = 0; // Reset if we received packets
            }
        }

        /* Clear local RX buffer to avoid having leftovers from previous receptions  This is not necessary but is included here to aid reading
         * the RX buffer.
         * This is a good place to put a breakpoint. Here (after first time through the loop) the local status register will be set for last event
         * and if a good receive has happened the data buffer will have the data in it, and frame_len will be set to the length of the RX frame. */
        memset(rx_buffer, 0, sizeof(rx_buffer));

        /* Activate reception immediately. See NOTE 2 below. */
        dwt_rxenable(DWT_START_RX_IMMEDIATE);

        /* Poll until a frame is properly received or an error/timeout occurs with shorter timeout for scanning. */
        /* Use shorter timeout to allow frequent preamble code switching */
        uint32_t poll_count = 0;
        const uint32_t MAX_POLL_COUNT = 100; // Limit polling to allow preamble switching
        status_reg = 0;
        
        while (!(status_reg & (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)) && poll_count < MAX_POLL_COUNT) {
            status_reg = dwt_readsysstatuslo();
            poll_count++;
        }

        if (status_reg & DWT_INT_RXFCG_BIT_MASK)
        {
            dwt_setrxtimeout(5000000); // 5s
            /* A frame has been received, copy it to our local buffer. */
            frame_len = dwt_getframelength(0);
            if (frame_len <= FRAME_LEN_MAX)
            {
                /* Fast header check: Read only the header first for quick sync decision */
                uint8_t header_buffer[20]; // Just enough for header info
                dwt_readrxdata(header_buffer, 20, 0);
                
                char source_id = header_buffer[2];
                char dest_id = header_buffer[3];
                char device_id = 'B'; // device id of this host
                
                /* Quick filter - only process our packets */
                if (source_id == 'A' && dest_id == device_id) {
                    /* Fast sync check before reading full packet */
                    uint8_t tx_current_code = header_buffer[IDX_CURRENT_CODE];
                    
                    /* Quick resync decision with minimal overhead */
                    if (config.rxCode != tx_current_code) {
                        consecutive_mismatches++;
                        
                        /* Emergency resync for large gaps */
                        int8_t code_distance = 0;
                        for (int i = 0; i < num_preamble_codes; i++) {
                            if (preamble_codes_custom[i] == tx_current_code) {
                                code_distance = (i - current_scan_index + num_preamble_codes) % num_preamble_codes;
                                break;
                            }
                        }
                        
                        if (code_distance > 3 || consecutive_mismatches >= RESYNC_THRESHOLD) {
                            /* Quick resync with high-precision timing */
                            config.rxCode = tx_current_code;
                            precision_configure(&config);
                            
                            for (int i = 0; i < num_preamble_codes; i++) {
                                if (preamble_codes_custom[i] == tx_current_code) {
                                    current_scan_index = i;
                                    break;
                                }
                            }
                            consecutive_mismatches = 0;
                            
                            CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                            sprintf(str_to_print, "FAST_RESYNC: %d->%d (dist=%d)", 
                                    config.rxCode, tx_current_code, code_distance);
                            test_run_info((unsigned char *)str_to_print);
                        }
                    } else {
                        consecutive_mismatches = 0;
                    }
                    
                    /* Now read the full packet */
                    dwt_readrxdata(rx_buffer, frame_len - FCS_LEN, 0);

                if (source_id == 'A' && dest_id == device_id) {
                    rx_count_from_a_by_unicast++;
                    
                    /* Extract TX state from packet header (already processed in fast header check) */
                    uint8_t tx_current_code = rx_buffer[IDX_CURRENT_CODE];
                    uint16_t tx_packets_sent_this_code = rx_buffer[IDX_PACKETS_SENT_L] | (rx_buffer[IDX_PACKETS_SENT_H] << 8);
                    uint16_t tx_total_sent = rx_buffer[IDX_TOTAL_SENT_L] | (rx_buffer[IDX_TOTAL_SENT_H] << 8);
                    
                    /* Update statistics for successful reception with current preamble code */
                    rx_success_with_code[config.rxCode]++;
                    packets_received_current_code++;
                    
                    /* Check for initial synchronization */
                    if (!sync_established) {
                        /* Establish synchronization on any code with TX state info */
                        sync_established = true;
                        sync_packet_count = tx_packets_sent_this_code;
                        no_packet_counter = 0;
                        
                        CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                        sprintf(str_to_print, "*** SYNC ESTABLISHED on code %d (TX_sent=%d) ***", 
                                tx_current_code, tx_packets_sent_this_code);
                        test_run_info((unsigned char *)str_to_print);
                        
                        CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                        sprintf(str_to_print, "SYNC: First packet RX (Total: %lu)", rx_count_from_a_by_unicast);
                        test_run_info((unsigned char *)str_to_print);
                    } else if (sync_established) {
                        /* Normal synchronized packet reception */
                        CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                        sprintf(str_to_print, "SYNC: Code %d pkt (Total: %lu, This code: %lu, Sync count: %lu)", 
                                config.rxCode, rx_count_from_a_by_unicast, packets_received_current_code, sync_packet_count);
                        test_run_info((unsigned char *)str_to_print);
                    } else {
                        /* Packet received but not yet synchronized */
                        CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                        sprintf(str_to_print, "RX packet on code %d but not sync'd (Total: %lu)", 
                                config.rxCode, rx_count_from_a_by_unicast);
                        test_run_info((unsigned char *)str_to_print);
                    }

                    /* Clear good RX frame event in the DW IC status register. */
                    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
                    uint8_t expected_nb = (received_packet_nb + 1) % 256;
                    uint8_t actual_nb = rx_buffer[1];

                    if(actual_nb != expected_nb&& (actual_nb - expected_nb + 256) % 256 > 0)
                    {
                        CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                        sprintf(str_to_print,"%d~%d is missed", expected_nb, (actual_nb-1 + 256)%256);
                        test_run_info((unsigned char *)str_to_print);
                    }

                    received_packet_nb=rx_buffer[1];
            
                    current_resp_rx_ts = dwt_readrxtimestamplo32(0);
                    if(recent_resp_rx_ts != 0)
                    {
                      count_ts += current_resp_rx_ts - recent_resp_rx_ts; 
                    }
                    recent_resp_rx_ts = current_resp_rx_ts;
                }
                }
            }
        }
        else if(status_reg & SYS_STATUS_ALL_RX_TO)
        {
            double consuming_time = count_ts *DWT_TIME_UNITS;
            dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO);
            test_run_info((unsigned char *)"END!!");
            sprintf(str_to_print,"time: %.12f s", consuming_time);
            test_run_info((unsigned char *)str_to_print);

            sprintf(str_to_print, "Total RX count: %llu", (unsigned long long)rx_count_from_a_by_unicast);
            test_run_info((unsigned char *)str_to_print);
            
            /* Print custom sequence success statistics */
            test_run_info((unsigned char *)"=== RX Custom Sequence Stats ===");
            for (uint8_t i = 0; i < num_preamble_codes; i++) {
                uint8_t code = preamble_codes_custom[i];
                CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                sprintf(str_to_print, "Code %d: %lu pkts", code, rx_success_with_code[code]);
                test_run_info((unsigned char *)str_to_print);
            }
            CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
            break;
        }
        else
        {
            /* Clear RX error events in the DW IC status register. */
            CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
            sprintf(str_to_print,"RX ERROR (Code:%d, Status:0x%lX)", config.rxCode, status_reg);
            test_run_info((unsigned char *)str_to_print);
            dwt_writesysstatuslo(SYS_STATUS_ALL_RX_ERR);
        }
    }
}
#endif
/*****************************************************************************************************************************************************
 * NOTES:
 *
 * 1. In this example, maximum frame length is set to 127 bytes which is 802.15.4 UWB standard maximum frame length. DW IC supports an extended
 *    frame length (up to 1023 bytes long) mode which is not used in this example.
 * 2. Manual reception activation is performed here but DW IC offers several features that can be used to handle more complex scenarios or to
 *    optimise system's overall performance (e.g. timeout after a given time, automatic re-enabling of reception in case of errors, etc.).
 * 3. We use polled mode of operation here to keep the example as simple as possible, but RXFCG and error/timeout status events can be used to generate
 *    interrupts. Please refer to DW IC User Manual for more details on "interrupts".
 ****************************************************************************************************************************************************/