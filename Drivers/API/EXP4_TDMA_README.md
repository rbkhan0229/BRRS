# BRRS Experiment 4: fixed-superframe TDMA capacity

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
    -> 500 us SYNC preparation interval
```

- Coordinator: sends one PLEN-256 SYNC beacon, then receives every sensor slot
  with delayed-RX.
- The first SYNC is immediate; later SYNC beacons use DW3000 delayed-TX at the
  previous scheduled RMARKER plus exactly 10,000 us. MCU work and RTT output do
  not accumulate in the superframe period.
- Sensor: receives the beacon and sends one DATA frame in its assigned slot with
  delayed-TX.
- Every SYNC carries a 16-bit `superframe_seq` (1 through 1000). DATA echoes the
  sequence so the coordinator can reject stale or wrong-superframe frames.
- DATA preamble: 32, 64, 128, or 256 symbols.
- Application payload: 16 bytes.
- PSDU: 12-byte experiment header + 16-byte payload + 2-byte FCS = 30 bytes.
- Default inter-frame guard: 100 us. This includes coordinator time to read one
  frame and arm the next delayed-RX window; it is a firmware implementation
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
- `EXP4_REARM_CSV`: coordinator processing time and remaining time before the
  next delayed-RX window. It has `count=0` in an S1 run because there is no
  adjacent sensor slot to re-arm.
- `BRRS_SLOT_TIMING_CSV`: signed DATA arrival error in ns, calculated from
  DW3000 RMARKER timestamps. Its sample count must equal the accepted RX count.
- `EXP4_STATUS_CSV`: schedule, UWB timing-sample integrity, and collection
  validity separated from measured link loss.
- `EXP4_DONE`: collection completeness marker.

`status=PASS` means the 1000-superframe schedule and explicit END collection
completed without delayed scheduling, wrong-slot, or wrong-sequence failures.
It does not mean PER is zero; PER is an experimental result. Sensor output also
separates `schedule=PASS/FAIL` from `beacon=PASS/LOSS`.

With the current 30-byte PSDU and 100 us guard, the compiled timing model is:

| DATA preamble | Frame airtime | Slot interval | Calculated slots |
|---:|---:|---:|---:|
| 32 sym | 101 us | 201 us | 31 |
| 64 sym | 134 us | 234 us | 27 |
| 256 sym | 329 us | 429 us | 15 |

The preamble component itself falls by about 8x from 256 to 32 symbols, while
the complete slot and calculated node capacity improve by smaller factors because
SFD, PHR, PSDU, guard, beacon buffer, and SYNC preparation time remain.

## First smoke test in SES

For the current two-board setup, use the one-sensor (`S1`) configurations:

| DATA preamble | Coordinator | Sensor N2 |
|---:|---|---|
| 32 | `Exp4_32_S1_Init` | `Exp4_32_S1_N2` |
| 64 | `Exp4_64_S1_Init` | `Exp4_64_S1_N2` |
| 256 | `Exp4_256_S1_Init` | `Exp4_256_S1_N2` |

To validate back-to-back slot re-arming with three boards, use the two-sensor
(`S2`) configurations:

| DATA preamble | Coordinator | Sensor N2 | Sensor N3 |
|---:|---|---|---|
| 32 | `Exp4_32_S2_Init` | `Exp4_32_S2_N2` | `Exp4_32_S2_N3` |
| 64 | `Exp4_64_S2_Init` | `Exp4_64_S2_N2` | `Exp4_64_S2_N3` |
| 256 | `Exp4_256_S2_Init` | `Exp4_256_S2_N2` | `Exp4_256_S2_N3` |

1. For S1, Build and Debug N2. For S2, Build and Debug both N2 and N3.
2. Let every sensor board remain in SYNC wait.
3. Build and Debug INIT last. It waits 10 seconds and starts 1000 superframes.
4. Check that each sensor prints `EXP4_TX_DONE ... end=1 schedule=PASS`.
5. Check that INIT prints `EXP4_DONE ... status=PASS` and one `EXP4_NODE_CSV`
   row per configured sensor.
6. Check `EXP4_TIMING_CSV`: `period_count=1000`, average approximately
   10,000 us, `sync_delayed_late=0`, and `end_tx=3`.
7. Check `BRRS_SLOT_TIMING_CSV`: `samples` equals `rx` and `status=PASS`.

The one-sensor guard-time test does not validate back-to-back slot processing.
During the first N2+N3 smoke test, check `delayed late`. If it is nonzero, increase
`BRRS_SLOT_GUARD_US`; the coordinator did not have enough time to finish one RX
and arm the next delayed-RX window. After a stable baseline is obtained, sweep
the guard downward separately to find the minimum multi-slot value.

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

All devices in a run must use the same preamble and sensor-count build.
They must also use the same guard build. A non-default guard is stored in a
separate directory such as `plen32_sensors2_guard50`.

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

For multiple sensors, the theoretical command deadline requires
`guard >= lead`. The practical requirement is larger:

```text
guard >= 12 us lead + maximum coordinator RX re-arm time + safety margin
```

Use `EXP4_REARM_CSV` and PER to reduce guard from 100 us. An S1 result cannot
validate this because it does not contain a back-to-back RX re-arm.

## Sparse active-node bitmap validation

The `Exp4_32_S3_B05_Init` coordinator configuration advertises the sparse
bitmap `0x05`, so N2 and N4 are active while N3 is omitted. Use the existing
`Exp4_32_S3_N2` and `Exp4_32_S3_N4` sensor configurations. The first schedule
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
fixed-period diagnostics are missing, whose average period differs from 10 ms
by more than 5 us, or whose END sequence is incomplete. Logs with different
guard values must be analyzed in separate invocations so they are not mistaken
for replicates of the same condition.
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
