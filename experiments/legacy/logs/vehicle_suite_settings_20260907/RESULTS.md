# 전체 단계 보완 — 고정 역할·설정 전달 완료

2026-09-07. 작업 사본은 `DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907`. 이번 범위는 공통 manifest와 설정 전달이며, **양쪽 배포·case 간 동기화 실행기·RF는 다음 단계**다. 원본 Git·기존 실험 로그·보드 flash는 변경하지 않았다.

## 연결한 내용

1. 공통 `brrs_vehicle_manifest.json`에 RX와 TX6의 정확한 serial/역할을 고정했다. 모든 1:1은 물리 N4 `1050282818`, 단일 링크 논리 N2다. 통합 런너 및 auto-TX는 기본적으로 고정 map을 사용하고 serial 충돌을 거부한다. USB 순서와 run 번호로 역할을 회전하지 않는다.
2. Stage0와 후속 설정을 구분했다. Stage0는 PAC4/8 각각 41개 lead 조건을 유지한다. 최적 lead는 미선정/null 상태이며, 근거를 기록하고 동결하기 전에는 본 실험 계획을 생성하지 않는다. 테스트에 사용한 PAC4 lead17/PAC8 lead25는 시험용 fixture일 뿐 실제 선정값이 아니다.
3. Exp2 PAC를 통합 런너·캡처·빌드·캐시 stamp·로그명·metadata까지 연결했다. Exp2 INIT에 설정 마커를 추가하여 실행 로그의 M/PAC/SFD timeout/lead가 요청값과 일치하는지 검증한다. 추가 마커는 Exp2에만 컴파일된다.
4. Exp4의 `--slotted-rx`/`--spi-opt`/SB/SP/cycles를 통합 런너와 하위 도구에 전달했다. 이미지 경로와 metadata, 슬롯별 수신창 검증까지 연결했다. RX PAC 비교에서 TX는 PAC8 정의로 고정한다. 단순 TX 패킷 송신 조건을 함께 바꾸지 않도록 한 것이다.
5. Exp3 A/B/C, Exp5 M1024/PAC32는 단계별 조건으로 유지했다. 물리 TX6과 논리 슬롯 수를 분리하고 M별 타이밍 상한을 검증한다. 현 G250/SB3000/SP2500에서 M32/M64/M256 상한은 각각 13/12/8슬롯이다. 기본 계획에 6슬롯과 상한을 담았지만 아직 RF 실행하지 않았다.
6. 모든 캡처 metadata에 실제 serial과 실행기가 제공하는 manifest/case/조건 식별자, 물리 역할·논리 ID를 기록할 수 있게 했다. 이 식별자 기록 자체가 flash readback을 대신하지는 않는다.
7. READY/END 누락·중복을 수집 실패로 처리한다. Exp4에서는 합법적인 슬롯 timeout과 수집 오류를 구분하고, `per_node_goal`을 전체 PER와 별도로 계산한다.

## 검증 결과

- 새 실제 빌드 **2개**: Exp2 M32/PAC4/lead17 RX, Exp4 PAC4 조건 N4 TX. 둘 다 실제 캡처 CLI의 `--build-only` 경로로 성공했다. build-only는 J-Link 접속 전에 종료한다.
- Exp2 ELF에서 PAC4/SFD timeout37과 새 설정 마커를 확인했다. lead17은 빌드 입력·캐시 stamp에 반영됐다.
- Exp4 PAC4 조건의 N4 HEX는 기존 PAC8 조건 TX HEX와 SHA256이 동일하다: `42737cae87424c4a91a52336d509c766e596fd809a085ff5665818a90690b8d2`.
- 기존 N4 PAC8 이미지는 통합 런너의 고정 map→하위 캡처→이미지/RTT 확인 경로로 재사용했다. 다시 빌드하지 않았다.
- **10개 테스트 통과**: 고정 역할, 미인식/다른 serial 거부, 미선정 lead 차단, PAC별 다른 lead 전달, 단계별 PHY 보존, 슬롯 상한, 잘못된 창/FWTO/RDB 및 중복 마커 거부, 캐시 PAC 불일치 거부 등.
- 관련 셸/Python **33개 파일 문법 검사 통과**.
- 모의 RTT를 사용해 실제 Exp2 RX/TX 캡처·검증·metadata 경로를 확인했다. RX 933, TX 818(물리 N4/논리 N2), PAC4/lead17과 case hash가 metadata에 일치했다. 로그의 PAC만 틀리게 만든 fixture는 거부했다. **모의 자료는 `mock_` 이름과 MOCK REPLAY 표식으로 구분하며 새 RF 결과가 아니다.**
- 기존 R13P4 로그 재검증에서 수집 정상, 최악 N4 PER7.15%, `per_node_goal=FAIL_PER`를 확인했다. 손실을 성공으로 바꾸지 않았다.

새 RF·플래시·reset·원격 배포·commit/push는 모두 **0회**다. 이전 정상 RX·TX 조건을 반복 실험하지 않았다. 새 lead 선택 이후의 모든 이미지가 준비됐다는 뜻은 아니다.

## 산출물과 다음 작업

- [운영 설명](../../DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API/BRRS_VEHICLE_MANIFEST_KR.md)
- [공통 manifest](../../DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API/brrs_vehicle_manifest.json)
- [Stage0 명령 계획](stage0_plan.json): 82개의 서로 다른 PAC×lead 조건. 아직 실행하지 않음.
- [설정/계획 검사](plan_and_syntax_checks.json), [최종 테스트 로그](tests_final.log)
- [실제 빌드·이미지 확인](compiled_parameter_checks.json), [모의 metadata 확인](capture_metadata_checks.json)

다음은 동일 이미지와 수집 도구를 양쪽에 배포하고, 비참여 TX 정지·case별 TX READY→RX 시작·결과 합산 검증을 맡는 실행기를 연결하는 작업이다. 그 전까지 새 manifest의 역할별 RF 명령을 각각 실행해서 전체 준비가 끝났다고 판단하지 않는다.
