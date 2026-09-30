# BRRS Standard v2 — 실험 계획과 결과 열람 설계

작성일: 2026-09-25 · 개정: 2026-09-27 · 상태: 논문 및 실험 **설계안**. [기존 Standard](BRRS_EXPERIMENT_STANDARD_KR.md), 현재 실행기·매니페스트·원본 로그·공식 판정·STOP은 그대로다. 이 문서의 최대 906케이스는 아직 실행기가 생성하는 목록이 아니다.

## 1. 연구 순서와 공통 조건

매니페스트는 보드 serial/위치, 환경, PHY, lead, 반복과 슬롯 수를 실행기와 검증기가 읽는 JSON 조건표다. 현재 매니페스트는 기존 [1,593케이스 계획](../../../logs/final_vehicle_preparation_20260922_222350/FINAL_STANDARD_PLAN_KR.md)을 담고 있다.

1. 기존 데이터의 원본·정정·환경·유효성을 확인한다. 집에서 추가 실험을 하고 표·그림·원고를 `HOME_PROVISIONAL`로 완성한다. 실제로 배치한 링크만 측정 완료로 기록한다.
2. 최종 차량의 설치 상태를 새로 기록하고 7대 점검·실기 qualification·Stage0부터 수행한다. 같은 조건표의 데이터를 `VEHICLE_FINAL`로 다시 얻는다.
3. `실험 / M / PAC / 물리 링크 또는 활성 TX 수 / application K / 반복 block / 산식`이 같은 논문 칸을 차량 값으로 1:1 교체한다. 원본을 덮어쓰지 않고 표·그림·통계·초록·결론을 다시 계산한다. 차량 미측정 칸은 `NOT_MEASURED`로 둔다.

CH5, 비컨 M512, 10,000 µs/superframe, 1,000 superframe/run, application payload 16 B, PSDU 26 B. Exp4는 guard 200 µs, SB 1,700 µs, SP 2,200 µs, 슬롯별 delayed RX, persistent SPIM, fast PHY 전환 ON, PGF calibration 유지. Exp4 물리 TX 역할은 6개 보드의 전체 회전을 12블록 동안 두 번 수행한다. lead는 집과 차량에서 각각 선정하므로 1:1 비교 키에 lead 숫자를 포함하지 않되 기록에는 반드시 남긴다.

| 단계 | 조건 | 기본 case |
|---|---|---:|
| Stage0 | M32/PAC4·M32/PAC8·M1024/PAC32 각각 lead 0–40 µs | 123 |
| Stage0 confirmation | 위 3설정 선정 lead 각각 3회(각 2,000프레임) | 9 |
| 긴 PHY lead 확인 | M64/M128/M256 PAC8의 조건부 국소 탐색·6링크 확인(현재 실행 목록 아님) | 최대 계획 81 |
| Exp2 | M32/PAC4·8 + M64/128/256/PAC8 ×6링크×3회 | 90 |
| Exp3 | A/B/C 각 3회, 반복마다 짝 비교 | 9 |
| Exp4 S1–S5 | M32/PAC8·M256/PAC8 ×S1–S5×12블록 | 120 |
| Exp4 S6 | 위 2 PHY × application K6–24×12블록 | 456 |
| Exp5 | M1024/PAC32×6링크×3회: 위치별 관측 CIR·수신 품질 | 18 |
| **합계** | 본 실험 825 + 긴 PHY 확인 조건부 81 | **최대 계획 906** |

재시도·설치 점검은 합계 밖이다. 순수 채널 보정 reference·K-인자·경로손실 지수 추정은 이번 Exp5의 필수 결과나 825케이스에 포함하지 않는다. Exp1의 최종 송수신 경로 1:1 비교는 Exp4 S1 회전에 포함한다. Exp4에서 PAC8을 고정해 M32와 M256을 비교해도 **PAC4는 Stage0와 Exp2에서 계속 측정**한다.

## 2. lead 선정: PAC4와 PAC8은 독립

`L4`는 M32/PAC4 전체 Stage0 grid와 confirmation에서, `L8`은 M32/PAC8의 **별도** grid와 confirmation에서 정한다. 두 값 사이의 공식이나 순서 관계를 가정하지 않는다. M1024/PAC32도 독립 선정한다. PAC4 결과를 PAC8의 초기값으로 사용하지 않는다.

긴 M64/M128/M256은 모두 PAC8이므로 **PAC8 내부의** lead–ACCUM–PER 관측 패턴을 예측 근거로 검토한다. 예전 M32/PAC8 한 grid에서 lead 21의 성공 수신 modal ACCUM 15/PER 33.3%, lead 22의 8/0.1%, lead 23의 9/0%가 나타났다. 해당 lead 21 원문에는 실패 유형별 `FAIL_ACCUM_SUMMARY_CSV`와 `FAIL_ACCUM_HIST_CSV`도 있다. `valid`와 `invalid_raw`를 구분한다. `M−32`가 8의 배수라는 사실은 같은 PAC8 위상이 나타날 **가설**이고, 긴 PHY의 최적 lead가 `L8`과 같다는 증거는 아니다.

1. 각 환경의 PAC8 전체 grid에서 성공 ACCUM mode·분포, 유효 실패 ACCUM, 실패 유형별 PER/이벤트 수, 획득 경계를 분석한다. 사용 가능한 이전 PAC8의 M64/M256 관측도 출처와 환경을 구분해 참고한다.
2. M64/M128/M256 각각의 예측 lead 중심 `L64_pred`, `L128_pred`, `L256_pred`와 산출 근거를 **PHY별로** 기록한다. 근거가 부족하면 `UNRESOLVED`로 두고 국소 탐색 범위를 별도로 결정한다. `L8`을 복사해 예측했다고 쓰지 않는다.
3. 예측점이 정의된 PHY마다 범위 0–40 µs 안의 연속 9점(약 PAC8 한 주기)을 N4 링크에서 측정한다. 시작 전에 점·순서를 고정한다. 세 PHY 모두 가능하면 27케이스다.
4. PER, 성공 ACCUM 위상·wrap, 유효 실패 ACCUM과 오류 종류, 경계 여유를 함께 보고 PHY별 lead를 선택한다. 이후 N2–N7 각 링크 3회 확인한다(최대 54케이스). 각 유효 run의 PER <1%, 링크별 합산 Wilson 95% 상한 <1%, 시스템·수집 검증을 확인한다. 이 54회는 Exp2의 90회와 중복 계산하지 않는다.
5. 실패·미측정 lead를 숨기지 않는다. 검증이 안 된 PHY의 손실을 프리앰블 길이의 효과로 단정하지 않는다. 집과 최종 차량은 각각 새 lead 선정 근거를 만든다.

## 3. Exp2–Exp5 해석 범위

- Exp2는 5 PHY·6물리 링크의 PER와 **수신 성공 프레임 조건부 FP-SNR 분포·ACCUM·CIR/품질**을 3회 반복한다. FP-SNR은 각 유효 프레임의 `fp_snr_ratio_x1000` 및 유효 표본 수를 보이고, 필요하면 프레임별 파워비를 dB로 변환해 별도 요약한다. 손실 순간의 CIR/FP-SNR은 관측한 것으로 쓰지 않는다. RSSI/FP 절대값은 유효 플래그와 후처리 버전 검증 후 사용한다.
- Exp3는 A=SFD8+STD PHR, B=SFD16+STD PHR, C=SFD8+DTA PHR을 **각 3회** 측정한다(총 9케이스). 한 반복의 A/B/C를 짝지어 EXTTXE `B−A`·`A−C`와 RX PER를 계산하고, 세 반복의 변동도 보고한다. 실행 순서는 반복 1 `A→B→C`, 반복 2 `C→B→A`, 반복 3 `B→A→C`로 사전 고정한다. 헤더 없는 프레임의 복호 성공 근거로 사용하지 않는다.
- Exp4 S1–S5는 같은 PAC8에서 M32/M256의 노드 수 곡선이다. S6은 매 superframe K6–24 요청을 만들고 시간 예산에 들어간 것만 RF에 싣는다. 계산상 수용 K는 M32=20, M256=12다. `APPLICATION_DEMAND.csv`는 **컨트롤러가 생성한 요청/수용 결정**이며 무선 수신 로그가 아니다. `offered → admitted → actual TX → RX`를 분리한다. `scheduled PER=(admitted−RX)/admitted`, `application loss=(offered−RX)/offered`, `goodput=RX×128/실측초`다. 분모 0은 `NA_DENOM_ZERO`. 유효 과부하의 PER100%/RX0은 공식 FAIL_PER로 남긴다.
- Exp5는 차량의 6개 물리 센서 위치에서 M1024/PAC32로 링크별 3회 측정한다. 성공 수신 전체의 CIR/FP-SNR·전력 진단과 성공 수신순번 1,34,…,958에서 최대 raw30×300 complex CIR을 저장한다. 같은 noise threshold·FP 정렬·window로 계산한 **관측 PDP/RMS(송수신기·안테나+전파 응답)**와 반복 범위·처리 민감도를 비교한다. 30 raw 프레임은 성공 수신 조건부이며 정적 링크의 독립 공간 표본 30개가 아니다. 순수 전파 RMS, 라이시안 K-인자, 경로손실 지수 n은 이번 결과 항목에서 제외한다. 기존 실내 reference/CLEAN 미검증 상태는 [과거 검증 계획](../../../logs/final_vehicle_preparation_20260922_222350/EXP5_VALIDATION_PROTOCOL_KR.md)에 보존한다. Exp5의 관측 특성은 같은 물리 링크의 Exp2 M별 PER과 **설명적으로만** 비교하고 최소 M의 결정 근거로 단독 사용하지 않는다.

기존의 동일 조건 새 ID 최대 1회 재시도, 첫 유효 판정 보존, 원본·정정·STOP 보존, 시스템 오류와 RF 손실 구분을 따른다. 모든 보드의 물리 serial·논리 역할·위치·실제 조건/hash를 확인한다.

---

## 4. 결과를 읽기 위한 데이터 설계 — 상세 표와 원본 연결

**목적은 수기 입력이 아니라, 원본 로그를 사람이 읽고 논문에 사용할 수 있는 결과로 바꾸는 것이다.** 아래 표의 세밀함은 실험 전에 필요한 데이터를 빠짐없이 정의하고, 실행 후 자동 추출·검증·표/그림 생성을 요구하기 위한 것이다. 손으로도 모든 값을 점검할 수 있을 정도로 열을 명시하지만, 수천 개 원시 행을 다시 타이핑하는 방식은 요구하지 않는다. **현재 이 문서는 결과 화면/보고서의 설계안이며, 자동 결과 생성기가 이 형식을 이미 구현했다는 뜻은 아니다.** 실행기·원본 로그·공식 판정은 수정하지 않는다.

실험 후 열람 순서는 다음과 같다.

1. **한눈에 보기:** 환경별·실험별 완료/미측정/무효/유효 PER 실패 수와 논문 핵심 수치·그림을 본다. 실패를 평균에서 지우지 않는다.
2. **조건·반복 비교:** 5장의 모든 예정 행에 실제 `case_id`, 반복, 물리 링크/역할, 주요 수치, 공식 판정, 집↔차량 대응을 붙인다. 같은 조건의 반복 분포와 최악 링크도 함께 본다.
3. **케이스 상세:** 4.1–4.6의 TX→RX 수량, 오류 종류, ACCUM/CIR/타이밍 분포, 계산 분모와 결측 이유를 확인한다. 논문용 요약값에서 원시 프레임·샘플 표까지 내려갈 수 있어야 한다.
4. **원본 대조:** 상세 표의 각 값은 원본 파일 경로·SHA256·marker/열·행 수·분석 산식/버전·공식 판정으로 되돌아가 검증할 수 있어야 한다. 해석용 표는 봉인된 원본을 대체하거나 고치지 않는다.

| 단계 | 결과에서 먼저 볼 질문 | 사람이 읽을 핵심 결과·그림 | 논문에서 쓸 범위와 주의점 |
|---|---|---|---|
| Stage0·긴 PHY lead | 어느 lead에서 획득이 안정되고 PAC 경계에서 손실이 튀는가? | PHY/PAC별 lead–PER, 성공·유효 실패 ACCUM histogram, 오류 유형, 반복 확인 | PHY·환경별 lead 선정 근거. 성공 ACCUM만으로 선정하거나 PAC4/8을 섞지 않음 |
| Exp2 | 프리앰블 길이와 링크에 따라 수신율·성공 프레임 품질이 어떻게 달라지는가? | PHY×6링크×3회 PER/Wilson, 성공 조건부 CIR·FP-SNR·ACCUM 분포, 최악 링크 | 손실 순간의 CIR로 주장하지 않음. 무효 RSSI/FP 값 제외 근거 표시 |
| Exp3 | SFD 길이·PHR 속도 변경의 실제 airtime 차이는 얼마인가? | 반복별 A/B/C 짝 비교, EXTTXE 폭 분포와 B−A/A−C, RX PER | 표준 프레임의 차이만 실측. SFD/PHR 제거 성공으로 해석하지 않음 |
| Exp4 | 요청 부하가 늘 때 수용·실제 송신·수신·goodput은 어떻게 변하는가? | offered→admitted→TX→RX, PHY·S·K별 goodput/PER 곡선, 노드/블록 분포 | 수용 정책의 포화와 RF 손실을 분리. 계산상 수용 상한을 충돌 한계 실측으로 부르지 않음 |
| Exp5 | 차량 위치별 관측 CIR/PDP·지연 확산·수신 품질이 어떻게 다른가? | 6링크×3회 PER·FP-SNR·CIR 품질, raw 선택 범위, 관측 RMS의 프레임/반복 분포와 처리 민감도 | 송수신기·안테나+전파 응답. K/n·순수 채널 RMS는 미추정. Exp2의 M별 PER과 설명적 비교만 가능 |

아래 `[ ]`는 **미리 정의한 결과 열의 자리**이며, 나중에 원본으로부터 채울 값이다. 각 칸의 상태는 `PLANNED / NOT_MEASURED / VALID / FAIL_PER_VALID / INVALID / INCOMPLETE / EXCLUDED_WITH_REASON / UNKNOWN` 중 하나다. 관측되지 않은 값은 `0` 대신 `NA`로 쓴다. `paper_slot_id`는 집·차량의 대응 조건이고, `case_id`와 `attempt_id`는 각각의 원본을 가리킨다. 대량 원시 행은 검색·필터 가능한 별도 상세표로 연결하고, 요약에는 행 수·결측/중복 검증·출처를 표시한다.

**예시 읽는 법:** `예시(가상·집계 제외)` 행·열·원시행은 칸의 형식만 보여준다. 실측 결과·선정 lead·논문 수치·예정 case 수에 포함하지 않는다. 옆이나 아래의 `[ ]`가 실제 결과를 채울 자리다. Markdown의 옅은 글씨는 표시 환경마다 달라지므로 글자로 구분한다. `EXAMPLE_ONLY`로 표시한 serial·case·경로·hash는 실제 증거가 아니다.

### 4.1 공통 환경·case 카드 — 모든 case에서 보여줄 항목

| 항목 | 기입값 | 입력 예시(가상·집계 제외) |
|---|---|---|
| 데이터 구분·환경 ID (`EXISTING_CONTEXT/HOME_PROVISIONAL/VEHICLE_FINAL`) | `[ ]` | VEHICLE_FINAL; env=EXAMPLE_ONLY_CAR_A |
| paper_slot_id / case_id / attempt_id / 반복 block / 회전 index | `[ ]` | slot=EXAMPLE_ONLY_EXP2_M32_N2_R01; case=EXAMPLE_ONLY_CASE; attempt=a1; block=R01; rotation=0 |
| stage·실험 종류 / 측정 시작·종료 시간·시간대 | `[ ]` | Exp2; 2026-10-01 10:00–10:02 KST |
| 실험 장소·차종/방 구조·사진 경로 / 변화·정정 파일 | `[ ]` | 차량 A; 사진=EXAMPLE_ONLY/layout.jpg; 정정=없음 |
| INIT 및 N2–N7의 물리 serial·위치·안테나 방향·논리 역할 매핑 | `[ ]` | INIT=SERIAL_EXAMPLE_RX(대시보드); N2=SERIAL_EXAMPLE_TX(도어); 나머지=매핑표 참조 |
| TX–RX 안테나 기준 거리·높이·기준면·근접 금속/유리/차폐 | `[ ]` | 안테나 간 1.20 m; 높이 0.75 m; 기준면=시트 상단; 근접 금속=도어 프레임 |
| 문·사람·키/휴대폰·라우터·Wi-Fi·전원/V2L·USB 상태 | `[ ]` | 문 닫힘; 사람 없음; 키/휴대폰=차량 밖; Wi-Fi ON; V2L ON; 유전원 USB 허브 |
| CH·SYNC preamble code/M/PAC·DATA preamble code/M/PAC·PRF·data rate·STS·SFD/PHR·PSDU/payload·TX power·주기 | `[ ]` | CH5; SYNC code10/M512/PAC8; DATA code9/M32/PAC8; PRF64 MHz; 6.8 Mb/s; STS OFF; SFD8/PHR STD; PSDU 26 B/payload 16 B; TX power=설정값+출처; superframe 10 ms |
| guard/SB/SP·lead·K/S·slot owner sequence·SF 목표/실제·수집 시간 | `[ ]` | guard=200 µs; SB=1700 µs; SP=2200 µs; lead=예시 24 µs; K/S=NA; owners=N2; SF 목표/실제=1000/1000; 실측 10.2 s |
| 매니페스트·조건·assignment·source·HEX/ELF·도구 SHA256 | `[ ]` | manifest=EXAMPLE_ONLY/manifest.json; assignment=EXAMPLE_ONLY/assignment.csv; source·HEX·ELF·tool hash=EXAMPLE_ONLY |
| root / TX 원문·RX 원문·metadata·`ASSESSMENT.json` 경로 | `[ ]` | root=EXAMPLE_ONLY/root; TX=tx.log; RX=rx.log; metadata=meta.json; ASSESSMENT=assessment.json |
| 각 원시 파일 SHA256·marker별 행 수·헤더·누락/중복 검사 | `[ ]` | tx.log SHA256=EXAMPLE_ONLY; TX 원시 marker 합계=1000행; 헤더·결측·중복 검사=PASS |
| READY/END·flash/readback·전압/HALT·수집0·STOP 증거 | `[ ]` | READY 2/2·END 2/2; HEX readback=PASS; 3300 mV·HALT=PASS; collector=0; STOP=유지 |
| 공식 판정 / 유효 PER 실패 / 제어·수집 오류 / 제외·재시도 연결 | `[ ]` | 공식=VALID; FAIL_PER_VALID=아니오; 제어오류=0; 제외/재시도=없음 |

SYNC/DATA preamble code는 `같음`으로 뭉뚱그리지 않고 각각 숫자를 적는다. 각 code에 `설정값·확인 출처(봉인된 C 설정/빌드 조건/실기 readback 중 실제 확보한 것)·출처 hash·일치 판정`을 붙인다. 현재 기본 BRRS C 설정은 DATA 9/SYNC 10이지만, 이를 모든 과거·미래 이미지에 자동 상속하지 않는다. 매니페스트만으로 code를 확인할 수 없으면 봉인 이미지에 대응하는 소스를 대조하고, 끝내 검증할 수 없으면 `UNKNOWN`으로 둔다. 사용하지 않는 PHY 항목은 `NA_NOT_USED`로 구분한다. 결과 생성 시 원본의 숫자형 CSV marker를 전수 목록화하고, 아래 상세 카드에 해석 열이 없는 marker도 `marker/전체 원문 열·값/행 수/파일·hash/해석 상태` 보조표에 남긴다. 따라서 새 marker나 미해석값을 조용히 버리지 않는다.

### 4.1a 반복값·신뢰구간 공통 요약 — 같은 조건의 모든 유효 회차

Stage0 선정 lead 확인, Exp2, Exp3, Exp4는 단일 합산값만 만들지 않는다. 각 유효 회차의 수치와 공식 판정을 먼저 보존하고, 동일한 비교 조건·물리 링크/역할에서 아래 요약을 만든다. `FAIL_PER_VALID`도 유효 회차에 포함하고 `INVALID`·미측정은 0으로 대체하지 않는다. Exp4의 역할 회전은 물리 serial과 위치를 표시한 채 층화해 보며, 서로 다른 물리 링크를 조용히 한 링크의 반복으로 합치지 않는다.

| env·stage·조건 key·물리 링크/역할 | 계획/유효/FAIL_PER_VALID/INVALID/미측정 회차 수 | R별 case_id·원본·판정 | R별 분자/분모·측정값 | R별 PER%·Wilson95% | PER 반복 min–max·중앙값 | 합산 분자/분모·PER%·Wilson95% | 다른 핵심 지표의 R별 값·min–max·중앙값 | 집계 산식·분석 버전·상관/제외 주의 |
|---|---|---|---|---|---|---|---|---|
| 예시(가상·집계 제외): 차량 Exp2 M32/PAC8 N2 | 3/3/0/0/0 | R01/R02/R03=EXAMPLE_ONLY_CASE_01/02/03; 원본=EXAMPLE_ONLY; 모두 VALID | 손실/기회=1/1000, 2/1000, 0/1000; RX=999,998,1000 | 0.10% [0.018–0.564], 0.20% [0.055–0.726], 0% [0–0.383] | 0–0.20%; 중앙값 0.10% | 손실 3/3000; PER 0.10%; Wilson95 [0.034–0.294]% | FP-SNR 중앙 dB=3.1/3.3/3.0; 범위 3.0–3.3; 중앙 3.1 | 손실=expected−RX; Wilson z=1.96; 분석=EXAMPLE_ONLY_v1; 시간 상관 별도 검토 |
| `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |

PER의 Wilson95는 **프레임 수 기준 구간**이고, 회차 간 min–max는 **실제 반복 변동 범위**다. 둘을 같은 신뢰구간이나 독립 반복의 통계적 불확도로 부르지 않는다. 연속 프레임 손실의 시간 상관 때문에 합산 Wilson만으로 반복 변동을 대체하지 않는다. Exp3의 EXTTXE 폭·짝 차분과 Exp4의 goodput은 이항 PER이 아니므로 Wilson을 붙이지 않고 회차별 값·min–max 및 사전에 정한 별도 불확도 산식을 쓴다. Figure의 오차막대 방식은 나중에 결정하되, 그 선택에 필요한 회차별 원값·분자·분모는 여기서 잃지 않는다.

| 단계 | 반복에서 보존할 값 | 반복 범위·집계에서 볼 값 |
|---|---|---|
| Stage0 선정 lead 확인 | 회차별 TX/RX/PER·Wilson95, 성공/유효 실패 ACCUM, 오류 유형 | PER 및 ACCUM mode의 min–max, 합산 PER·Wilson95 |
| Exp2 | 링크·PHY별 회차 PER·Wilson95, FP-SNR 유효 n·프레임별 dB 요약, ACCUM | PER·FP-SNR 요약값의 min–max, 합산 PER·Wilson95; FP-SNR은 성공 수신 조건부 |
| Exp3 | A/B/C 회차별 RX/PER·Wilson95, EXTTXE 폭, 같은 회차의 B−A·A−C | 폭과 짝 차분의 3회 min–max·중앙값; 차분에 Wilson을 적용하지 않음 |
| Exp4 | PHY·S/K·block·물리 serial별 offered/admitted/TX/RX·PER·Wilson95·goodput | 12블록의 PER·goodput·worst-node 값 min–max, 합산 분자/분모·Wilson95, 회전별 편차 |

### 4.2 Stage0 및 긴 PHY lead — **lead 하나당 한 행**

실제 Stage0에서는 `EXP1_DONE`과 본문 통계, 실패 ACCUM marker를 읽는다. 성공 ACCUM histogram과 **실패 유형별 valid/invalid ACCUM histogram**을 따로 적는다. PAC4와 PAC8 표를 섞지 않는다. Confirmation 3회도 별도 행으로 적는다.

| env | PHY M/PAC | 단계(grid/confirm/long-scan/long-confirm) | lead µs | link·block | expected | TX attempts/success·SYNC timeout/RX-error | RX/miss | PER %·Wilson95 | rx timeouts fwto/pto | rx errors sfdto/phe/fce/fsl | late·beacon/data config error | 성공 ACCUM min/max/avg·hist | 실패 유형별 events/diag_ok/valid/invalid_no_rxprd/invalid_zero/invalid_range/read_fail/hist_overflow·valid/invalid_raw hist | slot timing ns min/max/avg/n | collection/link/공식 판정 | 원문·hash |
|---|---|---|---:|---|---:|---|---|---|---|---|---|---|---|---|---|---|
| 예시(가상·집계 제외): 차량 | 32/8 | confirm | 24 | N4/R01 | 2000 | attempt/success=2000/2000; SYNC timeout=0; RX error=0 | 1998/2 | 0.10%; [0.027–0.364]% | fwto/pto=2/0 | sfdto/phe/fce/fsl=0/0/0/0 | late=0; beacon/data config=0 | min/max/avg=8/9/8.4; hist={8:1200,9:798} | fwto: events=2, diag_ok=2, valid=1, no_rxprd=1, zero/range/read_fail/overflow=0; valid_hist={8:1}; invalid_raw={0:1} | 100/160/130/1998 | collection/link=PASS; 공식=VALID | EXAMPLE_ONLY/stage0.log; hash=EXAMPLE_ONLY |
| `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |

실패 ACCUM의 `invalid_raw` histogram은 참고용 원시값이지 유효 누산 분포가 아니다. `hist_overflow>0`이면 표에 보이는 histogram이 전체 사건을 대표한다고 주장하지 않는다. 실패 유형별 `events` 합계와 RX 오류·timeout 계수도 대조한다.

긴 PHY 예측·선정 보조표:

| M/PAC | PAC8 참고 grid·출처 | 예측 알고리즘/버전 | 예측 중심·근거 | 9개 사전 고정 lead | 성공/유효 실패 ACCUM 위상·경계 | 선정 lead | 6링크 각 3회 PER/Wilson95 | 결론/보류 이유 |
|---|---|---|---|---|---|---|---|---|
| 예시(가상·집계 제외): 64/8 | 32/8 grid=EXAMPLE_ONLY; PAC4 미사용 | phase_predict EXAMPLE_ONLY_v1 | 중심 24 µs; PAC8 ACCUM 경계·PER 상승점 기반(가상) | 20,21,22,23,24,25,26,27,28 µs | 성공 mode=9; 유효 실패 mode=8; 경계=24 µs(가상) | 25 µs(가상 선정 예) | N2–N7 × R01–R03: PER·Wilson95 각각 원본 연결(예: N2 R01 1/1000, 0.10%, [0.018–0.564]%) | 모든 필수 링크·반복 확인 뒤에만 선정; 현재 실측 아님 |
| `64/8` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |
| `128/8` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |
| `256/8` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |

### 4.3 Exp2 — PHY×물리 링크×block 한 행 + **성공 프레임별 FP-SNR·CIR 상세**

Exp2의 첫 화면에서 **FP-SNR을 PER 옆에** 둔다. 원본 `fp_snr_ratio_x1000`은 dB가 아닌 선형 파워비의 1,000배다. 펌웨어의 `CIR_SUMMARY_CSV`는 유효 프레임의 ratio min/avg/max를 제공한다. dB 분포가 필요하면 유효한 프레임별 `10log10(fp_snr_ratio_x1000/1000)`을 계산하고, 변환식·표본 수·분석 버전을 함께 표시한다. `10log10(평균 ratio)`를 프레임별 dB의 평균으로 표기하지 않는다.

| 논문용 결과 열 | 각 PHY×물리 링크×반복에서 보여줄 값 | 원본·해석 제한 |
|---|---|---|
| 수신 성능 | TX attempts/success, RX/미수신, PER, Wilson 95%, 오류 유형 | 송신 기회와 실제 TX를 구분 |
| **FP-SNR** | 유효 n/무효 n, 선형 ratio min/avg/max, 프레임별 dB 분포(중앙값·범위·분위수는 후처리 시) | `CIR_CSV`·`CIR_DIAG_CSV`·`CIR_SUMMARY_CSV`; 성공 수신 조건부 |
| 획득·CIR | ACCUM histogram, first-path sample·peak, FP peak/noise power, CIR 행 수·누락/중복 | 실패 프레임의 CIR로 확대 해석 금지 |
| 보조 품질·시각 | RSSI/FP dBm 유효 n·분포, slot timing min/max/avg/n | 무효 절대값은 평균에서 제외 |

| env·case | M/PAC·lead | physical TX serial·위치 | block | target/attempts/TX success | RX/miss/PER/Wilson95 | beacon RX/miss | `CIR_CSV` rows/순번·cycle 검증 | `CIR_DIAG_CSV` rows/유효성 | RSSI/FP/SNR valid·invalid·diag read error | `CIR_SUMMARY_CSV`의 FP-SNR min/max/avg, RSSI min/max/avg, FP min/max/avg | slot timing min/max/avg/n | 공식 판정·원문/hash |
|---|---|---|---:|---|---|---|---|---|---|---|---|---|
| 예시(가상·집계 제외): 차량/EXAMPLE_ONLY_CASE | 32/8; lead 24 µs | SERIAL_EXAMPLE_TX; 도어 | R01 | 1000/1000/1000 | 999/1; 0.10%; [0.018–0.564]% | 1000/0 | 999행; seq 1–999 중복0; cycle 연결 PASS | 999행; 유효 995/무효4 | RSSI 995/4; FP 995/4; SNR 995/4; read error 0 | FP-SNR 선형×1000=1500/2000/2800; RSSI dBm=−80/−76/−72; FP dBm=−84/−80/−76 | 100/160/130 ns; n=999 | VALID; EXAMPLE_ONLY/exp2.log; hash=EXAMPLE_ONLY |
| `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |

`CIR_CSV` 프레임 상세표의 열: 1개 행이 수신 성공 프레임 1개다. 아래는 자동 추출 결과에서 볼 열의 정의이며 수기 전사를 요구하지 않는다. 결과에는 행 수·누락/중복·원문 경로/hash를 함께 표시한다.

```text
rx_seq,cycle,node,plen,fp_sample,peak_idx,accum,rssi_dbm,fp_dbm,rssi_fp_gap_db,fp_peak_power,noise_floor_power,noise_samples,fp_snr_ratio_x1000
# 예시(가상·집계 제외): 1,7,N2,32,101.2,102,9,-76,-80,4,240,120,16,2000
[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ]
```

`CIR_DIAG_CSV` 상세표의 열: 같은 `rx_seq,cycle`로 위 행과 연결한다. `*_valid=0`인 절대값을 측정치처럼 평균내지 않는다.

```text
rx_seq,cycle,node,plen,diag_rc,power,accum,F1,F2,F3,dgc_decision,rx_pcode,cia_conf,rssi_rc,fp_rc,rssi_q8_8,fp_q8_8,rssi_valid,fp_valid,snr_valid
# 예시(가상·집계 제외): 1,7,N2,32,0,240,9,100,90,80,1,9,2,0,0,-19456,-20480,1,1,1
[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ]
```

### 4.4 Exp3 — A/B/C 각 3회, 케이스당 1,000 TX 행과 RX 결과

| env·case | variant·SFD symbols·PHR rate | lead·M/PAC·PSDU | TX attempts/success/captures·END | TX EXTTXE ticks/ns 원시행 수 | width ns mean/median/min/max/분산 | RX expected/rx/miss/PER | `B−A` 또는 `A−C` 실측·이론·차이 | 공식 판정·원문/hash |
|---|---|---|---|---:|---|---|---|---|
| 예시(가상·집계 제외): EXAMPLE_ONLY_CASE_A | A / 8 / STD | lead 24 µs; M32/PAC8; PSDU 26 B | 1000/1000/1000; END=1 | 1000행; tick→ns 변환 버전=EXAMPLE_ONLY_v1 | 250000/250001/249990/250010 ns; 분산 16 ns² | 1000/999/1; PER 0.10% | A행은 비교 기준; B−A=8142 ns(가상), 이론≈8141 ns, 차이≈+1 ns | VALID; EXAMPLE_ONLY/exp3.log; hash=EXAMPLE_ONLY |
| `[ ]` | `A / 8 / STD` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |
| `[ ]` | `B / 16 / STD` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |
| `[ ]` | `C / 8 / DTA` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |

`EXP3_TX_MODEL_CSV`의 preamble/SFD/PHR/PSDU/DP/frame 모델 시간(ns)과 RS parity(bits), `EXP3_TX_SUMMARY_CSV`의 attempts/success/captures/invalid·incomplete pulse 합계/min/max/avg/model ns, `EXP3_RX_RESULT_CSV`의 expected/RX/miss/PER ppm/frame model ns/status를 케이스별로 대조한다. 모델값과 EXTTXE 실측값을 같은 열의 실측으로 혼동하지 않는다.

| env·case | TX 모델의 preamble/SFD/PHR/PSDU/DP/frame ns·RS parity bits | TX summary의 attempts/success/captures/invalid·incomplete/min/max/avg/model ns | RX result의 expected/RX/miss/PER ppm/frame model ns/status | model·summary·RX 원문/hash |
|---|---|---|---|---|
| 예시(가상·집계 제외): EXAMPLE_ONLY_CASE_A | preamble=100000; SFD=8141; PHR=20000; PSDU=100000; DP=120000; frame=228141 ns; RS parity=48 bits(가상) | attempt/success/capture=1000/1000/1000; invalid/incomplete=0/0; width min/max/avg=249990/250010/250000 ns; model=228141 ns | expected/RX/miss=1000/999/1; PER=1000 ppm; frame model=228141 ns; status=VALID | EXAMPLE_ONLY/model.log,summary.log,rx.log; hash=EXAMPLE_ONLY |
| `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |

`EXP3_TX_CSV`의 모든 원시 행과 `EXP3_RX_RESULT_CSV` 원문을 보존한다. 프레임별 상세표에서 볼 열:

```text
seq,variant,sfd_symbols,phr_rate,psdu_bytes,ticks,width_ns
# 예시(가상·집계 제외): 1,A,8,STD,26,EXAMPLE_ONLY_TICKS,250000
[ ],[ ],[ ],[ ],[ ],[ ],[ ]
```

### 4.5 Exp4 — 케이스·물리 노드·슬롯·수요를 각각 기록

케이스 전체:

| env·case | M/PAC·lead | S·application K·RF admitted slots | block/rotation·physical→logical mapping | guard/SB/SP·frame airtime·slot length·slot owners·max slots | SF target/실제·elapsed_us | offered/admitted/admission drop/actual TX/RX | aggregate PER·worst-node PER·goodput kbps | `EXP4_STATUS_CSV`/공식 판정 | 원문·demand CSV/hash |
|---|---|---|---|---|---|---|---|---|---|
| 예시(가상·집계 제외): EXAMPLE_ONLY_CASE | 32/8; lead 24 µs | S6; K8; RF admitted slots=6 | R01/rotation0; SERIAL_EXAMPLE_TX→N2 | guard=200; SB=1700; SP=2200 µs; airtime=650 µs(가상); slot=850 µs; owners=N2–N7; max_slots=6 | 1000/1000; elapsed=10000000 µs | 8000/6000/2000/5990/5980 | scheduled PER=20/6000=0.333%; worst node=0.5%; goodput=76.54 kb/s(16 B payload·10 s 가정) | status=VALID; 공식=VALID | EXAMPLE_ONLY/exp4.log,demand.csv; hash=EXAMPLE_ONLY |
| `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |

`EXP4_CONFIG_CSV`와 `EXP4_SLOT_SCHEDULE_CSV`의 전체 설정값·slot-owner 순서·active bitmap·반복수는 위 케이스 값과 대조하고, `EXP4_RF_CONFIG_CSV`의 data/sync/hardware channel·TX power index/register·PG delay·linear TX status도 별도 확인한다. 원문 marker와 hash를 함께 연결한다. `EXP4_SUMMARY_CSV`는 `M/활성 노드/슬롯 수/반복 수/PSDU/application bytes/slot·guard·max_slots/SF 수/expected/RX/PER ppm/goodput bps/offered bps/elapsed_us/delayed-RX fallback/wrong-slot/status`의 **전체 열**을 별도 요약행으로 보여준다. 특히 `max_slots`는 계산된 스케줄 상한이며 실제 RF 충돌 한계가 아니다.

| env·case | summary의 expected/RX/PER ppm | offered·goodput bps/elapsed_us | delayed-RX fallback·wrong-slot | wrong-length·wrong-superframe·RX/TX schedule late·config error | rearm deadline miss·RX buffer/deferred overflow | schedule/timing/collection/link 상태 | summary·validation 원문/hash |
|---|---|---|---|---|---|---|---|
| 예시(가상·집계 제외): EXAMPLE_ONLY_CASE | 6000/5980/3333 ppm | offered=102400 bps; goodput=76544 bps; elapsed=10000000 µs | fallback=0; wrong-slot=0 | wrong-length=0; wrong-SF=0; RX/TX late=0/0; config=0 | rearm miss=0; RX overflow=0; deferred overflow=0 | schedule/timing/collection/link=PASS/PASS/PASS/PASS | EXAMPLE_ONLY/summary.log,validation.log; hash=EXAMPLE_ONLY |
| `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |

마지막 행의 오류 수는 `EXP4_SUMMARY_CSV`, `TDMA validation`, `EXP4_REARM_CSV`, `EXP4_DEFERRED_CSV` 및 TX marker에서 출처별로 읽는다. 단순 `PASS`만 적지 않고 실제 0/비0을 표시한다. 현재의 슬롯별 CSV는 **슬롯별 집계**이므로, superframe마다 모든 슬롯이 동시에 성공한 비율은 이 자료만으로 계산할 수 없다. 별도 SF별 관측이 없는 경우 `NOT_MEASURED`로 두며 PER에서 독립성을 가정해 추정하지 않는다.

물리 TX **각 노드마다** 아래 행을 만든다. `EXP4_NODE_CSV`의 expected/RX/miss/error와 `EXP4_TX_RESULT_CSV`의 beacon·attempt·success·late를 실제 로그에서 대조한다. 부하 모드의 오른쪽 계산값은 [수요 분리 코드](../../../logs/final_vehicle_preparation_20260922_222350/controller/sdk/Drivers/API/brrs_exp4_load.py)의 필드명과 일치한다.

| physical serial·위치 | logical N/owned slots | beacon received/missed | application offered | admitted | admission dropped | not transmitted after admission | TX attempts/success | RX/miss·RX error | transmitted not received | scheduled PER % | TX-conditioned PER % | application loss % | goodput kbps | node 판정 |
|---|---|---|---:|---:|---:|---:|---|---|---:|---:|---:|---:|---:|---|
| 예시(가상·집계 제외): SERIAL_EXAMPLE_TX/도어 | N2/slot1 | 1000/0 | 2000 | 1800 | 200 | 2 | 1800/1798 | 1790/10; RX error=2 | 8 | 10/1800=0.556% | 8/1798=0.445% | 210/2000=10.5% | 22.91(16 B payload, 10 s 가정) | VALID |
| `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |

슬롯별 `EXP4_SLOT_RX_CSV`는 RF 슬롯 수만큼 자동 추출해 별도 상세표에서 열람한다. 아래는 그 열의 정의다.

```text
slot,owner,window_us,fwto_uus,attempted,armed,late,rx_good,timeout,error,min_arm_slack_us,slack_samples
# 예시(가상·집계 제외): 1,N2,1200,1100,1000,1000,0,998,2,0,60,1000
[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ],[ ]
```

Exp4 application 수요는 `APPLICATION_DEMAND.csv`의 **모든 요청 행**을 원본 경로·hash·행 수로 연결하고, superframe·요청 순서·노드·결정별로 검색할 수 있게 한다. 원문 열은 다음과 같다. `decision`은 `admitted` 또는 `admission_drop`이다.

```text
superframe,request_index,logical_node,physical_serial,decision
# 예시(가상·집계 제외): 1,1,N2,SERIAL_EXAMPLE_TX,admitted
[ ],[ ],[ ],[ ],[ ]
```

타이밍·수신 경로·시스템 카운터도 케이스마다 기록한다. 다음 표에서 `marker 원문 경로/hash`는 해당 RX/TX 로그 파일을 가리키고, 값에는 각 marker의 숫자 전체 또는 `NA_DISABLED`를 적는다.

| 로그 marker/영역 | 기입할 측정 열·상태 | 값/원문 위치 | 입력 예시(가상·집계 제외) |
|---|---|---|---|
| `EXP4_TIMING_CSV` | period_count, min/max/avg period, elapsed_us, sync_delayed_late, tx_wait_timeout | `[ ]` | period_count=1000; min/max/avg=9980/10020/10000 µs; elapsed=10000000 µs; sync late=0; TX wait timeout=0; EXAMPLE_ONLY/exp4.log |
| `EXP4_SYNC_PREP_CSV`, `EXP4_SYNC_PREP_E2E_CSV` | budget/count/min/max/avg, p50/p95/p99, remaining lead, late/overflow | `[ ]` | budget=200 µs; count=1000; min/max/avg=90/140/110 µs; p50/p95/p99=108/130/138; remaining lead=60 µs; late/overflow=0/0; EXAMPLE_ONLY/log |
| `EXP4_FIRST_RX_ARM_CSV`, TX `EXP4_TX_FIRST_ARM_CSV` | first-arm min/max/avg·percentiles, RX-open/data-rmarker slack·overflow·late | `[ ]` | first-arm min/max/avg=100/160/130 µs; p50/p95/p99=128/150/158; RX-open slack=60 µs; overflow/late=0/0; EXAMPLE_ONLY/log |
| `BRRS_SLOT_TIMING_CSV`, TX `EXP4_TX_SLOT_TIMING_CSV` | 노드별 signed timing error min/max/avg/n과 참조 이벤트 | `[ ]` | N2 timing error min/max/avg=−20/+25/+2 ns; n=1000; 참조=beacon; EXAMPLE_ONLY/log |
| `EXP4_BURST_CSV`, `EXP4_REARM_CSV` | close 유형/건수, rearm time·polls·required guard·late | `[ ]` | close=normal 1000, timeout 0; rearm min/max/avg=20/35/27 µs; polls=2; required guard=200 µs; late=0; EXAMPLE_ONLY/log |
| `EXP4_DOUBLE_BUFFER_CSV`, `EXP4_DEFERRED_CSV` | RDB good/mismatch/incomplete/resync, queue overflow, RX timeout/error, deadline miss | `[ ]` | RDB good/mismatch/incomplete/resync=998/0/0/0; queue overflow=0; RX timeout/error=2/0; deadline miss=0; EXAMPLE_ONLY/log |
| `EXP4_SPI_CSV`, `EXP4_HOT_PATH_CSV` | SPI begin/end·transfer/timeout/error, hot-path min/max/avg/p95/p99 | `[ ]` | SPI begin/end=1000/1000; timeout/error=0/0; hot-path min/max/avg/p95/p99=10/20/15/18/19 µs; EXAMPLE_ONLY/log |
| `EXP4_PHY_FAST_SELFTEST_CSV`, `EXP4_PHY_FAST_FIRST_RX_CSV` | PHY config mismatch, first RX good/no-preamble/SFD/post-SFD error | `[ ]` | PHY mismatch=0; first RX good/no-preamble/SFD/post-SFD=998/1/1/0; EXAMPLE_ONLY/log |
| `EXP4_RX_ERROR_DIAG_*` (옵션 활성 시) | config; events/errors/timeouts/processed/rearmed/overflow; 48bit pre/post counts; 최대64 raw sample; phase별 min/max/avg | `[ ]` | NA_DISABLED (옵션 OFF); 켠 경우 config, 48bit count 48행, raw ≤64행, phase timing 전체와 hash 연결 |

상세 RX 오류 진단이 꺼진 케이스는 `NA_DISABLED`로 기록한다. 옵션이 켜져 수집된 경우 `EXP4_RX_ERROR_DIAG_CSV`, `EXP4_RX_ERROR_BIT_CSV` 48행, `EXP4_RX_ERROR_SAMPLE_CSV` 최대64행, `EXP4_RX_ERROR_TIMING_CSV`의 전 행·hash를 남긴다. 샘플의 `estimated_slot`은 추정 슬롯이며 확정된 송신 노드 ID가 아니다. 진단 활성화가 throughput에 미치는 영향도 별도 기록한다.

### 4.6 Exp5 — 차량 위치별 관측 CIR/PDP/RMS와 수신 품질

Exp5는 **4.3의 TX/RX·CIR_CSV·CIR_DIAG_CSV 전 필드**를 그대로 기록한다. 그 위에 아래 항목을 각 링크×block마다 채운다.

| env·case/link/block | expected/TX/RX/PER·Wilson95 | 성공 수신 CIR/품질 유효·무효 행 수 | raw stride/선택 성공순번 | raw frame 수×sample 수/총 sample 행 | 첫·마지막 실제 cycle | noise threshold·FP 정렬·window·정규화·분석 버전 | 관측 PDP와 프레임별 RMS n·중앙값·P10–P90·min–max | 처리 파라미터 민감도·결측/제외 이유 | 공식 판정·원문/hash |
|---|---|---|---|---|---|---|---|---|---|
| 예시(가상·집계 제외): EXAMPLE_ONLY_CASE/N2/R01 | 1000/1000/999; PER 0.10%; Wilson95 [0.018–0.564]% | CIR 999행; 품질 valid/invalid=995/4 | stride 33; 선택 성공순번=1,34,…,958(가상) | 30×300=9000행 | cycle=7…1008(가상; 성공순번과 별개) | noise=EXAMPLE_ONLY 기준; FP 정렬=0; window=300 samples; 전력 정규화=1; 분석=v1 | 관측 PDP=EXAMPLE_ONLY/pdp.csv; RMS n=30, 중앙 8.0 ns, P10–P90=6.0–11.0 ns, min–max=5.0–13.0 ns | threshold 대안±3 dB 시 중앙 7.5–8.6 ns; 제외=0 | VALID; EXAMPLE_ONLY/raw.csv,pdp.csv; hash=EXAMPLE_ONLY |
| `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |

원시 헤더와 300 complex sample의 **모든 행**을 보존하고 프레임·sample index별 상세표/파형으로 열람한다. 아래는 열의 정의다. 성공순번과 실제 cycle을 같은 값으로 가정하지 않는다.

```text
CIR_RAW_HEADER: frame,cycle,plen,sample_offset,n_samples,fp_sample
# 예시(가상·집계 제외): 1,7,1024,-20,300,101.2
[ ],[ ],[ ],[ ],[ ],[ ]
CIR_RAW: frame,sample_index,I,Q
# 예시(가상·집계 제외): 1,0,12,-3
[ ],[ ],[ ],[ ]
```

PDP/RMS는 이번 Exp5에서 항상 `관측(송수신기·안테나+전파)`으로 표기한다. K-인자·경로손실 지수 n·순수 채널 RMS는 이번 산출 대상이 아니므로 `NOT_ESTIMATED (범위 밖)`로 명시하고 빈칸이나 0으로 표시하지 않는다. M1024/PAC32는 CIR 관측용 PHY 설정이지 순수 채널을 알려 주는 reference가 아니다. Exp2의 M32–M256/PAC4·8과 수신 누산 조건도 다르므로, 링크별 관측값과 Exp2의 M별 PER 사이에 상관이 보여도 최소 M을 그 지표가 결정한다고 주장하지 않는다.

실내 예비자료로 noise threshold·FP 정렬·분석 window·정규화의 기본값과 민감도 대안 범위를 차량 결과를 보기 **전에** 고정한다. 각 프레임의 RMS와 분석 가능/제외 사유를 원시 frame·cycle로 연결한다. 아래 반복 요약은 원시 CIR 30프레임을 독립 차량 위치 30개로 취급하지 않고, 3회 run의 변동을 따로 보여준다.

| 물리 TX serial·차량 위치/INIT RX serial | R01·R02·R03 case/root·판정 | 회차별 RX/expected·PER·Wilson95 | 회차별 raw 분석 가능 n/선택·실제 cycle | 회차별 관측 RMS 중앙값·P10–P90 | 세 회차 중앙값의 min–max | 회차별 FP-SNR 유효 n·중앙값/범위 | 대응 Exp2의 동일 물리 링크 M/PAC별 3회 case/root·PER | 설명적 비교·결측/교란 메모 |
|---|---|---|---|---|---|---|---|---|
| 예시(가상·집계 제외): SERIAL_EXAMPLE_TX/도어; INIT=SERIAL_EXAMPLE_RX | R01/02/03=EXAMPLE_ONLY_CASE_01/02/03; 각 root·판정=EXAMPLE_ONLY/VALID | 999/1000=0.10% [0.018–0.564]; 998/1000=0.20% [0.055–0.726]; 1000/1000=0% [0–0.383] | 30/30/29; 선택 seq·실제 cycle 각 원본 연결 | 8.0 [6–11], 8.4 [6.2–11.3], 7.8 [5.9–10.8] ns | 7.8–8.4 ns | 995/3.1 dB [2–5]; 994/3.3 [2–5]; 998/3.0 [2–4] | M32/PAC8×3회=EXAMPLE_ONLY_CASE; PER=0.1/0.2/0% | 동일 배치 확인=예시만; RMS로 최소 M 결정하지 않음 |
| `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` | `[ ]` |

Exp2와 비교할 때 환경·물리 serial·안테나 배치·회차 순서가 다르면 일치하는 링크처럼 합치지 않고 차이를 메모한다. Exp5의 M1024/PAC32 관측 RMS로 Exp2의 M32–M256 최소 신뢰 M을 역산하지 않는다.

### 4.7 집 원고 → 차량 원고 교체 확인

| 확인 항목 | 결과·증거 | 입력 예시(가상·집계 제외) |
|---|---|---|
| 각 `paper_slot_id`에 집/차량 **별도 case와 원본 hash**가 있는가? 누락 칸을 표시했는가? | `[ ]` | PASS; slot=EXAMPLE_ONLY_EXP2_N2_R01; 집/차량 별도 root·hash 연결; 누락=0 |
| PHY·PAC·S·K·block·payload·guard·산식이 일치하는가? 환경별 lead·serial·위치 차이를 설명했는가? | `[ ]` | PASS; 동일 PHY/PAC/S/K/payload/guard; lead 차이 2 µs·serial/위치 차이 본문 주석 |
| 6링크·12블록 완료 여부, 원본 FAIL_PER와 모든 재시도·제외 사유를 반영했는가? | `[ ]` | PARTIAL; Exp4 R07 1개 NOT_MEASURED; 원본 FAIL_PER 2개·재시도 1개 별도 표시 |
| 표·그림·통계·초록·결론을 차량 원본으로 재생성하고 집 판본을 보존했는가? | `[ ]` | PASS; figure source hash=EXAMPLE_ONLY; 집 판본=EXAMPLE_ONLY/home_snapshot |
| Exp3의 헤더 시간 차감, Exp5의 관측 RMS가 헤더 없는 PHY/순수 채널 측정으로 과장되지 않았는가? K/n·RMS가 최소 M을 결정한다는 미검증 주장을 제외했는가? | `[ ]` | PASS; Exp3는 표준 프레임 차이만, Exp5는 관측 RMS만 서술; K/n·최소 M 인과 문구 없음 |

문서에 정의되지 않은 새 firmware marker가 생기면 그 marker의 헤더, 원시행 수·경로·hash와 해석 상태를 결과 표에 추가한 뒤 분석한다. 빈칸은 측정이 아직 없다는 뜻이지 0이나 PASS가 아니다.


## 5. 환경별 전체 조건·반복회차 추적표

집(`HOME_PROVISIONAL`)과 최종 차량(`VEHICLE_FINAL`)에 **아래 표를 각각 한 벌** 둔다. 이는 누락 없는 결과 열람·논문 대응을 위한 예정 행 목록이지 손으로 채우는 작업 계획이 아니다. 실행 후 각 `[ ]`를 원본에서 추출한 `case_id / 핵심 측정값 / 공식 판정 / 원시 root`로 채우고 4장의 상세 표·원시 파일에 연결한다. 미실행은 `NOT_MEASURED`, 무효는 `INVALID`, 유효한 PER 실패는 `FAIL_PER_VALID`로 표시한다. 빈칸을 합격이나 0으로 해석하지 않는다.

각 표 맨 위의 `예시(가상·집계 제외)` 행은 **한 칸의 기입 형식**을 실제 반복 열 R01–R12에 대응시켜 보여준다. 계획된 실제 행과 케이스 수에는 포함하지 않는다. 12블록 Exp4처럼 가로로 긴 표는 읽기 쉽게 표 바로 앞에 공통 셀 예시를 적고, 모든 R칸에 같은 서식을 적용한다.

### 5.1 Stage0 grid: 세 설정의 lead 0–40 µs 전체

행마다 `TX/RX, PER, 성공 ACCUM histogram, 실패 ACCUM valid/invalid·오류 종류, timing, 판정, root`를 표시한다. Stage0 grid는 각 lead를 1회 탐색하고 선정 lead는 다음 표에서 3회 확인한다.

| PHY | lead µs | TX/RX·PER | 성공 ACCUM | 실패 ACCUM 유효/무효·오류 | timing·판정 | case/root |
| --- | --- | --- | --- | --- | --- | --- |
| 예시(가상·집계 제외): M32/PAC8 | 24 | 2000/1998; PER 0.10%; Wilson95 [0.027–0.364]% | min/max/avg=8/9/8.4; hist={8:1200,9:798} | fwto 2건; valid 1/invalid_no_rxprd 1; valid_hist={8:1} | slot min/max/avg/n=100/160/130 ns/1998; VALID | EXAMPLE_ONLY_CASE; EXAMPLE_ONLY/root/stage0.log |
| M32/PAC4 | 0 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 1 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 2 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 3 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 4 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 5 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 6 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 7 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 8 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 9 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 10 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 11 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 12 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 13 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 14 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 15 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 16 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 17 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 18 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 19 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 20 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 21 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 22 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 23 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 24 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 25 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 26 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 27 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 28 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 29 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 30 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 31 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 32 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 33 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 34 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 35 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 36 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 37 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 38 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 39 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC4 | 40 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 0 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 1 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 2 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 3 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 4 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 5 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 6 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 7 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 8 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 9 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 10 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 11 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 12 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 13 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 14 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 15 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 16 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 17 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 18 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 19 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 20 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 21 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 22 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 23 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 24 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 25 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 26 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 27 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 28 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 29 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 30 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 31 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 32 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 33 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 34 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 35 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 36 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 37 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 38 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 39 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | 40 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 0 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 1 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 2 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 3 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 4 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 5 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 6 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 7 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 8 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 9 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 10 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 11 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 12 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 13 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 14 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 15 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 16 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 17 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 18 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 19 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 20 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 21 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 22 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 23 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 24 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 25 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 26 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 27 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 28 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 29 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 30 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 31 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 32 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 33 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 34 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 35 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 36 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 37 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 38 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 39 | [ ] | [ ] | [ ] | [ ] | [ ] |
| M1024/PAC32 | 40 | [ ] | [ ] | [ ] | [ ] | [ ] |

### 5.2 Stage0 선정 lead 확인: PHY별 3회

전체 grid에서 고른 M32/PAC4·M32/PAC8·M1024/PAC32 lead를 **각각 2,000프레임씩 3회** 재확인한다(총 9케이스). 이는 기존 Stage0의 N4 단일 링크에서 선정 lead를 확인하는 단계이며, 다른 물리 링크까지 통과했다는 뜻은 아니다. 6링크 성능은 Exp2에서 별도로 본다. 각 칸은 `lead / TX·RX/expected / PER / Wilson95 / 성공·유효 실패 ACCUM / 오류 종류 / 공식 판정 / case·root`를 보여준다. 세 유효 반복 각각 PER <1%, 합산 Wilson 95% 상한 <1%, 오류·ACCUM 경계의 불안정 여부를 함께 판단한다. 0/6,000 손실일 때 Wilson 상한은 약 0.064%지만, 이는 시간 상관·환경 변화가 없다는 증거가 아니므로 반복별 값도 숨기지 않는다. 세 반복이 불일치하면 동결하지 않고 원인을 검토한다.

| PHY | R01 | R02 | R03 |
| --- | --- | --- | --- |
| 예시(가상·집계 제외): M32/PAC8 | lead24; TX/RX=2000/1998; PER0.10% [0.027–0.364]; ACCUM mode8; fwto2; VALID; EXAMPLE_ONLY_R01/root | lead24; 2000/1999; 0.05% [0.009–0.283]; mode8; fwto1; VALID; EXAMPLE_ONLY_R02/root | lead24; 2000/2000; 0% [0–0.192]; mode8; 오류0; VALID; EXAMPLE_ONLY_R03/root |
| M32/PAC4 | [ ] | [ ] | [ ] |
| M32/PAC8 | [ ] | [ ] | [ ] |
| M1024/PAC32 | [ ] | [ ] | [ ] |

### 5.3 조건부: M64/M128/M256의 lead 별도 검증 (최대 81케이스)

**무엇을 하나?** Stage0의 123점 grid는 M32/PAC4·M32/PAC8·M1024/PAC32만 측정한다. 따라서 Exp2의 M64/M128/M256에서 M32/PAC8 lead를 그대로 쓰면, 길이 효과와 lead 정렬 실패를 구분하기 어렵다. 이 절은 긴 PHY마다 lead가 맞는지 Exp2 **전에** 확인하는 별도 보정안이다. Exp2의 FP-SNR/PER 측정 자체를 대신하지 않는다.

**왜 최대 81인가?** 긴 PHY별로 PAC8 위상·ACCUM·PER를 근거로 예측한 중심 주변 9개 연속 lead를 한 링크(N4)에서 탐색한다(3×9=27). 선정 lead를 각 PHY의 6개 물리 링크에서 3회 확인한다(3×6×3=54). 합계 최대 81이다. 아래 P01–P09와 R01–R03는 서로 다른 단계다.

**현재 상태:** 이 81개는 v2 문서의 조건부 제안이며 현재 실행기/기존 1,593케이스 매니페스트에 추가되어 있지 않다. 별도 실행을 결정하지 않으면 `NOT_MEASURED`로 두고, 긴 PHY의 lead가 독립 검증되지 않았음을 Exp2 해석에 표시한다. 예측 근거가 없으면 `UNRESOLVED`로 두며 9점을 임의로 실행하지 않는다. 탐색의 µs 값은 PHY별 예측과 범위를 확정한 뒤 채운다. 각 행에 `lead·PER·성공/유효 실패 ACCUM·판정·root`를 표시한다.

| PHY | 탐색점 | lead µs | RX/expected·PER | ACCUM·경계 | 판정·case/root |
| --- | --- | --- | --- | --- | --- |
| 예시(가상·집계 제외): M64/PAC8 | P01 | 20 | 1990/2000; PER0.50%; Wilson95는 산출값 기입 | 성공 mode8; 유효 실패 mode7; PAC8 경계 후보 | VALID; EXAMPLE_ONLY_SCAN_P01/root |
| M64/PAC8 | P01 | [ ] | [ ] | [ ] | [ ] |
| M64/PAC8 | P02 | [ ] | [ ] | [ ] | [ ] |
| M64/PAC8 | P03 | [ ] | [ ] | [ ] | [ ] |
| M64/PAC8 | P04 | [ ] | [ ] | [ ] | [ ] |
| M64/PAC8 | P05 | [ ] | [ ] | [ ] | [ ] |
| M64/PAC8 | P06 | [ ] | [ ] | [ ] | [ ] |
| M64/PAC8 | P07 | [ ] | [ ] | [ ] | [ ] |
| M64/PAC8 | P08 | [ ] | [ ] | [ ] | [ ] |
| M64/PAC8 | P09 | [ ] | [ ] | [ ] | [ ] |
| M128/PAC8 | P01 | [ ] | [ ] | [ ] | [ ] |
| M128/PAC8 | P02 | [ ] | [ ] | [ ] | [ ] |
| M128/PAC8 | P03 | [ ] | [ ] | [ ] | [ ] |
| M128/PAC8 | P04 | [ ] | [ ] | [ ] | [ ] |
| M128/PAC8 | P05 | [ ] | [ ] | [ ] | [ ] |
| M128/PAC8 | P06 | [ ] | [ ] | [ ] | [ ] |
| M128/PAC8 | P07 | [ ] | [ ] | [ ] | [ ] |
| M128/PAC8 | P08 | [ ] | [ ] | [ ] | [ ] |
| M128/PAC8 | P09 | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | P01 | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | P02 | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | P03 | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | P04 | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | P05 | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | P06 | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | P07 | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | P08 | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | P09 | [ ] | [ ] | [ ] | [ ] |

확인 반복의 각 칸: `선정 lead / RX/expected / PER / Wilson95 / ACCUM / 판정 / case/root`. 링크는 실제 물리 serial과 위치로도 확인한다.

| PHY | 물리 링크 | R01 | R02 | R03 |
| --- | --- | --- | --- | --- |
| 예시(가상·집계 제외): M64/PAC8 | N2=SERIAL_EXAMPLE_TX/도어 | lead25; 999/1000; PER0.10% [0.018–0.564]; ACCUM mode9; VALID; EXAMPLE_ONLY_R01/root | lead25; 998/1000; PER0.20% [0.055–0.726]; mode9; VALID; EXAMPLE_ONLY_R02/root | lead25; 1000/1000; PER0% [0–0.383]; mode9; VALID; EXAMPLE_ONLY_R03/root |
| M64/PAC8 | N2 | [ ] | [ ] | [ ] |
| M64/PAC8 | N3 | [ ] | [ ] | [ ] |
| M64/PAC8 | N4 | [ ] | [ ] | [ ] |
| M64/PAC8 | N5 | [ ] | [ ] | [ ] |
| M64/PAC8 | N6 | [ ] | [ ] | [ ] |
| M64/PAC8 | N7 | [ ] | [ ] | [ ] |
| M128/PAC8 | N2 | [ ] | [ ] | [ ] |
| M128/PAC8 | N3 | [ ] | [ ] | [ ] |
| M128/PAC8 | N4 | [ ] | [ ] | [ ] |
| M128/PAC8 | N5 | [ ] | [ ] | [ ] |
| M128/PAC8 | N6 | [ ] | [ ] | [ ] |
| M128/PAC8 | N7 | [ ] | [ ] | [ ] |
| M256/PAC8 | N2 | [ ] | [ ] | [ ] |
| M256/PAC8 | N3 | [ ] | [ ] | [ ] |
| M256/PAC8 | N4 | [ ] | [ ] | [ ] |
| M256/PAC8 | N5 | [ ] | [ ] | [ ] |
| M256/PAC8 | N6 | [ ] | [ ] | [ ] |
| M256/PAC8 | N7 | [ ] | [ ] | [ ] |

### 5.4 Exp2: 5 PHY×6링크×3회 = 90칸

**이 표에서 가장 먼저 읽을 값은 PHY·링크·반복별 PER와 FP-SNR이다.** 각 R칸에는 `TX/RX/PER/Wilson95 ; FP-SNR valid n + ratio min/avg/max ; ACCUM mode·분포 ; RSSI/FP valid n ; 공식 판정 ; case/root`를 표시한다. FP-SNR의 dB 중앙값·분위수는 4.3의 유효 프레임을 후처리했을 때만 추가하고, 원본 ratio와 구별한다. 각 칸은 4.3의 프레임별 FP-SNR/CIR 상세표로 연결한다. 3회 평균만 보여주지 말고 반복별 변화·최악 링크·무효 비율을 함께 본다. PER은 전체 전송 기회 기준, FP-SNR은 **성공 수신 조건부**이므로 둘을 같은 표본 집단처럼 해석하지 않는다.

논문용 비교 그림은 **(1) PHY별·링크별 PER, (2) PHY별·링크별 유효 FP-SNR dB 분포와 3회 변동, (3) ACCUM–FP-SNR 관계**를 기본으로 예상한다. M32의 PAC4와 PAC8은 별도 표식으로 구분한다. FP-SNR이 올라가도 손실 프레임의 미관측 품질까지 좋아졌다고 주장하지 않는다.

| PHY | 물리 링크 | R01: PER·FP-SNR | R02: PER·FP-SNR | R03: PER·FP-SNR |
| --- | --- | --- | --- | --- |
| 예시(가상·집계 제외): M32/PAC8 | N2=SERIAL_EXAMPLE_TX/도어 | TX/RX=1000/999; PER0.10% [0.018–0.564]; FP-SNR valid n=995, ratio×1000 min/avg/max=1500/2000/2800; ACCUM mode9; RSSI/FP valid995; VALID; EXAMPLE_ONLY_R01/root | 1000/998; 0.20% [0.055–0.726]; FP-SNR n994, ratio=1400/1950/2700; mode9; RSSI/FP n994; VALID; EXAMPLE_ONLY_R02/root | 1000/1000; 0% [0–0.383]; FP-SNR n998, ratio=1600/2100/2900; mode9; RSSI/FP n998; VALID; EXAMPLE_ONLY_R03/root |
| M32/PAC4 | N2 | [ ] | [ ] | [ ] |
| M32/PAC4 | N3 | [ ] | [ ] | [ ] |
| M32/PAC4 | N4 | [ ] | [ ] | [ ] |
| M32/PAC4 | N5 | [ ] | [ ] | [ ] |
| M32/PAC4 | N6 | [ ] | [ ] | [ ] |
| M32/PAC4 | N7 | [ ] | [ ] | [ ] |
| M32/PAC8 | N2 | [ ] | [ ] | [ ] |
| M32/PAC8 | N3 | [ ] | [ ] | [ ] |
| M32/PAC8 | N4 | [ ] | [ ] | [ ] |
| M32/PAC8 | N5 | [ ] | [ ] | [ ] |
| M32/PAC8 | N6 | [ ] | [ ] | [ ] |
| M32/PAC8 | N7 | [ ] | [ ] | [ ] |
| M64/PAC8 | N2 | [ ] | [ ] | [ ] |
| M64/PAC8 | N3 | [ ] | [ ] | [ ] |
| M64/PAC8 | N4 | [ ] | [ ] | [ ] |
| M64/PAC8 | N5 | [ ] | [ ] | [ ] |
| M64/PAC8 | N6 | [ ] | [ ] | [ ] |
| M64/PAC8 | N7 | [ ] | [ ] | [ ] |
| M128/PAC8 | N2 | [ ] | [ ] | [ ] |
| M128/PAC8 | N3 | [ ] | [ ] | [ ] |
| M128/PAC8 | N4 | [ ] | [ ] | [ ] |
| M128/PAC8 | N5 | [ ] | [ ] | [ ] |
| M128/PAC8 | N6 | [ ] | [ ] | [ ] |
| M128/PAC8 | N7 | [ ] | [ ] | [ ] |
| M256/PAC8 | N2 | [ ] | [ ] | [ ] |
| M256/PAC8 | N3 | [ ] | [ ] | [ ] |
| M256/PAC8 | N4 | [ ] | [ ] | [ ] |
| M256/PAC8 | N5 | [ ] | [ ] | [ ] |
| M256/PAC8 | N6 | [ ] | [ ] | [ ] |
| M256/PAC8 | N7 | [ ] | [ ] | [ ] |

### 5.5 Exp3: A/B/C×3회 = 9칸

각 칸: `EXTTXE mean/median/min/max ns / RX/1000·PER·Wilson95 / 판정 / case/root`. 각 반복의 세 변형은 같은 배치·PHY 조건에서 측정한다. 회차별 폭과 짝 차분의 범위는 아래 차분표와 4.1a 반복 요약에 남긴다.

| 변형 | R01 (A→B→C) | R02 (C→B→A) | R03 (B→A→C) |
| --- | --- | --- | --- |
| 예시(가상·집계 제외): A/SFD8/STD | EXTTXE mean/median/min/max=250000/250001/249990/250010 ns; RX999/1000, PER0.10% [0.018–0.564]; VALID; EXAMPLE_ONLY_A_R01/root | 250003/250002/249995/250011 ns; RX1000/1000, PER0% [0–0.383]; VALID; EXAMPLE_ONLY_A_R02/root | 249999/250000/249988/250009 ns; RX998/1000, PER0.20% [0.055–0.726]; VALID; EXAMPLE_ONLY_A_R03/root |
| A: SFD8·STD PHR | [ ] | [ ] | [ ] |
| B: SFD16·STD PHR | [ ] | [ ] | [ ] |
| C: SFD8·DTA PHR | [ ] | [ ] | [ ] |

반복별 차분을 **그 반복의 측정값끼리** 계산한다. 세 반복의 평균·산포는 세 차분에서 산출한다.

| 차분 | R01 ns / PER 근거 | R02 ns / PER 근거 | R03 ns / PER 근거 | 3회 평균·범위/불확도 |
| --- | --- | --- | --- | --- |
| 예시(가상·집계 제외): B−A | B 258142−A 250000=+8142 ns; A/B PER=0.10/0.20% | B 258148−A 250003=+8145 ns; A/B PER=0/0.10% | B 258137−A 249999=+8138 ns; A/B PER=0.20/0.10% | 평균+8141.7 ns; 범위+8138…+8145 ns; 불확도 산식=사전 지정 후 기입 |
| B−A | [ ] | [ ] | [ ] | [ ] |
| A−C | [ ] | [ ] | [ ] | [ ] |

### 5.6 Exp4: PHY×활성 수 또는 application K×12블록 = 576칸

블록 셀은 `case_id / offered·admitted·TX·RX / worst-node PER / goodput / 공식 판정 / root`를 적는다. 블록 R01–R06과 R07–R12는 같은 물리 역할 회전을 두 번 수행한다. 각 셀의 6개 물리 노드값·슬롯별값·수요 CSV·timing/error marker는 4.5의 상세 카드에 기입한다. 블록별·물리 노드별 PER Wilson95와 12블록 PER·goodput 범위/합산값은 4.1a에 별도 표시한다.

S1–S5: 각 S에서 application K=S.

R01–R12 **공통 셀 예시(가상·집계 제외):** `EXAMPLE_ONLY_S1_R01 / offered=1000, admitted=1000, TX=1000, RX=999 / worst-node PER=0.10% / goodput=12.79 kb/s(16 B payload·10 s 가정) / VALID / EXAMPLE_ONLY/root`. 각 R칸에는 해당 회차의 값과 물리 serial→논리 역할 매핑을 따로 연결한다. 실제 12칸은 아래에서 비워둔다.

| PHY | 활성 TX | R01 | R02 | R03 | R04 | R05 | R06 | R07 | R08 | R09 | R10 | R11 | R12 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M32/PAC8 | S1 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | S2 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | S3 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | S4 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | S5 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | S1 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | S2 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | S3 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | S4 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | S5 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |

S6: K는 controller-generated application 요청 수다. admitted·admission drop과 실제 TX/RX를 분리한다.

R01–R12 **공통 셀 예시(가상·집계 제외):** `EXAMPLE_ONLY_K8_R01 / offered=8000, admitted=6000, admission_drop=2000, TX=5990, RX=5980 / worst-node PER=0.50% / goodput=76.54 kb/s(16 B payload·10 s 가정) / VALID / EXAMPLE_ONLY/root`. 다음 R칸에도 **새 case/root와 그 회차의 수요·송신·수신값**을 넣으며 위 숫자를 복사하지 않는다.

| PHY | application K | R01 | R02 | R03 | R04 | R05 | R06 | R07 | R08 | R09 | R10 | R11 | R12 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M32/PAC8 | K6 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K7 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K8 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K9 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K10 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K11 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K12 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K13 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K14 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K15 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K16 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K17 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K18 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K19 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K20 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K21 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K22 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K23 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M32/PAC8 | K24 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K6 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K7 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K8 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K9 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K10 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K11 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K12 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K13 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K14 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K15 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K16 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K17 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K18 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K19 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K20 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K21 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K22 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K23 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| M256/PAC8 | K24 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |

### 5.7 Exp5 관측 CIR·수신 품질: 6링크×3회 = 18칸

각 칸: `TX/RX/PER·Wilson95 / CIR·품질 유효 행 수 / raw frame×300 sample 행 수 / 관측 RMS 프레임별 n·중앙값·P10–P90·min–max / 처리 버전·민감도 / 판정 / case/root`. 실제 cycle과 성공 수신순번은 4.6에 연결한다. 각 물리 링크의 R01–R03 관측 RMS·FP-SNR·PER 범위는 4.6의 반복 요약에 별도로 표시한다.

| 물리 링크 | R01 | R02 | R03 |
| --- | --- | --- | --- |
| 예시(가상·집계 제외): N2=SERIAL_EXAMPLE_TX/도어 | TX/RX=1000/999; PER0.10% [0.018–0.564]; CIR valid999; raw30×300=9000; 관측 RMS n30, 중앙8.0 ns, P10–P90=6–11, min–max=5–13; 분석v1; VALID; EXAMPLE_ONLY_R01/root | 1000/998; 0.20% [0.055–0.726]; CIR valid998; raw30×300; RMS n30, 중앙8.4 ns, P10–P90=6.2–11.3; 분석v1; VALID; EXAMPLE_ONLY_R02/root | 1000/1000; 0% [0–0.383]; CIR valid1000; raw30×300; RMS n29, 중앙7.8 ns, P10–P90=5.9–10.8; 제외1; 분석v1; VALID; EXAMPLE_ONLY_R03/root |
| N2 | [ ] | [ ] | [ ] |
| N3 | [ ] | [ ] | [ ] |
| N4 | [ ] | [ ] | [ ] |
| N5 | [ ] | [ ] | [ ] |
| N6 | [ ] | [ ] | [ ] |
| N7 | [ ] | [ ] | [ ] |

### 5.8 반복 완료 검산

| 단계 | 조건 수 | 회차 수 | 예정 case | 실제 유효·FAIL_PER·INVALID·미측정 | 원본/판정 링크 |
| --- | --- | --- | --- | --- | --- |
| 예시(가상·집계 제외): Stage0 grid | 3 PHY×41 lead | 1 | 123 | VALID=120; FAIL_PER_VALID=2; INVALID=1; NOT_MEASURED=0(가상 집계) | EXAMPLE_ONLY/root; ledger·ASSESSMENT 경로와 hash |
| Stage0 grid | 3 PHY×41 lead | 1 | 123 | [ ] | [ ] |
| Stage0 확인 | 3 PHY | 3 | 9 | [ ] | [ ] |
| 긴 PHY 탐색·확인 | 3 PHY×9 lead + 3 PHY×6링크 | 1 + 3 | 조건부 81 | [ ] | [ ] |
| Exp2 | 5 PHY×6링크 | 3 | 90 | [ ] | [ ] |
| Exp3 | 3 변형 | 3 | 9 | [ ] | [ ] |
| Exp4 S1–S5 | 2 PHY×5 S | 12 | 120 | [ ] | [ ] |
| Exp4 S6 | 2 PHY×19 K | 12 | 456 | [ ] | [ ] |
| Exp5 | 6링크 | 3 | 18 | [ ] | [ ] |
| 합계 | 본 실험 825 + 긴 PHY 최대 81 | 환경마다 별도 | 최대 906 | [ ] | [ ] |
