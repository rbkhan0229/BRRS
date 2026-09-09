> 2026-09-08 후처리 완료: [종합 보고서](postprocess_20260908/REPORT.md), [전력 지표의 새 문제](postprocess_20260908/POWER_DIAGNOSTICS.md). TX6의 PER 판정은 유지된다. M32 RSSI 하한값과 FP power의 RX code 추출 버그를 발견해 품질 플래그·별도 보정 추정값을 제공했다. 원본·펌웨어·보드는 변경하지 않았다. 아래는 수집 직후 인계 기록이다.

# 9월8일 아침 차량 실행 준비

완료: 추가 Exp1 6조건·Exp2 7조건과 Exp4 27조건을 각각1회 실행했다. 이 중 TX6의 신규16조건은 모두 각 노드 PER<1%였으며, M32/PAC8/6슬롯의 N5가0.1%, 나머지는0%였다. 남은 소수TX28조건은 사용자 요청으로 생략했다. M32/13슬롯은 이전 실행 증거를 재사용했으므로 오늘 신규16조건에 포함되지 않는다. 추가 후처리는 나중으로 미뤘다.

오늘 밤 NLOS6.9m의 각 조건1회 점검 결과는 `RESULTS.md`와 `observations.json`에 모은다. 차량용 환경 기록 틀은 `vehicle_manifest_TEMPLATE.json`이며, 실제 차량 자료로 사용하기 전에 차종·설치 위치·차량 상태를 채워 새 manifest로 저장한다. NLOS 캡처 bundle을 그대로 재실행하면 NLOS metadata가 붙으므로 차량 측정에는 새 bundle을 만든다.

| 물리 라벨 | J-Link 일련번호 | 예정 차량 위치 |
|---|---|---|
| RX/INIT | 1050270933 | 프렁크 |
| N2 | 1050211584 | 범퍼 A |
| N3 | 1050273888 | 범퍼 B |
| N4 | 1050282818 | 운전석 |
| N5 | 1050208509 | 조수석 |
| N6 | 1050227627 | 트렁크 A |
| N7 | 1050204212 | 트렁크 B |

내연기관 차량을 사용하면 RX의 실제 엔진룸/앞쪽 설치 위치를 새로 기록한다. 프렁크와 같은 환경으로 합치지 않는다. 모든1:1 단계와 Exp4 S1은 물리N4를 사용하며 논리ID는N2다. 논문 반복의 역할 회전은 보드 위치·USB 배선을 유지한 채 설치표 기준으로 수행한다.

1. 설치 후 `ssh s-macbook-air`와 로컬RX1/원격TX6의 정확한 serial 집합을 확인한다. 별칭 실패 시 `100.115.225.85`를 사용한다. 허브 외부 전원, 차종, 문·창문·보닛·트렁크, 시동 상태, 방향·높이·배선을 기록한다.
2. 첫 차량 확인 후보는 M32/PAC8/lead26µs와 M32/PAC4/lead27µs다. NLOS에서 준비 점검용으로 고른 값이며 차량 최적값은 아니다. 동일 배치·같은 역할에서 각각1회 확인할 수 있다. 단일N4만 통과해도 전체6TX 통과로 간주하지 않는다.
3. 차량에서 사용할 PAC별 lead를 정한 뒤 Exp1·Exp2·Exp3·Exp4·Exp5에 manifest로 전달한다. 본 논문용 선정/확인 정책과 반복 수는 기존 `BRRS_PAPER_CAMPAIGN_KR.md`를 따른다. 오늘 밤 기능 점검의1회를 논문 반복 완료로 대체하지 않는다.
4. 모든 조건은 TX READY 확인 후 RX를 시작한다. 비참여TX는 정지하고, 끝나면 로그·metadata·HEX readback·노드별 offered/RX/PER를 검증한다. PER≥1%인 유효 실행도 보존하고 사전 계획한 다음 조건으로 진행한다. 사람 통행 등 교란은 원문을 남기고 해당 실행의 제외 사유를 기록한다.

## 단계와 기본 행렬

| 단계 | 차량 측정 내용 | 1회 길이 |
|---|---|---:|
| Stage0 | N4↔RX, M32, PAC4/8별 lead 선정 및6TX 확인 | 2,000 frame/단일 링크 조건 |
| Exp1 | M32/64/128/256 × PAC4/8, N4 링크 PER | 2,000 frame |
| Exp2 | 같은 M/PAC의 성공 패킷 CIR·FP-SNR, PER와 표본 수 동시 보고 | 1,000 attempt |
| Exp3 | A/B/C SFD·PHR airtime, PAC8 | EXTTXE1,000개 |
| Exp4 | S1~S6 및 TX6의 다중 슬롯 부하, PAC4/8·M4종 | 1,000 SF |
| Exp5 | M1024/PAC32 채널 측정 | 1,000 attempt, raw 최대30×300 |

Exp4는 G250/SB3000/SP2500/SF10ms, 슬롯별 bounded delayed-RX·SPI 경로를 사용한다. TX6의 슬롯 수는 M32=6/12/13, M64=6/12, M128=6/10, M256=6/8이다. 계산상 최대13/12/10/8슬롯과 실제 노드별 PER<1%를 만족하는 용량을 구분한다. 최대 슬롯이 실패하면 용량 도구가 중간 부하를 제시할 수 있지만, 오늘 밤의 기능 점검에서 추가 최적화는 하지 않는다.

## 실행 도구

작업 디렉터리는 `/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API`다. 소스·도구와 조건별 HEX는 로컬에서 준비하고 원격에는 같은 immutable bundle을 배포한다. 원격 기본 SDK에서 따로 빌드하지 않는다.

```bash
# 실제 설치 기록을 채운 새 파일. 아래 명령은 계획 확인이며 RF 없음.
python3 brrs_suite_manifest.py check /absolute/path/vehicle_manifest.json
python3 brrs_suite_manifest.py plan /absolute/path/vehicle_manifest.json --stage stage0 --profile paper

# 선정 lead를 반영한 manifest에서 필요한 case ID만 선택해 준비.
python3 brrs_suite_campaign.py prepare \
  --manifest /absolute/path/vehicle_frozen.json --stage exp4 --profile paper \
  --cases CASE_ID_FROM_PLAN --root /absolute/path/new_vehicle_campaign

# 보드 실행은 이 명령에서 시작됨. 완료 case는 재검증하고 건너뜀.
python3 brrs_suite_campaign.py run --root /absolute/path/new_vehicle_campaign
```

위 경로와 CASE_ID는 설명용 자리표시자다. 차량 설치 정보와 lead가 정해지면 planner가 출력한 정확한 경로·ID로 실행한다. 현재 템플릿은 lead를 미선정으로 두어 NLOS 준비값이 차량의 논문용 선정값으로 자동 기록되지 않게 했다.
