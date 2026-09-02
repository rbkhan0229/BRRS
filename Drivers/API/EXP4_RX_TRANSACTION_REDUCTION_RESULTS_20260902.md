# BRRS Exp4 Task 8 — RX good-path transaction reduction

Date: 2026-09-02  
Branch: `exp4-rx-transaction-reduction-task8`  
Implementation commits: `3cd3dfe`, `2ca5f06`  
IRQ: not used or modified

## Conclusion

The DW3000 User Manual v1.1 does not support any of the proposed good-path
transaction removals or cross-register merges. The good path therefore remains
at a maximum of eight DW3000 transactions; no unsafe 8-to-5/6 rewrite was
implemented. This follows the task's rule that an optimization must not be
implemented without datasheet support.

The evidence-backed diagnostic in item 5 was implemented behind
`BRRS_OPT_SPIM_START_END_PROFILE` (default `0`). PPI starts SPIM3 from TIMER3
COMPARE[0] and the SPIM3 END event captures TIMER3 CC[1], so CPU polling is not
part of the measured interval. Across 1000 one-byte transfers at 32 MHz, every
sample was 19 timer ticks = 1.1875 us. Timeouts, histogram overflow, register
restore mismatches, and pre/post DW3000 ID mismatches were all zero.

This result rejects the proposed “15 us or more means no software headroom”
condition: the hardware interval is much less than 15 us. The previously
observed 19–25 us transaction time is therefore dominated by software setup,
buffer preparation, CS handling, and call overhead rather than SPIM3 wire time.

G=150 passed 1000 superframes with no system faults under both the Task 7
M=32 condition and the paper comparison baseline M=256. The M=32 G=100 run
produced one double-buffer host mismatch, one resynchronisation, and one
overrun, so it failed the fail-closed collection policy. G=75 and G=50 were not
attempted. The validated minimum in this sweep is G=150, not G=100.

## Datasheet adjudication

| Item | Manual evidence | Decision |
|---|---|---|
| 1. Remove global `SYS_STATUS` RX clear | Section 4.4, Figure 17 (pp. 43–44) directs the host to clear RX event flags in both `SYS_STATUS` and `RDB_STATUS`. Section 8.2.25.1 (pp. 233–234) states that `FINT_STAT` is a reduced, read-only status view whose latched bits are cleared by writing the corresponding `SYS_STATUS` bits. | Rejected. Leaving `SYS_STATUS.RXOK` latched would leave `FINT_STAT` asserted and destroy event-edge semantics. |
| 2. Fold `RDB_STATUS` W1C into `CMD_DB_TOGGLE` | Section 4.4 (p. 43) says to issue `CMD_DB_TOGGLE` to free the buffer **and also** clear the processed `RDB_STATUS` bits. Section 9.20 only defines the toggle as notification that the host finished with the buffer; it does not specify W1C behavior. | Rejected. They are separate required operations. |
| 3. Add adjacent burst reads | `RDB_STATUS` is `0x01:24`; its neighbour is `RDB_DIAG` at `0x01:28`, which is not good-path metadata. Table 44 puts double-buffered `RX_FINFO` and `RX_TIME` at offsets `0x00` and `0x04` in register file `0x18`; these are already one combined metadata read. Payload/header bytes are in register files `0x12`/`0x13`. | No legal/useful additional merge. One SPI transaction cannot span those register files. |
| 4. Merge the final `FINT_STAT` poll with RDB state | `FINT_STAT` is `0x1F:00`; `RDB_STATUS` is `0x01:24`. `FINT_STAT` also carries masked RX-good, RX-error, RX-timeout, and panic classes, whereas `RDB_STATUS` only describes double-buffer reception state. Figure 17 places RXFCG wait before the RDB check. | Rejected. The addresses and event roles differ; polling RDB alone would lose fail-closed error/timeout classification. |
| 5. Measure SPIM3 START-to-END | nRF52840 PPI/TIMER capture around SPIM3, 1000 one-byte transfers, CS held high so DW3000 ignores the bytes. | Implemented under one default-OFF flag. Result: min=max=p99=1.1875 us. |

No flags were added for items 1–4 because adding a selectable unsafe path would
itself violate the “no datasheet evidence, no implementation” requirement. The
only new flag controls the supported measurement in item 5.

## Historical fail-closed evidence

The manual decision is also consistent with earlier hardware trials:

- Commit `6174006` tested toggle-only buffer release. Run
  `exp4_32_s3_r5_init.log` received 2975/3000 but recorded one RDB incomplete,
  one resync, and one overrun, so collection failed. RDB W1C was restored.
- Commit `e316fe2` moved status maintenance after buffer release. Run
  `exp4_32_s3_r6_init.log` received only 1127/3000 and recorded 486 resyncs and
  486 overruns. Commit `48f3ef1` restored RX status clear before buffer toggle.

These runs are not used as the primary specification; they are regression
evidence supporting the manual-defined ordering.

## Implementation and self-validation

`BRRS_OPT_SPIM_START_END_PROFILE=1` requires direct persistent SPIM and is
enabled only in the coordinator image by `--spim-start-end-profile`. Sensor
images retain the default `0` even when a matching image-set suffix is used.

The boot-only profiler:

1. requires SPIM3 disabled, 32 MHz, no SPI lock/burst/CS assertion, CS output
   high, DWT cycle counting enabled, and its TIMER3/PPI resources idle;
2. saves PPI endpoints, TIMER3 mode/bitmode/prescaler/shorts/CC/events, and
   SPIM3 configuration, frequency, and pin selections;
3. runs 1000 hardware-timestamped transfers with CS high;
4. restores the saved state and compares every saved register;
5. checks the DW3000 device ID before and after the benchmark; and
6. halts before the experiment if any timeout, overflow, register mismatch, or
   identity mismatch occurs.

The build, capture, multi-node orchestration, metadata, and verifier scripts all
carry the new flag. A profile-enabled coordinator log must contain exactly one
`EXP4_SPIM_START_END_CSV` PASS row; a default build must contain none.

## Hardware results

Common configuration: PAC8, three sensors, 1000 superframes, 3000 offered DATA
frames, polling + optimized SPI, PHY fast switch with PGF retained,
`sync_buffer=1703 us`, `sync_prep=2002 us`, lead=15 us, 32 MHz SPI.

| M | Guard | RX | PER | Required guard | Rearm max | Hot-path max / p99 | DB faults | Result |
|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 256 | 200 us | 3000/3000 | 0.000% | 88 us | 73 us | 203 / 202 us | 0 | PASS |
| 256 | 150 us | 2997/3000 | 0.100% | 88 us | 73 us | 203 / 202 us | 0 | PASS |
| 32 | 200 us | 2980/3000 | 0.667% | 88 us | 73 us | 203 / 202 us | 0 | PASS |
| 32 | 150 us | 2984/3000 | 0.533% | 88 us | 73 us | 203 / 202 us | 0 | PASS |
| 32 | 100 us | 2932/3000 | 2.267% | 89 us | 74 us | 203 / 202 us | mismatch=1, resync=1, overrun=1 | FAIL |

The full machine-readable table is in
`EXP4_RX_TRANSACTION_REDUCTION_RESULTS_20260902.csv`.

Raw coordinator logs:

- M=256, G=200: `../../../logs/exp4_task8_spimhwprofile_m256_0m_g200_l15_pac8_sb1703_sp2002_20260902_spiopt_spimhwprofile_phyfast/exp4_256_s3_r1_init.log`
- M=256, G=150: `../../../logs/exp4_task8_spimhwprofile_m256_0m_g150_l15_pac8_sb1703_sp2002_20260902_spiopt_spimhwprofile_phyfast/exp4_256_s3_r1_init.log`
- M=32, G=200: `../../../logs/exp4_task8_spimhwprofile_0m_g200_l15_pac8_sb1703_sp2002_20260902_spiopt_spimhwprofile_phyfast/exp4_32_s3_r2_init.log`
- M=32, G=150: `../../../logs/exp4_task8_spimhwprofile_0m_g150_l15_pac8_sb1703_sp2002_20260902_spiopt_spimhwprofile_phyfast/exp4_32_s3_r1_init.log`
- M=32, G=100: `../../../logs/exp4_task8_spimhwprofile_0m_g100_l15_pac8_sb1703_sp2002_20260902_spiopt_spimhwprofile_phyfast/exp4_32_s3_r1_init.log`

## Next safe optimization direction

Transaction-count reduction has reached an interface boundary: the remaining
register operations are separated by DW3000 register files or explicitly
required by the double-buffer protocol. The 1.1875 us hardware result instead
justifies profiling and specializing the per-transaction software path. A next
task should split the roughly 18 us non-wire cost into CS toggle, EasyDMA pointer
programming, event clear/check, anomaly-198 preparation, buffer assembly/copy,
and function/lock overhead. Any optimization must preserve per-transaction CS
toggle, the SYS/RDB clear ordering, buffer toggle, and existing fail-closed
checks. IRQ should remain excluded.
