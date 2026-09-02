/*! ----------------------------------------------------------------------------
 * @file    deca_spi.c
 * @brief   SPI access functions
 *
 * @attention
 *
 * Copyright 2015 - 2021 (c) DecaWave Ltd, Dublin, Ireland.
 *
 * All rights reserved.
 *
 * @author DecaWave
 */

#include "deca_spi.h"
#include "port.h"
#include <deca_device_api.h>
#include <string.h>

#ifndef BRRS_EXP4_SPI_DIRECT
#define BRRS_EXP4_SPI_DIRECT 0
#endif
#if BRRS_EXP4_SPI_DIRECT
#define DW_SPI_HOT_OPT __attribute__((optimize("O3")))
#else
#define DW_SPI_HOT_OPT
#endif

// static
// spi_handle_t spi_handler = {
///* below will be configured in the port_init_dw_chip() */
//  .spi_inst       = 0,
//  .frequency_slow = 0,
//  .frequency_fast = 0,
//  .spi_config     = 0,
//
//  .csPin          = DW3000_CS_Pin,
//  .lock           = DW_HAL_NODE_UNLOCKED
//};

static spi_handle_t spi1_handler;
static spi_handle_t spi2_handler;
static spi_handle_t *pgSpiHandler = &spi1_handler;

uint16_t  current_cs_pin=DW3000_CS_Pin;
uint16_t  current_irq_pin=DW3000_IRQn_Pin;

dw_t s1
=
{
    .irqPin    = DW3000_IRQn_Pin,
    .rstPin    = DW3000_RESET_Pin,
    .wkupPin   = DW3000_WAKEUP_Pin,
    .csPin     = DW3000_CS_Pin,    //'1' steady state
    .pSpi      = &spi1_handler,
};

dw_t s2
=
{
    .irqPin    = DW3000_IRQ2n_Pin,
    .rstPin    = DW3000_RESET_Pin,
    .wkupPin   = DW3000_WAKEUP_Pin,
    .csPin     = DW3000_CS2_Pin,    //'0' steady state
    .pSpi      = &spi2_handler,
};

const dw_t *SPI1 = &s1; /**< by default SPI1 */
const dw_t *SPI2 = &s2; /**< by default SPI2 */

static volatile bool spi_xfer_done;
static uint8_t spi_init_stat = 0; // use 1 for slow, use 2 for fast;
static bool spi_burst_active = false;
static bool spi_cs_asserted = false;
static spi_handle_t *spi_burst_handler = NULL;
static dw_spi_burst_stats_t spi_burst_stats;

static uint8_t idatabuf[DATALEN1] = { 0 }; // Never define this inside the Spi read/write
static uint8_t itempbuf[DATALEN1] = { 0 }; // As that will use the stack from the Task, which are not such long!!!!
                                           // You will face a crashes which are not expected!

/****************************************************************************
 *
 *                              DW3000 SPI section
 *
 *******************************************************************************/

/* @fn      change_SPI
 * @brief   Select Host to work with (SPIM3 or SPIM2)
 *
 * @param   spi - HOST enum to work with
 * */
void change_SPI(host_using_spi_e    spi)
{
    if (spi==SPI_1)
    {
        pgSpiHandler=&spi1_handler;
        current_cs_pin=DW3000_CS_Pin;
        current_irq_pin=DW3000_IRQn_Pin;
    }
    else
    {//SPI 2
        pgSpiHandler=&spi2_handler;
        current_cs_pin=DW3000_CS2_Pin_WU;
        current_irq_pin=DW3000_IRQ2n_Pin;
    }

}

/* @fn    nrf52840_dk_spi_init
 * Initialise nRF52840-DK SPI
 * */
void nrf52840_dk_spi_init(void)
{
    nrf_drv_spi_t *spi_inst;
    nrf_drv_spi_config_t *spi_config;

    spi_handle_t *pSPI1_handler = SPI1->pSpi;

    pSPI1_handler->frequency_slow = NRF_SPIM_FREQ_4M;
    pSPI1_handler->frequency_fast = NRF_SPIM_FREQ_32M;

    pSPI1_handler->lock = DW_HAL_NODE_UNLOCKED;

    spi_inst = &pSPI1_handler->spi_inst;
    spi_config = &pSPI1_handler->spi_config;

    spi_inst->inst_idx = SPI3_INSTANCE_INDEX;
    spi_inst->use_easy_dma = SPI3_USE_EASY_DMA;
    spi_inst->u.spim.p_reg = NRF_SPIM3;
    spi_inst->u.spim.drv_inst_idx = NRFX_SPIM3_INST_IDX;

    spi_config->sck_pin = DW3000_CLK_Pin;
    spi_config->mosi_pin = DW3000_MOSI_Pin;
    spi_config->miso_pin = DW3000_MISO_Pin;
    spi_config->ss_pin   = NRF_DRV_SPI_PIN_NOT_USED;
    spi_config->irq_priority = (APP_IRQ_PRIORITY_MID - 2);
    spi_config->orc = 0xFF;
    spi_config->frequency = NRF_SPIM_FREQ_4M;
    spi_config->mode = NRF_DRV_SPI_MODE_0;
    spi_config->bit_order = NRF_DRV_SPI_BIT_ORDER_MSB_FIRST;

    // Configure the chip select of SPI1 as an output pin that can be toggled
    nrf_drv_gpiote_out_config_t out_config = NRFX_GPIOTE_CONFIG_OUT_TASK_TOGGLE(NRF_GPIOTE_INITIAL_VALUE_HIGH);
    nrf_drv_gpiote_out_init(DW3000_CS_Pin, &out_config);

    spi2_init();
}

/* @fn    spi2_init
 * Initialise nRF52840-DK SPI2
 * */
void spi2_init(void)
{
    ret_code_t err_code;

    nrf_drv_spi_t   *spi_inst;
    nrf_drv_spi_config_t  *spi_config;

    spi_handle_t *pSPI2_handler = SPI2->pSpi;

    pSPI2_handler->frequency_slow = NRF_SPIM_FREQ_4M;
    pSPI2_handler->frequency_fast = NRF_SPIM_FREQ_8M;

    pSPI2_handler->lock = DW_HAL_NODE_UNLOCKED;

    spi_inst = &pSPI2_handler->spi_inst;
    spi_config = &pSPI2_handler->spi_config;

    spi_inst->inst_idx = SPI2_INSTANCE_INDEX;
    spi_inst->use_easy_dma = SPI2_USE_EASY_DMA;
    spi_inst->u.spim.p_reg = NRF_SPIM2;
    spi_inst->u.spim.drv_inst_idx = NRFX_SPIM2_INST_IDX;

    spi_config->sck_pin  = DW3000_CLK2_Pin;
    spi_config->mosi_pin = DW3000_MOSI2_Pin;
    spi_config->miso_pin = DW3000_MISO2_Pin;
    spi_config->ss_pin   = NRF_DRV_SPI_PIN_NOT_USED;
    spi_config->irq_priority = (APP_IRQ_PRIORITY_MID - 1);
    spi_config->orc = 0xFF;
    spi_config->frequency = NRF_SPIM_FREQ_4M;
    spi_config->mode = NRF_DRV_SPI_MODE_0;
    spi_config->bit_order = NRF_DRV_SPI_BIT_ORDER_MSB_FIRST;

    // SPI2 chip select can be selected via two pin(shorted on Arduino shield)
    // Select one pin as input and the one that can be toggled as output.
    nrf_drv_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_HITOLO(true);
    in_config.pull = NRF_GPIO_PIN_PULLDOWN;

    err_code = nrf_drv_gpiote_in_init(DW3000_CS2_Pin, &in_config, NULL);
    APP_ERROR_CHECK(err_code);

    nrf_drv_gpiote_in_event_enable(DW3000_CS2_Pin, false);

    //Setting the chip select of second SPI with active high state.
    nrf_drv_gpiote_out_config_t out_config = NRFX_GPIOTE_CONFIG_OUT_TASK_TOGGLE(NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_drv_gpiote_out_init(DW3000_CS2_Pin_WU, &out_config);
}

/*! ------------------------------------------------------------------------------------------------------------------
 * Function: openspi()
 *
 * Low level abstract function to open and initialise access to the SPI device.
 * returns 0 for success, or -1 for error
 */
static int openspi(nrf_drv_spi_t *p_instance)
{
    NRF_SPIM_Type *p_spi = p_instance->u.spim.p_reg;
    nrf_spim_enable(p_spi);
    return 0;
} // end openspi()

/*! ------------------------------------------------------------------------------------------------------------------
 * Function: closespi()
 *
 * Low level abstract function to close the the SPI device.
 * returns 0 for success, or -1 for error
 */
static int closespi(nrf_drv_spi_t *p_instance)
{
    NRF_SPIM_Type *p_spi = p_instance->u.spim.p_reg;
    nrf_spim_disable(p_spi);
    return 0;
} // end closespi()

DW_SPI_HOT_OPT
static void spi_assert_cs(void)
{
    if (!spi_cs_asserted)
    {
#if BRRS_EXP4_SPI_DIRECT
        nrf_gpio_pin_toggle(current_cs_pin);
#else
        nrfx_gpiote_out_toggle(current_cs_pin);
#endif
        spi_cs_asserted = true;
    }
}

DW_SPI_HOT_OPT
static void spi_deassert_cs(void)
{
    if (spi_cs_asserted)
    {
#if BRRS_EXP4_SPI_DIRECT
        nrf_gpio_pin_toggle(current_cs_pin);
#else
        nrfx_gpiote_out_toggle(current_cs_pin);
#endif
        spi_cs_asserted = false;
        /* Per-transaction SPIM disable used to provide an implicit idle gap.
         * Preserve a conservative CS-high interval when the peripheral stays
         * enabled so adjacent DW3000 commands cannot run together. */
        if (spi_burst_active)
        {
#if BRRS_EXP4_SPI_DIRECT
            /* DW3000 t9 requires at least 40 ns between consecutive SPI
             * accesses. Eight 64 MHz core NOPs provide a 125 ns floor in
             * addition to GPIO and call overhead, without paying 1 us on
             * every hot-path transaction. */
            __NOP(); __NOP(); __NOP(); __NOP();
            __NOP(); __NOP(); __NOP(); __NOP();
#else
            nrf_delay_us(1U);
#endif
        }
    }
}

DW_SPI_HOT_OPT
static bool spi_keep_enabled(void)
{
    return spi_burst_active && spi_burst_handler == pgSpiHandler;
}

#if BRRS_EXP4_SPI_DIRECT
#define DW_SPI_DIRECT_TIMEOUT_CYCLES 64000U

DW_SPI_HOT_OPT
static uint32_t spi_anomaly_198_enable(const uint8_t *buffer,
                                       uint32_t length)
{
    uint32_t preserved = *((volatile uint32_t *)0x40000E00);
    uint32_t buffer_end;
    uint32_t block_addr;
    uint32_t block_flag;
    uint32_t occupied_blocks = 0U;

    if (length == 0U)
    {
        return preserved;
    }
    buffer_end = (uint32_t)buffer + length;
    block_addr = (uint32_t)buffer & ~0x1FFFU;
    block_flag = 1UL << ((block_addr >> 13) & 0xFFFFU);
    if (block_addr >= 0x20010000U)
    {
        occupied_blocks = 1UL << 8;
    }
    else
    {
        do
        {
            occupied_blocks |= block_flag;
            block_flag <<= 1;
            block_addr += 0x2000U;
        } while (block_addr < buffer_end && block_addr < 0x20012000U);
    }
    *((volatile uint32_t *)0x40000E00) = occupied_blocks;
    return preserved;
}

DW_SPI_HOT_OPT
static ret_code_t spi_direct_transfer(const uint8_t *tx_buffer,
                                      uint32_t length,
                                      uint8_t *rx_buffer)
{
    NRF_SPIM_Type *spim = pgSpiHandler->spi_inst.u.spim.p_reg;
    uint32_t anomaly_preserved;
    uint32_t start_cycles;

    if (!spi_keep_enabled() || spim != NRF_SPIM3 ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        return nrf_drv_spi_transfer(&pgSpiHandler->spi_inst,
                                    tx_buffer, length,
                                    rx_buffer, length);
    }

    anomaly_preserved = spi_anomaly_198_enable(tx_buffer, length);
    nrf_spim_tx_list_disable(spim);
    nrf_spim_rx_list_disable(spim);
    nrf_spim_tx_buffer_set(spim, tx_buffer, length);
    nrf_spim_rx_buffer_set(spim, rx_buffer, length);
    nrf_spim_event_clear(spim, NRF_SPIM_EVENT_END);
    start_cycles = DWT->CYCCNT;
    nrf_spim_task_trigger(spim, NRF_SPIM_TASK_START);
    while (!nrf_spim_event_check(spim, NRF_SPIM_EVENT_END))
    {
        if ((uint32_t)(DWT->CYCCNT - start_cycles) >=
            DW_SPI_DIRECT_TIMEOUT_CYCLES)
        {
            nrf_spim_task_trigger(spim, NRF_SPIM_TASK_STOP);
            *((volatile uint32_t *)0x40000E00) = anomaly_preserved;
            spi_burst_stats.direct_timeout_count++;
            return NRF_ERROR_TIMEOUT;
        }
    }
    *((volatile uint32_t *)0x40000E00) = anomaly_preserved;
    spi_burst_stats.direct_transfer_count++;
    return NRF_SUCCESS;
}

#if BRRS_OPT_SPIM_START_END_PROFILE
#define DW_SPIM_PROFILE_SAMPLES       1000U
#define DW_SPIM_PROFILE_HIST_BINS     256U
#define DW_SPIM_PROFILE_START_PPI_CH  18U
#define DW_SPIM_PROFILE_END_PPI_CH    19U
#define DW_SPIM_PROFILE_PPI_MASK      \
    ((1UL << DW_SPIM_PROFILE_START_PPI_CH) | \
     (1UL << DW_SPIM_PROFILE_END_PPI_CH))
#define DW_SPIM_PROFILE_TIMER         NRF_TIMER3

/* Measure only the SPIM3 START-task to END-event interval. TIMER3 compare 0
 * starts SPIM3 through PPI, and the SPIM3 END event captures TIMER3 CC[1]
 * through a second PPI channel. CPU event polling therefore cannot extend
 * the captured interval. CS remains high, so the DW3000 ignores all 1000
 * one-byte transfers. */
int32_t port_dw_spim_start_end_profile(
    dw_spim_start_end_profile_t *profile)
{
    static uint32_t histogram[DW_SPIM_PROFILE_HIST_BINS];
    static uint8_t tx_byte = 0U;
    static uint8_t rx_byte = 0U;
    NRF_SPIM_Type *spim = pgSpiHandler->spi_inst.u.spim.p_reg;
    NRF_TIMER_Type *timer = DW_SPIM_PROFILE_TIMER;
    uint32_t saved_start_eep;
    uint32_t saved_start_tep;
    uint32_t saved_end_eep;
    uint32_t saved_end_tep;
    uint32_t saved_timer_mode;
    uint32_t saved_timer_bitmode;
    uint32_t saved_timer_prescaler;
    uint32_t saved_timer_shorts;
    uint32_t saved_timer_cc0;
    uint32_t saved_timer_cc1;
    uint32_t saved_timer_event0;
    uint32_t saved_timer_event1;
    uint32_t saved_spim_config;
    uint32_t saved_spim_frequency;
    uint32_t saved_spim_sck;
    uint32_t saved_spim_mosi;
    uint32_t saved_spim_miso;
    uint32_t anomaly_preserved = 0U;
    uint32_t sample;
    uint32_t cumulative;
    uint32_t p99_target;
    int32_t result = NRF_SUCCESS;
    bool spi_open = false;
    bool anomaly_active = false;

    if (profile == NULL)
    {
        return NRF_ERROR_NULL;
    }
    memset(profile, 0, sizeof(*profile));
    memset(histogram, 0, sizeof(histogram));
    profile->min_ticks = UINT32_MAX;

    if (!BRRS_EXP4_SPI_DIRECT || spim != NRF_SPIM3 ||
        spi_burst_active || spi_cs_asserted ||
        pgSpiHandler->lock != DW_HAL_NODE_UNLOCKED ||
        /* CS input is disconnected for this output pin, so GPIO.IN reads low
         * even while the driven output is high. Validate the OUT latch. */
        nrf_gpio_pin_out_read(current_cs_pin) == 0U ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U ||
        (NRF_PPI->CHEN & DW_SPIM_PROFILE_PPI_MASK) != 0U ||
        timer->INTENSET != 0U || timer->SHORTS != 0U ||
        spim->ENABLE != SPIM_ENABLE_ENABLE_Disabled ||
        spim->FREQUENCY != NRF_SPIM_FREQ_32M)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    saved_start_eep = NRF_PPI->CH[DW_SPIM_PROFILE_START_PPI_CH].EEP;
    saved_start_tep = NRF_PPI->CH[DW_SPIM_PROFILE_START_PPI_CH].TEP;
    saved_end_eep = NRF_PPI->CH[DW_SPIM_PROFILE_END_PPI_CH].EEP;
    saved_end_tep = NRF_PPI->CH[DW_SPIM_PROFILE_END_PPI_CH].TEP;
    saved_timer_mode = timer->MODE;
    saved_timer_bitmode = timer->BITMODE;
    saved_timer_prescaler = timer->PRESCALER;
    saved_timer_shorts = timer->SHORTS;
    saved_timer_cc0 = timer->CC[0];
    saved_timer_cc1 = timer->CC[1];
    saved_timer_event0 = timer->EVENTS_COMPARE[0];
    saved_timer_event1 = timer->EVENTS_COMPARE[1];
    saved_spim_config = spim->CONFIG;
    saved_spim_frequency = spim->FREQUENCY;
    saved_spim_sck = spim->PSEL.SCK;
    saved_spim_mosi = spim->PSEL.MOSI;
    saved_spim_miso = spim->PSEL.MISO;

    pgSpiHandler->lock = DW_HAL_NODE_LOCKED;
    if (openspi(&pgSpiHandler->spi_inst) != 0)
    {
        result = NRF_ERROR_INTERNAL;
        goto cleanup;
    }
    spi_open = true;
    anomaly_preserved = spi_anomaly_198_enable(&tx_byte, 1U);
    anomaly_active = true;

    nrf_spim_tx_list_disable(spim);
    nrf_spim_rx_list_disable(spim);
    timer->TASKS_STOP = 1U;
    timer->MODE = TIMER_MODE_MODE_Timer;
    timer->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    timer->PRESCALER = 0U;
    timer->SHORTS = 0U;
    timer->INTENCLR = 0xFFFFFFFFUL;
    timer->CC[0] = 1U;

    NRF_PPI->CH[DW_SPIM_PROFILE_START_PPI_CH].EEP =
        (uint32_t)(uintptr_t)&timer->EVENTS_COMPARE[0];
    NRF_PPI->CH[DW_SPIM_PROFILE_START_PPI_CH].TEP =
        (uint32_t)(uintptr_t)&spim->TASKS_START;
    NRF_PPI->CH[DW_SPIM_PROFILE_END_PPI_CH].EEP =
        (uint32_t)(uintptr_t)&spim->EVENTS_END;
    NRF_PPI->CH[DW_SPIM_PROFILE_END_PPI_CH].TEP =
        (uint32_t)(uintptr_t)&timer->TASKS_CAPTURE[1];
    NRF_PPI->CHENSET = DW_SPIM_PROFILE_PPI_MASK;

    for (sample = 0U; sample < DW_SPIM_PROFILE_SAMPLES; sample++)
    {
        uint32_t timeout_start;
        uint32_t ticks;

        nrf_spim_tx_buffer_set(spim, &tx_byte, 1U);
        nrf_spim_rx_buffer_set(spim, &rx_byte, 1U);
        nrf_spim_event_clear(spim, NRF_SPIM_EVENT_END);
        timer->TASKS_STOP = 1U;
        timer->TASKS_CLEAR = 1U;
        timer->EVENTS_COMPARE[0] = 0U;
        timer->EVENTS_COMPARE[1] = 0U;
        timeout_start = DWT->CYCCNT;
        timer->TASKS_START = 1U;

        while (!nrf_spim_event_check(spim, NRF_SPIM_EVENT_END))
        {
            if ((uint32_t)(DWT->CYCCNT - timeout_start) >=
                DW_SPI_DIRECT_TIMEOUT_CYCLES)
            {
                nrf_spim_task_trigger(spim, NRF_SPIM_TASK_STOP);
                profile->timeout_count++;
                result = NRF_ERROR_TIMEOUT;
                goto cleanup;
            }
        }
        timer->TASKS_STOP = 1U;
        if (timer->CC[1] <= timer->CC[0])
        {
            profile->histogram_overflow++;
            result = NRF_ERROR_INVALID_DATA;
            goto cleanup;
        }
        ticks = timer->CC[1] - timer->CC[0];
        profile->count++;
        profile->sum_ticks += ticks;
        if (ticks < profile->min_ticks)
        {
            profile->min_ticks = ticks;
        }
        if (ticks > profile->max_ticks)
        {
            profile->max_ticks = ticks;
        }
        if (ticks < DW_SPIM_PROFILE_HIST_BINS)
        {
            histogram[ticks]++;
        }
        else
        {
            profile->histogram_overflow++;
        }
    }

    p99_target = (profile->count * 99U + 99U) / 100U;
    cumulative = 0U;
    for (sample = 0U; sample < DW_SPIM_PROFILE_HIST_BINS; sample++)
    {
        cumulative += histogram[sample];
        if (cumulative >= p99_target)
        {
            profile->p99_ticks = sample;
            break;
        }
    }
    if (profile->histogram_overflow != 0U ||
        profile->count != DW_SPIM_PROFILE_SAMPLES ||
        profile->p99_ticks == 0U)
    {
        result = NRF_ERROR_INVALID_DATA;
    }

cleanup:
    NRF_PPI->CHENCLR = DW_SPIM_PROFILE_PPI_MASK;
    timer->TASKS_STOP = 1U;
    timer->TASKS_CLEAR = 1U;
    timer->EVENTS_COMPARE[0] = 0U;
    timer->EVENTS_COMPARE[1] = 0U;
    NRF_PPI->CH[DW_SPIM_PROFILE_START_PPI_CH].EEP = saved_start_eep;
    NRF_PPI->CH[DW_SPIM_PROFILE_START_PPI_CH].TEP = saved_start_tep;
    NRF_PPI->CH[DW_SPIM_PROFILE_END_PPI_CH].EEP = saved_end_eep;
    NRF_PPI->CH[DW_SPIM_PROFILE_END_PPI_CH].TEP = saved_end_tep;
    timer->MODE = saved_timer_mode;
    timer->BITMODE = saved_timer_bitmode;
    timer->PRESCALER = saved_timer_prescaler;
    timer->SHORTS = saved_timer_shorts;
    timer->CC[0] = saved_timer_cc0;
    timer->CC[1] = saved_timer_cc1;
    timer->EVENTS_COMPARE[0] = saved_timer_event0;
    timer->EVENTS_COMPARE[1] = saved_timer_event1;
    if (anomaly_active)
    {
        *((volatile uint32_t *)0x40000E00) = anomaly_preserved;
    }
    if (spi_open)
    {
        closespi(&pgSpiHandler->spi_inst);
    }
    pgSpiHandler->lock = DW_HAL_NODE_UNLOCKED;

    if (NRF_PPI->CH[DW_SPIM_PROFILE_START_PPI_CH].EEP != saved_start_eep ||
        NRF_PPI->CH[DW_SPIM_PROFILE_START_PPI_CH].TEP != saved_start_tep ||
        NRF_PPI->CH[DW_SPIM_PROFILE_END_PPI_CH].EEP != saved_end_eep ||
        NRF_PPI->CH[DW_SPIM_PROFILE_END_PPI_CH].TEP != saved_end_tep ||
        timer->MODE != saved_timer_mode ||
        timer->BITMODE != saved_timer_bitmode ||
        timer->PRESCALER != saved_timer_prescaler ||
        timer->SHORTS != saved_timer_shorts ||
        timer->CC[0] != saved_timer_cc0 ||
        timer->CC[1] != saved_timer_cc1 ||
        timer->EVENTS_COMPARE[0] != saved_timer_event0 ||
        timer->EVENTS_COMPARE[1] != saved_timer_event1 ||
        spim->CONFIG != saved_spim_config ||
        spim->FREQUENCY != saved_spim_frequency ||
        spim->PSEL.SCK != saved_spim_sck ||
        spim->PSEL.MOSI != saved_spim_mosi ||
        spim->PSEL.MISO != saved_spim_miso ||
        spim->ENABLE != SPIM_ENABLE_ENABLE_Disabled ||
        nrf_gpio_pin_out_read(current_cs_pin) == 0U ||
        (NRF_PPI->CHEN & DW_SPIM_PROFILE_PPI_MASK) != 0U)
    {
        profile->register_mismatch++;
        result = NRF_ERROR_INVALID_STATE;
    }
    return result;
}
#endif
#endif

DW_SPI_HOT_OPT
static ret_code_t spi_transfer(const uint8_t *tx_buffer,
                               uint32_t length,
                               uint8_t *rx_buffer)
{
#if BRRS_EXP4_SPI_DIRECT
    if (spi_keep_enabled())
    {
        return spi_direct_transfer(tx_buffer, length, rx_buffer);
    }
#endif
    return nrf_drv_spi_transfer(&pgSpiHandler->spi_inst,
                                tx_buffer, length, rx_buffer, length);
}

void port_dw_spi_burst_force_recover(void)
{
    spi_deassert_cs();
    if (spi_burst_active && spi_burst_handler != NULL)
    {
        closespi(&spi_burst_handler->spi_inst);
    }
    spi_burst_active = false;
    spi_burst_handler = NULL;
    spi_burst_stats.active = 0U;
    spi_burst_stats.recovery_count++;
}

int32_t port_dw_spi_burst_begin(void)
{
    if (spi_burst_active || spi_cs_asserted ||
        pgSpiHandler->lock != DW_HAL_NODE_UNLOCKED)
    {
        spi_burst_stats.state_error_count++;
        port_dw_spi_burst_force_recover();
        return NRF_ERROR_INVALID_STATE;
    }

    if (openspi(&pgSpiHandler->spi_inst) != 0)
    {
        spi_burst_stats.state_error_count++;
        return NRF_ERROR_INTERNAL;
    }

    spi_burst_handler = pgSpiHandler;
    spi_burst_active = true;
    spi_burst_stats.active = 1U;
    spi_burst_stats.begin_count++;
    return NRF_SUCCESS;
}

int32_t port_dw_spi_burst_end(void)
{
    if (!spi_burst_active || spi_burst_handler != pgSpiHandler ||
        pgSpiHandler->lock != DW_HAL_NODE_UNLOCKED)
    {
        spi_burst_stats.state_error_count++;
        port_dw_spi_burst_force_recover();
        return NRF_ERROR_INVALID_STATE;
    }

    if (spi_cs_asserted)
    {
        spi_burst_stats.state_error_count++;
        spi_deassert_cs();
    }
    closespi(&pgSpiHandler->spi_inst);
    spi_burst_active = false;
    spi_burst_handler = NULL;
    spi_burst_stats.active = 0U;
    spi_burst_stats.end_count++;
    return NRF_SUCCESS;
}

void port_dw_spi_burst_get_stats(dw_spi_burst_stats_t *stats)
{
    if (stats != NULL)
    {
        *stats = spi_burst_stats;
    }
}

/**
 * @brief SPI user event handler.
 * @param event
 */
void spi_event_handler(nrf_drv_spi_evt_t const *p_event, void *p_context)
{
    UNUSED_PARAMETER(p_event);
    UNUSED_PARAMETER(p_context);
    spi_xfer_done = true;
}

/* @fn      port_set_dw_ic_spi_slowrate
 * @brief   set 4MHz
 * */
void port_set_dw_ic_spi_slowrate(void)
{
    // Make sure it's uninitialized first
    nrf_drv_spi_uninit(&pgSpiHandler->spi_inst);

    pgSpiHandler->spi_config.frequency = pgSpiHandler->frequency_slow;

    APP_ERROR_CHECK(nrf_drv_spi_init(&pgSpiHandler->spi_inst,
                                     &pgSpiHandler->spi_config,
                                     NULL,
                                     NULL) );


    nrf_delay_ms(2);

}

/* @fn      port_set_dw_ic_spi_fastrate
 * @brief   set 16MHz for SPI_1 and 8MHz for SPI_2
 * */
void port_set_dw_ic_spi_fastrate(void)
{
    // Make sure it's uninitialized first
    nrf_drv_spi_uninit(&pgSpiHandler->spi_inst);
    
    pgSpiHandler->spi_config.frequency = pgSpiHandler->frequency_fast;

    APP_ERROR_CHECK( nrf_drv_spi_init(&pgSpiHandler->spi_inst,
                                      &pgSpiHandler->spi_config,
                                      NULL,
                                      NULL) );

    nrf_gpio_cfg(pgSpiHandler->spi_config.sck_pin,
                     NRF_GPIO_PIN_DIR_OUTPUT,
                     NRF_GPIO_PIN_INPUT_CONNECT,
                     NRF_GPIO_PIN_NOPULL,
                     NRF_GPIO_PIN_H0H1,
                     NRF_GPIO_PIN_NOSENSE);
    nrf_gpio_cfg( pgSpiHandler->spi_config.mosi_pin,
                     NRF_GPIO_PIN_DIR_OUTPUT,
                     NRF_GPIO_PIN_INPUT_DISCONNECT,
                     NRF_GPIO_PIN_NOPULL,
                     NRF_GPIO_PIN_H0H1,
                     NRF_GPIO_PIN_NOSENSE);

    nrf_delay_ms(2);

}

/*! ------------------------------------------------------------------------------------------------------------------
 * Function: writetospiwithcrc()
 *
 * Low level abstract function to write to the SPI when SPI CRC mode is used
 * Takes two separate byte buffers for write header and write data, and a CRC8 byte which is written last
 * returns 0 for success, or -1 for error
 */
DW_SPI_HOT_OPT
int32_t writetospiwithcrc(uint16_t headerLength, const uint8_t *headerBuffer, uint16_t bodyLength, const uint8_t *bodyBuffer, uint8_t crc8)
{
#ifdef DWT_ENABLE_CRC
    uint8_t *p1;
    ret_code_t transfer_result;
    uint32_t idatalength = headerLength + bodyLength + sizeof(crc8); // It cannot be more than 255 in total length (header + body)

    if (idatalength > DATALEN1)
    {
        return NRF_ERROR_NO_MEM;
    }

    while(pgSpiHandler->lock);

    __HAL_LOCK(pgSpiHandler);

    if (!spi_keep_enabled())
    {
        openspi(&pgSpiHandler->spi_inst);
    }

    p1 = idatabuf;
    memcpy(p1, headerBuffer, headerLength);
    p1 += headerLength;
    memcpy(p1, bodyBuffer, bodyLength);
    p1 += bodyLength;
    memcpy(p1, &crc8, 1);

    spi_assert_cs();

    spi_xfer_done = false;
    transfer_result = spi_transfer(idatabuf, idatalength, itempbuf);

    spi_deassert_cs();
    if (!spi_keep_enabled())
    {
        closespi(&pgSpiHandler->spi_inst);
    }

    __HAL_UNLOCK(pgSpiHandler);
    if (transfer_result != NRF_SUCCESS)
    {
        spi_burst_stats.transfer_error_count++;
        port_dw_spi_burst_force_recover();
        return (int32_t)transfer_result;
    }
#endif //DWT_ENABLE_CRC
    return 0;
} // end writetospiwithcrc()

/*! ------------------------------------------------------------------------------------------------------------------
 * Function: writetospi()
 *
 * Low level abstract function to write to the SPI
 * Takes two separate byte buffers for write header and write data
 * returns 0 for success, or -1 for error
 */
DW_SPI_HOT_OPT
int32_t writetospi(uint16_t headerLength, const uint8_t *headerBuffer, uint16_t bodyLength, const uint8_t *bodyBuffer)
{
    uint8_t *p1;
    ret_code_t transfer_result;
    uint32_t idatalength = headerLength + bodyLength;

    if (idatalength > DATALEN1)
    {
        return NRF_ERROR_NO_MEM;
    }

    while(pgSpiHandler->lock);

    __HAL_LOCK(pgSpiHandler);

    if (!spi_keep_enabled())
    {
        openspi(&pgSpiHandler->spi_inst);
    }

    p1 = idatabuf;
    memcpy(p1, headerBuffer, headerLength);
    p1 += headerLength;
    memcpy(p1, bodyBuffer, bodyLength);

    spi_assert_cs();

    spi_xfer_done = false;
    transfer_result = spi_transfer(idatabuf, idatalength, itempbuf);

    spi_deassert_cs();
    if (!spi_keep_enabled())
    {
        closespi(&pgSpiHandler->spi_inst);
    }
    __HAL_UNLOCK(pgSpiHandler);

    if (transfer_result != NRF_SUCCESS)
    {
        spi_burst_stats.transfer_error_count++;
        port_dw_spi_burst_force_recover();
        return (int32_t)transfer_result;
    }

    return 0;
} // end writetospi()

/*! ------------------------------------------------------------------------------------------------------------------
 * Function: readfromspi()
 *
 * Low level abstract function to read from the SPI
 * Takes two separate byte buffers for write header and read data
 * returns the offset into read buffer where first byte of read data may be found,
 * or returns -1 if there was an error
 */
DW_SPI_HOT_OPT
int32_t readfromspi(uint16_t headerLength, uint8_t *headerBuffer, uint16_t readLength, uint8_t *readBuffer)
{
    uint8_t *p1;
    ret_code_t transfer_result;
    uint32_t idatalength = headerLength + readLength;

    if (idatalength > DATALEN1)
    {
        return NRF_ERROR_NO_MEM;
    }

    while(pgSpiHandler->lock);

    __HAL_LOCK(pgSpiHandler);

    if (!spi_keep_enabled())
    {
        openspi(&pgSpiHandler->spi_inst);
    }

    p1 = idatabuf;
    memcpy(p1, headerBuffer, headerLength);

    p1 += headerLength;
    memset(p1, 0x00, readLength);

    idatalength = headerLength + readLength;

    spi_assert_cs();

    spi_xfer_done = false;
    transfer_result = spi_transfer(idatabuf, idatalength, itempbuf);

    spi_deassert_cs();
    if (!spi_keep_enabled())
    {
        closespi(&pgSpiHandler->spi_inst);
    }

    __HAL_UNLOCK(pgSpiHandler);

    if (transfer_result != NRF_SUCCESS)
    {
        spi_burst_stats.transfer_error_count++;
        port_dw_spi_burst_force_recover();
        return (int32_t)transfer_result;
    }

    p1 = itempbuf + headerLength;
    memcpy(readBuffer, p1, readLength);

    return 0;
} // end readfromspi()

/****************************************************************************
 *
 *                              END OF DW3000 SPI section
 *
 *******************************************************************************/
