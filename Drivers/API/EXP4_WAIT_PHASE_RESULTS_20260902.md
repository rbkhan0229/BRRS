# Exp4 preserved wait-phase log inventory (2026-09-02)

## Coverage

- Sensor logs with phase records: 54.
- Coordinator logs with equivalent phase records: 20.
- Five sensor phases complete in every sensor log: True.
- Four coordinator phases complete in every coordinator log: True.
- Long-form CSV rows: 350.

## Sensor phase aggregate

| phase | count | min (us) | max (us) | weighted avg (us) |
|---|---:|---:|---:|---:|
| sync_event_detect_to_frame_read | 52506 | 126 | 126 | 126.000 |
| sync_event_detect_to_beacon_ready | 52506 | 153 | 685 | 156.827 |
| sensor_data_phy_config | 52506 | 990 | 991 | 990.943 |
| sensor_tx_buffer_write | 52506 | 73 | 73 | 73.000 |
| sensor_delayed_tx_arm_call | 52506 | 174 | 174 | 174.000 |

## Coordinator phase aggregate

| phase | count | min (us) | max (us) | weighted avg (us) |
|---|---:|---:|---:|---:|
| coordinator_data_phy_config | 20000 | 1019 | 1028 | 1026.285 |
| coordinator_delayed_rx_arm_call | 20000 | 154 | 221 | 154.237 |
| sync_prep_deadline_detect_lateness | 19981 | 138 | 580 | 182.417 |
| sync_prep_burst_close | 19981 | 45 | 46 | 45.800 |

## PHY configuration share of first-slot preparation

- Sensor DATA PHY configuration: max 991 us; per-log share 46.7% to 62.7%.
- Coordinator DATA PHY configuration: max 1028 us; per-log share 60.4% to 65.2%.
- Coordinator equivalent phase instrumentation is present; it was not missing from the preserved INIT logs.

## Sensor log-by-log DATA PHY phase

| experiment | run | node | config max (us) | first arm max (us) | share (%) | result |
|---|---|---:|---:|---:|---:|:---:|
| wait_budget_baseline | r1 | N2 | 990 | 2068 | 47.9 | PASS |
| wait_budget_baseline | r1 | N3 | 990 | 2063 | 48.0 | PASS |
| wait_budget_baseline | r1 | N4 | 990 | 2030 | 48.8 | PASS |
| wait_budget_baseline_fix | r2 | N2 | 991 | 2040 | 48.6 | PASS |
| wait_budget_baseline_fix | r2 | N3 | 991 | 2111 | 46.9 | PASS |
| wait_budget_baseline_fix | r2 | N4 | 991 | 2081 | 47.6 | PASS |
| wait_budget_baseline_fix | r3 | N2 | 991 | 2091 | 47.4 | PASS |
| wait_budget_baseline_fix | r3 | N3 | 991 | 2049 | 48.4 | PASS |
| wait_budget_baseline_fix | r3 | N4 | 991 | 2087 | 47.5 | PASS |
| wait_buffer_sweep | r1 | N2 | 991 | 1581 | 62.7 | PASS |
| wait_buffer_sweep | r1 | N3 | 991 | 1599 | 62.0 | PASS |
| wait_buffer_sweep | r1 | N4 | 991 | 1595 | 62.1 | PASS |
| wait_buffer_sweep | r1 | N2 | 991 | 1580 | 62.7 | PASS |
| wait_buffer_sweep | r1 | N3 | 991 | 1599 | 62.0 | PASS |
| wait_buffer_sweep | r1 | N4 | 991 | 1594 | 62.2 | PASS |
| wait_buffer_sweep | r1 | N2 | 991 | 1581 | 62.7 | PASS |
| wait_buffer_sweep | r1 | N3 | 991 | 1600 | 61.9 | PASS |
| wait_buffer_sweep | r1 | N4 | 991 | 1594 | 62.2 | PASS |
| wait_buffer_sweep | r1 | N2 | 991 | 2074 | 47.8 | PASS |
| wait_buffer_sweep | r1 | N3 | 991 | 2120 | 46.7 | PASS |
| wait_buffer_sweep | r1 | N4 | 991 | 2074 | 47.8 | PASS |
| wait_buffer_sweep | r3 | N2 | 991 | 1594 | 62.2 | PASS |
| wait_buffer_sweep | r3 | N3 | 991 | 1580 | 62.7 | PASS |
| wait_buffer_sweep | r3 | N4 | 991 | 1600 | 61.9 | PASS |
| wait_capacity19 | r1 | N2 | 991 | 1625 | 61.0 | PASS |
| wait_capacity19 | r1 | N3 | 991 | 1641 | 60.4 | PASS |
| wait_capacity19 | r1 | N4 | 991 | 1635 | 60.6 | PASS |
| wait_capacity20 | r1 | N2 | 991 | 1626 | 60.9 | PASS |
| wait_capacity20 | r1 | N3 | 991 | 1644 | 60.3 | PASS |
| wait_capacity20 | r1 | N4 | 991 | 1637 | 60.5 | PASS |
| wait_guard_sweep | r1 | N2 | 991 | 1582 | 62.6 | PASS |
| wait_guard_sweep | r1 | N3 | 991 | 1600 | 61.9 | PASS |
| wait_guard_sweep | r1 | N4 | 991 | 1593 | 62.2 | PASS |
| wait_guard_sweep | r1 | N2 | 991 | 1581 | 62.7 | PASS |
| wait_guard_sweep | r1 | N3 | 991 | 1599 | 62.0 | PASS |
| wait_guard_sweep | r1 | N4 | 991 | 1592 | 62.2 | PASS |
| wait_guard_sweep | r1 | N2 | 991 | 1583 | 62.6 | PASS |
| wait_guard_sweep | r1 | N3 | 991 | 1600 | 61.9 | PASS |
| wait_guard_sweep | r1 | N4 | 991 | 1592 | 62.2 | PASS |
| wait_interaction | r2 | N2 | 991 | 1601 | 61.9 | PASS |
| wait_interaction | r2 | N3 | 991 | 1593 | 62.2 | PASS |
| wait_interaction | r2 | N4 | 991 | 1582 | 62.6 | PASS |
| wait_interaction | r3 | N2 | 991 | 1592 | 62.2 | PASS |
| wait_interaction | r3 | N3 | 991 | 1581 | 62.7 | PASS |
| wait_interaction | r3 | N4 | 991 | 1600 | 61.9 | PASS |
| wait_interaction | r4 | N2 | 991 | 1582 | 62.6 | PASS |
| wait_interaction | r4 | N3 | 991 | 1600 | 61.9 | PASS |
| wait_interaction | r4 | N4 | 991 | 1593 | 62.2 | PASS |
| wait_prep_sweep | r3 | N2 | 991 | 1594 | 62.2 | PASS |
| wait_prep_sweep | r3 | N3 | 991 | 1582 | 62.6 | PASS |
| wait_prep_sweep | r3 | N4 | 991 | 1598 | 62.0 | PASS |
| wait_prep_sweep | r1 | N2 | 991 | 1581 | 62.7 | PASS |
| wait_prep_sweep | r1 | N3 | 991 | 1598 | 62.0 | PASS |
| wait_prep_sweep | r1 | N4 | 991 | 1594 | 62.2 | PASS |

The companion long-form CSV contains count/min/max/average for every available phase in every preserved sensor and coordinator log.
