# BRRS Exp4 final-evaluation freeze

Date: 2026-09-02  
Branch: `exp4-final-eval-20260902`  
Source base: `104651e`  
Frozen firmware source commit: `479e1ea4b41274428778eb70c11d468d95b9d7a3`

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

Status: **PASS**

The frozen images were built from a clean worktree at commit `479e1ea` and
tested on four boards for 1000 superframes using M256/S3/G200. The coordinator
received 3000/3000 DATA frames (PER 0.000%), and every sensor transmitted
1000/1000 frames with zero beacon loss. The measured required guard was 87 us;
the frozen evaluation guard remains 200 us.

| Check | Result |
|---|---|
| Coordinator verifier | PASS: 1000 superframes, 3000/3000 RX, PER 0.000% |
| N2 verifier | PASS: 1000/1000 TX, beacon loss 0/1000 |
| N3 verifier | PASS: 1000/1000 TX, beacon loss 0/1000 |
| N4 verifier | PASS: 1000/1000 TX, beacon loss 0/1000 |
| Schedule and timing | PASS; required guard 87 us |
| RX hot path | min 172 us, max/p99 200 us, 3000 samples |
| Deadline/config/RX errors | all zero |
| RDB mismatch/incomplete/overrun | all zero |
| SPI failure/timeout/recovery | all zero |
| IRQ and diagnostic profile rows | absent, as required |

### Firmware images and board assignment

Image directory:

```text
Drivers/API/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4/
  plen256_sensors3_sb1703_sp2002_guard200_spiopt_phyfast/
```

| Role | J-Link serial | Firmware SHA-256 |
|---|---:|---|
| INIT | 1050270933 | `74addac966a28b2e56c8834ff740f053ccd17d2f7843a88499037977fd986844` |
| N2 | 1050211584 | `4d15aad34901e9ab97f4398ab1a2fa1271a48ae54b6e9fbbed49ed196c3f109e` |
| N3 | 1050273888 | `4943d808fa142a1c2917abcfc1b9b062ded6146935af400e37f75b9623c9aae8` |
| N4 | 1050282818 | `7b5da519681ceea4be4b92c461832470992757b6bb4b5cf632407193e4314987` |

### Raw evidence

Log directory:

```text
../logs/exp4_final_eval_smoke_0m_g200_l15_pac8_sb1703_sp2002_20260902_spiopt_phyfast/
```

| Role | Raw log | Raw-log SHA-256 |
|---|---|---|
| INIT | `exp4_256_s3_r1_init.log` | `31acb294f406fabb761e6922bd95558354b8926e47204fc38eee03a90256ae14` |
| N2 | `exp4_256_s3_r1_n2.log` | `0a33a0c55dd69048c6f95daad3ad9ad6dd20de188eefb6ae2630eaf80e64bd8d` |
| N3 | `exp4_256_s3_r1_n3.log` | `4b3ea167b27a9abee17a1f36da5ced0dc6ee722ec90f4c3cd74bbcbd6dfd96f4` |
| N4 | `exp4_256_s3_r1_n4.log` | `9346e21176da36bdb19d899dfb6403e79d1a5cef629145401e2540691040619e` |

`EXP4_FINAL_EVAL_MANIFEST_20260902.csv` contains the same role-to-probe,
firmware, log, hash, and verifier mapping in machine-readable form. A later
documentation-only commit that records this result does not change the frozen
firmware source commit or any image hash above.
