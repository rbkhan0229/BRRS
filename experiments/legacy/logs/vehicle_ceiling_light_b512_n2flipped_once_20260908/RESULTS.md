# 차량 천장 전등 RX · 비컨 M512 · 1회

**노드별 PER <1% 목표: FAIL. RX 1,000SF 완료: 예.**

사용자는 RX를 천장 전등 쪽으로 옮겼다고 알렸다. N2는 뒤집은 상태를 유지한 것으로 기록했다. 나머지 배치·시동 켜짐·유전원 허브·문 닫힘은 직전 확인 상태를 이어 기록했다. N2/N3 앞범퍼 바깥, N4 운전석, N5 조수석, N6/N7 트렁크. 천장 전등 위치에서 이번 한 번을 측정했다.

SSH 및 실제 serial 7개를 확인했다. 직전 512 측정과 정확히 같은 7개 HEX를 재사용했다. 비컨 M512/PAC8, DATA M32/PAC8, lead 26µs, TX 6대·13슬롯(2345672345673), G250, SB/SP 3000/2500µs, 주기 10ms, 목표 1,000SF, 슬롯별 delayed-RX, 재전송 없음. 빌드나 펌웨어 코드 변경 없음.

## 노드별 결과

| 노드 | 비컨 | TX 시도 / 완료 | RX / 예정 | 예정 전송 기준 PER | 직전 PER |
|---|---:|---:|---:|---:|---:|
| N2 앞범퍼 A (뒤집음) | 0 | 0 / 0 | 0 / 2,000 | 100.00% | 100.00% |
| N3 앞범퍼 B | 0 | 0 / 0 | 0 / 3,000 | 100.00% | 100.00% |
| N4 운전석 | 1,000 | 2,000 / 2,000 | 1,999 / 2,000 | 0.05% | 0.70% |
| N5 조수석 | 1,000 | 2,000 / 2,000 | 1,972 / 2,000 | 1.40% | 9.20% |
| N6 트렁크 A | 1,000 | 2,000 / 2,000 | 1,793 / 2,000 | 10.35% | 2.55% |
| N7 트렁크 B | 1,000 | 2,000 / 2,000 | 1,513 / 2,000 | 24.35% | 0.70% |

예정 전송 기준 손실에는 비컨 미수신으로 송신하지 않은 슬롯도 포함된다. 송신 0회인 노드의 송신 후 PHY PER은 정의하지 않는다. TX success는 송신 완료이며 수신 보장이 아니다. 조기 종료된 TX의 비컨 누계를 정상 완주 비컨 PER로 해석하지 않는다.

전체 RX 7,277/13,000, 예정 전송 기준 손실률 44.0231%. 실제 TX 완료 8,000회.

## 수집 및 오류

- N2: metadata=없음, END 통계=False, END 비컨=False, delayed-TX late=0; TX 최종 통계 없음; recovery RAM 사용
- N3: metadata=없음, END 통계=False, END 비컨=False, delayed-TX late=0; TX 최종 통계 없음; recovery RAM 사용
- N4: metadata=PASS, END 통계=True, END 비컨=True, delayed-TX late=0; SYNC loss: 0 timeouts  RX errors=0  beacon_config_errors=0  data_config_errors=0
- N5: metadata=PASS, END 통계=True, END 비컨=True, delayed-TX late=0; SYNC loss: 0 timeouts  RX errors=0  beacon_config_errors=0  data_config_errors=0
- N6: metadata=PASS, END 통계=True, END 비컨=True, delayed-TX late=0; SYNC loss: 0 timeouts  RX errors=0  beacon_config_errors=0  data_config_errors=0
- N7: metadata=PASS, END 통계=True, END 비컨=True, delayed-TX late=0; SYNC loss: 0 timeouts  RX errors=0  beacon_config_errors=0  data_config_errors=0

```text
RX timeouts=5001 (fwto=5001 pto=0)  RX errors=722 (sfdto=698 phe=17 fce=0 fsl=7 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0
TDMA validation: wrong-length=0 wrong-slot=0 wrong-superframe=0 data-config-errors=0 rx-schedule-late=0 sync-delayed-late=0 rearm-deadline-miss=0 rx-buffer-overrun=0 deferred-overflow=0 rx-timeout=5001 rx-error=722
EXP4_DOUBLE_BUFFER_CSV,mode=manual,release=rdb_w1c_plus_cmd_db_toggle,rx_good_events=7277,rdb_good_events=7277,rdb_dispatches=7277,rdb_host_mismatch=0,rdb_incomplete=0,rdb_incomplete_recovered=0,rdb_global_ciadone=0,rdb_resync=0,free_count=7277,free_min_us=38,free_max_us=38,free_avg_x1000_us=38000,overrun=0
EXP4_TIMING_CSV,period_count=1000,min_x1000_us=9999993,max_x1000_us=10000250,avg_x1000_us=9999997,elapsed_us=9999998,sync_delayed_late=0,tx_wait_timeout=0,end_tx=3
EXP4_DEFERRED_CSV,batches=1000,pending=0,queue_overflow=0,rearm_deadline_miss=0,rx_timeout=5001,rx_error=722,status=PASS
EXP4_SPI_CSV,mode=persistent_data_burst,begin=1000,end=1000,active=0,begin_fail=0,end_fail=0,device_id_fail=0,state_error=0,transfer_error=0,direct_xfers=239508,direct_timeout=0,recovery=0,status=PASS
EXP4_STATUS_CSV,schedule=PASS,timing=PASS,collection=PASS,link=LOSS
EXP4_SUMMARY_CSV,32,6,13,1,26,16,347,250,13,1000,13000,7277,440231,93145,166400,9999998,0,0,PASS
```

세부 카운터가 없는 항목은 0으로 가정하지 않는다. N2/N3 beacon RX 오류 누계는 RX DATA 구간 오류와 합산하지 않는다. 수집 실패 판정은 그대로 보존했으며, 다른 보드 수집을 끝까지 기다리는 직전 수집 도구를 유지했다.

## 역할 및 HEX SHA256

| 역할 | Serial | SHA256 |
|---|---|---|
| init | 1050270933 | `d48615c9cd6a3dcce4ab1d0785b644f8c259e3af72ff9786dd3690f0cc8aeb00` |
| N2 | 1050211584 | `dd0fa35df59fa67eec32880da060500d24d5a9a5eca7eb1ef628eb0550094cf6` |
| N3 | 1050273888 | `c4b726f9e20456056709b85522662ecefa347e9d673681f5bb01f997391cd455` |
| N4 | 1050282818 | `998d287b827cec0cdde7a3c4c590f3fca8cb0687e188792894e9910b3b9eec57` |
| N5 | 1050208509 | `892c271d77059275efa26614017f32fd87534f352a425ca9f7db21d4b1a0bcef` |
| N6 | 1050227627 | `97483fbf34ed5e5fdf0b52fe4bbc1cb8656c8614717e571319c8753ef02e32fe` |
| N7 | 1050204212 | `2177124d049d709c5a0e2269d63eefd6e3b9d7ada996d1521c5a0764e623ed90` |

정확히 1회 RF 실행. 종료 후 7대 플래시 readback PASS 및 정지를 확인했다. 로컬·원격 잔여 캡처 프로세스 0, 기존 Git 브랜치·HEAD·dirty 상태 변경 없음. commit/push 없음. RX 위치 변경 1회 관측으로 단일 원인을 확정하지 않는다.

원시 자료: `capture1/logs/`(RX), `remote_capture_files/`(TX), `recovery_*.json`(정지·플래시·RAM), `preflight_*.json`/`postflight_*.json`, `manifest.json`, `image_reuse_proof.json`, `deployment.json`, `capture1/results/`.
