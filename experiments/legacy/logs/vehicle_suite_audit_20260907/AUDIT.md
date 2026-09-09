> 2026-09-08 후처리 추가 발견: CIR 절대 전력 지표의 품질 문제를 확인했다. M32 RSSI는 하한/오류값이 다수이며, FP power 함수의 RX code cast 순서 버그가 Exp2/5 ELF에도 있다. PER와 CIR 직접 비율 FP-SNR 판정은 별개다. [상세 근거](../vehicle_onepass_run_20260908_0039/postprocess_20260908/POWER_DIAGNOSTICS.md). 펌웨어 수정·RF 재실행은 하지 않았다.

> 2026-09-08 최종 준비 점검: 사용자 요청으로 남은 소수TX 조건은 생략하고 TX6 신규16조건을 각각1회 완료했다. 모두 각 노드 PER<1%, 최악0.1%였다. Exp1 추가6·Exp2 추가7조건도 완료했고 Exp3/Exp5는 앞선 정상 실행을 재사용했다. 원본 Git/펌웨어 C 유지, 7대 원래 역할 M32/PAC8/lead25 기준 HEX 복원·정지 완료. 추가 후처리는 보류했다. 전체 S1~S6 행렬·논문 반복·차량 성능 완료가 아닌 선택한 준비 범위의 완료다. [최종 기록](../vehicle_onepass_run_20260908_0039/RESULTS.md), [차량 아침 안내](../vehicle_onepass_run_20260908_0039/VEHICLE_MORNING.md).

> 2026-09-08 추가 실측: S2 부분 활성화와 block2 전체6TX 회전의 수집·역할/HEX·readback 경로를 확인했다. 단일N4 Stage0의 lead20은6TX에 충분하지 않았고, PAC8/25는 회전 후에도 최악0.55%, 26·27은 모두0%였다. PAC4의 이전 최저27µs는 N7 1.00%, 전선 가림 보고 후 같은 조건1회는6.8%여서 안정적인 선정값이 아니다. [전체 기록](../nlos69_lead_screen_rotation_20260907_2326/RESULTS.md), [요청한 재측정](../exp4_pac4_lead27_cable_recheck_20260908_0018/RESULTS.md). 가림 대상·시점은 불명이며 이전 결과를 임의 제외하지 않았다. 전체 lead/반복/회전 및 차량 성능의 완료 판정은 아니다.

> 2026-09-07 후속 상태: 이 초기 audit의 구현 공백을 독립 fix 사본에서 보완했다. [구현 결과](../vehicle_paper_impl_20260907/RESULTS.md). 이후 현재 NLOS에서 Stage0·Exp1·Exp2·Exp3 A/B/C·Exp5 대표 RF 7회와 후처리를 통과했고, 직전 Exp4 TX6/13슬롯도 각 노드 PER<1%였다. 후처리 분석기 2곳을 추가 보완했다. [대표 실측 결과](../vehicle_suite_nlos69_smoke_20260907_2301/RESULTS.md). PAC별 최적 lead, 전체 조건·반복·회전 RF와 차량 실측은 남아 있다. 아래 내용은 최초 발견 당시 기록이다.

# Stage0~Exp5 펌웨어·설정·수집 경로 점검 — 2026-09-07

## 판정

**장비 연결은 정상으로 복구됐지만, 기존 통합 실행 도구와 최신 Exp4 소스를 그대로 조합해서 전체 단계를 실행할 수는 없다.** 단일 링크 TX의 실제 컴파일 실패, Exp4 옵션 전달 누락, PAC 전달 공백, 자동 serial 재배정과 양쪽 소스 불일치를 확인했다. 이번 작업은 점검이며 해당 문제를 고친 릴리스는 아직 만들지 않았다.

사용자 지정: 설치 순서는 기존 표를 적용하고, 모든 1:1 시험은 **RX 1050270933 + 물리 N4/TX 1050282818**을 사용한다. 단일 링크 TX 빌드의 논리 ID는 `TEST_NODE_2`다. 물리 N4를 다른 보드로 바꾸는 뜻이 아니며, Exp4로 돌아갈 때는 N4 역할 이미지를 사용한다. 상세 매핑은 [node_assignments.json](node_assignments.json).

## 장비·소스 보존

- 로컬 INIT 1050270933 인식, SSH `s-macbook-air` 정상.
- 최초 원격 확인은 TX 5대였으며 N6 1050227627 미인식. 사용자 재연결 후 **16:08 KST에 N2~N7 6대 모두 인식**. 두 번의 미인식 기록도 보존했다.
- 양쪽 잔여 캡처 프로세스 없음. 보드 플래시·reset·halt·RF는 이번 점검에서 0회.
- 원본 Git 브랜치·HEAD·dirty 상태 유지. commit/push 없음.
- 최근 정상 Exp4 소스 `DW3_QM33_SDK_1.0.2_exp4_s6_multislot_20260907`에서 `.git`과 기존 Output을 제외한 독립 점검 사본 `DW3_QM33_SDK_1.0.2_vehicle_suite_audit_20260907`을 만들었다. 펌웨어 C 파일과 프로젝트 내용은 변경하지 않았다. 새 빌드는 이 사본에만 생성했다.
- 현재 장비의 마지막 탑재 기록은 Exp4 PAC4/SFD64 진단 이미지다. 이번에는 flash readback을 다시 하거나 이를 다른 단계 이미지로 교체하지 않았다.

## 확인한 정상 경로

| 단계 | 확인 결과 | 남은 한계 |
|---|---|---|
| Stage0 | lead 0~40 정수 41개 프로젝트 설정 모두 존재. 대표 M32/PAC4/lead25 RX 빌드·RTT 주소·종료 마커 정상 | 전체 lead의 RF sweep이나 최적 lead 선정은 하지 않음. TX 빌드 실패 |
| Exp1 | M32/64/128/256, PAC8/lead25 RX 4개 빌드 정상. 요청 PAC/lead/mode를 검증기로 전달 | PAC별 lead는 별도 실행 블록 필요. TX 공통 실패 |
| Exp2 | M32/64/128/256 RX 4개 빌드 정상. 실제 DATA PAC8. CIR READY/END 및 수신·CIR 행수 검증 연결 | 선택 PAC4를 전달할 경로 없음. TX 공통 실패 |
| Exp3 | A/B/C RX 3개 빌드 정상. ELF에서 A=SFD8/STD PHR, B=SFD16/STD PHR, C=SFD8/DTA PHR 확인 | TX 공통 실패. EXTTXE 실측은 이번에 하지 않음 |
| Exp4 | 기존 M32 6TX/12·13슬롯 실측·이미지 증거 재사용. 새 M64·M256 각각 S6/G250/lead25/슬롯별 RX/SPI 최적화 INIT 빌드 1회씩 정상 | 새 두 빌드는 기본 논리 6슬롯이며 RF/최대 부하 검증 아님. 통합 도구로 최신 옵션 전달 불가 |
| Exp5 | M1024 RX 빌드 정상. 실제 DATA PAC32, SFD timeout 1001. CIR READY/END와 raw dump 마커 연결 | M32의 선택 PAC4/8을 기계적으로 복사할 대상 아님. TX는 Exp2_Normal 공통 실패 |

대표 RX 빌드 **15개 성공**, 단일 링크 TX 실제 빌드 **1개 실패**. 같은 TX 공통 소스의 실패를 다른 설정 이름으로 5회 재현하는 빌드는 생략했다. Stage0 대표 lead25는 전달·빌드 점검값이며 최적값으로 선정한 것이 아니다. Exp1의 모든 M×PAC 조합이나 Stage0의 모든 lead를 빌드한 것으로 표현하지 않는다.

별도로 전체 6단계×2역할 **12개 dry-run, 총 120개 case 확장**을 확인했다. 모두 지정 serial을 하위 명령으로 전달했고, 기본 반복은 1이다. 관련 셸/Python **32개 파일의 문법 검사 통과**. 이는 RF 성공 또는 전체 단계 간 동기화 보증과 구분한다.

RX ELF 13개에서 실제 `config_data`/`config_sync` 바이트를 읽었다. 모두 채널9·6.8Mbps enum이며 SYNC 설정은 동일하다. M32는 PAC4/SFD timeout37, PAC8/33이다. Exp3 B/C의 의도된 SFD·PHR 차이와 Exp5 PAC32가 유지된다. 단순 파일명 추정이 아닌 결과는 [compiled_phy_audit.json](compiled_phy_audit.json)에 있다.

## 실행 전에 해결할 연결 공백

### 1. 단일 링크 TX가 최신 공통 소스에서 컴파일되지 않음

`brrs_normal.c`의 `brrs_exp4_phy_switch()` 정의는 `#if BRRS_EXPERIMENT == 4` 안에 있지만, DATA/SYNC 설정 전환 호출(1982, 2005행)은 다른 실험에도 남아 있다. `Stage0_Normal` 실제 빌드에서 **implicit declaration of function**으로 실패했다. Stage0/Exp1/Exp2/Exp3 및 Exp5가 재사용하는 TX 경로에 공통으로 해당한다. Exp4 정상 실측만으로 다른 단계의 TX 빌드까지 정상이라고 볼 수 없었다.

수정 방향: 해당 호출도 Exp4 조건으로 제한하고 나머지 실험은 기존 `dwt_configure()` 경로로 연결한다. 이번 점검에서 펌웨어 수정은 하지 않았다. [실제 실패 로그](stage0_tx.build.log), [빌드 기록](build_results.json).

### 2. 기존 통합 런너가 최신 Exp4 RX 경로를 선택하지 못함

`brrs_run_experiment.sh`는 `--slotted-rx`/`--spi-opt`를 지원·전달하지 않는다. 하위 `brrs_exp4_build.sh`에는 두 옵션이 있으나 기본값은 0이다. `brrs_exp4_capture.sh` 및 `brrs_exp4_multi_tx.sh`에도 slotted-RX 전달 경로가 없다. 따라서 기존 통합 실행은 최근 검증한 슬롯별 RX·SPI 경로와 달라진다. 최신 Exp4 캠페인은 별도 orchestrator/manifest로 실행했던 것이다.

M별 논리 슬롯 상한도 별도 선택해야 한다. 현재 G250/SB3000/SP2500/SF10ms 조건에서 M32의 13슬롯 설정을 모든 M에 그대로 확장하면 안 된다. 이번 M64/M256 빌드는 기본 6슬롯으로 각 비교 RX 경로의 컴파일 가능성만 확인했다.

### 3. PAC별 lead와 단계별 PAC 전달을 명시해야 함

- Stage0/Exp1은 PAC 전달 가능. Exp1의 한 번의 통합 호출은 전체 PAC 목록에 하나의 lead만 적용하므로, **PAC4+lead4와 PAC8+lead8은 별도 블록**으로 구성하면 현재 옵션 안에서도 구분할 수 있다. lead 선택·동결은 Stage0에서 한다.
- Exp2는 런너와 캡처 모두 PAC 인자가 없고 실제 빌드는 PAC8이다. Exp1 PAC4와 같은 조건의 CIR이라고 기록하면 틀린다. PAC 인자·빌드 정의·출력 경로·metadata·검증이 함께 연결되어야 한다.
- Exp3 A/B/C는 의도적으로 PHY가 다르므로 짝을 맞춰야 한다. Exp5는 M1024/PAC32 기준 채널 측정이다. ‘일관성’은 모든 단계를 같은 PAC/M으로 바꾸는 뜻이 아니다.

### 4. 기존 auto-TX가 현재 보드 역할을 바꿈

실제 6개 serial을 하위 배정 함수에 넣은 읽기 전용 검사에서, run1 자동 배정은 N2=4212, N3=8509, N4=584, N5=7627, N6=888, N7=818이 됐다. **현재 고정 매핑과 6개 모두 다르다.** `--probe-serials` 입력 순서도 내부 정렬되므로 순서 지정만으로 해결되지 않는다.

Exp4에서는 manifest의 정확한 역할/serial을 사용해야 한다. 1:1에서는 물리 N4 serial을 지정하고 나머지 5TX를 송신하지 않는 상태로 관리해야 한다. 기존 단일 보드 캡처 도구는 비참여 보드 정지를 자동으로 보장하지 않는다. 두 노트북의 독립 suite를 시작하는 것만으로 각 case의 TX READY→RX 시작 동기화를 보장하지도 않는다.

### 5. 양쪽 노트북의 공통 버전이 준비되지 않음

원격에 최신 S6/SFD64 SDK 전체 사본은 없다. 최근 Exp4는 필요한 이미지·캠페인·수집 도구를 따로 배포한 형태다. 양쪽 기본 SDK의 핵심 C 파일, Exp4 빌드/캡처, RTT 수집기 hash도 다르다. 원격 기본 SDK에서 전체 단계를 새로 빌드하면 로컬 최신 소스와 같다고 보장할 수 없다.

차량용 공통 소스와 조건별 이미지 manifest를 한 번 확정하고, 동일 이미지와 수집/검증 도구를 양쪽에 배포한 뒤 hash를 비교해야 한다. 로컬 빌드·원격 캡처 방식이면 원격에 컴파일러나 분석 패키지를 추가할 필요는 없다. 양쪽 pylink는 있고, 분석에 필요한 numpy/matplotlib은 로컬에 있다. 이번에는 패키지 설치나 원격 파일 변경을 하지 않았다.

### 6. 수집 성공과 실험 목표 성공을 분리해야 함

Exp2/Exp5는 수신 0개, CIR 행수·종료 자료 불일치를 거부한다. Exp5는 성공 CIR 최대30프레임×300 taps를 검증한다. Stage0/Exp1의 `collection=PASS`와 통합 `SUITE_DONE ... PASS`는 PER<1% 판정이 아니다. Stage0의 RX0 지점은 수신 전이 탐색 자료로 보존하더라도 RF 성공으로 판정하지 않는다.

현재 일반 RTT 수집기는 READY를 발견했는지 기록하지만 최종 성공 판정에서 READY 누락 자체를 거부하지 않는다. 종료 마커 중복도 일반 하위 검증기는 마지막 행을 사용하는 부분이 있다. 최근 Exp4 캠페인의 엄격한 READY/END 개수, 실제 TX 송신량, 역할·설정 검증을 전체 단계의 공통 판정에도 연결해야 한다. 사용자 목표는 계속 각 TX PER<1%이고 전체 평균이나 수집 PASS로 대체하지 않는다.

## 다음 작업의 범위

1. 독립 공통 소스에서 비-Exp4 TX 호출 분기부터 수정하고, 실패했던 경로만 새로 빌드한다.
2. N4 단일 링크/비참여TX 정지와 Exp4 고정 serial 매핑, PAC별 lead, Exp2 PAC, Exp4 슬롯별 RX/SPI 옵션을 조건 manifest에서 빌드·캡처·검증까지 연결한다.
3. 동일 파일을 양쪽에 배포해 검증한 뒤, 단계별로 아직 검증하지 않은 최소 대표 조건만 실행한다. 정상으로 확인된 동일 조건의 반복 RF는 추가하지 않는다.

이번 결과를 차량 RF 측정이나 PAC/lead 최적화 결과로 해석하지 않는다. 진단 사본의 빌드 산출물도 아직 차량용 릴리스 이미지로 지정하지 않는다.

## 근거

- [연결 최종 확인](remote_preflight_n6_reconnected.json), [로컬 확인](local_preflight.json)
- [설정 확장·문법·자동 배정 검사](pipeline_checks.json)
- [단일 링크 빌드 기록](build_results.json), [Exp4 추가 빌드 기록](exp4_build_results.json)
- [로컬 파일 inventory](local_inventory.json), [원격 파일 inventory](remote_inventory.json)
- [원본 보존 확인](preservation_check.json), [독립 사본 해시](source_copy.json)
