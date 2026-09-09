#!/usr/bin/env python3
"""Final evidence and reports after user-steered cable recheck; no hardware I/O."""
from datetime import datetime
import json
from pathlib import Path
import sys
from zoneinfo import ZoneInfo

R=Path(__file__).resolve().parent
API=Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
sys.path.insert(0,str(API))
from brrs_suite_case import checked,save
from brrs_suite_results import assess

def evidence(path):
    b=Path(path);c=checked(b);a=assess(b)
    o=json.loads((b/'results/orchestration.json').read_text())
    a['execution_kst']={k:datetime.fromisoformat(o[k]).astimezone(ZoneInfo('Asia/Seoul')).isoformat(timespec='seconds') for k in ['started_at','finished_at']}
    a['orchestration']=o;a['images']=[];a['control_by_side']={};a['raw_counters_by_role']={}
    for j in c['jobs']:
        a['images'].append({k:j[k] for k in ['physical_role','logical_node','serial','hex','hex_sha256','elf_sha256']})
        side='local' if j['host']=='local' else 'remote';role=j['physical_role']
        a['control_by_side'][side]=json.loads((b/'results'/side/'status.json').read_text())
        lines=(b/'results'/side/(role+'.log')).read_text().splitlines()
        a['raw_counters_by_role'][role]=[l for l in lines if l.startswith(('RX timeouts=','TDMA validation:',
            'SYNC loss:','My TX:','EXP1_DONE,','EXP1_TX_DONE,','EXP4_','===== END STATS ====='))]
    return a

stage0={k:{**evidence(v['bundle']),'reused':v['reused']} for k,v in json.loads((R/'observations.json').read_text()).items()}
network={k:evidence(v['bundle']) for k,v in json.loads((R/'rotation_observations.json').read_text()).items()}
Q=Path(json.loads((R/'cable_recheck_root.json').read_text())['root'])
latest=evidence(Q/'capture1')
restoration={side:json.loads((R/f'restore_{side}.json').read_text()) for side in ['local','remote']}
preservation={}
for side in ['local','remote']:
    pre=json.loads((R/f'preflight_{side}.json').read_text());post=json.loads((R/f'postflight_{side}.json').read_text())
    preservation[side]={'git_unchanged':pre['git']==post['git'],'probes_unchanged':sorted(pre['probes'])==sorted(post['probes']),
                        'capture_processes':post['capture_processes'],'preflight':pre,'postflight':post}
    assert preservation[side]['git_unchanged'] and preservation[side]['probes_unchanged'] and not post['capture_processes']
    assert restoration[side]['all_original_roles_halted'] and not restoration[side]['extra_rf_run_started']

selection={'scope':'best observed settings in fixed block2, before cable-report recheck; not a global or paper-qualified optimum',
    'paper_qualified':False,'full_grid_complete':False,'current_context_PAC8_validated_after_cable_report':False,'pacs':{}}
for pac in [8,4]:
    rows=[a for a in network.values() if a['conditions']['sensors']==6 and a['conditions']['rx_pac']==pac]
    points={a['conditions']['lead_us']:a for a in rows}
    best=min(rows,key=lambda a:(a['worst_node_per_percent'],a['aggregate']['per_percent'],a['conditions']['lead_us']))
    eligible=[]
    for lead in points:
        ns=[lead-1,lead,lead+1]
        if all(n in points for n in ns):
            v=[points[n]['worst_node_per_percent'] for n in ns]
            if max(v)<1:eligible.append((max(v),sum(v),points[lead]['worst_node_per_percent'],lead))
    selection['pacs'][str(pac)]={'best_observed_lead_us':best['conditions']['lead_us'],
        'best_observed_worst_per_percent':best['worst_node_per_percent'],'best_observed_case':best['case_id'],
        'robust_three_point_candidate_us':min(eligible)[3] if eligible else None,
        'measured_leads_us':sorted(points),'all_node_pass_observed':any(a['verdict']=='PASS' for a in rows)}
selection['PAC4_requested_recheck']={'lead_us':27,'worst_per_percent':latest['worst_node_per_percent'],'verdict':latest['verdict'],'bundle':str(Q/'capture1')}
save(R/'refined_network_candidates.json',selection)

result={'scope':'Stage0 quick screen, selected rotations and lead refinement; paused after cable report and one requested PAC4 recheck',
    'stage0_new_runs':sum(not a['reused'] for a in stage0.values()),'stage0_reused_runs':sum(a['reused'] for a in stage0.values()),
    'exp4_network_runs':len(network),'cable_recheck_runs':1,'stage0':stage0,'network':network,'cable_recheck':latest,
    'selection':selection,'restoration':restoration,'preservation':preservation,
    'hub_external_power_confirmed':True,'firmware_C_modified':False,'commit_or_push_performed':False,
    'cable_uncertainty':'Affected board and onset unknown. Prior results preserved, no arbitrary exclusion, recheck context separate and never pooled.',
    'optimization_incomplete_after_user_steering':True}
save(R/'RESULTS.json',result)
save(R/'image_inventory.json',{k:a['images'] for k,a in {**stage0,**network,'cable_recheck':latest}.items()})

lines=['# NLOS 6.9m — lead 탐색·회전·전선 보고 후 재측정','',
 'PAC8은 현재 회전 배정에서 **25µs 최악0.55%, 26·27µs는 6대 모두0%**였다. PAC4의 이전 최저는27µs/N7 1.00%였으며, 사용자가 전선 가림을 알린 뒤 같은 조건을1회 재측정하자 **N7 6.8%, 나머지0%**였다. 각 TX PER<1% 기준으로 PAC4는 둘 다 실패다. 전선이 가렸던 보드와 시작 시점은 불명이며, 이전 자료를 임의로 제외하거나 새 결과와 합산하지 않았다.','',
 f"실행 수: Stage0 신규{result['stage0_new_runs']}회+기존{result['stage0_reused_runs']}회 재사용, Exp4 {len(network)}회, 요청한 별도 재측정1회. 마지막 요청에 따라 나머지 탐색은 중단했다. 진행 중이던 PAC8/30µs 캡처는 정상 종료 후 보존했다. 원래 배정/25µs의 추가 RF 반복은 하지 않았다.",'',
 '## 현재 상태와 연결','',
 '로컬 INIT1050270933, SSH `s-macbook-air`, 원격 TX6대 모두 정상. 허브 외부 전원 확인 상태를 유지했다. 로컬·원격 preflight/postflight에 실제 serial·USB 허브 트리·프로세스·Git을 보존했다. 원본 Git4곳의 branch/HEAD/dirty/diff hash는 전후 동일하다. 펌웨어 C 수정·commit·push 없음.','',
 '마지막에는 **원래 물리 역할의 PAC8/25µs/S6/13슬롯 HEX**로7대를 복원하고 readback 및 halt를 검증했다. 복원 후 RF는 추가하지 않았다. 잔여 캡처 프로세스 없음.','',
 '## 조건과 회전 배정','',
 'Exp4: M32, G250, SB/SP3000/2500µs, SF10ms, 1000SF,13슬롯 `2345672345673`, 슬롯별 bounded delayed-RX, 기존 SPI 최적화. S2 대표만2슬롯 `23`. RX lead는 **예상 프리앰블 시작보다 앞서 켜는 추가 여유**다. `RX_EARLY=PREAMBLE+SFD+lead`, 전체 창은 `97+lead`µs이며, 남겨 받는 프리앰블 길이를 뜻하지 않는다. TX6대의 역할별 HEX는 sweep 전체에서 동일하며 RX PAC/lead만 바뀐다.','',
 '| 물리 역할 | serial | 이번 block2 논리 역할 | 원래 역할로 복원 |','|---|---|---|---|']
sample=next(a for a in network.values() if a['conditions']['sensors']==6)
for j in sample['images']:lines.append(f"| {j['physical_role']} | {j['serial']} | {'INIT' if j['logical_node']==1 else 'N'+str(j['logical_node'])} | {j['physical_role']} |")
lines+=['','## Exp4 결과 — 각 물리 노드 PER(%)','','| PAC | leadµs | 슬롯/TX | N2 | N3 | N4 | N5 | N6 | N7 | 전체 PER | 판정 |','|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---|']
for a in network.values():
    p=a['conditions'];n={x['physical_role']:x for x in a['nodes_by_serial'].values()}
    cells=['—' if role not in n else f"{n[role]['per_percent']:.3f}" for role in ['N2','N3','N4','N5','N6','N7']]
    lines.append(f"| {p['rx_pac']} | {p['lead_us']} | {len(p['slot_owners'])}/{p['sensors']} | "+' | '.join(cells)+f" | {a['aggregate']['per_percent']:.4f} | {a['verdict']} |")
lines+=['','![측정한 lead와 최악 노드 PER](network_lead_per.png)','',
 '아까 원래 배정의 PAC8/25µs에서 N6 0.50%, N7 0.70%였던 결과와 모순되지 않는다. 현재 block2에서도 같은25µs로 통과했고, 논리 역할별7개 HEX 모두 아까와 일치한다. 즉 lead를 바꾼 실패를 이전25µs 조건의 재실패로 해석하면 안 된다. 역할 회전과 시간 변동까지 단일 원인으로 배제한 것은 아니다. [HEX 대조](original_vs_rotated_25_hex_comparison.json).','',
 '## 수집·오류·해시','',
 '모든 완료 실행은 정확한 probe 집합, TX READY→RX, 종료 마커·metadata·raw hash, 실행 후 flash readback을 검증했다. S2의 비참여4TX와 Stage0의 비참여5TX 정지를 전후 확인했다. 수집 PASS와 PER PASS를 분리했고 수신0인 Stage0는 실패 전이 자료로 보존했다.','',
 '| PAC/lead | 시작–종료 KST | RX timeout·오류 원문 |','|---|---|---|']
for a in network.values():
    p=a['conditions'];t=a['execution_kst'];lines.append(f"| {p['rx_pac']}/{p['lead_us']} S{p['sensors']} | {t['started_at']}–{t['finished_at']} | {'; '.join(a['rx_error_summary'])} |")
lines+=['', 'TX별 offered/RX/PER, beacon/missed, attempts/success/late/END와 slot별 수신, RXFSL/PHR/CRC/SFD/FWTO/PTO, TDMA 및 RDB mismatch/incomplete/resync/overrun, SPI/수집 상태 원문은 [RESULTS.json](RESULTS.json)에 전부 포함했다. [실제 serial·역할별 HEX/ELF SHA256](image_inventory.json).','',
 '## Stage0와 선정 한계','',
 'Stage0는 물리N4→INIT 단일 링크, M32, G500, tail0,2000SF였다. 단일 링크에서 고른20µs를6TX에 그대로 적용했을 때 실패한 것이 이번 중요한 발견이다. Stage0의 좋은 값을 전체 네트워크 최적값으로 취급하지 않는다. Stage0 PAC8/25의 직전 자료는 해시와 설치 context를 확인해 재사용했다.','',
 '| leadµs | PAC4 PER% | PAC8 PER% |','|---:|---:|---:|']
for lead in sorted({a['conditions']['lead_us'] for a in stage0.values()}):
    row=[]
    for pac in [4,8]:
        a=stage0.get(f'stage0_m32_pac{pac}_l{lead}');row.append('미측정' if a is None else f"{a['worst_node_per_percent']:.2f}"+(' (재사용)' if a['reused'] else ''))
    lines.append(f'| {lead} | '+ ' | '.join(row)+' |')
lines+=['', 'PAC8/26µs는 주변25/26/27µs의 최악 노드 PER이0.55/0/0%인 관측 후보다. 전선 보고 후 현재 상태에서 PAC8을 다시 검증한 것은 아니다. PAC4는27µs의1.00%와 재측정6.8% 모두 strict <1%에 미달한다. 추가 정수 lead29/31 등은 마지막 사용자 요청으로 실행하지 않았다. 전역 최적점·논문용 반복 동결·모든 회전·차량 성능 완료를 주장하지 않는다. [후보 근거](refined_network_candidates.json).','',
 '현재 자료는 lead와 수신 시작 시점에 민감한 저손실 구간을 지지한다. continuous RX와의 A/B가 아니므로 과거 manual-rearm 구조나 금속·전선 하나를 단일 원인으로 확정하지 않는다. RXFSL 중심 오류와 FWTO 중심 오류가 조건별로 다르며, 현재 측정은 이전 S3/lead15 burst 펌웨어와도 다르다.','',
 'PAC4/25 Stage0 준비 중의 첫8-worker 빌드 실패는 RF 전에 멈췄다. 로그를 보존하고 소스 수정 없이1-worker build-only로 복구한 뒤 RF1회만 수행했다. [빌드 기록](build_recovery.json).','',
 f"[전선 보고 후 별도 재측정 보고서](../{Q.name}/RESULTS.md)",'']
(R/'RESULTS.md').write_text('\n'.join(lines))

a=latest;p=a['conditions'];t=a['execution_kst'];prior=network['paper_exp4_m32_pac4_l27_k13_s6_b02'];g=a['aggregate']
q=['# PAC4/27µs — 전선 보고 후 요청한 재측정1회','',
 '**N7 PER6.8%, 나머지5대0%로 목표 실패다.** 같은 조건의 직전 N7 PER1.00%보다5.8%p 높았다. 송신은13000/13000 성공했다.','',
 'M32/PAC4, lead27µs, G250, SB/SP3000/2500µs,6TX/13슬롯,1000SF, 슬롯별 delayed-RX. 같은 block2 배정·같은7개 HEX를 사용했다.','',
 f"측정 시각(KST): {t['started_at']}–{t['finished_at']}. 전체 RX {g['rx']}/{g['offered']}, PER {g['per_percent']:.4f}%.",'',
 '| 물리 역할 / serial | 논리 역할 | offered | TX attempt/success | RX | PER | beacon/누락 | TX late/END |',
 '|---|---|---:|---:|---:|---:|---:|---:|']
for serial,n in a['nodes_by_serial'].items():q.append(f"| {n['physical_role']}/{serial} | N{n['logical_node']} | {n['offered']} | {n['tx_attempts']}/{n['tx_success']} | {n['rx']} | {n['per_percent']:.2f}% | {n['beacons']}/{n['beacon_missed']} | {n['delayed_tx_late']}/{n['end_marker']} |")
q+=['','```text']+[l for l in a['raw_counters_by_role']['init'] if l.startswith(('RX timeouts=','TDMA validation:','EXP4_DOUBLE_BUFFER_CSV,','EXP4_DEFERRED_CSV,','EXP4_SPI_CSV,'))]+['```','',
 '수집 timeout 없음. RX/TX 종료·metadata·이미지 해시 및7개 보드 flash readback 통과. SSH·허브 외부 전원 조건 유지. 원래 역할의 PAC8/25µs HEX로 복원하고 모두 halt했다. 복원 후 RF는 실행하지 않았다.','',
 '전선이 가렸던 노드와 시작 시점을 사용자가 특정하지 못했다. 이전 결과를 임의로 제외하지 않고 현재 결과와 별도 보존한다. 같은 설정에서1.00→6.8%로 달라졌으므로 PAC4/27µs를 안정적인 최적값으로 확정할 수 없다. 전선 제거 효과, 시간 변화, 다른 채널 변화를 이1회로 분리할 수 없다. 이전의 PAC8/26·27µs 모두0% 결과도 현재 전선 상태의 재검증으로 취급하지 않는다.','',
 '- [동일7개 HEX·serial·논리 역할 대조](exact_image_and_assignment_comparison.json)',
 '- [전후 개별 결과](comparison.json)',
 '- [원문과 수집/flash 상태](capture1/results/)',
 f'- [전체 탐색·보드 복원·Git 확인](../{R.name}/RESULTS.md)','']
(Q/'RESULTS.md').write_text('\n'.join(q))
save(Q/'FINAL_EVIDENCE.json',{'measurement':a,'restoration':restoration,'preservation':preservation,
    'prior_case':prior,'physical_context_uncertainty':result['cable_uncertainty']})
print(json.dumps({'main_report':str(R/'RESULTS.md'),'recheck_report':str(Q/'RESULTS.md'),
    'new_rf_runs':result['stage0_new_runs']+len(network)+1,'current_recheck_per':a['worst_node_per_percent']},ensure_ascii=False,indent=2))
