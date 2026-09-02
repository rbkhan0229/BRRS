# Exp4 RX service-path profile (2026-09-02)

## Scope and validity

- Implementation commit: `bfc1021` on `exp4-rx-path-profile-task7`.
- `BRRS_OPT_RX_PATH_PROFILE` defaults to 0. `--rx-path-profile` selects a separate diagnostic image; it cannot be combined with the existing IRQ-dispatch mode.
- Valid run: M32, PAC8, S3, G=200 us, lead=15 us, sync buffer/prep=1703/2002 us, persistent SPIM, retained-PGF fast PHY switch, 1000 superframes.
- Coordinator result: 2987/3000 frames, PER 0.433%, collection PASS. N2/N3/N4 each transmitted 1000/1000 scheduled frames and passed verification.
- Integrity result: zero delayed-late events, deadline misses, RX-buffer mismatch/incomplete/overrun events, SPI faults, timeouts, missing IRQ timestamps, duplicate IRQ timestamps, or histogram overflows. Ten radio RX-error events and three further losses produced the 13 missing frames; link status is therefore LOSS while collection status remains PASS under the predeclared 5% PER ceiling.
- Raw logs are under `../logs/exp4_task7_rxprofile_0m_g200_l15_pac8_sb1703_sp2002_20260902_spiopt_rxprofile_phyfast/`. Run 2 is the valid result.
- Because the hardware run preceded the implementation commit, its metadata records base `e9d8310` with a dirty tree. A clean rebuild at `bfc1021` reproduced the recorded coordinator firmware SHA-256 exactly: `ba12409e1b47a77df5214bc9dd0cae9dd65502b4e5558e371c46d937aadd452c`.

Run 1 is INVALID and excluded from the formal result. Its measurements completed, but the first verifier revision compared 1988 good-path next-RX commands with 1998 total rearm commands, the latter including ten error-recovery rearms. Commit `bfc1021` added a dedicated `good_rearm_commands` counter; run 2 then passed the firmware and host-side structural checks. The invalid log is retained as `exp4_32_s3_r1_init.log`.

## Measurement semantics

The normal receiver remains FINT-polled and foreground code remains the only SPI owner. In the diagnostic build, the DW3000 GPIO interrupt only captures the IRQ-assert cycle so phase 1 can be measured; the ISR does not dispatch RX work or use SPI. Raw cycle deltas are saved on the hot path, while histogram/statistics aggregation occurs only after `CMD_DB_TOGGLE`, outside every measured phase and the total endpoint.

The implementation's actual good-frame path differs from the conceptual eight-step list in four important ways:

1. It does not read global `SYS_STATUS` on the good path; it reads `FINT_STAT` in the polling loop and then `RDB_STATUS`.
2. Frame length and adjusted timestamp are fetched in one adjacent 9-byte `FINFO + RX_TIME` SPI burst. Only their CPU decode costs can be separated without changing the path being measured.
3. Exp4 copies the 8-byte protocol header only. It does not copy the unused 16-byte application payload on the hot path.
4. Returning a manual double buffer consists of both `RDB_STATUS` W1C and `CMD_DB_TOGGLE`; both are reported rather than hiding the first operation inside the second.

## Phase results

All times are microseconds. `next_rx_fast_command` occurs only after a good frame that has a later slot, hence 1989 samples rather than 2987. The two zero-count rows explicitly prove that global `SYS_STATUS` and payload copying are absent from the good path.

| Requested stage / actual operation | Count | Min | Max | Average | p99 |
|---|---:|---:|---:|---:|---:|
| 1. IRQ assert to polling-loop FINT detection | 2987 | 10 | 37 | 24.795 | 37 |
| 2a. Global `SYS_STATUS` read | 0 | 0 | 0 | 0.000 | 0 |
| 2b. `RDB_STATUS` read | 2987 | 21 | 21 | 21.000 | 21 |
| 3. Next-RX fast command | 1989 | 19 | 19 | 19.000 | 19 |
| 4/5. Combined FINFO + RX_TIME SPI read | 2987 | 24 | 24 | 24.000 | 24 |
| 4. Frame-length decode after combined read | 2987 | 1 | 1 | 1.000 | 1 |
| 5. Timestamp decode after combined read | 2987 | 1 | 1 | 1.000 | 1 |
| 6a. 8-byte header copy | 2987 | 25 | 25 | 25.000 | 25 |
| 6b. Application-payload copy | 0 | 0 | 0 | 0.000 | 0 |
| 7. Global RX-status clear | 2987 | 23 | 25 | 23.666 | 24 |
| 8a. `RDB_STATUS` W1C | 2987 | 20 | 20 | 20.000 | 20 |
| 8b. `CMD_DB_TOGGLE` | 2987 | 19 | 19 | 19.000 | 19 |
| **IRQ assert to DB-toggle complete** | **2987** | **168** | **223** | **201.379** | **223** |

The first three explicit phase p99 values sum to 77 us (`37 + 21 + 19`), but this is not a composite percentile because the samples need not peak on the same event. The existing rearm scope, which also contains branches and timer/accounting work, measured average 88.793 us and maximum 102 us. That reconciles the earlier approximately 88 us observation with the internal phase values.

## Bottleneck conclusion

Task 2's pattern did not repeat: no two RX phases contain 79% of the cost. The largest individual average is the 8-byte header transaction at 25 us, only 12.4% of the 201.379 us total average. Poll detection has the largest tail at p99 37 us. Metadata read and status clear are approximately 24 us each; RDB read/clear and the two fast commands are each 19-21 us. The two CPU decodes together cost only 2 us, about 1.0%.

After weighting `next_rx_fast_command` by its 1989/2987 occurrence rate, the explicitly measured phases sum to about 172.1 us per good event, 85.5% of the total average. Device-facing polling/SPI/fast-command work accounts for about 170.1 us, or 84.5%; CPU decodes account for 2.0 us; the remaining approximately 29.3 us covers branches, timer reads, bookkeeping, and diagnostic-ISR/control overhead between phase boundaries. The optimization target is therefore transaction count and per-transaction software overhead, not frame-length or timestamp arithmetic.

The clearest candidate clusters are:

1. Replace the FINT SPI polling transaction with a proven low-overhead IRQ/pending-event handoff; polling alone reaches p99 37 us.
2. Reduce fixed transaction setup around short reads and writes. The combined metadata read already demonstrates the intended direction, while the 8-byte header still costs 25 us because setup dominates payload transfer time.
3. Re-examine, with fail-closed double-buffer tests, whether global status clear, RDB W1C, and DB toggle can be safely reordered or partially deferred. Together they consume approximately 63 us at their p99 values. None may simply be removed because they enforce the current buffer-lifecycle invariants.

## Profiling overhead and guard interpretation

This diagnostic image is deliberately not the production guard reference. In three unprofiled M32/PAC8/S3 runs with the same 1703/2002 us wait budgets and retained-PGF fast switch, the existing event-detect-to-buffer-free metric had p99 201 us and maximum 202 us, and required guard was 87-88 us. The profiling run measured that legacy hot-path metric at p99/max 233 us and required guard 117 us. Thus timestamp IRQ plus phase instrumentation added about 32 us (15.9%) to the legacy p99.

Use the 223 us IRQ-assert-to-toggle p99 to understand the instrumented phase composition, not to increase the production guard. The unprofiled measurements remain the guard-selection evidence; the profile identifies where their approximately 202 us service time is spent.
