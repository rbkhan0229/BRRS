/*! ----------------------------------------------------------------------------
 * @file    example_selection.h
 * @brief   Example selection is configured here
 *
 * @author Decawave
 *
 * @copyright SPDX-FileCopyrightText: Copyright (c) 2024 Qorvo US, Inc.
 *            SPDX-License-Identifier: LicenseRef-QORVO-2
 *
 */

#ifndef TEST_SELECTION_
#define TEST_SELECTION_

#ifdef __cplusplus
extern "C"
{
#endif

    // Enable the needed example/test. Please enable only one example/test!

//#define TEST_READING_DEV_ID

// preamble pre-defined sequence hopping TX
//#define TEST_SIMPLE_TX

//#define TEST_SIMPLE_TX_PDOA
//#define TEST_TX_SLEEP
//#define TEST_TX_SLEEP_IDLE_RC
//#define TEST_TX_SLEEP_AUTO
//#define TEST_TX_SLEEP_TIMED
//#define TEST_TX_WITH_CCA
//#define TEST_SIMPLE_TX_AUTOMOTIVE

// preamble pre-defined sequence hopping TX
//#define TEST_SIMPLE_RX

//#define TEST_SIMPLE_RX_NLOS
//#define TEST_RX_DIAG
//#define TEST_RX_SNIFF
//#define TEST_DOUBLE_BUFFER_RX
//#define TEST_RX_TRIM
//#define TEST_SIMPLE_RX_PDOA
//#define TEST_SIMPLE_RX_CIR

//#define TEST_SIMPLE_TX_STS_SDC
//#define TEST_SIMPLE_RX_STS_SDC

//#define TEST_SIMPLE_TX_AES
//#define TEST_SIMPLE_RX_AES

//#define TEST_TX_WAIT_RESP
//#define TEST_TX_WAIT_RESP_INT
//#define TEST_RX_SEND_RESP
//#define TEST_RX_ADC_CAPTURE

//#define TEST_CONTINUOUS_WAVE
//#define TEST_CONTINUOUS_FRAME

//#define TEST_DS_TWR_INITIATOR_STS
//#define TEST_DS_TWR_RESPONDER_STS

//#define TEST_DS_TWR_INITIATOR
//#define TEST_DS_TWR_RESPONDER

//#define TEST_DS_TWR_STS_SDC_INITIATOR
//#define TEST_DS_TWR_STS_SDC_RESPONDER

//#define TEST_SS_TWR_INITIATOR
//#define TEST_SS_TWR_RESPONDER

//#define TEST_SS_TWR_INITIATOR_STS
//#define TEST_SS_TWR_RESPONDER_STS

//#define TEST_SS_TWR_INITIATOR_STS_NO_DATA
//#define TEST_SS_TWR_RESPONDER_STS_NO_DATA

//#define TEST_AES_SS_TWR_INITIATOR
//#define TEST_AES_SS_TWR_RESPONDER

//#define TEST_ACK_DATA_TX
//#define TEST_ACK_DATA_RX

//#define TEST_SPI_CRC
//#define TEST_GPIO
//#define TEST_TIMER

//#define TEST_OTP_WRITE

//#define TEST_LE_PEND_TX
//#define TEST_LE_PEND_RX

//#define TEST_PLL_CAL

//#define TEST_BW_CAL

//#define TEST_FRAME_FILTERING_TX
//#define TEST_FRAME_FILTERING_RX

//#define TEST_TX_POWER_ADJUSTMENT
//#define TEST_LINEAR_TX_POWER

//#define TEST_SIMPLE_AES

//#define TEST_DDS_PUB

// 이게 기본 simple tx
//#define TEST_TX_UNICAST

// 이게 기본 simple rx
//#define TEST_RX_UNICAST

// 커스텀 순환 (17->24->9)
//#define TEST_TX_CUSTOM
//#define TEST_RX_CUSTOM

// UWB 프로토콜 Initiator
//#define TEST_UWB_PROTOCOL_INIT

// UWB 프로토콜 Normal Node
//#define TEST_UWB_PROTOCOL_NORMAL_NODE

// UWB 프로토콜 New Initiator (interrupt-based)
//#define TEST_UWB_PROTOCOL_NEW_INIT

// Initiator Node Polling (DWT timer-based)
//#define TEST_INITIATOR_NODE_POLLING

// Normal Node Polling (DWT timer-based)
//#define TEST_NORMAL_NODE_POLLING

// Initiator Node (tx_wait_resp + DWT timers + queues)
//#define TEST_INITIATOR_NODE

// Normal Node (rx_send_resp + DWT timers + queues)
//#define TEST_NORMAL_NODE

// DWT Cycle Counter Initiator Node (SPI-free timing)
//#define TEST_DWT_CYCLE_COUNTER_INIT

// DWT Cycle Counter Normal Node (SPI-free timing)
//#define TEST_DWT_CYCLE_COUNTER_NORMAL


//#define TEST_INIT_AGGREGATED_ACKS

//#define TEST_NORMAL_AGGREGATED_ACKS

//#define TEST_WITHOUT_RELAY_AND_RETRANS_INIT

//#define TEST_WITHOUT_RELAY_AND_RETRANS_NORMAL

//#define TEST_ONLY_RETRANS_INIT

//#define TEST_ONLY_RETRANS_NORMAL


//------------------------------------------

//#define TEST_INIT_MORE_RETRANS
  
//#define TEST_NORMAL_MORE_RETRANS
    
//#define TEST_INIT_PREAMBLE_HOP

//#define TEST_NORMAL_PREAMBLE_HOP

//------ 2-Node Test (INIT + N5 only) ------
//#define TEST_INIT_AGGREGATED_ACKS_2NODES
//#define TEST_NORMAL_AGGREGATED_ACKS_2NODES
//#define TEST_INIT_PREAMBLE_HOP_2NODES
//#define TEST_NORMAL_PREAMBLE_HOP_2NODES


//#define TEST_INTERFERENCE_PREAMBLE_HOP
//#define TEST_INTERFERENCE_NONHOP

//------ BRRS Experiment (Preamble Reduction PER/CIR/TDMA) ------
	/* Command-line builds may define one role without editing this file.
	 * SES Build and Debug keeps INIT as the default. */
	#if !defined(TEST_BRRS_INIT) && !defined(TEST_BRRS_NORMAL)
		#define TEST_BRRS_INIT              // ex_35a: BRRS Experiment - INIT node
		//#define TEST_BRRS_NORMAL            // ex_35b: BRRS Experiment - Normal node
	#endif

//------ PAC4 Control (no BRRS delayed-RX; immediate RX before arrival) ------
	//#define TEST_PAC4_CONTROL_INIT       // ex_36a: PAC4 control - INIT/RX
	//#define TEST_PAC4_CONTROL_NORMAL     // ex_36b: matched timing - Normal/TX

#ifdef __cplusplus
}
#endif

#endif
