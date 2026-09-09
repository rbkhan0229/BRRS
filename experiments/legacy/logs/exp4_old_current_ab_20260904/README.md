# Exact historical vs current matched firmware A/B/A

2026-09-04. User authorized old/current/old comparison at the same original positions. No physical movement, board role rotation, power topology change or source-code fix is part of this comparison.

- INIT933 local; remote N2=584, N3=replacement4212, N4=818.
- M32/PAC8/S3/G250/lead15/SB3000/SP2500/1000SF, full PHY configure, IRQ/fast/RX diagnostic/profiling OFF.
- Old: four exact Aug25 HEX hashes. Candidate source90cdffb, exact source commit not proven. Original old N3 was888; today's within-session comparisons use4212 throughout.
- Current: source55a23fa, firmware4f0c9be; SPI optimization ON. Newly built at matched longer wait and G250, not default G200/short-wait final setup.
- Same current RTT capture code for both, no reconnect retry, explicit board serial, hash checking, bounded timeouts, no overwrite or erase-all.
- First sequence: old_r1 → current_r2 → old_r3. Each side is separately supervised; TX ALL_READY before INIT. New session directories preserve failures.
- All four old HEX hashes verified against original metadata. Their original filenames are recorded in manifest; misleading lead11 directory name does not override the hash-matched metadata.
- Old compatibility verifier extracted from90cdffb unchanged and successfully validated original G250 raw before hardware. Current verifier used for current raw. Common checks and 5% PER threshold applied independently; absent old SPI instrumentation must NOT be reported as measured zero.
- 5% is a diagnostic threshold only, not final vehicle/reliability acceptance. Every run's PER is reported even when collection succeeds. Zero valid RX or any detected system fault is never PASS.

Status: all three runs completed and audited. See [RESULTS.md](RESULTS.md). All three are FAIL_PER; recorded system-integrity checks passed. No collector processes remain. Boards currently contain the final old_r3 historical G250 images; they have NOT been restored to the current firmware.
