# Exp4 retained-PGF fast-switch guard boundary (2026-09-02)

## Decision

The minimum guard established by the fail-closed experiment remains **200 us**.
G=100 us is rejected: only 3/10 runs passed, with six RDB host mismatches and
four RX buffer overruns across the ten runs. G=75 and G=50 were not run after
the G=100 boundary failed, as required by the rollback rule.

The recommended measured configuration is therefore M32, PAC8, S3, lead=15 us,
retained-PGF fast PHY switch, persistent SPIM, polling, G=200 us,
`BRRS_SYNC_BUFFER_US=1703`, and `BRRS_EXP4_SYNC_PREP_US=2002`. Feature defaults
remain unchanged and no remote push was performed.

## RDB ordering safety decision

The requested aggressive order would issue `CMD_DB_TOGGLE` before reading the
completed buffer. It was **not implemented**:

- DW3000 User Manual section 4.4.2 says that the host issues `CMD_DB_TOGGLE`
  after it has finished accessing the register set. Its Figure 17 reads the RX
  buffer and associated registers before clearing/toggling the processed buffer.
- Software API Guide sections 5.3.21-5.3.22 say to call
  `dwt_signal_rx_buff_free()` once the frame data has been read; the function
  means that the host has finished with the current buffer.
- Qorvo's `ex_02e_rx_dbl_buff` example permits re-enabling RX before all frame
  data is read, but does not permit declaring that unread buffer free.

The current optimized Exp4 implementation already uses the safe useful part of
arm-before-read:

1. detect the event and validate the selected RDB buffer is ready;
2. issue the next RX fast command;
3. read metadata and the eight-byte protocol header from the completed buffer;
4. clear its status and issue `CMD_DB_TOGGLE` only after those values are cached.

Consequently, no `BRRS_OPT_RX_ARM_BEFORE_READ` flag was added. A flag whose ON
path only duplicated the existing safe order would be misleading, while the
more aggressive interpretation lacks vendor support.

## Hardware protocol

- M32, 26-byte PSDU, 16-byte application payload, PAC8, S3.
- Retained-PGF fast switch and persistent SPIM enabled; FINT polling retained.
- Selected Task 4 wait budgets: 1703 us first-slot buffer and 2002 us SYNC prep.
- 1000 superframes and 3000 scheduled DATA packets per run.
- Three fixed sensor probes and one fixed coordinator probe; every run reflashed
  and reset all four boards.
- G=200 reference: the already completed Task 4 boundary series, 10 runs.
- G=100 boundary: 10 new runs. Any mismatch, incomplete RDB state, overrun,
  deadline miss, SPI fault, or timeout makes the run fail independent of PER.

## Results

| Guard | Runs passed | Received | PER (Wilson 95%) | Mismatch | Overrun | Deadline miss | SPI fault | Decision |
|---:|---:|---:|---:|---:|---:|---:|---:|:---:|
| 200 us | 10/10 | 29798/30000 | 0.673% (0.587-0.772%) | 0 | 0 | 0 | 0 | PASS |
| 100 us | 3/10 | 29778/30000 | 0.740% (0.649-0.843%) | 6 | 4 | 0 | 0 | **FAIL** |
| 75 us | 0 | not run | - | - | - | - | - | stopped after G100 |
| 50 us | 0 | not run | - | - | - | - | - | stopped after G100 |

All ten G=100 link PER values were below the 5% ceiling, and its pooled interval
overlaps the G=200 interval. Link quality is therefore not the reason for the
rejection. The reason is the non-zero system-fault counters.

The critical-prefix metric reported a nominal `required_guard_us` of 87-88 us
and had no deadline misses. That metric ends when the next RX command is issued.
The full event-to-buffer-free path was 202 us maximum and 201 us at p99, while a
G=100 M32 slot is 197 us. The repeated mismatch/overrun failures show that the
critical-prefix number alone is insufficient when the next reception overlaps
the previous buffer's readout and error recovery. The 200 us guard is not a
clock-drift requirement; it is the smallest tested end-to-end safe service
budget for this polling/SPI/RDB implementation.

Raw G=100 logs are in:

`../logs/exp4_task5_guard100_0m_g100_l15_pac8_sb1703_sp2002_20260902_spiopt_phyfast/`

The companion CSV contains one row per run. G=200 pooled values come from the
Task 4 p20 ten-run boundary reported in
`EXP4_WAIT_BUDGET_FAST_SWITCH_RESULTS_20260902.md`.
