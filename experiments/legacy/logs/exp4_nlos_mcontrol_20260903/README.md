# NLOS 6.9 m — matched M32/M256 diagnostic at SB2000/SP2002

## Purpose and preflight adjustment

User agreed to an M256 control after repeated original-position M32 loss and asked about the true cause. One matched M32 and one M256 run will be taken, without moving boards or modifying firmware source. A read-only RX-path code audit is being performed separately; it does not control hardware.

The initially planned M256 full-configure SB1703/SP2002 image set was built but **not flashed or measured**. Preflight found an RX-opening timing risk: brrs_init.c RX_EARLY_US=ceil(preamble)+ceil(SFD)+lead, so M32 RX opening is SB−57 us and M256 is SB−285 us. SB1703 would require M256 RX at1418 us, whereas preceding M32 full-configure first-arm measurements reached1598 us (minimum1443). This is a prediction from code/other-mode measurements, not an observed M256 late result. Use **SB2000/SP2002 for both** modes to avoid mixing this unvalidated deadline with RF loss. The M256 SB1703 artifacts are preserved as unused builds.

## Fixed conditions

- Physical original NLOS6.9m positions retained: INIT/local1050270933; TX/remote N2=1050211584, N3=1050273888, N4=1050282818.
- M32 vs M256 only among selectable PHY parameters; PAC8, S3, G200, lead15, SB2000/SP2002, 1000 superframes, PSDU26/app16, polling optimized SPI, full dwt_configure, IRQ/fast-switch/profiling OFF.
- Each preamble naturally changes airtime, first RX opening and slot interval; this is an end-to-end firmware preamble comparison, not a channel-sensitivity-only isolation.
- Same source f420f95e29351e4b737cc76561bffae7191f4376 on diagnostic branch exp4-nlos-phy-ab-20260903. M32 images reused from earlier SB2000 build; M256 images newly built from identical tracked source/SDK/toolchain. Local untracked SDK symlink remains as before.
- nRF5 SDK17.0.2; SES8.28; GCC15.2.1(20251203)/Arm15.2.Rel1 and SEGGER runtime/compiler20.1.3 as recorded in earlier campaign. Metadata and manifest preserve per-role build/binary identifiers.
- Powered hub + adapter remain fixed by agreement; physical cable/port/orientation and door-material path have not been independently measured.
- Environment nlos_mcontrol_full, run1 for each M. Same logical role mapping; distinct filenames exp4_32_s3_r1 / exp4_256_s3_r1 preserve all existing results. Captures use --no-build but still flash/start each board.
- Order M32 then M256. Time variation, lack of randomization, single repetitions and board/slot confounding must be reported.

## Pass and interpretation rules

Do not declare success with any system fault, zero valid RX, missing capture or bad identity/hash. Check actual first-arm RX-open slack and delayed-late in each mode; a timing failure invalidates a pure RF comparison. Legacy aggregate PER ceiling5% remains only a diagnostic harness criterion. It is not final vehicle PER<1%, worst-node qualification or M256-equivalent reliability. A numerical acceptable relative PER degradation is not yet agreed.

M256 good/M32 poor would narrow the issue to a short-preamble-sensitive board/channel/software interaction, not prove propagation alone or clear the firmware. Both poor would prioritize a shared link/board/system issue. A later role-only swap with physical positions fixed is needed to isolate board identity from logical slot. No role-only experiment is part of this pair.

## Completed matched pair

All eight board captures completed with per-role firmware/raw hashes, assignments and parameters verified by `audit_mcontrol.py`. Results are preserved in `m32_r1.audit.json` and `m256_r1.audit.json`. M256's 12 HEX/ELF/build-log artifacts were verified identical between local and remote before flashing. Hardware SPI=32MHz, MCU=64MHz, build metadata=debug-project-O0_direct-spi-hot-functions-O3; newly built M256 ELF comments confirm GCC15.2.1/SEGGER compiler20.1.3.

| Board / role | M32 RX/1000 | M32 PER | M256 RX/1000 | M256 PER |
| --- | --- | --- | --- | --- |
| 1050211584 / N2 | 1000 | 0.0% | 1000 | 0.0% |
| 1050273888 / N3 | 533 | 46.7% | 1000 | 0.0% |
| 1050282818 / N4 | 996 | 0.4% | 1000 | 0.0% |
| Total /3000 | 2529 | 15.7% | 3000 | 0.0% |

M32 metadata timestamp2026-09-03T23:15:43+0900; M25623:19:17+0900. Each run1000 superframes. M32 final disposition=FAIL_PER; M256=PASS for this diagnostic run. All TX boards in both modes received1000/1000 beacons and reported1000/1000 TX completions. All system-integrity checks passed: no deadline miss, delayed schedule late, RDB mismatch/incomplete/overrun, deferred overflow, SPI errors/timeouts, zero-RX or incomplete captures.

Measured first RX arm maximum was1589us in M32 and1595us in M256; minimum RX-open slack353us and119us respectively, both positive. Thus the matched pair is not invalidated by an observed first-RX delayed-arm miss. M32 reported379 PHY RX error events (378FINT-only); M256 reported0. These event counts differ from packet-loss counts. All sensor setups succeeded on first attempts; completed worker logs show no setup retries/reconnect warnings. No human-passage report was received; absence of passage was not independently observed.

M256 observed0/1000 loss on each node, with nominal binomial Wilson95% upper limit0.383% per node (aggregate0/3000 upper0.128%). These intervals do not account for packet dependence, board/run/environment variability, and **do not certify true PER=0, vehicle PER<1%, or repeatable NLOS reliability**. The M32-vs-M256 node3 observed difference46.7 percentage points is not a small-degradation outcome in this fixture.

## Interpretation and next diagnostic

In this matched pair,888 delivered all1000 packets using M256 but only533 using M32. A total inability of the board to transmit or a preamble-independent total link failure does not explain this result. It does not prove that the board is fully healthy or that propagation alone causes the M32 loss. Preamble-dependent RX sensitivity/processing, board/path interaction, slot/rearm history, natural airtime/slot-timing changes and temporal variation remain possible.

Read-only code/historical findings are recorded in `root_cause_notes.md`. In particular, first-slot delayed RX versus later immediate rearm leaves a slot-history confound, and the old888/N3 2.2% M32 observation is genuine (although that old run had system errors). An M256 pass does not close the historical M32-regression investigation.

The highest-information proposed next test is M32 with the **boards and logical identities fixed**, but owner order234→324→234, so888/N3 is temporarily first and584/N2 second. This is distinct from physically swapping boards; it changes INIT's announced schedule using existing --sequence support. No such test has yet been run.

Both matched runs and all capture processes completed. No additional radio test, firmware source edit, commit or push was performed. Original frozen source and existing results remain preserved. All currently flashed boards hold the M256/SB2000/SP2002 full-configure diagnostic binaries; future M32 testing must explicitly reflash the matching set. The broader SB A/B investigation remains paused.
