# BRRS 개발·실험 보관 기준

2026-09-09부터 현재 개발 기준은 **이 저장소의 `vehicle-experiments` 브랜치**다. 로컬 작업 폴더는 `/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2`다. 기존 기본 브랜치 `main`과 이전 검증 브랜치는 변경하지 않았다. GitHub에서 최신 코드를 보려면 `vehicle-experiments`를 선택한다.

## 현재 코드

차량용 `vehicle_beacon512_20260908` 사본의 소스·빌드 설정·수집기·분석기·문서를 가져왔다. 원래 사본은 보존한다. 지금부터 기능 수정은 현재 저장소에서 하고, 실험 조건은 manifest와 빌드 옵션으로 기록한다. 다른 구현을 비교할 때는 Git 브랜치를 사용한다. 이번 정리는 통신 성능이나 RF 검증을 추가한 작업이 아니다.

- [차량 설정·보드 역할](../Drivers/API/BRRS_VEHICLE_MANIFEST_KR.md)
- [Stage0~Exp5 논문 반복·회전](../Drivers/API/BRRS_PAPER_CAMPAIGN_KR.md)
- [Exp2·Exp5 물리 링크 순회](../Drivers/API/BRRS_CIR_LINKS_KR.md)

공통 manifest의 PAC별 lead는 아직 미선정 상태다. 조건별 실험 근거와 환경 기록을 갖춘 manifest로 계획을 만든다. 보관 브랜치 이름이나 TEST_ONLY 이미지의 숫자로 실제 설정을 추정하지 않는다.

## 이전 사본의 복원

| 기존 사본 구분 | Git 브랜치 |
|---|---|
| 기존 최종 평가 | `exp4-final-eval-20260902` |
| PHY A/B 작업 트리 | `exp4-nlos-phy-ab-20260903` |
| RX 오류 진단 | `exp4-rx-error-diag-20260904` |
| 슬롯별 RX A/B | `archive/exp4-slotrx-ab-20260907` |
| RX lead | `archive/exp4-rxlead-20260907` |
| 6TX 다중 슬롯 | `archive/exp4-s6-multislot-20260907` |
| 6TX SFD64 | `archive/exp4-s6-sfd64-20260907` |
| 차량 실행기 감사 | `archive/vehicle-suite-audit-20260907` |
| 차량 실행기 수정 | `archive/vehicle-suite-fix-20260907` |
| 비컨512 및 Exp2/Exp5 링크 순회 원본 | `archive/vehicle-beacon512-20260908` |

각 보관 브랜치는 정리 당시 파일 바이트와 실행 권한을 보존한 snapshot이다. 이름의 날짜는 기존 폴더를 식별하며, 그날의 마지막 펌웨어를 뜻하지 않는다. 특히 beacon512 사본에는 9월 9일의 CIR 링크 순회 수정도 들어 있다. snapshot commit은 지금 생성한 보관 이력이며 과거 실제 편집 시각이나 편집 순서를 복원했다고 주장하지 않는다.

commit과 원본 폴더 대응은 [snapshot_refs.json](reproducibility/snapshot_refs.json), 전체 파일별 SHA256은 [source_inventory.json](reproducibility/source_inventory.json)에 있다. 생성물·벤더 SDK·개인 설정은 제외했다. 같은 코드와 문서는 Git 객체로 중복 저장을 줄인다.

```bash
# 현재 작업을 보존한 뒤 필요한 브랜치를 선택한다.
git switch vehicle-experiments

# 특정 과거 파일은 작업 트리를 바꾸지 않고 볼 수 있다.
git show archive/exp4-slotrx-ab-20260907:Drivers/API/brrs_exp4_build.sh

# 보관된 10개 원본의 파일 내용과 권한을 Git 객체에서 검증한다.
python3 tools/verify_experiment_archive.py
```

## 실험 기록과 큰 파일

작업 폴더 바깥 `DWM3000/logs`에 있던 가벼운 보고서·개별 실행/후처리 코드 267개를 [experiments/legacy](../experiments/legacy) 아래에 원문 그대로 보존했다. 이는 과거 실행의 출처이며 최신 사용법이 아니다. 일부 문서·코드는 당시 절대 경로와 실행 조건을 포함하므로 현재 실행에는 위 campaign 도구를 사용한다.

- [실행 bundle 110개 목록](reproducibility/experiment_bundles.json): case 설정, 역할·serial, HEX/ELF hash, 펌웨어 C 출처 hash, payload index hash, 결과 폴더 존재 여부. TEST_ONLY 및 준비만 한 bundle도 포함된다. 목록 개수는 유효 RF 실행 수가 아니다.
- [로컬 자료 12,049개 해시 목록](reproducibility/local_artifacts.json): workspace 상대 경로, 크기, SHA256. 원본 raw·metadata·설정·보고서를 추적한다.
- [보존한 보고서·스크립트 목록](reproducibility/legacy_publication_files.json): 원래 경로와 바이트 hash.

이 목록은 자료의 위치와 무결성을 연결하며, raw·HEX를 GitHub에 백업했다는 뜻은 아니다. 큰 파일은 현재 로컬 보관소에 그대로 있다. 로그 목록은 런타임 SDK 복제, 빌드 산출물, 캐시, 그림 등을 제외하므로 모든 바이트를 포함한 디스크 백업 목록도 아니다. 별도 오프라인 원본이 필요하면 해당 실험 폴더를 따로 보관해야 한다.

```bash
# 현재 보관 경로의 원본 파일까지 다시 확인한다. RF/보드 접근 없음.
python3 tools/verify_experiment_archive.py \
  --workspace /Users/songchieon/Desktop/DWM3000
```

새 `prepare`는 사용한 소스 commit·브랜치·dirty 상태를 `case.json`과 `provenance/source_git.json`에 자동 기록한다. tracked 변경이 있으면 patch도 보존한다. untracked 파일은 상태 목록에 표시하며 commit만으로 현재 작업 트리를 복원할 수 있다고 표시하지 않는다. Git이 없는 옛 사본은 출처 미확인으로 명시한다. 이 정보는 결과 판정에도 전달된다.

이 출처를 환경별 manifest, 보드 역할, HEX/ELF hash, 원문·metadata와 연결한다. 준비된 bundle은 실행 당시 코드의 고정 사본이므로 나중에 소스를 수정해도 기존 bundle을 덮어쓰지 않는다. 큰 SDK 사본을 새 개발 폴더로 계속 만드는 대신 이 실행별 기록을 유지한다.

## 오프라인 검증과 현재 범위

```bash
python3 -B -m unittest discover -s Drivers/API/tests -p 'test_*.py' -v
```

테스트에 필요한 작은 raw fixture를 저장소 안으로 옮겨 개인 컴퓨터의 과거 로그 폴더에 대한 의존성을 없앴다. [fixture 출처](../Drivers/API/tests/fixtures/origins.json)를 함께 보존한다. 제어·readback 증거는 테스트에서 합성하며, fixture는 새 실험 결과가 아니다. 무선 C 소스는 가져온 최신 사본과 동일하다.

이전 SDK 폴더와 Git worktree는 아직 삭제하지 않았다. 일부 옛 실행 도구와 로그가 그 절대 경로를 참조한다. 이후 정리가 필요하면 고유 생성물과 참조 관계까지 확인하고 진행한다. 이번 작업으로 추가 SDK 사본을 만들지는 않았다.
