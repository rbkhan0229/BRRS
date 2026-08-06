/*! ----------------------------------------------------------------------------
 *  @file    pac4_control_normal.c
 *  @brief   Conventional PAC4 control - Normal Node
 *
 *           PAC4 immediate-RX 대조군을 위한 송신 노드다.
 *           BRRS 조건과 동일한 프레임 및 RF 도착 시각을 만들기 위해
 *           SYNC timestamp 기반 delayed-TX는 통제 변수로 유지한다.
 */

#include "deca_probe_interface.h"
#include <deca_device_api.h>
#include <deca_spi.h>
#include <example_selection.h>
#include <port.h>
#include <shared_defines.h>
#include <shared_functions.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "nrf_delay.h"
#include "nrf.h"

#if defined(TEST_PAC4_CONTROL_NORMAL)

extern void test_run_info(unsigned char *data);
extern int SEGGER_RTT_ConfigUpBuffer(unsigned BufferIndex, const char* sName, void* pBuffer, unsigned BufferSize, unsigned Flags);
extern unsigned SEGGER_RTT_WriteString(unsigned BufferIndex, const char* s);

#define EXP_LOG_RTT_CHANNEL     1
#define EXP_LOG_RTT_BUFFER_SIZE 8192
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
#define TEST_NODE_2
//#define TEST_NODE_3
//#define TEST_NODE_4
//#define TEST_NODE_5
//#define TEST_NODE_6
//#define TEST_NODE_7
//#define TEST_NODE_8

#ifdef TEST_NODE_2
    #define APP_NAME "PAC4 CONTROL NODE 2 v1.0 (matched delayed-TX)"
    #define MY_NODE_ID  '2'
    #define MY_NODE_SEQ 2
#elif defined(TEST_NODE_3)
    #define APP_NAME "PAC4 CONTROL NODE 3 v1.0 (matched delayed-TX)"
    #define MY_NODE_ID  '3'
    #define MY_NODE_SEQ 3
#elif defined(TEST_NODE_4)
    #define APP_NAME "PAC4 CONTROL NODE 4 v1.0 (matched delayed-TX)"
    #define MY_NODE_ID  '4'
    #define MY_NODE_SEQ 4
#elif defined(TEST_NODE_5)
    #define APP_NAME "PAC4 CONTROL NODE 5 v1.0 (matched delayed-TX)"
    #define MY_NODE_ID  '5'
    #define MY_NODE_SEQ 5
#elif defined(TEST_NODE_6)
    #define APP_NAME "PAC4 CONTROL NODE 6 v1.0 (matched delayed-TX)"
    #define MY_NODE_ID  '6'
    #define MY_NODE_SEQ 6
#elif defined(TEST_NODE_7)
    #define APP_NAME "PAC4 CONTROL NODE 7 v1.0 (matched delayed-TX)"
    #define MY_NODE_ID  '7'
    #define MY_NODE_SEQ 7
#elif defined(TEST_NODE_8)
    #define APP_NAME "PAC4 CONTROL NODE 8 v1.0 (matched delayed-TX)"
    #define MY_NODE_ID  '8'
    #define MY_NODE_SEQ 8
#else
    #error "Please select a node type (TEST_NODE_2 ~ TEST_NODE_8)"
#endif

#define CONTROL_EXPERIMENT  1

#define DATA_PLEN       DWT_PLEN_32
#define SYNC_PLEN       DWT_PLEN_512
#define ENABLE_CIR      0
#define TARGET_CYCLES   1000

#if CONTROL_EXPERIMENT == 1 || CONTROL_EXPERIMENT == 2
#define TOTAL_NODES         2
#define TOTAL_SLOTS         2
#define TOTAL_ARRAY_SIZE    2
#elif CONTROL_EXPERIMENT == 4
#define TOTAL_NODES         3
#define TOTAL_SLOTS         3
#define TOTAL_ARRAY_SIZE    3
#endif
#define PERIODS_PER_CYCLE   6

#if MY_NODE_SEQ > TOTAL_NODES
#error "선택한 노드 번호가 TOTAL_NODES 초과. 실험 1/2는 TEST_NODE_2만 사용 가능."
#endif

#define SLOT_GUARD_US       500
#define SYNC_BUFFER_US      3000
#define PSDU_BYTES          127
/* PRF64 preamble/SFD 심볼 길이 = 1017.63ns. 올림(ceil) 처리.
 * 주의: brrs_init.c와 반드시 동일해야 함 (SLOT_INTERVAL 일치). */
#define SFD_SYMBOLS         8
#define SFD_US              (((SFD_SYMBOLS) * 10177 + 9999) / 10000)   /* 8sym -> 9us (ceil) */
#define PHR_US              25
#define DATA_RATE_KBPS      6800
#define PSDU_US             (((PSDU_BYTES) * 8 * 1000 + DATA_RATE_KBPS - 1) / DATA_RATE_KBPS)
#define PHR_PSDU_US         (PHR_US + PSDU_US)
#define PREAMBLE_SYMBOLS    ((DATA_PLEN + 1) * 8)
#define PREAMBLE_US         (((PREAMBLE_SYMBOLS) * 10177 + 9999) / 10000)  /* ceil(sym * 1.0177us) */
#define SLOT_INTERVAL_US    (PREAMBLE_US + SFD_US + PHR_PSDU_US + SLOT_GUARD_US)
#define CONFIG_SWITCH_US    (SYNC_BUFFER_US + TOTAL_SLOTS * SLOT_INTERVAL_US + 2000)
#define PERIOD_US           (CONFIG_SWITCH_US + 4000)

/* 자기 슬롯 시작 시간 (SYNC 종료로부터의 오프셋) */
#define MY_SLOT_START_US    (SYNC_BUFFER_US + (MY_NODE_SEQ - 1) * SLOT_INTERVAL_US)

/* ========== [NEW] DW3000 timestamp 변환 ========== */
#define DWT_TIME_UNITS_PER_US  63898ULL
#define US_TO_DWT_TIME(us)     ((uint64_t)(us) * DWT_TIME_UNITS_PER_US)

/* dwt_setrxtimeout() 단위는 UUS(1.0256us). us->UUS ceil 변환. */
#define US_TO_UUS(us)          (((uint32_t)(us) * 10000UL + 10255UL) / 10256UL)

/* ========== [NEW] ACK RX 윈도우 (실험 4) ==========
 * INIT/다른 노드가 전송하는 ACK_ARRAY를 받는 윈도우.
 * 다중 노드 확장 실험에서만 사용한다.
 */
#define ACK_RX_WINDOW_US    (PREAMBLE_US + SFD_US + PHR_PSDU_US + 100)

/* DWT 타이머 상수 */
#define CPU_FREQ_MHZ 64
#define CYCLES_PER_US  (CPU_FREQ_MHZ)

/* Default communication configuration */
static dwt_config_t config_data = {
    9, DATA_PLEN, DWT_PAC4,
    9, 9, 1,
    DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    (PREAMBLE_SYMBOLS + 1 + 8 - 4),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};

static dwt_config_t config_sync = {
    9, SYNC_PLEN, DWT_PAC8,
    10, 10, 1,
    DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    (512 + 1 + 8 - 8),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};

#define NODE_INIT '1'
#define NODE_ALL  'B'

#define MSG_TYPE_SYNC       0x01
#define MSG_TYPE_DATA       0x02
#define MSG_TYPE_ACK_ARRAY  0x07

enum {
    IDX_FTYPE         = 0,
    IDX_SEQ           = 1,
    IDX_SOURCE        = 2,
    IDX_DEST          = 3,
    IDX_MSG_TYPE      = 4,
    IDX_PRIORITY      = 5,
    IDX_PERIOD_INFO   = 6,
    IDX_TX_TIMESTAMP  = 8,
    IDX_DATA_PAYLOAD  = 12,
    IDX_ACK_ARRAY     = 8
};

#define TX_POWER_INDEX_10dB    40
#define USE_TX_POWER_INDEX     TX_POWER_INDEX_10dB

static uint8_t tx_msg[PSDU_BYTES] = { 0x41, 0x8C, 0, 0x9A, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 'D', 'W', 0x10, 0x00, 0, 0, 0, 0, 0 };
#define DATA_FRAME_SN_IDX 2

#define TX_TO_RX_DELAY_UUS  60
/* SYNC RX 타임아웃은 길게 (비컨 신뢰성), 그 외엔 짧게 동적 설정.
 * dwt_setrxtimeout(0)은 API 명세상 "타임아웃 비활성(무한 대기)"이지만,
 * SYNC 유실 감지(sync_lost 카운트)를 위해 명시적 유한값을 사용한다.
 * PERIOD(~10.4ms)보다 충분히 크면 정상 동작에는 영향 없음.
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

static uint32_t total_bytes_received = 0;

typedef struct {
    uint32_t min_us;
    uint32_t max_us;
    uint64_t sum_us;
    uint32_t count;
} latency_stats_t;

static latency_stats_t node_latency[TOTAL_ARRAY_SIZE];

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

static void update_node_latency(latency_stats_t *stats, uint32_t latency_us) {
    if (latency_us < stats->min_us) stats->min_us = latency_us;
    if (latency_us > stats->max_us) stats->max_us = latency_us;
    stats->sum_us += latency_us;
    stats->count++;
}

/* ========== 프로토콜 상태 ========== */
static bool synchronized = false;
static uint32_t period_count = 0;
static uint8_t current_period_in_cycle = 0;
static uint32_t current_cycle = 0;

static uint8_t data_received_from[TOTAL_ARRAY_SIZE] = {0};
static uint8_t cumulative_ack_confirmed[TOTAL_ARRAY_SIZE] = {0};
static uint8_t cumulative_ack_count = 0;

static uint8_t expected_acks = TOTAL_NODES - 1;

static uint32_t pair1_success = 0, pair1_fail = 0, pair1_idle = 0;
static uint32_t pair2_success = 0, pair2_fail = 0, pair2_idle = 0;
static uint32_t pair3_success = 0, pair3_fail = 0, pair3_idle = 0;

static uint32_t total_cycles = 0;
static uint32_t successful_cycles = 0;
static uint32_t failed_cycles = 0;
#define MAX_FAILED_CYCLES_LOG 10
static uint32_t failed_cycle_numbers[MAX_FAILED_CYCLES_LOG] = {0};

static bool success_in_current_cycle = false;
static bool final_stats_printed = false;

typedef struct {
    uint32_t total_timeouts;
    uint32_t period_skip_count[PERIODS_PER_CYCLE];
} sync_loss_stats_t;

static sync_loss_stats_t sync_loss_stats = {0};
static bool sync_lost = false;

typedef enum {
    TX_STATE_IDLE,
    TX_STATE_FIRST_TX,
    TX_STATE_RETRANS
} transmission_state_t;

static transmission_state_t tx_state = TX_STATE_FIRST_TX;
static uint8_t retrans_msg[FRAME_LEN_MAX];

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

/* ========================================================================
 * MAIN FUNCTION
 * ======================================================================== */
int pac4_control_normal(void)
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
    /* SYNC는 무한 대기 (timeout 0) */
    dwt_setrxtimeout(US_TO_UUS(SYNC_RX_TIMEOUT_US));

    dwt_setinterrupt(0, 0, DWT_ENABLE_INT);
    dwt_writesysstatuslo(0xFFFFFFFF);

    {
        static char cfg_msg[240];
        snprintf(cfg_msg, sizeof(cfg_msg),
                 "%s: SEQ=%d SLOT_START=%dus PRE_US=%d DATA_PLEN=%d(%dsym) CIR=%d",
                 APP_NAME, MY_NODE_SEQ, MY_SLOT_START_US, PREAMBLE_US,
                 DATA_PLEN, PREAMBLE_SYMBOLS, ENABLE_CIR);
        test_run_info((unsigned char *)cfg_msg);
    }

    dwt_timer_init();

    {
        uint8_t i;
        for (i = 0; i < TOTAL_ARRAY_SIZE; i++) {
            node_latency[i].min_us = 0xFFFFFFFF;
            node_latency[i].max_us = 0;
            node_latency[i].sum_us = 0;
            node_latency[i].count  = 0;
        }
    }

    uint32_t last_sync_cycles = 0;
    uint32_t config_switch_cycles = us_to_cpu_cycles(CONFIG_SWITCH_US);
    uint32_t sync_timeout_cycles = us_to_cpu_cycles(27000);
    uint32_t final_timeout_cycles = us_to_cpu_cycles(5000000);
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
        if (last_sync_cycles != 0 && !sync_lost) {
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
            bool by_cycle   = (current_cycle > TARGET_CYCLES);

            if (by_timeout || by_cycle) {
                final_stats_printed = true;

                if (by_timeout && current_cycle > 0) {
                    if (success_in_current_cycle) successful_cycles++;
                    else {
                        failed_cycles++;
                        if (failed_cycles <= MAX_FAILED_CYCLES_LOG)
                            failed_cycle_numbers[failed_cycles - 1] = current_cycle;
                    }
                    total_cycles++;
                }

                static char hdr[80];
                snprintf(hdr, sizeof(hdr), "\n===== %s FINAL STATS (PLEN=%d, %dsym) =====",
                         APP_NAME, DATA_PLEN, PREAMBLE_SYMBOLS);
                final_log_info(hdr);

#if CONTROL_EXPERIMENT == 4
                {
                    static char s[120];
                    snprintf(s, sizeof(s), "P1: S=%lu F=%lu I=%lu | P2: S=%lu F=%lu I=%lu | P3: S=%lu F=%lu I=%lu",
                             (unsigned long)pair1_success, (unsigned long)pair1_fail, (unsigned long)pair1_idle,
                             (unsigned long)pair2_success, (unsigned long)pair2_fail, (unsigned long)pair2_idle,
                             (unsigned long)pair3_success, (unsigned long)pair3_fail, (unsigned long)pair3_idle);
                    final_log_info(s);
                }

                {
                    float rate = (total_cycles > 0) ? (float)successful_cycles / total_cycles * 100 : 0;
                    static char s[100];
                    snprintf(s, sizeof(s), "Cycles: total=%lu success=%lu(%.1f%%) fail=%lu",
                             (unsigned long)total_cycles, (unsigned long)successful_cycles,
                             rate, (unsigned long)failed_cycles);
                    final_log_info(s);
                }
#endif

#if CONTROL_EXPERIMENT == 1 || CONTROL_EXPERIMENT == 2
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
                    static char s[100];
                    snprintf(s, sizeof(s), "SYNC loss: %lu timeouts  RX errors=%lu",
                             (unsigned long)sync_loss_stats.total_timeouts,
                             (unsigned long)total_rx_errors);
                    final_log_info(s);
                }

                final_log_info("--- Latency per node ---");
                {
                    uint8_t i;
                    for (i = 0; i < TOTAL_ARRAY_SIZE; i++) {
                        if (node_latency[i].count > 0) {
                            float avg_us = (float)node_latency[i].sum_us / node_latency[i].count;
                            static char s[120];
                            snprintf(s, sizeof(s), "%s: min=%luus max=%luus avg=%.0fus (n=%lu)",
                                     get_slot_description(i),
                                     (unsigned long)node_latency[i].min_us,
                                     (unsigned long)node_latency[i].max_us,
                                     avg_us, (unsigned long)node_latency[i].count);
                            final_log_info(s);
                        }
                    }
                }

                final_log_info("===== END STATS =====\n");
                dwt_forcetrxoff();
                break;
            }
        }

        /* ========== [C] Config Switch (DATA→SYNC 준비) ==========
         * SYNC TX 시점이 다가오면 SYNC config로 돌아가서 SYNC RX 준비
         */
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

        /* ========== [D] RX 폴링 ========== */
        {
            uint32_t status_reg = dwt_readsysstatuslo();

            if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
                memset(rx_buffer, 0, sizeof(rx_buffer));
                dwt_readrxdata(rx_buffer, FRAME_LEN_MAX, 0);
                dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

                uint8_t msg_type = rx_buffer[IDX_MSG_TYPE];
                uint8_t src_node = rx_buffer[IDX_SOURCE];

                /* [D-1] SYNC 수신 - 핵심 타이밍 기준 */
                if (msg_type == MSG_TYPE_SYNC) {
                    uint32_t current_cycles = dwt_timer_get_cycles();
                    last_sync_cycles = current_cycles;

                    /* [NEW] DW3000 RX timestamp 획득 - delayed-TX 기준 */
                    last_sync_rx_ts_high32 = dwt_readrxtimestamphi32();

                    slot_tx_done = false;

                    uint8_t sync_period = rx_buffer[IDX_PERIOD_INFO];

                    if (sync_lost) {
                        sync_lost = false;
                        test_run_info((unsigned char *)"SYNC recovered");
                    }

                    period_count++;
                    current_period_in_cycle = sync_period;

                    /* 새 Cycle 감지 */
                    if (sync_period == 1) {
                        if (current_cycle > 0) {
                            if (success_in_current_cycle) successful_cycles++;
                            else {
                                failed_cycles++;
                                if (failed_cycles <= MAX_FAILED_CYCLES_LOG)
                                    failed_cycle_numbers[failed_cycles - 1] = current_cycle;
                            }
                            total_cycles++;
                        }
                        current_cycle++;
                        success_in_current_cycle = false;
                        memset(cumulative_ack_confirmed, 0, sizeof(cumulative_ack_confirmed));
                        cumulative_ack_count = 0;
                        tx_state = TX_STATE_FIRST_TX;
                    }

                    if (sync_period % 2 == 1) {
                        memset(data_received_from, 0, sizeof(data_received_from));
                    }

                    /* ===== [NEW] DATA config로 전환 후 delayed-TX 예약 ===== */
                    dwt_forcetrxoff();
                    dwt_writesysstatuslo(0xFFFFFFFF);

                    if (dwt_configure(&config_data) == DWT_SUCCESS) {
                        config_is_sync = false;
                    }

                    /* TX 메시지 준비 */
                    bool should_transmit = false;
                    bool is_data_period = (sync_period % 2 == 1);

                    if (is_data_period) {
#if CONTROL_EXPERIMENT == 1 || CONTROL_EXPERIMENT == 2
                        should_transmit = (current_cycle <= TARGET_CYCLES && sync_period == 1);
                        if (should_transmit) {
                            tx_msg[0] = 0xC5;
                            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
                            tx_msg[IDX_SOURCE] = MY_NODE_ID;
                            tx_msg[IDX_DEST] = NODE_ALL;
                            tx_msg[IDX_PRIORITY] = 1;
                            {
                                uint32_t ts = (dwt_timer_get_cycles() - last_sync_cycles) / 64;
                                memcpy(&tx_msg[IDX_TX_TIMESTAMP], &ts, sizeof(uint32_t));
                            }
                            memcpy(retrans_msg, tx_msg, sizeof(tx_msg));
                        }
#elif CONTROL_EXPERIMENT == 4
                        if (tx_state == TX_STATE_IDLE || success_in_current_cycle) {
                            should_transmit = false;
                            if (sync_period == 1) pair1_idle++;
                            else if (sync_period == 3) pair2_idle++;
                            else if (sync_period == 5) pair3_idle++;
                        } else {
                            should_transmit = true;
                            if (sync_period == 1) {
                                tx_msg[0] = 0xC5;
                                tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
                                tx_msg[IDX_SOURCE] = MY_NODE_ID;
                                tx_msg[IDX_DEST] = NODE_ALL;
                                tx_msg[IDX_PRIORITY] = 1;
                                {
                                    uint32_t ts = (dwt_timer_get_cycles() - last_sync_cycles) / 64;
                                    memcpy(&tx_msg[IDX_TX_TIMESTAMP], &ts, sizeof(uint32_t));
                                }
                                memcpy(retrans_msg, tx_msg, sizeof(tx_msg));
                            } else {
                                memcpy(tx_msg, retrans_msg, sizeof(tx_msg));
                            }
                            per_stats[my_slot_idx()].tx_count++;
                        }
#endif
                    } else {
#if CONTROL_EXPERIMENT == 4
                        should_transmit = true;
                        tx_msg[0] = 0xC5;
                        tx_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK_ARRAY;
                        tx_msg[IDX_SOURCE] = MY_NODE_ID;
                        tx_msg[IDX_DEST] = NODE_ALL;
                        memcpy(&tx_msg[IDX_ACK_ARRAY], data_received_from, TOTAL_ARRAY_SIZE);
#else
                        should_transmit = false;
#endif
                    }

                    /* ===== [NEW] Delayed-TX 실행 ===== */
                    if (should_transmit) {
                        total_tx_attempts++;
                        dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
                        dwt_writetxfctrl(sizeof(tx_msg), 0, 0);

                        int tx_result = schedule_delayed_tx(
                            last_sync_rx_ts_high32,
                            MY_SLOT_START_US,
                            0  /* no response expected */
                        );

                        if (tx_result == DWT_SUCCESS) {
                            uint32_t tx_status = 0;
                            waitforsysstatus(&tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
                            if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
                                dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
#if CONTROL_EXPERIMENT == 1 || CONTROL_EXPERIMENT == 2
                                per_stats[my_slot_idx()].tx_count++;
#endif
                                tx_msg[DATA_FRAME_SN_IDX]++;
                            }
                        } else {
                            total_tx_delayed_late++;
                            dwt_forcetrxoff();
                        }
                    }

                    slot_tx_done = true;

                    /* TX 끝났으면 DATA RX 윈도우 오픈 (다른 노드 메시지 수신용) */
                    dwt_forcetrxoff();
                    dwt_writesysstatuslo(0xFFFFFFFF);

#if CONTROL_EXPERIMENT == 4
                    /* 실험 4: 다른 노드의 DATA/ACK_ARRAY를 받기 위해 RX */
                    dwt_setrxtimeout(US_TO_UUS(ACK_RX_WINDOW_US));
                    dwt_rxenable(DWT_START_RX_IMMEDIATE);
#elif CONTROL_EXPERIMENT == 1 || CONTROL_EXPERIMENT == 2
                    /* 실험 1/2: Normal은 RX 안 함 (TX 전용)
                     * 다음 SYNC를 위해 config switch 타이밍에 SYNC config로 복귀
                     */
#endif

                    synchronized = true;
                    continue;
                }

#if CONTROL_EXPERIMENT == 4
                /* [D-2] DATA 수신 (홀수 Period) */
                if (msg_type == MSG_TYPE_DATA && (current_period_in_cycle % 2 == 1)) {
                    uint8_t src_idx = node_id_to_index(src_node);
                    if (src_idx < TOTAL_ARRAY_SIZE && src_idx != my_slot_idx()) {
                        if (!data_received_from[src_idx]) {
                            data_received_from[src_idx] = 1;
                        }
                        per_stats[src_idx].rx_count++;
                        total_bytes_received += sizeof(tx_msg);

                        {
                            uint32_t rx_ts = (dwt_timer_get_cycles() - last_sync_cycles) / 64;
                            uint32_t tx_ts;
                            memcpy(&tx_ts, &rx_buffer[IDX_TX_TIMESTAMP], sizeof(uint32_t));
                            if (rx_ts > tx_ts) {
                                update_node_latency(&node_latency[src_idx], rx_ts - tx_ts);
                            }
                        }

                        #if ENABLE_CIR
                        {
                            int n_samples = DWT_CIR_LEN_IP_PRF64;
                            memset(cir_buf, 0, sizeof(cir_buf));
                            dwt_readcir((uint32_t*)cir_buf, DWT_ACC_IDX_IP_M, 0,
                                        n_samples, DWT_CIR_READ_FULL);
                            static char cir_hdr[60];
                            snprintf(cir_hdr, sizeof(cir_hdr), "CIR from %s PLEN=%d",
                                     get_slot_description(src_idx), PREAMBLE_SYMBOLS);
                            test_run_info((unsigned char *)cir_hdr);
                            print_cir_data(cir_buf, n_samples);
                        }
                        #endif
                    }
                }

                /* [D-3] ACK_ARRAY 수신 (짝수 Period) */
                else if (msg_type == MSG_TYPE_ACK_ARRAY && (current_period_in_cycle % 2 == 0)) {
                    uint8_t src_idx = node_id_to_index(src_node);
                    if (src_idx < TOTAL_ARRAY_SIZE && src_idx != my_slot_idx()) {
                        uint8_t *ack_array = &rx_buffer[IDX_ACK_ARRAY];

                        if (ack_array[my_slot_idx()] == 1) {
                            if (!cumulative_ack_confirmed[src_idx]) {
                                cumulative_ack_confirmed[src_idx] = 1;
                                cumulative_ack_count++;

                                if (cumulative_ack_count >= expected_acks) {
                                    if (!success_in_current_cycle) {
                                        success_in_current_cycle = true;
                                        tx_state = TX_STATE_IDLE;
                                        if (current_period_in_cycle <= 2) pair1_success++;
                                        else if (current_period_in_cycle <= 4) pair2_success++;
                                        else pair3_success++;
                                    }
                                }
                            }
                        }
                    }
                }

                /* 다음 메시지 수신 대기 (실험 4) */
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);
                dwt_setrxtimeout(US_TO_UUS(ACK_RX_WINDOW_US));
                dwt_rxenable(DWT_START_RX_IMMEDIATE);
#endif
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

#endif /* TEST_PAC4_CONTROL_NORMAL */
