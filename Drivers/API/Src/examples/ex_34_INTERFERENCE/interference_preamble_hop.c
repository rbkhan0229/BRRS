/*! ----------------------------------------------------------------------------
 *  @file    interference_preamble_hop.c
 *  @brief   Preamble Hopping Interference Source
 *
 *           간섭원: init_aggregated_only_init_n5.c 기반으로 preamble hopping 적용
 *           - 다른 VEHICLE_AA 사용 → 다른 preamble 시퀀스
 *           - SYNC 전송 + 정확한 슬롯 타이밍 (DWT cycle counter 기반)
 *           - 모든 12개 슬롯에서 DATA 전송 (간섭 극대화)
 *
 * @author  Modified for interference testing
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

#if defined(TEST_INTERFERENCE_PREAMBLE_HOP)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "INTERFERENCE PREAMBLE HOP v2.0"

/* ========== TDMA 프로토콜 파라미터 ========== */
#define TOTAL_SLOTS         12      // Total TDMA slots
#define PERIODS_PER_CYCLE   6       // 6 periods per cycle

/* ========== Preamble Code Hopping 설정 (다른 VEHICLE_AA) ========== */
#define PREAMBLE_CODE_COUNT  12

/* Channel 9, PRF 64MHz 유효 Preamble Code */
static const uint8_t VALID_PREAMBLE_CODES[PREAMBLE_CODE_COUNT] = {
    9, 10, 11, 12, 13, 14, 15, 16, 21, 22, 23, 24
};

/* 간섭원 고유 ID - 프로토콜과 다른 값 사용!
 * 프로토콜: VEHICLE_AA = 0x12345678U
 * 간섭원:   VEHICLE_AA = 0xAABBCCDDU → 다른 preamble 시퀀스
 */
#define VEHICLE_AA  0xAABBCCDDU

/* Channel Identifier = AA[31:16] XOR AA[15:0] (BLE CSA#2 spec) */
#define CHANNEL_ID  ((uint16_t)(((VEHICLE_AA) >> 16) ^ ((VEHICLE_AA) & 0xFFFF)))

/* Global Period Counter for Preamble Hopping */
static uint16_t global_period_counter = 0;

/* ========== 메시지 타입 정의 ========== */
#define MSG_TYPE_SYNC      0x01
#define MSG_TYPE_DATA      0x02

/* ========== 메시지 인덱스 정의 ========== */
enum {
  IDX_FTYPE     = 0,
  IDX_SEQ       = 1,
  IDX_SOURCE    = 2,
  IDX_DEST      = 3,
  IDX_MSG_TYPE  = 4,
  IDX_PRIORITY  = 5,
  IDX_PERIOD_INFO = 6,
  IDX_TX_TIMESTAMP = 8,
  /* Preamble Code for Preamble Hopping (SYNC only) */
  IDX_CURRENT_PREAMBLE = 8,
  IDX_NEXT_PREAMBLE = 9
};

/* Frame structure */
static uint8_t tx_msg[] = { 0x41, 0x8C, 0, 0x9A, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 'I', 'N', 'T', 'F', 0, 0, 0, 0, 0 };
#define DATA_FRAME_SN_IDX   2

/* Config for DATA - PLEN64 */
static dwt_config_t config_data = {
    9,                /* Channel number. */
    DWT_PLEN_64,      /* Preamble length */
    DWT_PAC8,
    9,                /* TX preamble code - will be changed */
    9,                /* RX preamble code - will be changed */
    1,
    DWT_BR_6M8,
    DWT_PHRMODE_STD,
    DWT_PHRRATE_STD,
    (64 + 1 + 8 - 8),
    DWT_STS_MODE_OFF,
    DWT_STS_LEN_64,
    DWT_PDOA_M0
};

/* Config for SYNC - PLEN512 */
static dwt_config_t config_sync = {
    9,
    DWT_PLEN_512,
    DWT_PAC8,
    9,                /* TX preamble code - will be changed */
    9,                /* RX preamble code - will be changed */
    1,
    DWT_BR_6M8,
    DWT_PHRMODE_STD,
    DWT_PHRRATE_STD,
    (512 + 1 + 8 - 8),
    DWT_STS_MODE_OFF,
    DWT_STS_LEN_64,
    DWT_PDOA_M0
};

/* TX Power */
#define TX_POWER_INDEX_10dB    40
#define USE_TX_POWER_INDEX  TX_POWER_INDEX_10dB

extern dwt_txconfig_t txconfig_options;

/* ========== DWT 사이클 카운터 함수 ========== */
#define DWT_CTRL     (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT   (*(volatile uint32_t*)0xE0001004)

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
    uint32_t current_cycles = dwt_timer_get_cycles();
    uint32_t elapsed = current_cycles - start_cycles;
    return (elapsed >= duration_cycles);
}

/* ========== BLE CSA#2 Preamble Hopping Functions ========== */

static uint8_t reverse8(uint8_t x) {
    x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1);
    x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2);
    x = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4);
    return x;
}

static uint16_t perm(uint16_t x) {
    uint8_t hi = reverse8((uint8_t)(x >> 8));
    uint8_t lo = reverse8((uint8_t)(x & 0xFF));
    return ((uint16_t)hi << 8) | lo;
}

static uint16_t mam(uint16_t a, uint16_t b) {
    return (uint16_t)((17U * a + b) & 0xFFFF);
}

static uint16_t generate_prn_e(uint16_t counter, uint16_t channel_id) {
    uint16_t x = counter ^ channel_id;
    x = perm(x); x = mam(x, channel_id);
    x = perm(x); x = mam(x, channel_id);
    x = perm(x); x = mam(x, channel_id);
    return x ^ channel_id;
}

static uint8_t get_preamble_code_for_period(uint16_t counter) {
    uint16_t prn_e = generate_prn_e(counter, CHANNEL_ID);
    uint8_t idx = prn_e % PREAMBLE_CODE_COUNT;
    return VALID_PREAMBLE_CODES[idx];
}

/* Set preamble code for both configs */
static void set_preamble_code(uint8_t preamble_code) {
    config_data.txCode = preamble_code;
    config_data.rxCode = preamble_code;
    config_sync.txCode = preamble_code;
    config_sync.rxCode = preamble_code;
}

/**
 * Application entry point.
 */
int interference_preamble_hop(void)
{
    uint32_t dev_id;
    char buf[150];

    /* Display application name */
    test_run_info((unsigned char *)APP_NAME);

    /* Configure SPI rate */
    port_set_dw_ic_spi_fastrate();

    /* Reset DW IC */
    reset_DWIC();
    Sleep(2);

    /* Probe for device driver */
    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    dev_id = dwt_readdevid();

    while (!dwt_checkidlerc()) { };

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        test_run_info((unsigned char *)"INIT FAILED");
        while (1) { };
    }

    /* Initialize DWT cycle counter */
    dwt_timer_init();

    /* Get initial preamble code */
    uint8_t initial_preamble = get_preamble_code_for_period(0);
    set_preamble_code(initial_preamble);

    if (dwt_configure(&config_data))
    {
        test_run_info((unsigned char *)"CONFIG FAILED");
        while (1) { };
    }

    /* Configure TX power */
    {
        power_indexes_t power_indexes = {0};
        tx_adj_res_t linear_results = {0};
        dwt_txconfig_t linear_txconfig;

        power_indexes.input[0] = USE_TX_POWER_INDEX;
        power_indexes.input[1] = USE_TX_POWER_INDEX;
        power_indexes.input[2] = USE_TX_POWER_INDEX;
        power_indexes.input[3] = USE_TX_POWER_INDEX;

        if (dwt_calculate_linear_tx_setting((int)config_data.chan, &power_indexes, &linear_results) == DWT_SUCCESS) {
            linear_txconfig.power = linear_results.tx_frame_cfg.tx_power_setting;
            linear_txconfig.PGcount = txconfig_options.PGcount;
            linear_txconfig.PGdly = txconfig_options.PGdly;
            dwt_configuretxrf(&linear_txconfig);
            dwt_set_pll_config(linear_results.tx_frame_cfg.pll_cfg);
        } else {
            dwt_configuretxrf(&txconfig_options);
        }
    }

    snprintf(buf, sizeof(buf), "VEHICLE_AA: 0x%08lX (different from protocol 0x12345678)", (unsigned long)VEHICLE_AA);
    test_run_info((unsigned char *)buf);
    snprintf(buf, sizeof(buf), "CHANNEL_ID: 0x%04X, Initial Preamble: %d", CHANNEL_ID, initial_preamble);
    test_run_info((unsigned char *)buf);

    /* Timing parameters (same as protocol) */
    uint32_t period_interval_cycles = us_to_cpu_cycles(21200);      // 21.2ms
    uint32_t slot_interval_cycles = us_to_cpu_cycles(1100);          // 1.1ms
    uint32_t config_switch_time_cycles = us_to_cpu_cycles(17200);   // 17.2ms

    /* Slot start times (all 12 slots for interference) */
    uint32_t slot_start_cycles[TOTAL_SLOTS];
    for (int i = 0; i < TOTAL_SLOTS; i++) {
        slot_start_cycles[i] = us_to_cpu_cycles(2000 + i * 1100);  // Start at 2ms, 1.1ms interval
    }

    snprintf(buf, sizeof(buf), "Slot timing: 2.0ms start, 1.1ms interval, all 12 slots active");
    test_run_info((unsigned char *)buf);

    /* State variables */
    uint32_t last_sync_cycles = 0;
    uint32_t period_count = 0;
    uint32_t current_cycle = 1;
    bool config_is_sync = false;
    bool slot_executed[TOTAL_SLOTS] = {false};
    uint32_t total_tx_count = 0;

    /* Wait for sync with protocol */
    test_run_info((unsigned char *)"Waiting 20s to sync with protocol...");
    Sleep(20000);
    test_run_info((unsigned char *)"Starting interference with preamble hopping!");

    while (1)
    {
        /* ========== [A] SYNC 전송 (21.2ms Period) ========== */
        if (last_sync_cycles == 0 || dwt_timer_elapsed(last_sync_cycles, period_interval_cycles)) {
            uint32_t current_cycles = dwt_timer_get_cycles();

            /* Update timing reference FIRST (same as protocol) */
            last_sync_cycles = current_cycles;
            period_count++;

            /* Update cycle */
            if ((period_count - 1) % PERIODS_PER_CYCLE == 0) {
                if (period_count > 1) current_cycle++;
            }

            /* Get preamble codes BEFORE incrementing global_period_counter (same as protocol) */
            uint8_t current_preamble = get_preamble_code_for_period(global_period_counter);
            uint8_t next_preamble = get_preamble_code_for_period(global_period_counter + 1);

            /* Apply preamble code to configs BEFORE dwt_configure (same as protocol) */
            config_data.txCode = current_preamble;
            config_data.rxCode = current_preamble;
            config_sync.txCode = current_preamble;
            config_sync.rxCode = current_preamble;

            /* Reset slot flags */
            for (int i = 0; i < TOTAL_SLOTS; i++) {
                slot_executed[i] = false;
            }

            /* Switch to SYNC config (with preamble applied) */
            dwt_forcetrxoff();
            if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                config_is_sync = true;
            }

            /* Increment global_period_counter AFTER preamble calculation (same as protocol) */
            global_period_counter++;

            /* Prepare SYNC frame */
            tx_msg[0] = 0xC5;
            tx_msg[IDX_MSG_TYPE] = MSG_TYPE_SYNC;
            tx_msg[IDX_SOURCE] = 'I';  // Interference source
            tx_msg[IDX_PERIOD_INFO] = (uint8_t)((period_count - 1) % PERIODS_PER_CYCLE + 1);
            tx_msg[IDX_CURRENT_PREAMBLE] = current_preamble;
            tx_msg[IDX_NEXT_PREAMBLE] = next_preamble;

            dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
            dwt_writetxfctrl(sizeof(tx_msg), 0, 0);

            if (dwt_starttx(DWT_START_TX_IMMEDIATE) == DWT_SUCCESS) {
                waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
                dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
                total_tx_count++;

                /* Switch to DATA config */
                dwt_forcetrxoff();
                if (dwt_configure(&config_data) == DWT_SUCCESS) {
                    config_is_sync = false;
                }
            }

            /* Log every 100 periods */
            if (period_count % 100 == 0) {
                snprintf(buf, sizeof(buf), "Period %lu, Cycle %lu, Preamble %d, TX count %lu",
                        (unsigned long)period_count, (unsigned long)current_cycle,
                        current_preamble, (unsigned long)total_tx_count);
                test_run_info((unsigned char *)buf);
            }

            /* Check termination (2000 cycles = 12000 periods) */
            if (current_cycle > 2000) {
                snprintf(buf, sizeof(buf), "Interference complete! Total TX: %lu, Periods: %lu",
                        (unsigned long)total_tx_count, (unsigned long)period_count);
                test_run_info((unsigned char *)buf);
                break;
            }

            tx_msg[DATA_FRAME_SN_IDX]++;
            continue;
        }

        /* ========== [B] Config Switch (17.2ms) ========== */
        if (last_sync_cycles != 0 && !config_is_sync) {
            if (dwt_timer_elapsed(last_sync_cycles, config_switch_time_cycles)) {
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    config_is_sync = true;
                }
            }
        }

        /* ========== [C] 모든 12개 슬롯에서 DATA 전송 ========== */
        if (last_sync_cycles != 0 && !config_is_sync) {
            for (int slot = 0; slot < TOTAL_SLOTS; slot++) {
                if (!slot_executed[slot] && dwt_timer_elapsed(last_sync_cycles, slot_start_cycles[slot])) {
                    /* Prepare DATA frame */
                    tx_msg[0] = 0x41;
                    tx_msg[IDX_MSG_TYPE] = MSG_TYPE_DATA;
                    tx_msg[IDX_SEQ] = (uint8_t)(slot + 1);  // SEQ 1-12
                    tx_msg[IDX_SOURCE] = 'I';  // Interference source

                    dwt_forcetrxoff();
                    dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
                    dwt_writetxfctrl(sizeof(tx_msg), 0, 0);

                    if (dwt_starttx(DWT_START_TX_IMMEDIATE) == DWT_SUCCESS) {
                        waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
                        total_tx_count++;
                    }

                    slot_executed[slot] = true;
                    tx_msg[DATA_FRAME_SN_IDX]++;
                    break;  // One slot per loop iteration
                }
            }
        }
    }

    return 0;
}

#endif

/*****************************************************************************************************************************************************
 * NOTES:
 *
 * This interference source is based on init_aggregated_only_init_n5.c with preamble hopping added.
 *
 * Key differences from protocol:
 * 1. Different VEHICLE_AA (0xAABBCCDD vs 0x12345678) → Different preamble sequence
 * 2. Transmits in ALL 12 slots (not just SEQ 1, 10, 12) → Maximizes interference
 * 3. No RX logic (interference only)
 *
 * Preamble hopping collision probability: 1/12 per slot when preambles happen to match.
 *
 * Timing is exact using DWT cycle counter:
 * - 21.2ms period with SYNC at start
 * - 1.1ms slot intervals starting at 2.0ms
 * - Config switch at 17.2ms for next SYNC
 *
 ****************************************************************************************************************************************************/
