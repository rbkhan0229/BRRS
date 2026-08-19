/*! ----------------------------------------------------------------------------
 *  @file    brrs_init.c
 *  @brief   BRRS Experiment - INIT (Coordinator) Node
 *
 *           BRRS(Beacon-Rate Reduced Synchronization) 논문 실험용 Initiator 노드.
 *           ex_29a 기반에서 릴레이와 재전송을 제거.
 *           실험 4는 코디네이터 1대와 N2~N8 중 1~7개 센서 슬롯을 지원한다.
 *
 *           [v1.1 변경사항]
 *           - Delayed-RX 적용: 논문 실험 1 요구사항 ("예정된 시각에 RX 윈도우")
 *           - RX 윈도우 동적 축소: preamble 길이에 비례
 *           - SYNC TX timestamp 기반 슬롯 시각 계산
 *
 *           [v1.2 변경사항] (2026-07)
 *           - PREAMBLE_US/SFD_US 올림(ceil) 계산으로 절사 오차 제거
 *           - dwt_setrxtimeout에 US_TO_UUS 단위 변환 적용
 *           - 진단 로깅: timeout(fwto/pto)·에러(sfdto/phe/fce/fsl) 세분화,
 *             Ipatov accumCount, RX-open→RMARKER 오프셋, config에 LEAD/TAIL 출력
 *           - lead margin 특성 실측: logs/lead_sweep_32sym_1m_office_20260703.md
 *
 *           [v1.5 변경사항] (2026-08)
 *           - CPU 기반 pseudo-latency 제거
 *           - SYNC TX/DATA RX RMARKER 기반 signed slot timing error 추가
 *           - DATA timestamp 필드를 예약 슬롯 오프셋으로 명확히 통일
 *           - 실험 1~4의 SYNC 비컨 프리앰블을 256 symbol로 통일
 *
 *           [v1.6 변경사항] (2026-08)
 *           - 연속 DATA 슬롯은 첫 delayed-RX 이후 즉시 재수신하여 host rearm 지연 제거
 *
 *           [v1.7 변경사항] (2026-08)
 *           - 연속 슬롯 RX 재무장을 DW3000 fast command로 단축
 *           - 재무장 SPI 단계별 실행시간 진단 추가
 *
 *           [v1.8 변경사항] (2026-08)
 *           - 연속 슬롯 timeout 재설정 제거, RX fast command를 최우선 실행
 *           - timestamp 읽기와 상태 해제를 RX 재무장 이후로 이동
 *
 *           [v1.9 변경사항] (2026-08)
 *           - 실험 4 DATA 구간을 첫 delayed-RX 이후 continuous RX burst로 수신
 *           - 앞 슬롯 유실 시 DATA 헤더로 실제 슬롯을 복구하여 뒤 슬롯 연쇄 유실 방지
 *           - DATA PHY 설정 실패를 명시적으로 기록하고 해당 superframe 수집 중단
 *           - 실험 1 완료 마커 및 collection/link 상태 분리
 *
 *           [v2.0 변경사항] (2026-08)
 *           - DATA 헤더의 중복 슬롯 오프셋을 제거하고 RX RMARKER로 슬롯 식별
 *           - 마지막 DATA 슬롯 처리 시 RX burst 조기 종료, 유실 시 예약 종료
 *           - beacon protocol v3의 8-byte DATA 헤더와 축소 beacon 레이아웃 적용
 *
 *           [v2.1 변경사항] (2026-08)
 *           - 실험 4 연속 슬롯 RX에 DW3000 manual double buffer 적용
 *           - FINT_STAT 기반 빠른 이벤트 검출과 현재 RDB_STATUS 검증 추가
 *           - RX 재무장/버퍼 수명주기/guard 하한을 fail-closed로 검증
 *
 *           [v2.2 변경사항] (2026-08)
 *           - FINT_STAT 집계에 필요한 RX good/error/timeout/overrun interrupt mask 활성화
 *           - SYS_ENABLE_LO readback을 부팅 시 fail-closed로 검증
 *
 *           [v2.3 변경사항] (2026-08)
 *           - nRF GPIOTE IRQ 활성 상태와 DW3000 IRQ 핀 레벨을 분리
 *           - Exp4 polling 모드의 외부 IRQ 비활성 상태를 부팅 시 fail-closed로 검증
 *           - 처리 버퍼의 RXFR/RXFCG/CIADONE 검증 및 RDB_STATUS W1C 정리 추가
 *
 *           [v2.4 진단 변경사항] (2026-08)
 *           - Exp4 CRC 정상 프레임의 source/observed-slot/RX-buffer 집계 추가
 *           - wrong-slot 프레임의 UWB RX offset 범위 집계 추가
 *
 *           [v2.5 변경사항] (2026-08)
 *           - 다음 슬롯 preamble이 SET RX_TIME을 갱신하기 전에 완료 버퍼 timestamp를 캐시
 *           - END beacon 이후에도 source/observed-slot 진단용 슬롯 구성을 보존
 *           - measured re-arm guard 계산에서 status poll 중복 합산 제거
 *
 *           [v2.7 실험 조건 변경사항] (2026-08)
 *           - 실험 1~4 DATA를 8 B header + 16 B application + 2 B FCS로 통일
 *           - 실험 1~3의 기존 최대 PSDU 127 B 조건을 제출용 26 B 조건으로 교체
 *
 *           [v2.8 변경사항] (2026-08)
 *           - Exp4 double buffer의 완료 버퍼 번호로 RX timestamp/FINFO/header를 명시 판독
 *           - 잘못된 frame length의 슬롯/버퍼/raw value 진단 추가
 *
 *           지원 실험:
 *           - 실험 1: 프리앰블 축소 PER 측정 (DATA_PLEN 변경)
 *           - 실험 2: CIR 수집 (ENABLE_CIR=1)
 *           - 실험 3: TX EXTTXE airtime 차분으로 SFD/PHR 오버헤드 측정
 *           - 실험 4: 고정 10 ms 슈퍼프레임에서 축소 프리앰블 TDMA 처리량 측정
 *
 *           TDMA 구조:
 *           - SYNC: 모든 실험에서 PLEN256 비컨 사용
 *           - DATA: DATA_PLEN (가변, 실험 파라미터)
 */

#include "deca_probe_interface.h"
#include <deca_device_api.h>
#include <deca_private.h>
#include <deca_spi.h>
#include <dw3000_deca_regs.h>
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

#if defined(TEST_BRRS_INIT)

extern void test_run_info(unsigned char *data);
extern int SEGGER_RTT_ConfigUpBuffer(unsigned BufferIndex, const char* sName, void* pBuffer, unsigned BufferSize, unsigned Flags);
extern unsigned SEGGER_RTT_WriteString(unsigned BufferIndex, const char* s);

#define APP_NAME "BRRS INIT NODE v2.10 (beacon-scheduled delayed-RX)"

/* ========== 실험 모드 선택 ========== */
#ifndef BRRS_EXPERIMENT
#define BRRS_EXPERIMENT  3
#endif
#ifndef BRRS_EXPLICIT_PROFILE
#define BRRS_EXPLICIT_PROFILE 1
#endif
#if !BRRS_EXPLICIT_PROFILE
#error "Select an explicit Exp*_Init Build Configuration; generic Debug/Release is disabled"
#endif
#if BRRS_EXPERIMENT < 1 || BRRS_EXPERIMENT > 4
#error "BRRS_EXPERIMENT must be between 1 and 4"
#endif

/* Experiment 3 PHY condition. Both INIT and NORMAL must use the same value.
 * The legacy RX-stage observation is optional because TX EXTTXE differential
 * capture is the primary Experiment 3 timing measurement. */
#define EXP3_VARIANT_A 1
#define EXP3_VARIANT_B 2
#define EXP3_VARIANT_C 3
#ifndef EXP3_PHY_VARIANT
#define EXP3_PHY_VARIANT EXP3_VARIANT_A
#endif
#if EXP3_PHY_VARIANT < EXP3_VARIANT_A || EXP3_PHY_VARIANT > EXP3_VARIANT_C
#error "EXP3_PHY_VARIANT must be EXP3_VARIANT_A, B, or C"
#endif
#define EXP3_RX_STAGE_DIAG 0

/* ========== BRRS 실험 파라미터 ========== */
#ifndef BRRS_DATA_PLEN
#define BRRS_DATA_PLEN  DWT_PLEN_32
#endif
#define DATA_PLEN       BRRS_DATA_PLEN
#define SYNC_PLEN       DWT_PLEN_256
#define SYNC_PREAMBLE_SYMBOLS 256

/* Experiment 2 is the CIR acquisition run. */
#define ENABLE_CIR      (BRRS_EXPERIMENT == 2)

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

/* 실험 3 CSV는 측정 종료 후 RTT channel 1로 일괄 출력한다. */
#define EXP3_SAMPLE_DUMP_DELAY_MS 2

/* 측정 사이클 수 */
#ifndef BRRS_TARGET_CYCLES
#define BRRS_TARGET_CYCLES 1000
#endif
#define TARGET_CYCLES   BRRS_TARGET_CYCLES

/* Debugger로 두 보드를 번갈아 시작할 때 Normal 노드가 SYNC RX에 들어갈 여유 시간 */
#define STARTUP_GRACE_MS 10000

/* ========== TDMA 프로토콜 파라미터 ========== */
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
#define BRRS_CONFIGURED_SENSOR_MASK ((1U << BRRS_SENSOR_NODES) - 1U)
#ifndef BRRS_ACTIVE_NODE_BITMAP
#define BRRS_ACTIVE_NODE_BITMAP BRRS_CONFIGURED_SENSOR_MASK
#endif
#if (BRRS_ACTIVE_NODE_BITMAP & ~BRRS_CONFIGURED_SENSOR_MASK) != 0
#error "BRRS_ACTIVE_NODE_BITMAP selects a node outside BRRS_SENSOR_NODES"
#endif
#if BRRS_ACTIVE_NODE_BITMAP == 0
#error "BRRS_ACTIVE_NODE_BITMAP must select at least one sensor"
#endif
#define BRRS_ACTIVE_SENSOR_COUNT \
    ((((BRRS_ACTIVE_NODE_BITMAP) >> 0) & 1U) + \
     (((BRRS_ACTIVE_NODE_BITMAP) >> 1) & 1U) + \
     (((BRRS_ACTIVE_NODE_BITMAP) >> 2) & 1U) + \
     (((BRRS_ACTIVE_NODE_BITMAP) >> 3) & 1U) + \
     (((BRRS_ACTIVE_NODE_BITMAP) >> 4) & 1U) + \
     (((BRRS_ACTIVE_NODE_BITMAP) >> 5) & 1U) + \
     (((BRRS_ACTIVE_NODE_BITMAP) >> 6) & 1U))
#ifndef BRRS_EXP4_SLOT_REPEATS
#define BRRS_EXP4_SLOT_REPEATS 1
#endif
#if BRRS_EXP4_SLOT_REPEATS < 1
#error "BRRS_EXP4_SLOT_REPEATS must be at least 1"
#endif
#define BRRS_EXP4_DATA_SLOT_COUNT \
    (BRRS_ACTIVE_SENSOR_COUNT * BRRS_EXP4_SLOT_REPEATS)
_Static_assert(BRRS_EXP4_DATA_SLOT_COUNT <= BRRS_MAX_DATA_SLOTS,
               "Exp4 slot schedule exceeds the beacon slot-owner capacity");
#define TOTAL_NODES         (BRRS_SENSOR_NODES + 1)
#define TOTAL_SLOTS         BRRS_SENSOR_NODES
#define TOTAL_ARRAY_SIZE    TOTAL_NODES
#endif
/* All experiments use one beacon and one DATA schedule per superframe. */
#define PERIODS_PER_CYCLE   1
#define MY_NODE_SEQ         1

/* 슬롯 타이밍 (마이크로초) */
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

/* DW3000 datasheet timing model (PRF64, 6.81 Mbps). */
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

#define PREAMBLE_US         ((int)NS_TO_US_CEIL(PREAMBLE_MODEL_NS))
#define SFD_US              ((int)NS_TO_US_CEIL(SFD_MODEL_NS))
#define PHR_PSDU_US         ((int)NS_TO_US_CEIL(DP_MODEL_NS))
#define BEACON_PHR_PSDU_US  ((int)NS_TO_US_CEIL(BEACON_DP_MODEL_NS))
#define SYNC_RMARKER_OFFSET_US ((int)NS_TO_US_CEIL(SYNC_PREAMBLE_MODEL_NS + SYNC_SFD_MODEL_NS))
#define SLOT_INTERVAL_US    (PREAMBLE_US + SFD_US + PHR_PSDU_US + SLOT_GUARD_US)

/* ========== Delayed-RX 윈도우 계산 ==========
 * RMARKER는 SFD 이후 기준점이므로 RX_EARLY_US는 preamble+SFD와 앞쪽 마진을 포함한다.
 * RX_WINDOW_US는 RX ON부터 RMARKER 이후 PHR/PSDU 수신과 뒤쪽 마진까지의 전체 시간이다.
 */
#ifndef BRRS_RX_LEAD_MARGIN_US
#define BRRS_RX_LEAD_MARGIN_US 15
#endif
#ifndef BRRS_RX_TAIL_MARGIN_US
#define BRRS_RX_TAIL_MARGIN_US 0
#endif
#define RX_LEAD_MARGIN_US   BRRS_RX_LEAD_MARGIN_US
#define RX_TAIL_MARGIN_US   BRRS_RX_TAIL_MARGIN_US
#define RX_EARLY_US         (PREAMBLE_US + SFD_US + RX_LEAD_MARGIN_US)
#define RX_WINDOW_US        (RX_EARLY_US + PHR_PSDU_US + RX_TAIL_MARGIN_US)
#define BRRS_END_REPEAT_COUNT 3
#define BRRS_END_REPEAT_DELAY_MS 5
#if BRRS_EXPERIMENT == 4
_Static_assert(BRRS_SENSOR_NODES == 1 || SLOT_GUARD_US >= RX_LEAD_MARGIN_US,
               "Multi-slot Exp4 guard must not be shorter than the RX lead margin");
#endif

/* Common fixed superframe timing for Experiments 1-4. */
#ifndef BRRS_SUPERFRAME_US
#define BRRS_SUPERFRAME_US  10000
#endif
#if BRRS_EXPERIMENT == 4
#ifndef BRRS_EXP4_SYNC_PREP_US
/* Reserve the measured DATA->SYNC PHY transition and delayed-TX arm time. */
#define BRRS_EXP4_SYNC_PREP_US 2500
#endif
#define EXP4_SYNC_PREP_US   BRRS_EXP4_SYNC_PREP_US
#define EXP4_TX_WAIT_TIMEOUT_US 3000
/* CIADONE can settle some time after RXFCG/RXFR on the just-completed
 * buffer. This read happens after the next slot's RX is already re-armed
 * (exp4_rearm_after_event), so waiting here does not touch the guard
 * budget -- it only avoids failing a good frame that was polled a moment
 * too early. Bounded by elapsed time, not iteration count, since SPI read
 * latency alone was not enough headroom in practice. */
#define EXP4_RDB_CIADONE_RETRY_TIMEOUT_US 300
#define EXP4_END_REPEAT_COUNT BRRS_END_REPEAT_COUNT
#define EXP4_END_REPEAT_DELAY_MS BRRS_END_REPEAT_DELAY_MS
#define CONFIG_SWITCH_US    (BRRS_SUPERFRAME_US - EXP4_SYNC_PREP_US)
#define PERIOD_US           BRRS_SUPERFRAME_US
#define EXP4_SLOT_BUDGET_US (CONFIG_SWITCH_US - SYNC_BUFFER_US)
_Static_assert(EXP4_SLOT_BUDGET_US > PHR_PSDU_US + SLOT_GUARD_US,
               "Exp4 DATA budget cannot fit even one guarded slot");
#define EXP4_TIMING_MAX_DATA_SLOTS \
    (1 + (EXP4_SLOT_BUDGET_US - PHR_PSDU_US - SLOT_GUARD_US) / \
         SLOT_INTERVAL_US)
#define EXP4_MAX_DATA_SLOTS \
    ((EXP4_TIMING_MAX_DATA_SLOTS < BRRS_MAX_DATA_SLOTS) ? \
     EXP4_TIMING_MAX_DATA_SLOTS : BRRS_MAX_DATA_SLOTS)
#define EXP4_DATA_BURST_END_US \
    (SYNC_BUFFER_US + \
     (BRRS_EXP4_DATA_SLOT_COUNT - 1U) * SLOT_INTERVAL_US + \
     PHR_PSDU_US + SLOT_GUARD_US)
_Static_assert(BRRS_EXP4_DATA_SLOT_COUNT > 0,
               "Exp4 requires at least one DATA slot");
_Static_assert(BRRS_EXP4_DATA_SLOT_COUNT <= EXP4_MAX_DATA_SLOTS,
               "Configured Exp4 DATA slots do not fit in the superframe");
_Static_assert(EXP4_DATA_BURST_END_US <= CONFIG_SWITCH_US,
               "Exp4 DATA burst overlaps the next SYNC preparation interval");
#else
#define CONFIG_SWITCH_US    (SYNC_BUFFER_US + TOTAL_SLOTS * SLOT_INTERVAL_US + 2000)
#define PERIOD_US           BRRS_SUPERFRAME_US
_Static_assert(CONFIG_SWITCH_US < BRRS_SUPERFRAME_US,
               "DATA schedule does not fit in the common superframe");
#endif

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

/* Default communication configuration for DATA */
static dwt_config_t config_data = {
    9, DATA_PLEN, DWT_PAC8,
    9, 9, DATA_SFD_TYPE,
    DWT_BR_6M8, DWT_PHRMODE_STD, DATA_PHR_RATE,
    (PREAMBLE_SYMBOLS + 1 + SFD_SYMBOLS - 8),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};

static dwt_config_t config_sync = {
    9, SYNC_PLEN, DWT_PAC8,
    10, 10, 1,
    DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    (SYNC_PREAMBLE_SYMBOLS + 1 + 8 - 8),
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
#define MSG_TYPE_END        0x03

/* ========== 메시지 인덱스 정의 ========== */
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
static uint8_t beacon_msg[BRRS_BEACON_PSDU_BYTES];

/* ex_29a 잔재. RESPONSE_EXPECTED 플래그 없이 TX하므로 rx-after-tx 자동 RX는
 * 발동하지 않음 (모든 RX는 schedule_delayed_rx()로 명시 제어). 무해하여 유지. */
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
static uint32_t expected_rx[TOTAL_ARRAY_SIZE] = {0};
static uint32_t total_rx_errors = 0;
static uint32_t total_rx_timeouts = 0;
static uint32_t total_rx_delayed_fallbacks = 0;
static uint32_t data_config_errors = 0;
static uint32_t run_end_tx_count = 0;

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
static bool cir_final_collection_pass = false;
static bool cir_final_link_pass = false;
static uint32_t cir_final_expected = 0;
static uint32_t cir_final_rx = 0;
static uint32_t cir_final_valid = 0;

static uint32_t cir_final_per_x1000(void)
{
    uint32_t missed = (cir_final_expected >= cir_final_rx) ?
                      (cir_final_expected - cir_final_rx) : 0U;

    return (cir_final_expected > 0U) ?
           (uint32_t)(((uint64_t)missed * 100000ULL +
                       cir_final_expected / 2U) / cir_final_expected) : 0U;
}

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
             "CIR_DUMP_START,plen=%d,expected=%lu,rx=%lu,valid_cir=%lu,dump_count=%lu,collection=%s,link=%s,per_x1000=%lu,status=%s",
             PREAMBLE_SYMBOLS,
             (unsigned long)cir_final_expected,
             (unsigned long)cir_final_rx,
             (unsigned long)cir_final_valid,
             (unsigned long)cir_sample_log_count,
             cir_final_collection_pass ? "PASS" : "FAIL",
             cir_final_link_pass ? "PASS" : "LOSS",
             (unsigned long)cir_final_per_x1000(),
             cir_final_collection_pass ? "PASS" : "FAIL");
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
             "CIR_DUMP_DONE,plen=%d,expected=%lu,rx=%lu,valid_cir=%lu,dump_count=%lu,collection=%s,link=%s,per_x1000=%lu,status=%s",
             PREAMBLE_SYMBOLS,
             (unsigned long)cir_final_expected,
             (unsigned long)cir_final_rx,
             (unsigned long)cir_final_valid,
             (unsigned long)cir_sample_log_count,
             cir_final_collection_pass ? "PASS" : "FAIL",
             cir_final_link_pass ? "PASS" : "LOSS",
             (unsigned long)cir_final_per_x1000(),
             cir_final_collection_pass ? "PASS" : "FAIL");
    cir_log_info(csv_line);
    test_run_info((unsigned char *)csv_line);

    snprintf(csv_line, sizeof(csv_line),
             "EXP2_DONE,plen=%d,expected=%lu,rx=%lu,valid_cir=%lu,dump_count=%lu,end_tx=%lu,collection=%s,link=%s,per_x1000=%lu,status=%s",
             PREAMBLE_SYMBOLS,
             (unsigned long)cir_final_expected,
             (unsigned long)cir_final_rx,
             (unsigned long)cir_final_valid,
             (unsigned long)cir_sample_log_count,
             (unsigned long)run_end_tx_count,
             cir_final_collection_pass ? "PASS" : "FAIL",
             cir_final_link_pass ? "PASS" : "LOSS",
             (unsigned long)cir_final_per_x1000(),
             cir_final_collection_pass ? "PASS" : "FAIL");
    cir_log_info(csv_line);
    test_run_info((unsigned char *)csv_line);
}

static void print_exp2_done_marker(void)
{
    static char csv_line[260];

    snprintf(csv_line, sizeof(csv_line),
             "EXP2_DONE,plen=%d,expected=%lu,rx=%lu,valid_cir=%lu,dump_count=%lu,end_tx=%lu,collection=%s,link=%s,per_x1000=%lu,status=%s",
             PREAMBLE_SYMBOLS,
             (unsigned long)cir_final_expected,
             (unsigned long)cir_final_rx,
             (unsigned long)cir_final_valid,
             (unsigned long)cir_sample_log_count,
             (unsigned long)run_end_tx_count,
             cir_final_collection_pass ? "PASS" : "FAIL",
             cir_final_link_pass ? "PASS" : "LOSS",
             (unsigned long)cir_final_per_x1000(),
             cir_final_collection_pass ? "PASS" : "FAIL");
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
    cir_log_info(line);
}

#if !ENABLE_CIR
static void terminal_log_info(unsigned char *data)
{
    test_run_info(data);
    cir_log_info((const char *)data);
}

#define test_run_info(data) terminal_log_info(data)
#endif

#if BRRS_EXPERIMENT == 3 && EXP3_RX_STAGE_DIAG
enum {
    EXP3_OBS_RXPRD = 1U << 0,
    EXP3_OBS_RXSFDD = 1U << 1,
    EXP3_OBS_RXPHD = 1U << 2,
    EXP3_OBS_RXFR = 1U << 3
};

typedef struct {
    bool active;
    uint8_t obs_mask;
    uint32_t cycle_no;
    uint32_t rx_open_high32;
    uint32_t rxprd_obs_cpu;
    uint32_t rxsfdd_obs_cpu;
    uint32_t rxphd_obs_cpu;
    uint32_t rxfr_obs_cpu;
    uint32_t rxprd_poll_cycles;
    uint32_t rxsfdd_poll_cycles;
    uint32_t rxphd_poll_cycles;
    uint32_t rxfr_poll_cycles;
} exp3_trace_t;

typedef struct {
    uint32_t rx_seq;
    uint32_t cycle_no;
    uint8_t src_idx;
    uint8_t obs_mask;
    uint32_t rx_open_high32;
    uint32_t rmarker_high32;
    uint32_t anchor_sys_high32;
    uint32_t anchor_cpu;
    uint32_t anchor_read_cycles;
    uint32_t rxprd_obs_cpu;
    uint32_t rxsfdd_obs_cpu;
    uint32_t rxphd_obs_cpu;
    uint32_t rxfr_obs_cpu;
    uint32_t rxprd_poll_cycles;
    uint32_t rxsfdd_poll_cycles;
    uint32_t rxphd_poll_cycles;
    uint32_t rxfr_poll_cycles;
} exp3_sample_t;

typedef struct {
    int64_t min_ns;
    int64_t max_ns;
    int64_t sum_ns;
    uint32_t count;
} exp3_metric_stats_t;

static exp3_trace_t exp3_trace;
static exp3_sample_t exp3_samples[TARGET_CYCLES];
static uint32_t exp3_sample_count = 0;
static bool exp3_final_pass = false;
static uint32_t exp3_final_expected = 0;
static uint32_t exp3_final_rx = 0;
#endif

/* ========== 처리량 측정 ========== */
#if BRRS_EXPERIMENT == 4
static uint64_t exp4_payload_bytes_received = 0;
static uint32_t exp4_frames_received = 0;
static uint32_t exp4_wrong_slot_frames = 0;
static uint32_t exp4_wrong_superframe_frames = 0;
static uint32_t exp4_sync_tx_delayed_late = 0;
static uint32_t exp4_tx_wait_timeouts = 0;
static uint32_t exp4_measurement_start_high32 = 0;
static uint32_t exp4_measurement_end_high32 = 0;
static uint32_t exp4_next_sync_tx_high32 = 0;
static uint32_t exp4_end_tx_count = 0;
static uint32_t exp4_period_min_x1000_us = 0xFFFFFFFF;
static uint32_t exp4_period_max_x1000_us = 0;
static uint64_t exp4_period_sum_x1000_us = 0;
static uint32_t exp4_period_count = 0;
static bool exp4_data_burst_active = false;
static uint32_t exp4_burst_early_close_count = 0;
static uint32_t exp4_burst_deadline_close_count = 0;
static uint32_t exp4_burst_forced_prep_close_count = 0;
static uint32_t exp4_wrong_length_frames = 0;
static uint32_t exp4_rx_good_events = 0;
static uint32_t exp4_rx_buffer_overruns = 0;
static uint32_t exp4_rdb_good_events = 0;
static uint32_t exp4_rdb_dispatches = 0;
static uint32_t exp4_rdb_host_mismatches = 0;
static uint32_t exp4_rdb_incomplete_events = 0;
static uint32_t exp4_rdb_incomplete_recovered = 0;
static uint32_t exp4_rdb_incomplete_ciaerr = 0;
static uint32_t exp4_rdb_resync_count = 0;
static uint8_t exp4_rx_host_buffer = 0;
static uint32_t exp4_wrong_length_by_slot[BRRS_MAX_DATA_SLOTS + 1U] = {0};
static uint32_t exp4_wrong_length_by_buffer[2] = {0};
static uint32_t exp4_wrong_length_hist[128] = {0};
#endif

/* ========== 노드별 UWB 타이밍 및 진단 통계 ========== */
typedef struct {
    uint32_t min_us;
    uint32_t max_us;
    uint64_t sum_us;
    uint32_t count;
} latency_stats_t;

static latency_stats_t node_uwb_rx_offset[TOTAL_ARRAY_SIZE];

typedef struct {
    int64_t min_ns;
    int64_t max_ns;
    int64_t sum_ns;
    uint32_t count;
} signed_timing_stats_t;

/* DATA RX RMARKER의 실제 슬롯 도착 시각과 비컨 예약 시각의 차이.
 * 같은 DW3000 40-bit clock domain에서 계산하며 음수(early)를 보존한다. */
static signed_timing_stats_t node_slot_timing_error[TOTAL_ARRAY_SIZE];

/* [DIAG] Ipatov accumCount: CIR 누산에 실제 들어간 preamble 심볼 수.
 * PLEN - accumCount는 누산되지 않은 구간의 관측값이며 RX 기동, 획득,
 * PAC 양자화가 함께 반영될 수 있다. 약 21-symbol 차이는 실측 경향이지
 * 제조사 알고리즘의 고정 비용으로 단정하지 않는다. */
static latency_stats_t node_accum[TOTAL_ARRAY_SIZE];
/* [DIAG] 창 열림(programmed) 시각 -> RMARKER 도착까지의 오프셋(us).
 * 기대값 = 실제 preamble+SFD 길이 + lead margin. 스케줄링 모델 오차 검증용. */
static latency_stats_t node_open_to_rmarker[TOTAL_ARRAY_SIZE];
#if BRRS_EXPERIMENT == 4
static latency_stats_t exp4_status_poll_stats;
static latency_stats_t exp4_rearm_service_stats;
static latency_stats_t exp4_rearm_rdb_status_stats;
static latency_stats_t exp4_rdb_incomplete_retry_stats;
static latency_stats_t exp4_rearm_rx_metadata_stats;
static latency_stats_t exp4_rearm_header_read_stats;
static latency_stats_t exp4_rearm_status_clear_pre_stats;
static latency_stats_t exp4_rearm_status_clear_post_stats;
static latency_stats_t exp4_rearm_rx_enable_stats;
static latency_stats_t exp4_sync_prep_stats;
static latency_stats_t exp4_rx_buffer_free_stats;
#define EXP4_SLOT_CLASS_INVALID BRRS_MAX_DATA_SLOTS
static uint32_t exp4_slot_class_count[TOTAL_ARRAY_SIZE]
                                      [BRRS_MAX_DATA_SLOTS + 1U];
static uint32_t exp4_slot_class_host_count[TOTAL_ARRAY_SIZE]
                                           [BRRS_MAX_DATA_SLOTS + 1U][2];
static latency_stats_t exp4_wrong_slot_rx_offset[TOTAL_ARRAY_SIZE];
static uint8_t exp4_diag_slot_count = 0U;
static uint8_t exp4_diag_slot_owner[BRRS_MAX_DATA_SLOTS];
#endif

/* [DIAG] accumCount 히스토그램: min/max/avg만으로는 분포 모양(양봉 vs 연속)을
 * 구분할 수 없어 값별 빈도를 직접 기록. PAC 양자화 가설 검증용. */
static uint32_t accum_hist[PREAMBLE_SYMBOLS + 1] = {0};

#if BRRS_EXPERIMENT != 4
/* Failed receptions may leave CIA diagnostics from an earlier frame.  Keep
 * values without RXPRD as raw observations, never as valid accumCount data. */
typedef enum {
    FAIL_ACCUM_FWTO = 0,
    FAIL_ACCUM_PTO,
    FAIL_ACCUM_SFDTO,
    FAIL_ACCUM_PHE,
    FAIL_ACCUM_FCE,
    FAIL_ACCUM_FSL,
    FAIL_ACCUM_OTHER,
    FAIL_ACCUM_CAUSE_COUNT
} fail_accum_cause_t;

#define FAIL_ACCUM_HIST_CAPACITY 16

typedef struct {
    uint16_t accum;
    uint32_t count;
} failed_accum_hist_entry_t;

typedef struct {
    uint32_t events;
    uint32_t diag_read_ok;
    uint32_t valid;
    uint32_t invalid_no_rxprd;
    uint32_t invalid_zero;
    uint32_t invalid_range;
    uint32_t read_fail;
    uint32_t valid_hist_overflow;
    uint32_t invalid_hist_overflow;
    uint8_t valid_hist_used;
    uint8_t invalid_hist_used;
    failed_accum_hist_entry_t valid_hist[FAIL_ACCUM_HIST_CAPACITY];
    failed_accum_hist_entry_t invalid_raw_hist[FAIL_ACCUM_HIST_CAPACITY];
} failed_accum_stats_t;

static failed_accum_stats_t failed_accum_stats[TOTAL_ARRAY_SIZE][FAIL_ACCUM_CAUSE_COUNT];
#endif

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

static int64_t dwt_slot_error_to_ns(uint32_t actual_offset_high32,
                                    uint32_t expected_offset_high32)
{
    int32_t error_high32 =
        (int32_t)(actual_offset_high32 - expected_offset_high32);
    int64_t numerator = (int64_t)error_high32 * 256000LL;

    if (numerator >= 0) {
        return (numerator + (int64_t)DWT_TIME_UNITS_PER_US / 2) /
               (int64_t)DWT_TIME_UNITS_PER_US;
    }
    return (numerator - (int64_t)DWT_TIME_UNITS_PER_US / 2) /
           (int64_t)DWT_TIME_UNITS_PER_US;
}

#if BRRS_EXPERIMENT == 3 && EXP3_RX_STAGE_DIAG
static int64_t exp3_high32_delta_to_ns(uint32_t start_high32, uint32_t end_high32)
{
    int32_t delta_high32 = (int32_t)(end_high32 - start_high32);
    int64_t numerator = (int64_t)delta_high32 * 256000LL;

    if (numerator >= 0) {
        return (numerator + (int64_t)DWT_TIME_UNITS_PER_US / 2) /
               (int64_t)DWT_TIME_UNITS_PER_US;
    }
    return (numerator - (int64_t)DWT_TIME_UNITS_PER_US / 2) /
           (int64_t)DWT_TIME_UNITS_PER_US;
}

static int64_t exp3_cpu_cycles_to_ns(uint32_t cycles)
{
    return ((int64_t)cycles * 1000000000LL + CPU_FREQ_HZ / 2) / CPU_FREQ_HZ;
}

static int64_t exp3_observation_from_rmarker_ns(const exp3_sample_t *entry,
                                                 uint32_t observation_cpu)
{
    int64_t anchor_from_rmarker_ns =
        exp3_high32_delta_to_ns(entry->rmarker_high32, entry->anchor_sys_high32);
    uint32_t observation_to_anchor_cycles = entry->anchor_cpu - observation_cpu;

    return anchor_from_rmarker_ns -
           exp3_cpu_cycles_to_ns(observation_to_anchor_cycles);
}

static void exp3_trace_begin(uint32_t cycle_no, uint32_t rx_open_high32)
{
    memset(&exp3_trace, 0, sizeof(exp3_trace));
    exp3_trace.active = true;
    exp3_trace.cycle_no = cycle_no;
    exp3_trace.rx_open_high32 = rx_open_high32;
}

static void exp3_trace_cancel(void)
{
    exp3_trace.active = false;
}

/* RXPRD/RXSFDD/RXPHD/RXFR에는 전용 하드웨어 timestamp 레지스터가 없다.
 * 상태를 읽은 SPI transaction의 MCU-cycle 중간점을 최초 관측 시각으로 저장한다.
 * 수신 도중 SYS_TIME을 읽지 않아 계측 SPI가 다음 수신 단계를 지연시키지 않게 한다. */
static void exp3_capture_status(uint32_t status_reg,
                                uint32_t poll_start_cpu,
                                uint32_t poll_end_cpu)
{
    uint8_t new_mask = 0;
    uint32_t poll_cycles;
    uint32_t obs_cpu;

    if (!exp3_trace.active) {
        return;
    }

    if ((status_reg & DWT_INT_RXPRD_BIT_MASK) &&
        !(exp3_trace.obs_mask & EXP3_OBS_RXPRD)) {
        new_mask |= EXP3_OBS_RXPRD;
    }
    if ((status_reg & DWT_INT_RXSFDD_BIT_MASK) &&
        !(exp3_trace.obs_mask & EXP3_OBS_RXSFDD)) {
        new_mask |= EXP3_OBS_RXSFDD;
    }
    if ((status_reg & DWT_INT_RXPHD_BIT_MASK) &&
        !(exp3_trace.obs_mask & EXP3_OBS_RXPHD)) {
        new_mask |= EXP3_OBS_RXPHD;
    }
    if ((status_reg & (DWT_INT_RXFR_BIT_MASK | DWT_INT_RXFCG_BIT_MASK)) &&
        !(exp3_trace.obs_mask & EXP3_OBS_RXFR)) {
        new_mask |= EXP3_OBS_RXFR;
    }

    if (new_mask == 0) {
        return;
    }

    poll_cycles = poll_end_cpu - poll_start_cpu;
    obs_cpu = poll_start_cpu + poll_cycles / 2U;

    if (new_mask & EXP3_OBS_RXPRD) {
        exp3_trace.rxprd_obs_cpu = obs_cpu;
        exp3_trace.rxprd_poll_cycles = poll_cycles;
    }
    if (new_mask & EXP3_OBS_RXSFDD) {
        exp3_trace.rxsfdd_obs_cpu = obs_cpu;
        exp3_trace.rxsfdd_poll_cycles = poll_cycles;
    }
    if (new_mask & EXP3_OBS_RXPHD) {
        exp3_trace.rxphd_obs_cpu = obs_cpu;
        exp3_trace.rxphd_poll_cycles = poll_cycles;
    }
    if (new_mask & EXP3_OBS_RXFR) {
        exp3_trace.rxfr_obs_cpu = obs_cpu;
        exp3_trace.rxfr_poll_cycles = poll_cycles;
    }
    exp3_trace.obs_mask |= new_mask;
}

static void exp3_store_success(uint8_t src_idx, uint32_t rx_seq, uint32_t rmarker_high32)
{
    exp3_sample_t *entry;
    uint32_t anchor_read_start;
    uint32_t anchor_read_end;
    uint32_t anchor_read_cycles;
    uint32_t anchor_sys_high32;

    if (!exp3_trace.active || exp3_sample_count >= TARGET_CYCLES) {
        exp3_trace_cancel();
        return;
    }

    /* SYS_TIME read latch를 안전한 W1C=0 write로 해제한 뒤 한 번만 읽는다.
     * read transaction의 MCU-cycle 중간점을 UWB/MCU clock 정렬점으로 사용한다. */
    dwt_writesysstatuslo(0U);
    anchor_read_start = dwt_timer_get_cycles();
    anchor_sys_high32 = dwt_readsystimestamphi32();
    anchor_read_end = dwt_timer_get_cycles();
    anchor_read_cycles = anchor_read_end - anchor_read_start;

    entry = &exp3_samples[exp3_sample_count++];
    entry->rx_seq = rx_seq;
    entry->cycle_no = exp3_trace.cycle_no;
    entry->src_idx = src_idx;
    entry->obs_mask = exp3_trace.obs_mask;
    entry->rx_open_high32 = exp3_trace.rx_open_high32;
    entry->rmarker_high32 = rmarker_high32;
    entry->anchor_sys_high32 = anchor_sys_high32;
    entry->anchor_cpu = anchor_read_start + anchor_read_cycles / 2U;
    entry->anchor_read_cycles = anchor_read_cycles;
    entry->rxprd_obs_cpu = exp3_trace.rxprd_obs_cpu;
    entry->rxsfdd_obs_cpu = exp3_trace.rxsfdd_obs_cpu;
    entry->rxphd_obs_cpu = exp3_trace.rxphd_obs_cpu;
    entry->rxfr_obs_cpu = exp3_trace.rxfr_obs_cpu;
    entry->rxprd_poll_cycles = exp3_trace.rxprd_poll_cycles;
    entry->rxsfdd_poll_cycles = exp3_trace.rxsfdd_poll_cycles;
    entry->rxphd_poll_cycles = exp3_trace.rxphd_poll_cycles;
    entry->rxfr_poll_cycles = exp3_trace.rxfr_poll_cycles;
    exp3_trace_cancel();
}

static void exp3_metric_init(exp3_metric_stats_t *stats)
{
    stats->min_ns = INT64_MAX;
    stats->max_ns = INT64_MIN;
    stats->sum_ns = 0;
    stats->count = 0;
}

static void exp3_metric_update(exp3_metric_stats_t *stats, int64_t value_ns)
{
    if (value_ns < stats->min_ns) stats->min_ns = value_ns;
    if (value_ns > stats->max_ns) stats->max_ns = value_ns;
    stats->sum_ns += value_ns;
    stats->count++;
}

static void exp3_print_metric(const char *name, const exp3_metric_stats_t *stats)
{
    static char line[180];
    int64_t avg_ns = (stats->count > 0) ? (stats->sum_ns / (int64_t)stats->count) : 0;
    int64_t min_ns = (stats->count > 0) ? stats->min_ns : 0;
    int64_t max_ns = (stats->count > 0) ? stats->max_ns : 0;

    snprintf(line, sizeof(line),
             "EXP3_METRIC_CSV,N2,%d,%d,%s,n=%lu,min_ns=%lld,max_ns=%lld,avg_ns=%lld",
             PREAMBLE_SYMBOLS, PSDU_BYTES, name,
             (unsigned long)stats->count,
             (long long)min_ns, (long long)max_ns, (long long)avg_ns);
    final_log_info(line);
}

static void exp3_print_summary(void)
{
    exp3_metric_stats_t open_to_rmarker;
    exp3_metric_stats_t rxprd_from_rmarker;
    exp3_metric_stats_t rxsfdd_from_rmarker;
    exp3_metric_stats_t rxphd_from_rmarker;
    exp3_metric_stats_t rxfr_from_rmarker;
    exp3_metric_stats_t rxfr_excess;
    exp3_metric_stats_t status_poll_width;
    exp3_metric_stats_t anchor_read_width;
    uint32_t fully_observed_count = 0;
    uint32_t i;
    static char line[260];

    exp3_metric_init(&open_to_rmarker);
    exp3_metric_init(&rxprd_from_rmarker);
    exp3_metric_init(&rxsfdd_from_rmarker);
    exp3_metric_init(&rxphd_from_rmarker);
    exp3_metric_init(&rxfr_from_rmarker);
    exp3_metric_init(&rxfr_excess);
    exp3_metric_init(&status_poll_width);
    exp3_metric_init(&anchor_read_width);

    for (i = 0; i < exp3_sample_count; i++) {
        const exp3_sample_t *entry = &exp3_samples[i];
        int64_t value_ns;

        if (entry->obs_mask == (EXP3_OBS_RXPRD | EXP3_OBS_RXSFDD |
                                EXP3_OBS_RXPHD | EXP3_OBS_RXFR)) {
            fully_observed_count++;
        }
        exp3_metric_update(&open_to_rmarker,
                           exp3_high32_delta_to_ns(entry->rx_open_high32,
                                                  entry->rmarker_high32));
        exp3_metric_update(&anchor_read_width,
                           exp3_cpu_cycles_to_ns(entry->anchor_read_cycles));
        if (entry->obs_mask & EXP3_OBS_RXPRD) {
            exp3_metric_update(&rxprd_from_rmarker,
                               exp3_observation_from_rmarker_ns(entry,
                                                               entry->rxprd_obs_cpu));
            exp3_metric_update(&status_poll_width,
                               exp3_cpu_cycles_to_ns(entry->rxprd_poll_cycles));
        }
        if (entry->obs_mask & EXP3_OBS_RXSFDD) {
            exp3_metric_update(&rxsfdd_from_rmarker,
                               exp3_observation_from_rmarker_ns(entry,
                                                               entry->rxsfdd_obs_cpu));
            exp3_metric_update(&status_poll_width,
                               exp3_cpu_cycles_to_ns(entry->rxsfdd_poll_cycles));
        }
        if (entry->obs_mask & EXP3_OBS_RXPHD) {
            exp3_metric_update(&rxphd_from_rmarker,
                               exp3_observation_from_rmarker_ns(entry,
                                                               entry->rxphd_obs_cpu));
            exp3_metric_update(&status_poll_width,
                               exp3_cpu_cycles_to_ns(entry->rxphd_poll_cycles));
        }
        if (entry->obs_mask & EXP3_OBS_RXFR) {
            value_ns = exp3_observation_from_rmarker_ns(entry,
                                                        entry->rxfr_obs_cpu);
            exp3_metric_update(&rxfr_from_rmarker, value_ns);
            exp3_metric_update(&rxfr_excess, value_ns - (int64_t)DP_MODEL_NS);
            exp3_metric_update(&status_poll_width,
                               exp3_cpu_cycles_to_ns(entry->rxfr_poll_cycles));
        }
    }

    snprintf(line, sizeof(line),
             "EXP3_MODEL_CSV,plen=%d,psdu_bytes=%d,preamble_ns=%llu,sfd_ns=%llu,shr_ns=%llu,phr_ns=%llu,psdu_ns=%llu,dp_ns=%llu,scheduled_dp_us=%d",
             PREAMBLE_SYMBOLS, PSDU_BYTES,
             (unsigned long long)PREAMBLE_MODEL_NS,
             (unsigned long long)SFD_MODEL_NS,
             (unsigned long long)SHR_MODEL_NS,
             (unsigned long long)PHR_MODEL_NS,
             (unsigned long long)PSDU_MODEL_NS,
             (unsigned long long)DP_MODEL_NS,
             PHR_PSDU_US);
    final_log_info(line);
    exp3_print_metric("programmed_open_to_rmarker", &open_to_rmarker);
    exp3_print_metric("rxprd_obs_est_from_rmarker", &rxprd_from_rmarker);
    exp3_print_metric("rxsfdd_obs_est_from_rmarker", &rxsfdd_from_rmarker);
    exp3_print_metric("rxphd_obs_est_from_rmarker", &rxphd_from_rmarker);
    exp3_print_metric("rxfr_obs_est_from_rmarker", &rxfr_from_rmarker);
    exp3_print_metric("rxfr_obs_est_excess_vs_model_dp", &rxfr_excess);
    exp3_print_metric("status_poll_transaction_width", &status_poll_width);
    exp3_print_metric("anchor_systime_read_width", &anchor_read_width);

    exp3_final_expected = expected_rx[1];
    exp3_final_rx = per_stats[1].rx_count;
    exp3_final_pass = (exp3_final_expected == TARGET_CYCLES &&
                       exp3_final_rx == TARGET_CYCLES &&
                       exp3_sample_count == TARGET_CYCLES &&
                       fully_observed_count == TARGET_CYCLES);

    snprintf(line, sizeof(line),
             "EXP3_RUN_RESULT,N2,plen=%d,psdu_bytes=%d,expected=%lu,rx=%lu,samples=%lu,all_stages=%lu,status=%s",
             PREAMBLE_SYMBOLS, PSDU_BYTES,
             (unsigned long)exp3_final_expected,
             (unsigned long)exp3_final_rx,
             (unsigned long)exp3_sample_count,
             (unsigned long)fully_observed_count,
             exp3_final_pass ? "PASS" : "FAIL");
    final_log_info(line);
}

static void exp3_dump_samples(void)
{
    uint32_t i;
    static char line[420];

    snprintf(line, sizeof(line),
             "EXP3_DUMP_START,plen=%d,psdu_bytes=%d,expected=%lu,rx=%lu,count=%lu,status=%s",
             PREAMBLE_SYMBOLS, PSDU_BYTES,
             (unsigned long)exp3_final_expected,
             (unsigned long)exp3_final_rx,
             (unsigned long)exp3_sample_count,
             exp3_final_pass ? "PASS" : "FAIL");
    test_run_info((unsigned char *)line);

    cir_log_info("EXP3_CSV_HEADER,rx_seq,cycle,node,plen,psdu_bytes,obs_mask,rx_open_high32,rmarker_high32,anchor_sys_high32,anchor_cpu,anchor_read_cycles,rxprd_obs_cpu,rxsfdd_obs_cpu,rxphd_obs_cpu,rxfr_obs_cpu,rxprd_poll_cycles,rxsfdd_poll_cycles,rxphd_poll_cycles,rxfr_poll_cycles,open_to_rmarker_ns,rxprd_obs_est_from_rmarker_ns,rxsfdd_obs_est_from_rmarker_ns,rxphd_obs_est_from_rmarker_ns,rxfr_obs_est_from_rmarker_ns,rxfr_obs_est_excess_vs_model_dp_ns");

    for (i = 0; i < exp3_sample_count; i++) {
        const exp3_sample_t *entry = &exp3_samples[i];
        int64_t open_ns = exp3_high32_delta_to_ns(entry->rx_open_high32,
                                                  entry->rmarker_high32);
        int64_t rxprd_ns = (entry->obs_mask & EXP3_OBS_RXPRD) ?
                           exp3_observation_from_rmarker_ns(entry,
                                                           entry->rxprd_obs_cpu) : 0;
        int64_t rxsfdd_ns = (entry->obs_mask & EXP3_OBS_RXSFDD) ?
                            exp3_observation_from_rmarker_ns(entry,
                                                            entry->rxsfdd_obs_cpu) : 0;
        int64_t rxphd_ns = (entry->obs_mask & EXP3_OBS_RXPHD) ?
                           exp3_observation_from_rmarker_ns(entry,
                                                           entry->rxphd_obs_cpu) : 0;
        int64_t rxfr_ns = (entry->obs_mask & EXP3_OBS_RXFR) ?
                          exp3_observation_from_rmarker_ns(entry,
                                                          entry->rxfr_obs_cpu) : 0;
        int64_t excess_ns = (entry->obs_mask & EXP3_OBS_RXFR) ?
                            rxfr_ns - (int64_t)DP_MODEL_NS : 0;

        snprintf(line, sizeof(line),
                 "EXP3_CSV,%lu,%lu,%s,%d,%d,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lld,%lld,%lld,%lld,%lld,%lld",
                 (unsigned long)entry->rx_seq,
                 (unsigned long)entry->cycle_no,
                 get_slot_description(entry->src_idx),
                 PREAMBLE_SYMBOLS, PSDU_BYTES, entry->obs_mask,
                 (unsigned long)entry->rx_open_high32,
                 (unsigned long)entry->rmarker_high32,
                 (unsigned long)entry->anchor_sys_high32,
                 (unsigned long)entry->anchor_cpu,
                 (unsigned long)entry->anchor_read_cycles,
                 (unsigned long)entry->rxprd_obs_cpu,
                 (unsigned long)entry->rxsfdd_obs_cpu,
                 (unsigned long)entry->rxphd_obs_cpu,
                 (unsigned long)entry->rxfr_obs_cpu,
                 (unsigned long)entry->rxprd_poll_cycles,
                 (unsigned long)entry->rxsfdd_poll_cycles,
                 (unsigned long)entry->rxphd_poll_cycles,
                 (unsigned long)entry->rxfr_poll_cycles,
                 (long long)open_ns,
                 (long long)rxprd_ns,
                 (long long)rxsfdd_ns,
                 (long long)rxphd_ns,
                 (long long)rxfr_ns,
                 (long long)excess_ns);
        cir_log_info(line);
        Sleep(EXP3_SAMPLE_DUMP_DELAY_MS);
    }

    snprintf(line, sizeof(line),
             "EXP3_DUMP_DONE,plen=%d,psdu_bytes=%d,expected=%lu,rx=%lu,count=%lu,status=%s",
             PREAMBLE_SYMBOLS, PSDU_BYTES,
             (unsigned long)exp3_final_expected,
             (unsigned long)exp3_final_rx,
             (unsigned long)exp3_sample_count,
             exp3_final_pass ? "PASS" : "FAIL");
    test_run_info((unsigned char *)line);

    snprintf(line, sizeof(line),
             "EXP3_DONE,plen=%d,psdu_bytes=%d,expected=%lu,rx=%lu,samples=%lu,status=%s",
             PREAMBLE_SYMBOLS, PSDU_BYTES,
             (unsigned long)exp3_final_expected,
             (unsigned long)exp3_final_rx,
             (unsigned long)exp3_sample_count,
             exp3_final_pass ? "PASS" : "FAIL");
    test_run_info((unsigned char *)line);
}
#endif

static void update_node_latency(latency_stats_t *stats, uint32_t latency_us) {
    if (latency_us < stats->min_us) stats->min_us = latency_us;
    if (latency_us > stats->max_us) stats->max_us = latency_us;
    stats->sum_us += latency_us;
    stats->count++;
}

static void update_signed_timing(signed_timing_stats_t *stats, int64_t value_ns)
{
    if (value_ns < stats->min_ns) stats->min_ns = value_ns;
    if (value_ns > stats->max_ns) stats->max_ns = value_ns;
    stats->sum_ns += value_ns;
    stats->count++;
}

static bool slot_timing_counts_match(void)
{
    uint8_t i;

    for (i = 1U; i < TOTAL_ARRAY_SIZE; i++) {
        if (expected_rx[i] > 0U &&
            node_slot_timing_error[i].count != per_stats[i].rx_count) {
            return false;
        }
    }
    return true;
}

static uint8_t brrs_configured_sensor_bitmap(void)
{
#if BRRS_EXPERIMENT == 4
    return (uint8_t)BRRS_ACTIVE_NODE_BITMAP;
#else
    return brrs_node_bitmap_bit(2U);
#endif
}

static brrs_beacon_config_t current_beacon_config;

static void brrs_prepare_beacon(uint8_t message_type,
                                uint16_t superframe_seq,
                                uint8_t active_node_bitmap)
{
    brrs_beacon_config_t config;
    uint8_t repeats = 1U;

    config.superframe_seq = superframe_seq;
    config.data_preamble_symbols = PREAMBLE_SYMBOLS;
    config.data_psdu_bytes = PSDU_BYTES;
    config.data_rate = (uint8_t)DWT_BR_6M8;
    config.active_node_bitmap = active_node_bitmap;
    config.first_slot_offset_us = SYNC_BUFFER_US;
    config.slot_interval_us = SLOT_INTERVAL_US;
    config.superframe_period_us = PERIOD_US;
#if BRRS_EXPERIMENT == 4
    repeats = BRRS_EXP4_SLOT_REPEATS;
#endif
    if (!brrs_build_round_robin_schedule(&config, repeats)) {
        config.slot_count = 0U;
        memset(config.slot_owner, 0, sizeof(config.slot_owner));
    }
    current_beacon_config = config;
    brrs_encode_beacon(beacon_msg, NODE_INIT, NODE_ALL, message_type, &config);
}

#if BRRS_EXPERIMENT == 4
static uint16_t exp4_read_superframe_seq(const uint8_t *msg)
{
    return brrs_get_u16_le(&msg[IDX_SUPERFRAME_SEQ]);
}

static uint32_t exp4_future_delta_us(uint32_t now_high32, uint32_t target_high32)
{
    int32_t delta_high32 = (int32_t)(target_high32 - now_high32);
    if (delta_high32 <= 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)(uint32_t)delta_high32 << 8) /
                      DWT_TIME_UNITS_PER_US);
}

static uint64_t exp4_high32_delta_to_us_x1000(uint32_t start_high32,
                                               uint32_t end_high32)
{
    uint32_t delta_high32 = end_high32 - start_high32;
    uint64_t numerator = (uint64_t)delta_high32 * 256000ULL;
    return (numerator + DWT_TIME_UNITS_PER_US / 2ULL) /
           DWT_TIME_UNITS_PER_US;
}

static void exp4_record_period(uint32_t start_high32, uint32_t end_high32)
{
    uint32_t period_x1000_us = (uint32_t)
        exp4_high32_delta_to_us_x1000(start_high32, end_high32);
    if (period_x1000_us < exp4_period_min_x1000_us) {
        exp4_period_min_x1000_us = period_x1000_us;
    }
    if (period_x1000_us > exp4_period_max_x1000_us) {
        exp4_period_max_x1000_us = period_x1000_us;
    }
    exp4_period_sum_x1000_us += period_x1000_us;
    exp4_period_count++;
}
#endif

/* ========== [NEW] Delayed-RX 헬퍼 ==========
 * SYNC TX 완료 후 특정 슬롯 시각에 RX 윈도우를 연다.
 *
 * @param sync_tx_ts_high32: SYNC TX timestamp의 상위 32비트
 *                            (dwt_readtxtimestamphi32 결과)
 * @param slot_offset_us: SYNC TX 완료 시각으로부터 슬롯 시작까지의 오프셋
 *
 * 주의: dwt_setdelayedtrxtime은 system time의 상위 32비트만 받음.
 *       하위 8비트는 0으로 가정되므로 실제 분해능은 ~4ns.
 */
static uint32_t last_rx_open_high32 = 0;  /* [DIAG] 마지막으로 프로그램한 창 열림 시각 */
#if BRRS_EXPERIMENT == 4
static bool current_rx_open_timing_valid = false;
#endif

static bool schedule_delayed_rx(uint32_t sync_tx_ts_high32, uint32_t slot_offset_us)
{
    uint64_t offset_ticks = US_TO_DWT_TIME(slot_offset_us);
    /* 상위 32비트로 변환: >> 8 */
    uint32_t offset_high32 = (uint32_t)(offset_ticks >> 8);
    uint32_t rx_time_high32 = sync_tx_ts_high32 + offset_high32;

    last_rx_open_high32 = rx_time_high32;

    dwt_setdelayedtrxtime(rx_time_high32);
#if BRRS_EXPERIMENT == 4
    /* Exp4 receives all contiguous DATA slots as one burst.  A per-slot frame
     * timeout can expire over the following slot and misattribute its frame,
     * so the CPU-side superframe deadline ends the burst instead. */
    dwt_setrxtimeout(0U);
#else
    dwt_setrxtimeout(US_TO_UUS(RX_WINDOW_US));
#endif

    int result = dwt_rxenable(DWT_START_RX_DELAYED | DWT_IDLE_ON_DLY_ERR);
    if (result != DWT_SUCCESS) {
        total_rx_delayed_fallbacks++;
        dwt_forcetrxoff();
        return false;
    }

#if BRRS_EXPERIMENT == 4
    current_rx_open_timing_valid = true;
#endif
    return true;
}

/* ========== DATA 추적 ========== */
static uint8_t data_received_from[TOTAL_ARRAY_SIZE] = {0};

static uint32_t current_cycle = 1;

static uint32_t total_cycles = 0;
static uint32_t data_successful_cycles = 0;
static uint32_t failed_cycles = 0;
#define MAX_FAILED_CYCLES_LOG 10
static uint32_t failed_cycle_numbers[MAX_FAILED_CYCLES_LOG] = {0};

static bool data_success_in_current_cycle = false;
static bool final_stats_printed = false;
#if BRRS_EXPERIMENT == 4
static uint8_t exp4_received_in_current_superframe = 0;
static uint8_t exp4_slot_received[BRRS_MAX_DATA_SLOTS] = {0};
#endif

/* ========== [NEW] 슬롯 RX 스케줄링 상태 ========== */
static uint32_t last_sync_tx_ts_high32 = 0;  /* SYNC TX timestamp 저장 */
static bool slots_scheduled[BRRS_MAX_DATA_SLOTS] = {false};
static uint8_t current_rx_slot = 0xFF;  /* 현재 대기 중인 슬롯 (RX 윈도우 안에서) */

#if BRRS_EXPERIMENT != 4
static const char *failed_accum_cause_name(fail_accum_cause_t cause)
{
    static const char *const names[FAIL_ACCUM_CAUSE_COUNT] = {
        "fwto", "pto", "sfdto", "phe", "fce", "fsl", "other"
    };
    return (cause < FAIL_ACCUM_CAUSE_COUNT) ? names[cause] : "other";
}

static fail_accum_cause_t failed_accum_classify(uint32_t status_reg)
{
    if (status_reg & DWT_INT_RXFTO_BIT_MASK) return FAIL_ACCUM_FWTO;
    if (status_reg & DWT_INT_RXPTO_BIT_MASK) return FAIL_ACCUM_PTO;
    if (status_reg & DWT_INT_RXSTO_BIT_MASK) return FAIL_ACCUM_SFDTO;
    if (status_reg & DWT_INT_RXPHE_BIT_MASK) return FAIL_ACCUM_PHE;
    if (status_reg & DWT_INT_RXFCE_BIT_MASK) return FAIL_ACCUM_FCE;
    if (status_reg & DWT_INT_RXFSL_BIT_MASK) return FAIL_ACCUM_FSL;
    return FAIL_ACCUM_OTHER;
}

static uint8_t failed_accum_owner_index(void)
{
    if (current_rx_slot < current_beacon_config.slot_count) {
        uint8_t owner_seq = current_beacon_config.slot_owner[current_rx_slot];
        uint8_t owner_idx = (uint8_t)(owner_seq - 1U);
        if (owner_seq >= 2U && owner_idx < TOTAL_ARRAY_SIZE) {
            return owner_idx;
        }
    }
    return 0U;
}

static void failed_accum_hist_add(failed_accum_hist_entry_t *hist,
                                  uint8_t *used,
                                  uint32_t *overflow,
                                  uint16_t accum)
{
    uint8_t i;

    for (i = 0U; i < *used; i++) {
        if (hist[i].accum == accum) {
            hist[i].count++;
            return;
        }
    }

    if (*used < FAIL_ACCUM_HIST_CAPACITY) {
        hist[*used].accum = accum;
        hist[*used].count = 1U;
        (*used)++;
    } else {
        (*overflow)++;
    }
}

static void capture_failed_accum(uint32_t status_reg)
{
    uint8_t owner_idx = failed_accum_owner_index();
    fail_accum_cause_t cause = failed_accum_classify(status_reg);
    failed_accum_stats_t *stats = &failed_accum_stats[owner_idx][cause];
    dwt_cirdiags_t diag;
    uint16_t raw_accum;

    stats->events++;
    memset(&diag, 0, sizeof(diag));
    if (dwt_readdiagnostics_acc(&diag, DWT_ACC_IDX_IP_M) != DWT_SUCCESS) {
        stats->read_fail++;
        return;
    }

    stats->diag_read_ok++;
    raw_accum = diag.accumCount;

    if (!(status_reg & DWT_INT_RXPRD_BIT_MASK)) {
        stats->invalid_no_rxprd++;
        failed_accum_hist_add(stats->invalid_raw_hist,
                              &stats->invalid_hist_used,
                              &stats->invalid_hist_overflow,
                              raw_accum);
    } else if (raw_accum == 0U) {
        stats->invalid_zero++;
        failed_accum_hist_add(stats->invalid_raw_hist,
                              &stats->invalid_hist_used,
                              &stats->invalid_hist_overflow,
                              raw_accum);
    } else if (raw_accum > PREAMBLE_SYMBOLS) {
        stats->invalid_range++;
        failed_accum_hist_add(stats->invalid_raw_hist,
                              &stats->invalid_hist_used,
                              &stats->invalid_hist_overflow,
                              raw_accum);
    } else {
        stats->valid++;
        failed_accum_hist_add(stats->valid_hist,
                              &stats->valid_hist_used,
                              &stats->valid_hist_overflow,
                              raw_accum);
    }
}

static void print_failed_accum_stats(void)
{
    uint8_t node_idx;
    uint8_t cause_idx;

    final_log_info("--- Failed-frame Ipatov accumCount ---");
    final_log_info("FAIL_ACCUM_NOTE,valid_requires=RXPRD_and_1_to_PLEN,invalid_raw_may_be_stale");

    for (node_idx = 0U; node_idx < TOTAL_ARRAY_SIZE; node_idx++) {
        for (cause_idx = 0U; cause_idx < FAIL_ACCUM_CAUSE_COUNT; cause_idx++) {
            failed_accum_stats_t *stats = &failed_accum_stats[node_idx][cause_idx];
            uint8_t bin;
            static char line[220];

            if (stats->events == 0U) {
                continue;
            }

            snprintf(line, sizeof(line),
                     "FAIL_ACCUM_SUMMARY_CSV,%s,%s,events=%lu,diag_ok=%lu,valid=%lu,invalid_no_rxprd=%lu,invalid_zero=%lu,invalid_range=%lu,read_fail=%lu,hist_overflow=%lu",
                     get_slot_description(node_idx),
                     failed_accum_cause_name((fail_accum_cause_t)cause_idx),
                     (unsigned long)stats->events,
                     (unsigned long)stats->diag_read_ok,
                     (unsigned long)stats->valid,
                     (unsigned long)stats->invalid_no_rxprd,
                     (unsigned long)stats->invalid_zero,
                     (unsigned long)stats->invalid_range,
                     (unsigned long)stats->read_fail,
                     (unsigned long)(stats->valid_hist_overflow +
                                     stats->invalid_hist_overflow));
            final_log_info(line);

            for (bin = 0U; bin < stats->valid_hist_used; bin++) {
                snprintf(line, sizeof(line),
                         "FAIL_ACCUM_HIST_CSV,%s,%s,valid,accum=%u,n=%lu",
                         get_slot_description(node_idx),
                         failed_accum_cause_name((fail_accum_cause_t)cause_idx),
                         stats->valid_hist[bin].accum,
                         (unsigned long)stats->valid_hist[bin].count);
                final_log_info(line);
            }
            for (bin = 0U; bin < stats->invalid_hist_used; bin++) {
                snprintf(line, sizeof(line),
                         "FAIL_ACCUM_HIST_CSV,%s,%s,invalid_raw,accum=%u,n=%lu",
                         get_slot_description(node_idx),
                         failed_accum_cause_name((fail_accum_cause_t)cause_idx),
                         stats->invalid_raw_hist[bin].accum,
                         (unsigned long)stats->invalid_raw_hist[bin].count);
                final_log_info(line);
            }
        }
    }
}
#endif
static uint8_t current_active_node_bitmap = 0U;

static bool schedule_rx_slot(uint8_t slot_idx)
{
    while (slot_idx < current_beacon_config.slot_count) {
        uint8_t owner_seq = current_beacon_config.slot_owner[slot_idx];
        uint32_t slot_start_us;

        if (owner_seq < 2U || owner_seq > 8U) {
            slot_idx++;
            continue;
        }

        slot_start_us = current_beacon_config.first_slot_offset_us +
                        (uint32_t)slot_idx * current_beacon_config.slot_interval_us;
        uint32_t slot_offset_us = slot_start_us - RX_EARLY_US;
        slots_scheduled[slot_idx] = true;
        current_rx_slot = slot_idx;

        if (schedule_delayed_rx(last_sync_tx_ts_high32, slot_offset_us)) {
#if BRRS_EXPERIMENT == 3 && EXP3_RX_STAGE_DIAG
            if (owner_seq == 2U && current_cycle <= TARGET_CYCLES) {
                exp3_trace_begin(current_cycle, last_rx_open_high32);
            }
#endif
            return true;
        }

        /* Late scheduling is a missed slot. Keep scanning so later TDMA slots can still be measured. */
        slot_idx++;
    }

    current_rx_slot = 0xFF;
    return false;
}

#if BRRS_EXPERIMENT == 4
/* DW3000 fast command 0x02 enables RX. The previous slot already left the
 * receiver PLL configured, so the burst re-arm path does not repeat the
 * PLL_COMMON write performed by dwt_rxenable(). */
#define BRRS_DW3000_FAST_CMD_RX 0x02U

static bool exp4_find_slot_by_rx_timestamp(uint32_t rx_ts_high32,
                                           uint8_t *slot_idx)
{
    uint32_t first_slot_high32;
    uint32_t interval_high32;
    int32_t relative_high32;
    int32_t candidate;

    if (last_sync_tx_ts_high32 == 0U ||
        current_beacon_config.slot_interval_us == 0U) {
        return false;
    }

    first_slot_high32 = (uint32_t)
        (US_TO_DWT_TIME(current_beacon_config.first_slot_offset_us) >> 8);
    interval_high32 = (uint32_t)
        (US_TO_DWT_TIME(current_beacon_config.slot_interval_us) >> 8);
    if (interval_high32 == 0U) {
        return false;
    }

    relative_high32 = (int32_t)
        ((rx_ts_high32 - last_sync_tx_ts_high32) - first_slot_high32);
    if (relative_high32 < -(int32_t)(interval_high32 / 2U)) {
        return false;
    }

    candidate = (relative_high32 + (int32_t)(interval_high32 / 2U)) /
                (int32_t)interval_high32;
    if (candidate < 0 || candidate >= current_beacon_config.slot_count) {
        return false;
    }

    *slot_idx = (uint8_t)candidate;
    return true;
}

static uint8_t exp4_slot_from_event_time(void)
{
    uint32_t now_high32;
    uint32_t elapsed_us;
    uint32_t candidate;

    if (last_sync_tx_ts_high32 == 0U ||
        current_beacon_config.slot_interval_us == 0U) {
        return current_rx_slot;
    }

    now_high32 = dwt_readsystimestamphi32();
    elapsed_us = dwt_high32_delta_to_us(last_sync_tx_ts_high32, now_high32);
    if (elapsed_us < current_beacon_config.first_slot_offset_us) {
        return current_rx_slot;
    }

    /* RX errors may be reported just before RMARKER (SFD failure) or after it
     * (PHR/FCS failure).  Nearest-slot rounding keeps both sides assigned to
     * the scheduled transmission that caused the event. */
    candidate = (elapsed_us - current_beacon_config.first_slot_offset_us +
                 current_beacon_config.slot_interval_us / 2U) /
                current_beacon_config.slot_interval_us;
    return (candidate < current_beacon_config.slot_count) ?
           (uint8_t)candidate : current_rx_slot;
}

static void exp4_read_rx_metadata(uint8_t host_buffer,
                                  uint32_t *timestamp_high32,
                                  uint16_t *frame_length)
{
    uint8_t metadata[RX_FINFO_LEN + RX_TIME_RX_STAMP_LEN];
    uint16_t finfo16;

    /* FINFO and RX_TIME are adjacent in each swinging set. Read them in one
     * SPI transaction so both values describe the same completed frame even
     * while the other double buffer is receiving the next slot. */
    if ((host_buffer & 1U) == 0U) {
        dwt_readfromdevice(BUF0_RX_FINFO, 0U, sizeof(metadata), metadata);
    } else {
        dwt_readfromdevice(INDIRECT_POINTER_B_ID, 0U,
                           sizeof(metadata), metadata);
    }

    finfo16 = (uint16_t)metadata[0] | ((uint16_t)metadata[1] << 8);
    *frame_length = finfo16 & (uint16_t)RX_FINFO_STD_RXFLEN_MASK;
    *timestamp_high32 = ((uint32_t)metadata[8] << 24) |
                        ((uint32_t)metadata[7] << 16) |
                        ((uint32_t)metadata[6] << 8) |
                        (uint32_t)metadata[5];
}

static void exp4_read_rx_buffer(uint8_t host_buffer, uint8_t *buffer,
                                uint16_t length, uint16_t offset)
{
    uint32_t buffer_id = ((host_buffer & 1U) == 0U) ?
                         RX_BUFFER_0_ID : RX_BUFFER_1_ID;

    dwt_readfromdevice(buffer_id, offset, length, buffer);
}

static void exp4_clear_rx_status(uint32_t mask, latency_stats_t *stats)
{
    uint32_t phase_start_cycles = dwt_timer_get_cycles();
    uint32_t phase_cycles;
    uint32_t phase_us;

    dwt_writesysstatuslo(mask);
    phase_cycles = dwt_timer_get_cycles() - phase_start_cycles;
    phase_us = (phase_cycles + (CPU_FREQ_HZ / 1000000UL) - 1UL) /
               (CPU_FREQ_HZ / 1000000UL);
    if (stats != NULL) {
        update_node_latency(stats, phase_us);
    }
}

static void exp4_release_rx_buffer(void)
{
    uint32_t phase_start_cycles = dwt_timer_get_cycles();
    uint32_t phase_cycles;
    uint32_t phase_us;
    uint8_t clear_mask = (exp4_rx_host_buffer == 0U) ?
        DWT_RDB_STATUS_CLEAR_BUFF0_EVENTS :
        DWT_RDB_STATUS_CLEAR_BUFF1_EVENTS;

    /* DW3000 User Manual section 4.4 requires the processed buffer's
     * RDB_STATUS flags to be cleared as well as issuing CMD_DB_TOGGLE. */
    dwt_writerdbstatus(clear_mask);
    dwt_signal_rx_buff_free();
    exp4_rx_host_buffer ^= 1U;
    phase_cycles = dwt_timer_get_cycles() - phase_start_cycles;
    phase_us = (phase_cycles + (CPU_FREQ_HZ / 1000000UL) - 1UL) /
               (CPU_FREQ_HZ / 1000000UL);
    update_node_latency(&exp4_rx_buffer_free_stats, phase_us);
}

static bool exp4_has_later_slot(void)
{
    return exp4_data_burst_active &&
           current_rx_slot != 0xFFU &&
           (uint8_t)(current_rx_slot + 1U) <
               current_beacon_config.slot_count;
}

static bool exp4_rearm_after_event(uint32_t event_start_cycles)
{
    if (exp4_has_later_slot()) {
        uint8_t next_slot = (current_rx_slot == 0xFF) ?
                            0xFF : (uint8_t)(current_rx_slot + 1U);
        uint32_t phase_start_cycles;
        uint32_t phase_cycles;
        uint32_t phase_us;

        /* The frame header is parsed after this command.  Do not advance the
         * logical slot yet: a missing earlier frame may make the completed
         * frame belong to a later scheduled slot. */
        current_rx_open_timing_valid = false;

        phase_start_cycles = dwt_timer_get_cycles();
        dwt_writetodevice(BRRS_DW3000_FAST_CMD_RX, 0U, 0U, NULL);
        phase_cycles = dwt_timer_get_cycles() - phase_start_cycles;
        phase_us = (phase_cycles + (CPU_FREQ_HZ / 1000000UL) - 1UL) /
                   (CPU_FREQ_HZ / 1000000UL);
        update_node_latency(&exp4_rearm_rx_enable_stats, phase_us);
        if (next_slot < current_beacon_config.slot_count) {
            slots_scheduled[next_slot] = true;
        }

        uint32_t service_cycles = dwt_timer_get_cycles() - event_start_cycles;
        uint32_t service_us = (service_cycles + (CPU_FREQ_HZ / 1000000UL) - 1UL) /
                              (CPU_FREQ_HZ / 1000000UL);
        update_node_latency(&exp4_rearm_service_stats, service_us);
        return true;
    }

    return false;
}

typedef enum {
    EXP4_BURST_CLOSE_LAST_EVENT,
    EXP4_BURST_CLOSE_DEADLINE,
    EXP4_BURST_CLOSE_SYNC_PREP
} exp4_burst_close_reason_t;

static void exp4_close_data_burst(exp4_burst_close_reason_t reason)
{
    if (!exp4_data_burst_active) {
        return;
    }

    dwt_forcetrxoff();
    dwt_writesysstatuslo(0xFFFFFFFF);
    dwt_writerdbstatus(0xFFU);
    exp4_data_burst_active = false;
    current_rx_slot = 0xFF;

    if (reason == EXP4_BURST_CLOSE_LAST_EVENT) {
        exp4_burst_early_close_count++;
    } else if (reason == EXP4_BURST_CLOSE_DEADLINE) {
        exp4_burst_deadline_close_count++;
    } else {
        exp4_burst_forced_prep_close_count++;
    }
}

static void exp4_advance_after_event(uint8_t observed_slot,
                                     bool close_on_final_slot)
{
    uint8_t next_slot = 0xFF;

    if (current_rx_slot < current_beacon_config.slot_count) {
        next_slot = (uint8_t)(current_rx_slot + 1U);
    }
    if (observed_slot < current_beacon_config.slot_count &&
        (next_slot == 0xFF || (uint8_t)(observed_slot + 1U) > next_slot)) {
        next_slot = (uint8_t)(observed_slot + 1U);
    }

    if (next_slot < current_beacon_config.slot_count) {
        current_rx_slot = next_slot;
        slots_scheduled[next_slot] = true;
    } else if (close_on_final_slot) {
        exp4_close_data_burst(EXP4_BURST_CLOSE_LAST_EVENT);
    } else {
        /* An RX error near the final slot is not proof that the expected DATA
         * frame was processed. Keep RX active until the scheduled deadline so
         * a later valid frame or the fallback close can finish the burst. */
        current_rx_slot = (uint8_t)(current_beacon_config.slot_count - 1U);
    }
}

static bool exp4_transmit_control_frame(uint8_t msg_type,
                                        uint16_t superframe_seq,
                                        bool delayed,
                                        uint32_t delayed_time_high32,
                                        bool *config_is_sync,
                                        uint32_t *actual_tx_high32)
{
    bool timing_exact = true;
    bool immediate_fallback = false;
    uint32_t tx_wait_timeout_us = EXP4_TX_WAIT_TIMEOUT_US;
    uint32_t sync_prep_start_cycles = dwt_timer_get_cycles();
    int tx_result;

    dwt_forcetrxoff();
    dwt_writesysstatuslo(0xFFFFFFFF);
    if (dwt_configure(&config_sync) != DWT_SUCCESS) {
        return false;
    }
    *config_is_sync = true;

    current_active_node_bitmap =
        (msg_type == MSG_TYPE_SYNC) ? brrs_configured_sensor_bitmap() : 0U;
    brrs_prepare_beacon(msg_type, superframe_seq, current_active_node_bitmap);
    if (msg_type == MSG_TYPE_SYNC && superframe_seq == 1U) {
        static char schedule_line[160];
        char owners[BRRS_MAX_DATA_SLOTS + 1U];
        uint8_t slot;

        exp4_diag_slot_count = current_beacon_config.slot_count;
        for (slot = 0U; slot < current_beacon_config.slot_count; slot++) {
            owners[slot] = (char)('0' + current_beacon_config.slot_owner[slot]);
            exp4_diag_slot_owner[slot] = current_beacon_config.slot_owner[slot];
        }
        owners[current_beacon_config.slot_count] = '\0';
        snprintf(schedule_line, sizeof(schedule_line),
                 "EXP4_SLOT_SCHEDULE_CSV,active_bitmap=0x%02X,slot_count=%u,slot_owners=%s,repeats=%u",
                 current_active_node_bitmap,
                 current_beacon_config.slot_count,
                 owners,
                 BRRS_EXP4_SLOT_REPEATS);
        test_run_info((unsigned char *)schedule_line);
    }
    dwt_setrxaftertxdelay(0);
    dwt_writetxdata(BRRS_BEACON_PSDU_BYTES, beacon_msg, 0);
    dwt_writetxfctrl(BRRS_BEACON_PSDU_BYTES, 0, 0);

    if (delayed) {
        tx_wait_timeout_us += exp4_future_delta_us(dwt_readsystimestamphi32(),
                                                   delayed_time_high32);
        dwt_setdelayedtrxtime(delayed_time_high32);
        tx_result = dwt_starttx(DWT_START_TX_DELAYED);
        if (msg_type == MSG_TYPE_SYNC) {
            uint32_t prep_cycles =
                dwt_timer_get_cycles() - sync_prep_start_cycles;
            uint32_t prep_us =
                (prep_cycles + (CPU_FREQ_HZ / 1000000UL) - 1UL) /
                (CPU_FREQ_HZ / 1000000UL);
            update_node_latency(&exp4_sync_prep_stats, prep_us);
        }
        if (tx_result != DWT_SUCCESS) {
            exp4_sync_tx_delayed_late++;
            timing_exact = false;
            immediate_fallback = true;
            tx_wait_timeout_us = EXP4_TX_WAIT_TIMEOUT_US;
            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            tx_result = dwt_starttx(DWT_START_TX_IMMEDIATE);
        }
    } else {
        tx_result = dwt_starttx(DWT_START_TX_IMMEDIATE);
    }

    if (tx_result != DWT_SUCCESS) {
        return false;
    }

    {
        uint32_t tx_status = 0;
        uint32_t wait_start_cycles = dwt_timer_get_cycles();

        do {
            tx_status = dwt_readsysstatuslo();
            if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
                break;
            }
        } while (!dwt_timer_elapsed(wait_start_cycles,
                                    us_to_cpu_cycles(tx_wait_timeout_us)));

        if (!(tx_status & DWT_INT_TXFRS_BIT_MASK) && delayed && !immediate_fallback) {
            static char timeout_msg[120];

            exp4_tx_wait_timeouts++;
            exp4_sync_tx_delayed_late++;
            timing_exact = false;
            snprintf(timeout_msg, sizeof(timeout_msg),
                     "EXP4_TX_WAIT_TIMEOUT,seq=%u,status=0x%08lX,fallback=immediate",
                     (unsigned)superframe_seq, (unsigned long)tx_status);
            test_run_info((unsigned char *)timeout_msg);

            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            tx_result = dwt_starttx(DWT_START_TX_IMMEDIATE);
            if (tx_result != DWT_SUCCESS) {
                return false;
            }

            wait_start_cycles = dwt_timer_get_cycles();
            do {
                tx_status = dwt_readsysstatuslo();
                if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
                    break;
                }
            } while (!dwt_timer_elapsed(wait_start_cycles,
                                        us_to_cpu_cycles(EXP4_TX_WAIT_TIMEOUT_US)));
        }

        if (!(tx_status & DWT_INT_TXFRS_BIT_MASK)) {
            static char failure_msg[112];

            exp4_tx_wait_timeouts++;
            snprintf(failure_msg, sizeof(failure_msg),
                     "EXP4_TX_FAILED,seq=%u,status=0x%08lX,reason=no_txfrs",
                     (unsigned)superframe_seq, (unsigned long)tx_status);
            test_run_info((unsigned char *)failure_msg);
            dwt_forcetrxoff();
            dwt_writesysstatuslo(0xFFFFFFFF);
            return false;
        }
        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    }

    *actual_tx_high32 = dwt_readtxtimestamphi32();
    return timing_exact;
}

static bool exp4_send_end_beacons(bool *config_is_sync)
{
    uint32_t actual_tx_high32 = 0;
    bool exact = exp4_transmit_control_frame(MSG_TYPE_END,
                                             TARGET_CYCLES,
                                             true,
                                             exp4_next_sync_tx_high32,
                                             config_is_sync,
                                             &actual_tx_high32);
    uint32_t repeat;

    if (actual_tx_high32 == 0) {
        return false;
    }

    exp4_measurement_end_high32 = actual_tx_high32;
    exp4_end_tx_count = 1;
    exp4_record_period(last_sync_tx_ts_high32, actual_tx_high32);

    for (repeat = 1; repeat < EXP4_END_REPEAT_COUNT; repeat++) {
        Sleep(EXP4_END_REPEAT_DELAY_MS);
        if (exp4_transmit_control_frame(MSG_TYPE_END,
                                        TARGET_CYCLES,
                                        false,
                                        0,
                                        config_is_sync,
                                        &actual_tx_high32)) {
            exp4_end_tx_count++;
        }
    }

    return exact && exp4_end_tx_count == EXP4_END_REPEAT_COUNT;
}
#endif

#if BRRS_EXPERIMENT != 4
static bool brrs_send_end_beacons(bool *config_is_sync)
{
    uint32_t repeat;

    run_end_tx_count = 0U;
    for (repeat = 0U; repeat < BRRS_END_REPEAT_COUNT; repeat++) {
        uint32_t tx_status = 0U;

        if (repeat > 0U) {
            Sleep(BRRS_END_REPEAT_DELAY_MS);
        }
        dwt_forcetrxoff();
        dwt_writesysstatuslo(0xFFFFFFFF);
        if (dwt_configure(&config_sync) != DWT_SUCCESS) {
            continue;
        }
        *config_is_sync = true;
        brrs_prepare_beacon(MSG_TYPE_END, TARGET_CYCLES, 0U);
        dwt_setrxaftertxdelay(0);
        dwt_writetxdata(BRRS_BEACON_PSDU_BYTES, beacon_msg, 0);
        dwt_writetxfctrl(BRRS_BEACON_PSDU_BYTES, 0, 0);
        if (dwt_starttx(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS) {
            continue;
        }
        waitforsysstatus(&tx_status, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
        if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
            run_end_tx_count++;
        }
    }

    return run_end_tx_count == BRRS_END_REPEAT_COUNT;
}
#endif

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
int brrs_init(void)
{
    cir_rtt_init();
    cir_log_info("EXP_LOG_READY,channel=1");
    {
        static char exp_log_cfg[180];
        snprintf(exp_log_cfg, sizeof(exp_log_cfg),
                 "EXP_LOG_CONFIG_CSV,experiment=%d,plen=%d,lead_us=%d,tail_us=%d,target=%d,cir=%d",
                 BRRS_EXPERIMENT, PREAMBLE_SYMBOLS,
                 RX_LEAD_MARGIN_US, RX_TAIL_MARGIN_US,
                 TARGET_CYCLES, ENABLE_CIR);
        cir_log_info(exp_log_cfg);
    }
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
#if BRRS_EXPERIMENT == 4
    /* DW3000 supports only manual RX re-enable in double-buffer mode. */
    dwt_setdblrxbuffmode(DBL_BUF_STATE_EN, DBL_BUF_MODE_MAN);
    exp4_rx_host_buffer = 0U;
    {
        uint32_t sys_cfg = dwt_read_reg(SYS_CFG_ID);
        if ((sys_cfg & SYS_CFG_DIS_DRXB_BIT_MASK) != 0U ||
            (sys_cfg & SYS_CFG_RXAUTR_BIT_MASK) != 0U) {
            test_run_info((unsigned char *)
                "EXP4_DOUBLE_BUFFER_CONFIG_CSV,enabled=0,mode=invalid,status=FAIL");
            while (1) { };
        }
        test_run_info((unsigned char *)
            "EXP4_DOUBLE_BUFFER_CONFIG_CSV,enabled=1,mode=manual,status=PASS");
    }
#endif
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
    /* dwt_setrxtimeout은 schedule_delayed_rx()에서 설정 */

#if BRRS_EXPERIMENT == 4
    {
        const uint32_t exp4_event_mask =
            DWT_INT_RXFCG_BIT_MASK |
            SYS_STATUS_ALL_RX_ERR |
            SYS_STATUS_ALL_RX_TO |
            DWT_INT_RXOVRR_BIT_MASK;
        uint32_t enabled_event_mask;
        static char event_mask_line[192];
        static char host_irq_line[128];

        /* FINT_STAT is a masked aggregate: the corresponding SYS_ENABLE_LO
         * bits must be enabled even though Exp4 polls rather than servicing
         * the DW3000 IRQ pin. */
        port_DisableEXT_IRQ();
        dwt_setinterrupt(exp4_event_mask, 0U, DWT_ENABLE_INT_ONLY);
        enabled_event_mask = dwt_read_reg(SYS_ENABLE_LO_ID);
        snprintf(event_mask_line, sizeof(event_mask_line),
                 "EXP4_EVENT_MASK_CONFIG_CSV,expected_lo=0x%08lX,actual_lo=0x%08lX,status=%s",
                 (unsigned long)exp4_event_mask,
                 (unsigned long)enabled_event_mask,
                 ((enabled_event_mask & exp4_event_mask) == exp4_event_mask) ?
                     "PASS" : "FAIL");
        test_run_info((unsigned char *)event_mask_line);
        if ((enabled_event_mask & exp4_event_mask) != exp4_event_mask) {
            while (1) { };
        }
        snprintf(host_irq_line, sizeof(host_irq_line),
                 "EXP4_HOST_IRQ_CONFIG_CSV,mode=polling,enabled=%lu,status=%s",
                 (unsigned long)port_GetEXT_IRQStatus(),
                 (port_GetEXT_IRQStatus() == 0U) ? "PASS" : "FAIL");
        test_run_info((unsigned char *)host_irq_line);
        if (port_GetEXT_IRQStatus() != 0U) {
            while (1) { };
        }
    }
#else
    dwt_setinterrupt(0, 0, DWT_ENABLE_INT);
#endif
    dwt_writesysstatuslo(0xFFFFFFFF);

    /* Print configuration */
    {
        static char cfg_msg[240];
        snprintf(cfg_msg, sizeof(cfg_msg),
                 "BRRS v2.10: EXP=%d SYNC_PLEN=%d DATA_PLEN=%d(%dsym) PRE_US=%d SLOT=%dus RX_WIN=%dus LEAD=%dus TAIL=%dus SUPERFRAME=%dus PERIODS=%d TARGET=%d CIR=%d",
                 BRRS_EXPERIMENT, SYNC_PLEN, DATA_PLEN, PREAMBLE_SYMBOLS,
                 PREAMBLE_US, SLOT_INTERVAL_US, RX_WINDOW_US,
                 RX_LEAD_MARGIN_US, RX_TAIL_MARGIN_US, PERIOD_US,
                 PERIODS_PER_CYCLE, TARGET_CYCLES, ENABLE_CIR);
        test_run_info((unsigned char *)cfg_msg);
    }
    {
        static char beacon_cfg_msg[240];
        snprintf(beacon_cfg_msg, sizeof(beacon_cfg_msg),
                 "BRRS_BEACON_CONFIG_CSV,version=%u,beacon_psdu=%u,m=%u,data_psdu=%u,data_rate=%u,active_bitmap=0x%02X,first_slot_rmarker_us=%u,slot_interval_us=%u,period_us=%u",
                 BRRS_PROTOCOL_VERSION, BRRS_BEACON_PSDU_BYTES,
                 PREAMBLE_SYMBOLS, PSDU_BYTES, DWT_BR_6M8,
                 brrs_configured_sensor_bitmap(), SYNC_BUFFER_US,
                 SLOT_INTERVAL_US, PERIOD_US);
        test_run_info((unsigned char *)beacon_cfg_msg);
    }
    test_run_info((unsigned char *)
        "BRRS_TIMING_CONFIG_CSV,metric=uwb_signed_slot_error,reference=sync_tx_rmarker,observation=data_rx_rmarker,slot_identity=nearest_scheduled_rmarker,unit=ns,resolution_ns_x1000=4006");
#if BRRS_EXPERIMENT == 3
    {
        static char cfg_msg[260];
        snprintf(cfg_msg, sizeof(cfg_msg),
                 "EXP3_RX_CONFIG_CSV,%s,%d,%s,%d,%lu,%lu,%lu,%lu,%lu,%lu",
                 EXP3_VARIANT_NAME, SFD_SYMBOLS, EXP3_PHR_RATE_NAME, PSDU_BYTES,
                 (unsigned long)PREAMBLE_MODEL_NS,
                 (unsigned long)SFD_MODEL_NS,
                 (unsigned long)PHR_MODEL_NS,
                 (unsigned long)PSDU_MODEL_NS,
                 (unsigned long)PSDU_RS_PARITY_BITS,
                 (unsigned long)FRAME_MODEL_NS);
        final_log_info(cfg_msg);
    }
#endif
#if BRRS_EXPERIMENT == 4
    {
        static char cfg_msg[560];
        snprintf(cfg_msg, sizeof(cfg_msg),
                 "EXP4_CONFIG_CSV,physical_sensors=%d,data_slots=%d,slot_repeats=%d,data_plen=%d,psdu_bytes=%d,app_payload_bytes=%d,sync_plen=%d,superframe_us=%d,sync_rmarker_offset_us=%d,sync_buffer_us=%d,frame_airtime_us=%d,slot_us=%d,guard_us=%d,lead_us=%d,tail_us=%d,data_burst_deadline_us=%d,sync_prep_deadline_us=%d,burst_rearm_budget_us=%d,max_slots=%d",
                 brrs_active_node_count(brrs_configured_sensor_bitmap()),
                 BRRS_EXP4_DATA_SLOT_COUNT, BRRS_EXP4_SLOT_REPEATS,
                 PREAMBLE_SYMBOLS, PSDU_BYTES,
                 BRRS_APP_PAYLOAD_BYTES, SYNC_PREAMBLE_SYMBOLS,
                 BRRS_SUPERFRAME_US,
                 SYNC_RMARKER_OFFSET_US, SYNC_BUFFER_US,
                 SLOT_INTERVAL_US - SLOT_GUARD_US,
                 SLOT_INTERVAL_US, SLOT_GUARD_US,
                 RX_LEAD_MARGIN_US, RX_TAIL_MARGIN_US,
                 EXP4_DATA_BURST_END_US,
                 CONFIG_SWITCH_US,
                 SLOT_GUARD_US,
                 EXP4_MAX_DATA_SLOTS);
        final_log_info(cfg_msg);
        test_run_info((unsigned char *)
            "EXP4_FIRMWARE_REV,rev=23,beacon_protocol=3,data_header_bytes=8,slot_identity=rx_rmarker,data_rx=delayed_first_manual_double_buffer_burst,rearm=only_if_later_slot,event_poll=fint_status,event_mask=validated,host_irq=disabled_polling,rx_metadata=single_completed_buffer_spi_read,rdb_status=validate_current_host_buffer_post_metadata,slot_class_diag=source_observed_host,burst_end=last_valid_frame_or_schedule_deadline,error_attribution=nearest_event_time,sync_arm=measured_reserved_prep,tx_wait=bounded,elapsed=u64,timing_metric=uwb_signed_slot_error");
    }
#endif

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
            node_uwb_rx_offset[i].min_us = 0xFFFFFFFF;
            node_uwb_rx_offset[i].max_us = 0;
            node_uwb_rx_offset[i].sum_us = 0;
            node_uwb_rx_offset[i].count  = 0;
            node_slot_timing_error[i].min_ns = INT64_MAX;
            node_slot_timing_error[i].max_ns = INT64_MIN;
            node_slot_timing_error[i].sum_ns = 0;
            node_slot_timing_error[i].count = 0;
            node_accum[i].min_us = 0xFFFFFFFF;
            node_accum[i].max_us = 0;
            node_accum[i].sum_us = 0;
            node_accum[i].count  = 0;
            node_open_to_rmarker[i].min_us = 0xFFFFFFFF;
            node_open_to_rmarker[i].max_us = 0;
            node_open_to_rmarker[i].sum_us = 0;
            node_open_to_rmarker[i].count  = 0;
#if BRRS_EXPERIMENT == 4
            exp4_wrong_slot_rx_offset[i].min_us = 0xFFFFFFFF;
            exp4_wrong_slot_rx_offset[i].max_us = 0;
            exp4_wrong_slot_rx_offset[i].sum_us = 0;
            exp4_wrong_slot_rx_offset[i].count = 0;
#endif
#if ENABLE_CIR
            init_signal_stats(&rssi_stats[i]);
            init_signal_stats(&fp_power_stats[i]);
            init_signal_stats(&fp_gap_stats[i]);
            init_ratio_stats(&fp_snr_ratio_stats[i]);
#endif
        }
    }
#if BRRS_EXPERIMENT == 4
    exp4_status_poll_stats.min_us = 0xFFFFFFFF;
    exp4_rearm_service_stats.min_us = 0xFFFFFFFF;
    exp4_rearm_rdb_status_stats.min_us = 0xFFFFFFFF;
    exp4_rdb_incomplete_retry_stats.min_us = 0xFFFFFFFF;
    exp4_rearm_rx_metadata_stats.min_us = 0xFFFFFFFF;
    exp4_rearm_header_read_stats.min_us = 0xFFFFFFFF;
    exp4_rearm_status_clear_pre_stats.min_us = 0xFFFFFFFF;
    exp4_rearm_status_clear_post_stats.min_us = 0xFFFFFFFF;
    exp4_rearm_rx_enable_stats.min_us = 0xFFFFFFFF;
    exp4_sync_prep_stats.min_us = 0xFFFFFFFF;
    exp4_rx_buffer_free_stats.min_us = 0xFFFFFFFF;
#endif

    int period_count = 0;
    uint32_t last_sync_cycles = 0;
#if BRRS_EXPERIMENT != 4
    uint32_t period_interval_cycles = us_to_cpu_cycles(PERIOD_US);
    uint32_t config_switch_time_cycles = us_to_cpu_cycles(CONFIG_SWITCH_US);
#endif
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

    while (1)
    {
        /* ========== [A] SYNC 전송 (Period Timer) ========== */
#if BRRS_EXPERIMENT == 4
        bool sync_tx_due =
            (last_sync_tx_ts_high32 == 0U) ||
            (last_sync_cycles != 0U &&
             dwt_timer_elapsed(last_sync_cycles,
                               us_to_cpu_cycles(CONFIG_SWITCH_US)));
        if (sync_tx_due) {
            if (exp4_data_burst_active) {
                exp4_close_data_burst(EXP4_BURST_CLOSE_SYNC_PREP);
            }
#else
        if (last_sync_cycles == 0 || dwt_timer_elapsed(last_sync_cycles, period_interval_cycles)) {
#endif
            period_count++;

            /* [A-1] One beacon starts one new superframe. */
            if (period_count > 1) current_cycle++;

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
#if BRRS_EXPERIMENT == 4
            exp4_received_in_current_superframe = 0;
            memset(exp4_slot_received, 0, sizeof(exp4_slot_received));
#endif

            if (current_cycle <= TARGET_CYCLES) {
                uint8_t k;
                for (k = 1; k < TOTAL_ARRAY_SIZE; k++) {
                    if ((brrs_configured_sensor_bitmap() &
                         brrs_node_bitmap_bit((uint8_t)(k + 1U))) != 0U) {
#if BRRS_EXPERIMENT == 4
                        expected_rx[k] += BRRS_EXP4_SLOT_REPEATS;
#else
                        expected_rx[k]++;
#endif
                    }
                }
            }

#if BRRS_EXPERIMENT == 4
            if (current_cycle == 1) {
                static char run_start[112];
                snprintf(run_start, sizeof(run_start),
                         "EXP4_RUN_START,superframes=%d,fixed_period_us=%d,cycle_logs=off",
                         TARGET_CYCLES, BRRS_SUPERFRAME_US);
                test_run_info((unsigned char *)run_start);
            }
#endif
#if BRRS_EXPERIMENT != 3 && BRRS_EXPERIMENT != 4 && (!ENABLE_CIR || CIR_LOG_CYCLE_LINES)
            if (current_cycle == 1U || (current_cycle % 100U) == 0U) {
                static char cycle_msg[100];
                snprintf(cycle_msg, sizeof(cycle_msg),
                         "BRRS_PROGRESS,superframe=%lu/%d",
                         (unsigned long)current_cycle, TARGET_CYCLES);
                test_run_info((unsigned char *)cycle_msg);
            }
#endif

            /* [A-2] 종료 조건 */
            if (current_cycle > TARGET_CYCLES && !final_stats_printed) {
                final_stats_printed = true;
#if BRRS_EXPERIMENT == 4
                bool exp4_end_ok = exp4_send_end_beacons(&config_is_sync);
#else
                bool run_end_ok = brrs_send_end_beacons(&config_is_sync);
                {
                    static char end_line[112];
                    snprintf(end_line, sizeof(end_line),
                             "BRRS_END_TX_CSV,count=%lu,expected=%u,status=%s",
                             (unsigned long)run_end_tx_count,
                             BRRS_END_REPEAT_COUNT,
                             run_end_ok ? "PASS" : "FAIL");
                    final_log_info(end_line);
                }
#endif
                static char hdr[80];
                snprintf(hdr, sizeof(hdr), "\n===== BRRS FINAL STATS (PLEN=%d, %d sym) =====",
                         DATA_PLEN, PREAMBLE_SYMBOLS);
                final_log_info(hdr);

#if BRRS_EXPERIMENT == 4
                {
                    uint32_t active_sensor_count =
                        brrs_active_node_count(brrs_configured_sensor_bitmap());
                    uint32_t data_slots_per_superframe = BRRS_EXP4_DATA_SLOT_COUNT;
                    uint32_t expected_frames = data_slots_per_superframe * TARGET_CYCLES;
                    uint32_t missed_frames = (expected_frames >= exp4_frames_received) ?
                                             (expected_frames - exp4_frames_received) : 0;
                    uint32_t per_ppm = (expected_frames > 0) ?
                        (uint32_t)(((uint64_t)missed_frames * 1000000ULL + expected_frames / 2U) /
                                   expected_frames) : 0;
                    uint64_t elapsed_us =
                        (exp4_high32_delta_to_us_x1000(exp4_measurement_start_high32,
                                                       exp4_measurement_end_high32) + 500ULL) /
                        1000ULL;
                    uint64_t goodput_bps = (elapsed_us > 0) ?
                        (exp4_payload_bytes_received * 8ULL * 1000000ULL / elapsed_us) : 0;
                    uint64_t offered_bps = (elapsed_us > 0) ?
                        ((uint64_t)expected_frames * BRRS_APP_PAYLOAD_BYTES * 8ULL * 1000000ULL /
                         elapsed_us) : 0;
                    bool schedule_pass =
                        (exp4_sync_tx_delayed_late == 0 &&
                         total_rx_delayed_fallbacks == 0 &&
                         data_config_errors == 0 &&
                         exp4_wrong_length_frames == 0 &&
                         exp4_wrong_slot_frames == 0 &&
                         exp4_wrong_superframe_frames == 0 &&
                         exp4_rx_buffer_overruns == 0 &&
                         exp4_rdb_dispatches == exp4_rx_good_events &&
                         exp4_rdb_good_events == exp4_rx_good_events &&
                         exp4_rdb_host_mismatches == 0 &&
                         exp4_rdb_incomplete_events == 0 &&
                         exp4_rx_buffer_free_stats.count == exp4_rx_good_events &&
                         exp4_status_poll_stats.count ==
                             exp4_rearm_service_stats.count &&
                         exp4_rearm_rx_enable_stats.count ==
                             exp4_rearm_service_stats.count &&
                         exp4_rearm_status_clear_pre_stats.count +
                             exp4_rearm_status_clear_post_stats.count ==
                             exp4_rearm_service_stats.count &&
                         exp4_rearm_service_stats.max_us +
                             RX_LEAD_MARGIN_US <= SLOT_GUARD_US &&
                         exp4_burst_forced_prep_close_count == 0);
                    bool timing_pass = slot_timing_counts_match();
                    bool link_pass = (exp4_frames_received == expected_frames);
                    bool collection_pass =
                        (total_cycles == TARGET_CYCLES &&
                         expected_frames == data_slots_per_superframe * total_cycles &&
                         exp4_end_ok &&
                         exp4_end_tx_count == EXP4_END_REPEAT_COUNT &&
                         exp4_burst_early_close_count +
                             exp4_burst_deadline_close_count == total_cycles &&
                         schedule_pass &&
                         timing_pass);
                    static char s[640];

                    snprintf(s, sizeof(s),
                             "Superframes: total=%lu all-slots-ok=%lu fail=%lu success=%.2f%%",
                             (unsigned long)total_cycles,
                             (unsigned long)data_successful_cycles,
                             (unsigned long)failed_cycles,
                             (total_cycles > 0) ?
                             (double)data_successful_cycles * 100.0 / total_cycles : 0.0);
                    final_log_info(s);

                    snprintf(s, sizeof(s),
                             "Aggregate: rx=%lu/%lu PER=%.3f%% app_goodput=%llu bps offered=%llu bps elapsed=%lluus",
                             (unsigned long)exp4_frames_received,
                             (unsigned long)expected_frames,
                             (double)per_ppm / 10000.0,
                             (unsigned long long)goodput_bps,
                             (unsigned long long)offered_bps,
                             (unsigned long long)elapsed_us);
                    final_log_info(s);

                    snprintf(s, sizeof(s),
                             "TDMA validation: wrong-length=%lu wrong-slot=%lu wrong-superframe=%lu data-config-errors=%lu rx-schedule-late=%lu sync-delayed-late=%lu rx-buffer-overrun=%lu",
                             (unsigned long)exp4_wrong_length_frames,
                             (unsigned long)exp4_wrong_slot_frames,
                             (unsigned long)exp4_wrong_superframe_frames,
                             (unsigned long)data_config_errors,
                             (unsigned long)total_rx_delayed_fallbacks,
                             (unsigned long)exp4_sync_tx_delayed_late,
                             (unsigned long)exp4_rx_buffer_overruns);
                    final_log_info(s);

                    {
                        uint64_t period_avg_x1000 = exp4_period_count ?
                            (exp4_period_sum_x1000_us / exp4_period_count) : 0;
                        snprintf(s, sizeof(s),
                                 "EXP4_TIMING_CSV,period_count=%lu,min_x1000_us=%lu,max_x1000_us=%lu,avg_x1000_us=%llu,elapsed_us=%llu,sync_delayed_late=%lu,tx_wait_timeout=%lu,end_tx=%lu",
                                 (unsigned long)exp4_period_count,
                                 (unsigned long)(exp4_period_count ? exp4_period_min_x1000_us : 0),
                                 (unsigned long)exp4_period_max_x1000_us,
                                 (unsigned long long)period_avg_x1000,
                                 (unsigned long long)elapsed_us,
                                 (unsigned long)exp4_sync_tx_delayed_late,
                                 (unsigned long)exp4_tx_wait_timeouts,
                                 (unsigned long)exp4_end_tx_count);
                        final_log_info(s);
                    }

                    {
                        uint64_t prep_avg_x1000 = exp4_sync_prep_stats.count ?
                            (exp4_sync_prep_stats.sum_us * 1000ULL /
                             exp4_sync_prep_stats.count) : 0;
                        snprintf(s, sizeof(s),
                                 "EXP4_SYNC_PREP_CSV,budget_us=%u,count=%lu,min_us=%lu,max_us=%lu,avg_x1000_us=%llu,delayed_late=%lu",
                                 (unsigned)EXP4_SYNC_PREP_US,
                                 (unsigned long)exp4_sync_prep_stats.count,
                                 (unsigned long)(exp4_sync_prep_stats.count ?
                                     exp4_sync_prep_stats.min_us : 0),
                                 (unsigned long)exp4_sync_prep_stats.max_us,
                                 (unsigned long long)prep_avg_x1000,
                                 (unsigned long)exp4_sync_tx_delayed_late);
                        final_log_info(s);
                    }

                    snprintf(s, sizeof(s),
                             "EXP4_FRAME_LENGTH_CSV,source=single_completed_buffer_metadata_read,expected=%u,wrong=%lu,buffer0=%lu,buffer1=%lu",
                             (unsigned int)PSDU_BYTES,
                             (unsigned long)exp4_wrong_length_frames,
                             (unsigned long)exp4_wrong_length_by_buffer[0],
                             (unsigned long)exp4_wrong_length_by_buffer[1]);
                    final_log_info(s);
                    {
                        uint8_t slot;
                        uint16_t frame_len;

                        for (slot = 0U; slot <= BRRS_MAX_DATA_SLOTS; slot++) {
                            if (exp4_wrong_length_by_slot[slot] > 0U) {
                                snprintf(s, sizeof(s),
                                         "EXP4_WRONG_LENGTH_SLOT_CSV,slot_index=%u,valid=%u,count=%lu",
                                         (unsigned int)slot,
                                         (unsigned int)(slot !=
                                             EXP4_SLOT_CLASS_INVALID),
                                         (unsigned long)
                                             exp4_wrong_length_by_slot[slot]);
                                final_log_info(s);
                            }
                        }
                        for (frame_len = 0U; frame_len < 128U; frame_len++) {
                            if (exp4_wrong_length_hist[frame_len] > 0U) {
                                snprintf(s, sizeof(s),
                                         "EXP4_WRONG_LENGTH_VALUE_CSV,length=%u,count=%lu",
                                         (unsigned int)frame_len,
                                         (unsigned long)
                                             exp4_wrong_length_hist[frame_len]);
                                final_log_info(s);
                            }
                        }
                    }

                    snprintf(s, sizeof(s),
                             "EXP4_BURST_CSV,mode=last_valid_frame_or_schedule_deadline,scheduled_end_us=%u,early_close=%lu,deadline_close=%lu,forced_prep_close=%lu,total=%lu",
                             (unsigned)EXP4_DATA_BURST_END_US,
                             (unsigned long)exp4_burst_early_close_count,
                             (unsigned long)exp4_burst_deadline_close_count,
                             (unsigned long)exp4_burst_forced_prep_close_count,
                             (unsigned long)(exp4_burst_early_close_count +
                                             exp4_burst_deadline_close_count +
                                             exp4_burst_forced_prep_close_count));
                    final_log_info(s);

                    {
                        uint64_t service_avg_x1000 = exp4_rearm_service_stats.count ?
                            (exp4_rearm_service_stats.sum_us * 1000ULL /
                             exp4_rearm_service_stats.count) : 0;
                        snprintf(s, sizeof(s),
                                 "EXP4_REARM_CSV,mode=manual_double_buffer_burst,scope=event_specific_detect_to_rx_command,count=%lu,service_min_us=%lu,service_max_us=%lu,service_avg_x1000_us=%llu,poll_count=%lu,rx_enable_count=%lu,clear_pre_count=%lu,clear_post_count=%lu,poll_max_us=%lu,poll_detection_allowance_us=%lu,startup_allowance_us=%u,required_guard_us=%lu,delayed_schedule_late=%lu",
                                 (unsigned long)exp4_rearm_service_stats.count,
                                 (unsigned long)(exp4_rearm_service_stats.count ? exp4_rearm_service_stats.min_us : 0),
                                 (unsigned long)exp4_rearm_service_stats.max_us,
                                 (unsigned long long)service_avg_x1000,
                                 (unsigned long)exp4_status_poll_stats.count,
                                 (unsigned long)exp4_rearm_rx_enable_stats.count,
                                 (unsigned long)exp4_rearm_status_clear_pre_stats.count,
                                 (unsigned long)exp4_rearm_status_clear_post_stats.count,
                                 (unsigned long)exp4_status_poll_stats.max_us,
                                 (unsigned long)exp4_status_poll_stats.max_us,
                                 (unsigned)RX_LEAD_MARGIN_US,
                                 (unsigned long)(exp4_rearm_service_stats.max_us +
                                                 RX_LEAD_MARGIN_US),
                                 (unsigned long)total_rx_delayed_fallbacks);
                        final_log_info(s);
                    }

                    {
                        latency_stats_t *phase_stats[] = {
                            &exp4_status_poll_stats,
                            &exp4_rearm_rx_enable_stats,
                            &exp4_rearm_rdb_status_stats,
                            &exp4_rearm_rx_metadata_stats,
                            &exp4_rearm_header_read_stats,
                            &exp4_rearm_status_clear_pre_stats,
                            &exp4_rearm_status_clear_post_stats
                        };
                        const char *phase_names[] = {
                            "status_poll_detecting_read",
                            "rx_enable_critical",
                            "rdb_status_read_post_rearm",
                            "rx_metadata_read_post_rearm",
                            "data_header_read_post_rearm",
                            "status_clear_pre_rearm_error_path",
                            "status_clear_post_rearm_good_path"
                        };
                        uint8_t phase;

                        for (phase = 0U; phase < 7U; phase++) {
                            latency_stats_t *stats = phase_stats[phase];
                            uint64_t avg_x1000 = stats->count ?
                                (stats->sum_us * 1000ULL / stats->count) : 0;
                            snprintf(s, sizeof(s),
                                     "EXP4_REARM_PHASE_CSV,phase=%s,count=%lu,min_us=%lu,max_us=%lu,avg_x1000_us=%llu",
                                     phase_names[phase],
                                     (unsigned long)stats->count,
                                     (unsigned long)(stats->count ? stats->min_us : 0),
                                     (unsigned long)stats->max_us,
                                     (unsigned long long)avg_x1000);
                            final_log_info(s);
                        }
                    }

                    {
                        uint64_t free_avg_x1000 = exp4_rx_buffer_free_stats.count ?
                            (exp4_rx_buffer_free_stats.sum_us * 1000ULL /
                             exp4_rx_buffer_free_stats.count) : 0;
                        snprintf(s, sizeof(s),
                                 "EXP4_DOUBLE_BUFFER_CSV,mode=manual,rx_good_events=%lu,rdb_good_events=%lu,rdb_dispatches=%lu,rdb_host_mismatch=%lu,rdb_incomplete=%lu,rdb_incomplete_recovered=%lu,rdb_resync=%lu,free_count=%lu,free_min_us=%lu,free_max_us=%lu,free_avg_x1000_us=%llu,overrun=%lu",
                                 (unsigned long)exp4_rx_good_events,
                                 (unsigned long)exp4_rdb_good_events,
                                 (unsigned long)exp4_rdb_dispatches,
                                 (unsigned long)exp4_rdb_host_mismatches,
                                 (unsigned long)exp4_rdb_incomplete_events,
                                 (unsigned long)exp4_rdb_incomplete_recovered,
                                 (unsigned long)exp4_rdb_resync_count,
                                 (unsigned long)exp4_rx_buffer_free_stats.count,
                                 (unsigned long)(exp4_rx_buffer_free_stats.count ?
                                     exp4_rx_buffer_free_stats.min_us : 0),
                                 (unsigned long)exp4_rx_buffer_free_stats.max_us,
                                 (unsigned long long)free_avg_x1000,
                                 (unsigned long)exp4_rx_buffer_overruns);
                        final_log_info(s);
                    }

                    if (exp4_rdb_incomplete_retry_stats.count > 0U) {
                        uint64_t retry_avg_x1000 =
                            (exp4_rdb_incomplete_retry_stats.sum_us * 1000ULL /
                             exp4_rdb_incomplete_retry_stats.count);
                        snprintf(s, sizeof(s),
                                 "EXP4_RDB_RETRY_CSV,count=%lu,recovered=%lu,ciaerr=%lu,min_us=%lu,max_us=%lu,avg_x1000_us=%llu,timeout_us=%d",
                                 (unsigned long)exp4_rdb_incomplete_retry_stats.count,
                                 (unsigned long)exp4_rdb_incomplete_recovered,
                                 (unsigned long)exp4_rdb_incomplete_ciaerr,
                                 (unsigned long)exp4_rdb_incomplete_retry_stats.min_us,
                                 (unsigned long)exp4_rdb_incomplete_retry_stats.max_us,
                                 (unsigned long long)retry_avg_x1000,
                                 (int)EXP4_RDB_CIADONE_RETRY_TIMEOUT_US);
                        final_log_info(s);
                    }

                    snprintf(s, sizeof(s),
                             "EXP4_STATUS_CSV,schedule=%s,timing=%s,collection=%s,link=%s",
                             schedule_pass ? "PASS" : "FAIL",
                             timing_pass ? "PASS" : "FAIL",
                             collection_pass ? "PASS" : "FAIL",
                             link_pass ? "PASS" : "LOSS");
                    final_log_info(s);

                    snprintf(s, sizeof(s),
                             "EXP4_SUMMARY_CSV,%d,%d,%lu,%d,%d,%d,%d,%d,%d,%lu,%lu,%lu,%lu,%llu,%llu,%llu,%lu,%lu,%s",
                             PREAMBLE_SYMBOLS, active_sensor_count,
                             (unsigned long)data_slots_per_superframe,
                             BRRS_EXP4_SLOT_REPEATS, PSDU_BYTES,
                             BRRS_APP_PAYLOAD_BYTES, SLOT_INTERVAL_US,
                             SLOT_GUARD_US,
                             EXP4_MAX_DATA_SLOTS,
                             (unsigned long)total_cycles,
                             (unsigned long)expected_frames,
                             (unsigned long)exp4_frames_received,
                             (unsigned long)per_ppm,
                             (unsigned long long)goodput_bps,
                             (unsigned long long)offered_bps,
                             (unsigned long long)elapsed_us,
                             (unsigned long)total_rx_delayed_fallbacks,
                             (unsigned long)exp4_wrong_slot_frames,
                             collection_pass ? "PASS" : "FAIL");
                    final_log_info(s);

                    snprintf(s, sizeof(s),
                             "EXP4_DONE,plen=%d,physical_sensors=%d,data_slots=%lu,slot_repeats=%d,superframes=%lu,expected=%lu,rx=%lu,collection=%s,link=%s,status=%s",
                             PREAMBLE_SYMBOLS, active_sensor_count,
                             (unsigned long)data_slots_per_superframe,
                             BRRS_EXP4_SLOT_REPEATS,
                             (unsigned long)total_cycles,
                             (unsigned long)expected_frames,
                             (unsigned long)exp4_frames_received,
                             collection_pass ? "PASS" : "FAIL",
                             link_pass ? "PASS" : "LOSS",
                             collection_pass ? "PASS" : "FAIL");
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
                            final_log_info(s);
#if BRRS_EXPERIMENT == 4
                            snprintf(s, sizeof(s),
                                     "EXP4_NODE_CSV,%s,%d,%lu,%lu,%lu,%lu",
                                     get_slot_description(i), PREAMBLE_SYMBOLS,
                                     (unsigned long)expected_rx[i],
                                     (unsigned long)per_stats[i].rx_count,
                                     (unsigned long)miss,
                                     (unsigned long)per_stats[i].rx_error_count);
                            final_log_info(s);
#endif
                        }
                    }
                }

#if BRRS_EXPERIMENT == 4
                final_log_info("--- Exp4 source-to-observed-slot classification ---");
                {
                    uint8_t src_idx;
                    for (src_idx = 1U; src_idx < TOTAL_ARRAY_SIZE; src_idx++) {
                        uint8_t observed_slot;
                        for (observed_slot = 0U;
                             observed_slot < exp4_diag_slot_count;
                             observed_slot++) {
                            uint32_t count =
                                exp4_slot_class_count[src_idx][observed_slot];
                            if (count > 0U) {
                                uint8_t owner_seq =
                                    exp4_diag_slot_owner[observed_slot];
                                static char line[192];
                                snprintf(line, sizeof(line),
                                         "EXP4_SLOT_CLASS_CSV,src=%s,observed_slot=%u,observed_owner=N%u,count=%lu,host0=%lu,host1=%lu",
                                         get_slot_description(src_idx),
                                         observed_slot, owner_seq,
                                         (unsigned long)count,
                                         (unsigned long)exp4_slot_class_host_count
                                             [src_idx][observed_slot][0],
                                         (unsigned long)exp4_slot_class_host_count
                                             [src_idx][observed_slot][1]);
                                final_log_info(line);
                            }
                        }

                        if (exp4_slot_class_count[src_idx]
                                                     [EXP4_SLOT_CLASS_INVALID] > 0U) {
                            static char line[192];
                            snprintf(line, sizeof(line),
                                     "EXP4_SLOT_CLASS_CSV,src=%s,observed_slot=invalid,observed_owner=NA,count=%lu,host0=%lu,host1=%lu",
                                     get_slot_description(src_idx),
                                     (unsigned long)exp4_slot_class_count[src_idx]
                                         [EXP4_SLOT_CLASS_INVALID],
                                     (unsigned long)exp4_slot_class_host_count
                                         [src_idx][EXP4_SLOT_CLASS_INVALID][0],
                                     (unsigned long)exp4_slot_class_host_count
                                         [src_idx][EXP4_SLOT_CLASS_INVALID][1]);
                            final_log_info(line);
                        }

                        if (exp4_wrong_slot_rx_offset[src_idx].count > 0U) {
                            uint64_t avg_x1000 =
                                exp4_wrong_slot_rx_offset[src_idx].sum_us * 1000ULL /
                                exp4_wrong_slot_rx_offset[src_idx].count;
                            static char line[192];
                            snprintf(line, sizeof(line),
                                     "EXP4_WRONG_SLOT_OFFSET_CSV,src=%s,n=%lu,min_us=%lu,max_us=%lu,avg_x1000_us=%llu",
                                     get_slot_description(src_idx),
                                     (unsigned long)exp4_wrong_slot_rx_offset[src_idx].count,
                                     (unsigned long)exp4_wrong_slot_rx_offset[src_idx].min_us,
                                     (unsigned long)exp4_wrong_slot_rx_offset[src_idx].max_us,
                                     (unsigned long long)avg_x1000);
                            final_log_info(line);
                        }
                    }
                }
#endif

                {
                    static char s[256];
#if BRRS_EXPERIMENT == 4
                    snprintf(s, sizeof(s), "RX timeouts=%lu (fwto=%lu pto=%lu)  RX errors=%lu (sfdto=%lu phe=%lu fce=%lu fsl=%lu overrun=%lu)  delayed schedule late=%lu  data config errors=%lu",
                             (unsigned long)total_rx_timeouts,
                             (unsigned long)rx_to_frame,
                             (unsigned long)rx_to_preamble,
                             (unsigned long)total_rx_errors,
                             (unsigned long)rx_err_sfdto,
                             (unsigned long)rx_err_phe,
                             (unsigned long)rx_err_fce,
                             (unsigned long)rx_err_fsl,
                             (unsigned long)exp4_rx_buffer_overruns,
                             (unsigned long)total_rx_delayed_fallbacks,
                             (unsigned long)data_config_errors);
#else
                    snprintf(s, sizeof(s), "RX timeouts=%lu (fwto=%lu pto=%lu)  RX errors=%lu (sfdto=%lu phe=%lu fce=%lu fsl=%lu)  delayed schedule late=%lu  data config errors=%lu",
                             (unsigned long)total_rx_timeouts,
                             (unsigned long)rx_to_frame,
                             (unsigned long)rx_to_preamble,
                             (unsigned long)total_rx_errors,
                             (unsigned long)rx_err_sfdto,
                             (unsigned long)rx_err_phe,
                             (unsigned long)rx_err_fce,
                             (unsigned long)rx_err_fsl,
                             (unsigned long)total_rx_delayed_fallbacks,
                             (unsigned long)data_config_errors);
#endif
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

#if BRRS_EXPERIMENT != 4
                print_failed_accum_stats();
#endif

                /* [DIAG] 창 열림 -> RMARKER: 기대값 = 실제(preamble+SFD) + lead margin */
                final_log_info("--- RX-open to RMARKER (expect pre+SFD+lead) ---");
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

                final_log_info("--- UWB slot timing error (actual - scheduled RMARKER) ---");
                {
                    uint8_t i;
                    for (i = 1U; i < TOTAL_ARRAY_SIZE; i++) {
                        if (expected_rx[i] > 0U) {
                            uint32_t samples = node_slot_timing_error[i].count;
                            uint32_t received = per_stats[i].rx_count;
                            bool timing_pass = (samples == received);
                            int64_t min_ns = samples ?
                                node_slot_timing_error[i].min_ns : 0;
                            int64_t max_ns = samples ?
                                node_slot_timing_error[i].max_ns : 0;
                            int64_t avg_x1000_ns = samples ?
                                (node_slot_timing_error[i].sum_ns * 1000LL /
                                 (int64_t)samples) : 0;
                            static char s[220];

                            if (samples > 0U) {
                                snprintf(s, sizeof(s),
                                         "%s: min=%lldns max=%lldns avg=%.1fns (n=%lu)",
                                         get_slot_description(i),
                                         (long long)min_ns,
                                         (long long)max_ns,
                                         (double)node_slot_timing_error[i].sum_ns /
                                             (double)samples,
                                         (unsigned long)samples);
                                final_log_info(s);
                            }

                            snprintf(s, sizeof(s),
                                     "BRRS_SLOT_TIMING_CSV,%s,%d,%lld,%lld,%lld,%lu,%lu,%s",
                                     get_slot_description(i), PREAMBLE_SYMBOLS,
                                     (long long)min_ns,
                                     (long long)max_ns,
                                     (long long)avg_x1000_ns,
                                     (unsigned long)samples,
                                     (unsigned long)received,
                                     timing_pass ? "PASS" : "FAIL");
                            final_log_info(s);
                        }
                    }
                }

#if ENABLE_CIR
                final_log_info("--- CIR quality ---");
                {
                    uint8_t i;
                    bool cir_has_expected = false;
                    cir_final_collection_pass = run_end_ok;
                    cir_final_link_pass = true;
                    cir_final_expected = 0;
                    cir_final_rx = 0;
                    cir_final_valid = 0;

                    for (i = 0; i < TOTAL_ARRAY_SIZE; i++) {
                        uint32_t expected = expected_rx[i];
                        uint32_t received = per_stats[i].rx_count;
                        uint32_t snr_count = fp_snr_ratio_stats[i].count;
                        bool node_collection_pass;
                        bool node_link_pass;
                        uint32_t missed;
                        uint32_t per_x1000;

                        if (expected == 0U) {
                            continue;
                        }

                        cir_has_expected = true;
                        cir_final_expected += expected;
                        cir_final_rx += received;
                        cir_final_valid += snr_count;

                        node_collection_pass = (received == snr_count);
                        node_link_pass = (received == expected);
                        missed = (expected >= received) ? (expected - received) : 0U;
                        per_x1000 = (uint32_t)(((uint64_t)missed * 100000ULL +
                                                expected / 2U) / expected);

                        if (!node_collection_pass) {
                            cir_final_collection_pass = false;
                        }
                        if (!node_link_pass) {
                            cir_final_link_pass = false;
                        }

                        if (rssi_stats[i].count > 0) {
                            int32_t rssi_avg = (int32_t)(rssi_stats[i].sum_x100 / (int64_t)rssi_stats[i].count);
                            int32_t fp_avg = (int32_t)(fp_power_stats[i].sum_x100 / (int64_t)fp_power_stats[i].count);
                            int32_t gap_avg = (int32_t)(fp_gap_stats[i].sum_x100 / (int64_t)fp_gap_stats[i].count);
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
                            final_log_info(s);

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
                            final_log_info(s);
                        }

                        {
                            static char s[260];
                            snprintf(s, sizeof(s),
                                     "CIR_RUN_RESULT,%s,%d,expected=%lu,rx=%lu,cir=%lu,collection=%s,link=%s,per_x1000=%lu,status=%s",
                                     get_slot_description(i), PREAMBLE_SYMBOLS,
                                     (unsigned long)expected,
                                     (unsigned long)received,
                                     (unsigned long)snr_count,
                                     node_collection_pass ? "PASS" : "FAIL",
                                     node_link_pass ? "PASS" : "LOSS",
                                     (unsigned long)per_x1000,
                                     node_collection_pass ? "PASS" : "FAIL");
                            final_log_info(s);
                        }
                    }

                    if (!cir_has_expected ||
                        total_cycles != TARGET_CYCLES ||
                        data_config_errors != 0U ||
                        cir_final_rx == 0U ||
                        cir_sample_log_count != cir_final_valid ||
                        cir_final_valid != cir_final_rx) {
                        cir_final_collection_pass = false;
                    }
                }
#endif

#if BRRS_EXPERIMENT == 3 && EXP3_RX_STAGE_DIAG
                final_log_info("--- Experiment 3 timestamp observations ---");
                exp3_print_summary();
#endif
#if BRRS_EXPERIMENT == 3
                {
                    uint32_t expected = expected_rx[1];
                    uint32_t received = per_stats[1].rx_count;
                    uint32_t missed = (expected >= received) ?
                                      (expected - received) : 0;
                    uint32_t per_x1000 =
                        (expected > 0) ?
                        (uint32_t)(((uint64_t)missed * 100000ULL +
                                    expected / 2U) / expected) : 0;
                    bool collection_pass =
                        (total_cycles == TARGET_CYCLES &&
                         expected == TARGET_CYCLES &&
                         data_config_errors == 0U &&
                         run_end_ok);
                    static char line[260];

                    final_log_info("--- Experiment 3 RX link validation ---");
                    snprintf(line, sizeof(line),
                             "EXP3_RX_RESULT_CSV,%s,%d,%s,%d,%lu,%lu,%lu,%lu,%lu,%s",
                             EXP3_VARIANT_NAME, SFD_SYMBOLS,
                             EXP3_PHR_RATE_NAME, PSDU_BYTES,
                             (unsigned long)expected,
                             (unsigned long)received,
                             (unsigned long)missed,
                             (unsigned long)per_x1000,
                             (unsigned long)FRAME_MODEL_NS,
                             collection_pass ? "PASS" : "FAIL");
                    final_log_info(line);

                    snprintf(line, sizeof(line),
                             "EXP3_RX_DONE,variant=%s,expected=%lu,rx=%lu,per_x1000=%lu,end_tx=%lu,status=%s",
                             EXP3_VARIANT_NAME,
                             (unsigned long)expected,
                             (unsigned long)received,
                             (unsigned long)per_x1000,
                             (unsigned long)run_end_tx_count,
                             collection_pass ? "PASS" : "FAIL");
                    final_log_info(line);
                }
#endif
#if BRRS_EXPERIMENT == 1
                {
                    uint32_t expected = expected_rx[1];
                    uint32_t received = per_stats[1].rx_count;
                    uint32_t missed = (expected >= received) ?
                                      (expected - received) : 0U;
                    uint32_t per_x1000 =
                        (expected > 0U) ?
                        (uint32_t)(((uint64_t)missed * 100000ULL +
                                    expected / 2U) / expected) : 0U;
                    bool collection_pass =
                        (total_cycles == TARGET_CYCLES &&
                         expected == TARGET_CYCLES &&
                         total_rx_delayed_fallbacks == 0U &&
                         data_config_errors == 0U &&
                         run_end_ok);
                    bool link_pass = (received == expected);
                    static char line[300];

                    snprintf(line, sizeof(line),
                             "EXP1_DONE,plen=%d,lead_us=%d,tail_us=%d,expected=%lu,rx=%lu,delayed_late=%lu,data_config_errors=%lu,end_tx=%lu,collection=%s,link=%s,per_x1000=%lu,status=%s",
                             PREAMBLE_SYMBOLS,
                             RX_LEAD_MARGIN_US,
                             RX_TAIL_MARGIN_US,
                             (unsigned long)expected,
                             (unsigned long)received,
                             (unsigned long)total_rx_delayed_fallbacks,
                             (unsigned long)data_config_errors,
                             (unsigned long)run_end_tx_count,
                             collection_pass ? "PASS" : "FAIL",
                             link_pass ? "PASS" : "LOSS",
                             (unsigned long)per_x1000,
                             collection_pass ? "PASS" : "FAIL");
                    final_log_info(line);
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
#if BRRS_EXPERIMENT == 3 && EXP3_RX_STAGE_DIAG
                exp3_dump_samples();
#endif
                break;
            }

            /* [A-3] Each superframe has one fresh DATA schedule. */
            memset(data_received_from, 0, sizeof(data_received_from));

            /* [NEW] 슬롯 예약 상태 리셋 */
            memset(slots_scheduled, 0, sizeof(slots_scheduled));
            current_rx_slot = 0xFF;

            /* [A-4] SYNC TX */
            {
#if BRRS_EXPERIMENT == 4
                bool delayed_sync = (last_sync_tx_ts_high32 != 0);
                uint32_t previous_sync_tx_high32 = last_sync_tx_ts_high32;
                uint32_t requested_sync_tx_high32 = exp4_next_sync_tx_high32;
                uint32_t actual_sync_tx_high32 = 0;
                bool sync_timing_exact = exp4_transmit_control_frame(
                    MSG_TYPE_SYNC,
                    (uint16_t)current_cycle,
                    delayed_sync,
                    requested_sync_tx_high32,
                    &config_is_sync,
                    &actual_sync_tx_high32);

                if (actual_sync_tx_high32 == 0) {
                    test_run_info((unsigned char *)"DBG: SYNC TX FAILED!");
                    break;
                }

                last_sync_tx_ts_high32 = actual_sync_tx_high32;
                last_sync_cycles = dwt_timer_get_cycles() - us_to_cpu_cycles(BEACON_PHR_PSDU_US);

                if (current_cycle == 2 || (current_cycle % 100U) == 0U) {
                    static char progress_msg[96];
                    snprintf(progress_msg, sizeof(progress_msg),
                             "EXP4_SYNC_PROGRESS,seq=%lu,delayed_late=%lu,tx_wait_timeout=%lu",
                             (unsigned long)current_cycle,
                             (unsigned long)exp4_sync_tx_delayed_late,
                             (unsigned long)exp4_tx_wait_timeouts);
                    test_run_info((unsigned char *)progress_msg);
                }

                if (!delayed_sync) {
                    exp4_measurement_start_high32 = actual_sync_tx_high32;
                    exp4_next_sync_tx_high32 = actual_sync_tx_high32 +
                        (uint32_t)(US_TO_DWT_TIME(BRRS_SUPERFRAME_US) >> 8);
                } else {
                    exp4_record_period(previous_sync_tx_high32,
                                       actual_sync_tx_high32);
                    if (sync_timing_exact) {
                        exp4_next_sync_tx_high32 = requested_sync_tx_high32 +
                            (uint32_t)(US_TO_DWT_TIME(BRRS_SUPERFRAME_US) >> 8);
                    } else {
                        exp4_next_sync_tx_high32 = actual_sync_tx_high32 +
                            (uint32_t)(US_TO_DWT_TIME(BRRS_SUPERFRAME_US) >> 8);
                    }
                }
#else
                dwt_forcetrxoff();
                if (dwt_configure(&config_sync) == DWT_SUCCESS) {
                    config_is_sync = true;
#if ENABLE_CIR
                    enable_cir_diagnostics();
#endif
                }

                current_active_node_bitmap = brrs_configured_sensor_bitmap();
                brrs_prepare_beacon(MSG_TYPE_SYNC, (uint16_t)current_cycle,
                                    current_active_node_bitmap);

                dwt_setrxaftertxdelay(0);
                dwt_writetxdata(BRRS_BEACON_PSDU_BYTES, beacon_msg, 0);
                dwt_writetxfctrl(BRRS_BEACON_PSDU_BYTES, 0, 0);

                last_sync_cycles = dwt_timer_get_cycles();

                /* [CHANGED] DWT_RESPONSE_EXPECTED 제거 - delayed-RX 직접 제어 */
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
#endif

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
                } else {
                    static char config_error_line[112];
                    data_config_errors++;
                    config_is_sync = true;
                    current_rx_slot = 0xFF;
                    snprintf(config_error_line, sizeof(config_error_line),
                             "BRRS_DATA_CONFIG_ERROR,role=INIT,experiment=%d,superframe=%lu,count=%lu",
                             BRRS_EXPERIMENT,
                             (unsigned long)current_cycle,
                             (unsigned long)data_config_errors);
                    test_run_info((unsigned char *)config_error_line);
                }
                dwt_setrxaftertxdelay(TX_TO_RX_DELAY_UUS);

                /* [NEW] 첫 번째 Normal 슬롯에 대한 delayed-RX 예약
                 * 실험 1/2/3: Normal 노드들이 SEQ 2부터 시작
                 * 실험 4: INIT도 SEQ 1 슬롯 사용 (별도 처리)
                 */
#if BRRS_EXPERIMENT == 1 || BRRS_EXPERIMENT == 2 || BRRS_EXPERIMENT == 3
                if (!config_is_sync && current_beacon_config.slot_count > 0U &&
                    !slots_scheduled[0]) {
                    schedule_rx_slot(0);
                }
#elif BRRS_EXPERIMENT == 4
                if (!config_is_sync && current_beacon_config.slot_count > 0U &&
                    !slots_scheduled[0]) {
                    if (schedule_rx_slot(0)) {
                        exp4_data_burst_active = true;
                    }
                }
#endif
            }
        }

        /* ========== [B] Config switch (SYNC 준비) ========== */
#if BRRS_EXPERIMENT != 4
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
#endif

        /* ========== [D] RX 폴링 ========== */
        {
#if BRRS_EXPERIMENT == 3 && EXP3_RX_STAGE_DIAG
            uint32_t status_poll_start = dwt_timer_get_cycles();
            uint32_t status_reg = dwt_readsysstatuslo();
            uint32_t status_poll_end = dwt_timer_get_cycles();
            exp3_capture_status(status_reg, status_poll_start, status_poll_end);
#elif BRRS_EXPERIMENT == 4
            uint32_t exp4_status_poll_start_cycles = dwt_timer_get_cycles();
            uint8_t exp4_fint_status = 0U;
            uint32_t status_reg = 0U;

            /* FINT_STAT is the driver's fast masked aggregate event source.
             * Its RX masks were verified at boot. Polling one byte keeps the
             * good-frame path short; detailed SYS_STATUS is needed only to
             * classify an RX error or timeout. */
            dwt_readfromdevice(FINT_STAT_ID, 0U, 1U, &exp4_fint_status);
            uint32_t exp4_status_poll_cycles =
                dwt_timer_get_cycles() - exp4_status_poll_start_cycles;
            uint32_t exp4_status_poll_us =
                (exp4_status_poll_cycles + (CPU_FREQ_HZ / 1000000UL) - 1UL) /
                (CPU_FREQ_HZ / 1000000UL);
            if ((exp4_fint_status &
                 (FINT_STAT_RXERR_BIT_MASK |
                  FINT_STAT_RXTO_BIT_MASK)) != 0U) {
                status_reg = dwt_readsysstatuslo();
            }
            if (exp4_data_burst_active &&
                (exp4_fint_status & FINT_STAT_RXOK_BIT_MASK) != 0U) {
                status_reg |= DWT_INT_RXFCG_BIT_MASK;
            }
#else
            uint32_t status_reg = dwt_readsysstatuslo();
#endif

#if BRRS_EXPERIMENT == 4
            if (status_reg & DWT_INT_RXOVRR_BIT_MASK) {
                exp4_rx_buffer_overruns++;
                total_rx_errors++;
                exp4_close_data_burst(EXP4_BURST_CLOSE_DEADLINE);
                dwt_setdblrxbuffmode(DBL_BUF_STATE_DIS, DBL_BUF_MODE_MAN);
                dwt_setdblrxbuffmode(DBL_BUF_STATE_EN, DBL_BUF_MODE_MAN);
                exp4_rx_host_buffer = 0U;
            }
            else
#endif
            if (status_reg & DWT_INT_RXFCG_BIT_MASK) {
#if BRRS_EXPERIMENT == 4
                uint32_t exp4_event_start_cycles = exp4_status_poll_start_cycles;
                uint32_t exp4_phase_start_cycles;
                uint32_t exp4_phase_cycles;
                uint32_t exp4_phase_us;
                uint32_t rx_ts_high32;
                uint32_t rx_open_high32 = last_rx_open_high32;
                uint16_t rx_frame_len;
                uint16_t header_read_len;
                uint8_t completed_rx_slot = current_rx_slot;
                uint8_t observed_rx_slot = completed_rx_slot;
                bool observed_slot_valid = false;
                bool frame_length_valid;
                bool exp4_slot_event_processed = false;
                bool rx_open_timing_valid = current_rx_open_timing_valid;
                bool exp4_rearm_needed = exp4_has_later_slot();
                uint8_t exp4_rdb_status = 0U;
                uint8_t exp4_rdb_current_good_mask;
                uint8_t exp4_rdb_current_ready_mask;
                uint8_t completed_host_buffer = exp4_rx_host_buffer;

                if (exp4_rearm_needed) {
                    update_node_latency(&exp4_status_poll_stats,
                                        exp4_status_poll_us);
                }

                /* Manual double buffering lets the other buffer receive while
                 * the completed buffer is still owned by the host. Re-enable
                 * RX first, then clear this event before the next frame can
                 * complete and assert a fresh RXFCG. */
                exp4_rearm_after_event(exp4_event_start_cycles);

                /* Read RDB_STATUS before touching any buffer-specific
                 * register (DW3000 manual section 4.4, Figure 17: "Check
                 * RDB_STATUS" determines SET_1 vs SET_2 before reading
                 * either). exp4_rx_host_buffer is our own bookkeeping and
                 * can drift from the DW3000's actual double-buffer swing
                 * state; resync from RDB_STATUS truth instead of trusting
                 * the tracked index blindly. */
                exp4_phase_start_cycles = dwt_timer_get_cycles();
                dwt_readfromdevice(RDB_STATUS_ID, 0U, 1U,
                                   &exp4_rdb_status);
                exp4_phase_cycles =
                    dwt_timer_get_cycles() - exp4_phase_start_cycles;
                exp4_phase_us =
                    (exp4_phase_cycles + (CPU_FREQ_HZ / 1000000UL) - 1UL) /
                    (CPU_FREQ_HZ / 1000000UL);
                if (exp4_rearm_needed) {
                    update_node_latency(&exp4_rearm_rdb_status_stats,
                                        exp4_phase_us);
                }
                {
                    bool good0 = (exp4_rdb_status &
                                  DWT_RDB_STATUS_RXFCG0_BIT_MASK) != 0U;
                    bool good1 = (exp4_rdb_status &
                                  DWT_RDB_STATUS_RXFCG1_BIT_MASK) != 0U;
                    if (good0 && !good1) {
                        completed_host_buffer = 0U;
                    } else if (good1 && !good0) {
                        completed_host_buffer = 1U;
                    }
                    /* Else neither (handled as a mismatch below) or both
                     * appear ready -- keep the tracked guess. */
                    if (completed_host_buffer != exp4_rx_host_buffer) {
                        exp4_rdb_resync_count++;
                        {
                            static char resync_line[150];
                            snprintf(resync_line, sizeof(resync_line),
                                     "EXP4_RDB_RESYNC_CSV,count=%lu,slot=%u,tracked=%u,actual=%u,rdb_status=0x%02X",
                                     (unsigned long)exp4_rdb_resync_count,
                                     (unsigned int)completed_rx_slot,
                                     (unsigned int)exp4_rx_host_buffer,
                                     (unsigned int)completed_host_buffer,
                                     (unsigned int)exp4_rdb_status);
                            test_run_info((unsigned char *)resync_line);
                        }
                        exp4_rx_host_buffer = completed_host_buffer;
                    }
                }

                /* Cache adjacent FINFO and RX_TIME in one transaction before
                 * the next slot can advance the double-buffer swinging set.
                 * completed_host_buffer is now resynced against hardware
                 * truth above. */
                exp4_phase_start_cycles = dwt_timer_get_cycles();
                exp4_read_rx_metadata(completed_host_buffer, &rx_ts_high32,
                                      &rx_frame_len);
                exp4_phase_cycles =
                    dwt_timer_get_cycles() - exp4_phase_start_cycles;
                exp4_phase_us =
                    (exp4_phase_cycles + (CPU_FREQ_HZ / 1000000UL) - 1UL) /
                    (CPU_FREQ_HZ / 1000000UL);
                if (exp4_rearm_needed) {
                    update_node_latency(&exp4_rearm_rx_metadata_stats,
                                        exp4_phase_us);
                }

                /* RXFR + RXFCG identifies a complete CRC-valid frame;
                 * CIADONE validates the adjusted receive timestamp. */
                exp4_rdb_current_good_mask = (completed_host_buffer == 0U) ?
                    DWT_RDB_STATUS_RXFCG0_BIT_MASK :
                    DWT_RDB_STATUS_RXFCG1_BIT_MASK;
                exp4_rdb_current_ready_mask = (completed_host_buffer == 0U) ?
                    (DWT_RDB_STATUS_RXFCG0_BIT_MASK |
                     DWT_RDB_STATUS_RXFR0_BIT_MASK |
                     DWT_RDB_STATUS_CIADONE0_BIT_MASK) :
                    (DWT_RDB_STATUS_RXFCG1_BIT_MASK |
                     DWT_RDB_STATUS_RXFR1_BIT_MASK |
                     DWT_RDB_STATUS_CIADONE1_BIT_MASK);
                if ((exp4_rdb_status & exp4_rdb_current_good_mask) == 0U) {
                    exp4_rdb_host_mismatches++;
                    total_rx_errors++;
                    {
                        static char diag_line[160];
                        snprintf(diag_line, sizeof(diag_line),
                                 "EXP4_RDB_DIAG_CSV,kind=mismatch,count=%lu,slot=%u,host_buffer=%u,rdb_status=0x%02X,sys_status=0x%08lX",
                                 (unsigned long)exp4_rdb_host_mismatches,
                                 (unsigned int)completed_rx_slot,
                                 (unsigned int)exp4_rx_host_buffer,
                                 (unsigned int)exp4_rdb_status,
                                 (unsigned long)dwt_readsysstatuslo());
                        test_run_info((unsigned char *)diag_line);
                    }
                    exp4_close_data_burst(EXP4_BURST_CLOSE_DEADLINE);
                    dwt_setdblrxbuffmode(DBL_BUF_STATE_DIS,
                                         DBL_BUF_MODE_MAN);
                    dwt_setdblrxbuffmode(DBL_BUF_STATE_EN,
                                         DBL_BUF_MODE_MAN);
                    exp4_rx_host_buffer = 0U;
                    continue;
                }
                if ((exp4_rdb_status & exp4_rdb_current_ready_mask) !=
                    exp4_rdb_current_ready_mask) {
                    /* RXFCG passed (checked above), but per the DW3000
                     * manual (SYS_STATUS RXFR/CIADONE description, section
                     * 4.4), RXFRx is only set once CIA processing has
                     * concluded -- either successfully (CIADONEx) or with
                     * an error, reported via the *global* SYS_STATUS
                     * CIAERR bit (RDB_STATUS has no per-buffer CIAERR0/1).
                     * If CIA erred, CIADONEx will never be set, so a short
                     * settling wait is still worth trying (in case this is
                     * genuinely just late), but stop immediately once
                     * CIAERR appears instead of waiting out the full
                     * timeout for a bit that cannot arrive. The next
                     * slot's RX is already re-armed at this point, so this
                     * wait does not cost any guard budget. */
                    uint32_t retry_start_cycles = dwt_timer_get_cycles();
                    uint32_t retry_elapsed_us = 0U;
                    bool cia_error = false;
                    for (;;) {
                        dwt_readfromdevice(RDB_STATUS_ID, 0U, 1U,
                                           &exp4_rdb_status);
                        if ((exp4_rdb_status & exp4_rdb_current_ready_mask) ==
                            exp4_rdb_current_ready_mask) {
                            break;
                        }
                        if (dwt_readsysstatuslo() & DWT_INT_CIAERR_BIT_MASK) {
                            cia_error = true;
                            dwt_writesysstatuslo(DWT_INT_CIAERR_BIT_MASK);
                            break;
                        }
                        retry_elapsed_us =
                            (dwt_timer_get_cycles() - retry_start_cycles) /
                            (CPU_FREQ_HZ / 1000000UL);
                        if (retry_elapsed_us >= EXP4_RDB_CIADONE_RETRY_TIMEOUT_US) {
                            break;
                        }
                    }
                    retry_elapsed_us =
                        (dwt_timer_get_cycles() - retry_start_cycles) /
                        (CPU_FREQ_HZ / 1000000UL);
                    update_node_latency(&exp4_rdb_incomplete_retry_stats,
                                        retry_elapsed_us);
                    if ((exp4_rdb_status & exp4_rdb_current_ready_mask) ==
                        exp4_rdb_current_ready_mask) {
                        exp4_rdb_incomplete_recovered++;
                    } else {
                        if (cia_error) {
                            exp4_rdb_incomplete_ciaerr++;
                        }
                        exp4_rdb_incomplete_events++;
                        total_rx_errors++;
                        {
                            static char diag_line[190];
                            snprintf(diag_line, sizeof(diag_line),
                                     "EXP4_RDB_DIAG_CSV,kind=incomplete,count=%lu,slot=%u,host_buffer=%u,rdb_status=0x%02X,expected_mask=0x%02X,sys_status=0x%08lX,ciaerr=%u,wait_us=%lu",
                                     (unsigned long)exp4_rdb_incomplete_events,
                                     (unsigned int)completed_rx_slot,
                                     (unsigned int)exp4_rx_host_buffer,
                                     (unsigned int)exp4_rdb_status,
                                     (unsigned int)exp4_rdb_current_ready_mask,
                                     (unsigned long)dwt_readsysstatuslo(),
                                     cia_error ? 1U : 0U,
                                     (unsigned long)retry_elapsed_us);
                            test_run_info((unsigned char *)diag_line);
                        }
                        exp4_close_data_burst(EXP4_BURST_CLOSE_DEADLINE);
                        dwt_setdblrxbuffmode(DBL_BUF_STATE_DIS,
                                             DBL_BUF_MODE_MAN);
                        dwt_setdblrxbuffmode(DBL_BUF_STATE_EN,
                                             DBL_BUF_MODE_MAN);
                        exp4_rx_host_buffer = 0U;
                        continue;
                    }
                }
                exp4_rdb_good_events++;
                exp4_rdb_dispatches++;
                exp4_rx_good_events++;
                exp4_clear_rx_status(SYS_STATUS_ALL_RX_GOOD,
                                     exp4_rearm_needed ?
                                         &exp4_rearm_status_clear_post_stats : NULL);

                observed_slot_valid = exp4_find_slot_by_rx_timestamp(
                    rx_ts_high32, &observed_rx_slot);
                frame_length_valid = (rx_frame_len == PSDU_BYTES);
                if (!frame_length_valid) {
                    exp4_wrong_length_frames++;
                    exp4_wrong_length_by_buffer[completed_host_buffer & 1U]++;
                    exp4_wrong_length_by_slot[observed_slot_valid ?
                        observed_rx_slot : EXP4_SLOT_CLASS_INVALID]++;
                    if (rx_frame_len < 128U) {
                        exp4_wrong_length_hist[rx_frame_len]++;
                    }
                }

                /* Exp4 validates only the protocol header. Avoid copying the
                 * unused payload while the next slot is already receiving. */
                memset(rx_buffer, 0, sizeof(rx_buffer));
                exp4_phase_start_cycles = dwt_timer_get_cycles();
                header_read_len = (rx_frame_len < IDX_DATA_PAYLOAD) ?
                                  rx_frame_len : IDX_DATA_PAYLOAD;
                if (header_read_len > 0U) {
                    exp4_read_rx_buffer(completed_host_buffer, rx_buffer,
                                        header_read_len, 0U);
                }
                exp4_phase_cycles =
                    dwt_timer_get_cycles() - exp4_phase_start_cycles;
                exp4_phase_us =
                    (exp4_phase_cycles + (CPU_FREQ_HZ / 1000000UL) - 1UL) /
                    (CPU_FREQ_HZ / 1000000UL);
                if (exp4_rearm_needed) {
                    update_node_latency(&exp4_rearm_header_read_stats,
                                        exp4_phase_us);
                }

                /* All buffer-specific values are cached now. Return this
                 * buffer and advance the driver's host pointer. */
                exp4_release_rx_buffer();
#else
                uint32_t rx_ts_high32;
                uint32_t rx_open_high32;
                uint16_t rx_frame_len;
                uint8_t completed_rx_slot;
                bool frame_length_valid;
                bool rx_open_timing_valid = true;

                rx_ts_high32 = dwt_readrxtimestamphi32();
                rx_open_high32 = last_rx_open_high32;
                rx_frame_len = dwt_getframelength(0);
                frame_length_valid = (rx_frame_len == PSDU_BYTES);
                completed_rx_slot = current_rx_slot;
#endif

#if BRRS_EXPERIMENT != 4
                memset(rx_buffer, 0, sizeof(rx_buffer));
                if (rx_frame_len > 0U && rx_frame_len <= sizeof(rx_buffer)) {
                    dwt_readrxdata(rx_buffer, rx_frame_len, 0);
                }
#endif

                uint8_t msg_type = rx_buffer[IDX_MSG_TYPE];
                uint8_t src_node = rx_buffer[IDX_SOURCE];
                uint8_t src_idx = node_id_to_index(src_node);

                /* [D-1] DATA 수신 */
                if (frame_length_valid && msg_type == MSG_TYPE_DATA &&
                    rx_buffer[IDX_PROTOCOL_VERSION] == BRRS_PROTOCOL_VERSION) {
                    if (src_idx < TOTAL_ARRAY_SIZE && src_idx != 0) {
                        bool slot_matches = true;
                        bool accept_new_frame = false;
                        uint32_t expected_slot_start_us = 0U;
#if BRRS_EXPERIMENT == 4
                        uint8_t expected_owner_seq = 0U;
                        uint8_t class_slot = EXP4_SLOT_CLASS_INVALID;
                        uint32_t classified_rx_offset_us = 0U;
#endif

#if BRRS_EXPERIMENT == 4
                        if (observed_slot_valid) {
                            expected_owner_seq =
                                current_beacon_config.slot_owner[observed_rx_slot];
                            expected_slot_start_us =
                                current_beacon_config.first_slot_offset_us +
                                (uint32_t)observed_rx_slot *
                                current_beacon_config.slot_interval_us;
                            class_slot = observed_rx_slot;
                        }

                        exp4_slot_class_count[src_idx][class_slot]++;
                        exp4_slot_class_host_count[src_idx][class_slot]
                                                   [completed_host_buffer & 1U]++;
                        classified_rx_offset_us = dwt_high32_delta_to_us(
                            last_sync_tx_ts_high32, rx_ts_high32);

                        if (!observed_slot_valid ||
                            src_idx != (uint8_t)(expected_owner_seq - 1U)) {
                            exp4_wrong_slot_frames++;
                            update_node_latency(
                                &exp4_wrong_slot_rx_offset[src_idx],
                                classified_rx_offset_us);
                            slot_matches = false;
                        }
                        if (exp4_read_superframe_seq(rx_buffer) != (uint16_t)current_cycle) {
                            exp4_wrong_superframe_frames++;
                            slot_matches = false;
                        }

                        if (slot_matches) {
                            exp4_slot_event_processed = true;
                            if (!exp4_slot_received[observed_rx_slot]) {
                                exp4_slot_received[observed_rx_slot] = 1U;
                                accept_new_frame = true;
                            }
                        }
#else
                        if (completed_rx_slot < current_beacon_config.slot_count) {
                            expected_slot_start_us =
                                current_beacon_config.first_slot_offset_us +
                                (uint32_t)completed_rx_slot *
                                current_beacon_config.slot_interval_us;
                        }
                        if (slot_matches && !data_received_from[src_idx]) {
                            data_received_from[src_idx] = 1U;
                            accept_new_frame = true;
                        }
#endif
                        if (accept_new_frame) {
                            per_stats[src_idx].rx_count++;
#if BRRS_EXPERIMENT == 4
                            exp4_frames_received++;
                            exp4_payload_bytes_received += BRRS_APP_PAYLOAD_BYTES;
                            exp4_received_in_current_superframe++;
                            if (exp4_received_in_current_superframe >=
                                current_beacon_config.slot_count) {
                                data_success_in_current_cycle = true;
                            }
#endif

                            uint32_t uwb_rx_offset_us = dwt_high32_delta_to_us(last_sync_tx_ts_high32,
                                                                               rx_ts_high32);
                            update_node_latency(&node_uwb_rx_offset[src_idx], uwb_rx_offset_us);

#if BRRS_EXPERIMENT == 4
                            if (observed_rx_slot < current_beacon_config.slot_count) {
#else
                            if (completed_rx_slot < current_beacon_config.slot_count) {
#endif
                                uint32_t actual_offset_high32 =
                                    rx_ts_high32 - last_sync_tx_ts_high32;
                                uint32_t expected_offset_high32 = (uint32_t)
                                    (US_TO_DWT_TIME(expected_slot_start_us) >> 8);
                                update_signed_timing(
                                    &node_slot_timing_error[src_idx],
                                    dwt_slot_error_to_ns(actual_offset_high32,
                                                         expected_offset_high32));
                            }

                            /* [DIAG] 창 열림 -> RMARKER 도착 오프셋 (기대값: preamble+SFD+lead) */
#if BRRS_EXPERIMENT == 4
                            if (rx_open_timing_valid && observed_slot_valid &&
                                observed_rx_slot == completed_rx_slot) {
#else
                            if (rx_open_timing_valid) {
#endif
                                update_node_latency(&node_open_to_rmarker[src_idx],
                                                    dwt_high32_delta_to_us(rx_open_high32, rx_ts_high32));
                            }

                            /* [DIAG] Ipatov accumCount (누산된 preamble 심볼 수) */
#if BRRS_EXPERIMENT != 4
                            {
                                dwt_cirdiags_t rx_diag;
                                if (dwt_readdiagnostics_acc(&rx_diag, DWT_ACC_IDX_IP_M) == DWT_SUCCESS) {
                                    uint16_t acc = rx_diag.accumCount;
                                    update_node_latency(&node_accum[src_idx], acc);
                                    if (acc > PREAMBLE_SYMBOLS) acc = PREAMBLE_SYMBOLS;
                                    accum_hist[acc]++;
                                }
                            }
#endif
#if ENABLE_CIR
                            log_cir_quality(src_idx, per_stats[src_idx].rx_count, current_cycle);
#endif
#if BRRS_EXPERIMENT == 3 && EXP3_RX_STAGE_DIAG
                            if (current_cycle <= TARGET_CYCLES) {
                                exp3_store_success(src_idx, per_stats[src_idx].rx_count,
                                                   rx_ts_high32);
                            }
#endif
                        }
                    }
                }

#if BRRS_EXPERIMENT == 4
                if (exp4_slot_event_processed) {
                    exp4_advance_after_event(observed_rx_slot, true);
                }
#endif

                /* 실험 1~3은 진단값을 읽은 다음 다음 슬롯을 예약한다. */
#if BRRS_EXPERIMENT != 4
#if BRRS_EXPERIMENT == 3 && EXP3_RX_STAGE_DIAG
                exp3_trace_cancel();
#endif
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);

                if (current_rx_slot != 0xFF && (current_rx_slot + 1) < TOTAL_ARRAY_SIZE) {
                    schedule_rx_slot(current_rx_slot + 1);
                } else {
                    current_rx_slot = 0xFF;
                    /* 다음 SYNC TX 대기 - RX 안 켬 */
                }
#endif
            }

            /* RX timeout - 예약된 윈도우 안에서 패킷 못 받음 (PER에 반영) */
            else if (status_reg & SYS_STATUS_ALL_RX_TO) {
#if BRRS_EXPERIMENT == 4
                uint32_t exp4_event_start_cycles = exp4_status_poll_start_cycles;
                uint8_t failed_rx_slot = exp4_slot_from_event_time();
                bool exp4_rearm_needed = exp4_has_later_slot();
                if (exp4_rearm_needed) {
                    update_node_latency(&exp4_status_poll_stats,
                                        exp4_status_poll_us);
                }
                exp4_clear_rx_status(SYS_STATUS_ALL_RX_TO |
                                     DWT_INT_CIADONE_BIT_MASK,
                                     exp4_rearm_needed ?
                                         &exp4_rearm_status_clear_pre_stats : NULL);
                exp4_rearm_after_event(exp4_event_start_cycles);
#endif
#if BRRS_EXPERIMENT == 3 && EXP3_RX_STAGE_DIAG
                exp3_trace_cancel();
#endif
#if BRRS_EXPERIMENT != 4
                /* Read CIA diagnostics before clearing status or forcing RX off. */
                capture_failed_accum(status_reg);
#endif
#if BRRS_EXPERIMENT != 4
                dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO);
#endif
                total_rx_timeouts++;
                /* [DIAG] timeout 종류 구분 */
                if (status_reg & DWT_INT_RXFTO_BIT_MASK) rx_to_frame++;
                if (status_reg & DWT_INT_RXPTO_BIT_MASK) rx_to_preamble++;
#if BRRS_EXPERIMENT == 4
                exp4_advance_after_event(failed_rx_slot, false);
#else
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);

                /* [CHANGED] 타임아웃 후 다음 슬롯 예약 */
                if (current_rx_slot != 0xFF && (current_rx_slot + 1) < TOTAL_ARRAY_SIZE) {
                    schedule_rx_slot(current_rx_slot + 1);
                } else {
                    current_rx_slot = 0xFF;
                }
#endif
            }

            /* RX errors */
            else if (status_reg & SYS_STATUS_ALL_RX_ERR) {
                uint8_t failed_rx_slot = current_rx_slot;
#if BRRS_EXPERIMENT == 4
                uint32_t exp4_event_start_cycles = exp4_status_poll_start_cycles;
                failed_rx_slot = exp4_slot_from_event_time();
                bool exp4_rearm_needed = exp4_has_later_slot();
                if (exp4_rearm_needed) {
                    update_node_latency(&exp4_status_poll_stats,
                                        exp4_status_poll_us);
                }
                exp4_clear_rx_status(SYS_STATUS_ALL_RX_ERR |
                                     DWT_INT_CIADONE_BIT_MASK |
                                     DWT_INT_RXFR_BIT_MASK,
                                     exp4_rearm_needed ?
                                         &exp4_rearm_status_clear_pre_stats : NULL);
                exp4_rearm_after_event(exp4_event_start_cycles);
#endif
#if BRRS_EXPERIMENT == 3 && EXP3_RX_STAGE_DIAG
                exp3_trace_cancel();
#endif
#if BRRS_EXPERIMENT != 4
                /* RXPRD distinguishes current-frame diagnostics from stale raw data. */
                capture_failed_accum(status_reg);
#endif
                total_rx_errors++;
                /* [DIAG] 에러 종류 구분 */
                if (status_reg & DWT_INT_RXSTO_BIT_MASK) rx_err_sfdto++;
                if (status_reg & DWT_INT_RXPHE_BIT_MASK) rx_err_phe++;
                if (status_reg & DWT_INT_RXFCE_BIT_MASK) rx_err_fce++;
                if (status_reg & DWT_INT_RXFSL_BIT_MASK) rx_err_fsl++;
                if (failed_rx_slot < current_beacon_config.slot_count) {
                    uint8_t owner_seq = current_beacon_config.slot_owner[failed_rx_slot];
                    uint8_t owner_idx = (uint8_t)(owner_seq - 1U);
                    if (owner_seq >= 2U && owner_idx < TOTAL_ARRAY_SIZE) {
                        per_stats[owner_idx].rx_error_count++;
                    }
                }
#if BRRS_EXPERIMENT == 4
                exp4_advance_after_event(failed_rx_slot, false);
#else
                dwt_writesysstatuslo(SYS_STATUS_ALL_RX_ERR);
                dwt_forcetrxoff();
                dwt_writesysstatuslo(0xFFFFFFFF);

                /* [CHANGED] 에러 후 다음 슬롯 예약 */
                if (current_rx_slot != 0xFF && (current_rx_slot + 1) < TOTAL_ARRAY_SIZE) {
                    schedule_rx_slot(current_rx_slot + 1);
                } else {
                    current_rx_slot = 0xFF;
                }
#endif
            }
        }

#if BRRS_EXPERIMENT == 4
        if (exp4_data_burst_active && last_sync_cycles != 0U &&
            dwt_timer_elapsed(last_sync_cycles,
                              us_to_cpu_cycles(EXP4_DATA_BURST_END_US))) {
            exp4_close_data_burst(EXP4_BURST_CLOSE_DEADLINE);
        }
#endif

    } /* end while */

    return 0;
}

#endif /* TEST_BRRS_INIT */
