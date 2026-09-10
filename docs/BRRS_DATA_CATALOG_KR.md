# BRRS 실험 데이터 카탈로그

이 문서는 `/Users/songchieon/Desktop/DWM3000` 아래에 흩어진 원시 로그, 선별 결과, 분석 자료를 **이동하지 않고도** 찾을 수 있게 하는 기준표다. 현재 실행기가 `DWM3000/logs` 경로를 사용하고 과거 문서에도 절대 경로가 남아 있으므로, 차량 실험 전에는 기존 결과 폴더를 물리적으로 재배치하지 않는다.

## 데이터의 세 단계

| 단계 | 기본 위치 | 의미 |
|---|---|---|
| 원본 | `DWM3000/logs/<campaign-or-run>/` | RTT 원문, build log, manifest, readback, 실행 상태의 기준 |
| 선별본 | `DWM3000/paper_results_<date>*/` | 특정 분석에 사용한 원본 복사와 결과물. 포함·제외 기준은 각 README가 결정 |
| 해석본 | `DWM3000/draft/`, `reports/`, `experiment_analysis_*` | 논문·회의용 표, 그림, 서술. 원본의 대체물이 아님 |

`DW3_QM33_SDK_1.0.2/experiments/legacy/logs`에는 Git 보존을 위해 복사한 작은 보고서와 후처리 코드만 있다. 대용량 원시 로그의 공식 위치는 여전히 `DWM3000/logs`다.

## 대표 데이터 세트

| 날짜 | 환경·목적 | 선별 결과 위치 | 펌웨어 식별 수준 | 사용 시 주의 |
|---|---|---|---|---|
| 2026-08-12 | 철문 NLOS 6.9m 초기 실험 | `paper_results_20260812/` | README에 정확한 commit·HEX hash 없음 | legacy로 취급. 현재 펌웨어 결과와 직접 합산 금지 |
| 2026-08-19 | 연구실/home 단일머신 기준 | `paper_results_20260819/` | 버전 계열만 남고 정확한 commit·HEX hash 없음 | 선택 run은 README 기준 |
| 2026-08-22 | 연구실 단일머신 재현성 run 1 | `paper_results_20260822_run1_singlemachine/` | 정확한 commit·HEX hash 미기록 | 일부 로그는 `.prev` 복원본임 |
| 2026-08-22 | 연구실 크로스머신 재현성 run 2 | `paper_results_20260822_run2_crossmachine/` | run 1과 같은 코드라는 실험 기록은 있으나 정확한 commit·HEX hash 미기록 | 일부 TX 로그 덮어쓰기·복원 이력 확인 |
| 2026-08-23 | NLOS 6.9m 크로스머신 | `paper_results_20260823_nlos_6.9m/` | 8월 22일 run 2와 같은 코드라는 기록만 존재 | 기존 NLOS 기준 데이터. 새 최종 펌웨어 데이터와 별도 표기 |
| 2026-09-02 | Exp4 SPI·PHY·wait·guard 최적화 | `logs/exp4_*_20260902*`, `Drivers/API/EXP4_*_RESULTS_20260902.md` | `exp4-final-eval-20260902` 태그와 개별 결과 문서·로그 사용 | 계산 용량과 실측 통과를 구분 |
| 2026-09-07~08 | 6TX NLOS 준비·lead·차량 기능 점검 | `logs/nlos69_prevehicle_summary_20260907_08/` | 실행별 manifest와 HEX hash 있음 | 준비·진단 결과이며 최종 논문 반복 실험이 아님 |
| 2026-09-08 | 현대 코나 차량 7회 | `logs/vehicle_day_summary_20260908/` | 역할별 HEX SHA-256, serial, 배치가 summary와 manifest에 있음 | 6링크 동시 PER<1%를 달성한 세트는 없음 |
| 2026-09-10 이후 | 최종 차량 실험 후보 | `vehicle-experiments`의 clean commit에서 생성될 새 `logs/` bundle | commit+dirty 상태+HEX hash를 모두 요구 | 최종 논문용 여부는 반복·회전 완료 후 판정 |

과거 자료의 정확한 commit을 폴더 날짜나 파일명으로 추측하지 않는다. source commit이 없고 HEX hash만 있으면 **binary identified / source commit unresolved**, 둘 다 없으면 **firmware identity incomplete**로 기록한다.

## 실행 폴더에서 펌웨어를 찾는 순서

1. `provenance/source_git.json`이 있으면 `commit`, `branch`, `dirty`를 확인한다.
2. `case.json` 또는 최상위 `manifest.json`에서 실험 조건과 물리 역할을 확인한다.
3. 실행별 `board_manifest.json` 또는 payload index에서 역할별 serial과 HEX SHA-256를 확인한다.
4. `status.json`, readback 기록, 원시 `.log`의 완료 마커가 서로 일치하는지 확인한다.
5. `RESULTS.md`나 `SUMMARY.md`에서 유효성·PER 판정을 읽되 원시 증거보다 우선하지 않는다.

`TEST_ONLY`, build-only, prepare-only, collection 실패, 유효 RX 0인 실행은 RF 성능 데이터로 승격하지 않는다. 실패 로그는 삭제하지 않고 결과 상태만 명확히 붙인다.

## 새 데이터의 표준 구조

새 campaign은 기존 실행기가 만드는 `DWM3000/logs/<campaign>_<date>/` 아래에 둔다.

```text
logs/<campaign>_<date>/
├── manifest.json                 # 환경, 조건, 보드 역할, 반복 계획
├── RESULTS.md 또는 SUMMARY.md    # 사람용 판정과 해석 한계
├── <case-id>/
│   ├── case.json                 # 정확한 case 조건
│   ├── board_manifest.json       # serial, 역할, 펌웨어 hash
│   ├── provenance/
│   │   └── source_git.json       # commit, branch, dirty 상태
│   ├── *.log                     # 원시 RTT 및 build 로그
│   └── status.json               # 실행·수집·readback 상태
└── analysis/                     # 원본에서 재생성 가능한 표·그림
```

실제 도구에 따라 파일명이 조금 다를 수 있지만 위 정보는 빠지면 안 된다. 여러 실행을 하나의 선별 결과로 복사할 때는 원본 상대 경로와 SHA-256를 기록한다.

## 현재 펌웨어 기준

- 저장소: <https://github.com/rbkhan0229/BRRS>
- 기존 기준선: `main` at `90cdffb`
- 현재 차량 실험 후보의 펌웨어 기준: `vehicle-experiments` at `4ee929a` (이후 문서 전용 commit은 펌웨어 동작을 바꾸지 않음)
- 과거 독립 구현: Git tags의 `snapshot/*`
- 기각된 IRQ 구현: `rejected/exp4-irq-fastpath-20260831`

새 펌웨어 코드 변경 후 위 commit 값은 자동으로 낡아진다. 실험 폴더의 provenance가 최종 기준이며 이 문서의 commit은 탐색용 펌웨어 기준값이다.

## 기계 판독 인덱스

- `docs/reproducibility/experiment_bundles.json`: 기존 bundle별 조건·역할·HEX/ELF hash
- `docs/reproducibility/local_artifacts.json`: 통합 시점 로컬 자료의 경로·크기·SHA-256
- `docs/reproducibility/snapshot_refs.json`: 제거한 SDK 사본과 Git 복원 지점 대응
- `docs/reproducibility/source_inventory.json`: 과거 소스 파일별 SHA-256

검증 명령은 보드나 RF를 사용하지 않는다.

```bash
cd /Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2
python3 tools/verify_experiment_archive.py
```
