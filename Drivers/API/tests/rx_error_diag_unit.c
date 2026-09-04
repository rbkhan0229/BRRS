/* Host-only test of the exact RAM collector used by firmware; no hardware. */
#include <assert.h>
#include <stdio.h>
#include "../Src/examples/ex_35a_brrs_init/brrs_rx_error_diag.h"

static brrs_rxerr_diag_t diag;

static brrs_rxerr_record_t sample(void)
{
    brrs_rxerr_record_t r = {0};
    r.sf = 1;
    r.logical_slot = r.estimated_slot = 1;
    r.pre_lo = (1UL << 18) | (1UL << 29) | (1UL << 28); /* CIA/ARFE/CPERR */
    r.pre_hi = 1U << 1; /* RXPREJ */
    r.poll_fint = 0x14;
    r.pre_cycles = 65;
    r.post_cycles = 128;
    r.fint_cycles = 1;
    r.rearmed = 1;
    r.rearm_cycles = 640;
    return r;
}

int main(void)
{
    brrs_rxerr_process(&diag, 64);
    assert(diag.events == 0 && diag.timing[0].count == 0);
    brrs_rxerr_record_t r = sample();
    brrs_rxerr_enqueue(&diag, &r);
    assert(diag.events == 1 && diag.processed == 0); /* deferred */
    brrs_rxerr_process(&diag, 64);
    assert(diag.events == 1 && diag.processed == 1 && diag.errors == 1);
    assert(diag.changed == 1 && diag.pre_zero == 0 && diag.post_zero == 1);
    assert(diag.pre_bits[18] == 1 && diag.pre_bits[29] == 1 && diag.pre_bits[28] == 1);
    assert(diag.pre_bits[33] == 1 && diag.post_bits[33] == 0);
    assert(diag.timing[0].min_us == 2 && diag.timing[0].sum_us == 2);
    assert(diag.timing[3].max_us == 10 && diag.hw_faults == 0);
    assert(diag.samples[0].event == 1 && diag.samples[0].pre_hi == 2);

    r = sample();
    r.timeout = 1;
    r.rearmed = 0;
    r.pre_lo = r.post_lo = 1UL << 17;
    r.pre_hi = r.post_hi = 0;
    brrs_rxerr_enqueue(&diag, &r);
    brrs_rxerr_process(&diag, 64);
    assert(diag.timeouts == 1 && diag.errors == 1 && diag.rearmed == 1);
    assert(diag.timing[0].count == 2 && diag.timing[3].count == 1);
    assert(diag.changed == 1 && diag.samples[1].event == 2);

    memset(&diag, 0, sizeof(diag));
    for (unsigned i = 0; i < 70; i++) {
        r = sample();
        brrs_rxerr_enqueue(&diag, &r);
        brrs_rxerr_process(&diag, 64);
    }
    assert(diag.events == 70 && diag.processed == 70);
    assert(diag.sample_count == 64 && diag.samples_omitted == 6);
    assert(diag.samples[63].event == 64 && diag.pre_bits[18] == 70);
    assert(diag.queue_overflow == 0);

    memset(&diag, 0, sizeof(diag));
    for (unsigned i = 0; i < 34; i++) {
        r = sample();
        brrs_rxerr_enqueue(&diag, &r);
    }
    assert(diag.queue_count == 32 && diag.queue_overflow == 2);
    brrs_rxerr_process(&diag, 64);
    assert(diag.processed + diag.queue_overflow == diag.events);
    assert(diag.processed == 32 && diag.sample_count == 32);

    assert(!brrs_rxerr_hardware_fault(1UL << 2, 0)); /* SPI CRC disabled */
    const unsigned fault_bits[] = {19,20,25,27,40,41,42,43};
    for (unsigned i = 0; i < sizeof(fault_bits)/sizeof(fault_bits[0]); i++) {
        unsigned b = fault_bits[i];
        assert(brrs_rxerr_hardware_fault(b < 32 ? 1UL << b : 0,
                                         b >= 32 ? 1U << (b-32) : 0));
    }
    memset(&diag, 0, sizeof(diag));
    r = sample();
    r.post_hi = 1U << 8; /* CMD_ERR only after rearm */
    brrs_rxerr_enqueue(&diag, &r);
    brrs_rxerr_process(&diag, 64);
    assert(diag.hw_faults == 1 && diag.post_bits[40] == 1);
    puts("PASS: firmware RAM collector, 48-bit counters, timing, bounded samples and overflow");
    return 0;
}
