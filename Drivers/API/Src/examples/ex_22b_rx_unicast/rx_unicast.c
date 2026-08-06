/*! ----------------------------------------------------------------------------
 *  @file    simple_rx.c
 *  @brief   Simple RX example code
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

#if defined(TEST_RX_UNICAST)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "RX Unicast Interference Test v1.1" // Interference test

/* Interference test Frame type - different from protocol (0x41/0xC5) */
#define INTERFERENCE_FRAME_TYPE 0xAA

/* Default communication configuration. Channel 9, PLEN_64 (same as protocol DATA) */
static dwt_config_t config = {
    9,                /* Channel 9 (same as protocol) */
    DWT_PLEN_64,      /* Preamble length PLEN_64 (same as protocol DATA) */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (64 + 1 + 8 - 8), /* SFD timeout for PLEN_64. Used in RX only. */
    DWT_STS_MODE_OFF, /* STS disabled */
    DWT_STS_LEN_64,   /* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       /* PDOA mode off */
};

/* Buffer to store received frame. See NOTE 1 below. */
static uint8_t rx_buffer[FRAME_LEN_MAX];

//HYEOKJAE ADDED
static char str_to_print[15];

static uint16_t expected_packet_nb=0;
#define CLEAR_ARRAY(array, size) for(int i = 0; i < size; i++) array[i] = 0

//END

/**
 * Application entry point.
 */
int rx_unicast(void)
{
    /* Hold copy of status register state here for reference so that it can be examined at a debug breakpoint. */
    uint32_t status_reg;
    /* Hold copy of frame length of frame received (if good) so that it can be examined at a debug breakpoint. */
    uint32_t frame_len;

    //HYEOKJAE ADDED
    uint8_t received_packet_nb;
    uint32_t current_resp_rx_ts=0;
    uint32_t recent_resp_rx_ts=0;
    uint64_t count_ts=0;
    //END
    uint64_t rx_count_from_a_by_unicast = 0;
    uint64_t rx_count_from_a = 0;
    uint64_t rx_count_from_b = 0;
    uint64_t rx_count_from_c = 0;
    uint64_t rx_count_from_d = 0;



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

    /* Configure DW IC. */
    /* if the dwt_configure returns DWT_ERROR either the PLL or RX calibration has failed the host should reset the device */
    if (dwt_configure(&config))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    dwt_setrxtimeout(0); //5000000 us = 10s


    /* Loop forever receiving frames. */
    while (TRUE)
    {
        /* TESTING BREAKPOINT LOCATION #1 */
        

        /* Clear local RX buffer to avoid having leftovers from previous receptions  This is not necessary but is included here to aid reading
         * the RX buffer.
         * This is a good place to put a breakpoint. Here (after first time through the loop) the local status register will be set for last event
         * and if a good receive has happened the data buffer will have the data in it, and frame_len will be set to the length of the RX frame. */
        memset(rx_buffer, 0, sizeof(rx_buffer));

        /* Activate reception immediately. See NOTE 2 below. */
        dwt_rxenable(DWT_START_RX_IMMEDIATE);

        /* Poll until a frame is properly received or an error/timeout occurs. See NOTE 3 below.
         * STATUS register is 5 bytes long but, as the event we are looking at is in the first byte of the register, we can use this simplest API
         * function to access it. */
        waitforsysstatus(&status_reg, NULL, (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO |SYS_STATUS_ALL_RX_ERR), 0);

        if (status_reg & DWT_INT_RXFCG_BIT_MASK)
        {
            dwt_setrxtimeout(5000000); // 5s
            /* A frame has been received, copy it to our local buffer. */
            frame_len = dwt_getframelength(0);
            if (frame_len <= FRAME_LEN_MAX)
            {
                dwt_readrxdata(rx_buffer, frame_len - FCS_LEN, 0); /* No need to read the FCS/CRC. */

                char source_id;
                char dest_id;
                source_id = rx_buffer[2];
                dest_id = rx_buffer[3];
                char device_id = 'D'; // device id of this host


                /* Filter: only accept interference test packets (Frame type 0xAA) */
                if (rx_buffer[0] == INTERFERENCE_FRAME_TYPE && source_id == 'C' && dest_id == device_id) {
                    rx_count_from_a_by_unicast++;
                    test_run_info((unsigned char *)"Frame Received");

                    /* Clear good RX frame event in the DW IC status register. */
                    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
                    uint8_t expected_nb = (received_packet_nb + 1) % 256;
                    uint8_t actual_nb = rx_buffer[1];
                    //HYEOKJAE ADDED

                    if(actual_nb != expected_nb&& (actual_nb - expected_nb + 256) % 256 > 0)
                    {
                        CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                        //sprintf(str_to_print, "%d is lossed", expected_packet_nb);
                        sprintf(str_to_print,"%d~%d is missed", expected_nb, (actual_nb-1 + 256)%256);
                
                        test_run_info((unsigned char *)str_to_print);
                        //END
                    }
                   // CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
                
                   //sprintf(str_to_print,"%d is received", rx_buffer[1]);
                
                   //test_run_info((unsigned char *)str_to_print);
            
           

                    received_packet_nb=rx_buffer[1];
            

                    //if (header_device_id == 'A') {
                    //    rx_count_from_a++;
                    //} else if (header_device_id == 'B') {
                    //    rx_count_from_b++;
                    //} else if (header_device_id == 'C') {
                    //    rx_count_from_c++;
                    //} else if (header_device_id == 'D') {
                    //    rx_count_from_d++;
                    //}
                    current_resp_rx_ts = dwt_readrxtimestamplo32(0);
                    if(recent_resp_rx_ts != 0)
                    {
                      count_ts += current_resp_rx_ts - recent_resp_rx_ts; 
                    }
                    recent_resp_rx_ts = current_resp_rx_ts;
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

            sprintf(str_to_print, "RX count from A: %llu\n", (unsigned long long)rx_count_from_a_by_unicast);
            test_run_info((unsigned char *)str_to_print);
            CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
            //sprintf(str_to_print, "RX count from B: %llu\n", (unsigned long long)rx_count_from_b);
            //test_run_info((unsigned char *)str_to_print);
            //CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
            //sprintf(str_to_print, "RX count from C: %llu\n", (unsigned long long)rx_count_from_c);
            //test_run_info((unsigned char *)str_to_print);
            //CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
            //sprintf(str_to_print, "RX count from D: %llu\n", (unsigned long long)rx_count_from_d);
            //test_run_info((unsigned char *)str_to_print);
            //CLEAR_ARRAY(str_to_print, sizeof(str_to_print));
            break;
        }
        else
        {
            /* Clear RX error events in the DW IC status register. */
            sprintf(str_to_print,"%d ERROR", received_packet_nb+1);
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