# BRRS Experiment 4: fixed-superframe TDMA capacity

This procedure requires the visible v2.1 firmware banner and Exp4 diagnostic
revision 16 or newer on the coordinator and sensor nodes.

## Purpose

Experiment 4 evaluates how a shorter DATA preamble changes TDMA slot duration,
aggregate packet error rate (PER), application goodput, and the number of sensor
slots that fit in a fixed 10 ms superframe.

This firmware does not remove SFD or PHR. It validates the dominant currently
implementable BRRS component: DATA preamble reduction under beacon-referenced
delayed-TX and delayed-RX.

## Implemented protocol

```text
10 ms superframe

SYNC beacon (PLEN 256)
    -> 3000 us schedule buffer
    -> N2 DATA slot
    -> N3 DATA slot
    -> ...
    -> N8 DATA slot
    -> 2500 us reserved SYNC preparation interval
```

- Coordinator: sends one PLEN-256 SYNC beacon and opens the first DATA slot
  with delayed-RX. Adjacent DATA slots use the DW3000 manual double RX buffer:
  the completed event is cleared, RX is immediately re-armed into the other
  buffer, and then the host reads and releases the completed buffer.
  It closes RX immediately after the last-slot event, or at the scheduled end
  of the last slot when that frame is absent. The independent 7500-us deadline
  is retained only to prepare the next fixed-period SYNC.
- The first SYNC is immediate; later SYNC beacons use DW3000 delayed-TX at the
  previous scheduled RMARKER plus exactly 10,000 us. MCU work and RTT output do
  not accumulate in the superframe period.
- Sensor: receives the beacon and sends one DATA frame in its assigned slot with
  delayed-TX.
- Every SYNC carries a 16-bit `superframe_seq` (1 through 1000). DATA echoes the
  sequence so the coordinator can reject stale or wrong-superframe frames.
- DATA preamble: 32, 64, 128, or 256 symbols.
- Application payload: 16 bytes.
- Protocol: version 3. The DATA header contains source, destination, message
  type, protocol version, and superframe sequence. Slot identity is inferred
  from the received RMARKER and the beacon schedule; no slot offset is repeated
  in DATA.
- PSDU: 8-byte protocol header + 16-byte payload + 2-byte FCS = 26 bytes.
- Beacon PSDU: 39 bytes, including the packed 32-entry slot-owner table and FCS.
- Default inter-frame guard: 100 us. This includes the coordinator's immediate
  RX re-arm service and receiver turn-on margin; it is a firmware implementation
  overhead and must be reported with the result.
- Retransmission and ACK: disabled.
- Run length: 1000 superframes, or 10 seconds at 100 Hz.
- After superframe 1000, the coordinator sends `EXP4_END` three times. Sensors
  terminate on this marker instead of treating the end of the run as SYNC loss.
- Supported physical sensor IDs: N2 through N8, at most seven sensors.

The coordinator prints `EXP4_CONFIG_CSV` at startup and the following final
records on RTT channels 0 and 1:

- `EXP4_SUMMARY_CSV`: aggregate PER, goodput, offered load, slot capacity.
- `EXP4_NODE_CSV`: per-node expected, received, missed, and RX error counts.
- `EXP4_TIMING_CSV`: measured SYNC period, total elapsed time, delayed-SYNC
  scheduling failures, and END transmission count.
- `EXP4_SYNC_PREP_CSV`: measured DATA-PHY to SYNC-PHY transition plus
  delayed-TX arm time. A 1000-superframe run has 999 delayed SYNC preparations;
  `max_us` must remain below `budget_us` and `delayed_late` must be zero.
- `EXP4_DOUBLE_BUFFER_CONFIG_CSV`: boot-time verification that double buffering
  is enabled in manual mode. The run does not start if this check fails.
- `EXP4_REARM_CSV`: coordinator critical-path service time inside the
  double-buffered DATA-slot burst. A good frame re-arms RX before clearing and
  parsing the completed buffer; an error or timeout clears the required status
  first and then re-arms RX. An S1 final-slot event is also provisionally
  re-armed and then closed after validation. The
  `delayed_schedule_late` field applies to delayed scheduling of the first slot.
  `required_guard_us` adds one extra status-poll transaction (the event can
  arrive just after the preceding poll sampled the register) and the configured
  RX startup allowance to the measured worst-case detecting-read-to-command
  service time.
- `EXP4_REARM_PHASE_CSV`: SPI service time split into the detecting status read,
  critical RX fast command, error-path pre-rearm status clear, good-path
  post-rearm status clear, post-rearm RX timestamp, and 8-byte DATA header read
  operations. Exp4 disables the hardware frame-wait timeout during the burst;
  the last-slot event or scheduled last-slot end closes RX. This lets a later
  slot remain receivable when an earlier sensor frame is absent.
  Post-rearm operations are not part of the `EXP4_REARM_CSV` value.
- `EXP4_DOUBLE_BUFFER_CSV`: good-frame events dispatched from `FINT_STAT` and
  verified in the current host buffer's `RDB_STATUS`, returned-buffer count,
  buffer release service time, host-pointer mismatches, and RX-buffer overruns.
  `rdb_dispatches`, `rdb_good_events`, and `free_count` must equal
  `rx_good_events`; `rdb_host_mismatch` and `overrun` must be zero.
- `EXP4_BURST_CSV`: number of superframes closed by a valid received DATA frame
  in the final slot, by the scheduled last-slot deadline, or forcibly at SYNC
  preparation. RX errors do not trigger an early close. `forced_prep_close`
  must be zero.
- `BRRS_SLOT_TIMING_CSV`: signed DATA arrival error in ns, calculated from
  DW3000 RMARKER timestamps. Its sample count must equal the accepted RX count.
- `EXP4_STATUS_CSV`: schedule, UWB timing-sample integrity, collection validity,
  and zero-loss link status as separate fields.
- `EXP4_DONE`: collection completeness marker.

`status=PASS` means the 1000-superframe schedule and explicit END collection
completed without delayed scheduling, wrong-slot, or wrong-sequence failures.
It does not mean PER is zero; check `link=PASS/LOSS` separately because PER is
an experimental result. Sensor output also
separates `schedule=PASS/FAIL` from `beacon=PASS/LOSS`.

The coordinator keeps RX active only across the scheduled contiguous DATA-slot burst. Exp4
therefore evaluates TDMA capacity, aggregate throughput, and loss isolation; it
must not be cited as a per-slot delayed-RX energy measurement.

Because the burst frame-wait timeout is disabled, a missing DATA frame is
reported by `expected - rx`, not by `RX timeouts`. Error-only frames have no
decodable DATA header, so their per-node attribution uses the nearest scheduled
slot at the error-event read time; aggregate error and PER counts remain the
authoritative result.

With the current 26-byte PSDU and 100 us guard, the compiled timing model is:

| DATA preamble | Frame airtime | Slot interval | Calculated slots |
|---:|---:|---:|---:|
| 32 sym | 97 us | 197 us | 23 |
| 64 sym | 130 us | 230 us | 19 |
| 128 sym | 195 us | 295 us | 15 |
| 256 sym | 325 us | 425 us | 11 |

Slot capacity is calculated in the same SYNC-RMARKER clock domain used by the
beacon's slot offsets. The capacity calculation reserves the final slot's guard
before the 7500-us SYNC preparation deadline. The coordinator reports the
measured DATA-to-SYNC configuration and delayed-TX arm service time in
`EXP4_SYNC_PREP_CSV`; a valid run must keep it inside the 2500-us budget and
must report `sync_delayed_late=0`.

The preamble component itself falls by about 8x from 256 to 32 symbols, while
the complete slot and calculated node capacity improve by smaller factors because
SFD, PHR, PSDU, guard, beacon buffer, and SYNC preparation time remain.

## First smoke test in SES

Normal-node firmware now reads DATA preamble length `m` from every valid beacon.
Build it once per physical node ID; the same N2/N3 image follows a 32, 64, 128,
or 256-symbol INIT without reflashing.

For the current two-board setup, use the one-sensor (`S1`) configurations:

| DATA preamble | Coordinator | Sensor N2 |
|---:|---|---|
| 32 | `Exp4_32_S1_Init` | `Exp4_N2` |
| 64 | `Exp4_64_S1_Init` | `Exp4_N2` |
| 256 | `Exp4_256_S1_Init` | `Exp4_N2` |

To validate back-to-back slot re-arming with three boards, use the two-sensor
(`S2`) configurations:

| DATA preamble | Coordinator | Sensor N2 | Sensor N3 |
|---:|---|---|---|
| 32 | `Exp4_32_S2_Init` | `Exp4_N2` | `Exp4_N3` |
| 64 | `Exp4_64_S2_Init` | `Exp4_N2` | `Exp4_N3` |
| 256 | `Exp4_256_S2_Init` | `Exp4_N2` | `Exp4_N3` |

1. For S1, Build and Debug N2. For S2, Build and Debug both N2 and N3.
2. Let every sensor board remain in SYNC wait.
3. Build and Debug INIT last. It waits 10 seconds and starts 1000 superframes.
4. Check that each sensor prints `EXP4_TX_DONE ... end=1 schedule=PASS`.
5. Check that INIT prints `EXP4_DONE ... status=PASS` and one `EXP4_NODE_CSV`
   row per configured sensor.
6. Check `EXP4_TIMING_CSV`: `period_count=1000`, average approximately
   10,000 us, `sync_delayed_late=0`, and `end_tx=3`.
7. Check `EXP4_SYNC_PREP_CSV`: `count=999`, `max_us<budget_us`, and
   `delayed_late=0`.
8. Check `BRRS_SLOT_TIMING_CSV`: `samples` equals `rx` and `status=PASS`.
9. Check `EXP4_DOUBLE_BUFFER_CSV`:
   `rdb_dispatches=rdb_good_events=free_count=rx_good_events`,
   `rdb_host_mismatch=0`, and `overrun=0`.
10. Check `EXP4_REARM_CSV`: `required_guard_us` (measured service maximum plus
    RX startup allowance) must not exceed `guard_us` from `EXP4_CONFIG_CSV`.
11. Check each sensor for `BRRS_DATA_PHY_APPLIED_CSV`: `m` must match the
   coordinator configuration.

The one-sensor guard-time test does not validate back-to-back slot processing.
During the first N2+N3 smoke test, check `EXP4_REARM_CSV`, the seven
`EXP4_REARM_PHASE_CSV` rows, and N3 PER. If the maximum re-arm service time
exceeds the guard or N3 is consistently absent, increase `BRRS_SLOT_GUARD_US`
and repeat; the coordinator did not re-enable RX before the next preamble.
After a stable baseline is obtained, sweep the guard downward separately to
find the minimum multi-slot value.

Then repeat S2 with N2 powered off and N3 still running. N2 should show nearly
all misses while N3 continues to be received in its RMARKER-derived slot, with
`wrong-slot=0`. This dropout-isolation run verifies that one absent early slot
does not shift or suppress the later-slot collection state.

## Build any N2-N8 combination

Build all images for one preamble and sensor count. The optional third argument
is guard time in microseconds and defaults to 100:

```bash
chmod +x Drivers/API/brrs_exp4_build.sh
Drivers/API/brrs_exp4_build.sh 32 7 100
```

The default 100-us example creates one INIT image and N2-N8 images in:

```text
Drivers/API/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4/plen32_sensors7/
```

On Linux, set `EMBUILD` if `emBuild` is not on `PATH`:

```bash
EMBUILD="/full/path/to/emBuild" Drivers/API/brrs_exp4_build.sh 32 7
```

The generated images remain run-specific for reproducibility. In SES, the
universal `Exp4_N2` through `Exp4_N8` configurations can instead be reused;
their DATA preamble and owned slots come from the beacon. A non-default INIT
guard is stored in a separate directory such as `plen32_sensors2_guard50`.

To flash one generated image and capture RTT channel 1 in one command:

```bash
chmod +x Drivers/API/brrs_exp4_flash_and_log.sh

# Run each sensor first, on the laptop connected to that board.
Drivers/API/brrs_exp4_flash_and_log.sh 32 7 N2 \
  ~/Desktop/DWM3000/result4/exp4_32_s7_N2.log 100

# Run INIT last. Its 10-second startup grace gives the sensors time to wait.
Drivers/API/brrs_exp4_flash_and_log.sh 32 7 init \
  ~/Desktop/DWM3000/result4/exp4_32_s7_init.log 100
```

For a guard sweep, rebuild and flash every board with the same value:

```bash
Drivers/API/brrs_exp4_build.sh 32 2 50
Drivers/API/brrs_exp4_flash_and_log.sh 32 2 N2 /path/to/N2.log 50
Drivers/API/brrs_exp4_flash_and_log.sh 32 2 N3 /path/to/N3.log 50
Drivers/API/brrs_exp4_flash_and_log.sh 32 2 init /path/to/init.log 50
```

For multiple sensors, the practical requirement is:

```text
guard >= maximum coordinator immediate-RX re-arm time + receiver safety margin
```

Here the re-arm budget includes worst-case status-poll detection latency, the
detecting status read, the RX fast command, and the configured RX startup
allowance. Use `EXP4_REARM_CSV` and PER to reduce guard from 100 us. An S1
result cannot validate this because it does not contain a back-to-back RX
re-arm requirement, even though the firmware still measures the provisional
final-slot re-arm.

## Sparse active-node bitmap validation

The `Exp4_32_S3_B05_Init` coordinator configuration advertises the sparse
bitmap `0x05`, so N2 and N4 are active while N3 is omitted. Use `Exp4_N2` and
`Exp4_N4` on the sensor boards. The first schedule
line must be:

```text
EXP4_SLOT_SCHEDULE_CSV,active_bitmap=0x05,slot_count=2,slot_owners=24,repeats=1
```

This proves that inactive IDs do not leave empty TDMA slots: N2 owns the first
DATA slot and N4 immediately owns the second. A complete simultaneous test
requires the coordinator plus two sensor boards.

## Log and plot

Save the coordinator RTT channel 1 output for every run. After collecting the
INIT logs, create a sorted CSV and three plots:

```bash
python3 Drivers/API/brrs_exp4_log_to_csv_plot.py \
  /path/to/exp4_32_s3_init.log \
  /path/to/exp4_64_s3_init.log \
  /path/to/exp4_256_s3_init.log \
  -o /path/to/exp4_result \
  --prefix exp4_controlled
```

The script rejects a log whose final `EXP4_SUMMARY_CSV` status is `FAIL`, whose
firmware revision is older than 16, whose manual-double-buffer, fixed-period,
SYNC-preparation, re-arm, or burst diagnostics are missing or invalid, whose
average period differs from 10 ms by more than 5 us, or whose END sequence is
incomplete. Logs with different guard values must be analyzed in separate
invocations so they are not mistaken for replicates of the same condition.
Generated files are:

- `exp4_controlled_summary.csv`
- `exp4_controlled_aggregate.csv` (replicate mean and standard deviation)
- `exp4_controlled_goodput.png`
- `exp4_controlled_per.png`
- `exp4_controlled_capacity.png`

## Interpretation boundary

`max_slots` is calculated from the implemented 10 ms timing budget and slot
duration. It is a capacity estimate, not proof that the coordinator successfully
received that many independent physical nodes. The measured evidence is limited
to the number of boards used in each run. A stronger maximum-capacity claim needs
either enough physical nodes or a separately disclosed virtual-load experiment.
