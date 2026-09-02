# Exp4 post-fast-switch wait-budget sweep (2026-09-02)

## Outcome

The selected retained-PGF fast switch supports a default wait budget of:

- `BRRS_SYNC_BUFFER_US = 1703`
- `BRRS_EXP4_SYNC_PREP_US = 2002`

This is the smallest tested candidate satisfying all fail-closed checks and the directive's `arm p99 <= 80% of budget` rule. It passed 10/10 independent 1000-superframe S3 runs, covering 30,000 scheduled DATA packets. No deadline miss, delayed-RX/TX late event, RDB mismatch/incomplete event, overrun, SPI fault/recovery/timeout, or experiment timeout occurred.

This result does not change the repository defaults yet. The fast-switch feature remains default OFF, and no push was performed.

## Candidate derivation

Task 3 selected the PGF-retained path and measured these worst maxima:

- first-slot arm: 1377 us
- next-SYNC arm end-to-end: 1399 us

The directive's physical additions give the zero-margin bounds:

- `sync_buffer_min = 1377 + 42 = 1419 us`
- `sync_prep_min = 1399 + 269 = 1668 us`

Each multiplied value was rounded upward:

| Candidate | Margin | Sync buffer | Sync prep |
|---|---:|---:|---:|
| p30 | +30% | 1845 us | 2169 us |
| p20 | +20% | 1703 us | 2002 us |
| p10 | +10% | 1561 us | 1835 us |
| p0 | +0% | 1419 us | 1668 us |

## Hardware results

Configuration was M32, PAC8, S3, G=200 us, lead=15 us, persistent SPIM, polling, and retained PGF. Every standard candidate received three 1000-superframe runs unless fail-closed stopped it; the pass/fail boundary p20 received ten.

| Candidate | Runs | Received | PER (Wilson 95%) | Worst first p99 / budget | Worst sync p99 / budget | Min first slack | Faults | Result |
|---|---:|---:|---:|---:|---:|---:|---:|:---:|
| p30 | 3 | 8943/9000 | 0.633% (0.489-0.820%) | 1352/1845 = 73.28% | 1379/2169 = 63.58% | 411 us | 0 | PASS |
| p20 | 10 | 29798/30000 | 0.673% (0.587-0.772%) | 1353/1703 = 79.45% | 1384/2002 = 69.13% | 266 us | 0 | PASS |
| p10 | 3 | 8956/9000 | 0.489% (0.364-0.656%) | 1354/1561 = 86.74% | 1387/1835 = 75.59% | 127 us | 0 | **FAIL margin rule** |
| p0 | 1 | 2984/3000 | 0.533% (0.329-0.865%) | 1340/1419 = 94.43% | 1384/1668 = 82.97% | **0 us** | 0 | **FAIL firmware** |

The p10 firmware completed all three runs without a system fault, but it is not an acceptable budget because its first-slot p99 consumed 86.74% of the available sync-buffer budget. The p0 run reached zero coordinator RX-open slack and reported `schedule=FAIL`, `collection=FAIL`; the remaining two repetitions were intentionally not run after this fail-closed boundary was observed.

One p30 run and one p10 run initially lost a contiguous RTT final-statistics segment. Those attempts are preserved with `.prev` suffixes and classified INVALID. Immediate repeats completed and are the only versions included in the table.

Raw logs are under:

- `../logs/exp4_task4_p30_0m_g200_l15_pac8_sb1845_sp2169_20260902_spiopt_phyfast/`
- `../logs/exp4_task4_p20_0m_g200_l15_pac8_sb1703_sp2002_20260902_spiopt_phyfast/`
- `../logs/exp4_task4_p10_0m_g200_l15_pac8_sb1561_sp1835_20260902_spiopt_phyfast/`
- `../logs/exp4_task4_p0_0m_g200_l15_pac8_sb1419_sp1668_20260902_spiopt_phyfast/`

## Capacity implication at the current guard

With M32 airtime 97 us and G=200 us, the slot interval is 297 us.

- Previous 2000/2000 budget: DATA budget 6000 us, timing capacity `floor(6000/297) = 20` slots.
- Selected 1703/2002 budget: DATA budget 6295 us, timing capacity `floor(6295/297) = 21` slots.

Thus wait-budget optimization alone recovers one theoretical G200 slot. Larger gains still require guard reduction, which is Task 5. The p10 and p0 theoretical capacities of 22 and 23 slots must not be claimed because those candidates failed the safety policy.
