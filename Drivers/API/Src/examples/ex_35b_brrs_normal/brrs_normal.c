/*! ----------------------------------------------------------------------------
 *  @file    brrs_normal.c
 *  @brief   BRRS Experiment - Normal Node
 *
 *           BRRS(Beacon-Rate Reduced Synchronization) 논문 실험용 Normal 노드.
 *
 *           [v1.1 변경사항]
 *           - Delayed-TX 적용: SYNC RX timestamp 기반으로 정확한 슬롯 시각에 송신
 *           - 코디네이터의 delayed-RX 윈도우와 일치하도록 타이밍 정밀화
 *           - SYNC RX 윈도우만 폭넓게, DATA RX는 최소화
 *
 *           [v1.2 변경사항] (2026-07)
 *           - PREAMBLE_US/SFD_US 올림(ceil) 계산 — brrs_init.c와 반드시 동일해야 함
 *           - dwt_setrxtimeout에 US_TO_UUS 단위 변환 적용
 *
 *           [v1.5 변경사항] (2026-08)
 *           - DATA timestamp 필드를 CPU 준비 시각이 아닌 예약 슬롯 오프셋으로 통일
 *           - CPU 기반 pseudo-latency 통계 제거
 *
 *           [v1.6 변경사항] (2026-08)
 *           - 실험 4 DATA 프리앰블을 비컨의 m 필드에서 런타임 적용
 *
 *           [v1.7 변경사항] (2026-08)
 *           - 비컨 기반 DATA 프리앰블 적용을 실험 1~4 전체로 확장
 *
 *           [v1.8 변경사항] (2026-08)
 *           - coordinator fast-command-priority 연속 슬롯 RX와 릴리스 동기화
 *
 *           [v1.9 변경사항] (2026-08)
 *           - 비컨 DATA PHY 적용 실패 및 dwt_configure 실패 시 fail-closed 복구
 *           - 실험 4 coordinator continuous RX burst와 릴리스 동기화
 *
 *           [v2.0 변경사항] (2026-08)
 *           - beacon protocol v3 및 8-byte DATA 헤더 적용
 *           - 중복 슬롯 오프셋 필드 제거, 송신 슬롯은 비컨 스케줄만 사용
 *           - 잘못된 길이/비제어 프레임 수신 후 SYNC RX 복구 보강
 *
 *           [v2.1 변경사항] (2026-08)
 *           - coordinator manual double-buffer/FINT-RDB 수신 경로와 버전 동기화
 *           - 비컨 슬롯 간격과 마지막 슬롯 경계를 런타임 PHY 값으로 검증
 *
 *           [v2.2 변경사항] (2026-08)
 *           - coordinator FINT event-mask 검증 릴리스와 버전 동기화
 *
 *           [v2.3 변경사항] (2026-08)
 *           - coordinator polling IRQ 상태 추적 수정 릴리스와 버전 동기화
 *
 *           [v2.4 진단 변경사항] (2026-08)
 *           - 비컨 RX RMARKER 대비 실제 DATA TX RMARKER 오차 집계 추가
 *
 *           [v2.6 실험 조건 변경사항] (2026-08)
 *           - 실험 1~4 DATA를 8 B header + 16 B application + 2 B FCS로 통일
 *           - 실험 1~3의 기존 최대 PSDU 127 B 조건을 제출용 26 B 조건으로 교체
 */

#include "deca_probe_interface.h"
#include <deca_device_api.h>
#include <deca_spi.h>
#include <example_selection.h>
#include <port.h>
#include <shared_defines.h>
#include <shared_functions.h>
#include "../brrs_beacon_protocol.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "nrf_delay.h"
#include "nrf.h"

#if defined(TEST_BRRS_NORMAL)

extern void test_run_info(unsigned char *data);
extern int SEGGER_RTT_ConfigUpBuffer(unsigned BufferIndex, const char* sName, void* pBuffer, unsigned BufferSize, unsigned Flags);
extern unsigned SEGGER_RTT_WriteString(unsigned BufferIndex, const char* s);

#define EXP_LOG_RTT_CHANNEL     1
#define EXP_LOG_RTT_BUFFER_SIZE 32768
#define EXP_LOG_RTT_MODE_BLOCK  2U

static char exp_log_rtt_buffer[EXP_LOG_RTT_BUFFER_SIZE];
static bool exp_log_rtt_configured = false;

static void exp_log_init(void)
{
    if (!exp_log_rtt_configured) {
        SEGGER_RTT_ConfigUpBuffer(EXP_LOG_RTT_CHANNEL, "EXP_LOG",
                                  exp_log_rtt_buffer, sizeof(exp_log_rtt_buffer),
                                  EXP_LOG_RTT_MODE_BLOCK);
        exp_log_rtt_configured = true;
    }
}

static void exp_log_info(const char *line)
{
    exp_log_init();
    SEGGER_RTT_WriteString(EXP_LOG_RTT_CHANNEL, line);
    SEGGER_RTT_WriteString(EXP_LOG_RTT_CHANNEL, "\n");
}

static void final_log_info(const char *line)
{
    test_run_info((unsigned char *)line);
    exp_log_info(line);
}

static void terminal_log_info(unsigned char *data)
{
    test_run_info(data);
    exp_log_info((const char *)data);
}

#define test_run_info(data) terminal_log_info(data)

/* ========== 노드 선택 ========== */
#if !defined(TEST_NODE_2) && !defined(TEST_NODE_3) && !defined(TEST_NODE_4) && \
    !defined(TEST_NODE_5) && !defined(TEST_NODE_6) && !defined(TEST_NODE_7) && \
    !defined(TEST_NODE_8)
#define TEST_NODE_2
#endif
//#define TEST_NODE_3
//#define TEST_NODE_4
//#define TEST_NODE_5
//#define TEST_NODE_6
//#define TEST_NODE_7
//#define TEST_NODE_8

#ifdef TEST_NODE_2
    #define APP_NAME "BRRS NODE 2 v2.6 (beacon-scheduled delayed-TX)"
    #define MY_NODE_ID  '2'
    #define MY_NODE_SEQ 2
#elif defined(TEST_NODE_3)
    #define APP_NAME "BRRS NODE 3 v2.6 (beacon-scheduled delayed-TX)"
    #define MY_NODE_ID  '3'
    #define MY_NODE_SEQ 3
#elif defined(TEST_NODE_4)
    #define APP_NAME "BRRS NODE 4 v2.6 (beacon-scheduled delayed-TX)"
    #define MY_NODE_ID  '4'
    #define MY_NODE_SEQ 4
#elif defined(TEST_NODE_5)
    #define APP_NAME "BRRS NODE 5 v2.6 (beacon-scheduled delayed-TX)"
    #define MY_NODE_ID  '5'
    #define MY_NODE_SEQ 5
#elif defined(TEST_NODE_6)
    #define APP_NAME "BRRS NODE 6 v2.6 (beacon-scheduled delayed-TX)"
    #define MY_NODE_ID  '6'
    #define MY_NODE_SEQ 6
#elif defined(TEST_NODE_7)
    #define APP_NAME "BRRS NODE 7 v2.6 (beacon-scheduled delayed-TX)"
    #define MY_NODE_ID  '7'
    #define MY_NODE_SEQ 7
#elif defined(TEST_NODE_8)
    #define APP_NAME "BRRS NODE 8 v2.6 (beacon-scheduled delayed-TX)"
    #define MY_NODE_ID  '8'
    #define MY_NODE_SEQ 8
#else
    #error "Please select a node type (TEST_NODE_2 ~ TEST_NODE_8)"
#endif

#ifndef BRRS_EXPERIMENT
#define BRRS_EXPERIMENT  3
#endif
#ifndef BRRS_EXPLICIT_PROFILE
#define BRRS_EXPLICIT_PROFILE 1
#endif
#if !BRRS_EXPLICIT_PROFILE
#error "Select an explicit Exp*_Normal Build Configuration; generic Debug/Release is disabled"
#endif
#if BRRS_EXPERIMENT < 1 || BRRS_EXPERIMENT > 4
#error "BRRS_EXPERIMENT must be between 1 and 4"
#endif

/* Experiment 3 PHY condition. Both INIT and NORMAL must use the same value.
 * A: 8-symbol SFD + standard-rate PHR
 * B: 16-symbol SFD + standard-rate PHR
 * C: 8-symbol SFD + data-rate PHR */
#define EXP3_VARIANT_A 1
#define EXP3_VARIANT_B 2
#define EXP3_VARIANT_C 3
#ifndef EXP3_PHY_VARIANT
#define EXP3_PHY_VARIANT EXP3_VARIANT_A
#endif
#if EXP3_PHY_VARIANT < EXP3_VARIANT_A || EXP3_PHY_VARIANT > EXP3_VARIANT_C
#error "EXP3_PHY_VARIANT must be EXP3_VARIANT_A, B, or C"
#endif

#ifndef BRRS_DATA_PLEN
#define BRRS_DATA_PLEN  DWT_PLEN_32
#endif
#define DATA_PLEN       BRRS_DATA_PLEN
#define SYNC_PLEN       DWT_PLEN_256
#define SYNC_PREAMBLE_SYMBOLS 256
#define ENABLE_CIR      0
#ifndef BRRS_TARGET_CYCLES
#define BRRS_TARGET_CYCLES 1000
#endif
#define TARGET_CYCLES   BRRS_TARGET_CYCLES

#if BRRS_EXPERIMENT == 1 || BRRS_EXPERIMENT == 2 || BRRS_EXPERIMENT == 3
#define TOTAL_NODES         2
#define TOTAL_SLOTS         2
#define TOTAL_ARRAY_SIZE    2
#elif BRRS_EXPERIMENT == 4
#ifndef BRRS_SENSOR_NODES
#define BRRS_SENSOR_NODES   2
#endif
#if BRRS_SENSOR_NODES < 1 || BRRS_SENSOR_NODES > 7
#error "BRRS_SENSOR_NODES must be between 1 and 7"
#endif
#define TOTAL_NODES         (BRRS_SENSOR_NODES + 1)
#define TOTAL_SLOTS         BRRS_SENSOR_NODES
#define TOTAL_ARRAY_SIZE    TOTAL_NODES
#endif
/* All experiments use one beacon and one DATA schedule per superframe. */
#define PERIODS_PER_CYCLE   1

#if BRRS_EXPERIMENT == 4
#if MY_NODE_SEQ < 2 || (MY_NODE_SEQ - 1) > BRRS_SENSOR_NODES
#error "Selected Exp4 node is outside BRRS_SENSOR_NODES"
#endif
#elif MY_NODE_SEQ > TOTAL_NODES
#error "선택한 노드 번호가 TOTAL_NODES 초과. 실험 1/2/3은 TEST_NODE_2만 사용 가능."
#endif

#if BRRS_EXPERIMENT == 4
#ifndef BRRS_SLOT_GUARD_US
#define BRRS_SLOT_GUARD_US  100
#endif
#define SLOT_GUARD_US       BRRS_SLOT_GUARD_US
#else
#define SLOT_GUARD_US       500
#endif
#ifndef BRRS_SYNC_BUFFER_US
#define BRRS_SYNC_BUFFER_US 3000
#endif
#define SYNC_BUFFER_US      BRRS_SYNC_BUFFER_US
/* All experiments use the same short sensor frame on air. */
#ifndef BRRS_APP_PAYLOAD_BYTES
#define BRRS_APP_PAYLOAD_BYTES 16
#endif
#define BRRS_PROTOCOL_HEADER_BYTES BRRS_COMMON_HEADER_BYTES
#define IEEE_802154_FCS_BYTES       BRRS_IEEE_802154_FCS_BYTES
#define PSDU_BYTES          (BRRS_PROTOCOL_HEADER_BYTES + BRRS_APP_PAYLOAD_BYTES + IEEE_802154_FCS_BYTES)
_Static_assert(PSDU_BYTES <= 127U, "DATA PSDU exceeds the IEEE 802.15.4 maximum");

#if EXP3_PHY_VARIANT == EXP3_VARIANT_A
#define EXP3_VARIANT_NAME   "A"
#define EXP3_PHR_RATE_NAME  "STD"
#define DATA_SFD_TYPE       1
#define DATA_PHR_RATE       DWT_PHRRATE_STD
#define SFD_SYMBOLS         8
#define PHR_SYMBOL_X100_NS  102564UL
#elif EXP3_PHY_VARIANT == EXP3_VARIANT_B
#define EXP3_VARIANT_NAME   "B"
#define EXP3_PHR_RATE_NAME  "STD"
#define DATA_SFD_TYPE       2
#define DATA_PHR_RATE       DWT_PHRRATE_STD
#define SFD_SYMBOLS         16
#define PHR_SYMBOL_X100_NS  102564UL
#else
#define EXP3_VARIANT_NAME   "C"
#define EXP3_PHR_RATE_NAME  "DTA"
#define DATA_SFD_TYPE       1
#define DATA_PHR_RATE       DWT_PHRRATE_DTA
#define SFD_SYMBOLS         8
#define PHR_SYMBOL_X100_NS  12821UL
#endif

/* DW3000 datasheet timing model (PRF64, 6.81 Mbps).
 * PSDU over-the-air time includes 48 Reed-Solomon parity bits for every
 * block of up to 330 PSDU bits. */
#define IPATOV_SYMBOL_X100_NS  101763UL
#define DATA_SYMBOL_X100_NS    12821UL
#define PHR_SYMBOLS             21UL
#define RS_DATA_BITS_PER_BLOCK 330UL
#define RS_PARITY_BITS          48UL
#define PSDU_BITS               ((uint32_t)PSDU_BYTES * 8UL)
#define PSDU_RS_BLOCKS          ((PSDU_BITS + RS_DATA_BITS_PER_BLOCK - 1UL) / RS_DATA_BITS_PER_BLOCK)
#define PSDU_RS_PARITY_BITS     (PSDU_RS_BLOCKS * RS_PARITY_BITS)
#define PSDU_ENCODED_BITS       (PSDU_BITS + PSDU_RS_PARITY_BITS)
#define BEACON_PSDU_BITS        ((uint32_t)BRRS_BEACON_PSDU_BYTES * 8UL)
#define BEACON_RS_BLOCKS        ((BEACON_PSDU_BITS + RS_DATA_BITS_PER_BLOCK - 1UL) / RS_DATA_BITS_PER_BLOCK)
#define BEACON_ENCODED_BITS     (BEACON_PSDU_BITS + BEACON_RS_BLOCKS * RS_PARITY_BITS)
#define X100_NS_TO_NS_CEIL(v)   (((v) + 99UL) / 100UL)
#define NS_TO_US_CEIL(v)        (((v) + 999UL) / 1000UL)

#define PREAMBLE_SYMBOLS    ((DATA_PLEN + 1) * 8)
#define PREAMBLE_MODEL_NS   X100_NS_TO_NS_CEIL((uint32_t)PREAMBLE_SYMBOLS * IPATOV_SYMBOL_X100_NS)
#define SFD_MODEL_NS        X100_NS_TO_NS_CEIL((uint32_t)SFD_SYMBOLS * IPATOV_SYMBOL_X100_NS)
#define SYNC_PREAMBLE_MODEL_NS X100_NS_TO_NS_CEIL((uint32_t)SYNC_PREAMBLE_SYMBOLS * IPATOV_SYMBOL_X100_NS)
#define SYNC_SFD_MODEL_NS   X100_NS_TO_NS_CEIL(8UL * IPATOV_SYMBOL_X100_NS)
#define PHR_MODEL_NS        X100_NS_TO_NS_CEIL(PHR_SYMBOLS * PHR_SYMBOL_X100_NS)
#define PSDU_MODEL_NS       X100_NS_TO_NS_CEIL(PSDU_ENCODED_BITS * DATA_SYMBOL_X100_NS)
#define SYNC_PHR_MODEL_NS   X100_NS_TO_NS_CEIL(PHR_SYMBOLS * 102564UL)
#define BEACON_PSDU_MODEL_NS X100_NS_TO_NS_CEIL(BEACON_ENCODED_BITS * DATA_SYMBOL_X100_NS)
#define SHR_MODEL_NS        (PREAMBLE_MODEL_NS + SFD_MODEL_NS)
#define DP_MODEL_NS         (PHR_MODEL_NS + PSDU_MODEL_NS)
#define BEACON_DP_MODEL_NS  (SYNC_PHR_MODEL_NS + BEACON_PSDU_MODEL_NS)
#define FRAME_MODEL_NS      (SHR_MODEL_NS + DP_MODEL_NS)

/* Integer microseconds are used only for TDMA scheduling. The exact ns model
 * above is retained for the Experiment 3 comparison. */
#define PREAMBLE_US         ((int)NS_TO_US_CEIL(PREAMBLE_MODEL_NS))
#define SFD_US              ((int)NS_TO_US_CEIL(SFD_MODEL_NS))
#define PHR_PSDU_US         ((int)NS_TO_US_CEIL(DP_MODEL_NS))
#define BEACON_PHR_PSDU_US  ((int)NS_TO_US_CEIL(BEACON_DP_MODEL_NS))
#define SYNC_RMARKER_OFFSET_US ((int)NS_TO_US_CEIL(SYNC_PREAMBLE_MODEL_NS + SYNC_SFD_MODEL_NS))
#define SYNC_FRAME_US       (SYNC_RMARKER_OFFSET_US + BEACON_PHR_PSDU_US)
#define SYNC_RX_LEAD_MARGIN_US 12
#define SYNC_RX_EARLY_US    (SYNC_RMARKER_OFFSET_US + SYNC_RX_LEAD_MARGIN_US)
#define SYNC_RX_WINDOW_US   (SYNC_RX_EARLY_US + BEACON_PHR_PSDU_US)
#define SLOT_INTERVAL_US    (PREAMBLE_US + SFD_US + PHR_PSDU_US + SLOT_GUARD_US)
/* Common fixed superframe timing for Experiments 1-4. */
#ifndef BRRS_SUPERFRAME_US
#define BRRS_SUPERFRAME_US  10000
#endif
#if BRRS_EXPERIMENT == 4
#ifndef BRRS_EXP4_SYNC_PREP_US
/* Keep the sensor's static capacity model aligned with the coordinator. */
#define BRRS_EXP4_SYNC_PREP_US 2500
#endif
#define EXP4_SYNC_PREP_US   BRRS_EXP4_SYNC_PREP_US
#define EXP4_COORD_CONFIG_SWITCH_US (BRRS_SUPERFRAME_US - EXP4_SYNC_PREP_US)
/* NORMAL starts its MCU timer when the complete SYNC frame is reported. */
#define CONFIG_SWITCH_US    (EXP4_COORD_CONFIG_SWITCH_US - SYNC_FRAME_US)
#define PERIOD_US           BRRS_SUPERFRAME_US
#define EXP4_SLOT_BUDGET_US (EXP4_COORD_CONFIG_SWITCH_US - SYNC_BUFFER_US)
_Static_assert(EXP4_SLOT_BUDGET_US > PHR_PSDU_US + SLOT_GUARD_US,
               "Exp4 DATA budget cannot fit even one guarded slot");
#define EXP4_TIMING_MAX_DATA_SLOTS \
    (1 + (EXP4_SLOT_BUDGET_US - PHR_PSDU_US - SLOT_GUARD_US) / \
         SLOT_INTERVAL_US)
#define EXP4_MAX_DATA_SLOTS \
    ((EXP4_TIMING_MAX_DATA_SLOTS < BRRS_MAX_DATA_SLOTS) ? \
     EXP4_TIMING_MAX_DATA_SLOTS : BRRS_MAX_DATA_SLOTS)
_Static_assert(EXP4_MAX_DATA_SLOTS >= 1,
               "Exp4 superframe cannot fit even one sensor slot");
#else
#define CONFIG_SWITCH_US    (SYNC_BUFFER_US + TOTAL_SLOTS * SLOT_INTERVAL_US + 2000)
#define PERIOD_US           BRRS_SUPERFRAME_US
_Static_assert(CONFIG_SWITCH_US < BRRS_SUPERFRAME_US,
               "DATA schedule does not fit in the common superframe");
#endif

/* Static startup estimate. The received beacon is authoritative at runtime. */
#define MY_SLOT_START_US    (SYNC_BUFFER_US + (MY_NODE_SEQ - 2) * SLOT_INTERVAL_US)

/* ========== [NEW] DW3000 timestamp 변환 ========== */
#define DWT_TIME_UNITS_PER_US  63898ULL
#define US_TO_DWT_TIME(us)     ((uint64_t)(us) * DWT_TIME_UNITS_PER_US)

/* dwt_setrxtimeout() 단위는 UUS(1.0256us). us->UUS ceil 변환. */
#define US_TO_UUS(us)          (((uint32_t)(us) * 10000UL + 10255UL) / 10256UL)

/* DWT 타이머 상수 */
#define CPU_FREQ_MHZ 64
#define CYCLES_PER_US  (CPU_FREQ_MHZ)

/* Default communication configuration */
static dwt_config_t config_data = {
    9, DATA_PLEN, DWT_PAC8,
    9, 9, DATA_SFD_TYPE,
    DWT_BR_6M8, DWT_PHRMODE_STD, DATA_PHR_RATE,
    (PREAMBLE_SYMBOLS + 1 + SFD_SYMBOLS - 8),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};

static uint16_t current_data_plen = DATA_PLEN;
static uint16_t current_data_preamble_symbols = PREAMBLE_SYMBOLS;

static bool brrs_data_plen_from_symbols(uint16_t symbols, uint16_t *plen)
{
    switch (symbols) {
    case 32U:
        *plen = DWT_PLEN_32;
        return true;
    case 64U:
        *plen = DWT_PLEN_64;
        return true;
    case 128U:
        *plen = DWT_PLEN_128;
        return true;
    case 256U:
        *plen = DWT_PLEN_256;
        return true;
    default:
        return false;
    }
}

static bool brrs_apply_beacon_data_phy(uint16_t symbols)
{
    uint16_t plen;

    if (!brrs_data_plen_from_symbols(symbols, &plen)) {
        return false;
    }

    config_data.txPreambLength = plen;
    config_data.sfdTO = (uint16_t)(symbols + 1U + SFD_SYMBOLS - 8U);
    current_data_plen = plen;
    current_data_preamble_symbols = symbols;
    return true;
}

static dwt_config_t config_sync = {
    9, SYNC_PLEN, DWT_PAC8,
    10, 10, 1,
    DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    (SYNC_PREAMBLE_SYMBOLS + 1 + 8 - 8),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};

#define NODE_INIT '1'
#define NODE_ALL  'B'

#define MSG_TYPE_SYNC       0x01
#define MSG_TYPE_DATA       0x02
#define MSG_TYPE_END        0x03

enum {
    IDX_FTYPE         = 0,
    IDX_SEQ           = 1,
    IDX_SOURCE        = BRRS_IDX_SOURCE,
    IDX_DEST          = BRRS_IDX_DEST,
    IDX_MSG_TYPE      = BRRS_IDX_MSG_TYPE,
    IDX_PROTOCOL_VERSION = BRRS_IDX_PROTOCOL_VERSION,
    IDX_SUPERFRAME_SEQ = BRRS_IDX_SUPERFRAME_SEQ,
    IDX_DATA_PAYLOAD  = BRRS_IDX_DATA_PAYLOAD
};

#define TX_POWER_INDEX_10dB    40
#define USE_TX_POWER_INDEX     TX_POWER_INDEX_10dB

static uint8_t tx_msg[PSDU_BYTES] = {
    [0] = 0x41,
    [1] = 0x8C,
    [BRRS_IDX_DATA_PAYLOAD] = 'D',
    [BRRS_IDX_DATA_PAYLOAD + 1] = 'W'
};

#define TX_TO_RX_DELAY_UUS  60
/* SYNC RX 타임아웃은 길게 (비컨 신뢰성), 그 외엔 짧게 동적 설정.
 * dwt_setrxtimeout(0)은 API 명세상 "타임아웃 비활성(무한 대기)"이지만,
 * SYNC 유실 감지(sync_lost 카운트)를 위해 명시적 유한값을 사용한다.
 * 10 ms superframe보다 충분히 크면 정상 동작에는 영향 없음.
 */
#define SYNC_RX_TIMEOUT_US  50000   /* 50ms - PERIOD보다 충분히 큼 */

static uint8_t rx_buffer[FRAME_LEN_MAX];
extern dwt_txconfig_t txconfig_options;

typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t rx_error_count;
} per_stats_t;

static per_stats_t per_stats[TOTAL_ARRAY_SIZE] = {0};
static uint32_t total_rx_errors = 0;
static uint32_t total_tx_attempts = 0;
static uint32_t total_tx_delayed_late = 0;
#if BRRS_EXPERIMENT == 4
static uint32_t exp4_sync_frames_received = 0;
static uint32_t exp4_sync_frames_missed = 0;
static uint32_t exp4_sync_duplicates = 0;
static uint32_t exp4_last_sync_seq = 0;
static bool exp4_end_received = false;
static uint32_t exp4_sync_rx_scheduled = 0;
static uint32_t exp4_sync_rx_delayed_late = 0;
typedef struct {
    int64_t min_ns;
    int64_t max_ns;
    int64_t sum_ns;
    uint32_t count;
} exp4_tx_slot_error_stats_t;
static exp4_tx_slot_error_stats_t exp4_tx_slot_error_stats = {
    INT64_MAX, INT64_MIN, 0, 0
};
#endif

#if BRRS_EXPERIMENT == 3
/* DWM3000EVB routes DW3000 GPIO5 to Arduino D1. GPIO5 is configured as
 * EXTTXE, which is high while the DW3000 is in TX mode. A GPIOTE event
 * triggers TIMER4 capture through PPI channel 19, so the timestamp itself
 * does not contain interrupt or software latency. */
#define EXP3_EXTTXE_CAPTURE_PIN ARDUINO_1_PIN
#define EXP3_CAPTURE_TIMER      NRF_TIMER4
#define EXP3_CAPTURE_PPI_CH     19U
#define EXP3_CAPTURE_PPI_MASK   (1UL << EXP3_CAPTURE_PPI_CH)
#define EXP3_TIMER_HZ           16000000UL
#define EXP3_TICK_X2_NS         125UL

static volatile bool exp3_capture_ready = false;
static volatile bool exp3_capture_armed = false;
static volatile bool exp3_rise_seen = false;
static volatile uint32_t exp3_rise_tick = 0;
static volatile uint32_t exp3_width_ticks[TARGET_CYCLES];
static volatile uint32_t exp3_capture_count = 0;
static volatile uint32_t exp3_invalid_edges = 0;
static volatile uint32_t exp3_incomplete_pulses = 0;
static bool exp3_tx_final_pass = false;

static uint32_t exp3_ticks_to_ns(uint32_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * EXP3_TICK_X2_NS + 1ULL) / 2ULL);
}

static void exp3_exttxe_edge_handler(nrf_drv_gpiote_pin_t pin,
                                     nrf_gpiote_polarity_t action)
{
    uint32_t captured_tick = EXP3_CAPTURE_TIMER->CC[0];
    bool pin_is_high = (nrf_gpio_pin_read(pin) != 0);
    (void)action;

    if (!exp3_capture_ready) {
        return;
    }

    if (pin_is_high) {
        if (exp3_capture_armed) {
            exp3_rise_tick = captured_tick;
            exp3_rise_seen = true;
        }
        return;
    }

    if (exp3_capture_armed && exp3_rise_seen) {
        uint32_t width_ticks = captured_tick - exp3_rise_tick;

        if (width_ticks > 0 && exp3_capture_count < TARGET_CYCLES) {
            exp3_width_ticks[exp3_capture_count++] = width_ticks;
        } else {
            exp3_invalid_edges++;
        }
        exp3_capture_armed = false;
        exp3_rise_seen = false;
    } else if (exp3_capture_armed) {
        exp3_invalid_edges++;
        exp3_capture_armed = false;
    }
}

static bool exp3_exttxe_capture_init(void)
{
    ret_code_t err_code;
    nrf_drv_gpiote_in_config_t input_config =
        GPIOTE_CONFIG_IN_SENSE_TOGGLE(true);

    input_config.pull = NRF_GPIO_PIN_PULLDOWN;
    err_code = nrf_drv_gpiote_in_init(EXP3_EXTTXE_CAPTURE_PIN,
                                      &input_config,
                                      exp3_exttxe_edge_handler);
    if (err_code != NRF_SUCCESS) {
        return false;
    }

    EXP3_CAPTURE_TIMER->TASKS_STOP = 1;
    EXP3_CAPTURE_TIMER->MODE = TIMER_MODE_MODE_Timer;
    EXP3_CAPTURE_TIMER->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    EXP3_CAPTURE_TIMER->PRESCALER = 0;
    EXP3_CAPTURE_TIMER->SHORTS = 0;
    EXP3_CAPTURE_TIMER->INTENCLR = 0xFFFFFFFFUL;
    EXP3_CAPTURE_TIMER->TASKS_CLEAR = 1;

    NRF_PPI->CHENCLR = EXP3_CAPTURE_PPI_MASK;
    NRF_PPI->CH[EXP3_CAPTURE_PPI_CH].EEP =
        nrf_drv_gpiote_in_event_addr_get(EXP3_EXTTXE_CAPTURE_PIN);
    NRF_PPI->CH[EXP3_CAPTURE_PPI_CH].TEP =
        (uint32_t)(uintptr_t)&EXP3_CAPTURE_TIMER->TASKS_CAPTURE[0];
    NRF_PPI->CHENSET = EXP3_CAPTURE_PPI_MASK;

    EXP3_CAPTURE_TIMER->TASKS_START = 1;
    exp3_capture_ready = true;
    nrf_drv_gpiote_in_event_enable(EXP3_EXTTXE_CAPTURE_PIN, true);
    return true;
}

static void exp3_exttxe_capture_arm(void)
{
    if (!exp3_capture_ready) {
        return;
    }
    if (exp3_capture_armed) {
        exp3_incomplete_pulses++;
    }
    exp3_rise_seen = false;
    exp3_capture_armed = true;
}

static void exp3_exttxe_capture_cancel(void)
{
    if (exp3_capture_armed && exp3_rise_seen) {
        exp3_incomplete_pulses++;
    }
    exp3_capture_armed = false;
    exp3_rise_seen = false;
}

static void exp3_tx_print_summary(void)
{
    uint32_t i;
    uint32_t count = exp3_capture_count;
    uint32_t min_ticks = UINT32_MAX;
    uint32_t max_ticks = 0;
    uint64_t sum_ticks = 0;
    uint32_t min_ns = 0;
    uint32_t max_ns = 0;
    uint32_t avg_ns = 0;
    static char line[300];

    if (exp3_capture_armed) {
        exp3_exttxe_capture_cancel();
    }

    for (i = 0; i < count; i++) {
        uint32_t ticks = exp3_width_ticks[i];
        if (ticks < min_ticks) min_ticks = ticks;
        if (ticks > max_ticks) max_ticks = ticks;
        sum_ticks += ticks;
    }

    if (count > 0) {
        min_ns = exp3_ticks_to_ns(min_ticks);
        max_ns = exp3_ticks_to_ns(max_ticks);
        avg_ns = (uint32_t)((sum_ticks * EXP3_TICK_X2_NS +
                             (uint64_t)count) / ((uint64_t)count * 2ULL));
    }

    exp3_tx_final_pass =
        (total_tx_attempts == TARGET_CYCLES &&
         per_stats[MY_NODE_SEQ - 1].tx_count == TARGET_CYCLES &&
         count == TARGET_CYCLES &&
         exp3_invalid_edges == 0 &&
         exp3_incomplete_pulses == 0);

    snprintf(line, sizeof(line),
             "EXP3_TX_MODEL_CSV,%s,%d,%s,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
             EXP3_VARIANT_NAME, SFD_SYMBOLS, EXP3_PHR_RATE_NAME, PSDU_BYTES,
             (unsigned long)PREAMBLE_MODEL_NS,
             (unsigned long)SFD_MODEL_NS,
             (unsigned long)PHR_MODEL_NS,
             (unsigned long)PSDU_MODEL_NS,
             (unsigned long)PSDU_RS_PARITY_BITS,
             (unsigned long)DP_MODEL_NS,
             (unsigned long)FRAME_MODEL_NS);
    final_log_info(line);

    snprintf(line, sizeof(line),
             "EXP3_TX_SUMMARY_CSV,%s,%d,%s,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
             EXP3_VARIANT_NAME, SFD_SYMBOLS, EXP3_PHR_RATE_NAME, PSDU_BYTES,
             (unsigned long)total_tx_attempts,
             (unsigned long)per_stats[MY_NODE_SEQ - 1].tx_count,
             (unsigned long)count,
             (unsigned long)(exp3_invalid_edges + exp3_incomplete_pulses),
             (unsigned long)min_ns,
             (unsigned long)max_ns,
             (unsigned long)avg_ns,
             (unsigned long)FRAME_MODEL_NS);
    final_log_info(line);

    snprintf(line, sizeof(line),
             "EXP3_TX_RESULT,variant=%s,attempts=%lu,success=%lu,captures=%lu,status=%s",
             EXP3_VARIANT_NAME,
             (unsigned long)total_tx_attempts,
             (unsigned long)per_stats[MY_NODE_SEQ - 1].tx_count,
             (unsigned long)count,
             exp3_tx_final_pass ? "PASS" : "FAIL");
    final_log_info(line);
}

static void exp3_tx_dump_samples(void)
{
    uint32_t i;
    static char line[160];

    snprintf(line, sizeof(line),
             "EXP3_TX_DUMP_START,variant=%s,expected=%d,count=%lu,status=%s",
             EXP3_VARIANT_NAME, TARGET_CYCLES,
             (unsigned long)exp3_capture_count,
             exp3_tx_final_pass ? "PASS" : "FAIL");
    final_log_info(line);
    exp_log_info("EXP3_TX_CSV_HEADER,seq,variant,sfd_symbols,phr_rate,psdu_bytes,ticks,width_ns");

    for (i = 0; i < exp3_capture_count; i++) {
        uint32_t ticks = exp3_width_ticks[i];
        snprintf(line, sizeof(line),
                 "EXP3_TX_CSV,%lu,%s,%d,%s,%d,%lu,%lu",
                 (unsigned long)(i + 1U),
                 EXP3_VARIANT_NAME, SFD_SYMBOLS, EXP3_PHR_RATE_NAME,
                 PSDU_BYTES, (unsigned long)ticks,
                 (unsigned long)exp3_ticks_to_ns(ticks));
        exp_log_info(line);
    }

    snprintf(line, sizeof(line),
             "EXP3_TX_DUMP_DONE,variant=%s,expected=%d,count=%lu,status=%s",
             EXP3_VARIANT_NAME, TARGET_CYCLES,
             (unsigned long)exp3_capture_count,
             exp3_tx_final_pass ? "PASS" : "FAIL");
    final_log_info(line);
}
#endif

#if ENABLE_CIR
static uint8_t cir_buf[DWT_CIR_LEN_MAX * 6];

static void print_cir_data(uint8_t *buf, int n_samples) {
    int i;
    test_run_info((unsigned char *)"\nCIR_START");
    for (i = 0; i < n_samples; i++) {
        int32_t real_val, imag_val;
        uint8_t lo_re, mid_re, hi_re, sign_re;
        uint8_t lo_im, mid_im, hi_im, sign_im;
        static char cir_line[40];

        lo_re  = buf[i * 6 + 0]; mid_re = buf[i * 6 + 1]; hi_re  = buf[i * 6 + 2];
        lo_im  = buf[i * 6 + 3]; mid_im = buf[i * 6 + 4]; hi_im  = buf[i * 6 + 5];

        sign_re = ((hi_re & 0x80) == 0x80) ? 0xFF : 0;
        sign_im = ((hi_im & 0x80) == 0x80) ? 0xFF : 0;

        real_val = (int32_t)((uint32_t)sign_re << 24 | (uint32_t)hi_re << 16 | (uint32_t)mid_re << 8 | lo_re);
        imag_val = (int32_t)((uint32_t)sign_im << 24 | (uint32_t)hi_im << 16 | (uint32_t)mid_im << 8 | lo_im);

        snprintf(cir_line, sizeof(cir_line), "%ld,%ld,", (long)real_val, (long)imag_val);
        test_run_info((unsigned char *)cir_line);
        Sleep(1);
    }
    test_run_info((unsigned char *)"CIR_END");
}
#endif

#define DWT_CTRL    (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t*)0xE0001004)
#ifndef DWT_CTRL_CYCCNTENA_Msk
#define DWT_CTRL_CYCCNTENA_Msk  (1UL << 0)
#endif
#define CPU_FREQ_HZ  64000000

static void dwt_timer_init(void) {
    if (!(DWT_CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
        DWT_CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

static uint32_t dwt_timer_get_cycles(void) {
    return DWT_CYCCNT;
}

static uint32_t us_to_cpu_cycles(uint32_t microseconds) {
    return microseconds * (CPU_FREQ_HZ / 1000000);
}

static bool dwt_timer_elapsed(uint32_t start_cycles, uint32_t duration_cycles) {
    return ((dwt_timer_get_cycles() - start_cycles) >= duration_cycles);
}

static uint32_t cycles_to_ms(uint32_t cycles) {
    return cycles / (CPU_FREQ_HZ / 1000);
}

#if BRRS_EXPERIMENT == 4
static uint16_t exp4_read_superframe_seq(const uint8_t *msg)
{
    return brrs_get_u16_le(&msg[IDX_SUPERFRAME_SEQ]);
}
#endif

static brrs_beacon_config_t current_beacon_config;
static uint32_t current_owned_slot_start_us[BRRS_MAX_DATA_SLOTS];
static uint8_t current_owned_slot_count = 0U;
static uint32_t beacon_config_errors = 0U;
static uint32_t data_config_errors = 0U;
static bool beacon_config_logged = false;

static uint32_t brrs_preamble_us_from_symbols(uint16_t symbols)
{
    return (uint32_t)(((uint64_t)symbols * IPATOV_SYMBOL_X100_NS +
                       99999ULL) / 100000ULL);
}

static const char *brrs_beacon_reject_reason(uint8_t message_type,
                                              const brrs_beacon_config_t *config)
{
    uint8_t schedule_bitmap = 0U;
    uint8_t slot;
    uint32_t expected_slot_interval_us;
    uint32_t schedule_end_us;
#if BRRS_EXPERIMENT == 4
    const uint32_t data_deadline_us = EXP4_COORD_CONFIG_SWITCH_US;
#else
    const uint32_t data_deadline_us = config->superframe_period_us;
#endif

    {
        uint16_t ignored_plen;
        if (!brrs_data_plen_from_symbols(config->data_preamble_symbols,
                                         &ignored_plen)) {
            return "unsupported_data_preamble";
        }
    }
    if (config->data_psdu_bytes != PSDU_BYTES) {
        return "data_psdu";
    }
    if (config->data_rate != (uint8_t)DWT_BR_6M8) {
        return "data_rate";
    }
    expected_slot_interval_us =
        brrs_preamble_us_from_symbols(config->data_preamble_symbols) +
        SFD_US + PHR_PSDU_US + SLOT_GUARD_US;
    if (config->slot_interval_us != expected_slot_interval_us) {
        return "slot_interval";
    }
    if (config->superframe_period_us == 0U) {
        return "period_zero";
    }
    if (config->first_slot_offset_us >= config->superframe_period_us) {
        return "first_slot_range";
    }
    if (message_type == MSG_TYPE_END) {
        return (config->slot_count == 0U &&
                config->active_node_bitmap == 0U) ?
               NULL : "end_schedule";
    }
    if (config->slot_count == 0U) {
        return "slot_count_zero";
    }

    for (slot = 0U; slot < config->slot_count; slot++) {
        uint8_t owner = config->slot_owner[slot];
        uint8_t owner_bit = brrs_node_bitmap_bit(owner);

        if (owner_bit == 0U ||
            (config->active_node_bitmap & owner_bit) == 0U) {
            return "slot_owner";
        }
        schedule_bitmap |= owner_bit;
    }

    if (schedule_bitmap != config->active_node_bitmap) {
        return "active_bitmap";
    }

    schedule_end_us = (uint32_t)config->first_slot_offset_us +
        (uint32_t)(config->slot_count - 1U) * config->slot_interval_us +
        PHR_PSDU_US + SLOT_GUARD_US;
    if (schedule_end_us > data_deadline_us) {
        return "schedule_range";
    }

    return NULL;
}

static void brrs_log_beacon_config(const brrs_beacon_config_t *config)
{
    static char line[320];
    char owners[BRRS_MAX_DATA_SLOTS + 1U];
    uint8_t slot;

    for (slot = 0U; slot < config->slot_count; slot++) {
        owners[slot] = (char)('0' + config->slot_owner[slot]);
    }
    owners[config->slot_count] = '\0';

    snprintf(line, sizeof(line),
             "BRRS_BEACON_RX_CSV,version=%u,seq=%u,m=%u,data_psdu=%u,data_rate=%u,active_bitmap=0x%02X,slot_count=%u,slot_owners=%s,first_slot_rmarker_us=%u,slot_interval_us=%u,period_us=%u",
             BRRS_PROTOCOL_VERSION, config->superframe_seq,
             config->data_preamble_symbols, config->data_psdu_bytes,
             config->data_rate, config->active_node_bitmap,
             config->slot_count, owners,
             config->first_slot_offset_us, config->slot_interval_us,
             config->superframe_period_us);
    test_run_info((unsigned char *)line);
}

static void brrs_load_owned_slots(const brrs_beacon_config_t *config)
{
    uint8_t slot;

    current_owned_slot_count = 0U;
    for (slot = 0U; slot < config->slot_count; slot++) {
        if (config->slot_owner[slot] == MY_NODE_SEQ) {
            uint32_t start_us = config->first_slot_offset_us +
                (uint32_t)slot * config->slot_interval_us;
            current_owned_slot_start_us[current_owned_slot_count++] = start_us;
        }
    }
}

/* ========== 프로토콜 상태 ========== */
static bool synchronized = false;
static uint32_t current_cycle = 0;
static bool final_stats_printed = false;

typedef struct {
    uint32_t total_timeouts;
} sync_loss_stats_t;

static sync_loss_stats_t sync_loss_stats = {0};
static bool sync_lost = false;


/* ========== [NEW] SYNC RX timestamp 기반 타이밍 ========== */
static uint32_t last_sync_rx_ts_high32 = 0;  /* SYNC RX timestamp */

/* ========== 유틸리티 함수 ========== */

static uint8_t node_id_to_index(uint8_t node_id) {
    if (node_id >= '1' && node_id <= '8') return (uint8_t)(node_id - '1');
    return 0xFF;
}

static uint8_t my_slot_idx(void) {
    return (uint8_t)(MY_NODE_SEQ - 1);
}

static const char* get_slot_description(uint8_t slot_idx) {
    static const char* names[] = {"INIT","N2","N3","N4","N5","N6","N7","N8"};
    if (slot_idx < 8) return names[slot_idx];
    return "???";
}

/* ========== [NEW] Delayed-TX 헬퍼 ==========
 * SYNC RX timestamp 기준으로 정확한 슬롯 시각에 송신.
 * 코디네이터의 delayed-RX 윈도우와 일치해야 함.
 */
static int schedule_delayed_tx(uint32_t sync_rx_ts_high32, uint32_t slot_offset_us, uint8_t flags)
{
    uint64_t offset_ticks = US_TO_DWT_TIME(slot_offset_us);
    uint32_t offset_high32 = (uint32_t)(offset_ticks >> 8);
    uint32_t tx_time_high32 = sync_rx_ts_high32 + offset_high32;

    dwt_setdelayedtrxtime(tx_time_high32);
    return dwt_starttx(DWT_START_TX_DELAYED | flags);
}

#if BRRS_EXPERIMENT == 4
static int64_t exp4_tx_slot_error_ns(uint32_t sync_rx_ts_high32,
                                     uint32_t tx_ts_high32,
                                     uint32_t scheduled_offset_us)
{
    uint32_t actual_offset_high32 = tx_ts_high32 - sync_rx_ts_high32;
    uint32_t scheduled_offset_high32 = (uint32_t)
        (US_TO_DWT_TIME(scheduled_offset_us) >> 8);
    int32_t error_high32 =
        (int32_t)(actual_offset_high32 - scheduled_offset_high32);
    int64_t numerator = (int64_t)error_high32 * 256000LL;

    if (numerator >= 0) {
        return (numerator + (int64_t)DWT_TIME_UNITS_PER_US / 2) /
               (int64_t)DWT_TIME_UNITS_PER_US;
    }
    return (numerator - (int64_t)DWT_TIME_UNITS_PER_US / 2) /
           (int64_t)DWT_TIME_UNITS_PER_US;
}

static void exp4_record_tx_slot_error(int64_t error_ns)
{
    if (error_ns < exp4_tx_slot_error_stats.min_ns) {
        exp4_tx_slot_error_stats.min_ns = error_ns;
    }
    if (error_ns > exp4_tx_slot_error_stats.max_ns) {
        exp4_tx_slot_error_stats.max_ns = error_ns;
    }
    exp4_tx_slot_error_stats.sum_ns += error_ns;
    exp4_tx_slot_error_stats.count++;
}

static bool exp4_schedule_next_sync_rx(void)
{
    uint32_t offset_us =
        current_beacon_config.superframe_period_us - SYNC_RX_EARLY_US;
    uint32_t offset_high32 =
        (uint32_t)(US_TO_DWT_TIME(offset_us) >> 8);
    uint32_t rx_time_high32 = last_sync_rx_ts_high32 + offset_high32;

    dwt_setdelayedtrxtime(rx_time_high32);
    dwt_setrxtimeout(US_TO_UUS(SYNC_RX_WINDOW_US));
    if (dwt_rxenable(DWT_START_RX_DELAYED | DWT_IDLE_ON_DLY_ERR) == DWT_SUCCESS) {
        exp4_sync_rx_scheduled++;
        return true;
    }

    exp4_sync_rx_delayed_late++;
    dwt_forcetrxoff();
    dwt_writesysstatuslo(0xFFFFFFFF);
    dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    return false;
}
#endif

/* ========================================================================
 * MAIN FUNCTION
 * ======================================================================== */
int brrs_normal(void)
{
    exp_log_init();
    exp_log_info("EXP_LOG_READY,channel=1");
    test_run_info((unsigned char *)APP_NAME);

    port_set_dw_ic_spi_fastrate();
    reset_DWIC();
    Sleep(2);

    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);
    while (!dwt_checkidlerc()) { };

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
        test_run_info((unsigned char *)"INIT FAILED");
        while (1) { };
    }

    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

    if (dwt_configure(&config_sync)) {
        test_run_info((unsigned char *)"CONFIG FAILED");
        while (1) { };
    }

#if BRRS_EXPERIMENT == 3
    /* EXTTXE requires the external-PA GPIO mode and coarse TX sequencing.
     * GPIO4 EXTPA is also selected by this API but is not connected to a PA
     * on the DWM3000EVB. */
    dwt_setfinegraintxseq(0);
    dwt_setlnapamode(DWT_PA_ENABLE);
    if (!exp3_exttxe_capture_init()) {
        test_run_info((unsigned char *)"EXP3 EXTTXE capture init FAILED");
    } else {
        test_run_info((unsigned char *)"EXP3 EXTTXE capture ready: GPIO5 -> Arduino D1, TIMER4 16MHz");
    }
#endif
#if BRRS_EXPERIMENT == 4
    {
        static char cfg_msg[320];
        snprintf(cfg_msg, sizeof(cfg_msg),
                 "EXP4_TX_BOOT_CSV,%s,seq=%d,data_plen_source=beacon,default_m=%d,psdu_bytes=%d,app_payload_bytes=%d,superframe_us=%d,sync_frame_us=%d,sync_rx_open_offset_us=%d,sync_rx_window_us=%d",
                 APP_NAME, MY_NODE_SEQ,
                 PREAMBLE_SYMBOLS, PSDU_BYTES, BRRS_APP_PAYLOAD_BYTES,
                 BRRS_SUPERFRAME_US,
                 SYNC_FRAME_US, BRRS_SUPERFRAME_US - SYNC_RX_EARLY_US,
                 SYNC_RX_WINDOW_US);
        final_log_info(cfg_msg);
        test_run_info((unsigned char *)
            "EXP4_TX_FIRMWARE_REV,rev=20,beacon_protocol=3,data_header_bytes=8,slot_identity=coordinator_rx_rmarker,data_phy=from_beacon,slot_owner_schedule=1,sync_rx=delayed_after_data,data_config=fail_closed,tx_slot_diag=actual_tx_rmarker,timing_metric=uwb_signed_slot_error");
    }
#endif

    power_indexes_t power_indexes = {0};
    tx_adj_res_t linear_results = {0};
    dwt_txconfig_t linear_txconfig;

    power_indexes.input[0] = USE_TX_POWER_INDEX;
    power_indexes.input[1] = USE_TX_POWER_INDEX;
    power_indexes.input[2] = USE_TX_POWER_INDEX;
    power_indexes.input[3] = USE_TX_POWER_INDEX;

    if (dwt_calculate_linear_tx_setting((int)config_sync.chan, &power_indexes, &linear_results) == DWT_SUCCESS) {
        linear_txconfig.power = linear_results.tx_frame_cfg.tx_power_setting;
        linear_txconfig.PGcount = txconfig_options.PGcount;
        linear_txconfig.PGdly = txconfig_options.PGdly;
        dwt_configuretxrf(&linear_txconfig);
        dwt_set_pll_config(linear_results.tx_frame_cfg.pll_cfg);
    } else {
        dwt_configuretxrf(&txconfig_options);
    }

    dwt_setrxaftertxdelay(TX_TO_RX_DELAY_UUS);
    /* 초기/복구 SYNC 대기는 50 ms 유한 timeout 뒤 즉시 다시 연다. */
    dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));

    dwt_setinterrupt(0, 0, DWT_ENABLE_INT);
    dwt_writesysstatuslo(0xFFFFFFFF);

    {
        static char cfg_msg[240];
        snprintf(cfg_msg, sizeof(cfg_msg),
                 "%s: EXP=%d SEQ=%d SLOT_START=beacon DATA_PLEN=beacon(default=%d/%dsym) SUPERFRAME=%dus PERIODS=%d TARGET=%d CIR=%d",
                 APP_NAME, BRRS_EXPERIMENT, MY_NODE_SEQ, DATA_PLEN,
                 PREAMBLE_SYMBOLS, PERIOD_US, PERIODS_PER_CYCLE,
                 TARGET_CYCLES, ENABLE_CIR);
        test_run_info((unsigned char *)cfg_msg);
    }
#if BRRS_EXPERIMENT == 3
    {
        static char cfg_msg[260];
        snprintf(cfg_msg, sizeof(cfg_msg),
                 "EXP3_TX_CONFIG_CSV,%s,%d,%s,%d,%lu,%lu,%lu,%lu,%lu,pin=ARDUINO_D1,timer_hz=%lu",
                 EXP3_VARIANT_NAME, SFD_SYMBOLS, EXP3_PHR_RATE_NAME, PSDU_BYTES,
                 (unsigned long)PREAMBLE_MODEL_NS,
                 (unsigned long)SFD_MODEL_NS,
                 (unsigned long)PHR_MODEL_NS,
                 (unsigned long)PSDU_MODEL_NS,
                 (unsigned long)FRAME_MODEL_NS,
                 (unsigned long)EXP3_TIMER_HZ);
        final_log_info(cfg_msg);
    }
#endif

    dwt_timer_init();

    uint32_t last_sync_cycles = 0;
    uint32_t config_switch_cycles = us_to_cpu_cycles(CONFIG_SWITCH_US);
    uint32_t sync_timeout_cycles = us_to_cpu_cycles(27000);
#if BRRS_EXPERIMENT == 4
    uint32_t final_timeout_cycles = us_to_cpu_cycles(100000);
#else
    uint32_t final_timeout_cycles = us_to_cpu_cycles(5000000);
#endif
    bool slot_tx_done = false;       /* 이번 SYNC 사이클에서 TX 끝났는지 */
    bool config_is_sync = true;

    /* [DEBUG] 초기 RX 활성화 직전 상태 확인 */
    test_run_info((unsigned char *)"DBG: enabling initial RX...");
    dwt_writesysstatuslo(0xFFFFFFFF);
    int rx_result = dwt_rxenable(DWT_START_RX_IMMEDIATE);
    if (rx_result != DWT_SUCCESS) {
        test_run_info((unsigned char *)"DBG: initial dwt_rxenable FAILED!");
    } else {
        test_run_info((unsigned char *)"DBG: initial RX enabled, waiting for SYNC...");
    }

    /* [DEBUG] 주기적 status 로그용 */
    uint32_t last_debug_cycles = dwt_timer_get_cycles();
    uint32_t debug_interval_cycles = us_to_cpu_cycles(2000000);  /* 2초마다 */
    uint32_t debug_count = 0;

    while (1)
    {
        /* [DEBUG] 2초마다 상태 출력 (SYNC 못 받는 경우 진단용) */
        if (dwt_timer_elapsed(last_debug_cycles, debug_interval_cycles)) {
            last_debug_cycles = dwt_timer_get_cycles();
            debug_count++;
            if (!synchronized) {
                uint32_t sys_status = dwt_readsysstatuslo();
                static char dbg[120];
                snprintf(dbg, sizeof(dbg),
                         "DBG[%lu]: no SYNC yet. status=0x%08lX config_is_sync=%d",
                         (unsigned long)debug_count,
                         (unsigned long)sys_status,
                         config_is_sync);
                test_run_info((unsigned char *)dbg);

                /* RX가 꺼져있을 수도 있으니 강제 재활성화 */
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);
                if (!config_is_sync) {
                    if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                        config_is_sync = true;
                    }
                }
                dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
                test_run_info((unsigned char *)"DBG: forced RX re-enable");
            }
        }

        /* ========== [A] SYNC Loss Detection ========== */
        if (last_sync_cycles != 0 && !sync_lost
#if BRRS_EXPERIMENT == 4
            && current_cycle < TARGET_CYCLES && !exp4_end_received
#endif
        ) {
            if (dwt_timer_elapsed(last_sync_cycles, sync_timeout_cycles)) {
                sync_lost = true;
                sync_loss_stats.total_timeouts++;
                /* SYNC config로 돌아가 RX 재오픈 */
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    config_is_sync = true;
                }
                dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
        }

        /* ========== [B] Final Statistics ========== */
        if (last_sync_cycles != 0 && !final_stats_printed) {
            bool by_timeout = dwt_timer_elapsed(last_sync_cycles, final_timeout_cycles);
#if BRRS_EXPERIMENT == 4
            bool by_cycle   = exp4_end_received;
#else
            bool by_cycle   =
                (current_cycle >= TARGET_CYCLES &&
                 total_tx_attempts >= TARGET_CYCLES);
#endif

            if (by_timeout || by_cycle) {
                final_stats_printed = true;

                static char hdr[80];
                snprintf(hdr, sizeof(hdr), "\n===== %s FINAL STATS (PLEN=%d, %dsym) =====",
                         APP_NAME, current_data_plen,
                         current_data_preamble_symbols);
                final_log_info(hdr);

#if BRRS_EXPERIMENT == 4
                {
                    uint32_t beacon_missed =
                        (TARGET_CYCLES >= exp4_sync_frames_received) ?
                        (TARGET_CYCLES - exp4_sync_frames_received) : 0;
                    bool schedule_pass =
                        (per_stats[my_slot_idx()].tx_count == total_tx_attempts &&
                         total_tx_delayed_late == 0 &&
                         exp4_sync_rx_delayed_late == 0 &&
                         exp4_sync_duplicates == 0 &&
                         data_config_errors == 0);
                    bool collection_pass = exp4_end_received && schedule_pass;
                    bool beacon_pass = (beacon_missed == 0);
                    static char s[240];
                    snprintf(s, sizeof(s),
                             "My TX: success=%lu attempts=%lu delayed_late=%lu",
                             (unsigned long)per_stats[my_slot_idx()].tx_count,
                             (unsigned long)total_tx_attempts,
                             (unsigned long)total_tx_delayed_late);
                    final_log_info(s);
                    if (exp4_tx_slot_error_stats.count > 0U) {
                        int64_t avg_ns = exp4_tx_slot_error_stats.sum_ns /
                                         exp4_tx_slot_error_stats.count;
                        snprintf(s, sizeof(s),
                                 "EXP4_TX_SLOT_TIMING_CSV,node=N%u,n=%lu,min_error_ns=%lld,max_error_ns=%lld,avg_error_ns=%lld,reference=sync_rx_rmarker,observation=data_tx_rmarker",
                                 MY_NODE_SEQ,
                                 (unsigned long)exp4_tx_slot_error_stats.count,
                                 (long long)exp4_tx_slot_error_stats.min_ns,
                                 (long long)exp4_tx_slot_error_stats.max_ns,
                                 (long long)avg_ns);
                        final_log_info(s);
                    }
                    snprintf(s, sizeof(s),
                             "EXP4_TX_SYNC_RX_CSV,scheduled=%lu,delayed_late=%lu,early_us=%d,window_us=%d",
                             (unsigned long)exp4_sync_rx_scheduled,
                             (unsigned long)exp4_sync_rx_delayed_late,
                             SYNC_RX_EARLY_US, SYNC_RX_WINDOW_US);
                    final_log_info(s);
                    snprintf(s, sizeof(s),
                             "Beacon RX: received=%lu expected=%d miss=%lu gaps=%lu duplicates=%lu end=%d",
                             (unsigned long)exp4_sync_frames_received,
                             TARGET_CYCLES,
                             (unsigned long)beacon_missed,
                             (unsigned long)exp4_sync_frames_missed,
                             (unsigned long)exp4_sync_duplicates,
                             exp4_end_received ? 1 : 0);
                    final_log_info(s);
                    snprintf(s, sizeof(s),
                             "EXP4_TX_RESULT_CSV,%s,%d,%d,%lu,%lu,%lu,%lu,%lu,%d,%s,%s",
                             APP_NAME, MY_NODE_SEQ, current_data_preamble_symbols,
                             (unsigned long)exp4_sync_frames_received,
                             (unsigned long)beacon_missed,
                             (unsigned long)total_tx_attempts,
                             (unsigned long)per_stats[my_slot_idx()].tx_count,
                             (unsigned long)total_tx_delayed_late,
                             exp4_end_received ? 1 : 0,
                             schedule_pass ? "PASS" : "FAIL",
                             beacon_pass ? "PASS" : "LOSS");
                    final_log_info(s);
                    snprintf(s, sizeof(s),
                             "EXP4_TX_DONE,node=%s,plen=%d,beacons=%lu/%d,attempts=%lu,success=%lu,end=%d,schedule=%s,beacon=%s,status=%s",
                             APP_NAME, current_data_preamble_symbols,
                             (unsigned long)exp4_sync_frames_received,
                             TARGET_CYCLES,
                             (unsigned long)total_tx_attempts,
                             (unsigned long)per_stats[my_slot_idx()].tx_count,
                             exp4_end_received ? 1 : 0,
                             schedule_pass ? "PASS" : "FAIL",
                             beacon_pass ? "PASS" : "LOSS",
                             collection_pass ? "PASS" : "FAIL");
                    final_log_info(s);
                }
#endif

#if BRRS_EXPERIMENT == 1 || BRRS_EXPERIMENT == 2 || BRRS_EXPERIMENT == 3
                {
                    static char s[130];
                    snprintf(s, sizeof(s), "My TX: success=%lu attempts=%lu delayed_late=%lu (PER -> INIT)",
                             (unsigned long)per_stats[my_slot_idx()].tx_count,
                             (unsigned long)total_tx_attempts,
                             (unsigned long)total_tx_delayed_late);
                    final_log_info(s);
                }
#endif

                {
                    static char s[140];
                    snprintf(s, sizeof(s),
                             "SYNC loss: %lu timeouts  RX errors=%lu  beacon_config_errors=%lu  data_config_errors=%lu",
                             (unsigned long)sync_loss_stats.total_timeouts,
                             (unsigned long)total_rx_errors,
                             (unsigned long)beacon_config_errors,
                             (unsigned long)data_config_errors);
                    final_log_info(s);
                }

#if BRRS_EXPERIMENT == 3
                final_log_info("--- Experiment 3 TX EXTTXE airtime capture ---");
                exp3_tx_print_summary();
#endif
                final_log_info("===== END STATS =====\n");
                dwt_forcetrxoff();
#if BRRS_EXPERIMENT == 3
                exp3_tx_dump_samples();
#endif
                break;
            }
        }

        /* ========== [C] Config Switch (DATA→SYNC 준비) ==========
         * SYNC TX 시점이 다가오면 SYNC config로 돌아가서 SYNC RX 준비
         */
#if BRRS_EXPERIMENT != 4
        if (last_sync_cycles != 0 && !config_is_sync && slot_tx_done) {
            if (dwt_timer_elapsed(last_sync_cycles, config_switch_cycles)) {
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    config_is_sync = true;
                }
                dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
            }
        }
#endif

        /* ========== [D] RX 폴링 ========== */
        {
            uint32_t status_reg = dwt_readsysstatuslo();

            if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
                uint16_t rx_frame_len = dwt_getframelength(0);
                memset(rx_buffer, 0, sizeof(rx_buffer));
                if (rx_frame_len > 0U && rx_frame_len <= FRAME_LEN_MAX) {
                    dwt_readrxdata(rx_buffer, rx_frame_len, 0);
                }
                dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

                uint8_t msg_type = rx_buffer[IDX_MSG_TYPE];
                uint8_t src_node = rx_buffer[IDX_SOURCE];
                uint8_t dest_node = rx_buffer[IDX_DEST];
                bool is_control_frame =
                    (msg_type == MSG_TYPE_SYNC || msg_type == MSG_TYPE_END);

                if (is_control_frame) {
                    brrs_beacon_config_t decoded_config = {0};
                    const char *reject_reason = NULL;
                    bool decoded = false;
                    bool valid_beacon;

                    if (rx_frame_len != BRRS_BEACON_PSDU_BYTES) {
                        reject_reason = "frame_length";
                    } else if (src_node != NODE_INIT || dest_node != NODE_ALL) {
                        reject_reason = "header";
                    } else if (!brrs_decode_beacon(rx_buffer, &decoded_config)) {
                        reject_reason = "decode";
                    } else {
                        decoded = true;
                        reject_reason = brrs_beacon_reject_reason(
                            msg_type, &decoded_config);
                    }
                    valid_beacon = (reject_reason == NULL);

                    if (!valid_beacon) {
                        static char error_line[480];
                        char owners[BRRS_MAX_DATA_SLOTS + 1U] = {0};
                        uint8_t slot;

                        beacon_config_errors++;
                        if (decoded) {
                            for (slot = 0U; slot < decoded_config.slot_count; slot++) {
                                owners[slot] = (char)('0' + decoded_config.slot_owner[slot]);
                            }
                        }
                        snprintf(error_line, sizeof(error_line),
                                 "BRRS_BEACON_REJECT,count=%lu,reason=%s,frame_len=%u,expected_frame_len=%u,version=%u,m=%u,build_default_m=%u,psdu=%u,expected_psdu=%u,rate=%u,expected_rate=%u,active_bitmap=0x%02X,slot_count=%u,slot_owners=%s,first_slot_us=%u,slot_interval_us=%u,period_us=%u",
                                 (unsigned long)beacon_config_errors,
                                 reject_reason,
                                 rx_frame_len,
                                 BRRS_BEACON_PSDU_BYTES,
                                 rx_buffer[IDX_PROTOCOL_VERSION],
                                 brrs_get_u16_le(&rx_buffer[BRRS_BEACON_IDX_PREAMBLE_SYMBOLS]),
                                 PREAMBLE_SYMBOLS,
                                 brrs_get_u16_le(&rx_buffer[BRRS_BEACON_IDX_PSDU_BYTES]),
                                 PSDU_BYTES,
                                 rx_buffer[BRRS_BEACON_IDX_DATA_RATE],
                                 (unsigned int)DWT_BR_6M8,
                                 decoded_config.active_node_bitmap,
                                 decoded_config.slot_count,
                                 owners,
                                 decoded_config.first_slot_offset_us,
                                 decoded_config.slot_interval_us,
                                 decoded_config.superframe_period_us);
                        test_run_info((unsigned char *)error_line);
                        dwt_forcetrxoff();
                        if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                            config_is_sync = true;
                        }
                        dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));
                        dwt_writesysstatuslo(0xFFFFFFFF);
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);
                        continue;
                    }

                    if (!brrs_apply_beacon_data_phy(
                            decoded_config.data_preamble_symbols)) {
                        static char phy_error_line[160];
                        beacon_config_errors++;
                        snprintf(phy_error_line, sizeof(phy_error_line),
                                 "BRRS_BEACON_REJECT,count=%lu,reason=apply_data_phy,m=%u,node=%u",
                                 (unsigned long)beacon_config_errors,
                                 decoded_config.data_preamble_symbols,
                                 MY_NODE_SEQ);
                        test_run_info((unsigned char *)phy_error_line);
                        dwt_forcetrxoff();
                        if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                            config_is_sync = true;
                        }
                        dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));
                        dwt_writesysstatuslo(0xFFFFFFFF);
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);
                        continue;
                    }
                    current_beacon_config = decoded_config;
                    if (!beacon_config_logged) {
                        brrs_log_beacon_config(&current_beacon_config);
                        {
                            static char phy_line[180];
                            snprintf(phy_line, sizeof(phy_line),
                                     "BRRS_DATA_PHY_APPLIED_CSV,experiment=%d,source=beacon,m=%u,plen_code=%u,sfd_to=%u,node=%u",
                                     BRRS_EXPERIMENT,
                                     current_data_preamble_symbols,
                                     current_data_plen,
                                     config_data.sfdTO,
                                     MY_NODE_SEQ);
                            test_run_info((unsigned char *)phy_line);
                        }
                        beacon_config_logged = true;
                    }
                    brrs_load_owned_slots(&current_beacon_config);
                }

                if (!is_control_frame) {
                    dwt_forcetrxoff();
                    if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                        config_is_sync = true;
                    }
                    dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));
                    dwt_writesysstatuslo(0xFFFFFFFF);
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                    continue;
                }

#if BRRS_EXPERIMENT == 4
                if (msg_type == MSG_TYPE_END && src_node == NODE_INIT) {
                    exp4_end_received = true;
                    synchronized = true;
                    last_sync_cycles = dwt_timer_get_cycles();
                    dwt_forcetrxoff();
                    dwt_writesysstatuslo(0xFFFFFFFF);
                    continue;
                }
#endif

                /* [D-1] SYNC 수신 - 핵심 타이밍 기준 */
                if (msg_type == MSG_TYPE_SYNC) {
                    uint32_t current_cycles = dwt_timer_get_cycles();
                    last_sync_cycles = current_cycles;

                    /* [NEW] DW3000 RX timestamp 획득 - delayed-TX 기준 */
                    last_sync_rx_ts_high32 = dwt_readrxtimestamphi32();

                    slot_tx_done = false;

                    if (sync_lost) {
                        sync_lost = false;
                        test_run_info((unsigned char *)"SYNC recovered");
                    }

#if BRRS_EXPERIMENT == 4
                    {
                        uint16_t superframe_seq = exp4_read_superframe_seq(rx_buffer);
                        bool new_superframe = true;

                        if (superframe_seq == 0 || superframe_seq > TARGET_CYCLES) {
                            new_superframe = false;
                        } else if (exp4_last_sync_seq != 0) {
                            if (superframe_seq <= exp4_last_sync_seq) {
                                exp4_sync_duplicates++;
                                new_superframe = false;
                            } else if (superframe_seq > exp4_last_sync_seq + 1U) {
                                exp4_sync_frames_missed +=
                                    superframe_seq - exp4_last_sync_seq - 1U;
                            }
                        } else if (superframe_seq > 1U) {
                            exp4_sync_frames_missed += superframe_seq - 1U;
                        }

                        if (!new_superframe) {
                            dwt_forcetrxoff();
                            if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                                config_is_sync = true;
                            }
                            dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));
                            dwt_writesysstatuslo(0xFFFFFFFF);
                            dwt_rxenable(DWT_START_RX_IMMEDIATE);
                            continue;
                        }

                        exp4_last_sync_seq = superframe_seq;
                        exp4_sync_frames_received++;
                        current_cycle = superframe_seq;
                    }
#else
                    current_cycle = current_beacon_config.superframe_seq;
#endif

                    /* ===== [NEW] DATA config로 전환 후 delayed-TX 예약 ===== */
                    dwt_forcetrxoff();
                    dwt_writesysstatuslo(0xFFFFFFFF);

                    if (dwt_configure(&config_data) == DWT_SUCCESS) {
                        config_is_sync = false;
                    } else {
                        static char config_error_line[112];
                        data_config_errors++;
                        snprintf(config_error_line, sizeof(config_error_line),
                                 "BRRS_DATA_CONFIG_ERROR,role=NORMAL,node=%u,experiment=%d,superframe=%lu,count=%lu",
                                 MY_NODE_SEQ, BRRS_EXPERIMENT,
                                 (unsigned long)current_cycle,
                                 (unsigned long)data_config_errors);
                        test_run_info((unsigned char *)config_error_line);

                        dwt_forcetrxoff();
                        if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                            config_is_sync = true;
                        }
                        dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));
                        dwt_writesysstatuslo(0xFFFFFFFF);
                        dwt_rxenable(DWT_START_RX_IMMEDIATE);
                        continue;
                    }

                    /* 한 비컨에 같은 물리 노드의 슬롯이 여러 개 있을 수 있다. */
                    if (current_owned_slot_count > 0U &&
                        current_cycle <= TARGET_CYCLES) {
                        uint8_t owned_slot;

                        for (owned_slot = 0U;
                             owned_slot < current_owned_slot_count;
                             owned_slot++) {
                            uint32_t slot_start_us =
                                current_owned_slot_start_us[owned_slot];
                            int tx_result;

                            tx_msg[0] = 0xC5;
                            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
                            tx_msg[IDX_SOURCE] = MY_NODE_ID;
#if BRRS_EXPERIMENT == 4
                            tx_msg[IDX_DEST] = NODE_INIT;
#else
                            tx_msg[IDX_DEST] = NODE_ALL;
#endif
                            tx_msg[IDX_PROTOCOL_VERSION] = BRRS_PROTOCOL_VERSION;
                            brrs_put_u16_le(&tx_msg[IDX_SUPERFRAME_SEQ],
                                            current_beacon_config.superframe_seq);

                            total_tx_attempts++;
                            dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
                            dwt_writetxfctrl(sizeof(tx_msg), 0, 0);

#if BRRS_EXPERIMENT == 3
                            exp3_exttxe_capture_arm();
#endif
                            tx_result = schedule_delayed_tx(
                                last_sync_rx_ts_high32,
                                slot_start_us,
                                0);

                            if (tx_result == DWT_SUCCESS) {
                                uint32_t tx_status = 0;
                                waitforsysstatus(&tx_status, NULL,
                                                 DWT_INT_TXFRS_BIT_MASK, 0);
                                if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
#if BRRS_EXPERIMENT == 4
                                    uint32_t actual_tx_ts_high32 =
                                        dwt_readtxtimestamphi32();
#endif
                                    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
                                    per_stats[my_slot_idx()].tx_count++;
#if BRRS_EXPERIMENT == 4
                                    exp4_record_tx_slot_error(
                                        exp4_tx_slot_error_ns(
                                            last_sync_rx_ts_high32,
                                            actual_tx_ts_high32,
                                            slot_start_us));
#endif
                                }
                            } else {
#if BRRS_EXPERIMENT == 3
                                exp3_exttxe_capture_cancel();
#endif
                                total_tx_delayed_late++;
                                dwt_forcetrxoff();
                            }
                        }
                    }

                    slot_tx_done = true;

                    /* TX 끝났으면 DATA RX 윈도우 오픈 (다른 노드 메시지 수신용) */
                    dwt_forcetrxoff();
                    dwt_writesysstatuslo(0xFFFFFFFF);

#if BRRS_EXPERIMENT == 4
                    if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                        config_is_sync = true;
                        exp4_schedule_next_sync_rx();
                    } else {
                        exp4_sync_rx_delayed_late++;
                    }
#endif

#if BRRS_EXPERIMENT == 1 || BRRS_EXPERIMENT == 2 || BRRS_EXPERIMENT == 3
                    /* 실험 1/2: Normal은 RX 안 함 (TX 전용)
                     * 다음 SYNC를 위해 config switch 타이밍에 SYNC config로 복귀
                     */
#endif

                    synchronized = true;
                    continue;
                }

            }

            /* RX timeout */
            else if (status_reg & SYS_STATUS_ALL_RX_TO) {
                dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO);
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);

                /* SYNC config 상태면 다시 SYNC 대기, DATA config면 그냥 두고 다음 cycle 기다림 */
                if (config_is_sync) {
                    dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                }
            }

            /* RX errors */
            else if (status_reg & SYS_STATUS_ALL_RX_ERR) {
                total_rx_errors++;
                dwt_writesysstatuslo(SYS_STATUS_ALL_RX_ERR);
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);

                if (config_is_sync) {
                    dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
                }
            }
        }

    } /* end while */

    return 0;
}

#endif /* TEST_BRRS_NORMAL */
