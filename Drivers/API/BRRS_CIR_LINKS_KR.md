# Exp2·Exp5의 물리 링크 6개 자동 측정

2026-09-09 구현 기준. Exp2·Exp5는 설치한 TX 6대 중 **한 대만 활성화**하여 RX와 통신하고, 다음 물리 TX로 넘어간다. Exp4의 실제 6TX TDMA와 구분한다. 보드 위치·방향·케이블은 그대로 두며, 이번 변경으로 무선 실험을 실행하지 않았다.

## 무엇을 바꿨는가

기존 단일 링크 펌웨어의 논리 N2 TX 이미지는 선택한 어느 물리 보드에도 사용할 수 있다. 따라서 무선 프레임이나 CIR 획득 C 코드를 변경하지 않고, 설정 계획→이미지 준비→대상 보드 제어→CIR 수집→물리 링크별 판정 경로를 확장했다. 새로운 링크 선택 때문에 PAC·lead·출력·가드·수신 방식이 임의로 바뀌지는 않는다.

| 실험 | 준비 | full | standard | essential | lite |
|---|---:|---:|---:|---:|---:|
| Exp2 | 48 | 144 | 72 | 72 | 24 |
| Exp5 | 6 | 18 | 18 | 18 | 6 |

Exp2의 full/준비는 M32/64/128/256 × PAC4/8 × 물리 링크6개를 사용한다.
Standard/essential/lite는 같은 링크와 PAC를 유지하고 M32/M256 끝점만 사용한다. Exp5의 조건
집합은 네 profile이 같고 반복 수만 다르다.

각 링크에서 활성 TX의 펌웨어 논리 ID와 raw의 `N2`는 동일하다. 실제 보드는 case ID의 `_txN2`~`_txN7`, 검증된 `physical_role`, serial, 위치로 식별한다. Exp2와 Exp5의 `ASSESSMENT.json`에는 `stage_metrics.physical_link`를 추가했다. 다른 물리 링크를 하나의 평균으로 합치지 않으며, 누락된 링크는 INCOMPLETE로 남는다. 수신 0은 PASS가 아니다.

| 물리 역할 | J-Link serial |
|---|---|
| INIT/RX | 1050270933 |
| N2 | 1050211584 |
| N3 | 1050273888 |
| N4 | 1050282818 |
| N5 | 1050208509 |
| N6 | 1050227627 |
| N7 | 1050204212 |

Stage0·Exp1·Exp3 및 Full/Essential/Lite Exp4 S1은 기존 N4 단일 링크를 유지한다.
Standard Exp4 S1만 여섯 block에서 N2~N7을 순회한다. Exp4 다중 TX의 논문용 역할
회전도 유지한다. `cir_link_tx_roles`가 없는 과거 manifest와 실행 bundle은 기존 N4 단일
링크로 해석하며 수정하지 않는다.

## 설정과 실행 방식

환경별 manifest에 다음 목록을 둔다. 새 공통 템플릿에는 이미 추가되어 있다.

```json
"cir_link_tx_roles": ["N2", "N3", "N4", "N5", "N6", "N7"]
```

준비 모드와 lite는 각 M/PAC 조건에서 N2→N7 순서로 한 번씩 측정한다. Full/standard/essential은 사전 계획한 반복별로 전체 조건 순서를 순환·정역 교대한다. 이는 Exp4의 논리 슬롯 역할 회전과 별개다. `--cases`로 계획에 있는 일부 case만 선택할 수 있다.

실행기는 매 case에서 다음을 수행한다.

1. 배포 파일 hash와 실제 연결된 일곱 serial을 확인한다.
2. 보드를 정지하고 선택한 TX와 RX에 해당 case의 준비된 이미지만 사용한다. 비참여 5대는 플래시하지 않는다.
3. TX의 READY를 확인한 후 RX를 시작한다.
4. 종료 마커·raw·metadata·flash readback·비참여 TX 정지를 검증한다.
5. 실제 물리 링크의 PER/CIR 결과를 저장한 뒤 다음 계획 case로 넘어간다.

수집이 유효한 PER 실패는 결과로 보존하고 다음 조건을 진행한다. 제어/수집 실패는 중단하며 자동 재시도하지 않는다. 재개할 때 완료 case는 원문 증거를 다시 확인한 뒤 건너뛴다. 동일 case에 결과를 덮어쓰지 않는다.

기존에도 고정 조건표는 실행기가 자동 순회할 수 있었다. 현장에서는 사용자가 요청한 한 조건만 실행하거나, 결과를 보고 다음 lead·PAC·배치를 결정하는 작업을 Codex가 했다. 이번 변경으로 Exp2·Exp5의 **물리 TX 선택과 링크 순회도 사전 계획에 포함**된다. 조건 변경이 필요하면 새 manifest와 새 계획으로 기록한다.

## 명령 예시

아래는 API 폴더에서 실행한다. `vehicle_frozen.json`은 **실제 배치와 PAC별 선정 근거를 기록한 환경별 파일**이다. 기본 템플릿의 lead는 아직 미선정이며, TEST_ONLY 문서나 빌드 검증용 lead를 차량 최적값으로 사용하지 않는다. Exp5의 lead는 기존 정책대로 선정된 PAC8 값을 참조하며 PAC32 최적값을 탐색한 것은 아니다.

```bash
# 계획 출력만 한다. SSH/보드 접근 없음.
python3 brrs_suite_manifest.py plan vehicle_frozen.json --stage exp2 --profile preparation
python3 brrs_suite_manifest.py plan vehicle_frozen.json --stage exp5 --profile preparation

# 예정 case 목록만 확인. 빌드/RF 없음.
python3 brrs_suite_campaign.py prepare --manifest vehicle_frozen.json \
  --stage exp2 --profile preparation --root /private/tmp/vehicle_exp2_links --dry-run

# 로컬 이미지와 독립 실행 묶음 준비만 한다. 보드 접근 없음.
python3 brrs_suite_campaign.py prepare --manifest vehicle_frozen.json \
  --stage exp2 --profile preparation --root /private/tmp/vehicle_exp2_links

# 실제 실험 시에만 실행한다. 이 명령은 플래시와 RF 송수신을 시작한다.
python3 brrs_suite_campaign.py run --root /private/tmp/vehicle_exp2_links
```

Exp5는 `--stage exp5`와 새 root를 사용한다. Profile은 범위가 큰 순서로 `full > standard > essential > lite`이며, 차량 본 실험은 `--profile standard`, 빠른 점검은 lite를 사용한다. 개별 저수준 실행이 필요한 경우 `brrs_run_experiment.sh exp2 tx ... --physical-tx-role N7`처럼 물리 TX를 명시할 수 있다. 이 명령은 한쪽 보드용이며, 7대 정지/READY 순서를 함께 제어하려면 위 campaign 경로를 사용한다.

## 호스트 구성

- RX는 현재 컴퓨터, TX 6대는 맥북에어: INIT의 `host=local`, N2~N7의 `host=s-macbook-air`.
- RX까지 일곱 보드가 모두 맥북에어: 일곱 `host`를 모두 `s-macbook-air`로 지정. 같은 campaign 명령이 원격 단일 호스트 수집기를 실행하고 결과를 가져온다.
- 일곱 보드가 모두 실행 컴퓨터에 직접 연결: 모든 `host=local`. SSH 없이 로컬 단일 호스트 수집기를 실행한다.

SSH 별칭 대신 같은 노트북의 IP로 접속해야 할 때는 campaign `run`에 `--host 100.115.225.85`를 붙일 수 있다. probe serial은 별도로 확인한다. 전체 campaign의 원격 단일 호스트 경로는 순차 SSH 실행이므로 campaign이 도는 동안 제어 컴퓨터 연결을 유지한다. 별도 `brrs_single_host.py start`는 준비된 **개별 case 한 개**의 분리 실행 기능이며 전체 campaign 분리 실행을 뜻하지 않는다. 현재 campaign 경로는 Stage0~Exp5를 한 호스트에서 실행할 수 있다.

이미 준비된 bundle은 당시 도구의 고정 사본이다. 새 기능을 사용하려면 현재 API에서 새 bundle을 준비한다. 기존 실험 bundle을 직접 수정하지 않는다.

## 검증 범위

자동 계획, 역할/serial 구분, 물리 링크별 집계, 원문/metadata 불일치 거부, 비참여 TX 정지 실패, 단일 호스트 제어 흐름, 과거 N4 bundle 호환성을 오프라인으로 검사한다. Exp2/Exp5에서 N2와 N7을 선택한 네 개의 build-only bundle을 준비한다. 합성 제어 증거를 사용한 테스트는 실제 RF 결과와 구분한다. 실장비에서 새 링크 순회 경로를 확인하는 작업은 실험 재개 후 남아 있다.

기존의 수신 성공 프레임에 한정된 CIR 표본 선택과 FP 전력 계산의 알려진 후처리 문제는 이번 제어 경로 변경으로 해결되지 않는다. 이 자료를 수신 실패 시점의 CIR로 해석하거나 Exp2/Exp5만으로 Exp4 다중 TX 성능을 대체하지 않는다.
