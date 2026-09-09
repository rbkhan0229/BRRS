# N3 relocation quick test — run 7

2026-09-04. User moved N3 (1050204212) between N2 and N4 because the fixed door cannot open, and requested one quick test. The physical position is user-reported; new distance/orientation were not independently measured.

Run 7 uses the previous diagnostic ON firmware and fixed roles, M32/PAC8/S3/G200/lead15/SB2000/SP2002/full PHY configure/optimized SPI/1000 SF. Only the reported N3 placement changes. Capture environment name `nlos_rxerr_new4212` and distance label `6.9` are inherited campaign labels, NOT verification that the relocated N3 is precisely 6.9 m away or on the original path.

Keep this run separate from original-position runs 1–6. Do not aggregate it into that baseline. It is a single exploratory placement comparison, not isolation of metal attenuation from all propagation/orientation effects.

Status: completed at 2026-09-04 16:58 KST. Capture and all-role read-only audit PASS; all worker sessions ended.

| Node | Received / expected | PER |
| --- | --- | --- |
| N2 | 1000/1000 | 0% |
| N3 (relocated 4212) | 1000/1000 | 0% |
| N4 | 997/1000 | 0.3% |
| Total | 2997/3000 | 0.1% |

All three TX logs show 1000 beacons, attempts and successful TX each. Deadline miss, delayed-late, RDB mismatch/incomplete, overrun and SPI errors/timeouts are zero. RX diagnostic has two RXPHE events and zero RXFSL events. Three missed packets are not equated to the two captured error events.

Same diagnostic ON baseline N3 PER was 39.1%, 37.2%, 39.1% (runs 2/4/6). Relocated run is 0/1000 losses; this is not proof of zero underlying PER (Wilson 95% interval approximately 0–0.383%). Strong exploratory evidence of placement/path influence, not proof of metal as the sole cause or exclusion of firmware/path interactions. Repetition and old/new/old placement control remain necessary for causal attribution.

Raw logs and per-role metadata: `../exp4_nlos_rxerr_new4212_6.9m_g200_l15_pac8_sb2000_sp2002_20260904_spiopt_rxerrdiag/exp4_32_s3_r7_{init,n2,n3,n4}.{log,meta.txt}`. Audit command: `python3 logs/exp4_rxerr_restart_20260904_1612/audit_rxdiag.py --run 7 --diag on`. Metadata hashes and fixed serial assignments all verified.
