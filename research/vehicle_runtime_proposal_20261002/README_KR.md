# 차량 실행 변경 분리본 — 채택 전 검토용

이 브랜치는 원 PR #2의 `4cc28ef` 중 `Drivers/API/` 17파일 변경만 `vehicle-experiments` 기준에서 **바이트 동일 패치**로 분리한 것이다. 원 PR #2, 그 작업트리의 미커밋 V2 문서·Excel, 준비된 차량 controller/root/STOP·원시 로그는 수정하지 않았다. 비컨 연구 보존본은 독립 Draft PR #5에 있다.

## 포함한 변경

- Exp4 CH5/CH9 수동 빌드·캡처 옵션 및 PHY/RF 설정 로그.
- Exp4 fast PHY·PGF 관련 계획/검증과 Standard timing·K 변경.
- RTT strict connection, 진단용 기존 이미지 확인 후 reset-only, source-build/봉인 provenance, 범위 필터, N6 템플릿 serial 변경.

이는 **최종 차량 실행기 아님**. 보존된 1,593케이스 CH5 매니페스트를 이 브랜치 planner에 오프라인으로 넣으면 `standard Exp2 must use PAC4 only for M32`에서 거부된다. 준비본은 M64/M128 Exp2와 `exp2_extended_preambles=true`를 쓰지만 이 브랜치 validator는 M32/M256만 허용한다. 또한 이 브랜치의 `brrs_suite_manifest.py`는 `uwb_channel`을 조건·캡처 인자로 전달하지 않는다. 즉 Exp4 수동 스크립트에 CH5 옵션이 있어도 Standard 전체 CH5 계획이 된 것은 아니다. 준비된 차량 controller의 추가 코드를 검토·검증 없이 자동 이식하지 않는다.

`brrs_vehicle_manifest.json`의 N6=`1050257038`은 과거 N6 교체 템플릿이고, 보존된 다음 차량 1,593케이스 초안은 복귀한 `1050227627`을 N6로 쓴다. 최종 차량 장착과 장비 연결을 확인한 뒤 환경별 manifest에서 결정해야 한다. 저장소 기본 Standard TEST ONLY lead 생성은 474케이스이고 V2 제안 825, 차량 준비 초안 1,593과 동일한 실행 목록이 아니다.

## 검증과 보류 조건

분리 패치 SHA256 `0f7718269eda99673d639f53043040d77d4893e0e239174fa9e1f8506e09fd22`가 원 PR #2의 `Drivers/API/` diff와 일치한다. `git diff --check` PASS. Ubuntu 오프라인 단위 시험 74건 중 73 PASS, 1건은 `pylink` 미설치로 import ERROR이며 코드 PASS로 취급하지 않는다. RF·flash·reset 0회.

동일 소스를 Air의 새 격리 로그 root에 checksum 동일하게 복사한 뒤 `python3 -m unittest discover`를 보드 접근 없이 재실행해 **74/74 PASS**했다. 따라서 Ubuntu의 1건 ERROR는 환경 의존성으로 분리했지만, 이 결과가 RF·차량 조건을 검증한 것은 아니다.

머지 전에는 최종 실험 범위·채널·N6·비컨 정책을 확정하고, 실제 환경별 manifest→case→빌드 인자→metadata→검증까지 동일 값이 전달되는 테스트, 오류 시 STOP/readback, Air build-only, 집 대표 RF 회귀가 필요하다. 단순히 GitHub 충돌이 없거나 기존 자택 단일 링크가 통과했다는 이유로 채택하지 않는다.
