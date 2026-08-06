/*! ----------------------------------------------------------------------------
 *  @file    simple_tx.c
 *  @brief   Simple TX example code
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

#if defined(TEST_TX_UNICAST)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "TX Unicast Interference Test v1.1"

#define USE_SPI2 1 // set this to 1 to use DW37X0 SPI2

/* Interference test Frame type - different from protocol (0x41/0xC5) */
#define INTERFERENCE_FRAME_TYPE 0xAA

/* TX Power Index (same as protocol) */
#define TX_POWER_INDEX_0dB      0
#define TX_POWER_INDEX_3dB     12
#define TX_POWER_INDEX_10dB    40
#define TX_POWER_INDEX_15dB    60
#define TX_POWER_INDEX_20dB    80

#define USE_TX_POWER_INDEX  TX_POWER_INDEX_15dB  /* Set same as protocol */

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
    DWT_STS_MODE_OFF, /* No STS mode enabled (STS Mode 0). */
    DWT_STS_LEN_64,   /* STS length, see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       /* PDOA mode off */
};

enum {
  IDX_FTYPE     = 0,  // 0: frame type
  IDX_SEQ       = 1,  // 1: seq
  IDX_DEV_ID    = 2,  // 2~9: device ID
  IDX_TOPIC_L   = 10, // 10: topic LSB
  IDX_TOPIC_H   = 11, // 11: topic MSB
  IDX_PAYLOAD   = 12, // 12~: payload
  TX_MSG_SIZE   = 120
};

/* The frame sent in this example is an 802.15.4e standard blink. It is a 12-byte frame composed of the following fields:
 *     - byte 0: frame type (0xC5 for a blink).
 *     - byte 1: sequence number, incremented for each new frame.
 *     - byte 2 -> 9: device ID, see NOTE 1 below.
 */
static uint8_t tx_msg[120] = {
INTERFERENCE_FRAME_TYPE, // Frame type (0xAA - unique for interference test)
0,    // Seq num
'C',  // Source id <- device id of this host
'D',  // Dest id
'C', 'A', 'W', 'A', 'V', 'E',[10 ... 119]= 0x00 };
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

/**
 * Application entry point.
 */
int tx_unicast(void)
{
#if USE_SPI2
    uint8_t sema_res;
#endif
    uint32_t dev_id;

    //HYEOKJAE ADDED
    uint32_t total_send = 0;

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

    /* Configure the TX spectrum parameters using Linear TX Power API (same as protocol) */
    {
        power_indexes_t power_indexes = {0};
        tx_adj_res_t linear_results = {0};
        dwt_txconfig_t linear_txconfig;

        power_indexes.input[0] = USE_TX_POWER_INDEX;
        power_indexes.input[1] = USE_TX_POWER_INDEX;
        power_indexes.input[2] = USE_TX_POWER_INDEX;
        power_indexes.input[3] = USE_TX_POWER_INDEX;

        if (dwt_calculate_linear_tx_setting((int)config.chan, &power_indexes, &linear_results) == DWT_SUCCESS) {
            linear_txconfig.power = linear_results.tx_frame_cfg.tx_power_setting;
            linear_txconfig.PGcount = txconfig_options.PGcount;
            linear_txconfig.PGdly = txconfig_options.PGdly;
            dwt_configuretxrf(&linear_txconfig);
            dwt_set_pll_config(linear_results.tx_frame_cfg.pll_cfg);
        } else {
            /* Fallback to default config if linear calculation fails */
            dwt_configuretxrf(&txconfig_options);
        }
    }
    
    Sleep(20000);  // 20초 대기 (프로토콜 init과 동일)
    /* Loop forever sending frames periodically. */
    while (1)
    {
        /* Write frame data to DW IC and prepare transmission. See NOTE 3 below.*/
        dwt_writetxdata(FRAME_LENGTH - FCS_LEN, tx_msg, 0); /* Zero offset in TX buffer. */

        /* In this example since the length of the transmitted frame does not change,
         * nor the other parameters of the dwt_writetxfctrl function, the
         * dwt_writetxfctrl call could be outside the main while(1) loop.
         */
        dwt_writetxfctrl(FRAME_LENGTH, 0, 0); /* Zero offset in TX buffer, no ranging. */

        /* Start transmission. */
        dwt_starttx(DWT_START_TX_IMMEDIATE);
        /* Poll DW IC until TX frame sent event set. See NOTE 4 below.
         * STATUS register is 4 bytes long but, as the event we are looking
         * at is in the first byte of the register, we can use this simplest
         * API function to access it.*/
        waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

        /* Clear TX frame sent event. */
        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

        //test_run_info((unsigned char *)"TX Frame Sent");
        total_send++;
        // 320000 packets for 127.2 seconds + margin (프로토콜 1000 cycles 커버)
        if(total_send >= 320000)
        {
          test_run_info((unsigned char *)"End!!");
          break;
        }
        /* Execute a delay between transmissions. */
        nrf_delay_us(TX_DELAY_US);

        /* Increment the blink frame sequence number (modulo 256). */
        tx_msg[BLINK_FRAME_SN_IDX]++;
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
