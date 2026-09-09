# NLOS M32 — predecessor/spacing/absolute-time controls

User authorized continued root-cause isolation. Original physical positions, serial roles and powered hub remain fixed. Source f420f95 on diagnostic branch exp4-nlos-phy-ab-20260903 is unchanged. All images use M32/PAC8/S3/lead15/SP2002, full configure, optimized polling SPI, IRQ/fast/profiling OFF,1000SF. Default G200 remains the official setting; larger guard here is diagnostic only.

Prior order crossover234→324→234 gave N3 PER49.2→11.5→52.2%, with TX1000/1000 and system errors0. Since N3's RMARKER moved2297→2000→2297us, that crossover alone does not separate absolute time from receiver/predecessor history.

Planned controls:

- `g400`: order234, SB2000, G400; N3 RMARKER2497us. Changes inter-frame gap and absolute N3 time, not only one underlying RF factor.
- `first2297`: order324, SB2297, G200; N3 first/delayed at2297us. Compare with234/SB2000/G200 N3 second/rearmed at the same2297us. All four images rebuilt for matchingSB rather than weakening sensor boot checks.
- A matching G200/order234/SB2000 return can follow to check time drift; further controls depend on results.

Keep raw capture/identity/hash/system checks fail-closed. Aggregate5% threshold is diagnostic only, not vehicle<1% or M256-equivalence. No specific physical mechanism is proven by an improvement alone. No new board/USB power cycle is performed without user involvement.

## Completed controls

| Control | N2 PER | N3 PER | N4 PER | Total RX | Disposition |
| --- | ---: | ---: | ---: | ---: | --- |
| G400, order234, SB2000, optimized SPI, r1 | 0% | 40.7% | 0.3% | 2590/3000 | FAIL_PER |
| G200, order324, SB2297, optimized SPI, r1 | 2.1% | 12.4% | 1.5% | 2840/3000 | FAIL_PER |

Both runs have TX1000/1000 on each board, matched hashes/roles/parameters and all subsequent system-integrity checks passed. N3 first at2297us remains much better than N3 second at2297us (prior49.2%,52.2%). Thus absolute N3 transmit time alone does not explain the difference. Receiver/predecessor history is implicated; no analog mechanism is proven.

## SPI-mode isolation

Compare optimized → legacy → optimized at G400/order234/SB2000/SP2002. Only INIT differs; legacy TX HEX/ELF/build artifacts are copied byte-identically from the corresponding optimized G400 build. `firmware_manifest.json` records all16 images. The SPI flag bundles multiple software changes and does not isolate persistence or establish EMI.

Legacy r1 ended with TX harness timeouts before INIT was started; no INIT log or valid RF measurement exists. Preserved under the raw legacy directory as **ABORTED_BEFORE_INIT**, excluded from PER comparisons. Retry uses r4 to avoid overwriting and retain rotation0. Optimized return also uses r4, in the distinct `_spiopt` folder.

Completed ON→OFF→ON: N3 PER40.7%→45.7%→41.0%; total RX2590→2537→2586/3000. Legacy r4 N2/N4 PER0%/0.6%; optimized return r4 N2/N4 PER0%/0.4%. All TX1000/1000, identities/hashes/parameters match, all system-integrity checks passed. Required guard87→116→87us, below diagnosticG400; legacy hot-path max279us. These results do not support disabling SPI optimization as a solution to the large loss. Legacy here is currentrev47 with SPI flags OFF, not the Aug25rev24 firmware. Audits preserved as JSON.
