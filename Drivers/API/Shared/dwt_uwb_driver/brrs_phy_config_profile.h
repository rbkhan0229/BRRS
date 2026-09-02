#ifndef BRRS_PHY_CONFIG_PROFILE_H
#define BRRS_PHY_CONFIG_PROFILE_H

#include <stdint.h>

#ifndef BRRS_OPT_PHY_CONFIG_PROFILE
#define BRRS_OPT_PHY_CONFIG_PROFILE 0
#endif

typedef enum {
    BRRS_PHY_PROFILE_SHORT = 0,
    BRRS_PHY_PROFILE_LONG,
    BRRS_PHY_PROFILE_TARGET_COUNT
} brrs_phy_profile_target_t;

typedef enum {
    BRRS_PHY_PROFILE_TEMP_VDDDIG = 0,
    BRRS_PHY_PROFILE_REGISTER_SETUP,
    BRRS_PHY_PROFILE_SETCHANNEL,
    BRRS_PHY_PROFILE_DGC_RX_TUNING,
    BRRS_PHY_PROFILE_PGF_CAL,
    BRRS_PHY_PROFILE_TOTAL,
    BRRS_PHY_PROFILE_PHASE_COUNT
} brrs_phy_profile_phase_t;

typedef struct {
    uint32_t count;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint64_t sum_cycles;
} brrs_phy_profile_stats_t;

typedef struct {
    brrs_phy_profile_stats_t
        phase[BRRS_PHY_PROFILE_TARGET_COUNT][BRRS_PHY_PROFILE_PHASE_COUNT];
    uint32_t state_samples[BRRS_PHY_PROFILE_TARGET_COUNT];
    uint32_t state_idle[BRRS_PHY_PROFILE_TARGET_COUNT];
    uint32_t state_not_idle[BRRS_PHY_PROFILE_TARGET_COUNT];
} brrs_phy_profile_snapshot_t;

#if BRRS_OPT_PHY_CONFIG_PROFILE
void brrs_phy_profile_reset(void);
void brrs_phy_profile_snapshot(brrs_phy_profile_snapshot_t *snapshot);
#endif

#endif
