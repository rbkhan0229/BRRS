# Exp4 wait-budget implementation and hardware results (2026-09-02)

## Git and scope

- Starting branch/SHA: `exp4-irq-spi-opt` / `eae019efc6cb8dfb4bf630f19f43c40c7356a869`.
- Experiment branch: `exp4-wait-budget-sweep`.
- Local checkpoints: `d206559` (instrumentation), `32a72ff` (defer first-beacon logs), and `922fb8f` (RTT reconnect recovery).
- `main`, `origin/main`, and `origin/exp4-irq-spi-opt` were not changed. No push was performed.
- BRRS beacon-referenced delayed-TX/RX and radio-off intervals were preserved; continuous RX was not introduced.

## What the two values mean

`BRRS_SYNC_BUFFER_US` is not a blocking delay. It is the SYNC RMARKER to first DATA RMARKER scheduling offset used by the coordinator's first delayed-RX and the first slot owner's delayed-TX.

`BRRS_EXP4_SYNC_PREP_US` reserves the end of the 10 ms superframe. The coordinator detects `10000 - sync_prep_us`, closes the DATA burst, switches to the SYNC PHY, prepares the beacon, and arms the next exact delayed-TX. `EXP4_TX_WAIT_TIMEOUT_US=3000` is a separate timeout and was not changed.

## Implementation and measurement coverage

- Both existing compile-time macros are exposed as `--sync-buffer` and `--sync-prep` in build/capture/multi-board tooling. Binary paths and metadata include both values and the resulting DATA budget.
- Coordinator RAM instrumentation covers first delayed-RX arm/slack, DATA-PHY configuration, delayed-RX command time, CONFIG_SWITCH detect lateness, burst close, end-to-end next-SYNC arm, and remaining lead.
- Sensor RAM instrumentation covers SYNC frame read/decode readiness, DATA-PHY configuration, TX-buffer write, first delayed-TX arm, and first-slot arm slack. Aggregates are emitted only after the run.
- The first-beacon RTT configuration logs were moved after the first delayed-TX arm. This reduced the observed one-time sensor maximum from about 2.12 ms to about 1.60 ms without changing radio scheduling.
- The verifier remains fail-closed for zero valid RX, delayed-late, deadline miss, RDB mismatch/incomplete, overrun, SPI error, timeout, wrong slot/superframe, config error, and incomplete instrumentation.

A representative 2000/2000 us S3 run measured:

- worst-side first-slot arm mean/max/p95/p99: 1575.113/1603/1597/1599 us; minimum arm slack 339 us;
- scheduled CONFIG_SWITCH to next-SYNC arm mean/max/p95/p99: 1581.684/1610/1602/1605 us; minimum remaining lead 388 us;
- event-to-buffer-free hot path mean/max/p95/p99: 190.773/201/200/200 us; required guard 88 us.
- sensor DATA PHY configuration aggregate max: 991 us (61.9% of the worst sensor first-slot arm time);
- coordinator DATA PHY configuration max: 1028 us (64.1% of the coordinator first-slot arm time).

## Selected hardware runs

All rows come from completed coordinator RTT logs. PASS additionally requires completed PASS logs from every configured sensor.

| buffer/prep (us) | G | PAC | slots | result | runs | RX/expected | PER (%) | first slack min (us) | SYNC slack min (us) | required G (us) |
|---:|---:|---:|---:|:---:|---:|---:|---:|---:|---:|---:|
| 1600/2500 | 200 | 8 | 3 | FAIL | 1 | 2951/3000 | 1.633 | 0 | 883 | 88 |
| 1750/2500 | 200 | 8 | 3 | PASS | 1 | 2969/3000 | 1.033 | 88 | 879 | 88 |
| 2000/2000 | 75 | 8 | 3 | FAIL | 1 | 1937/3000 | 35.433 | 356 | 405 | 88 |
| 2000/2000 | 100 | 8 | 3 | PASS | 1 | 2962/3000 | 1.267 | 352 | 392 | 88 |
| 2000/2000 | 150 | 8 | 3 | PASS | 1 | 2974/3000 | 0.867 | 343 | 392 | 88 |
| 2000/2000 | 200 | 8 | 3 | PASS | 3 | 8922/9000 | 0.867 | 339 | 388 | 88 |
| 2000/2000 | 200 | 8 | 19 | PASS | 1 | 18883/19000 | 0.616 | 345 | 331 | 87 |
| 2000/2000 | 200 | 8 | 20 | FAIL | 1 | 9955/20000 | 50.225 | 363 | 0 | 87 |
| 2000/2500 | 200 | 8 | 3 | PASS | 1 | 2957/3000 | 1.433 | 348 | 883 | 88 |
| 2500/2500 | 200 | 8 | 3 | PASS | 1 | 2967/3000 | 1.100 | 839 | 883 | 88 |
| 3000/1750 | 200 | 8 | 3 | FAIL | 1 | 1493/3000 | 50.233 | 1365 | 138 | 88 |
| 3000/2000 | 200 | 8 | 3 | PASS | 1 | 2966/3000 | 1.133 | 1343 | 388 | 88 |
| 3000/2500 | 200 | 8 | 3 | PASS | 1 | 2964/3000 | 1.200 | 1338 | 876 | 88 |

## Failure boundaries

- 1600/2500 us, G200, 3 slots: coordinator first delayed-RX arm late (PER 1.633%).
- 2000/2000 us, G75, 3 slots: RX re-arm deadline miss (PER 35.433%).
- 2000/2000 us, G200, 20 slots: next-SYNC delayed-TX lead insufficient (PER 50.225%).
- 3000/1750 us, G200, 3 slots: next-SYNC delayed-TX lead insufficient (PER 50.233%).

## Data-backed recommendations

- Sync buffer: 1750 us passed one smoke run, while 1600 us failed; use 2000 us to retain roughly 300 us or more worst-case arm margin.
- Sync prep: 2000 us passed and 1750 us failed because next-SYNC delayed-TX lead was insufficient; use 2000 us.
- Guard: G100 passed one smoke run and G75 failed; retain G150 until G100 has repeated worst-case validation.
- Capacity at 2000/2000 us and M32/G200: 19 slots passed, 20 slots failed. The measured stable capacity is therefore 19 slots.

The default 3000/2500 us DATA budget is 4500 us (15 calculated slots). The recommended 2000/2000 us budget is 6000 us and achieved 19 stable slots in hardware, a 26.7% measured slot-capacity increase.

## Validation limits and remaining work

- The recommended 2000/2000 us S3 combination completed three valid repeated runs (8922/9000, PER 0.867%). Boundary and saturation points are currently smoke runs, not the planned 10-run campaign.
- PAC4 validation is INVALID, not FAIL: probe `1050211584` reported 0.0 V target voltage and could not be flashed after three attempts. Repeat PAC4 after restoring that board's target power.
- Results are from the present local 0 m bench. Cross-laptop and board-role portability still require a separate repeated campaign with the same firmware hashes.
- G100 passed one run but has only 12 us over the measured 88 us requirement. G150 remains the conservative guard recommendation until G100 is repeated under worst-case load and board rotation.

The companion CSV contains one row per selected run, including source log, git SHA, percentile metrics, all fault counters, and the explicit PASS/FAIL reason. It now also includes the five sensor and four coordinator wait-phase aggregates. `EXP4_WAIT_PHASE_RESULTS_20260902` provides the complete preserved-log inventory.
