# 7보드 HALT 시간 A/B 격리 진단

이 폴더의 코드는 **실험 실행기 변경이 아니다.** 봉인된 자택 Stage0 bundle의 지정 J-Link 7대가 이미 정지·연결된 상태에서, 직렬 `park_all` A → 독립 프로세스 병렬 HALT B → 직렬 `park_all` A를 비교하기 위한 비RF 진단이다. flash, reset, RF, 이미지 변경 명령이 없다.

실행 전 동일 bundle의 `STOP`, Air 전역 `STOP_ALL`, 정확한 7대 serial, 캡처/J-Link 점유 프로세스 없음, 사용자 배치 확인이 필요하다. 스크립트는 기존 machine lock과 preflight를 사용하고, 각 worker가 자기 serial에 HALT 후 연결을 끊고 다시 연결해 HALT 유지와 3.0–3.6 V를 확인한다. 단계 사이 전체 7대 상태를 검사한다. 어떤 오류든 기존 직렬 `park_all`로 최종 복구를 시도하고 활성 INIT/N7 전체 HEX readback을 기록한다. 전역 STOP이나 bundle STOP은 옮기거나 해제하지 않는다.

이 코드는 오프라인 단위 테스트와 Air 검증 뒤에도 자동으로 실험기에 적용되지 않는다. 병렬화 중 USB/J-Link 경합, 정지 오류 또는 readback 불일치가 있으면 후보를 채택하지 않는다. 시험 결과에서 차이가 나더라도 Stage0 단일 사례가 Exp4 다중 TX 전체 시간을 대표하지 않는다.

## 2026-10-02 자택 HALT-only A/B/A 1회 결과

사용자 확인 후 지정 7보드에서 순차 A 8.684초 → 병렬 B 1.554초 → 순차 A 8.307초. 각 단계 7대 HALT·3300mV, 마지막 기존 순차 정지와 활성 INIT/N7 전체 HEX readback PASS, 전역·case STOP 유지, 수집 프로세스 0, RF·flash·reset 0회였다. 병렬 B는 두 순차 단계 평균보다 6.941초 짧았지만, 정지된 보드의 단일 조건 실측치다. 공식 실행기의 시작/종료 HALT에 아직 적용하지 않았고, 전체 케이스 절감시간도 확정하지 않는다. 원시 결과와 상세 제한은 로컬 `logs/home_parallel_halt_trial_20261002_m2jAbm/`에 별도 보존했다.
