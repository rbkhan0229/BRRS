# NLOS6.9m 차량 준비 실험 후처리

**이번 TX6 16조건은 모두 각 노드 PER<1%였다. PAC4와 PAC8의 우열은 이1회 자료로 정하지 않는다.** 기존 정상·실패 원문을 분리해 검증했고, CIR 전력 지표의 하한값과 드라이버 계산 문제를 새로 확인했다.

보드·SSH·빌드·플래시 없이 로컬 파일만 처리했다. 현재40회와 과거 문맥 자료를 합쳐 **서로 다른86개 bundle**의 manifest/HEX/raw hash, 역할·TX/RX·종료·수집·저장된 readback 증거를 다시 검증했다. 현재40회는 새 RF가 아니다. 소수TX28조건은 사용자가 생략한 상태 그대로다. 원본 raw와 firmware는 변경하지 않았다.

## 1. 현재 실측 결과

| 항목 | 결과 |
|---|---|
| Exp1 추가6조건 | 모두 PER<1%; M32/PAC4와 M256/PAC8 각각1/2000손실(0.05%) |
| Exp2 추가7조건 | 모두 PER<1%; M256/PAC4만1/1000손실(0.1%), CIR999행; 나머지1000행 |
| Exp4 TX1 8조건·TX2 3조건 | 모두 무손실; 나머지 소수TX 조건은 생략 |
| Exp4 TX6 16조건 | 132,000 offered / 131,999 RX; 개별 노드·조건 최악0.1% |
| 유일한 TX6 손실 | M32/PAC8/lead26/6슬롯, 물리N5=1050208509, 논리N5, 슬롯3에서 PHR 오류1회 |
| TX6 송신·시스템 오류 | 132,000/132,000 TX 성공. beacon 미수신·TX late·wrong slot/SF·RX late·RDB mismatch/incomplete/resync/overrun·SPI/queue 오류0 |

TX6의 SF는 총16,000개이며15,999개에서 모든 배정 슬롯을 수신했다. 이는 전체 평균으로 노드 실패를 감추는 판정이 아니다. 96개 노드×조건 각각의 PER를 확인했다. [노드별 원표](nodes.csv), [슬롯별 원표](slots.csv), [오류 카운터](errors.csv), [조건별 시각·전송률](cases.csv).

![노드별 PER](tx6_node_per.png)

0/1000손실의 개별 Wilson95% 상한은 약0.383%, 1/1000은 약0.564%다. 패킷 독립 가정의 기술 통계이며, 시간 상관·96개 구간의 동시 신뢰도·반복 실행·차량 환경의 신뢰성 보장은 아니다.

## 2. PAC4/27µs와 PAC8/26µs

| M | 슬롯/SF | PAC4 최악 PER(%) | PAC8 최악 PER(%) | 같은 TX HEX·역할 |
|---|---|---|---|---|
| 32 | 6 | 0 | 0.1 | 6대 모두 일치 |
| 32 | 12 | 0 | 0 | 6대 모두 일치 |
| 64 | 6 | 0 | 0 | 6대 모두 일치 |
| 64 | 12 | 0 | 0 | 6대 모두 일치 |
| 128 | 6 | 0 | 0 | 6대 모두 일치 |
| 128 | 10 | 0 | 0 | 6대 모두 일치 |
| 256 | 6 | 0 | 0 | 6대 모두 일치 |
| 256 | 8 | 0 | 0 | 6대 모두 일치 |

8개 짝에서 TX HEX와 물리·논리 배정이 모두 같다. INIT의 PAC와 lead, 연동 SFD timeout·RX 창은 다르다. 따라서 PAC 하나만 바꾼 인과 실험이 아니라 각 PAC에 정한 lead를 포함한 설정 조합 비교다. PAC4 블록을 먼저, PAC8을 나중에 측정했고 조건당1회이므로 순서·환경 변화도 분리할 수 없다. 1개 손실 차이를 PAC4의 우월성으로 해석하지 않는다. [짝별 근거](pac_comparisons.csv).

![PER와 goodput](tx6_per_and_goodput.png)

## 3. 용량·이용률·타이밍

| M | 현재 상한 슬롯까지 검사 | 모델상 슬롯 상한 | 해당 고부하 app goodput | 전체 SF 기준 payload 이용률 |
|---|---|---|---|---|
| 32 | 12 | 13 | 153.6 kbps | 2.259% |
| 64 | 12 | 12 | 153.6 kbps | 2.259% |
| 128 | 10 | 10 | 128.0 kbps | 1.882% |
| 256 | 8 | 8 | 102.4 kbps | 1.506% |

조건은 application16B, DATA PSDU26B, 6.8Mbps, SF10ms, G250/SB3000/SP2500이다. `app goodput=수신보고수×128bit/실제 경과시간`; 표의 이용률은 이를6.8Mbps로 나눈 값이다. 논문의 SFD/PHR를 제거한 이상적 점근 이용률과 다른 값이다. DATA frame 내부 유효 payload 시간 비중, 설정된 DATA RX 창 비중도 cases.csv에 별도 열로 뒀다. RX 창 합은 계획상 시간 예산이며 실제 전류·에너지 측정값이 아니다.

같은12슬롯에서는 M32와 M64 모두153.6kbps다. 더 짧은 프리앰블의 goodput 이득은 슬롯을 더 넣어야 나타난다. M32의13슬롯은 이번16조건에 포함하지 않았다. 과거 PAC8/13슬롯 성공을 현재 PAC4/13슬롯 성공이나 차량 용량으로 바꾸지 않는다. M64/128/256은 이번1회에 타이밍 상한까지 통과했지만 논문용 반복·회전을 완료한 최대 용량 판정은 아니다. 독립 TX는6대이며13대가 아니다.

TX6의 첫 RX 창 개방 여유 최솟값은 **1073µs**, SYNC 준비 후 남은 여유는 **681µs**, TX 첫 예약송신 RMARKER 여유는 **1395µs**였다. RX RMARKER의 스케줄 대비 관측 오차는 **272~300ns**다. 뒤 슬롯의 slack_samples=0인 값은 미측정으로 내보냈으며 0µs 여유라고 해석하지 않았다. [타이밍 표](timing.csv).

## 4. CIR·전력 지표의 새 발견

| M | PAC4 FP-SNR 평균 | PAC8 FP-SNR 평균 | CIR 표본 |
|---|---|---|---|
| 32 | 25.03 dB | 23.03 dB | 각1000 |
| 64 | 30.17 dB | 29.67 dB | 각1000 |
| 128 | 34.66 dB | 34.43 dB | 각1000 |
| 256 | 37.52 dB | 37.48 dB | 각1000; M256/PAC4는999 |

FP-SNR는 FP 주위5개 CIR tap의 최대 power / FP 앞쪽12개 tap 평균 power를 dB로 바꾼 값이다(guard2 tap). 일반적인 RF 입력 SNR나 채널 추정 오차를 직접 측정한 값은 아니다. 저장된 peak/noise와 정수 ratio가 모든 표본에서 일치한다. 수신 성공 패킷만의 분포이며, M32/PAC8은 이전 lead25µs 자료다. 그 한 점은 현재 PAC4/27µs와 동일 시각·lead·차폐 조건의 엄밀한 대조가 아니다. N4 한 링크를 다른5개 링크로 일반화하지 않는다.

![CIR 품질](exp2_snr_and_rssi_quality.png)

**RSSI:** M32/PAC4는800/1000, 이전 M32/PAC8은1000/1000이 -128dBm 하한/오류 코드였다. 이를 전부 평균해 -119.19dBm 또는 -128dBm을 채널 전력으로 보고하면 잘못된다. 원문은 보존하고 각 표본에 `rssi_floor_or_error`를 표시했으며, nonfloor 평균은 선택된 부분집합임을 명시했다. 원래 CIR diagnostics의 power·DGC 값이 저장되지 않아 하한/오류값의 정확한 원인은 이 자료로 확정하거나 복원할 수 없다.

**First-path dBm:** DW3000 driver가 RX code 레지스터를8bit로 잘라낸 뒤8bit shift해 코드가 항상0이 되는 문제를 발견했다. Exp2 8개·Exp5 1개 ELF 모두 해당 상수0 호출을 확인했다. DATA code9 기준 PRF64 상수 대신 PRF16 상수를 사용하므로 기록된 FP power는 이 분기 때문에 약7.8984dB 높다. 별도 `fp_dbm_alpha_only_adjusted_estimate` 열에 상수 차이를 뺀 값을 제공했다. 원본 FP/RSSI-gap 값과 보정 추정값을 구분하며, 이를 절대 전력 교정 완료로 간주하지 않는다. FP-SNR는 이 dBm 함수의 출력을 사용하지 않으므로 영향을 받지 않는다. [계산·ELF 근거](POWER_DIAGNOSTICS.md).

이 버그는 CIR 로그의 전력 변환 경로에서 확인한 것이며 Exp4 PER 고손실의 원인으로 확인한 것이 아니다. 펌웨어·보드 이미지는 이번 후처리에서 변경하지 않았다. [표본·품질 플래그](cir_samples_quality_flags.csv), [요약](cir_summary.csv).

## 5. 기존 Stage0·13슬롯과 Exp3·Exp5

과거 단일N4 Stage0 lead20 통과가6TX의 적정 lead를 보장하지 않았던 사실은 그대로다. 13슬롯/block2의 PAC8은 lead25 최악0.55%,26·27은0%였고, PAC4/27은1.0%, 전선 가림 보고 뒤 같은 설정1회는6.8%였다. 현재6/12슬롯·block1 결과와 부하·역할·시각·전선 상태가 다르므로 합치지 않았다. 가림 노드와 시작 시각이 불명인 과거 자료를 임의 제외하지 않았다.

![과거 lead 문맥](historical_lead_context.png)

이번 자료는 슬롯별 scheduled delayed-RX에서 낮은 PER가 가능하다는 증거다. 현재 continuous/manual-rearm A/B를 한 자료가 아니므로 continuous RX 단일 원인설을 확정하거나 반박하지 않는다. 과거 저손실 펌웨어도 manual burst 구조였던 사실을 유지한다.

Exp1의8개 M/PAC 지점도 한 표로 정리했다. 현재6조건 외 PAC8/M32·M64는 이전 lead25µs 자료다. M32는 RX/TX HEX가 동일한 Stage0 원문을 proxy로 썼다는 근거를 남겼다. [Exp1 요약](exp1_summary.csv), [그림](exp1_summary.png), [동일 HEX 근거](exp1_stage0_reuse_proof.json).

Exp3 A/B/C 원문을 다시 분석해 각각1,000개 EXTTXE를 확인했다. 평균 airtime은95.881544/104.090884/76.973006µs, B−A=8.209340µs, A−C=18.908538µs다. 같은 기존 실행의 재분석이며 새로운 RF 반복이 아니다. [Exp3 출력](exp3/exp3_reused_summary.csv).

Exp5는 기존 M1024/PAC32,1000/1000 RX와raw30×300을 다시 검증했다. 관측 PDP RMS 폭 평균은28.91ns(27.18~31.62ns)다. 수신기 응답이 포함되므로 전파 채널만의 지연확산이 아니다. dominant-to-residual 값도 Rician K-factor가 아니다. [Exp5 표](exp5_channel.csv), [그림](exp5_observed_pdp_width.png).

## 6. 차량 실험에 반영할 판단

M32/PAC8/lead26µs를 차량의 첫 확인 후보로 유지할 근거는 있다. 현재 측정뿐 아니라 과거13슬롯에서도 통과한 이력이 있기 때문이다. 이는 차량 최적값 선정이 아니다. PAC4/27µs도 현재6TX의6·12슬롯에서 통과했으므로 같은 차량 배치·부하에서 비교할 가치가 있다. 차량 설치 후6TX/6슬롯에서 각 노드 PER를 확인하고, 같은 조건의12·13슬롯 부하로 확장한다. 논문 반복에서는 설치표 기준의 역할 회전을 유지한다.

PER·전송률·타이밍과 CIR 기반 FP-SNR 분석은 사용할 수 있다. 논문의 절대 RSSI/FP-power·gap 해석에는 새로 확인한 품질 제한을 반영해야 한다. 이후 진단 펌웨어를 손볼 때는 FP code 추출 순서 수정과 RSSI 하한 상태 및 원래 diagnostics 기록을 우선 검토한다. 이번 후처리가 송수신 펌웨어 수정·추가 RF·GitHub 업로드를 수행한 것은 아니다.

[전체 원문 검증 목록](evidence_ledger.csv) · [검증 재생성 JSON](revalidated_assessments.json) · [입력 해시](input_hashes.json) · [후처리 요약 JSON](SUMMARY.json)
