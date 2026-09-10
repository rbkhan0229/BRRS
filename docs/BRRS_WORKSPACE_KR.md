# BRRS 개발·실험 보관 기준

2026-09-10부터 펌웨어 개발은 **단일 저장소** `/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2`에서만 한다. 현재 개발선은 `vehicle-experiments`, 기존 기준선은 `main`이다.

| 브랜치 | 현재 commit | 의미 |
|---|---|---|
| `main` | `90cdffb` | 2026-08-26 기준 기존 안정판 |
| `vehicle-experiments` | `4ee929a` (펌웨어 기준) | 이후 최적화·진단·Exp5·차량 실행기·비컨 옵션이 포함된 최종 차량 실험 후보. 뒤의 문서 전용 commit은 동작을 바꾸지 않음 |

`vehicle-experiments`는 `main`을 모두 포함한다. 최종 차량 실험과 논문용 재검증이 끝나기 전에는 `main`으로 병합하지 않는다. GitHub 첫 화면은 `main`이므로 최신 코드를 볼 때 브랜치를 확인한다.

## 현재 작업 원칙

- 기능 수정, 빌드, 플래시는 정식 저장소에서만 수행한다.
- 조건 변화는 CLI 옵션과 manifest로 표현한다. SDK 전체 복사본을 만들지 않는다.
- 일회성 비교가 필요하면 짧은 로컬 commit 또는 임시 Git worktree를 사용하고, 검증 후 commit/tag로 보존한 다음 worktree를 제거한다.
- `Drivers/API/Build_Platforms/nRF52840-DK/Output`은 재생성 가능한 빌드 산출물이며 Git에 넣지 않는다.
- `DWM3000/logs`의 원시 실행 폴더는 덮어쓰거나 이름을 바꾸지 않는다.

실험과 데이터의 위치·펌웨어 연결 방법은 [BRRS 실험 데이터 카탈로그](BRRS_DATA_CATALOG_KR.md)를 따른다.

## 커밋 시점

키 입력마다 commit하지 않는다. 다음 경계에서 작은 논리 단위로 commit한다.

1. 하나의 기능 또는 진단 변경을 완료했을 때
2. 관련 오프라인 테스트와 대표 build-only 검증을 통과했을 때
3. 실험용 설정을 동결하기 직전
4. 유효한 실험 결과를 반영하는 문서·분석을 마쳤을 때

실제 RF 실험은 반드시 `git status --short`가 빈 clean commit에서 시작한다. 실험 도중 임시 수정이 필요하면 먼저 현재 결과를 닫고 새 commit으로 구분한다. 현장 실험 직전과 하루 종료 시점에는 원격 `vehicle-experiments`에 push한다.

## 과거 SDK 사본의 복원

과거 SDK 전체 사본은 소스 SHA-256와 Git tree 일치를 검증한 뒤 2026-09-10에 제거했다. 병합된 개발 이력은 `vehicle-experiments`에 있고, 독립 스냅샷은 태그로 보존한다.

| 제거한 사본 구분 | 복원 기준 |
|---|---|
| 기존 최종 평가 | tag `exp4-final-eval-20260902` |
| PHY A/B 작업 트리 | 위 태그와 같은 commit `f420f95` |
| RX 오류 진단 | tag `snapshot/exp4-rx-error-diag-20260904` |
| 슬롯별 RX A/B | tag `snapshot/exp4-slotrx-ab-20260907` |
| RX lead | tag `snapshot/exp4-rxlead-20260907` |
| 6TX 다중 슬롯 | tag `snapshot/exp4-s6-multislot-20260907` |
| 6TX SFD64 | tag `snapshot/exp4-s6-sfd64-20260907` |
| 차량 실행기 감사 | tag `snapshot/vehicle-suite-audit-20260907` |
| 차량 실행기 수정 | tag `snapshot/vehicle-suite-fix-20260907` |
| 비컨512 및 Exp2/Exp5 링크 순회 원본 | tag `snapshot/vehicle-beacon512-20260908` |
| 기각된 IRQ fast path | tag `rejected/exp4-irq-fastpath-20260831` |

태그 날짜는 사본 식별일이며 실제 편집 순서나 유효한 RF 결과를 뜻하지 않는다. 특정 파일은 현재 작업 트리를 바꾸지 않고 볼 수 있다.

```bash
git show snapshot/exp4-slotrx-ab-20260907:Drivers/API/brrs_exp4_build.sh
```

commit과 제거한 원본 폴더 대응은 [snapshot_refs.json](reproducibility/snapshot_refs.json), 파일별 SHA-256은 [source_inventory.json](reproducibility/source_inventory.json)에 있다.

## 실험 기록과 큰 파일

- 실제 대용량 원본: `/Users/songchieon/Desktop/DWM3000/logs`
- 과거 선별 논문 데이터: `/Users/songchieon/Desktop/DWM3000/paper_results_*`
- Git에 보존된 작은 과거 보고서·후처리 코드: `experiments/legacy`
- 실행 bundle 조건·역할·펌웨어 hash: [experiment_bundles.json](reproducibility/experiment_bundles.json)
- 통합 시점 로컬 자료 해시: [local_artifacts.json](reproducibility/local_artifacts.json)

해시 인덱스는 로컬 파일의 위치와 무결성을 연결하지만 대용량 로그와 HEX가 GitHub에 백업됐다는 뜻은 아니다. 중요한 최종 campaign은 별도 오프라인 백업도 만든다.

새 `prepare`는 source commit·branch·dirty 상태를 `case.json`과 `provenance/source_git.json`에 기록한다. 역할별 HEX/ELF hash, 보드 serial, 조건 manifest, 원시 로그, readback을 같은 bundle에 보존한다. source commit이 없는 과거 결과에 commit을 추측해서 붙이지 않는다.

## 오프라인 검증

아래 명령은 SSH, flash, 보드, RF를 사용하지 않는다.

```bash
python3 -B -m unittest discover -s Drivers/API/tests -p 'test_*.py' -v
python3 tools/verify_experiment_archive.py
```

- [차량 설정·보드 역할](../Drivers/API/BRRS_VEHICLE_MANIFEST_KR.md)
- [Stage0~Exp5 논문 반복·회전](../Drivers/API/BRRS_PAPER_CAMPAIGN_KR.md)
- [실험 case와 실행 방식](../Drivers/API/exp_cases.md)
- [Exp2·Exp5 물리 링크 순회](../Drivers/API/BRRS_CIR_LINKS_KR.md)
