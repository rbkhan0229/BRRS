# 차량 준비용 고정 역할과 단계별 설정

공통 설정은 `brrs_vehicle_manifest.json`이다. 물리 RX는 1050270933, 모든 1:1 TX는 **N4 1050282818**이다. 단일 링크 펌웨어의 논리 ID는 N2이므로 metadata의 `physical_role`과 `logical_node`를 구분한다. Exp4는 N2~N7의 원래 serial 매핑을 유지한다.

이 사본의 통합 런너와 auto-TX는 기본적으로 위 manifest를 사용한다. USB 열거 순서나 run 번호에 따라 역할을 바꾸지 않는다. 지정 `--serial`이 manifest와 다르면 실행 전에 거부한다. auto-TX는 선택한 보드가 정확히 연결되었는지 확인하며, 모든 TX가 연결된 상태의 부분집합 제어는 별도 실행기에서 관리한다.

## 계획 확인

```bash
python3 brrs_suite_manifest.py check brrs_vehicle_manifest.json
python3 brrs_suite_manifest.py plan brrs_vehicle_manifest.json --stage stage0
```

`plan`은 JSON 명령 계획만 출력한다. 보드에 접속하거나 플래시하지 않는다. 각 case에는 조건 hash, 각 보드의 serial/물리 역할/논리 ID, 실제 캡처 명령 `argv`, `build_only_argv`, 실행 시 함께 적용할 `environment`, 비참여 TX 목록이 있다. `environment`의 case/manifest 식별자는 캡처 metadata에 기록된다. 조건 hash는 보드 flash readback의 대체물이 아니다.

Stage0는 원래 lead 0~40us의 41점 순서와 PAC4/8을 유지한다. `lead_selection`은 아직 미선정 상태다. Stage0 결과를 근거로 `lead_us_by_pac`와 `evidence`를 채우고 `frozen`을 true로 바꾼 뒤에야 나머지 단계의 계획이 생성된다. 본 실험용 lead를 임의로 정하지 않는다.

- Exp1/Exp2: 각각 PAC4+lead4와 PAC8+lead8, M32/64/128/256.
- Exp2: PAC는 CLI→빌드 정의→캐시 확인→로그 파일명→펌웨어 설정 마커→검증→metadata로 전달된다. 새 설정 마커가 없는 이전 Exp2 이미지는 새 검증 경로의 준비 이미지로 간주하지 않는다.
- Exp3: A/B/C의 SFD·PHR 차이를 유지하고 PAC8, lead는 동결된 PAC8 값을 사용한다.
- Exp4: M32/64/128/256을 비교하며 물리 TX6과 논리 슬롯 수를 별도로 둔다. G250/SB3000/SP2500/SF10ms에서 모델상 최대는 M32=13, M64=12, M128=10, M256=8이다. 기본 계획은 각 M·PAC의 6슬롯과 해당 상한, 총16조건이다. 이것은 최대 부하 RF 검증 완료를 뜻하지 않는다. M32 13슬롯 배정은 기존 `2345672345673`을 유지한다.
- Exp4: 슬롯별 bounded delayed-RX·SPI 최적화를 빌드와 검증에 함께 전달한다. PAC 비교는 INIT DATA RX에만 적용하며 TX DATA PAC 정의는 8로 고정하여 PAC 조건 사이 TX 이미지가 달라지지 않게 했다.
- Exp5: M1024/PAC32를 유지한다. lead 참조는 Stage0의 동결 PAC8 값이며, 이를 PAC32의 최적 lead를 측정했다는 뜻으로 해석하지 않는다.

## 빌드만 확인

단계별 캡처 도구와 통합 런너에 `--build-only`를 붙이면 이미지/RTT 주소 확인 후 J-Link 접속 전에 끝난다. `--no-build`와 함께 쓰면 같은 설정의 기존 이미지를 확인한다. auto-TX는 하드웨어 실행기이므로 build-only를 거부하며, 빌드는 N2~N7의 명시적 역할로 수행한다.

```bash
bash brrs_run_experiment.sh exp4 N4 build_check \
  --sensors 6 --preambles 32 --guard 250 --lead 25 --pac 8 \
  --slotted-rx --spi-opt --build-only --no-build
```

위 25us는 기존 이미지의 전달 경로 확인 예시이며 선정된 최적 lead가 아니다. 새 PHY 탐색 조건은 기존 이미지가 없으면 `--no-build`를 사용하지 않는다.

## 판정과 다음 작업

RTT 수집기는 READY/END 각 1개를 요구한다. Exp4 검증기는 슬롯별 arm/종료 카운터·수신창·FWTO와 노드별 집계를 확인한다. `collection=PASS`와 `per_node_goal=PASS/FAIL_PER`는 별도이며, 목표는 모든 TX PER<1%다. 전체 수신 0은 Exp4 통과가 아니다. `--max-per-percent`는 기존 전체 PER 제한이며 노드별 목표를 대체하지 않는다.

## case 준비와 양쪽 실행

`brrs_suite_case.py prepare --manifest brrs_vehicle_manifest.json --stage stage0 --case stage0_m32_pac8_l25 --reuse --bundle <새-실행-폴더>`는 기존 캡처 CLI의 build-only로 이미지를 확인하고, 모든 단계 도구와 해당 case의 HEX/ELF/설정 stamp를 독립 capture-only SDK로 묶는다. `--reuse`는 동일 조건의 검증된 이미지가 있을 때만 쓴다. 새로운 조건의 빌드는 원본 Git이 아닌 현재 fix 사본에서 수행한다. prepare는 보드에 접속하지 않는다.

생성된 폴더를 원격 노트북의 동일 절대 경로에 새로 배포한 뒤, 그 폴더 안 `sdk/Drivers/API/brrs_suite_case.py run --bundle <실행-폴더>`를 로컬에서 실행한다. 원격 경로를 덮어쓰지 않는다. 양쪽 파일 hash와 payload index가 다르거나 지정 probe set이 다르면 보드 실행 전에 거부한다. 각 단계의 원래 캡처·검증 도구를 `--no-build`로 호출하므로 원격 빌드에 의한 이미지 차이가 생기지 않는다.

실행기는 INIT를 먼저 정지하고, 원격 보드를 정지한 뒤 지정 TX들의 READY를 확인해야 RX를 시작한다. J-Link의 기본 close 동작은 CPU를 재개하므로, 제어 연결은 `SetRestartOnClose=0`을 명시한다. close/reconnect 후의 정지와 실행 종료 후 비참여 TX 정지를 모두 검증한다. 물리 위치나 케이블을 바꾸거나 비참여 보드를 플래시하지 않는다. 오류 실행은 보존하고 현재 case의 worker만 종료하며 무제한 자동 재시도는 하지 않는다.

2026-09-07 실제 N4↔RX Stage0 M32/PAC8/lead25 2,000회에서 무손실, TX5 정지 유지, READY/END와 metadata, flash readback까지 통과했다. 최초 실행의 비참여 TX 제어 실패는 보존·제외하고 수정 후 한 번 검증했다. 근거는 `logs/vehicle_suite_rf_20260907/RESULTS.md`다. 전체 Stage0 탐색 및 나머지 단계의 새 실행기 RF 검증이 끝났다는 뜻은 아니다.

`run`의 완료 상태 `COLLECTION_AND_READBACK_PASS_PER_PENDING`는 수집·배포·제어 무결성에 대한 결과다. 실제 offered/TX/RX를 합쳐 노드별 PER 판정을 별도로 저장해야 한다. 이번 Stage0 합산 도구는 `logs/vehicle_suite_rf_20260907/assess_runs.py`이며 수신0 또는 제어 실패는 INVALID, 정확히1%는 FAIL_PER다. Exp4는 아래 도구로 노드별 결과와 전체 TX 송신량을 확인한다. 모의 자료와 제어 실패 자료를 유효 RF 결과에 합산하지 않는다.

## Exp4 심볼별 용량 탐색

현재 고정 역할·조건당1회 준비 점검에 사용하는 절차다. PAC별 lead를 선정·동결한 동일 manifest와 완료된 case bundle을 사용한다. PHY·위치·역할·guard·SB/SP를 바꾸지 않고 슬롯 부하만 낮춘다.

| M | 공통 기준 슬롯 | 계산상 최대 슬롯/SF | 최대 offered reports/s | 무손실 app goodput 상한 |
|---:|---:|---:|---:|---:|
| 32 | 6 | 13 | 1,300 | 166.4 kbps |
| 64 | 6 | 12 | 1,200 | 153.6 kbps |
| 128 | 6 | 10 | 1,000 | 128.0 kbps |
| 256 | 6 | 8 | 800 | 102.4 kbps |

위 전송률은 SF10ms·app16B의 계산값이며 실제 goodput이나 독립 TX 수를 뜻하지 않는다. 물리 TX는 최대6대이며 한 보드의 여러 슬롯은 각각 새 보고 기회다.

```bash
# 조건만 출력. flash/RF 없음. 본 manifest의 lead 동결 전에는 거부됨.
python3 brrs_suite_manifest.py plan brrs_vehicle_manifest.json --stage exp4
python3 brrs_suite_manifest.py plan brrs_vehicle_manifest.json --stage exp4 --capacity-candidates

# 처음에는 각 M/PAC의 6슬롯 기준을 제시한다.
python3 brrs_exp4_capacity.py brrs_vehicle_manifest.json

# 이미 완료한 bundle들을 모두 입력하면 결과와 다음 case ID를 출력한다.
python3 brrs_exp4_capacity.py brrs_vehicle_manifest.json --bundles <완료-bundle-1> <완료-bundle-2>
```

기준6슬롯을 기록한 뒤 최대 슬롯을 검사한다. 최대가 유효 수집이지만 노드별 PER에서 실패하면 `최대-1, 최대-2, …, 7` 순서로 다음 조건을 제시한다. M128은6→10→9→8→7이다. 더 큰 후보를 모두 검사한 상태에서 통과한 가장 큰 슬롯 수를 찾으면 아래 부하는 생략한다. PER이 슬롯 수에 단조롭게 변한다고 가정하지 않으므로 기준6슬롯이 실패해도 최대 조건은 별도로 확인한다. 모든 조건이 실패하면 ‘6TX에서 검사한 범위 내 통과 없음’이며 5TX 이하의 결과를 추정하지 않는다.

`--capacity-candidates`는 총46개의 **조건부 후보 목록**이다. 전부 반복 실행하라는 뜻이 아니다. 다음 case는 `brrs_suite_case.py prepare --manifest ... --stage exp4 --case <제시된-ID> --bundle <새-폴더>`로 기존 준비·양쪽 실행 경로에 연결된다. 중간 슬롯 수 case도 선택할 수 있으며, 정확한 이미지가 있을 때만 `--reuse`를 붙인다.

용량 도구는 보드에 접속하지 않는다. bundle/payload hash, 실행 상태, serial·역할·조건·manifest·HEX·raw metadata, READY/END, 기존 INIT/TX 검증과 송신 분모를 다시 확인한다. 각 물리 serial의 offered/RX/PER 및 TX beacon/attempt/success, 전체 전송률·app goodput을 출력한다. offered에는 beacon 미수신으로 송신하지 못한 기회도 포함한다. 어느 보드든 PER≥1%면 FAIL_PER, 전체 수신0·수집/제어 오류·증거 불일치는 INVALID로 중단한다. 동일 case의 여러 실행을 넣어 좋은 결과만 고르는 것도 거부한다.

`SCREENING_MAX_FOUND`는 이 고정 배치에서 조건당1회로 확인한 **준비 점검 상한**이다. 준비 모드의 단일 실행 값은 논문용 최종 용량이 아니다. `brrs_exp4_capacity.py --profile paper`는 사전 계획한 반복·회전의 완료 여부와 노드별 결과를 합쳐 별도로 판정한다. 후속 구현에서 `--profile paper`의 반복·회전·S1~S6 선택·serial 집계를 연결했다. [논문용 실행 안내](BRRS_PAPER_CAMPAIGN_KR.md)를 따른다. PAC별 lead 미선정 상태도 유지한다. M128의 lead25·10슬롯 PAC4/PAC8 이미지와 준비/캡처 build-only 경로를 오프라인으로 검증했으며, 그 진단값을 최적 lead로 선정하거나 새 RF 결과로 사용하지 않는다. 근거는 `logs/vehicle_capacity_fix_20260907/RESULTS.md`다.
