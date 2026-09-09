# NLOS 6.9m — M32 owner order 234 → 324 → 234

## Authorized investigation and design

User asked to continue until the cause is identified and suggested replacing888. First isolate slot order without asking for a physical swap. Do not invent a single root cause from a suggestive result; pause for user action if board replacement becomes necessary. Firmware source/frozen branch remains preserved; this uses existing schedule build support, not an RX-path implementation change.

- Original physical positions: INIT1050270933 local; N2=1050211584, N3=1050273888, N4=1050282818 remote. Powered hub and USB topology fixed by agreement. No physical movement or role reassignment.
- M32/PAC8/S3/G200/lead15, SB2000/SP2002, 1000SF/run; full dwt_configure, optimized polling SPI; IRQ/fast switch/profiling OFF.
- A1: default234/run1; B: custom324/run1; A2: default234/run4. Run4 preserves serial-role rotation0. Fresh environment nlos_slotorder_full; sequence324 uses its own path suffix.
- INIT custom324 is built from the unchanged source f420f95e29351e4b737cc76561bffae7191f4376 in exp4-nlos-phy-ab-20260903. TX role binaries remain byte-identical across A/B (sensors already follow the beacon owner schedule); copied into the matching sequence image directory without rebuilding them.
- A/B shifts which board is first/second and its RMARKER by297us. It does not isolate every consequence of first-slot readiness, previous-frame reception, delayed/immediate RX or absolute slot timing.
- Verify each role/HEX/raw hash, advertised owner order, received source-to-slot classification, TX counts, PER and system counters. Any system fault invalidates a clean RF interpretation. Aggregate diagnostic5% PASS is not final vehicle<1% or M256-equivalence.
- No hardware power removal, new board or firmware source fix is part of these three runs. Human passage reports will be recorded; absent reports do not independently prove no passage.

## Decision boundaries

If loss follows the second slot, investigate RX rearm/history and slot-dependent timing. If loss remains with888 at its physical position when it becomes first, retain board/path hypotheses. If234 recovery does not return the baseline, repeat/interleave before interpreting. A clean M256 result alone does not settle historical M32 regression; the earlier low-PER rev24 run had system errors but genuine lower observed loss.

## Completed crossover

| Order/run | N2 PER | N3 PER | N4 PER | Total RX/3000 | Total PER |
| --- | --- | --- | --- | --- | --- |
|234/r1|0.0%|49.2%|0.4%|2504|16.533%|
|324/r1|2.4%|11.5%|1.7%|2844|5.200%|
|234/r4|0.1%|52.2%|0.4%|2473|17.567%|

All three runs are FAIL_PER under the unchanged aggregate5% limit. All four role/raw/firmware hashes and expected parameters were verified in each run; all sensor beacon/TX counts were1000/1000 and system-integrity checks passed. The swapped324 successful RX classification confirms N3 is observed slot0 andN2 slot1. Failures were not discarded or converted to PASS. Full audit JSONs are preserved per run.

N3 loss fell when made first and returned when made second, while no board was physically moved and TX binaries stayed byte-identical. This supports a slot/order/timing interaction, not a universal second-slot failure (N2 second was2.4%, not49%). It does not identify an AGC, PLL, SPI or board defect. Absolute N3 RMARKER changed2297→2000→2297us along with first-versus-rearmed RX. Further controls are needed: G400 at the sameSB changes inter-frame spacing and absolute N3 time; first-slot324 atSB2297 holds N3's original absolute time while removing the preceding frame/rearmed history. These are recorded separately rather than pooled into this crossover.

No source code was changed. The caller continues the authorized root-cause investigation in a separate history-controls campaign; physical board replacement has not been requested yet.
