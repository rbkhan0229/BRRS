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

#if defined(TEST_TX_WAIT_RESP)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "TX WAITRESP v2.0"

/* Default communication configuration */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_128,     /* Preamble length. */
    DWT_PAC8,         /* PAC size. */
    9,                /* TX preamble code. */
    9,                /* RX preamble code. */
    1,                /* SFD type. */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (129 + 8 - 8),    /* SFD timeout. */
    DWT_STS_MODE_OFF, /* STS disabled. */
    DWT_STS_LEN_64,   /* STS length. */
    DWT_PDOA_M0       /* PDOA off. */
};

/* Blink frame */
static uint8_t tx_msg[] = { 0xC5, 0, 'D', 'E', 'C', 'A', 'W', 'A', 'V', 'E', 0x43, 0x02, 0, 0 };
#define BLINK_FRAME_SN_IDX 1

/* Configs */
#define TX_DELAY_MS       1000
#define TX_TO_RX_DELAY_UUS 60
#define RX_RESP_TO_UUS    5000

/* 수신 버퍼 */
static uint8_t rx_buffer[FRAME_LEN_MAX];

/* RF TX config */
extern dwt_txconfig_t txconfig_options;

/* 여러 응답 수신 관련 */
#define MAX_RESPONSES 10

int tx_wait_resp(void)
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

    if (dwt_configure(&config))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    dwt_configuretxrf(&txconfig_options);

    dwt_setrxaftertxdelay(TX_TO_RX_DELAY_UUS);
    dwt_setrxtimeout(RX_RESP_TO_UUS);

    while (1)
    {
        /* 1. 프레임 송신 */
        dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
        dwt_writetxfctrl(sizeof(tx_msg), 0, 0);
        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

        test_run_info((unsigned char *)"TX done, waiting for responses...");

        /* 2. 여러 응답 수신 루프 */
        int rx_count = 0;
        while (rx_count < MAX_RESPONSES)
        {
            waitforsysstatus(&status_reg, NULL,
                             (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR),
                             0);

            if (status_reg & DWT_INT_RXFCG_BIT_MASK)
            {
                /* 정상 프레임 수신 */
                frame_len = dwt_getframelength(0);
                if (frame_len <= FRAME_LEN_MAX)
                {
                    dwt_readrxdata(rx_buffer, frame_len, 0);
                }

                dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

                rx_count++;

                /* 수신 내용 출력 */
                static char info_str[100];
                snprintf(info_str, sizeof(info_str), "Resp %d len: %d, Data: %02X %02X %02X %02X",
                         rx_count, frame_len,
                         rx_buffer[0], rx_buffer[1], rx_buffer[2], rx_buffer[3]);
                test_run_info((unsigned char *)info_str);

                /* 다시 RX 켜기 */
                dwt_setrxtimeout(RX_RESP_TO_UUS);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
            else
            {
                /* 타임아웃/에러 시 루프 종료 */
                dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
                break;
            }
        }

        /* 3. 인터프레임 딜레이 */
        Sleep(TX_DELAY_MS);

        tx_msg[BLINK_FRAME_SN_IDX]++;
    }
}
#endif
