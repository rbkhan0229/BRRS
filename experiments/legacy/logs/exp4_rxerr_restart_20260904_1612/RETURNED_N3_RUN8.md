# N3 original-position return — run 8

2026-09-04 17:02 KST. User reported returning N3 (1050204212) to its original position after relocated run 7. One 1000-superframe run, same role assignments and diagnostic ON binaries: M32/PAC8/S3/G200/lead15/SB2000/SP2002, full PHY configure, optimized SPI. No firmware source changes.

| Node | RX/expected | PER |
| --- | --- | --- |
| N2 | 1000/1000 | 0% |
| N3 | 727/1000 | 27.3% |
| N4 | 997/1000 | 0.3% |
| Total | 2724/3000 | 9.2% |

All three TX logs verified 1000 beacons/attempts/successes. All system integrity checks passed: deadline, delayed late, RDB mismatch/incomplete, overrun and SPI errors/timeouts zero. Final disposition FAIL_PER (5% threshold). Both capture sessions completed.

Pre-rearm error diagnostic: 233 events, RXFSL149, RXPHE34, RXSTO45, RXFCE5. Post-rearm legacy output says fint-only233 and fsl0; the pre-rearm diagnostic is required to retain true flags. Error events are not one-to-one with all 276 missing packets and their time-estimated slot is not a decoded source identity.

Placement sequence with diagnostic ON: original-position runs 2/4/6 N3 PER39.1/37.2/39.1%; relocated run7 0%; returned run8 27.3%. Strong evidence of placement/path contribution, but not exact reproduction of the earlier 39.1% magnitude; user-reported placement and temporal variability remain limitations. Does not isolate metal material, nor exclude firmware/path interactions.

## Historical NLOS 6.9m M32/S3 raw-log comparison

| Date/run | Guard | Total PER | N3 PER | Collection |
| --- | --- | --- | --- | --- |
| 2026-08-23 baseline r1 | 200 us | 7.833% | 14.3% | PASS |
| 2026-08-25 latest r1, PAC8 | 200 us | 1.067% | 2.2% | FAIL |
| 2026-08-25 latest r2, PAC8 | 200 us | 0.767% | 0.1% | FAIL |
| 2026-08-25 guard250 r1, PAC8 | 250 us | 0.633% | 1.8% | PASS |

G200 August25 runs contain overrun=1 each and are NOT stable system-pass references. Roles may rotate between historical repetitions; do not interpret r2 N3 as the same physical board as r1 N3. Current N3 is replacement4212, not old888. Historical wait budget3000/2500 differs from current2000/2002; firmware also differs. Historical numbers confirm lower measured PER existed, but cannot alone attribute the change to firmware or position.

Sources: `paper_results_20260823_nlos_6.9m/raw_logs/exp4_baseline_g200_l15_20260823/exp4_32_s3_r1_init.log`; `logs/exp4_nlos_6.9m_latest_6.9m_g200_l15_pac8_20260825/exp4_32_s3_r{1,2}_init.log`; `logs/exp4_nlos_6.9m_guard250_6.9m_g250_l15_pac8_20260825/exp4_32_s3_r1_init.log`.

Current raw logs/meta: `logs/exp4_nlos_rxerr_new4212_6.9m_g200_l15_pac8_sb2000_sp2002_20260904_spiopt_rxerrdiag/exp4_32_s3_r8_{init,n2,n3,n4}.{log,meta.txt}`. Verified with `audit_rxdiag.py --run 8 --diag on`, including recorded raw hashes and explicit serial assignments.
