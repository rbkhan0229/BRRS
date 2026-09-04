#ifndef BRRS_RX_ERROR_DIAG_H
#define BRRS_RX_ERROR_DIAG_H

/* RAM-only diagnostics. Register bits follow DW3000 User Manual v1.1,
 * SYS_STATUS octets 0..5 (pp93-100). No SPI, logging or dynamic allocation.
 * Bit occurrences are not mutually exclusive packet-loss classifications.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BRRS_RXERR_QUEUE_CAPACITY 32U
#define BRRS_RXERR_SAMPLE_CAPACITY 64U
#define BRRS_RXERR_STATUS_BITS 48U
#define BRRS_RXERR_TIMINGS 4U

typedef struct {
    uint32_t event, sf, pre_lo, post_lo;
    uint32_t pre_cycles, post_cycles, fint_cycles, rearm_cycles;
    uint16_t pre_hi, post_hi;
    uint8_t timeout, logical_slot, estimated_slot, host;
    uint8_t poll_fint, post_fint, rearmed;
} brrs_rxerr_record_t;

typedef struct {
    uint32_t count, min_us, max_us;
    uint64_t sum_us;
} brrs_rxerr_timing_t;

typedef struct {
    brrs_rxerr_record_t queue[BRRS_RXERR_QUEUE_CAPACITY];
    brrs_rxerr_record_t samples[BRRS_RXERR_SAMPLE_CAPACITY];
    uint32_t events, errors, timeouts, processed, rearmed, queue_overflow;
    uint32_t queue_count, sample_count, samples_omitted;
    uint32_t pre_zero, post_zero, changed, hw_faults;
    uint32_t pre_bits[BRRS_RXERR_STATUS_BITS];
    uint32_t post_bits[BRRS_RXERR_STATUS_BITS];
    brrs_rxerr_timing_t timing[BRRS_RXERR_TIMINGS];
} brrs_rxerr_diag_t;

static inline void brrs_rxerr_enqueue(brrs_rxerr_diag_t *diag,
                                     brrs_rxerr_record_t *record)
{
    record->event = ++diag->events;
    if (record->timeout) diag->timeouts++;
    else diag->errors++;
    if (diag->queue_count == BRRS_RXERR_QUEUE_CAPACITY) {
        diag->queue_overflow++;
        return;
    }
    diag->queue[diag->queue_count++] = *record;
}

static inline void brrs_rxerr_observe(brrs_rxerr_timing_t *timing,
                                     uint32_t cycles, uint32_t cycles_per_us)
{
    uint32_t us = cycles / cycles_per_us + ((cycles % cycles_per_us) != 0U);
    if (timing->count == 0U || us < timing->min_us) timing->min_us = us;
    if (us > timing->max_us) timing->max_us = us;
    timing->count++;
    timing->sum_us += us;
}

static inline bool brrs_rxerr_hardware_fault(uint32_t lo, uint16_t hi)
{
    /* VWARN, RXOVRR, PLL_HILO, HPDWARN; CMD_ERR/SPI_OVF/SPI_UNF/SPIERR.
     * Do NOT treat SPICRCE(bit2) as an error: CRC-off can leave it asserted.
     * These are observations of latched bits, not distinct fault counts.
     */
    const uint32_t lo_mask = (1UL << 19) | (1UL << 20) |
                             (1UL << 25) | (1UL << 27);
    return (lo & lo_mask) != 0U || (hi & 0x0F00U) != 0U;
}

/* Called only after DATA RX is stopped. All events contribute to counters;
 * first64 raw examples are retained and intentional omissions are explicit.
 * A queue overflow is different: information was lost and run must FAIL.
 */
static inline void brrs_rxerr_process(brrs_rxerr_diag_t *diag,
                                     uint32_t cycles_per_us)
{
    for (uint32_t i = 0; i < diag->queue_count; i++) {
        const brrs_rxerr_record_t *r = &diag->queue[i];
        diag->processed++;
        diag->rearmed += r->rearmed != 0U;
        diag->pre_zero += r->pre_lo == 0U && r->pre_hi == 0U;
        diag->post_zero += r->post_lo == 0U && r->post_hi == 0U;
        diag->changed += r->pre_lo != r->post_lo || r->pre_hi != r->post_hi;
        diag->hw_faults += brrs_rxerr_hardware_fault(r->pre_lo | r->post_lo,
                                                   r->pre_hi | r->post_hi);
        for (uint32_t bit = 0; bit < BRRS_RXERR_STATUS_BITS; bit++) {
            uint32_t pre = bit < 32U ? r->pre_lo : r->pre_hi;
            uint32_t post = bit < 32U ? r->post_lo : r->post_hi;
            uint32_t shift = bit < 32U ? bit : bit - 32U;
            diag->pre_bits[bit] += (pre >> shift) & 1U;
            diag->post_bits[bit] += (post >> shift) & 1U;
        }
        brrs_rxerr_observe(&diag->timing[0], r->pre_cycles, cycles_per_us);
        brrs_rxerr_observe(&diag->timing[1], r->post_cycles, cycles_per_us);
        brrs_rxerr_observe(&diag->timing[2], r->fint_cycles, cycles_per_us);
        if (r->rearmed) {
            brrs_rxerr_observe(&diag->timing[3], r->rearm_cycles, cycles_per_us);
        }
        if (diag->sample_count < BRRS_RXERR_SAMPLE_CAPACITY) {
            diag->samples[diag->sample_count++] = *r;
        } else {
            diag->samples_omitted++;
        }
    }
    diag->queue_count = 0U;
}

#endif
