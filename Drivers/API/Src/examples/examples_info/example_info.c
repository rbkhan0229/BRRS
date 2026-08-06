/*! ----------------------------------------------------------------------------
 * @file    example_info.h
 * @brief
 *
 * @author Decawave
 *
 * @copyright SPDX-FileCopyrightText: Copyright (c) 2024 Qorvo US, Inc.
 *            SPDX-License-Identifier: LicenseRef-QORVO-2
 *
 */

#include "examples_defines.h"
#include <assert.h>
#include <example_selection.h>

example_ptr example_pointer;

void build_examples(void)
{
    unsigned char test_cnt = 0;

#ifdef TEST_READING_DEV_ID
    extern int read_dev_id(void);

    example_pointer = read_dev_id;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_TX
    extern int simple_tx(void);

    example_pointer = simple_tx;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_TX_PDOA
    extern int simple_tx_pdoa(void);

    example_pointer = simple_tx_pdoa;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_RX
    extern int simple_rx(void);

    example_pointer = simple_rx;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_RX_NLOS
    extern int simple_rx_nlos(void);

    example_pointer = simple_rx_nlos;
    test_cnt++;
#endif

#ifdef TEST_RX_SNIFF
    extern int rx_sniff(void);

    example_pointer = rx_sniff;
    test_cnt++;
#endif

#ifdef TEST_RX_TRIM
    extern int rx_with_xtal_trim(void);

    example_pointer = rx_with_xtal_trim;
    test_cnt++;
#endif

#ifdef TEST_RX_DIAG
    extern int rx_diagnostics(void);

    example_pointer = rx_diagnostics;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_RX_CIR
    extern int simple_rx_cir(void);

    example_pointer = simple_rx_cir;
    test_cnt++;
#endif

#ifdef TEST_TX_SLEEP
    extern int tx_sleep(void);

    example_pointer = tx_sleep;
    test_cnt++;
#endif

#ifdef TEST_TX_SLEEP_IDLE_RC
    extern int tx_sleep_idleRC(void);

    example_pointer = tx_sleep_idleRC;
    test_cnt++;
#endif

#ifdef TEST_TX_SLEEP_TIMED
    extern int tx_timed_sleep(void);

    example_pointer = tx_timed_sleep;
    test_cnt++;
#endif

#ifdef TEST_TX_SLEEP_AUTO
    extern int tx_sleep_auto(void);

    example_pointer = tx_sleep_auto;
    test_cnt++;
#endif

#ifdef TEST_TX_WITH_CCA
    extern int tx_with_cca(void);

    example_pointer = tx_with_cca;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_TX_AES
    extern int simple_tx_aes(void);

    example_pointer = simple_tx_aes;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_TX_AUTOMOTIVE
    extern int simple_tx_automotive(void);

    example_pointer = simple_tx_automotive;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_RX_AES
    extern int simple_rx_aes(void);

    example_pointer = simple_rx_aes;
    test_cnt++;
#endif

#ifdef TEST_TX_WAIT_RESP
    extern int tx_wait_resp(void);

    example_pointer = tx_wait_resp;
    test_cnt++;
#endif

#ifdef TEST_TX_WAIT_RESP_INT
    extern int tx_wait_resp_int(void);

    example_pointer = tx_wait_resp_int;
    test_cnt++;
#endif

#ifdef TEST_RX_SEND_RESP
    extern int rx_send_resp(void);

    example_pointer = rx_send_resp;
    test_cnt++;
#endif

#ifdef TEST_SS_TWR_RESPONDER
    extern int ss_twr_responder(void);

    example_pointer = ss_twr_responder;
    test_cnt++;
#endif

#ifdef TEST_SS_TWR_INITIATOR
    extern int ss_twr_initiator(void);

    example_pointer = ss_twr_initiator;
    test_cnt++;
#endif

#ifdef TEST_SS_TWR_INITIATOR_STS
    extern int ss_twr_initiator_sts(void);

    example_pointer = ss_twr_initiator_sts;
    test_cnt++;
#endif

#ifdef TEST_SS_TWR_RESPONDER_STS
    extern int ss_twr_responder_sts(void);

    example_pointer = ss_twr_responder_sts;
    test_cnt++;
#endif

#ifdef TEST_SS_TWR_INITIATOR_STS_NO_DATA
    extern int ss_twr_initiator_sts_no_data(void);

    example_pointer = ss_twr_initiator_sts_no_data;
    test_cnt++;
#endif

#ifdef TEST_SS_TWR_RESPONDER_STS_NO_DATA
    extern int ss_twr_responder_sts_no_data(void);

    example_pointer = ss_twr_responder_sts_no_data;
    test_cnt++;
#endif

#ifdef TX_RX_AES_VERIFICATION
    extern int tx_rx_aes_verification(void);

    example_pointer = tx_rx_aes_verification;
    test_cnt++;
#endif

#ifdef TEST_AES_SS_TWR_INITIATOR
    extern int ss_aes_twr_initiator(void);

    example_pointer = ss_aes_twr_initiator;
    test_cnt++;
#endif

#ifdef TEST_AES_SS_TWR_RESPONDER
    extern int ss_aes_twr_responder(void);

    example_pointer = ss_aes_twr_responder;
    test_cnt++;
#endif

#ifdef TEST_DS_TWR_INITIATOR
    extern int ds_twr_initiator(void);

    example_pointer = ds_twr_initiator;
    test_cnt++;
#endif

#ifdef TEST_DS_TWR_RESPONDER
    extern int ds_twr_responder(void);

    example_pointer = ds_twr_responder;
    test_cnt++;
#endif

#ifdef TEST_DS_TWR_RESPONDER_STS
    extern int ds_twr_responder_sts(void);

    example_pointer = ds_twr_responder_sts;
    test_cnt++;
#endif

#ifdef TEST_DS_TWR_INITIATOR_STS
    extern int ds_twr_initiator_sts(void);

    example_pointer = ds_twr_initiator_sts;
    test_cnt++;
#endif

#ifdef TEST_DS_TWR_STS_SDC_INITIATOR
    extern int ds_twr_sts_sdc_initiator(void);

    example_pointer = ds_twr_sts_sdc_initiator;
    test_cnt++;
#endif

#ifdef TEST_DS_TWR_STS_SDC_RESPONDER
    extern int ds_twr_sts_sdc_responder(void);

    example_pointer = ds_twr_sts_sdc_responder;
    test_cnt++;
#endif

#ifdef TEST_CONTINUOUS_WAVE
    extern int continuous_wave_example(void);

    example_pointer = continuous_wave_example;
    test_cnt++;
#endif

#ifdef TEST_CONTINUOUS_FRAME
    extern int continuous_frame_example(void);

    example_pointer = continuous_frame_example;
    test_cnt++;

#endif

#ifdef TEST_ACK_DATA_RX
    extern int ack_data_rx(void);

    example_pointer = ack_data_rx;
    test_cnt++;

#endif

#ifdef TEST_ACK_DATA_TX
    extern int ack_data_tx(void);

    example_pointer = ack_data_tx;
    test_cnt++;

#endif

#ifdef TEST_GPIO
    extern int gpio_example(void);

    example_pointer = gpio_example;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_TX_STS_SDC
    extern int simple_tx_sts_sdc(void);

    example_pointer = simple_tx_sts_sdc;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_RX_STS_SDC
    extern int simple_rx_sts_sdc(void);

    example_pointer = simple_rx_sts_sdc;
    test_cnt++;
#endif

#ifdef TEST_FRAME_FILTERING_TX
    extern int frame_filtering_tx(void);

    example_pointer = frame_filtering_tx;
    test_cnt++;
#endif

#ifdef TEST_FRAME_FILTERING_RX
    extern int frame_filtering_rx(void);

    example_pointer = frame_filtering_rx;
    test_cnt++;
#endif

#ifdef TEST_SPI_CRC
    extern int spi_crc(void);

    example_pointer = spi_crc;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_RX_PDOA
    extern int simple_rx_pdoa(void);

    example_pointer = simple_rx_pdoa;
    test_cnt++;
#endif

#ifdef TEST_OTP_WRITE
    extern int otp_write(void);

    example_pointer = otp_write;
    test_cnt++;
#endif

#ifdef TEST_LE_PEND_TX
    extern int le_pend_tx(void);

    example_pointer = le_pend_tx;
    test_cnt++;
#endif

#ifdef TEST_LE_PEND_RX
    extern int le_pend_rx(void);

    example_pointer = le_pend_rx;
    test_cnt++;
#endif

#ifdef TEST_PLL_CAL
    extern int pll_cal(void);

    example_pointer = pll_cal;
    test_cnt++;
#endif

#ifdef TEST_BW_CAL
    extern int bw_cal(void);

    example_pointer = bw_cal;
    test_cnt++;
#endif

#ifdef TEST_DOUBLE_BUFFER_RX
    extern int double_buffer_rx(void);

    example_pointer = double_buffer_rx;
    test_cnt++;
#endif

#ifdef TEST_TIMER
    extern int timer_example(void);

    example_pointer = timer_example;
    test_cnt++;
#endif

#ifdef TEST_TX_POWER_ADJUSTMENT
    extern int tx_power_adjustment_example(void);

    example_pointer = tx_power_adjustment_example;
    test_cnt++;
#endif

#ifdef TEST_SIMPLE_AES
    extern int simple_aes(void);

    example_pointer = simple_aes;
    test_cnt++;
#endif

#ifdef TEST_LINEAR_TX_POWER
    extern int linear_tx_power_example(void);

    example_pointer = linear_tx_power_example;
    test_cnt++;
#endif

#ifdef TEST_RX_ADC_CAPTURE
    extern int rx_adc_capture(void);

    example_pointer = rx_adc_capture;
    test_cnt++;
#endif

#ifdef TEST_DDS_PUB
    extern int uwb_dds_pub(void);
    

    example_pointer = uwb_dds_pub;
    test_cnt++;
#endif

#ifdef TEST_TX_UNICAST
    extern int tx_unicast(void);
    

    example_pointer = tx_unicast;
    test_cnt++;
#endif

#ifdef TEST_RX_UNICAST
    extern int rx_unicast(void);
    

    example_pointer = rx_unicast;
    test_cnt++;
#endif

#ifdef TEST_TX_CUSTOM
    extern int tx_custom(void);
    

    example_pointer = tx_custom;
    test_cnt++;
#endif

#ifdef TEST_RX_CUSTOM
    extern int rx_custom(void);
    

    example_pointer = rx_custom;
    test_cnt++;
#endif

#ifdef TEST_UWB_PROTOCOL_INIT
    extern int UWB_protocol_init(void);
    

    example_pointer = UWB_protocol_init;
    test_cnt++;
#endif

#ifdef TEST_UWB_PROTOCOL_NORMAL_NODE
    extern int UWB_protocol_normal_node(void);


    example_pointer = UWB_protocol_normal_node;
    test_cnt++;
#endif

#ifdef TEST_UWB_PROTOCOL_NEW_INIT
    extern int UWB_protocol_new_init(void);


    example_pointer = UWB_protocol_new_init;
    test_cnt++;
#endif

#ifdef TEST_INITIATOR_NODE_POLLING
    extern int initiator_node_polling(void);


    example_pointer = initiator_node_polling;
    test_cnt++;
#endif

#ifdef TEST_NORMAL_NODE_POLLING
    extern int normal_node_polling(void);


    example_pointer = normal_node_polling;
    test_cnt++;
#endif

#ifdef TEST_INITIATOR_NODE
    extern int initiator_node(void);


    example_pointer = initiator_node;
    test_cnt++;
#endif

#ifdef TEST_NORMAL_NODE
    extern int normal_node(void);


    example_pointer = normal_node;
    test_cnt++;
#endif

#ifdef TEST_DWT_CYCLE_COUNTER_INIT
    extern int dwt_cycle_counter_init(void);


    example_pointer = dwt_cycle_counter_init;
    test_cnt++;
#endif

#ifdef TEST_DWT_CYCLE_COUNTER_NORMAL
    extern int dwt_cycle_counter_normal(void);


    example_pointer = dwt_cycle_counter_normal;
    test_cnt++;
#endif

#ifdef TEST_INIT_AGGREGATED_ACKS
    extern int dwt_init_aggregated_acks(void);


    example_pointer = dwt_init_aggregated_acks;
    test_cnt++;
#endif

#ifdef TEST_NORMAL_AGGREGATED_ACKS
    extern int dwt_normal_aggregated_acks(void);


    example_pointer = dwt_normal_aggregated_acks;
    test_cnt++;
#endif

#ifdef TEST_WITHOUT_RELAY_AND_RETRANS_INIT
    extern int plain_schedule(void);


    example_pointer = plain_schedule;
    test_cnt++;
#endif

#ifdef TEST_WITHOUT_RELAY_AND_RETRANS_NORMAL
    extern int plain_schedule_normal_node(void);


    example_pointer = plain_schedule_normal_node;
    test_cnt++;
#endif

#ifdef TEST_ONLY_RETRANS_INIT
    extern int only_retrans_init(void);


    example_pointer = only_retrans_init;
    test_cnt++;
#endif

#ifdef TEST_ONLY_RETRANS_NORMAL
    extern int only_retrans_normal(void);


    example_pointer = only_retrans_normal;
    test_cnt++;
#endif

#ifdef TEST_INIT_MORE_RETRANS
    extern int dwt_init_more_retrans(void);


    example_pointer = dwt_init_more_retrans;
    test_cnt++;
#endif

#ifdef TEST_NORMAL_MORE_RETRANS
    extern int dwt_normal_more_retrans(void);


    example_pointer = dwt_normal_more_retrans;
    test_cnt++;
#endif

#ifdef TEST_INIT_PREAMBLE_HOP
    extern int dwt_init_preamble_hop(void);


    example_pointer = dwt_init_preamble_hop;
    test_cnt++;
#endif

#ifdef TEST_NORMAL_PREAMBLE_HOP
    extern int dwt_normal_preamble_hop(void);


    example_pointer = dwt_normal_preamble_hop;
    test_cnt++;
#endif

#ifdef TEST_INIT_AGGREGATED_ACKS_2NODES
    extern int dwt_init_aggregated_acks(void);

    example_pointer = dwt_init_aggregated_acks;
    test_cnt++;
#endif

#ifdef TEST_NORMAL_AGGREGATED_ACKS_2NODES
    extern int dwt_normal_aggregated_acks(void);

    example_pointer = dwt_normal_aggregated_acks;
    test_cnt++;
#endif

#ifdef TEST_INIT_PREAMBLE_HOP_2NODES
    extern int dwt_init_preamble_hop(void);

    example_pointer = dwt_init_preamble_hop;
    test_cnt++;
#endif

#ifdef TEST_NORMAL_PREAMBLE_HOP_2NODES
    extern int dwt_normal_preamble_hop(void);

    example_pointer = dwt_normal_preamble_hop;
    test_cnt++;
#endif

#ifdef TEST_INTERFERENCE_PREAMBLE_HOP
    extern int interference_preamble_hop(void);

    example_pointer = interference_preamble_hop;
    test_cnt++;
#endif

#ifdef TEST_INTERFERENCE_NONHOP
    extern int interference_nonhop(void);

    example_pointer = interference_nonhop;
    test_cnt++;
#endif

#ifdef TEST_BRRS_INIT
    extern int brrs_init(void);

    example_pointer = brrs_init;
    test_cnt++;
#endif

#ifdef TEST_BRRS_NORMAL
    extern int brrs_normal(void);

    example_pointer = brrs_normal;
    test_cnt++;
#endif

#ifdef TEST_PAC4_CONTROL_INIT
    extern int pac4_control_init(void);

    example_pointer = pac4_control_init;
    test_cnt++;
#endif

#ifdef TEST_PAC4_CONTROL_NORMAL
    extern int pac4_control_normal(void);

    example_pointer = pac4_control_normal;
    test_cnt++;
#endif

    // Check that only 1 test was enabled in test_selection.h file
    assert(test_cnt == 1);
}
