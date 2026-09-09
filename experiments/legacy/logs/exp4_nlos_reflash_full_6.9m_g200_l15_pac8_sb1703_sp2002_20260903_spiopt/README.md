# NLOS 6.9 m — same-image reflash/restart repeat, one run

## User request and scope

The user requested reflashing and one additional run before an M256 control. The boards remain at the restored original positions. This is a repeat of the previous original-position capture, not a firmware change or a power-cycle experiment. Prior captures also invoked the standard RTT runner's flash/start path; do not describe them as unflashed controls.

- INIT/local: 1050270933. TX/remote: N2=1050211584, N3=1050273888, N4=1050282818. Logical roles and physical positions fixed.
- M32/PAC8/S3/G200, lead 15 us, SB/SP=1703/2002 us, 1000 superframes.
- Full dwt_configure (fast OFF), optimized polling SPI ON, IRQ and diagnostic profiling OFF.
- Existing per-role HEX images are reused without rebuilding via --no-build, which skips building, not the capture runner's flash operation. No mass erase, factory reset, UICR alteration, or USB power cycle is requested or performed as a separate step.
- Source f420f95e29351e4b737cc76561bffae7191f4376, diagnostic branch exp4-nlos-phy-ab-20260903. Local/remote HEX hashes checked against the previous mode-1703 manifest before capture. No tracked source edits; the local untracked SDK symlink is retained as before.
- Powered hub and adapter remain the agreed setup; cable/port/orientation details are not independently measured.
- Fresh environment nlos_reflash_full / run 1 preserves all earlier logs. Previous comparison: `../exp4_nlos_posreturn_full_6.9m_g200_l15_pac8_sb1703_sp2002_20260903_spiopt/`, PER N2=0.1%, N3=48.3%, N4=0.5%, total=16.3%, all TX successes 1000/1000, system-integrity checks passed, FAIL_PER.

## Interpretation and stop rule

Run once and report. Do not automatically start M256 or resume the paused SB A/B sequence. A repeated high loss after standard flash/restart would show that this procedure did not clear the symptom in this run; it would not rule out all persistent hardware/PHY state, power effects, or firmware defects. A decrease alone would not prove stale firmware state because prior runs also used flash/start and channel conditions can vary.

Diagnostic aggregate PER limit remains 5%; this is not the user's final vehicle <1% target or evidence of M256-equivalent reliability. Preserve failures and nonzero PHY loss even when system counters are zero.

## Completed result

One 1000-superframe run completed at metadata timestamp 2026-09-03T23:08:15+0900. All four boards went through the standard flash/start procedure with identical per-role HEX hashes. The runner calls reset(halt=True), flash_file, its usual APPROTECT check, then reset(halt=False); --no-build does not skip this path. No separate full erase or power removal was performed.

| Board / role | Previous original-position PER | This repeat PER | This RX/expected |
| --- | --- | --- | --- |
| 1050211584 / N2 | 0.1% | 0.1% | 999/1000 |
| 1050273888 / N3 | 48.3% | 43.3% | 567/1000 |
| 1050282818 / N4 | 0.5% | 0.4% | 996/1000 |
| Total | 16.3% | 14.6% | 2562/3000 |

All three sensors received 1000/1000 beacons and reported 1000/1000 TX successes. Deadline miss, delayed schedule late, RDB mismatch/incomplete/overrun, SPI errors/timeouts and deferred queue overflow were zero. PHY RX errors remained nonzero: 346 events (sfdto=0, phe=1, fce=0, fsl=1, fint-only=344); the dominant FINT-only subtype is unresolved and error-event counts are not total packet losses.

`audit_reflash.py` verified all raw/firmware hashes, role assignments, parameters, completion markers and the full system-integrity checks. The overall result remains **FAIL_PER**, not PASS. N3 PER Wilson 95% interval is 40.260–46.391%. See `run1.audit.json` for the complete audit. All TX workers reached READY on setup 1/3 and completed; saved worker logs show flash/start without reconnect warnings or setup retries. INIT also completed normally. No human-passage report was received during the run; passage absence was not independently observed.

The high-loss symptom persisted after this standard same-image flash/restart. The decrease from 48.3% to 43.3% does not establish a reflash benefit: the preceding run also used flash/restart, and time/channel conditions are not controlled. This result does not isolate all persistent PHY state, power, hardware or firmware causes. The user's vehicle <1% and M256-comparable reliability goals remain unmet/unverified in this M32 diagnostic.

Both capture processes exited. No second repeat, M256 test, code edit or push was performed. Existing results remain preserved, and the broader SB A/B investigation remains paused.
