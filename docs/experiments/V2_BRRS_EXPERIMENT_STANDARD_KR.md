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

## 4. 실험 기록표

[Excel 기록표](outputs/01a0cdf1-2f17-7f21-a220-3e3e7eee0235/V2_BRRS_EXPERIMENT_RECORD_KR.xlsx)에 조건별·반복별 기입 칸, 공통 배치·PHY 조건, 단계별 측정값, 원본 marker 목록, 반복 집계, 차트 연결을 둔다. 이 파일은 **실험 설계와 결과 열람용 빈 기록표**이며 실행기·매니페스트·공식 판정·원본 로그를 변경하지 않는다. 자동 로그 추출기가 이미 구현됐다는 뜻도 아니다.

기록표의 한 행은 환경과 paper_slot_id가 정하는 **한 예정 회차**다. 현재 준비된 차량 실행안 1,593칸, 이 문서의 V2안은 집·차량 각각 825칸, 긴 PHY lead 확인은 환경별 조건부 81칸을 표시한다. 겹치는 차량 조건은 같은 행의 포함 여부 열로 구분해 이중 계산하지 않는다. 아직 최종 실행 목록을 확정한 것은 아니다.

실행 후에는 각 행에 실제 case_id·attempt_id·물리 serial/역할·배치·lead·SYNC/DATA preamble code와 확인 근거·TX/RX/오류·공식 판정·원본 경로와 SHA256을 연결한다. 원본에서 확인되지 않은 값은 숫자 0이 아니라 미측정/무효 상태로 남긴다. 상세 원시 CIR·slot·application demand·프레임 행은 봉인 파일의 경로·hash·행 수·헤더·누락/중복 검사로 연결하고, 엑셀 요약이 원본을 대체하지 않는다. 새 숫자형 marker도 누락하지 않고 원본목록에 추가한다.

실제 기입 상태는 PLANNED, NOT_MEASURED, VALID, FAIL_PER_VALID, INVALID, INCOMPLETE, EXCLUDED_WITH_REASON, UNKNOWN을 구분한다. 유효한 PER 실패는 평균과 범위에서 지우지 않는다. 재시도는 새 ID 행으로 추가하고 최초 원본·판정을 보존한다. Exp4에는 별도 재시도 100행(1922–2021행)이 준비돼 있고, 차트 수식은 5000행까지 참조한다. 그 밖의 단계나 추가 Exp4 재시도는 해당 시트에 새 행을 추가하고 수식·집계 범위를 검산한다. 예시 시트의 값은 **가상·집계 제외**이며 실측 시트에 복사하지 않는다.

Exp4노드 시트에는 예정 Exp4 1,920회차의 활성 노드별 10,500행이 있다. `paper_slot_id`·환경·블록과 논리 N2…N(S+1)은 계획값이며 실제 송신의 증거가 아니다. 논리 슬롯은 고정되고 물리 보드는 블록별로 회전한다. `case_id`·실제 물리 serial·위치·노드별 값·원본 경로는 실행 후 기입한다. 재시도에서 노드별 자료를 얻으면 같은 `paper_slot_id`라도 새 `case_id`로 노드행을 추가한다.

반복 결과는 회차별 분자·분모·값, 프레임 기준 Wilson 95% 구간, 회차 간 min–max를 별도로 보존한다. 두 불확도 표현을 같은 것으로 부르지 않는다. Exp3의 폭 차분과 Exp4 goodput에는 Wilson을 붙이지 않는다. 현재 차트 시트는 Exp4 차량 `M32/PAC8`·`M256/PAC8`의 유효 실측 goodput만 표시하는 입력 점검용이다. Exp4의 `논문 집계 대상` 열은 예정행 YES·재시도 준비행 NO로 시작한다. 재시도를 채택할 때는 원본을 보존한 채 해당 조건의 유효 행 중 **환경+paper_slot_id당 하나만 YES**로 지정하고 중복을 검산한다. 유효한 `FAIL_PER_VALID`는 집계 대상에서 임의로 빼지 않는다. 최종 논문 figure의 신뢰구간·반복 범위 표시 방식은 실측 자료와 최종 조건표를 확정한 뒤 정한다. 빈 차트는 측정 전 상태이며 0이라는 뜻이 아니다.

## 5. 집 자료와 최종 차량 자료의 교체 검산

동일 paper_slot_id의 집과 차량 기록을 PHY·PAC·물리 링크 또는 활성 TX 수·application K·반복 block·payload·guard·산식 기준으로 대조한다. 환경별 선정 lead 숫자와 물리 serial·위치 차이는 기록하되 비교 키를 임의로 바꾸지 않는다. 차량에서 측정하지 못한 칸은 NOT_MEASURED로 두며 집 값이나 보간값을 차량 실측으로 표시하지 않는다.

논문 표·그림·통계·초록·결론을 차량 원본으로 다시 산출할 때는 집 판본과 양쪽 원본 hash를 보존한다. Exp3의 표준 프레임 airtime 차이는 헤더 없는 PHY 복호 검증이 아니며, Exp5의 관측 RMS는 순수 전파 RMS가 아니다. 라이시안 K-인자와 경로손실 지수는 이번 필수 산출 항목이 아니다.

준비된 1,593케이스 실행안과 이 V2 문서의 825케이스 제안은 범위가 다르다. 특히 Stage0 confirmation은 5회 대 3회, Exp3는 1회 대 3회, Exp4는 1,344회 대 576회다. 실행 시간 계측 후 어느 범위를 최종 논문 조건으로 삼을지 별도로 결정한다.
