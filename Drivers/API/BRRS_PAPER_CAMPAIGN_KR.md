# Stage0~Exp5 논문용 실행 경로

이 문서는 `DW3_QM33_SDK_1.0.2/Drivers/API`의 구현 기준이다. 준비 점검은 `--profile preparation`으로 고정 역할·1회, 논문용 측정은 `--profile paper`로 사전 계획한 반복·회전을 사용한다. **plan/prepare/dry-run/결과 집계는 RF를 시작하지 않는다. 실제 송수신은 `run`에서만 시작한다.** `deploy`는 원격 파일 배포만 한다.

## 환경별 manifest

`brrs_vehicle_manifest.json`을 환경별 새 파일로 복사한다. `environment`, `distance_m`, 각 board의 `location`을 실제 NLOS/차량 배치에 맞게 기록한다. 템플릿의 frunk/bumper 등은 차량 예정 위치이며 NLOS 실측 위치로 사용하지 않는다. 허브 외부 전원·USB 구성·차폐·방향·높이·차량 상태는 같은 manifest에 `setup_record` 객체로 추가할 수 있다. 이 정보도 환경 hash에 포함되어 다른 환경 결과의 합산을 막는다. serial은 설치표를 유지한다. 새 환경에서는 `lead_selection`을 미선정으로 시작한다.

실험의 논리 역할과 물리 보드는 구분한다. RX는 1050270933으로 고정한다. Stage0/Exp1/Exp3 및 Exp4 S1은 물리 N4(1050282818)가 논리 N2를 맡는다. Exp2/Exp5는 `cir_link_tx_roles`에 따라 물리 N2~N7을 한 대씩 활성화하여 각 링크를 측정한다. 활성 TX의 논리 ID는 N2이며 나머지 5대는 USB를 뽑지 않고 실행기가 정지 상태를 유지·검증한다. 이 필드가 없는 과거 manifest는 기존 N4 단일 링크와 case ID를 유지한다. [링크 순회 실행 안내](BRRS_CIR_LINKS_KR.md).

## 반복과 활성 집합

`paper.repeats_by_stage`에 Stage0=1, Exp1=5, Exp2=3, Exp3=3, Exp4=12, Exp5=3을 설정했다. Exp4=12는6회전×2주기, Exp5=3은 이번 실행 기본값으로 채택한 구성값이며 변경 가능하다. Exp4 반복은 균형을 위해6의 배수로 제한한다. Stage0 후보와 양옆 lead 확인은 `stage0_confirmation_repeats=5`다. 준비 모드의 기존1회 정책은 유지한다.

- Exp2: M 4개 × PAC 2개 × 물리 링크 6개 = 준비 모드 48 case, 논문 모드 3회씩 144 case. Exp5: 물리 링크 6개 = 준비 모드 6 case, 논문 모드 3회씩 18 case. 준비 순서는 각 PHY 조건에서 N2→N7이며 논문 모드는 반복별 조건 순서를 순환·정역 교대한다. case ID의 `_txN2`~`_txN7`과 metadata의 실제 serial로 구분한다.
- Exp4 S1: N4만 활성, 모든 반복에서 논리 N2.
- S2~S5: 설치표 순서 N2/N3/N4/N5/N6/N7을 반복 block마다 한 칸 순환하고 앞 S개를 활성화한다. 활성 보드에 논리 N2~N(S+1)를 배정한다. 6 block마다 모든 물리 보드가 각 논리 역할을 한 번씩 맡는다.
- S6: 같은 순환을 전체6보드에 적용한다. block1은 원래 설치표 매핑이다. serial 숫자 정렬은 사용하지 않는다.
- 같은 block의 PAC·M·슬롯 조건에는 같은 배정을 사용한다. 조건 실행 순서만 별도로 순환·정역 교대한다.
- 모든 case의 물리 serial/위치, 논리 ID, 반복 번호, 회전 index, 조건/배정 hash, HEX/ELF hash를 보존한다. 캡처 로그 경로도 case ID로 나뉜다.

S1~S5는 노드당1슬롯이다. S6는 M32의6/12/13, M64의6/12, M128의6/10, M256의6/8슬롯을 비교한다. PAC2개를 적용하면 Exp4의 기본 물리/부하 조건은58개이며12회 반복 시696 case다. 이는 전체 논문 계획의 개수이며, 준비 점검을696회 돌린다는 뜻이 아니다. `--cases`로 필요한 준비·검증 조건을 명시적으로 선택할 수 있다.

## 계획과 이미지 준비

아래 예시는 현재 디렉터리가 API이며 환경별 파일명이 `nlos_manifest.json`인 경우다.

```bash
python3 brrs_suite_manifest.py check nlos_manifest.json
python3 brrs_suite_manifest.py plan nlos_manifest.json --stage stage0 --profile paper
python3 brrs_suite_campaign.py prepare --manifest nlos_manifest.json \
  --stage stage0 --profile paper --root /private/tmp/nlos_stage0 --dry-run
```

`prepare`에서 `--dry-run`을 빼면 로컬에서 이미지와 독립 case bundle만 준비한다. 설정이 일치하는 캐시는 먼저 확인해 재사용하며, 맞지 않으면 source build를 한다. `--reuse`는 새 빌드를 허용하지 않고 정확한 캐시가 없으면 실패한다. 전체 조건을 준비하기 전에 `--cases <case-ID ...>`로 원하는 조건만 선택할 수 있다. 실패한 준비 폴더를 덮어쓰지 않는다.

```bash
# 아래 ID의 숫자는 예시다. 실제 선정 lead와 planner가 출력한 ID를 사용한다.
python3 brrs_suite_case.py prepare --manifest nlos_manifest.json --stage exp4 \
  --profile paper --case paper_exp4_m128_pac8_l25_k10_s6_b02 \
  --bundle /private/tmp/nlos_selected_case
```

새 bundle은 모든 도구와 해당 case 이미지, manifest와 펌웨어 C 출처 hash를 담는다. 사용하던 Git 작업 트리를 변경하지 않는다. TEST_ONLY manifest/bundle은 현장 실행에 사용하지 않는다.

## 배포·실행·재개

```bash
# dry-run은 SSH/J-Link를 호출하지 않는다.
python3 brrs_suite_campaign.py run --root /private/tmp/nlos_stage0 --dry-run

# 실제 실험할 때 실행: case별 동일 파일 배포 → probe set → TX READY → RX → 수집/검증/집계
python3 brrs_suite_campaign.py run --root /private/tmp/nlos_stage0
```

기본 SSH 주소는 manifest의 `s-macbook-air`다. 별칭 접속이 안 되면 `--host 100.115.225.85`로 같은 경로를 사용할 수 있다. 어떤 주소로 접속하든 실제 probe serial 집합은 정확히 대조한다. 배포는 새 목적지에만 쓰거나 기존의 완전히 동일한 payload를 확인한다. 다른 파일을 덮어쓰거나 삭제하지 않는다. 원격 파일 전체 hash 확인 후에만 board 실행으로 넘어간다.

완료된 case는 원문과 제어 증거를 재검증하고 건너뛴다. 수집이 유효하되 PER가 높은 case는 실패 결과로 보존하면서 사전 계획한 다음 비교를 진행한다. 제어·수집 실패, 해시·역할 불일치 또는 불완전한 결과가 있으면 중단하며 자동 재실행하지 않는다. TX READY 전에 RX를 시작하지 않고, 비참여 TX의 J-Link close 후 정지 유지도 확인한다. 결과 복사 실패 역시 실행 실패로 기록한다.

Exp2/Exp5는 RX까지 원격 노트북에 연결한 단일 호스트 구성도 같은 campaign 명령으로 실행한다. 이때 manifest의 일곱 `host`를 모두 같은 노트북 주소로 기록한다. 실행하는 컴퓨터에 일곱 보드를 직접 연결했다면 모두 `local`이며 SSH 없이 실행한다. 다른 단계의 단일 호스트 지원을 의미하지는 않는다.

단일 bundle은 `brrs_suite_campaign.py deploy --bundle <폴더>` 후 그 bundle의 `sdk/Drivers/API/brrs_suite_case.py run --bundle <폴더>`로 실행할 수도 있다. 전체 준비 목록은 `campaign.json`, 진행 결과는 `progress.json`, 각 case는 `results/ASSESSMENT.json`, 완료 집계는 `RESULTS.json`이다.

## PAC별 lead 선정 연결

```bash
# Stage0 전체 grid의 실제 완료 bundle 목록을 입력한다.
python3 brrs_suite_leads.py candidates nlos_manifest.json \
  --bundles <Stage0-grid-bundle들> --output-manifest nlos_candidates.json

python3 brrs_suite_manifest.py plan nlos_candidates.json --stage stage0 --profile paper --confirmation
python3 brrs_suite_campaign.py prepare --manifest nlos_candidates.json \
  --stage stage0 --profile paper --confirmation --root /private/tmp/nlos_confirmation

# 확인 측정을 마친 뒤 해당 bundle 목록으로 동결한다.
python3 brrs_suite_leads.py freeze nlos_candidates.json \
  --bundles <Stage0-confirmation-bundle들> --output-manifest nlos_frozen.json
```

후보 선택은 PAC별 전체0~40us grid가 완전하고 유효해야 한다. 자신과 양옆 lead에서 모두 PER<1%인 후보 중 주변 최악 PER, 주변 합산 PER 순으로 고르며 동률은20us와 가까운 값, 작은 lead 순이다. 후보·양옆을 각5회 확인한 뒤 모든 run이 PER<1%이고 각 조건의 합산 Wilson95 상한도1% 미만일 때만 새 manifest를 동결한다. 기준 미달·누락·수집 실패를 임의 lead로 대체하지 않는다. RX0인 Stage0 지점은 전이 자료로 남지만 PASS가 아니다.

이 선택 규칙은 이번 구현의 명시적 기본 정책이다. packet-level Wilson 구간은 손실 상관을 반영한 독립 run 분석을 대체하지 않으므로 run별 결과를 함께 남긴다. NLOS에서 고른 lead를 차량의 검증된 최적값으로 취급하지 않는다. 동결 이후 Exp1~Exp5는 PAC별 값을 자동 전달한다. Exp5는 고유 M1024/PAC32를 유지하며 lead만 PAC8 선정값을 참조한다.

## 집계와 용량

```bash
python3 brrs_suite_results.py nlos_frozen.json --stage exp4 --profile paper --bundles <완료-bundle들>
python3 brrs_exp4_capacity.py nlos_frozen.json --profile paper --bundles <S6-용량-bundle들>
```

집계는 물리 serial을 기준으로 하며 논리 역할·슬롯·run 정보를 보존한다. 노드별 offered에는 beacon 미수신으로 송신하지 못한 기회도 포함된다. 어느 한 보드·run이라도 PER≥1%면 조건은 FAIL_PER다. 합산 PER·Wilson95 구간과 해당 실패 목록을 함께 보여준다. 누락은 INCOMPLETE, 제어/수집·PHY 설정·원문 불일치는 INVALID다. 수신0인 Exp1~Exp5는 성공으로 처리하지 않는다. Exp2의 CIR 표본, Exp3의 EXTTXE tick/ns, Exp5의30프레임 상한·프레임당300 raw samples까지 재검증한다.

S6 용량 도구는 논문 모드에서도 기본6슬롯→계산상 최대→중간 부하로 연결된다. 해당 부하의 예정 반복을 모두 채운 뒤 판정하며, 더 높은 후보가 남아 있으면 최대라고 확정하지 않는다. 이 결과는 측정한 환경/설정의 실측 상한이고 독립 TX13대의 검증이 아니다.

사람 통행 등 오염 실행은 raw를 보존하고 `--exclusions <JSON>`으로 명시적으로 제외한다. JSON은 `{ "bundle의 절대 경로": {"reason":"사용자가 알린 교란 내용", "payload_index_sha256":"해당 index SHA256"} }` 형식이다. 정상 PER 실패를 지우고 좋은 실행만 선택하는 용도로 쓰지 않는다. 대체 실행이 필요하면 같은 case ID의 새 bundle을 별도 폴더에 준비하고 원본과 함께 집계하되 원본 제외 근거를 남긴다. 동일 case가 둘 다 유효 입력으로 들어오면 거부한다.

## 구현 검증과 현장 작업

2026-09-07 밤 현재 NLOS 6.9m에서 Stage0 M32/PAC8, Exp1 M64/PAC8, Exp2 M32/PAC8, Exp3 A/B/C, Exp5 M1024/PAC32를 대표 조건별 1회씩 실제 실행했고 모두 PER0%·수집·flash readback을 통과했다. 직전 Exp4 M32/PAC8/S6/13슬롯은 최악 노드0.70%였다. lead25는 이 기능 점검의 고정값이며 PAC별 최적값 선정은 아니다. Exp1·Exp3 후처리 분석기의 현재 로그 호환도 보완했다. [대표 실측·후처리 결과](../../../logs/vehicle_suite_nlos69_smoke_20260907_2301/RESULTS.md). 당시 미확인 항목 중 부분 활성·회전 실장비 경로는 아래 후속 측정으로 확인했다. 전체 lead/논문 반복 및 차량 RF는 별도다.

논문 반복·회전·부분 활성화·metadata·이미지 선택·배포·재개·Stage0 선정·전 단계 원문 검증·serial 집계는 구현했다. 앞선 오프라인 구현과 이미지 준비 결과는 `logs/vehicle_paper_impl_20260907/RESULTS.md`, 이후 대표 RF 검증은 위 실측 보고서에 구분해 기록한다. 현재 남은 실측 범위는 아래 환경 변경 가능성을 반영한 PAC별 선정 확인, 전체 조건·회전·논문 반복 및 실제 차량 배치다.

2026-09-07~08 후속으로 Stage0 빠른 탐색과 Exp4 S2/block2·S6/13슬롯/block2의 실제 역할 회전을 실행했다. 단일N4에서 통과한 lead20은 약한 링크N6/N7을 포함한6TX에서 실패했다. **단일 링크 Stage0 선정을 전체 네트워크의 검증된 lead로 취급하면 안 된다.** PAC8은25µs 최악0.55%, 26·27µs 모두0%로 낮은 손실 구간을 확인했다. PAC4의 이전 최저27µs/N7 1.00%도 strict<1% 실패이며, 전선 가림 보고 후 같은 조건1회는6.8%였다. 가림 대상/시점은 불명이므로 이전 자료를 임의 제외하거나 재측정과 합산하지 않았다. 추가 탐색은 사용자 요청에 따라 중단했고 공통 manifest는 논문용 선정 완료로 변경하지 않았다. [전체 실측·후보 및 한계](../../../logs/nlos69_lead_screen_rotation_20260907_2326/RESULTS.md), [마지막 PAC4 재측정](../../../logs/exp4_pac4_lead27_cable_recheck_20260908_0018/RESULTS.md).
