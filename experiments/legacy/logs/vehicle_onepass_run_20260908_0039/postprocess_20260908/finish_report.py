#!/usr/bin/env python3
"""Write the offline analysis report and remaining single-link plots."""
import csv
import json
import os
from pathlib import Path
import sys

O=Path(__file__).resolve().parent;R=O.parent;B=R.parents[1]
os.environ.setdefault('MPLCONFIGDIR',str(O/'plot_cache'))
os.environ.setdefault('XDG_CACHE_HOME',str(O/'plot_cache'))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
API=B/'DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API'
S=B/'logs/vehicle_suite_nlos69_smoke_20260907_2301'
sys.path.insert(0,str(API))
from brrs_exp1_log_to_csv_plot import parse_log as exp1parse
from brrs_suite_case import checked,sha,save

def rows(name):return list(csv.DictReader((O/name).open()))
def table(headers,data):return '\n'.join(['| '+' | '.join(headers)+' |','|'+'|'.join(['---']*len(headers))+'|',*['| '+' | '.join(map(str,r))+' |' for r in data]])
def writecsv(name,rr):
    with (O/name).open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=list(rr[0]));w.writeheader();w.writerows(rr)
summary=json.loads((O/'SUMMARY.json').read_text());ev=json.loads((O/'revalidated_assessments.json').read_text())
cases=rows('cases.csv');nodes=rows('nodes.csv');cir=rows('cir_summary.csv');pairs=rows('pac_comparisons.csv')

single=[]
for b in [r['bundle'] for r in cases if r['stage']=='exp1']+[str(S/'stage0_m32_pac8_l25'),str(S/'exp1_m64_pac8_l25')]:
    a=ev[b];p=a['conditions'];parsed=exp1parse(Path(b)/'results/local/init.log')
    single.append({'source_case_id':a['case_id'],'M':p['preamble'],'PAC':p['rx_pac'],'lead_us':p['lead_us'],
        'stage0_identical_HEX_proxy':p['stage']=='stage0','prior_context':b.startswith(str(S)),
        'offered':parsed.expected,'rx':parsed.rx,'PER_percent':parsed.per_percent,
        'accum_mean':parsed.accum_mean,'accum_mode':parsed.accum_mode,**parsed.failure_counts,'bundle':b})
proxy=checked(S/'exp1_m32_pac8_l25');original=checked(S/'stage0_m32_pac8_l25')
hashes=lambda c:{j['logical_node']:j['hex_sha256'] for j in c['jobs']}
assert hashes(proxy)==hashes(original)
single.sort(key=lambda r:(r['M'],r['PAC']));writecsv('exp1_summary.csv',single)
save(O/'exp1_stage0_reuse_proof.json',{'proxy_stage0_bundle':str(S/'stage0_m32_pac8_l25'),
    'prepared_exp1_bundle':str(S/'exp1_m32_pac8_l25'),'RX_and_TX_HEX_identical':True,'hashes':hashes(proxy),'extra_RF_performed':False})
fig,axs=plt.subplots(1,2,figsize=(10,4));x=np.arange(4)
for pac,dx,color in [(4,-.13,'#D0781F'),(8,.13,'#2865A5')]:
    rr=[r for r in single if r['PAC']==pac]
    axs[0].scatter(x+dx,[r['PER_percent'] for r in rr],label=f'PAC{pac}',color=color)
    axs[1].bar(x+dx,[r['accum_mean'] for r in rr],width=.25,color=color,label=f'PAC{pac}')
axs[0].set_ylim(-.02,.15);axs[0].set_ylabel('PER (%)');axs[0].set_title('Exp1: 2,000 frames per point')
axs[1].set_ylabel('Mean accumulated preamble count');axs[1].set_title('Diagnostic accumulation counts')
for ax in axs:ax.set_xticks(x,['M32','M64','M128','M256']);ax.legend();ax.grid(axis='y',alpha=.2)
fig.text(.5,-.04,'PAC4 = lead 27 us; PAC8 = 26 us except earlier M32/M64 at 25 us. M32/PAC8 is an identical-HEX Stage0 proxy.',ha='center',fontsize=8)
fig.tight_layout()
for ext in ['png','svg']:fig.savefig(O/f'exp1_summary.{ext}',dpi=180,bbox_inches='tight')
plt.close(fig)

capacity=[]
for M,maximum in [(32,13),(64,12),(128,10),(256,8)]:
    rr=[r for r in cases if r['stage']=='exp4' and r['physical_TX']=='6' and int(r['M'])==M]
    k=max(int(r['slots_per_SF']) for r in rr)
    high=[r for r in rr if int(r['slots_per_SF'])==k]
    capacity.append({'M':M,'timing_max_slots':maximum,'current_highest_tested_pass_slots':k,
        'offered_reports_per_second':100*k,'observed_goodput_kbps_min':min(float(r['app_goodput_kbps']) for r in high),
        'normalized_app_efficiency_percent':min(float(r['app_payload_efficiency_over_elapsed_percent']) for r in high),
        'current_timing_max_measured':k==maximum,'independent_TX_limit':6,'paper_capacity_validated':False})
writecsv('capacity_observations.csv',capacity)

report=['# NLOS6.9m 차량 준비 실험 후처리', '',
    '**이번 TX6 16조건은 모두 각 노드 PER<1%였다. PAC4와 PAC8의 우열은 이1회 자료로 정하지 않는다.** 기존 정상·실패 원문을 분리해 검증했고, CIR 전력 지표의 하한값과 드라이버 계산 문제를 새로 확인했다.', '',
    '보드·SSH·빌드·플래시 없이 로컬 파일만 처리했다. 현재40회와 과거 문맥 자료를 합쳐 **서로 다른86개 bundle**의 manifest/HEX/raw hash, 역할·TX/RX·종료·수집·저장된 readback 증거를 다시 검증했다. 현재40회는 새 RF가 아니다. 소수TX28조건은 사용자가 생략한 상태 그대로다. 원본 raw와 firmware는 변경하지 않았다.', '',
    '## 1. 현재 실측 결과', '',
    table(['항목','결과'],[
        ['Exp1 추가6조건','모두 PER<1%; M32/PAC4와 M256/PAC8 각각1/2000손실(0.05%)'],
        ['Exp2 추가7조건','모두 PER<1%; M256/PAC4만1/1000손실(0.1%), CIR999행; 나머지1000행'],
        ['Exp4 TX1 8조건·TX2 3조건','모두 무손실; 나머지 소수TX 조건은 생략'],
        ['Exp4 TX6 16조건','132,000 offered / 131,999 RX; 개별 노드·조건 최악0.1%'],
        ['유일한 TX6 손실','M32/PAC8/lead26/6슬롯, 물리N5=1050208509, 논리N5, 슬롯3에서 PHR 오류1회'],
        ['TX6 송신·시스템 오류','132,000/132,000 TX 성공. beacon 미수신·TX late·wrong slot/SF·RX late·RDB mismatch/incomplete/resync/overrun·SPI/queue 오류0'],
    ]), '',
    'TX6의 SF는 총16,000개이며15,999개에서 모든 배정 슬롯을 수신했다. 이는 전체 평균으로 노드 실패를 감추는 판정이 아니다. 96개 노드×조건 각각의 PER를 확인했다. [노드별 원표](nodes.csv), [슬롯별 원표](slots.csv), [오류 카운터](errors.csv), [조건별 시각·전송률](cases.csv).', '',
    '![노드별 PER](tx6_node_per.png)', '',
    '0/1000손실의 개별 Wilson95% 상한은 약0.383%, 1/1000은 약0.564%다. 패킷 독립 가정의 기술 통계이며, 시간 상관·96개 구간의 동시 신뢰도·반복 실행·차량 환경의 신뢰성 보장은 아니다.', '',
    '## 2. PAC4/27µs와 PAC8/26µs', '',
    table(['M','슬롯/SF','PAC4 최악 PER(%)','PAC8 최악 PER(%)','같은 TX HEX·역할'],[
        [p['M'],p['slots'],f"{float(p['PAC4_worst_node_PER']):g}",f"{float(p['PAC8_worst_node_PER']):g}",'6대 모두 일치'] for p in pairs]), '',
    '8개 짝에서 TX HEX와 물리·논리 배정이 모두 같다. INIT의 PAC와 lead, 연동 SFD timeout·RX 창은 다르다. 따라서 PAC 하나만 바꾼 인과 실험이 아니라 각 PAC에 정한 lead를 포함한 설정 조합 비교다. PAC4 블록을 먼저, PAC8을 나중에 측정했고 조건당1회이므로 순서·환경 변화도 분리할 수 없다. 1개 손실 차이를 PAC4의 우월성으로 해석하지 않는다. [짝별 근거](pac_comparisons.csv).', '',
    '![PER와 goodput](tx6_per_and_goodput.png)', '',
    '## 3. 용량·이용률·타이밍', '',
    table(['M','현재 상한 슬롯까지 검사','모델상 슬롯 상한','해당 고부하 app goodput','전체 SF 기준 payload 이용률'],[
        [r['M'],r['current_highest_tested_pass_slots'],r['timing_max_slots'],f"{r['observed_goodput_kbps_min']:.1f} kbps",f"{r['normalized_app_efficiency_percent']:.3f}%"] for r in capacity]), '',
    '조건은 application16B, DATA PSDU26B, 6.8Mbps, SF10ms, G250/SB3000/SP2500이다. `app goodput=수신보고수×128bit/실제 경과시간`; 표의 이용률은 이를6.8Mbps로 나눈 값이다. 논문의 SFD/PHR를 제거한 이상적 점근 이용률과 다른 값이다. DATA frame 내부 유효 payload 시간 비중, 설정된 DATA RX 창 비중도 cases.csv에 별도 열로 뒀다. RX 창 합은 계획상 시간 예산이며 실제 전류·에너지 측정값이 아니다.', '',
    '같은12슬롯에서는 M32와 M64 모두153.6kbps다. 더 짧은 프리앰블의 goodput 이득은 슬롯을 더 넣어야 나타난다. M32의13슬롯은 이번16조건에 포함하지 않았다. 과거 PAC8/13슬롯 성공을 현재 PAC4/13슬롯 성공이나 차량 용량으로 바꾸지 않는다. M64/128/256은 이번1회에 타이밍 상한까지 통과했지만 논문용 반복·회전을 완료한 최대 용량 판정은 아니다. 독립 TX는6대이며13대가 아니다.', '',
    f"TX6의 첫 RX 창 개방 여유 최솟값은 **{summary['TX6_min_first_RX_open_slack_us']}µs**, SYNC 준비 후 남은 여유는 **{summary['TX6_min_sync_prep_remaining_us']}µs**, TX 첫 예약송신 RMARKER 여유는 **{summary['TX6_min_TX_first_RMARKER_slack_us']}µs**였다. RX RMARKER의 스케줄 대비 관측 오차는 **{summary['TX6_RX_timing_range_ns'][0]}~{summary['TX6_RX_timing_range_ns'][1]}ns**다. 뒤 슬롯의 slack_samples=0인 값은 미측정으로 내보냈으며 0µs 여유라고 해석하지 않았다. [타이밍 표](timing.csv).", '',
    '## 4. CIR·전력 지표의 새 발견', '',
    table(['M','PAC4 FP-SNR 평균','PAC8 FP-SNR 평균','CIR 표본'],[
        [M,f"{float(next(r for r in cir if int(r['M'])==M and r['PAC']=='4')['FP_SNR_mean_dB']):.2f} dB",f"{float(next(r for r in cir if int(r['M'])==M and r['PAC']=='8')['FP_SNR_mean_dB']):.2f} dB",'각1000; M256/PAC4는999' if M==256 else '각1000'] for M in [32,64,128,256]]), '',
    'FP-SNR는 FP 주위5개 CIR tap의 최대 power / FP 앞쪽12개 tap 평균 power를 dB로 바꾼 값이다(guard2 tap). 일반적인 RF 입력 SNR나 채널 추정 오차를 직접 측정한 값은 아니다. 저장된 peak/noise와 정수 ratio가 모든 표본에서 일치한다. 수신 성공 패킷만의 분포이며, M32/PAC8은 이전 lead25µs 자료다. 그 한 점은 현재 PAC4/27µs와 동일 시각·lead·차폐 조건의 엄밀한 대조가 아니다. N4 한 링크를 다른5개 링크로 일반화하지 않는다.', '',
    '![CIR 품질](exp2_snr_and_rssi_quality.png)', '',
    '**RSSI:** M32/PAC4는800/1000, 이전 M32/PAC8은1000/1000이 -128dBm 하한/오류 코드였다. 이를 전부 평균해 -119.19dBm 또는 -128dBm을 채널 전력으로 보고하면 잘못된다. 원문은 보존하고 각 표본에 `rssi_floor_or_error`를 표시했으며, nonfloor 평균은 선택된 부분집합임을 명시했다. 원래 CIR diagnostics의 power·DGC 값이 저장되지 않아 하한/오류값의 정확한 원인은 이 자료로 확정하거나 복원할 수 없다.', '',
    '**First-path dBm:** DW3000 driver가 RX code 레지스터를8bit로 잘라낸 뒤8bit shift해 코드가 항상0이 되는 문제를 발견했다. Exp2 8개·Exp5 1개 ELF 모두 해당 상수0 호출을 확인했다. DATA code9 기준 PRF64 상수 대신 PRF16 상수를 사용하므로 기록된 FP power는 이 분기 때문에 약7.8984dB 높다. 별도 `fp_dbm_alpha_only_adjusted_estimate` 열에 상수 차이를 뺀 값을 제공했다. 원본 FP/RSSI-gap 값과 보정 추정값을 구분하며, 이를 절대 전력 교정 완료로 간주하지 않는다. FP-SNR는 이 dBm 함수의 출력을 사용하지 않으므로 영향을 받지 않는다. [계산·ELF 근거](POWER_DIAGNOSTICS.md).', '',
    '이 버그는 CIR 로그의 전력 변환 경로에서 확인한 것이며 Exp4 PER 고손실의 원인으로 확인한 것이 아니다. 펌웨어·보드 이미지는 이번 후처리에서 변경하지 않았다. [표본·품질 플래그](cir_samples_quality_flags.csv), [요약](cir_summary.csv).', '',
    '## 5. 기존 Stage0·13슬롯과 Exp3·Exp5', '',
    '과거 단일N4 Stage0 lead20 통과가6TX의 적정 lead를 보장하지 않았던 사실은 그대로다. 13슬롯/block2의 PAC8은 lead25 최악0.55%,26·27은0%였고, PAC4/27은1.0%, 전선 가림 보고 뒤 같은 설정1회는6.8%였다. 현재6/12슬롯·block1 결과와 부하·역할·시각·전선 상태가 다르므로 합치지 않았다. 가림 노드와 시작 시각이 불명인 과거 자료를 임의 제외하지 않았다.', '',
    '![과거 lead 문맥](historical_lead_context.png)', '',
    '이번 자료는 슬롯별 scheduled delayed-RX에서 낮은 PER가 가능하다는 증거다. 현재 continuous/manual-rearm A/B를 한 자료가 아니므로 continuous RX 단일 원인설을 확정하거나 반박하지 않는다. 과거 저손실 펌웨어도 manual burst 구조였던 사실을 유지한다.', '',
    'Exp1의8개 M/PAC 지점도 한 표로 정리했다. 현재6조건 외 PAC8/M32·M64는 이전 lead25µs 자료다. M32는 RX/TX HEX가 동일한 Stage0 원문을 proxy로 썼다는 근거를 남겼다. [Exp1 요약](exp1_summary.csv), [그림](exp1_summary.png), [동일 HEX 근거](exp1_stage0_reuse_proof.json).', '',
    'Exp3 A/B/C 원문을 다시 분석해 각각1,000개 EXTTXE를 확인했다. 평균 airtime은95.881544/104.090884/76.973006µs, B−A=8.209340µs, A−C=18.908538µs다. 같은 기존 실행의 재분석이며 새로운 RF 반복이 아니다. [Exp3 출력](exp3/exp3_reused_summary.csv).', '',
    f"Exp5는 기존 M1024/PAC32,1000/1000 RX와raw30×300을 다시 검증했다. 관측 PDP RMS 폭 평균은{summary['exp5_observed_PDP_RMS_ns']['mean']:.2f}ns({summary['exp5_observed_PDP_RMS_ns']['min']:.2f}~{summary['exp5_observed_PDP_RMS_ns']['max']:.2f}ns)다. 수신기 응답이 포함되므로 전파 채널만의 지연확산이 아니다. dominant-to-residual 값도 Rician K-factor가 아니다. [Exp5 표](exp5_channel.csv), [그림](exp5_observed_pdp_width.png).", '',
    '## 6. 차량 실험에 반영할 판단', '',
    'M32/PAC8/lead26µs를 차량의 첫 확인 후보로 유지할 근거는 있다. 현재 측정뿐 아니라 과거13슬롯에서도 통과한 이력이 있기 때문이다. 이는 차량 최적값 선정이 아니다. PAC4/27µs도 현재6TX의6·12슬롯에서 통과했으므로 같은 차량 배치·부하에서 비교할 가치가 있다. 차량 설치 후6TX/6슬롯에서 각 노드 PER를 확인하고, 같은 조건의12·13슬롯 부하로 확장한다. 논문 반복에서는 설치표 기준의 역할 회전을 유지한다.', '',
    'PER·전송률·타이밍과 CIR 기반 FP-SNR 분석은 사용할 수 있다. 논문의 절대 RSSI/FP-power·gap 해석에는 새로 확인한 품질 제한을 반영해야 한다. 이후 진단 펌웨어를 손볼 때는 FP code 추출 순서 수정과 RSSI 하한 상태 및 원래 diagnostics 기록을 우선 검토한다. 이번 후처리가 송수신 펌웨어 수정·추가 RF·GitHub 업로드를 수행한 것은 아니다.', '',
    '[전체 원문 검증 목록](evidence_ledger.csv) · [검증 재생성 JSON](revalidated_assessments.json) · [입력 해시](input_hashes.json) · [후처리 요약 JSON](SUMMARY.json)', '']
(O/'REPORT.md').write_text('\n'.join(report))

power=f'''# CIR 전력 변환의 품질 문제

분석 범위는 로컬 원문과 저장된 ELF다. firmware·raw·HEX를 수정하거나 보드를 재실행하지 않았다.

## RSSI -128dBm

M32/PAC4:800/1000, 이전 M32/PAC8:1000/1000. `deca_rsl.c`의 계산은 power 또는 accumulation이0이면 SHRT_MIN(-128dBm)을 반환하고, 계산값이 표현 하한 아래여도 같은 값으로 제한한다. 상위 `ull_calculate_rssi()`는 이 출력 자체를 실패로 바꾸지 않고 DWT_SUCCESS를 반환한다. 현재 기록의 accum은 양수지만 원래 diagnostics power/DGC가 없어 나머지 원인을 구분할 수 없다. nonfloor 부분만의 평균도 선택 편향이 있으므로 M별 평균 RSSI 비교의 근거로 쓰지 않는다.

## FP power의 RX code 절단

대상 [driver]({API/'Shared/dwt_uwb_driver/dw3000/dw3000_device.c'}:8227)의 식은 다음과 같다.

```c
uint8_t rx_pcode = (uint8_t)(reg & 0x1F00) >> 8;
```

0x1F00은8~12bit이므로 uint8_t cast가 먼저 모든 비트를 버린다. RX code9라면 원래0x0900을8bit로 자른0을 다시 shift하므로0이 전달된다. 올바른 연산 순서는 mask→shift→8bit cast이다. 이는 후속 수정안이며 현재 firmware에는 적용하지 않았다.

Exp2의8개 RX ELF와 Exp5 RX ELF에서 레지스터 read 뒤 `movs r3, #0`을 확인했다. [ELF별 근거](fp_power_evidence.json)와 각 `*_fp_power.disassembly.txt`에 주소·명령을 저장했다. DATA config의 RX code는9이며 [RSL 상수]({API/'Shared/dwt_uwb_driver/deca_rsl.c'}:17)는 PRF64=31155/256, PRF16=29133/256dB다. 따라서 잘못된 분기는 FP power를 `(31155−29133)/256 = 7.8984375dB` 높인다.

`FP_alpha_only_estimate = logged_FP_dBm − 7.8984375dB`를 별도 열로 제공했다. 로그의0.01dB 반올림 오차는 남으며 안테나·보드별 절대 전력 교정, 하드웨어 diagnostics 정확성, 포화 여부를 보증하지 않는다. 원래 FP/RSSI-gap을 덮어쓰지 않았다. 기존 M64 이상에서 FP power가 전체 RSSI보다 높았던 약3~4dB 역전은 이 오류와 일관되지만, M32 RSSI 하한값 문제를 이 상수만으로 해결할 수는 없다.

## FP-SNR와 PER에 미치는 범위

FP-SNR는 `max(power[FP−1..FP+3]) / mean(power[FP−14..FP−3])`이고 저장된 정수 peak/noise/ratio를 모두 대조했다. 이 경로는 dBm/PRF 상수 계산을 사용하지 않는다. PER는 TX offered/RX frame 집계다. 따라서 위 dBm 버그 때문에 성공 패킷이나 PER를 재분류하지 않았다. Exp4의 continuous RX 원인설을 검증하는 자료도 아니다.
'''
(O/'POWER_DIAGNOSTICS.md').write_text(power)
print('REPORT_AND_EXP1_COMPLETE')
