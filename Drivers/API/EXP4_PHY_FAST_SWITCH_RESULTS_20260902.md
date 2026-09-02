# Exp4 fast PHY switch implementation and hardware results (2026-09-02)

## Scope and decision

- Implementation commit: `1e74fbc` on local branch `exp4-phy-fastswitch-task3`.
- Configuration: M32, PAC8, S3, G=200 us, lead=15 us, sync buffer/prep=2000/2000 us, persistent SPIM, polling, 1000 superframes per repeated run.
- `BRRS_OPT_PHY_FAST_SWITCH` and `BRRS_OPT_PHY_FAST_SWITCH_SKIP_PGF` both default to 0. The legacy full-configuration path remains the default.
- Both fast variants passed the boot register-equivalence self-test, every first-RX check, and all fail-closed system-fault checks.
- Selected continuation for Task 4: **fast switch with PGF retained**. The no-PGF path is measurably faster, but its pooled PER was higher and therefore did not establish the required link non-regression.
- No push was performed.

Raw logs are under:

- `../logs/exp4_phyfast_pgf_s3_0m_g200_l15_pac8_sb2000_sp2000_20260902_spiopt_phyfast/`
- `../logs/exp4_phyfast_skippgf_s3_0m_g200_l15_pac8_sb2000_sp2000_20260902_spiopt_phyfast_skippgf/`

The companion CSV contains one row per valid run. One first attempt at retained-PGF run 2 is preserved with a `.prev` suffix and excluded as INVALID because the RTT capture lost a contiguous part of the final statistics, including `EXP4_DONE`. The immediate repeat completed normally.

## Implementation

The delta operation is driver-owned so the driver's cached preamble length, OPS sleep state, and last verified configuration remain coherent. It refuses the operation unless the radio is IDLE, the channel and invariant PHY fields match, the codes are valid PRF64 codes, and a prior complete configuration is cached.

The fast path updates:

1. OTP OPS selection and kick, including the cached sleep-mode selection.
2. `DTUNE0.PRE_PAC_SYM` so PAC4 remains supported.
3. `CHAN_CTRL` TX/RX preamble codes and SFD type.
4. `FINE_PLEN` and `TX_FCTRL` preamble/data-rate fields.
5. `DTUNE0` SFD timeout.
6. `DTUNE4.RX_SFD_HLDOFF`.
7. PGF calibration only in the retained-PGF variant.

Temperature/VDDDIG work, invariant `SYS_CFG`/PDOA/STS/DTUNE3 values, same-channel tuning, DGC LUT reload, and constant TX control writes are omitted. A boot-only self-test constructs full M256 and M32 reference snapshots, performs both delta transitions, compares masked `FINE_PLEN`, `TX_FCTRL`, `CHAN_CTRL`, `DTUNE0`, `DTUNE4`, and `OTP_CFG` values, and restores a full M256 configuration on every exit. Any mismatch stops the firmware before the experiment.

## Timing results

The profiling reference is the valid Task 2 full-`dwt_configure()` S3 run. It used 500 superframes and is a timing reference, not one of the three Task 3 link repeats.

| Path | Valid runs | Worst first-slot prep max | Worst next-SYNC prep max | Sensor DATA PHY max | Coordinator DATA PHY max | Required guard |
|---|---:|---:|---:|---:|---:|---:|
| Full configuration reference | 1 | 1640 us | 1652 us | 1032 us | 1059 us | 88 us |
| Fast switch, PGF retained | 3 | 1377 us | 1399 us | 745 us | 793 us | 88 us |
| Fast switch, PGF skipped | 3 | 1023 us | 1038 us | 375 us | 437 us | 88 us |

Relative to the full reference, retaining PGF reduced the worst first-slot preparation by 263 us (16.0%) and next-SYNC preparation by 253 us (15.3%). Skipping PGF reduced them by 617 us (37.6%) and 614 us (37.2%), respectively. These gains are substantially below the original 1000-1400 us estimate because Task 2 measured the entire full call at only 890-935 us.

## Link and first-RX results

| Path | Received | PER | Wilson 95% interval | First DATA after switch | First SYNC after switch | System faults |
|---|---:|---:|---:|---:|---:|---:|
| Full configuration profile reference | 1493/1500 | 0.467% | 0.226-0.960% | not instrumented | not instrumented | 0 |
| Fast switch, PGF retained | 8957/9000 | 0.478% | 0.355-0.643% | 3000/3000 | 9000/9000 | 0 |
| Fast switch, PGF skipped | 8935/9000 | 0.722% | 0.567-0.919% | 3000/3000 | 9000/9000 | 0 |

`System faults` is the sum of deadline misses, delayed RX/TX late events, RDB host mismatch/incomplete events, overruns, SPI errors/recoveries/timeouts, and experiment timeouts. It was zero in every valid run. All three sensor boards received 1000/1000 beacons and transmitted 1000/1000 scheduled frames in every Task 3 run.

The no-PGF variant was 0.244 percentage points worse than retained PGF when the three runs were pooled. A simple two-proportion comparison gives `p=0.034`. Because the two variants were run sequentially rather than in a randomized interleaved block, this is not sufficient to claim PGF omission caused the degradation. It is sufficient to reject a non-regression claim under the fail-safe protocol. PGF is therefore retained for the budget sweep; the no-PGF flag remains an experimental option and must not become the default.

## Task 4 inputs

Using the selected retained-PGF path and worst observed maxima:

- First-slot arm maximum: 1377 us.
- Next-SYNC arm end-to-end maximum: 1399 us.
- Physical additions required by the directive: 42 us DATA preamble+SFD and 269 us SYNC RMARKER offset.
- Zero-margin lower bounds: `sync_buffer = 1377 + 42 = 1419 us`; `sync_prep = 1399 + 269 = 1668 us`.

The next sweep must derive its +30%, +20%, +10%, and +0% points from those values rather than using arbitrary round numbers.
