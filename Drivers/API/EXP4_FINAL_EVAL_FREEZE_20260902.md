# BRRS Exp4 final-evaluation freeze

Date: 2026-09-02  
Branch: `exp4-final-eval-20260902`  
Source base: `104651e`

## Frozen firmware configuration

| Field | Value |
|---|---|
| RX event handling | FINT polling |
| SPI | persistent SPIM + direct hot-path transfers, 32 MHz |
| PHY transition | delta fast switch with PGF retained |
| SYNC-to-first-DATA budget | 1703 us |
| DATA-to-next-SYNC reserve | 2002 us |
| Inter-slot guard | 200 us |
| RX lead | 15 us |
| DATA PAC | 8 |
| Superframes per standard run | 1000 |
| IRQ pending mode | disabled |
| PHY configuration profiler | disabled |
| RX-path profiler | disabled |
| SPIM START-to-END profiler | disabled |
| Continuous RX | not used |

`brrs_exp4_final_build.sh` is the only build entry point for final-evaluation
images. It intentionally exposes only preamble, sensor count, and role. Timing,
SPI, PHY-switch, IRQ, and profiling choices are fixed in the wrapper.

Example for the required M256/S3 smoke image set:

```sh
Drivers/API/brrs_exp4_final_build.sh 256 3 all
```

The expected image directory is:

```text
Drivers/API/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4/
  plen256_sensors3_sb1703_sp2002_guard200_spiopt_phyfast/
```

## Fail-closed acceptance policy

A run is valid only when the coordinator and every sensor verifier pass. The
coordinator must report a non-zero valid RX count and zero deadline misses,
delayed RX/TX late events, RDB host mismatches, incomplete RDB states, buffer
overruns, SPI errors/recoveries/timeouts, experiment timeouts, configuration
errors, and wrong-slot/superframe records. PHY losses remain visible as PER and
are not reclassified as successful receptions.

## Freeze rule

After the smoke test passes, do not modify firmware source or build flags for
the NLOS campaign. Any required source change creates a new freeze revision,
new firmware hashes, and a new smoke test. Diagnostic builds must use a
different branch and output label and must never be mixed with final-evaluation
logs.

## Smoke result and immutable artifacts

Pending. This section will record the clean source commit, four firmware
SHA-256 values, probe-role mapping, raw-log hashes, and verifier results after
the M256/S3/G200 smoke run.
