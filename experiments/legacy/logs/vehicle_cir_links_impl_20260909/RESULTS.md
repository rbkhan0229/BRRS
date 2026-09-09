# Exp2·Exp5 물리 링크 순회 구현 결과 — 2026-09-09

**구현과 오프라인 검증 완료. 새 RF 측정, SSH 접속, 보드 플래시를 수행하지 않았다.**

작업 대상은 독립 차량용 사본 `DW3_QM33_SDK_1.0.2_vehicle_beacon512_20260908/Drivers/API`다. 기존 무선 C 코드는 어느 물리 TX든 논리 N2로 측정할 수 있으므로, 무선 동작을 변경하지 않고 펌웨어 선택·제어·수집 도구를 확장했다. main/diag Git 작업 트리의 상태는 작업 전후 동일하며 commit/push하지 않았다.

## 최종 동작

- Exp2: M32/64/128/256 × PAC4/8 각각에서 물리 N2~N7 한 대씩 실행. 준비 모드 48 case, 논문 기본 3회 반복 포함 144 case.
- Exp5: M1024/PAC32에서 물리 N2~N7 한 대씩 실행. 준비 모드 6 case, 논문 기본 3회 반복 포함 18 case.
- 한 case는 RX+선택 TX 한 대만 활성화한다. 다른 5대는 정지 상태를 검증하고 플래시하지 않는다. 보드 재배치/USB 교체가 필요 없다.
- TX READY 이후 RX 시작, 이미지 readback·종료 마커·metadata·serial 검증을 유지한다. 비참여 보드 정지 실패도 실행 실패로 반영한다.
- case ID에 `_txN2`~`_txN7`을 추가한다. raw의 논리 N2와 실제 물리 보드를 구분하여 `nodes_by_serial` 및 `stage_metrics.physical_link`로 저장한다. 누락 링크와 수신 0을 성공으로 집계하지 않는다.
- RX 로컬/TX 원격, 보드 7대 모두 원격 맥북에어, 보드 7대 모두 로컬의 Exp2/Exp5 실행을 연결했다. 실제 단일 호스트 RF는 미검증이다.
- 기존 통합 실행기의 단일 TX 선택에 `--physical-tx-role`을 연결했다. 새 복수 링크 manifest에서 선택을 생략하면 임의 N4를 고르지 않고 거부한다. 이 경로에서 발견한 macOS 기본 Bash의 빈 배열/unset 오류도 수정했다.
- Stage0·Exp1·Exp3·Exp4 S1의 N4 선택과 Exp4 다중 TX 역할 회전은 유지한다. 새 필드가 없는 과거 manifest/bundle은 원래 N4 단일 링크로 해석한다.

## 검증

| 검사 | 결과 |
|---|---|
| 기존 논문 실행 계획·증거·집계 회귀 | 24/24 PASS |
| 새 CIR 링크·serial·누락·제어 경로 회귀 | 12/12 PASS |
| 단일 호스트 worker 생명주기 | 8/8 PASS |
| 변경 Python 구문 / Bash 구문 | PASS |
| 과거 실제 Exp2 N4 bundle 재판정 | PASS |
| 과거 실제 Exp5 N4 bundle 재판정 | PASS |
| 최종 도구와 이미지 포함 build-only bundle | Exp2/Exp5 × N2/N7, 4개 PASS |

테스트는 합성 제어 증거 또는 모의 worker를 사용하며 새 RF 결과가 아니다. 실제 과거 CIR raw를 다른 물리 역할의 합성 metadata로 감싼 검사는 **식별 검증기의 동작만** 확인한다. 과거 N4 자료가 N2/N7 실측으로 바뀌었다는 뜻이 아니다. 원본은 수정하지 않았으며 테스트 임시 결과를 RF 집계에 넣지 않았다.

최종 네 bundle은 `TEST_ONLY_final_*`이고 lead26은 build-only 경로 검증용이다. PAC별 lead 선정 근거로 사용하지 않는다. 공통 manifest의 미선정 상태는 유지했다. 초기 네 `TEST_ONLY_*` bundle도 중간 준비 기록으로 보존했으며 최종 도구 기준은 `TEST_ONLY_final_*`다.

최종 build-only HEX SHA256:

| 이미지 | SHA256 |
|---|---|
| Exp2 M32/PAC8/lead26 INIT | `d7c48812323937b72af73a185815bc886eece925b981075b4b539516009d56b1` |
| Exp5 M1024/PAC32/lead26 INIT | `70d0ca0e0e164b8a3bc8e2730bfdffee4cd79e0ace7386155666c3164d76c473` |
| 공통 논리 N2 TX | `f57cb9e4950bfc756268951fd02430a5ef20e73d5b5c7737287a912045681102` |

N2와 N7을 선택해도 TX HEX가 같은 것은 의도한 동작이다. 실제 flash 대상을 serial로 정하며, 논리 N2의 1:1 TX 펌웨어를 재사용한다. INIT/TX C 파일 SHA256도 작업 전후 동일하다.

## 남은 확인과 사용 안내

새 물리 링크 순회를 실장비에서 확인하는 일은 사용자가 실험을 재개할 때 수행한다. 이번 작업으로 PER 개선이나 차량 성공을 확인한 것은 아니다. 기존 CIR의 수신 성공 표본 편향과 FP 전력 계산 문제도 별개다.

실행은 환경별 manifest에 조건·보드·반복을 기록하고 `plan → prepare → run → assess`로 진행한다. 정해진 계획은 자동 실행하며, adaptive sweep이나 배치 변경처럼 결과 해석이 필요한 다음 조건은 별도로 결정한다. PER가 나쁘면 기록하고 다음 조건으로 넘어가며, 제어·수집 실패 시 중단하고 자동 재시도하지 않는다.

- [새 사용 문서](../../DW3_QM33_SDK_1.0.2_vehicle_beacon512_20260908/Drivers/API/BRRS_CIR_LINKS_KR.md)
- [변경 파일 목록](changed_files.json) / [전체 패치](implementation.patch)
- [테스트 실행 결과](test_results.json)
- [최종 bundle·serial·HEX/ELF hash 검증](final_build_only_checks.json)
- [C 코드 및 Git 상태 보존 확인](unchanged_checks.json)
- [과거 실제 bundle 호환성](historical_assessment_compatibility.json)
