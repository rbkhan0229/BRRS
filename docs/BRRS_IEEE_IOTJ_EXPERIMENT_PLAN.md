# BRRS IEEE Internet of Things Journal Submission Experiment Plan

Updated: 2026-08-10

## 1. Submission Position

Target venue: IEEE Internet of Things Journal (IoT-J).

The journal explicitly covers IoT communication/networking protocols, sensor
networks, embedded software, and IoT trials/testbeds. The paper should therefore
be framed as a networked sensing protocol and testbed contribution, not as a
DW3000 register-characterization report.

Primary application: short sensor reports from distributed vehicle sensors to
an in-cabin coordinator. Ranging is outside this paper.

Claim boundary:

1. A standard 256-symbol beacon distributes `M`, PSDU length, data rate, active
   node bitmap, and TDMA timing.
2. Beacon RMARKER timestamps drive delayed-TX and delayed-RX.
3. Short DATA preambles reduce measured frame airtime and increase the number of
   reliable sensor slots and aggregate application goodput.
4. The DWM3000 prototype retains SFD and PHR. Ideal BRRS performance is therefore
   reconstructed using independently measured SFD/PHR overhead.
5. The paper must not claim that preamble detection, SFD, or PHR was physically
   disabled on the DWM3000.

## 2. Research Questions and Success Criteria

| ID | Research question | Primary success criterion |
|---|---|---|
| RQ1 | What is the minimum reliable DATA preamble under beacon-scheduled delayed-RX? | Upper bound of the one-sided 95% PER confidence interval is at most 1% |
| RQ2 | Does CIR quality increase with preamble length as predicted by processing gain? | Estimated gain per preamble doubling is compatible with 3.01 dB, with environment effects reported |
| RQ3 | How much airtime is occupied by SFD and PHR? | Differential EXTTXE measurements agree with the timing model within 0.5 us |
| RQ4 | How much node capacity and aggregate goodput does short-preamble TDMA provide? | Higher maximum reliable active-node count and goodput than the 256-symbol baseline at the same superframe period |
| RQ5 | Does the result survive controlled NLOS and the vehicle use case? | Direction and practical conclusion remain consistent; environment-specific limits are reported |

A reliable Experiment 4 configuration must satisfy all of the following:

- aggregate and per-node PER confidence-bound criterion;
- `wrong-slot = 0`;
- `wrong-superframe = 0`;
- RX and TX `delayed_late = 0`;
- beacon configuration errors = 0;
- complete logs and expected run count.

## 3. Protocol and Firmware Freeze

The submission firmware uses BRRS beacon protocol version 3 and firmware v2.6.

Beacon payload values are transmitted directly rather than through a private
profile table:

- DATA preamble symbols `M`;
- DATA PSDU bytes;
- DATA rate;
- active-node bitmap for N2 through N8;
- first DATA RMARKER offset from beacon RMARKER;
- slot interval;
- superframe period;
- superframe sequence number.
- DATA slot count and an explicit slot-owner sequence.

DATA frames echo only the superframe sequence. They do not repeat their slot
offset. The coordinator derives the physical slot from the DATA RX RMARKER and
checks its source against the slot-owner sequence. This avoids trusting a
sender-declared schedule value and reduces the DATA header from 12 to 8 bytes.

The active bitmap is operational. An inactive node remains silent, and active
nodes are packed in ascending node-ID order. For example, bitmap `0x0D` produces
the order N2, N4, N5 without reserving an empty N3 slot.

For engineering tests with limited hardware, the slot-owner sequence may reuse
the same physical nodes, for example N2/N3/N2/N3/N2/N3/N2/N3. Such a run measures
eight scheduled transmissions and coordinator slot-processing capacity, not
eight independent-node capacity. Claims about independent node count require
the corresponding number of independently clocked radios.

Before collecting submission data:

1. Complete a two-node smoke test for every build configuration.
2. Complete an active-bitmap test using at least one non-contiguous set.
3. Verify that mismatched `M`, PSDU length, and data rate produce
   `BRRS_BEACON_REJECT` and no DATA transmission.
4. Record the firmware revision, compiler version, build configuration, and
   source commit in every run manifest.
5. Freeze the firmware. Any later firmware change invalidates affected runs.

## 4. Common Controlled Setup

Hold these factors constant unless they are the independent variable:

- DWM3000 hardware revision and antenna type;
- channel 9, preamble code 9, PRF 64 MHz, PAC8, STS off;
- 6.81 Mbps DATA rate;
- TX power index and regulatory configuration;
- antenna polarization, height, and orientation;
- coordinator and sensor mounting fixtures;
- beacon preamble: 256 symbols;
- application payload: 16 bytes;
- on-air DATA PSDU: 26 bytes (8-byte protocol header + 16-byte application
  payload + 2-byte FCS);
- no retransmission and no ACK;
- one DATA report per active node per superframe;
- documented room layout and photographs;
- measured distance using a tape or laser meter;
- people and movable obstructions kept outside the test area;
- temperature and test date recorded.

Use at least three board pairs, or rotate board roles and model board pair as a
random effect. Repeat key conditions on at least two different days.

Randomize the order of preamble settings within each block so that temperature,
battery, and time-of-day drift do not always favor one setting.

## 5. Stage 0: Receiver Timing Calibration and Ablation

Purpose: select the lead margin before the main experiments and demonstrate why
lead margin matters.

Primary configuration: DATA preamble 32, PAC8, 1 m LOS, fixed mounts.

Lead sweep:

`0, 2, 4, 6, 8, 10, 12, 14, 15, 16, 20 us`

Tail ablation:

1. lead 0, tail 0;
2. selected lead, tail 0;
3. lead 0, tail 100;
4. selected lead, tail 100.

For each condition, collect five independent runs of 2,000 frames. Repeat the
critical transition region in one reproducible NLOS condition.

Outputs:

- PER and 95% confidence interval;
- FWTO, PTO, SFDTO, PHE, FCE, and FSL composition;
- successful-frame `accumCount` histogram;
- RX-open-to-RMARKER distribution;
- selected fixed lead margin and written selection rule.

Do not select the final lead solely from the lowest observed point estimate.
Select the smallest value whose confidence bound satisfies the reliability
criterion in both LOS and controlled NLOS, while avoiding an unstable PAC-boundary
condition.

## 6. Experiment 1: Minimum Reliable Preamble

Independent variables:

- `M = 32, 64, 128, 256` symbols;
- distance = 0.5, 1, 2, 3, 5 m;
- channel condition = controlled LOS and controlled NLOS.

NLOS must use a fixed, measured obstruction such as a metal plate or closed
partition placed at a documented position. Do not use a changing object such as
laundry or a person.

For each condition:

- five independent runs;
- 2,000 scheduled DATA attempts per run;
- fixed 26-byte DATA PSDU;
- power-cycle or reset both radios between runs;
- randomized `M` order;
- no repositioning within a replicate block.

Report:

- run-level and pooled PER;
- Wilson or exact binomial 95% confidence intervals;
- error-class composition;
- beacon loss and delayed scheduling failures separately from DATA PER;
- minimum reliable `M` by environment and distance.

Primary result: a reliability-versus-airtime tradeoff curve, not a universal
claim that 32 symbols always works.

## 7. Experiment 2: CIR and First-Path SNR

Use the same mounts and conditions as Experiment 1 for a reduced matrix:

- `M = 32, 64, 128, 256`;
- distance = 1 and 3 m;
- LOS and controlled NLOS;
- three independent runs per condition;
- 1,000 transmission attempts per run.
- the same 26-byte DATA PSDU used in Experiment 1.

Store CIR only when the DW3000 reports a valid frame/CIR, but retain the total
attempt and failure counters. Never replace missing CIR rows with a summary row.

The analysis must explicitly state that CIR metrics are conditional on successful
reception. This survivor-selection effect is mitigated by presenting CIR results
together with PER for every condition.

Primary metric:

`FP_SNR_dB = 10 log10(fp_snr_ratio_x1000 / 1000)`

Do not use the known-invalid `RSSI = -128 dBm` value as the primary result.

Statistical analysis:

- median, interquartile range, and bootstrap 95% interval per run;
- mixed-effects or blocked regression against `10 log10(M)`;
- compare relative gain per doubling with 3.01 dB;
- include distance/environment interaction;
- report valid-CIR sample count next to every estimate.

## 8. Experiment 3: SFD and PHR Airtime

Core variants:

- A: SFD8 + standard-rate PHR;
- B: SFD16 + standard-rate PHR;
- C: SFD8 + data-rate PHR.

For each variant:

- three independent resets;
- 1,000 EXTTXE captures per run;
- fixed 26-byte DATA PSDU for the primary A/B/C comparison;
- `captures = successful TX attempts` required;
- report mean, standard deviation, min/max, and 95% interval.

Differential estimates:

- `B - A`: additional eight SFD symbols;
- `A - C`: standard-rate versus data-rate PHR cost;
- ideal BRRS subslot: measured standard airtime minus measured SFD and estimated
  standard PHR.

Recommended strengthening experiment:

- optionally sweep PSDU lengths around the 330-data-bit Reed-Solomon boundaries,
  including 26, 41/42, 82/83, 123/124, and 127 bytes;
- verify that the timing model includes RS parity steps;
- validate one representative waveform with an external logic analyzer or
  oscilloscope if available.

## 9. Experiment 4: Multi-Node Capacity and Goodput

Hardware requirement: one coordinator plus up to seven independently powered
sensor nodes. Two laptops are sufficient for flashing/logging; every sensor does
not need a dedicated laptop after flashing.

### 9.1 Beacon-to-first-slot calibration

The current 3,000 us synchronization buffer is conservative and dominates short
superframes. Sweep `BRRS_SYNC_BUFFER_US` using fixed mounts:

`3000, 1500, 1000, 750, 500 us`

Choose the smallest value with zero delayed scheduling errors and stable link
results across ten 1,000-superframe runs. Freeze this value before capacity tests.

### 9.2 Baselines

Use identical beacon, payload, period, TX power, and node placement:

1. standard-compatible TDMA with DATA `M = 256`;
2. proposed short-preamble TDMA with DATA `M = 32`;
3. optional robustness point `M = 64`;
4. analytically reconstructed ideal BRRS after subtracting Experiment 3 SFD/PHR.

### 9.3 Capacity sweep

Select a superframe period at which 32- and 256-symbol configurations have
different feasible node capacities. A candidate after buffer calibration is
approximately 3 ms; the exact value must be computed from the measured frame and
rearm times.

For each `M`, activate N = 1 through 7 nodes using the bitmap, up to the largest
schedule that fits. Collect ten independent runs of 1,000 superframes per
condition.

Define measured capacity as the largest active-node count satisfying every
reliability and schedule criterion in Section 2. Do not define capacity only by
the compile-time slot formula.

Report:

- per-node PER with confidence intervals;
- all-nodes-success probability per superframe;
- aggregate application goodput and offered load;
- beacon airtime and total superframe utilization;
- wrong-slot, wrong-superframe, delayed-late, and rearm slack;
- measured versus predicted capacity;
- 32-versus-256 capacity and goodput gain.

Run a non-contiguous bitmap test, such as N2/N4/N5, to demonstrate that the
beacon controls participation and compact slot ordering.

## 10. Vehicle Case Study

Use the target application geometry rather than arbitrary desk positions.

Suggested coordinator location: fixed in-cabin central position.

Suggested sensor positions:

- front bumper left/right;
- rear bumper or trunk;
- wheel-arch or door position if relevant to the target sensor set.

For each position, document vehicle model, doors/windows state, node height,
antenna orientation, and obstruction path. Measure `M = 32, 64, 256` with five
independent runs of 2,000 frames.

Primary goal: verify whether the laboratory conclusion about the useful minimum
`M` and capacity direction transfers to the vehicle. Report vehicle-specific
limits rather than presenting one vehicle as universal.

If the full channel-characterization claim from the 0330 draft is retained,
collect a longer CIR using a 1024-symbol preamble and implement RMS delay spread,
Rician K-factor, and path-loss analysis. The current 64-sample first-path window
alone is insufficient for a strong full-channel characterization claim.

## 11. Primary Network Metrics and Optional Energy Measurement

The primary vehicle-network benefit is channel-time efficiency rather than
coordinator energy. Report these as the main system outcomes:

- maximum reliable sensor reports per superframe;
- maximum sensor count at a fixed reporting rate;
- maximum reporting rate at a fixed sensor count;
- aggregate application goodput and channel utilization;
- worst-case delivery latency and all-slots-success probability;
- coexistence implications from reduced UWB airtime.

Do not present coordinator RX energy as a primary contribution when the
coordinator is powered by the vehicle electrical system. Energy measurement is
optional and is justified only if peripheral sensors are battery powered,
energy harvesting, active while the vehicle is parked, or placed in a genuinely
power-constrained wiring-replacement scenario.

If that sensor-power model is retained, measure RX energy using a Nordic Power
Profiler Kit II or a calibrated shunt/oscilloscope setup.

Compare:

- immediate or continuous RX baseline;
- beacon-scheduled delayed-RX;
- `M = 32` and `M = 256`;
- N = 1 and the maximum reliable N.

Report RX-on time, average current, energy per delivered application bit, and
energy per superframe. Treat this as a secondary result rather than a requirement
for the network paper.

## 12. Statistical Rules

The independent run, not each frame, is the primary replication unit.

- Report every run, including failed runs.
- Use run-level bootstrap confidence intervals or a mixed-effects model.
- Use frame-level exact/Wilson intervals as supporting estimates.
- For zero observed errors, report a confidence upper bound instead of `PER = 0`
  as proof of perfect reliability.
- Predefine outlier and exclusion rules before final collection.
- Correct multiple pairwise comparisons, for example with Holm correction.
- Report effect sizes and confidence intervals in addition to p-values.

## 13. Data and Reproducibility

Recommended directory convention:

```text
results/submission/<date>/<experiment>/<environment>/<condition>/run_<nn>/
  init.log
  normal_N2.log
  normal_N3.log
  manifest.yaml
  photo.jpg
```

Each manifest should contain:

- source commit/hash and firmware revision;
- SES configuration name and compiler version;
- board serial/role mapping;
- all PHY, beacon, slot, lead/tail, and TX-power values;
- environment, distance, obstruction, date, and operator;
- expected attempts and completion markers.

Analysis scripts must fail when completion markers or expected records are
missing. Keep raw logs immutable and generate CSV/figures into a separate derived
directory.

IEEE encourages detailed methods and public code/data for reproducibility. Share
analysis code, configuration manifests, raw/processed logs, and the modified BRRS
source through a versioned repository and archive release. If the Qorvo SDK
license prevents redistribution of the full SDK, publish the modified files or
patches plus instructions for obtaining the vendor SDK.

## 14. Paper Figure and Table Set

Core figures:

1. Vehicle sensor-network scenario and BRRS superframe timeline.
2. Beacon payload and delayed-RX/delayed-TX operation.
3. Lead/tail ablation with error-class breakdown.
4. PER versus `M`, faceted by distance and environment.
5. FP-SNR versus `M` with theoretical relative-gain line.
6. Measured SFD/PHR differential airtime.
7. Aggregate goodput and maximum reliable N for M32 versus M256.
8. Vehicle-case reliability and CIR result.
9. Maximum reporting rate or sensor count at a fixed reliability target.

Core tables:

1. PHY/MAC parameters and fixed controls.
2. Comparison against closest related work.
3. Experiment matrix and sample counts.
4. Measured versus modeled airtime and capacity.
5. Limitations and unsupported ideal-BRRS features.

## 15. Manuscript Structure

1. Introduction and vehicle-sensor motivation.
2. Related work and explicit novelty comparison.
3. System model and overhead problem definition.
4. BRRS design, beacon format, and utilization analysis.
5. DWM3000 implementation and hardware limitations.
6. Experimental methodology and statistical protocol.
7. Results: acquisition, CIR, airtime, network capacity, vehicle case.
8. Discussion, threats to validity, and limitations.
9. Conclusion and future PHY/ranging work.

The IEEE IoT-J author guidelines require the IEEE double-column format and a
150-250 word abstract. Pages beyond the first eight published pages incur the
journal's mandatory over-length charge, so decide early whether to target a tight
eight-page article or budget for a fuller archival paper.

Official references:

- IEEE IoT-J scope and author guidelines:
  https://ieee-iotj.org/guidelines-for-authors/
- IEEE research reproducibility guidance:
  https://journals.ieeeauthorcenter.ieee.org/create-your-ieee-journal-article/research-reproducibility/

## 16. Immediate Execution Order

1. Flash v2.6/protocol-v3 INIT and NORMAL firmware and complete the beacon protocol smoke tests.
2. Run Stage 0 lead/tail calibration and freeze the lead value.
3. Freeze fixtures, environments, scripts, and run manifests.
4. Collect Experiment 1 and Experiment 2 in the same physical blocks.
5. Collect Experiment 3 A/B/C and optional payload sweep.
6. Calibrate the synchronization buffer and superframe period.
7. Acquire/prepare enough boards and run Experiment 4 capacity tests.
8. Run the vehicle case study and optional energy measurement only when the
   sensor power model requires it.
9. Lock raw data, run one-command analysis, and draft the paper from the
   predeclared research questions.
