# BRRS SES build configurations

DATA preamble length is carried in the beacon `m` field. Normal nodes validate
`m` and apply the corresponding DW3000 TX preamble at runtime. Changing only
the preamble therefore requires rebuilding INIT, not the Normal node.

## Stage 0

- INIT: `Stage0_L*_T*_Init`
- N2: `Stage0_Normal`

## Experiment 1

- INIT: `Exp1_32_Init`, `Exp1_64_Init`, `Exp1_128_Init`, or `Exp1_256_Init`
- N2: `Exp1_Normal`

## Experiment 2

- INIT: `Exp2_32_Init`, `Exp2_64_Init`, `Exp2_128_Init`, or `Exp2_256_Init`
- N2: `Exp2_Normal`

## Experiment 3

- A: `Exp3_A_Init` and `Exp3_A_Normal`
- B: `Exp3_B_Init` and `Exp3_B_Normal`
- C: `Exp3_C_Init` and `Exp3_C_Normal`

A/B/C remain separate because they change SFD length and PHR rate, not only
the DATA preamble length.

## Experiment 4

- INIT: choose an existing 32, 64, or 256-symbol `Exp4_<m>_S<n>_Init` for the
  advertised preamble and schedule.
- Sensors: use `Exp4_N2` through `Exp4_N8` once per physical node ID.

The same Exp4 sensor image follows 32, 64, 128, or 256-symbol beacons and uses
only slots assigned to its node ID. Legacy length- and sensor-count-specific
Normal configurations were removed to prevent mismatched builds.

Generic `Debug` and `Release` configurations intentionally fail the BRRS
explicit-profile check. They remain only as SES baseline configurations; select
one of the named experiment configurations before Build and Debug.
