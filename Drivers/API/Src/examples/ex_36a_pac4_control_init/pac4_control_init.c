/*! ----------------------------------------------------------------------------
 *  @file    pac4_control_init.c
 *  @brief   Conventional PAC4 control - INIT (Coordinator) Node
 *
 *           BRRS delayed-RX와 PAC8 조합의 대조군이다.
 *           송신 프레임과 도착 시각은 BRRS 실험과 동일하게 유지하되,
 *           코디네이터는 DATA 설정 직후 RX를 즉시 켜서 패킷 도착 전부터
 *           계속 청취하며 DATA 프리앰블 획득에는 PAC4를 사용한다.
 *
 *           TDMA 구조:
 *           - SYNC: PLEN512 (고정, 비컨 신뢰성)
 *           - DATA: PLEN32, PAC4
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

#if defined(TEST_PAC4_CONTROL_INIT)

extern void test_run_info(unsigned char *data);
extern int SEGGER_RTT_ConfigUpBuffer(unsigned BufferIndex, const char* sName, void* pBuffer, unsigned BufferSize, unsigned Flags);
extern unsigned SEGGER_RTT_WriteString(unsigned BufferIndex, const char* s);

#define APP_NAME "PAC4 CONTROL INIT v1.0 (immediate-RX)"

/* ========== 실험 모드 선택 ========== */
#define CONTROL_EXPERIMENT  1

/* ========== PAC4 대조군 실험 파라미터 ========== */
#define DATA_PLEN       DWT_PLEN_32
#define SYNC_PLEN       DWT_PLEN_512

/* CIR 수집 토글 (실험 2) */
#define ENABLE_CIR      0

#define CIR_ANALYSIS_SAMPLES    64
#define CIR_LOG_PER_FRAME       1
#define CIR_LOG_PER_FRAME_TO_TERMINAL 1
#define CIR_LOG_CYCLE_LINES     0
#define CIR_DUMP_SAMPLES_AT_END 0
#define CIR_RAW_LOG_LIMIT       0
#define CIR_SAMPLE_DUMP_DELAY_MS 5
#define CIR_RTT_CHANNEL         1
#define CIR_RTT_BUFFER_SIZE     32768
#define CIR_RTT_MODE_BLOCK      2U
#define CIR_RAW_SAMPLES         CIR_ANALYSIS_SAMPLES
#define CIR_RAW_PRE_FP_SAMPLES  16
#define CIR_NOISE_PRE_FP_SAMPLES 12
#define CIR_NOISE_GUARD_SAMPLES  2
#define CIR_FP_PEAK_PRE_SAMPLES  1
#define CIR_FP_PEAK_POST_SAMPLES 3

/* 측정 사이클 수 */
#define TARGET_CYCLES   1000

/* Debugger로 두 보드를 번갈아 시작할 때 Normal 노드가 SYNC RX에 들어갈 여유 시간 */
#define STARTUP_GRACE_MS 10000

/* ========== TDMA 프로토콜 파라미터 ========== */
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
#define MY_NODE_SEQ         1

/* 슬롯 타이밍 (마이크로초) */
#define SLOT_GUARD_US       500
#define SYNC_BUFFER_US      3000

/* 프리앰블 길이와 PSDU 길이에 따른 슬롯 간격 계산 */
#define PSDU_BYTES          127
/* PRF64 preamble/SFD 심볼 길이 = 1017.63ns. 올림(ceil) 처리로 창이 늦게 열리는
 * 방향의 절사 오차를 제거 (기존 (sym*103)/100은 32/64sym에서 실제보다 작게 나왔음).
 * 주의: brrs_normal.c와 반드시 동일해야 함 (SLOT_INTERVAL 일치). */
#define SFD_SYMBOLS         8
#define SFD_US              (((SFD_SYMBOLS) * 10177 + 9999) / 10000)   /* 8sym -> 9us (ceil) */
#define PHR_US              25
#define DATA_RATE_KBPS      6800
#define PSDU_US             (((PSDU_BYTES) * 8 * 1000 + DATA_RATE_KBPS - 1) / DATA_RATE_KBPS)
#define PHR_PSDU_US         (PHR_US + PSDU_US)
#define PREAMBLE_SYMBOLS    ((DATA_PLEN + 1) * 8)
#define PREAMBLE_US         (((PREAMBLE_SYMBOLS) * 10177 + 9999) / 10000)  /* ceil(sym * 1.0177us) */
#define SLOT_INTERVAL_US    (PREAMBLE_US + SFD_US + PHR_PSDU_US + SLOT_GUARD_US)

/* DATA 설정 직후부터 모든 DATA 슬롯이 끝날 때까지 RX를 켜 둔다.
 * 예약 delayed-RX를 사용하지 않으므로 lead/tail margin은 없다. */
#define CONTROL_RX_TIMEOUT_US \
    (SYNC_BUFFER_US + TOTAL_SLOTS * SLOT_INTERVAL_US + 1000)

/* Period 타이밍 */
#define CONFIG_SWITCH_US    (SYNC_BUFFER_US + TOTAL_SLOTS * SLOT_INTERVAL_US + 2000)
#define PERIOD_US           (CONFIG_SWITCH_US + 4000)

/* ========== [NEW] DW3000 timestamp 변환 ==========
 * DW3000 system time: ~15.65 ps/tick, 40-bit counter
 * 1 us = 63897.6 ticks ≈ 63898 ticks
 * dwt_setdelayedtrxtime()은 상위 32비트만 사용 (하위 8비트는 0)
 * 따라서 실제 분해능: 256 ticks ≈ 4.0064 ns
 */
#define DWT_TIME_UNITS_PER_US  63898ULL  /* 1 us 당 DW3000 시간 단위 */
#define US_TO_DWT_TIME(us)     ((uint64_t)(us) * DWT_TIME_UNITS_PER_US)

/* dwt_setrxtimeout()의 단위는 us가 아니라 UUS(1.0256us = 512/499.2MHz).
 * us 값을 그대로 넘기면 창이 2.6% 길어진다. ceil 변환으로 의도한 길이를 보장. */
#define US_TO_UUS(us)          (((uint32_t)(us) * 10000UL + 10255UL) / 10256UL)

/* Default communication configuration for DATA/ACK */
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

/* ========== 노드 ID 정의 ========== */
#define NODE_INIT '1'
#define NODE_2    '2'
#define NODE_3    '3'
#define NODE_4    '4'
#define NODE_5    '5'
#define NODE_6    '6'
#define NODE_7    '7'
#define NODE_8    '8'
#define NODE_ALL  'B'

/* ========== 메시지 타입 정의 ========== */
#define MSG_TYPE_SYNC       0x01
#define MSG_TYPE_DATA       0x02
#define MSG_TYPE_ACK_ARRAY  0x07

/* ========== 메시지 인덱스 정의 ========== */
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

/* RESPONSE_EXPECTED 플래그 없이 TX하므로 rx-after-tx 자동 RX는 발동하지 않는다. */
#define TX_TO_RX_DELAY_UUS  60

static uint8_t rx_buffer[FRAME_LEN_MAX];
extern dwt_txconfig_t txconfig_options;

/* ========== PER 통계 ========== */
typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t rx_error_count;
} per_stats_t;

static per_stats_t per_stats[TOTAL_ARRAY_SIZE] = {0};
static uint32_t total_rx_errors = 0;
static uint32_t total_rx_timeouts = 0;
static uint32_t total_rx_enable_failures = 0;

/* [DIAG] 실패 원인 세분화: "수신 실패"와 "예약 실패"를 구분하기 위한 카운터 */
static uint32_t rx_to_frame = 0;     /* RXFTO: 창 내 preamble 미검출 (frame wait TO) */
static uint32_t rx_to_preamble = 0;  /* RXPTO: preamble detection timeout (PRETOC 설정 시) */
static uint32_t rx_err_sfdto = 0;    /* RXSTO: preamble은 잡았으나 SFD 실패 (노이즈 오검출 포함) */
static uint32_t rx_err_phe = 0;      /* PHY header error */
static uint32_t rx_err_fce = 0;      /* CRC error */
static uint32_t rx_err_fsl = 0;      /* Reed-Solomon / sync loss */

static const char* get_slot_description(uint8_t slot_idx);

/* ========== CIR 수집 ========== */
#if ENABLE_CIR
static uint8_t cir_buf[DWT_CIR_LEN_MAX * 6];

static void enable_cir_diagnostics(void)
{
    dwt_configciadiag(DW_CIA_DIAG_LOG_ALL);
}

typedef struct {
    int32_t min_x100;
    int32_t max_x100;
    int64_t sum_x100;
    uint32_t count;
} signal_stats_t;

static signal_stats_t rssi_stats[TOTAL_ARRAY_SIZE];
static signal_stats_t fp_power_stats[TOTAL_ARRAY_SIZE];
static signal_stats_t fp_gap_stats[TOTAL_ARRAY_SIZE];

typedef struct {
    uint64_t min_x1000;
    uint64_t max_x1000;
    uint64_t sum_x1000;
    uint32_t count;
} ratio_stats_t;

static ratio_stats_t fp_snr_ratio_stats[TOTAL_ARRAY_SIZE];
static uint32_t cir_raw_logs = 0;
static bool cir_final_pass = false;
static uint32_t cir_final_expected = 0;
static uint32_t cir_final_rx = 0;
static uint32_t cir_final_valid = 0;

#else

static char cir_rtt_buffer[CIR_RTT_BUFFER_SIZE];
static bool cir_rtt_configured = false;

static void cir_rtt_init(void)
{
    if (!cir_rtt_configured) {
        SEGGER_RTT_ConfigUpBuffer(CIR_RTT_CHANNEL, "EXP_LOG",
                                  cir_rtt_buffer, sizeof(cir_rtt_buffer),
                                  CIR_RTT_MODE_BLOCK);
        cir_rtt_configured = true;
    }
}

static void cir_log_info(const char *line)
{
    cir_rtt_init();
    SEGGER_RTT_WriteString(CIR_RTT_CHANNEL, line);
    SEGGER_RTT_WriteString(CIR_RTT_CHANNEL, "\n");
}

#endif

#if ENABLE_CIR

typedef struct {
    uint32_t frame_no;
    uint32_t cycle_no;
    uint8_t src_idx;
    uint16_t fp_sample;
    uint16_t peak_idx;
    uint16_t accum;
    int32_t rssi_x100;
    int32_t fp_x100;
    int32_t gap_x100;
    uint64_t fp_peak_power;
    uint64_t noise_floor_power;
    uint16_t noise_samples;
    uint64_t snr_ratio_x1000;
} cir_sample_log_t;

static cir_sample_log_t cir_sample_logs[TARGET_CYCLES];
static uint32_t cir_sample_log_count = 0;
static char cir_rtt_buffer[CIR_RTT_BUFFER_SIZE];
static bool cir_rtt_configured = false;

static void cir_rtt_init(void)
{
    if (!cir_rtt_configured) {
        SEGGER_RTT_ConfigUpBuffer(CIR_RTT_CHANNEL, "CIR_CSV",
                                  cir_rtt_buffer, sizeof(cir_rtt_buffer),
                                  CIR_RTT_MODE_BLOCK);
        cir_rtt_configured = true;
    }
}

static void cir_log_info(const char *line)
{
    cir_rtt_init();
    SEGGER_RTT_WriteString(CIR_RTT_CHANNEL, line);
    SEGGER_RTT_WriteString(CIR_RTT_CHANNEL, "\n");
}

static void init_signal_stats(signal_stats_t *stats) {
    stats->min_x100 = 0x7FFFFFFF;
    stats->max_x100 = (int32_t)0x80000000;
    stats->sum_x100 = 0;
    stats->count = 0;
}

static void update_signal_stats(signal_stats_t *stats, int32_t value_x100) {
    if (value_x100 < stats->min_x100) stats->min_x100 = value_x100;
    if (value_x100 > stats->max_x100) stats->max_x100 = value_x100;
    stats->sum_x100 += value_x100;
    stats->count++;
}

static void init_ratio_stats(ratio_stats_t *stats) {
    stats->min_x1000 = UINT64_MAX;
    stats->max_x1000 = 0;
    stats->sum_x1000 = 0;
    stats->count = 0;
}

static void update_ratio_stats(ratio_stats_t *stats, uint64_t value_x1000) {
    if (value_x1000 < stats->min_x1000) stats->min_x1000 = value_x1000;
    if (value_x1000 > stats->max_x1000) stats->max_x1000 = value_x1000;
    stats->sum_x1000 += value_x1000;
    stats->count++;
}

static int32_t q8_8_to_x100(int16_t value_q8_8) {
    return ((int32_t)value_q8_8 * 100) / 256;
}

static void format_x100(char *buf, size_t size, int32_t value_x100) {
    char sign = '\0';
    int32_t abs_value = value_x100;
    if (value_x100 < 0) {
        sign = '-';
        abs_value = -value_x100;
    }
    if (sign) {
        snprintf(buf, size, "-%ld.%02ld", (long)(abs_value / 100), (long)(abs_value % 100));
    } else {
        snprintf(buf, size, "%ld.%02ld", (long)(abs_value / 100), (long)(abs_value % 100));
    }
}

static void print_cir_data(uint8_t *buf, int n_samples) {
    int i;
    test_run_info((unsigned char *)"\nCIR_START");
    for (i = 0; i < n_samples; i++) {
        int32_t real_val, imag_val;
        uint8_t lo_re, mid_re, hi_re, sign_re;
        uint8_t lo_im, mid_im, hi_im, sign_im;
        static char cir_line[40];

        lo_re  = buf[i * 6 + 0];
        mid_re = buf[i * 6 + 1];
        hi_re  = buf[i * 6 + 2];
        lo_im  = buf[i * 6 + 3];
        mid_im = buf[i * 6 + 4];
        hi_im  = buf[i * 6 + 5];

        sign_re = ((hi_re & 0x80) == 0x80) ? 0xFF : 0;
        sign_im = ((hi_im & 0x80) == 0x80) ? 0xFF : 0;

        real_val = (int32_t)((uint32_t)sign_re << 24 | (uint32_t)hi_re << 16 | (uint32_t)mid_re << 8 | lo_re);
        imag_val = (int32_t)((uint32_t)sign_im << 24 | (uint32_t)hi_im << 16 | (uint32_t)mid_im << 8 | lo_im);

        snprintf(cir_line, sizeof(cir_line), "%ld,%ld,", (long)real_val, (long)imag_val);
        test_run_info((unsigned char *)cir_line);
    }
    test_run_info((unsigned char *)"CIR_END");
}

static void store_cir_sample(uint8_t src_idx,
                             uint32_t frame_no,
                             uint32_t cycle_no,
                             uint16_t fp_sample,
                             uint16_t peak_idx,
                             uint16_t accum,
                             int32_t rssi_x100,
                             int32_t fp_x100,
                             int32_t gap_x100,
                             uint64_t fp_peak_power,
                             uint64_t noise_floor_power,
                             uint16_t noise_samples,
                             uint64_t snr_ratio_x1000)
{
    cir_sample_log_t *entry;

    if (cir_sample_log_count >= TARGET_CYCLES) {
        return;
    }

    entry = &cir_sample_logs[cir_sample_log_count++];
    entry->frame_no = frame_no;
    entry->cycle_no = cycle_no;
    entry->src_idx = src_idx;
    entry->fp_sample = fp_sample;
    entry->peak_idx = peak_idx;
    entry->accum = accum;
    entry->rssi_x100 = rssi_x100;
    entry->fp_x100 = fp_x100;
    entry->gap_x100 = gap_x100;
    entry->fp_peak_power = fp_peak_power;
    entry->noise_floor_power = noise_floor_power;
    entry->noise_samples = noise_samples;
    entry->snr_ratio_x1000 = snr_ratio_x1000;
}

static void dump_cir_samples(void)
{
    uint32_t i;
    static char csv_line[360];

    snprintf(csv_line, sizeof(csv_line),
             "CIR_DUMP_START,plen=%d,expected=%lu,rx=%lu,valid_cir=%lu,dump_count=%lu,status=%s",
             PREAMBLE_SYMBOLS,
             (unsigned long)cir_final_expected,
             (unsigned long)cir_final_rx,
             (unsigned long)cir_final_valid,
             (unsigned long)cir_sample_log_count,
             cir_final_pass ? "PASS" : "FAIL");
    cir_log_info(csv_line);
    test_run_info((unsigned char *)csv_line);

    cir_log_info("CIR_CSV_HEADER,rx_seq,cycle,node,plen,fp_sample,peak_idx,accum,rssi_dbm,fp_dbm,rssi_fp_gap_db,fp_peak_power,noise_floor_power,noise_samples,fp_snr_ratio_x1000");

    for (i = 0; i < cir_sample_log_count; i++) {
        cir_sample_log_t *entry = &cir_sample_logs[i];
        char rssi_str[16], fp_str[16], gap_str[16];

        format_x100(rssi_str, sizeof(rssi_str), entry->rssi_x100);
        format_x100(fp_str, sizeof(fp_str), entry->fp_x100);
        format_x100(gap_str, sizeof(gap_str), entry->gap_x100);

        snprintf(csv_line, sizeof(csv_line),
                 "CIR_CSV,%lu,%lu,%s,%d,%u,%u,%u,%s,%s,%s,%llu,%llu,%u,%llu",
                 (unsigned long)entry->frame_no,
                 (unsigned long)entry->cycle_no,
                 get_slot_description(entry->src_idx), PREAMBLE_SYMBOLS,
                 entry->fp_sample, entry->peak_idx, entry->accum,
                 rssi_str, fp_str, gap_str,
                 (unsigned long long)entry->fp_peak_power,
                 (unsigned long long)entry->noise_floor_power,
                 entry->noise_samples,
                 (unsigned long long)entry->snr_ratio_x1000);
        cir_log_info(csv_line);
        Sleep(CIR_SAMPLE_DUMP_DELAY_MS);
    }

    snprintf(csv_line, sizeof(csv_line),
             "CIR_DUMP_DONE,plen=%d,expected=%lu,rx=%lu,valid_cir=%lu,dump_count=%lu,status=%s",
             PREAMBLE_SYMBOLS,
             (unsigned long)cir_final_expected,
             (unsigned long)cir_final_rx,
             (unsigned long)cir_final_valid,
             (unsigned long)cir_sample_log_count,
             cir_final_pass ? "PASS" : "FAIL");
    cir_log_info(csv_line);
    test_run_info((unsigned char *)csv_line);

    snprintf(csv_line, sizeof(csv_line),
             "EXP2_DONE,plen=%d,expected=%lu,rx=%lu,valid_cir=%lu,dump_count=%lu,status=%s",
             PREAMBLE_SYMBOLS,
             (unsigned long)cir_final_expected,
             (unsigned long)cir_final_rx,
             (unsigned long)cir_final_valid,
             (unsigned long)cir_sample_log_count,
             cir_final_pass ? "PASS" : "FAIL");
    cir_log_info(csv_line);
    test_run_info((unsigned char *)csv_line);
}

static void print_exp2_done_marker(void)
{
    static char csv_line[180];

    snprintf(csv_line, sizeof(csv_line),
             "EXP2_DONE,plen=%d,expected=%lu,rx=%lu,valid_cir=%lu,dump_count=%lu,status=%s",
             PREAMBLE_SYMBOLS,
             (unsigned long)cir_final_expected,
             (unsigned long)cir_final_rx,
             (unsigned long)cir_final_valid,
             (unsigned long)cir_sample_log_count,
             cir_final_pass ? "PASS" : "FAIL");
    cir_log_info(csv_line);
    test_run_info((unsigned char *)csv_line);
}

static int32_t cir_read_s24(const uint8_t *sample)
{
    uint32_t raw = ((uint32_t)sample[2] << 16) |
                   ((uint32_t)sample[1] << 8) |
                   (uint32_t)sample[0];

    if (raw & 0x800000UL) {
        raw |= 0xFF000000UL;
    }
    return (int32_t)raw;
}

static uint64_t cir_sample_power(const uint8_t *buf, uint16_t local_idx)
{
    const uint8_t *sample = &buf[local_idx * 6U];
    int32_t real_val = cir_read_s24(&sample[0]);
    int32_t imag_val = cir_read_s24(&sample[3]);
    int64_t real64 = real_val;
    int64_t imag64 = imag_val;

    return (uint64_t)(real64 * real64) + (uint64_t)(imag64 * imag64);
}

static bool calculate_fp_snr_from_cir(uint8_t *buf,
                                      uint16_t n_samples,
                                      uint16_t sample_offs,
                                      uint16_t fp_sample,
                                      uint64_t *fp_peak_power,
                                      uint64_t *noise_floor_power,
                                      uint16_t *noise_samples,
                                      uint64_t *snr_ratio_x1000)
{
    uint16_t fp_local = (fp_sample > sample_offs) ? (uint16_t)(fp_sample - sample_offs) : 0;
    uint16_t noise_end;
    uint16_t noise_start;
    uint16_t fp_start;
    uint16_t fp_end;
    uint16_t i;
    uint64_t noise_sum = 0;
    uint64_t peak_power = 0;

    *fp_peak_power = 0;
    *noise_floor_power = 0;
    *noise_samples = 0;
    *snr_ratio_x1000 = 0;

    if (fp_local >= n_samples) {
        return false;
    }

    noise_end = (fp_local > CIR_NOISE_GUARD_SAMPLES) ?
                (uint16_t)(fp_local - CIR_NOISE_GUARD_SAMPLES) : 0;
    noise_start = (noise_end > CIR_NOISE_PRE_FP_SAMPLES) ?
                  (uint16_t)(noise_end - CIR_NOISE_PRE_FP_SAMPLES) : 0;

    for (i = noise_start; i < noise_end; i++) {
        noise_sum += cir_sample_power(buf, i);
        (*noise_samples)++;
    }

    fp_start = (fp_local > CIR_FP_PEAK_PRE_SAMPLES) ?
               (uint16_t)(fp_local - CIR_FP_PEAK_PRE_SAMPLES) : 0;
    fp_end = (uint16_t)(fp_local + CIR_FP_PEAK_POST_SAMPLES + 1);
    if (fp_end > n_samples) {
        fp_end = n_samples;
    }

    for (i = fp_start; i < fp_end; i++) {
        uint64_t power = cir_sample_power(buf, i);
        if (power > peak_power) {
            peak_power = power;
        }
    }

    if (*noise_samples == 0 || noise_sum == 0 || peak_power == 0) {
        return false;
    }

    *fp_peak_power = peak_power;
    *noise_floor_power = noise_sum / *noise_samples;
    if (*noise_floor_power == 0) {
        return false;
    }

    *snr_ratio_x1000 = (peak_power * 1000ULL) / *noise_floor_power;
    return true;
}

static void log_cir_quality(uint8_t src_idx, uint32_t frame_no, uint32_t cycle_no)
{
    dwt_cirdiags_t diag;
    int16_t rssi_q8_8 = 0;
    int16_t fp_q8_8 = 0;
    int32_t rssi_x100 = 0;
    int32_t fp_x100 = 0;
    int32_t gap_x100 = 0;
    uint16_t fp_sample;
    uint16_t sample_offs;
    uint64_t fp_peak_power = 0;
    uint64_t noise_floor_power = 0;
    uint16_t noise_samples = 0;
    uint64_t snr_ratio_x1000 = 0;
    bool snr_valid;
    char rssi_str[16], fp_str[16], gap_str[16];

    if (dwt_readdiagnostics_acc(&diag, DWT_ACC_IDX_IP_M) != DWT_SUCCESS) {
        return;
    }

    if (dwt_calculate_rssi(&diag, DWT_ACC_IDX_IP_M, &rssi_q8_8) != DWT_SUCCESS) {
        return;
    }
    if (dwt_calculate_first_path_power(&diag, DWT_ACC_IDX_IP_M, &fp_q8_8) != DWT_SUCCESS) {
        return;
    }

    rssi_x100 = q8_8_to_x100(rssi_q8_8);
    fp_x100 = q8_8_to_x100(fp_q8_8);
    gap_x100 = rssi_x100 - fp_x100;

    update_signal_stats(&rssi_stats[src_idx], rssi_x100);
    update_signal_stats(&fp_power_stats[src_idx], fp_x100);
    update_signal_stats(&fp_gap_stats[src_idx], gap_x100);

    fp_sample = (uint16_t)(diag.FpIndex >> 6);
    sample_offs = (fp_sample > CIR_RAW_PRE_FP_SAMPLES) ?
                  (uint16_t)(fp_sample - CIR_RAW_PRE_FP_SAMPLES) : 0;

    memset(cir_buf, 0, sizeof(cir_buf));
    dwt_readcir((uint32_t*)cir_buf, DWT_ACC_IDX_IP_M, sample_offs,
                CIR_ANALYSIS_SAMPLES, DWT_CIR_READ_FULL);

    snr_valid = calculate_fp_snr_from_cir(cir_buf, CIR_ANALYSIS_SAMPLES,
                                          sample_offs, fp_sample,
                                          &fp_peak_power, &noise_floor_power,
                                          &noise_samples, &snr_ratio_x1000);
    if (snr_valid) {
        update_ratio_stats(&fp_snr_ratio_stats[src_idx], snr_ratio_x1000);
    }

    store_cir_sample(src_idx, frame_no, cycle_no, fp_sample, diag.peakIndex, diag.accumCount,
                     rssi_x100, fp_x100, gap_x100,
                     fp_peak_power, noise_floor_power, noise_samples, snr_ratio_x1000);

    format_x100(rssi_str, sizeof(rssi_str), rssi_x100);
    format_x100(fp_str, sizeof(fp_str), fp_x100);
    format_x100(gap_str, sizeof(gap_str), gap_x100);

#if CIR_LOG_PER_FRAME
    {
        static char csv_line[360];
        snprintf(csv_line, sizeof(csv_line),
                 "CIR_CSV,%lu,%lu,%s,%d,%u,%u,%u,%s,%s,%s,%llu,%llu,%u,%llu",
                 (unsigned long)frame_no,
                 (unsigned long)cycle_no,
                 get_slot_description(src_idx), PREAMBLE_SYMBOLS,
                 fp_sample, diag.peakIndex, diag.accumCount,
                 rssi_str, fp_str, gap_str,
                 (unsigned long long)fp_peak_power,
                 (unsigned long long)noise_floor_power,
                 noise_samples,
                 (unsigned long long)snr_ratio_x1000);
        cir_log_info(csv_line);
#if CIR_LOG_PER_FRAME_TO_TERMINAL
        test_run_info((unsigned char *)csv_line);
#endif
    }
#endif

    if (cir_raw_logs < CIR_RAW_LOG_LIMIT) {
        static char diag_line[220];

        snprintf(diag_line, sizeof(diag_line),
                 "CIR_DIAG frame=%lu node=%s plen=%d fp_samp=%u peak_idx=%u accum=%u rssi=%sdBm fp=%sdBm gap=%sdB power=%lu F1=%lu F2=%lu F3=%lu peak=%lu",
                 (unsigned long)frame_no, get_slot_description(src_idx), PREAMBLE_SYMBOLS,
                 fp_sample, diag.peakIndex, diag.accumCount,
                 rssi_str, fp_str, gap_str,
                 (unsigned long)diag.power,
                 (unsigned long)diag.F1,
                 (unsigned long)diag.F2,
                 (unsigned long)diag.F3,
                 (unsigned long)diag.peakAmp);
        test_run_info((unsigned char *)diag_line);

        snprintf(diag_line, sizeof(diag_line),
                 "CIR_WINDOW frame=%lu node=%s start_sample=%u samples=%u",
                 (unsigned long)frame_no, get_slot_description(src_idx),
                 sample_offs, CIR_RAW_SAMPLES);
        test_run_info((unsigned char *)diag_line);
        print_cir_data(cir_buf, CIR_RAW_SAMPLES);
        cir_raw_logs++;
    }
}
#endif

static void final_log_info(const char *line)
{
    test_run_info((unsigned char *)line);
#if !ENABLE_CIR
    cir_log_info(line);
#endif
}

#if !ENABLE_CIR
static void terminal_log_info(unsigned char *data)
{
    test_run_info(data);
    cir_log_info((const char *)data);
}

#define test_run_info(data) terminal_log_info(data)
#endif

/* ========== 처리량 측정 ========== */
static uint32_t total_bytes_received = 0;
static uint32_t measurement_start_cycles = 0;

/* ========== 노드별 지연시간 추적 ========== */
typedef struct {
    uint32_t min_us;
    uint32_t max_us;
    uint64_t sum_us;
    uint32_t count;
} latency_stats_t;

static latency_stats_t node_latency[TOTAL_ARRAY_SIZE];
static latency_stats_t node_rx_offset[TOTAL_ARRAY_SIZE];
static latency_stats_t node_uwb_rx_offset[TOTAL_ARRAY_SIZE];

/* [DIAG] Ipatov accumCount: CIR 누산에 실제 들어간 preamble 심볼 수.
 * PLEN - accumCount = (창 지연으로 놓친 심볼) + (검출 고정 비용 ~21심볼).
 * lead margin 스윕 시 바이어스를 심볼 단위로 직접 측정하는 용도. */
static latency_stats_t node_accum[TOTAL_ARRAY_SIZE];
/* [DIAG] 창 열림(programmed) 시각 -> RMARKER 도착까지의 오프셋(us).
 * 기대값 = 실제 preamble+SFD 길이 + lead margin. 스케줄링 모델 오차 검증용. */
static latency_stats_t node_open_to_rmarker[TOTAL_ARRAY_SIZE];

/* [DIAG] accumCount 히스토그램: min/max/avg만으로는 분포 모양(양봉 vs 연속)을
 * 구분할 수 없어 값별 빈도를 직접 기록. PAC 양자화 가설 검증용. */
static uint32_t accum_hist[PREAMBLE_SYMBOLS + 1] = {0};

/* ========== DWT 사이클 카운터 (MCU 측) ========== */
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

static uint32_t dwt_high32_delta_to_us(uint32_t start_high32, uint32_t end_high32) {
    uint32_t delta_high32 = end_high32 - start_high32;
    uint64_t delta_ticks = (uint64_t)delta_high32 << 8;
    return (uint32_t)(delta_ticks / DWT_TIME_UNITS_PER_US);
}

static void update_node_latency(latency_stats_t *stats, uint32_t latency_us) {
    if (latency_us < stats->min_us) stats->min_us = latency_us;
    if (latency_us > stats->max_us) stats->max_us = latency_us;
    stats->sum_us += latency_us;
    stats->count++;
}

/* ========== PAC4 대조군 RX 헬퍼 ==========
 * DATA 설정이 끝나는 즉시 RX를 켜고 패킷 도착 전부터 계속 청취한다.
 * last_rx_open_high32는 명령 시각이며 실제 RF 안정화 완료 시각은 아니다.
 */
static uint32_t last_rx_open_high32 = 0;

static bool start_control_rx(void)
{
    int result;

    last_rx_open_high32 = dwt_readsystimestamphi32();
    dwt_setrxtimeout(US_TO_UUS(CONTROL_RX_TIMEOUT_US));
    result = dwt_rxenable(DWT_START_RX_IMMEDIATE);
    if (result != DWT_SUCCESS) {
        total_rx_enable_failures++;
        dwt_forcetrxoff();
        return false;
    }

    return true;
}

/* ========== ACK/DATA 추적 ========== */
static uint8_t data_received_from[TOTAL_ARRAY_SIZE] = {0};
static uint8_t cumulative_ack_confirmed[TOTAL_ARRAY_SIZE] = {0};
static uint8_t cumulative_ack_count = 0;

static uint32_t expected_rx[TOTAL_ARRAY_SIZE] = {0};

static uint32_t current_cycle = 1;
static uint8_t period_in_cycle = 1;

static uint32_t pair1_success = 0, pair1_fail = 0, pair1_idle = 0;
static uint32_t pair2_success = 0, pair2_fail = 0, pair2_idle = 0;
static uint32_t pair3_success = 0, pair3_fail = 0, pair3_idle = 0;

static uint32_t total_cycles = 0;
static uint32_t data_successful_cycles = 0;
static uint32_t failed_cycles = 0;
#define MAX_FAILED_CYCLES_LOG 10
static uint32_t failed_cycle_numbers[MAX_FAILED_CYCLES_LOG] = {0};

static bool data_success_in_current_cycle = false;
static bool final_stats_printed = false;

static uint8_t expected_acks = TOTAL_NODES - 1;

typedef enum {
    TX_STATE_IDLE,
    TX_STATE_FIRST_TX,
    TX_STATE_RETRANS
} transmission_state_t;

static transmission_state_t tx_state = TX_STATE_FIRST_TX;
static uint8_t retrans_msg[FRAME_LEN_MAX];

/* ========== 슬롯 RX 상태 ========== */
static uint32_t last_sync_tx_ts_high32 = 0;  /* SYNC TX timestamp 저장 */
static bool slots_scheduled[TOTAL_ARRAY_SIZE] = {false};
static uint8_t current_rx_slot = 0xFF;

static bool start_control_rx_slot(uint8_t slot_idx)
{
    if (slot_idx >= TOTAL_ARRAY_SIZE) {
        current_rx_slot = 0xFF;
        return false;
    }

    slots_scheduled[slot_idx] = true;
    current_rx_slot = slot_idx;
    if (start_control_rx()) {
        return true;
    }

    current_rx_slot = 0xFF;
    return false;
}

/* ========== 유틸리티 함수 ========== */

static uint8_t node_id_to_index(uint8_t node_id) {
    if (node_id >= '1' && node_id <= '8') return (uint8_t)(node_id - '1');
    return 0xFF;
}

static uint8_t index_to_node_id(uint8_t index) {
    if (index < 8) return (uint8_t)('1' + index);
    return '?';
}

static const char* get_slot_description(uint8_t slot_idx) {
    static const char* names[] = {"INIT","N2","N3","N4","N5","N6","N7","N8"};
    if (slot_idx < 8) return names[slot_idx];
    return "???";
}

/* ========================================================================
 * MAIN FUNCTION
 * ======================================================================== */
int pac4_control_init(void)
{
    cir_rtt_init();
    cir_log_info("EXP_LOG_READY,channel=1");
#if ENABLE_CIR
    cir_log_info("CIR_RTT_READY,channel=1,name=CIR_CSV");
#endif
    test_run_info((unsigned char *)APP_NAME);
#if ENABLE_CIR
    test_run_info((unsigned char *)"EXP2 CSV output is on RTT channel 1 (CIR_CSV)");
#endif

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
#if ENABLE_CIR
    enable_cir_diagnostics();
#endif

    /* Linear TX Power 설정 */
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
    /* DATA RX timeout은 start_control_rx()에서 설정 */

    dwt_setinterrupt(0, 0, DWT_ENABLE_INT);
    dwt_writesysstatuslo(0xFFFFFFFF);

    /* Print configuration */
    {
        static char cfg_msg[240];
        snprintf(cfg_msg, sizeof(cfg_msg),
                 "PAC4 CONTROL: SYNC_PLEN=%d DATA_PLEN=%d(%dsym) PRE_US=%d SLOT=%dus RX=IMMEDIATE PAC=4 RX_TIMEOUT=%dus PERIOD=%dus CIR=%d",
                 SYNC_PLEN, DATA_PLEN, PREAMBLE_SYMBOLS,
                 PREAMBLE_US, SLOT_INTERVAL_US, CONTROL_RX_TIMEOUT_US,
                 PERIOD_US, ENABLE_CIR);
        test_run_info((unsigned char *)cfg_msg);
    }

#if ENABLE_CIR && CIR_LOG_PER_FRAME
#if CIR_LOG_PER_FRAME_TO_TERMINAL
    test_run_info((unsigned char *)"CIR_CSV_HEADER,rx_seq,cycle,node,plen,fp_sample,peak_idx,accum,rssi_dbm,fp_dbm,rssi_fp_gap_db,fp_peak_power,noise_floor_power,noise_samples,fp_snr_ratio_x1000");
#endif
    cir_log_info("CIR_CSV_HEADER,rx_seq,cycle,node,plen,fp_sample,peak_idx,accum,rssi_dbm,fp_dbm,rssi_fp_gap_db,fp_peak_power,noise_floor_power,noise_samples,fp_snr_ratio_x1000");
#endif
#if ENABLE_CIR
    cir_log_info("CIR_SUMMARY_CSV_HEADER,node,plen,n,fp_snr_ratio_min_x1000,fp_snr_ratio_max_x1000,fp_snr_ratio_avg_x1000,rssi_min_x100,rssi_max_x100,rssi_avg_x100,fp_min_x100,fp_max_x100,fp_avg_x100");
#endif

    dwt_timer_init();

    {
        uint8_t i;
        for (i = 0; i < TOTAL_ARRAY_SIZE; i++) {
            node_latency[i].min_us = 0xFFFFFFFF;
            node_latency[i].max_us = 0;
            node_latency[i].sum_us = 0;
            node_latency[i].count  = 0;
            node_rx_offset[i].min_us = 0xFFFFFFFF;
            node_rx_offset[i].max_us = 0;
            node_rx_offset[i].sum_us = 0;
            node_rx_offset[i].count  = 0;
            node_uwb_rx_offset[i].min_us = 0xFFFFFFFF;
            node_uwb_rx_offset[i].max_us = 0;
            node_uwb_rx_offset[i].sum_us = 0;
            node_uwb_rx_offset[i].count  = 0;
            node_accum[i].min_us = 0xFFFFFFFF;
            node_accum[i].max_us = 0;
            node_accum[i].sum_us = 0;
            node_accum[i].count  = 0;
            node_open_to_rmarker[i].min_us = 0xFFFFFFFF;
            node_open_to_rmarker[i].max_us = 0;
            node_open_to_rmarker[i].sum_us = 0;
            node_open_to_rmarker[i].count  = 0;
#if ENABLE_CIR
            init_signal_stats(&rssi_stats[i]);
            init_signal_stats(&fp_power_stats[i]);
            init_signal_stats(&fp_gap_stats[i]);
            init_ratio_stats(&fp_snr_ratio_stats[i]);
#endif
        }
    }

    int period_count = 0;
    uint32_t last_sync_cycles = 0;
    uint32_t period_interval_cycles = us_to_cpu_cycles(PERIOD_US);
    uint32_t config_switch_time_cycles = us_to_cpu_cycles(CONFIG_SWITCH_US);
    bool config_is_sync = false;

    /* 시작 직후에는 SYNC를 보내지 않고 Normal 노드가 RX 대기 상태에 들어갈 시간을 준다. */
    {
        static char startup_msg[80];
        snprintf(startup_msg, sizeof(startup_msg),
                 "Startup grace: %lu ms before first SYNC",
                 (unsigned long)STARTUP_GRACE_MS);
        test_run_info((unsigned char *)startup_msg);
    }
    Sleep(STARTUP_GRACE_MS);

    /* [NEW] 시작 시 RX 안 켬 (SYNC TX가 첫 동작) */
    measurement_start_cycles = dwt_timer_get_cycles();

    while (1)
    {
        /* ========== [A] SYNC 전송 (Period Timer) ========== */
        if (last_sync_cycles == 0 || dwt_timer_elapsed(last_sync_cycles, period_interval_cycles)) {
            uint32_t current_cycles = dwt_timer_get_cycles();
            last_sync_cycles = current_cycles;

            period_count++;

            /* [A-1] 새 Cycle 시작 */
            if ((period_count - 1) % PERIODS_PER_CYCLE == 0) {
                if (period_count > 1) current_cycle++;
                period_in_cycle = 1;

                tx_state = TX_STATE_FIRST_TX;
                memset(retrans_msg, 0, sizeof(retrans_msg));
                memset(cumulative_ack_confirmed, 0, sizeof(cumulative_ack_confirmed));
                cumulative_ack_count = 0;

                if (current_cycle > 1) {
                    if (data_success_in_current_cycle) data_successful_cycles++;
                    if (!data_success_in_current_cycle) {
                        failed_cycles++;
                        if (failed_cycles <= MAX_FAILED_CYCLES_LOG)
                            failed_cycle_numbers[failed_cycles - 1] = current_cycle - 1;
                    }
                    total_cycles++;
                }
                data_success_in_current_cycle = false;

#if CONTROL_EXPERIMENT == 1 || CONTROL_EXPERIMENT == 2
                if (current_cycle <= TARGET_CYCLES) {
                    uint8_t k;
                    for (k = 1; k < TOTAL_ARRAY_SIZE; k++) {
                        expected_rx[k]++;
                    }
                }
#endif

#if !ENABLE_CIR || CIR_LOG_CYCLE_LINES
                {
                    static char cycle_msg[100];
                    snprintf(cycle_msg, sizeof(cycle_msg), "CYCLE %lu P%d (g%d)",
                            (unsigned long)current_cycle, period_in_cycle, period_count);
                    test_run_info((unsigned char *)cycle_msg);
                }
#endif
            } else {
                period_in_cycle = ((period_count - 1) % PERIODS_PER_CYCLE) + 1;
            }

            /* [A-2] 종료 조건 */
            if (current_cycle > TARGET_CYCLES && !final_stats_printed) {
                final_stats_printed = true;
                static char hdr[80];
                snprintf(hdr, sizeof(hdr), "\n===== PAC4 CONTROL FINAL STATS (PLEN=%d, %d sym) =====",
                         DATA_PLEN, PREAMBLE_SYMBOLS);
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
                    float success_rate = (total_cycles > 0) ? (float)data_successful_cycles / total_cycles * 100 : 0;
                    static char s[120];
                    snprintf(s, sizeof(s), "Cycles: total=%lu success=%lu(%.1f%%) fail=%lu",
                             (unsigned long)total_cycles, (unsigned long)data_successful_cycles, success_rate,
                             (unsigned long)failed_cycles);
                    final_log_info(s);
                }
                {
                    uint32_t elapsed_ms = cycles_to_ms(dwt_timer_get_cycles() - measurement_start_cycles);
                    float throughput_bps = (elapsed_ms > 0) ? (float)total_bytes_received * 8 * 1000 / elapsed_ms : 0;
                    static char s[120];
                    snprintf(s, sizeof(s), "Throughput: %lu bytes in %lums = %.1f bps",
                             (unsigned long)total_bytes_received, (unsigned long)elapsed_ms, throughput_bps);
                    final_log_info(s);
                }
#endif

                final_log_info("--- PER per node ---");
                {
                    uint8_t i;
                    for (i = 1; i < TOTAL_ARRAY_SIZE; i++) {
                        if (expected_rx[i] > 0) {
                            uint32_t miss = (expected_rx[i] > per_stats[i].rx_count) ?
                                            (expected_rx[i] - per_stats[i].rx_count) : 0;
                            float per = (float)miss / expected_rx[i] * 100.0f;
                            static char s[120];
                            snprintf(s, sizeof(s), "%s: rx=%lu expected=%lu miss=%lu PER=%.2f%% err=%lu",
                                     get_slot_description(i),
                                     (unsigned long)per_stats[i].rx_count,
                                     (unsigned long)expected_rx[i],
                                     (unsigned long)miss, per,
                                     (unsigned long)per_stats[i].rx_error_count);
#if ENABLE_CIR
                            cir_log_info(s);
#endif
                            final_log_info(s);
                        }
                    }
                }

                {
                    static char s[200];
                    snprintf(s, sizeof(s), "RX timeouts=%lu (fwto=%lu pto=%lu)  RX errors=%lu (sfdto=%lu phe=%lu fce=%lu fsl=%lu)  RX enable fail=%lu",
                             (unsigned long)total_rx_timeouts,
                             (unsigned long)rx_to_frame,
                             (unsigned long)rx_to_preamble,
                             (unsigned long)total_rx_errors,
                             (unsigned long)rx_err_sfdto,
                             (unsigned long)rx_err_phe,
                             (unsigned long)rx_err_fce,
                             (unsigned long)rx_err_fsl,
                             (unsigned long)total_rx_enable_failures);
                    final_log_info(s);
                }

                /* [DIAG] accumCount: PLEN 대비 결손 = 창 지연 + 검출 고정비용(~21sym) */
                final_log_info("--- Ipatov accumCount (of PLEN) ---");
                {
                    uint8_t i;
                    for (i = 0; i < TOTAL_ARRAY_SIZE; i++) {
                        if (node_accum[i].count > 0) {
                            float avg = (float)node_accum[i].sum_us / node_accum[i].count;
                            static char s[140];
                            snprintf(s, sizeof(s), "%s: min=%lu max=%lu avg=%.1f / plen=%d (n=%lu)",
                                     get_slot_description(i),
                                     (unsigned long)node_accum[i].min_us,
                                     (unsigned long)node_accum[i].max_us,
                                     avg, PREAMBLE_SYMBOLS,
                                     (unsigned long)node_accum[i].count);
                            final_log_info(s);
                        }
                    }
                }

                /* [DIAG] accum 분포: 0이 아닌 빈만 출력 */
                final_log_info("--- accum histogram ---");
                {
                    uint16_t a;
                    for (a = 0; a <= PREAMBLE_SYMBOLS; a++) {
                        if (accum_hist[a] > 0) {
                            static char s[64];
                            snprintf(s, sizeof(s), "accum=%u: n=%lu",
                                     a, (unsigned long)accum_hist[a]);
                            final_log_info(s);
                        }
                    }
                }

                /* [DIAG] 즉시 RX 명령 -> RMARKER. 예약 RX의 lead 값과 직접 비교하지 않는다. */
                final_log_info("--- RX-command to RMARKER (immediate RX control) ---");
                {
                    uint8_t i;
                    for (i = 0; i < TOTAL_ARRAY_SIZE; i++) {
                        if (node_open_to_rmarker[i].count > 0) {
                            float avg = (float)node_open_to_rmarker[i].sum_us / node_open_to_rmarker[i].count;
                            static char s[140];
                            snprintf(s, sizeof(s), "%s: min=%luus max=%luus avg=%.0fus (n=%lu)",
                                     get_slot_description(i),
                                     (unsigned long)node_open_to_rmarker[i].min_us,
                                     (unsigned long)node_open_to_rmarker[i].max_us,
                                     avg, (unsigned long)node_open_to_rmarker[i].count);
                            final_log_info(s);
                        }
                    }
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
#if ENABLE_CIR
                            cir_log_info(s);
#endif
                            final_log_info(s);
                        }
                    }
                }

                final_log_info("--- RX offset from SYNC ---");
                {
                    uint8_t i;
                    for (i = 0; i < TOTAL_ARRAY_SIZE; i++) {
                        if (node_rx_offset[i].count > 0) {
                            float avg_us = (float)node_rx_offset[i].sum_us / node_rx_offset[i].count;
                            static char s[120];
                            snprintf(s, sizeof(s), "%s: min=%luus max=%luus avg=%.0fus (n=%lu)",
                                     get_slot_description(i),
                                     (unsigned long)node_rx_offset[i].min_us,
                                     (unsigned long)node_rx_offset[i].max_us,
                                     avg_us, (unsigned long)node_rx_offset[i].count);
#if ENABLE_CIR
                            cir_log_info(s);
#endif
                            final_log_info(s);
                        }
                    }
                }

                final_log_info("--- UWB RX offset from SYNC TX ---");
                {
                    uint8_t i;
                    for (i = 0; i < TOTAL_ARRAY_SIZE; i++) {
                        if (node_uwb_rx_offset[i].count > 0) {
                            float avg_us = (float)node_uwb_rx_offset[i].sum_us / node_uwb_rx_offset[i].count;
                            static char s[120];
                            snprintf(s, sizeof(s), "%s: min=%luus max=%luus avg=%.0fus (n=%lu)",
                                     get_slot_description(i),
                                     (unsigned long)node_uwb_rx_offset[i].min_us,
                                     (unsigned long)node_uwb_rx_offset[i].max_us,
                                     avg_us, (unsigned long)node_uwb_rx_offset[i].count);
                            final_log_info(s);
                        }
                    }
                }

#if ENABLE_CIR
                test_run_info((unsigned char *)"--- CIR quality ---");
                {
                    uint8_t i;
                    bool cir_has_expected = false;
                    cir_final_pass = true;
                    cir_final_expected = 0;
                    cir_final_rx = 0;
                    cir_final_valid = 0;

                    for (i = 0; i < TOTAL_ARRAY_SIZE; i++) {
                        if (rssi_stats[i].count > 0) {
                            int32_t rssi_avg = (int32_t)(rssi_stats[i].sum_x100 / (int64_t)rssi_stats[i].count);
                            int32_t fp_avg = (int32_t)(fp_power_stats[i].sum_x100 / (int64_t)fp_power_stats[i].count);
                            int32_t gap_avg = (int32_t)(fp_gap_stats[i].sum_x100 / (int64_t)fp_gap_stats[i].count);
                            uint64_t snr_count = fp_snr_ratio_stats[i].count;
                            bool node_pass = (expected_rx[i] == per_stats[i].rx_count &&
                                              expected_rx[i] == snr_count);
                            uint64_t snr_ratio_min = (snr_count > 0) ? fp_snr_ratio_stats[i].min_x1000 : 0;
                            uint64_t snr_ratio_max = (snr_count > 0) ? fp_snr_ratio_stats[i].max_x1000 : 0;
                            uint64_t snr_ratio_avg = (snr_count > 0) ?
                                                     (fp_snr_ratio_stats[i].sum_x1000 / snr_count) : 0;
                            char rssi_min[16], rssi_max[16], rssi_avg_s[16];
                            char fp_min[16], fp_max[16], fp_avg_s[16];
                            char gap_avg_s[16];
                            static char s[320];

                            format_x100(rssi_min, sizeof(rssi_min), rssi_stats[i].min_x100);
                            format_x100(rssi_max, sizeof(rssi_max), rssi_stats[i].max_x100);
                            format_x100(rssi_avg_s, sizeof(rssi_avg_s), rssi_avg);
                            format_x100(fp_min, sizeof(fp_min), fp_power_stats[i].min_x100);
                            format_x100(fp_max, sizeof(fp_max), fp_power_stats[i].max_x100);
                            format_x100(fp_avg_s, sizeof(fp_avg_s), fp_avg);
                            format_x100(gap_avg_s, sizeof(gap_avg_s), gap_avg);

                            snprintf(s, sizeof(s),
                                     "%s: RSSI min=%s max=%s avg=%sdBm | FP min=%s max=%s avg=%sdBm | RSSI-FP avg=%sdB | FP-SNR ratiox1000 min=%llu max=%llu avg=%llu (n=%lu)",
                                     get_slot_description(i),
                                     rssi_min, rssi_max, rssi_avg_s,
                                     fp_min, fp_max, fp_avg_s,
                                     gap_avg_s,
                                     (unsigned long long)snr_ratio_min,
                                     (unsigned long long)snr_ratio_max,
                                     (unsigned long long)snr_ratio_avg,
                                     (unsigned long)snr_count);
                            test_run_info((unsigned char *)s);

                            snprintf(s, sizeof(s),
                                     "CIR_SUMMARY_CSV,%s,%d,%lu,%llu,%llu,%llu,%ld,%ld,%ld,%ld,%ld,%ld",
                                     get_slot_description(i), PREAMBLE_SYMBOLS,
                                     (unsigned long)snr_count,
                                     (unsigned long long)snr_ratio_min,
                                     (unsigned long long)snr_ratio_max,
                                     (unsigned long long)snr_ratio_avg,
                                     (long)rssi_stats[i].min_x100,
                                     (long)rssi_stats[i].max_x100,
                                     (long)rssi_avg,
                                     (long)fp_power_stats[i].min_x100,
                                     (long)fp_power_stats[i].max_x100,
                                     (long)fp_avg);
                            cir_log_info(s);
                            test_run_info((unsigned char *)s);

                            snprintf(s, sizeof(s),
                                     "CIR_RUN_RESULT,%s,%d,expected=%lu,rx=%lu,cir=%lu,status=%s",
                                     get_slot_description(i), PREAMBLE_SYMBOLS,
                                     (unsigned long)expected_rx[i],
                                     (unsigned long)per_stats[i].rx_count,
                                     (unsigned long)snr_count,
                                     node_pass ? "PASS" : "FAIL");
                            cir_log_info(s);
                            test_run_info((unsigned char *)s);

                            if (expected_rx[i] > 0) {
                                cir_has_expected = true;
                                cir_final_expected += expected_rx[i];
                                cir_final_rx += per_stats[i].rx_count;
                                cir_final_valid += (uint32_t)snr_count;
                                if (!node_pass) {
                                    cir_final_pass = false;
                                }
                            }
                        }
                        else if (expected_rx[i] > 0) {
                            static char s[160];
                            snprintf(s, sizeof(s),
                                     "CIR_RUN_RESULT,%s,%d,expected=%lu,rx=%lu,cir=0,status=FAIL",
                                     get_slot_description(i), PREAMBLE_SYMBOLS,
                                     (unsigned long)expected_rx[i],
                                     (unsigned long)per_stats[i].rx_count);
                            cir_log_info(s);
                            test_run_info((unsigned char *)s);
                            cir_has_expected = true;
                            cir_final_expected += expected_rx[i];
                            cir_final_rx += per_stats[i].rx_count;
                            cir_final_pass = false;
                        }
                    }

                    if (!cir_has_expected) {
                        cir_final_pass = false;
                    }
                }
#endif

                final_log_info("===== END STATS =====\n");
#if ENABLE_CIR
                print_exp2_done_marker();
#endif
                dwt_forcetrxoff();
#if ENABLE_CIR && CIR_DUMP_SAMPLES_AT_END
                dump_cir_samples();
#endif
                break;
            }

            /* [A-3] DATA 수신 추적 배열 리셋 */
            if (period_in_cycle % 2 == 1) {
                memset(data_received_from, 0, sizeof(data_received_from));
            }

            /* [NEW] 슬롯 예약 상태 리셋 */
            memset(slots_scheduled, 0, sizeof(slots_scheduled));
            current_rx_slot = 0xFF;

            /* [A-4] SYNC TX */
            {
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    config_is_sync = true;
#if ENABLE_CIR
                    enable_cir_diagnostics();
#endif
                }

                tx_msg[0] = 0xC5;
                tx_msg[IDX_MSG_TYPE] = MSG_TYPE_SYNC;
                tx_msg[IDX_PERIOD_INFO] = period_in_cycle;

                dwt_setrxaftertxdelay(0);
                dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
                dwt_writetxfctrl(sizeof(tx_msg), 0, 0);

                /* RESPONSE_EXPECTED 없이 SYNC만 전송하고 DATA RX는 아래에서 직접 켠다. */
                int sync_result = dwt_starttx(DWT_START_TX_IMMEDIATE);
                if (sync_result == DWT_SUCCESS) {
                    uint32_t tx_status = 0;
                    waitforsysstatus(&tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
                    if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                        /* [NEW] SYNC TX timestamp 획득 - 이후 슬롯 RX 시각 계산의 기준 */
                        last_sync_tx_ts_high32 = dwt_readtxtimestamphi32();
                    }
                } else {
                    test_run_info((unsigned char *)"DBG: SYNC TX FAILED!");
                }

                /* DATA config로 전환 */
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);
                if (dwt_configure(&config_data) == DWT_SUCCESS) {
                    config_is_sync = false;
#if ENABLE_CIR
                    enable_cir_diagnostics();
#else
                    /* [DIAG] CIR 미사용 시에도 accumCount 읽기를 위해 CIA 진단 로깅 활성화 */
                    dwt_configciadiag(DW_CIA_DIAG_LOG_ALL);
#endif
                }
                dwt_setrxaftertxdelay(TX_TO_RX_DELAY_UUS);

                /* 첫 번째 Normal 슬롯 수신 준비
                 * 실험 1/2: DATA 설정 직후 즉시 RX를 켜서 SEQ 2 도착 전부터 청취
                 * 실험 4: INIT도 SEQ 1 슬롯 사용 (별도 처리)
                 */
#if CONTROL_EXPERIMENT == 1 || CONTROL_EXPERIMENT == 2
                /* Normal SEQ 2 도착 전에 RX를 즉시 켜 둔다. */
                if (period_in_cycle == 1 && TOTAL_NODES > 1 && !slots_scheduled[1]) {
                    start_control_rx_slot(1);
                }
#elif CONTROL_EXPERIMENT == 4
                /* 실험 4는 [C] 블록에서 SEQ 1 TX 후 SEQ 2 RX 예약 */
#endif
            }
        }

        /* ========== [B] Config switch (SYNC 준비) ========== */
        if (last_sync_cycles != 0 && !config_is_sync) {
            if (dwt_timer_elapsed(last_sync_cycles, config_switch_time_cycles)) {
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    config_is_sync = true;
#if ENABLE_CIR
                    enable_cir_diagnostics();
#endif
                }
                dwt_writesysstatuslo(0xFFFFFFFF);
                /* [CHANGED] 다음 SYNC TX 대기 - RX 안 켬 */
            }
        }

        /* ========== [C] SEQ 1 슬롯 실행 (실험 4 전용) ========== */
#if CONTROL_EXPERIMENT == 4
        if (last_sync_cycles != 0 && !slots_scheduled[0] && config_is_sync == false) {
            uint32_t seq1_offset_cycles = us_to_cpu_cycles(SYNC_BUFFER_US);
            if (dwt_timer_elapsed(last_sync_cycles, seq1_offset_cycles)) {
                slots_scheduled[0] = true;
                bool should_transmit = false;
                bool is_data_period = (period_in_cycle % 2 == 1);

                if (is_data_period) {
                    if (tx_state == TX_STATE_IDLE || data_success_in_current_cycle) {
                        should_transmit = false;
                        if (period_in_cycle == 1) pair1_idle++;
                        else if (period_in_cycle == 3) pair2_idle++;
                        else if (period_in_cycle == 5) pair3_idle++;
                    } else {
                        should_transmit = true;
                        if (period_in_cycle == 1) {
                            tx_msg[0] = 0xC5;
                            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
                            tx_msg[IDX_SOURCE] = NODE_INIT;
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
                        per_stats[0].tx_count++;
                    }
                } else {
                    should_transmit = true;
                    tx_msg[0] = 0xC5;
                    tx_msg[IDX_MSG_TYPE] = MSG_TYPE_ACK_ARRAY;
                    tx_msg[IDX_SOURCE] = NODE_INIT;
                    tx_msg[IDX_DEST] = NODE_ALL;
                    memcpy(&tx_msg[IDX_ACK_ARRAY], data_received_from, TOTAL_ARRAY_SIZE);
                }

                if (should_transmit) {
                    dwt_forcetrxoff();
                    dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
                    dwt_writetxfctrl(sizeof(tx_msg), 0, 0);
                    int tx_result = dwt_starttx(DWT_START_TX_IMMEDIATE);
                    if (tx_result == DWT_SUCCESS) {
                        uint32_t tx_status = 0;
                        waitforsysstatus(&tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
                        if (tx_status & DWT_INT_TXFRS_BIT_MASK)
                            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
                        tx_msg[DATA_FRAME_SN_IDX]++;
                    }
                }
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);

                /* 다음 슬롯(SEQ 2) 도착 전에 RX를 즉시 켠다. */
                if (TOTAL_NODES > 1 && !slots_scheduled[1]) {
                    start_control_rx_slot(1);
                }
            }
        }
#endif

        /* ========== [D] RX 폴링 ========== */
        {
            uint32_t status_reg = dwt_readsysstatuslo();

            if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
                memset(rx_buffer, 0, sizeof(rx_buffer));
                dwt_readrxdata(rx_buffer, FRAME_LEN_MAX, 0);

                uint8_t msg_type = rx_buffer[IDX_MSG_TYPE];
                uint8_t src_node = rx_buffer[IDX_SOURCE];
                uint8_t src_idx = node_id_to_index(src_node);

                /* [D-1] DATA 수신 */
                if (msg_type == MSG_TYPE_DATA && (period_in_cycle % 2 == 1)) {
                    if (src_idx < TOTAL_ARRAY_SIZE && src_idx != 0) {
                        if (!data_received_from[src_idx]) {
                            data_received_from[src_idx] = 1;
                        }
                        total_bytes_received += sizeof(tx_msg);

                        if (period_in_cycle == 1) {
                            per_stats[src_idx].rx_count++;
                        }

                        {
                            uint32_t rx_ts = (dwt_timer_get_cycles() - last_sync_cycles) / 64;
                            uint32_t rx_ts_high32 = dwt_readrxtimestamphi32();
                            uint32_t uwb_rx_offset_us = dwt_high32_delta_to_us(last_sync_tx_ts_high32,
                                                                                rx_ts_high32);
                            uint32_t tx_ts;
                            update_node_latency(&node_rx_offset[src_idx], rx_ts);
                            update_node_latency(&node_uwb_rx_offset[src_idx], uwb_rx_offset_us);

                            /* [DIAG] 창 열림 -> RMARKER 도착 오프셋 (기대값: preamble+SFD+lead) */
                            update_node_latency(&node_open_to_rmarker[src_idx],
                                                dwt_high32_delta_to_us(last_rx_open_high32, rx_ts_high32));

                            /* [DIAG] Ipatov accumCount (누산된 preamble 심볼 수) */
                            {
                                dwt_cirdiags_t rx_diag;
                                if (dwt_readdiagnostics_acc(&rx_diag, DWT_ACC_IDX_IP_M) == DWT_SUCCESS) {
                                    uint16_t acc = rx_diag.accumCount;
                                    update_node_latency(&node_accum[src_idx], acc);
                                    if (acc > PREAMBLE_SYMBOLS) acc = PREAMBLE_SYMBOLS;
                                    accum_hist[acc]++;
                                }
                            }
#if ENABLE_CIR
                            log_cir_quality(src_idx, per_stats[src_idx].rx_count, current_cycle);
#endif
                            memcpy(&tx_ts, &rx_buffer[IDX_TX_TIMESTAMP], sizeof(uint32_t));
                            if (rx_ts > tx_ts) {
                                update_node_latency(&node_latency[src_idx], rx_ts - tx_ts);
                            }
                        }

                    }
                }

#if CONTROL_EXPERIMENT == 4
                else if (msg_type == MSG_TYPE_ACK_ARRAY && (period_in_cycle % 2 == 0)) {
                    if (src_idx < TOTAL_ARRAY_SIZE && src_idx != 0) {
                        uint8_t *ack_array = &rx_buffer[IDX_ACK_ARRAY];

                        if (ack_array[0] == 1) {
                            if (!cumulative_ack_confirmed[src_idx]) {
                                cumulative_ack_confirmed[src_idx] = 1;
                                cumulative_ack_count++;

                                if (cumulative_ack_count >= expected_acks) {
                                    if (!data_success_in_current_cycle) {
                                        data_success_in_current_cycle = true;
                                        tx_state = TX_STATE_IDLE;
                                        if (period_in_cycle <= 2) pair1_success++;
                                        else if (period_in_cycle <= 4) pair2_success++;
                                        else pair3_success++;
                                    }
                                }
                            }
                        }
                    }
                }
#endif

                /* 수신 후 다음 슬롯이 있다면 RX를 즉시 다시 켠다. */
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);

                if (current_rx_slot != 0xFF && (current_rx_slot + 1) < TOTAL_ARRAY_SIZE) {
                    start_control_rx_slot(current_rx_slot + 1);
                } else {
                    current_rx_slot = 0xFF;
                    /* 다음 SYNC TX 대기 - RX 안 켬 */
                }
            }

                /* RX timeout - 미리 켜 둔 RX에서 패킷을 받지 못함 (PER에 반영) */
            else if (status_reg & SYS_STATUS_ALL_RX_TO) {
                dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO);
                total_rx_timeouts++;
                /* [DIAG] timeout 종류 구분 */
                if (status_reg & DWT_INT_RXFTO_BIT_MASK) rx_to_frame++;
                if (status_reg & DWT_INT_RXPTO_BIT_MASK) rx_to_preamble++;
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);

                /* 타임아웃 후 다음 슬롯이 있으면 즉시 RX 재활성화 */
                if (current_rx_slot != 0xFF && (current_rx_slot + 1) < TOTAL_ARRAY_SIZE) {
                    start_control_rx_slot(current_rx_slot + 1);
                } else {
                    current_rx_slot = 0xFF;
                }
            }

            /* RX errors */
            else if (status_reg & SYS_STATUS_ALL_RX_ERR) {
                total_rx_errors++;
                /* [DIAG] 에러 종류 구분 */
                if (status_reg & DWT_INT_RXSTO_BIT_MASK) rx_err_sfdto++;
                if (status_reg & DWT_INT_RXPHE_BIT_MASK) rx_err_phe++;
                if (status_reg & DWT_INT_RXFCE_BIT_MASK) rx_err_fce++;
                if (status_reg & DWT_INT_RXFSL_BIT_MASK) rx_err_fsl++;
                if (current_rx_slot < TOTAL_ARRAY_SIZE) {
                    per_stats[current_rx_slot].rx_error_count++;
                }
                dwt_writesysstatuslo(SYS_STATUS_ALL_RX_ERR);
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);

                /* 에러 후 다음 슬롯이 있으면 즉시 RX 재활성화 */
                if (current_rx_slot != 0xFF && (current_rx_slot + 1) < TOTAL_ARRAY_SIZE) {
                    start_control_rx_slot(current_rx_slot + 1);
                } else {
                    current_rx_slot = 0xFF;
                }
            }
        }

    } /* end while */

    return 0;
}

#endif /* TEST_PAC4_CONTROL_INIT */
