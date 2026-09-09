# 차량 N2 뒷범퍼 · N3 대시보드 · RX 수납함 위 · 비컨 M512 1회

**노드별 PER <1% 목표: FAIL. RX 1,000SF 완료: 예.**

사용자는 N2를 뒷범퍼, N3를 대시보드, RX를 운전석과 조수석 사이 수납함 위로 옮겼다고 알렸다. N2의 새 부착 방향은 지정되지 않았다. N4 운전석, N5 조수석, N6/N7 트렁크와 시동 켜짐·유전원 허브·문 닫힘은 직전 확인 상태를 이어 기록했다. 최초 SSH 연결은 MacBook Air Wi-Fi 미연결로 실패했으며 사용자 수정 후 재확인하고 첫 RF를 시작했다.

SSH 및 실제 serial 7개를 확인했다. 직전 512 측정과 정확히 같은 7개 HEX를 재사용했다. 비컨 M512/PAC8, DATA M32/PAC8, lead 26µs, TX 6대·13슬롯(2345672345673), G250, SB/SP 3000/2500µs, 주기 10ms, 목표 1,000SF, 슬롯별 delayed-RX, 재전송 없음. 빌드나 펌웨어 코드 변경 없음.

## 노드별 결과

| 노드 | 비컨 | TX 시도 / 완료 | RX / 예정 | 예정 전송 기준 PER | 직전 PER |
|---|---:|---:|---:|---:|---:|
| N2 뒷범퍼 | 5 | 10 / 10 | 0 / 2,000 | 100.00% | 100.00% |
| N3 대시보드 | 1,000 | 3,000 / 3,000 | 2,958 / 3,000 | 1.40% | 100.00% |
| N4 운전석 | 1,000 | 2,000 / 2,000 | 1,918 / 2,000 | 4.10% | 0.05% |
| N5 조수석 | 1,000 | 2,000 / 2,000 | 1,762 / 2,000 | 11.90% | 1.40% |
| N6 트렁크 A | 1,000 | 2,000 / 2,000 | 1,901 / 2,000 | 4.95% | 10.35% |
| N7 트렁크 B | 1,000 | 2,000 / 2,000 | 1,857 / 2,000 | 7.15% | 24.35% |

예정 전송 기준 손실에는 비컨 미수신으로 송신하지 않은 슬롯도 포함된다. 송신 0회인 노드의 송신 후 PHY PER은 정의하지 않는다. TX success는 송신 완료이며 수신 보장이 아니다. 조기 종료된 TX의 비컨 누계를 정상 완주 비컨 PER로 해석하지 않는다.

전체 RX 10,396/13,000, 예정 전송 기준 손실률 20.0308%. 실제 TX 완료 11,010회.

## 수집 및 오류

- N2: metadata=FAIL, END 통계=True, END 비컨=False, delayed-TX late=0; SYNC loss: 3 timeouts  RX errors=25  beacon_config_errors=0  data_config_errors=0
- N3: metadata=PASS, END 통계=True, END 비컨=True, delayed-TX late=0; SYNC loss: 0 timeouts  RX errors=0  beacon_config_errors=0  data_config_errors=0
- N4: metadata=PASS, END 통계=True, END 비컨=True, delayed-TX late=0; SYNC loss: 0 timeouts  RX errors=1  beacon_config_errors=0  data_config_errors=0
- N5: metadata=PASS, END 통계=True, END 비컨=True, delayed-TX late=0; SYNC loss: 0 timeouts  RX errors=0  beacon_config_errors=0  data_config_errors=0
- N6: metadata=PASS, END 통계=True, END 비컨=True, delayed-TX late=0; SYNC loss: 0 timeouts  RX errors=0  beacon_config_errors=0  data_config_errors=0
- N7: metadata=PASS, END 통계=True, END 비컨=True, delayed-TX late=0; SYNC loss: 0 timeouts  RX errors=0  beacon_config_errors=0  data_config_errors=0

```text
RX timeouts=1994 (fwto=1994 pto=0)  RX errors=610 (sfdto=571 phe=23 fce=0 fsl=16 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0
TDMA validation: wrong-length=0 wrong-slot=0 wrong-superframe=0 data-config-errors=0 rx-schedule-late=0 sync-delayed-late=0 rearm-deadline-miss=0 rx-buffer-overrun=0 deferred-overflow=0 rx-timeout=1994 rx-error=610
EXP4_DOUBLE_BUFFER_CSV,mode=manual,release=rdb_w1c_plus_cmd_db_toggle,rx_good_events=10396,rdb_good_events=10396,rdb_dispatches=10396,rdb_host_mismatch=0,rdb_incomplete=0,rdb_incomplete_recovered=0,rdb_global_ciadone=0,rdb_resync=0,free_count=10396,free_min_us=38,free_max_us=38,free_avg_x1000_us=38000,overrun=0
EXP4_TIMING_CSV,period_count=1000,min_x1000_us=9999993,max_x1000_us=10000250,avg_x1000_us=9999997,elapsed_us=9999998,sync_delayed_late=0,tx_wait_timeout=0,end_tx=3
EXP4_DEFERRED_CSV,batches=1000,pending=0,queue_overflow=0,rearm_deadline_miss=0,rx_timeout=1994,rx_error=610,status=PASS
EXP4_SPI_CSV,mode=persistent_data_burst,begin=1000,end=1000,active=0,begin_fail=0,end_fail=0,device_id_fail=0,state_error=0,transfer_error=0,direct_xfers=242491,direct_timeout=0,recovery=0,status=PASS
EXP4_STATUS_CSV,schedule=PASS,timing=PASS,collection=PASS,link=LOSS
EXP4_SUMMARY_CSV,32,6,13,1,26,16,347,250,13,1000,13000,10396,200308,133068,166400,9999998,0,0,PASS
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

정확히 1회 RF 실행. 종료 후 7대 플래시 readback PASS 및 정지를 확인했다. 로컬·원격 잔여 캡처 프로세스 0, 기존 Git 브랜치·HEAD·dirty 상태 변경 없음. commit/push 없음. N2·N3·RX 위치를 함께 바꾼 1회 관측이므로 각 위치의 효과를 따로 확정하지 않는다.

원시 자료: `capture1/logs/`(RX), `remote_capture_files/`(TX), `recovery_*.json`(정지·플래시·RAM), `preflight_*.json`/`postflight_*.json`, `manifest.json`, `image_reuse_proof.json`, `deployment.json`, `capture1/results/`.
