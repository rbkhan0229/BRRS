# NLOS 6.9 m fixed-role symptom reproduction — 2026-09-03

## 결과 요약 — 기준 재현 3회 완료

**N3에 집중된 높은 M32 PER가 오늘도 반복됐다.** 어제와 손실률의 절대값까지 같지는 않다. 오늘 전체 PER는 10.53–11.00%, N3는 30.4–31.9%로, 어제 동일 역할의 전체 15.9–17.3%, N3 46.5–51.4%보다 낮다. 그러나 정상화됐다고 볼 수 없으며, 원인이 전원/무선 환경/펌웨어 중 무엇인지는 아직 분리되지 않았다.

| 반복 (파일 run ID) | 종료 시각 KST | 유효 RX / 예상 | 전체 PER | N2 PER | N3 PER | N4 PER | 판정 |
|---|---|---|---|---|---|---|---|
| 1 (r1) | 21:41:05 | 2684 / 3000 | 10.533% | 0.0% | 30.4% | 1.2% | FAIL_PER |
| 2 (r4) | 21:42:42 | 2678 / 3000 | 10.733% | 0.0% | 30.8% | 1.4% | FAIL_PER |
| 3 (r7) | 21:45:08 | 2670 / 3000 | 11.000% | 0.0% | 31.9% | 1.1% | FAIL_PER |
| 합계 | — | 8032 / 9000 | 10.756% | 0 / 3000 손실 | 931 / 3000 손실 (31.033%) | 37 / 3000 손실 (1.233%) | 3/3 FAIL_PER |

전체 PER의 Wilson 95% 구간: 반복 1 = 9.485–11.683%, 반복 2 = 9.675–11.892%, 반복 3 = 9.930–12.170%. 합산 전체 = 10.132–11.412%, 합산 N3 = 29.403–32.712%. 이는 패킷을 이항 표본으로 취급한 기술적 구간이며, 슈퍼프레임/시간 상관이나 일간 변동을 반영한 신뢰성 보장은 아니다.

### 무결성 확인

- 12개 INIT/TX 원시 로그의 크기와 SHA-256이 각 메타데이터와 일치한다.
- 모든 실행의 보드 역할, 바이너리 SHA-256, 설정, 기록 커밋이 기준과 일치한다.
- 모든 TX: 각 실행마다 비컨 1000/1000 수신, DATA 1000/1000 송신. TX 지연/스케줄 오류와 비컨 누락 없음.
- INIT: deadline miss, delayed RX/TX late, RDB mismatch/incomplete, overrun, SPI 오류/timeout, RX timeout 모두 0.
- PHY fast-switch 레지스터 자기검증은 INIT/TX 모두 통과. 이것만으로 RF 감도 동등성을 증명하는 것은 아니다.
- **무선 RX error는 0이 아님:** 실행별 253 / 259 / 260 이벤트. PER와 별도로 보존하며, 시스템 오류와 구분한다.
- 기존 5% PER 기준을 유지했으므로 세 실행 모두 전체 판정은 FAIL. 펌웨어의 `collection=PASS,link=LOSS`를 링크 성능 PASS로 해석하지 않는다.
- 기존 검증기가 PER 초과에서 조기 종료하므로, [읽기 전용 감사 스크립트](audit_reproduction.py)는 해당 FAIL을 기록한 채 나머지 무결성 검사까지 이어서 확인한다. 판정 기준이나 원본 검증기/로그는 수정하지 않는다. 세 실행 모두 PER 외 후속 검사 통과.
- 모든 TX가 첫 준비 시도에서 READY. TX 작업 로그에 재접속/예외/재시도 경고 없음. INIT 수집 세션에도 재접속/예외 없음.

| 계측 | 반복 1 | 반복 2 | 반복 3 |
|---|---|---|---|
| RX hot path 평균 / 최대 / p99 (us) | 192.298 / 202 / 202 | 192.276 / 202 / 202 | 192.331 / 202 / 202 |
| rearm 기준 required guard (us) | 88 | 88 | 88 |
| first-RX arm 최대 / RX-open 최소 여유 (us) | 1366 / 279 | 1365 / 280 | 1368 / 277 |
| next-SYNC prep E2E 최대 / 최소 잔여 lead (us) | 1392 / 609 | 1401 / 599 | 1396 / 604 |

스케줄/버퍼 처리의 계측상 실패가 보이지 않는다는 뜻이지, PHY 전환이나 wait 축소의 RF 영향까지 배제됐다는 뜻은 아니다. 지금 데이터만으로 “허브가 원인” 또는 “최적화 펌웨어가 원인”이라고 결론내릴 수 없다. 또한 오늘의 재현만으로 8월 기준 대비 펌웨어 회귀를 입증한 것은 아니다.

### 다음 단계 및 종료 상태

1. 기준 데이터 확보 완료. 다음은 동일 배치에서 허브 전원 조건을 비교한다.
2. 가능하면 **같은 허브에 외부 전원만 추가**한다. 허브 자체를 교체해야 하면 허브/USB 토폴로지도 바뀌므로 “전원만의 A/B”로 부르지 않고 구분 기록한다. 가능하면 이후 원래 조건으로 복귀해 시간 변화를 확인한다.
3. 전원 비교 뒤 필요하면 별도 진단 바이너리로 fast switch ON/OFF, wait budget, SPI를 한 변수씩 비교한다. 기존 동결 바이너리/기준 데이터는 유지한다.
4. 원인 분리 전 B/C 포화 및 G150 실험은 시작하지 않는다.

허브 전원/케이블/보드 역할을 바꾸지 않았으며 새 빌드, 펌웨어 소스 수정, git commit/push 없음. 원격 TX 로그를 이 폴더에 복사했고 해시를 검증했다. 로컬/원격 동결 작업트리는 종료 시에도 clean. 캡처 프로세스는 완료됐고 원격 잔류 수집 프로세스 없음. 결과는 현재 로컬 로그 폴더에 보존되어 있으며 GitHub에는 올리지 않았다.

## Purpose and controlled conditions

Reproduce the elevated M32 PER observed on 2026-09-02 before changing hub power, PHY switching, wait budgets, or SPI options. The user confirmed that placement, direction, cables, and hub power state remain unchanged. Physical conditions are user-reported, not independently measured.

- M32 / PAC8 / S3 / G200; lead 15 us.
- SB/SP = 1703/2002 us; 1000 superframes per repeat.
- Optimized polling SPI and retained-PGF fast PHY switch ON.
- IRQ and Task 7/8 profiling OFF. No source edits or rebuilds.
- Three repeats, no board-role rotation.
- Existing script run IDs **1, 4, 7** represent repeats **1, 2, 3**: `(run - 1) % 3 = 0` preserves the same assignment with the unmodified script.
- Before TX setup, reset/halt only the explicitly selected local INIT MCU to avoid old beacons; the normal INIT capture then programs and runs the verified M32 image.
- Existing 5% host PER limit remains unchanged. Link-loss rejection is not conflated with firmware/system-integrity failure. Incomplete or zero-valid-packet captures are not PASS.

## Preflight

- Local and remote record commit: `f420f95e29351e4b737cc76561bffae7191f4376`.
- Firmware source freeze: `479e1ea`; branch/tag: `exp4-final-eval-20260902`.
- Both worktrees clean at preflight; no existing capture processes detected.
- SSH verified using `100.115.225.85` (the short hostname did not resolve).
- Remote immutable worktree: `/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_exp4_final_20260902`.
- One local INIT probe and the three expected remote sensor probes detected.
- Local and remote HEX and ELF hashes match; HEX hashes also match yesterday's capture metadata.

| Role | Host | Probe serial | HEX SHA-256 |
|---|---|---|---|
| INIT | Local | 1050270933 | `8c39923d0ba863126597de6497bc428d6c0bfd8e85f91513f004e2a865120907` |
| N2 | Remote | 1050211584 | `36f27fc6bab9dd7be10b3d2bb83eedc53eed6e03d457f0239ccdc135f95cc3ae` |
| N3 | Remote | 1050273888 | `fa1cd328a43d8cf52db99854f10fda655ccbdb53c0ba30c02d6305490afea560` |
| N4 | Remote | 1050282818 | `8fa8df5912b8e4c606f73d89c37c254a2ebed5721898fe1b242b6539de4750c8` |

## Reference (2026-09-02, same roles)

| Reference run | Total RX / expected | Overall PER | N3 PER |
|---|---|---|---|
| 1 | 2481 / 3000 | 17.3% | 51.4% |
| 4 | 2523 / 3000 | 15.9% | 46.5% |

All three planned attempts completed. The original raw logs, metadata (including FAIL status), TX assignments, and TX worker/orchestrator logs are preserved here. No hub/power A/B or firmware A/B is included in this baseline phase.
