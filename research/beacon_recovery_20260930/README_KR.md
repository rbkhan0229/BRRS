# 비컨 누락 복구·수신 준비 시각 자택 진단 보존본

이 디렉터리는 2026-09-29~30 자택 **단일 링크** 연구의 소스와 결과 요약을 Git에 보존한다. 차량 Stage0, Exp4 다중 노드, Standard qualification 또는 개선 채택본이 아니다. 특히 `candidate_600us`의 짧은 수신창은 AUX ON에서 공식 PER가 악화되어 **채택하지 않았다**. 여기의 펌웨어를 Standard 이미지로 간주하거나 기존 캠페인에 적용하지 않는다.

## 소스와 원본

- `reference/firmware/`와 `reference/controller/`: CH5 M64/PAC8, SYNC M512/code10, DATA code11, lead44µs 임시, 비컨 단발 누락 예측 송신, RX 시작 RMARKER−800µs의 봉인된 기준 소스 스냅샷. N7 송신기와 INIT 수신기, AUX 송신기 및 호스트 검증 코드가 포함된다.
- `candidate_600us/SOURCE_DELTA.txt`: 기준 N7 코드의 예정 SYNC RX 시작값을 800→600µs로 바꾼 정확히 한 줄의 차이. 후보의 전체 이미지를 채택한 것이 아니다.
- `analysis/`: 송수신 시각 계측 재분석 코드와 결과. 추정된 RX-enable API 반환 시각은 실제 RF 수신 준비 완료 시각의 직접 측정이 아니다.
- `results/`: 강한 AUX CH9 비교와 CH5 800/600µs 비교의 보고서. 각 보고서의 Air `logs/...` root가 원본이며 이 Git 복사본에는 원시 RTT 로그, HEX/ELF, 개인 장치 경로의 실행 상태 파일을 싣지 않았다.
- `SOURCE_SHA256.json`: 이 보존본의 개별 파일 SHA256. 원래 case의 firmware/toolchain/manifest/payload 해시는 각 Air 원본 `bundle/`의 provenance와 payload 색인에 남아 있다.

CH9 강한 AUX 비교에서는 단발 비컨 누락 복구 ON이 OFF보다 offered PER 합산 4.375→3.450%로 낮았지만, 모든 run이 공식 PER<1%에 실패했다. CH5 수신 준비 시각 비교에서는 AUX ON offered PER가 800µs 기준 1.10%, 600µs 후보 1.75%로 모두 공식 실패였다. 조건과 시간이 다른 적은 반복이므로 차량 성능 개선이나 인과 효과를 주장하지 않는다.

이 보존본은 전체 vendor SDK와 실행 환경을 담은 독립 실행 패키지가 아니다. 실제 실행 전에는 별도 검증된 SDK·도구·물리 배치와 새 case/root, 새 source/firmware hash, 기존 STOP 및 readback 절차가 필요하다. Ubuntu 메모리 무결성 문제는 미수리이며 이전 실기는 Air에서 빌드·제어·수집했다.

같은 브랜치의 `Drivers/API` 및 `docs/experiments` 변경은 이 격리 연구보다 앞서 로컬에 남아 있던 차량 실험 코드·설계 초안도 함께 보존한 것이다. `brrs_vehicle_manifest.json`의 환경·serial은 해당 과거 초안의 값이며 현재 차량 배치의 승인된 실행 매니페스트가 아니다. `V2_BRRS_EXPERIMENT_STANDARD_KR.md`는 논문·실험 **설계안**으로, 실행기에서 완성·검증된 906케이스 캠페인이라는 뜻이 아니다.
