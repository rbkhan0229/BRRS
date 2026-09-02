# Exp4 PHY configure internal profile (2026-09-02)

## Scope and validity

- Branch baseline: `exp4-wait-budget-sweep` at `03f372a`.
- Profiling implementation: `bb2173e` (`BRRS_OPT_PHY_CONFIG_PROFILE=1`).
- Configuration: M32, PAC8, S3, G=200 us, lead=15 us, sync buffer/prep=2000/2000 us, persistent SPIM, polling, 500 superframes.
- Valid hardware run: `exp4_32_s3_r2_*`; coordinator and N2/N3/N4 verifiers all PASS.
- Coordinator: 1493/1500 DATA frames, PER 0.467%, `required_guard_us=88`, no delayed-late or buffer/SPI faults.
- Sensors: each received 500/500 beacons and transmitted 500/500 scheduled DATA frames.
- Run 1 is INVALID and excluded: sensors latched a stale S2 beacon (`slot_owners=232`) from the still-running coordinator before the new S3 coordinator image was flashed. Run 2 decoded the intended S3 schedule (`slot_owners=234`).

Raw logs are under `../logs/exp4_phyprofile_task2_s3_0m_g200_l15_pac8_sb2000_sp2000_20260902_spiopt_phyprofile_cycles500/`.

## Internal `dwt_configure()` cost

Values below are averages; parenthesized values are observed maxima. Sensor values are identical within rounding across N2-N4, so N2 is representative.

| Role | Target | Temp/VDDDIG | Register setup | `setchannel` | DGC/RX tuning | PGF calibration | Total |
|---|---|---:|---:|---:|---:|---:|---:|
| Coordinator | short (M32) | 0.093 (1) | 348.990 (350) | 28.360 (29) | 119.376 (120) | 354.223 (355) | 889.187 (890) us |
| Coordinator | long (M256) | 0.093 (1) | 348.989 (350) | 28.360 (29) | 119.564 (120) | 354.224 (355) | 889.372 (890) us |
| Sensor N2 | short (M32) | 0.093 (1) | 367.659 (368) | 29.877 (30) | 125.548 (126) | 371.410 (372) | 934.231 (935) us |
| Sensor N2 | long (M256) | 0.093 (1) | 367.659 (368) | 29.876 (30) | 125.737 (126) | 371.410 (372) | 934.418 (935) us |

All 1,003 profiled coordinator transitions and all 3,000 profiled sensor transitions (4,003 total) entered `dwt_configure()` in `DW_SYS_STATE_IDLE`; `not_idle=0` in every role/target group. The same-channel `setchannel` section costs only 28-30 us, so a non-IDLE-triggered PLL recalibration is not the observed bottleneck.

For the sensor short transition, register setup is 39.35% and PGF calibration is 39.75% of total time; DGC/RX tuning is 13.44%, `setchannel` is 3.20%, and the measured sections leave about 4.25% in surrounding work. The coordinator has the same shape. Thus the two dominant targets for a safe delta switch are redundant register writes and, only if link testing permits, PGF calibration.

## Decision for Task 3

The fast-switch work remains justified, but the instruction's estimated 1000-1400 us saving per transition is higher than the measured full call itself (890-935 us). It must not be presented as an observed or achievable value. A no-PGF delta path has a theoretical ceiling below roughly 890-935 us per transition; a PGF-retaining path necessarily preserves about 355-372 us plus the required delta writes.

Implementation must also update RX PAC when PAC4 is selected. M32/PAC8 and M256/PAC8 differ in the four fields listed in the directive, but PAC4 is an independent supported configuration and changes `rxPAC`; omitting it would make the feature incorrect outside the profiled PAC8 case.

The next step is therefore a default-OFF driver-owned delta configuration path that preserves the driver's cached preamble/OPS state, with separate PGF-retained and PGF-skipped variants and fail-closed register comparison before hardware sweeps.
