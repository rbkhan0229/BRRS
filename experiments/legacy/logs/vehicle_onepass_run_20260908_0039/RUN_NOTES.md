
## 실행기 수정과 제외 범위

첫 Exp4 S1/M32/PAC4는 RF·수집·flash readback이 정상 완료됐으나, 판정기가 capacity_search가 없는 manifest에도 용량 후보 확장을 강제해 종료 후 오류를 냈다. 독립 fix 사본의 brrs_suite_results.py 한 분기를 수정하여 manifest의 옵션 유무를 따르게 했다. 최초 오류를 orchestration.json에 보존하고 동일 원문으로 PASS를 확인했으며 RF는 반복하지 않았다. 기존 capacity_search 활성 bundle의 실패 판정도 그대로 유지됨을 확인했다. 근거: assessment_fix.json. 펌웨어 C와 Git 원본은 수정하지 않았다.

S2/M256/PAC4 준비는 emBuild 종료1로 실패했으며 RF를 시작하지 않았다. 사용자 요청에 따라 소수TX 조건을 생략하는 시점이었으므로 해당 준비 실패 폴더를 보존하고 TX6 조건으로 넘어갔다. TX6의16조건은 모두 빌드·역할·수집·readback을 완료했다. 생략한28조건은 ACTIVE_PLAN.json의 scope_change에 나열했다. 이를 실행 또는 소프트웨어 검증 완료로 표시하지 않는다.

Exp5는 이번에 중복 실행하지 않았다. 2026-09-07 23:08:22~23:09:07 KST, RX1050270933↔물리N4/1050282818, M1024/PAC32/lead25µs에서1000/1000수신·PER0%, CIR1000행과raw30×300을 확보하고 기존 후처리까지 검증했다. 이전 원문은 ../vehicle_suite_nlos69_smoke_20260907_2301/exp5_m1024_pac32_l25/results에 있다.
