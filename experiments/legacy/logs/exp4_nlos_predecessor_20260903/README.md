# M32 predecessor identity crossover

Unchanged diagnostic source f420f95e29351e4b737cc76561bffae7191f4376; no frozen/source modifications. M32/PAC8/S3/G200/lead15/SB2000/SP2002,1000SF, full PHY configure, optimized polling SPI, IRQ/profiling OFF.

Fixed original positions and powered hub: INIT1050270933,N2=1050211584,N3=1050273888,N4=1050282818. Planned sequence234(r1) →432(r1) →234(r4); all rotation0. N3 remains second at RMARKER2297us. N2 and N4 exchange first/last transmission only; boards are not moved.432 changes INIT image only; all three TX HEX/ELF/build logs are byte-identical copies of the matching archived G200/SB2000 build, verified in firmware_manifest.json. Not the G400 TX binaries.

Interpretation: a change in N3 PER with N4 first-slot RX complete supports predecessor/history dependence; it does not prove AGC, EMI, or a software mechanism. Poor N4 first-slot reception confounds this interpretation. Any errors or loss are preserved, not reclassified as PASS. Audit only intercepts the existing aggregate5% PER limit to continue other integrity checks; it never relaxes system errors. A single1000SF diagnostic is not final qualification.

## Completed sequence

| Order/run | N2 PER | N3 PER | N4 PER | Total RX | Disposition |
| --- | ---: | ---: | ---: | ---: | --- |
|234/r1|0.0%|50.7%|0.8%|2485/3000|FAIL_PER|
|432/r1|1.3%|48.1%|0.0%|2506/3000|FAIL_PER|
|234/r4|0.1%|52.5%|0.9%|2465/3000|FAIL_PER|

Each board reported1000/1000 beacon receptions and TX successes in all3 runs. Hashes, roles, advertised owner sequence and parameters match; all subsequent system-integrity checks passed. N4 first-slot RX in432 is1000/1000, so the comparison is not explained by an absent preceding successful receive. Large N3 loss persists with either predecessor. This weakens a cause exclusive toN2 as predecessor; it does not establish a universal second-slot defect or a specific RF mechanism.

Original234/G200/SB2000 configuration restored on all boards. Last run234/r4 is the immediate old888 reference for the pending new-board comparison, provided fixture conditions remain fixed. All capture workers completed; no automatic experiment remains. User confirmed a matching spare nRF52840+DWM3000 assembly is ready but has not yet confirmed replacement.

Next manual step: replace only888/N3 assembly at the same antenna position/orientation, using the same cable/hub port and powered hub. Preserve888 for old→new→old restoration. Resolve new probe serial before flashing the identical N3 HEX (b2042182f1528d4288f439580caf8954b01b7990bd9ffdc4420561ab2a4bf21d). INIT/N2/N4 stay unchanged. Improvement would establish a fixture-dependent assembly effect, not yet identify a defective component. Hardware replacement has NOT occurred in this campaign.
