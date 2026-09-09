# 전체 단계 연결 보완 — 1차: 단일 링크 TX 빌드 복구

2026-09-07. 가장 먼저 Stage0/Exp1/Exp2/Exp3/Exp5의 공통 TX 컴파일 오류를 수정했다. **빌드 차단은 해소됐지만 전체 단계의 통합 실행 준비는 아직 완료되지 않았다.** 원래 점검 기록은 [AUDIT.md](../vehicle_suite_audit_20260907/AUDIT.md)에 보존한다.

## 수정

독립 사본 `DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907`에서 `brrs_normal.c`의 DATA 설정 전환 및 실패 시 SYNC 복구 두 곳을 분기했다. Exp4는 기존 `brrs_exp4_phy_switch()`를 유지하고, 나머지 실험은 직접 `dwt_configure()`를 호출한다. Exp4에만 정의된 함수를 다른 실험에서 호출하던 컴파일 실패를 제거했다.

실제 런타임 변경은 이 C 파일 하나뿐이다. 빌드 도우미의 `-fmacro-prefix-map` 입력 경로도 독립 사본 위치에 맞춰 수정하여 기존 Exp4 이미지와 비교할 수 있게 했다. INIT·드라이버·PHY·PAC·lead·송신 출력·슬롯 설정은 변경하지 않았다. [정확한 변경](change.patch), [변경 범위 확인](firmware_diff_scope.json).

## 검증

| 경로 | 결과 |
|---|---|
| Stage0/Exp1 TX | Stage0_Normal 빌드·READY/END·RTT 주소 확인 통과. Exp1_Normal은 이름을 제외한 프로젝트 속성이 동일하여 중복 빌드 생략 |
| Exp2/Exp5 TX | 공용 Exp2_Normal 빌드·READY/END·RTT 주소 확인 통과 |
| Exp3 A/B/C TX | 서로 다른 3개 PHY 변형 각각 빌드·종료 마커 확인 통과 |
| Exp4 N4, M32/S6/PAC8/G250/lead25 | 회귀 빌드 통과. HEX SHA256이 이전에 실제 RF 검증한 N4 이미지와 완전히 일치 |

Exp4 비교 SHA256: `42737cae87424c4a91a52336d509c766e596fd809a085ff5665818a90690b8d2`.

새 빌드는 총 6개, 모든 빌드 성공. 정상 RX 빌드는 반복하지 않았다. Exp3 종료 마커는 ELF에 printf 템플릿으로 저장되므로 템플릿을 확인하고 단계별 A/B/C 프로젝트 설정과 함께 해석했다. 검증기 초기 문자열 검사만 바로잡았으며 그 때문에 펌웨어를 다시 빌드하지 않았다.

상세 명령·HEX/ELF hash·RTT 주소는 [verification.json](verification.json). 이 결과는 컴파일·정적 검증이며 새 펌웨어의 무선 성능 검증은 아니다. 보드 플래시·reset·RF·원격 파일 변경·원본 Git 변경·commit/push는 하지 않았다.

## 다음 순서

1. **완료: 공통 TX 빌드 복구.** 이후 공통 작업의 기준 사본은 이번 fix 사본이며, 기존 진단/감사 사본은 보존한다.
2. **조건과 보드 선택 연결.** 1:1은 RX 1050270933 + 물리 N4 1050282818(단일 링크 논리 N2), Exp4는 원래 6TX 역할 매핑을 고정한다. PAC별 lead와 Exp2 PAC, Exp4 슬롯별 RX/SPI 옵션을 빌드·이미지명·metadata·검증까지 연결한다. Exp3 A/B/C와 Exp5 M1024/PAC32는 단계 고유 조건으로 유지한다.
3. **양쪽 실행·수집 연결.** 같은 이미지와 도구를 manifest/hash로 배포하고, 비참여 TX 정지와 case별 TX READY→RX 시작 순서를 보장한다. 수집 성공과 PER 목표 판정을 분리한다.
4. **RF 검증.** 통합 후 지정 N4로 아직 확인하지 않은 대표 경로를 확인하고 Stage0의 PAC별 lead 탐색·동결을 진행한다. 이후 Exp1~Exp5의 필요한 서로 다른 조건을 수행하며 정상 동일 조건은 반복하지 않는다.

PHY 신뢰도 개선에 관한 새로운 PAC/lead/SFD 결론은 아직 없다. 먼저 실험 도구가 의도한 조건을 정확히 실행하게 만드는 순서다.
