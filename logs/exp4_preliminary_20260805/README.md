# Experiment 4 preliminary environment comparison

Date: 2026-08-05

Common configuration:

- Firmware: INIT rev4, NORMAL rev4
- Data preamble: 32 symbols
- Sensors: 1 (N2)
- Superframe: 10,000 us, 1,000 repetitions
- Data slot RMARKER: SYNC RMARKER + 3,000 us
- Lead margin: 12 us
- Slot guard: 100 us

Observed result:

- With the drying rack present, DATA PER was 24.5% (245/1000 lost).
- After removing the drying rack, DATA PER was 0.8% (8/1000 lost).
- Beacon reception was 1000/1000 and DATA delayed-TX was 1000/1000 in both runs.
- The programmed UWB DATA offset was exactly 3,000 us in both runs.
- Most of the difference was SFD timeout: 219 with the rack and 1 without it.

Interpretation:

The fixed-period scheduler and beacon reacquisition were stable in both runs. The observed difference is therefore associated with the 32-symbol DATA reception channel rather than a missing beacon or TDMA scheduling failure. A metal drying rack can change obstruction, reflection, antenna surroundings, and multipath. Because the two conditions currently have one run each and the rack removal may also have moved the boards or changed orientation, this is preliminary evidence rather than a controlled causal conclusion.

Recommended validation:

Repeat an A-B-A sequence (rack present, removed, present again) while fixing board position, height, and antenna orientation. Use at least five 1,000-superframe repetitions per condition.
