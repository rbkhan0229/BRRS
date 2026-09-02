#ifndef BRRS_PHY_FAST_SWITCH_H
#define BRRS_PHY_FAST_SWITCH_H

#include "deca_device_api.h"
#include <stdint.h>
#include <string.h>

#ifndef BRRS_OPT_PHY_FAST_SWITCH
#define BRRS_OPT_PHY_FAST_SWITCH 0
#endif

#ifndef BRRS_OPT_PHY_FAST_SWITCH_SKIP_PGF
#define BRRS_OPT_PHY_FAST_SWITCH_SKIP_PGF 0
#endif

#if BRRS_OPT_PHY_FAST_SWITCH_SKIP_PGF && !BRRS_OPT_PHY_FAST_SWITCH
#error "BRRS_OPT_PHY_FAST_SWITCH_SKIP_PGF requires BRRS_OPT_PHY_FAST_SWITCH"
#endif

enum {
    BRRS_PHY_FAST_DIFF_FINE_PLEN = 1U << 0,
    BRRS_PHY_FAST_DIFF_TX_FCTRL = 1U << 1,
    BRRS_PHY_FAST_DIFF_CHAN_CTRL = 1U << 2,
    BRRS_PHY_FAST_DIFF_DTUNE0 = 1U << 3,
    BRRS_PHY_FAST_DIFF_DTUNE4 = 1U << 4,
    BRRS_PHY_FAST_DIFF_OTP_CFG = 1U << 5,
};

typedef struct {
    uint32_t fine_plen;
    uint32_t tx_fctrl;
    uint32_t chan_ctrl;
    uint32_t dtune0;
    uint32_t dtune4;
    uint32_t otp_cfg;
} brrs_phy_fast_snapshot_t;

typedef struct {
    dwt_config_t *config;
    uint8_t run_pgf;
} brrs_phy_fast_switch_request_t;

typedef struct {
    uint32_t data_mismatch;
    uint32_t sync_mismatch;
    uint8_t failed_stage;
} brrs_phy_fast_self_test_t;

#if BRRS_OPT_PHY_FAST_SWITCH
int32_t brrs_phy_fast_switch(dwt_config_t *config, uint8_t run_pgf);
int32_t brrs_phy_fast_snapshot(brrs_phy_fast_snapshot_t *snapshot);

static inline uint32_t brrs_phy_fast_snapshot_diff(
    const brrs_phy_fast_snapshot_t *expected,
    const brrs_phy_fast_snapshot_t *actual)
{
    uint32_t mismatch = 0U;

    if (expected->fine_plen != actual->fine_plen) {
        mismatch |= BRRS_PHY_FAST_DIFF_FINE_PLEN;
    }
    if (expected->tx_fctrl != actual->tx_fctrl) {
        mismatch |= BRRS_PHY_FAST_DIFF_TX_FCTRL;
    }
    if (expected->chan_ctrl != actual->chan_ctrl) {
        mismatch |= BRRS_PHY_FAST_DIFF_CHAN_CTRL;
    }
    if (expected->dtune0 != actual->dtune0) {
        mismatch |= BRRS_PHY_FAST_DIFF_DTUNE0;
    }
    if (expected->dtune4 != actual->dtune4) {
        mismatch |= BRRS_PHY_FAST_DIFF_DTUNE4;
    }
    if (expected->otp_cfg != actual->otp_cfg) {
        mismatch |= BRRS_PHY_FAST_DIFF_OTP_CFG;
    }
    return mismatch;
}

/* Boot-only comparison. Every exit restores a full SYNC configuration so a
 * failed diagnostic cannot leave the radio in a partially switched state. */
static inline int32_t brrs_phy_fast_self_test(
    dwt_config_t *sync_config,
    dwt_config_t *data_config,
    uint8_t run_pgf,
    brrs_phy_fast_self_test_t *result)
{
    brrs_phy_fast_snapshot_t full_sync;
    brrs_phy_fast_snapshot_t full_data;
    brrs_phy_fast_snapshot_t fast_sync;
    brrs_phy_fast_snapshot_t fast_data;
    int32_t status = DWT_ERROR;

    memset(result, 0, sizeof(*result));
    result->failed_stage = 1U;
    if (dwt_configure(sync_config) != DWT_SUCCESS ||
        brrs_phy_fast_snapshot(&full_sync) != DWT_SUCCESS) {
        goto restore_sync;
    }

    result->failed_stage = 2U;
    if (dwt_configure(data_config) != DWT_SUCCESS ||
        brrs_phy_fast_snapshot(&full_data) != DWT_SUCCESS) {
        goto restore_sync;
    }

    result->failed_stage = 3U;
    if (dwt_configure(sync_config) != DWT_SUCCESS ||
        brrs_phy_fast_switch(data_config, run_pgf) != DWT_SUCCESS ||
        brrs_phy_fast_snapshot(&fast_data) != DWT_SUCCESS) {
        goto restore_sync;
    }
    result->data_mismatch =
        brrs_phy_fast_snapshot_diff(&full_data, &fast_data);

    result->failed_stage = 4U;
    if (brrs_phy_fast_switch(sync_config, run_pgf) != DWT_SUCCESS ||
        brrs_phy_fast_snapshot(&fast_sync) != DWT_SUCCESS) {
        goto restore_sync;
    }
    result->sync_mismatch =
        brrs_phy_fast_snapshot_diff(&full_sync, &fast_sync);
    if (result->data_mismatch == 0U && result->sync_mismatch == 0U) {
        status = DWT_SUCCESS;
        result->failed_stage = 0U;
    }

restore_sync:
    if (dwt_configure(sync_config) != DWT_SUCCESS) {
        status = DWT_ERROR;
        result->failed_stage = 5U;
    }
    return status;
}
#endif

#endif
