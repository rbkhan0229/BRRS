# PAC4/27µs — 전선 보고 후 요청한 재측정1회

**N7 PER6.8%, 나머지5대0%로 목표 실패다.** 같은 조건의 직전 N7 PER1.00%보다5.8%p 높았다. 송신은13000/13000 성공했다.

M32/PAC4, lead27µs, G250, SB/SP3000/2500µs,6TX/13슬롯,1000SF, 슬롯별 delayed-RX. 같은 block2 배정·같은7개 HEX를 사용했다.

측정 시각(KST): 2026-09-08T00:18:53+09:00–2026-09-08T00:19:41+09:00. 전체 RX 12864/13000, PER 1.0462%.

| 물리 역할 / serial | 논리 역할 | offered | TX attempt/success | RX | PER | beacon/누락 | TX late/END |
|---|---|---:|---:|---:|---:|---:|---:|
| N3/1050273888 | N2 | 2000 | 2000/2000 | 2000 | 0.00% | 1000/0 | 0/1 |
| N4/1050282818 | N3 | 3000 | 3000/3000 | 3000 | 0.00% | 1000/0 | 0/1 |
| N5/1050208509 | N4 | 2000 | 2000/2000 | 2000 | 0.00% | 1000/0 | 0/1 |
| N6/1050227627 | N5 | 2000 | 2000/2000 | 2000 | 0.00% | 1000/0 | 0/1 |
| N7/1050204212 | N6 | 2000 | 2000/2000 | 1864 | 6.80% | 1000/0 | 0/1 |
| N2/1050211584 | N7 | 2000 | 2000/2000 | 2000 | 0.00% | 1000/0 | 0/1 |

```text
TDMA validation: wrong-length=0 wrong-slot=0 wrong-superframe=0 data-config-errors=0 rx-schedule-late=0 sync-delayed-late=0 rearm-deadline-miss=0 rx-buffer-overrun=0 deferred-overflow=0 rx-timeout=130 rx-error=6
EXP4_DOUBLE_BUFFER_CSV,mode=manual,release=rdb_w1c_plus_cmd_db_toggle,rx_good_events=12864,rdb_good_events=12864,rdb_dispatches=12864,rdb_host_mismatch=0,rdb_incomplete=0,rdb_incomplete_recovered=0,rdb_global_ciadone=0,rdb_resync=0,free_count=12864,free_min_us=38,free_max_us=38,free_avg_x1000_us=38000,overrun=0
EXP4_DEFERRED_CSV,batches=1000,pending=0,queue_overflow=0,rearm_deadline_miss=0,rx_timeout=130,rx_error=6,status=PASS
EXP4_SPI_CSV,mode=persistent_data_burst,begin=1000,end=1000,active=0,begin_fail=0,end_fail=0,device_id_fail=0,state_error=0,transfer_error=0,direct_xfers=241953,direct_timeout=0,recovery=0,status=PASS
RX timeouts=130 (fwto=130 pto=0)  RX errors=6 (sfdto=0 phe=1 fce=0 fsl=5 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0
```

수집 timeout 없음. RX/TX 종료·metadata·이미지 해시 및7개 보드 flash readback 통과. SSH·허브 외부 전원 조건 유지. 원래 역할의 PAC8/25µs HEX로 복원하고 모두 halt했다. 복원 후 RF는 실행하지 않았다.

전선이 가렸던 노드와 시작 시점을 사용자가 특정하지 못했다. 이전 결과를 임의로 제외하지 않고 현재 결과와 별도 보존한다. 같은 설정에서1.00→6.8%로 달라졌으므로 PAC4/27µs를 안정적인 최적값으로 확정할 수 없다. 전선 제거 효과, 시간 변화, 다른 채널 변화를 이1회로 분리할 수 없다. 이전의 PAC8/26·27µs 모두0% 결과도 현재 전선 상태의 재검증으로 취급하지 않는다.

- [동일7개 HEX·serial·논리 역할 대조](exact_image_and_assignment_comparison.json)
- [전후 개별 결과](comparison.json)
- [원문과 수집/flash 상태](capture1/results/)
- [전체 탐색·보드 복원·Git 확인](../nlos69_lead_screen_rotation_20260907_2326/RESULTS.md)
