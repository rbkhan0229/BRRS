/*! ----------------------------------------------------------------------------
 *  @file    init_aggregated_acks.c
 *  @brief   INIT Node for Aggregated ACKs MAC Protocol (V2)
 *
 *           Aggregated ACKs 기반 UWB MAC 프로토콜의 Initiator 노드 구현.
 *           - SYNC 브로드캐스트로 네트워크 동기화
 *           - 12-슬롯 TDMA 구조 (10개 노드 + 2개 릴레이 슬롯)
 *           - Period 1,3,5: DATA 전송, Period 2,4,6: ACK_ARRAY 전송
 *           - FL/FR 노드를 위한 릴레이 기능 (SEQ 10, 12)
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

// Standard C includes
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Platform includes
#include "nrf_delay.h"
#include "nrf.h"

#if defined(TEST_INIT_AGGREGATED_ACKS)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "INITIATOR NODE v1.0"


/* ========== TDMA 프로토콜 파라미터 - Aggregated ACKs ========== */
// Period: 21.2ms (1.0ms guard time for all slots)
// Slot: 1.1ms (0.1ms TX + 1.0ms guard)
// Config switch: 17.2ms (4.0ms buffer for SYNC at 21.2ms)

#define TOTAL_NODES         10      // Total physical nodes in network
#define TOTAL_SLOTS         12      // Total TDMA slots (includes relay slots)
#define TOTAL_ARRAY_SIZE    12      // Array size for tracking (nodes + relay slots)
#define PERIODS_PER_CYCLE   6       // 6 periods per cycle (3 DATA/ACK pairs)
#define MY_NODE_SEQ         1       // INIT node - SEQ 1 (first slot after SYNC)


/* Default communication configuration for DATA/ACK - PLEN64 */
static dwt_config_t config_data = {
    9,                /* Channel number. */
    DWT_PLEN_64,     /* Preamble length - Fast for DATA/ACK */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (64 + 1 + 8 - 8),    /* SFD timeout for PLEN64 */
    DWT_STS_MODE_OFF, /* No STS mode enabled */
    DWT_STS_LEN_64,   /* STS length */
    DWT_PDOA_M0       /* PDOA mode off */
};

/* SYNC communication configuration - PLEN512 for better reliability */
static dwt_config_t config_sync = {
    9,                /* Channel number. */
    DWT_PLEN_512,    /* Preamble length - Long for SYNC reliability */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (512 + 1 + 8 - 8),   /* SFD timeout for PLEN512 */
    DWT_STS_MODE_OFF, /* No STS mode enabled */
    DWT_STS_LEN_64,   /* STS length */
    DWT_PDOA_M0       /* PDOA mode off */
};

/* ========== 노드 ID 정의 ========== */
#define NODE_INIT '1'    // Initiator node (this node) - SEQ 1, index 0
#define NODE_2    '2'    // Normal node 2 - SEQ 2, index 1
#define NODE_3    '3'    // Normal node 3 - SEQ 3, index 2
#define NODE_4    '4'    // Normal node 4 - SEQ 4, index 3
#define NODE_5    '5'    // Normal node 5 - SEQ 5, index 4
#define NODE_6    '6'    // Normal node 6 - SEQ 6, index 5
#define NODE_7    '7'    // Normal node 7 - SEQ 7, index 6
#define NODE_8    '8'    // Normal node 8 - SEQ 8, index 7
#define NODE_FL   '9'    // Front Left node - SEQ 9, index 8 (needs relay)
#define NODE_FR   'A'    // Front Right node - SEQ 11, index 9 (needs relay)
#define NODE_ALL  'B'    // Broadcast to all nodes

/* ========== 테스트 모드 설정 ========== */
#define TEST_MODE 1  // Enable test mode with 4 nodes only

#if TEST_MODE
#define TEST_TOTAL_NODES 5     // Only 5 nodes in test: INIT, NODE_4, NODE_5, NODE_FL, NODE_FR
#define TEST_EXPECTED_ACKS 4   // Expect ACKs from 4 other nodes for own data (exclude self)
#define TEST_RELAY_EXPECTED_ACKS 2  // For relay slots: only NODE_4 and NODE_5 respond (FL/FR don't ACK relayed messages)
#endif

/* ========== 메시지 타입 정의 ========== */
#define MSG_TYPE_SYNC      0x01
#define MSG_TYPE_DATA      0x02
#define MSG_TYPE_ACK       0x03
#define MSG_TYPE_URGENT    0x04
#define MSG_TYPE_RELAY_DATA   0x05  // Relayed data from FL/FR
#define MSG_TYPE_RELAY_ACK    0x06  // ACK for relayed message
#define MSG_TYPE_ACK_ARRAY    0x07  // ACK array broadcast (Period 2,4,6)

/* ========== 메시지 인덱스 정의 ========== */
enum {
  IDX_FTYPE     = 0,  // 0: frame type
  IDX_SEQ       = 1,  // 1: seq
  IDX_SOURCE    = 2,  // 2: Source ID
  IDX_DEST      = 3,  // 3: Destination ID
  IDX_MSG_TYPE  = 4,  // 4: Message type (SYNC/DATA/ACK/etc)
  IDX_PRIORITY  = 5,  // 5: Priority (URGENT/NORMAL)
  IDX_ORIG_SRC  = 6,  // 6: Original source (for relay)
  IDX_PERIOD_INFO = 6,  // 6: Period number (1-6) - SYNC only, reuses ORIG_SRC position
  IDX_ORIG_DST  = 7,  // 7: Original dest (for relay)
  IDX_TX_TIMESTAMP = 8,    // 8~11: TX timestamp (4 bytes, us since SYNC) - DATA only
  IDX_DATA_PAYLOAD = 12,   // 12~: Data payload (for DATA messages)
  IDX_ACK_ARRAY = 8        // 8~19: ACK array payload (12 bytes) - ACK_ARRAY only
};

/* INIT Node has 3 transmission slots: SEQ 1 (own), SEQ 10 (FL relay), SEQ 12 (FR relay) */
typedef enum {
    SLOT_TYPE_OWN = 1,      // SEQ 1 - Own data transmission
    SLOT_TYPE_FL_RELAY = 10, // SEQ 10 - FL relay transmission
    SLOT_TYPE_FR_RELAY = 12  // SEQ 12 - FR relay transmission
} init_slot_type_t;

/* Frame structure - 22 bytes payload + 2 bytes CRC (auto-appended by DW3000) */
static uint8_t tx_msg[] = { 0x41, 0x8C, 0, 0x9A, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 'D', 'W', 0x10, 0x00, 0, 0, 0, 0, 0 };
#define DATA_FRAME_SN_IDX   2   // Sequence number index

/* Timing configs */
#define TX_TO_RX_DELAY_UUS 60   // TX to RX turnaround delay (us)
#define RX_RESP_TO_UUS    5000  // RX response timeout (us)

/* Buffers */
static uint8_t rx_buffer[FRAME_LEN_MAX];

/* RF TX config */
extern dwt_txconfig_t txconfig_options;

/* ========== TX Power 설정 (Linear TX Power API 기반) ========== */
/*
 * dwt_calculate_linear_tx_setting() API를 사용한 정확한 TX Power 제어
 * 인덱스 1당 -0.25dB 감쇄 (선형 제어)
 *
 * 인덱스 = 감쇄량(dB) / 0.25
 */
#define TX_POWER_INDEX_0dB      0    // 0dB  (최대 출력)
#define TX_POWER_INDEX_3dB     12    // -3dB  (3 / 0.25 = 12)
#define TX_POWER_INDEX_6dB     24    // -6dB  (6 / 0.25 = 24)
#define TX_POWER_INDEX_10dB    40    // -10dB (10 / 0.25 = 40)
#define TX_POWER_INDEX_15dB    60    // -15dB (15 / 0.25 = 60)
#define TX_POWER_INDEX_16dB    64    // -16dB (16 / 0.25 = 64)
#define TX_POWER_INDEX_17dB    68    // -17dB (17 / 0.25 = 68)
#define TX_POWER_INDEX_18dB    72    // -18dB (18 / 0.25 = 72)
#define TX_POWER_INDEX_19dB    76    // -19dB (19 / 0.25 = 76)
#define TX_POWER_INDEX_20dB    80    // -20dB (20 / 0.25 = 80)

/* 현재 사용할 TX Power Index 선택 (하나만 활성화) */
#define USE_TX_POWER_INDEX  TX_POWER_INDEX_10dB    // <-- 여기서 변경

/* ========== 노드별 지연시간 추적 ========== */
/* 단방향 지연시간 통계 구조체 */
typedef struct {
    uint32_t min_us;           // 최소 지연시간 (마이크로초)
    uint32_t max_us;           // 최대 지연시간
    uint64_t sum_us;           // 합계 (평균 계산용)
    uint32_t count;            // 샘플 수
} latency_stats_t;

/* 노드별 지연시간 추적 (12 슬롯: INIT, NODE_2-8, FL, FL_RELAY, FR, FR_RELAY) */
static latency_stats_t node_latency[TOTAL_ARRAY_SIZE] = {
    {0xFFFFFFFF, 0, 0, 0},  // INIT (slot 0)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_2 (slot 1)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_3 (slot 2)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_4 (slot 3)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_5 (slot 4)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_6 (slot 5)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_7 (slot 6)
    {0xFFFFFFFF, 0, 0, 0},  // NODE_8 (slot 7)
    {0xFFFFFFFF, 0, 0, 0},  // FL (slot 8)
    {0xFFFFFFFF, 0, 0, 0},  // FL_RELAY (slot 9)
    {0xFFFFFFFF, 0, 0, 0},  // FR (slot 10)
    {0xFFFFFFFF, 0, 0, 0}   // FR_RELAY (slot 11)
};

/* ========== DWT 사이클 카운터 함수 ========== */

// ARM Cortex-M4 DWT 레지스터 주소
#define DWT_CTRL     (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT   (*(volatile uint32_t*)0xE0001004)

// DWT Control 레지스터의 사이클 카운터 활성화 비트
#ifndef DWT_CTRL_CYCCNTENA_Msk
#define DWT_CTRL_CYCCNTENA_Msk  (1UL << 0)  // Bit 0: 사이클 카운터 활성화
#endif

// CPU 클럭 주파수 (타이밍 계산용)
#define CPU_FREQ_HZ  64000000  // 64MHz nRF52840

/*
 * DWT(Data Watchpoint and Trace) 사이클 카운터 초기화
 *
 * ARM Cortex-M4의 DWT 사이클 카운터를 활성화하여
 * SPI 접근 없이 정확한 타이밍 측정이 가능하도록 설정
 */
static void dwt_timer_init(void) {
    if (!(DWT_CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
        DWT_CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

/*
 * 현재 CPU 사이클 카운트 반환
 *
 * 반환값:
 *   현재 DWT_CYCCNT 레지스터 값 (32비트 사이클 카운트)
 */
static uint32_t dwt_timer_get_cycles(void) {
    return DWT_CYCCNT;
}

/*
 * 마이크로초를 CPU 사이클로 변환
 *
 * 파라미터:
 *   microseconds: 변환할 시간 (마이크로초)
 *
 * 반환값:
 *   해당 시간에 대응하는 CPU 사이클 수 (64MHz 기준: 64 cycles = 1us)
 */
static uint32_t us_to_cpu_cycles(uint32_t microseconds) {
    return microseconds * (CPU_FREQ_HZ / 1000000);
}

/*
 * 지정된 시간이 경과했는지 확인
 *
 * 파라미터:
 *   start_cycles:    시작 시점의 사이클 카운트
 *   duration_cycles: 경과 확인할 사이클 수
 *
 * 반환값:
 *   true:  지정 시간 경과
 *   false: 아직 미경과
 *
 * 참고: 32비트 오버플로우를 자동으로 처리
 */
static bool dwt_timer_elapsed(uint32_t start_cycles, uint32_t duration_cycles) {
    uint32_t current_cycles = dwt_timer_get_cycles();
    uint32_t elapsed = current_cycles - start_cycles;
    return (elapsed >= duration_cycles);
}

/*
 * CPU 사이클을 밀리초로 변환 (로깅용)
 *
 * 파라미터:
 *   cycles: CPU 사이클 수
 *
 * 반환값:
 *   밀리초 단위 시간 (64MHz 기준: 64000 cycles = 1ms)
 */
static uint32_t cycles_to_ms(uint32_t cycles) {
    return cycles / (CPU_FREQ_HZ / 1000);
}

/*
 * 마지막 SYNC 이후 경과 시간 반환 (로깅용)
 *
 * 파라미터:
 *   last_sync_cycles: 마지막 SYNC TX 완료 시점의 사이클 카운트
 *
 * 반환값:
 *   SYNC 이후 경과 시간 (밀리초)
 */
static uint32_t get_elapsed_ms(uint32_t last_sync_cycles) {
    if (last_sync_cycles == 0) return 0;
    uint32_t current = dwt_timer_get_cycles();
    return cycles_to_ms(current - last_sync_cycles);
}

/*
 * 노드별 지연시간 통계 업데이트
 *
 * 파라미터:
 *   stats:      업데이트할 latency_stats_t 구조체 포인터
 *   latency_us: 측정된 지연시간 (마이크로초)
 *
 * 동작:
 *   - 최소/최대값 갱신
 *   - 합계 누적 (평균 계산용)
 *   - 샘플 카운트 증가
 */
static void update_node_latency(latency_stats_t *stats, uint32_t latency_us) {
    if (latency_us < stats->min_us) {
        stats->min_us = latency_us;
    }
    if (latency_us > stats->max_us) {
        stats->max_us = latency_us;
    }
    stats->sum_us += latency_us;
    stats->count++;
}

/* ACK source tracking for current collection period */
static uint8_t ack_received_from[TOTAL_NODES] = {0};    // Track which nodes sent ACKs this period (for own data)
static uint8_t unique_ack_count = 0;                    // Count of unique ACK sources (for own data)

/* Separate ACK tracking for FL and FR relay transmissions */
static uint8_t fl_relay_ack_received_from[TOTAL_NODES] = {0};  // Track ACKs for FL relay
static uint8_t fr_relay_ack_received_from[TOTAL_NODES] = {0};  // Track ACKs for FR relay
static uint8_t fl_relay_unique_ack_count = 0;                  // Count of unique FL relay ACKs
static uint8_t fr_relay_unique_ack_count = 0;                  // Count of unique FR relay ACKs

/* DATA message tracking - Aggregated ACKs approach */
/* Instead of collecting ACKs, we track which slots' transmissions we received during the period */
/* Array indices: 0=INIT, 1=Node2, ..., 8=FL, 9=FL_RELAY, 10=FR, 11=FR_RELAY */

/* Period-level DATA reception tracking (reset every odd period for ACK_ARRAY) */
static uint8_t data_received_from[TOTAL_ARRAY_SIZE] = {0};  // Track which slots' DATA we received in this period
static uint8_t unique_data_count = 0;                       // Count of unique DATA sources

/* Cycle-level cumulative tracking (reset every cycle) */
static uint8_t cumulative_ack_confirmed[TOTAL_ARRAY_SIZE] = {0};  // Cumulative ACK confirmations across period pairs
static uint8_t cumulative_ack_count = 0;                           // Count of confirmed ACKs in this cycle

/* Slot index mapping for 12-slot system */
#define SLOT_IDX_INIT       0   // INIT (SEQ 1)
#define SLOT_IDX_NODE_2     1   // Node 2 (SEQ 2)
#define SLOT_IDX_NODE_3     2   // Node 3 (SEQ 3)
#define SLOT_IDX_NODE_4     3   // Node 4 (SEQ 4)
#define SLOT_IDX_NODE_5     4   // Node 5 (SEQ 5)
#define SLOT_IDX_NODE_6     5   // Node 6 (SEQ 6)
#define SLOT_IDX_NODE_7     6   // Node 7 (SEQ 7)
#define SLOT_IDX_NODE_8     7   // Node 8 (SEQ 8)
#define SLOT_IDX_FL         8   // FL (SEQ 9)
#define SLOT_IDX_FL_RELAY   9   // FL Relay by INIT (SEQ 10)
#define SLOT_IDX_FR         10  // FR (SEQ 11)
#define SLOT_IDX_FR_RELAY   11  // FR Relay by INIT (SEQ 12)

/* Cycle and Period management for retransmission */
static uint32_t current_cycle = 1;  // Start from cycle 1 for 3 cycles
static uint8_t period_in_cycle = 1;  // 1-6 (3 period pairs)

/* ========== Pair 단위 통계 (사이클당 3개 pair) ========== */
// Pair 1 (Period 1-2)
static uint32_t pair1_data_success = 0, pair1_data_fail = 0, pair1_data_idle = 0;
static uint32_t pair1_fl_relay_success = 0, pair1_fl_relay_fail = 0, pair1_fl_relay_idle = 0;
static uint32_t pair1_fr_relay_success = 0, pair1_fr_relay_fail = 0, pair1_fr_relay_idle = 0;

// Pair 2 (Period 3-4)
static uint32_t pair2_data_success = 0, pair2_data_fail = 0, pair2_data_idle = 0;
static uint32_t pair2_fl_relay_success = 0, pair2_fl_relay_fail = 0, pair2_fl_relay_idle = 0;
static uint32_t pair2_fr_relay_success = 0, pair2_fr_relay_fail = 0, pair2_fr_relay_idle = 0;

// Pair 3 (Period 5-6)
static uint32_t pair3_data_success = 0, pair3_data_fail = 0, pair3_data_idle = 0;
static uint32_t pair3_fl_relay_success = 0, pair3_fl_relay_fail = 0, pair3_fl_relay_idle = 0;
static uint32_t pair3_fr_relay_success = 0, pair3_fr_relay_fail = 0, pair3_fr_relay_idle = 0;

/* ========== 사이클 단위 통계 ========== */
static uint32_t total_cycles = 0;
static uint32_t data_successful_cycles = 0;      // DATA succeeded in any pair
static uint32_t fl_relay_successful_cycles = 0;  // FL_RELAY succeeded in any pair
static uint32_t fr_relay_successful_cycles = 0;  // FR_RELAY succeeded in any pair
static uint32_t perfect_cycles = 0;              // All 3 items succeeded
static uint32_t failed_cycles = 0;               // All 3 items failed in all pairs

/* Failed cycle tracking (최근 10개 저장) */
#define MAX_FAILED_CYCLES_LOG 10
static uint32_t failed_cycle_numbers[MAX_FAILED_CYCLES_LOG] = {0};

/* Tracking per-cycle success for each item (reset every cycle) */
static bool data_success_in_current_cycle = false;
static bool fl_relay_success_in_current_cycle = false;
static bool fr_relay_success_in_current_cycle = false;

/* Final statistics flag */
static bool final_stats_printed = false;
#if TEST_MODE
static uint8_t expected_nodes = TEST_EXPECTED_ACKS;  // Test mode: dynamically set based on slot type (own data vs relay)
#else
static uint8_t expected_nodes = TOTAL_NODES - 1;     // Normal mode: expect all normal nodes should ACK
#endif

/* Retransmission state management */
typedef enum {
    TX_STATE_IDLE,        // No transmission needed
    TX_STATE_FIRST_TX,    // First transmission of new data
    TX_STATE_RETRANS      // Retransmission mode
} transmission_state_t;

static transmission_state_t tx_state = TX_STATE_FIRST_TX;  // Start with first transmission
static uint8_t retrans_msg[FRAME_LEN_MAX];                 // Store message for retransmission (SEQ 1)
static bool has_pending_retrans = false;                   // Flag for pending retransmission
static uint8_t retrans_attempt = 0;                        // Number of retransmission attempts in current cycle

/* FL/FR Relay system */
static uint8_t fl_relay_msg[FRAME_LEN_MAX];                // Store FL message for relay
static uint8_t fr_relay_msg[FRAME_LEN_MAX];                // Store FR message for relay
static uint8_t fl_retrans_msg[FRAME_LEN_MAX];              // FL relay retransmission buffer
static uint8_t fr_retrans_msg[FRAME_LEN_MAX];              // FR relay retransmission buffer
static bool has_fl_data = false;                           // Flag for FL data availability
static bool has_fr_data = false;                           // Flag for FR data availability
static bool fl_pending_retrans = false;                    // FL relay retransmission flag
static bool fr_pending_retrans = false;                    // FR relay retransmission flag
static bool fl_relay_completed = false;                    // FL relay completed successfully this cycle
static bool fr_relay_completed = false;                    // FR relay completed successfully this cycle

/*
 * 노드 ID 문자를 배열 인덱스로 변환
 *
 * 파라미터:
 *   node_id: 노드 ID 문자 ('1'-'9', 'A')
 *
 * 반환값:
 *   배열 인덱스 (0-10), 잘못된 ID인 경우 0xFF
 *
 * 매핑:
 *   '1'(INIT)→0, '2'→1, ..., '9'(FL)→8, 'A'(FR)→10
 */
static uint8_t node_id_to_index(uint8_t node_id) {
    switch(node_id) {
        case NODE_INIT: return 0;
        case NODE_2:    return 1;
        case NODE_3:    return 2;
        case NODE_4:    return 3;
        case NODE_5:    return 4;
        case NODE_6:    return 5;
        case NODE_7:    return 6;
        case NODE_8:    return 7;
        case NODE_FL:   return 8;
        case NODE_FR:   return 10;
        default:        return 0xFF;
    }
}

/*
 * 배열 인덱스를 노드 ID 문자로 변환
 *
 * 파라미터:
 *   index: 배열 인덱스 (0-10)
 *
 * 반환값:
 *   노드 ID 문자, 잘못된 인덱스인 경우 '?'
 *
 * 참고: 인덱스 9, 11은 릴레이 슬롯으로 '?' 반환
 */
static uint8_t index_to_node_id(uint8_t index) {
    switch(index) {
        case 0:  return NODE_INIT;
        case 1:  return NODE_2;
        case 2:  return NODE_3;
        case 3:  return NODE_4;
        case 4:  return NODE_5;
        case 5:  return NODE_6;
        case 6:  return NODE_7;
        case 7:  return NODE_8;
        case 8:  return NODE_FL;
        case 10: return NODE_FR;
        default: return '?';
    }
}

/*
 * 메시지 타입과 소스 정보로부터 슬롯 인덱스 계산
 *
 * 파라미터:
 *   msg_type: 메시지 타입 (MSG_TYPE_DATA 또는 MSG_TYPE_RELAY_DATA)
 *   src_node: 메시지 송신 노드 ID
 *   orig_src: 릴레이 메시지의 원본 소스 노드 ID
 *
 * 반환값:
 *   해당하는 TDMA 슬롯 인덱스 (0-11), 잘못된 경우 0xFF
 *
 * 슬롯 매핑:
 *   - DATA: 노드별 직접 전송 슬롯 (0-8, 10)
 *   - RELAY_DATA: 릴레이 슬롯 (FL→9, FR→11)
 */
static uint8_t get_slot_index(uint8_t msg_type, uint8_t src_node, uint8_t orig_src) {
    if (msg_type == MSG_TYPE_DATA) {
        if (src_node == NODE_FL) {
            return SLOT_IDX_FL;
        } else if (src_node == NODE_FR) {
            return SLOT_IDX_FR;
        } else {
            return node_id_to_index(src_node);
        }
    } else if (msg_type == MSG_TYPE_RELAY_DATA) {
        if (orig_src == NODE_FL) {
            return SLOT_IDX_FL_RELAY;
        } else if (orig_src == NODE_FR) {
            return SLOT_IDX_FR_RELAY;
        }
    }
    return 0xFF;
}

/*
 * 특정 전송 슬롯에 대해 해당 슬롯으로부터 ACK를 기대하는지 확인하는 헬퍼 함수
 *
 * 파라미터:
 *   ack_slot_idx: ACK를 전송하는 슬롯의 인덱스 (어떤 노드가 ACK를 보내는지)
 *   tx_slot_idx:  ACK 대상이 되는 전송 슬롯의 인덱스 (어떤 슬롯의 전송에 대한 ACK인지)
 *
 * 반환값:
 *   true:  ack_slot_idx 슬롯이 tx_slot_idx 슬롯의 전송에 대해 ACK를 보낼 것으로 기대됨
 *   false: ack_slot_idx 슬롯으로부터 ACK를 기대하지 않음
 *
 * 예시:
 *   is_expected_ack_slot_for(SLOT_IDX_NODE_4, SLOT_IDX_INIT)
 *     → true (NODE_4는 INIT의 데이터에 대해 ACK를 보내야 함)
 *   is_expected_ack_slot_for(SLOT_IDX_NODE_4, SLOT_IDX_FR_RELAY)
 *     → true (NODE_4는 FR_RELAY 전송에 대해 ACK를 보내야 함)
 *   is_expected_ack_slot_for(SLOT_IDX_FL, SLOT_IDX_FL_RELAY)
 *     → false (FL은 자신의 데이터를 relay한 것에 대해서는 ACK를 보내지 않음)
 */
static bool is_expected_ack_slot_for(uint8_t ack_slot_idx, uint8_t tx_slot_idx) {
    #if TEST_MODE
    if (tx_slot_idx == SLOT_IDX_INIT) {
        // SEQ 1 (own data): expect ACK from NODE_4, NODE_5, FL, FR
        if (ack_slot_idx == SLOT_IDX_NODE_4) return true;
        if (ack_slot_idx == SLOT_IDX_NODE_5) return true;
        if (ack_slot_idx == SLOT_IDX_FL) return true;
        if (ack_slot_idx == SLOT_IDX_FR) return true;
        return false;
    } else if (tx_slot_idx == SLOT_IDX_FL_RELAY || tx_slot_idx == SLOT_IDX_FR_RELAY) {
        // SEQ 10, 12 (relay): expect ACK from NODE_4, NODE_5 only (FL/FR don't ACK relays)
        if (ack_slot_idx == SLOT_IDX_NODE_4) return true;
        if (ack_slot_idx == SLOT_IDX_NODE_5) return true;
        return false;
    }
    return false;
    #else
    // Normal mode: expect all except self
    return (ack_slot_idx != tx_slot_idx && ack_slot_idx < TOTAL_NODES);
    #endif
}

/*
 * 슬롯 인덱스를 로깅용 문자열로 변환
 *
 * 파라미터:
 *   slot_idx: TDMA 슬롯 인덱스 (0-11)
 *
 * 반환값:
 *   슬롯 설명 문자열 ("INIT", "N2"~"N8", "FL", "FL_REL", "FR", "FR_REL")
 */
static const char* get_slot_description(uint8_t slot_idx) {
    switch(slot_idx) {
        case SLOT_IDX_INIT: return "INIT";
        case SLOT_IDX_NODE_2: return "N2";
        case SLOT_IDX_NODE_3: return "N3";
        case SLOT_IDX_NODE_4: return "N4";
        case SLOT_IDX_NODE_5: return "N5";
        case SLOT_IDX_NODE_6: return "N6";
        case SLOT_IDX_NODE_7: return "N7";
        case SLOT_IDX_NODE_8: return "N8";
        case SLOT_IDX_FL: return "FL";
        case SLOT_IDX_FL_RELAY: return "FL_REL";
        case SLOT_IDX_FR: return "FR";
        case SLOT_IDX_FR_RELAY: return "FR_REL";
        default: return "???";
    }
}

/*
 * Aggregated ACKs MAC 프로토콜 - Initiator 노드 메인 함수
 *
 * 기능:
 *   - DW3000 UWB 트랜시버 초기화
 *   - 21.2ms 주기로 SYNC 브로드캐스트 전송
 *   - 12-슬롯 TDMA 구조에서 3개 슬롯 관리 (SEQ 1, 10, 12)
 *   - FL/FR 노드 데이터 릴레이 수행
 *   - Period 1,3,5: DATA 전송, Period 2,4,6: ACK_ARRAY 전송
 *
 * TDMA 슬롯 타이밍 (SYNC TX 완료 기준):
 *   - SEQ 1:  2.0ms (자신의 DATA/ACK_ARRAY)
 *   - SEQ 10: 12.4ms (FL 릴레이)
 *   - SEQ 12: 15.1ms (FR 릴레이)
 *
 * 반환값:
 *   정상 종료 시 반환하지 않음 (무한 루프)
 */
int dwt_init_aggregated_acks(void)
{
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

    /* Enabling LEDs here for debug - SAME AS WORKING EXAMPLE */
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

    /* Start with SYNC config since we begin with SYNC transmission */
    if (dwt_configure(&config_sync))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    /* Linear TX Power 설정 - dwt_calculate_linear_tx_setting API 사용 */
    power_indexes_t power_indexes = {0};
    tx_adj_res_t linear_results = {0};
    dwt_txconfig_t linear_txconfig;

    /* 모든 프레임 부분에 동일한 인덱스 적용 */
    power_indexes.input[0] = USE_TX_POWER_INDEX;  // DATA
    power_indexes.input[1] = USE_TX_POWER_INDEX;  // PHR
    power_indexes.input[2] = USE_TX_POWER_INDEX;  // SHR
    power_indexes.input[3] = USE_TX_POWER_INDEX;  // STS

    if (dwt_calculate_linear_tx_setting((int)config_sync.chan, &power_indexes, &linear_results) == DWT_SUCCESS) {
        linear_txconfig.power = linear_results.tx_frame_cfg.tx_power_setting;
        linear_txconfig.PGcount = txconfig_options.PGcount;
        linear_txconfig.PGdly = txconfig_options.PGdly;

        dwt_configuretxrf(&linear_txconfig);
        dwt_set_pll_config(linear_results.tx_frame_cfg.pll_cfg);

        /* 디버그 출력: 적용된 TX Power 설정 */
        static char tx_power_debug[150];
        snprintf(tx_power_debug, sizeof(tx_power_debug),
                 "TX Power Index: %d (%.2fdB), Setting: 0x%08lX, PLL: 0x%X",
                 USE_TX_POWER_INDEX, -(float)USE_TX_POWER_INDEX * 0.25,
                 linear_results.tx_frame_cfg.tx_power_setting,
                 linear_results.tx_frame_cfg.pll_cfg);
        test_run_info((unsigned char *)tx_power_debug);
    } else {
        test_run_info((unsigned char *)"TX Power API failed, using default txconfig_options");
        dwt_configuretxrf(&txconfig_options);
    }

    dwt_setrxaftertxdelay(TX_TO_RX_DELAY_UUS);
    dwt_setrxtimeout(RX_RESP_TO_UUS);

    /* POLLING MODE - No interrupt callbacks */
    /* Disable all interrupts to prevent SPI lock issues */
    dwt_setinterrupt(0, 0, DWT_ENABLE_INT);  // Disable all interrupts

    /* Clear any pending interrupts */
    dwt_writesysstatuslo(0xFFFFFFFF);  // Clear all status flags

    /* Do NOT install IRQ handler - using polling instead */
    // port_set_dwic_isr(dwt_isr);  // DISABLED for polling mode

    test_run_info((unsigned char *)"Initiator Node initialized in POLLING MODE (no interrupts)");

    /* Print current configuration for debugging */
    static char config_debug[200];
    snprintf(config_debug, sizeof(config_debug),
             "INIT CONFIG: SYNC(512 symbols) DATA(64 symbols) Ch=%d, TxCode=%d, RxCode=%d",
             config_sync.chan, config_sync.txCode, config_sync.rxCode);
    test_run_info((unsigned char *)config_debug);
    
    //MARK: DWT Cycle Counter Timer Configuration
    /* Initialize DWT cycle counter for SPI-free timing */
    dwt_timer_init();

    /* Non-blocking loop with periodic SYNC and real-time RX using CPU cycle timing */
    int period_count = 0;
    uint32_t last_sync_cycles = 0;                         // When last SYNC was completed (CPU cycles)
    uint32_t period_interval_cycles = us_to_cpu_cycles(21200);     // 21.2ms period interval (1.0ms guard time)
    uint32_t slot_interval_cycles = us_to_cpu_cycles(1100);        // 1.1ms slot timing (0.1ms TX + 1.0ms guard)
    uint32_t slot_duration_cycles = us_to_cpu_cycles(100);         // 0.1ms TX duration only
    uint32_t config_switch_time_cycles = us_to_cpu_cycles(17200);  // 17.2ms - switch to SYNC config (4.0ms buffer)
    /* INIT node has 3 independent slots with separate execution flags */
    bool seq1_executed = false;     // SEQ 1 (own data) execution flag
    bool seq10_executed = false;    // SEQ 10 (FL relay) execution flag
    bool seq12_executed = false;    // SEQ 12 (FR relay) execution flag
    bool config_is_sync = false;    // Track current config state (starts in SYNC for first TX)

    /* Independent slot timers for all 3 SEQs (absolute timing from SYNC TX completion) */
    uint32_t seq1_start_cycles = us_to_cpu_cycles(2000);   // SEQ 1 (INIT): 2ms buffer after SYNC TX
    uint32_t seq10_start_cycles = us_to_cpu_cycles(12400);  // SEQ 10 (FL_RELAY): 12.4ms (10.8 + 0.1 + 1.5)
    uint32_t seq12_start_cycles = us_to_cpu_cycles(15100);  // SEQ 12 (FR_RELAY): 15.1ms (13.5 + 0.1 + 1.5)

    bool slot_active = false;                               // Track if currently in any slot

    /* Debug: Print calculated intervals */
    static char interval_debug[150];
    snprintf(interval_debug, sizeof(interval_debug),
             "DWT Intervals: period=%u cycles (21.2ms), slot=%u cycles (1.1ms), TX_duration=%u cycles (0.1ms)",
             period_interval_cycles, slot_interval_cycles, slot_duration_cycles);
    test_run_info((unsigned char *)interval_debug);

    /* Enable RX immediately for continuous listening */
    test_run_info((unsigned char *)"Starting Aggregated ACKs: 21.2ms period, 1.1ms slots, 4.0ms SYNC buffer");

    /* Initial RX enable - no critical section needed in polling mode */
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    Sleep(20000);  // 20 sec - Wait for other nodes to initialize

    while (1)
    {
        /*
         * ========== [A] SYNC 전송 체크 (21.2ms Period Timer) ==========
         *
         * 조건: 첫 실행(last_sync_cycles==0) 또는 21.2ms 경과 시
         * 동작: SYNC 브로드캐스트 전송 → 새 Period 시작
         *
         * Period 구조:
         *   - 홀수 Period (1,3,5): DATA 전송/수신
         *   - 짝수 Period (2,4,6): ACK_ARRAY 전송/수신
         */
        //MARK: 새로운 period: SYNC 전송
        if (last_sync_cycles == 0 || dwt_timer_elapsed(last_sync_cycles, period_interval_cycles)) {
            /* Variable declarations for C89 compatibility */
            uint32_t current_cycles;
            uint32_t elapsed_since_last;
            char info_str[120];
            static char sync_debug[150];
            int sync_result;
            uint32_t tx_status;
            uint32_t current_sync_cycles;
            uint32_t cycles_since_last_sync;
            float ms_since_last_sync;
            static char sync_interval_log[120];

            /* Get current CPU cycle count - no SPI access needed */
            current_cycles = dwt_timer_get_cycles();

            if (last_sync_cycles == 0 || dwt_timer_elapsed(last_sync_cycles, period_interval_cycles)) {
                /* Store previous sync cycles for interval logging */
                uint32_t prev_sync_cycles = last_sync_cycles;

                /* CRITICAL: Set timing reference IMMEDIATELY at period start to maintain exact 21.2ms intervals */
                /* This prevents config switch and TX processing delays from accumulating */
                last_sync_cycles = current_cycles;

                period_count++;

                /*
                 * [A-1] 새 Cycle 시작 체크
                 *
                 * 조건: (period_count - 1) % 6 == 0 (매 6번째 Period마다)
                 * 동작:
                 *   - current_cycle 증가
                 *   - 모든 상태 변수 초기화 (TX state, ACK 추적, 릴레이 플래그)
                 *   - 이전 Cycle 통계 집계
                 */
                if ((period_count - 1) % PERIODS_PER_CYCLE == 0) {
                    if (period_count > 1) {
                        current_cycle++;
                    }
                    period_in_cycle = 1;

                    /* Reset retransmission state for new cycle */
                    tx_state = TX_STATE_FIRST_TX;
                    has_pending_retrans = false;
                    retrans_attempt = 0;
                    memset(retrans_msg, 0, sizeof(retrans_msg));

                    /* Reset ACK tracking for new cycle (cumulative per cycle) */
                    memset(ack_received_from, 0, sizeof(ack_received_from));
                    unique_ack_count = 0;

                    /* Reset relay ACK tracking arrays */
                    memset(fl_relay_ack_received_from, 0, sizeof(fl_relay_ack_received_from));
                    memset(fr_relay_ack_received_from, 0, sizeof(fr_relay_ack_received_from));
                    fl_relay_unique_ack_count = 0;
                    fr_relay_unique_ack_count = 0;

                    /* Reset relay completion flags for new cycle */
                    fl_relay_completed = false;
                    fr_relay_completed = false;

                    /* CRITICAL: Reset relay data flags for new cycle */
                    /* Without this, old data from previous cycle will be retransmitted! */
                    has_fl_data = false;
                    has_fr_data = false;

                    /* Evaluate previous cycle (for cycle 2+) */
                    if (current_cycle > 1) {
                        /* Update cycle-level statistics */
                        if (data_success_in_current_cycle) data_successful_cycles++;
                        if (fl_relay_success_in_current_cycle) fl_relay_successful_cycles++;
                        if (fr_relay_success_in_current_cycle) fr_relay_successful_cycles++;

                        /* Check for perfect or failed cycle */
                        if (data_success_in_current_cycle && fl_relay_success_in_current_cycle && fr_relay_success_in_current_cycle) {
                            perfect_cycles++;
                        } else if (!data_success_in_current_cycle && !fl_relay_success_in_current_cycle && !fr_relay_success_in_current_cycle) {
                            failed_cycles++;
                            /* Store failed cycle number (최근 10개만 유지) */
                            if (failed_cycles <= MAX_FAILED_CYCLES_LOG) {
                                failed_cycle_numbers[failed_cycles - 1] = current_cycle - 1;
                            }
                        }

                        total_cycles++;
                    }

                    /* Reset per-cycle success flags for new cycle */
                    data_success_in_current_cycle = false;
                    fl_relay_success_in_current_cycle = false;
                    fr_relay_success_in_current_cycle = false;

                    /* Reset cumulative ACK tracking for new cycle */
                    memset(cumulative_ack_confirmed, 0, sizeof(cumulative_ack_confirmed));
                    cumulative_ack_count = 0;

                    static char cycle_msg[150];
                    snprintf(cycle_msg, sizeof(cycle_msg),
                            "NEW CYCLE %d started - Period %d (global period %d) - TX state reset",
                            current_cycle, period_in_cycle, period_count);
                    test_run_info((unsigned char *)cycle_msg);
                } else {
                    period_in_cycle = ((period_count - 1) % PERIODS_PER_CYCLE) + 1;

                    static char period_msg[100];
                    snprintf(period_msg, sizeof(period_msg), "Cycle %d - Period %d (global period %d)",
                            current_cycle, period_in_cycle, period_count);
                    test_run_info((unsigned char *)period_msg);
                }

                /*
                 * [A-2] 종료 조건 체크 (1000 Cycle 완료)
                 *
                 * 조건: current_cycle > 1000 && !final_stats_printed
                 * 동작:
                 *   - Pair별 통계 출력 (Period 1-2, 3-4, 5-6)
                 *   - Cycle별 성공률 출력
                 *   - 노드별 지연시간 통계 출력
                 *   - break로 메인 루프 탈출
                 */
                //MARK: 종료 조건
                if (current_cycle > 1000 && !final_stats_printed) {
                    /* Print final statistics with pair-level and cycle-level details */
                    static char final_stats[1200];
                    float data_rate = (total_cycles > 0) ? (float)data_successful_cycles / total_cycles * 100 : 0;
                    float fl_relay_rate = (total_cycles > 0) ? (float)fl_relay_successful_cycles / total_cycles * 100 : 0;
                    float fr_relay_rate = (total_cycles > 0) ? (float)fr_relay_successful_cycles / total_cycles * 100 : 0;
                    float perfect_rate = (total_cycles > 0) ? (float)perfect_cycles / total_cycles * 100 : 0;

                    snprintf(final_stats, sizeof(final_stats),
                            "\n=== PAIR STATISTICS (1000 cycles) ===\n"
                            "Pair 1 (Period 1-2):\n"
                            "  DATA:     Success=%d, Fail=%d, IDLE=%d\n"
                            "  FL_RELAY: Success=%d, Fail=%d, IDLE=%d\n"
                            "  FR_RELAY: Success=%d, Fail=%d, IDLE=%d\n\n"
                            "Pair 2 (Period 3-4):\n"
                            "  DATA:     Success=%d, Fail=%d, IDLE=%d\n"
                            "  FL_RELAY: Success=%d, Fail=%d, IDLE=%d\n"
                            "  FR_RELAY: Success=%d, Fail=%d, IDLE=%d\n\n"
                            "Pair 3 (Period 5-6):\n"
                            "  DATA:     Success=%d, Fail=%d, IDLE=%d\n"
                            "  FL_RELAY: Success=%d, Fail=%d, IDLE=%d\n"
                            "  FR_RELAY: Success=%d, Fail=%d, IDLE=%d\n\n"
                            "=== CYCLE STATISTICS ===\n"
                            "Total Cycles: %d\n"
                            "DATA Successful Cycles: %d (%.2f%%)\n"
                            "FL_RELAY Successful Cycles: %d (%.2f%%)\n"
                            "FR_RELAY Successful Cycles: %d (%.2f%%)\n"
                            "Perfect Cycles: %d (%.2f%%)\n"
                            "Failed Cycles: %d",
                            pair1_data_success, pair1_data_fail, pair1_data_idle,
                            pair1_fl_relay_success, pair1_fl_relay_fail, pair1_fl_relay_idle,
                            pair1_fr_relay_success, pair1_fr_relay_fail, pair1_fr_relay_idle,
                            pair2_data_success, pair2_data_fail, pair2_data_idle,
                            pair2_fl_relay_success, pair2_fl_relay_fail, pair2_fl_relay_idle,
                            pair2_fr_relay_success, pair2_fr_relay_fail, pair2_fr_relay_idle,
                            pair3_data_success, pair3_data_fail, pair3_data_idle,
                            pair3_fl_relay_success, pair3_fl_relay_fail, pair3_fl_relay_idle,
                            pair3_fr_relay_success, pair3_fr_relay_fail, pair3_fr_relay_idle,
                            total_cycles,
                            data_successful_cycles, data_rate,
                            fl_relay_successful_cycles, fl_relay_rate,
                            fr_relay_successful_cycles, fr_relay_rate,
                            perfect_cycles, perfect_rate,
                            failed_cycles);
                    test_run_info((unsigned char *)final_stats);

                    /* Print failed cycle numbers (최근 10개) */
                    if (failed_cycles > 0) {
                        static char failed_cycles_msg[300];
                        int pos = snprintf(failed_cycles_msg, sizeof(failed_cycles_msg),
                                          "\nFailed Cycles (first %d): ",
                                          (failed_cycles < MAX_FAILED_CYCLES_LOG) ? failed_cycles : MAX_FAILED_CYCLES_LOG);

                        uint32_t max_to_print = (failed_cycles < MAX_FAILED_CYCLES_LOG) ? failed_cycles : MAX_FAILED_CYCLES_LOG;
                        for (uint32_t i = 0; i < max_to_print && pos < sizeof(failed_cycles_msg) - 10; i++) {
                            pos += snprintf(failed_cycles_msg + pos, sizeof(failed_cycles_msg) - pos,
                                           "%d ", failed_cycle_numbers[i]);
                        }
                        test_run_info((unsigned char *)failed_cycles_msg);
                    }

                    /* Print per-node latency statistics */
                    test_run_info((unsigned char *)"\n=== ONE-WAY LATENCY BY NODE ===");
                    for (uint8_t i = 0; i < TOTAL_ARRAY_SIZE; i++) {
                        if (i== 9 || i == 11) {
                            continue; // Skip relay slots
                        }
                        latency_stats_t *stats = &node_latency[i];

                        /* Only print nodes with samples */
                        if (stats->count > 0) {
                            float min_ms = stats->min_us / 1000.0;
                            float max_ms = stats->max_us / 1000.0;
                            float avg_us = (float)stats->sum_us / stats->count;
                            float avg_ms = avg_us / 1000.0;

                            static char latency_output[200];
                            snprintf(latency_output, sizeof(latency_output),
                                    "%s (slot %d):\n"
                                    "  Min: %u us (%.3f ms)\n"
                                    "  Max: %u us (%.3f ms)\n"
                                    "  Avg: %.1f us (%.3f ms)\n"
                                    "  Samples: %u",
                                    get_slot_description(i), i,
                                    stats->min_us, min_ms,
                                    stats->max_us, max_ms,
                                    avg_us, avg_ms,
                                    stats->count);
                            test_run_info((unsigned char *)latency_output);
                        }
                    }

                    final_stats_printed = true;
                    break;
                }

                /* IMMEDIATELY reset all slot flags when new period starts - before any TX operations */
                seq1_executed = false;
                seq10_executed = false;
                seq12_executed = false;

                /* Switch to SYNC config before SYNC TX */
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    test_run_info((unsigned char *)"Switched to SYNC config (PLEN512) for SYNC TX");
                    config_is_sync = true;  // Mark that we're now in SYNC config
                } else {
                    test_run_info((unsigned char *)"WARNING: Failed to switch to SYNC config!");
                }

                /* Debug: Show period interval timing */
                elapsed_since_last = (last_sync_cycles != 0) ? (current_cycles - last_sync_cycles) : 0;
                snprintf(info_str, sizeof(info_str), "Period %d start (interval: %u cycles, expected: %u) - slot flag RESET",
                         period_count, elapsed_since_last, period_interval_cycles);
                test_run_info((unsigned char *)info_str);

                /* Prepare SYNC frame */
                tx_msg[0] = 0xC5;
                tx_msg[IDX_MSG_TYPE] = MSG_TYPE_SYNC;
                tx_msg[IDX_PERIOD_INFO] = period_in_cycle;  // Add period info (1-6) for recovery

                /* Print SYNC frame content */
                snprintf(sync_debug, sizeof(sync_debug), "SYNC TX: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                        tx_msg[0], tx_msg[1], tx_msg[2], tx_msg[3], tx_msg[4], tx_msg[5],
                        tx_msg[6], tx_msg[7], tx_msg[8], tx_msg[9], tx_msg[10], tx_msg[11]);
                test_run_info((unsigned char *)sync_debug);

                dwt_setrxaftertxdelay(0);
                dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
                dwt_writetxfctrl(sizeof(tx_msg), 0, 0);

                /*
                 * [A-3] SYNC TX 실행 및 결과 처리
                 *
                 * 성공 시:
                 *   - TX 완료 대기 (waitforsysstatus)
                 *   - DATA config로 전환 (PLEN64)
                 *   - 슬롯 플래그 초기화
                 *   - Period 홀수면 data_received_from[] 초기화
                 *   - RX 재활성화
                 *
                 * 실패 시:
                 *   - 로그 출력만 (타이밍 유지를 위해 continue)
                 */
                sync_result = dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                if (sync_result == DWT_SUCCESS) {

                    test_run_info((unsigned char *)"SYNC TX started successfully");

                    tx_status = 0;
                    waitforsysstatus(&tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                    if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
                        test_run_info((unsigned char *)"SYNC TX completed successfully");
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                        /* Calculate and log SYNC transmission interval for timing verification */
                        current_sync_cycles = dwt_timer_get_cycles();
                        if (prev_sync_cycles != 0) {
                            cycles_since_last_sync = current_sync_cycles - prev_sync_cycles;
                            ms_since_last_sync = cycles_since_last_sync / 64.0;
                            snprintf(sync_interval_log, sizeof(sync_interval_log),
                                    "INIT SYNC TX interval: %u cycles (%.2f ms) - expected 21.2ms",
                                    cycles_since_last_sync, ms_since_last_sync);
                            test_run_info((unsigned char *)sync_interval_log);
                        }

                        /* CRITICAL: Clear RX buffer to discard any old messages from previous period */
                        dwt_forcetrxoff();
                        dwt_writesysstatuslo(0xFFFFFFFF);  // Clear all status flags and RX buffer

                        /* Switch to DATA config after SYNC TX for DATA/ACK operations */
                        if (dwt_configure(&config_data) == DWT_SUCCESS) {
                            test_run_info((unsigned char *)"Switched to DATA config (PLEN64) for normal operations");
                            config_is_sync = false;  // Mark that we're now in DATA config
                        } else {
                            test_run_info((unsigned char *)"WARNING: Failed to switch to DATA config!");
                        }

                        /* Note: last_sync_cycles was already set at period start */
                        /* This ensures exact 21.2ms intervals regardless of SYNC TX processing delays */

                        /* Reset all slot flags - the new SYNC period starts now */
                        seq1_executed = false;
                        seq10_executed = false;
                        seq12_executed = false;
                        slot_active = false;

                        /* Reset data_received_from array at start of odd periods (DATA periods) */
                        if (period_in_cycle % 2 == 1) {
                            memset(data_received_from, 0, sizeof(data_received_from));
                            unique_data_count = 0;
                        }

                        /* Re-enable RX for continuous listening with clean buffer */
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);
                    } else {
                        test_run_info((unsigned char *)"SYNC TX completion failed!");
                        /* Note: last_sync_cycles already set at period start to prevent rapid retries */
                    }
                } else {
                    test_run_info((unsigned char *)"SYNC TX failed to start");
                    /* Note: last_sync_cycles already set at period start to prevent rapid retries */
                }

                /* Increment sequence number */
                tx_msg[DATA_FRAME_SN_IDX]++;

                /* RX already enabled above after buffer clear */
                continue;    
            }
        }

        /*
         * ========== [B] SYNC Config 전환 (17.2ms 시점) ==========
         *
         * 조건: last_sync_cycles != 0 && !config_is_sync && 17.2ms 경과
         * 목적: 다음 SYNC 수신을 위해 미리 PLEN512 config로 전환
         * 버퍼: 21.2ms - 17.2ms = 4.0ms (SYNC 수신 준비 시간)
         */
        if (last_sync_cycles != 0 && !config_is_sync) {
            uint32_t current_cycles = dwt_timer_get_cycles();
            if (dwt_timer_elapsed(last_sync_cycles, config_switch_time_cycles)) {
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    test_run_info((unsigned char *)"INIT: Switched to SYNC config at 17.2ms (4.0ms buffer)");
                    config_is_sync = true;
                } else {
                    test_run_info((unsigned char *)"WARNING: INIT failed to switch to SYNC config!");
                }
                /* Re-enable RX after config change */
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
        }

        /*
         * ========== [C] INIT 3-Slot 처리: SEQ 1, 10, 12 ==========
         *
         * INIT 노드는 3개의 독립적인 TX 슬롯을 관리:
         *   - SEQ 1  (2.0ms):  자신의 DATA/ACK_ARRAY 전송
         *   - SEQ 10 (12.4ms): FL 노드 데이터 릴레이
         *   - SEQ 12 (15.1ms): FR 노드 데이터 릴레이
         *
         * 각 슬롯은 독립적인 실행 플래그(seq*_executed)로 관리
         */
        //MARK: INIT 3-slot handling: SEQ 1, 10, 12 with independent timers
        if (last_sync_cycles != 0) {
            uint32_t current_cycles = dwt_timer_get_cycles();
            uint32_t cycles_since_sync = current_cycles - last_sync_cycles;

            init_slot_type_t active_slot_type = 0;
            bool should_execute_slot = false;

            /*
             * [C-1] SEQ 1 체크: 자신의 데이터 슬롯 (2.0ms 후)
             *
             * 조건: !seq1_executed && 2.0ms 경과
             * 동작: SLOT_TYPE_OWN으로 설정, TX 결정 로직으로 진행
             */
            if (!seq1_executed && dwt_timer_elapsed(last_sync_cycles, seq1_start_cycles)) {
                /* CRITICAL: Check for pending RX BEFORE starting TX */
                uint32_t pre_tx_status = dwt_readsysstatuslo();
                if (pre_tx_status & DWT_INT_RXFCG_BIT_MASK) {
                    test_run_info((unsigned char *)"[PRE-SEQ1] RX found in buffer before TX!");

                    /* Process the RX message immediately */
                    uint16_t frame_len = FRAME_LEN_MAX;
                    memset(rx_buffer, 0, sizeof(rx_buffer));
                    dwt_readrxdata(rx_buffer, frame_len, 0);
                    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

                }

                active_slot_type = SLOT_TYPE_OWN;
                seq1_executed = true;
                should_execute_slot = true;

                static char slot_debug[200];
                snprintf(slot_debug, sizeof(slot_debug),
                        "SEQ 1 (OWN) start! cycles_since_sync=%u, elapsed=%ums (2ms buffer complete)",
                        cycles_since_sync, cycles_since_sync / (CPU_FREQ_HZ/1000));
                test_run_info((unsigned char *)slot_debug);
            }
            /*
             * [C-2] SEQ 10 체크: FL 릴레이 슬롯 (12.4ms 후)
             *
             * 조건: !seq10_executed && has_fl_data && !fl_relay_completed && 12.4ms 경과
             * 참고: FL 데이터가 있고, 아직 릴레이 완료되지 않은 경우에만 실행
             */
            else if (!seq10_executed && has_fl_data && !fl_relay_completed && dwt_timer_elapsed(last_sync_cycles, seq10_start_cycles)) {
                /* CRITICAL: Check for pending RX BEFORE starting TX */
                uint32_t pre_tx_status = dwt_readsysstatuslo();
                if (pre_tx_status & DWT_INT_RXFCG_BIT_MASK) {
                    test_run_info((unsigned char *)"[PRE-SEQ10] RX found in buffer before TX!");

                    /* Process the RX message immediately */
                    uint16_t frame_len = FRAME_LEN_MAX;
                    memset(rx_buffer, 0, sizeof(rx_buffer));
                    dwt_readrxdata(rx_buffer, frame_len, 0);
                    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

                }

                active_slot_type = SLOT_TYPE_FL_RELAY;
                seq10_executed = true;
                should_execute_slot = true;

                static char slot_debug[200];
                snprintf(slot_debug, sizeof(slot_debug),
                        "SEQ 10 (FL_RELAY) start! cycles_since_sync=%u, elapsed=%ums, has_fl_data=%d",
                        cycles_since_sync, cycles_since_sync / (CPU_FREQ_HZ/1000), has_fl_data);
                test_run_info((unsigned char *)slot_debug);
            }
            /*
             * [C-3] SEQ 12 체크: FR 릴레이 슬롯 (15.1ms 후)
             *
             * 조건: !seq12_executed && has_fr_data && !fr_relay_completed && 15.1ms 경과
             * 참고: FR 데이터가 있고, 아직 릴레이 완료되지 않은 경우에만 실행
             */
            else if (!seq12_executed && has_fr_data && !fr_relay_completed && dwt_timer_elapsed(last_sync_cycles, seq12_start_cycles)) {
                /* CRITICAL: Check for pending RX BEFORE starting TX */
                /* This ensures we process any DATA messages that arrived just before our TX slot */
                uint32_t pre_tx_status = dwt_readsysstatuslo();
                if (pre_tx_status & DWT_INT_RXFCG_BIT_MASK) {
                    test_run_info((unsigned char *)"[PRE-SEQ12] RX found in buffer before TX!");

                    /* Process the RX message immediately */
                    uint16_t frame_len = FRAME_LEN_MAX;
                    memset(rx_buffer, 0, sizeof(rx_buffer));
                    dwt_readrxdata(rx_buffer, frame_len, 0);
                    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

                }

                active_slot_type = SLOT_TYPE_FR_RELAY;
                seq12_executed = true;
                should_execute_slot = true;

                static char slot_debug[200];
                snprintf(slot_debug, sizeof(slot_debug),
                        "SEQ 12 (FR_RELAY) start! cycles_since_sync=%u, elapsed=%ums, has_fr_data=%d",
                        cycles_since_sync, cycles_since_sync / (CPU_FREQ_HZ/1000), has_fr_data);
                test_run_info((unsigned char *)slot_debug);
            }

            /*
             * ========== [C-4] TX 결정 로직 ==========
             *
             * 슬롯이 활성화되면, 슬롯 타입에 따라 TX 수행 여부 결정:
             *   - SLOT_TYPE_OWN:      Period 홀수면 DATA, 짝수면 ACK_ARRAY
             *   - SLOT_TYPE_FL_RELAY: FL 데이터 릴레이 (has_fl_data && !fl_relay_completed)
             *   - SLOT_TYPE_FR_RELAY: FR 데이터 릴레이 (has_fr_data && !fr_relay_completed)
             *
             * TX 실행 후:
             *   - 릴레이 메시지는 retrans_msg에 저장 (재전송용)
             *   - RX 모드로 즉시 복귀
             */
            if (should_execute_slot) {
                /* Debug: Show which slot is starting */
                slot_active = true;  // Mark slot as active for ACK rejection logic

                /* Decide what to transmit based on active slot type */
                bool should_transmit = false;
                const char* tx_type_desc = "";

                /*
                 * [C-4a] SLOT_TYPE_OWN: 자신의 데이터/ACK 슬롯
                 *
                 * Period 홀수 (1,3,5): DATA 전송
                 *   - Period 1: 새 메시지 생성 + retrans_msg에 저장
                 *   - Period 3,5: retrans_msg에서 복사 (재전송)
                 *   - 이미 성공 시 IDLE (TX 스킵)
                 *
                 * Period 짝수 (2,4,6): ACK_ARRAY 전송
                 *   - data_received_from[] 배열을 페이로드로 전송
                 */
                if (active_slot_type == SLOT_TYPE_OWN) {
                    /* SEQ 1 - Determine period type: odd (1,3,5) = DATA, even (2,4,6) = ACK_ARRAY */
                    bool is_data_period = (period_count % 2 == 1);  // Odd period = DATA

                    if (is_data_period) {
                        /* Period 1, 3, 5: Send DATA (first TX or retransmission) */
                        if (tx_state == TX_STATE_IDLE || data_success_in_current_cycle) {
                            /* Already successful - no transmission needed, count as IDLE */
                            should_transmit = false;
                            tx_type_desc = "DATA IDLE (already successful)";

                            /* Count IDLE by pair */
                            if (period_in_cycle == 1) {
                                pair1_data_idle++;
                            } else if (period_in_cycle == 3) {
                                pair2_data_idle++;
                            } else if (period_in_cycle == 5) {
                                pair3_data_idle++;
                            }
                        } else {
                            /* Need to transmit DATA */
                            should_transmit = true;

                            /* CRITICAL: Only prepare NEW message in Period 1 */
                            /* Period 3, 5 should retransmit the SAME message from Period 1 */
                            if (period_in_cycle == 1) {
                                /* Period 1: Prepare NEW DATA frame */
                                tx_type_desc = "OWN DATA FIRST TX";
                                tx_msg[0] = 0xC5;
                                tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
                                tx_msg[IDX_SOURCE] = NODE_INIT;
                                tx_msg[IDX_DEST] = NODE_ALL;  // Broadcast
                                tx_msg[IDX_PRIORITY] = 1;
                                tx_msg[IDX_ORIG_SRC] = 0;
                                tx_msg[IDX_ORIG_DST] = 0;

                                /* Add TX timestamp (SYNC-relative time in microseconds) */
                                uint32_t tx_timestamp_us = (dwt_timer_get_cycles() - last_sync_cycles) / 64;
                                memcpy(&tx_msg[IDX_TX_TIMESTAMP], &tx_timestamp_us, sizeof(uint32_t));

                                /* Store for potential retransmission in Period 3, 5 */
                                memcpy(retrans_msg, tx_msg, sizeof(tx_msg));
                            } else {
                                /* Period 3, 5: RETRANSMIT same message from Period 1 */
                                tx_type_desc = "OWN DATA RETRANS";
                                memcpy(tx_msg, retrans_msg, sizeof(tx_msg));
                            }
                        }

                    } else {
                        /* Period 2, 4, 6: Send ACK_ARRAY */
                        should_transmit = true;
                        tx_type_desc = "OWN ACK_ARRAY TX";

                        /* Prepare ACK_ARRAY frame */
                        tx_msg[0] = 0xC5;
                        tx_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK_ARRAY;
                        tx_msg[IDX_SOURCE] = NODE_INIT;
                        tx_msg[IDX_DEST] = NODE_ALL;  // Broadcast
                        tx_msg[IDX_PRIORITY] = 1;
                        tx_msg[IDX_ORIG_SRC] = 0;
                        tx_msg[IDX_ORIG_DST] = 0;

                        /* Copy data_received_from[] array to payload (all 12 slots including relay) */
                        memcpy(&tx_msg[IDX_ACK_ARRAY], data_received_from, TOTAL_ARRAY_SIZE);

                        static char ack_array_debug[200];
                        int pos = snprintf(ack_array_debug, sizeof(ack_array_debug),
                                          "ACK_ARRAY payload: ");
                        for (int i = 0; i < TOTAL_ARRAY_SIZE && pos < sizeof(ack_array_debug) - 15; i++) {
                            if (data_received_from[i]) {
                                // Print node ID or relay slot name
                                if (i == SLOT_IDX_FL_RELAY) {
                                    pos += snprintf(ack_array_debug + pos, sizeof(ack_array_debug) - pos, "FL_REL ");
                                } else if (i == SLOT_IDX_FR_RELAY) {
                                    pos += snprintf(ack_array_debug + pos, sizeof(ack_array_debug) - pos, "FR_REL ");
                                } else {
                                    pos += snprintf(ack_array_debug + pos, sizeof(ack_array_debug) - pos,
                                                  "%c ", index_to_node_id(i));
                                }
                            }
                        }
                        test_run_info((unsigned char *)ack_array_debug);
                    }

                /*
                 * [C-4b] SLOT_TYPE_FL_RELAY: FL 릴레이 슬롯 (SEQ 10)
                 *
                 * 조건:
                 *   - has_fl_data: FL로부터 DATA 수신 완료
                 *   - !fl_relay_completed: 아직 릴레이 성공하지 않음
                 *
                 * 동작:
                 *   - 첫 전송: fl_relay_msg 사용
                 *   - 재전송: fl_retrans_msg 사용 (fl_pending_retrans 플래그)
                 */
                } else if (active_slot_type == SLOT_TYPE_FL_RELAY) {
                    /* SEQ 10 - FL relay transmission */
                    if (has_fl_data && !fl_relay_completed) {
                        if (!fl_pending_retrans) {
                            /* First FL relay transmission */
                            should_transmit = true;
                            tx_type_desc = "FL RELAY FIRST TX";
                            memcpy(tx_msg, fl_relay_msg, sizeof(tx_msg));
                        } else {
                            /* FL relay retransmission */
                            should_transmit = true;
                            tx_type_desc = "FL RELAY RETRANS";
                            memcpy(tx_msg, fl_retrans_msg, sizeof(tx_msg));
                        }
                    } else if (fl_relay_completed) {
                        should_transmit = false;
                        tx_type_desc = "FL RELAY SKIP (already completed)";
                    } else {
                        should_transmit = false;
                        tx_type_desc = "FL RELAY SKIP (no FL data)";
                    }

                /*
                 * [C-4c] SLOT_TYPE_FR_RELAY: FR 릴레이 슬롯 (SEQ 12)
                 *
                 * 조건:
                 *   - has_fr_data: FR로부터 DATA 수신 완료
                 *   - !fr_relay_completed: 아직 릴레이 성공하지 않음
                 *
                 * 동작:
                 *   - 첫 전송: fr_relay_msg 사용
                 *   - 재전송: fr_retrans_msg 사용 (fr_pending_retrans 플래그)
                 */
                } else if (active_slot_type == SLOT_TYPE_FR_RELAY) {
                    /* SEQ 12 - FR relay transmission */
                    if (has_fr_data && !fr_relay_completed) {
                        if (!fr_pending_retrans) {
                            /* First FR relay transmission */
                            should_transmit = true;
                            tx_type_desc = "FR RELAY FIRST TX";
                            memcpy(tx_msg, fr_relay_msg, sizeof(tx_msg));
                        } else {
                            /* FR relay retransmission */
                            should_transmit = true;
                            tx_type_desc = "FR RELAY RETRANS";
                            memcpy(tx_msg, fr_retrans_msg, sizeof(tx_msg));
                        }
                    } else if (fr_relay_completed) {
                        should_transmit = false;
                        tx_type_desc = "FR RELAY SKIP (already completed)";
                    } else {
                        should_transmit = false;
                        tx_type_desc = "FR RELAY SKIP (no FR data)";
                    }
                } else {
                    /* Invalid slot type */
                    should_transmit = false;
                    tx_type_desc = "INVALID SLOT TYPE";
                }

                static char tx_decision_msg[200];
                snprintf(tx_decision_msg, sizeof(tx_decision_msg),
                        "TX Decision: %s - SlotType: %d, State: %d, Period: %d/%d, Attempt: %d",
                        tx_type_desc, active_slot_type, tx_state, period_in_cycle, PERIODS_PER_CYCLE, retrans_attempt);
                test_run_info((unsigned char *)tx_decision_msg);

                if (should_transmit) {
                    /* Disable RX temporarily for TX */
                    dwt_forcetrxoff();

                    /* Send frame */
                    dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
                    dwt_writetxfctrl(sizeof(tx_msg), 0, 0);

                    /* Send DATA - no critical section needed in polling mode */
                    int tx_result = dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

                    if (tx_result == DWT_SUCCESS) {
                        /* Wait for TX completion */
                        uint32_t tx_status = 0;
                        waitforsysstatus(&tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                        if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
                            test_run_info((unsigned char *)"TX completed, returning to continuous RX");
                            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                            /* Store relay messages for potential retransmission */
                            /* Note: OWN data is already stored in Period 1 preparation */
                            if (active_slot_type == SLOT_TYPE_FL_RELAY) {
                                /* Store for FL relay retransmission */
                                memcpy(fl_retrans_msg, tx_msg, sizeof(tx_msg));
                            } else if (active_slot_type == SLOT_TYPE_FR_RELAY) {
                                /* Store for FR relay retransmission */
                                memcpy(fr_retrans_msg, tx_msg, sizeof(tx_msg));
                            }

                            /* Immediately return to RX mode for continuous listening */
                            dwt_forcetrxoff();
                            dwt_writesysstatuslo(0xFFFFFFFF);
                            dwt_rxenable(DWT_START_RX_IMMEDIATE);

                            /* Note: No ACK collection - we passively listen for DATA/ACK_ARRAY messages */
                        } else {
                            test_run_info((unsigned char *)"TX completed but status check failed");
                        }

                        /* Increment sequence number for next TX */
                        tx_msg[DATA_FRAME_SN_IDX]++;
                    } else {
                        test_run_info((unsigned char *)"DATA TX failed to start");
                    }
                } else {
                    /* Not transmitting - just log and finish slot */
                    test_run_info((unsigned char *)"Slot completed - No transmission needed");

                    /* Re-enable RX for normal operation */
                    dwt_forcetrxoff();
                    dwt_writesysstatuslo(0xFFFFFFFF);
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                }
                /* Slot finished */
                slot_active = false;
            }
        }

        /* Note: No ACK collection end check - we use continuous RX mode */

        /*
         * ========== [D] RX 폴링: 메시지 수신 처리 ==========
         *
         * 상태 레지스터를 폴링하여 RX 이벤트 감지:
         *   - DWT_INT_RXFCG_BIT_MASK: 프레임 수신 성공
         *   - DWT_INT_RXFTO_BIT_MASK: RX 타임아웃
         *   - 에러 플래그: PHE, FCE, FSL, STO
         *
         * 수신 메시지 처리:
         *   - Period 홀수: DATA/RELAY_DATA → data_received_from[] 업데이트
         *   - Period 짝수: ACK_ARRAY → ACK 확인 및 성공 판정
         *   - FL/FR DATA: 릴레이용 메시지 저장
         */

        /* 2. POLLING: Check for RX events by reading status register */
        uint32_t status_reg = dwt_readsysstatuslo();

        /*
         * [D-1] 프레임 수신 성공 (RX Frame Good)
         *
         * 처리 순서:
         *   1. 프레임 데이터 읽기
         *   2. 메시지 타입에 따른 분기
         *   3. RX 재활성화
         */
        if (status_reg & DWT_INT_RXFCG_BIT_MASK) {  // RX frame received
            /* Get frame length - use a fixed max length for simplicity */
            uint16_t frame_len = FRAME_LEN_MAX;  // Read max possible length

            /* Clear buffer and read frame */
            memset(rx_buffer, 0, sizeof(rx_buffer));

            /* Read the received frame data */
            dwt_readrxdata(rx_buffer, frame_len, 0);

            /* Clear RX good flag */
            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

            /* Process all received messages */
            uint32_t elapsed_ms = get_elapsed_ms(last_sync_cycles);

            /* Print received frame for debugging */
            static char rx_log[100];
            snprintf(rx_log, sizeof(rx_log), "[%ums] RX: type=0x%02X, src=%c",
                    elapsed_ms, rx_buffer[IDX_MSG_TYPE], rx_buffer[IDX_SOURCE]);
            test_run_info((unsigned char *)rx_log);

            /* ========== Aggregated ACKs 프로토콜: 메시지 타입 처리 ========== */

            /*
             * [D-2] Period 홀수 (1,3,5): DATA 메시지 수신 처리
             *
             * 대상: MSG_TYPE_DATA, MSG_TYPE_RELAY_DATA
             * 동작:
             *   - get_slot_index()로 슬롯 인덱스 계산
             *   - data_received_from[slot_idx] = 1로 수신 기록
             *   - 지연시간 측정 (TX timestamp - RX timestamp)
             */
            if ((period_count % 2 == 1) &&
                (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA || rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_RELAY_DATA)) {

                /* Get slot index for this transmission */
                uint8_t slot_idx = get_slot_index(
                    rx_buffer[IDX_MSG_TYPE],
                    rx_buffer[IDX_SOURCE],
                    rx_buffer[IDX_ORIG_SRC]
                );

                if (slot_idx != 0xFF && slot_idx < TOTAL_ARRAY_SIZE) {
                    if (!data_received_from[slot_idx]) {
                        data_received_from[slot_idx] = 1;
                        unique_data_count++;

                        static char data_msg[150];
                        snprintf(data_msg, sizeof(data_msg),
                                "[%ums] P%d DATA from %s (slot %d) - count: %d",
                                elapsed_ms, period_count, get_slot_description(slot_idx),
                                slot_idx, unique_data_count);
                        test_run_info((unsigned char *)data_msg);
                    }

                    /* Latency measurement (both DATA and RELAY_DATA) */
                    /* Calculate RX timestamp (SYNC-relative, in microseconds) */
                    uint32_t rx_timestamp_us = (dwt_timer_get_cycles() - last_sync_cycles) / 64;

                    /* Extract TX timestamp from message */
                    uint32_t tx_timestamp_us;
                    memcpy(&tx_timestamp_us, &rx_buffer[IDX_TX_TIMESTAMP], sizeof(uint32_t));

                    /* Calculate one-way delay (sanity check: RX > TX) */
                    if (rx_timestamp_us > tx_timestamp_us) {
                        uint32_t oneway_delay_us = rx_timestamp_us - tx_timestamp_us;

                        /* For RELAY_DATA, attribute latency to relay slot (includes relay overhead) */
                        uint8_t latency_slot_idx = slot_idx;

                        /* Update per-node latency statistics */
                        update_node_latency(&node_latency[latency_slot_idx], oneway_delay_us);

                        /* Debug log */
                        static char latency_log[150];
                        snprintf(latency_log, sizeof(latency_log),
                                "Latency from %s: %u us (%.3f ms) - TX:%u, RX:%u",
                                get_slot_description(latency_slot_idx), oneway_delay_us,
                                oneway_delay_us / 1000.0, tx_timestamp_us, rx_timestamp_us);
                        test_run_info((unsigned char *)latency_log);
                    }
                }

            }

            /*
             * [D-3] Period 짝수 (2,4,6): ACK_ARRAY 메시지 수신 처리
             *
             * 대상: MSG_TYPE_ACK_ARRAY
             * 동작:
             *   - INIT 자신의 ACK 확인: ack_array[SLOT_IDX_INIT]
             *   - FL_RELAY ACK 확인: ack_array[SLOT_IDX_FL_RELAY]
             *   - FR_RELAY ACK 확인: ack_array[SLOT_IDX_FR_RELAY]
             *
             * 성공 조건:
             *   - INIT DATA: 4개 노드로부터 ACK 수신
             *   - FL/FR RELAY: 2개 노드로부터 ACK 수신
             */
            else if ((period_count % 2 == 0) && (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_ACK_ARRAY)) {

                uint8_t src_node = rx_buffer[IDX_SOURCE];
                uint8_t src_slot_idx = node_id_to_index(src_node);
                uint8_t *ack_array = &rx_buffer[IDX_ACK_ARRAY];  // 12 bytes

                /* Display ACK_ARRAY content for visibility */
                static char ack_array_display[250];
                int pos = snprintf(ack_array_display, sizeof(ack_array_display),
                                  "RX ACK_ARRAY from %s: ", get_slot_description(src_slot_idx));
                for (int i = 0; i < TOTAL_ARRAY_SIZE && pos < sizeof(ack_array_display) - 15; i++) {
                    if (ack_array[i]) {
                        // Print node ID or relay slot name
                        if (i == SLOT_IDX_FL_RELAY) {
                            pos += snprintf(ack_array_display + pos, sizeof(ack_array_display) - pos, "FL_REL ");
                        } else if (i == SLOT_IDX_FR_RELAY) {
                            pos += snprintf(ack_array_display + pos, sizeof(ack_array_display) - pos, "FR_REL ");
                        } else {
                            pos += snprintf(ack_array_display + pos, sizeof(ack_array_display) - pos,
                                          "%c ", index_to_node_id(i));
                        }
                    }
                }
                test_run_info((unsigned char *)ack_array_display);

                /* INIT has 3 slots to check: INIT(0), FL_RELAY(9), FR_RELAY(11) */
                /* Check each slot separately */

                /* Check INIT's own data (SLOT_IDX_INIT = 0) */
                if (is_expected_ack_slot_for(src_slot_idx, SLOT_IDX_INIT)) {
                    if (ack_array[SLOT_IDX_INIT] == 1) {
                        if (!cumulative_ack_confirmed[src_slot_idx]) {
                            cumulative_ack_confirmed[src_slot_idx] = 1;
                            cumulative_ack_count++;

                            static char init_ack_msg[150];
                            snprintf(init_ack_msg, sizeof(init_ack_msg),
                                    "INIT ACK from %s (slot %d) - cumulative: %d/%d",
                                    get_slot_description(src_slot_idx), src_slot_idx,
                                    cumulative_ack_count, TEST_EXPECTED_ACKS);
                            test_run_info((unsigned char *)init_ack_msg);

                            /* Check if all expected ACKs for INIT received */
                            if (cumulative_ack_count >= TEST_EXPECTED_ACKS) {
                                /* DATA success - track by pair and update cycle flag */
                                if (!data_success_in_current_cycle) {
                                    data_success_in_current_cycle = true;

                                    /* Increment pair-specific success counter */
                                    if (period_in_cycle == 1 || period_in_cycle == 2) {
                                        pair1_data_success++;
                                    } else if (period_in_cycle == 3 || period_in_cycle == 4) {
                                        pair2_data_success++;
                                    } else if (period_in_cycle == 5 || period_in_cycle == 6) {
                                        pair3_data_success++;
                                    }

                                    static char init_success_msg[150];
                                    snprintf(init_success_msg, sizeof(init_success_msg),
                                            "DATA SUCCESS in Period %d (Pair %d) - All 4 ACKs received",
                                            period_in_cycle, (period_in_cycle + 1) / 2);
                                    test_run_info((unsigned char *)init_success_msg);
                                }
                            }
                        }
                    }
                }

                /* Check FL_RELAY (SLOT_IDX_FL_RELAY = 9) */
                if (is_expected_ack_slot_for(src_slot_idx, SLOT_IDX_FL_RELAY)) {
                    if (ack_array[SLOT_IDX_FL_RELAY] == 1) {
                        if (!fl_relay_ack_received_from[src_slot_idx]) {
                            fl_relay_ack_received_from[src_slot_idx] = 1;
                            fl_relay_unique_ack_count++;

                            static char fl_ack_msg[150];
                            snprintf(fl_ack_msg, sizeof(fl_ack_msg),
                                    "FL_RELAY ACK from %s (slot %d) - count: %d/%d",
                                    get_slot_description(src_slot_idx), src_slot_idx,
                                    fl_relay_unique_ack_count, TEST_RELAY_EXPECTED_ACKS);
                            test_run_info((unsigned char *)fl_ack_msg);

                            /* Check if all expected ACKs for FL_RELAY received */
                            if (fl_relay_unique_ack_count >= TEST_RELAY_EXPECTED_ACKS) {
                                fl_relay_completed = true;  // Mark as completed

                                /* FL_RELAY success - track by pair and update cycle flag */
                                if (!fl_relay_success_in_current_cycle) {
                                    fl_relay_success_in_current_cycle = true;

                                    /* Increment pair-specific success counter (early completion = IDLE) */
                                    if (period_in_cycle == 1 || period_in_cycle == 2) {
                                        pair1_fl_relay_idle++;  // Early completion in Period 2 = IDLE
                                    } else if (period_in_cycle == 3 || period_in_cycle == 4) {
                                        pair2_fl_relay_idle++;
                                    } else if (period_in_cycle == 5 || period_in_cycle == 6) {
                                        pair3_fl_relay_idle++;
                                    }

                                    static char fl_success_msg[150];
                                    snprintf(fl_success_msg, sizeof(fl_success_msg),
                                            "FL_RELAY EARLY SUCCESS in Period %d (Pair %d) - IDLE (skip TX)",
                                            period_in_cycle, (period_in_cycle + 1) / 2);
                                    test_run_info((unsigned char *)fl_success_msg);
                                }
                            }
                        }
                    }
                }

                /* Check FR_RELAY (SLOT_IDX_FR_RELAY = 11) */
                if (is_expected_ack_slot_for(src_slot_idx, SLOT_IDX_FR_RELAY)) {
                    if (ack_array[SLOT_IDX_FR_RELAY] == 1) {
                        if (!fr_relay_ack_received_from[src_slot_idx]) {
                            fr_relay_ack_received_from[src_slot_idx] = 1;
                            fr_relay_unique_ack_count++;

                            static char fr_ack_msg[150];
                            snprintf(fr_ack_msg, sizeof(fr_ack_msg),
                                    "FR_RELAY ACK from %s (slot %d) - count: %d/2",
                                    get_slot_description(src_slot_idx), src_slot_idx,
                                    fr_relay_unique_ack_count);
                            test_run_info((unsigned char *)fr_ack_msg);

                            /* Check if all expected ACKs for FR_RELAY received */
                            if (fr_relay_unique_ack_count >= TEST_RELAY_EXPECTED_ACKS) {
                                fr_relay_completed = true;  // Mark as completed

                                /* FR_RELAY success - track by pair and update cycle flag */
                                if (!fr_relay_success_in_current_cycle) {
                                    fr_relay_success_in_current_cycle = true;

                                    /* Increment pair-specific success counter (early completion = IDLE) */
                                    if (period_in_cycle == 1 || period_in_cycle == 2) {
                                        pair1_fr_relay_idle++;  // Early completion in Period 2 = IDLE
                                    } else if (period_in_cycle == 3 || period_in_cycle == 4) {
                                        pair2_fr_relay_idle++;
                                    } else if (period_in_cycle == 5 || period_in_cycle == 6) {
                                        pair3_fr_relay_idle++;
                                    }

                                    static char fr_success_msg[150];
                                    snprintf(fr_success_msg, sizeof(fr_success_msg),
                                            "FR_RELAY EARLY SUCCESS in Period %d (Pair %d) - IDLE (skip TX)",
                                            period_in_cycle, (period_in_cycle + 1) / 2);
                                    test_run_info((unsigned char *)fr_success_msg);
                                }
                            }
                        }
                    }
                }
            }

            /*
             * [D-4] FL/FR 릴레이 데이터 저장
             *
             * FL(NODE_FL) 또는 FR(NODE_FR)로부터 DATA 수신 시:
             *   - MSG_TYPE을 RELAY_DATA로 변환
             *   - 원본 소스/목적지 정보 보존
             *   - has_fl_data / has_fr_data 플래그 설정
             *   - SEQ 10 / SEQ 12 슬롯에서 릴레이 전송
             */
            if (rx_buffer[IDX_MSG_TYPE] == MSG_TYPE_DATA) {
                uint8_t src_node = rx_buffer[IDX_SOURCE];

                if (src_node == NODE_FL) {
                    /* Store FL message for relay in SEQ 10 */
                    memcpy(fl_relay_msg, rx_buffer, FRAME_LEN_MAX);
                    /* Convert to relay message */
                    fl_relay_msg[IDX_MSG_TYPE] = MSG_TYPE_RELAY_DATA;
                    fl_relay_msg[IDX_SOURCE] = NODE_INIT;  // INIT becomes the sender
                    fl_relay_msg[IDX_DEST] = NODE_ALL;     // Broadcast relay
                    fl_relay_msg[IDX_ORIG_SRC] = src_node; // Original source
                    fl_relay_msg[IDX_ORIG_DST] = rx_buffer[IDX_DEST]; // Original destination
                    has_fl_data = true;
                    fl_pending_retrans = false;

                    static char fl_msg[100];
                    snprintf(fl_msg, sizeof(fl_msg), "[%ums] FL data stored for relay in SEQ 10", elapsed_ms);
                    test_run_info((unsigned char *)fl_msg);

                } else if (src_node == NODE_FR) {
                    /* Store FR message for relay in SEQ 12 */
                    memcpy(fr_relay_msg, rx_buffer, FRAME_LEN_MAX);
                    /* Convert to relay message */
                    fr_relay_msg[IDX_MSG_TYPE] = MSG_TYPE_RELAY_DATA;
                    fr_relay_msg[IDX_SOURCE] = NODE_INIT;  // INIT becomes the sender
                    fr_relay_msg[IDX_DEST] = NODE_ALL;     // Broadcast relay
                    fr_relay_msg[IDX_ORIG_SRC] = src_node; // Original source
                    fr_relay_msg[IDX_ORIG_DST] = rx_buffer[IDX_DEST]; // Original destination
                    has_fr_data = true;
                    fr_pending_retrans = false;

                    static char fr_msg[100];
                    snprintf(fr_msg, sizeof(fr_msg), "[%ums] FR data stored for relay in SEQ 12", elapsed_ms);
                    test_run_info((unsigned char *)fr_msg);
                }
            }

            /* Re-enable RX for continuous reception */
            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }

        /*
         * [D-5] RX 타임아웃 처리
         *
         * 원인: 설정된 RX 타임아웃 시간 내 수신 없음
         * 동작: 상태 플래그 클리어 후 RX 재활성화
         */
        if (status_reg & DWT_INT_RXFTO_BIT_MASK) {
            /* Clear timeout flag and re-enable RX */
            dwt_writesysstatuslo(DWT_INT_RXFTO_BIT_MASK);
            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }

        /*
         * [D-6] RX 에러 처리
         *
         * 에러 유형:
         *   - RXPHE: PHR 에러 (Physical Header Error)
         *   - RXFCE: FCS 에러 (Frame Check Sequence Error)
         *   - RXFSL: Reed-Solomon 동기화 손실
         *   - RXSTO: SFD 타임아웃 (Start of Frame Delimiter)
         *
         * 동작: 에러 플래그 클리어 후 RX 재활성화
         */
        if (status_reg & (DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXFCE_BIT_MASK |
                          DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXSTO_BIT_MASK)) {
            /* Clear error flags and re-enable RX */
            dwt_writesysstatuslo(DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXFCE_BIT_MASK |
                               DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXSTO_BIT_MASK);
            dwt_forcetrxoff();
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }

    }
}


#endif
