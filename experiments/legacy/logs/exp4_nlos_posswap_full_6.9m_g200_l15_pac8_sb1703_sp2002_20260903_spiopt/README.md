# NLOS 6.9 m — N2/N3 physical position swap, single-run diagnostic

## User authorization and setup

On 2026-09-03 the user confirmed that the physical swap was complete and requested one run, followed by a report before further experiments or restoring the original positions.

- N2 / 1050211584 moves to the previous N3 high-loss position.
- N3 / 1050273888 moves to the previous N2 low-loss position.
- Logical firmware roles remain tied to board serials. INIT 1050270933 and N4 1050282818 remain fixed by the setup instructions.
- Powered hub with external adapter remains the agreed power condition. Instructions were to preserve each board's cable/port and reproduce the original orientation at each position; these details have not been independently observed.
- User observation: the former high-loss position's straight path to RX appears to intersect only the metal portion of the door, whereas other positions intersect a glass portion. Record this as a geometry/material hypothesis, not a verified causal conclusion.

## Matched comparison

Compare against the immediately preceding completed original-position run:
`../exp4_nlos_sbab_full_6.9m_g200_l15_pac8_sb1703_sp2002_20260903_spiopt/exp4_32_s3_r1_init.log`.

- M32/PAC8/S3/G200, lead 15 us, SB/SP=1703/2002 us, 1000 superframes.
- Full dwt_configure (fast switch OFF), optimized polling SPI ON, IRQ and diagnostic profiling OFF.
- Source f420f95e29351e4b737cc76561bffae7191f4376, diagnostic branch exp4-nlos-phy-ab-20260903. No source edits or new build; reflash the same per-role HEX files with --no-build.
- The original-position firmware manifest is `../exp4_nlos_sb_ab_20260903/firmware_manifest.json` (mode 1703). All four local/remote HEX hashes were checked before flashing and match this manifest.
- Original-position r1: N2 1000/1000 (PER 0.0%), N3 633/1000 (36.7%), N4 991/1000 (0.9%), total 2624/3000 (12.533%). All sensors received 1000 beacons and reported 1000 TX successes; system-integrity checks passed, overall disposition FAIL_PER.
- Swap run: environment nlos_posswap_full, run 1, fixed serials N2=1050211584 / N3=1050273888 / N4=1050282818. New raw paths preserve prior data.

## Interpretation rules

Loss moving to 584 at the old high-loss position supports a position/channel association over a board-888-only or logical-N3-only explanation. Loss remaining with 888 does not itself prove defective hardware: its firmware slot, cable, and port remain associated with it. One before/after pair cannot distinguish door metal attenuation from other position-dependent multipath/orientation effects or temporal changes, and does not settle the earlier firmware-regression question.

## Completed single-run result

Capture metadata timestamp: 2026-09-03T22:53:03+0900. One run / 1000 superframes completed; no second run was started.

| Board / unchanged role | Original-position PER | Swapped-position PER | Swapped RX/expected |
| --- | --- | --- | --- |
| 1050211584 / N2 | 0.0% | 4.9% | 951/1000 |
| 1050273888 / N3 | 36.7% | 5.6% | 944/1000 |
| 1050282818 / N4 (not moved) | 0.9% | 1.5% | 985/1000 |
| Total | 12.533% | 4.000% | 2880/3000 |

`audit_position_swap.py` checked all four raw/firmware hashes, board-role assignments, unchanged parameters, completion markers, sensor TX counts, and the original verifier's system-integrity checks. Detailed results and Wilson 95% intervals are in `run1.audit.json`.

- All three TX nodes received 1000/1000 beacons and reported 1000/1000 successful TX attempts. TX completion does not imply receiver delivery.
- Deadline misses, delayed scheduling late, buffer mismatch/incomplete/overrun, SPI errors/timeouts and deferred queue overflow were zero.
- PHY receive errors were nonzero: 97 events (sfdto=1, phe=10, fce=0, fsl=1, fint-only=85). They are not all packet losses and the FINT-only subtype remains unresolved.
- All TX workers reached READY on setup attempt 1/3 and completed; their saved worker logs show no reconnect warnings or setup retries. INIT capture likewise completed normally.
- The existing aggregate PER <=5% verifier reports **PASS**, but link=LOSS and N3 PER=5.6%. This is not a loss-free result, a per-node 5% pass, or final NLOS qualification.
- No human-passage report was received during this run; passage absence was not independently observed.

## Limited interpretation and stop point

Moving 888 reduced its PER by 31.1 percentage points; moving 584 to the old high-loss position increased its PER by 4.9 points. This supports investigating position/channel effects but **does not show the previous 36.7% loss simply transferring to 584**. Board sensitivity differences, orientation/cable effects, and temporal channel changes remain possible. The user's door-metal observation is plausible as a hypothesis, not established by this one pair. These results neither certify all hardware healthy nor exonerate all firmware changes.

The before/after comparison is not interleaved and is separated in time (original 22:36:57 vs swap 22:53:03 metadata timestamps). An original-position return run with the same binaries would be the next discriminating check. Do not initiate it until the user restores positions and confirms. All raw logs are preserved and no firmware source or frozen branch was changed.

### Follow-up: original-position return completed

After user confirmation, one matched return run completed at metadata timestamp 23:01:27: N2 0.1%, N3 48.3%, N4 0.5%, aggregate 16.3% (FAIL_PER), system-integrity checks passed. Full A–B–A results and remaining confounders are preserved in `../exp4_nlos_posreturn_full_6.9m_g200_l15_pac8_sb1703_sp2002_20260903_spiopt/README.md`. This strengthens the location association but does not isolate door material, physical board, or logical slot. No further run was started.
