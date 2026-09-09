# NLOS 6.9 m — original-position return, single-run diagnostic

## Authorization and unchanged conditions

User confirmed that N2/N3 were restored to their original positions after the single physical-swap diagnostic. Run exactly one matched original-position return capture; preserve all preceding data. The intervening interrupted assistant turn did not start a capture. Preflight found no live local/remote Exp4 capture processes.

- N2 / 1050211584: original N2 location; N3 / 1050273888: original high-loss N3 location. INIT 1050270933 and N4 1050282818 unchanged by the setup instructions.
- M32/PAC8/S3/G200, lead 15 us, SB/SP=1703/2002 us, 1000 superframes.
- Full dwt_configure (fast switch OFF), optimized polling SPI ON; IRQ and diagnostic profiling OFF.
- Same per-role HEX images as the original-position and swapped-position captures; no source edits or rebuilding. Local and remote hashes checked against the preceding mode-1703 manifest. Source f420f95e29351e4b737cc76561bffae7191f4376, branch exp4-nlos-phy-ab-20260903.
- Powered hub + adapter remain the agreed power condition; physical restoration is user-reported. Cable/port/orientation details and the user's door-metal-versus-glass path observation have not been independently measured.
- A: original baseline `../exp4_nlos_sbab_full_6.9m_g200_l15_pac8_sb1703_sp2002_20260903_spiopt/`.
- B: position swap `../exp4_nlos_posswap_full_6.9m_g200_l15_pac8_sb1703_sp2002_20260903_spiopt/`.
- A-return: this folder, new environment nlos_posreturn_full, run 1, fixed firmware roles N2=584/N3=888/N4=818.

## Reliability goal clarified by the user

The research objective is throughput improvement while retaining M256-equivalent reception reliability, or allowing only a small, explicitly bounded degradation. Increased throughput alone does not compensate for unrestricted PER deterioration. The stated future vehicle target is PER below 1%; the legacy diagnostic aggregate verifier limit of 5% must not be represented as meeting that target. A numerical acceptable M-versus-M256 PER difference has not yet been agreed; it must not be selected after looking at favorable results. No M256 comparison or vehicle qualification is performed in this one-run diagnostic.

## Completed A–B–A sequence

All three entries below used identical per-role binaries, M32/PAC8/S3/G200/lead15, SB1703/SP2002, full configure and optimized polling SPI. Each is 1000 superframes, with 1000 expected reports per node.

| Board / logical role | Original A (22:36:57) PER | Swapped B (22:53:03) PER | Original-return A (23:01:27) PER | Return RX |
| --- | --- | --- | --- | --- |
| 1050211584 / N2 | 0.0% | 4.9% | 0.1% | 999/1000 |
| 1050273888 / N3 | 36.7% | 5.6% | 48.3% | 517/1000 |
| 1050282818 / N4, not moved | 0.9% | 1.5% | 0.5% | 995/1000 |
| Total | 12.533% | 4.000% | 16.300% | 2511/3000 |

Times are metadata timestamps on 2026-09-03, UTC+09:00, not precise on-air start times. The trials are separated in time, not simultaneous or randomized.

Return capture `run1.audit.json` confirms all four raw/firmware hashes, board assignments, parameters, completion markers, and full system-integrity checks. All TX nodes received 1000/1000 beacons and reported 1000/1000 TX successes; this does not imply end-to-end packet delivery. Deadline misses, delayed schedule late, buffer mismatch/incomplete/overrun, SPI errors/timeouts, and deferred queue overflow were zero. The return run is **FAIL_PER** (16.3% > diagnostic aggregate 5% limit), not PASS. N3 PER Wilson 95% interval is 45.215–51.398%; no qualification against the future vehicle <1% target is claimed.

PHY receive errors were 403 events (sfdto=0, phe=2, fce=0, fsl=0, fint-only=401). Most subtype details remain unresolved. Error-event counts do not equal total packet losses. All TX workers used setup attempt 1/3; completed worker logs show no reconnect warnings or setup retries. INIT capture completed normally. No human-passage report was received during this run; absence of passage was not independently observed.

## Interpretation and stop point

888's high loss returned after original-position restoration: 36.7→5.6→48.3%. 584's loss rose at the other position and fell after restoration: 0→4.9→0.1%. This A–B–A pattern strengthens the position/channel association relative to the single A–B comparison. It is not evidence that 888 has a position-independent fixed failure rate.

However, at the nominal high-loss position, 584 had 4.9% while 888 had 36.7%/48.3%. A position-only model with interchangeable boards/slots is not established. Board RF differences or a fault, orientation/placement details, cable/port effects, logical N3 slot versus N2, and temporal channel variation remain candidates. The user's metal-versus-glass door-path observation remains an unmeasured hypothesis. The A–B–A sequence does not prove the door material is the sole cause, certify 888 healthy, or absolve changes relative to the historical low-PER firmware.

No M256 capture was performed in this sequence. An M256 control in the same restored setup is needed before quantifying M32-minus-M256 reliability degradation; separating physical board from logical slot requires an independent role-only crossover. These are proposed next checks, not completed or automatically scheduled work.

One return run completed and both local INIT and remote TX capture processes exited. No additional radio run was started. Original and diagnostic firmware sources were not changed, and no push was performed. All previous and current raw logs are preserved. SB A/B remains paused after its first pair.
