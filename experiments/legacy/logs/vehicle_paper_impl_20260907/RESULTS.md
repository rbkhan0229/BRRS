# Stage0~Exp5 남은 소프트웨어 구현 — 2026-09-07

사용자 요청: 실제 장비로 해야 하는 확인은 남기고 구현을 완료한다. 기본 실험 셋의 반복·회전·S1~S6 활성화·lead 선정 연결·배포·재개·결과 집계를 독립 fix 사본에 구현했다. **이번 작업은 실제 RF0회, flash0회, SSH0회, 원격 배포0회다.** 원본 Git의 branch/HEAD/dirty 상태와 INIT/TX C 파일은 변경하지 않았다.

## 구현 완료

| 항목 | 구현 결과 |
|---|---|
| 준비/논문 모드 | preparation은 고정 역할·1회 유지. paper는 단계별 독립 반복을 생성하며 case ID/로그/metadata가 반복별로 구분됨 |
| 역할 회전 | 설치표 기준6보드 순환. 한 block의 PAC·M·슬롯 비교에는 같은 매핑, 다음 block에서만 회전. 조건 순서 교대는 별도 |
| S1~S6 | S1은 준비·논문·저수준 단일 실행에서 물리 N4→논리 N2 고정. S2~S5는 매 block6보드를 순환한 뒤 앞 S개 활성화. S6는 전체 회전. 모든 USB 보드의 serial을 확인하고 비참여 보드는 정지 유지 |
| 이미지 선택 | 논리 역할의 HEX를 해당 물리 serial에 연결. build-only·RTT·HEX/ELF hash 검증과 설정이 정확한 캐시 재사용 |
| 수집 metadata | serial/물리 위치/논리 ID/run/block/rotation/배정 hash/조건 hash/manifest/HEX/raw hash를 연결. 불일치·중복 metadata는 거부 |
| Stage0→본 실험 | 전체 PAC별 lead grid에서 후보 선택→후보/양옆 lead 반복→검증된 값과 증거를 새 manifest로 동결. 미선정 값을 임의로 채우지 않음 |
| 양쪽 배포 | payload만 새 원격 폴더에 배포·전체 hash 검증. 기존 목적지는 완전히 동일할 때만 재사용. 배포와 RF 실행 명령 구분 |
| 실행·재개 | TX READY→RX 순서, 실행 후 flash readback·inactive halt·원문 검증. 완료 case는 재검증 후 건너뜀. 제어/수집 실패는 보존·중단, 자동 재실행 없음 |
| 전체 단계 집계 | Stage0/Exp1 PER, Exp2 CIR 행/프레임/설정, Exp3 EXTTXE tick/ns·변형/PSDU, Exp5 raw CIR 프레임/300 samples, Exp4 송신 분모·각 슬롯·RDB/SPI/예약 카운터 검증 |
| 논문 판정 | 물리 serial별 offered/RX/PER·Wilson95 구간 및 run별 논리 역할/실패 목록. 합산 평균으로 개별 run의 실패를 숨기지 않음. 반복 누락·중복·혼합 환경/소스는 별도로 거부 |
| 용량·오염 실행 | paper 모드도 S6에서6슬롯→상한→중간 부하로 연결. 그 부하의 예정 반복을 채운 후 판정. 명시적 이유/index hash가 있는 오염 제외 지원 |

새 주요 도구는 `brrs_suite_paper.py`, `brrs_suite_evidence.py`, `brrs_suite_results.py`, `brrs_suite_leads.py`, `brrs_suite_campaign.py`다. 기존 manifest/case/capacity 및5개 캡처 엔진을 함께 연결했다. Stage0는 Exp1 엔진, Exp2는 v3 엔진을 경유하므로 두 wrapper까지 적용된다.

## 기본 논문 행렬

| 단계 | 기본 case 수 | 반복 설정 |
|---|---:|---|
| Stage0 grid | 82 | PAC2×lead41, 각1회 |
| Stage0 확인 | 최대30 | PAC별 후보·양옆3조건×5회, 실측 후 생성 |
| Exp1 | 40 | M4×PAC2×5회 |
| Exp2 | 24 | M4×PAC2×3회 |
| Exp3 | 9 | A/B/C×3회 |
| Exp4 | 696 | S1~S5 각8조건 + S6의18조건 =58조건×12회 |
| Exp5 | 3 | 고유 M1024/PAC32,3회 |

Exp4 S6: M32=6/12/13, M64=6/12, M128=6/10, M256=6/8슬롯. Exp4 12회는6회전×2주기, Exp5 3회는 변경 가능한 기본값으로 이번에 구성했다. 준비 모드는 여전히1회이며, 위 논문 행렬을 지금 실행한 것이 아니다. 필요한 case만 지정해 준비/검증할 수 있다. 별도 보조 프로토콜(tail100 대조, payload 경계 sweep, buffer 축소 등)은 이 기본 실행 행렬의 범위에 넣지 않았다.

## 오프라인 검증

- **48개 테스트 통과:** 새 논문 경로24개 + 기존 용량/설정 회귀24개. [새 테스트 로그](paper_tests.log), [회귀 로그](prior_regression.log). 역할/참여 균형, 전체 probe와 부분 활성화, 역할과 HEX 선택, PAC별 lead, 실제 원문의 회전 후 serial 집계, 개별 실패 보존, 누락/중복/오염 제외, CIR/EXTTXE 부정 사례, 배포 hash·덮어쓰기 거부, READY 이전 RX 차단, 결과 복사 실패, 완료 실행 재개를 확인했다.
- **S1/S2/S6의3개 bundle·이미지12개 확인:** S1 M128/PAC8에서 N4가 N2 HEX를 선택; S2 block2에서는 물리 N3/N4가 논리 N2/N3 HEX를 선택; S6 block2는 모든 논리 역할을 설치표 한 칸 순환. 실제 build-only와 ELF RTT/READY·HEX/ELF hash·설정 캐시 경로가 통과했다. 기존7개 이미지는 캐시, 새 S1/S2 이미지5개는 빌드했다. [이미지·매핑 증거](offline_verification.json).
- S6 `2345672345673`의1,000SF×12 block에서 각 물리 serial의 offered가 모두26,000으로 균등함을 계산으로 확인했다. 실제 송신 횟수가 아니다.
- 기존 M32/PAC4/13슬롯 raw를 **모의 제어 증거**로 감싼 회전 테스트에서 논리 N4의 PER7.15%가 block2의 물리 N5 serial1050208509로 정확히 이동했다. 기존 raw를 수정하거나 새 RF로 취급하지 않았다. 테스트 wrapper는 임시 폴더에만 생성했다.
- Python 구문·셸 구문, 모든 단계 계획, paper 용량의 다음 case 및 campaign dry-run을 확인했다. `TEST_ONLY_*` 파일의 lead25/17은 전달 경로 검증용이며 현장 최적값이나 실행 자료가 아니다.

## 실제 환경에서 남은 작업

1. 현재 SSH·RX1/TX6 연결과 전원, 실제 배치·환경 기록.
2. Stage0의 PAC별 lead 실측 및 후보 확인. 본 manifest의 lead는 미선정 상태로 유지했다.
3. 선정 조건에서 각 단계의 대표 RF·수집, 부분 활성화·회전 실동작 확인. 이후 사전 계획한 논문 반복과 각 보드 PER<1% 및 용량 판정.

기본 실험 셋의 남은 소프트웨어 연결은 완료했다. RF에서 아직 발견하지 못한 문제까지 해결됐다는 뜻은 아니다. 현재까지 새 실행기의 실제 장비 증거는 이전 Stage0 한 조건이며 이번 구현에서 이를 확대해 주장하지 않는다.

현장 명령과 결과 판정 절차는 [논문용 실행 안내](../../DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API/BRRS_PAPER_CAMPAIGN_KR.md)를 따른다. [변경 내역](source_changes.patch), [작업 전 상태](before.json), [작업 후 상태](after.json).
