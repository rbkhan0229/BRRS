/* Isolated home interference study AUX, 2026-09-27.
 * Ordinary finite CH5 packets; no autonomous RF before host ARM.
 * Replace ex_34_INTERFERENCE/interference_nonhop.c ONLY in a study copy.
 */
#include "deca_probe_interface.h"
#include <deca_device_api.h>
#include <dw3000_deca_regs.h>
#include <deca_spi.h>
#include <example_selection.h>
#include <port.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "nrf.h"

#if defined(TEST_INTERFERENCE_NONHOP)
extern int SEGGER_RTT_ConfigUpBuffer(unsigned,const char *,void *,unsigned,unsigned);
extern unsigned SEGGER_RTT_WriteString(unsigned,const char *);
extern dwt_txconfig_t txconfig_options;

#define AUX_ARM_MAGIC       0x41524d31UL
#define AUX_STOP_MAGIC      0x53544f50UL
#define AUX_CPU_CYCLES_US   64UL
#define AUX_MAX_RUN_MS      60000UL
#define AUX_ARM_WAIT_MS     90000UL
#define AUX_PERIOD_US       1009UL
#define AUX_TX_TIMEOUT_US   800UL
#define AUX_MAX_PACKETS     45000UL
#define AUX_TRACE_CAPACITY  45000UL
#define AUX_TX_POWER_INDEX  40U
#define AUX_SCHEMA         "aux-finite-packet-v1"
#define AUX_IMPLEMENTATION "postinit-status-v2"
#define AUX_HI_FAULT_MASK (SYS_STATUS_HI_SPIERR_BIT_MASK | SYS_STATUS_HI_SPI_UNF_BIT_MASK | SYS_STATUS_HI_SPI_OVF_BIT_MASK | SYS_STATUS_HI_CMD_ERR_BIT_MASK)

/* Exported aligned RAM symbols. Obtain addresses from THIS build's ELF/map.
 * Host writes duration first, then ARM after READY. STOP wins over ARM.
 * Reset clears commands. END cannot be rearmed without an explicit reset.
 */
volatile uint32_t aux_host_arm = 0U;
volatile uint32_t aux_host_stop = 0U;
volatile uint32_t aux_duration_ms = AUX_MAX_RUN_MS;
volatile uint32_t aux_state = 0U; /* 0 boot, 1 ready, 2 running, 3 end, 4 error */
volatile uint32_t aux_tx_count = 0U; /* Count only observed TXFRS. */
volatile uint32_t aux_tx_attempts = 0U;
volatile uint32_t aux_error_code = 0U;
volatile uint32_t aux_end_reason = 0U; /* 1 duration,2 host STOP,3 arm timeout,4 count */
volatile uint32_t aux_elapsed_us = 0U;
volatile uint32_t aux_tx_timeout_count = 0U;
volatile uint32_t aux_schedule_overrun_count = 0U;
volatile uint32_t aux_last_status = 0U;
volatile uint32_t aux_last_status_hi = 0U;
volatile uint32_t aux_init_status_before = 0U;
volatile uint32_t aux_init_status_after = 0U;
volatile uint32_t aux_init_status_hi_before = 0U;
volatile uint32_t aux_init_status_hi_after = 0U;
volatile uint32_t aux_init_sys_cfg = 0U;
volatile uint32_t aux_rf_off_commanded = 0U;
/* One actual DW3000 TX RMARKER high32 per successful TXFRS, index = packet sequence. */
volatile uint32_t aux_tx_timestamp_count = 0U;
volatile uint32_t aux_tx_timestamp_hi32[AUX_TRACE_CAPACITY];
static char aux_rtt_buffer[8192];

static void aux_log(const char *s)
{
    SEGGER_RTT_WriteString(1U,s);
    SEGGER_RTT_WriteString(1U,"\n");
}

static dwt_config_t aux_config = {
    5, DWT_PLEN_64, DWT_PAC8, 9, 9, 1,
    DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    65, DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};
/* IEEE802.15.4 data frame, distinct PAN/source; 26-byte PSDU incl. HW FCS.
 * This is intentionally not a BRRS beacon or a valid BRRS source payload.
 */
static uint8_t aux_frame[26] = {
    0x41,0x88,0x00,0xA6,0xD1,0xFF,0xFF,0xA8,0x00,
    'B','R','R','S','-','A','U','X',0,0,0,0,0,0,0,0,0
};
static bool aux_radio_probed = false;
static uint32_t aux_run_start = 0U;
static uint32_t aux_run_limit_cycles = 0U;

static uint32_t aux_now(void) { return DWT->CYCCNT; }
static uint32_t aux_since(uint32_t start) { return (uint32_t)(aux_now()-start); }

static bool aux_spi_bad(void)
{
    dw_spi_burst_stats_t s = {0};
    port_dw_spi_burst_get_stats(&s);
    return s.transfer_error_count || s.state_error_count || s.direct_timeout_count;
}

static int aux_finish(uint32_t reason, uint32_t error)
{
    char line[512];
    dw_spi_burst_stats_t s = {0};
    if (aux_radio_probed) {
        dwt_forcetrxoff();
        aux_rf_off_commanded=1U;
    }
    port_dw_spi_burst_get_stats(&s);
    if ((s.transfer_error_count || s.state_error_count || s.direct_timeout_count) && error==0U)
        error=16U;
    if (aux_state==2U) aux_elapsed_us=aux_since(aux_run_start)/AUX_CPU_CYCLES_US;
    aux_error_code=error;
    aux_end_reason=reason;
    aux_state=error ? 4U : 3U;
    snprintf(line,sizeof(line),
        "AUX_END schema=%s reason=%lu error=%lu tx=%lu attempts=%lu elapsed_us=%lu timeout=%lu overrun=%lu spi_transfer=%lu spi_state=%lu spi_timeout=%lu rf_off_commanded=%lu",
        AUX_SCHEMA,(unsigned long)reason,(unsigned long)error,
        (unsigned long)aux_tx_count,(unsigned long)aux_tx_attempts,
        (unsigned long)aux_elapsed_us,(unsigned long)aux_tx_timeout_count,
        (unsigned long)aux_schedule_overrun_count,(unsigned long)s.transfer_error_count,
        (unsigned long)s.state_error_count,(unsigned long)s.direct_timeout_count,
        (unsigned long)aux_rf_off_commanded);
    aux_log(line);
    return error ? -1 : 0;
}

/* Caller checks this before every TX and during TX wait. */
static uint32_t aux_stop_reason(void)
{
    if (aux_host_stop==AUX_STOP_MAGIC) return 2U;
    if (aux_since(aux_run_start)>=aux_run_limit_cycles) return 1U;
    if (aux_tx_count>=AUX_MAX_PACKETS) return 4U;
    return 0U;
}

int interference_nonhop(void)
{
    char line[512];
    uint32_t t,reason,requested_ms,next_cycles=0U,power_readback=0U,channel_ctrl_readback=0U;
    uint64_t arm_wait_cycles=0U;
    power_indexes_t powers={0};
    tx_adj_res_t adjusted={0};
    dwt_txconfig_t txrf;
    if (SEGGER_RTT_ConfigUpBuffer(1U,"AUX_LOG",aux_rtt_buffer,sizeof(aux_rtt_buffer),0U)<0)
        return -1;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    t=aux_now(); nrf_delay_us(100);
    if (aux_now()==t) return aux_finish(0U,10U);
    port_set_dw_ic_spi_fastrate();
    reset_DWIC(); Sleep(2);
    if (dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf)!=DWT_SUCCESS)
        return aux_finish(0U,11U);
    aux_radio_probed=true;
    t=aux_now();
    while (!dwt_checkidlerc()) {
        if (aux_host_stop==AUX_STOP_MAGIC) return aux_finish(2U,0U);
        if (aux_since(t)>=100000UL*AUX_CPU_CYCLES_US) return aux_finish(0U,12U);
    }
    if (dwt_initialise(DWT_DW_INIT)!=DWT_SUCCESS) return aux_finish(0U,13U);
    dwt_forcetrxoff();
    if (dwt_configure(&aux_config)!=DWT_SUCCESS) return aux_finish(0U,14U);
    channel_ctrl_readback=dwt_read_reg(CHAN_CTRL_ID);
    if (channel_ctrl_readback==0xffffffffUL ||
        (channel_ctrl_readback&CHAN_CTRL_RF_CHAN_BIT_MASK)!=0U)
        return aux_finish(0U,28U); /* CH5 register readback required. */
    for (unsigned i=0U;i<4U;i++) powers.input[i]=AUX_TX_POWER_INDEX;
    if (dwt_calculate_linear_tx_setting(5,&powers,&adjusted)!=DWT_SUCCESS)
        return aux_finish(0U,15U); /* No silent power fallback. */
    for (unsigned i=0U;i<4U;i++)
        if (powers.output[i]!=AUX_TX_POWER_INDEX) return aux_finish(0U,26U);
    txrf.power=adjusted.tx_frame_cfg.tx_power_setting;
    txrf.PGcount=txconfig_options.PGcount;
    txrf.PGdly=txconfig_options.PGdly;
    dwt_configuretxrf(&txrf);
    dwt_set_pll_config(adjusted.tx_frame_cfg.pll_cfg);
    power_readback=dwt_read_reg(TX_POWER_ID);
    if (power_readback!=txrf.power) return aux_finish(0U,27U);
    dwt_forcetrxoff();
    aux_rf_off_commanded=1U;
    if (aux_spi_bad()) return aux_finish(0U,16U);
    /* SYS_STATUS is latched. Preserve the complete post-init observation,
     * then perform the same one-time W1C initialization as ordinary BRRS.
     * The CRC check mode is not enabled by this firmware; unexpected mode
     * or a high-word SPI/CMD error is fatal, never silently cleared.
     */
    aux_init_sys_cfg=dwt_read_reg(SYS_CFG_ID);
    aux_init_status_before=dwt_readsysstatuslo();
    aux_init_status_hi_before=dwt_readsysstatushi();
    snprintf(line,sizeof(line),
        "AUX_INIT_STATUS implementation=%s phase=before_clear sys_cfg=0x%08lx lo=0x%08lx hi=0x%08lx",
        AUX_IMPLEMENTATION,(unsigned long)aux_init_sys_cfg,
        (unsigned long)aux_init_status_before,(unsigned long)aux_init_status_hi_before);
    aux_log(line);
    if (aux_init_sys_cfg==0xffffffffUL || aux_init_status_before==0xffffffffUL ||
        aux_init_status_hi_before==0xffffffffUL || (aux_init_status_hi_before&AUX_HI_FAULT_MASK))
        return aux_finish(0U,23U);
    if (aux_init_sys_cfg&SYS_CFG_SPI_CRC_BIT_MASK) return aux_finish(0U,24U);
    dwt_writesysstatuslo(0xffffffffUL);
    aux_init_status_after=dwt_readsysstatuslo();
    aux_init_status_hi_after=dwt_readsysstatushi();
    snprintf(line,sizeof(line),
        "AUX_INIT_STATUS implementation=%s phase=after_clear sys_cfg=0x%08lx lo=0x%08lx hi=0x%08lx",
        AUX_IMPLEMENTATION,(unsigned long)aux_init_sys_cfg,
        (unsigned long)aux_init_status_after,(unsigned long)aux_init_status_hi_after);
    aux_log(line);
    if (aux_init_status_after==0xffffffffUL || aux_init_status_hi_after==0xffffffffUL ||
        (aux_init_status_after&DWT_INT_SPICRCE_BIT_MASK) || (aux_init_status_hi_after&AUX_HI_FAULT_MASK))
        return aux_finish(0U,25U);
    if (aux_spi_bad()) return aux_finish(0U,16U);
    aux_state=1U;
    snprintf(line,sizeof(line),
        "AUX_READY schema=%s channel=5 channel_hw=%u M=64 PAC=8 code=9 sfd=1 psdu=26 period_us=%lu max_ms=%lu power_index=%lu applied_data=%u applied_phr=%u applied_shr=%u applied_sts=%u tx_power_readback=0x%08lx tx_power=0x%08lx arm=0x%08lx stop=0x%08lx duration=0x%08lx rf_off_commanded=1",
        AUX_SCHEMA,(unsigned)((channel_ctrl_readback&CHAN_CTRL_RF_CHAN_BIT_MASK)?9U:5U),
        (unsigned long)AUX_PERIOD_US,(unsigned long)AUX_MAX_RUN_MS,
        (unsigned long)AUX_TX_POWER_INDEX,
        (unsigned)powers.output[0],(unsigned)powers.output[1],
        (unsigned)powers.output[2],(unsigned)powers.output[3],
        (unsigned long)power_readback,(unsigned long)txrf.power,(unsigned long)(uintptr_t)&aux_host_arm,
        (unsigned long)(uintptr_t)&aux_host_stop,(unsigned long)(uintptr_t)&aux_duration_ms);
    aux_log(line);
    t=aux_now();
    while (aux_host_arm!=AUX_ARM_MAGIC) {
        uint32_t now=aux_now();
        arm_wait_cycles+=(uint32_t)(now-t);
        t=now;
        if (aux_host_stop==AUX_STOP_MAGIC) return aux_finish(2U,0U);
        if (arm_wait_cycles>=(uint64_t)AUX_ARM_WAIT_MS*1000ULL*AUX_CPU_CYCLES_US)
            return aux_finish(3U,0U);
        nrf_delay_us(100);
    }
    if (aux_host_stop==AUX_STOP_MAGIC) return aux_finish(2U,0U);
    requested_ms=aux_duration_ms;
    if (requested_ms==0U || requested_ms>AUX_MAX_RUN_MS) return aux_finish(0U,17U);
    aux_run_limit_cycles=requested_ms*1000UL*AUX_CPU_CYCLES_US;
    aux_run_start=aux_now();
    aux_state=2U;
    aux_log("AUX_START armed=1 channel=5");
    for (;;) {
        uint32_t elapsed;
        if ((reason=aux_stop_reason())!=0U) return aux_finish(reason,0U);
        elapsed=aux_since(aux_run_start);
        if (elapsed<next_cycles) continue;
        if (elapsed-next_cycles>AUX_PERIOD_US*AUX_CPU_CYCLES_US) {
            aux_schedule_overrun_count++;
            return aux_finish(0U,18U); /* No catch-up burst. */
        }
        /* Do not clear a new error before recording/failing it. */
        aux_last_status=dwt_readsysstatuslo();
        aux_last_status_hi=dwt_readsysstatushi();
        if (aux_last_status==0xffffffffUL || aux_last_status_hi==0xffffffffUL ||
            (aux_last_status&DWT_INT_SPICRCE_BIT_MASK) || (aux_last_status_hi&AUX_HI_FAULT_MASK))
            return aux_finish(0U,21U);
        aux_frame[2]=(uint8_t)aux_tx_count;
        aux_frame[17]=(uint8_t)(aux_tx_count>>8);
        aux_frame[18]=(uint8_t)(aux_tx_count>>16);
        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
        if (dwt_writetxdata(sizeof(aux_frame),aux_frame,0)!=DWT_SUCCESS)
            return aux_finish(0U,19U);
        dwt_writetxfctrl(sizeof(aux_frame),0,0);
        if ((reason=aux_stop_reason())!=0U) return aux_finish(reason,0U);
        if (aux_spi_bad()) return aux_finish(0U,16U);
        aux_tx_attempts++;
        aux_rf_off_commanded=0U;
        if (dwt_starttx(DWT_START_TX_IMMEDIATE)!=DWT_SUCCESS)
            return aux_finish(0U,20U);
        t=aux_now();
        for (;;) {
            aux_last_status=dwt_readsysstatuslo();
            aux_last_status_hi=dwt_readsysstatushi();
            if (aux_last_status==0xffffffffUL || aux_last_status_hi==0xffffffffUL ||
                (aux_last_status&DWT_INT_SPICRCE_BIT_MASK) || (aux_last_status_hi&AUX_HI_FAULT_MASK))
                return aux_finish(0U,21U);
            if (aux_last_status&DWT_INT_TXFRS_BIT_MASK) break;
            if ((reason=aux_stop_reason())!=0U) return aux_finish(reason,0U);
            if (aux_since(t)>=AUX_TX_TIMEOUT_US*AUX_CPU_CYCLES_US) {
                aux_tx_timeout_count++;
                return aux_finish(0U,22U);
            }
        }
        if (aux_tx_count>=AUX_TRACE_CAPACITY) return aux_finish(0U,29U);
        aux_tx_timestamp_hi32[aux_tx_count]=dwt_readtxtimestamphi32();
        aux_tx_timestamp_count=aux_tx_count+1U;
        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
        aux_tx_count++;
        if (aux_spi_bad()) return aux_finish(0U,16U);
        next_cycles+=AUX_PERIOD_US*AUX_CPU_CYCLES_US;
        aux_elapsed_us=aux_since(aux_run_start)/AUX_CPU_CYCLES_US;
        if (aux_tx_count%1000UL==0U) {
            snprintf(line,sizeof(line),"AUX_PROGRESS tx=%lu attempts=%lu elapsed_us=%lu",
                (unsigned long)aux_tx_count,(unsigned long)aux_tx_attempts,
                (unsigned long)aux_elapsed_us);
            aux_log(line);
        }
    }
}
#endif
