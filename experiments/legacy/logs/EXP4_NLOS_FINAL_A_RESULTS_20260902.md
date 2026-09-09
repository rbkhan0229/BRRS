# Exp4 final NLOS 6.9 m — A preamble comparison

Date: 2026-09-02  
Frozen source commit: `479e1ea4b41274428778eb70c11d468d95b9d7a3`  
Final record commit/tag: `f420f95e29351e4b737cc76561bffae7191f4376` / `exp4-final-eval-20260902`

## Fixed conditions

- Three physical sensors, S3, 1,000 superframes per run, 3,000 offered reports per run.
- NLOS distance 6.9 m, guard 200 us, lead 15 us.
- Sync buffer/prep 1703/2002 us; DATA budget 6295 us.
- Polling + persistent optimized SPI + retained-PGF fast PHY switch.
- IRQ and Task 7/8 profiling disabled.
- Role rotation:
  - rotation 0: N2=`1050211584`, N3=`1050273888`, N4=`1050282818`
  - rotation 1: N2=`1050273888`, N3=`1050282818`, N4=`1050211584`
  - rotation 2: N2=`1050282818`, N3=`1050211584`, N4=`1050273888`

## Primary three-rotation results

| Preamble | PAC | Received / offered | Aggregate PER | Wilson 95% CI | System verdict |
|---:|---:|---:|---:|---:|---|
| 32 | 4 | 7,189 / 9,000 | 20.1222% | 19.3068–20.9632% | collection PASS, link LOSS |
| 32 | 8 | 7,862 / 9,000 | 12.6444% | 11.9737–13.3470% | collection PASS, link LOSS |
| 64 | 8 | 9,000 / 9,000 | 0% | 0–0.0427% | PASS |
| 128 | 8 | 9,000 / 9,000 | 0% | 0–0.0427% | PASS |
| 256 | 8 | 9,000 / 9,000 | 0% | 0–0.0427% | PASS |

M32/PAC4, although the nominally recommended PAC for a 32-symbol preamble, was 7.4778 percentage points worse than M32/PAC8 in this NLOS placement. This is an experimental result for this setup, not a general claim that PAC8 is always superior.

## M32/PAC8 confirmation run

The first primary run had PER 17.300%, so the same physical-role assignment was repeated as run 4. The confirmation result was 2,523/3,000, PER 15.900%. Therefore the high first-run PER was not treated as a one-off outlier. Across all four M32/PAC8 runs the result was 10,385/12,000, PER 13.4583%, Wilson 95% CI 12.8594–14.0807%.

## Interpretation and validity

- Loss followed physical sensor/path `1050273888` as roles rotated: it was N3 in runs 1 and 4, N2 in run 2, and N4 in run 3. This shows that the large M32 loss is associated mainly with a weak physical NLOS link, not a fixed logical slot.
- All 16 valid runs passed schedule, timing, and collection checks. Deadline miss, delayed RX/TX late, RDB mismatch/incomplete, buffer overrun, SPI error/timeout, and deferred overflow were zero.
- Required guard measured 88 us for M32/M64/M128 and 87 us for M256; the configured 200 us guard retained margin.
- M32 PHY receive errors and losses are link outcomes and are intentionally not reclassified as firmware/system failures.
- No human-passage contamination was reported during these valid runs.
- The initial M32/PAC8 zero-reception setup attempt is excluded. Sensors briefly observed a stale M256 beacon while INIT was being reflashed to M32; its files were preserved with `.prev.<timestamp>` suffixes. All valid runs preconditioned INIT before starting sensor capture.
- Saturated-capacity experiment B has not started.

## Raw evidence locations

- PAC8: `/Users/songchieon/Desktop/DWM3000/logs/exp4_nlos_final_6.9m_g200_l15_pac8_sb1703_sp2002_20260902_spiopt_phyfast`
- PAC4: `/Users/songchieon/Desktop/DWM3000/logs/exp4_nlos_final_6.9m_g200_l15_pac4_sb1703_sp2002_20260902_spiopt_phyfast`
- Per-run machine-readable table: `/Users/songchieon/Desktop/DWM3000/logs/EXP4_NLOS_FINAL_A_RESULTS_20260902.csv`
