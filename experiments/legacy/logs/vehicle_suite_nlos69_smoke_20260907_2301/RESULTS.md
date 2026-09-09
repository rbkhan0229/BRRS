# 차량 실험 사전 리허설 · 현재 NLOS 6.9m · 2026-09-07

**선택한 대표 RF 7회 모두 정상 수집·PER0%로 통과했다.** 바로 앞 Exp4 TX6/13슬롯 결과도 각 노드 PER<1%였으므로 중복 실행하지 않고 함께 참조한다. 이번 작업은 현재 NLOS에서 실제 송수신·수집·판정·CSV/그림 분석 연결을 확인한 것이며, 차량 실측이나 전체 논문 행렬 검증 완료는 아니다.

RX1050270933, 모든1:1 TX는 사용자 지정 물리N4(1050282818)이며 단일 링크 논리 ID는N2다. N2/N3/N5/N6/N7은 연결을 유지하고 모든 case 준비·종료에서 MCU 정지 상태를 검증했다. SSH s-macbook-air, 보드7대, 사용자가 확인한 TX 허브 외부 전원 및 현재 NLOS6.9m 배치를 유지했다. 상세 위치·거리·높이·방향은 독립 계측하지 않았다.

준비 프로파일에서 조건당1회, 공통 lead25µs/tail0을 사용했다. lead25는 직전 검증 조건으로 고정한 진단값이며 PAC별 최적값이나 논문용 동결값으로 선정하지 않았다. Stage0 한 점 뒤 바로 대표 경로를 검사했으며 전체 탐색을 수행한 것으로 취급하지 않는다.

## 실행 결과

| 단계 | 대표 조건 | 수신/예정 | PER | 수집·검증 | KST 제어·수집 구간 |
|---|---|---:|---:|---|---|
| Stage0 | M32/PAC8/lead25 | 2000/2000 | 0.00% | TX/종료/metadata/flash PASS | 23:02:44~23:03:35 |
| Exp1 | M64/PAC8/lead25 | 2000/2000 | 0.00% | TX/종료/metadata/flash PASS | 23:05:01~23:05:47 |
| Exp2 | M32/PAC8/lead25 | 1000/1000 | 0.00% | CIR 1000행 | 23:05:49~23:06:28 |
| Exp3 A | M32/PAC8/lead25 | 1000/1000 | 0.00% | EXTTXE 1000개 | 23:06:30~23:07:09 |
| Exp3 B | M32/PAC8/lead25 | 1000/1000 | 0.00% | EXTTXE 1000개 | 23:07:10~23:07:44 |
| Exp3 C | M32/PAC8/lead25 | 1000/1000 | 0.00% | EXTTXE 1000개 | 23:07:46~23:08:20 |
| Exp5 | M1024/PAC32/lead25 | 1000/1000 | 0.00% | CIR 1000행, raw 30×300샘플 | 23:08:22~23:09:07 |
| Exp4 (직전 실행 재사용) | M32/PAC8, TX6,13슬롯,G250,lead25 | 12976/13000 | 전체0.1846%, 노드최대0.70% | 7대 제어·TX/RX·readback PASS | 22:50:25~22:51:21 |

Stage0와Exp1의 M32/PAC8/lead25 RX/TX HEX가 완전히 같아 Exp1 M32는 build-only까지만 확인하고 RF를 생략했다. Exp1은 M64로 심볼 변경 경로를 별도 확인했다. M64 사용은 실험 경로 점검이며 M32 고손실을 우회하는 해결책으로 채택한 것이 아니다. 단일 링크 논리ID N2를 실제 물리N2 보드로 혼동하지 않는다.

신규7회에서 TX는 총9,000/9,000회 송신했고 각 조건의RX도예정량과 일치했다. 각 case READY/END, 실제 역할·serial, 설정·HEX·raw hash, 수집 상태와flash readback을 통과했다. 비참여TX5의 정지가 모두 유지됐고 수집timeout은 없었다. 각 단계의RX PHY·예약·설정 및TX SYNC/설정 진단 원문은RESULTS.json의new_runs/raw_diagnostics에 보존했다. 단계마다 없는 카운터를0으로 만들어 채우지 않았다.

## 수집한 측정값과 후처리

- Exp1 M64: 결과·accum histogram CSV 및PER/accum/error PNG·SVG 생성 정상. 그림은lead25 한 점이며 lead의 전이구간을 측정한 것이 아니다.
- Exp2 M32: 성공 패킷1,000개에 대응하는 CIR1,000행, sample/summary CSV와FP-SNR SVG 생성 정상.
- Exp3 A/B/C: 각각1,000개 하드웨어TIMER4 EXTTXE 캡처를 확보했다. 평균 폭은A95.881544µs, B104.090884µs, C76.973006µs. B−A=8.209340µs(로그의 이론값8.141µs 대비+0.068340µs), A−C=18.908538µs(18.846µs 대비+0.062538µs). absolute/differential/RX-PER PNG와CSV 생성 정상. 단일 실행이며 장기·보드간 재현성을 확정하지 않는다.
- Exp5 M1024/PAC32: CIR1,000행과raw30프레임×300샘플을 검증했다. 채널 분석기strict 검사와프레임별CSV 생성 정상. 관측PDP RMS 폭 평균28.91ns는 수신기 응답도 포함하며 독립 전파 지연확산값으로 해석하지 않는다.

## 발견해 보완한 후처리 연결

무선 펌웨어나 수집 원문은 수정하지 않았다. 독립 fix 사본의 분석기2개를 보완했다.

1. brrs_exp3_exttxe_analyze.py: 현재 EXP3_TX_RESULT에 추가된 end=1을 이전 정규식이 읽지 못해 정상 A 결과를 누락했다. 새 필드를 인식하고 end≠1은 거부하며, 과거 end 필드가 없는 로그도 지원했다. 현재 A/B/C 각 1,000샘플, 과거 로그 호환, END 실패·펌웨어 FAIL·샘플 누락 거부를 검증했다.
2. brrs_exp1_log_to_csv_plot.py: 이전 종료 필드 순서와 leadNN 파일명에 의존해 새 EXP1_DONE 및 bundle의 init.log를 처리하지 못했다. key/value 필드를 읽고 기존 수집 검증기의 PAC·RX 모드·END·예약·PER 검사를 공유하도록 보완했다. leadNN과 _lNN 파일명의 불일치는 계속 거부하며 init.log는 헤더와 종료 마커로 검증한다. CSV에 PAC/RX 모드를 보존했다. 실제 현재 로그·과거 로그 호환 및 잘못된 PAC/END/수집 상태/카운터/중복 필드/파일명 lead 거부를 확인했다. 기존 그림에 고정돼 있던 “4~6µs 전이” 표시는 제거해 미측정 결론이 보이지 않게 했다.

수정 전후 스크립트와 patch, 회귀검사 결과는 exp1_analyzer_* / exp3_analyzer_*에 보존했다. 캡처 bundle은 불변으로 남겨 이번 실행에 쓴 도구 hash를 보존하며, 수정된 후처리 분석은 로컬 fix 사본의 새 파일로 실행했다. 다음에 prepare하는 bundle에는 수정본이 포함된다. 후처리 수정 때문에 RF를 다시 실행하지 않았다.

## 종료 상태와 다음 순서

RX와 N4에는 직전 통과한 Exp4 M32/PAC8/S6/13슬롯 HEX를 복원했다. 원격 나머지 5대는 재플래시하지 않고 기존 Exp4 HEX를 readback으로 대조했다. 최종 7대 모두 원래 역할 이미지이며 MCU 정지 상태다. 복원은 새 RF 실행을 시작하지 않았다. 다음 측정도 실행기가 명시적으로 시작해야 한다.

전후 SSH/USB/probe 집합과 원본 Git 4개의 branch/HEAD/dirty/diff hash가 동일하고 잔여 캡처 프로세스는 없다. INIT/TX 펌웨어 C SHA도 변경 없다. Git commit/push는 하지 않았다.

이제 대표 경로의 기능 확인은 마쳤다. 차량 대여 전 추가 우선순위는 현재 NLOS에서 PAC4/PAC8별 Stage0 lead 탐색·선정 경로를 확인하고, 실제 차량에서는 설치 후 해당 채널에서 다시 확인하는 것이다. Exp4 S1~S5 부분 활성·논리 역할 회전의 실장비 경로, 전체 M/PAC/슬롯 부하 조합과 논문 반복은 이번 대표 점검 범위 밖이며 완료로 표시하지 않는다.

각 case의 case.json과 image_inventory.json에 실제 역할·serial·HEX/ELF SHA를 보존했다. 원문은 case/results/local/init.log, case/results/remote/N4.log이며, 각각 ASSESSMENT.json과 status.json에 판정·제어·flash 검증이 있다. 정리 결과는 RESULTS.json, 후처리는 analysis/ 아래에 있다.
