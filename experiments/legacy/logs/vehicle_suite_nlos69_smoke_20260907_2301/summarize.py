import csv
import datetime
import hashlib
import json
import pathlib
import sys

ROOT=pathlib.Path(__file__).resolve().parent
BASE=pathlib.Path('/Users/songchieon/Desktop/DWM3000')
API=BASE/'DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API'
sys.path.insert(0,str(API))
from brrs_suite_results import assess
from brrs_suite_evidence import read_evidence
from brrs_suite_manifest import load,plan
def read(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n')
selection=read(ROOT/'selected_cases.json')
m=load(ROOT/'manifest.json')
runs={}
for selected in selection['new_cases']:
    cid=selected['case_id'];bundle=ROOT/cid
    expected=next(x for x in plan(m,selected['stage']) if x['id']==cid)
    c,raw,orch=read_evidence(bundle,expected)
    a=assess(bundle)
    assert a['verdict']=='PASS'
    controls={}
    for j in c['jobs']:
        side='local' if j['logical_node']==1 else 'remote'
        state=read(bundle/'results'/side/'status.json')
        controls[j['physical_role']]=state['workers'][j['physical_role']]['readback']
    assert all(x['status']=='PASS' for x in controls.values())
    remote=read(bundle/'results/remote/status.json')
    assert len(remote['inactive_halted_after'])==5 and all(remote['inactive_halted_after'].values())
    runs[cid]={'assessment':a,'orchestration':orch,'readback':controls,'inactive_tx_halted':remote['inactive_halted_after'],
        'raw_diagnostics':{role:[l for l in ls if l.startswith(('RX timeouts=','SYNC loss:','My TX:','EXP3_TX_RESULT,','EXP3_TX_SUMMARY_CSV,'))] for role,ls in raw.items()}}
previous=BASE/'logs/exp4_nlos69_s6_pac8_recheck_20260907_2248/capture1'
old_case=read(previous/'case.json')
restore={}
for side,at,changed in [('local','2026-09-07T14:10:53.163221+00:00','init'),('remote','2026-09-07T14:11:12.561034+00:00','N4')]:
    jobs=[j for j in old_case['jobs'] if (j['host']=='local')==(side=='local')]
    restore[side]={'source':'Successful restore_exp4.py stdout observed in this task','at':at,'changed_role':changed,
        'readback':{j['physical_role']:{'serial':j['serial'],'hex_sha256':j['hex_sha256'],'bytes_verified':159773 if j['logical_node']==1 else 135149,'status':'PASS'} for j in jobs},
        'all_case_boards_halted':True,'extra_rf_run_started':False}
save(ROOT/'restore_status.json',restore)
old=read(previous/'results/ASSESSMENT.json')
source=read(ROOT/'stage0_m32_pac8_l25/case.json')['firmware_source_sha256']
assert all(sha(API/f)==h for f,h in source.items())
fixes={}
for stem,name in [('exp1','brrs_exp1_log_to_csv_plot.py'),('exp3','brrs_exp3_exttxe_analyze.py')]:
    fixes[name]={'before_sha256':sha(ROOT/(stem+'_analyzer_before.py')),'after_sha256':sha(API/name),'regression':read(ROOT/(stem+'_analyzer_regression.json')),'patch':stem+'_analyzer_fix.patch'}
with (ROOT/'analysis/exp3/exp3_smoke_differential.csv').open() as f:deltas=list(csv.DictReader(f))
artifacts={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'analysis').rglob('*') if p.is_file()}
out={'scope':'Current NLOS 6.9m vehicle rehearsal: representative functional cases only, one each; not a vehicle RF result or optimum-lead selection',
     'valid_new_rf_runs':7,'failed_or_excluded_rf_runs':0,'new_runs':runs,'reused_exp4':{'bundle':str(previous),'assessment':old},
     'firmware_C_unchanged':source,'analysis_fixes':fixes,'analysis_artifact_hashes':artifacts,'exp3_differentials':deltas,
     'restore':restore,'postflight':read(ROOT/'postflight_comparison.json'),'not_measured':['PAC4 and PAC-specific optimal lead grid/confirmation','all M/PAC/load combinations','Exp4 S1-S5 and role rotation RF','paper repetitions','vehicle placement RF','independent channel-reference measurement'],
     'status':'REPRESENTATIVE_RF_AND_ANALYSIS_PASS'}
save(ROOT/'RESULTS.json',out)
kst=datetime.timezone(datetime.timedelta(hours=9))
def local(s):return datetime.datetime.fromisoformat(s).astimezone(kst).strftime('%H:%M:%S')
names={'stage0':'Stage0','exp1':'Exp1','exp2':'Exp2','exp3':'Exp3','exp5':'Exp5'}
lines=['# 차량 실험 사전 리허설 · 현재 NLOS 6.9m · 2026-09-07','',
'**선택한 대표 RF 7회 모두 정상 수집·PER0%로 통과했다.** 바로 앞 Exp4 TX6/13슬롯 결과도 각 노드 PER<1%였으므로 중복 실행하지 않고 함께 참조한다. 이번 작업은 현재 NLOS에서 실제 송수신·수집·판정·CSV/그림 분석 연결을 확인한 것이며, 차량 실측이나 전체 논문 행렬 검증 완료는 아니다.','',
'RX1050270933, 모든1:1 TX는 사용자 지정 물리N4(1050282818)이며 단일 링크 논리 ID는N2다. N2/N3/N5/N6/N7은 연결을 유지하고 모든 case 준비·종료에서 MCU 정지 상태를 검증했다. SSH s-macbook-air, 보드7대, 사용자가 확인한 TX 허브 외부 전원 및 현재 NLOS6.9m 배치를 유지했다. 상세 위치·거리·높이·방향은 독립 계측하지 않았다.','',
'준비 프로파일에서 조건당1회, 공통 lead25µs/tail0을 사용했다. lead25는 직전 검증 조건으로 고정한 진단값이며 PAC별 최적값이나 논문용 동결값으로 선정하지 않았다. Stage0 한 점 뒤 바로 대표 경로를 검사했으며 전체 탐색을 수행한 것으로 취급하지 않는다.','',
'## 실행 결과','',
'| 단계 | 대표 조건 | 수신/예정 | PER | 수집·검증 | KST 제어·수집 구간 |',
'|---|---|---:|---:|---|---|']
for cid,run in runs.items():
    a=run['assessment'];p=a['conditions'];n=next(iter(a['nodes_by_serial'].values()))
    label=names[p['stage']]+(' '+p['variant'] if p['variant'] else '')
    metric=a['stage_metrics']
    detail=('CIR '+str(metric['cir_rows'])+'행, raw '+str(metric['raw_cir_frames'])+'×300샘플') if 'raw_cir_frames' in metric else 'CIR '+str(metric['cir_rows'])+'행' if 'cir_rows' in metric else 'EXTTXE '+str(metric['exttxe_captures'])+'개' if 'exttxe_captures' in metric else 'TX/종료/metadata/flash PASS'
    lines.append(f"| {label} | M{p['preamble']}/PAC{p['rx_pac']}/lead25 | {n['rx']}/{n['offered']} | {n['per_percent']:.2f}% | {detail} | {local(run['orchestration']['started_at'])}~{local(run['orchestration']['finished_at'])} |")
lines+=['| Exp4 (직전 실행 재사용) | M32/PAC8, TX6,13슬롯,G250,lead25 | 12976/13000 | 전체0.1846%, 노드최대0.70% | 7대 제어·TX/RX·readback PASS | 22:50:25~22:51:21 |','',
'Stage0와Exp1의 M32/PAC8/lead25 RX/TX HEX가 완전히 같아 Exp1 M32는 build-only까지만 확인하고 RF를 생략했다. Exp1은 M64로 심볼 변경 경로를 별도 확인했다. M64 사용은 실험 경로 점검이며 M32 고손실을 우회하는 해결책으로 채택한 것이 아니다. 단일 링크 논리ID N2를 실제 물리N2 보드로 혼동하지 않는다.','',
'신규7회에서 TX는 총9,000/9,000회 송신했고 각 조건의RX도예정량과 일치했다. 각 case READY/END, 실제 역할·serial, 설정·HEX·raw hash, 수집 상태와flash readback을 통과했다. 비참여TX5의 정지가 모두 유지됐고 수집timeout은 없었다. 각 단계의RX PHY·예약·설정 및TX SYNC/설정 진단 원문은RESULTS.json의new_runs/raw_diagnostics에 보존했다. 단계마다 없는 카운터를0으로 만들어 채우지 않았다.','',
'## 수집한 측정값과 후처리','',
'- Exp1 M64: 결과·accum histogram CSV 및PER/accum/error PNG·SVG 생성 정상. 그림은lead25 한 점이며 lead의 전이구간을 측정한 것이 아니다.',
'- Exp2 M32: 성공 패킷1,000개에 대응하는 CIR1,000행, sample/summary CSV와FP-SNR SVG 생성 정상.',
'- Exp3 A/B/C: 각각1,000개 하드웨어TIMER4 EXTTXE 캡처를 확보했다. 평균 폭은A95.881544µs, B104.090884µs, C76.973006µs. B−A=8.209340µs(로그의 이론값8.141µs 대비+0.068340µs), A−C=18.908538µs(18.846µs 대비+0.062538µs). absolute/differential/RX-PER PNG와CSV 생성 정상. 단일 실행이며 장기·보드간 재현성을 확정하지 않는다.',
'- Exp5 M1024/PAC32: CIR1,000행과raw30프레임×300샘플을 검증했다. 채널 분석기strict 검사와프레임별CSV 생성 정상. 관측PDP RMS 폭 평균28.91ns는 수신기 응답도 포함하며 독립 전파 지연확산값으로 해석하지 않는다.','',
'## 발견해 보완한 후처리 연결','',
'무선 펌웨어나 수집 원문은 수정하지 않았다. 독립 fix 사본의 분석기2개를 보완했다.','',
'1. brrs_exp3_exttxe_analyze.py: 현재EXP3_TX_RESULT에추가된end=1을이전정규식이읽지못해정상A결과를누락했다. 새필드를인식하고end≠1은거부하며과거end필드없는로그도지원했다. 현재A/B/C각1,000샘플,legacy호환,END실패·펌웨어FAIL·샘플누락거부를검증했다.',
'2. brrs_exp1_log_to_csv_plot.py: 이전종료필드순서와leadNN파일명에의존해새EXP1_DONE및bundle의init.log를처리하지못했다. key/value필드를읽고기존수집검증기의PAC·RX모드·END·예약·PER검사를공유하도록보완했다. leadNN과_lNN파일명의불일치는계속거부하며init.log는헤더와종료마커로검증한다. CSV에PAC/RX모드를보존했다. 실제현재로그·legacy호환및잘못된PAC/END/수집상태/카운터/중복필드/파일명lead거부를확인했다. 기존그림에고정돼있던“4~6µs전이”표시는제거해미측정결론이보이지않게했다.','',
'수정전후스크립트와patch, 회귀검사결과는exp1_analyzer_* / exp3_analyzer_*에보존했다. 캡처bundle은불변으로남겨이번실행에쓴도구hash를보존하며, 수정된후처리분석은로컬fix사본의새파일로실행했다. 다음에prepare하는bundle에는수정본이포함된다. 후처리수정때문에RF를다시실행하지않았다.','',
'## 종료 상태와 다음 순서','',
'RX와N4에는직전통과한Exp4 M32/PAC8/S6/13슬롯HEX를복원했다. 원격나머지5대는재플래시하지않고기존Exp4HEX를readback으로대조했다. 최종7대모두원래역할이미지이며MCU정지상태다. 복원은새RF실행을시작하지않았다. 다음측정도실행기가명시적으로시작해야한다.','',
'전후SSH/USB/probe집합과원본Git4개의branch/HEAD/dirty/diffhash가동일하고잔여캡처프로세스는없다. INIT/TX펌웨어C SHA도변경없다. Git commit/push는하지않았다.','',
'이제대표경로의기능확인은마쳤다. 차량대여전추가우선순위는현재NLOS에서PAC4/PAC8별Stage0 lead탐색·선정경로를확인하고, 실제차량에서는설치후해당채널에서다시확인하는것이다. Exp4 S1~S5 부분활성·논리역할회전의실장비경로, 전체M/PAC/슬롯부하조합과논문반복은이번대표점검범위밖이며완료로표시하지않는다.','',
'각case의case.json과image_inventory.json에실제역할·serial·HEX/ELF SHA를보존했다. 원문은case/results/local/init.log, case/results/remote/N4.log이며, 각각ASSESSMENT.json과status.json에판정·제어·flash검증이있다. 정리결과는RESULTS.json, 후처리는analysis/ 아래에있다.','']
(ROOT/'RESULTS.md').write_text('\n'.join(lines))
print(json.dumps({'status':out['status'],'valid_new_rf_runs':7,'failed_rf_runs':0,'report':str(ROOT/'RESULTS.md')},indent=2))

