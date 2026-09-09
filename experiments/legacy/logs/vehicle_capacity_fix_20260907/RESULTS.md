# Exp4 심볼별 용량 누락 보완 — 2026-09-07

Exp4 기본 비교에서 빠진 M128을 추가하고, 최대 부하 실패 시 중간 슬롯 부하를 선택·검증하는 경로를 연결했다. 독립 `DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907` 사본만 수정했다. **새 RF 0회, flash 0회, 원격 배포 0회, commit/push 0회.** 실제 최대 용량 측정 결과는 아직 없다.

## 변경

- manifest와 기본 런너의 Exp4 프리앰블: M32/64/128/256. 하위 빌드·캡처·검증기는 이미 M128을 지원해 펌웨어 C 수정은 필요하지 않았다.
- 기본 비교: 4종 M × PAC4/8 × 공통6슬롯/계산상 최대 슬롯 = 16조건. PAC별 선정 lead를 사용하며 본 manifest의 lead는 여전히 미선정·미동결이다.
- 조건부 후보 계획: 각 M/PAC에서6슬롯 기준→최대→한 슬롯씩 감소. 모두 열거하면46조건이지만 필요한 후보만 실행하며, 더 큰 후보가 모두 실패하고 통과 지점을 찾으면 아래 부하는 생략한다. 중간 슬롯 case도 `brrs_suite_case.py prepare`로 준비 가능하다.
- 새 읽기 전용 `brrs_exp4_capacity.py`: 완성된 case bundle의 payload/HEX/raw hash, metadata, 실행 상태, 기존 INIT/TX 검증, 실제 TX 송신량을 교차 확인한다. 물리 serial별 offered/RX/PER·beacon/attempt/success, 전체 offered/RX·보고율·app goodput과 다음 case ID를 출력한다. 실행·플래시를 자동 시작하지 않는다.
- 어느 보드든 PER≥1%면 FAIL_PER. 전체 수신0, 잘못된 분모·serial·조건, 수집/제어 실패는 INVALID로 중단한다. 중복 case 입력으로 좋은 실행만 고르는 것을 거부한다. PER의 부하 단조성을 가정하지 않는다.
- 차량 전체 계획, 논문 계획, manifest 안내와 기본 런너 문서에 M128 및 용량 판정 범위를 반영했다.

## 용량 정의

| M | 계산상 최대 슬롯/SF | 준비 탐색 순서 | 최대 offered reports/s | 무손실 app goodput 상한 |
|---:|---:|---|---:|---:|
| 32 | 13 | 6→13→12→…→7 | 1,300 | 166.4 kbps |
| 64 | 12 | 6→12→11→…→7 | 1,200 | 153.6 kbps |
| 128 | 10 | 6→10→9→8→7 | 1,000 | 128.0 kbps |
| 256 | 8 | 6→8→7 | 800 | 102.4 kbps |

G250/SB3000/SP2500/SF10ms/app16B 기준이다. TX6에 여러 새 보고 기회를 배정하는 것으로,13슬롯을 독립 TX13대 실측이라고 해석하지 않는다. 기본16조건과 하강 탐색은 준비용이며 모든 부하의 완전한 PER 곡선을 측정하는 절차가 아니다. `SCREENING_MAX_FOUND`는 조건당1회에서 찾은 준비 점검 상한이다. 최종 논문 용량에는 사전 확정 반복·역할 회전·신뢰구간이 필요하다.

## 검증

1. **M128 HEX/ELF 8개 통과:** PAC8 INIT+N2~N7와 PAC4 INIT. G250, lead25, SB3000/SP2500, S6,10슬롯 `2345672345`,1,000SF, 슬롯별 bounded delayed-RX·SPI 최적화의 진단 빌드다. lead25를 두 PAC의 최적값으로 선정한 것이 아니다.
2. **실제 빌드·캡처 연결 확인:** PAC8 7개 역할은 `prepare --reuse`가 원래 캡처 CLI의 build-only를 호출해 TEST_ONLY 독립 bundle을 만들었다. PAC4 INIT도 `--no-build --build-only`로 이미지/RTT를 확인했다. 모두 J-Link 접속 전에 끝났다. ELF DATA PHY는 M128/PAC4 또는8, SFD timeout133/129를 확인했고 SYNC M256/PAC8/SFD timeout257이 유지됐다. 전체 HEX/ELF SHA256은 [빌드 검증 JSON](build_verification.json)에 있다.
3. **24개 테스트 통과:** 기존 설정 회귀10개 + 용량 검증14개. M128/PAC별 lead·수신창, 중간 슬롯 계획, 정확히1% 실패, 한 노드 실패, 수신0·불가능한 카운터, beacon 손실 분모, 유효하지 않은 수집 중단, 높은 부하 미측정 상태의 최대 확정 방지, metadata/hash/역할 불일치와 중복 입력 거부를 확인했다. [테스트 로그](tests.log).
4. 기존 R13P4 실측 원문을 **모의 제어 metadata로 감싼 테스트 자료**에서 전체 INIT/TX 파싱을 검증했다. 기존 RX12,857/13,000, N4 PER7.15%가 FAIL_PER로 유지된다. 이 모의 wrapper는 새 RF·실제 flash readback 증거가 아니며 임시 폴더에서만 사용했다. 기존 raw/bundle은 수정하지 않았다.
5. 본 manifest 검증, 셸 구문 검사 통과. PAC별 미선정 lead가 본 실험 계획을 차단하는 정책은 유지된다. 두 원본 Git의 branch/HEAD/dirty 상태와 INIT/TX C의 SHA256이 작업 전과 동일하다. [전 상태](before.json), [후 상태](after.json), [변경 내용](source_changes.patch).

`TEST_ONLY_*`는 계획/빌드 전달 경로를 검증하기 위한 인공 lead 설정과 자료다. 현장 실행에 사용하거나 유효 실험 결과에 합산하지 않는다. 새 도구 사용 절차는 [차량 manifest 안내](../../DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API/BRRS_VEHICLE_MANIFEST_KR.md)의 ‘Exp4 심볼별 용량 탐색’을 따른다.

## 남은 범위

PAC별 Stage0 lead 선정·동결, 선택된 조건의 현장 RF 및 실제 용량 측정은 남아 있다. 새 실행기의 논문용 반복·논리 역할 회전과 S1~S5 활성 집합 연결도 아직 미구현이며, 이번 용량 보완으로 전체 차량 논문 실험 준비가 완료됐다고 판정하지 않는다. 현재 물리 역할 고정·정상 조건의 준비 점검 반복 생략 정책은 유지한다.
